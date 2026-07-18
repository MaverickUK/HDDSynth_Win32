#ifndef HDDSYNTH_MIXER_H
#define HDDSYNTH_MIXER_H

#include <stddef.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Loads the three sample layers and resets playback to the start of the
// spin-up sample. Returns true on success, false (with nothing latched)
// if any file fails to load.
bool MixerInit(const char *spinupPath, const char *idlePath, const char *accessPath);

// Fills `count` samples of 16-bit mono PCM by advancing whichever layers
// are currently active: spin-up plays once, then idle loops forever with
// the access sample layered on top (also looping) while active. Called
// from the audio engine's buffer-refill path.
void MixerFillBuffer(short *out, size_t count);

// Toggles whether the access layer is mixed in. Safe to call from a
// different thread than MixerFillBuffer runs on (uses InterlockedExchange).
void MixerSetAccessActive(BOOL active);

unsigned long MixerGetSampleRate();

void MixerShutdown();

#endif
