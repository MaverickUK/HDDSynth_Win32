#ifndef HDDSYNTH_SETTINGS_H
#define HDDSYNTH_SETTINGS_H

// Matches audio.cpp's MIN_BUFFERS/MAX_BUFFERS (~256ms/~2048ms); kept as
// round numbers here since this is the user-facing slider range, not
// derived from the exact buffer-unit arithmetic.
#define MIN_AUDIO_BUFFER_MS 250
#define MAX_AUDIO_BUFFER_MS 2000

struct Settings {
    int volume;                  // 0-100
    int balance;                 // 0-100, 50 = idle/activity equally loud
    int minPlaybackMs;            // access sample minimum play time
    int activityThresholdBytes;   // bytes/poll before it counts as activity
    int audioBufferMs;            // total queued audio depth: latency vs stall resilience
    char samplePack[64];
};

// Reads hddsynth.ini next to the exe, falling back to defaults for
// anything missing (including a missing file entirely -- first run).
void LoadSettings(Settings *out);

// Writes all fields back to hddsynth.ini next to the exe.
void SaveSettings(const Settings *s);

#endif
