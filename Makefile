CXX := i686-w64-mingw32-g++
WINDRES := i686-w64-mingw32-windres

# Conservative Win9x target: restrict API surface to Win95-era Windows/IE,
# target original Pentium instruction set, avoid winpthreads (Win95 lacks
# the newer sync primitives it depends on), statically link the runtime so
# no extra DLLs are needed on the target machine, and stamp the PE with an
# old subsystem version.
#
# -mcrtdll=msvcrt-os forces linking against the classic msvcrt.dll instead
# of this GCC's default UCRT api-ms-win-crt-*.dll forwarders, which only
# exist on Windows 10+ and would silently fail to load on Win9x.
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

.PHONY: all spike clean
all: spike hddsynth

spike: $(BUILD)/hddsynth_spike.exe

hddsynth: $(BUILD)/hddsynth.exe

$(BUILD)/hddsynth_spike.exe: src/spike_main.cpp
	mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) -lshell32 -luser32 -lgdi32 -lkernel32

$(BUILD)/hddsynth.res.o: res/hddsynth.rc res/gray.ico res/green.ico src/resource.h
	mkdir -p $(BUILD)
	$(WINDRES) -I src $< -O coff -o $@

HDDSYNTH_SRCS := src/tray.cpp src/audio.cpp src/mixer.cpp src/wav.cpp src/diskmon.cpp

$(BUILD)/hddsynth.exe: $(HDDSYNTH_SRCS) src/audio.h src/mixer.h src/wav.h src/diskmon.h $(BUILD)/hddsynth.res.o
	mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(HDDSYNTH_SRCS) $(BUILD)/hddsynth.res.o -o $@ $(LDFLAGS) -lshell32 -luser32 -lgdi32 -lkernel32 -lwinmm -ladvapi32

clean:
	rm -rf $(BUILD)
