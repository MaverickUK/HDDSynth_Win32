// Tray shell: hidden message-only-style window, tray icon, right-click
// Exit menu. Exposes SetTrayActive() so other subsystems (once added) can
// flip the icon between gray (idle) and green (activity) without knowing
// anything about window messages.
//
// See spike_main.cpp for why WIN32_LEAN_AND_MEAN is required here.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "resource.h"
#include "audio.h"
#include "diskmon.h"

#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_EXIT 1001

static NOTIFYICONDATAA g_nid;
static HWND g_hwnd;
static HICON g_iconGray;
static HICON g_iconGreen;
static BOOL g_active = FALSE;

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

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TRAYICON:
            if (lp == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU menu = CreatePopupMenu();
                AppendMenuA(menu, MF_STRING, ID_TRAY_EXIT, "Exit");
                SetForegroundWindow(hwnd);
                TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(menu);
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == ID_TRAY_EXIT) {
                DestroyWindow(hwnd);
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

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    HWND hwnd = CreateTrayShell(hInst);

    // Expects samples\ copied next to the exe. Long idle loop per user
    // feedback -- the short hdd_idle.wav loop was too short/repetitive.
    InitAudio(hwnd, "samples\\hdd_spinup.wav", "samples\\hdd_idle_long.wav",
              "samples\\hdd_access.wav");
    StartDiskActivityMonitor(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
