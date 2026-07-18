# HDDSynth-Win9x

A pure-software reimplementation of [HDDSynth](https://github.com/MaverickUK/HDDSynth) for Windows 95/98/ME.

The original HDDSynth is a Raspberry Pi Pico-based hardware device that reads a PC's
physical HDD LED wire and plays back mechanical-drive sound samples. This project drops
the hardware entirely: it's a small Windows 9x system tray application that watches for
disk activity in software and plays the same sample-based audio, so you get authentic
spinning-HDD sound (and a gray/green tray icon) on a machine that's actually running a
solid-state drive — no wiring required.

**Status**: working end-to-end, confirmed on real Windows 98 hardware.

## Behavior

- On launch: plays the spin-up sample once, then loops the idle sample continuously.
- While real disk activity is detected: mixes the access sample in on top of the idle loop,
  starting from a random point in the sample each time so repeated triggers don't sound
  identical, with a 200ms minimum play time so brief activity doesn't get clipped to a click.
- Tray icon is gray when idle, green while activity is detected.
- Ordinary background OS housekeeping I/O doesn't trigger it — only sustained transfers above
  a byte-rate threshold count as "activity" (see Design notes).
- Runs quietly in the background alongside other software — event-driven audio thread that's
  fully asleep between buffer refills, sleep-based poll loop for disk detection, no busy-
  waiting anywhere.

## Project layout

```
src/        C++ sources
res/        Icon resources (gray.ico / green.ico) + the .rc resource script
samples/    The four original WAV samples (fetched from HDDSynth's samples/original/)
tools/      tools/make_icons.py (icon generator), tools/build-pentium-crt.sh (see Toolchain)
build/      Build output (gitignored-worthy; not currently a git repo)
Makefile    Cross-compilation build rules
```

| File | Role |
|---|---|
| `src/tray.cpp` | Tray icon, context menu, window/message loop, `WinMain` |
| `src/audio.cpp` / `audio.h` | Owns the single `waveOut` stream and its dedicated refill thread |
| `src/mixer.cpp` / `mixer.h` | Software PCM mixing: spin-up → idle loop → access layering |
| `src/wav.cpp` / `wav.h` | Minimal WAV/PCM loader |
| `src/diskmon.cpp` / `diskmon.h` | Background thread polling `HKEY_DYN_DATA\PerfStats` for disk activity |
| `src/spike_main.cpp` | Standalone toolchain-validation spike (not part of `hddsynth.exe`) |

## Samples

`samples/` contains the four original HDDSynth WAV files, fetched as-is from the upstream
repo (GPL-3.0, same license as HDDSynth itself):

- `hdd_spinup.wav` — played once at startup
- `hdd_idle_long.wav` — looped while idle (the shorter `hdd_idle.wav` is also present but
  unused — it felt too repetitive in testing)
- `hdd_access.wav` — mixed in during detected activity

All four are 16-bit PCM, 16kHz; `hdd_idle_long.wav` is stereo while the others are mono — the
WAV loader downmixes everything to mono at load time so the mixer can do simple sample-by-
sample addition.

## Design notes

| Concern | Choice | Reasoning |
|---|---|---|
| Target OS | Windows 95, 98, ME | Broadest "Win9x" support; drives most constraints below. |
| Audio | Manual PCM mixing over `winmm` (`waveOutOpen`/`waveOutWrite`), one output stream | DirectSound needs DirectX installed and adds a COM dependency. `winmm` ships with every Windows since 3.1. Opening *multiple* simultaneous `waveOutOpen` handles (one per sample) is a classic Win9x trap — cards without hardware mixing (common pre-98SE) reject the second handle. Mixing the layers ourselves in software and writing to a single stream sidesteps that. |
| Buffer refills | Dedicated thread woken by a `CALLBACK_EVENT`, not `MM_WOM_DONE` on the GUI thread | The GUI-thread-message approach caused audible dropouts during heavy disk I/O — exactly when the app most needs to keep playing, since that's also when the GUI message queue is most likely to be delayed. |
| Detection scope | System-wide (any drive), not a specific drive letter | Matches the original hardware's single-LED simplicity. |
| Detection source | `HKEY_DYN_DATA\PerfStats\{StartStat,StatData,StopStat}` | Win9x has no modern disk-IO-counter API; this is the same real-time counter feed `sysmon.exe` reads. See Toolchain/gotchas for the two non-obvious things about it. |
| Detection signal | Delta between polls of `VFAT\BReadsSec`/`BWritesSec`, thresholded | These counters are cumulative totals, not rates (see gotchas) — and even a correct delta-based reading needs a minimum-bytes threshold, or ordinary OS housekeeping I/O triggers it every few seconds. |
| Language | C++, conservative subset | No STL threading (`std::thread`/`std::mutex`) — MinGW's `winpthreads` backend calls synchronization primitives that don't exist on Win95. Plain Win32 primitives (`_beginthreadex`, `CRITICAL_SECTION`/`Interlocked*`) instead. |

## Toolchain

Built on macOS (also works on Windows 11/Linux) using a cross-compiler — no Windows 9x-era
IDE or VM needed just to *build* the code, only to *run and test* it.

- **Compiler**: `mingw-w64` via Homebrew (`brew install mingw-w64`), targeting `i686-w64-mingw32`.
- **Critical flag**: `-mcrtdll=msvcrt-os`. Modern mingw-w64 (this build is GCC 16) defaults to
  linking against the Universal CRT (`api-ms-win-crt-*.dll`), which only exists on Windows 10+
  and silently fails to load on Win9x. `-mcrtdll=msvcrt-os` forces linking against the real,
  always-present `msvcrt.dll` instead.
- **CPU target**: `-march=pentium -mtune=pentium`, so the binary never emits an instruction
  the original Pentium (and thus the oldest realistic Win95 boxes) can't execute.
- **Custom Pentium-safe CRT** (`tools/build-pentium-crt.sh`): Homebrew's mingw-w64 ships its
  CRT startup objects and static support libs (`crt2.o`, `libmingw32.a`, `libmingwex.a`, ...)
  *prebuilt* for a P6+ CPU baseline, containing `cmove`/`cmovne` instructions — our own
  `-march=pentium` only affects code we compile, not these prebuilt archives. On a real/
  emulated Pentium (no CMOV) that's an illegal-instruction crash at startup, before `main` is
  even reached (this is exactly what the first real-hardware test hit). The script rebuilds
  just those pieces from the matching mingw-w64 source release with `-march=pentium`, installs
  them to `/tmp/mingw-pentium-sysroot`, and the `Makefile` prefers them via `-B`/`-L`.
- **Threading**: `-no-pthread`, since GCC otherwise links `libpthread`/`winpthreads` into every
  binary by default, whether or not the code uses threads — and that library's init path isn't
  Win95-safe.
- **Static linking**: `-static -static-libgcc -static-libstdc++`, so the only DLLs the exe
  depends on are the ones Windows itself ships (`kernel32`, `user32`, `shell32`, `msvcrt`,
  `winmm`, `advapi32`) — no GCC runtime redistributable to install on the target machine.
- **Subsystem stamping**: linker flags set the PE's subsystem/OS version to 4.0 (Win95-era).
- **Stripped (`-s`)**: no debug symbols in the shipped binary (~22KB stripped) — there's no
  debugger on the target machine to use them anyway.

One-time setup (rebuilds the Pentium-safe CRT above; only needs re-running if Homebrew updates
the `mingw-w64` formula):

```sh
tools/build-pentium-crt.sh
```

Then build with:

```sh
make            # builds build/hddsynth_spike.exe and build/hddsynth.exe
make clean
```

Verification approach throughout development: statically via `objdump -p`/`-d` on the built
`.exe` (confirms only Win95-era DLLs/symbols are imported and zero CMOV/RDTSC/CPUID
instructions anywhere), but that only rules out *known* bad patterns — actually running it on
real Windows 98 hardware is what caught everything else below.

### Gotchas found the hard way

These cost real debugging time against real hardware and are easy to reintroduce by accident,
so they're recorded here rather than just in commit history:

- **`WIN32_LEAN_AND_MEAN` is required before every `#include <windows.h>`.** Without it,
  `windows.h` pulls in the OLE/COM headers, which drag in libstdc++'s `<cstdlib>` wrapper —
  and that wrapper unconditionally references `quick_exit`, a UCRT-only symbol not declared
  anywhere once `-mcrtdll=msvcrt-os` is in effect. Hard compile error otherwise.
- **`<stdlib.h>`/`<cstdlib>` are unusable anywhere in this codebase**, not just via
  `windows.h` — the same `quick_exit` error resurfaces from a *direct* include too, since it's
  libstdc++'s C++ wrapper header doing this regardless of which name you spell. Use
  `HeapAlloc`/`HeapFree` instead of `malloc`/`free`, and a hand-rolled PRNG instead of `rand()`
  (see `mixer.cpp`). `<stdio.h>` and `<string.h>` are fine — the problem is specific to
  `quick_exit`/`at_quick_exit`.
- **Any thread that touches CRT functions must be started with `_beginthreadex`, not
  `CreateThread`.** The CRT keeps per-thread state (`errno`, stdio buffers, ...) that only gets
  initialized through `_beginthreadex`'s startup path; skipping it is a source of silent heap
  corruption or hangs rather than a clean crash. This caused a real, confusing failure during
  development (audio and disk-monitor logging both went silent at once, no crash dialog) before
  being traced back to this.
- **`MM_WOM_DONE`'s parameters**: `wParam` is the `HWAVEOUT`, `lParam` is the finished
  `WAVEHDR*` — not the other way round. (Moot now since audio uses `CALLBACK_EVENT` instead of
  window messages, but worth knowing if `CALLBACK_WINDOW` comes back for any reason.)
- **`HKEY_DYN_DATA\PerfStats` counter names are not guessable from documentation** — they're
  discoverable at `HKLM\System\CurrentControlSet\Control\PerfStats\Enum\<Object>`, as
  *subkeys*, not values, and with no spaces or "/Second" suffixes (e.g. `VFAT\BReadsSec`, not
  `VFAT\Bytes Read/Second`). Confirming this took building a diagnostic that enumerated the
  registry directly on real Windows 98 rather than guessing further.
- **Those counters are cumulative totals despite the "Sec" in their name**, not pre-computed
  rates — raw reads climb monotonically forever. "Value is nonzero" is true from the very
  first read onward and useless as a signal; only the *delta* between consecutive polls
  indicates whether anything happened in between, and even that needs a minimum-bytes
  threshold or ordinary background I/O reads as constant "activity".

## Open items

- **Large file copies can freeze the whole Windows UI for a few seconds** (not just this app —
  audio, GDI/USER, everything). This persisted even after ruling out this app's own thread
  priority as the cause (rolled back to normal, no change) and while the disk monitor was
  provably idle throughout. Most likely a system-level PIO-vs-DMA disk transfer issue on the
  test machine, not something this app can fix — worth checking Control Panel → System →
  Performance tab and Device Manager → disk → DMA setting. Set aside per user request; a small
  batch of smaller file copies showed no such freeze.
- **Toolchain longevity**: this relies on Homebrew's current mingw-w64 (GCC 16) working with
  `-mcrtdll=msvcrt-os`, which isn't its primary supported configuration. If deeper issues
  surface with more Win32 API surface, the fallback is an older/legacy toolchain (classic
  MinGW.org line, or TDM-GCC 4.7.1/4.9.2).
- **No installer/packaging yet** — currently just a folder (`hddsynth.exe` + `samples/`)
  copied onto the target machine; `hddsynth.exe` expects `samples\` alongside it.
