// Tray shell: hidden message-only-style window, tray icon, right-click
// menu (Sample pack submenu, Settings, About, Exit). Exposes
// SetTrayActive() so other subsystems can flip the icon between gray
// (idle) and green (activity) without knowing anything about window
// messages.
//
// See spike_main.cpp for why WIN32_LEAN_AND_MEAN is required here.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include "resource.h"
#include "audio.h"
#include "diskmon.h"
#include "settings.h"
#include "settings_dialog.h"
#include "about_dialog.h"
#include "samplepack.h"

#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_SETTINGS 1002
#define ID_TRAY_ABOUT 1003
#define ID_TRAY_SAMPLE_BASE 2000

static NOTIFYICONDATAA g_nid;
static HWND g_hwnd;
static HINSTANCE g_hInst;
static HICON g_iconGray;
static HICON g_iconGreen;
static BOOL g_active = FALSE;
static Settings g_settings;

// Rebuilt each time the context menu is opened (WM_TRAYICON) and consulted
// again when the resulting WM_COMMAND arrives, so dynamically assigned
// sample-pack menu IDs can be mapped back to pack names.
static char g_menuPackNames[SAMPLEPACK_MAX_PACKS][SAMPLEPACK_NAME_LEN];
static int g_menuPackCount = 0;

static void UpdateTrayIcon(HICON icon) {
    g_nid.hIcon = icon;
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}

void SetTrayActive(BOOL active) {
    if (active == g_active) {
        return;
    }
    g_active = active;
    UpdateTrayIcon(active ? g_iconGreen : g_iconGray);
}

static void SwitchToSamplePack(const char *packName) {
    char spinupPath[MAX_PATH], idlePath[MAX_PATH], accessPath[MAX_PATH];
    BuildSamplePackPaths(packName, spinupPath, idlePath, accessPath, MAX_PATH);
    if (SwitchAudioSamplePack(spinupPath, idlePath, accessPath)) {
        lstrcpynA(g_settings.samplePack, packName, sizeof(g_settings.samplePack));
        SaveSettings(&g_settings);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TRAYICON:
            if (lp == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);

                g_menuPackCount = ScanSamplePacks(g_menuPackNames, SAMPLEPACK_MAX_PACKS);

                HMENU menu = CreatePopupMenu();
                if (g_menuPackCount > 0) {
                    HMENU sampleMenu = CreatePopupMenu();
                    for (int i = 0; i < g_menuPackCount; i++) {
                        UINT flags = MF_STRING;
                        if (lstrcmpiA(g_menuPackNames[i], g_settings.samplePack) == 0) {
                            flags |= MF_CHECKED;
                        }
                        AppendMenuA(sampleMenu, flags, ID_TRAY_SAMPLE_BASE + i, g_menuPackNames[i]);
                    }
                    AppendMenuA(menu, MF_POPUP, (UINT_PTR)sampleMenu, "Sample");
                }
                AppendMenuA(menu, MF_STRING, ID_TRAY_SETTINGS, "Settings...");
                AppendMenuA(menu, MF_STRING, ID_TRAY_ABOUT, "About...");
                AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
                AppendMenuA(menu, MF_STRING, ID_TRAY_EXIT, "Exit");

                SetForegroundWindow(hwnd);
                TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(menu); // recursively destroys the Sample submenu too
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == ID_TRAY_EXIT) {
                DestroyWindow(hwnd);
            } else if (LOWORD(wp) == ID_TRAY_SETTINGS) {
                ShowSettingsDialog(hwnd, g_hInst, &g_settings);
            } else if (LOWORD(wp) == ID_TRAY_ABOUT) {
                ShowAboutDialog(hwnd, g_hInst);
            } else if (LOWORD(wp) >= ID_TRAY_SAMPLE_BASE &&
                       LOWORD(wp) < ID_TRAY_SAMPLE_BASE + g_menuPackCount) {
                SwitchToSamplePack(g_menuPackNames[LOWORD(wp) - ID_TRAY_SAMPLE_BASE]);
            }
            return 0;
        case WM_DISKACTIVITY:
            SetTrayActive((BOOL)wp);
            SetAudioAccessActive((BOOL)wp);
            return 0;
        case WM_DESTROY:
            StopDiskActivityMonitor();
            ShutdownAudio();
            Shell_NotifyIconA(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

HWND CreateTrayShell(HINSTANCE hInst) {
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "HDDSynthTrayWnd";
    RegisterClassA(&wc);

    g_hwnd = CreateWindowA("HDDSynthTrayWnd", "HDDSynth", 0,
                            0, 0, 0, 0, NULL, NULL, hInst, NULL);

    g_iconGray = (HICON)LoadImageA(hInst, MAKEINTRESOURCEA(IDI_GRAY), IMAGE_ICON,
                                    16, 16, LR_DEFAULTCOLOR);
    g_iconGreen = (HICON)LoadImageA(hInst, MAKEINTRESOURCEA(IDI_GREEN), IMAGE_ICON,
                                     16, 16, LR_DEFAULTCOLOR);

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATAA);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_iconGray;
    lstrcpynA(g_nid.szTip, "HDDSynth", sizeof(g_nid.szTip));
    Shell_NotifyIconA(NIM_ADD, &g_nid);

    return g_hwnd;
}

#ifndef HDDSYNTH_TARGET_NT
static BOOL CALLBACK WrongOsDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
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
#endif

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    g_hInst = hInst;

#ifndef HDDSYNTH_TARGET_NT
    // This build targets Windows 95/98/ME's real-mode disk-activity polling
    // (see diskmon.cpp); it doesn't work on the NT kernel (2000/XP+), which
    // needs hddsynth-nt.exe's PDH-based diskmon_nt.cpp instead. A plain
    // DialogBoxParamA (already linked in for the About/Settings dialogs) is
    // used here rather than MessageBoxA -- pulling in that one extra USER32
    // import alone was enough to push the .rsrc section past a linker bug
    // in this toolchain ("(.rsrc) is too large"), even though its own
    // content never changed size.
    OSVERSIONINFOA osvi;
    ZeroMemory(&osvi, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    if (GetVersionExA(&osvi) && osvi.dwPlatformId == VER_PLATFORM_WIN32_NT) {
        DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_WRONGOS), NULL, WrongOsDlgProc, 0);
        return 1;
    }
#endif

    InitCommonControls(); // needed before creating the Settings dialog's trackbars

    LoadSettings(&g_settings);

    // Validate the remembered sample pack still exists (folder could have
    // been removed, or this could be a first run with nothing saved yet);
    // fall back to the first pack found, or "original" if none are.
    char packNames[SAMPLEPACK_MAX_PACKS][SAMPLEPACK_NAME_LEN];
    int packCount = ScanSamplePacks(packNames, SAMPLEPACK_MAX_PACKS);
    bool found = false;
    for (int i = 0; i < packCount; i++) {
        if (lstrcmpiA(packNames[i], g_settings.samplePack) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        lstrcpynA(g_settings.samplePack, packCount > 0 ? packNames[0] : "original",
                  sizeof(g_settings.samplePack));
    }

    HWND hwnd = CreateTrayShell(hInst);

    char spinupPath[MAX_PATH], idlePath[MAX_PATH], accessPath[MAX_PATH];
    BuildSamplePackPaths(g_settings.samplePack, spinupPath, idlePath, accessPath, MAX_PATH);
    InitAudio(hwnd, spinupPath, idlePath, accessPath,
              g_settings.volume, g_settings.balance, g_settings.minPlaybackMs,
              g_settings.audioBufferMs);
    StartDiskActivityMonitor(hwnd, g_settings.activityThresholdBytes);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
