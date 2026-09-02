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

Read `docs/ARCHITECTURE.md` first for how the pieces fit, and `docs/DECISIONS.md` for why they
fit that way: that is the dated log this section used to be, newest first, with Owen's own words
where they were the ask and every superseded claim kept and marked where it sits. What follows
is the map of what is true today. Every rule ends with the entry that argues it, so the
reasoning is one search away.

### The files

`src/`

- `PluginProcessor.{h,cpp}` - `KeysProcessor`: the parameters, the note path, the four arp
  lines, the chord pads, the session tree, undo, the take. Everything that outlives a view.
- `KeysParams.{h,cpp}` - `createLayout` and the migrations.
- `LayoutState.h` - the persisted UI layout struct.
- `TakeRecorder.{h,cpp}` - take capture.
- `PluginEditor.{h,cpp}` - `KeysEditor`: the four sections, their bars, the detach machinery,
  and the window's own floors (`minWidthForView`, `idealHeight`).
- `ArpEngine.h` - the arpeggiator core, one per line. Pure and header-only so it unit-tests;
  the only playhead reader in Keys, and only while its rate is in Sync.
- `NoteMath.h` - which MIDI note a drawn key sends: the scale-lock snap, then the octave.
- `Chords.h` - naming a chord from its sounding notes. `ChordSuggest.h` - where one could go next.
- `ScaleModes.h` - the modes generation reads. Not the kit's scale table: it answers a quality
  per degree as well as membership.
- `ChordGen.h` - the weighted pool, tiered by scale compliance. `ChordSources.h` - circle of
  fifths, Neo-Riemannian PLR, progression templates, negative harmony, planing, and
  `applyVoiceLeading`. `ChordMarkov.h` with `MarkovData.h` - the bigram chain and its 88
  mood-tagged progressions. `ChordLibrary.h` - 355 named progressions stored as roman numerals.
- `ChordVoicing.h` - the three voicing passes (`applyMajorMinorBias`, `fitVoicing`,
  `applyVoicingPipeline`), pure, so a test reaches them without a live processor.
- `ChordNumerals.h` - the roman numeral a card prints, in one place rather than one per surface.
- `EuclidGen.h` - Bjorklund, for the Euclid strip's fill of the Chance lane.

`src/ui/`

- `NoteSurface.{h,cpp}` - the note bookkeeping every playable surface derives from.
  `PianoKeyboard.{h,cpp}` - the keybed's geometry and paint over it.
- `ArpPanel.{h,cpp}` - the arp section: the All view, the three deep pages, the slot row, and
  the panel's own derived floors.
- `MacroRow.{h,cpp}` - the macro card and its layout constants.
- `LaneGrid.{h,cpp}` - the lane grid, the loop bar and the MUTE strip.
- `ArpRateMode.h` - the Sync/Hz attachment swap, written once for both surfaces that turn a
  rate dial.
- `ChordPads.{h,cpp}` - the twelve-pad strip, the live chord card, and the two range knobs.
- `ChordGenMenu.{h,cpp}` - the generator's brain: the eight sources, the settings reads, the
  audition path. Draws nothing and is a member of the editor, so it outlives every window.
- `ChordGenPanel.{h,cpp}`, `ChordTray.{h,cpp}`, `SourceViz.{h,cpp}` - the generator's window,
  its tray of candidates with the reference card, and its read-only diagram of the source.
- `ChordLibraryPanel.{h,cpp}` - the library window: twelve rows a page, Hear, place, star, Follows.
- `TakePanel.{h,cpp}` - the take's own window. A view of `takeNotes()`, never an editor.
- `KnobBank.{h,cpp}` with `CCMenu.h` - the eight assignable CC knobs and their one-click picker.
- `RangeKnob.h` - a rotary whose own arc is a band, with the satellite that drags it.
  `RangeSlider.h` - the two-value slider under Strum and the pads' Humanize.
- `ChordDrag.h` - the drag payload, plus `taken` and `consumed`, the two vetoes JUCE has no
  opinion about.
- `ComboMenu.h` - the two-column popup Shape and Harmony share. `StepComboBox.h` - unused, kept.
- `SectionBar.h` - a section's fold header. `DetachedWindow.h` - a section, or a window-only
  panel, in a frame of its own.
- `KeysLookAndFeel.{h,cpp}` - the skin: every colour and font as a `skin::` token.

`src/host/` is Keys Host: `KeysHostProcessor` runs one instrument VST3 in-process,
`KeysHostEditor` owns the window and the instrument chip's menu. `src/mcp/KeysMcp.{h,cpp}`
registers the tools on the kit's MCP server. `tests/` holds `ArpTests`, `StateTests`,
`LayoutTests`, `TakeTests`, `ChordSourceTests`, `ChordLibraryTests`, `MarkovTests` and
`McpTests`, all run from `KeysTests.cpp`.

### The rules

**The note path is one-way through a collector.** The editor and every playable surface call
`processor.noteOn` / `noteOff` on the message thread; `processBlock` drains the collector. The
UI never touches the outgoing `MidiBuffer`. `dest` says which queue a note is fired into: 0 is
the track output, 1..`numArpLines` a line's own input, and `chordCollector` is dest 0's second
queue, drained after the lines have lifted what they listen to, so no line can reach a pad's
chord. `chordStream` records which of dest 0's two queues opened a pitch, so its note-off
follows it there. (DECISIONS.md: **UI → audio note path is a**, and **A pad click never reaches
the arpeggiator, one level lower than before.**)

