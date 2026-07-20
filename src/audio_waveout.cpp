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
// This is the fallback-of-last-resort backend (see audio.cpp's
// dispatcher and audio_dsound.cpp): winmm ships with every Windows since
// 3.1, so it's always available, unlike DirectSound. Sample loading is
// owned by audio.cpp (MixerInit is called once regardless of which
// backend ends up active) -- this file only owns the waveOut device and
// its buffers.
//
// See wav.cpp for why plain C headers (not stdlib.h) and HeapAlloc (not
// malloc) are used throughout this codebase.
#include "audio_backend.h"
#include "mixer.h"
#include <mmsystem.h>
#include <process.h>

// A buffer's content is fixed at the moment it's generated and doesn't
// change once queued, and a freshly-generated buffer has to wait behind
// whatever's already ahead of it in the device's FIFO -- so total queued
// depth (buffer count * BUFFER_SAMPLES) is *both* the worst-case delay
// before an activity-flag change becomes audible *and* how much of a
// driver/CPU stall (e.g. a PIO-mode disk transfer with no DMA hogging
// the CPU enough to delay the sound driver's own completion signaling --
// not something any thread priority on our side can work around) the
// audio can absorb before it goes silent. These two things trade
// directly against each other with this buffering approach, which is why
// depth is now a user-facing "Audio Buffering" Settings slider
// (audioBufferMs) rather than a fixed constant: there's no single right
// answer, it depends on the machine and what's more annoying to a given
// user, lag or the occasional dropout during something like a Scandisk
// surface scan.
#define BUFFER_UNIT_SAMPLES 800 // ~50ms at 16kHz; fixed refill granularity
#define MIN_BUFFERS 2             // ~100ms floor
#define MAX_BUFFERS 40            // ~2000ms ceiling; also the fixed array size below

static HWAVEOUT g_hWaveOut;
static WAVEHDR g_headers[MAX_BUFFERS];
static short *g_bufferData[MAX_BUFFERS];
static int g_numBuffers = 4;
static HANDLE g_event;
static HANDLE g_thread;
static volatile LONG g_stopRequested = 0;
static unsigned long g_currentSampleRate = 0;

// Diagnostics (see audio.h's GetAudioLatencyMs/GetAudioUnderrunCount): total
// samples handed to the device so far, and a count of times the whole queue
// was found drained on one wake (WHDR_DONE on every buffer at once -- normally
// only a subset finish between wakeups, so all of them done means playback
// ran dry before the refill thread caught up, i.e. an audible gap happened).
static unsigned long g_samplesWritten = 0;
static volatile LONG g_underrunCount = 0;

static int MsToBufferCount(int ms) {
    // ~128ms; assumes a 16kHz sample rate, true for every shipped pack.
    // BUFFER_UNIT_SAMPLES is a fixed sample count, so a pack at some
    // other rate would make each buffer a slightly different real-world
    // duration than this nominal figure -- a minor approximation, not
    // worth the complexity of re-deriving it from MixerGetSampleRate()
    // for what's meant to be a rough "how much lag/headroom" slider.
    const int unitMs = BUFFER_UNIT_SAMPLES * 1000 / 16000;
    int count = (ms + unitMs / 2) / unitMs; // round to nearest
    if (count < MIN_BUFFERS) count = MIN_BUFFERS;
    if (count > MAX_BUFFERS) count = MAX_BUFFERS;
    return count;
}

