# CLAUDE.md

Guidance for working in this repo. See `README.md` for the full design rationale and gotcha
list — this file is the condensed set of rules to follow so future changes stay consistent
with them, not a replacement for it.

## What this project is

A Windows 95/98/ME system tray app, cross-compiled from macOS (also works on Windows 11/Linux)
using mingw-w64. There is no local way to run the built `.exe` — verification is static
(`objdump`) here, and real behavioral verification happens on the user's actual Windows 98
hardware. Always be explicit about which kind of verification you've actually done.

## Build

```sh
tools/build-pentium-crt.sh   # one-time; only re-run if the mingw-w64 formula updates
make                          # builds build/hddsynth_spike.exe and build/hddsynth.exe
make clean
```

After every change, before handing off for hardware testing:

```sh
i686-w64-mingw32-objdump -d build/hddsynth.exe > /tmp/check.txt
grep -ciE '\b(cmov[a-z]*|rdtsc|cpuid)\b' /tmp/check.txt   # must be 0
i686-w64-mingw32-objdump -p build/hddsynth.exe | grep "DLL Name"  # only Win95-era DLLs
```

Expected DLL set: `KERNEL32`, `USER32`, `GDI32`, `SHELL32`, `msvcrt`, `WINMM`, `ADVAPI32`,
`COMCTL32`. A new dependency outside this set is worth a second look before assuming it's fine.

## Hard rules (violating these breaks the build or silently breaks Win9x compatibility)

1. **`#define WIN32_LEAN_AND_MEAN` before every `#include <windows.h>`** — in `.cpp` files *and*
   `.rc` files. Without it in a `.cpp`, `windows.h` pulls in OLE/COM headers that drag in a
   libstdc++ wrapper referencing a UCRT-only symbol our CRT target doesn't have (hard compile
   error). `.rc` files need a plain `#include <windows.h>` (no `WIN32_LEAN_AND_MEAN` needed
   there) so `WS_*`/`DS_*` style macros are defined for the resource compiler.
2. **Never include `<stdlib.h>` or `<cstdlib>`, anywhere, directly or transitively.** Same
   `quick_exit`/UCRT problem as above, and it happens regardless of which spelling you use.
   Use `HeapAlloc`/`HeapFree` (Win32's process heap) instead of `malloc`/`free`. Need a random
   number? Write a small PRNG (see `mixer.cpp`) instead of `rand()`. `<stdio.h>` and
   `<string.h>` are fine.
3. **Start every thread with `_beginthreadex`/implicit `_endthreadex`, never raw
   `CreateThread`.** Any thread that touches CRT functions (`fopen`, `malloc` — even
   indirectly) needs the CRT's own per-thread init, or you get silent heap corruption/hangs
   instead of a clean crash. Just always use `_beginthreadex`; there's no upside to `CreateThread`
   here.
4. **No STL threading** (`std::thread`, `std::mutex`, etc.) — MinGW's `winpthreads` backend
   calls synchronization primitives that don't exist on Windows 95. Use plain Win32 primitives:
   `_beginthreadex`, `CRITICAL_SECTION`, `Interlocked*`.
5. **Compiler/linker flags are load-bearing, not style choices** — `-march=pentium -mtune=pentium`,
   `-mcrtdll=msvcrt-os`, `-no-pthread`, the `-B`/`-L` pointing at the Pentium-safe CRT sysroot,
   and `-static -static-libgcc -static-libstdc++`. Don't remove or "simplify" these in the
   `Makefile` without understanding why each exists (see README's Toolchain section) — several
   were added in direct response to real crashes on hardware.
6. **Never build paths from a bare relative string or assume the current working directory.**
   Use `GetExeDir`/`BuildExePath` (`src/paths.cpp`) for anything relative to the exe — CWD only
   happens to match the exe's folder when double-clicked in Explorer, and some Win32 APIs
   (`GetPrivateProfileString`) look in the Windows directory instead of the CWD for a bare
   filename regardless.

## Conventions

- **One subsystem per `.cpp`/`.h` pair**, each with a narrow, documented responsibility (audio,
  mixer, diskmon, settings, samplepack, paths, the two dialogs). Keep that separation when
  adding features rather than growing `tray.cpp` into a catch-all.
- **Settings persistence is an INI file (`hddsynth.ini`) via `GetPrivateProfileString`/
  `WritePrivateProfileString`**, not the registry — keeps the app self-contained the way
  `samples/` already is. Add new tunables to `Settings` (`settings.h`) and both directions of
  `settings.cpp`'s load/save, with a sensible default for a missing key (covers first run).
- **Sample packs are subfolders of `samples/`**, each containing exactly `spinup.wav`,
  `idle.wav`, `access.wav`. `samplepack.cpp` scans for subfolders at menu-open time — don't
  hardcode pack names anywhere.
- **Shared mutable state between the GUI thread and the audio/diskmon threads**: a single
  flag/int can use `Interlocked*` ops directly (see `g_accessActive`). Anything compound
  (multiple fields that must stay consistent with each other, e.g. the loaded WAV buffers)
  needs a `CRITICAL_SECTION` — don't reach for `Interlocked` on a struct.
- **Versioning**: SemVer in `src/version.h`, pre-1.0 (nothing has been released yet). Bump it
  with each meaningful change — PATCH for fixes, MINOR for feature additions.
- **Record hard-won gotchas in README's "Gotchas found the hard way" section**, not just in
  commit messages — several of these (CRT/threading issues especially) are easy to
  reintroduce by accident and expensive to re-debug from scratch.
- **Images for Win9x-era UI must go through a period-correct format.** Classic Win9x GDI has
  no PNG support; `BITMAP` resources have no alpha channel. Composite onto a solid background
  (see `tools/make_about_logo.py`, using the classic Win95 dialog face gray `RGB(192,192,192)`)
  and save as a classic (non-V4/V5) BMP before adding as a resource — verify with `file` that
  it reports as "Windows 3.x format", not a newer DIB header variant.
- **Don't add a new Win32 DLL dependency without checking it's Win95-era-safe** first (it
  shipped with Windows 95, or was a standard redistributable common by the Win9x era — e.g.
  `comctl32` for common controls is fine).

## Testing workflow

There's no Windows 9x environment available to Claude directly. The loop is:
1. Make the change, build, run the static `objdump` checks above.
2. Hand off `build/` (including `samples/`) to the user to copy onto real Windows 98 hardware.
3. Treat their report as the actual test result — static checks only rule out *known* bad
   patterns (wrong instructions, wrong DLLs); they don't prove the feature works.
4. When something fails on hardware, prefer adding a small, temporary diagnostic (e.g. the
   registry-enumeration logging that found the real `HKEY_DYN_DATA` counter names) over
   guessing repeatedly — guessing blind wasted multiple hardware-test round-trips earlier in
   this project.
