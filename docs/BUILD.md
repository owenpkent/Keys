# Building Keys

## Prerequisites

- **CMake** 3.22 or newer
- **Visual Studio 2022** (Windows) with the Desktop C++ workload
- A **JUCE 8** checkout at `../JUCE` (sibling of this repo)
- The **kit** at `../okstudio-juce-kit` (sibling of this repo)
- For installers: **Inno Setup 6** (`winget install JRSoftware.InnoSetup`)
- For the Transcribe section (on by default): a working internet connection the first time
  you configure, and patience — see below

Layout:

```
dev/
├── JUCE/
├── okstudio-juce-kit/
└── Keys/
```

## The transcription dependency

The Transcribe section is built on the kit's `okstudio_basicpitch` target, and that brings two
things with it. Neither is a choice: both are properties of the prebuilt ONNX Runtime the
engine links, published by the NeuralNote authors because building ONNX Runtime from source is
its own project.

- **A large download at configure time.** The archive is fetched into the build tree and
  unpacks to a few gigabytes. It is never committed, and never re-downloaded once it is there,
  but a fresh build tree pays for it again. Deleting `build/` is not free any more.

  Unless you keep it somewhere else: `-DOKSTUDIO_ONNXRUNTIME_CACHE=<dir>` puts the unpacked
  runtime at a path of your choosing, shared by every build tree that points at it. Worth
  setting once if you ever delete `build/`. CI uses the same flag with an `actions/cache`
  step, keyed on the ONNX Runtime release.
- **The static MSVC runtime.** That library is `/MT`, and MSVC will not link objects that
  disagree, so `CMAKE_MSVC_RUNTIME_LIBRARY` is set before `add_subdirectory(JUCE)` in the
  top-level `CMakeLists.txt`. Everything in the binary — JUCE included — is built that way.
  The upside is one fewer redistributable to worry about; the cost is a slightly larger
  binary and a full rebuild if you flip the option.

`-DKEYS_TRANSCRIBE=OFF` avoids both, at the cost of the section. Anything that only touches
the keyboard, the pads, the generator or the arp can be built that way and will configure and
link much faster from cold.

**Give it its own build tree rather than flipping it in place.** The flag decides the C
runtime for the entire binary, so toggling it invalidates every object in the tree — and with
it on, re-configuring is the expensive part. `cmake -B build-notranscribe -DKEYS_TRANSCRIBE=OFF`
keeps both trees warm and lets you move between them for the cost of a link.

The engine and its own tests live in the kit; see its `docs/TRANSCRIPTION.md`.

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

Keys Host can own a top-level window per detached section on top of its own and the hosted
instrument's GUI — eight in all, if you pull everything out — and Windows picks `MainWindowHandle`
between them heuristically, so the close is aimed at the window titled after the product
(closing the instrument window only hides it, which would cost you a force-kill and the
loaded synth).

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
scale modes, chord generation and suggestion, the Markov progression model, and the
arpeggiator engine's scheduling. They build as a separate console target, so normal plugin
builds stay fast:

```powershell
cmake --build build --config Release --target Keys_tests
ctest --test-dir build -C Release --output-on-failure
```

CI builds and runs them on every push to `main` and on every pull request, except for
changes that only touch Markdown, `docs/` or `assets/` (`paths-ignore`). The line's
testing convention (what to test,
how to keep it testable) is in
[okstudio-juce-kit/docs/TESTING.md](../../okstudio-juce-kit/docs/TESTING.md).

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
| `KEYS_TRANSCRIBE` | `ON` | build the Transcribe section. Off drops the section, a multi-gigabyte ONNX Runtime download, and the static-MSVC-runtime requirement below |

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
