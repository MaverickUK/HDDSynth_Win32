// Backend dispatcher: decides once, at InitAudio time, whether to use
// the DirectSound backend (audio_dsound.cpp) or fall back to waveOut/MME
// (audio_waveout.cpp), then routes every subsequent call to whichever one
// won. See audio_backend.h for the internal interface both backends
// implement, and audio_dsound.cpp's header comment for why DirectSound
// detection happens entirely at runtime (LoadLibraryA), never at link
// time -- this file and the Makefile must never require dsound.dll to
// exist.
//
// Mixer ownership lives here, not in either backend: MixerInit/
// MixerShutdown run exactly once regardless of which backend is active,
// since sample loading is backend-independent (see mixer.h).
#include "audio.h"
#include "audio_backend.h"
#include "mixer.h"

enum AudioBackend { BACKEND_NONE, BACKEND_WAVEOUT, BACKEND_DSOUND };

static AudioBackend g_activeBackend = BACKEND_NONE;

// Remembered across the app's lifetime so SetAudioApi() can restart just
// the backend later without needing its caller to re-supply them.
static HWND g_hwnd;
static int g_bufferMs;
static int g_requestedAudioApi = AUDIO_API_AUTO;

static bool StartBackend(int audioApi) {
    if (audioApi != AUDIO_API_WAVEOUT && DSoundInit(g_hwnd, g_bufferMs)) {
        g_activeBackend = BACKEND_DSOUND;
        return true;
    }

    // Either WaveOut was explicitly requested, or DirectSound was
    // requested/preferred but isn't usable on this machine (no DirectX
    // installed, no working driver behind it, etc.) -- either way, fall
    // back to the backend that's always available.
    if (WaveOutInit(g_hwnd, g_bufferMs)) {
        g_activeBackend = BACKEND_WAVEOUT;
        return true;
    }

    g_activeBackend = BACKEND_NONE;
    return false;
}

bool InitAudio(HWND hwnd, const char *spinupWavPath, const char *idleWavPath,
                const char *accessWavPath, int idleVolume, int accessVolume, int spinupVolume,
                int minPlaybackMs, int bufferMs, int audioApi) {
    if (!MixerInit(spinupWavPath, idleWavPath, accessWavPath, idleVolume, accessVolume,
                   spinupVolume, minPlaybackMs)) {
        return false;
    }

    g_hwnd = hwnd;
    g_bufferMs = bufferMs;
    g_requestedAudioApi = audioApi;

    // InitAudio only fails outright if no backend at all could start,
    // matching the original failure mode (no sound hardware/driver at all).
    if (StartBackend(audioApi)) {
        return true;
    }

    MixerShutdown();
    return false;
}

bool SetAudioApi(int newAudioApi) {
    if (newAudioApi == g_requestedAudioApi && g_activeBackend != BACKEND_NONE) {
        return true; // nothing to do
    }
    g_requestedAudioApi = newAudioApi;

    if (g_activeBackend == BACKEND_DSOUND) {
        DSoundShutdown();
    } else if (g_activeBackend == BACKEND_WAVEOUT) {
        WaveOutShutdown();
    }
    g_activeBackend = BACKEND_NONE;

    return StartBackend(newAudioApi);
}

const char *GetActiveAudioBackendName() {
    switch (g_activeBackend) {
        case BACKEND_DSOUND:
            return "DirectSound";
        case BACKEND_WAVEOUT:
            return "WaveOut (MME)";
        default:
            return "None";
    }
}

void SetAudioAccessActive(BOOL active) {
    MixerSetAccessActive(active);
}

void SetAudioIdleVolume(int volume) {
    MixerSetIdleVolume(volume);
}

void SetAudioAccessVolume(int volume) {
    MixerSetAccessVolume(volume);
}

void SetAudioSpinupVolume(int volume) {
    MixerSetSpinupVolume(volume);
}

void SetAudioMinPlaybackMs(int ms) {
    MixerSetMinPlaybackMs(ms);
}

void SetAudioBufferMs(int ms) {
    g_bufferMs = ms;
    if (g_activeBackend == BACKEND_DSOUND) {
        DSoundSetBufferMs(ms);
    } else if (g_activeBackend == BACKEND_WAVEOUT) {
        WaveOutSetBufferMs(ms);
    }
}

int GetAudioLatencyMs() {
    if (g_activeBackend == BACKEND_DSOUND) {
        return DSoundGetLatencyMs();
    } else if (g_activeBackend == BACKEND_WAVEOUT) {
        return WaveOutGetLatencyMs();
    }
    return 0;
}

unsigned long GetAudioUnderrunCount() {
    if (g_activeBackend == BACKEND_DSOUND) {
        return DSoundGetUnderrunCount();
    } else if (g_activeBackend == BACKEND_WAVEOUT) {
        return WaveOutGetUnderrunCount();
    }
    return 0;
}

bool SwitchAudioSamplePack(const char *spinupWavPath, const char *idleWavPath,
                            const char *accessWavPath) {
    if (!MixerSwitchSamplePack(spinupWavPath, idleWavPath, accessWavPath)) {
        return false;
    }

    unsigned long newRate = MixerGetSampleRate();
    if (g_activeBackend == BACKEND_DSOUND) {
        return DSoundSwitchSamplePack(newRate);
    } else if (g_activeBackend == BACKEND_WAVEOUT) {
        return WaveOutSwitchSamplePack(newRate);
    }
    return true;
}

void ShutdownAudio() {
    if (g_activeBackend == BACKEND_DSOUND) {
        DSoundShutdown();
    } else if (g_activeBackend == BACKEND_WAVEOUT) {
        WaveOutShutdown();
    }
    g_activeBackend = BACKEND_NONE;
    MixerShutdown();
}
