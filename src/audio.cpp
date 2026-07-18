// waveOut engine: owns a single output stream, refilled by its own
// dedicated thread rather than via window messages on the GUI thread.
//
// The first version used CALLBACK_WINDOW (MM_WOM_DONE posted to the GUI
// message queue), which caused audible stutters/silence during heavy
// disk I/O -- exactly when this app most needs to keep playing, since
// that's when the GUI thread's message queue gets delayed. CALLBACK_EVENT
// plus a dedicated thread decouples buffer refills from whatever the GUI
// thread is doing.
//
// See wav.cpp for why plain C headers (not stdlib.h) and HeapAlloc (not
// malloc) are used throughout this codebase.
#include "audio.h"
#include "mixer.h"
#include <mmsystem.h>
#include <process.h>

#define NUM_BUFFERS 4
#define BUFFER_SAMPLES 8192 // ~512ms at 16kHz per buffer; ~2s buffered
                             // total, traded for resilience against GUI-
                             // thread stalls during heavy disk activity.
                             // The tray icon still flips instantly (that's
                             // posted independently, not tied to audio
                             // buffer depth) -- only the audible access
                             // layer lags by up to ~2s behind detection.

static HWAVEOUT g_hWaveOut;
static WAVEHDR g_headers[NUM_BUFFERS];
static short *g_bufferData[NUM_BUFFERS];
static HANDLE g_event;
static HANDLE g_thread;
static volatile LONG g_stopRequested = 0;
static unsigned long g_currentSampleRate = 0;

static void FillAndQueue(int index) {
    MixerFillBuffer(g_bufferData[index], BUFFER_SAMPLES);

    WAVEHDR *hdr = &g_headers[index];
    hdr->lpData = (LPSTR)g_bufferData[index];
    hdr->dwBufferLength = BUFFER_SAMPLES * sizeof(short);
    hdr->dwFlags = 0;
    hdr->dwLoops = 0;

    waveOutPrepareHeader(g_hWaveOut, hdr, sizeof(WAVEHDR));
    waveOutWrite(g_hWaveOut, hdr, sizeof(WAVEHDR));
}

static unsigned __stdcall AudioThreadProc(void *) {
    while (!InterlockedExchangeAdd(&g_stopRequested, 0)) {
        WaitForSingleObject(g_event, INFINITE);
        if (InterlockedExchangeAdd(&g_stopRequested, 0)) {
            break;
        }
        for (int i = 0; i < NUM_BUFFERS; i++) {
            if (g_headers[i].dwFlags & WHDR_DONE) {
                waveOutUnprepareHeader(g_hWaveOut, &g_headers[i], sizeof(WAVEHDR));
                FillAndQueue(i);
            }
        }
    }
    return 0;
}

static bool OpenWaveOutDevice() {
    WAVEFORMATEX wfx;
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = MixerGetSampleRate();
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    if (waveOutOpen(&g_hWaveOut, WAVE_MAPPER, &wfx, (DWORD_PTR)g_event, 0, CALLBACK_EVENT) !=
        MMSYSERR_NOERROR) {
        return false;
    }
    g_currentSampleRate = wfx.nSamplesPerSec;
    return true;
}

static void StartAudioThread() {
    g_stopRequested = 0;
    unsigned tid;
    g_thread = (HANDLE)_beginthreadex(NULL, 0, AudioThreadProc, NULL, 0, &tid);
}

static void StopAudioThread() {
    if (g_thread) {
        InterlockedExchange(&g_stopRequested, 1);
        SetEvent(g_event); // wake the thread so it notices the stop request
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
}

bool InitAudio(HWND hwnd, const char *spinupWavPath, const char *idleWavPath,
                const char *accessWavPath, int volume, int balance, int minPlaybackMs) {
    (void)hwnd;

    if (!MixerInit(spinupWavPath, idleWavPath, accessWavPath, volume, balance, minPlaybackMs)) {
        return false;
    }

    g_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!g_event) {
        MixerShutdown();
        return false;
    }

    if (!OpenWaveOutDevice()) {
        CloseHandle(g_event);
        g_event = NULL;
        MixerShutdown();
        return false;
    }

    ZeroMemory(g_headers, sizeof(g_headers));
    for (int i = 0; i < NUM_BUFFERS; i++) {
        g_bufferData[i] = (short *)HeapAlloc(GetProcessHeap(), 0, BUFFER_SAMPLES * sizeof(short));
        FillAndQueue(i);
    }

    // Deliberately left at normal priority (no SetThreadPriority call):
    // an earlier version raised this to ABOVE_NORMAL, and the very next
    // real-hardware test showed the whole Windows UI freezing for a few
    // seconds during a large file copy -- on Win9x's largely single-
    // threaded GDI/USER subsystem, an elevated-priority thread waking
    // frequently is a plausible contributor, and the ~2s of buffering
    // already queued should absorb ordinary scheduling delays without it.
    StartAudioThread();

    return true;
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

bool SwitchAudioSamplePack(const char *spinupWavPath, const char *idleWavPath,
                            const char *accessWavPath) {
    if (!MixerSwitchSamplePack(spinupWavPath, idleWavPath, accessWavPath)) {
        return false;
    }

    unsigned long newRate = MixerGetSampleRate();
    if (newRate == g_currentSampleRate) {
        return true;
    }

    // Sample rate changed: the open waveOut device is locked to the rate
    // it was opened with, so it has to be reopened. Rare in practice
    // (all shipped packs are 16kHz) but a custom pack could differ.
    // Briefly stopping playback here is expected/acceptable UX for a
    // user-triggered pack switch, unlike the buffer-starvation stutter
    // that motivated the dedicated thread in the first place.
    StopAudioThread();

    waveOutReset(g_hWaveOut);
    for (int i = 0; i < NUM_BUFFERS; i++) {
        waveOutUnprepareHeader(g_hWaveOut, &g_headers[i], sizeof(WAVEHDR));
    }
    waveOutClose(g_hWaveOut);

    if (!OpenWaveOutDevice()) {
        return false;
    }
    for (int i = 0; i < NUM_BUFFERS; i++) {
        FillAndQueue(i);
    }

    StartAudioThread();
    return true;
}

void ShutdownAudio() {
    StopAudioThread();
    if (g_event) {
        CloseHandle(g_event);
        g_event = NULL;
    }
    if (g_hWaveOut) {
        waveOutReset(g_hWaveOut);
        for (int i = 0; i < NUM_BUFFERS; i++) {
            waveOutUnprepareHeader(g_hWaveOut, &g_headers[i], sizeof(WAVEHDR));
            if (g_bufferData[i]) {
                HeapFree(GetProcessHeap(), 0, g_bufferData[i]);
                g_bufferData[i] = NULL;
            }
        }
        waveOutClose(g_hWaveOut);
        g_hWaveOut = NULL;
    }
    MixerShutdown();
}
