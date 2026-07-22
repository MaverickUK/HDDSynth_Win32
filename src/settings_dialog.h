#ifndef HDDSYNTH_SETTINGS_DIALOG_H
#define HDDSYNTH_SETTINGS_DIALOG_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "settings.h"

// Shows the modal Settings dialog pre-filled from *s. The dialog also has
// an Apply button: clicking it updates *s, persists it (SaveSettings),
// and applies the changes live (per-layer audio volumes, min-playback,
// buffer depth, disk monitor threshold) without closing the dialog --
// same effect as OK, just without ending it. Returns true if closed via OK,
// false if closed via Cancel; either way, *s and live state reflect
// whatever was last Applied (or OK'd) during this call, same as any
// other Win32 dialog with an Apply button -- Cancel doesn't roll that
// back, only whatever's been changed in the controls since.
bool ShowSettingsDialog(HWND parent, HINSTANCE hInst, Settings *s);

#endif
