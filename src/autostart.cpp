#include "autostart.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winreg.h>

static const char *RUN_KEY_PATH = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const char *RUN_VALUE_NAME = "HDDSynth";

bool IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type;
    LONG rc = RegQueryValueExA(hKey, RUN_VALUE_NAME, NULL, &type, NULL, NULL);
    RegCloseKey(hKey);

    return rc == ERROR_SUCCESS && type == REG_SZ;
}

bool SetAutoStartEnabled(bool enable) {
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey,
                          NULL) != ERROR_SUCCESS) {
        return false;
    }

    LONG rc;
    if (enable) {
        char exePath[MAX_PATH];
        DWORD len = GetModuleFileNameA(NULL, exePath, sizeof(exePath));
        if (len == 0 || len >= sizeof(exePath)) {
            RegCloseKey(hKey);
            return false;
        }

        // Quoted since the path may contain spaces (e.g. "C:\Program
        // Files\HDD Synth\hddsynth.exe") -- the Run key's value is run
        // as a raw command line, not treated as a pre-split argv[0].
        char quotedPath[MAX_PATH + 2];
        wsprintfA(quotedPath, "\"%s\"", exePath);

        rc = RegSetValueExA(hKey, RUN_VALUE_NAME, 0, REG_SZ, (const BYTE *)quotedPath,
                             (DWORD)(lstrlenA(quotedPath) + 1));
    } else {
        rc = RegDeleteValueA(hKey, RUN_VALUE_NAME);
        if (rc == ERROR_FILE_NOT_FOUND) {
            rc = ERROR_SUCCESS; // already disabled is not a failure
        }
    }

    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}
