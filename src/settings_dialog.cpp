// Settings dialog: three trackbar sliders (volume, balance, audio
// buffering) plus two edit fields for the less frequently touched
// tunables (min playback time, activity threshold), plus an Apply button
// alongside OK/Cancel so changes can be tried live without closing the
// dialog -- handy for A/B-testing settings against something like a large
// background file copy. Trackbar is a common control (comctl32.dll,
// present on Win95+) -- InitCommonControls() must be called once before
// any dialog containing one is created (done in tray.cpp's WinMain).
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
#include <commctrl.h>

static void UpdateVolumeLabel(HWND hDlg, int pos) {
    char buf[16];
    wsprintfA(buf, "%d%%", pos);
    SetDlgItemTextA(hDlg, IDC_VOLUME_LABEL, buf);
}

static void UpdateBalanceLabel(HWND hDlg, int pos) {
    char buf[16];
    wsprintfA(buf, "%d", pos);
    SetDlgItemTextA(hDlg, IDC_BALANCE_LABEL, buf);
}

static void UpdateBufferLabel(HWND hDlg, int pos) {
    char buf[16];
    wsprintfA(buf, "%dms", pos);
    SetDlgItemTextA(hDlg, IDC_BUFFER_LABEL, buf);
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

static const char HELP_TEXT[] =
    "Volume\r\n"
    "Overall loudness of everything the app plays.\r\n"
    "\r\n"
    "Balance\r\n"
    "Crossfades between the idle hum and the access sound. All the way "
    "to \"Idle\" mutes the access sound; all the way to \"Activity\" "
    "mutes the idle hum.\r\n"
    "\r\n"
    "Audio Buffering\r\n"
    "How much audio is queued ahead of time. Lower values react faster "
    "when disk activity starts or stops, but are more likely to glitch "
    "if the system stalls briefly (e.g. during heavy disk I/O). Higher "
    "values are safer but feel slightly laggier. Use the Diagnostics "
    "readout below to check whether a setting is actually safe on this "
    "machine.\r\n"
    "\r\n"
    "Minimum Access Playback (ms)\r\n"
    "Once triggered, the access sound keeps playing for at least this "
    "long, even if the real disk activity was shorter -- prevents a very "
    "brief access from being clipped into an abrupt near-silent blip. "
    "Set too high and the access sound can noticeably outlast the real "
    "activity.\r\n"
    "\r\n"
    "Activity Threshold (bytes/poll)\r\n"
    "How many bytes must move in one polling interval before it counts "
    "as \"activity\". Too low and background housekeeping I/O can "
    "trigger the access sound constantly; too high and real activity "
    "might not be noticed.\r\n"
    "\r\n"
    "Audio API\r\n"
    "Auto (Recommended) tries DirectSound first and automatically falls "
    "back to WaveOut/MME if DirectSound isn't usable on this machine -- "
    "safe for most users. WaveOut/MME is the most compatible option, "
    "supported by every version of Windows, but has more inherent "
    "latency. DirectSound can offer lower latency using hardware-"
    "assisted buffering where available.\r\n"
    "\r\n"
    "Diagnostics\r\n"
    "Latency shows the real measured queued audio delay right now. "
    "Glitches counts confirmed audible gaps since the audio engine was "
    "last (re)started. Both exist to help find a buffering setting "
    "that's actually safe on this machine, rather than guessing.";

static BOOL CALLBACK SettingsHelpDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            SetDlgItemTextA(hDlg, IDC_HELP_TEXT, HELP_TEXT);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
                EndDialog(hDlg, 0);
                return TRUE;
            }
            break;
    }
    return FALSE;
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
        UpdateBufferLabel(hDlg, minMs);
    }
}

