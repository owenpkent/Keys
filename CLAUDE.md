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

**The first configure is cheap again** (2026-07-30). It was not for a while: the Transcribe
section pulled a multi-gigabyte prebuilt ONNX Runtime into the build tree and forced the
static MSVC runtime on the whole binary, both properties of that library rather than choices.
Transcribe is gone from Keys, so the download, the `KEYS_TRANSCRIBE` option and the runtime
forcing all went with it, and Keys is back on the default dynamic CRT. Deleting `build/` costs
what it always used to.

**One trap if you reuse a build tree from before that.** Keys used to set the kit's option with
`CACHE ... FORCE`, so an old `build/CMakeCache.txt` still says `OKSTUDIO_KIT_BASICPITCH=ON` and
the kit dutifully fetches ONNX for a plugin that no longer links it. Configure once with
`-DOKSTUDIO_KIT_BASICPITCH=OFF` (or delete the cache); after that it stays off.

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

- **Kit-based.** Theme, scales, state persistence, the mouse-only contract and the updater all
  come from `../okstudio-juce-kit`. Fix shared behaviour there, not here. (The kit still ships
  `okstudio/Transcribe.h`; Keys stopped using it on 2026-07-30 and consumes no audio at all.)
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
  `allNotesOff()` has to forget it explicitly, and clicking the card that is currently feeding
  the arp **retriggers** the hold rather than ending it. The way out of a hold is
  `releaseArpHold()` (`stopChain()` then `releaseArpChord()`, in that order, or the heartbeat
  relaunches the next slot); the **Hold off** chip on the arp bar and ArpPanel's Stop both call
  it, and the chip is on the bar so it survives the section being folded shut.
  The **Chord lane** reads those same slot chords per step, which is the one thing the arp
  engine reads from outside itself. Slot chords are message-thread `std::vector`s, so the
  processor keeps an `ArpEngine::ChordTable` mirror of atomics and `syncArpChordTable()`
  rebuilds it whole; there is no choke point for slot writes, so **its call sites are the
  contract** (set, clear, copy, whole-slot write, session load). Miss one and a lane plays a
  stale chord.
- **The chord pads and the arpeggiator are each a section of their own**, stacked above the
  keyboard, so a chord card is on screen whatever else is open. **There is no centre view**
  (2026-07-30): the arp stopped being one on 2026-07-25, Chords went the day the generator lost
  its panel, Perform went with the Centre section itself, and there are no tabs left to switch.
  **There is exactly one set of chord cards**: the generator draws none of its own, because the
  grid it used to draw was the same sixteen pads of the same page as the strip below it. Its
  `Big` arrangement became the Pads section's. **The generator has no surface at all**:
  `ChordGenMenu` is a plain value member the editor holds for its whole life, reached from two
  24 px chips at the right end of the Pads *bar* (Fill / Regen, the left-click path into
  generation), from three 24 px combo boxes beside them (Key / Mode / Scale Compliance, APVTS
  attachments so the bar and the menu are one state), and from a pad's card menu through
  `addPadMenuItems` / `handlePadMenuChoice` - New chord, Next, Clear page, and every setting
  as a submenu of discrete ticked values hanging **directly** off the pad menu, since a
  `PopupMenu` cannot hold a slider and a wrapper submenu would cost a third leg of hover
  travel. Two levels is the ceiling; the Markov five are the one group that keeps its own,
  because they are inert unless Source is Markov. Clear page is a menu item
  and not a third chip because it empties every unlocked pad on the page and there is no undo
  anywhere in Keys. Those items are unconditional now: the old "is there a panel?" test hid
  them whenever the Chords view was closed, and there is no view left to close. Anything
  that wants to show a chord card should use the pads, not build a second grid. Controls that
  belong to a section but must cost no height ride its bar: it is 34 px that already exists.
  Fill and Regen stay live with the Pads section folded, for the same reason arp On does - the
  other route to the generator is a right-click on a card, and the cards fold away with the
  strip. See `docs/ARP_DESIGN.md`.
