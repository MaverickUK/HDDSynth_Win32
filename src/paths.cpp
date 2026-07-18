#include "paths.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void GetExeDir(char *buf, size_t bufSize) {
    DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)bufSize);
    if (len == 0 || len >= bufSize) {
        buf[0] = '\0';
        return;
    }
    for (DWORD i = len; i > 0; i--) {
        if (buf[i - 1] == '\\') {
            buf[i] = '\0';
            return;
        }
    }
    buf[0] = '\0';
}

void BuildExePath(char *out, size_t outSize, const char *relative) {
    char dir[MAX_PATH];
    GetExeDir(dir, sizeof(dir));
    wsprintfA(out, "%s%s", dir, relative);
    out[outSize - 1] = '\0';
}
