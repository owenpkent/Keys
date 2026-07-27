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

**Default dev loop: `run.py`.** Do not send Owen to Ableton to try a change. It closes
the running standalone, builds exactly one Standalone target, and relaunches it: about
5s for a touched .cpp, ~1s for a no-op. Keys Host standalone runs a real instrument VST3
in-process, so a click makes sound with no DAW involved and no rescan. It remembers the
loaded synth between launches, which is why the script asks the app to close politely (a
forced kill skips JUCE's settings write and loses it). Those seconds are the build:
Smart App Control is enforced here and dev builds are unsigned, so the *launch* of a
freshly linked exe can be held while Windows vets it. `run.py` waits that out with a
counter on screen (twenty minutes before it gives up, Ctrl+C to stop waiting);
`docs/BUILD.md` has the ways out of the wait itself.

**Owen runs it by double-clicking `run.py` in Explorer** — no arguments, no terminal, and
the console holds open on failure so he can read the error. Never tell him to type a
command when he could click the file instead. `run.ps1` is a thin shim over `run.py`, so
there is one copy of the logic; either is fine from a terminal.

```powershell
py run.py              # build + launch Keys Host standalone (makes sound)
py run.py --keys       # plain Keys instead (MIDI only, silent)
py run.py --no-build   # just relaunch what is already built
```

Go to a real Live load test only for what the standalone genuinely cannot show: bus
layout, plugin classification, installer, updater, host automation.

**The first configure is slow now.** The Transcribe section pulls a multi-gigabyte prebuilt
ONNX Runtime into the build tree, and forces the static MSVC runtime on the whole binary (both
are properties of that library, not choices). `-DKEYS_TRANSCRIBE=OFF` drops the section and
both costs, and is the right flag for work that never touches transcription. Deleting `build/`
is no longer cheap.

Full build (VST3 + install to the DAW):

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

- **Kit-based.** Theme, scales, state persistence, the mouse-only contract, the audio-to-MIDI
  transcription engine, and the
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
- **Arp slots carry chords, not just patterns.** The twelve slots hold lane data *and* a
  chord, a shape and a rate; launching one installs all of it and holds the chord into the
  arp (`holdArpChord`, tagged `arpChordTag` so it never collides with pad or live-card
  scheduling). Held means held: no note-off follows until something replaces it, so
  `allNotesOff()` has to forget it explicitly.
- **The chord pads and the arpeggiator are each a section of their own**, stacked between
  the centre view and the keyboard, so a chord card is on screen whatever else is open. The
  centre views are Perform and Chords only: the arp stopped being a third one on
  2026-07-25. The arp bar carries its On toggle, which survives folding the section shut.
  See `docs/ARP_DESIGN.md`.
- **Every section detaches, and the machinery is generic.** `KeysEditor::sections` is a
  table of six `Section`s (Controls, Centre, Arp, Pads, Transcribe, Keyboard); each owns a
  `Holder` its content is parented into, a Detach button, and the `DetachedWindow` it is
  currently in. Detaching is one re-parent of that holder, and `idealHeight()`,
  `syncSectionControls()` and `paint()` walk the table rather than naming sections.
  (`sectionHeight()` is still a switch and `resized()` still lays each bar out in its own
  block, because what those two spend per section genuinely differs.) Add a section by
  adding an entry, not by copying a code path.
  The Re-dock button travels into the window; controls that belong to the editor rather
  than the content (the centre tabs, arp On, the pad pages, the theme swatch) stay on the
  bar. The keybed keeps two extras of its own via `Section::travellers`.
- **Keys watches its MIDI input but never consumes it.** `watchInputNotes()` runs first
  thing in `processBlock`, before the collector drains, and records which pitches the
  incoming stream turns on (a flag per pitch, not a count). `isNoteSounding()` answers
  true for those too, which is all it takes to light the keybed for a physical keyboard
  and feed the live chord card. The stream itself passes through untouched, as always.
- **Transcribe is the only part of Keys that consumes audio.** Everything else produces MIDI.
  Keys is an instrument, so a host sends it MIDI and never audio: there is no track input, and
  `AudioCapture` opens an audio device of its own. That is what makes the section behave the
  same in the plugin and the standalone, and it is why choosing an input never touches the
  host's audio setup. The device is open only while the section is on screen or recording.
  The engine is the kit's (`okstudio/Transcribe.h`, basic-pitch ported from NeuralNote); the
  model runs on a background thread, never on the audio or message thread, and the panel is
  built and destroyed with its fold because it holds a device and a network's weights. All of
  it is behind `KEYS_TRANSCRIBE`, on by default. See `docs/ARCHITECTURE.md`.
- **One note-on per sounding pitch, released by the last owner.** Four sources can ask for
  the same pitch at once (a chord pad, the live card, a chord held into the arp, the
  keybed). `KeysProcessor::noteOn` emits MIDI only on the 0→1 transition of `noteRefs` and
  `noteOff` only on 1→0; `ArpEngine::Held::ons` counts owners the same way downstream.
  Break this and one source's release silences another's notes while the keys stay lit,
  and the arp's held set leaks so a released chord arpeggiates forever. Any new chord
  source has to go through `noteOn`/`noteOff`, and `Exclusive` has to reach it
  (`stopAllChordPads`).
- **MCP bridge.** Keys embeds an MCP server (`okstudio::mcp::Server`, transport in
  the kit at `okstudio/Mcp.h`) so Claude Code or any local MCP client can drive it.
  Tools are registered in `src/mcp/KeysMcp.cpp`; every handler runs on the message
  thread (the server marshals it there), so tool bodies call the processor/APVTS the
  same way the UI does. The stdio bridge processes connect through is `keys-mcp.exe`
  (`KEYS_BUILD_MCP_SHIM`). See `docs/MCP.md`.
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
  keyboard/double-click/modifiers. Sustain is an on-screen toggle by design, never a
  modifier key (the separate Latch toggle is gone: a left click on a held note releases
  it, which left nothing for the mode to do). Right-click is normally only an optional
  accelerator with a left-click equivalent (per-note latch on the note surfaces, at
  Owen's request).
  One owner-directed exception (2026-07-22): chord-pad card menus are right-click —
  in the generator (Lock / New chord / Next, restoring Octavium's card menu; the
  page-wide left-click Fill/Regen/Clear stay as the bulk path) and on the main-page
  strip (Edit on keyboard / Clear, plus **Send to arp slot** since 2026-07-25 — the
  only item in the plugin with no left-click twin, since binding a chord to one
  particular slot needs a target picker; a left click on a card with the arp **On**
  is the left-click way to get a chord into the arp, which is what keeps that
  exception to one item). Do not add further right-click-only
  paths without Owen's explicit say-so.
  The arp slot cards also carry a right-click menu, but it is an ordinary accelerator:
  Launch is a click on the card, and Clear chord, Copy and Randomize all have buttons
  under the slot row (Copy and Clear arm, then take a slot click). Randomize is greyed
  in the menu outside Pattern shape, because that is where its button lives.
  Every feature request has to answer "how is this reached with one left-click?" before
  it is worth designing; hold PRs to that. It is a rule, not a form: this line used to
  claim a feature-request template enforced it, and no issue template has ever existed
  in this repo (checked across every ref, 2026-07-27).
- **Audio thread**: no allocation, no locks. It drains the collector, notes what came
  in on the MIDI input (display only), and runs the arp stage.
- Parameter-layout changes break saved sessions — changelog loudly.
- **Updater contract** lives in the kit (`docs/AUTO_UPDATE.md`): releases repo is
  `okstudio1/keys-releases`; assets named exactly `KeysSetup-<version>.exe`, tag
  `v<version>`; every verification gate stays fail-closed.

## Screenshots for docs

Use `scripts/capture-window.ps1`, which is the PrintWindow approach (never
SetForegroundWindow/SetCursorPos — Owen is often using the machine, and a mis-capture can
grab his private windows). Screenshots live in `assets/screenshots/`, referenced from
README and docs. To reach a view, don't synthesize clicks — posted WM_LBUTTONDOWN never
reaches JUCE. Invoke the button through UI Automation instead (`-InvokeButtons`); no cursor
movement involved.

Three things will bite otherwise:

- **Pass `-WindowTitle` for anything but plain Keys.** The default target is
  `MainWindowHandle`, a heuristic that lands on the *hosted instrument's* GUI in Keys Host
  (that GUI is a top-level window of the same process) and on an arbitrary section once any
  are detached. The titles are `Keys Host`, and `Keys Controls` / `Keys Centre` /
  `Keys Arpeggiator` / `Keys Chord Pads` / `Keys Transcribe` / `Keys Keyboard` for the
  detached sections (the `wire(...)` calls in `PluginEditor.cpp`).
- **The Detach buttons are named per section**, because six buttons reading "Detach" are
  six identical accessible names. Invoke `Detach Pads`, `Re-dock Keyboard`, and so on; the
  name flips with the button's state.
- **Close Keys Host politely, never `Stop-Process`.** A forced kill skips JUCE's settings
  write and loses the loaded synth. `-KeepOpen`, then `close_running` out of `run.py`.

## Conventions

- Conventional commits (`feat:`, `fix:`, `docs:`, `chore:`).
- C++20, JUCE idioms, match surrounding style (`.clang-format`).
- Update `CHANGELOG.md` under `[Unreleased]` with every user-visible change.
- Never add AI attribution to commits.
- CI builds `windows-latest` only; don't pin a VS generator in workflows. The
  `macos-latest` leg was dropped 2026-07-22 (Owen ships Windows; macOS runners bill
  included Actions minutes at 10x) — revive from git history or a self-hosted Mac
  runner if mac builds ever return. Docs/assets-only pushes skip CI (`paths-ignore`).

## Sibling projects (same owner, same conventions)

`../okstudio-juce-kit` (shared code), `../Contour` (drawn melodic contours),
`../Lattice` (mouse-only polyphonic piano roll), `../Undertow` (bass), `../Beatform`
(drums), `../alpha-osk` (on-screen keyboard), `../Hex` (the hex-grid Harmonic Table
products, carved out of this repo), `../Octavium` (the original Python controller
these plugins are being carved out of). Installer/CI/signing mirror Undertow
and Beatform; EV signing uses the same OK Studio eToken thumbprint.

Keys is the kit's **reference consumer**: its `CMakeLists.txt`, test setup and updater
wiring get copied into every new line plugin. Changes here propagate by imitation, so a
structural fix is worth landing in the kit's `docs/USAGE.md` too, not only here.

**Keys is played; Contour and Lattice are authored.** Keys drains a
`MidiMessageCollector` fed by UI clicks. The generators author their whole outgoing
stream from the host `ppqPosition` and schedule note on/off at sample offsets. Do not
copy Keys' `processBlock` into a generator; copy Contour's. **One deliberate
exception:** the arpeggiator stage (`ArpEngine.h`, after the collector drain) reads
the playhead for tempo-synced scheduling, Contour-style, and free-runs on an internal
clock when the transport is stopped. It is the only playhead consumer in Keys; see
`docs/ARP_DESIGN.md`.
