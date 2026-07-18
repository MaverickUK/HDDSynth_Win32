#include "samplepack.h"
#include "paths.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int ScanSamplePacks(char names[][SAMPLEPACK_NAME_LEN], int maxPacks) {
    char pattern[MAX_PATH];
    BuildExePath(pattern, sizeof(pattern), "samples\\*");

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(pattern, &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return 0;
    }

    int count = 0;
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;
        }
        if (lstrcmpA(findData.cFileName, ".") == 0 || lstrcmpA(findData.cFileName, "..") == 0) {
            continue;
        }
        if (count < maxPacks) {
            lstrcpynA(names[count], findData.cFileName, SAMPLEPACK_NAME_LEN);
            count++;
        }
    } while (FindNextFileA(hFind, &findData) && count < maxPacks);

    FindClose(hFind);
    return count;
}

void BuildSamplePackPaths(const char *packName, char *spinupPath, char *idlePath,
                          char *accessPath, size_t pathSize) {
    char rel[MAX_PATH];

    wsprintfA(rel, "samples\\%s\\spinup.wav", packName);
    BuildExePath(spinupPath, pathSize, rel);

    wsprintfA(rel, "samples\\%s\\idle.wav", packName);
    BuildExePath(idlePath, pathSize, rel);

    wsprintfA(rel, "samples\\%s\\access.wav", packName);
    BuildExePath(accessPath, pathSize, rel);
}