- **Every section detaches, and the machinery is generic.** `KeysEditor::sections` is a
  table of four `Section`s (Controls, Arp, Pads, Keyboard); each owns a `Holder` its content
  is parented into, a Detach button, and the `DetachedWindow` it is
  currently in. Detaching is one re-parent of that holder, and `idealHeight()`,
  `syncSectionControls()` and `paint()` walk the table rather than naming sections.
  (`sectionHeight()` is still a switch and `resized()` still lays each bar out in its own
  block, because what those two spend per section genuinely differs.) Add a section by
  adding an entry, not by copying a code path. Centre and Transcribe were entries here until
  2026-07-30; removing a section is deleting its `SectionId` and its entry, nothing else.
  The Re-dock button travels into the window; controls that belong to the editor rather than
  the content (arp On, the pad pages, the theme swatch) stay on the bar. The keybed keeps two
  extras of its own via `Section::travellers`.
  **Only the left end of a bar folds it** (2026-07-30, Owen's ask): `SectionBar::hitTest`
  answers for `foldZone()` alone, the chevron plus its caption, 92 px at the narrowest, with a
  hairline painted where the target ends and only that zone lighting under the mouse. The rest
  of the strip is gap between controls that ride it, and z-order defends each control's own
  rectangle but not the gaps around it, so a click aimed a few pixels off Detach used to hide
  the section it was reaching into. This **reverses the 2026-07-27 removal** of the same
  override; any doc still saying the whole bar is the target is describing that three-day
  window. **Detach hides with its section**: the exceptions that stay on a folded bar are arp
  On, the arp's Hold off, the generator's Fill and Regen, and the theme swatch, each for a
  stated reason. Open and folded bars are painted at different weights on purpose;
  `captionWidth()` and `paintButton()` must use the one `captionFont()`,
  or the caption ellipsises and the controls beside it shift as a section folds.
- **The knob bank is the bottom row of the Controls section**, not a band of its own:
  `knobRowH` is 110, which gives 60 px knobs. The Knobs chip that folds just that row rides
  the Controls bar, so the row can go without the two header rows going with it. It is one of
  the bar controls that *does* hide with its section: a chip that folds a row of a band which
  is not on screen would be a control with nothing behind it.
- **Keys watches its MIDI input but never consumes it.** `watchInputNotes()` runs first
  thing in `processBlock`, before the collector drains, and records which pitches the
  incoming stream turns on (a flag per pitch, not a count). `isNoteSounding()` answers
  true for those too, which is all it takes to light the keybed for a physical keyboard
  and feed the live chord card. The stream itself passes through untouched, as always.
- **Keys consumes no audio at all.** Every part of it produces MIDI. Transcribe was the one
  exception: it opened an audio device of its own (`AudioCapture`) because a host sends an
  instrument MIDI and never audio, and it ran the kit's basic-pitch engine on a background
  thread. The whole section came out on 2026-07-30 - panel, capture, the `KEYS_TRANSCRIBE`
  option, the ONNX Runtime download and the static-MSVC-runtime forcing that library required.
  If something needs audio in again, it needs its own device and its own thread; do not reach
  for a track input, there isn't one.
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
  on-screen buttons. (Two of those went back to a menu on 2026-07-30 by Owen's call, when the
  generator lost its panel; see the mouse-only invariant for which and why.) Check ported logic
  against the invariants before trusting it.

## Invariants (don't break)

- **Keep the MIDI input bus** (`NEEDS_MIDI_INPUT TRUE`, set by `okstudio_add_plugin`;
  `acceptsMidi()` true). Ableton refuses to load an instrument without one. The
  failure is silent-looking ("This VST3 plug-in could not be opened") and pluginval
  passes without the bus, so only a real Live load test catches it.
- **Mouse-only UI**: single left-click or drag; targets ≥ ~34 px; no
  keyboard/double-click/modifiers. Sustain is an on-screen toggle by design, never a
  modifier key, and **Latch is a second one beside it** (restored 2026-07-30 at Owen's
  request, after a spell where a left click on any held note released it and the mode
  looked redundant — it wasn't: that made the pedal a per-note switch, so a repeated note
  over a ringing chord was unreachable). Under Sustain a second click on a ringing key
  **strikes it again**; under Latch it **releases** it. Right-click is normally only an
  optional accelerator with a left-click equivalent (per-note latch on the note surfaces,
  at Owen's request; its left-click release keys on the `latched` set, not on Latch mode,
  so it works with both buttons off).
  **Owner-directed exceptions, and they are a closed list.** Each one is Owen's call on a
  stated date, not something that drifted in:
  1. *The chord-pad card menu is right-click* (2026-07-22, widened 2026-07-30). Edit on
     keyboard / Clear pad / Lock, **Send to arp slot** (since 2026-07-25: binding a chord to
     one particular slot needs a target picker, and a left click on a card with the arp **On**
     is the left-click way to get a chord into the arp), and the whole chord generator, which
     restores Octavium's card menu.
  2. *Most of the generator's settings are right-click only* (2026-07-30, Owen's call when the
     generator gave up its panel). New chord, Next, Clear page and every setting live on the
     card menu, each setting a submenu of ticked discrete values one level down. **Fill and
     Regen survive as 24 px chips on the Pads bar and are the left-click path into
     generation**, and **Key, Mode and Scale Compliance are 24 px combo boxes beside them**,
     which is what keeps this an exception rather than a hole: the thing you do most and the
     three settings you change while auditioning stay one click, the rest became a menu. Those
     three are on the menu too - the bar is the fast path, the menu is the complete one, and
     both are the same parameters. Clear page is in the menu on purpose, because it is
     destructive and Keys has no undo.
  3. *A right-click release of a pedal-held note has no left-click twin* (2026-07-30, Owen
     sanctioned the asymmetry). A right-click on a key **that surface holds** releases it out
     of `latched` or `sustained` and leaves Sustain mode on, so a pedalled chord comes apart a
     note at a time; on any other key it latches. The reason there is no twin is that the left
     click is already spoken for: under Sustain a second click on a ringing key restrikes it,
     by design, and that is the one behaviour Latch exists to distinguish from.

  Do not add further right-click-only paths without Owen's explicit say-so.
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
README and docs. To reach a control, don't synthesize clicks: posted WM_LBUTTONDOWN never
reaches JUCE. Invoke the button through UI Automation instead (`-InvokeButtons`); no cursor
movement involved.