**One note-on per sounding pitch, released by the last owner.** `noteRefs` is a refcount per
destination stream; `noteOn` emits MIDI only on the 0 to 1 transition and `noteOff` only on 1 to
0, and `ArpEngine::Held::ons` counts the same way downstream. Any new chord source goes through
`noteOn` / `noteOff`, and Exclusive has to reach it through `stopAllChordPads`. The keybed's own
sounding set is the union of `pressed`, `latched` and `sustained`, diffed by
`NoteSurface::refresh()`. (DECISIONS.md: **One note-on per sounding pitch, released by the last
owner.**, and **Sounding = union of three sets.**)

**Four arp lines, routed by queue and never by mask.** `numArpLines` and `uiArpLines` are both
4. Line 0 keeps the bare parameter ids; B, C and D are `arp2*` / `arp3*` / `arp4*`. A chord
handed to a line is fired into that line's own `MidiMessageCollector` and only that engine
drains it, because a per-pitch ownership mask races: the message thread can clear an owner
before the matching note-off is drained, stranding that note in an engine's held set. A line
that is off still takes chords in; `enabled` gates firing alone. (DECISIONS.md: **Three
arpeggiator lines, A B C**, and **A line that is off still takes chords in; `enabled` gates only
firing**)

**The line bus flows downward, and the processor is the rule.** `runArpLines` walks the lines in
letter order on the audio thread, so each engine's `LineRecord` is readable by the lines running
after it. `ap.follow` is set to a source's record only when its index is strictly below this
line's, whatever the parameter says, which is what forbids a loop; the UI greys the letters, the
processor enforces it. `record.firedBefore` rolls over at the top of `process()`, not the end,
or a follower counts this block's fires twice. DUCK, RESET and NEIGHBOUR all read that record.
(DECISIONS.md: **The lines can hear each other, phase one: the bus, From, and DUCK**, and
**Phase two, the same day: RESET and NEIGHBOUR.**)

**The engine is stateless from the playhead, with three named exceptions.** Everything is
computed from the step index, so a transport jump lands right without walking there. The
exceptions are the Chain lane's `lastStepFired`, Legato's `prerollNext` lookahead, and the line
bus's `firedBefore` running count. Each self-corrects within a step or two, which is the price
that was agreed; a lane wanting more state than that needs a different design, not a bigger
cache. (DECISIONS.md: **Three lanes appended, and one of them is not stateless**, and
**`arpLegato`, appended, per line, default off. PARAMETER LAYOUT CHANGE.**)

**Track MIDI is held aside in `processBlock`, not gated in `runArpLines`.** With the global
`arpTrackMidi` off, the incoming stream is moved into `trackMidiAside` before the collector
merges and put back after the lines have run, note-offs included: by the time `runArpLines` sees
it, a clip's C4 and a clicked C4 are the same message. Closing the door releases what the *line*
took, through the per-line `trackHeldByLine` mask, never what the track holds. (DECISIONS.md:
**`arpTrackMidi`, appended, global, default false. PARAMETER LAYOUT CHANGE.**, and **The falling
edge must release what the *line* took, never what the *track* holds**)

**Scale Lock reaches the line's output.** `NoteMath::resolveOutputNote` snaps what you play into
a line; `ArpEngine::snapToMask`, called in `addHit`, snaps what the line plays out, in the one
place every emitted pitch passes through, and *before* the dedup, because two pitches rounding
onto one must collapse to one hit. It is global, and chord pads are untouched: a stored chord is
one you built. (DECISIONS.md: **Scale Lock reaches the line's output.**, and **The played note is
resolved at press time**)

**Eight chord sources, one voicing pipeline, in that order.** `ChordGenMenu::generateChords`
dispatches all but Markov, which keeps its own paths. Whatever any of them produced then runs
`chordvoicing::applyVoicingPipeline`: Lean (`applyMajorMinorBias`) first, because it changes
*which* notes a chord holds; `fitVoicing` second (root position, then the note count, then the
inversion); `applyVoiceLeading` last, because fitting moves whole chords between octaves and
would undo smoothing. The gates are constraints, not enables: unticked means the generator rolls
that setting itself, over the whole range the parameter can express. (DECISIONS.md: **Chord
generation is a weighted pool.**, and **A single note is a chord card**)

**Parameters and indexed lists are append-only, and `StateTests` pins them.** A new parameter is
**appended** to `createLayout`, and the golden list in `StateTests`' "parameter layout is
append-only" is extended to match; that is a **PARAMETER LAYOUT CHANGE** and belongs loudly in
the changelog, because a new parameter is absent from every saved session and every one of them
takes its default. The same rule and the same kind of test cover the `genSource` choice list and
`chordgen::types()`; the lane indices, the four Shape name lists and the three index-parallel
harmony tables must grow together and only at the end. A migration backfills what an absent
parameter should have meant. (DECISIONS.md: **Never reorder or insert into the `genSource`
choice list.**, and **Three lanes appended, and one of them is not stateless**)

