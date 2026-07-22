// Settings dialog: grouped into Playback (idle/activity/spin-up volume),
// Disk Activity (min access playback/activity threshold), Audio Engine
// (buffering/Audio API), and Diagnostics (latency/glitches) group boxes.
// Every value is a trackbar slider paired with an EDIT box (see
// SLIDER_EDITS below) so a value can be typed directly instead of only
// ever hunted for by dragging -- plus an Apply button alongside OK/Cancel
// so changes can be tried live without closing the dialog, handy for
// A/B-testing settings against something like a large background file
// copy. Trackbar is a common control (comctl32.dll, present on Win95+)
// -- InitCommonControls() must be called once before any dialog
// containing one is created (done in tray.cpp's WinMain). A Help button
// opens SettingsHelp.txt for the explanations that don't fit inline.
//
// Apply and OK do the exact same "read controls, save, apply live" work
// (ReadControlsIntoSettings + ApplySettingsLive below); the only
// difference is Apply leaves the dialog open and OK closes it. Like every
// other Win32 dialog with an Apply button, clicking it commits
// immediately -- Cancel afterward does not roll back whatever was already
// applied, only whatever's been changed in the controls since.
#include "settings_dialog.h"
#include "resource.h"
#include "audio.h"
#include "diskmon.h"
#include "paths.h"
#include <commctrl.h>
#include <shellapi.h>

// Every slider in this dialog is paired with an EDIT box showing the same
// value as plain digits (no embedded unit -- units are a separate static
// LTEXT next to the edit, see the .rc) so one generic sync routine can
// drive all of them, rather than hand-writing near-identical code six
// times over. The edit's valid range is read from the slider's own
// TBM_GETRANGEMIN/MAX rather than stored here, since the Buffer slider's
// range changes at runtime depending on the active audio backend (see
// UpdateBufferSliderRange) -- this way that's automatically respected
// without a special case.
struct SliderEditPair {
    int sliderId;
    int editId;
};

static const SliderEditPair SLIDER_EDITS[] = {
    {IDC_IDLEVOL_SLIDER, IDC_IDLEVOL_EDIT},
    {IDC_ACCESSVOL_SLIDER, IDC_ACCESSVOL_EDIT},
    {IDC_SPINUPVOL_SLIDER, IDC_SPINUPVOL_EDIT},
    {IDC_MINPLAY_SLIDER, IDC_MINPLAY_EDIT},
    {IDC_THRESHOLD_SLIDER, IDC_THRESHOLD_EDIT},
    {IDC_BUFFER_SLIDER, IDC_BUFFER_EDIT},
};
#define NUM_SLIDER_EDITS (sizeof(SLIDER_EDITS) / sizeof(SLIDER_EDITS[0]))

// Hand-rolled digit parsing, clamped to [lo, hi] -- <stdlib.h>/<cstdlib>
// (atoi, strtol, ...) are unusable anywhere in this codebase, see
// CLAUDE.md. Empty or non-digit input returns fallback unchanged rather
// than snapping to a range endpoint, so a momentarily-empty edit box
// (user selected-all and hasn't typed yet) doesn't fight them.
static int ParseIntClamped(const char *s, int lo, int hi, int fallback) {
    if (!s || !*s) {
        return fallback;
    }
    long value = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') {
            return fallback;
        }
        value = value * 10 + (*p - '0');
        if (value > hi) {
            value = hi;
            break;
        }
    }
    if (value < lo) value = lo;
    if (value > hi) value = hi;
    return (int)value;
}

static void UpdateEditFromSlider(HWND hDlg, int editId, int sliderId) {
    int pos = (int)SendMessageA(GetDlgItem(hDlg, sliderId), TBM_GETPOS, 0, 0);
    char buf[16];
    wsprintfA(buf, "%d", pos);
    SetDlgItemTextA(hDlg, editId, buf);
}

