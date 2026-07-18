#ifndef HDDSYNTH_DISKMON_H
#define HDDSYNTH_DISKMON_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Custom message posted to hwnd whenever the detected activity state
// changes: wParam is TRUE/FALSE (now active/idle), lParam unused. Posted
// (not sent) so the poll thread never blocks on the UI thread.
#define WM_DISKACTIVITY (WM_APP + 2)

// Starts a background thread polling HKEY_DYN_DATA\PerfStats for
// system-wide disk activity (see diskmon.cpp for why the exact counter
// names are still provisional). Returns true if the thread was created;
// this does NOT mean a usable counter was found yet -- that's decided at
// poll time, since HKEY_DYN_DATA only exists once the OS is actually
// Windows 9x, not on this dev machine.
bool StartDiskActivityMonitor(HWND hwnd);

void StopDiskActivityMonitor();

#endif
