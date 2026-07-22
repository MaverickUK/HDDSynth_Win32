#ifndef HDDSYNTH_DISKMON_H
#define HDDSYNTH_DISKMON_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Custom message posted to hwnd whenever the detected activity state
// changes: wParam is TRUE/FALSE (now active/idle), lParam unused. Posted
// (not sent) so the poll thread never blocks on the UI thread.
#define WM_DISKACTIVITY (WM_APP + 2)

// diskmon.cpp implements both OS families' disk-activity detection behind
// this one interface, picking between them at runtime (GetVersionExA):
// Windows 9x/ME uses HKEY_DYN_DATA\PerfStats (undocumented, cumulative
// counters needing manual delta/threshold tracking -- see its file header
// for the full story); Windows 2000/XP+ uses PDH (documented, already
// rate-based). Everything else in the app (tray.cpp, mixer.cpp, ...) only
// depends on this header, not on which backend actually runs.

// Starts a background thread polling for system-wide disk activity.
// Returns true if the thread was created; this does NOT guarantee
// detection is actually working yet on this specific machine -- see each
// implementation's file header for what can still go wrong per-OS.
bool StartDiskActivityMonitor(HWND hwnd, int activityThresholdBytes);

void StopDiskActivityMonitor();

// Threshold a counter's movement must exceed per poll to count as real
// activity rather than background noise. Units/semantics are documented
// per implementation (see each diskmon_*.cpp) since the underlying
// counters aren't measuring quite the same thing. Safe to call any time;
// takes effect on the monitor thread's next poll.
void SetDiskActivityThreshold(int thresholdBytes);

#endif
