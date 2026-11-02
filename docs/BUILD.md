# Building Keys

## Prerequisites

- **CMake** 3.22 or newer
- **Visual Studio 2022** (Windows) with the Desktop C++ workload
- A **JUCE 8** checkout at `../JUCE` (sibling of this repo)
- The **kit** at `../okstudio-juce-kit` (sibling of this repo)
- For installers: **Inno Setup 6** (`winget install JRSoftware.InnoSetup`)

Layout:

```
dev/
├── JUCE/
├── okstudio-juce-kit/
└── Keys/
```

## No heavy dependency any more

**The first configure is cheap again, and `build/` is cheap to delete.** Keys had a
**Transcribe** section for a few days, built on the kit's `okstudio_basicpitch` target, and
that target's prebuilt ONNX Runtime brought two costs with it that were properties of the
library rather than choices: a multi-gigabyte download unpacked into the build tree on the
first configure, and the **static MSVC runtime**, which is contagious (MSVC will not link
objects that disagree, so `CMAKE_MSVC_RUNTIME_LIBRARY` had to be forced on everything in the
binary, JUCE included).

The section was removed on 2026-07-30. Both costs went with it: there is no download, no
`OKSTUDIO_ONNXRUNTIME_CACHE`, no `KEYS_TRANSCRIBE` option, no second `build-notranscribe`
tree to keep warm, and Keys is back on the **default dynamic CRT**. The engine still lives in
the kit (`okstudio/Transcribe.h`, `-DOKSTUDIO_KIT_BASICPITCH=ON`, its `docs/TRANSCRIPTION.md`)
for whichever product wants it next; Keys just no longer asks.

**One catch on a build tree that predates the removal.** Keys used to set
`OKSTUDIO_KIT_BASICPITCH` with `CACHE FORCE`, so the `ON` is still sitting in an old
`CMakeCache.txt` and the kit will happily go on building the engine. Re-configure such a tree
once with the flag off:

```powershell
cmake -B build -DOKSTUDIO_KIT_BASICPITCH=OFF
```

`run.py` will not clear it for you. It configures only a *cold* tree, on the grounds that
the Visual Studio generator re-runs CMake by itself whenever `CMakeLists.txt` changes, and a
stale cache entry is not a `CMakeLists.txt` change. A fresh tree needs nothing: the kit's own
default is `OFF`. Deleting `build/` is the other way out, and it costs what it used to again.

## Testing a change (run.py)

The everyday loop. Builds exactly one Standalone target and relaunches it: about 5s
after touching a .cpp, ~1s for a no-op. No VST3, no signing, no DAW rescan.

**Double-click `run.py`** in Explorer (or right-click → Open) to build and launch Keys
Host. No arguments, no terminal: this is the mouse-only path, and it is the reason the
loop is a Python script rather than only a PowerShell one. If the build fails, the
console stays open so the error can be read instead of flashing past.

From a terminal, either entry point works and both do the same thing — `run.ps1` is a
thin shim over `run.py`, so there is one copy of the logic:

```powershell
py run.py                   # build + launch Keys Host standalone
py run.py --keys            # plain Keys instead (MIDI only, makes no sound)
py run.py --no-build        # just relaunch what is already built

./run.ps1                   # same three, with PowerShell-style switches
./run.ps1 -Keys
./run.ps1 -NoBuild
```

Keys Host standalone runs a real instrument VST3 in-process, so clicking a key makes
sound with no DAW involved. Load a synth into it once; it remembers between launches.

Reach for `build.ps1` below when a change needs a real Ableton load test: bus layout,
plugin classification, the installer, the updater, or host automation.

Three things `run.py` absorbs so they don't look like build failures. The third, Smart
App Control, has the section below to itself.

