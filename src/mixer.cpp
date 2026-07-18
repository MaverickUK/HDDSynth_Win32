// Software PCM mixer: spin-up plays once, then idle loops forever with
// the access sample layered on top while activity is detected. Runs
// entirely inside MixerFillBuffer, called from the audio engine's
// dedicated buffer-refill thread -- the only cross-thread entry point is
// MixerSetAccessActive (called from the GUI thread, forwarding the disk
// monitor's findings), hence the Interlocked access to g_accessActive.
//
// The access layer doesn't just loop continuously while active: it plays
// only while activity is detected, with a floor so a very brief blip
// still produces an audible ~200ms snippet rather than a clipped
// fraction-of-a-second click, and each activation starts from a random
// point in the sample rather than always the beginning, so repeated
// triggers don't sound identical/mechanical.
#include "mixer.h"
#include "wav.h"

// Access layer is mixed in below full volume so it reads as "activity on
// top of the idle hum" rather than replacing it, echoing the original
// hardware's balance control between idle and access.
#define ACCESS_MIX_NUM 3
#define ACCESS_MIX_DEN 4

#define ACCESS_MIN_PLAY_MS 200

enum MixerPhase { PHASE_SPINUP, PHASE_IDLE };

static WavPcm g_spinup;
static WavPcm g_idle;
static WavPcm g_access;

static MixerPhase g_phase = PHASE_SPINUP;
static size_t g_spinupPos = 0;
static size_t g_idlePos = 0;
static size_t g_accessPos = 0;

static volatile LONG g_accessActive = 0;
static bool g_accessPlaying = false;
static size_t g_accessMinRemaining = 0;

// Small hand-rolled PRNG instead of rand(): including <stdlib.h>/<cstdlib>
// anywhere in this codebase is a hard compile error under our CRT setup
// (see wav.cpp) because libstdc++'s C++ wrapper unconditionally
// references a UCRT-only symbol our msvcrt-os target doesn't declare.
// This doesn't need to be cryptographically anything, just varied enough
// that repeated access triggers don't all start at the same offset.
static unsigned long g_rngState = 1;

static void SeedRng(unsigned long seed) {
    g_rngState = seed ? seed : 1;
}

static unsigned long NextRandom() {
    g_rngState = g_rngState * 1103515245UL + 12345UL;
    return (g_rngState >> 16) & 0x7fffffffUL;
}

bool MixerInit(const char *spinupPath, const char *idlePath, const char *accessPath) {
    if (!LoadWavMono16(spinupPath, &g_spinup) ||
        !LoadWavMono16(idlePath, &g_idle) ||
        !LoadWavMono16(accessPath, &g_access)) {
        return false;
    }
    g_phase = PHASE_SPINUP;
    g_spinupPos = 0;
    g_idlePos = 0;
    g_accessPos = 0;
    g_accessPlaying = false;
    g_accessMinRemaining = 0;
    SeedRng((unsigned long)GetTickCount());
    return true;
}

static short ClampSample(int v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (short)v;
}

void MixerFillBuffer(short *out, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (g_phase == PHASE_SPINUP) {
            if (g_spinup.sampleCount == 0 || g_spinupPos >= g_spinup.sampleCount) {
                g_phase = PHASE_IDLE;
                g_idlePos = 0;
            } else {
                out[i] = g_spinup.samples[g_spinupPos++];
                continue;
            }
        }

        // PHASE_IDLE
        int mixed = g_idle.samples[g_idlePos];
        g_idlePos++;
        if (g_idlePos >= g_idle.sampleCount) {
            g_idlePos = 0;
        }

        // Checked per-sample (not once per buffer) so a rising edge is
        // caught promptly rather than waiting up to a whole buffer's
        // worth of latency (buffers are ~512ms).
        BOOL activeNow = InterlockedExchangeAdd(&g_accessActive, 0) != 0;
        if (activeNow && !g_accessPlaying && g_access.sampleCount > 0) {
            g_accessPlaying = true;
            g_accessPos = NextRandom() % g_access.sampleCount;
            g_accessMinRemaining = (size_t)((unsigned long long)g_idle.sampleRate * ACCESS_MIN_PLAY_MS / 1000);
        }

        if (g_accessPlaying && g_access.sampleCount > 0) {
            mixed += (g_access.samples[g_accessPos] * ACCESS_MIX_NUM) / ACCESS_MIX_DEN;
            g_accessPos++;
            if (g_accessPos >= g_access.sampleCount) {
                g_accessPos = 0;
            }

            if (g_accessMinRemaining > 0) {
                g_accessMinRemaining--;
            }
            if (!activeNow && g_accessMinRemaining == 0) {
                g_accessPlaying = false;
            }
        }

        out[i] = ClampSample(mixed);
    }
}

void MixerSetAccessActive(BOOL active) {
    InterlockedExchange(&g_accessActive, active ? 1 : 0);
}

unsigned long MixerGetSampleRate() {
    return g_idle.sampleRate;
}

void MixerShutdown() {
    FreeWavPcm(&g_spinup);
    FreeWavPcm(&g_idle);
    FreeWavPcm(&g_access);
}