static void FillAndQueue(int index) {
    MixerFillBuffer(g_bufferData[index], BUFFER_UNIT_SAMPLES);
    g_samplesWritten += BUFFER_UNIT_SAMPLES;

    WAVEHDR *hdr = &g_headers[index];
    hdr->lpData = (LPSTR)g_bufferData[index];
    hdr->dwBufferLength = BUFFER_UNIT_SAMPLES * sizeof(short);
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
        int doneCount = 0;
        for (int i = 0; i < g_numBuffers; i++) {
            if (g_headers[i].dwFlags & WHDR_DONE) {
                doneCount++;
                waveOutUnprepareHeader(g_hWaveOut, &g_headers[i], sizeof(WAVEHDR));
                FillAndQueue(i);
            }
        }
        if (doneCount == g_numBuffers) {
            InterlockedIncrement(&g_underrunCount);
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

static void FreeBuffers() {
    for (int i = 0; i < g_numBuffers; i++) {
        waveOutUnprepareHeader(g_hWaveOut, &g_headers[i], sizeof(WAVEHDR));
        if (g_bufferData[i]) {
            HeapFree(GetProcessHeap(), 0, g_bufferData[i]);
            g_bufferData[i] = NULL;
        }
    }
}

static void AllocateAndQueueBuffers() {
    ZeroMemory(g_headers, sizeof(g_headers));
    for (int i = 0; i < g_numBuffers; i++) {
        g_bufferData[i] = (short *)HeapAlloc(GetProcessHeap(), 0, BUFFER_UNIT_SAMPLES * sizeof(short));
        FillAndQueue(i);
    }
}

bool WaveOutInit(HWND hwnd, int bufferMs) {
    (void)hwnd;

    g_samplesWritten = 0;
    g_underrunCount = 0;
    g_numBuffers = MsToBufferCount(bufferMs);

    g_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!g_event) {
        return false;
    }

    if (!OpenWaveOutDevice()) {
        CloseHandle(g_event);
        g_event = NULL;
        return false;
    }

    AllocateAndQueueBuffers();

    // Deliberately left at normal priority (no SetThreadPriority call):
    // an earlier version raised this to ABOVE_NORMAL, and the very next
    // real-hardware test showed the whole Windows UI freezing for a few
    // seconds during a large file copy -- on Win9x's largely single-
    // threaded GDI/USER subsystem, an elevated-priority thread waking
    // frequently is a plausible contributor. That test later turned out
    // to implicate PIO-vs-DMA disk transfer instead (see the freeze
    // still happening with this thread at normal priority and the disk
    // monitor provably idle), but there's no evidence priority elevation
    // would actually help this specific failure mode either -- see
    // BUFFER_UNIT_SAMPLES's comment on why buffering depth, not thread
    // scheduling, is the real lever here.
    StartAudioThread();

    return true;
}

void WaveOutSetBufferMs(int ms) {
    int newCount = MsToBufferCount(ms);
    if (newCount == g_numBuffers) {
        return;
    }

    StopAudioThread();
    waveOutReset(g_hWaveOut);
    FreeBuffers();
    g_numBuffers = newCount;
    AllocateAndQueueBuffers();
    StartAudioThread();
}

bool WaveOutSwitchSamplePack(unsigned long newSampleRate) {
    if (newSampleRate == g_currentSampleRate) {
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
    FreeBuffers();
    waveOutClose(g_hWaveOut);

    if (!OpenWaveOutDevice()) {
        return false;
    }
    AllocateAndQueueBuffers();

    StartAudioThread();
    return true;
}

void WaveOutShutdown() {
    StopAudioThread();
    if (g_event) {
        CloseHandle(g_event);
        g_event = NULL;
    }
    if (g_hWaveOut) {
        waveOutReset(g_hWaveOut);
        FreeBuffers();
        waveOutClose(g_hWaveOut);
        g_hWaveOut = NULL;
    }
}

int WaveOutGetLatencyMs() {
    if (!g_hWaveOut || g_currentSampleRate == 0) {
        return 0;
    }
    MMTIME mmt;
    mmt.wType = TIME_SAMPLES;
    if (waveOutGetPosition(g_hWaveOut, &mmt, sizeof(mmt)) != MMSYSERR_NOERROR ||
        mmt.wType != TIME_SAMPLES) {
        return 0;
    }
    unsigned long played = mmt.u.sample;
    if (played > g_samplesWritten) {
        return 0; // shouldn't happen, but never report negative latency
    }
    return (int)((g_samplesWritten - played) * 1000 / g_currentSampleRate);
}

unsigned long WaveOutGetUnderrunCount() {
    return (unsigned long)InterlockedExchangeAdd(&g_underrunCount, 0);
}