It does not just run whatever `cmake` is on PATH. pip installs cmake behind a small
unsigned launcher in `Scripts`, and Smart App Control refuses to start it — which surfaces
as a bare `OSError` traceback out of `subprocess`, since Windows error 4551 has no errno
mapping. run.py tries a `CMAKE` environment override, Visual Studio's signed copy, pip's
real signed payload under `site-packages/cmake/data/bin`, an MSI install, and only then
PATH, taking the first that actually launches and saying which it picked when that is not
the one on PATH. If none starts, it names each one that was blocked instead of a traceback.

Keys Host can own a top-level window per detached section on top of its own, the hosted
instrument's GUI and the chord generator's window (seven in all, if you pull all four
sections out and open the generator), and Windows picks `MainWindowHandle` between them
heuristically, so the close is aimed at the window titled after the product (closing the
instrument window only hides it, which would cost you a force-kill and the loaded synth).

## Smart App Control

SAC is **enforced** on Owen's machine (`HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy` →
`VerifiedAndReputablePolicyState = 1`) and dev builds are unsigned, so it gates the first
launch of every freshly linked exe while it decides. It gates a *file hash*, not a path, so
every relink is a fresh verdict and the same target can be instant one build and blocked the
next.

What it actually does, measured here on 2026-07-27 from
`Microsoft-Windows-CodeIntegrity/Operational` (readable without admin):

- One `Keys.exe` build was blocked **175 times over 19 minutes 44 seconds** — and never
  cleared. It was superseded by a relink. `DefenderMadeCloudCall` was false on 173 of those
  175 events, i.e. the cloud reputation call was not completing at all.
- The builds either side of it — `Keys.exe`, `Keys FX.exe`, `keys-mcp.exe`, `Keys Host.exe`
  — each ran within about two seconds of being linked.

So "wait and it clears" is a coin toss, not a rule, and the old advice here (three minutes,
retry for four) was optimistic by a factor of five. `run.py` now waits up to twenty minutes
with a live counter and a Ctrl+C that works, because SAC blocks a launch **two different
ways**: with a verdict cached it fails `CreateProcess` outright, and while the lookup is in
flight it *blocks the call*. Only the first was handled, and the second is what used to
present as a dead hang with nothing on screen.

### Getting out of the wait

There is **no exemption**. No allowlist, no trusted folder, no Developer Mode carve-out, no
"run anyway" on the block dialog, and a self-signed certificate does not help — SAC only
honours certificates chaining to a CA in the Microsoft Trusted Root Program. Microsoft's
answer to developers is "sign your app with a valid certificate", which for this repo means
the EV eToken and a PIN per build: fine for a release, fatal for a five-second loop.

That leaves turning SAC off, which **used to be a one-way door** — and is not any more.
KB5079391 (26 March 2026, builds 26200.8116/26100.8116) shipped "You can turn Smart App
Control (SAC) on or off without needing a clean install", and the consumer FAQ now says
"Recent Windows updates allow Smart App Control to be re-enabled without requiring a clean
installation." This machine is on **26200.8875**, well past that. Be aware that Microsoft
has not retracted the old warning: the Learn "Application Control for Windows" page still
carries "Once you turn Smart App Control off, it can't be turned on without resetting or
reinstalling Windows", revised *after* the fix shipped. The free check is to open
**Settings → Privacy & security → Windows Security → App & browser control → Smart App
Control settings** and see whether the toggle moves both ways before committing to it.

Owen's machine, Owen's call — nothing in this repo changes that setting.

## Quick build (build.ps1)

```powershell
./build.ps1                 # Release VST3s (Keys + Keys Host), copied to %USERPROFILE%\Ableton\vst3
./build.ps1 -Standalone     # also build both standalone apps
./build.ps1 -Installer      # also build (and sign) the installer -> release/
./build.ps1 -Sign           # sign the binaries with the OK Studio EV cert (needs the eToken)
./build.ps1 -NoSign         # force signing off, even with -Installer
```

`build.ps1` owns the copy into the DAW folder, so if Ableton has Keys loaded (file
locked) it warns and carries on rather than failing the build — unload Keys or close
the DAW and rerun to install.

