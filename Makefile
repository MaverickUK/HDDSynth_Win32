CXX := i686-w64-mingw32-g++
WINDRES := i686-w64-mingw32-windres

# Single build target covering both Windows 9x/ME and Windows 2000/XP+:
# restrict API surface to Win95-era Windows/IE, target original Pentium
# instruction set, avoid winpthreads (Win95 lacks the newer sync
# primitives it depends on), statically link the runtime so no extra DLLs
# are needed on the target machine, and stamp the PE with an old subsystem
# version. None of this excludes NT-family Windows: Pentium-safe code (no
# CMOV/RDTSC/CPUID) runs fine on the P6+ CPUs NT requires too, since
# they're a superset, and a subsystem/OS version stamped 4.0 is honored by
# every later Windows release as well. See src/diskmon.cpp for the one
# place that still needs to tell the two OS families apart -- it does so
# at runtime (GetVersionExA), not via separate builds.
#
# -mcrtdll=msvcrt-os forces linking against the classic msvcrt.dll instead
# of this GCC's default UCRT api-ms-win-crt-*.dll forwarders. UCRT doesn't
# exist before Windows 10 even with the redistributable installed (it
# doesn't support XP/2000 at all, let alone 9x).
CXXFLAGS := -O2 -march=pentium -mtune=pentium -mcrtdll=msvcrt-os \
            -DWINVER=0x0400 -D_WIN32_WINDOWS=0x0400 -D_WIN32_WINNT=0x0400 -D_WIN32_IE=0x0300 \
            -fno-exceptions -fno-rtti
# Homebrew's mingw-w64 CRT objects/static libs (crt2.o, libmingw32.a, libmingwex.a, ...)
# are prebuilt for a P6+ baseline and contain CMOV instructions, regardless of our own
# -march=pentium -- that flag only affects code WE compile, not these prebuilt archives.
# On a real/emulated Pentium (no CMOV) that's an illegal-instruction crash at startup,
# before main() is even reached. PENTIUM_CRT is our own rebuild of just those pieces
# (see tools/build-pentium-crt.sh), and -B/-L here make the linker prefer it.
PENTIUM_CRT := /tmp/mingw-pentium-sysroot/lib

# -s strips symbols: this is a background app that needs to stay light,
# and debug symbols aren't useful on the target machine anyway.
LDFLAGS  := -mwindows -no-pthread -mcrtdll=msvcrt-os -static -static-libgcc -static-libstdc++ -s \
            -B$(PENTIUM_CRT) -L$(PENTIUM_CRT) \
            -Wl,--major-subsystem-version,4 -Wl,--minor-subsystem-version,0 \
            -Wl,--major-os-version,4 -Wl,--minor-os-version,0

BUILD := build

.PHONY: all spike hddsynth clean
all: spike hddsynth

spike: $(BUILD)/hddsynth_spike.exe

hddsynth: $(BUILD)/hddsynth.exe

$(BUILD)/hddsynth_spike.exe: src/spike_main.cpp
	mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) -lshell32 -luser32 -lgdi32 -lkernel32

$(BUILD)/hddsynth.res.o: res/hddsynth.rc res/gray.ico res/green.ico res/hddsynthlogo.bmp res/hddsynth.manifest src/resource.h
	mkdir -p $(BUILD)
	$(WINDRES) -I src $< -O coff -o $@

# Shared application logic -- see src/diskmon.h/diskmon.cpp: the disk-
# activity monitor tells the two OS families apart at runtime, not via
# separate source files or builds.
COMMON_SRCS := src/tray.cpp src/audio.cpp src/audio_waveout.cpp src/audio_dsound.cpp \
               src/mixer.cpp src/wav.cpp src/diskmon.cpp \
               src/paths.cpp src/samplepack.cpp src/settings.cpp src/autostart.cpp \
               src/about_dialog.cpp src/settings_dialog.cpp
COMMON_HDRS := src/audio.h src/audio_backend.h src/mixer.h src/wav.h src/diskmon.h src/paths.h \
               src/samplepack.h src/settings.h src/autostart.h src/about_dialog.h \
               src/settings_dialog.h src/version.h src/resource.h

$(BUILD)/hddsynth.exe: $(COMMON_SRCS) $(COMMON_HDRS) $(BUILD)/hddsynth.res.o
	mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(COMMON_SRCS) $(BUILD)/hddsynth.res.o -o $@ $(LDFLAGS) -lshell32 -luser32 -lgdi32 -lkernel32 -lwinmm -ladvapi32 -lcomctl32

clean:
	rm -rf $(BUILD)
