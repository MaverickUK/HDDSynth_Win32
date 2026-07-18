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
}

static void ApplySettingsLive(const Settings *s) {
    SaveSettings(s);
    SetAudioVolume(s->volume);
    SetAudioBalance(s->balance);
    SetAudioMinPlaybackMs(s->minPlaybackMs);
    SetDiskActivityThreshold(s->activityThresholdBytes);
    SetAudioBufferMs(s->audioBufferMs);
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
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                Settings *s = (Settings *)GetWindowLongPtrA(hDlg, GWLP_USERDATA);
                ReadControlsIntoSettings(hDlg, s);
                ApplySettingsLive(s);
                EndDialog(hDlg, IDOK);
                return TRUE;
            } else if (LOWORD(wp) == IDC_APPLY_BUTTON) {
                Settings *s = (Settings *)GetWindowLongPtrA(hDlg, GWLP_USERDATA);
                ReadControlsIntoSettings(hDlg, s);
                ApplySettingsLive(s);
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