static void ReadControlsIntoSettings(HWND hDlg, Settings *s) {
    s->volume = (int)SendMessageA(GetDlgItem(hDlg, IDC_VOLUME_SLIDER), TBM_GETPOS, 0, 0);
    s->balance = (int)SendMessageA(GetDlgItem(hDlg, IDC_BALANCE_SLIDER), TBM_GETPOS, 0, 0);
    s->audioBufferMs = (int)SendMessageA(GetDlgItem(hDlg, IDC_BUFFER_SLIDER), TBM_GETPOS, 0, 0);

    BOOL ok;
    int minPlay = GetDlgItemInt(hDlg, IDC_MINPLAY_EDIT, &ok, FALSE);
    if (ok) {
        s->minPlaybackMs = minPlay;
    }
    int threshold = GetDlgItemInt(hDlg, IDC_THRESHOLD_EDIT, &ok, FALSE);
    if (ok) {
        s->activityThresholdBytes = threshold;
    }

    s->audioApi = (int)SendMessageA(GetDlgItem(hDlg, IDC_AUDIOAPI_COMBO), CB_GETCURSEL, 0, 0);
}

// hDlg is needed here (not just a plain Settings* apply) so the buffer
// slider's range can be refreshed immediately after a live API switch --
// see UpdateBufferSliderRange's comment for why that timing matters.
static void ApplySettingsLive(HWND hDlg, const Settings *s) {
    SaveSettings(s);
    SetAudioVolume(s->volume);
    SetAudioBalance(s->balance);
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

            HWND hVol = GetDlgItem(hDlg, IDC_VOLUME_SLIDER);
            SendMessageA(hVol, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
            SendMessageA(hVol, TBM_SETPOS, TRUE, s->volume);
            UpdateVolumeLabel(hDlg, s->volume);

            HWND hBal = GetDlgItem(hDlg, IDC_BALANCE_SLIDER);
            SendMessageA(hBal, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
            SendMessageA(hBal, TBM_SETPOS, TRUE, s->balance);
            UpdateBalanceLabel(hDlg, s->balance);

            HWND hBuf = GetDlgItem(hDlg, IDC_BUFFER_SLIDER);
            SendMessageA(hBuf, TBM_SETRANGE, TRUE, MAKELONG(MIN_AUDIO_BUFFER_MS, MAX_AUDIO_BUFFER_MS));
            SendMessageA(hBuf, TBM_SETLINESIZE, 0, 50);
            SendMessageA(hBuf, TBM_SETPOS, TRUE, s->audioBufferMs);
            UpdateBufferLabel(hDlg, s->audioBufferMs);

            SetDlgItemInt(hDlg, IDC_MINPLAY_EDIT, s->minPlaybackMs, FALSE);
            SetDlgItemInt(hDlg, IDC_THRESHOLD_EDIT, s->activityThresholdBytes, FALSE);

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
            HWND hVol = GetDlgItem(hDlg, IDC_VOLUME_SLIDER);
            HWND hBal = GetDlgItem(hDlg, IDC_BALANCE_SLIDER);
            HWND hBuf = GetDlgItem(hDlg, IDC_BUFFER_SLIDER);
            if (hCtl == hVol) {
                UpdateVolumeLabel(hDlg, (int)SendMessageA(hVol, TBM_GETPOS, 0, 0));
            } else if (hCtl == hBal) {
                UpdateBalanceLabel(hDlg, (int)SendMessageA(hBal, TBM_GETPOS, 0, 0));
            } else if (hCtl == hBuf) {
                UpdateBufferLabel(hDlg, (int)SendMessageA(hBuf, TBM_GETPOS, 0, 0));
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
                ReadControlsIntoSettings(hDlg, s);
                ApplySettingsLive(hDlg, s);
                EndDialog(hDlg, IDOK);
                return TRUE;
            } else if (LOWORD(wp) == IDC_APPLY_BUTTON) {
                Settings *s = (Settings *)GetWindowLongPtrA(hDlg, GWLP_USERDATA);
                ReadControlsIntoSettings(hDlg, s);
                ApplySettingsLive(hDlg, s);
                return TRUE;
            } else if (LOWORD(wp) == IDC_AUDIOAPI_COMBO && HIWORD(wp) == CBN_SELCHANGE) {
                UpdateBufferSliderRange(hDlg);
                return TRUE;
            } else if (LOWORD(wp) == IDC_HELP_BUTTON) {
                HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrA(hDlg, GWLP_HINSTANCE);
                DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_SETTINGSHELP), hDlg,
                                 SettingsHelpDlgProc, 0);
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