// Reads the edit box's typed text, clamps it to the paired slider's
// current range, moves the slider there, and echoes the clamped value
// back into the edit -- so garbage/out-of-range/empty input never leaves
// the two controls disagreeing with each other.
static void SyncEditToSlider(HWND hDlg, int editId, int sliderId) {
    HWND hSlider = GetDlgItem(hDlg, sliderId);
    int lo = (int)SendMessageA(hSlider, TBM_GETRANGEMIN, 0, 0);
    int hi = (int)SendMessageA(hSlider, TBM_GETRANGEMAX, 0, 0);
    int current = (int)SendMessageA(hSlider, TBM_GETPOS, 0, 0);

    char buf[16];
    GetDlgItemTextA(hDlg, editId, buf, sizeof(buf));
    int value = ParseIntClamped(buf, lo, hi, current);

    SendMessageA(hSlider, TBM_SETPOS, TRUE, value);
    wsprintfA(buf, "%d", value);
    SetDlgItemTextA(hDlg, editId, buf);
}

// Called right before reading controls into Settings (OK/Apply) so that
// whatever's currently typed -- even in a box that never lost focus,
// e.g. Enter pressed straight after typing -- is folded into its slider
// first. Without this, a value typed but not yet KILLFOCUS-synced would
// be silently discarded in favor of the slider's last dragged position.
static void SyncAllEditsToSliders(HWND hDlg) {
    for (size_t i = 0; i < NUM_SLIDER_EDITS; i++) {
        SyncEditToSlider(hDlg, SLIDER_EDITS[i].editId, SLIDER_EDITS[i].sliderId);
    }
}

#define DIAGNOSTICS_TIMER_ID 1

static void UpdateDiagnosticsLabels(HWND hDlg) {
    char buf[32];
    wsprintfA(buf, "Latency: ~%dms", GetAudioLatencyMs());
    SetDlgItemTextA(hDlg, IDC_LATENCY_LABEL, buf);
    wsprintfA(buf, "Glitches: %lu", GetAudioUnderrunCount());
    SetDlgItemTextA(hDlg, IDC_UNDERRUN_LABEL, buf);
}

// Auto mode picks a backend silently -- show which one it actually
// landed on so that choice (and any silent fallback away from
// DirectSound) is visible here too, not just in the About dialog. Only
// meaningful right after WM_INITDIALOG or a live apply (see
// ApplySettingsLive) -- NOT on every combo selection change, since
// selecting a different value in the dropdown doesn't itself change
// what's active until Applied, and showing it there would be misleading.
static void UpdateAudioApiStatusLabel(HWND hDlg) {
    char buf[48];
    wsprintfA(buf, "Currently active: %s", GetActiveAudioBackendName());
    SetDlgItemTextA(hDlg, IDC_AUDIOAPI_STATUS, buf);
}

// Opens SettingsHelp.txt (shipped next to the exe, same convention as
// hddsynth.ini/samples/ -- see paths.h) in whatever the user has
// associated with .txt, normally Notepad. A plain text file rather than
// a custom in-app dialog: simpler, and editable/searchable/printable
// with whatever the user already has, rather than a bespoke read-only
// control this codebase would otherwise have to maintain.
static void OpenSettingsHelp(HWND hDlg) {
    char helpPath[MAX_PATH];
    BuildExePath(helpPath, sizeof(helpPath), "SettingsHelp.txt");
    ShellExecuteA(hDlg, "open", helpPath, NULL, NULL, SW_SHOWNORMAL);
}

// The buffer slider's floor depends on which backend the "Audio API"
// combo would resolve to -- DirectSound's own buffering already supports
// much lower depths than waveOut ever will (see settings.h), so the
// slider shouldn't cap a DirectSound user at waveOut's more conservative
// floor, or let a waveOut user drag below the depth that's actually safe
// on that backend. Called on init, whenever the combo selection changes,
// and right after a live API switch actually takes effect (Apply/OK),
// since that's the point where "which backend is really active" can
// change out from under the slider's current range.
static void UpdateBufferSliderRange(HWND hDlg) {
    int apiSel = (int)SendMessageA(GetDlgItem(hDlg, IDC_AUDIOAPI_COMBO), CB_GETCURSEL, 0, 0);
    bool allowLow = (apiSel == AUDIO_API_DSOUND) ||
                    (apiSel == AUDIO_API_AUTO &&
                     lstrcmpA(GetActiveAudioBackendName(), "DirectSound") == 0);
    int minMs = allowLow ? MIN_AUDIO_BUFFER_MS_DSOUND : MIN_AUDIO_BUFFER_MS;

    HWND hBuf = GetDlgItem(hDlg, IDC_BUFFER_SLIDER);
    SendMessageA(hBuf, TBM_SETRANGE, TRUE, MAKELONG(minMs, MAX_AUDIO_BUFFER_MS));
    int pos = (int)SendMessageA(hBuf, TBM_GETPOS, 0, 0);
    if (pos < minMs) {
        SendMessageA(hBuf, TBM_SETPOS, TRUE, minMs);
    }
    UpdateEditFromSlider(hDlg, IDC_BUFFER_EDIT, IDC_BUFFER_SLIDER);
}

