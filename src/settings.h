#ifndef HDDSYNTH_SETTINGS_H
#define HDDSYNTH_SETTINGS_H

struct Settings {
    int volume;                  // 0-100
    int balance;                 // 0-100, 50 = idle/activity equally loud
    int minPlaybackMs;            // access sample minimum play time
    int activityThresholdBytes;   // bytes/poll before it counts as activity
    char samplePack[64];
};

// Reads hddsynth.ini next to the exe, falling back to defaults for
// anything missing (including a missing file entirely -- first run).
void LoadSettings(Settings *out);

// Writes all fields back to hddsynth.ini next to the exe.
void SaveSettings(const Settings *s);

#endif
