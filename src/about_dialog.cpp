// Classic Win9x-style About box: system dialog frame, MS Sans Serif,
// app icon, and a blue underlined "hyperlink" static control that opens
// the GitHub page via ShellExecute -- an authentic period touch (real
// hyperlink controls didn't exist until much later Windows versions).
#include "about_dialog.h"
#include "resource.h"
#include "version.h"
#include <shellapi.h>

static HFONT g_linkFont = NULL;

static BOOL CALLBACK AboutDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            char buf[64];
            wsprintfA(buf, "Version %s", HDDSYNTH_VERSION_STRING);
            SetDlgItemTextA(hDlg, IDC_ABOUT_VERSION, buf);
            SetDlgItemTextA(hDlg, IDC_ABOUT_LINK, HDDSYNTH_GITHUB_URL);

            HWND hLink = GetDlgItem(hDlg, IDC_ABOUT_LINK);
            HFONT baseFont = (HFONT)SendMessageA(hLink, WM_GETFONT, 0, 0);
            LOGFONTA lf;
            if (baseFont && GetObjectA(baseFont, sizeof(lf), &lf)) {
                lf.lfUnderline = TRUE;
                g_linkFont = CreateFontIndirectA(&lf);
                if (g_linkFont) {
                    SendMessageA(hLink, WM_SETFONT, (WPARAM)g_linkFont, TRUE);
                }
            }
            return TRUE;
        }
        case WM_CTLCOLORSTATIC: {
            HWND hCtl = (HWND)lp;
            if (GetDlgCtrlID(hCtl) == IDC_ABOUT_LINK) {
                HDC hdc = (HDC)wp;
                SetTextColor(hdc, RGB(0, 0, 255));
                SetBkMode(hdc, TRANSPARENT);
                return (BOOL)(INT_PTR)GetSysColorBrush(COLOR_3DFACE);
            }
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
                EndDialog(hDlg, 0);
                return TRUE;
            }
            if (LOWORD(wp) == IDC_ABOUT_LINK && HIWORD(wp) == STN_CLICKED) {
                ShellExecuteA(hDlg, "open", HDDSYNTH_GITHUB_URL, NULL, NULL, SW_SHOWNORMAL);
                return TRUE;
            }
            break;
        case WM_DESTROY:
            if (g_linkFont) {
                DeleteObject(g_linkFont);
                g_linkFont = NULL;
            }
            break;
    }
    return FALSE;
}

void ShowAboutDialog(HWND parent, HINSTANCE hInst) {
    DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_ABOUT), parent, AboutDlgProc, 0);
}
