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

## Quick build (build.ps1)

```powershell
./build.ps1                 # Release VST3, copied to %USERPROFILE%\Ableton\vst3
./build.ps1 -Standalone     # also build the standalone app
./build.ps1 -Installer      # also build (and sign) the installer -> release/
./build.ps1 -NoSign         # dev build without signing
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

Unit tests (JUCE UnitTest) cover the pure note-resolution logic. They build as a
separate console target, so normal plugin builds stay fast:

```powershell
cmake --build build --config Release --target Keys_tests
ctest --test-dir build -C Release --output-on-failure
```

CI builds and runs them on every push. The line's testing convention (what to test,
how to keep it testable) is in
[okstudio-juce-kit/docs/TESTING.md](../../okstudio-juce-kit/docs/TESTING.md).

## CMake options

| Option | Default | Meaning |
|--------|---------|---------|
| `KEYS_JUCE_PATH` | `../JUCE` | JUCE checkout to use |
| `OKSTUDIO_KIT_PATH` | `../okstudio-juce-kit` | kit checkout to use |
| `KEYS_COPY_PLUGIN` | `ON` | copy the built VST3 to `KEYS_VST3_COPY_DIR` after build |
| `KEYS_VST3_COPY_DIR` | `%USERPROFILE%/Ableton/vst3` | where the copy lands |

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
