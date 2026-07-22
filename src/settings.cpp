// Settings persistence via a classic Win9x-era INI file (hddsynth.ini
// next to the exe), read/written with GetPrivateProfileString/
// WritePrivateProfileString rather than the registry -- keeps the app
// self-contained the same way samples/ already is, no install/uninstall
// registry cleanup to think about.
#include "settings.h"
#include "paths.h"
#include "audio.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DEFAULT_IDLE_VOLUME 100
#define DEFAULT_ACCESS_VOLUME 100
#define DEFAULT_SPINUP_VOLUME 100
#define DEFAULT_MIN_PLAYBACK_MS 200
#define DEFAULT_ACTIVITY_THRESHOLD_BYTES 2048
#define DEFAULT_AUDIO_BUFFER_MS 750
#define DEFAULT_AUDIO_API AUDIO_API_AUTO
#define DEFAULT_SAMPLE_PACK "original"

static void GetIniPath(char *out, size_t outSize) {
    BuildExePath(out, outSize, "hddsynth.ini");
}

void LoadSettings(Settings *out) {
    char iniPath[MAX_PATH];
    GetIniPath(iniPath, sizeof(iniPath));

    out->idleVolume = GetPrivateProfileIntA("Audio", "IdleVolume", DEFAULT_IDLE_VOLUME, iniPath);
    out->accessVolume = GetPrivateProfileIntA("Audio", "AccessVolume", DEFAULT_ACCESS_VOLUME, iniPath);
    out->spinupVolume = GetPrivateProfileIntA("Audio", "SpinupVolume", DEFAULT_SPINUP_VOLUME, iniPath);
    out->minPlaybackMs = GetPrivateProfileIntA("Audio", "MinPlaybackMs", DEFAULT_MIN_PLAYBACK_MS, iniPath);
    out->activityThresholdBytes = GetPrivateProfileIntA("Audio", "ActivityThresholdBytes",
                                                         DEFAULT_ACTIVITY_THRESHOLD_BYTES, iniPath);
    out->audioBufferMs = GetPrivateProfileIntA("Audio", "AudioBufferMs",
                                                 DEFAULT_AUDIO_BUFFER_MS, iniPath);
    out->audioApi = GetPrivateProfileIntA("Audio", "AudioApi", DEFAULT_AUDIO_API, iniPath);
    GetPrivateProfileStringA("Audio", "SamplePack", DEFAULT_SAMPLE_PACK,
                              out->samplePack, sizeof(out->samplePack), iniPath);

    if (out->idleVolume < 0) out->idleVolume = 0;
    if (out->idleVolume > 100) out->idleVolume = 100;
    if (out->accessVolume < 0) out->accessVolume = 0;
    if (out->accessVolume > 100) out->accessVolume = 100;
    if (out->spinupVolume < 0) out->spinupVolume = 0;
    if (out->spinupVolume > 100) out->spinupVolume = 100;
    if (out->minPlaybackMs < 0) out->minPlaybackMs = 0;
    if (out->activityThresholdBytes < 0) out->activityThresholdBytes = 0;
    if (out->audioBufferMs < MIN_AUDIO_BUFFER_MS) out->audioBufferMs = MIN_AUDIO_BUFFER_MS;
    if (out->audioBufferMs > MAX_AUDIO_BUFFER_MS) out->audioBufferMs = MAX_AUDIO_BUFFER_MS;
    if (out->audioApi < AUDIO_API_AUTO || out->audioApi > AUDIO_API_DSOUND) {
        out->audioApi = DEFAULT_AUDIO_API;
    }
}

void SaveSettings(const Settings *s) {
    char iniPath[MAX_PATH];
    GetIniPath(iniPath, sizeof(iniPath));

    char buf[32];
    wsprintfA(buf, "%d", s->idleVolume);
    WritePrivateProfileStringA("Audio", "IdleVolume", buf, iniPath);

    wsprintfA(buf, "%d", s->accessVolume);
    WritePrivateProfileStringA("Audio", "AccessVolume", buf, iniPath);

    wsprintfA(buf, "%d", s->spinupVolume);
    WritePrivateProfileStringA("Audio", "SpinupVolume", buf, iniPath);

    wsprintfA(buf, "%d", s->minPlaybackMs);
    WritePrivateProfileStringA("Audio", "MinPlaybackMs", buf, iniPath);

    wsprintfA(buf, "%d", s->activityThresholdBytes);
    WritePrivateProfileStringA("Audio", "ActivityThresholdBytes", buf, iniPath);

    wsprintfA(buf, "%d", s->audioBufferMs);
    WritePrivateProfileStringA("Audio", "AudioBufferMs", buf, iniPath);

    wsprintfA(buf, "%d", s->audioApi);
    WritePrivateProfileStringA("Audio", "AudioApi", buf, iniPath);

    WritePrivateProfileStringA("Audio", "SamplePack", s->samplePack, iniPath);
}
