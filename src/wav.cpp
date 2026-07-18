// WAV loader for the four original HDDSynth PCM samples.
//
// Allocation goes through Win32's process heap (HeapAlloc/HeapFree)
// rather than malloc/free: *any* include of <stdlib.h> or <cstdlib> here
// pulls in libstdc++'s C++ wrapper, which unconditionally references
// ::quick_exit -- a UCRT-only symbol that mingw's own headers don't
// declare once targeting the classic msvcrt-os CRT (see Makefile/README).
// That's a hard compile error, and WIN32_LEAN_AND_MEAN doesn't help here
// since it only changes what windows.h itself pulls in, not a direct
// include. <stdio.h>/<string.h> don't have the same problem.
#include "wav.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static void *HeapAllocZ(size_t size) {
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static void HeapFreeP(void *p) {
    if (p) {
        HeapFree(GetProcessHeap(), 0, p);
    }
}

struct RiffChunkHeader {
    char id[4];
    unsigned int size;
};

bool LoadWavMono16(const char *path, WavPcm *out) {
    out->samples = NULL;
    out->sampleCount = 0;
    out->sampleRate = 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }

    char riffId[4];
    unsigned int riffSize;
    char waveId[4];
    if (fread(riffId, 1, 4, f) != 4 || memcmp(riffId, "RIFF", 4) != 0 ||
        fread(&riffSize, 4, 1, f) != 1 ||
        fread(waveId, 1, 4, f) != 4 || memcmp(waveId, "WAVE", 4) != 0) {
        fclose(f);
        return false;
    }

    unsigned short numChannels = 0;
    unsigned int sampleRate = 0;
    unsigned short bitsPerSample = 0;
    unsigned short formatTag = 0;
    bool haveFmt = false;

    void *rawData = NULL;
    unsigned int rawDataSize = 0;

    for (;;) {
        RiffChunkHeader chunk;
        if (fread(&chunk, sizeof(chunk), 1, f) != 1) {
            break;
        }

        if (memcmp(chunk.id, "fmt ", 4) == 0) {
            unsigned char fmtBuf[16];
            if (chunk.size < sizeof(fmtBuf) || fread(fmtBuf, 1, sizeof(fmtBuf), f) != sizeof(fmtBuf)) {
                fclose(f);
                HeapFreeP(rawData);
                return false;
            }
            formatTag = (unsigned short)(fmtBuf[0] | (fmtBuf[1] << 8));
            numChannels = (unsigned short)(fmtBuf[2] | (fmtBuf[3] << 8));
            sampleRate = (unsigned int)(fmtBuf[4] | (fmtBuf[5] << 8) | (fmtBuf[6] << 16) | (fmtBuf[7] << 24));
            bitsPerSample = (unsigned short)(fmtBuf[14] | (fmtBuf[15] << 8));
            haveFmt = true;
            if (chunk.size > sizeof(fmtBuf)) {
                fseek(f, chunk.size - sizeof(fmtBuf), SEEK_CUR);
            }
        } else if (memcmp(chunk.id, "data", 4) == 0) {
            rawData = HeapAllocZ(chunk.size);
            if (!rawData || fread(rawData, 1, chunk.size, f) != chunk.size) {
                fclose(f);
                HeapFreeP(rawData);
                return false;
            }
            rawDataSize = chunk.size;
        } else {
            fseek(f, chunk.size, SEEK_CUR);
        }

        if (chunk.size & 1) {
            fseek(f, 1, SEEK_CUR); // chunks are word-aligned
        }
    }

    fclose(f);

    // formatTag 1 == WAVE_FORMAT_PCM. Only 16-bit mono/stereo PCM is
    // supported -- that's what all four original samples are; anything
    // else is a format this loader deliberately doesn't try to guess at.
    if (!haveFmt || !rawData || formatTag != 1 || bitsPerSample != 16 ||
        (numChannels != 1 && numChannels != 2)) {
        HeapFreeP(rawData);
        return false;
    }

    const short *src = (const short *)rawData;
    size_t srcSampleCount = rawDataSize / 2; // 16-bit samples

    if (numChannels == 1) {
        short *samples = (short *)HeapAllocZ(srcSampleCount * sizeof(short));
        memcpy(samples, src, srcSampleCount * sizeof(short));
        out->samples = samples;
        out->sampleCount = srcSampleCount;
    } else {
        size_t frames = srcSampleCount / 2;
        short *samples = (short *)HeapAllocZ(frames * sizeof(short));
        for (size_t i = 0; i < frames; i++) {
            int l = src[i * 2];
            int r = src[i * 2 + 1];
            samples[i] = (short)((l + r) / 2);
        }
        out->samples = samples;
        out->sampleCount = frames;
    }

    out->sampleRate = sampleRate;
    HeapFreeP(rawData);
    return true;
}

void FreeWavPcm(WavPcm *wav) {
    HeapFreeP(wav->samples);
    wav->samples = NULL;
    wav->sampleCount = 0;
}
