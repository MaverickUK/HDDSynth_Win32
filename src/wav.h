#ifndef HDDSYNTH_WAV_H
#define HDDSYNTH_WAV_H

#include <stddef.h>

// Raw 16-bit mono PCM, decoded from a WAV file. Stereo sources are
// downmixed to mono at load time so the mixer can always do simple
// sample-by-sample addition without per-file format branching.
struct WavPcm {
    short *samples;
    size_t sampleCount;
    unsigned long sampleRate;
};

// Returns true on success. All four original HDDSynth samples are 16-bit
// PCM (mono or stereo); anything else (8-bit, float, ADPCM, ...) is
// rejected rather than guessed at.
bool LoadWavMono16(const char *path, WavPcm *out);
void FreeWavPcm(WavPcm *wav);

#endif
