# CLAUDE.md — Keys AI Onboarding

## About the Owner

Owen is a wheelchair user with muscular dystrophy who produces music with a single
mouse. Typing is hard — be proactive, make decisions, offer A/B/C choices so he can
answer with one letter. Mouse-only operability is not a preference here, it is the
product.

## What This Is

A JUCE VST3 (+ Standalone) **playable MIDI keyboard**. It makes no sound; you click
an on-screen piano and it emits MIDI note on/off to the track output to drive a
downstream instrument. Part of the OK Studio line (Undertow = bass, Beatform =
drums, Keys = the played keyboard). Where those two generate, Keys is performed.

## Build & Run

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DKEYS_COPY_PLUGIN=OFF
cmake --build build --config Release --target Keys_VST3 Keys_Standalone
```

Artifacts: `build/Keys_artefacts/Release/{VST3,Standalone}/`. Note the snippet above
passes `-DKEYS_COPY_PLUGIN=OFF`, so it builds but does **not** install to the DAW
folder — Ableton keeps loading the previously installed binary. If Owen reports "the
VST didn't update," this is the usual cause: run `build.ps1` (or copy the bundle
yourself, then have him rescan in Live). `build.ps1` wraps
build → (sign) → copy-to-Ableton → (installer). Local test install: `build.ps1`
copies the .vst3 to `%USERPROFILE%\Ableton\vst3` (Owen's Ableton custom folder;
`%USERPROFILE%\VST3` is NOT scanned). Needs JUCE at `../JUCE` and the kit at
`../okstudio-juce-kit`.

## Architecture

Read `docs/ARCHITECTURE.md` first. Load-bearing ideas:

- **Kit-based.** Theme, scales, state persistence, the mouse-only contract, and the
  updater all come from `../okstudio-juce-kit`. Fix shared behaviour there, not here.
- **UI → audio note path is a `juce::MidiMessageCollector`.** The editor/keyboard
  call `processor.noteOn/noteOff` on the message thread; `processBlock` drains the
  collector. Never touch the outgoing `MidiBuffer` from the UI thread directly.
- **Sounding = union of three sets.** `pressed` (mouse), `latched`, `sustained`.
  `PianoKeyboard::refresh()` diffs that union against what's currently on and emits
  the delta. All state changes go through it, so notes never double-fire or stick.
- **The played note is resolved at press time** (scale-lock snap, then octave) and
  remembered, so note-off matches even if octave/scale change while it sounds.
- **Chord generation is a weighted pool.** `ChordGen.h` builds candidate chords in
  tiers (diatonic → borrowed → secondary dominants → chromatic); Scale Compliance
  decides which tiers enter it, Lock Influence re-weights it toward the families of
  locked chords. `ScaleModes.h` is deliberately *not* the kit's scale table: that one
  answers "is this note in the scale", generation also needs a quality per degree.
  All of it (plus `ChordSuggest.h`) is UI-free so it unit-tests.
- **Ports from Octavium are not transcriptions.** Two of its generator bugs were fixed
  rather than reproduced (non-diatonic Sus2/Add9 at 100% compliance; regenerate
  dropping the note-count filter), and its right-click affordances had to be rebuilt as
  on-screen buttons. Check ported logic against the invariants before trusting it.

## Invariants (don't break)

- **Keep the MIDI input bus** (`NEEDS_MIDI_INPUT TRUE`, set by `okstudio_add_plugin`;
  `acceptsMidi()` true). Ableton refuses to load an instrument without one. The
  failure is silent-looking ("This VST3 plug-in could not be opened") and pluginval
  passes without the bus, so only a real Live load test catches it.
- **Mouse-only UI**: single left-click or drag; targets ≥ ~34 px; no
  keyboard/double-click/modifiers, and nothing may *require* right-click. Latch and
  Sustain are on-screen toggles by design, never modifier keys. Right-click exists
  only as an optional accelerator with a left-click equivalent (per-note latch on the
  note surfaces, at Owen's request). The feature-request template requires an
  accessibility answer — hold PRs to it.
- **Audio thread**: no allocation, no locks. It only drains the collector.
- Parameter-layout changes break saved sessions — changelog loudly.
- **Updater contract** lives in the kit (`docs/AUTO_UPDATE.md`): releases repo is
  `okstudio1/keys-releases`; assets named exactly `KeysSetup-<version>.exe`, tag
  `v<version>`; every verification gate stays fail-closed.

## Screenshots for docs

Use the PrintWindow approach (never SetForegroundWindow/SetCursorPos — Owen is often
using the machine, and a mis-capture can grab his private windows). Launch the
standalone, capture via `PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT)`, kill the
process. Screenshots live in `assets/screenshots/`, referenced from README and docs.

## Conventions

- Conventional commits (`feat:`, `fix:`, `docs:`, `chore:`).
- C++20, JUCE idioms, match surrounding style (`.clang-format`).
- Update `CHANGELOG.md` under `[Unreleased]` with every user-visible change.
- Never add AI attribution to commits.
- CI builds `windows-latest` + `macos-latest`; don't pin a VS generator in workflows.

## Sibling projects (same owner, same conventions)

`../okstudio-juce-kit` (shared code), `../Contour` (drawn melodic contours),
`../Lattice` (mouse-only polyphonic piano roll), `../Undertow` (bass), `../Beatform`
(drums), `../alpha-osk` (on-screen keyboard), `../Octavium` (the original Python
controller these plugins are being carved out of). Installer/CI/signing mirror Undertow
and Beatform; EV signing uses the same OK Studio eToken thumbprint.

Keys is the kit's **reference consumer**: its `CMakeLists.txt`, test setup and updater
wiring get copied into every new line plugin. Changes here propagate by imitation, so a
structural fix is worth landing in the kit's `docs/USAGE.md` too, not only here.

**Keys is played; Contour and Lattice are authored.** Keys drains a
`MidiMessageCollector` fed by UI clicks and never reads the playhead. The generators
author their whole outgoing stream from the host `ppqPosition` and schedule note on/off
at sample offsets. Do not copy Keys' `processBlock` into a generator; copy Contour's.