## Plain CMake

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DKEYS_COPY_PLUGIN=OFF
cmake --build build --config Release --target Keys_VST3
cmake --build build --config Release --target Keys_Standalone   # optional
```

Artifacts:

- `build/Keys_artefacts/Release/VST3/Keys.vst3`
- `build/Keys_artefacts/Release/Standalone/Keys.exe`

> **This does not install the plugin.** `-DKEYS_COPY_PLUGIN=OFF` disables the copy
> into the DAW folder, so the build tree gets a fresh `Keys.vst3` but Ableton keeps
> loading the last *installed* one. This is the usual cause of "I built but the VST
> didn't update." To install, either run `./build.ps1` instead, or copy the bundle
> yourself:
>
> ```powershell
> Remove-Item -Recurse -Force "$env:USERPROFILE\Ableton\vst3\Keys.vst3" -ErrorAction SilentlyContinue
> Copy-Item -Recurse -Force build\Keys_artefacts\Release\VST3\Keys.vst3 "$env:USERPROFILE\Ableton\vst3"
> ```
>
> Drop `-DKEYS_COPY_PLUGIN=OFF` (it defaults `ON`) if you want plain CMake to install
> on every build too.

## Tests

Unit tests (JUCE UnitTest) cover the UI-free logic: note resolution, chord detection, the
scale modes, chord generation, the voicing cycle, chord suggestion, the Markov progression
model, and the arpeggiator engine's scheduling in both rate modes (tempo-synced divisions
and free Hz). They build as a separate console target, so normal plugin builds stay fast:

```powershell
cmake --build build --config Release --target Keys_tests
ctest --test-dir build -C Release --output-on-failure
```

The target is a plain console exe at `build/Release/Keys_tests.exe`; run it directly and it
prints any failures and then the totals, since "all tests passed" alone cannot tell a full
suite from one that silently stopped registering itself.

CI builds and runs them on every push to `main` and on every pull request, except for
changes that only touch Markdown, `docs/` or `assets/` (`paths-ignore`). The line's
testing convention (what to test,
how to keep it testable) is in
[okstudio-juce-kit/docs/TESTING.md](../../okstudio-juce-kit/docs/TESTING.md).

## Nightly fuzzing

`Keys_fuzz` (`tests/FuzzTests.cpp`) is the randomised half of the suite: a seeded generator
draws inputs nobody wrote a case for and checks invariants rather than expected values. Seven
targets: `chordsym` (the typed-chord parser, whose canonical name must re-parse to the same
chord), `detect` (a chord's name must not depend on note order or octave), `notemath` (a
scale-locked key always lands in the scale), `euclid`, `chordgen` (every generated or revoiced
note is a MIDI note), `arp` (one engine with random parameters, lanes, chord and transport: every
event inside its block, no note-off for a silent pitch, no second note-on for a sounding one,
nothing held once the chord is gone) and `state` (a session file with flipped bytes, a truncated
tail or attributes the UI could never produce must load, play a few blocks, save and load again).

`.github/workflows/fuzz-nightly.yml` builds it with AddressSanitizer and runs it once a day on
a fresh seed (the run id) with a large budget, so each night explores new space and a failure is
allowed to be news. It is not a required check and never gates a merge. CI builds the binary on
every push so it cannot rot, but does not run it. A failure prints the replay:

```
FAIL  arp case 21: block 2: note-off for a silent 126
      reproduce: Keys_fuzz --target arp --seed 24301 --case 21
