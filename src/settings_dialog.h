#ifndef HDDSYNTH_SETTINGS_DIALOG_H
#define HDDSYNTH_SETTINGS_DIALOG_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "settings.h"

// Shows the modal Settings dialog pre-filled from *s. On OK: updates *s,
// persists it (SaveSettings), applies the changes live (audio volume/
// balance/min-playback, disk monitor threshold), and returns true. On
// Cancel, *s and all live state are left untouched and this returns false.
bool ShowSettingsDialog(HWND parent, HINSTANCE hInst, Settings *s);

#endif
