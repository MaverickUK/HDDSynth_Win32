// Settings persistence via a classic Win9x-era INI file (hddsynth.ini
// next to the exe), read/written with GetPrivateProfileString/
// WritePrivateProfileString rather than the registry -- keeps the app
// self-contained the same way samples/ already is, no install/uninstall
// registry cleanup to think about.
#include "settings.h"
#include "paths.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DEFAULT_VOLUME 100
#define DEFAULT_BALANCE 50
#define DEFAULT_MIN_PLAYBACK_MS 200
#define DEFAULT_ACTIVITY_THRESHOLD_BYTES 2048
#define DEFAULT_SAMPLE_PACK "original"

static void GetIniPath(char *out, size_t outSize) {
    BuildExePath(out, outSize, "hddsynth.ini");
}

void LoadSettings(Settings *out) {
    char iniPath[MAX_PATH];
    GetIniPath(iniPath, sizeof(iniPath));

    out->volume = GetPrivateProfileIntA("Audio", "Volume", DEFAULT_VOLUME, iniPath);
    out->balance = GetPrivateProfileIntA("Audio", "Balance", DEFAULT_BALANCE, iniPath);
    out->minPlaybackMs = GetPrivateProfileIntA("Audio", "MinPlaybackMs", DEFAULT_MIN_PLAYBACK_MS, iniPath);
    out->activityThresholdBytes = GetPrivateProfileIntA("Audio", "ActivityThresholdBytes",
                                                         DEFAULT_ACTIVITY_THRESHOLD_BYTES, iniPath);
    GetPrivateProfileStringA("Audio", "SamplePack", DEFAULT_SAMPLE_PACK,
                              out->samplePack, sizeof(out->samplePack), iniPath);

    if (out->volume < 0) out->volume = 0;
    if (out->volume > 100) out->volume = 100;
    if (out->balance < 0) out->balance = 0;
    if (out->balance > 100) out->balance = 100;
    if (out->minPlaybackMs < 0) out->minPlaybackMs = 0;
    if (out->activityThresholdBytes < 0) out->activityThresholdBytes = 0;
}

void SaveSettings(const Settings *s) {
    char iniPath[MAX_PATH];
    GetIniPath(iniPath, sizeof(iniPath));

    char buf[32];
    wsprintfA(buf, "%d", s->volume);
    WritePrivateProfileStringA("Audio", "Volume", buf, iniPath);

    wsprintfA(buf, "%d", s->balance);
    WritePrivateProfileStringA("Audio", "Balance", buf, iniPath);

    wsprintfA(buf, "%d", s->minPlaybackMs);
    WritePrivateProfileStringA("Audio", "MinPlaybackMs", buf, iniPath);

    wsprintfA(buf, "%d", s->activityThresholdBytes);
    WritePrivateProfileStringA("Audio", "ActivityThresholdBytes", buf, iniPath);

    WritePrivateProfileStringA("Audio", "SamplePack", s->samplePack, iniPath);
}
