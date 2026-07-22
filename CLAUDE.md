# CLAUDE.md

Guidance for working in this repo. See `README.md` for the full design rationale and gotcha
list — this file is the condensed set of rules to follow so future changes stay consistent
with them, not a replacement for it.

## What this project is

A single Windows system tray app (`hddsynth.exe`), cross-compiled from macOS (also works on
Windows 11/Linux) using mingw-w64, that runs on both Windows 95/98/ME and Windows 2000/XP+ from
one binary. This used to be two separate builds (`hddsynth.exe`/`hddsynth-nt.exe`) differing only
in the disk-activity monitor; that seam is now a runtime dispatch inside a single `diskmon.cpp`
(`GetVersionExA` picks the Win9x `HKEY_DYN_DATA` path or the NT-family PDH path — see README's
"One build, runtime dispatch"), built throughout with the conservative Win9x-safe compiler/linker
flags, which are a strict subset of what NT-family Windows needs. There is no local way to run
the built `.exe` — verification is static (`objdump`) here. Real behavioral verification happens
on the user's actual Windows 98 hardware for the Win9x path; the NT-family (PDH) path was
confirmed working on real Windows XP SP3 hardware via a user report **when it was still a
separate `hddsynth-nt.exe` build** — that confirmation does not automatically carry over to this
binary's merged, runtime-dispatched form, so **there is still no NT-family hardware/VM available
in this environment** to re-verify it, and any *new* change touching the NT-family path in
`diskmon.cpp` remains unconfirmed regardless of how clean the build looks until it's actually
tested on real hardware. Always be explicit about which kind of verification you've actually
done, and for which OS path.

## Build

```sh
tools/build-pentium-crt.sh   # one-time; only re-run if the mingw-w64 formula updates
make                          # builds build/hddsynth_spike.exe and build/hddsynth.exe
make clean
```

After every change, before handing off for testing:

```sh
i686-w64-mingw32-objdump -d build/hddsynth.exe > /tmp/check.txt
grep -ciE '\b(cmov[a-z]*|rdtsc|cpuid)\b' /tmp/check.txt   # must be 0 -- the whole binary is built
                                                            # Win9x-safe, since that instruction
                                                            # set is a strict subset of what NT
                                                            # needs too (see README's Toolchain)
i686-w64-mingw32-objdump -p build/hddsynth.exe | grep "DLL Name"
```

Expected DLL set: `KERNEL32`, `USER32`, `GDI32`, `SHELL32`, `msvcrt`, `WINMM`, `ADVAPI32`,
`COMCTL32`. **No `PDH`** should appear here — the NT-family disk-activity path loads `pdh.dll`
dynamically via `LoadLibraryA`/`GetProcAddress` at runtime instead of a static import, precisely
so its absence on Win9x doesn't stop the exe loading there at all; `PDH` showing up in this list
would mean that dynamic-loading discipline was broken. A new dependency outside the expected set
is worth a second look before assuming it's fine.

## Hard rules (violating these breaks the build or silently breaks compatibility)

Rules 1-4 are consequences of targeting classic `msvcrt.dll` via `-mcrtdll=msvcrt-os` (UCRT
doesn't exist on XP/2000 either, not just Win9x), so these rules apply to every file in `src/`.

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
   `-no-pthread`, `-static -static-libgcc -static-libstdc++`, `-march=pentium -mtune=pentium`,
   and the `-B`/`-L` pointing at the Pentium-safe CRT sysroot all apply to the one `CXXFLAGS`/
   `LDFLAGS` pair in the `Makefile` — there's only one build now, and the Pentium-safe flags are
   what let it cover NT-family Windows too (see README's Toolchain section for why that's safe).
   Don't remove or "simplify" any of these without understanding why each exists — several were
   added in direct response to real crashes on hardware.
6. **Never build paths from a bare relative string or assume the current working directory.**
   Use `GetExeDir`/`BuildExePath` (`src/paths.cpp`) for anything relative to the exe — CWD only
   happens to match the exe's folder when double-clicked in Explorer, and some Win32 APIs
   (`GetPrivateProfileString`) look in the Windows directory instead of the CWD for a bare
   filename regardless.
7. **OS-family-specific code belongs behind `diskmon.h`'s interface, nowhere else.** That's the
   one deliberate seam between the two OS paths, resolved at runtime inside `diskmon.cpp` (see
   README's "One build, runtime dispatch") rather than by separate builds. If a future change
   needs OS-version-specific behavior anywhere else, that's a sign the seam needs to move/widen,
   not a reason to sprinkle `GetVersionExA`/`#ifdef` checks through shared files like `tray.cpp`
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
- **Releasing**: bump `src/version.h`, commit, then run `tools/release.sh` — it builds the
  binary, runs the static safety checks, packages `build/hddsynth.exe` + `samples/` into a zip,
  generates release notes from commit history (see above), tags, pushes, and creates the GitHub
  release via `gh`. Requires `gh auth login` once beforehand.
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

There's no Windows environment available to Claude directly for either OS family. The loop for
the Win9x path is:
1. Make the change, build, run the static `objdump` checks above.
2. Hand off `build/` (including `samples/`) to the user to copy onto real Windows 98 hardware.
3. Treat their report as the actual test result — static checks only rule out *known* bad
   patterns (wrong instructions, wrong DLLs); they don't prove the feature works.
4. When something fails on hardware, prefer adding a small, temporary diagnostic (e.g. the
   registry-enumeration logging that found the real `HKEY_DYN_DATA` counter names) over
   guessing repeatedly — guessing blind wasted multiple hardware-test round-trips earlier in
   this project.

For the NT-family path (the PDH branch inside `diskmon.cpp`), there's no hardware/VM in this
environment to hand off to directly. A user confirmed the PDH-based disk-activity detection
working on real Windows XP SP3 hardware, but that was **before** the merge into this single
runtime-dispatched binary, back when it shipped as the separate `hddsynth-nt.exe` build — treat
that confirmation as historical context for the underlying PDH logic, not as verification of the
merged binary itself. For any change touching the NT-family path, don't imply it works (e.g.
don't say a change to the PDH branch "works," only that it "builds cleanly") until it's actually
been tested on real Windows 2000/XP hardware in this new single-binary form.
