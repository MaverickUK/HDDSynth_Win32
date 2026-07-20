#ifndef HDDSYNTH_VERSION_H
#define HDDSYNTH_VERSION_H

// SemVer. Pre-1.0 since this hasn't been released yet -- bump PATCH for
// fixes, MINOR for feature additions, per change made to the project.
#define HDDSYNTH_VERSION_MAJOR 0
#define HDDSYNTH_VERSION_MINOR 9
#define HDDSYNTH_VERSION_PATCH 0
#define HDDSYNTH_VERSION_STRING "0.9.0"

#define HDDSYNTH_APP_NAME "HDD Synth Win32"
#define HDDSYNTH_AUTHOR "Peter Bridger"
#define HDDSYNTH_GITHUB_URL "https://github.com/MaverickUK/HDDSynth_Win32"

// HDDSYNTH_TARGET_NT is defined by the Makefile's NT_CXXFLAGS (only for
// the hddsynth-nt.exe build) -- see "Two builds, one codebase" in
// README.md. Shown in the About box so it's obvious at a glance which
// binary is actually running, since the two look and behave identically
// otherwise.
#if defined(HDDSYNTH_TARGET_NT)
#define HDDSYNTH_BUILD_NAME "Windows 2000/XP+ build"
#else
#define HDDSYNTH_BUILD_NAME "Windows 95/98/ME build"
#endif

#endif
