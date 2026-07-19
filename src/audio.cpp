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

bool InitAudio(HWND hwnd, const char *spinupWavPath, const char *idleWavPath,
                const char *accessWavPath, int volume, int balance, int minPlaybackMs,
                int bufferMs, int audioApi) {
    if (!MixerInit(spinupWavPath, idleWavPath, accessWavPath, volume, balance, minPlaybackMs)) {
        return false;
    }

    if (audioApi != AUDIO_API_WAVEOUT && DSoundInit(hwnd, bufferMs)) {
        g_activeBackend = BACKEND_DSOUND;
        return true;
    }

    // Either WaveOut was explicitly requested, or DirectSound was
    // requested/preferred but isn't usable on this machine (no DirectX
    // installed, no working driver behind it, etc.) -- either way, fall
    // back to the backend that's always available. InitAudio only fails
    // outright if this also fails, matching today's original failure
    // mode (no sound hardware/driver at all).
    if (WaveOutInit(hwnd, bufferMs)) {
        g_activeBackend = BACKEND_WAVEOUT;
        return true;
    }

    MixerShutdown();
    g_activeBackend = BACKEND_NONE;
    return false;
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

void SetAudioVolume(int volume) {
    MixerSetVolume(volume);
}

void SetAudioBalance(int balance) {
    MixerSetBalance(balance);
}

void SetAudioMinPlaybackMs(int ms) {
    MixerSetMinPlaybackMs(ms);
}

void SetAudioBufferMs(int ms) {
    if (g_activeBackend == BACKEND_DSOUND) {
        DSoundSetBufferMs(ms);
    } else if (g_activeBackend == BACKEND_WAVEOUT) {
        WaveOutSetBufferMs(ms);
    }
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
