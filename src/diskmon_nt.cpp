// Windows 2000/XP+ implementation of diskmon.h -- see diskmon.cpp for
// the Windows 9x/ME counterpart. This one is substantially simpler as a
// direct consequence of NT having a real, documented performance-counter
// API where Win9x has neither: PDH (Performance Data Helper, pdh.dll --
// shipped with every NT-family Windows since NT4) exposes
// "\PhysicalDisk(_Total)\Disk Bytes/sec", and PDH computes that rate
// itself between successive PdhCollectQueryData calls -- unlike Win9x's
// HKEY_DYN_DATA counters, there's no cumulative-counter/manual-delta trap
// to fall into here. "_Total" aggregates across all physical disks,
// matching the same system-wide "any drive" detection scope the Win9x
// build uses.
//
// Known limitation: PdhAddCounterA takes the *localized* counter path --
// "PhysicalDisk"/"Disk Bytes/sec" are the English names, and this will
// fail to resolve on a non-English Windows install. The locale-
// independent fix is looking counters up by their numeric index (via
// HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Perflib)
// rather than by name; not implemented here since there's no NT-family
// hardware available to verify any of this against in the first place
// (unlike the Win9x build, which was verified against real Windows 98
// hardware at every step -- this file is unverified beyond "it compiles
// and links cleanly").
#include "diskmon.h"
#include <pdh.h>
#include <pdhmsg.h>
#include <process.h>

#define POLL_INTERVAL_MS 150
// Once activity is seen, stay "active" for this long after the last hit
// so brief gaps between disk operations don't flicker the icon/audio.
#define ACTIVITY_HANGOVER_MS 250

static PDH_HQUERY g_query = NULL;
static PDH_HCOUNTER g_counter = NULL;
// Despite the setting's name/UI label ("bytes per poll") carried over
// from the Win9x build, this counter already reports bytes/sec directly
// -- so here the same numeric field is interpreted as a bytes/sec
// threshold instead of a per-150ms-poll one.
static volatile LONG g_activityThresholdBytesPerSec = 2048;

static HANDLE g_thread;
static volatile LONG g_stopRequested = 0;
static HWND g_hwnd;

static unsigned __stdcall PollThreadProc(void *) {
    // A differential counter like this needs two samples before it can
    // report a real rate; this first collect just seeds it; its data
    // isn't meaningful and is deliberately not read.
    PdhCollectQueryData(g_query);

    BOOL currentlyActive = FALSE;
    DWORD lastActivityTick = 0;

    while (!InterlockedExchangeAdd(&g_stopRequested, 0)) {
        Sleep(POLL_INTERVAL_MS);

        BOOL sawActivity = FALSE;
        if (PdhCollectQueryData(g_query) == ERROR_SUCCESS) {
            PDH_FMT_COUNTERVALUE value;
            DWORD type;
            if (PdhGetFormattedCounterValue(g_counter, PDH_FMT_LONG, &type, &value) == ERROR_SUCCESS &&
                (value.CStatus == PDH_CSTATUS_VALID_DATA || value.CStatus == PDH_CSTATUS_NEW_DATA)) {
                LONG threshold = InterlockedExchangeAdd(&g_activityThresholdBytesPerSec, 0);
                if (value.longValue > threshold) {
                    sawActivity = TRUE;
                }
            }
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
    }
    return 0;
}

bool StartDiskActivityMonitor(HWND hwnd, int activityThresholdBytes) {
    g_hwnd = hwnd;
    g_activityThresholdBytesPerSec = activityThresholdBytes;

    if (PdhOpenQueryA(NULL, 0, &g_query) != ERROR_SUCCESS) {
        return false;
    }
    if (PdhAddCounterA(g_query, "\\PhysicalDisk(_Total)\\Disk Bytes/sec", 0, &g_counter) != ERROR_SUCCESS) {
        PdhCloseQuery(g_query);
        g_query = NULL;
        return false;
    }

    g_stopRequested = 0;
    // _beginthreadex, not CreateThread -- see diskmon.cpp/CLAUDE.md for
    // why: this thread calls into the CRT-adjacent PDH machinery and
    // should get the CRT's own per-thread init like every other thread
    // in this codebase.
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
    if (g_query) {
        PdhCloseQuery(g_query);
        g_query = NULL;
        g_counter = NULL;
    }
}

void SetDiskActivityThreshold(int thresholdBytes) {
    InterlockedExchange(&g_activityThresholdBytesPerSec, thresholdBytes);
}
