// Disk activity monitor -- one implementation covering both OS families,
// dispatching at runtime (see diskmon.h) rather than at build time. The
// two backends remain as different from each other as they always were;
// only the selection moved from link time to a GetVersionExA check made
// once in StartDiskActivityMonitor.
//
// Windows 9x/ME: HKEY_DYN_DATA\PerfStats -- the same real-time performance
// data System Monitor (sysmon.exe) reads (there's no modern IoCounters-style
// API on this OS). Per Microsoft KB Q174631, you "enable" a counter by
// querying its object\counter name under PerfStats\StartStat, then
// repeatedly read the current value from PerfStats\StatData using the same
// name.
//
// HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\PerfStats\Enum lists
// "VFAT" as the object covering 32-bit file system activity; BReadsSec/
// BWritesSec are its byte-count counters, confirmed against a real Windows
// 98 box as subkeys (not values) of PerfStats\Enum\VFAT -- no spaces, no
// "/Second" suffix. Despite the "Sec" in the name, these are NOT
// pre-computed rates: raw StatData reads showed them monotonically
// increasing on every single poll, i.e. they're cumulative totals (bytes
// since boot or since StartStat was called). So "current value is nonzero"
// is true from the very first read onward and useless as an activity
// signal -- what actually indicates activity is the *delta* between
// consecutive polls, thresholded so ordinary background I/O doesn't
// register as "activity" (see g_activityThresholdBytes).
//
// Windows 2000/XP+: PDH (Performance Data Helper, pdh.dll -- shipped with
// every NT-family Windows since NT4) exposes
// "\PhysicalDisk(_Total)\Disk Bytes/sec", and PDH computes that rate itself
// between successive PdhCollectQueryData calls -- unlike Win9x's
// HKEY_DYN_DATA counters, there's no cumulative-counter/manual-delta trap
// to fall into here. "_Total" aggregates across all physical disks,
// matching the same system-wide "any drive" detection scope as the Win9x
// path. pdh.dll is loaded with LoadLibraryA/GetProcAddress rather than
// linked as a static import -- a static import would make the whole
// process refuse to start on Win9x, where pdh.dll doesn't exist.
//
// Known limitation (NT path): PdhAddCounterA takes the *localized* counter
// path -- "PhysicalDisk"/"Disk Bytes/sec" are the English names, and this
// will fail to resolve on a non-English Windows install. The locale-
// independent fix is looking counters up by their numeric index (via
// HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Perflib)
// rather than by name; not implemented here since there's no NT-family
// hardware available in this environment to verify any of this against
// beyond "it builds cleanly" (unlike the Win9x path, verified against real
// Windows 98 hardware at every step).
#include "diskmon.h"
#include <winreg.h>
#include <process.h>
#include <pdh.h>
#include <pdhmsg.h>

#define POLL_INTERVAL_MS 150
// Once activity is seen, stay "active" for this long after the last hit
// so brief gaps between disk operations don't flicker the icon/audio.
// This adds directly to how long the effect lingers after real activity
// stops, so kept fairly tight -- was 400ms, trimmed after testing found
// the combination of this plus audio buffering made release noticeably
// laggy (see audio.cpp's BUFFER_SAMPLES, the much bigger contributor).
#define ACTIVITY_HANGOVER_MS 250
#define MAX_COUNTERS 8

static bool g_isNT = false;

static HANDLE g_thread;
static volatile LONG g_stopRequested = 0;
static HWND g_hwnd;
// Win9x backend interprets this as bytes moved per POLL_INTERVAL_MS poll;
// the NT/PDH backend's counter already reports a rate, so there it's
// reinterpreted as bytes/sec instead. Same setting/UI field either way
// (see settings.h) -- only the units it's compared against differ per
// backend, as they always have.
static volatile LONG g_activityThresholdBytes = 2048;

// ---- Windows 9x/ME backend (HKEY_DYN_DATA\PerfStats) ----

struct CounterCandidate {
    const char *object;
    const char *counter;
};

static const CounterCandidate CANDIDATES[] = {
    {"VFAT", "BReadsSec"},
    {"VFAT", "BWritesSec"},
};
#define NUM_CANDIDATES (sizeof(CANDIDATES) / sizeof(CANDIDATES[0]))

static char g_confirmedNames[MAX_COUNTERS][128];
static int g_numConfirmed = 0;
static DWORD g_prevValue[MAX_COUNTERS];
static bool g_havePrevValue[MAX_COUNTERS];

static void BuildCounterName(char *out, size_t outSize, const char *obj, const char *counter) {
    wsprintfA(out, "%s\\%s", obj, counter);
    out[outSize - 1] = '\0';
}

static bool TryEnableCounter(const char *valueName) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_DYN_DATA, "PerfStats\\StartStat", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    unsigned char buf[8];
    DWORD cbData = sizeof(buf);
    DWORD type;
    LONG rc = RegQueryValueExA(hKey, valueName, NULL, &type, buf, &cbData);
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

