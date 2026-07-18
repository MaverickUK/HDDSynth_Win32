#ifndef HDDSYNTH_AUDIO_H
#define HDDSYNTH_AUDIO_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Opens a single waveOut stream driven by the mixer (see mixer.h) on its
// own dedicated thread: plays spinupWavPath once, then loops idleWavPath
// forever with accessWavPath layered on top while
// SetAudioAccessActive(TRUE) is in effect. Returns true on success.
//
// Buffer refills happen on their own thread (woken by a waveOut event),
// not via window messages on the GUI thread -- heavy disk I/O can delay
// the GUI message queue for a second or more, and that's exactly when
// this app most needs to keep playing.
bool InitAudio(HWND hwnd, const char *spinupWavPath, const char *idleWavPath,
                const char *accessWavPath, int volume, int balance, int minPlaybackMs,
                int bufferMs);

// Thin pass-throughs to the mixer, so callers only need to depend on
// audio.h.
void SetAudioAccessActive(BOOL active);
void SetAudioVolume(int volume);
void SetAudioBalance(int balance);
void SetAudioMinPlaybackMs(int ms);

// Total queued audio depth in ms -- trades directly between latency
// (lower is snappier) and resilience against playback stalls (higher
// survives longer CPU/driver hiccups, e.g. a PIO-mode disk transfer,
// without going silent). Reallocates and requeues buffers; briefly
// stops/restarts the refill thread, same as a sample pack switch.
void SetAudioBufferMs(int ms);

// Switches to a different sample pack's WAVs live (see
// MixerSwitchSamplePack), reopening the waveOut device first if the new
// pack's sample rate differs from the one currently playing. Returns
// false (leaving the current pack in place) if any file fails to load.
bool SwitchAudioSamplePack(const char *spinupWavPath, const char *idleWavPath,
                            const char *accessWavPath);

void ShutdownAudio();

#endif
