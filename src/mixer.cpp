// Software PCM mixer: spin-up plays once, then idle loops forever with
// the access sample layered on top while activity is detected. Runs
// entirely inside MixerFillBuffer, called from the audio engine's
// dedicated buffer-refill thread.
//
// Two kinds of shared mutable state, two synchronization approaches:
//  - g_accessActive is a single flag set from the GUI thread (forwarding
//    the disk monitor's findings) -- Interlocked access is enough.
//  - The sample buffers themselves (g_spinup/g_idle/g_access) and the
//    volume/balance/min-playback settings can all be replaced from the
//    GUI thread (Settings dialog, sample pack switch) while the audio
//    thread is mid-mix. These are compound/multi-field state an
//    Interlocked op can't swap atomically, so they're behind
//    g_lock instead.
#include "mixer.h"
#include "wav.h"

enum MixerPhase { PHASE_SPINUP, PHASE_IDLE };

static CRITICAL_SECTION g_lock;
static WavPcm g_spinup;
static WavPcm g_idle;
static WavPcm g_access;
static int g_volume = 100;
static int g_balance = 50;
static int g_minPlaybackMs = 200;

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

bool MixerInit(const char *spinupPath, const char *idlePath, const char *accessPath,
               int volume, int balance, int minPlaybackMs) {
    InitializeCriticalSection(&g_lock);

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
    g_volume = volume;
    g_balance = balance;
    g_minPlaybackMs = minPlaybackMs;
    SeedRng((unsigned long)GetTickCount());
    return true;
}

static short ClampSample(int v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (short)v;
}

void MixerFillBuffer(short *out, size_t count) {
    EnterCriticalSection(&g_lock);

    int volume = g_volume;
    // Straight linear crossfade: idle at 100% / access at 0% when
    // balance=0 ("Idle" end), the reverse at balance=100 ("Activity"
    // end), 50/50 at center. Previously this floored each side at 50%
    // (never fully silencing either layer) so that neither could be
    // muted by balance alone -- but that meant balance=100 still left
    // idle audible, which is exactly backwards from what the slider
    // labels ("Idle" / "Activity") imply and what a user reasonably
    // expects. Master Volume still scales the combined result, so
    // overall loudness at the 50/50 center can be compensated there if
    // it feels quieter than before.
    int idleWeightX100 = 100 - g_balance;
    int accessWeightX100 = g_balance;

    for (size_t i = 0; i < count; i++) {
        if (g_phase == PHASE_SPINUP) {
            if (g_spinup.sampleCount == 0 || g_spinupPos >= g_spinup.sampleCount) {
                g_phase = PHASE_IDLE;
                g_idlePos = 0;
            } else {
                int v = (g_spinup.samples[g_spinupPos++] * volume) / 100;
                out[i] = ClampSample(v);
                continue;
            }
        }

        // PHASE_IDLE
        int mixed = (g_idle.samples[g_idlePos] * idleWeightX100) / 100;
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
            g_accessMinRemaining = (size_t)((unsigned long long)g_idle.sampleRate * g_minPlaybackMs / 1000);
        }

        if (g_accessPlaying && g_access.sampleCount > 0) {
            mixed += (g_access.samples[g_accessPos] * accessWeightX100) / 100;
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

        mixed = (mixed * volume) / 100;
        out[i] = ClampSample(mixed);
    }

    LeaveCriticalSection(&g_lock);
}

void MixerSetAccessActive(BOOL active) {
    InterlockedExchange(&g_accessActive, active ? 1 : 0);
}

void MixerSetVolume(int volume) {
    EnterCriticalSection(&g_lock);
    g_volume = volume;
    LeaveCriticalSection(&g_lock);
}

void MixerSetBalance(int balance) {
    EnterCriticalSection(&g_lock);
    g_balance = balance;
    LeaveCriticalSection(&g_lock);
}

void MixerSetMinPlaybackMs(int ms) {
    EnterCriticalSection(&g_lock);
    g_minPlaybackMs = ms;
    LeaveCriticalSection(&g_lock);
}

unsigned long MixerGetSampleRate() {
    return g_idle.sampleRate;
}

bool MixerSwitchSamplePack(const char *spinupPath, const char *idlePath, const char *accessPath) {
    WavPcm newSpinup, newIdle, newAccess;
    if (!LoadWavMono16(spinupPath, &newSpinup)) {
        return false;
    }
    if (!LoadWavMono16(idlePath, &newIdle)) {
        FreeWavPcm(&newSpinup);
        return false;
    }
    if (!LoadWavMono16(accessPath, &newAccess)) {
        FreeWavPcm(&newSpinup);
        FreeWavPcm(&newIdle);
        return false;
    }

    EnterCriticalSection(&g_lock);
    FreeWavPcm(&g_spinup);
    FreeWavPcm(&g_idle);
    FreeWavPcm(&g_access);
    g_spinup = newSpinup;
    g_idle = newIdle;
    g_access = newAccess;
    // The drive is already "spun up" -- a pack switch goes straight to
    // idle rather than replaying spin-up.
    g_phase = PHASE_IDLE;
    g_idlePos = 0;
    g_accessPos = 0;
    g_accessPlaying = false;
    g_accessMinRemaining = 0;
    LeaveCriticalSection(&g_lock);
    return true;
}

void MixerShutdown() {
    FreeWavPcm(&g_spinup);
    FreeWavPcm(&g_idle);
    FreeWavPcm(&g_access);
    DeleteCriticalSection(&g_lock);
}
