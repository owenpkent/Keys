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

**A line can be thinned from its card, and a thinned line can hold through the gaps
(2026-09-01).** Owen: *"a density knob where it controls, like, how much notes there are, and also
a legato button. So when the density is lower or a note is skipped, it continues nicely"* - then,
shown the choices, took Density as Chance promoted to the card, and Legato as hold-through-skips
rather than every-note-to-the-next.

- **DENSITY is `arpChance` given a face on the macro card.** No new parameter. The Play page's
  slider has carried it since Mutate and Lock replaced Chance on the card (2026-08-18), and the
  card is where you sit and turn things; "how many notes" is the knob Owen reached for there.
  Renamed **Density on both surfaces** and in the host-facing parameter name (the id does not
  move), and deliberately *not* the lane's word: the Chance lane is one step's odds, the knob
  thins the whole line, and one word for two things is the mistake the lane's own rename fixed.
  `kDensity` sits between `kGate` and `kMutate` - how long, how many, then how the run explores -
  free because the `Knob` enum is UI indexing. **Ten knobs**, so "a tenth does not fit" below is
  history: `minMacroWidth()` moved itself when `numKnobs` did, and the docked card at 1320 had
  the room (614 px against a 462 px strip). The half of the ask that would *add* notes is the
  Ratchet lane and the harmony voices, which already exist.
- **`arpLegato`, appended, per line, default off. PARAMETER LAYOUT CHANGE.** Off is byte-for-byte
  what the engine did. On, a step that does not fire - Density, the Chance lane, a mute, a rest,
  a failed Chain - no longer leaves a silence: the note before it is held open and released one
  sample *after* the next fired step's note-on (`emitHit`'s `closeHeld`), the overlap a synth's
  legato or glide mode needs. **The other way round from the tie branch, on purpose**: a tie is
  the same pitch and its off must come first or the voice stacks; this is a different pitch and
  the on must come first or the synth hears a gap. A held pitch retriggered on itself still takes
  the tie branch.
  **It looks one step ahead, and that is the whole design.** Whether a note is held or ends at
  its gate depends on the *next* step, and by the time that step is decided a gated note has
  already ended - so `fireStep` asks `prerollNext` the same four questions (chain, mute, rest,
  chance) one step early, keeps the chance draw, and `chanceFails` hands it back when the step
  arrives. Without it the engine could hold through skips *or* honour Gate, never both, and
  "every note held to the next note-on" is the reading Owen turned down. The second thing after
  `lastStepFired` that is not stateless from the playhead; a stale preroll (a transport jump) is
  discarded and being wrong costs one step either way. `Active::legato` marks a held entry: no
  due time, skipped by `retireDue` and the due branch, untouched by `advanceBlock`. **The one
  hang it makes possible is closed at its one exit**: `releaseLegato` runs when the flag goes
  off and in the bypassed / keys-up branch, since a held note waits for a next step and there is
  none. A ratchet holds only its last sub-hit. `ArpTests` pins all six, and the gate-kept case
  is the one that fails against a version without the lookahead.
- **The card's bottom strip is a floor too, and was already over it.** `minMacroWidth()` took the
  knob strip alone; the five-chip strip with the chord readout is 508 px against a 462 px knob
  strip, and at the detached window's width - where the card is exactly the strip wide -
  Details had been ten pixels short since the dice arrived, with nothing on screen to say so. A
  fifth chip would have laid it out at zero. The cells are named constants summed into
  `arpMacroModsW`, the card takes the larger row, the detached floor is 1108 (from 970), and
  `LayoutTests` measures the five chips and the readout at both floors by overlap and
  containment, since `takeMod` clamps rather than going negative - the arp bar's own lesson one
  level down.

**The track's own MIDI needs an invitation, and an instance can say which one it is
(2026-08-27/28).** Owen: *"why do I start recording in Ableton? It starts playing in something
different"*, then *"we need it to be easier to turn it off, and it's so unclear where that's
hiding"*, *"we need reset button in settings"*, and, earlier the same evening, the problem
underneath all of it: six Keys in one Live set that nothing could tell apart.

- **`arpTrackMidi`, appended, global, default false. PARAMETER LAYOUT CHANGE.** A new parameter
  is absent from every saved session, so **every existing set takes the new default** and a clip
  that was driving an arp goes quiet until the chip is switched on. That was Owen's call with
  both defaults in front of him, and the asymmetry is the argument: the new surprise is silent
  and one click from fixed, where the old one was silent in the other direction and the fix was
  **seven toggles across four windows**.
  **One switch was answering two questions.** Per-line **PLAY** (`arpKeys`) says "Play", its
  tooltip said *"what you play on the keyboard"*, and the stream it lifted was the keybed **and**
  whatever the DAW sent the track. Chord pads were split out of that stream on 2026-08-18; the
  track input never was. `arpKeys` also defaults **on** for all four lines, for a good reason of
  its own - a line you just switched on that does nothing until you find a second toggle reads as
  broken - so six untouched instances all had every line listening, and pressing record started
  four of them arpeggiating on tracks nobody was looking at.
  **Global rather than per line**, for Scale Lock's own reasoning: it is one door into the
  instance, and a door shut for A and open for C is not shut. **On the arp bar**, beside All Off
  and Hold off, because a control you reach for to make something *stop* cannot live two clicks
  inside a line's detail view; it never hides with the fold, same rule as Hold off. The notes
  still pass through to your instrument untouched either way - the chip decides what the
  *arpeggiator* hears, not what the track plays.
  **Held aside in `processBlock`, not gated in `runArpLines`**, and that ordering is the whole
  implementation: by the time `runArpLines` runs, the collector has merged and a clip's C4 and a
  clicked C4 are the same `MidiMessage`. Same reason `dest` exists. Everything goes aside,
  note-offs included - keeping only the note-ons back would let a line lift a note-off whose
  note-on passed straight through, hanging that note on the instrument for good.
- **The falling edge must release what the *line* took, never what the *track* holds**, and the
  first cut got this wrong in a way worth keeping. Closing the door strands any pitch a line had
  already taken in, whose note-offs are now routed around it, so the releases have to be
  synthesised - into the lines' own input alone, since the real note-offs still travel down the
  output stream. Firing them off `inputNoteOn` (every pitch the track is holding) looks right and
  is not: **`ArpEngine::Held::ons` is a refcount over every source that asked for a pitch, and
  `noteLeft` matches on pitch alone**, so an off for a clip note the line never received
  decrements whichever owner *is* there. Hold C4 on the keybed over a clip already sounding C4 -
  a clip note that began while the door was shut, so the line never got its note-on - close the
  door, and the line drops the note under your hand. `trackHeldByLine` is a per-line mask set
  where the notes are actually handed over, so a line that was not listening when they arrived
  never acquires a bit for them, and every off fired has an owner of its own to spend.
  **The shape to remember: "who is holding this pitch" and "who asked me to hold it" are
  different questions, and a refcount can only answer the first.** `StateTests` pins both halves,
  and the keybed one fails against the `inputNoteOn` version - which is the only thing that makes
  it a regression test rather than a description.
- **`KeysProcessor::updateTrackProperties`, and merge, never replace.** Every Keys in one Live
  set shares a process, so the MCP discovery file's pid is identical across all of them and
  **only the port differs** - a number nothing in the DAW ever shows you. Probing a real set of
  six, four were separable by their settings and **two were exact twins** on every readable
  field. JUCE's VST3 wrapper already implements `Vst::ChannelContext::IInfoListener` and marshals
  it to the message thread, so this override was the only missing piece: the information was
  arriving and being dropped. Both fields are plain `juce::String` members for that reason, not
  atomics.
  **Live makes three calls per instance as a set loads** - empty name + default colour, then the
  real name + default colour, then an empty name + the real colour - so storing what you are
  handed throws the name away on the third call, and the symptom is a track name that works until
  anything else on the track changes and then silently goes blank. Live's empty calls report a
  *present but empty* string, so testing `nullopt` alone is not enough; the colour is guarded the
  same way, since a default-constructed `juce::Colour` is transparent and Live's placeholder is
  transparent. `get_state` reports `trackName` / `trackColour` (empty means the host did not say,
  which is a different answer from a track with no name; the standalone is never told at all),
  and **the About box names the MCP port** and the host track, which is the only thing on screen
  that tells one instance from another.
- **Reset all settings, and it takes the settings in its own menu.** `resetAllParameters()` walks
  every parameter back to its default **and** the six behaviour switches that are not parameters:
  the three ticks in the settings menu itself, Light keys, and the Pads bar's Play and Keep arp.
  Parameters alone was the first cut, and a row named "Reset all settings" that leaves the
  settings six pixels above itself exactly as they were is wrong in the one place its name is
  read. It stops there: the theme, the folds, the detached windows, the current page and the
  library favourites are **where you left the furniture** rather than how the instrument behaves.
  It never touches chord pads, arp patterns or slots - that is work you made, undo covers it, and
  a Reset that quietly emptied a page would be the worst button in the plugin. It **releases
  every sounding note first** (`allNotesOff`), because a reset moves Root, Octave and the arp's
  whole routing underneath anything still ringing and the played note is resolved at press time
  and remembered. It asks first, since undo is content and this is not content.
- **A dot on the A/B/C/D letter when a line is sounding something nobody handed it**, the
  on-screen half of `get_state`'s `soundingNoteCount`: `heldChord` is only ever the chord you
  *gave* a line, so an instance playing continuously with every readout blank was diagnosable
  only by ear. **The keybed counts as handing it something** - gated on that line's own PLAY -
  or the dot lit for the commonest case there is and said nothing about the case it exists for.
- **The arp bar's floor did not move.** Track MIDI added 128 px to a bar that already carried
  four chips; measured at the 1320 px minimum the right end wants 440 and the deep view's left
  end ~591, leaving ~159 for a fold zone needing 92. `LayoutTests` sweeps it - and **the first
  version of that sweep could not fail**, which is the lesson: every chip is placed with
  `withSizeKeepingCentre`, which forces the size whatever cell it is given, and `removeFromRight`
  clamps rather than going negative, so `getBounds().getWidth()` is a constant and a width
  assertion compares two constants. A starved bar produces chips of exactly the right size
  sitting on top of each other. **Overlap and containment are the only things that can see it.**

**A chord reaches a line from wherever you were holding it, and Scale Lock reaches the line's
output (2026-08-26).** Owen: *"I can't drag the held chord onto the arpeggiator"*, *"I wanna be
able to hold the chord down to build it with my mouse, but then also to drag a new chord onto the
arpeggiator"*, and *"does the scale lock button at the top apply to arpeggiators and harmonies?"*
Three asks, and each one supersedes a claim below it.

- **Every arp target takes any chord drag now, not only `From::padSlot`.** The four of them - a
  macro card, a slot card, the panel itself, `ArpBarTab` - each read `p->from == padSlot` and then
  looked the chord up by `p->index`, so the **live card** (index -1, by construction) was refused
  four times over and its drop landed on nothing, with nothing lighting on the way to say why. The
  predicate is `chorddrag::chordBeingDragged(details) != nullptr` in all four now: a drag of ours
  with notes on it. **This retires "the arp's targets take `padSlot` alone and so refuse it"** in
  the reference-card bullet below, and the tray's own "not offered a slot today" with it - Owen
  asked for both.
  **`holdArpChord(notes, name, line)` was already the general form** and `holdArpChordFromPad` is
  the *pad* case of it, which is why this cost a predicate and a dispatch rather than an engine
  change. The pad overload is kept rather than folded in: it is what writes `lines[line].padSlot`,
  which is how a pad card wears its line's letter and how clicking a *cleared* card still feeding a
  line releases it. A chord from the live card marks no card, so Hold off and All Off are what stop
  it - and `ArpPanel::takeChordOnLine` / `KeysEditor::sendDroppedChordToArpLine` each carry two
  overloads for exactly that reason. **The pad menu's Send to arp rows keep the slot overload**,
  because they genuinely name one.
  **A tray candidate dropped on a line is copied, never consumed.** `consumed` is what empties a
  tray cell, and a line is not storage - a candidate that vanished into one would be
  unrecoverable, where a pad keeps it. So an arp target sets `taken` and nothing else, which is
  the reference box's own rule and is why the box needed no special case.
- **`LayoutState::padsKeepArpRunning`, a tick beside Play, default on.** Ticked, pressing a card on
  the strip - a pad or the live card - never releases an arp line's held chord, however Exclusive
  is set. **This is the half of the Play toggle's story Play could not fix**, and the bullet below
  telling you to turn Play *off* while dragging cards into the arpeggiator is superseded by it:
  Play decides whether the strip makes a **sound**, and what actually cut the lines off was the
  **choke**. The only way to avoid the choke was to give up the sound as well, so hold-to-build and
  drag-into-the-arp were two settings you kept swapping between.
  **It narrows one gesture, not the rule.** A *drop* on a line still replaces that line's chord and
  still chokes the pads and the live card under Exclusive. The distinction is the one
  `takeChordOnLine` already draws when it routes without navigating: **pressing a card is playing a
  chord, and a line's held chord is not something you are playing** - it is what the machine is
  chewing. Two call sites, `pressChordPad` and `pressLiveChord`, and `StateTests` pins both,
  because a gate stuck open and a gate doing its job look identical from the ticked side alone.
  **Two switches on one bar, and this is the case where that is not the `padHoldToPlay` mistake** -
  neither answer implies the other, and Owen wants both halves at once. Off by default it would
  have been a feature nobody found; on, an older session (whose layout tree has no such property)
  takes it too, and nothing about that session changes visibly, because Exclusive is off by default
  and the choke was only ever reachable with it on.