```

Case `n` of a target seeds its own generator from (seed, target, n), so that one line replays
the input alone, from `build/Release`. `KEYS_FUZZ_TRACE=1` prints every MIDI event the arp emits
and the case's parameters; `KEYS_FUZZ_OFF=legato,clock,harm,late,lock,duck,stray,mutate,drift,humanvel,ramp,swing,dividers`
switches named arp features off on top of the draw, for bisecting. Fix the bug, then add the
minimal input as a named test in the normal suite.

```powershell
cmake --build build --config Release --target Keys_fuzz
.\build\Release\Keys_fuzz.exe                       # fixed seed, 300 cases a target, a few seconds
.\build\Release\Keys_fuzz.exe --iterations 20000    # about half a minute
```

**Two findings from the first run (2026-11-02), both open:**

- `Keys_fuzz --target arp --seed 24301 --case 21`: with CLOCK on, a follower's ratchet sub-hits
  are placed across the source's step length, but the source's fires can be closer than that (a
  Late lane at 90% then a step at 0%). The next fire then arrives *before* sub-hits already
  emitted, and `emitHit`'s close-what-you-land-on pass (the tie branch, and Legato's release)
  emits their note-off at a sample *earlier* than their note-on. Downstream that is an off the
  synth ignores followed by an on nothing ever ends: a stuck note.
- `Keys_fuzz --target state --seed 24301 --case 302`: a parameter attribute holding `NaN` in the
  session XML loads as a NaN parameter value. Nothing clamps it on the way in.

Until those are fixed, `Keys_fuzz` is not registered with `ctest`, since a fixed-seed smoke would
turn every pull request red for a reason it did not cause (`CMakeLists.txt` says where to add it).

## CMake options

| Option | Default | Meaning |
|--------|---------|---------|
| `KEYS_JUCE_PATH` | `../JUCE` | JUCE checkout to use |
| `OKSTUDIO_KIT_PATH` | `../okstudio-juce-kit` | kit checkout to use |
| `KEYS_COPY_PLUGIN` | `ON` | copy the built VST3 to `KEYS_VST3_COPY_DIR` after build |
| `KEYS_VST3_COPY_DIR` | `%USERPROFILE%/Ableton/vst3` | where the copy lands |
| `KEYS_BUILD_HOST` | `ON` | build Keys Host (an instrument VST3 hosted inside Keys). Turning this off leaves `run.py`'s default target missing |
| `KEYS_BUILD_MCP_SHIM` | `ON` | build `keys-mcp.exe`, the stdio bridge (see [MCP.md](MCP.md)) |
| `KEYS_BUILD_MIDI_EFFECT` | `OFF` | also build Keys FX, the MIDI-effect variant Ableton rejects |
| `KEYS_FUZZ_ASAN` | `OFF` | compile `Keys_fuzz` with AddressSanitizer (the nightly turns it on; needs the MSVC ASan component and its runtime DLL on `PATH`) |

## Troubleshooting

- **`okstudio-juce-kit expects JUCE to be added before it`** — you added the kit
  before JUCE. In `CMakeLists.txt`, `add_subdirectory(JUCE)` must come before
  `add_subdirectory(okstudio-juce-kit)`.
- **Kit or JUCE not found** — check the sibling paths above, or pass
  `-DKEYS_JUCE_PATH=...` / `-DOKSTUDIO_KIT_PATH=...`.
- **Built, but the VST didn't update** — two causes. (1) You built with plain CMake
  and `-DKEYS_COPY_PLUGIN=OFF`, so nothing was copied to the DAW folder; see the note
  under [Plain CMake](#plain-cmake). (2) The copy ran but Live is still showing the
  old plugin because it caches its scan — rescan (Preferences → Plug-Ins → Rescan) or
  restart Live, and re-add the instance if one was already on a track. To confirm which
  binary Ableton will load, compare timestamps of
  `build\Keys_artefacts\Release\VST3\Keys.vst3\Contents\x86_64-win\Keys.vst3` and the
  copy under `%USERPROFILE%\Ableton\vst3\Keys.vst3\...`.
- **Copy step fails** — the DAW has Keys loaded and the file is locked. `build.ps1`
  warns and carries on rather than failing the build. Unload it or close the DAW, then
  rerun. The built binary is still in the build tree regardless.
- **First build is slow** — JUCE compiles from source once; later builds are
  incremental.
