# CLAUDE.md

Guidance for working in this repo. See `README.md` for the full design rationale and gotcha
list — this file is the condensed set of rules to follow so future changes stay consistent
with them, not a replacement for it.

## What this project is

Two Windows system tray apps built from one codebase, cross-compiled from macOS (also works on
Windows 11/Linux) using mingw-w64: `hddsynth.exe` (Windows 95/98/ME) and `hddsynth-nt.exe`
(Windows 2000/XP+). They share every source file except the disk-activity monitor
(`diskmon.cpp` vs `diskmon_nt.cpp`, both implementing `diskmon.h` — see README's "Two builds,
one codebase"). There is no local way to run either built `.exe` — verification is static
(`objdump`) here. Real behavioral verification happens on the user's actual Windows 98 hardware
for the Win9x build; **there is currently no hardware/VM to verify `hddsynth-nt.exe` against at
all**, so treat anything about its runtime behavior as unconfirmed regardless of how clean the
build looks. Always be explicit about which kind of verification you've actually done, and for
which target.

## Build

```sh
tools/build-pentium-crt.sh   # one-time; only re-run if the mingw-w64 formula updates; Win9x only
make                          # builds build/hddsynth_spike.exe and build/hddsynth.exe (Win9x)
make nt                       # builds build/hddsynth-nt.exe (Windows 2000/XP+)
make clean
```

After every change, before handing off for testing (repeat for whichever target(s) you touched):

```sh
i686-w64-mingw32-objdump -d build/hddsynth.exe > /tmp/check.txt      # or hddsynth-nt.exe
grep -ciE '\b(cmov[a-z]*|rdtsc|cpuid)\b' /tmp/check.txt   # must be 0 for hddsynth.exe (Win9x
                                                            # target only -- hddsynth-nt.exe is
                                                            # allowed CMOV/etc., its min-spec CPUs
                                                            # already have them)
i686-w64-mingw32-objdump -p build/hddsynth.exe | grep "DLL Name"
```

Expected DLL set for `hddsynth.exe` (Win9x): `KERNEL32`, `USER32`, `GDI32`, `SHELL32`, `msvcrt`,
`WINMM`, `ADVAPI32`, `COMCTL32`. For `hddsynth-nt.exe`: the same, but `ADVAPI32` replaced by
`PDH` (no registry calls in `diskmon_nt.cpp`). A new dependency outside these sets is worth a
second look before assuming it's fine.

## Hard rules (violating these breaks the build or silently breaks compatibility)

Rules 1-4 are consequences of targeting classic `msvcrt.dll` via `-mcrtdll=msvcrt-os` — that
choice applies to **both** build targets (UCRT doesn't exist on XP/2000 either, not just Win9x),
so these rules apply to every file in `src/`, regardless of which `.exe` it ends up in.

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
   calls synchronization primitives that don't exist on Windows 95, and this codebase avoids it
   uniformly rather than diverging per target. Use plain Win32 primitives: `_beginthreadex`,
   `CRITICAL_SECTION`, `Interlocked*`.
5. **Compiler/linker flags are load-bearing, not style choices.** `-mcrtdll=msvcrt-os`,
   `-no-pthread`, and `-static -static-libgcc -static-libstdc++` apply to **both**
   `CXXFLAGS`/`LDFLAGS` and `NT_CXXFLAGS`/`NT_LDFLAGS` in the `Makefile` for the reason above.
   `-march=pentium -mtune=pentium` and the `-B`/`-L` pointing at the Pentium-safe CRT sysroot are
   **Win9x-only** (`hddsynth-nt.exe` doesn't need them — see README's Toolchain section for why).
   Don't remove or "simplify" any of these without understanding why each exists — several were
   added in direct response to real crashes on hardware.
6. **Never build paths from a bare relative string or assume the current working directory.**
   Use `GetExeDir`/`BuildExePath` (`src/paths.cpp`) for anything relative to the exe — CWD only
   happens to match the exe's folder when double-clicked in Explorer, and some Win32 APIs
   (`GetPrivateProfileString`) look in the Windows directory instead of the CWD for a bare
   filename regardless.
7. **OS-family-specific code belongs behind `diskmon.h`'s interface, nowhere else.** That's the
   one deliberate seam between the two build targets (see README's "Two builds, one codebase").
   If a future change needs OS-version-specific behavior anywhere else, that's a sign the seam
   needs to move/widen, not a reason to sprinkle `#ifdef`s through shared files like `tray.cpp`
   or `mixer.cpp`.

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
- **Versioning**: SemVer in `src/version.h`, pre-1.0. Bump it with each meaningful change —
  PATCH for fixes, MINOR for feature additions. Tagged releases exist from 0.3.0 onward.
- **One logical change per commit, each with its own `feat:`/`fix:` subject** — don't bundle a
  feature and a fix (or two unrelated fixes) into a single commit. `tools/release.sh` groups
  commits since the last tag into Features/Fixes/Other sections by subject prefix when
  publishing a release, one line per commit — a bundled commit collapses into a single bullet
  under whichever prefix it happened to start with, silently dropping the rest from the notes.
  This bit us once already (an Apply-button feature + a balance-mute fix landed in one commit
  and one release-notes line — see the v0.5.0 release). If a change naturally splits into
  independent pieces, commit and (if releasing) it's fine to have several `feat:`/`fix:` commits
  land in the same release; what matters is each commit subject accurately describing that
  commit's one change, not spreading unrelated work across releases.
- **Releasing**: bump `src/version.h`, commit, then run `tools/release.sh` — it builds both
  targets, runs the static safety checks, packages `build/hddsynth.exe` +
  `build/hddsynth-nt.exe` + `samples/` into a zip, generates release notes from commit history
  (see above), tags, pushes, and creates the GitHub release via `gh`. Requires `gh auth login`
  once beforehand.
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

There's no Windows environment available to Claude directly for either target. The loop for the
Win9x build (`hddsynth.exe`) is:
1. Make the change, build, run the static `objdump` checks above.
2. Hand off `build/` (including `samples/`) to the user to copy onto real Windows 98 hardware.
3. Treat their report as the actual test result — static checks only rule out *known* bad
   patterns (wrong instructions, wrong DLLs); they don't prove the feature works.
4. When something fails on hardware, prefer adding a small, temporary diagnostic (e.g. the
   registry-enumeration logging that found the real `HKEY_DYN_DATA` counter names) over
   guessing repeatedly — guessing blind wasted multiple hardware-test round-trips earlier in
   this project.

For `hddsynth-nt.exe`, step 2 currently has no hardware/VM to hand off to — there's no real
verification loop for it at all yet, only the static checks. Don't imply otherwise (e.g. don't
say a change to `diskmon_nt.cpp` "works," only that it "builds cleanly") until someone actually
runs it on Windows 2000/XP.