static void ReadControlsIntoSettings(HWND hDlg, Settings *s) {
    s->idleVolume = (int)SendMessageA(GetDlgItem(hDlg, IDC_IDLEVOL_SLIDER), TBM_GETPOS, 0, 0);
    s->accessVolume = (int)SendMessageA(GetDlgItem(hDlg, IDC_ACCESSVOL_SLIDER), TBM_GETPOS, 0, 0);
    s->spinupVolume = (int)SendMessageA(GetDlgItem(hDlg, IDC_SPINUPVOL_SLIDER), TBM_GETPOS, 0, 0);
    s->audioBufferMs = (int)SendMessageA(GetDlgItem(hDlg, IDC_BUFFER_SLIDER), TBM_GETPOS, 0, 0);
    s->minPlaybackMs = (int)SendMessageA(GetDlgItem(hDlg, IDC_MINPLAY_SLIDER), TBM_GETPOS, 0, 0);
    s->activityThresholdBytes =
        (int)SendMessageA(GetDlgItem(hDlg, IDC_THRESHOLD_SLIDER), TBM_GETPOS, 0, 0);
    s->audioApi = (int)SendMessageA(GetDlgItem(hDlg, IDC_AUDIOAPI_COMBO), CB_GETCURSEL, 0, 0);
}

// hDlg is needed here (not just a plain Settings* apply) so the buffer
// slider's range can be refreshed immediately after a live API switch --
// see UpdateBufferSliderRange's comment for why that timing matters.
static void ApplySettingsLive(HWND hDlg, const Settings *s) {
    SaveSettings(s);
    SetAudioIdleVolume(s->idleVolume);
    SetAudioAccessVolume(s->accessVolume);
    SetAudioSpinupVolume(s->spinupVolume);
    SetAudioMinPlaybackMs(s->minPlaybackMs);
    SetDiskActivityThreshold(s->activityThresholdBytes);
    SetAudioApi(s->audioApi);
    SetAudioBufferMs(s->audioBufferMs);
    UpdateBufferSliderRange(hDlg);
    UpdateAudioApiStatusLabel(hDlg);
}

