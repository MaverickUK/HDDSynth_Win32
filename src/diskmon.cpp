// Disk activity detection via HKEY_DYN_DATA\PerfStats -- the same
// real-time performance data System Monitor (sysmon.exe) reads on
// Windows 95/98/ME (there's no modern IoCounters-style API on this OS).
// Per Microsoft KB Q174631, you "enable" a counter by querying its
// object\counter name under PerfStats\StartStat, then repeatedly read
// the current value from PerfStats\StatData using the same name.
//
// HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\PerfStats\Enum
// lists "VFAT" as the object covering 32-bit file system activity;
// BReadsSec/BWritesSec are its byte-count counters, confirmed against a
// real Windows 98 box as subkeys (not values) of PerfStats\Enum\VFAT --
// no spaces, no "/Second" suffix. Despite the "Sec" in the name, these
// are NOT pre-computed rates: raw StatData reads showed them
// monotonically increasing on every single poll, i.e. they're cumulative
// totals (bytes since boot or since StartStat was called). So "current
// value is nonzero" is true from the very first read onward and useless
// as an activity signal -- what actually indicates activity is the
// *delta* between consecutive polls, thresholded so ordinary background
// I/O doesn't register as "activity" (see ACTIVITY_BYTE_THRESHOLD).
#include "diskmon.h"
#include <winreg.h>
#include <process.h>

#define POLL_INTERVAL_MS 150
// Once activity is seen, stay "active" for this long after the last hit
// so brief gaps between disk operations don't flicker the icon/audio.
#define ACTIVITY_HANGOVER_MS 400
#define MAX_COUNTERS 8

struct CounterCandidate {
    const char *object;
    const char *counter;
};

static const CounterCandidate CANDIDATES[] = {
    {"VFAT", "BReadsSec"},
    {"VFAT", "BWritesSec"},
};
#define NUM_CANDIDATES (sizeof(CANDIDATES) / sizeof(CANDIDATES[0]))

// Bytes per POLL_INTERVAL_MS a counter must have moved by to count as
// real activity, not background noise. Default (2KB/150ms, ~13KB/s
// sustained) is comfortably below "copying a file" territory, well above
// typical idle housekeeping I/O; user-configurable via Settings.
static volatile LONG g_activityThresholdBytes = 2048;

static char g_confirmedNames[MAX_COUNTERS][128];
static int g_numConfirmed = 0;
static DWORD g_prevValue[MAX_COUNTERS];
static bool g_havePrevValue[MAX_COUNTERS];

static HANDLE g_thread;
static volatile LONG g_stopRequested = 0;
static HWND g_hwnd;

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

static void ResolveCounters() {
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

static unsigned __stdcall PollThreadProc(void *) {
    ResolveCounters();

    BOOL currentlyActive = FALSE;
    DWORD lastActivityTick = 0;

    while (!InterlockedExchangeAdd(&g_stopRequested, 0)) {
        BOOL sawActivity = FALSE;
        for (int i = 0; i < g_numConfirmed; i++) {
            DWORD value;
            if (!ReadCounterValue(g_confirmedNames[i], &value)) {
                continue;
            }
            // Unsigned subtraction handles a single 32-bit wraparound
            // correctly; the first read for a counter has nothing to
            // compare against yet, so it never claims activity on its own.
            LONG threshold = InterlockedExchangeAdd(&g_activityThresholdBytes, 0);
            if (g_havePrevValue[i] && (LONG)(value - g_prevValue[i]) > threshold) {
                sawActivity = TRUE;
            }
            g_prevValue[i] = value;
            g_havePrevValue[i] = true;
        }

        DWORD now = GetTickCount();
        if (sawActivity) {
            lastActivityTick = now;
        }

        BOOL shouldBeActive = sawActivity || (now - lastActivityTick) < ACTIVITY_HANGOVER_MS;
        if (shouldBeActive != currentlyActive) {
            currentlyActive = shouldBeActive;
            PostMessageA(g_hwnd, WM_DISKACTIVITY, currentlyActive, 0);
        }

        Sleep(POLL_INTERVAL_MS);
    }

    for (int i = 0; i < g_numConfirmed; i++) {
        DisableCounter(g_confirmedNames[i]);
    }
    return 0;
}

bool StartDiskActivityMonitor(HWND hwnd, int activityThresholdBytes) {
    g_hwnd = hwnd;
    g_stopRequested = 0;
    g_activityThresholdBytes = activityThresholdBytes;
    // _beginthreadex, not CreateThread: any thread running alongside CRT
    // usage elsewhere in the process needs the CRT's own per-thread init
    // (errno, stdio buffers, ...), or risks silent corruption/hangs rather
    // than a clean crash -- this bit us once already during development.
    unsigned tid;
    g_thread = (HANDLE)_beginthreadex(NULL, 0, PollThreadProc, NULL, 0, &tid);
    return g_thread != NULL;
}

void StopDiskActivityMonitor() {
    if (g_thread) {
        InterlockedExchange(&g_stopRequested, 1);
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
}

void SetDiskActivityThreshold(int thresholdBytes) {
    InterlockedExchange(&g_activityThresholdBytes, thresholdBytes);
}