**Undo is a subtree snapshot, and content only.** `pushUndo` stores `chordPadsToTree()` or
`arpToTree()`, the same trees the session file uses, so no action has a hand-written inverse and
anything whose data lands in one of those two trees is undoable for free. It covers chord pads,
arp lanes and arp slots, deliberately not parameters. `UndoGesture` is the RAII guard that makes
one gesture one entry, and undo releases every sounding chord first. (DECISIONS.md: **Undo is
content-only, and an entry is a subtree snapshot**)

**Four sections, and detaching is generic.** `KeysEditor::sections` is a table of `Section`
(Controls, Arp, Pads, Keyboard), each owning a holder, a Detach button and its `DetachedWindow`;
detaching is one re-parent, and `idealHeight()`, `syncSectionControls()` and `paint()` walk the
table rather than naming sections. Add a section by adding an entry. Only the left end of a bar
folds it (`SectionBar::hitTest` answers for `foldZone()` alone). What stays on a folded bar is
what you reach for while playing: the arp letters, All Off, Light keys, Track MIDI, Hold off,
Quantize, the tempo, Root, Scale, Lock, Voices, CH, the pads' generator cluster, and the
keybed's Size, Octave and hold switches. (DECISIONS.md: **Every section detaches, and the
machinery is generic.**)

