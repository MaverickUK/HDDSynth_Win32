#ifndef HDDSYNTH_SETTINGS_H
#define HDDSYNTH_SETTINGS_H

// Matches audio_waveout.cpp's MIN_BUFFERS/MAX_BUFFERS (~100ms/~2000ms);
// kept as round numbers here since this is the user-facing slider range,
// not derived from the exact buffer-unit arithmetic. This is the floor
// for the waveOut backend specifically -- DirectSound's own chunking
// already supports going much lower (see MIN_AUDIO_BUFFER_MS_DSOUND),
// since hardware-assisted buffering is the whole reason it was added.
// The Settings dialog picks which floor applies based on which backend
// is actually active.
#define MIN_AUDIO_BUFFER_MS 100
#define MIN_AUDIO_BUFFER_MS_DSOUND 25
#define MAX_AUDIO_BUFFER_MS 2000

// Slider range for minimum access playback -- 0 (no minimum, the access
// sound can cut off as soon as activity stops) to 1000ms (a full second
// of guaranteed playback per trigger).
#define MIN_MINPLAY_MS 0
#define MAX_MINPLAY_MS 1000

// Slider range for the activity threshold -- see diskmon.cpp's
// ACTIVITY_BYTE_THRESHOLD-equivalent comment: the default (2048) is
// "comfortably below file-copy territory, well above typical idle
// housekeeping I/O". 0 means any movement at all counts as activity;
// 32768 is high enough that only sustained heavy transfers would
// register, without exceeding TBM_SETRANGE's 16-bit-per-bound limit.
#define MIN_ACTIVITY_THRESHOLD_BYTES 0
#define MAX_ACTIVITY_THRESHOLD_BYTES 32768

struct Settings {
    int idleVolume;               // 0-100
    int accessVolume;             // 0-100
    int spinupVolume;             // 0-100, 0 skips spin-up playback entirely
    int minPlaybackMs;            // access sample minimum play time
    int activityThresholdBytes;   // bytes/poll before it counts as activity
    int audioBufferMs;            // total queued audio depth: latency vs stall resilience
    int audioApi;                 // AUDIO_API_AUTO/WAVEOUT/DSOUND (see audio.h)
    char samplePack[64];
};

// Reads hddsynth.ini next to the exe, falling back to defaults for
// anything missing (including a missing file entirely -- first run).
void LoadSettings(Settings *out);

// Writes all fields back to hddsynth.ini next to the exe.
void SaveSettings(const Settings *s);

#endif