static void DisableCounter(const char *valueName) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_DYN_DATA, "PerfStats\\StopStat", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return;
    }
    unsigned char buf[8];
    DWORD cbData = sizeof(buf);
    DWORD type;
    RegQueryValueExA(hKey, valueName, NULL, &type, buf, &cbData);
    RegCloseKey(hKey);
}

// These counters are cumulative (see file header comment), so a single
// read is meaningless on its own -- only the delta between two reads
// indicates whether anything happened in between. Returns the raw
// current DWORD value; callers compare against their own stored
// previous value.
static bool ReadCounterValue(const char *valueName, DWORD *outValue) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_DYN_DATA, "PerfStats\\StatData", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    unsigned char buf[8];
    DWORD cbData = sizeof(buf);
    DWORD type;
    LONG rc = RegQueryValueExA(hKey, valueName, NULL, &type, buf, &cbData);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS || cbData < 4) {
        return false;
    }
    *outValue = (DWORD)buf[0] | ((DWORD)buf[1] << 8) | ((DWORD)buf[2] << 16) | ((DWORD)buf[3] << 24);
    return true;
}

static void ResolveCounters9x() {
    g_numConfirmed = 0;
    for (size_t i = 0; i < NUM_CANDIDATES && g_numConfirmed < MAX_COUNTERS; i++) {
        char name[128];
        BuildCounterName(name, sizeof(name), CANDIDATES[i].object, CANDIDATES[i].counter);
        if (TryEnableCounter(name)) {
            lstrcpynA(g_confirmedNames[g_numConfirmed], name, sizeof(g_confirmedNames[0]));
            g_havePrevValue[g_numConfirmed] = false;
            g_numConfirmed++;
        }
    }
}

static void ShutdownBackend9x() {
    for (int i = 0; i < g_numConfirmed; i++) {
        DisableCounter(g_confirmedNames[i]);
    }
}

// Returns TRUE if this poll saw activity exceeding the configured threshold.
static BOOL PollOnce9x() {
    BOOL sawActivity = FALSE;
    for (int i = 0; i < g_numConfirmed; i++) {
        DWORD value;
        if (!ReadCounterValue(g_confirmedNames[i], &value)) {
            continue;
        }
        // Unsigned subtraction handles a single 32-bit wraparound
        // correctly; the first read for a counter has nothing to compare
        // against yet, so it never claims activity on its own.
        LONG threshold = InterlockedExchangeAdd(&g_activityThresholdBytes, 0);
        if (g_havePrevValue[i] && (LONG)(value - g_prevValue[i]) > threshold) {
            sawActivity = TRUE;
        }
        g_prevValue[i] = value;
        g_havePrevValue[i] = true;
    }
    return sawActivity;
}

// ---- Windows 2000/XP+ backend (PDH, loaded dynamically) ----

typedef PDH_STATUS (WINAPI *PdhOpenQueryA_t)(LPCSTR, DWORD_PTR, PDH_HQUERY *);
typedef PDH_STATUS (WINAPI *PdhAddCounterA_t)(PDH_HQUERY, LPCSTR, DWORD_PTR, PDH_HCOUNTER *);
typedef PDH_STATUS (WINAPI *PdhCollectQueryData_t)(PDH_HQUERY);
typedef PDH_STATUS (WINAPI *PdhGetFormattedCounterValue_t)(PDH_HCOUNTER, DWORD, LPDWORD, PPDH_FMT_COUNTERVALUE);
typedef PDH_STATUS (WINAPI *PdhCloseQuery_t)(PDH_HQUERY);

static HMODULE g_pdhModule = NULL;
static PdhOpenQueryA_t g_PdhOpenQueryA;
static PdhAddCounterA_t g_PdhAddCounterA;
static PdhCollectQueryData_t g_PdhCollectQueryData;
static PdhGetFormattedCounterValue_t g_PdhGetFormattedCounterValue;
static PdhCloseQuery_t g_PdhCloseQuery;

static PDH_HQUERY g_query = NULL;
static PDH_HCOUNTER g_counter = NULL;

// Loads pdh.dll and resolves the handful of entry points this file needs.
// Returns false (leaving g_pdhModule NULL) if the DLL or any export is
// missing -- the only path that should hit on Win9x, where pdh.dll doesn't
// exist at all.
static bool LoadPdh() {
    g_pdhModule = LoadLibraryA("pdh.dll");
    if (!g_pdhModule) {
        return false;
    }
    g_PdhOpenQueryA = (PdhOpenQueryA_t)GetProcAddress(g_pdhModule, "PdhOpenQueryA");
    g_PdhAddCounterA = (PdhAddCounterA_t)GetProcAddress(g_pdhModule, "PdhAddCounterA");
    g_PdhCollectQueryData = (PdhCollectQueryData_t)GetProcAddress(g_pdhModule, "PdhCollectQueryData");
    g_PdhGetFormattedCounterValue = (PdhGetFormattedCounterValue_t)GetProcAddress(g_pdhModule, "PdhGetFormattedCounterValue");
    g_PdhCloseQuery = (PdhCloseQuery_t)GetProcAddress(g_pdhModule, "PdhCloseQuery");
    if (!g_PdhOpenQueryA || !g_PdhAddCounterA || !g_PdhCollectQueryData ||
        !g_PdhGetFormattedCounterValue || !g_PdhCloseQuery) {
        FreeLibrary(g_pdhModule);
        g_pdhModule = NULL;
        return false;
    }
    return true;
}