**Every layout floor is derived and measured, never written down.**
`KeysEditor::minWidthForView` takes the max of each bar's own content width (the `controlsbar` /
`arpbar` / `padbar` / `keyboardbar` namespaces, Controls measured at its *tight* cell set),
`ArpPanel::minPanelWidth()` plus the two margins, and a 1320 shipped-floor term, so the day a
bar outgrows the floor the bar wins instead of a literal. `ArpPanel::minMacroWidth()` moves the
moment `numKnobs` does. `contentHeight()` returns `pageHeight()`, so nothing reserves room it
does not use. `LayoutTests` lays a real editor out at whatever those return and measures by
**overlap and containment**, which is the only thing that can see a starved bar. (DECISIONS.md:
**"The editor's minimum width" was the wrong floor, and that is the standing lesson**, and **A
window opens at its content's height, and its resize floor tracks it**)

**The macro card is eleven knobs and two rows, and both rows are floors.** The `Knob` enum is UI
indexing and nothing stores it, so inserting there is free: OctShift, Gate, Density, Duck,
Mutate, Stray, Lock, Swing, Offset, Vel, H.Time. The bottom strip's six chips and the chord
readout are the wider of the card's two rows, so `minMacroWidth()` takes the larger. Anything
fixed-size on the top row (the dice, the Keybed chip) is reserved *before* Shape takes its cut,
and Shape is capped rather than merely elastic. Nothing on a card is ever disabled: an off line
is scrimmed, so a chord dropped on it still lands. (DECISIONS.md: **DENSITY is `arpChance` given
a face on the macro card.**, **The card's bottom strip is a floor too, and was already over
it.**, and **The keybed switch is on the card, and it says Keybed**)

**One strip of pads, one tray of candidates.** Twelve pads a page over four pages
(`padsPerPage`, `numPadPages`); a card is all playing surface, so its edits live on a
right-click menu and its lock shows as a corner dot that is never a target. The generator's tray
holds candidates that belong to no slot and die with the window, plus a reference card the
tray's own actions cannot touch. Nothing generation does overwrites a chord. Every arp target
takes any chord drag and **copies** it; only a pad is storage. (DECISIONS.md: **Twelve pads a
page, with Strum and Humanize in the columns that freed up**, **The reference card is the tray's
fixed point**, and **The chord library is a table you look things up in, and its own window**)

**The settings gear holds the behaviour switches that are not parameters.** Three ticks (hold
visuals during sustain, sustained drag leaves a trail, sustained notes propose chords, the last
off by default), Reset all settings, and the links. `resetAllParameters()` walks every parameter
back to its default **and** those six behaviour switches, releases every sounding note first,
and never touches chord pads, arp patterns or slots. (DECISIONS.md: **A settings gear on the
Controls bar, plugin-level like the swatch**, and **Reset all settings, and it takes the
settings in its own menu.**)

**The take is captured on the audio thread and written at stop.** `captureBlock` copies the
stream leaving `processBlock` into a ring while REC is down; `writeTake()` puts it in
`takeFolder()` at once, and `setRecording` stays a pure state change so the capture tests
without writing into Documents. The tempo is frozen at arm. `TakePanel` draws `takeNotes()`,
built from the same sequence the file holds, so the picture is the bytes. (DECISIONS.md: **Keys
records itself, and a take you can look at before it leaves**)

**The MCP bridge is a server per instance, and the shim is the kit's.** `okstudio::mcp::Server`
takes a new loopback port each launch and publishes it; every tool handler is marshalled to the
message thread, so a tool body calls the processor exactly as the UI does. The stdio bridge is
`keys-mcp.exe`, whose reconnect logic lives in the kit. Probe the live port out of
`%APPDATA%\OK Studio\mcp` to tell a broken bridge from a broken plugin in one step.
(DECISIONS.md: **MCP bridge.**)

**Keys Host is Keys plus one instrument, and it asks for its size.** `KeysHostProcessor` runs a
hosted VST3 in-process on the MIDI Keys emits; `KeysHostEditor` has no bar of its own and
reaches Load, Show/Hide and Eject through the editor's Instrument chip, which only a host
supplying `onBuildInstrumentMenu` shows. Window geometry comes from `KeysEditor::idealHeight()`
and `minWidthForView()`, asked for rather than copied. (DECISIONS.md: **An Instrument chip on
the Controls bar, for a host that wants one**, and **A window opens at its content's height, and
its resize floor tracks it**)

### Traps already paid for

The log keeps restating these, so they are collected here, one search from the round that paid.

- **Reserve the fixed-size control first.** An elastic control asked to leave room for something
  else starves its neighbour, silently. (DECISIONS.md: **Reserve the fixed-size control first,
  always**, and **A crowded row grows a strip; it does not squeeze its targets**)
- **A constant that is a max over several sums grows the gap under every view but the tallest**,
  and nothing on screen says so. (DECISIONS.md: **A line's deep view is three pages, and the arp
  panel is one fixed height**)
- **Two writers on one parameter is a control that fights the hand.** When several controls
  misbehave in unrelated-looking ways at once, look for one writer they share before redesigning
  any of them. (DECISIONS.md: **A parameter with two writers, again: the pad range knobs were
  fighting the hand**)
- **A field that is copied but never read makes the copy look like the feature.** (DECISIONS.md:
  **A hit and its harmony voices share one velocity draw.**)
- **Measure widths, do not assert them in a comment.** A width written down goes stale in
  silence; `LayoutTests` measures against the real longest entry instead. (DECISIONS.md: **The
  Shape combo is capped, and the dice lives in what that freed**)
- **A layout sweep that reads `getWidth()` cannot fail.** Every chip is placed with
  `withSizeKeepingCentre` and `removeFromRight` clamps, so a starved bar produces chips of
  exactly the right size sitting on top of each other. Overlap and containment are the only
  things that can see it. (DECISIONS.md: **The arp bar's floor did not move.**)
- **A stale test binary runs green.** MSBuild's `/m` becomes a path under Git Bash, so `ctest`
  can pass against the previous build; check what actually got linked before trusting a green
  suite. (DECISIONS.md: **`Keys_tests` links with `/STACK:8388608`, and the reason is worth
  keeping.**)
- **A hash over "which step" is the wrong key whenever a step can carry more than one of the
  thing being decided.** (DECISIONS.md: **A step's strays are per note, not per step**)
- **A timer pull has to compare before it writes**, or it re-runs for the rest of the session
  under a comment claiming it early-outs, with no symptom until somebody profiles the paint.
  (DECISIONS.md: **A timer pull has to compare before it writes, and two here did not**)
- **A fix that names its call sites one at a time is only as complete as that list, and nothing
  checks the list.** Ask what else reaches the thing being fixed. (DECISIONS.md: **Every voice,
  no carve-out - and the first cut had one**)
- **Two tables that must agree are two tables that will drift.** Derive the second from the
  first. (DECISIONS.md: **`buildLaneRow` no longer takes a lo/hi pair, and that was a real bug,
  not tidying.**)
- **The label can be right and the implementation reading it wrong**, with something plausible
  in the air either way. Sweep the whole list in a test rather than spot-checking the entry you
  doubt. (DECISIONS.md: **"+ Octave & 5th" names two intervals and plays two notes**)
- **Widening a default band is free; sliding one is not.** A midpoint some migration converts
  against has to stay where it is. (DECISIONS.md: **Humanize's band was widened around its
  centre, not moved**)
- **Append a lane and you owe it a length.** A lane that did not exist when a session was saved
  arrives at the default while its neighbours are longer, and the grid draws each at its own
  length. (DECISIONS.md: **A lane appended after a session was saved arrives at the wrong
  length**)
- **A feel control applied to something inaudible is not neutral, it is a delay.**
  (DECISIONS.md: **Arp slots carry chords, not just patterns.**)

## Invariants (don't break)

- **Keep the MIDI input bus** (`NEEDS_MIDI_INPUT TRUE`, set by `okstudio_add_plugin`;
  `acceptsMidi()` true). Ableton refuses to load an instrument without one. The
  failure is silent-looking ("This VST3 plug-in could not be opened") and pluginval
  passes without the bus, so only a real Live load test catches it.
- **The 34 px floor applies to every target, including the small ones.** A check box, a stepper's
  `-`/`+` pair and a caption-row button are targets exactly as a `TextButton` is, and all three
  have been built too small at least once (2026-08-01: a Reroll button at 18 px in a caption
  strip, note-count steppers collapsed to 22 px in a 120 px cell, and a tick box that would have
  gone in a 14 px label strip). The fix is always the same: give the cell the height or width the
  target needs, rather than shrinking the target into the cell. Where a value has a slider, it
  also needs a click-only path, because **a slider is a drag** - the arp's rate steppers and the
  generator's note-count steppers exist for that reason, not as a convenience.
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
  1. *The chord-pad card menu is right-click* (2026-07-22, widened 2026-07-30, 2026-08-16,
     2026-08-17 and 2026-08-23). Seventeen rows:
     Edit on keyboard / Clear pad / Lock, **Copy chord** / **Paste chord** / **Save chord as
     MIDI** (2026-08-17, Owen: "need to be able to copy paste chords"), the two chord-shaping
     edits (Octave down/up, Next voicing), the generator's two per-card actions (New chord,
     Next: could follow),
     **Send to arp A** through **D**,
     **Send to arp slot**, and - alone in a group at the foot - **Clear page** (entry 6 below).
     Some of it is an accelerator - Clear pad is also a drag off the
     strip, and Fill and Regen on the bar are New chord in bulk - but the per-card edits are
     reached from this menu and nowhere else, because a card is all playing surface and there
     is nowhere left on it to put a button. Ending an edit is the exception that proves it: the
     tick on the card being edited is a left click. **Save chord as MIDI is here for the same
     reason**: a menu row cannot start a drag, so writing the file and revealing it in Explorer,
     ready for a short drag onto an Ableton track, is the closest one click gets to a route the
     Windows clipboard cannot offer (Live's own clipboard will not accept a paste from it). The
     paths below are the ones Owen ruled on
     one at a time; entry 2 has since been retired, and is kept because knowing why a rule
     existed is what stops it being reinvented.
  2. ***Send to arp slot* had no left-click twin* (2026-07-25), **and now it does** (2026-08-01).
     The reason it was ever an exception is that binding a chord to one *particular* slot needs
     a target picker; a drag is one, so **dragging a chord card onto a slot card** does the same
     job and the exception is retired. The menu item stays as the accelerator it always was.
     This is the one entry on this list that closed rather than opened, and it closed because
     the thing it was waiting for got built, not because anybody changed their mind: do not
     re-open it by removing the drag. A left click on a card with a line On used to be the
     unaimed way to feed the current line; Owen retired that on 2026-08-02 ("I don't want it
     to send it to the arpeggiator unless you drag it"), so the drag - onto a line's card,
     its letter tab on the arp bar, or a slot - is now the *only* left-click path into a
     line, and a click just plays the pad. Still mouse-only clean: a drag is a left gesture.
     **`Send to arp A` / `B` on the same menu are that drag's accelerator** (2026-08-16, Owen:
     "I'd like to be able to right click on a chord pad and say send to ARP a or b"), the exact
     relationship Send to arp slot has with a drop on a slot card - so it opens no new
     right-click-only path, and the drag stays the left-click one. Both go through
     `KeysEditor::sendPadToArpLine`, the one path a pad's chord takes into a line, and both are
     live on a line that is switched **off**: a line that is off still takes chords in. They part
     on `followAim` alone: the drop moves the aim to the line it landed on, the menu row moves a
     chord and leaves the panel where it was.
  3. ***Lock / Unlock has no left-click twin*** (2026-07-30, Owen: "I don't want the lock
     button to be visible. I only want it to be in right click"). This is the one path where a
     left-click twin was **built and then deliberately taken away**: a lock chip sat in the
     top-right corner of a filled card for a few hours the same day, and it cost the card
     roughly a quarter of its surface - dead to playing, dragging and feeding the arp, which is
     everything a card is for. A locked card still *shows* its state as a corner dot, so the
     lock is readable without opening the menu; the dot is a mark and never a target. **Do not
     "restore" a clickable lock.** The generator's settings were also right-click only for part
     of that day and are not any more: they are in a window opened by the **Generator** chip on
     the Pads bar, alongside Fill and Regen, with Key / Mode / Scale Compliance as combo boxes
     there too.
  4. *A right-click release of a pedal-held note has no left-click twin* (2026-07-30, Owen
     sanctioned the asymmetry). A right-click on a key **that surface holds** releases it out
     of `latched` or `sustained` and leaves Sustain mode on, so a pedalled chord comes apart a
     note at a time; on any other key it latches. The reason there is no twin is that the left
     click is already spoken for: under Sustain a second click on a ringing key restrikes it,
     by design, and that is the one behaviour Latch exists to distinguish from.
  5. *The **audition tray**'s card menu is right-click* (2026-08-01, Owen: "when you right click
     on a chord in there, I want you to have a whole bunch of options about trying to find
     similar ones or what might come next"). Eight rows on a filled card: Send to first empty pad,
     the two seeded
     fills (Fill tray with similar chords / with what could follow), the three shaping edits
     (Octave down/up, Next voicing), New chord here and Clear this card. **Two rows on an empty
     one** (2026-08-16): New chord here and Fill every empty card, the only two that need no seed.
     It earns the exception
     the way the pad card menu did - a tray card is all playing surface, and there is nowhere on
     it for eight buttons. Most of it has a left-click twin: Send to first empty pad is the
     commit drag with the aim taken out, and the two fills are the **Similar** and **Could
     follow** buttons beside the reference card, seeded from the reference instead of from that
     card. **Opening this menu makes no sound.** It auditioned the card for a few minutes on the
     day it was built and came straight back out (Owen: "when you right click, it plays the
     chord. We don't want it to play") - right-clicking to reach Clear made a noise on the way to
     throwing the chord away. Hearing a chord is a left click and nothing else, everywhere.

  6. ***Clear page* has no left-click twin* (2026-08-23, Owen: "we need to be able to clear all
     the chords on a pad page", then choosing this over a chip on the Pads bar when shown both).
     It empties every unlocked pad on the current page in one undo entry, and it is the only row
     on this menu that acts on anything but the card it was opened from - hence its own group at
     the foot, rather than a seat beside Clear pad, whose name it would otherwise read as the
     plural of. **The bar was the worse home and the record already said why**: the old Clear
     chip left that bar because a page wipe sitting 4 px from Regen and a few px from the page
     buttons is a destructive action on top of the two things you click constantly. The travel of
     a right-click is the price, and it is the right one. What makes this affordable now and did
     not on 2026-08-01, when the last page wipe was deleted for want of a home, is **undo**: the
     wipe is one entry (`KeysProcessor::clearChordPadPage`), so it is one click back. Locked pads
     are spared, the rule Regen follows. Do not restore a bar chip for it.

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
  `v<version>`; every verification gate stays fail-closed. **`docs/RELEASE.md` is the
  checklist that keeps all four true** (2026-08-20), alpha-osk's pipeline adapted to a
  JUCE plugin: bump `project(Keys VERSION)`, stamp the changelog, `build.ps1 -Installer`
  with the eToken in from a non-elevated shell, Live load test, tag, then
  `gh release create --repo okstudio1/keys-releases`. Every one of those contracts fails
  *silently* when broken, which is why the doc leads with them rather than with the steps.

## Skin

`src/ui/KeysLookAndFeel.h` holds every colour and font as a `skin::` token; never reintroduce
per-file hex chrome. One trap, learned the hard way on 2026-08-01: **judge the dim text tokens
against the background at the size they are actually used, not against `skin::text`.**
`textDim` and `textFaint` had been chosen by eye next to `text` and were too dark to read, because
nearly everything wearing them is 9 to 11 px letter-spaced uppercase (the micro-caps captions, the
note list under every chord card) and small letterforms need far more contrast than large ones for
the same effort. Both were lifted; `text` was never the problem.

## Reference manuals

Nineteen PDFs sit in **`manuals/`**, eighteen products (they are gitignored, so a fresh clone has
none - `manuals/README.md` is the manifest and carries a working download URL for every one of
them, plus the traps: two are account-gated, two Arturia links are a version behind, and the
Turing Machine PDF is a build guide rather than the concept). They were loose in the repo root
until 2026-08-17. `docs/REFERENCES.md` records what each one contributed,
what Keys took from it and what it deliberately did not. **Read the relevant one before
designing a feature, not after.** This is not a formality: the Serum guide corrected a built
`RangeKnob` satellite that looked right on screen, and the Cthulhu manual corrected a whole
randomness feature that had shipped as a global knob when the reference does it per step
(2026-08-14, Owen: "look at reference manuals"). Twice now a manual has been cheaper than the
rebuild it would have prevented.

The short version of which is which: **Cthulhu** is the arp's own architecture (per-parameter
step lanes, Link Lengths, the Rand lane, mute-preserves-value); **Kirnu Cream** is the richest
per-step vocabulary and the best source of unbuilt ideas; **Stochas** is probability and the
chain/conditional trigger Keys does not have; **Serum 2** is UI, not sequencing; **Subharmonicon**
is the polyrhythm dividers and the undertone series; **MatrixBrute** has ties and slides;
**Numerology 4** draws the skip-versus-mute distinction; **Arturia Acid V** is the 303 sequencer
and the source of `docs/ACID_DESIGN.md` (added 2026-08-16, proposed and unbuilt).
`docs/REFERENCES.md` ranks the three unbuilt ideas worth having.

**`docs/LINE_INTERACTION.md` is the design for the four lines listening to each other; its bus,
From, DUCK, RESET and NEIGHBOUR are built, CLOCK and everything after it is proposed and
unbuilt** (2026-09-01, Owen: *"can we get the arpeggiators to interact with each other, like
the step sequencers, so we can get interesting variations"*). One piece of plumbing - a per-line
record written in letter order on the audio thread and readable by the lines that run after it,
the same ordering `arpNoteLines` already leans on - and eight mechanisms over it: DUCK (the
hocket), NEIGHBOUR (the Chain lane reading another line), RESET, CLOCK, SHADOW, LOCK SYNC,
HANDOVER (deferred: it needs a runtime state beside On) and VEL FOLLOW. Two rules every one of
them keeps: **signal flows downward, A to D, so nothing can loop**, and **a source that is off
or silent leaves its follower playing as today**. Do not describe any of it as shipping.

**Eleven more arrived 2026-08-17** (Owen: "get the manuals. wide research") and they are surveyed
in `docs/SEQUENCER_LANDSCAPE.md`, not here: REFERENCES.md is the record of what Keys **took** from
a manual, and nothing has been taken from these yet. That file is the layer above it - the map of
which sequencer archetypes exist, which six Keys already is, and which six it is one feature away
from. The short version: **Hapax** has offline transforms over a selection (Flip, Curve, Shuffle,
Randomize), which is the palette Keys' own Select has been waiting for; **Metropolix** has the
accumulator, movement without randomness; **Torso T-1** dials a part instead of drawing one
(Phrase, Range, Style); **Ableton's Follow Actions** are the general form of Keys' Chain;
**Digitakt II** has the rest of the conditional-trigger vocabulary (A:B, 1ST/LST, NEI, FILL);
the **Turing Machine** is the one randomness Keys lacks, the kind that wanders and then hardens.
**René**, **Deluge**, **OXI One**, **Pamela's Pro Workout** and **KeyStep Pro** are coverage and
cross-checks rather than sources.

## Screenshots for docs

Use `scripts/capture-window.ps1`, which is the PrintWindow approach (never
SetForegroundWindow/SetCursorPos — Owen is often using the machine, and a mis-capture can
grab his private windows). Screenshots live in `assets/screenshots/`, referenced from
README and docs. To reach a control, don't synthesize clicks: posted WM_LBUTTONDOWN never
reaches JUCE. Invoke the button through UI Automation instead (`-InvokeButtons`); no cursor
movement involved.

Four things will bite otherwise:

- **`-WindowTitle` changes what is captured, NOT where `-InvokeButtons` and `-SetValues` look.**
  Those two resolve against `MainWindowHandle`, always, which in Keys Host is the *hosted synth's*
  GUI. So `-WindowTitle "Keys Chord Generator" -InvokeButtons "Fill"` fails with "UIA element not
  found" even though the button plainly exists, and so does naming the main window. To drive a
  control in any window that is not `MainWindowHandle`, enumerate it yourself: find the top-level
  `AutomationElement` whose `Name` matches, `FindFirst` the control by name under it, and Invoke
  its `InvokePattern`. Combo boxes are UIA read-only, so `ValuePattern.SetValue` throws "Value is
  read-only"; expand with `ExpandCollapsePattern` and Invoke the row instead, which is what the
  script's own `-SetValues` does. Worth knowing before you spend twenty minutes on it.
- **Pass `-WindowTitle` for anything but plain Keys.** The default target is
  `MainWindowHandle`, a heuristic that lands on the *hosted instrument's* GUI in Keys Host
  (that GUI is a top-level window of the same process) and on an arbitrary section once any
  are detached. The titles are `Keys Host`, and `Keys Controls` / `Keys Arpeggiator` /
  `Keys Chord Pads` / `Keys Keyboard` for the four detached sections (the `wire(...)` calls in
  `PluginEditor.cpp`), plus `Keys Chord Generator` and `Keys Chord Library`, which are windows
  but not sections (`KeysEditor::setChordGenWindowOpen` / `setChordLibWindowOpen`). `Keys Centre`
  and `Keys Transcribe` no longer exist.
  **Opening a second window moves `MainWindowHandle`** (2026-08-18, hit while shooting the library):
  with the library up, `-InvokeButtons "Chord generator window"` fails, because that chip is on the
  *main* window and the heuristic has landed on the library. Enumerate the `Keys` top-level element
  yourself and `FindFirst` under it - the same five-line UIA script the Keys Host trap needs.
- **A disabled control is absent from the UIA tree, not merely marked disabled** (2026-08-18). The
  library window's twenty-four row buttons could not be found at all until the generator window was
  open, which is what makes their greying real rather than cosmetic - but it also means "element not
  found" is the *expected* answer for a control that is correctly greyed, and not evidence that the
  control is missing. Check whether it should be enabled before hunting for the name.
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
- **The arp's own controls are named per line**, for the same first-match reason: on the bar,
  `Arp line A` / `B` are that line's own On switch (2026-08-02, seventh pass) as well as a
  chord-drop target, and the slot cards are `Arp slot 1`..`12`. **`Arp line A` used to carry a
  " tab" suffix** (`Arp line A tab`), back when a separate On chip did the switching and the
  tab only navigated the panel; the chip is gone, the tab absorbed its job, and the suffix went
  with it - do not look for `Arp line A tab` any more, and do not expect it to hide when the
  section folds, since it is a power switch now and stays on the bar folded or not. (The Pads
  bar's cycling letter, `Arp target line`, is **gone** since 2026-08-02 too - do not look for
  that either.) Hold off is `Arp hold off`, and the Quantize combo beside A, B and All is
  `Arp launch quantize`. The tempo is **`Tempo`, on the Controls bar** - it answered to
  `Arp BPM` on the arp bar for one build. Beside it, the **Sync** chip (2026-08-02) that opts
  out of following the host's tempo answers to `Tempo sync`, on-screen word "Sync". Beside that
  on the bar, and reachable by their
  *current text* the way every combo is, sit Root, Scale, Voices and the MIDI channel; Scale
  Lock is a toggle whose on-screen word is **"Lock"** but whose accessible name is still
  `Scale Lock`, so a script asks for the full phrase and a reader hears it. In Keys Host only,
  the same bar also carries `Instrument`, the chip whose menu holds Load/Show-Hide/Eject - it
  is invisible in plain Keys, so a script targeting it there will not find it, by design. The
  Draw page's own controls, all 2026-08-14: the lane tabs answer to their visible word
  (`Note`, `Octave`, `Velocity`, `Gate`, `Ratchet`, **`Chance`** - `Prob` until that day -
  `Transpose`, `Late`, `Harmony`, `Chord`, **`Rand`**, **`Chain`**, **`Reset`** (2026-08-18)), and the tools beside them
  are `Select steps`, `Reset lane`, `Roll lane`, `Less roll` / `More roll`, `Copy steps` /
  `Paste steps` (2026-08-18) and `Harmony voice`. The lane strip added the same day answers to
  `Lane on`, `Lane direction back` / `Lane direction forward` and the loop bar's own `Lane loop`,
  which is a plain Component with no invokable pattern - drive the lane's `loopFrom` / `loopTo`
  through the arp tree instead. **Steps, Speed and Link are on this page now**, not the Play page,
  so a script looking for them there will not find them.
  **`Chain` collides**: the progression button on the Cards page is also called Chain, and UIA
  takes the first match - they are never on screen together, so pick the page first. Mute has a
  lane but **no tab**, so there is nothing named for it; drive `laneMute` through MCP instead.
  The
  view tabs on the arp bar are `Arp all tab` plus the three page tabs `Arp page Play` /
  `Arp page Cards` / `Arp page Draw` (2026-08-14; they answered to `Arp page Steps` / `Slots` /
  `Setup` for one build the same day - do not look for those). The page tabs exist only in a
  line's deep view, so a script must leave the macro view before it can find one.
  **`-InvokeButtons` cannot reach any of them in Keys Host**: it resolves against
  `MainWindowHandle`, which there is the hosted synth's GUI, so enumerate the `Keys Host`
  top-level `AutomationElement` yourself and `FindFirst` under it. That is the trap documented
  further up, hit again on 2026-08-14 - a five-line UIA script is the way past it. The macro
  view's own controls are prefixed `Macro` so they never collide with the bar chips or a tab:
  `Macro rate A`, `Macro rate mode A`, `Macro shape A`,
  `Macro reroll A` (the dice, 2026-08-21 - it is a plain
  `juce::Button` and does offer an InvokePattern, but it is **disabled on every shape but Random
  Once**, and a disabled control is absent from the UIA tree entirely, so "element not found" is
  the expected answer there rather than evidence it is missing),
  `Macro dot A` / `Macro tuplet A` / `Macro anchor A` / `Macro legato A` / `Macro follows A` (the
  last two from 2026-09-01 - the From chip is a combo, so it is also reachable by its current text,
  "Off" or "From A"; `Macro trip A` until 2026-08-03, when
  the toggle became the Tuplet **combo**; the band's twin answers to `Arp tuplet`. Being a combo
  it is also reachable by its current text - "Straight", "Triplet", "5-tuplet" - with the usual
  first-match caveat), and `Macro OCT A` / `Macro GATE A` /
  `Macro VEL A` / `Macro H.TIME A` / ... one per knob heading - **eleven from 2026-09-01**, when
  `Macro DENSITY A` joined the row between GATE and MUTATE (it is `arpChance`, the Play page's
  slider, under the name it reads as on a card) and, later that day, `Macro DUCK A` beside it;
  nine from 2026-08-21, when
  `Macro STRAY A` joined the row between MUTATE and LOCK. It was eight from 2026-08-18:
  `Macro H.VEL A` retired on 2026-08-17 when Humanize Velocity folded into VEL's own ring (see
  the RangeKnob bullet above), and `Macro CHANCE A` was replaced the next day by `Macro MUTATE A`
  and `Macro LOCK A`. Do not look for `Macro CHANCE A` or `Macro H.VEL A`. **The harmony strip
  (2026-08-19)**: each card also answers to `Macro harmony 1 A` / `Macro harmony 2 A` (the
  interval combos, reachable by current text like every combo) and `Macro harmony 1 chance A` /
  `Macro harmony 2 chance A` (their knobs) - and every per-line name now comes in A through
  **D**, since all four lines are on screen. The bottom-row fold answers to
  `Minimise arp lines C and D` on the bar (on-screen words "Lines C D") and the collapsed strip
  to `Expand arp lines C and D`; the strip is a plain Component offering no invokable pattern, so
  drive the *chip*, and `-InvokeButtons` reaches neither in Keys Host - enumerate the `Keys Host`
  top-level element and `FindFirst` under it, the trap documented further up. From 2026-08-03, H.TIME carries a *second* name for its ring,
  `Macro H.TIME range A`, and a third for the satellite, `Macro H.TIME range handle A`, since
  face, ring and handle are three controls in one cell. **VEL gained the matching pair on
  2026-08-17**, `Macro VEL range A` / `Macro VEL range handle A` - ring and handle are plain
  Components on both knobs, so they answer to a name but offer no UIA pattern to invoke; drive
  `arpHumanizeSpan` (H.TIME's ring) or `arpHumanVel` (VEL's ring is that parameter directly, not
  a span of VEL's own value) instead - plus
  `Macro details A` / `B` (2026-08-02, seventh pass), the button that opens that line's deep
  view now that A and B on the bar no longer do. **`Macro line A` / `B` no longer exist**: that
  was the macro card's own On toggle, deleted the same pass, replaced by a scrim over the whole
  card rather than a second control. `Macro latch A`, `Macro keys A` and `Macro chain A` went
  with the row controls on 2026-08-02; the per-line band's keybed toggle answers to `Arp keybed`
  (the parameter is still `arpKeys`; it answered to `Arp play` and read "Play" until 2026-09-01,
  when the same switch came back onto the card as `Macro keybed A` and both took the word for the
  thing they gate - "Play" had read as the line's On, and "Keys" collides with the bar's Light
  keys). On the arp bar: `Arp all off`,
  `Arp light keys` and, from 2026-08-27, `Arp track MIDI` (on-screen words "Track MIDI").
- **Two known traps in this script, hit on 2026-08-01 and not yet fixed.** `-SetValues` is
  applied *before* `-InvokeButtons`, so a value inside a folded section cannot be reached in
  the same run - unfold in one call with `-KeepOpen`, set in the next. And a `-SetValues` that
  throws can leave the combo's popup open as a top-level window, after which every subsequent
  lookup roots itself on that popup: the tell is a capture that comes out about 156x159, and
  the only way back is to restart the app. Budget for it, or drive the parameter another way.
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
the playhead for tempo-synced scheduling, Contour-style, free-runs on an internal
clock when the transport is stopped, and does not read the playhead at all with the rate
in Hz. It is the only playhead consumer in Keys; see `docs/ARP_DESIGN.md`.
