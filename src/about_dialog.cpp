// Classic Win9x-style About box: system dialog frame, MS Sans Serif, the
// project logo (a BITMAP resource -- classic Win9x GDI has no PNG
// support, and BITMAP resources have no alpha channel, so res/hddsynthlogo.bmp
// is a 24-bit flattened/resized conversion of the original PNG; see
// tools/ for how it was produced), and a blue underlined "hyperlink"
// static control that opens the GitHub page via ShellExecute -- an
// authentic period touch (real hyperlink controls didn't exist until
// much later Windows versions).
#include "about_dialog.h"
#include "resource.h"
#include "version.h"
#include "audio.h"
#include <shellapi.h>

static HFONT g_linkFont = NULL;
static HBITMAP g_logoBitmap = NULL;

static BOOL CALLBACK AboutDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            HINSTANCE hInst = (HINSTANCE)lp;
            g_logoBitmap = LoadBitmapA(hInst, MAKEINTRESOURCEA(IDB_LOGO));
            if (g_logoBitmap) {
                SendDlgItemMessageA(hDlg, IDC_ABOUT_LOGO, STM_SETIMAGE,
                                     IMAGE_BITMAP, (LPARAM)g_logoBitmap);
            }

            char buf[64];
            SetWindowTextA(hDlg, HDDSYNTH_APP_NAME);
            SetDlgItemTextA(hDlg, IDC_ABOUT_APPNAME, HDDSYNTH_APP_NAME);
            wsprintfA(buf, "Version %s", HDDSYNTH_VERSION_STRING);
            SetDlgItemTextA(hDlg, IDC_ABOUT_VERSION, buf);

            OSVERSIONINFOA osvi;
            ZeroMemory(&osvi, sizeof(osvi));
            osvi.dwOSVersionInfoSize = sizeof(osvi);
            bool isNT = GetVersionExA(&osvi) && osvi.dwPlatformId == VER_PLATFORM_WIN32_NT;
            SetDlgItemTextA(hDlg, IDC_ABOUT_BUILD,
                            isNT ? "Running on Windows 2000/XP+" : "Running on Windows 95/98/ME");
            char audioBuf[48];
            wsprintfA(audioBuf, "Audio: %s", GetActiveAudioBackendName());
            SetDlgItemTextA(hDlg, IDC_ABOUT_AUDIOAPI, audioBuf);
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
            if (g_logoBitmap) {
                DeleteObject(g_logoBitmap);
                g_logoBitmap = NULL;
            }
            break;
    }
    return FALSE;
}

void ShowAboutDialog(HWND parent, HINSTANCE hInst) {
    DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_ABOUT), parent, AboutDlgProc, (LPARAM)hInst);
}