static bool SetupBackendNT() {
    if (!LoadPdh()) {
        return false;
    }
    if (g_PdhOpenQueryA(NULL, 0, &g_query) != ERROR_SUCCESS) {
        FreeLibrary(g_pdhModule);
        g_pdhModule = NULL;
        return false;
    }
    if (g_PdhAddCounterA(g_query, "\\PhysicalDisk(_Total)\\Disk Bytes/sec", 0, &g_counter) != ERROR_SUCCESS) {
        g_PdhCloseQuery(g_query);
        g_query = NULL;
        FreeLibrary(g_pdhModule);
        g_pdhModule = NULL;
        return false;
    }
    // A differential counter like this needs two samples before it can
    // report a real rate; this first collect just seeds it, its data
    // isn't meaningful and is deliberately not read.
    g_PdhCollectQueryData(g_query);
    return true;
}

static void ShutdownBackendNT() {
    if (g_query) {
        g_PdhCloseQuery(g_query);
        g_query = NULL;
        g_counter = NULL;
    }
    if (g_pdhModule) {
        FreeLibrary(g_pdhModule);
        g_pdhModule = NULL;
    }
}

static BOOL PollOnceNT() {
    BOOL sawActivity = FALSE;
    if (g_PdhCollectQueryData(g_query) == ERROR_SUCCESS) {
        PDH_FMT_COUNTERVALUE value;
        DWORD type;
        if (g_PdhGetFormattedCounterValue(g_counter, PDH_FMT_LONG, &type, &value) == ERROR_SUCCESS &&
            (value.CStatus == PDH_CSTATUS_VALID_DATA || value.CStatus == PDH_CSTATUS_NEW_DATA)) {
            LONG threshold = InterlockedExchangeAdd(&g_activityThresholdBytes, 0);
            if (value.longValue > threshold) {
                sawActivity = TRUE;
            }
        }
    }
    return sawActivity;
}

// ---- Shared poll loop ----

static unsigned __stdcall PollThreadProc(void *) {
    if (g_isNT) {
        // Backend already set up (query seeded) before this thread started.
    } else {
        ResolveCounters9x();
    }

    BOOL currentlyActive = FALSE;
    DWORD lastActivityTick = 0;

    while (!InterlockedExchangeAdd(&g_stopRequested, 0)) {
        Sleep(POLL_INTERVAL_MS);

        BOOL sawActivity = g_isNT ? PollOnceNT() : PollOnce9x();

        DWORD now = GetTickCount();
        if (sawActivity) {
            lastActivityTick = now;
        }

        BOOL shouldBeActive = sawActivity || (now - lastActivityTick) < ACTIVITY_HANGOVER_MS;
        if (shouldBeActive != currentlyActive) {
            currentlyActive = shouldBeActive;
            PostMessageA(g_hwnd, WM_DISKACTIVITY, currentlyActive, 0);
        }
    }

    if (!g_isNT) {
        ShutdownBackend9x();
    }
    return 0;
}

bool StartDiskActivityMonitor(HWND hwnd, int activityThresholdBytes) {
    g_hwnd = hwnd;
    g_stopRequested = 0;
    g_activityThresholdBytes = activityThresholdBytes;

    OSVERSIONINFOA osvi;
    ZeroMemory(&osvi, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    g_isNT = GetVersionExA(&osvi) && osvi.dwPlatformId == VER_PLATFORM_WIN32_NT;

    if (g_isNT && !SetupBackendNT()) {
        return false;
    }

    // _beginthreadex, not CreateThread: any thread running alongside CRT
    // usage elsewhere in the process needs the CRT's own per-thread init
    // (errno, stdio buffers, ...), or risks silent corruption/hangs rather
    // than a clean crash -- this bit us once already during development.
    unsigned tid;
    g_thread = (HANDLE)_beginthreadex(NULL, 0, PollThreadProc, NULL, 0, &tid);
    if (!g_thread && g_isNT) {
        ShutdownBackendNT();
    }
    return g_thread != NULL;
}

void StopDiskActivityMonitor() {
    if (g_thread) {
        InterlockedExchange(&g_stopRequested, 1);
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    if (g_isNT) {
        ShutdownBackendNT();
    }
}

void SetDiskActivityThreshold(int thresholdBytes) {
    InterlockedExchange(&g_activityThresholdBytes, thresholdBytes);
}
