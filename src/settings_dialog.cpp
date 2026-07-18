// Settings dialog: two trackbar sliders (volume, balance) plus two edit
// fields for the less frequently touched tunables (min playback time,
// activity threshold). Trackbar is a common control (comctl32.dll,
// present on Win95+) -- InitCommonControls() must be called once before
// any dialog containing one is created (done in tray.cpp's WinMain).
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

            SetDlgItemInt(hDlg, IDC_MINPLAY_EDIT, s->minPlaybackMs, FALSE);
            SetDlgItemInt(hDlg, IDC_THRESHOLD_EDIT, s->activityThresholdBytes, FALSE);
            return TRUE;
        }
        case WM_HSCROLL: {
            HWND hCtl = (HWND)lp;
            HWND hVol = GetDlgItem(hDlg, IDC_VOLUME_SLIDER);
            HWND hBal = GetDlgItem(hDlg, IDC_BALANCE_SLIDER);
            if (hCtl == hVol) {
                UpdateVolumeLabel(hDlg, (int)SendMessageA(hVol, TBM_GETPOS, 0, 0));
            } else if (hCtl == hBal) {
                UpdateBalanceLabel(hDlg, (int)SendMessageA(hBal, TBM_GETPOS, 0, 0));
            }
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                Settings *s = (Settings *)GetWindowLongPtrA(hDlg, GWLP_USERDATA);
                s->volume = (int)SendMessageA(GetDlgItem(hDlg, IDC_VOLUME_SLIDER), TBM_GETPOS, 0, 0);
                s->balance = (int)SendMessageA(GetDlgItem(hDlg, IDC_BALANCE_SLIDER), TBM_GETPOS, 0, 0);

                BOOL ok;
                int minPlay = GetDlgItemInt(hDlg, IDC_MINPLAY_EDIT, &ok, FALSE);
                if (ok) {
                    s->minPlaybackMs = minPlay;
                }
                int threshold = GetDlgItemInt(hDlg, IDC_THRESHOLD_EDIT, &ok, FALSE);
                if (ok) {
                    s->activityThresholdBytes = threshold;
                }

                EndDialog(hDlg, IDOK);
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
    if (result != IDOK) {
        return false;
    }

    SaveSettings(s);
    SetAudioVolume(s->volume);
    SetAudioBalance(s->balance);
    SetAudioMinPlaybackMs(s->minPlaybackMs);
    SetDiskActivityThreshold(s->activityThresholdBytes);
    return true;
}
