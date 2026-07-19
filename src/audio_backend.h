// Internal interface between audio.cpp's backend dispatcher and its two
// backend implementations (audio_waveout.cpp, audio_dsound.cpp). Not
// part of the public API in audio.h -- callers outside this trio never
// see these names.
//
// Both backends assume MixerInit has already succeeded (audio.cpp owns
// that call, once, regardless of which backend ends up active) and pull
// PCM from the mixer via MixerFillBuffer/MixerGetSampleRate exactly the
// same way, so a rate change only ever needs the backend to reopen its
// own device/buffer, never to reload sample data.
#ifndef HDDSYNTH_AUDIO_BACKEND_H
#define HDDSYNTH_AUDIO_BACKEND_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// waveOut (MME) backend. Always available (winmm ships with every
// Windows since 3.1) -- the fallback of last resort.
bool WaveOutInit(HWND hwnd, int bufferMs);
void WaveOutSetBufferMs(int ms);
// Reopens the device only if newSampleRate differs from what it's
// currently playing. Returns false if reopening fails.
bool WaveOutSwitchSamplePack(unsigned long newSampleRate);
void WaveOutShutdown();

// DirectSound backend. Returns false from DSoundInit if DirectSound
// isn't usable on this machine for any reason (dsound.dll missing, no
// working driver behind it, buffer/notify setup failure, ...) -- the
// caller (audio.cpp) falls back to the waveOut backend in that case, and
// none of the other DSound* functions are called unless DSoundInit
// succeeded.
bool DSoundInit(HWND hwnd, int bufferMs);
void DSoundSetBufferMs(int ms);
bool DSoundSwitchSamplePack(unsigned long newSampleRate);
void DSoundShutdown();

#endif
