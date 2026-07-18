CXX := i686-w64-mingw32-g++
WINDRES := i686-w64-mingw32-windres

# Conservative Win9x target: restrict API surface to Win95-era Windows/IE,
# target original Pentium instruction set, avoid winpthreads (Win95 lacks
# the newer sync primitives it depends on), statically link the runtime so
# no extra DLLs are needed on the target machine, and stamp the PE with an
# old subsystem version.
#
# -mcrtdll=msvcrt-os forces linking against the classic msvcrt.dll instead
# of this GCC's default UCRT api-ms-win-crt-*.dll forwarders. UCRT doesn't
# exist before Windows 10 even with the redistributable installed (it
# doesn't support XP/2000 at all, let alone 9x), so this flag is not
# Win9x-specific -- both build targets below use it.
CXXFLAGS := -O2 -march=pentium -mtune=pentium -mcrtdll=msvcrt-os \
            -DWINVER=0x0400 -D_WIN32_WINDOWS=0x0400 -D_WIN32_WINNT=0x0400 -D_WIN32_IE=0x0300 \
            -fno-exceptions -fno-rtti
# Homebrew's mingw-w64 CRT objects/static libs (crt2.o, libmingw32.a, libmingwex.a, ...)
# are prebuilt for a P6+ baseline and contain CMOV instructions, regardless of our own
# -march=pentium -- that flag only affects code WE compile, not these prebuilt archives.
# On a real/emulated Pentium (no CMOV) that's an illegal-instruction crash at startup,
# before main() is even reached. PENTIUM_CRT is our own rebuild of just those pieces
# (see tools/build-pentium-crt.sh), and -B/-L here make the linker prefer it. Only the
# Win9x target needs this -- Windows 2000/XP's minimum supported CPUs are already P6+.
PENTIUM_CRT := /tmp/mingw-pentium-sysroot/lib

# -s strips symbols: this is a background app that needs to stay light,
# and debug symbols aren't useful on the target machine anyway.
LDFLAGS  := -mwindows -no-pthread -mcrtdll=msvcrt-os -static -static-libgcc -static-libstdc++ -s \
            -B$(PENTIUM_CRT) -L$(PENTIUM_CRT) \
            -Wl,--major-subsystem-version,4 -Wl,--minor-subsystem-version,0 \
            -Wl,--major-os-version,4 -Wl,--minor-os-version,0

# Windows 2000/XP+ target (see src/diskmon_nt.cpp). Same -mcrtdll=msvcrt-os
# reasoning as above; drops -march=pentium and the Pentium-safe CRT since
# those OSes' minimum supported CPUs already have CMOV, so Homebrew's
# stock mingw-w64 CRT objects are safe to link as-is. Subsystem/OS version
# stamped 5.0 (Windows 2000) rather than 5.1 (XP-only) so the same binary
# satisfies both, per the "2000/XP+" scope this target is meant to cover.
NT_CXXFLAGS := -O2 -mcrtdll=msvcrt-os \
               -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -D_WIN32_IE=0x0600 -DHDDSYNTH_TARGET_NT \
               -fno-exceptions -fno-rtti
NT_LDFLAGS  := -mwindows -no-pthread -mcrtdll=msvcrt-os -static -static-libgcc -static-libstdc++ -s \
               -Wl,--major-subsystem-version,5 -Wl,--minor-subsystem-version,0 \
               -Wl,--major-os-version,5 -Wl,--minor-os-version,0

BUILD := build

.PHONY: all spike hddsynth nt clean
all: spike hddsynth

spike: $(BUILD)/hddsynth_spike.exe

hddsynth: $(BUILD)/hddsynth.exe

nt: $(BUILD)/hddsynth-nt.exe

$(BUILD)/hddsynth_spike.exe: src/spike_main.cpp
	mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) -lshell32 -luser32 -lgdi32 -lkernel32

$(BUILD)/hddsynth.res.o: res/hddsynth.rc res/gray.ico res/green.ico res/hddsynthlogo.bmp res/hddsynth.manifest src/resource.h
	mkdir -p $(BUILD)
	$(WINDRES) -I src $< -O coff -o $@

# Shared application logic -- see src/diskmon.h: the only file that
# differs per OS family is the disk-activity monitor implementation.
COMMON_SRCS := src/tray.cpp src/audio.cpp src/mixer.cpp src/wav.cpp \
               src/paths.cpp src/samplepack.cpp src/settings.cpp \
               src/about_dialog.cpp src/settings_dialog.cpp
COMMON_HDRS := src/audio.h src/mixer.h src/wav.h src/diskmon.h src/paths.h \
               src/samplepack.h src/settings.h src/about_dialog.h src/settings_dialog.h \
               src/version.h src/resource.h

HDDSYNTH_SRCS := $(COMMON_SRCS) src/diskmon.cpp
HDDSYNTH_NT_SRCS := $(COMMON_SRCS) src/diskmon_nt.cpp

$(BUILD)/hddsynth.exe: $(HDDSYNTH_SRCS) $(COMMON_HDRS) $(BUILD)/hddsynth.res.o
	mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(HDDSYNTH_SRCS) $(BUILD)/hddsynth.res.o -o $@ $(LDFLAGS) -lshell32 -luser32 -lgdi32 -lkernel32 -lwinmm -ladvapi32 -lcomctl32

$(BUILD)/hddsynth-nt.exe: $(HDDSYNTH_NT_SRCS) $(COMMON_HDRS) $(BUILD)/hddsynth.res.o
	mkdir -p $(BUILD)
	$(CXX) $(NT_CXXFLAGS) $(HDDSYNTH_NT_SRCS) $(BUILD)/hddsynth.res.o -o $@ $(NT_LDFLAGS) -lshell32 -luser32 -lgdi32 -lkernel32 -lwinmm -lcomctl32 -lpdh

clean:
	rm -rf $(BUILD)
