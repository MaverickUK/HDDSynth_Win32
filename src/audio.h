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
                const char *accessWavPath);

// Thin pass-through to the mixer, so callers only need to depend on
// audio.h once the disk-activity monitor is wired in.
void SetAudioAccessActive(BOOL active);

void ShutdownAudio();

#endif
