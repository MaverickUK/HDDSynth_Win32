#ifndef HDDSYNTH_SAMPLEPACK_H
#define HDDSYNTH_SAMPLEPACK_H

#include <stddef.h>

#define SAMPLEPACK_MAX_PACKS 32
#define SAMPLEPACK_NAME_LEN 64

// Scans <exeDir>\samples\ for subdirectories -- each one is a sample
// pack, expected to contain spinup.wav, idle.wav, and access.wav. Fills
// names[0..count) and returns count (0 if the samples folder is missing
// or empty).
int ScanSamplePacks(char names[][SAMPLEPACK_NAME_LEN], int maxPacks);

// Builds the three sample paths for a given pack name, e.g.
// "<exeDir>samples\original\spinup.wav".
void BuildSamplePackPaths(const char *packName, char *spinupPath, char *idlePath,
                          char *accessPath, size_t pathSize);

#endif
