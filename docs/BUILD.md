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

Three things `run.py` absorbs so they don't look like build failures.

It does not just run whatever `cmake` is on PATH. pip installs cmake behind a small
unsigned launcher in `Scripts`, and Smart App Control refuses to start it — which surfaces
as a bare `OSError` traceback out of `subprocess`, since Windows error 4551 has no errno
mapping. run.py tries a `CMAKE` environment override, Visual Studio's signed copy, pip's
real signed payload under `site-packages/cmake/data/bin`, an MSI install, and only then
PATH, taking the first that actually launches and saying which it picked when that is not
the one on PATH. If none starts, it names each one that was blocked instead of a traceback.

Keys Host can own up to four top-level windows (its own, the hosted instrument's GUI, and
the keybed and arpeggiator when they are detached), and Windows picks `MainWindowHandle`
between them heuristically, so the close is aimed at the window titled after the product
(closing the instrument window only hides it, which would cost you a force-kill and the
loaded synth).

And if Smart App Control is enforced, it blocks the first launch of a freshly linked
unsigned exe while its reputation check runs, then lets the same file through once the
check clears. That has been measured at up to three minutes here, so run.py keeps retrying
for four, says it is waiting, and prints `Cleared.` when the launch goes through. A long
pause there is the check, not a hang.

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