Four things will bite otherwise:

- **Pass `-WindowTitle` for anything but plain Keys.** The default target is
  `MainWindowHandle`, a heuristic that lands on the *hosted instrument's* GUI in Keys Host
  (that GUI is a top-level window of the same process) and on an arbitrary section once any
  are detached. The titles are `Keys Host`, and `Keys Controls` / `Keys Arpeggiator` /
  `Keys Chord Pads` / `Keys Keyboard` for the four detached sections (the `wire(...)` calls in
  `PluginEditor.cpp`). `Keys Centre` and `Keys Transcribe` no longer exist.
- **The Detach buttons are named per section**, because four buttons reading "Detach" are
  four identical accessible names. Invoke `Detach Pads`, `Re-dock Keyboard`, and so on; the
  name flips with the button's state.
- **A section bar *is* reachable** (2026-07-30). `SectionBar` is a `juce::Button` that calls
  `setTitle(caption + " section")`, so `-InvokeButtons "Arp section"` folds or unfolds a
  section from a script and no shot needs Owen to click a bar first. The name carries the
  " section" suffix on purpose: UIA takes the first match, and a bar answering to the bare
  caption would collide with a control riding on it. Any doc saying a bar is a plain Component
  with no accessibility handler is out of date. Combo
  boxes are reachable by their *current* text (`-SetValues "Up=Pattern"`), but two combos
  can read the same thing (Shape and the strum Dir were both "Up"), and it takes the first
  match; set the other one out of the way first.
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