static BOOL CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            Settings *s = (Settings *)lp;
            SetWindowLongPtrA(hDlg, GWLP_USERDATA, (LONG_PTR)s);

            HWND hIdleVol = GetDlgItem(hDlg, IDC_IDLEVOL_SLIDER);
            SendMessageA(hIdleVol, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
            SendMessageA(hIdleVol, TBM_SETPOS, TRUE, s->idleVolume);

            HWND hAccessVol = GetDlgItem(hDlg, IDC_ACCESSVOL_SLIDER);
            SendMessageA(hAccessVol, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
            SendMessageA(hAccessVol, TBM_SETPOS, TRUE, s->accessVolume);

            HWND hSpinupVol = GetDlgItem(hDlg, IDC_SPINUPVOL_SLIDER);
            SendMessageA(hSpinupVol, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
            SendMessageA(hSpinupVol, TBM_SETPOS, TRUE, s->spinupVolume);

            HWND hBuf = GetDlgItem(hDlg, IDC_BUFFER_SLIDER);
            SendMessageA(hBuf, TBM_SETRANGE, TRUE, MAKELONG(MIN_AUDIO_BUFFER_MS, MAX_AUDIO_BUFFER_MS));
            SendMessageA(hBuf, TBM_SETLINESIZE, 0, 50);
            SendMessageA(hBuf, TBM_SETPOS, TRUE, s->audioBufferMs);

            HWND hMinPlay = GetDlgItem(hDlg, IDC_MINPLAY_SLIDER);
            SendMessageA(hMinPlay, TBM_SETRANGE, TRUE, MAKELONG(MIN_MINPLAY_MS, MAX_MINPLAY_MS));
            SendMessageA(hMinPlay, TBM_SETLINESIZE, 0, 50);
            SendMessageA(hMinPlay, TBM_SETPOS, TRUE, s->minPlaybackMs);

            HWND hThreshold = GetDlgItem(hDlg, IDC_THRESHOLD_SLIDER);
            SendMessageA(hThreshold, TBM_SETRANGE, TRUE,
                         MAKELONG(MIN_ACTIVITY_THRESHOLD_BYTES, MAX_ACTIVITY_THRESHOLD_BYTES));
            SendMessageA(hThreshold, TBM_SETLINESIZE, 0, 256);
            SendMessageA(hThreshold, TBM_SETPOS, TRUE, s->activityThresholdBytes);

            for (size_t i = 0; i < NUM_SLIDER_EDITS; i++) {
                UpdateEditFromSlider(hDlg, SLIDER_EDITS[i].editId, SLIDER_EDITS[i].sliderId);
            }

            HWND hApi = GetDlgItem(hDlg, IDC_AUDIOAPI_COMBO);
            SendMessageA(hApi, CB_ADDSTRING, 0, (LPARAM)"Auto (Recommended)");
            SendMessageA(hApi, CB_ADDSTRING, 0, (LPARAM)"WaveOut / MME (Compatible)");
            SendMessageA(hApi, CB_ADDSTRING, 0, (LPARAM)"DirectSound (Lower Latency)");
            SendMessageA(hApi, CB_SETCURSEL, s->audioApi, 0);
            UpdateBufferSliderRange(hDlg);
            UpdateAudioApiStatusLabel(hDlg);

            UpdateDiagnosticsLabels(hDlg);
            SetTimer(hDlg, DIAGNOSTICS_TIMER_ID, 500, NULL);
            return TRUE;
        }
        case WM_HSCROLL: {
            HWND hCtl = (HWND)lp;
            for (size_t i = 0; i < NUM_SLIDER_EDITS; i++) {
                if (hCtl == GetDlgItem(hDlg, SLIDER_EDITS[i].sliderId)) {
                    UpdateEditFromSlider(hDlg, SLIDER_EDITS[i].editId, SLIDER_EDITS[i].sliderId);
                    break;
                }
            }
            return 0;
        }
        case WM_TIMER:
            if (wp == DIAGNOSTICS_TIMER_ID) {
                UpdateDiagnosticsLabels(hDlg);
                return 0;
            }
            break;
        case WM_DESTROY:
            KillTimer(hDlg, DIAGNOSTICS_TIMER_ID);
            break;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                Settings *s = (Settings *)GetWindowLongPtrA(hDlg, GWLP_USERDATA);
                SyncAllEditsToSliders(hDlg);
                ReadControlsIntoSettings(hDlg, s);
                ApplySettingsLive(hDlg, s);
                EndDialog(hDlg, IDOK);
                return TRUE;
            } else if (LOWORD(wp) == IDC_APPLY_BUTTON) {
                Settings *s = (Settings *)GetWindowLongPtrA(hDlg, GWLP_USERDATA);
                SyncAllEditsToSliders(hDlg);
                ReadControlsIntoSettings(hDlg, s);
                ApplySettingsLive(hDlg, s);
                return TRUE;
            } else if (LOWORD(wp) == IDC_AUDIOAPI_COMBO && HIWORD(wp) == CBN_SELCHANGE) {
                UpdateBufferSliderRange(hDlg);
                return TRUE;
            } else if (HIWORD(wp) == EN_KILLFOCUS) {
                for (size_t i = 0; i < NUM_SLIDER_EDITS; i++) {
                    if (LOWORD(wp) == (WORD)SLIDER_EDITS[i].editId) {
                        SyncEditToSlider(hDlg, SLIDER_EDITS[i].editId, SLIDER_EDITS[i].sliderId);
                        break;
                    }
                }
                return TRUE;
            } else if (LOWORD(wp) == IDC_HELP_BUTTON) {
                OpenSettingsHelp(hDlg);
                return TRUE;
            } else if (LOWORD(wp) == IDCANCEL) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

bool ShowSettingsDialog(HWND parent, HINSTANCE hInst, Settings *s) {
    INT_PTR result = DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_SETTINGS), parent,
                                       SettingsDlgProc, (LPARAM)s);
    // Settings are already saved/applied inside the dialog proc on both
    // OK and Apply -- nothing left to do here except report whether the
    // user closed it via OK (Cancel does not undo an earlier Apply, same
    // as every other Win32 dialog with an Apply button).
    return result == IDOK;
}