- **Scale Lock reaches the line's output.** `scaleLock` was read in exactly one place -
  `NoteMath.h`'s `resolveOutputNote`, at keybed press time - so it shaped what you played *into* a
  line and nothing the line did afterwards. Root and Scale have always reached the engine
  (`ap.rootPc` / `ap.scaleMask`, feeding the Transpose lane, Distance in degrees and Stray's lower
  zone); the *lock* was the piece that never arrived. `Params::scaleLock` plus
  `ArpEngine::snapToMask` snap **in `addHit`**, the one place every emitted pitch passes through -
  the walk, Octave and Transpose, a Chord-lane slot, the octave stacking, both harmony voices and
  Stray's strays, in one rule rather than five.
  **It snaps *before* the dedup**, and that is the half that matters: two pitches that round onto
  one have to collapse to one hit, because two hits on one pitch in one step is a **hung note**,
  not a doubled one (see `addHit`'s own note on the tie branch). A harmony voice that rounds onto
  its own source is dropped, exactly as one that clamped there already was.
  **Two consequences, both deliberate.** Harmony voices stop being chromatic while Lock is on - the
  entry below's "**Chromatic on purpose**" is now a statement about Lock *off*. And **Lock beats
  Stray**: Stray's upper zone exists to leave the scale, locking the output is what the toggle
  says, so under Lock its two zones read as one. Untick Lock to hear the wrong notes it exists to
  prevent.
  **Chord pads are untouched** and that is the line: a stored chord is a chord you built, and
  snapping it would silently rewrite a borrowed one. Lock is about what Keys *plays*, and a pad
  fired straight to the output is the chord you put there.
  **Global, unlike everything else in that params loop**: a lock that held on one line and not
  another would not be a lock.
- **`Keys_tests` links with `/STACK:8388608`, and the reason is worth keeping.** Adding four
  `beginTest` blocks made the binary die with `0xC00000FD` having printed **nothing**, which reads
  as the suite silently not running rather than as a crash. `ArpEngineTests::runTest` is one
  function holding eighty-odd `ArpEngine` locals, one per block, and MSVC lays out a frame for the
  whole function rather than reusing the slots as scopes close - so it had been sitting just under
  the default 1 MB for months. Raised at the link rather than worked around in the file, because
  the cause is the frame and not any one test; nothing about the product needs it, since an
  `ArpEngine` in the plugin is a member of the processor and never a local.

**Four arps, colours, harmony voices, and Mutate off the leash (2026-08-19).** Owen: *"I want 4
arps. and each one should have a color. and I want new knobs, 2 harmony drop down like the photo
[BigSky's shimmer interval list]. and each of those has a chance knob which effects the harmony
probability. and I want a mutate knob, which effects the notes being played. higher values can go
out of scale"* - then, asked, he picked the 2x2 grid, the one extended Mutate knob, harmony on the
cards, and per-line colours. Everything here supersedes the older bullets it contradicts:

- **`numArpLines` and `uiArpLines` are both 4.** Line D is `arp4*`, appended like B and C were, so
  earlier sessions open sounding identical; line C came back on screen in the same stroke. The
  "two lines" bullets below (2026-08-02) describe machinery that is all still true - only the
  count moved, and it still lives in those two constants. The All view is a **2x2 grid of cards**:
  two columns is load-bearing (a card's knob strip needs ~430 px), so more lines mean more rows,
  never narrower cards. `arpMacroH` is two cards plus a row gap and the layout in
  `ArpPanel::resized` walks rows of two.
- **The bottom row collapses to a 34 px strip** (2026-08-19, Owen: *"maybe you should be able to
  minimize bottom arps"*). Two card rows at 323 px each meant the All view alone set a **1349 px
  minimum window** - `applyLayout` passes `idealHeight()` in as the resize *minimum*, and this
  view is the default - which is 43 px under Owen's own 1392 px work area and more than a 1080p
  screen has at all. Collapsed, the minimum falls to **1060**.
  `LayoutState::arpMacroBottomFolded`, persisted, default false. The **Lines C D** chip on the arp
  bar is the toggle (there, not in the panel, for the standing reason: the bar is 34 px that
  already exists, and spending 34 inside the panel to buy back 289 would be absurd); the collapsed
  strip is itself a click target that opens the row again. **It folds the view, never the lines** -
  C and D keep their chords, their patterns and their output, the macro card's scrim rule one level
  up - and the strip therefore carries **no On switches**, because those are the bar's letters and
  a second writer for one parameter is exactly what deleted `MacroRow`'s own On toggle on
  2026-08-02. Two traps already paid for: the cards in a folded row are **hidden, not resized** (a
  card squeezed into 34 px draws its knobs over each other and still takes the mouse), and
  `setMacroView` has to hide the strip as well as the cards, since `resized()`'s whole macro block
  sits behind `if (macroView)` and a folded row would otherwise keep drawing its strip over the
  deep view. `LayoutTests` pins that the fold buys the height back and that unfolding returns it
  exactly.
- **Each line has a colour**: `skin::lineAccent(line)` - A cyan (the accent Keys shipped with),
  B magenta, C amber, D lime, the hexes reused from `accentChoices()`. Fixed, not
  theme-following, because the job is telling four lines apart. Worn by the macro card's frame,
  fill and caption, a stripe under the bar's letter switch, the deep view's LINE caption, the
  Draw grid's playhead - and, from 2026-08-22, **the keybed keys that line is playing** - the
  marks that say *which line*, never the controls, which is how the one-cyan skin law bends
  without breaking.
- **The keybed lights per line** (2026-08-22, Owen: *"new branch for each arp to play different
  colors on the keyboard"*). **Light keys** already lit the keybed for the arp's output; with
  four lines that was one colour for all of them, saying *that* the arp was playing and not
  *which line*. `arpNoteOn` is **`arpNoteLines`** now, a bitmask per pitch with one bit per
  line, and `arpLitLine()` reads the lowest set bit. **Lowest wins on an overlap, never a
  blend**: the palette exists to tell lines apart and a mix of two line colours is a fifth
  colour belonging to neither, and lowest-wins is *stable* while the note is held, so a key
  never changes colour as lines come and go under it. Every non-arp source - a press, a latch,
  a chord pad, the MIDI input, MCP - stays on the theme accent, which is what keeps a colour on
  the keybed meaning exactly one thing.
  **It retired a documented artefact**: two lines on one pitch shared a single flag, so the
  first note-off put the key out under the line still playing it. Per-line bits make that
  impossible. **Within** a line it still stands (two harmony voices, one pitch, first note-off
  wins) and that is still why this is a flag and not a count - a missed note-off would leak a
  refcount into a key lit forever. Single-writer: `runArpLines` walks the lines in order on the
  audio thread, so the read-modify-write is safe on *that* basis, not on the atomic.
  **The keybed's sixteen cyan gradients moved into the skin** as `skin::KeyLitSet`, reached
  through `skin::keyLitFor(line)` - they were per-file chrome of exactly the kind the skin rule
  forbids, and a second colour was impossible while they sat in `PianoKeyboard::paint`. **Cyan
  keeps its shipped values byte for byte** rather than being re-derived, the same reasoning
  `cyanAccent` is written down rather than derived; every other accent derives into the same
  relationship. The sets hang off one function rather than four that each re-sniffed
  `a.base == cyanAccent.base` - four copies of one decision, and a function guessing what its
  caller already knew.
  **A non-arp key is cyan, not the theme accent**, and that is the case to get right rather
  than the interesting one: before this, these gradients were hard-coded cyan whatever the
  swatch said (only the glow strokes followed the accent, and they still do), so deriving them
  from the theme - which the first cut did - silently changed the colour of your own presses on
  any non-default swatch. **The palette's honest limit**: line A *is* the default cyan, so on
  that swatch a chord pad and line A are the same colour. B, C and D are unambiguous. **`NoteSurface::externallySounding()` returns a map now**, key
  to line, and `refresh()` folds the line into its change cache through `litKey` - a state-only
  cache would hold the first colour when a key passed from one line to another without going
  out in between.
- **Two harmony voices per line** (`arp*Harm1/2` choice + `arp*Harm1/2Chance` int, appended,
  Off/100 defaults): `KeysProcessor::harmonyChoices()` is BigSky's shimmer list minus its two
  cents rows (MIDI cannot say ten cents), `harmonySemisFor` maps index to semitones, and the
  engine takes plain semitones (`Params::harmSemis/harmChance`) so the table stays out of it.
  In `fireStep` the voices copy the resolved hits - chord-lane steps and Mutate's strays
  included - at their interval; the chance is rolled per step per voice off a stateless hash. A
  voice that clamps onto its own source is dropped, the subharmonic rule. **Chromatic on
  purpose**: the dropdown names intervals, which is what keeps this from being a third copy of
  the Harmony lane's chord-tone counting. On each macro card: two combos with a chance knob
  each, on their own strip under the knobs (`Macro harmony 1 A` / `Macro harmony 1 chance A`).
- **"+ Octave & 5th" names two intervals and plays two notes** (fixed 2026-08-21, Owen: "in the
  harmony, when you select octave plus fifth, it looks like it only just does octave"). Every
  other entry in the list names one interval; that one says **&**, and `harmonySemisFor` read it
  as a *compound* interval instead - a single note 19 semitones up, which is neither of the two
  it names. `Params::harmSemisB` and `KeysProcessor::harmonySemisSecondFor` give a slot a second
  interval, 0 for the twenty-six entries that have none. **It stays one voice**: both pitches sit
  inside that slot's single chance roll, because a slot that half-fired would be the same bug
  wearing the chance knob. **There are three index-parallel tables now, not two** - `harmonyChoices`,
  `harmonySemisFor`, `harmonySemisSecondFor` - and appending to one means appending to all three;
  the `jassert` in each is what catches a miss, and `StateTests` sweeps the whole list for the
  second table being zero everywhere else and for no entry past Off naming nothing. The lesson is
  the older one restated: **the label was right and the implementation was reading it wrong**, and
  nothing on screen could show that, because both readings put *something* plausible in the air.
  **The pair is 12 and 19, not 12 and 7** (corrected the same day, in review). It shipped as the
  fifth *below* the octave for an afternoon, and the list itself is what settles it: the entries
  run as one ascending ramp from "- Octave" to "+ 2 Octaves", and this one sits between
  "+ Octave" and "+ 2 Octaves". Spelled 12-and-7 its lower pitch reached *below* the plain
  "+ Octave" directly above it - reading down the list made the shimmer go down - and the pair
  was an exact duplicate of "+ Perfect 5th" plus "+ Octave", two rows the list already has.
  **`StateTests` checks the ramp across the whole list now** rather than spot-checking this
  entry's two numbers: a spot check can only ever repeat whatever the table currently says,
  which is exactly why the wrong pair passed a green suite. The engine's slot gate tests **both**
  intervals too (`semis == 0 && semisB == 0`), so a future entry spelled with only a second one
  would sound instead of registering everywhere and playing nothing.
- **A step's strays are per note, not per step** (2026-08-21, in review of the above). Under
  **Chord** shape one step sounds several notes, and `mutatedPitch` hashed only (step, era) - so
  every note of the step drew one answer and moved together, turning a held C-E-G into a parallel
  D-F-A. That is the line changing key, which is not what Stray is for. The hash is salted with
  the hit index now. **Lock is untouched by this**: the cell is still (step, era) and the salt only
  picks a voice inside it, so the run is as stateless from the playhead as it ever was. Worth
  keeping as a shape of bug rather than an incident - **a hash over "which step" is the wrong key
  whenever a step can carry more than one of the thing being decided**.
- **"Make harmony 2 columns" meant the dropdown's own popup** (2026-08-19; a first reading put
  the card's controls in two columns, and Owen, shown the menu: "still one column"). The
  harmony combo opens as two columns, descending intervals left and ascending right, BigSky's
  own split: `ArpPanel::buildHarmonyMenu` rebuilds the ComboBox's menu (`showPopup` only calls
  it and shows the result), and `showPopup` must hand the menu its LookAndFeel, or it comes up
  in JUCE's stock grey. **The break is derived from the semitones, not the label text**, and the
  loop itself lives in `src/ui/ComboMenu.h`, shared with `StepComboBox` - both since 2026-08-22;
  see the Off-was-greyed entry further down for what a hand-rolled popup costs.
- **A Play toggle on the Pads bar** (`LayoutState::padsPlayOnClick`, default on; 2026-08-19,
  Owen: "I want a toggle above the keyboard to play notes... when I'm trying to drag a cord
  into the arpeggiator, it plays instead, and it stops everything"). Off, a pad click makes no
  sound - the strip is drag-only - so a fumbled drag toward the arp cannot fire a chord that
  Exclusive turns into a full stop of the running lines. **On, it is hold-to-play**
  (2026-08-22, Owen: *"when the play mode is checked on the pads, I want it to trigger as soon
  as you click on it and stay held until you let go"*): the press fires and the release ends it,
  so a stab is short and a lean is long. That was `padHoldToPlay`, a settings-gear tick, until
  this toggle absorbed it - **two switches for one question**, and the second one could quietly
  make Play mean something other than play. The one gate lives in `ChordPads::mouseDown`; the
  release path calls `endAudition` **unconditionally**, never `startAudition`, so a toggle
  flipped mid-press cannot strand a note. With nothing on the strip on a clock any more,
  `ChordPads` is no longer a `juce::Timer` and `auditionMs` is gone from it - the generator's
  tray keeps its own 800 ms, which was always a different question.
  The button hides with the Pads fold like the page buttons; accessible name
  `Pads play on click`, on-screen word "Play". **Beside it since 2026-08-26**, `Keep arp running`
  (on-screen word "Keep arp"), which decides whether a press on the strip may stop a running line;
  it hides with the Pads fold for the same reason.
- **The RangeKnob's face is the band's centre, and the halo only ever writes the span**
  (2026-08-19, two corrections in one afternoon: "it should expand in both directions. up is
  more", then "moving the halo shouldn't move knob. should be equal from center"). The band is
  value +/- reach with `reach()` stopping where a rail is nearer, so it is *equal on both
  sides always* - that is the contract, not a clamp artefact. The lit arc spans both halves
  through `skin::arcToProperty`, arcFromProperty's new twin. The engines follow: VEL's hits
  and H.TIME's lateness wander either side of their knob (H.TIME still never early relative to
  the grid - the low rail is zero-late), `arpHumanVelSpan` is no longer read by the engine at
  all, and the pads' Strum/Humanize faces lost their SliderAttachment - a centre is not a
  parameter, so KeysEditor::wireRange pushes both ends by hand and syncPadRangeKnobs() pulls
  on the timer. **The 2026-08-18 "never louder" claim and every "the knob is the ceiling"
  phrasing below are history**; "never early" stands. The wheel works on the halo and ring,
  up is more.
- **Mutate has three zones, and past 50 the 2026-08-18 "cannot leave the held chord" claim no
  longer holds - by Owen's own ask.** **Superseded 2026-08-21 by the entry below; the "three
  zones" reading lasted two days and is history.** To 50 the knob was byte-identical to what
  shipped; past 50 `ArpEngine::mutatedPitch` (a second stage, applied to the placed pitch after
  the index walk) could stray a scale degree or two off the chord note; past 75 a growing share
  of strays were chromatic semitones, all of them by 100. Both stages hash the same (step, era)
  cell (`mutateCell`, factored so they cannot disagree), so **Lock holds the out-of-scale finds
  too** - a wrong-note lick the machine found can harden into the part. That last sentence is
  the only part of this bullet still true as written.

**Mutate cannot leave the held chord again, and the strays have a knob (2026-08-21).** Owen:
*"the mutate doesn't really work the way I want ... it's adding additional notes in the
arpeggiator ... it should just change the existing ones"* - then, asked, he chose the strays kept
behind a control of their own rather than deleted, with Mutate's travel rescaled to the reach it
had been spending on them. This supersedes the three-zones bullet above it.

- **Nothing was ever added, and the report was still right.** A step fires **no more** hits at
  Mutate or Stray 100 than at 0 (`fireStep` resolves one hit per `playIdx` entry either way).
  Not *exactly as many*, and the difference is worth stating: under a multi-hit shape a stray
  can put two hits on one pitch, and `addHit` dedupes on (note, channel), so such a step comes
  out a voice thinner. That is the harmony voices' own rule - a hit collapsing onto its source
  is dropped rather than doubled, because a collapsed interval is a silence - and it errs the
  safe way for a report about notes *arriving*.
  What arrived were *pitches belonging to no chord Owen had played*, which is what extra notes
  sound like from the listening chair. `ArpTests.cpp` pins the count outright now, beside the
  pitch-set tests, so the two halves of that claim cannot rot apart. **Reach for this
  distinction before redesigning anything on a report of extra notes**: the per-line **harmony
  voices** genuinely do add hits, and they are the other thing to rule out first.
- **One dial was carrying two questions**, and that is the lesson rather than the change of
  mind: `mutatedIndex` asks *how hard does the run explore this chord* (never leaves it, at any
  setting) and `mutatedPitch` asks *may a step land outside the chord at all* (that is its
  entire job). Folded onto one knob, you could not ask the first without eventually being
  answered the second - "explore harder" had a ceiling at 50, above which it silently became a
  different feature. **Off has to be a position you can stay at.**
- **`mutatedPitch` reads `Params::stray`**, appended as `arpStray` (and `arp2/3/4Stray`),
  **default 0**, so a session that predates it cannot acquire a note it was not already playing
  and needs no migration. Its own two zones on its own travel: the knob is how often a step
  leaves the chord (0 never, 100 every step), and past **50** a growing share of strays are
  chromatic rather than in-scale, all of them at 100 - those thresholds sat at 50 and 75 of
  *Mutate's* travel before. **Independent of Mutate on purpose**: Stray with Mutate at zero is
  a plain Up run that occasionally plays a wrong note without its order changing, which was
  unreachable while the two shared a dial. **Lock still holds both**, same `mutateCell`.
- **Mutate's reach grows over the whole knob now**: `1 + amt * 3 / 100`, one chord entry to
  four, where it was `1 + amt * 2 / 100` - one entry until 50 and three only at exactly 100,
  because the upper half was spent elsewhere. **Capped at `count - 1`**, which is not tidying:
  reaching further than the chord is long only wraps onto notes a nearer reach already offers,
  so an uncapped reach reads as the knob doing *less* the higher it goes.
- **STRAY is the ninth knob on the macro card, inserted between MUTATE and LOCK** rather than
  appended to the row, so the three read left to right as one sentence - how hard it explores,
  how far outside it may go, how long it keeps what it finds. The `Knob` enum is UI indexing
  and nothing stores it, so inserting there is free; the *parameter* was appended, which is the
  order that is not free. **Nine is what the 38 px floor was chosen to survive**: 9 knobs plus
  the two rings and eight gaps is 422 px, so every knob still clears the 34 px mouse-only floor.
  **A tenth does not fit and must buy the width** - raise the floors, never starve the row.
  (**The tenth landed on 2026-09-01**, DENSITY, and bought nothing: the floor is derived from
  `numKnobs` and moved itself to 462 px of strip, which the docked card's 614 already held. What
  it *did* find was the card's other row, over its floor all along - see the round at the top of
  this section.)
  **Zero is its own off switch**, so Stray takes no toggle beside it, the reading that already
  leaves Strum, Lock Influence and Lean without one.
  **"The editor's minimum width" was the wrong floor, and that is the standing lesson**
  (corrected 2026-08-21, in review). The arp section **detaches into a window of its own**, whose
  minimum was 900 px - a card column of 420 against a strip asking for 422. JUCE answers a row
  that asks for more than it has by clamping, and the whole shortfall lands on the last cell, so
  H.TIME's face drew at 16 px with nothing on screen to say why. Two of the deep pages were
  already starving a control apiece at that width before the ninth knob arrived. So the floor is
  **derived and asked for** now: `ArpPanel::minMacroWidth()` walks the same insets the layout
  does and moves the moment `numKnobs` changes, `ArpPanel::minPanelWidth()` takes that against the
  deep pages' own measured requirement, and `PluginEditor` passes it to the detached window rather
  than carrying a literal. `LayoutTests` sweeps every view at that width, which is what keeps the
  measured half honest. **A view that can be drawn in two windows has two floors, and only the
  smaller one is ever tested by accident** - the same trap as `contentHeight()`'s max over five
  sums, one axis over.
  **The starvation sweep's own floor is not the mouse-only floor**, which is why it waved this
  through: `width >= 20` passes for a range knob whose 32 px cell is mostly ring, so the face
  inside it can be 16 px and still clear the test. `LayoutTests` measures the macro knobs against
  34 px directly for that reason, and counts them, so a name-matched sweep that stops matching
  fails instead of passing by finding nothing.

**The arp opens quieter and steadier, and a harmony voice is as loud as the note it thickens
(2026-08-23).** Owen: *"want default arp settings"*, holding up a card reading VEL 22-62 and
H.TIME 0-22, then - shown that harmony rolled its own loudness - *"harmony same velocity"*.

- **Three defaults moved, and they are one decision rather than three**: `arpVelLevel` 100 -> 42,
  `arpHumanVel` 18 -> 20, `arpHumanize` 24 -> 11. H.TIME now draws **0-22** and plays 0 to about
  5 ms late where it drew 0-48 and played up to 12; VEL draws **22-62**. The level is the part
  worth understanding: Humanize Velocity's reach stops at `min(level, 127 - level)`, so **a level
  of 100 caps its ring at +/-27 however far the ring is wound**, and a level of 42 lets it reach
  +/-42. Lowering the level is what gives the ring somewhere to go, which is why the two could not
  be chosen separately. `StateTests` pins that relationship rather than either number.
  **Defaults only.** A saved session stores all three and keeps what it said, and a session old
  enough to predate `arpVelLevel` does not take the default at all - `migrateVelLevel` computes a
  level that plays it at the loudness it was saved at. The pads' own Humanize band is untouched,
  so the **76** midpoint that migration converts against still means what it meant.
- **A hit and its harmony voices share one velocity draw.** The Humanize Velocity draw was made
  per *emitted* hit, in the ratchet loop, and by then a harmony voice is an ordinary hit - so a
  voice rolled its own number and could sit up to `2 * humanVel` from the note it was thickening.
  At the shipping defaults that is the full width of the band - a voice at 62 against the note it
  thickens at 22 - which is a second player rather than a thickening of the first. **Take the
  magnitude from `2 * humanVel` and the reach clamp, not from a remembered pair of numbers**: the
  reach stops at `min(level, 127 - level)`, so at 42/20 nothing outside 22-62 is reachable at all,
  and a wider-sounding example can only have come from settings that are not the defaults. `ArpEngine::Hit` carries **`src`**, the
  index of the hit it harmonises (or its own), and the draw is made once per source and read by
  its voices.
  **Per ratchet, deliberately.** The draw stays *inside* the ratchet loop, so each repeat of a
  ratcheted step still draws afresh and a roll keeps its life; what is shared is a hit and its
  harmony *within* one repeat. Hoisting it to once per step would have flattened every repeat to
  one velocity, which is a different feature and was not the ask.
  **This is what `Hit::vel` is for again**, and the dead field is the whole reason the bug was
  invisible: it had been written at every call site and never read since `velLevel` replaced the
  incoming chord's velocity (2026-08-18), and the harmony loop dutifully copied it - so the code
  read exactly as though a voice already took its source's loudness. `addHit` no longer takes a
  velocity at all. **The shape to remember: a field that is copied but never read makes the copy
  look like the feature.**
  **Every voice, no carve-out - and the first cut had one** (fixed 2026-08-24, in review). `src`
  reached the two *fixed* per-line voices and stopped there: the Harmony **lane**'s two modes
  still called `addHit` without naming a source, so they stayed their own source and went on
  rolling an independent draw. The reported bug surviving by the one route the fix did not cover,
  and *inconsistent* rather than merely missed - a fixed voice stacked on a lane-harmony hit did
  inherit that hit's velocity, so within one step some voices shared and some did not.
  **`addHit` returns the index it wrote or found**, which is what made this a one-argument fix,
  and the *found* half is load-bearing: on the dedup path the voice must name the hit that is
  actually sounding rather than the one that was refused, or it reads a velocity nobody drew.
  **The shape to remember: a fix that names its call sites one at a time is only as complete as
  that list, and nothing checks the list.** Ask what *else* reaches the thing being fixed.
  **Timing is still per voice**, so a harmony voice takes its own H.TIME lateness draw and can
  flam against its source. That is the identical question one axis over and is deliberately left
  as it was, not overlooked - it was not asked for, and a flam is sometimes what a thickening
  wants. `ArpTests` pins the velocity half for both routes, with a guard that the draw still
  varies between steps so the pairing cannot pass on a flat run, and **a ratchet case for the
  per-ratchet rule above** - without one the sharing tests run at ratchets = 1, where sharing and
  hoisting are indistinguishable, and the hoist is a one-line move away.

**Every range knob opens lit, and a page can be cleared again (2026-08-23).** Owen: *"we need to
be able to clear all the chords on a pad page"*, and *"I want the default strum up, humanize,
velocity, and H.TIME to have the range on and enabled by default"* - then, asked, he took the
medium of three amounts and the card menu over a chip on the Pads bar.

- **All four range knobs opened dark, which is a discoverability bug rather than a taste one.**
  Keys has exactly four (`RangeKnob`): Strum and Humanize in the pad strip, VEL and H.TIME on
  every macro card. Every one of them defaulted to a face of zero or a switch of off, so a fresh
  instance played every chord stamped out at one velocity, landing all at once, dead on the grid
  - and on three of the four the switch **is** the lamp on the knob, so the only route to the
  feature was to already know the satellite was there. Now: **Strum 30-80 ms** (direction Up,
  unchanged - it has been the default since it was a parameter), **Humanize on at 56-96**,
  **`arpHumanVel` 20**, **`arpHumanize` 11** with its ring already open, which draws as 0-22 and
  plays as 0 to about 5 ms late, and **`arpVelLevel` 42**. Those three read 18, 24 and 100 for
  the few hours between this entry and **The arp opens quieter and steadier** at the top of this
  section, which is where they moved and why.
- **A parameter with two writers, again: the pad range knobs were fighting the hand**
  (2026-08-23, Owen: "feels like it's fighting me... is there a race condition"). `timerCallback`
  pushed `abs(hi - lo)` - the band's **full** width - into `RangeKnob::setSpan`, whose span is the
  reach on *each* side, thirty times a second and with no `spanDragging()` guard (`KeysEditor` is
  `startTimerHz(30)`; the 10 Hz in this file is `ArpPanel`'s own). The band doubled
  every tick until it saturated against the nearer wall, so a saturated knob read exactly
  `[0, 2 x the knob]` - Strum at "0-128 ms" with its knob at 64. A leftover from before the band
  was centred on the face (2026-08-19), when the span *was* the full width;
  `syncPadRangeKnobs()` was added beside it rather than replacing it. **`syncPadRangeKnobs` is the
  one pull. Do not add a second writer beside it.**
  Three things worth keeping out of the afternoon it cost:
  **One bug wore four faces.** A halo that would not open, a knob that dragged its own band about,
  a band that crossed the whole range from one small drag, and a control that fought the hand were
  all this. Two geometry "fixes" went in on the strength of the first three and both came back
  out - **when several controls misbehave in unrelated-looking ways at once, look for one writer
  they share before redesigning any of them.**
  **The band must stay symmetric about the face, and that is not aesthetics.** Strum and Humanize
  are stored as nothing but their two ends and derive the face as the **midpoint**; symmetry is
  what makes that exact, so a halo drag can never move the knob. Clipping each end at its own wall
  was tried and reverted within the hour: the midpoint slid off the face and the pointer sat
  outside the middle of its own arc. A band that keeps its width at a wall needs somewhere to
  record a centre that is not the midpoint of its ends, and the pads have no such place.
  **A gesture's range must not depend on another control.** The halo's ceiling was made to track a
  wall - the nearer, then the farther - and both made the same drag worth a different amount
  depending on where the knob had been left. It is `spanMax()` and nothing else: `setSpanMax` for a
  ring carrying a parameter of its own, which is what the arp's VEL ring already did
  (`arpHumanVel` is 0..127 whatever the level does).
  **Half the face's travel is a bound on all of it, and it belongs in `spanMax()`** (corrected
  2026-08-23, in review). It shipped as `(hi - lo) * 0.5` handed in by `wireRange`, so the pads
  got it and the arp's own two range knobs - needing the identical reasoning - did not: H.TIME
  spent 228 px of a 300 px drag on band it could not reach, VEL about four fifths of its sweep.
  The half is arithmetic, not taste: `room()` is the smaller of two distances summing to the
  face's travel, so it never exceeds half, and `reach()` stops at `room()`. **A ceiling above
  half is inert by construction**, so stating it once bounds every consumer and narrows no band.
  The standing shape: *a bound derived from a control's own geometry belongs on the control, not
  at the call site that first noticed it needed one.*
  **"How far does the gesture reach" and "is there a band to open" are two questions.** Only the
  second may read the face, and folding them together is what cost the guard: `usefulSpanMax()`
  was `min(spanMax(), room())`, so taking the face out of the ceiling took with it the check that
  a halo at a rail writes nothing. Without it a drag there ran to completion, moved the parameter
  and bracketed a host automation gesture round it with **nothing to show on screen, in the
  readout or in the sound** - and H.TIME's face at 0 is an ordinary setting, not a corner case.
  `haloIsLive()` is that second question, and `usefulSpanMax()` is gone rather than left as an
  identity wrapper over `spanMax()` with two call sites still insisting the two differ.
  **A timer pull has to compare before it writes, and two here did not** (2026-08-23, in
  review). `syncPadRangeKnobs()` could not converge on an **odd-width** pair - the face snaps to
  whole units and the band is symmetric about it, so 30 and 81 give a centre of 55.5 whose ends
  round back to 31 and 82 and never match what is stored - so it re-ran every tick for the rest
  of the session under a comment saying it early-outs. `RangeKnob::refresh()` set two properties
  and asked two components to repaint on every tick whether or not anything had moved, directly
  beneath the comment claiming this region compares first. Both cache now, the shape
  `MacroRow`'s `lastLineOn` already used. **None of it was audible**, which is the point: a pull
  that cannot reach a fixed point has no symptom at all until somebody profiles the paint.
  **A test that watches parameters cannot see this class of bug**: `setSpan` fires no callback, so
  a wrong span corrupts only what is drawn. `LayoutTests` turns the real editor's timer through
  `KeysEditor::tickForTest()` and asserts on `rangeLo()`/`rangeHi()`; the first version of it
  watched the parameters and passed with the bug in place.
- **Humanize's band was widened around its centre, not moved**, and that is the load-bearing
  part: Humanize *off* plays the band's **midpoint** (`baseVelocity01`), and `migrateVelLevel`
  converts an old session's arp level against that same **76**. Keep the midpoint and both go on
  meaning what they meant; move it and a migration written months ago quietly starts converting
  against a different number. **The general rule: widening a default band is free, sliding one
  is not.**
  **The documented shipping band is asserted, not only its two parameters** (2026-08-23, in
  review). The rewrite dropped the only check that `arpHumanize` with a fully open ring actually
  *draws* the band this file names - 0-22 today, 0-48 when this was written -
  `StateTests` pins the parameters, and the two replacement tests used
  synthetic 0..200 knobs - so a change to either default or to `reach()`'s clamp could move a
  band this file names as shipped, with a green suite. `LayoutTests` builds the knob from the
  APVTS's own ranges and defaults, the way `ArpPanel` does.
  **A unit test that builds a real `KeysEditor` must set `KeysEditor::skipUpdateCheckForTest`
  first.** The constructor starts the updater's ambient check, a detached self-deleting thread
  that opens a URL to GitHub - so without it a layout test launches a network thread that
  outlives the `ScopedJuceInitialiser_GUI` shutting JUCE down at the end of the block, and blocks
  on a connection timeout on an offline runner. It cannot be a compile-time gate: `PluginEditor.cpp`
  is compiled into `Keys_SharedCode`, which the test target *links* rather than builds.
- **A ring wider than its rail allows is a lie on screen.** `arpHumanVel`'s reach stops at
  `min(level, 127 - level)`, so the level beside it decides how far the ring can ever reach: at
  the old VelLevel default of 100 the knob could never pass **+/-27** however far you turned it,
  and at 42 it reaches **+/-42**. The default ring sits under that ceiling with room to spare; a
  ring at or above it is a default that does nothing. `StateTests` pins the relationship rather
  than either number, so moving one has to answer for the other - which is what made the level
  and the ring one decision rather than two when they moved on 2026-08-23.
- **These are default changes and nothing else.** A saved session stores all five parameters and
  keeps what it said. What moves is a new instance, and a session old enough to predate one of
  the arp parameters, which takes the new default for it - lines B, C and D are off by default,
  so in practice that is line A a few milliseconds behind the grid.
- **A chord handed to an arp line is no longer raked**, found on the way here and a real fix.
  Routing a chord goes through `fireChord`, so Strum applied to it - but those notes go into that
  line's queue and make **no sound of their own**, so the rake was inaudible by construction and
  all it did was stagger when the engine learned each note. At 30-80 ms that is most of a 1/16 at
  120 bpm, so the first steps of a run fired on half a chord. `dest > 0` takes no strum now, the
  rule the Humanize velocity range has followed on that same path since 2026-08-02. **The shape
  to remember: a feel control applied to something inaudible is not neutral, it is a delay.** It
  had been latent for as long as Strum has existed and could only ever be found by turning Strum
  up, which is what a default of zero guarantees nobody does.
- **Clear page came back, on the card menu, because undo exists now.** `KeysProcessor::
  clearChordPadPage()` empties every unlocked pad on the current page in **one undo entry**, off
  a **Clear page** row alone in a group at the foot of a pad's card menu. It is the only row
  there that acts on anything but the card it was opened from, which is why it is not sitting
  beside Clear pad, whose name it would read as the plural of. The last page wipe was deleted on
  2026-08-01 for want of a home, and the two things that changed are that **undo arrived on
  2026-08-14** and covers the pad tree, and that the card menu is a place you go on purpose - the
  property the generator's window was standing in for. The Pads bar is still the wrong home for
  it, on the record from the day the old chip left it. See the right-click closed list, entry 6.
- **It lives on the processor, not on `ChordGenMenu`.** A page wipe is data work on the pad table
  and has nothing to do with generating chords; half the reason the old one had nowhere to go was
  that it was bolted to the brain. On `KeysProcessor` it is testable without an editor, which is
  what `StateTests` does with it. `pageHasClearablePads()` is the same query the row greys on, so
  the menu and the action can never disagree about whether there is anything to do, and the wipe
  pushes **no undo entry at all** when there is nothing to clear - an empty entry burying a real
  one is the failure a greyed row is not allowed to be the only guard against.

**The step sequencer pass (2026-08-18, second round of that day).** Owen: *"a usability and
functionality pass of the step sequencer. I wanna draw a lot of inspiration from [Kirnu Cream] and
how you can make really interesting, melodic patterns, and it's very easy to understand. Right now,
everything is kinda smushed together. And I'd like to explore the chance knob being a drift instead
where it explores other patterns and notes"* - then, asked what the knob should be: *"could be
multiple knobs. want notes. mutations"*. Everything here supersedes the older Draw-page bullets
further down; the ones it contradicts are marked where they sit.

- **The engine publishes a playhead, and `ArpEngine::laneStepIndex` is the shared arithmetic.**
  Nothing published the step position, so no grid could draw one - and with per-lane lengths every
  lane wraps differently, so "which step is sounding" has a different answer in each of them and
  none of those answers is the transport's. `uiRelStep` is the index relative to the last restart
  (`-1` when nothing is running: **no** playhead, rather than every lane parked on step 0, which
  reads as a stopped sequencer sitting on its first step), published where a step actually *fires*
  rather than off the clock - a suppressed divider boundary and a step the chord could not fill both
  pass through that loop without reading a lane. `laneStepIndex` is `laneValue`'s own index maths
  lifted out and called by both, so a grid cannot light a cell the engine did not look at. Do not
  reimplement it UI-side; that is the whole point of it being static.
- **Lanes grew a loop window, a direction and an on/off, and they are lane data, not parameters.**
  `Lanes::loopFrom` / `loopTo` / `dir` / `on`, riding the `"lane"` node of the arp tree beside
  `length` and `clockDiv`, each reading back as the old behaviour when its property is absent -
  which is why this needed no migration at all. **`loopTo` defaults to `maxSteps - 1`, past the
  end**, and is clamped at read: that is what makes "to the end" survive a length change without
  anything having to rewrite it. `ArpPattern` carries all four, so slots do too, and **the four
  pattern-copy sites are the contract** the way `syncArpChordTable`'s call sites are - miss one and
  a slot launches a lane with somebody else's window.
  `dirUpAlt` / `dirDownAlt` bounce with period `2*span - 2` and **do not repeat the turning
  points**: a doubled step at each end is audible as a stutter and is not what a bounce means.
  **Link pushes the window and not the direction** - Link on means the lanes share one grid and a
  window is part of which grid that is, but a direction is how a lane *walks* the grid it shares,
  and two lanes crossing the same eight steps in opposite directions is the entire point of having
  one. Off is polymeter and is left alone, unchanged.
- **A lane that is off keeps its drawing.** Kirnu's per-control on/off (its manual p12), and Keys
  had no way to take a lane out before it: Reset flattens the lane, which sounds the same and loses
  the work - the exact trap Cthulhu's mute-preserves-value rule already fixed one level down, on a
  single step. `laneValue` returns the lane's default and touches nothing. The grid **scrims** an
  off lane and never disables it: you draw on a lane before switching it on at least as often as
  after, and a disabled component takes no mouse events at all (the same reason the macro card's
  scrim exists rather than a `setEnabled(false)`).
- **The lane tabs report on their lanes**, which is the pass's biggest readability win for its size.
  A dot when the lane holds anything but its default **over its own length** (not over all 32 cells,
  or every lane that had ever been longer than it is now would light), struck through when the lane
  is off. Both are Kirnu's own marks. Eleven of twelve lanes are invisible at any moment and until
  this nothing said which of them were doing anything.
- **Steps, Speed and Link left the band for the Draw page.** They were in the *Play* page's STEPS
  group, so changing how long the lane you were drawing runs meant leaving the page you were drawing
  it on. They are per-lane controls; they belong beside the lane, in the new **lane strip**
  (`On | STEPS - n + | SPEED | DIR < d > | Link`). The band is **two** groups now, not three, and
  STEPS' 18 weight points went back to the two that stayed rather than being left as a gap.
- **Copy and Paste over the Select span; Clear deliberately not.** The last of Kirnu's palette
  (p8). Paste **tiles** - two copied steps fill eight, which is how a figure gets repeated, and it
  is the only reading under which a short clipboard does something useful. Same lane only, as in
  Kirnu: a Velocity lane pasted into Note would read as chord indices and play a melody nobody
  wrote. **Clear is Reset.** Its job in Cream is "set values to default" over the selection, and
  Keys' Reset already narrows to the Select span and already means that - a Clear beside it would be
  a second button doing its neighbour's job. Do not "finish the palette" by adding one.
- **The loop window is a bar under the grid, click or drag, nearer handle wins.** Kirnu's own rule
  (*"Loop points follow mouse click... the pointer closest to the mouse is moved"*), which is
  already a left-click-only path - so it needs no steppers beside it, unlike every other value on
  this page. **Kirnu's right-button-moves-the-far-handle half is not taken**: the right-click list
  is closed. The bar is laid out off the same rectangle as the grid, the rule the MUTE strip already
  follows, or every window cell slides off the step it belongs to.
- **The Note lane says notes.** A cell reading "3" is an index into a sorted chord nobody can see;
  the engine publishes what those indices currently name (`uiSeq`, written in `buildSequence` - the
  one place the sequence changes, so the lane follows a chord the moment it lands rather than at the
  next step) and the grid writes `E3`. Falls back to the number when nothing is held or the cell is
  under 28 px.
- **The Draw page is 358 px, up from 298**, and is now the tallest of the three where Play used to
  be. `contentHeight()` returns `pageHeight()`, so this moves the window only on this page - the
  cost paging already carries, spent on the page it buys something on.
- **The Note lane's top half is eight per-step shapes, and that is the pass's real headline.**
  From Cthulhu's Note graph (its manual p23-24), which is what Owen was pointing at. Values
  **13..20** name a `Direction` through `shapeForNoteValue`, appended above the Prev/Hi/Low/Rnd
  modes in Cthulhu's own bottom-to-top order so a drag up the lane meets them as the manual lists
  them. **They share one walk**: `nextDirectionIndex` gained an overload taking an explicit
  direction and the cursor is still one cursor, which is what "varies consecutively one step after
  another" means - four steps of Up then four of Down comes back down the line it went up. Mutate
  applies after this, so a per-step shape and Mutate compose.
  **`Direction` gained `fingeredBottom` and `fingeredTop`** (numDirections 12 -> 14), so the line's
  own Shape combo has them too. Appending is the only safe direction and `shapeBase` in the arp
  tree is what makes it safe; **all four shape-name lists must grow together** (the APVTS choice in
  `createLayout`, both `shapeBox.addItemList` calls, and `shapeNames[]` on the slot card, whose
  static_assert is the only thing that catches a missed one). The fingered walk covers the notes
  that are **not** the extreme it alternates with, or a triad comes out C-G-G-G instead of C-G-E-G.
- **The Note lane draws a marker at a height; every other lane draws a bar up to one.** The
  difference is what the value *means*: a Velocity of 120 is a magnitude and a filled column says
  so, but a Note of 5 is a name, and a column filled to 5 reads as "more than 4" - not something a
  chord entry can be. It is also what makes room for the shape glyphs, which are contours of six
  dashes drawn **inside** their own taller markers (`drawShapeGlyph`). Dashes drawn outside the
  marker were tried first and read as noise; the marker has to contain the picture.
- **A Reset lane (`laneReset`), Cthulhu's Position Reset.** It zeroes `dirCursor` and **must not
  touch `stepBase`**: the manual's example is about which note of the chord comes out, and rebasing
  the lanes onto the reset step would leave that lane reading its own reset cell for ever, so the
  pattern would never move again. It runs **after** mute, rest, chain and chance, so a reset on a
  low-Chance step does not fire on the passes the step itself skipped. It is a lane rather than
  Cthulhu's alt-click because the right-click list is closed and a modifier is not a gesture Keys
  may require - which is exactly what this file already said a per-step version would have to be.
- **`buildLaneRow` no longer takes a lo/hi pair, and that was a real bug, not tidying.** Those
  thirteen pairs were a second copy of `ArpEngine::laneRange`, whose own comment says three tables
  that must agree is three tables that will not. Widening the Note lane's range in the engine left
  every grid still clamped at the old ceiling, so the new values existed and could be neither drawn
  nor set. **The grid reads `laneRange`. Do not reintroduce the arguments.**
  In the same family: the lane tab row divided its width by a hard-coded twelve, so appending
  Reset laid its tab out at **four pixels** - the identical starvation the Chain lane caused when
  it made twelve, one row lower down. It counts `hasTab` now, and `LayoutTests` caught it.
- **MUTATE and LOCK replace CHANCE on the macro cards, and Mutate is not a reversal of the Drift
  rule.** *"Drift changes how a step plays, never which note it plays"* still stands, and Mutate
  meets it rather than breaking it: the fear behind that rule was a machine wandering onto notes
  nobody aimed at, and `mutatedIndex` moves the run to a different entry of **the sequence already
  built from the held chord**. Every note it can reach is a note that chord contains; the reach is
  in **chord entries, never semitones**. (**True again at every setting since 2026-08-21**, after two days
  in which it held only to the knob's halfway point: `mutatedPitch` moved out to its own
  parameter, `Stray`, defaulting to off - see the round at the top of this section.
  `ArpTests.cpp` sweeps 10..100 for the in-chord claim once more, and pins Stray's own zones
  separately.) `laneRand` is still the
  only thing allowed to change a note you *drew*, because you drew it there.
  **LOCK is the Turing Machine** (`docs/SEQUENCER_LANDSCAPE.md` ranked it as the one randomness Keys
  lacked): 0 redraws every pass, 100 is one era and the first variation repeats for good. It is a
  **hash of (step, era)**, not a shift register - a register would have been the second thing in the
  engine that is not stateless from the playhead, and `laneChain` is documented as the cheapest
  possible break of that rule rather than as an invitation. The pass is measured over the window the
  Note lane actually **walks**, not its length: a four-step loop inside a sixteen-step lane comes
  round every four, and a variation that changed every sixteen would be heard changing in the wrong
  place. Mutate applies **after** whichever route picked the note (fixed index, shape walk, or one of
  Kirnu's four questions) and **before** `lastPlayedIdx`, so a later Prev repeats what was heard.
  `mutateSeed` is the line index, so two lines at the same setting never explore in lockstep.
  **Chance lost nothing**: it is still a step lane and still has its slider in the Play page's
  PLAYBACK group, which is where a control you set once and leave belongs. The macro knob strip is
  **eight** again, the width it carried until H.VEL folded into VEL's ring on 2026-08-17.


**The 2026-08-18 round, and what it supersedes.** Nine changes landed in one session; each is
written up where it belongs below, but they contradict older bullets in this file, so the list of
what is no longer true lives here in one place:

- **A pad click never reaches the arpeggiator, one level lower than before.** The click path had
  been clean since 2026-08-02, but a chord still got there: a line with **Play** on lifts note
  events out of the outgoing stream, and a pad's chord sat in that stream beside the keys you
  play, indistinguishable. Play means *the keys you play* now. Pads, the live card and the
  generator's audition queue into a second output collector (`chordCollector`) that drains
  **after** the lift, so no line can reach them; the keybed, the MIDI input and the MCP bridge are
  unchanged. Both queues feed the same buffer and the same instrument - only the *ordering*
  separates them, because the audio thread cannot recover who asked for a note from the
  MidiMessage that arrives, the same reason `dest` exists. `chordStream` records which of dest 0's
  two queues opened each pitch so its note-off follows it there: `noteRefs` counts owners across
  both, so a pitch can be opened by the keys and closed by a pad, and a release down the other
  queue would strand the note in a listening line's engine forever.
- **A card sounds on release, and a drag makes no sound at all.** **Superseded 2026-08-22** -
  the press owns it again, and the Play toggle is what made that affordable; see the bullet above
  and keep reading here for the reason the release ever won. Firing a chord *chokes* the other
  chord sources, and with Exclusive on that reaches each line's held chord - so a press that
  turned out to be a drag had already stopped line A before the card moved, and silencing the
  blurt on the drag does not put that back. What changed is not that this stopped being true but
  that there is now a switch whose whole job is it: **Play off makes the strip drag-only**, which
  answers the drag case exactly, where a second tick only made the sounding half half-hearted for
  everyone. Turning Exclusive off alongside it is what makes the drag free.
  `LayoutState::padHoldToPlay` and the *Chord pads play while held* menu row are **deleted**; an
  older session's stray property is ignored on load, which is all an unknown key in the layout
  tree has ever cost - it carries no index anybody stores, unlike an APVTS parameter.
- **A drag that lands on nothing is a cancelled drag.** Dropping a card where no target claimed it
  used to clear the pad. Too much of the window is neither the strip nor a target, and dragging
  *up* into the arpeggiator crosses the Pads bar on the way, so a near miss destroyed the chord.
  Clear pad is the card menu's, which is where it was already documented to live. **The
  `ChordPads::mouseUp` drag guard named in the undo bullet below is gone**; the other three
  entries in that list stand.
- **Routing a chord never navigates to the line.** `followAim` / `makeCurrent` are deleted: a drop
  on a line's switch, its macro card or the panel no longer calls `setEditLine`. It wrote nothing,
  but every per-line readout jumped to the dropped-on line's own settings under the hand that was
  routing a chord, which is indistinguishable from the drop having changed them (Owen: "the number
  of steps changes, straight vs triplet etc"). The justification had expired anyway - it existed so
  "the next card click follows the same aim", and a card click stopped feeding a line on
  2026-08-02.
- **Two lines share one grid, and never cut each other short.** `HostClock::hasGrid` hands the
  engines the beat count Keys already kept for Launch Quantize, so Anchor works with no transport
  and two anchored lines walk in lockstep; `ArpMerge` folds their outputs under one note-on per
  sounding pitch, released by the last line holding it. Both are written up in
  `docs/ARP_DESIGN.md` under **Two lines at once**.
- **VEL is MIDI velocity, 0-127.** `arpVelLevel` replaces `arpVelTrim`, which stays registered and
  is read by nothing; `migrateVelLevel` converts through the old trim's own squared curve against
  76, the midpoint of the pads' default Humanize band. `arpHumanVel` widens to 0..127 and is now
  velocity units below the level rather than a percentage of a 30% shave. The engine's 0.05
  audibility floor went with the trim: it existed because a *trim* had to reach silence past a
  floor protecting programmed dynamics, and a level is the velocity itself. **Every bullet below
  describing Vel as bipolar, squared, or centred on "as played" is history.** The pads' own
  Humanize range moved 1..127 -> 0..127 for the same reason, and `noteOn`'s emitted floor dropped
  from 0.04 (about velocity 5) to one MIDI step, so the bottom of that band stopped lying.
- **The generator's Key and Mode drive Root and Scale.** They were independent settings that both
  read as "the key", so the keybed greyed to Scale while you were setting Mode - accurate, and
  about something else. One-way: `modes::kitScaleIndexFor` is total, but Whole Tone and Chromatic
  are scales the generator cannot express. **Mode is back on the Pads bar** beside Key.
- **The audition tray is twelve cards in three rows, not sixteen in four.** The pad strip went to
  twelve a page on 2026-08-03 and the tray never followed, so Fill generated four candidates that
  could never be committed. `ChordGenPanel` reads the row count off `ChordTray::rows` so the two
  cannot disagree again. The generator window also gained **page tabs**, a **Send all to pads**
  button, **SET / ROLL chips** in place of the six constraint tick boxes, and a **maximum height**:
  it had a floor and no ceiling, and nothing in that layout absorbs slack any more.
- **A drag out of the main window while a second Keys window is open.** JUCE resolves a
  cross-window drop through `Desktop::findComponentAt`, which returns from the first window whose
  bounds contain the point and never falls through to a lower one. The generator calls `toFront`
  when it opens; the plugin editor is a *child* window inside the host's and never reports the
  same thing, so JUCE's ordering kept the generator on top for good and swallowed every drop aimed
  at the arpeggiator. `ChordPads::beginChordDrag` reports its own window as front before starting
  a drag, which is simply true - the press that began it was there.


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
  remembered, so note-off matches even if octave/scale change while it sounds. This was the
  **only** thing `scaleLock` reached until 2026-08-26, when it began snapping the arp's output too
  (`ArpEngine::snapToMask`); a chord pad's stored notes are still fired exactly as stored. See the
  round at the top of the Architecture section.
- **Chord generation is a weighted pool.** `ChordGen.h` builds candidate chords in
  tiers (diatonic → borrowed → secondary dominants → chromatic); Scale Compliance
  decides which tiers enter it, Lock Influence re-weights it toward the families of
  locked chords. `ScaleModes.h` is deliberately *not* the kit's scale table: that one
  answers "is this note in the scale", generation also needs a quality per degree.
  All of it (plus `ChordSuggest.h`) is UI-free so it unit-tests.
  **Eight brains, not two** (seven on 2026-08-01, Library on 2026-08-18). `ChordSources.h` adds
  circle of fifths, Neo-Riemannian PLR, progression templates, negative harmony and planing to the
  weighted pool and `ChordMarkov.h`; `ChordLibrary.h` adds **Library**. Plus **voice leading** as a
  post-pass over whatever any of them produced (`genSmooth`, a percentage in the window's top row,
  not a source of its own; it changes only which octave a note sits in, never which notes).
  `ChordGenMenu::generateChords()` is the one dispatcher for all but Markov, which keeps its three
  paths because its chords carry a numeral ChordGen has no field for and its per-pad regenerate
  steps the chain from the left neighbour.
  **Library is the odd one out and worth naming as such: the other seven *compute* a chord
  sequence and it looks one up.** Everything downstream is identical either way, because they all
  hand back `chordgen::Chord` - so a library row goes through `fitVoicing`, `applyMajorMinorBias`
  and `applyVoiceLeading` exactly as a generated one does. See the **chord library** bullet below
  and `docs/CHORD_LIBRARY.md`.
  **Never reorder or insert into the `genSource` choice list.** Appending is safe and is why
  sessions saved before this still open on the right brain: APVTS stores a choice parameter's
  plain index, not a normalised fraction. Reordering would silently move every saved session's
  source, and there is no migration hook for it the way `migrateRateMode` covers the arp's clock.
  **The voicing is three post-passes, not seven implementations** (2026-08-01, Owen: "all of
  their options should have the option for how many notes and what inversion"). Note count
  (`genNotesMin`/`Max`, a range 2..11), register (`genOctave`/`genOctaveMax`) and inversions run
  in `fitVoicing` over whatever any source produced. **`Lean` (`genMajMin`) moves the thirds
  first**, then `fitVoicing`, then `applyVoiceLeading` picks the octaves. That order is
  load-bearing: Lean changes *which* notes a chord holds and the other two only move them about,
  and smoothing has to be last because `fitVoicing` moves whole chords between octaves and would
  undo it. `fitPads` is the Markov twin, since its chords carry a numeral `chordgen::Chord` has no
  field for. Growing stacks further thirds **through the mode** so an eleven-note chord is still in
  the key; shrinking drops from the top so the root and third survive. Inversions **replace** the
  rotation a chord arrived in, so ticking R alone means root position even from a pool that had
  inverted. **Inside `fitVoicing` the order is root position, then the count, then the inversion**,
  and that is load-bearing too (2026-08-01): `chordgen::rootPosition` is what makes an inversion
  replace rather than compound, but it also collapses repeated pitch classes and restacks what is
  left inside one octave. It ran *after* the grow loop for a few hours, which quietly threw the
  grow loop away - stacking thirds through a seven-note mode comes back to the root's own pitch
  class on the eighth note, so every count above seven returned seven and the two-octave stack
  returned a one-octave cluster. The ceiling is the number of distinct pitch classes the stack
  *visits*, which is not the mode's size: the +3-and-search step skips the degrees a third does
  not land on, so a pentatonic mode caps at **three**, not five. `ChordSourceTests.cpp` pins that,
  since `fitVoicing` is private to a class needing a live processor.
  **Changing a setting generates nothing** (Owen, same day: "I don't want it to auto generate when
  you change a source"). `settingsMovedSinceFill()` only makes the tray caption say so. The tray
  rerolled on any settings change for part of that day, which threw it away six times while you
  swept Source to hear the seven brains: a control you cannot explore without destroying your work
  is one you stop touching. Fill and Regen generate; nothing else does.
  **The gates are constraints, not enables** (`genUseKey`, `genUseMode`, `genUseOctave`,
  and they are SET / ROLL word chips rather than tick boxes since 2026-08-18 - see the round at
  the top of this section)
  `genUseNotes`, `genUseInversions`, `genUseCompliance`). Unticked means the generator rolls that
  setting itself, and Key and Mode roll **once per generation** rather than per chord, because
  every source takes a single root and mode for a whole batch. Lock Influence, Smooth Voicing and
  Lean have no box on purpose: their own zero already means off, so a box would be a second
  control for what the dial says. `readsScaleSettings()` and `readsMode()` are **two different
  questions** - Compliance and Lock Influence belong to the weighted pool alone, but Mode is read
  by every source except Markov, and conflating them greyed Mode under five sources that use it.
  **`SourceViz` is a picture and nothing else**: read-only, click-through
  (`setInterceptsMouseClicks(false, false)`), writes no parameter and plays no note. It draws the
  current source and the walk that produced the tray. If it ever needs to *do* something, that is
  a different component.
  **Rebuilt 2026-08-16** (Owen: "reexamine the graphic visuals on the chord generator. They don't
  make any sense"), and the lesson is a layout one that looked like seven separate art problems.
  Each branch gave the **diagram** a square the height of the 112 px box - ~80 px - and spent the
  other ~1500 px on a chip row restating the chord names already on the sixteen tray cards below.
  Informative half tiny, redundant half enormous, and *that geometry* is what broke the two
  wheels: `radius = jmax(20, wheelBox.getHeight()*0.5 - 16)` came to **24 px**, and twelve labels
  at `radius + 10` is ~17 px of arc for text needing ~18, so they overlapped. Chip rows deleted,
  strip at **160**, diagram takes the width, and **every source carries a one-line legend** - the
  single biggest "makes sense" win, because a diagram nobody can name is decoration. The two
  wheels anchor left and spend the freed width on a pill-and-arrow chain in Markov's own visual
  language; **the arrow carries the relationship** (signed step distance round the circle,
  `root → mirror` pairs), which is the entire difference from the rows that were deleted - those
  were bare names with `>` between them and said nothing the tray did not.
  **`ChordPad::numeral` is written only by the Markov branch** of `generateCandidates()`; every
  other source leaves it empty and sets `degree`. Progressions read it directly and drew sixteen
  `?`, and the second symptom is the instructive one: with every numeral equal, the repeat-period
  search found period 1 and drew one degenerate bracket per card, so a *display* bug produced what
  looked like a *logic* bug one component away. `progressionNumeral()` resolves numeral →
  `degree` → `degree` re-derived from `rootPc`, cased by the mode's per-degree quality, `?` last.
- **The chord library is a table you look things up in, and its own window** (2026-08-18, Owen:
  "collections or books of chords and progressions, things that go well together ... an outstanding
  library that makes it easy to compose, maybe organized by emotion or something. Scaler, the other
  VST has done a great job of this"). Full design and paper trail in `docs/CHORD_LIBRARY.md`; the
  load-bearing parts:
  **Keys already had two progression libraries that did not know about each other**, and that is
  the finding the whole feature came out of. `MarkovData.h` holds 88 hand-authored, **mood-tagged**
  progressions that no user could ever look at - the Markov source shreds them into bigrams and
  throws the sequences away, so asking for "Nostalgic" returned a statistical blur of the
  nostalgic progressions rather than the progressions. `ChordSources.h` held seven named ones with
  no tags at all. `ChordLibrary.h` is the table that joins them up: **355 rows**, sixty of them
  folded in from `MarkovData.h` (the other 28 of the 88 were already there under names of their
  own). They stay in `MarkovData.h` too - the Markov source still wants its transition table - so
  that fold is a **copy, not a move**.
  **Three axes, and the third one is the point.** Mood and Genre are Scaler 3's own vocabularies
  (Owen's copy of the two CSVs is at `E:\Ableton\Scaler 3 Moods and Genres\`), plus five words Keys
  already used that Scaler has no equivalent for: Haunting, Nostalgic, Rebellious, Spiritual,
  Tender. **Function** - Loop, Cadence, Turnaround, Vamp, Lift, Descent, Turn, Open - is the axis
  Scaler does not really have and the one that turns browsing into composing: "sad" is forty
  candidates, where "sad and it loops" and "sad and it ends" are two different requests. Two words
  on Scaler's own mood list give the game away, since Inconclusive and Resolved are not emotions.
  **A word list is a taxonomy, not a compilation.** Progressions are not copyrightable; a curated
  *list* of them can attract thin copyright in its selection and arrangement. So the content is
  authored here from the named canon, modal vamps, jazz turnarounds and film-score mediants -
  **written out from music-theory knowledge, with no corpus queried and no statistics computed**,
  which `ChordLibrary.h` says at the point somebody would extend the table. Sixty rows are
  `MarkovData.h`'s, itself hand-authored for Keys. **Nothing is copied from another product's
  curated library.**
  **Stored as roman numerals**, in the grammar `ChordMarkov.h` already parses, so one row serves
  twelve keys and the storage format is the notation the cards print in their corner. Six suffixes
  were **appended** to `suffixTable()` to make that possible (`m7b5`, `mM7`, `m6`, `madd9`, `M9`,
  `m9`); half-diminished is the one whose absence was not cosmetic, being the ii of every minor
  ii-V. Appending there is safe *unlike almost everywhere else in Keys*, because that table is
  looked up by exact string and nothing stores an index into it - which is precisely the opposite
  of `genSource`, the lane indices and `chordgen::types()`.
  **One spelling per chord**: a plain triad is `i`, never `im`; `bii`, never `bIIm`. Both parse to
  the same chord, and two rows holding one progression under two spellings are invisible to the
  duplicate check that is the only thing standing between this table and the same four chords
  appearing three times in one filtered list.
  **The table validates itself on every build**, and that is not ceremony. A hand-typed numeral has
  two silent failure modes: a token that will not parse is *skipped* at play time so the
  progression merely comes out short, and a misspelt tag is a row the picker that wanted it can
  never find. Neither shows a symptom on screen. `tests/ChordLibraryTests.cpp` walks every row for
  both, checks each transposes identically into all twelve keys, and **refuses two rows that hold
  the same chords and also share a genre**. That last rule caught fourteen redundancies while the
  table was being written and forced the canonical spelling above. Reusing one progression under
  two names is *correct* - the same four chords are how you find it from the Disco end and the Neo
  Soul end - so the rule is genre overlap rather than a flat ban.
  **`chordsFor` takes the mode to label degrees against as a parameter**, and `ChordGenMenu` passes
  **the entry's own** rather than the session's. Every other source wants the session's, and is
  right to: they generate *in* that mode, so a chord outside it genuinely is a borrowing. A minor
  row read against a major session resolves nothing, and the tray came back with half its cards
  labelled `?` about a progression perfectly in its own key. `degree` is stored on the pad, so this
  is what the strip shows afterwards too.
  **A generation lays whole progressions end to end rather than looping one.** The first cut looped
  a single row the way `sources::progressions` does with its templates, and it was wrong here for a
  reason only visible on screen: the library holds vamps, and rolling the two-chord "Minimal
  one-chord" filled all sixteen tray cards with the same Cm9. End to end, a **Vamp** filter gives
  eight vamps to compare and a **12-Bar Blues** fills the tray on its own. Rows are drawn shuffled
  and without replacement, so Regen is never inert under a narrow filter.
  **Two surfaces, and they are one state.** The **Library** entry on the generator's Source row
  (its band is Mood / Genre / Does-what plus a readout), and **`ChordLibraryPanel`**, a window of
  its own off a **Library** chip on the Pads bar. The three picks live on `ChordGenMenu`, not the
  APVTS - the shape Markov's Mood and Start already use, plus one reason of their own: a choice
  parameter's item list is append-only forever once a session stores an index into it, and locking
  a 46-word mood vocabulary into the layout before the library has settled would mean never being
  able to drop or rename one. The cost, and it is real: a pick does not survive reopening the
  session, exactly as a Markov mood does not.
  **The window is paged, not scrolled** - twelve rows and a `<` `>` pair, the pad strip's own
  shape. 355 rows is a scroll, and a scroll is the gesture the mouse-only contract is worst at: a
  scrollbar thumb is a small target that has to be *dragged*, and a wheel is not a gesture Keys may
  require. A row is a chord card that happens to hold several chords - the whole row is the Hear
  button, a second click on the walking row stops it, and two buttons at its right end send it to
  the generator's tray or straight onto the page's empty pads through the same
  `sendChordToFirstEmptyPad` every generation uses, so **nothing overwrites a chord you already
  have**, as one undo entry for the whole progression. **To tray greys when the generator window is
  shut** rather than opening it behind your back.
  **`ChordGenMenu::auditionProgression` walks a progression one chord at a time**, 550 ms each with
  the last given the full 800 ms a single chord gets, so a cadence is heard *arriving* rather than
  stopping. It lives on the brain for the reason every audition does - it calls `noteOn` with no
  pad behind it and the brain outlives every window - and it shares the one timer, with the queue
  saying which kind of tick this is. A second timer would be a second thing able to leave a note on.
  **`lastLibraryEntry` means "the row the tray is holding"**, not "the row the last generation
  used". It said the latter for about an hour and the To-tray push made that reading wrong on
  screen straight away: the tray held Bird changes and the diagram was still captioned Folia.
  **A pad remembers what it is part of.** `ChordPad::progression` (the row's **name**, never an
  index - `chordlib::table()` is the one append-*and*-insert-safe table in Keys precisely because
  nothing stores an index into it, and one here would take that back) and `progressionStep`. The
  strip brackets a run of adjacent pads sharing a row in step order, name on the run's first card.
  A run breaks on a row change, a step that does not follow, **and a row break** - pads wrap from
  the sixth to the seventh, so 5 and 6 are adjacent by index and nowhere near each other on screen.
  A run of one draws nothing: that is a chord that remembers where it came from, not a progression.
  **A pad from the library takes its numeral from `chordlib::numeralAt`, never from `degree`.**
  `degree` is an index into the mode a chord was *generated* in, and a library row is generated
  against its own - so an Andalusian cadence in a C major session read back as `I vii vi V` under a
  bracket correctly naming it. A row and a step name a chord exactly and need no mode.
  **`chordlib::couldFollow` is the relational layer**, surfaced as **Follows** in the library
  window. Two signals: `functionsAfter` **gates** (what follows a Cadence is not what follows a
  Turnaround; nothing follows an Open with another Open) and the harmonic join **orders** (last
  chord against first - falling fifth top, a repeat last without being disqualified). Function is a
  gate rather than a weight on purpose: a row that does not belong after this one is not a weak
  answer, it is the wrong one. It points at the **pads** rather than a row you select, scanning the
  current page backwards, because the last progression laid down is the one being followed.
  **Favourites are per session and kept by name** (`LayoutState::libraryFavourites`), which is the
  honest weakness - Scaler's are global and Keys has no global store for anything, the settings
  gear's own switches included. The star is *painted* rather than a `TextButton`, the lock-dot call:
  twelve more Components to lay out and re-title per page turn, for a two-state mark.
  **The row is a table and the columns are measured.** Each chord is one column - the numeral over
  the chord it comes out as in the current key - because two independent strings in two fonts drift
  apart along a row until the pairing has to be worked out rather than seen. Measured with
  `GlyphArrangement`: **`Font::getStringWidthFloat` under-measures**, and sizing the columns with it
  made every cell as wide as the *chord* underneath, so `iim7` drew as `iim` and a bare `V` as
  nothing at all. A row's name also drops its own trailing numerals when they are *exactly* the
  numerals shown beside it, which is display-only - `Entry::name` is still the identity a favourite
  and a pad store.
  **Where the rows came from, and it is not what the docs first claimed.** They were **written out
  from music-theory knowledge**; no corpus was queried and no statistic computed, which
  `ChordLibrary.h` states at the point somebody would add a row. That claim was overstated in seven
  places and corrected on the same day - if a row looks wrong it is wrong because somebody thought
  it was right, not because a dataset said so.
  **The corpora are in `datasets/`, gitignored, with `datasets/README.md` as the manifest** and the
  licence column as the reason it exists: the MIT MIDI pack may feed shipped content, Chordonomicon
  is CC-BY-NC and is Owen's personal-use call. **The trap they exposed is a second roman-numeral
  convention**: that pack spells minor progressions against the *minor* scale, so its `III`, `VI`,
  `VII` are already flat, and its `i VII VI V` is Keys' `i bVII bVI V`. Import one verbatim and it
  parses perfectly and plays the wrong chords, which `ChordLibraryTests.cpp` cannot catch.
  **The mood tags have been checked, not proven** (`docs/CHORD_LIBRARY.md` §11). The control - minor
  should read sadder than major - **failed first**, because the obvious split counts minor chords
  and a minor key is full of major triads, so the Andalusian cadence landed on the major side. Split
  on the row's declared mode and it passes at +0.018, which is then the yardstick: the mood spread
  is three times that, so most of it is genre and production rather than harmony. Valence and
  arousal are two numbers and the vocabulary is 46 words, so Haunting and Eerie can never be
  separated this way. No tag was changed by any of it.
- **A single note is a chord card** (2026-08-21, Owen: "I also like to allow one note to show up
  in the chord pad and the chord preview"). One line was refusing it: `ChordPads`' file-local
  `isChord` answered `notes.size() >= 2`, and it gated three things at once - the live card
  **named** a held note only from two up ("hold a chord" with one key down), the card could not
  be **pressed**, and an empty card is not draggable, so a single note could not be **carried
  onto a pad** at all. **Nothing downstream ever needed two**, which is the part worth
  remembering: `chords::detect` already names a lone pitch class by its note name,
  `applyInversion` and `applySpread` both return a one-note chord unchanged, `fitVoicing`'s
  shrink already guarded `want >= 1`, `setChordPad` has always stored what it was handed, and
  the arp builds a one-entry sequence from it. A gate was refusing what the rest of Keys could
  already do.
  **The predicate split in two, because one name was asking two questions.** `hasNotes` - is
  there anything here to show, play or drag - is what the live card, its press and its drag use.
  `canRevoice` keeps the genuine two-note requirement and is what **Next voicing** greys on,
  since a voicing moves notes about *within* a chord and one note has nowhere to go. A one-note
  pad is legal; that row simply has nothing to do with it. **Do not re-merge them.**
  **The generator's Notes range starts at 1**, so single notes can be asked for deliberately -
  bass lines, pedal tones, stabs. Widening the bottom of an int parameter is safe in a way
  reordering a choice list is not (the `genSource` rule): every value a saved session could hold
  is still in range, and the 3/4 defaults are untouched.
  **An unticked Notes range rolls 1..11 as well**, so the tray turns up the odd single note among
  its twelve. This was built the other way first, on the reading that a bare note would read as
  the tray having failed; Owen overruled it the same day, and the general rule is his: **an
  unticked gate rolls the whole range its parameter can express**, never a hand-picked sub-range,
  or the tick box stops meaning "you decide" and starts meaning "you decide, within limits nobody
  wrote down". `StateTests` pins both halves - a one-note pad round-tripping, and the parameter's
  floor - because a bound like that is exactly what gets tidied back to what the code around it
  assumes.
- **Every filled chord card carries its roman numeral in the top-left corner** (2026-08-18, Owen:
  "I want the progression number to show up in the generator on the chord pad") - the chord pads
  and the generator's audition tray both, because those are the same card read at two moments.
  "Am" says what a chord *is*; "vi" says what it *does*, and the second is what makes a row of
  cards read as a progression. Top-left is the one corner a card had left: the lock dot owns the
  top-right and the arp line's letter the bottom-right.
  **`src/ChordNumerals.h` is the one implementation**, moved out of `SourceViz.cpp` where it was
  private to the Progressions diagram. A copy per surface would have re-armed a trap that file has
  already paid for: the diagram drew sixteen `?` for a whole build because it read `numeral`, which
  only Markov writes, where every other source writes `degree`.
  **It answers empty rather than `?` when nothing resolves, and the surfaces part there.** A card
  draws nothing - a `?` in the corner of every hand-captured pad is noise standing in for
  information - while the diagram keeps its `?`, because it draws one chip per step and an empty
  chip would read as a gap in the walk. Pads resolve against the `genRoot` / `genMode` **parameters**
  rather than `ChordGenMenu::genRoot()`, which answers with whatever an unticked Key or Mode rolled
  for the last generation: a pad outlives that roll.
- **Two arpeggiator lines, A and B** (2026-08-02, Owen: "I only wanna view two arpeggiators in
  this window, and I wanna be able to drag a chord from below to each one"). **Superseded
  2026-08-19: both constants are 4 now** - see the round at the top of this section; the
  count-lives-in-two-constants machinery this bullet describes is exactly what made that a
  two-line change. Historical text follows. Everything in the
  bullet below still describes the machinery; what changed is the count, and it lives in one
  place. `numArpLines` stays **3** - the engines, the storage and the `arp3*` parameter ids are
  untouched, because dropping parameters from the layout is what breaks saved sessions -
  and **`uiArpLines` is 2**, which is what the UI counts off and what `arpLineOn()` gates on.
  That one gate makes line C inert everywhere at once (no engine run, no keys, `cardsFeedArp`
  stops counting it) rather than leaving an arpeggiator running that nothing on screen can stop.
  `arpCurrentLine()` clamps to it too, so a session saved with C current opens on B.
  **`layout.arpMacro` defaults true**: Keys opens in the macro view with both lines on screen
  over the chord strip, because dragging a card up onto a line's row is the point of the view.
  Raising `uiArpLines` back to 3 brings C back.
- **Three arpeggiator lines, A B C** (2026-08-01, Owen: "three arpeggiators so we can get
  polyrhythms and keep keeping what we currently have ... and then being able to feed cards into
  different lines"). Three `ArpEngine`s, each with its own rate, shape, step lanes, twelve slots,
  chord and chain. **`ArpEngine.h` did not change**: it never knew how many of it there were, so
  three of them cost a routing layer and no engine work. **Line 0 keeps every parameter id it has
  ever had** (`arpRate`, `arpSwing`, ...); B and C are `arp2*` / `arp3*`, appended, defaulting to
  off, so a session saved before this opens sounding identical. Two ids do appear on line A,
  `arpKeys` and `arpChannel`, both defaulting to the old behaviour.
  **Routing is a queue per line, never a pitch mask.** Each line has its own
  `MidiMessageCollector`; a chord handed to it is fired through the ordinary note path (so
  Exclusive, the Voices cap, Strum and the keybed lights all still apply) but queued there, and
  only that engine drains it. `runArpLines` lifts the keybed's notes out of the merged stream for
  the lines with **Keys** on, runs each enabled line into its own buffer, and merges them back
  (**and since 2026-08-27 the track's own MIDI is not in that stream at all unless Track MIDI is
  on** - it is held aside in `processBlock` and put back after, so the arp cannot see it and the
  instrument downstream cannot tell; see the round at the top of this section);
  a *disabled* line's input passed straight through, so a chord held to it sustained (**no longer
  true from 2026-08-02** - see the entry above; the engine now runs every block and takes the
  chord in silently). A per-pitch
  ownership mask races - the message thread can clear an owner before the matching note-off is
  drained, stranding that note in an engine's held set forever - and that is why it is queues.
  **`noteRefs` is per destination stream** for the same reason: "one note-on per sounding pitch"
  is a statement about one stream, and a pitch held into line B must not suppress the same pitch
  played to the output. On screen: **A/B on the arp bar** (one per line's On, and Hold off is
  still one button that releases both), **a tab per line on that same bar** choosing which
  line the panel edits (they lived at the left of the slot row until the fourth 2026-08-02
  pass; a tab change rebuilds every APVTS attachment against the new ids, the same move
  `refreshRateMode` makes for the rate dial). A letter chip on the Pads bar named the line
  *Send to arp slot* targets until 2026-08-02; it went when a card click stopped feeding a
  line at all, and those same tabs say it now. **Dragging a chord card onto an arp slot binds it there**, or
  onto a tab - or onto a line's **card in the macro view**, which is the same target the size
  of half the panel rather than the size of a tab - to hand it over now. The left-click twin
  *Send to arp slot* never had. **The pad menu's `Send to arp A` / `B` rows are the aimed
  accelerator** for the same thing (2026-08-16), through `KeysEditor::sendPadToArpLine`;
  `ArpPanel::takeChordOnLine` takes a **pad slot** rather than a drag payload for that reason,
  since the pad slot was the only thing a line ever wanted out of a drop. The slot cards, the tabs and the macro cards are each a
  `juce::DragAndDropTarget` (2026-08-02, see the chord-drag bullet below); JUCE walks *up* from
  whatever is under the point, which is what makes the whole macro row a target including the
  knobs on it. A drop sets the current line and never changes the view
  (`setEditLine(line, false)`): it is routing a chord, not navigating.
  **The menu row does not even set the line** (`followAim`, 2026-08-17). A drop *aimed* at the
  line, so the aim may follow the hand; a row reading "Send to arp B" promises to move a chord,
  and moving the panel with it tore you off the page and lane you had open - several clicks back
  on a mouse-only surface. Same method, one flag, and the chord lands either way because that
  call is unconditional; all the flag decides is whether the panel goes and looks.
  **A fourth tab, All, is the macro view** (2026-08-01, Owen: "the goal is to be able to create
  complex polyrhythms from one view"). It replaces the band and the step editor with one *card*
  per line, side by side under a 34 px header (2026-08-02, Owen: "parallel to each other
  instead of one on top of the other"). A card is three stacked lines - the line switch, a
  detented rate knob with its `<` `>` and Sync/Hz, and the shape with its own steppers; then
  **eight knobs** under their own headings (Oct, Gate, **Mutate, Lock**, Swing, Offset, Vel,
  H.Time - Chance became those two on 2026-08-18, see the step sequencer pass above; Oct
  Oct is the *transpose*, Vel is the bipolar level, and Humanize's timing half lives in
  H.Time; all three are the 2026-08-02 entries below). **H.Vel folded into Vel's own ring on
  2026-08-17** rather than keeping a knob of its own - see the RangeKnob bullet further down.
  Then Dot / Tuplet / Anchor with the held
  chord - because half the panel's width cannot hold what used to be one full-width row. It
  carried more for its first day - Latch, PLAY, Chain, a shared BPM row below and the slot row
  under that - and the slim-down bullet below is where all of it went. The knobs are the band's
  own rotary, not sliders, and every card carries its own headings: "written once on the top
  row" only worked while the rows stacked and B's columns sat exactly under A's.
  **It is a view, not a fourth line**: `editedLine` is untouched by it, so a chord card still has
  one target while all three are on screen, and the panel does not grow because the rows take the
  band's space rather than joining it. Each row's attachments bind to its own line for good,
  unlike the band's, which rebind on every tab change - three lines at once cannot each be "the
  current line". **A tab selects; it does not toggle** (clicking All while All is up means All).
  Two layout traps already paid for: **the knob strip is reserved out of the row before Shape
  takes its cut**, because laying the knobs last and giving the final one "whatever remains"
  starved it to nothing the moment the row got tight and eight knobs drew as seven with no other
  symptom; and **coming back from the macro view must leave STEPS following Shape**, or an empty
  ruled box is drawn beside the band on every plain shape.
- **Undo is content-only, and an entry is a subtree snapshot** (2026-08-14, Owen: "we should
  have undo"). It covers chord pads, arp lanes and arp slots - what destroys music - and
  **deliberately not parameters**: sweeping the rate dial would push forty entries and shove the
  pad you wanted back off the end, which is an undo that cannot undo anything. A knob you can
  turn back; a cleared pad you cannot.
  **No action has a hand-written inverse**, so no action can have a wrong one: `pushUndo`
  snapshots `chordPadsToTree()` or `arpToTree()` - the same trees the session file uses - and
  undo restores one. Anything added later is undoable the moment its data lands in one of those
  two trees, which is the whole reason this was affordable at all.
  **One entry per gesture, not per change.** A lane drag pushes on the press and never again; a
  single stroke would otherwise fill the stack. `KeysProcessor::UndoGesture` is the RAII guard
  that absorbs nested pushes, so a drop that clears a pad and then sets it costs one entry.
  **Undo/Redo ride the Controls bar** because it never hides with its fold - an undo you cannot
  reach because a section is folded is one you cannot trust. They grey rather than vanish, so
  the pair never reflows the bar; adding them pushed the bar into its `tight` cell set, which
  drops ROOT's caption exactly as that mechanism is documented to.
  **Undo releases every sounding chord first** (`stopAllChordPads`), the same choke point an
  audition uses: restoring pads can rewrite the chord a sustained card holds, and restoring the
  arp can rewrite the lanes under a running line.
  **"There is no undo anywhere in Keys" was load-bearing in four places** - Reset beside Roll,
  Clear page in a window, pad locks, and the `ChordPads::mouseUp` drag guard. All four stay:
  they are still good behaviour, they just stop being the only thing standing between a click
  and a lost chord. Do not remove one on the strength of undo existing.
- **Three lanes appended, and one of them is not stateless** (2026-08-14, the manual round).
  `numLanes` went 10 -> 13: **Rand** (Cthulhu's "Rand Sel", how random *each step* is, bipolar
  -8..+8), **Mute** (its own lane at last) and **Chain** (Stochas' condition: 0 always, 1 only
  if the step before sounded, 2 only if it did not). **A lane's index is what a saved session
  stores it under**, so appending is the only safe direction - the `genSource` rule again.
  **Rand is the one randomness allowed to change a note you drew**, because you drew it on that
  step (**Mutate joined it on 2026-08-18** and is a different claim: it changes which note the *run*
  lands on, and cannot leave the held chord - see the step sequencer pass above); Drift is a knob wandering over a part you did not aim at, so `laneDrifts` confines it to
  the lanes that decide *how* a step plays. Rand acts only on a fixed 1-8: a Note of 0 means
  "follow the shape", and randomising zero would quietly turn Up into a fixed entry.
  **Chain is the only thing in the engine that is not stateless from the playhead.** Everything
  else is computed from the step index so a transport jump lands right without walking there; a
  condition has to remember one bit about the step before. It self-corrects within one step,
  which is the cheapest possible break of that rule and is why this form was chosen over
  Stochas' arbitrary cell-to-cell reference. If a future lane wants more state than that, it
  needs a different design, not a bigger cache.
  **Mute stopped eating the step.** It wrote -1 into the Note lane, destroying whatever was
  there; Cthulhu's manual names preserving it as the entire reason mute buttons exist. Note = -1
  is still a *drawn rest*, a different thing, and Cthulhu has both. Mute gets **no tab** - the
  MUTE row is its editor and a tab as well would be two ways to draw one lane - and it is the
  Note lane's **companion, not a polymetric lane**: it reads, writes and syncs at the Note
  lane's length, because the engine wraps each lane by its own length and a disagreement would
  silence the wrong step. `LaneRow::hasTab` exists because Mute's default-constructed button was
  otherwise counted and laid out as a tab, which ate a cell and pushed Chain to zero width -
  present in the tree, invisible on screen, absent from UIA with nothing to say why.
  **The Note lane speaks Kirnu's ORDER vocabulary**: 9..12 are Prev, Highest, Lowest, Random,
  appended **above** the fixed indices rather than below -1 so saved values are untouched *and*
  a drag to the bottom of the grid still reaches the rest. Hi and Low **scan** the sequence
  rather than taking its ends - `buildSequence` sorts by pitch only for the shapes that walk by
  pitch and stacks octaves on top, so neither end is reliably the extreme.
- **A lane appended after a session was saved arrives at the wrong length** (2026-08-14, Owen:
  "Sometimes the steps do not match each other"). Rand, Mute and Chain all come in at
  `ArpPattern`'s default 8 while the rest of the pattern may be at 16 or 32, and the grid draws
  each lane at its **own** length - so three lanes drew a fraction of the cells their neighbours
  did with nothing on screen to explain it. `nudgeLength` has always written every lane when
  Link is on; the hole is that it cannot write a lane that did not exist when it last ran.
  `ArpPanel::enforceLinkedLengths` pushes the Note lane's length and speed onto every lane
  whenever the readouts refresh, so the repair lands on load rather than waiting for a nudge.
  Link **off** is polymeter and is left alone entirely. **This is the shape of the problem for
  every future lane**, not a one-off - append a lane and this is what you owe.
- **Roll, Reset, Select and Drift are four different randomnesses, and the differences are the
  design** (2026-08-14). **Roll** rerolls the lane you are looking at, once, visibly, by an
  amount. **Reset** puts it back to its default across its length - Roll is destructive and Keys
  has no undo anywhere, so the way back has to be one click. **Select** turns a drag into a span
  and narrows both to it: Kirnu's Random tool acts on selected steps, and a selection is the
  missing primitive behind Copy, Paste and Clear too. **Drift** strays from the lanes *while it
  plays*, so the part never repeats and the lane on screen never changes. Drift sits beside
  Humanize in FEEL because the two are the same question twice: Humanize is a **player**
  wandering (late and quieter, never early, never louder) and Drift is a **machine** wandering
  (either way, evenly around the drawn value).
  **A value at the edge of its lane ignored Roll and Drift** until `ArpEngine::strayWithin`.
  Both built `[value +/- reach/2]` and **clamped the result**, which is fine mid-lane and broken
  at the ends: a value at the bottom had half of every draw fall outside and clamp back to
  itself. Late, Harmony and Chord all default to 0 and Chance sits at its *top*, so "a lane of
  zeroes barely moves" was the common case, not a corner one. **The window slides; the result is
  never clamped.**
- **A draw gesture edits the step it started on, and only that one** (2026-08-14, Owen: "I don't
  want you to be able to jump from step to step. I just want it to be for that one when you're
  moving up and down"). `LaneGrid` painted whatever step was under the pointer, so a hand
  travelling up to set a height and drifting sideways rewrote every neighbour it crossed. The
  step is captured on the press and held for the gesture; horizontal travel is ignored.
  **`MuteRow` still paints across steps and should**: there the value is a toggle and a swipe
  means "all of these", where in the grid it is a height the pointer must travel vertically to
  set. Same gesture, opposite correct answer, because the value means a different thing.
- **A line's deep view is three pages, and the arp panel is one fixed height** (2026-08-14,
  Owen: "when you click details it shouldn't resize the whole window, just the full arp
  section. and we need a way to get out the detail view", then "can we simplify the detail
  view or organize into pages"). The deep view was every block at once - band 112, band2 64,
  lane tabs 34, grid 140, mute 46, slots 58, action row 34 - **612 px** against the macro
  view's 240, so Details grew the *window* by 372 px and All shrank it back. Paged by what you
  are doing rather than by what fits, the blocks come apart at **Draw 258 / Cards 124 /
  Play 208** (**Draw is 358 from 2026-08-18** - the lane strip and the loop bar - so it, not Play,
  is the tallest page now; see the step sequencer pass above), and the tallest is eighteen over the
  macro view rather than 372. So the panel
  takes **one height for every view and page** and the window stops moving between them.
  **`contentHeight()` and `pageHeight()` are a pair and the split is the point**:
  `contentHeight()` returns the constant and feeds the editor's `idealHeight()`, so a fold is
  the only thing that can move the window; `pageHeight()` returns what the current page needs
  and feeds `cardBounds()`, so the drawn card is only as tall as its content.
  **They became one answer on 2026-08-16** (Owen: "there's some deadspace I want to remove at
  bottom", then, shown the same fault one view over, "fix arp"). `contentHeight()` returns
  `pageHeight()`, so nothing reserves room it does not use. The constant was a `max` over five
  sums, and **the cost of a max is paid silently by everything under the tallest**: the macro
  view carried 58 px of dead panel and the Cards page carried **174**, 124 px of slots in a 298
  px reservation. It was designed as an 18 px gap and stopped being 18 the same day it was
  written, when the lane-tools strip pushed Draw from 258 to 298 and nobody re-measured what
  that opened underneath every other view.
  **The cost is real and was Owen's call**: All ↔ Details moves the window 58 px, paging moves
  it up to 174. That is not the 2026-08-14 problem returning - that was 372 px on a *fold* -
  but the way back is one line in `contentHeight()` and the comment there says which.
  `setMacroView`, `setPage` and the two strip toggles all call `onPreferredHeightChanged`.
  **The standing lesson is the arithmetic, not the number**: a constant that is a max over
  several sums silently grows the gap under every view but the tallest, and nothing on screen
  says so. Grow one of those sums and re-measure the others. Without the
  second one the Slots page drew its 154 px of content pinned to the bottom of a 258 px box,
  because that block lays out from the bottom up.
  **The page tabs ride the ARP section bar**, right of All, and that is what makes paging pay:
  the bar is 34 px that already exists, so the picker costs the panel nothing - putting it
  inside would have taken 34 px off the very budget paging was buying back. Same rule that put
  Fill/Regen/Generator on the Pads bar. They show only in a deep view, so the bar reads
  `A B All` in the overview and `A B All Play Cards Draw` in a page. **That is the "way
  out"**: All stops reading as a third letter beside two power switches and becomes the first
  entry of one view group, which is the honest fix rather than a second control doing All's
  job. `refreshArpBarTabs()` owns their visibility as well as their lit state, so entering or
  leaving the macro view shows or hides them at once instead of on the next 10 Hz tick.
  **`applyPageVisibility()` only ever hides.** `refreshShape()` is still the one place a
  control is turned *on*, on its own Shape and lane gates, and this runs at the end of it to
  take back what is off the current page - so the order is what makes it correct, and a second
  writer that could also show things would be the macro card's deleted On toggle all over
  again. The three `pageSteps` / `pageSlots` / `pageSetup` lists are built once in
  `buildPageLists()`, after every control exists, and name each control exactly once.
  **The tabs read Play / Cards / Draw, and the bar's order is not the enum's.** They were
  Steps / Slots / Setup for one build the same day - five letters each, all starting with S,
  which Owen could not read at a glance ("I don't understand this layout"). The names say what
  you *do*; `KeysEditor::arpPageForTab` maps bar position to `ArpPanel::Page`, because the enum
  stays steps=0/slots=1/setup=2 and renumbering it would move the page every saved session
  opens on - the `genSource` append-only rule again. **`LayoutState::arpPage` defaults to Play,
  not Draw**: Draw does nothing until you have drawn on it *and* set Shape to Pattern, so
  opening there is opening on a blank page with no way to tell why, which is exactly where Owen
  landed. Draw greys outside Pattern shape rather than vanishing (the group must not reflow
  under the mouse), and leaving Pattern with it up falls back to Play.
  **Voice left the STEPS band group for the lane-tab row** the same day, one day after it
  arrived there. It costs no height at all now, sits beside the Harmony lane it is contextual
  on, and reads `Voice: Chord` / `Voice: Sub` - a 12 px caption over it left the button 22 px,
  under the 34 px floor, and dropping the caption is what gave the target the whole row. Its
  cell is reserved **before** the tabs take their cut and **whether or not it is showing**:
  reserve-first is the standing rule, and reserving unconditionally is what stops all ten tabs
  resizing under the mouse the moment you select Harmony.
- **The All view is the header and the two rows, nothing else** (2026-08-02, second pass -
  and by the fourth pass, below, the header itself left for the section bar and the rows
  became boxed cards, so the view is now literally the two cards) (Owen:
  "we need to make the window shorter ... remove the chain button, maybe the play and the
  [latch] button, move the BPM up into the title ... and remove everything on the bottom. Copy,
  clear, stop, chain"). The twelve slot cards and the Copy / Clear / Stop / Chain action row
  left the view - they belong to the per-line tabs, where every one of those buttons still is -
  and the A/B/All tabs moved up into the LINES header alongside the BPM cell and Launch
  Quantize, whose shared row went with them. The tabs have to survive somewhere: they are the
  route to the per-line tabs, and in this view the header is the "title" Owen pointed at. LTCH,
  PLAY and Chain left the rows the same day. **Nothing lost its left-click path**: Latch was
  already on the band, Chain on the action row, and PLAY grew a band home - **Play**, beside
  Retrigger in PLAYBACK, reserved off the right end of the row *first* because Retrigger is the
  elastic one (the Shape lesson, logged twice below). Entering the view also disarms a pending
  Copy or Clear: an armed pick with no slots on screen would fire, tabs later, on a click the
  user armed minutes ago. The view is ~110 px shorter, which is most of what the ask was about.
  **VOL became VEL and HUMAN split into H.TIME and H.VEL the same day** (Owen: "the volume is
  weird ... it should start in the middle", "maybe we could split it up into two knobs").
  `arpVelTrim` is bipolar around "as played" (**retired 2026-08-18**; up boosts, down cuts, -100 mutes exactly as VOL 0
  did) and `arpHumanVel` is the velocity shave, leaving `arpHumanize` timing-only. Both are
  **appended** parameters; `migrateVelTrim` folds an old session's Volume into VelTrim exactly
  (volume% == 1 + (volume-100)/100, no approximation) and writes the absent parameters' defaults
  explicitly, the `migrateRateMode` shape. **That shape only works because the kit's
  `state::load` now copies** (same day, kit `StateHelpers.h`): `replaceState` backfills a child
  for every absent parameter into the tree it is handed, ValueTrees share nodes, and the shared
  root reached `onExtra` with every absence erased - so every absence-detecting migration in
  this file, the older two included, had been silently dead since they moved inside that
  callback. If a migration ever no-ops on the exact session it was written for, check what the
  root actually contains before trusting the tell. The per-line FEEL group is four sliders now (Ramp,
  Time, Human Time, Human Vel), paid for with 4 points of group weight from SPREAD, which still
  fits its three cells exactly at the editor's minimum width. `ArpTests.cpp` pins the split and
  the trim, mute included. **Both parameters are unchanged by the 2026-08-17 VEL/H.Vel merge
  below**: `arpHumanVel` still means exactly what it meant the day it was appended, and so does
  this per-line FEEL slider that reads it. Only the *macro card's* knob strip stopped giving
  H.Vel a face of its own.
- **VEL is squared, floored last, and its input is clean** (2026-08-02, third pass - **all
  three of those are history from 2026-08-18**, when Vel became plain MIDI velocity and the
  audibility floor went with the trim that needed it. Owen: "I
  was at negative 96, and it was still pretty loud. Is it passing it through the humanized
  volume range?"). Three causes compounded and each got its fix. The multiplier is
  `((100+VEL)/100)^2` - hearing is logarithmic, and the linear version crammed its audible
  change into the last few degrees. The engine's 0.05 audibility floor protects *programmed*
  dynamics (a Velocity lane at 0, a hard H.VEL draw) and used to sit after the level control,
  pinning everything below about -90 at velocity 6; the fader now multiplies **after** the
  floor and bottoms at MIDI velocity 1 - never 0, which is a note-off in disguise. And a note
  bound for a line's queue (dest > 0 in `noteOn`) skips the keyboard Humanize range's velocity
  replacement, which had been re-randomizing VEL's "as played" reference per note; the keybed's
  own notes keep it, because that is playing, and they keep it even when a line lifts them.
  `migrateVelTrim` folds Volume through the curve (`100*(sqrt(volume%)-1)`), still level-exact.
  The honest limit, worth repeating to Owen when it comes up: velocity is all MIDI has, so a
  patch with no velocity sensitivity flattens every velocity control in Keys, and only the
  -100 mute (which emits nothing) cuts through that.
- **Each card is a box, the bar carries what they share, and a click never feeds a line**
  (2026-08-02, fourth pass, Owen: "we need a bit more clear delineation between the two
  arpeggiators. They kinda look like one right now"). Three changes, one review:
  1. *Delineation.* Each `MacroRow` draws its own captioned ruled frame ("LINE A" punched
     through the top rule, group-box style) with a fill behind it, and the outer LINES frame
     and caption are gone - a box around both cards was the strongest cue they were one thing.
     RATE and SHAPE micro-caps sit over the top line's stepper groups, because two flanked
     `< >` pairs touching read as one puzzle without names ("the arrows ... are not clear as
     to what they're adjusting").
  2. *The bar.* The A/B/All tabs and Launch Quantize moved from the panel to
     the ARP section bar, left end after the fold zone. **Editor-owned** (`KeysEditor::
     ArpBarTab`, `quantizeBarBox`): the panel dies with the fold and the bar
     does not. (The tempo went with them for one build and then to the Controls bar; see the
     entry below.) The tabs hide when the section folds (a tab that navigates a panel that is not
     on screen is a control with nothing behind it - the pad-pages rule) and are laid out only
     while it is open so BPM slides left rather than orbiting a hole; BPM and Quantize stay,
     arp On's own argument. Each tab is still a chord drop target and still answers to
     `Arp line A tab` / `Arp all tab`. **No longer true of A/B from 2026-08-02, seventh pass**
     (see **The arp bar's A / B become the line switch**, below): A and B became that
     line's own On switch, dropped the " tab" suffix, and never hide with the fold any more -
     only `Arp all tab` still describes this paragraph as written. BPM is a LinearBar with
     `setSliderSnapsToMousePosition(false)` - on a 48 px bar a click must nudge, never jump -
     plus `<` `>` as the click-only path. The tabs' lit state is *derived* from
     `layout.arpMacro` + `arpCurrentLine` in `refreshArpBarTabs()`, so clicks, drops and
     session loads all land in one place. **Also no longer true of A/B**: they answer to their
     own `ButtonAttachment` now and would fight a second writer, so only the All tab's toggle
     state is still derived there. The panel's `LineTab` class, its slot-row cells and
     its header strip are deleted; the twelve slots now share the whole row.
  3. *The click.* `ChordPads` no longer hands a clicked card to a line ("I don't want it to
     send it to the arpeggiator unless you drag it") - a click plays the pad whatever the
     lines are doing, `toArp()` is gone, and the one surviving left-click arp behaviour is a
     stop: clicking a *cleared* card that still feeds a line (the ring with no notes) releases
     that hold, because a dead click on a lit target is worse. The Pads bar's letter chip now
     only names *Send to arp slot*'s target and cycles without leaving the All view.
- **The tempo is a number on the Controls bar** (2026-08-02, fifth pass, Owen: "I think the
  bpm should live in the controls header. I want it to be like the bpm in ableton, just a
  number"). It has had three homes in one day - a labelled drag slider in the Controls
  *band*, then the arp bar, now the Controls *bar* - and this is the right one: **the tempo
  is the plugin's clock, not the arpeggiator's**, and the arp is merely its loudest consumer.
  Launch Quantize deliberately stayed behind on the arp bar, which is the same distinction
  read the other way. It never hides: like arp On, it is a parameter you reach for while
  playing, and the arp reads it with the Controls section folded shut.
  **`KeysEditor::BpmField` is a `juce::Slider` subclass that overrides `paint`.** A Slider so
  the APVTS `SliderAttachment` still drives it; `paint` overridden rather than a style chosen,
  because every built-in style draws a track, a bar or a knob and Ableton's tempo field is
  *only* the number - overriding paint means the LookAndFeel is never consulted (Slider::paint
  is what calls it) while every drag and gesture behaviour is inherited untouched. Vertical
  drag, `setMouseDragSensitivity` tuned to about a BPM per 4 px. **The `<` `>` pair beside it
  is not optional**: a drag is a drag, and the mouse-only contract wants a click-only path to
  every value - this is the part of "just a number" Keys cannot copy from Ableton, which
  expects a keyboard for that field. Row B of the Controls band kept the 170 px the old
  slider held - for one build: two passes later Humanize and Velocity left that row too, and
  Size and Octave left Row A, which is what collapsed the band to the single row it is now (see
  **Size, Octave and Humanize leave the Controls band**, below).
- **The keyboard's own settings ride the Controls bar too** (2026-08-02, sixth pass, Owen:
  "add the scale, root and scale lock, voices and MIDI channel into the controls header").
  Root, Scale, Scale Lock, Voices and MIDI Ch left the band for the bar beside the tempo, so
  the band's first row is Size and Octave alone - **true for one build only**: the very next
  pass moved both of those off the band too, to the *Keyboard* bar rather than this one (see
  **Size, Octave and Humanize leave the Controls band**, below). Same never-hides rule as the
  tempo, and for a sharper reason: folding the settings band away is exactly when you still
  want to change key.
  **The group has two sizes and measures which one it can afford.** They did not fit at the
  editor's floor, and Owen's call was "I think we can resize the elements down" rather than a
  wider window - so `roomy` captions Root, Voices and CH (because "C", "Off" and "1" say
  nothing alone) while `tight` dropped every caption, and `resized()` picks by comparing the
  bar's actual width against the roomy total - **true for this pass only**: the Tempo Sync
  bullet below moves Voices and CH into `tight` too, so only Root's caption is left to drop.
  That test is not decoration: the **update
  button** claims 170 px of this same bar the day a release lands, and without it that day
  would starve the last combo to zero width - the 2026-08-02 Shape trap, which had no visible
  symptom. Scale and Lock never get a caption in either set: "Major" and "Lock" are their own.
  Two labels shrank to fit and both kept their real name where it matters - Scale Lock reads
  **"Lock"** but its accessible name is still `Scale Lock`, and MIDI Ch reads **"CH"** with the
  whole phrase in the tooltip. **Measure, do not assume**: `Voices` was built at 52 px and drew
  `"..."`, because "Off" plus a chevron is wider than the digits either side of it in the list,
  and a `juce::ComboBox` ellipsises rather than complaining.
- **`bpmSync` is the escape hatch, not the feature** (2026-08-02, Owen: "BPM and Off and one in
  the controls header needs labels. and we need BPM sync toggle to sync with DAW"). Keys already
  followed the host's tempo whenever the transport rolled and reported a valid bpm - the new
  `AudioParameterBool` ("Tempo Sync", appended last, default on) is not what adds that, it is
  the ability to turn it off and keep the arp on Keys' own tempo while the DAW plays. It is
  threaded through the two places that used to read `clock.playing && clock.bpm > 0` outright:
  `ArpEngine::Params::followHost` in `ArpEngine.h`, and `KeysProcessor::advanceChainClock`. The
  Hz free-rate path is deliberately deaf to it, the same as it always was to `fallbackBpm` and
  the host's own bpm: a subdivision of a beat means nothing where there is no beat.
  `migrateBpmSync` backfills an older session's absent parameter to the default, the
  `migrateRateMode` / `migrateVelTrim` shape.
  **`clock.playing` came back out of that test on 2026-08-16** (Owen: "bpm isn't syncing with
  daw"). A DAW's tempo is its tempo whether or not the transport is rolling - Ableton shows 120
  with everything stopped - so gating the *tempo* on `playing` meant Keys sat on its own number
  for exactly as long as you were setting up, which is when you look at it and notice it
  disagrees. Both `followHost` and `advanceChainClock` follow a host tempo now whenever the host
  reports one, rolling or not; the *position* each of them reads alongside it (`ppq`) keeps its
  own `playing` test, since a position genuinely means nothing while the transport is stopped.
  `HostClock` gained its own `hasBpm` flag for this rather than a `bpm > 0` test: `HostClock::bpm`
  defaults to **120, not 0**, so `> 0` would have read that default as a real host answer, and in
  the standalone - which has no playhead to ask at all - that would have made Keys quietly stop
  listening to its own Tempo field.
  A **Sync** chip beside the tempo field (accessible name `Tempo sync`) is the on-screen switch.
  While it is on and a host tempo is actually live this block (`KeysProcessor::hostTempoLive()`,
  published next to `arpBeatsBpm`), the field shows the host's own number and its drag and its
  `<` `>` steppers grey out - none of the three can change anything while the host owns the
  tempo. `BpmField::paint` dims itself explicitly for this, since overriding `paint` (see the
  tempo bullet above) means the LookAndFeel's own disabled dim never reaches it either. In the
  standalone there is no host transport, so Sync being lit changes nothing there.
  **BPM, Voices and CH are honest captions now, not bare controls** - the gap Owen was pointing
  at ("BPM and Off and one ... needs labels"). Voices and CH move into the `tight` cell set too,
  so their captions survive the narrow case that used to drop every caption there (see the
  sixth-pass bullet above); Root's stays roomy-only, and is the first thing to give way when the
  update button claims its 170 px.
  **The floor moves to 1280, up from 1070.** BPM's label, the Sync chip and the two newly
  captioned tight cells cost the Controls bar 186 px more than the 87 px of slack it had at the
  old floor, and CLAUDE.md's own rule is that a shortfall must not be paid by a starved control -
  so the floor rises instead. `KeysEditor::minWidthForView()` carries the arithmetic in full in
  its own comment. The standing lesson, worth repeating: when a bar outgrows its floor, raise
  the floor.
  `ArpTests.cpp` adds four cases pinning the tempo-source matrix (166 total, all passing). All
  four force `anchored = false`: the anchored branch reads `clock.ppq` straight off the playhead
  for step position and never touches bpm at all, so testing `followHost` through it would prove
  nothing about which bpm actually fed the step period.
- **A settings gear on the Controls bar, plugin-level like the swatch** (2026-08-17, Owen: "we
  need a settings icon and menu. populate menu"). Sits immediately left of the theme swatch,
  drawn as vector paint rather than an asset or an emoji (the same self-drawn-chrome rule
  `SectionBar`'s fold chevron follows), and never hides with the Controls fold for the same
  reason Tempo and Root don't. **The floor moves again, 1280 -> 1320**, to reserve the button
  plus the gap it puts between itself and Theme, ahead of the elastic Instrument chip, the same
  "raise the floor rather than starve a control" rule the bpmSync entry above already states.
  Three groups: the three ticks, **Reset all settings...** alone (2026-08-27 - see the round at
  the top of this section), then the links. **Hold visuals during sustain** (default on, and exactly
  today's behaviour - `PianoKeyboard::paint` reads `LayoutState::holdVisualsOnSustain`, and off
  makes a key held only by the pedal rest visually while it keeps sounding, paint only, nothing
  about what is actually heard); **Sustained drag leaves a trail** (default on, also today's
  behaviour - see the field's own note below on why the name is not Octavium's); **Sustained
  notes propose chords** (default **off** - see below); **Check for updates**, an explicit
  re-check that reuses `updaterConfig` and reports found / up to date / failed, which the
  existing once-per-process `okstudio::updater::checkAsync` cannot do on its own, so the kit
  gained `checkNowAsync` for it; and **User guide** / **About**, straight to
  `docs/CONTROLS.md` and a small dialog reading the product name and version live.
  **"Drag while sustain" did not survive contact with Keys' own drag** (Owen asked for
  Octavium's item by that name). Octavium's version decides whether a click-drag glides across
  the keys *at all*; Keys' drag has always glided, unconditionally, on every build, so a switch
  by that name would either do nothing or take gliding away, and the label promises neither.
  What is genuinely left to choose is whether a sustained glide piles up behind you or stays
  monophonic, so the item is named **Sustained drag leaves a trail** and gates exactly that
  branch of `NoteSurface::mouseDrag`. The field underneath keeps the name `dragWhileSustain`.
  **UI scale was built and removed before shipping** (Octavium's Zoom submenu, ported as eight
  presets). It ticked a percentage and nothing on screen moved - the editor does not resize or
  transform itself to match it yet - and a control that does not answer is worse than one that
  is not there at all, so it did not go on the menu. `LayoutState::uiScalePercent` is still in
  the struct, persisted so a choice would survive a reopen if the field returns, but nothing
  reads it back into the window today; do not describe it as shipping.
  **"Sustained notes propose chords" is off by default, and that default is the point**
  (Owen: "sustain shouldn't propose chords ... should be a menu option"). Keys is played with
  one mouse, so a chord is built one click at a time, and there are two ways to make a click
  stick: Latch and Sustain. Reading them the same way for "what chord is the keybed proposing"
  (the live card, an "Edit on keyboard" pad) meant the pedal's own passing notes kept rewriting
  the card and any pad being edited underneath your hand. **Latch builds a chord, Sustain plays
  one** - splitting them gives each a job, and the menu item is there for anyone who wants the
  old reading back. `NoteSurface::proposedChordNotes(sustainProposesChords)` is the one function
  both the live card and the keyboard-edit link now read instead of `soundingOutputNotes()`.
- **The arp bar's A / B become the line switch; Details opens the deep view** (2026-08-02,
  seventh pass, Owen: "the A and B on the left side of the header, I want those to be on and
  off buttons to turn on or off the ARP, and if it's turned off, gray it out below. And then we
  can remove the a and b check mark on the right side of the header and in the arpeggio window
  themselves. Maybe we can add another button on the bottom by anchor, like details, and that
  can open up the detailed arpeggiator view"). Four changes from one ask:
  1. **A and B stop navigating and start switching.** They used to be a pure tab, selecting
     which line the panel edited (accessible name `Arp line A tab`), with a separate lettered
     On chip doing the actual switching a few pixels away near Hold off. The chip
     (`arpOnButtons`/`arpOnAtts`) is deleted; A and B are bound straight to that line's `arpOn`
     / `arp2On` through an ordinary `ButtonAttachment` instead, the accessible name drops the
     " tab" suffix (there is nothing left to collide with), and there is no `onClick` left on
     them at all. Being a power switch rather than a navigation control changes what folding
     means for them too: they never hide with the section fold any more, the same "reach for
     it while playing" case Hold off and Quantize already made for staying on a folded bar.
     They remain a `DragAndDropTarget` apiece.
  2. **The macro card's own On toggle (`MacroRow::onButton`, "Macro line A" / "Macro line B")
     is deleted outright**, for the same reason: two switches bound to the same parameter, one
     of them buried in a card, was a control to get wrong twice. Its 40 px on the card's top
     row is not replaced with a spacer - everything shifts left and Shape, the tightest control
     on that row, gets the width instead.
  3. **An off line scrims its whole card instead of losing a control.**
     `MacroRow::paintOverChildren` fills the card body - not its `LINE A` / `LINE B` caption
     strip, which stays legible - with `skin::bgBot` at 0.38 alpha, skipped while the card is a
     drop target so a drop highlight is never muddied by it. Nothing on the card is ever
     `setEnabled(false)`: every knob, the rate dial and the card itself as a drop target stay
     fully live while greyed, both so a rate can be dialled in before switching the line on and
     because a chord dropped onto an off line has to land ("A line that is off still takes
     chords in", above - a disabled component takes no mouse events, which would have broken
     exactly that). The scrim's own cache (`lastLineOn`) is compared in `refresh()`, driven by
     the panel's 10 Hz timer while the macro view is up, so a repaint is asked for only on an
     actual change.
  4. **Each macro card gains a Details button** beside Anchor in its bottom sub-row (accessible
     name `Macro details A` / `Macro details B`), calling the same `setEditLine` a tab click
     used to call - the only way left from a macro card to that line's full detailed view, now
     that A and B navigate nothing. The per-line panel itself paints a small `LINE A` / `LINE B`
     caption in its own top margin (`ArpPanel::paint`, only outside the macro view, costing no
     extra height), because nothing on the arp bar names the edited line any more.
- **Size, Octave and Humanize leave the Controls band; the Knobs chip is gone** (2026-08-02,
  eighth pass, Owen: "I think we can remove the octave setting and the size can go down to the
  header of the keyboard button and remove the knobs button and make the knobs visible when you
  open controls," then, on Humanize: "I'm not sure what to do about the humanize section in
  controls. Maybe that could go in the pad header," and, choosing that, "make smaller to fit").
  **Size and Octave** move to the *Keyboard* bar, which never hides with that section for the
  same reason Root/Scale/CH never hide with Controls: Octave is the keybed's only pitch-range
  control (25 keys cannot pan; the keybed is C3..C5 by construction), and folding the band away
  is exactly when you still want it. Octave is not the band's `IncDecButtons` slider on the
  bar - a bar control is 24 px tall, and those arrows would stack to 12 px each, under the
  mouse-only floor - it is the BPM field's own shape instead: `octPrevButton` / `octaveReadout`
  / `octNextButton`, a `<` value `>` trio driven by the new `nudgeOctave()` (the same shape as
  `nudgeBpm`), reading a signed "+2" / "0" / "-3". **Humanize** and its velocity range move to
  the *Pads* bar instead, Owen's own pick once asked, after the pad page buttons and before the
  generator cluster so that cluster stays intact on the right; the on-bar label drops
  "VELOCITY" to fit a 36 px cell beside a 24 px toggle, and the slider's own tooltip keeps the
  full word. With both gone, the Controls band is down to **one row** (Strum and its
  direction): `headerH` drops from 112 to 52 px, and `sectionHeight(secControls)` and the
  editor's default `idealHeight()` both drop by 60. **The Knobs chip that folded the CC knob
  row is deleted**, not merely hidden: the row is unconditional now, whenever Controls itself
  is open, and `sectionHeight(secControls)` no longer branches on `layout.knobs`. That field
  stays in `LayoutState` only so a session's tree keeps round-tripping cleanly -
  `layoutFromTree` forces it `true` on every load regardless of what was saved, since there is
  no control left on screen that could turn the row back off. The detached Keyboard window's
  own second Size combo (`detachedSizeBox`, accessible name "Keybed size") - built only because
  Size used to live in the section you would fold away before detaching the keyboard - is
  deleted along with `Section::Traveller::detachedOnly`, which had no other user: Size now
  lives on the Keyboard bar directly and simply travels with it, the way Wheels always has.
- **An Instrument chip on the Controls bar, for a host that wants one** (2026-08-02, Owen: "the
  load instrument section with all that should go in the controls submenu"). `KeysEditor`
  grows its first-ever extension point for something embedding it: `onBuildInstrumentMenu`
  (fills a `juce::PopupMenu`), `instrumentName` (supplies the chip's caption) and
  `refreshInstrumentChip()` (call after a load or an eject so the caption catches up) - the
  same functional shape `ChordPads::onExtraMenuItems` already used internally, now exposed
  outward. Plain Keys (the VST3, the plain Standalone) never sets these, so `instrumentChip`
  stays invisible there (`addChildComponent`, not `addAndMakeVisible`) and their Controls bar
  is otherwise unchanged. It takes the cell the Knobs chip vacated and is the one *elastic*
  control on the bar: `resized()` measures the tempo group and the Root…MIDI Ch group first
  (both fixed-width), and the chip gets whatever is left over, clamped to `[80, 150]` px -
  reserve the fixed-size controls first, always, or the elastic one starves its neighbours
  instead of the other way round.
  **Keys Host's own 44 px top bar is deleted along with it** (`loadButton`, `instLabel`,
  `showHideButton`, `ejectButton`, the `barHeight` constant and the gradient `paint()` that
  went with them): `KeysHostEditor::resized()` is now just one line,
  `keysEditor.setBounds(getLocalBounds())`, and Load/Show-Hide/Eject move into a popup menu
  off the new chip instead. Every window-height calculation that used to add `barHeight` on top
  of `KeysEditor::idealHeight()` - `maxWindowHeight()`, `fitToKeysHeight()`, the constructor's
  initial `setResizeLimits` - drops it, since there is no bar left to account for.
  `KeysHostEditor::updateBar()` is renamed `refreshInstrumentUi()` to match: there is no bar
  left to update, only the chip's caption.
- **Launch Quantize is Ableton's transport Quantization, for the arp** (`arpQuantize`, 2026-08-01,
  the setting Owen described and could not name: "if you start a new note or something that goes
  into the next sequence, so it sounds good always"). Off - the default, and what Keys always did
  - fires a chord the instant you click. Anything else holds the *gesture* until the next boundary
  and then performs it whole, so a slot's pattern, shape, rate and chord land together on the grid.
  Global rather than per line, because the value of it is that the lines land together.
  **It never delays the keybed**: playing a note is playing an instrument. The public
  `holdArpChord` / `holdArpChordFromPad` / `launchArpSlot` defer; the `*Now` twins beside them are
  the gesture with the wait already served, and are what the chain calls (it is on a bar line by
  construction) and what a slot launch calls for its own chord (it has already waited). The
  deadline is wall clock, computed from `arpBeats` - the beat position the audio thread publishes
  each block, the host's own while it rolls and an internal count otherwise - and waited out on
  the **1 ms strum timer**, not the 50 Hz heartbeat: 20 ms is a sixth of a 1/16 at 120 bpm, which
  is the sloppiness the feature exists to remove. A panic and Hold off both clear what is pending.
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
  **A sequence of chords already reaches a line three ways, and the third one surprises people**
  (2026-08-21, Owen: *"for arpeggiators if I wanted to feed in a sequence of chords for it to
  play. How would we do that?"*). Chain walks the slots at **bar** resolution; the Chord lane
  swaps a slot's chord in for a single **step**; and a line with **Play** on **plus Track MIDI on**
  arpeggiates a clip on the track (`runArpLines`), so a progression drawn into an Ableton clip
  drives the arp with the **host** owning the durations. **Track MIDI is the part of that
  sentence which is new and off by default** (2026-08-27, see the round at the top of this
  section): until then Play alone did it, which is exactly why this route "surprises people" -
  it surprised six instances into arpeggiating at once. Nothing was written for that third one -
  it falls out of the routing - and it is the only route that can hold a chord for two bars and
  the next for two beats, which neither of the other two can express. What is genuinely missing
  is a **loading gesture**: filling twelve slots is one drag per chord, and `ChordLibrary.h`'s
  355 progressions know their own chord order but can only reach the tray and the pads.
  `docs/CHORD_SEQUENCE.md` is the survey (Scaler, Cthulhu, Kirnu's interval "stamp", Ripchord,
  Hapax) and five options, **proposed and unbuilt** - do not describe any of it as shipping, and
  read it before designing a fourth mechanism, since the recommendation is that Chain already is
  the third.
  **The rate is a dial with two clocks** (2026-07-30). `arpRate` is untouched - the same eleven
  divisions, the same order, the same default - and a Sync / Hz switch beside the dial adds
  `arpRateFree` and `arpRateHz`, 0.03125 to 32 Hz mapped exponentially, which is exactly what
  those divisions span at 120 bpm. In Hz the engine pins its clock to 60 bpm, so a step is
  simply the period, the playhead is not read at all, and Dot, Tuplet and Anchor grey out: a
  subdivision of a beat means nothing where there is no beat. The `<` `>` steppers beside the
  dial are load-bearing rather than a convenience, since a dial is a *drag* target and they are
  the click-only path to every value it can hold in either mode. A slot stores the mode and the
  Hz value alongside the division, and `migrateRateMode()` puts a session saved before any of
  this back into Sync - an absent parameter keeps the live instance's current value rather than
  resetting it, so without that a Hz session would reopen in Sync at whatever division it
  happened to hold, silently.
- **The arp bar carries All Off and Light keys** (2026-08-02, Owen's ask). **All Off**
  (`allArpOff()`) switches every line off *and* releases every hold, chain and pending launch.
  Switching off is the load-bearing half: release without it and the engines pick straight back
  up on whatever the keybed holds, so it would silence the room for a sixteenth note. It is
  always enabled, unlike Hold off, because a stop button you have to read before trusting is
  one you cannot reach for in the moment. **Light keys** (`layout.arpLights`) lights the keybed
  for the notes the arp is *playing*, each line in its own colour since 2026-08-22.
  `arpNoteLines` is a bitmask per pitch, one bit per line, written on the audio thread off each
  line's `out` buffer - never off the merged stream, where the arp's notes are indistinguishable
  from the pass-through, and never off `in`, which `noteRefs` already lights.
  **`keybedLit()` is the keybed's own question and not `isNoteSounding()`**: that answer feeds
  the live chord card too, and an arpeggio is a run of single notes, so folding the arp into it
  would rewrite the "current chord" as whichever note the arp is on. Only `NoteSurface` asks
  `keybedLit`. It also **withholds the chord handed to a running line** while Light keys is on,
  which is the difference between the option working and not: that chord is the run's *input*,
  so lighting it holds down every pitch the arp is chewing and the arpeggio inside it is
  invisible. Owen's report on the first cut was "it just shows the chords that are being
  played". Hiding the input is what makes the output visible.
- **A window opens at its content's height, and its resize floor tracks it** (2026-08-02, Owen:
  "when we open the window, the keyboard is cut off on the bottom. We should add some fail safes
  so that doesn't happen"). `KeysEditor::applyLayout` has done this since it grew folds - "the
  content's own size *is* the minimum" - but **Keys Host did not**: it opened at a literal
  `barHeight + 620` and floored itself at a separate literal, so the window could sit shorter
  than what was in it. The keybed is the **last section laid out**, so every pixel short comes
  off the bottom of it with nothing on screen to say so; making the arp's macro view the default
  is merely what pushed it over. `KeysHostEditor::fitToKeysHeight` is now the one answer for both
  the initial size and every fold, and the ceiling is **measured** off the display's work area
  (`maxWindowHeight`) rather than the literal 1700 it was, since a window sized to its content on
  a screen shorter than that content is the same bug by another route. `KeysEditor::idealHeight`
  went public for it, next to `minWidthForView`, which was made public in 2026-07-30 for exactly
  the same reason: a host that embeds the editor must *ask* it for its size and never copy it.
  `barHeight` itself is gone as of 2026-08-02 too, along with the bar it measured - see
  **An Instrument chip on the Controls bar**, above - so every one of these calculations is
  `KeysEditor::idealHeight()` alone now, with nothing added on top of it.
- **A line that is off still takes chords in; `enabled` gates only firing** (2026-08-02, Owen:
  "when you drag your chord onto an arp, I don't want it to play the chord sound when you
  release" and "when you turn on the arp, it should start playing whatever card is loaded").
  `runArpLines` **has no bypass branch any more**. It used to merge a disabled line's `in`
  straight to the output, so a dropped chord sustained like a pad *and* the engine never saw it
  - one cause, both symptoms. `ArpEngine::process` consumes note-ons in `noteArrived` outside
  the `p.enabled` gate, so passing `ap.enabled = arpOn` and always running it makes a disabled
  line a silent holder. The off→on edge calls **`restart()`, not `hardReset()`** - the new one
  is hardReset minus the held set, and hardReset there was what threw the waiting chord away.
  The keybed is unaffected: `listens[n]` is false with the line off, so notes you play are never
  lifted out of the stream. Playing the instrument is never gated on an arp switch.
- **`arpOctShift` and `arpVolume`, appended 2026-08-02.** (Vel became `arpVelLevel`, MIDI
  velocity, on 2026-08-18 - see the round at the top of this section.) OctShift **is not Octaves**: it
  transposes the whole run and is centred at 0, while `arpOctaves` beside it *stacks* copies
  upward and can only widen. "How high does it sit" and "how far does it reach" are different
  questions and only the first has a middle - the macro row's OCT knob drives the new one, and
  Octaves stays on the per-line tab with Distance, the other half of the same feature. Volume is
  a plain per-line output level, folded into `velScale` beside the ramp. The macro row's VOL
  replaced **both** Ramp and Time, which are one feature between them. **Volume lasted a day on
  screen**: the second 2026-08-02 pass replaced the knob with the bipolar `arpVelTrim` (see the
  All-view bullet above). The parameter stays registered and the engine still multiplies it,
  but nothing in the UI writes it and `migrateVelTrim` folds it back to 100 on every load.
- **PLAY and Light keys are not the same word twice** (2026-08-02). `arpKeys` routes the keybed
  *into* a line; `layout.arpLights` only decides whether the keybed lights *up*. They were
  labelled KEYS and "Show notes" and read as one idea - Owen asked what the difference was. Ids
  unchanged, labels renamed: a label names what the control touches.
- **Twelve pads a page, with Strum and Humanize in the columns that freed up** (2026-08-03,
  Owen: "reduce the pads grid to 12 and move strum and humanize into that with the same
  style"). The strip is two rows of six; the two columns it gave up carry Strum and Humanize
  as `RangeKnob`s. Both were already ranges (a two-handle `RangeSlider` each) and both shape
  what a chord *pad* does - the Controls band and the Pads bar were only where they fitted.
  **This drops pads and is not reversible.** 12 x 4 pages is 48 slots where it was 64.
  `chordPadsFromTree` re-bases a saved session's slots into the current page width, and that
  code was written for 8 -> 16, where every old position still had a home. **Narrowing has
  none**, and the old formula wrapped positions 12-15 onto the *front of the next page*, where
  they silently overwrote that page's own pads as the loop went on. A pad past the end of its
  page is dropped now: each page keeps its first twelve. Owen's call, asked before it was built.
  **The lamp is the switch as well as the handle** (same day: "clicking the blue satellite
  button should turn on or off the feature. And then I don't think we need the humanized check
  mark anymore"). A click toggles, a drag sets the range, four pixels of slop tell them apart -
  a click on a mouse-only surface is allowed to be untidy. `RangeKnob::isOn` / `setOn` are
  **opt-in**: set both and the lamp switches, leave them null and it is a handle only, which is
  what the arp's two want, since there "off" is just the knob at zero. Humanize's tick box is
  gone; Strum needs no on/off parameter because a strum of zero *is* off, so its lamp parks the
  range at zero and puts back what it was (`lastStrumMax`, a UI convenience, deliberately not
  stored - a session saved off opens off).
  **Switched off, the knob stops looking like a range**: the arc goes back to an ordinary one
  and the readout drops to a single number (Owen: "when humanize is off, there's still a range
  appearance"). An unlit lamp over a range arc is the control saying two things at once, and
  the arc is the louder. What that single number *means* is `textWhenOff`, the consumer's -
  **Humanize off plays the band's midpoint, not the knob's value**, so that is what it says.
  Strum's Dir is a `<` `>` pair beside the caption, and the caption reads the live direction
  (`STRUM RAND`) so no third control has to. They **wrap**, unlike the arp's steppers: three
  values with no scale to them are a ring, not a ladder, and stopping would leave one of the
  three reachable from one side only.
  **Budget the knob, do not give it the remainder.** The strip is only ~100 px tall, and handing
  the column the arp *card's* numbers (12 caption, 34 control, 15 readout, 8 inset) left a 26 px
  face floating in an empty column. The face is sized backwards from the target instead.
  With Strum gone the **Controls band has no rows left**, so that section is its CC knob row
  alone. Left behind and worth tidying: `humanizeButton` and `chordStrumDirBox` still exist
  with their attachments, simply parented to nothing.
- **`RangeKnob` is a rotary with two ends, and Humanize uses it** (2026-08-03, Owen: "a serum
  style knob where you can set a range in the knob. In serum they have like a little light next
  to it that sets the range ... I think this is gonna be a reusable component"). The face sets
  one end, the span reaches back from it, and **the range travels with the face** - turn the
  knob and both ends move together, which is the half Owen asked for by name ("when the outer
  ring is enabled, moving the dial moves the outer ring with it").
  **The knob's own arc is the range; there is no second ring** (Owen, same day: "it looks like
  there's two rings around the knob ... just have the inner ring have the features. Everything
  should be reflected on that single ring"). A concentric ring outside the face was built first
  and was one ring too many. What replaced it is **one line in the skin**:
  `KeysLookAndFeel::drawRotarySlider` already works out where a lit arc *starts* (zero, or a
  bipolar knob's centre), so a slider can now override that proportion through the
  `skin::arcFromProperty` component property and `RangeKnob` sets it to the range's bottom.
  Nothing is subclassed; no copy of the knob's look has to be kept in step.
  **Painting over the arc does not work, and that is worth knowing before trying it again.**
  Keys draws a value arc as **three strokes** - a halo at 2.1x the line width, a body at 1.15,
  a hot core at 0.55 - so a `paintOverChildren` mask sized to the line leaves the halo's edges
  showing all the way round the sweep. Owen's report was "a shadow of blue on the inner ring
  that isn't just the range", and it is why the property exists. The first attempt also took
  its geometry from the *kit's* `drawRotarySlider` (bounds reduced by 4, line a fifth of the
  radius) when Keys' own overrides it with different numbers entirely - a second reason a mask
  is the wrong tool here: it has to know how something else draws.
  When the override is set, the bipolar detent tick still marks **real zero** (`zeroAngle`),
  not the overridden origin: they were one variable, and sharing it would have moved the tick.
  **`RangeKnob::faceArc()` must use Keys' rotary geometry, not the kit's.** The two differ (the
  kit reduces the bounds by 4 and takes a fifth of the radius; Keys takes 3 off the radius and
  a seventh of it), a Keys editor installs Keys', and copying the kit's put the satellite
  *inside* the arc - "it's a little bit too close to the knob". What anything outside has to
  clear is the **halo pass at 2.1x the line width**, not the line: that is `FaceArc::outer`,
  and the lamp sits a further lamp-and-a-half beyond it, because touching the edge still reads
  as touching.
  **Built from the manual, which corrected the guess.** A first cut read the screenshot as a
  dot sitting on the ring; `Serum 2 User Guide.pdf` p195 says otherwise: *"A smaller blue halo
  appears to the top left of the knob ... Click and drag the arrow control to change the
  modulation depth amount. As you drag the arrow, notice how the halo shrinks or expands."* So
  the grab is a **satellite at the top left, dragged vertically** - and that is what makes it
  buildable here, because a satellite is a component of its own and can be sized to a real
  target instead of Serum's few pixels, in the corner a circle leaves empty in a rectangle. It
  is a child **above the face in z-order**: a Slider eats every press inside its rectangle,
  corners included, so anything drawn in that corner is dead unless it is a component on top.
  **Two departures, both forced by the contract.** Serum's fallback for the fiddly satellite is
  Option/Alt-click-drag on the knob body; a modifier is not a gesture Keys may require, so the
  fallback here is that **the whole margin around the face drags the span too** - every pixel
  the face does not cover, corners included, fallen onto rather than aimed at. The satellite
  itself is a **plain LED - solid, lit, unchanging**. It carried a miniature arc filling with
  the span for one build (the span drawn twice), then an outline and a pip, which read as a
  tiny knob (Owen: "a dot with a circle around it. Just make it look like a plain LED light").
  The knob's own ring is the one that reads. And there is no negative span:
  Serum flips the halo's hue for an inverted depth, but a range has nothing to invert into, so
  `Direction` picks which side of the value it reaches instead.
  **The component owns no parameter.** The span arrives through `setSpan()` and leaves through
  `onSpanChanged` / `onSpanDragStart` / `onSpanDragEnd`, so the consumer keeps the parameter,
  the gesture brackets and the undo story; `MacroRow` wires those to begin/set/endChangeGesture
  by hand, the shape Shape and the rate steppers already use. It lives in **Keys**
  (`src/ui/RangeKnob.h`) and is written kit-ready; promotion means moving it beside
  `okstudio/RotaryKnob.h` and swapping `skin::` for the theme's tokens, and is a deliberate
  step, not an automatic one.
  **`arpHumanizeSpan` / `arpHumanVelSpan` are the spans**, appended, **default 100**. The draw
  is uniform between `knob - span` and `knob`: span closed is a fixed offset with no randomness
  left, span at 100 puts the floor at zero wherever the knob sits, which is byte-for-byte what
  Humanize did alone. That default is why the migration matters more than most - it is the
  *top* of the range, so an absent parameter inherits something narrower rather than wider.
  **The engine clamps the floor to its own ceiling**, not the parameter layout - either can be
  automated past the other, and `process()` is the only place that sees both at once.
  **`arpHumanVelSpan` is pinned to 100 rather than removed, since 2026-08-17** (Owen, looking at
  the macro card: "I only want one velocity knob, and I want this humanize section to be the
  outer ring"). VEL merged with Humanize Velocity into one `RangeKnob`: the face is still
  `arpVelTrim`, bipolar and unchanged, but the ring is `arpHumanVel` **directly**, not a span of
  VEL's own value the way H.TIME's ring still reads `arpHumanizeSpan`. VEL can sit anywhere from
  -100 to 100 and the ring reaches straight down from wherever that is, which the bipolar case
  `RangeKnob` already handled. **One thing in `RangeKnob.h` did have to change, and it is the
  lesson of the merge**: the span's ceiling *and* its drag sensitivity were both taken from the
  **face's** range, which is correct for every ring that is a span of its own knob (H.TIME: face
  and ring both 0..100) and wrong for the first ring that is not. VEL's face is -100..100 and its
  ring is 0..100, so the drag was calibrated to 200 units against a parameter that stops at 100 -
  the top half of the satellite's travel wrote nothing, and the arc it drew fought `refresh()`,
  which reads the clamped parameter back at 10 Hz. `RangeKnob::setSpanMax` gives the span a
  ceiling of its own, defaulting to the face's travel so nothing that already worked moved; both
  range knobs set it from **their own ring parameter's range**, so H.TIME lands on exactly the
  number the default gave it and the rule lives in one place instead of as a special case on one
  knob. The standing form of it: **a ring that carries a parameter of its own owes that
  parameter's range to the knob**, or half the gesture is inert with nothing on screen to say so.
  `arpHumanVelSpan`, H.VEL's own
  former ring, would otherwise be dead weight on an absent control, so every write the new ring
  makes pins it to 100: the draw was already uniform between the floor and the knob at that
  default, and pinning it is what keeps that true with one fewer number on screen. The lamp is
  new here (H.TIME's ring has never had one): H.TIME's knob at zero already means "no wander",
  but VEL's knob at zero means "as played", so no position of the level could double as
  Humanize Velocity's own off switch - clicking the satellite now toggles it, off parking the
  amount at zero and remembering what it was, the way ChordPads' Strum lamp already works. The
  knob strip is seven now, not eight; `Macro H.VEL A` / `B` and their range and handle names are
  gone, and the ring answers to `Macro VEL range A` / `B`, the satellite to
  `Macro VEL range handle A` / `B` - see the screenshots section.
  **The satellite is outside the ring, not on it**, and it is small - about a quarter of the
  face, Serum's proportion. Two builds got this wrong before the screenshot showed it: the
  first put it at `1.25pi` (JUCE measures a rotary's angles **clockwise from twelve o'clock**,
  so that is the *bottom* left), and the second put it on the ring's own circle at 1.9x the
  ring's thickness, which read as a lump growing out of the dial - Owen: "the satellite should
  not be on the wheel". It is placed toward the dial box's **corner** rather than along a 45
  degree line, because the box is wider than it is tall and the pocket a circle leaves empty
  points at the corner; the distance clears the ring's stroke but is clamped so a narrow column
  never pushes the dot out of its own cell. A hairline stem joins it back to the ring, or it
  reads as an orphan floating between two columns. It is **drawn small and hit large** - the
  component is 8 px bigger than the dot - and the padding only ever overlaps the ring, where a
  press does the same job anyway, so nothing is stolen from anything.
  **The knob row grew 16 px rather than the faces shrinking**: squeezing a ring out of the
  space those two knobs already had would have taken them under the kit's 48 px advice and,
  because the strip is uniform, every other knob with them. Height is the cheap axis in this
  view; the same lesson as the sub-row strip. The two range knobs also **reserve their ring
  width out of the row** rather than taking it off a neighbour, so the face inside one is
  exactly as wide as every plain knob and the row still reads as eight of one size.
  Still ceiling-only: the band's **Human Time** and **Human Vel** on a line's Details view are
  linear sliders in a four-cell group, not knobs, so the floor is a macro-card control for now.
- **Trip became a Tuplet combo, and the rate readout is a fraction of a bar** (2026-08-03,
  Owen: "when triplet mode is enabled the division text should reflect. what if I want 1/5 or
  other division?", then, on the first cut: "confusing UI. it's a check box but it changes.
  fraction confusing too. shouldn't it just be 1/5 not 1/4:5?"). Two problems, one of them the
  other's cause: the dial's readout came from the `arpRate` choice parameter, which knows
  nothing about Dot or Trip, so a dotted triplet 1/8 read "1/8" - and a readout that cannot
  describe a modified rate is a poor place to add more modifiers.
  **`ArpEngine::rateSyncText` is the step length as an exact fraction of a bar**: `1/8`,
  `1/12` in threes, `1/10` in fives, **`1/5`** for a quarter in fives, `1/8.` dotted. This is
  the one notation that survives tuplets, and the reason the first cut had to invent `1/4:5`
  is that the universal DAW convention (`1/16T`, `1/16D`) has a letter for triplets and dotted
  and **no form at all** for a quintuplet. The fraction needs no letters: a quarter-note
  quintuplet is five in the space of four quarters, four fifths of a beat, one fifth of a bar.
  FL Studio's grid ("1/3 beat") is the same system. **Dot stays a dot** rather than folding in
  - a dotted 1/8 is 3/16 of a bar, but `1/8.` is universal and `3/16` has to be worked out.
  Straight, every reading is byte-identical to the division names the parameter already
  carried, which is what stops this being a second, drifting copy of the rate list.
  `installRateText()` runs **after every attachment swap**, because
  `SliderParameterAttachment` writes `textFromValueFunction` in its own constructor and would
  put the bare division back. Hz is untouched - the engine ignores both modifiers there.
  **`arpTuplet` is a choice over Straight / Triplet / 5-tuplet / 7-tuplet / 9-tuplet**,
  appended, and `ArpEngine::tupletFactor` is the whole feature: `tupletSpace(N)/N`, the largest
  power of two at or below N over N. So Triplet is exactly the 2/3 the old `triplet` branch
  hard-coded, and five quintuplet 1/16s fill the span four straight ones do. **Dot is a
  separate axis and stays one**: it lengthens a step by half where a tuplet divides a span, and
  folding them together would mean enumerating the product. The even numbers are absent on
  purpose - 4-in-4 is straight and 6-in-4 is a triplet one division down, so an int 1..9 would
  spend half its travel on rates the dial already has.
  **It is an ordinary combo with an ordinary `ComboBoxAttachment`.** It was a `ToggleButton`
  cycling its own text for one build, which is a control lying about its own shape; a combo is
  what Keys already means by "pick from a list" (Shape, Distance, Retrigger), so it needs no
  explaining, and a button could not have bound a choice parameter at all. The **entries name
  themselves** ("Triplet", not "3") because the macro sub-row is one 34 px strip with no
  caption anywhere on it. `refreshTuplet()` survives the change for one reason only: the
  *readout* is a function of three parameters and an attachment binds it to one.
  `arpTrip` stays registered and is read by nothing; `migrateTuplet` folds a set Trip into
  Triplet and returns Trip to its default - the `migrateVelTrim` retirement, exact rather than
  approximate.
  **Folding the tuplets into the rate list itself is documented and deliberately unbuilt**
  (`docs/ARP_DESIGN.md`): it is the cleanest reading of "it should just say 1/5" and the dial
  would walk 1/4, 1/5, 1/6, 1/8, 1/10 with no second control at all, but the list goes to about
  two dozen entries, and turning 1/4 to 1/64 from four stepper clicks into fifteen is not a
  trade to make on the one click-only path the rate has.
  Fixed on the way past: **`setEditLine` left the rate dial bound to the line you just left**
  whenever both lines were in the same rate mode. `refreshRateMode()` early-outs on an unchanged
  mode and the dial's attachment lives there rather than in `buildAttachments()`, so it was the
  one control that never rebound; `lastRateFree = -1` before the call forces the swap.
- **The Shape combo is capped, and the dice lives in what that freed** (2026-08-21, Owen: "the
  shape of the arpeggiator drop down doesn't need to be so big. Make it smaller. And I use the
  random ones a lot, and I'd like to have a dice button when those are active nearby to
  regenerate their pattern"). Shape was the **one elastic control** on the card's top row, which
  is correct at the editor's floor and absurd above it: on Owen's window it took about 540 px to
  hold "Fingered Bottom", the longest of its fifteen names. `arpMacroShapeMaxW` (170) is a
  **ceiling, not a size**, so above the floor the row simply stops giving the combo more and the
  reserve-the-fixed-thing-first rule is untouched. It does **not** shrink "exactly as before" at
  the narrow end, and the comment said so for an afternoon: the dice's 34 px cell and its 14 px
  gap come out of this same row, so wherever the cap is not biting, Shape is 48 px narrower than
  it used to be. That is affordable only because the panel's floor is set by the knob strip below,
  which is wider than this line needs. The **~166 px at the floor** this used to claim was carried
  over from before the dice and never re-derived - it is nearer 151, and less again at
  `minMacroWidth()`. Still room for the longest name, so nothing was broken, but **a width
  asserted in a comment goes stale silently**, which is this same round's other lesson wearing
  different clothes. `LayoutTests` measures the combo against its own longest entry at both
  floors now. **Narrow the floor and this is still the second thing that breaks, after the
  knobs.**
  **The dice deals this line's Random Once a new order**, and that is the whole feature:
  `ArpEngine::rerollRandomOrder()` sets `permDirty`, and the next step reshuffles. **It leaves
  `dirCursor` alone** - the cursor is the phase of the walk, so zeroing it would jolt the line
  back to the top of its bar as well as changing the order, and only one of those was asked for.
  **It greys outside Random Once**, because Random and Random Other draw fresh every step and
  have no stored order to deal again; a lit button there would promise what it cannot do. Its
  cell is reserved on every shape so the row never reflows, and `MacroRow::refreshDice()` is the
  one place that rule is written - called from `applyShape` (the instant the combo moves), from
  `refresh` (every other route: a host lane, a session load, MCP, the steppers), and from the
  constructor. **It reads the Shape combo's selection, so the combo has to have one**:
  `addItemList` selects nothing and Shape has no attachment to seed it either, so the constructor
  calls `syncShapeBox()` first. Without that the dice opened greyed on a Random Once session and
  came live only on the panel's next 10 Hz tick - a card built from a saved session has to be
  right when it is built, not a hundred milliseconds later.
  **The reroll crosses threads as a counter, not a flag**: `ArpLine::rerollRequest` is bumped on
  the message thread and matched against `rerollSeen` in `runArpLines`, so every write to engine
  state stays on the audio thread and each side writes only its own variable. It does **not**
  mean two clicks in one block are two rerolls - the comparison fires once however far the
  counter jumped, and it is right to, since `rerollRandomOrder()` only sets `permDirty` and a
  second deal before the next step is inaudible by construction. That claim was in the code, the
  changelog and this file, and was wrong in all three (corrected 2026-08-21, in review). **Placed beside the shape group with a 14 px gap**, not pinned to the
  right end of the row: pinned right it sat 250 px from the thing it acts on and read as
  unrelated, and Owen asked for it "nearby". The gap is wider than the 6 px inside the shape
  group, which is what stops it reading as a third stepper.
- **A crowded row grows a strip; it does not squeeze its targets** (2026-08-02, when Dot, Trip -
  now Tuplet - and Anchor joined the macro rows). The main line was already at every floor it has at Owen's
  window width, so two more 34 px targets in it would have driven the eight knobs under the
  mouse-only minimum - the row took a sub-row strip (`arpMacroMods`, 34 px) at the bottom instead, removed
  *before* the main line is laid out so nothing above it moved. Height is the cheap axis in this
  view since line C went; width is the expensive one. Note **PLAY and Light keys are unrelated
  controls with similar names**: `arpKeys` routes the keybed *into* a line, `layout.arpLights`
  only decides whether the keybed lights *up*. Owen asked what the difference was, which is why
  each label now names what it touches.
- **Reserve the fixed-size control first, always** (2026-08-02). `MacroRow::resized` expressed
  Shape's width as a subtraction inside the knobs' `jlimit(52, 96, ...)`, which is not a
  reservation: on Owen's window the knobs hit their floor, the clamp discarded the subtraction,
  and Shape got the ~77 px left over. Two symptoms, one cause - the combo drew "Random Other" as
  "R...", *and* the `>` stepper beside it was starved to zero width, a mouse-only target simply
  not on screen with nothing to see. An elastic control with a floor must never be asked to
  leave room for anything; take the constant-size cell out first and let the elastic one have
  the rest. This is the same family as the 2026-08-01 trap logged above, which is why it is
  worth two entries.
- **Exclusive does not reach across arp lines** (2026-08-02, Owen: "I want each arpeggiator to
  play different chords"). `stopAllChordPads()` releases every chord source including every
  line's hold, and `holdArpChordNow` called it, so handing B a chord took A's away and the
  second drag undid the first. It now passes `includeArpHolds = false`: the pads and the live
  card still give way in both directions, and the line's *own* previous hold still goes
  (`releaseArpChord(line)`, unconditional, just above it). The old reading - one chord at a
  time whichever surface started it - was right while the lines were something you switched
  between and wrong once they are two instruments you feed side by side. Nothing collides:
  each line's chord is fired into that line's own queue (`dest` is line + 1) and `noteRefs` is
  per destination stream. Every other caller of `stopAllChordPads()` still means all of it.
- **A chord card is hold-to-play** (2026-08-16) - **no longer true from 2026-08-18**, when it
  went back to sounding on release; see the round above for why, and for the settings-menu item
  that puts this behaviour back. The rest of the entry still explains what the 2026-08-02 change
  was really fixing, which is why it is kept. (Owen: "when you click a pad cord, it should
  only play it for the amount of time that you're holding it, not a fixed value"). The press
  fires the chord and the release lets it go; Sustain and Latch still decide what "let go"
  means, since `ChordPads::mouseUp` ends the note through the same `releaseChordPad` /
  `releaseLiveChord` the old mouse-up did. A pad is an instrument, and a fixed-length blip was
  a preview of one.
  **This reverses the 2026-08-02 entry above it in git history** ("a card sounds on release,
  never on press"), and the reason that one existed is worth keeping rather than losing: the
  bug it fixed was never the noise a press made, it was that the press branch also handed the
  card to a running arp line *and cleared `dragSource`*, so a card could not be dragged in the
  one mode where dragging it onto a line is the whole point. That branch is gone outright now -
  a click no longer feeds a line at all - so sounding on press costs nothing this time.
  `mouseDrag` calls `endAudition()` before it starts carrying the card, so what a drag still
  costs is a blurt for the roughly six pixels it takes to become a drag rather than a click;
  waiting out that threshold before sounding would put a lag on every note, the worse trade.
  **The generator's audition tray keeps its fixed `auditionMs` (800)** on purpose: a tray card
  is a candidate you are sampling, and a pad is an instrument you are playing, so the two were
  never the same question even while pads used the same timer.
- **The chord pads and the arpeggiator are each a section of their own**, stacked above the
  keyboard, so a chord card is on screen whatever else is open. **There is no centre view**
  (2026-07-30): the arp stopped being one on 2026-07-25, Chords went the day the generator lost
  its panel, Perform went with the Centre section itself, and there are no tabs left to switch.
  **There is exactly one set of chord *pads***: the generator draws no second view of the page,
  because the grid it used to draw was the same pads of the same page as the strip below
  it, written through the same `setChordPad`. Its
  `Big` arrangement became the Pads section's, and went for good on 2026-07-31: every pad now
  carries its chord's notes under the name on the ordinary two-rows-of-eight card, so there is
  no separate size left to switch to.
  **The generator window does draw a 4x4 grid again from 2026-08-01, and it is the other kind**
  (Owen: "I have four by four pad where you can audition new chords. We want to be able to try a
  bunch out"). `ChordTray` holds sixteen *candidates*: chords that belong to no slot, are not in
  the session, and die with the window. That is the line to hold when anything else proposes a
  grid - a view of the page is the removed one under a new name, a tray of uncommitted chords is
  not. It exists because hearing what the generator would produce used to cost a pad, so
  comparing eight chords meant filling the page with seven you did not want. Both gestures on a
  tray card are left-button, so the closed right-click list below is untouched: **click** to
  audition (through `ChordGenMenu::auditionChord`, so the note path and the 800 ms timer stay on
  the brain and neither `ChordGenPanel` nor `ChordTray` ever calls `noteOn`), **drag onto a pad**
  to commit. The drag is the only gesture that can name a slot, which is why it and not a second
  click is the commit. **It crosses two top-level windows and JUCE does that for free**
  (2026-08-02). This entry said the opposite for a day and a half - "no `DragAndDropContainer`
  spans them" - and the whole cross-window protocol was hand-rolled on screen positions passed
  through the editor because of it. `DragAndDropContainer::startDragging` takes a fourth
  parameter, `allowDraggingToOtherJuceWindows`, defaulting to false; pass true and the drag image
  goes on the desktop, which makes `getParentComponent()` null inside JUCE's `findTarget` and
  routes it through `findDesktopComponentBelow` - every desktop component in z-order, walking up
  for an interested target. Same hit test, already written. See `src/ui/ChordDrag.h`, which is
  also where the two things JUCE has *no* opinion about live: `taken` (the veto that stops
  reaching for the reference box from deleting the pad you reached with) and `consumed` (a tray
  candidate committed to a pad empties its cell; the same one copied to the reference does not).
  Both are read a message-loop turn after mouse-up, because a source's own `mouseUp` runs before
  its listeners and so before `itemDropped`, while `dragOperationEnded` waits out a 120 ms
  animation. A drop refuses a **locked** pad and calls `clearChordPad` before
  `setChordPad`, so a target left ringing by Sustain or feeding the arp gives its old notes up
  instead of stranding them; a drop that misses keeps the candidate and does nothing, because
  this gesture is the same shape as the strip's own drag-off-to-clear and must never lose work.
  `generateCandidates()` is the only generator entry point that hands chords back rather than
  placing them.
  **Nothing in the generator window writes a pad** (2026-08-01, Owen: "when you click on
  regenerate unlocked, I don't want it to regenerate the ones in the host window, only in the
  card generator window"). Its three buttons act on the tray - **Fill** the empty cells, **Regen**
  the filled ones, **Clear** the lot - and ride the tray's own header, which is the row that says
  what they belong to. They keep the safe/destructive split they had; what changed is what they
  are destructive *to*, and since a tray card is not in the session and is one drag from a pad,
  none of the three can lose work. **Clear page is gone** with that move: it had one home,
  deliberately, and this window was it. A committed card **leaves its cell empty**, which is both
  the record of what you have taken and the thing that gives Fill a job. The Pads bar still
  carries Fill and Regen for the page, next to the pads they write.
  **A hole is a target** (2026-08-16, Owen: "when you are generating chords and you move one off,
  there's an empty space, and then you can't regenerate it"). Clicking an empty cell generates a
  chord into it and auditions it, so taking a card and getting another one is the same gesture
  twice. Nothing about the hole changed - Regen still means "reroll the cards I kept", which is
  the distinction that earns Fill and Regen separate buttons - what changed is that the way back
  was **invisible and unaimed**: the only route was Fill on the header, which does all of them,
  and the cell painted as an unmarked well with no hover and no mark, so it read as scenery. It
  hovers and carries a `+` now, and `ChordTray::mouseDown` no longer returns before the
  right-click branch, so an empty cell's menu offers the two rows that need no seed (New chord
  here, Fill every empty card) instead of nothing at all.
  **The reference card is the tray's fixed point** (`ChordRefCard`, same files as `ChordTray`;
  Owen: "another box for the reference chord ... so when you regenerate everything, it doesn't
  erase your reference chord"). One chord that no tray action touches, filled by dragging a tray
  card *or* a pad from the main window onto it, with **Similar** and **Could follow** beside it.
  **It is a drag source as well from 2026-08-16** (Owen: "I'm not able to drag the currently held
  chord into the chord pad"). It was drop-only, on the reading that Similar and Could follow were
  route enough to the pads - but those two fill the *tray*, and neither puts the reference chord
  itself anywhere, so a card that visibly held a chord could not give it up. Dragging it off
  commits it to a pad, one difference from a tray card: it **copies**. That is
  `chorddrag::Payload::From::refCard`, a kind of its own rather than a reuse of `trayCell`,
  because the two are identical to every target except on `consumed` - and `consumed` is exactly
  what would have emptied the box the first time you used it, which is the opposite of a fixed
  point. The arp's targets took `padSlot` alone and so refused it until 2026-08-26; they take a
  chord from anywhere now, and copy rather than consume it - see the round at the top of this
  section.
  A pad dropped there is **copied**: dragging a card off the strip normally clears it, so the
  card setting `taken` on the drag payload is what suppresses that clear, and a gesture that
  reached for the reference and deleted a chord instead would be the worst bug in the window.
  **An audition takes the room.** `previewChord` calls `stopAllChordPads()` before it sounds
  anything. This is not optional politeness: Keys emits one note-on per pitch on the 0→1 refcount
  transition, so with Sustain on a ringing pad made an audition of the same chord *completely
  silent* and one that merely overlapped sound like a single note. An audition is a monitor, not
  a performance. Unconditional rather than only-on-collision, because which pitches overlap is
  invisible and a Hear-this button that works or does not depending on that is the same bug
  quieter. The cost, accepted: auditioning stops a deliberately sustained chord and the arp hold. **The generator is a brain plus four surfaces,
  and it owns none of them**: `ChordGenMenu` is a plain value member the editor holds for its
  whole life, and it is reached from (1) four 24 px chips at the right end of the Pads *bar* -
  Fill, Regen, **Generator** and **Library**, the last two opening a window each - plus the
  **Key** combo beside them
  (an APVTS attachment, so bar and window are one state; Mode and Scale Compliance sat there
  too until 2026-08-02, when Owen had them off the bar and left them to the window);
  (2) **its own window** (`ChordGenPanel`, 2026-07-30, Owen's call), which holds every setting,
  the Markov chain controls, and Fill / Regen / **Clear page**; (3) two items on a pad's card
  menu, New chord and Next, through `addPadMenuItems` / `handlePadMenuChoice`; (4) the
  **library's** window (`ChordLibraryPanel`, 2026-08-18), which shares the brain's three library
  picks and reaches back into the generator's tray. **The window is a
  view, never the owner** - it is built when it opens and destroyed when it closes, so the
  per-card menu items cannot come and go with it, which is the exact bug that made
  `ChordGenMenu` a plain member. It is not a `Section`: it never docks, so it has no bar, fold
  or caption, but it reuses `DetachedWindow` and keeps its frame in `LayoutState`
  (`chordGen` / `chordGenBounds`) like every detached section. **The library's window is the
  same shape line for line** (`chordLib` / `chordLibBounds`,
  `KeysEditor::setChordLibWindowOpen`): two windows rather than a mode inside one, because they
  answer different questions and you want both at once - the generator is "make me something",
  the library is "show me what there is", and browsing the second while a trayful sits in the
  first is the workflow the whole feature is for. Its minimum size is derived from
  the layout (`ChordGenPanel::contentSize`), not chosen. **The card menu has a budget and it is
  rows**: it is anchored to a pad near the bottom of a 699 px window at a 34 px item height, so
  it grows *upwards* off the screen, and JUCE answers a too-tall menu by splitting it into
  columns or making it hover-scroll - and a scrolling popup cannot be worked with one mouse. It
  is **17 rows and 3 separators, 629 px** (rows 34, separators 17) since **Clear page** took a
  group of its own at the foot on 2026-08-23. It was 16 rows from 2026-08-19, when `uiArpLines`
  went to four and Send to arp started emitting A, B, C and D - **the row count is a function of
  that constant, so raising it raises this** - and 14 from 2026-08-17, when Copy chord, Paste
  chord and Save chord as MIDI joined the first group (Owen: "need to be able to copy paste
  chords"). Checked against the panel's own arithmetic rather than assumed: the anchor sits under
  the arp section, so a taller arp pushes it *down*, which only helps, and the figures the code
  used to carry (a 240 px macro view, an anchor around y=656) were two rounds of arp work out of
  date - `arpMacroTotalH` is 401 px with the bottom row folded and 690 unfolded, so the anchor is
  160 to 450 px lower than what the old budget assumed. Send to arp A and B had already taken it
  to 11 rows on 2026-08-16; the settings took it to 23 rows and about 820 px for part of
  2026-07-30, which is what the window fixed. Two rows rather than a `Send to arp line` submenu
  costing one: Owen asked for it by name ("say send to ARP a or b"), and a submenu would have
  spent a hover to save 68 px this menu can afford.
  **Nothing generation does overwrites a chord** (Owen, same day): `fillPage()` writes
  only empty pads, a picked suggestion goes to the first empty pad and the row greys when
  there is none, `regeneratePage()` is the destructive one and skips locks, and each button
  greys when it would do nothing. "Empty" means empty, locked or not - one definition,
  `emptyPadsOnPage()`. Clear page is in the window and not a chip because it empties every
  unlocked pad on the page and there is no undo anywhere in Keys. **A card is all playing
  surface**: the lock is shown as a corner dot when set and is not a target anywhere on the
  card (a clickable chip lived there for a few hours on 2026-07-30 and Owen had it removed).
  Octave down/up and Next voicing are menu-only edits to one
  pad's stored chord that go through `rewritePadChord` so a ringing or arp-held card follows
  its notes (hold restored before press, and with Exclusive on only the hold, since both calls
  choke every source); they grey on the card being edited, which the keybed owns. A lock
  protects a chord from *generation*, never from the user. Anything
  that wants to show a chord card should use the pads, not build a second grid. Controls that
  belong to a section but must cost no height ride its bar: it is 34 px that already exists.
  Fill, Regen, Generator, Library and the combo beside them stay live with the Pads section
  folded, for the same reason arp On does - the other route to the generator is a right-click
  on a card, and the cards fold away with the strip. See `docs/ARP_DESIGN.md`.
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
  window. **Detach hides with its section**, and so does every control that would be reaching
  into content that is not on screen: the pad pages, Wheels, and the arp bar's **All**
  tab (which navigates a panel that is not there). The Knobs chip that used to be in this list
  is gone outright since 2026-08-02, not merely hidden: the row it folded is unconditional now,
  so there is nothing left for a chip to hide. The arp bar's A / B used to be in this list too,
  as a second, separate navigation tab beside their own On chip; since 2026-08-02, seventh
  pass, they are that line's On switch and nothing else, so they moved to the "stays" list
  below instead. What stays on a folded bar is what you reach for while playing or generating - the
  arp's A / B switches, All Off, Light keys, Hold off and Launch Quantize; the Controls bar's
  tempo, Root, Scale, Lock, Voices, CH and, in Keys Host, the Instrument chip (2026-08-02: a
  settings band you have folded away is exactly when you still want to change key); the Pads
  bar's Fill / Regen / Generator / Library and its Key combo (Humanize and its velocity range were on
  this list until 2026-08-03, when they became a range knob in the strip itself); the
  Keyboard bar's Size, Octave, Exclusive / Sustain / Latch / All Off - plus the theme swatch,
  which belongs to the plugin rather than to any one section. Open and folded bars are painted
  at different weights on purpose; `captionWidth()` and `paintButton()` must use the one
  `captionFont()`, or the caption ellipsises and the controls beside it shift as a section folds.
- **The knob bank is the bottom row of the Controls section**, not a band of its own:
  `knobRowH` is 110, which gives 60 px knobs. **The Knobs chip that used to fold just that row
  is gone** (2026-08-02, Owen: "remove the knobs button and make the knobs visible when you
  open controls"): the row is unconditional now, whenever Controls itself is open, and
  `sectionHeight(secControls)` no longer branches on it. `layout.knobs` stays in
  `LayoutState` only so the session tree keeps round-tripping - `layoutFromTree` forces it
  `true` on every load regardless of what was saved, since there is no control left on screen
  that could turn the row back off.
- **The live chord card shows every chord source, and that is not `isNoteSounding()`**
  (2026-08-16, Owen: "I'm not able to drag the currently held chord into the chord pad"). It was
  fed from `keyboard.soundingOutputNotes()` plus the MIDI input, and the first of those answers
  only for keys clicked on the **keybed surface** - so a chord fired from a pad or held into an
  arp line lit the keys up (that reads `isNoteSounding`) and left the card reading "hold a
  chord". Two views of one chord disagreeing, and only the card is draggable, so the gesture
  simply had nothing to pick up: an empty card fails `ChordPads::sourceIsDraggable`.
  `KeysProcessor::heldChordNotes()` is the **read half of `stopAllChordPads`**, and the symmetry
  is the definition - what Exclusive can choke as "a chord" is what the card can show as one.
  The pads, the live card's own gesture, and each line's held chord, **on or off**, because a
  line that is off still takes chords in and you must be able to pick that chord back up.
  Deliberately **not** the merged sounding set: that counts the arp's *output*, one note at a
  time, and would rewrite "the current chord" as whichever step the arp is on - the same
  distinction `keybedLit()` draws for the keybed lights. The chord held *into* a line is the
  chord; what the line makes of it is not. Also not the generator's 800 ms audition, which keeps
  its notes in `ChordGenMenu` and is a monitor rather than something you are holding.
  Sustain leaving `chordPadOn` / `liveChordOn` populated is correct here rather than a leak: the
  chord really is still ringing, so the card really should still show it, and that is the
  mouse-only route to capturing a chord you built one click at a time.
  **It answers with one chord, never the union.** It shipped as a union for a few hours and Owen
  caught it immediately ("the currently held chord should disappear when you play a new chord
  pad"): a union names the pile of everything ringing, which is not a chord, cannot be labelled
  and cannot be dragged. `lastChordSource` records the last tag to *fire* - written where a chord
  starts, never where one is released, because a source going quiet does not make an older one
  newer - and `heldChordNotes()` returns that source while it still sounds, then falls back by
  scanning so a pad left ringing by Sustain is not forgotten. **The editor makes the same choice
  one level up**, between that answer and the keybed, on which of the two last *changed*; the
  loser is used anyway when the winner is empty, so letting go of the keys over a sustained pad
  shows the pad rather than blanking the card.
- **A chord pad chokes the other chord pads whatever Exclusive says** (2026-08-16, Owen: "when you
  click a pad it should clear other presses"). `pressChordPad` stopped only the pad being
  re-pressed unless Exclusive was lit, so with Exclusive off - or with Sustain holding them -
  clicking round a page stacked chord on chord. **Exclusive's job got sharper rather than
  smaller**: it now decides whether a pad also chokes the *other* sources, the live card's gesture
  and each line's held chord. That is the honest split, because those are different instruments
  and stacking them is a real thing to want, where stacking two pads was only ever a way to make a
  pile. The strip is a palette you pick from.
- **Exclusive reaches the keybed's own holds too** (2026-08-16, Owen: "sustained or latched notes
  aren't cleared when pad played"). `latched` and `sustained` live in `NoteSurface`, on the
  message thread, which the processor cannot reach - so `stopAllChordPads()` used to choke every
  chord source it owns and leave a pedalled or latched key on the keybed ringing straight through
  a pad's chord. `KeysProcessor::releaseKeybedHolds` is a `std::function<void()>` the editor sets,
  the same "call outward for the one thing the processor doesn't own" shape as
  `onBuildInstrumentMenu`; it must be cleared in the editor's destructor, since it captures the
  editor and the processor outlives it. `pressLiveChord` is the one caller that passes
  `includeKeybed = false`: the chord it is choking *is* what the keybed is holding, so releasing
  the keybed there would unlatch the keys in the same breath as firing them.
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
  **The shim outlives Keys, and until 2026-08-18 it did not** - it connected once and then
  wrote into a dead socket for ever, so every tool call after a rebuild returned *nothing* and
  the client sat on its idle timeout. `run.py` closes and relaunches Keys on every build and the
  server takes a **new port each time**, so this was the normal case, not an edge one. It
  reconnects on demand now and answers every request even when there is nothing to connect to,
  because silence is the one failure a client cannot act on. The code is the kit's
  (`src/McpShimMain.cpp`, pinned by `tests/mcp_shim_reconnect.py`); fix it there, not here. If
  a tool call ever hangs again, read the live port out of `%APPDATA%\OK Studio\mcp` and talk to
  it directly - that is what tells a broken bridge from a broken plugin in one step.
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
  `Macro dot A` / `Macro tuplet A` / `Macro anchor A` / `Macro legato A` (the last from
  2026-09-01; `Macro trip A` until 2026-08-03, when
  the toggle became the Tuplet **combo**; the band's twin answers to `Arp tuplet`. Being a combo
  it is also reachable by its current text - "Straight", "Triplet", "5-tuplet" - with the usual
  first-match caveat), and `Macro OCT A` / `Macro GATE A` /
  `Macro VEL A` / `Macro H.TIME A` / ... one per knob heading - **ten from 2026-09-01**, when
  `Macro DENSITY A` joined the row between GATE and MUTATE (it is `arpChance`, the Play page's
  slider, under the name it reads as on a card); nine from 2026-08-21, when
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
  with the row controls on 2026-08-02; the per-line band's Play toggle answers to `Arp play`
  (the parameter is still `arpKeys` - the accessible name follows the label here because
  "keys" already means two other things in this window). On the arp bar: `Arp all off`,
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
