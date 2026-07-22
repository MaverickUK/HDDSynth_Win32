#ifndef HDDSYNTH_AUTOSTART_H
#define HDDSYNTH_AUTOSTART_H

// "Run at startup" via the per-user registry Run key
// (HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run) --
// no admin rights needed, works identically on every Windows version
// this project targets, and needs no COM (unlike a Startup-folder
// shortcut, which would need IShellLink/IPersistFile -- see CLAUDE.md's
// COM/OLE header warning this project has already been bitten by once).
// Not gated behind diskmon.h's OS-family seam: the registry API involved
// doesn't differ between Win9x and NT, so this is shared code, not an
// OS-family difference.
//
// There's no separate Settings/INI field for this -- the registry value
// itself is the single source of truth, so it can't drift out of sync
// with reality if the user (or another tool, e.g. msconfig) changes it
// directly.

// True if the app is currently registered to launch at logon.
bool IsAutoStartEnabled();

// Adds or removes the registry entry. Enabling stores the currently
// running exe's full path (quoted, since it may contain spaces);
// disabling removes the entry if present (already-disabled is not a
// failure). Returns true on success.
bool SetAutoStartEnabled(bool enable);

#endif
