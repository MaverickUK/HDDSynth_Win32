// Toolchain validation spike: minimal Win9x tray-icon app.
// Confirms the cross-compiled binary launches and shows a tray icon
// before any real feature work is built on top of this toolchain.
//
// WIN32_LEAN_AND_MEAN keeps windows.h from pulling in the OLE/COM headers,
// which otherwise drag in libstdc++'s <cstdlib> wrapper and its unconditional
// `using ::quick_exit;` -- a UCRT-only symbol not present in msvcrt.dll, and
// so not declared by mingw's headers when targeting the classic Win9x CRT.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_EXIT 1001

static NOTIFYICONDATAA g_nid;
static HWND g_hwnd;

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
        case WM_DESTROY:
            Shell_NotifyIconA(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "HDDSynthSpikeWnd";
    RegisterClassA(&wc);

    g_hwnd = CreateWindowA("HDDSynthSpikeWnd", "HDDSynth Spike", 0,
                            0, 0, 0, 0, NULL, NULL, hInst, NULL);

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATAA);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconA(NULL, MAKEINTRESOURCEA(32512)); // IDI_APPLICATION
    lstrcpynA(g_nid.szTip, "HDDSynth (spike)", sizeof(g_nid.szTip));
    Shell_NotifyIconA(NIM_ADD, &g_nid);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
