# HDDSynth-Win32

A pure-software reimplementation of [HDDSynth](https://github.com/MaverickUK/HDDSynth) for
32-bit Windows, from Windows 95 through XP.

The original HDDSynth is a Raspberry Pi Pico-based hardware device I built that reads a PC's
physical HDD LED wire and plays back mechanical-drive sound samples. I wanted a version that
drops the hardware entirely: a small Windows system tray application that watches for disk
activity in software and plays the same sample-based audio, so you get authentic spinning-HDD
sound (and a gray/green tray icon) on a machine that's actually running a solid-state drive —
no wiring required.

This was built with [Claude Code](https://claude.com/claude-code) — developed on macOS via
cross-compilation and verified in rounds against my real Windows 98 hardware.

**Status**: two builds from one codebase.
- **`hddsynth.exe`** (Windows 95/98/ME) — working end-to-end, confirmed on real Windows 98
  hardware, through many rounds of real-hardware testing (see Gotchas below).
- **`hddsynth-nt.exe`** (Windows 2000/XP+) — confirmed working on real Windows XP SP3 hardware.
  I still don't have any NT-family hardware/VM of my own to test against, so this build's
  verification loop relies on a user confirming behavior on their own machine rather than the
  round-trip testing `hddsynth.exe` has been through — see "Two builds, one codebase" below.

Versioned with SemVer (`src/version.h`) — pre-1.0 since this hasn't been released yet; I bump
it with each change.

## Demo video
[![Demo video](https://img.youtube.com/vi/oLoJ0eOow08/maxresdefault.jpg)](https://www.youtube.com/watch?v=oLoJ0eOow08)

## Behavior

- On launch: plays the spin-up sample once, then loops the idle sample continuously.
- While real disk activity is detected: mixes the access sample in on top of the idle loop,
  starting from a random point in the sample each time so repeated triggers don't sound
  identical, with a configurable minimum play time (default 200ms) so brief activity doesn't
  get clipped to a click.
- Tray icon is gray when idle, green while activity is detected.
- Ordinary background OS housekeeping I/O doesn't trigger it — only sustained transfers above
  a configurable byte-rate threshold count as "activity".
- Right-click the tray icon for:
  - **Sample** — a submenu listing every subfolder under `samples\` (each a self-contained
    sample pack); picking one switches live, no restart needed.
  - **Settings...** — volume, idle/activity balance (0 = idle only, 100 = access only, 50 =
    even), audio buffering (see note below), minimum access playback time, and the activity
    detection threshold, persisted to `hddsynth.ini` next to the exe. An Apply button commits
    changes live without closing the dialog, for quickly trying several settings in a row
    against the same real activity (e.g. a large background copy) — Cancel afterward doesn't
    undo whatever was already Applied, same as any other Win32 dialog with an Apply button.
  - **About...** — Win9x-style about box with the project logo, version, which build you're
    running (Windows 95/98/ME vs Windows 2000/XP+), author, and a link to the project page.
  - **Exit**.
- Runs quietly in the background alongside other software — event-driven audio thread that's
  fully asleep between buffer refills, sleep-based poll loop for disk detection, no busy-
  waiting anywhere.

### A note on dropouts during heavy disk operations

Under sustained, heavy disk activity — a Scandisk surface scan is the clearest example I've
hit — the audio can still cut out briefly even though normal use (copying files, everyday
activity) works fine. This is very likely PIO-mode disk transfer (no DMA) pegging the CPU
enough to delay the sound driver's own completion signaling, which no thread priority on this
app's side can work around — only having enough audio already queued up to ride out the stall
helps, and that directly costs latency (see **Audio buffering** in Settings). There's no
setting that eliminates this entirely without reintroducing the multi-second lag I started
with; the slider just lets you pick a point on that tradeoff rather than being stuck with mine.
If a particular machine hits this a lot, check whether Windows considers its disk to be running
in DMA mode (see Open items below) — that's the actual underlying cause, not something this app
can fix.

## Two builds, one codebase

`hddsynth.exe` (Win9x) and `hddsynth-nt.exe` (Windows 2000/XP+) share every line of application
logic — tray icon, mixer, WAV loading, settings, dialogs, sample packs (`src/tray.cpp` through
`src/paths.cpp` below). The **only** file that differs is the disk-activity monitor, because
Win9x and NT expose completely different (and non-overlapping) APIs for it:

- `src/diskmon.cpp` (Win9x) polls `HKEY_DYN_DATA\PerfStats`, a Win9x/ME-only pseudo-registry
  with undocumented counter names and counters that are cumulative totals rather than rates
  (see Gotchas).
- `src/diskmon_nt.cpp` (2000/XP+) uses PDH (Performance Data Helper, `pdh.dll` — shipped with
  every NT-family Windows since NT4) to read `\PhysicalDisk(_Total)\Disk Bytes/sec`, which is
  documented and already a rate — no delta-tracking needed, though it still gets thresholded the
  same way to filter background noise.

Both implement the same `src/diskmon.h` interface (`StartDiskActivityMonitor`/
`StopDiskActivityMonitor`/`SetDiskActivityThreshold`, posting `WM_DISKACTIVITY`), so nothing
else in the app knows or cares which OS family it's running on. The `Makefile` picks the right
one per target — see `make nt` under Toolchain.

I don't have Windows 2000/XP hardware (or even a VM) of my own, so `hddsynth-nt.exe` doesn't get
the same round-trip real-hardware testing loop `hddsynth.exe` has been through — its
verification has come from a user confirming it directly on real Windows XP SP3 hardware
(tray/audio/dialogs working as expected, PDH-based disk-activity detection reacting to real
activity). One specific known gap remains: `PdhAddCounterA` takes the *localized* counter path
("PhysicalDisk"/"Disk Bytes/sec" are the English names) and will fail to resolve on a
non-English Windows install; the locale-independent fix (looking counters up by numeric index
via `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Perflib`) isn't implemented.

## Project layout

```
src/        C++ sources
res/        Icons, the About logo, the .rc resource script + dialog templates
samples/    Sample packs -- one subfolder per pack, e.g. samples/original/
tools/      Icon/logo generation scripts, tools/build-pentium-crt.sh (see Toolchain)
build/      Build output (gitignored)
Makefile    Cross-compilation build rules
```

| File | Role |
|---|---|
| `src/tray.cpp` | Tray icon, context menu, window/message loop, `WinMain` |
| `src/audio.cpp` / `audio.h` | Owns the single `waveOut` stream and its dedicated refill thread |
| `src/mixer.cpp` / `mixer.h` | Software PCM mixing: spin-up → idle loop → access layering, volume/balance, live pack swaps |
| `src/wav.cpp` / `wav.h` | Minimal WAV/PCM loader |
| `src/diskmon.cpp` / `diskmon.h` | Win9x disk-activity monitor (`HKEY_DYN_DATA\PerfStats`) + the shared interface header |
| `src/diskmon_nt.cpp` | Windows 2000/XP+ disk-activity monitor (PDH) — same interface, only one of the two is linked in per build |
| `src/settings.cpp` / `settings.h` | `hddsynth.ini` load/save |
| `src/autostart.cpp` / `.h` | "Run at Windows Startup" via the per-user registry Run key |
| `src/settings_dialog.cpp` / `.h` | Settings dialog (volume/balance sliders, threshold/min-playback edits) |
| `src/about_dialog.cpp` / `.h` | About box |
| `src/samplepack.cpp` / `.h` | Scans `samples\` for pack subfolders, builds per-pack WAV paths |
| `src/paths.cpp` / `.h` | Resolves paths relative to the exe's own directory, not the CWD |
| `src/version.h` | SemVer + app name/author/GitHub URL, shown in the About box |
| `src/spike_main.cpp` | Standalone toolchain-validation spike (not part of either real build) |

## Samples

`samples/` holds one subfolder per **sample pack** — the Sample submenu lists whatever
subfolders it finds, so adding a pack is just adding a folder. Each pack must contain exactly
three files:

- `spinup.wav` — played once at startup (not replayed on a pack switch — the drive is already
  "spun up")
- `idle.wav` — looped while idle
- `access.wav` — mixed in during detected activity

`samples/original/` ships with the four original HDDSynth WAVs (GPL-3.0, same license as
HDDSynth itself) renamed to that convention — I used the longer of the two original idle loops
(`hdd_idle_long.wav`) as `idle.wav`, since the shorter one felt too repetitive; that shorter
file isn't used. All three are 16-bit PCM; `idle.wav` happens to be stereo while the others are
mono, but that's not a requirement — the WAV loader downmixes everything to mono at load time,
and a differing sample rate is handled too (the `waveOut` device is reopened if a pack's rate
doesn't match the one currently playing).

## Design notes

| Concern | Choice | Reasoning |
|---|---|---|
| Target OS | Windows 95/98/ME (`hddsynth.exe`) and Windows 2000/XP+ (`hddsynth-nt.exe`), one codebase | Broadest realistic 32-bit Windows range. Only the disk-activity monitor differs per OS family (see "Two builds, one codebase" above) — everything else is shared unmodified. |
| Audio | Manual PCM mixing over `winmm` (`waveOutOpen`/`waveOutWrite`), one output stream | DirectSound needs DirectX installed and adds a COM dependency. `winmm` ships with every Windows since 3.1. Opening *multiple* simultaneous `waveOutOpen` handles (one per sample) is a classic Win9x trap — cards without hardware mixing (common pre-98SE) reject the second handle. Mixing the layers myself in software and writing to a single stream sidesteps that. |
| Buffer refills | Dedicated thread woken by a `CALLBACK_EVENT`, not `MM_WOM_DONE` on the GUI thread | The GUI-thread-message approach caused audible dropouts during heavy disk I/O — exactly when the app most needs to keep playing, since that's also when the GUI message queue is most likely to be delayed. |
| Buffer depth | User-configurable (`audioBufferMs`, default 750ms; ~256ms-2048ms range), not a fixed constant | A buffer's content is fixed at generation time and has to wait behind whatever's already queued ahead of it — so total buffer depth is *both* the worst-case lag before a change is heard *and* how much of a driver/CPU stall (e.g. a PIO-mode disk transfer) the audio can absorb before going silent. Those two things trade directly against each other with no single right answer, so it's a Settings slider rather than my picking one number — see "A note on dropouts" above. |
| Detection scope | System-wide (any drive), not a specific drive letter | Matches the original hardware's single-LED simplicity. |
| Detection source (Win9x) | `HKEY_DYN_DATA\PerfStats\{StartStat,StatData,StopStat}` | Win9x has no modern disk-IO-counter API; this is the same real-time counter feed `sysmon.exe` reads. See Toolchain/gotchas for the two non-obvious things about it. |
| Detection signal (Win9x) | Delta between polls of `VFAT\BReadsSec`/`BWritesSec`, thresholded | These counters are cumulative totals, not rates (see gotchas) — and even a correct delta-based reading needs a minimum-bytes threshold, or ordinary OS housekeeping I/O triggers it every few seconds. |
| Detection source (2000/XP+) | PDH, `\PhysicalDisk(_Total)\Disk Bytes/sec` | Documented, always-present since NT4, and `_Total` gives the same system-wide "any drive" scope with no per-instance enumeration needed. |
| Detection signal (2000/XP+) | Raw formatted value, thresholded | PDH computes the rate between successive `PdhCollectQueryData` calls itself — no manual delta-tracking, unlike Win9x. Still thresholded for the same reason (filtering background I/O). |
| Settings persistence | INI file (`hddsynth.ini`) via `GetPrivateProfileString`/`WritePrivateProfileString`, not the registry | Keeps the app self-contained the same way `samples/` already is — no install/uninstall registry cleanup, and it's the period-correct pattern for the era. |
| Sample pack switching | Live swap under a `CRITICAL_SECTION` in the mixer, not a full restart | The three `WavPcm` buffers are compound state (pointer+count+rate) that can't be swapped atomically with a single `Interlocked` op the way the existing `g_accessActive` flag can — a lock is the honest way to let the GUI thread (menu click) replace what the audio thread is mid-mix on without tearing. |
| About box logo | Converted to a 24-bit BITMAP resource, not embedded as PNG | Classic Win9x GDI has no PNG support and BITMAP resources carry no alpha channel, so `tools/make_about_logo.py` composites the source PNG onto the classic Win95 dialog face gray and resizes it before conversion. |
| About box build label | `HDDSYNTH_BUILD_NAME` (`version.h`), driven by a `HDDSYNTH_TARGET_NT` macro the `Makefile` defines only for the NT target | The two `.exe`s look and behave identically otherwise; this makes it obvious at a glance which one is actually running without having to check the filename. |
| XP visual styles | Embedded manifest (`res/hddsynth.manifest`, `RT_MANIFEST` resource in the shared `.rc`) declaring the `Microsoft.Windows.Common-Controls` v6 dependency | Themes the Settings dialog's sliders/buttons with XP's native look. Lives in the shared resource script, not gated by `HDDSYNTH_TARGET_NT` — Win9x's loader doesn't process manifests at all, so it's silently inert there. |
| Tray icons | 32bpp with real per-pixel alpha (`tools/make_icons.py`), not the classic 24bpp/1-bit-mask format used everywhere else in this project | Windows 2000+ renders the alpha channel directly for genuinely smooth anti-aliased edges; Win9x-era shell32 ignores alpha and falls back to a mask thresholded from it, giving the same hard-edged look as before — one asset serves both targets' rendering ceiling. |
| Language | C++, conservative subset | No STL threading (`std::thread`/`std::mutex`) — MinGW's `winpthreads` backend calls synchronization primitives that don't exist on Win95. Plain Win32 primitives (`_beginthreadex`, `CRITICAL_SECTION`/`Interlocked*`) instead. |

## Toolchain

Built on macOS (also works on Windows 11/Linux) using a cross-compiler — no Windows 9x-era
IDE or VM needed just to *build* the code, only to *run and test* it.

- **Compiler**: `mingw-w64` via Homebrew (`brew install mingw-w64`), targeting `i686-w64-mingw32`.
- **Critical flag, both targets**: `-mcrtdll=msvcrt-os`. Modern mingw-w64 (this build is GCC 16)
  defaults to linking against the Universal CRT (`api-ms-win-crt-*.dll`), which doesn't exist
  before Windows 10 — not even with the redistributable installed, and not on XP/2000 any more
  than on Win9x. `-mcrtdll=msvcrt-os` forces linking against the real `msvcrt.dll` that's present
  on every Windows version from 95 through 11 instead, so both `hddsynth.exe` and
  `hddsynth-nt.exe` use it.
- **CPU target, Win9x only**: `-march=pentium -mtune=pentium`, so `hddsynth.exe` never emits an
  instruction the original Pentium (and thus the oldest realistic Win95 boxes) can't execute.
  `hddsynth-nt.exe` doesn't set this — Windows 2000/XP's minimum supported CPUs are already P6+.
- **Custom Pentium-safe CRT** (`tools/build-pentium-crt.sh`), **Win9x only**: Homebrew's
  mingw-w64 ships its CRT startup objects and static support libs (`crt2.o`, `libmingw32.a`,
  `libmingwex.a`, ...) *prebuilt* for a P6+ CPU baseline, containing `cmove`/`cmovne`
  instructions — my own `-march=pentium` only affects code I compile, not these prebuilt
  archives. On a real/emulated Pentium (no CMOV) that's an illegal-instruction crash at startup,
  before `main` is even reached (this is exactly what the first real-hardware test hit). The
  script rebuilds just those pieces from the matching mingw-w64 source release with
  `-march=pentium`, installs them to `/tmp/mingw-pentium-sysroot`, and the `Makefile` prefers
  them via `-B`/`-L` for the Win9x target only — `hddsynth-nt.exe` links Homebrew's stock CRT
  objects unmodified, since P6+ is already guaranteed on its target OSes.
- **Threading, both targets**: `-no-pthread`, since GCC otherwise links `libpthread`/
  `winpthreads` into every binary by default, whether or not the code uses threads — and that
  library's init path isn't Win95-safe (moot for NT, but harmless/unnecessary to link either way
  since this codebase only uses native Win32 threading regardless of target).
- **Static linking, both targets**: `-static -static-libgcc -static-libstdc++`, so the only DLLs
  either exe depends on are ones Windows itself ships — `hddsynth.exe` needs `kernel32`,
  `user32`, `gdi32`, `shell32`, `msvcrt`, `winmm`, `advapi32`, `comctl32`; `hddsynth-nt.exe` needs
  all of those too, plus `pdh` (see "Two builds, one codebase") — `advapi32` used to be
  Win9x-only (`diskmon.cpp`'s registry reads), but `autostart.cpp`'s "Run at Windows Startup"
  registry toggle is shared code, so both targets need it now. No GCC runtime redistributable to
  install on the target machine either way.
- **Subsystem stamping**: 4.0 (Win95-era) for `hddsynth.exe`, 5.0 (Windows 2000 — the lower of
  2000/XP, so both accept it) for `hddsynth-nt.exe`.
- **Stripped (`-s`)**: no debug symbols in either shipped binary — there's no debugger on the
  target machine to use them anyway.

One-time setup (rebuilds the Win9x target's Pentium-safe CRT above; only needs re-running if
Homebrew updates the `mingw-w64` formula; not needed for `hddsynth-nt.exe`):

```sh
tools/build-pentium-crt.sh
```

Then build with:

```sh
make            # builds build/hddsynth_spike.exe and build/hddsynth.exe (Win9x)
make nt         # builds build/hddsynth-nt.exe (Windows 2000/XP+)
make clean
```

Verification approach throughout development: statically via `objdump -p`/`-d` on the built
`.exe` (confirms only Win95-era DLLs/symbols are imported and zero CMOV/RDTSC/CPUID
instructions anywhere), but that only rules out *known* bad patterns — actually running it on
real Windows 98 hardware is what caught everything else below.

### Gotchas found the hard way

These cost real debugging time against real hardware and are easy to reintroduce by accident,
so I've recorded them here rather than leaving them buried in commit history:

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
  I traced it back to this.
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
- **`.rc` files need `#include <windows.h>` too**, not just `.cpp` files — without it, `WS_POPUP`,
  `DS_MODALFRAME`, and friends are just undefined macros as far as the resource compiler's
  preprocessor is concerned, and `windres` fails with a bare "syntax error" pointing at the
  `STYLE` line, not a missing-macro complaint that would've been easier to place.
- **Relying on the current working directory for relative paths doesn't hold up once there's
  more than one file to find.** It happened to work for `samples\...` when launched by
  double-clicking in Explorer, but broke down once an INI file and a directory *scan* (for
  sample packs) were added too — `GetPrivateProfileString` in particular looks in the Windows
  directory, not the CWD, if given a bare filename. Fixed by resolving everything relative to
  the exe's own directory via `GetModuleFileNameA` (`src/paths.cpp`) instead.
- **Deep audio buffering trades directly against how "live" the effect feels — but also against
  resilience to real stalls, in the opposite direction, with no way to get both from the same
  number.** A generously large buffer depth was worth it while refills were tied to the GUI
  thread (protection against message-queue stalls); once that architecture changed, the same
  depth just became ~2 seconds of lag between real disk activity and the sound reacting to it.
  I cut it down for that reason, then hit audible dropouts running a Scandisk surface scan —
  the *same* depth number that caused the lag was also what had been absorbing that kind of
  stall. Ended up exposing it as a Settings slider (`audioBufferMs`) rather than picking one
  fixed value, since which tradeoff is more annoying genuinely depends on the machine and what
  you're doing with it.
- **PDH's status constants (`PDH_CSTATUS_VALID_DATA`, `PDH_CSTATUS_NEW_DATA`) live in
  `<pdhmsg.h>`, not `<pdh.h>`** — including only `<pdh.h>` (which declares the functions/types)
  compiles fine right up until you reference one of those constants, at which point it's an
  undeclared-identifier error rather than a missing-include one.
- **The Balance slider used to floor each side at 50%, so it could never fully mute either
  layer** (`idleWeightX100 = 150 - balance`, `accessWeightX100 = 50 + balance` — at balance=100
  that's still 50% idle, not 0%). That was a deliberate choice at the time (never silence a
  layer via balance alone), but it directly contradicted what the slider's own end labels
  ("Idle"/"Activity") imply, and a user reported exactly that — setting Activity to 100 still
  left idle audible. Fixed to a straight linear crossfade (`100 - balance` / `balance`) so the
  extremes actually reach full mute, at the cost of each layer only being at 50% (not 100%) at
  the center — Volume still scales the combined result if that reads as quieter overall.

## Open items

- **Large file copies can freeze the whole Windows UI for a few seconds** (not just this app —
  audio, GDI/USER, everything). This persisted even after ruling out this app's own thread
  priority as the cause (rolled back to normal, no change) and while the disk monitor was
  provably idle throughout. Most likely a system-level PIO-vs-DMA disk transfer issue on my
  test machine, not something this app can fix — worth checking Control Panel → System →
  Performance tab and Device Manager → disk → DMA setting. Set aside for now; a small batch of
  smaller file copies showed no such freeze.
- **Toolchain longevity**: this relies on Homebrew's current mingw-w64 (GCC 16) working with
  `-mcrtdll=msvcrt-os`, which isn't its primary supported configuration. If deeper issues
  surface with more Win32 API surface, the fallback is an older/legacy toolchain (classic
  MinGW.org line, or TDM-GCC 4.7.1/4.9.2).
- **No installer/packaging yet** — currently just a folder (`hddsynth.exe` + `samples/`)
  copied onto the target machine; `hddsynth.exe` expects `samples\` alongside it.
- **`hddsynth-nt.exe` has been confirmed working on real Windows XP SP3 hardware** by a user
  (tray/audio/dialogs behaving as expected, PDH-based disk-activity detection reacting to real
  activity). I still don't have NT-family hardware or a VM of my own, so this build's ongoing
  verification depends on user reports rather than the direct round-trip testing loop
  `hddsynth.exe` gets — treat any *new* NT-specific change as unconfirmed until it's been tried
  on real hardware again, the same discipline already applied to Win9x changes.
- **`hddsynth-nt.exe` only resolves English-language PDH counter names** — see "Two builds, one
  codebase" above.
