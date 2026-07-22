#ifndef HDDSYNTH_MIXER_H
#define HDDSYNTH_MIXER_H

#include <stddef.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Loads the three sample layers and resets playback to the start of the
// spin-up sample. Returns true on success, false (with nothing latched)
// if any file fails to load. Each of idleVolume/accessVolume/spinupVolume
// is an independent 0-100 level for its own layer -- spinupVolume of 0
// skips spin-up playback entirely rather than playing it silently.
bool MixerInit(const char *spinupPath, const char *idlePath, const char *accessPath,
               int idleVolume, int accessVolume, int spinupVolume, int minPlaybackMs);

// Fills `count` samples of 16-bit mono PCM by advancing whichever layers
// are currently active: spin-up plays once, then idle loops forever with
// the access sample layered on top (also looping) while active. Called
// from the audio engine's buffer-refill path.
void MixerFillBuffer(short *out, size_t count);

// Toggles whether the access layer is mixed in. Safe to call from a
// different thread than MixerFillBuffer runs on (uses InterlockedExchange).
void MixerSetAccessActive(BOOL active);

// Independent 0-100 volume for each layer. MixerSetSpinupVolume(0) skips
// spin-up entirely on the next fill if it hasn't started yet, or as soon
// as the change takes effect if it's already mid-playback.
void MixerSetIdleVolume(int volume);
void MixerSetAccessVolume(int volume);
void MixerSetSpinupVolume(int volume);

// Minimum time (ms) the access layer keeps playing once triggered, even
// if activity stops sooner.
void MixerSetMinPlaybackMs(int ms);

unsigned long MixerGetSampleRate();

// Swaps in a different sample pack's WAVs live, without replaying
// spin-up (the drive is already "spun up") -- picks up in the idle
// phase immediately. Safe to call from a different thread than
// MixerFillBuffer runs on; internally serialized with a critical
// section since this replaces whole WavPcm buffers, not a single value
// an Interlocked op could swap atomically. Returns false (leaving the
// current pack in place) if any of the three files fail to load.
bool MixerSwitchSamplePack(const char *spinupPath, const char *idlePath, const char *accessPath);

void MixerShutdown();

#endif
