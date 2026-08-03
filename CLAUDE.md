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
  **Seven brains, not two** (2026-08-01). `ChordSources.h` adds circle of fifths, Neo-Riemannian
  PLR, progression templates, negative harmony and planing to the weighted pool and
  `ChordMarkov.h`, and **voice leading** as a post-pass over whatever any of them produced
  (`genSmooth`, a percentage in the window's top row, not a source of its own; it changes only
  which octave a note sits in, never which notes). `ChordGenMenu::generateChords()` is the one
  dispatcher for all but Markov, which keeps its three paths because its chords carry a numeral
  ChordGen has no field for and its per-pad regenerate steps the chain from the left neighbour.
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
  **The tick boxes are constraints, not enables** (`genUseKey`, `genUseMode`, `genUseOctave`,
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
- **Two arpeggiator lines, A and B** (2026-08-02, Owen: "I only wanna view two arpeggiators in
  this window, and I wanna be able to drag a chord from below to each one"). Everything in the
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
  the lines with **Keys** on, runs each enabled line into its own buffer, and merges them back;
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
  *Send to arp slot* never had. The slot cards, the tabs and the macro cards are each a
  `juce::DragAndDropTarget` (2026-08-02, see the chord-drag bullet below); JUCE walks *up* from
  whatever is under the point, which is what makes the whole macro row a target including the
  knobs on it. A drop sets the current line and never changes the view
  (`setEditLine(line, false)`): it is routing a chord, not navigating.
  **A fourth tab, All, is the macro view** (2026-08-01, Owen: "the goal is to be able to create
  complex polyrhythms from one view"). It replaces the band and the step editor with one *card*
  per line, side by side under a 34 px header (2026-08-02, Owen: "parallel to each other
  instead of one on top of the other"). A card is three stacked lines - the line switch, a
  detented rate knob with its `<` `>` and Sync/Hz, and the shape with its own steppers; then
  **eight knobs** under their own headings (Oct, Gate, Chance, Swing, Offset, Vel, H.Time,
  H.Vel - Oct is the *transpose*, Vel is the bipolar level, and Humanize is split into its
  halves; all three are the 2026-08-02 entries below); then Dot / Trip / Anchor with the held
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
  `arpVelTrim` is bipolar around "as played" (up boosts, down cuts, -100 mutes exactly as VOL 0
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
  the trim, mute included.
- **VEL is squared, floored last, and its input is clean** (2026-08-02, third pass, Owen: "I
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
  **The rate is a dial with two clocks** (2026-07-30). `arpRate` is untouched - the same eleven
  divisions, the same order, the same default - and a Sync / Hz switch beside the dial adds
  `arpRateFree` and `arpRateHz`, 0.03125 to 32 Hz mapped exponentially, which is exactly what
  those divisions span at 120 bpm. In Hz the engine pins its clock to 60 bpm, so a step is
  simply the period, the playhead is not read at all, and Dot, Trip and Anchor grey out: a
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
  for the notes the arp is *playing*. `arpNoteOn` is a flag per pitch written on the audio
  thread off each line's `out` buffer - never off the merged stream, where the arp's notes are
  indistinguishable from the pass-through, and never off `in`, which `noteRefs` already lights.
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
- **`arpOctShift` and `arpVolume`, appended 2026-08-02.** OctShift **is not Octaves**: it
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
- **A crowded row grows a strip; it does not squeeze its targets** (2026-08-02, when Dot, Trip
  and Anchor joined the macro rows). The main line was already at every floor it has at Owen's
  window width, so two more 34 px targets in it would have driven the eight knobs under the
  mouse-only minimum - the row took a `arpMacroSubRow` strip at the bottom instead, removed
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
- **A chord card sounds on release, never on press** (2026-08-02, Owen: "the chord shouldn't
  play right away when you click it. You should be able to drag it"). `ChordPads::mouseDown` is
  now silent and does nothing but remember where the press landed; `mouseUp` decides which
  gesture it was, in the same order the press used to test in. A click auditions the chord for
  `auditionMs` (800, the generator tray's own length, so hearing a chord is one gesture
  everywhere); a drag makes no sound at all. **The bug this fixes is not the blurt.** With an
  arp line on, the press branch handed the card to that line *and cleared `dragSource`*, so a
  card could not be dragged in the one mode where dragging it onto a line is the whole point.
  Sustain, Latch and Exclusive are untouched: the audition timer calls the same
  `releaseChordPad` the old mouse-up did and they decide what it means. `endAudition()` is
  called first thing on every left click, before any early return, so nothing is ever left
  ringing with no owner.
- **The chord pads and the arpeggiator are each a section of their own**, stacked above the
  keyboard, so a chord card is on screen whatever else is open. **There is no centre view**
  (2026-07-30): the arp stopped being one on 2026-07-25, Chords went the day the generator lost
  its panel, Perform went with the Centre section itself, and there are no tabs left to switch.
  **There is exactly one set of chord *pads***: the generator draws no second view of the page,
  because the grid it used to draw was the same sixteen pads of the same page as the strip below
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
  **The reference card is the tray's fixed point** (`ChordRefCard`, same files as `ChordTray`;
  Owen: "another box for the reference chord ... so when you regenerate everything, it doesn't
  erase your reference chord"). One chord that no tray action touches, filled by dragging a tray
  card *or* a pad from the main window onto it, with **Similar** and **Could follow** beside it.
  A pad dropped there is **copied**: dragging a card off the strip normally clears it, so the
  card setting `taken` on the drag payload is what suppresses that clear, and a gesture that
  reached for the reference and deleted a chord instead would be the worst bug in the window.
  **An audition takes the room.** `previewChord` calls `stopAllChordPads()` before it sounds
  anything. This is not optional politeness: Keys emits one note-on per pitch on the 0→1 refcount
  transition, so with Sustain on a ringing pad made an audition of the same chord *completely
  silent* and one that merely overlapped sound like a single note. An audition is a monitor, not
  a performance. Unconditional rather than only-on-collision, because which pitches overlap is
  invisible and a Hear-this button that works or does not depending on that is the same bug
  quieter. The cost, accepted: auditioning stops a deliberately sustained chord and the arp hold. **The generator is a brain plus three surfaces,
  and it owns none of them**: `ChordGenMenu` is a plain value member the editor holds for its
  whole life, and it is reached from (1) three 24 px chips at the right end of the Pads *bar* -
  Fill, Regen and **Generator**, which opens the window - plus the **Key** combo beside them
  (an APVTS attachment, so bar and window are one state; Mode and Scale Compliance sat there
  too until 2026-08-02, when Owen had them off the bar and left them to the window);
  (2) **its own window** (`ChordGenPanel`, 2026-07-30, Owen's call), which holds every setting,
  the Markov chain controls, and Fill / Regen / **Clear page**; (3) two items on a pad's card
  menu, New chord and Next, through `addPadMenuItems` / `handlePadMenuChoice`. **The window is a
  view, never the owner** - it is built when it opens and destroyed when it closes, so the
  per-card menu items cannot come and go with it, which is the exact bug that made
  `ChordGenMenu` a plain member. It is not a `Section`: it never docks, so it has no bar, fold
  or caption, but it reuses `DetachedWindow` and keeps its frame in `LayoutState`
  (`chordGen` / `chordGenBounds`) like every detached section. Its minimum size is derived from
  the layout (`ChordGenPanel::contentSize`), not chosen. **The card menu has a budget and it is
  rows**: it is anchored to a pad near the bottom of a 699 px window at a 34 px item height, so
  it grows *upwards* off the screen, and JUCE answers a too-tall menu by splitting it into
  columns or making it hover-scroll - and a scrolling popup cannot be worked with one mouse. It
  is **9 rows and 2 separators, 340 px** (rows 34, separators 17); the settings took it to 23
  rows and about 820 px for part of 2026-07-30, which is what the window fixed.
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
  Fill, Regen, Generator and the three combos beside them stay live with the Pads section
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
  bar's Humanize and its velocity range, Fill / Regen / Generator and its Key combo; the
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
  1. *The chord-pad card menu is right-click* (2026-07-22, widened 2026-07-30). Nine rows:
     Edit on keyboard / Clear pad / Lock, the two chord-shaping edits (Octave down/up, Next
     voicing), the generator's two per-card actions (New chord, Next: could follow), and
     **Send to arp slot**. Some of it is an accelerator - Clear pad is also a drag off the
     strip, and Fill and Regen on the bar are New chord in bulk - but the per-card edits are
     reached from this menu and nowhere else, because a card is all playing surface and there
     is nowhere left on it to put a button. Ending an edit is the exception that proves it: the
     tick on the card being edited is a left click. The paths below are the ones Owen ruled on
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
     similar ones or what might come next"). Eight rows: Send to first empty pad, the two seeded
     fills (Fill tray with similar chords / with what could follow), the three shaping edits
     (Octave down/up, Next voicing), New chord here and Clear this card. It earns the exception
     the way the pad card menu did - a tray card is all playing surface, and there is nowhere on
     it for eight buttons. Most of it has a left-click twin: Send to first empty pad is the
     commit drag with the aim taken out, and the two fills are the **Similar** and **Could
     follow** buttons beside the reference card, seeded from the reference instead of from that
     card. **Opening this menu makes no sound.** It auditioned the card for a few minutes on the
     day it was built and came straight back out (Owen: "when you right click, it plays the
     chord. We don't want it to play") - right-clicking to reach Clear made a noise on the way to
     throwing the chord away. Hearing a chord is a left click and nothing else, everywhere.

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

## Skin

`src/ui/KeysLookAndFeel.h` holds every colour and font as a `skin::` token; never reintroduce
per-file hex chrome. One trap, learned the hard way on 2026-08-01: **judge the dim text tokens
against the background at the size they are actually used, not against `skin::text`.**
`textDim` and `textFaint` had been chosen by eye next to `text` and were too dark to read, because
nearly everything wearing them is 9 to 11 px letter-spaced uppercase (the micro-caps captions, the
note list under every chord card) and small letterforms need far more contrast than large ones for
the same effort. Both were lifted; `text` was never the problem.

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
  `PluginEditor.cpp`), plus `Keys Chord Generator`, which is a window but not a section
  (`KeysEditor::setChordGenWindowOpen`). `Keys Centre` and `Keys Transcribe` no longer exist.
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
  one tab left on the arp bar is `Arp all tab`, and the macro
  view's own controls are prefixed `Macro` so they never collide with the bar chips or the tab:
  `Macro rate A`, `Macro rate mode A`, `Macro shape A`,
  `Macro dot A` / `Macro trip A` / `Macro anchor A`, and `Macro OCT A` / `Macro GATE A` /
  `Macro VEL A` / `Macro H.TIME A` / `Macro H.VEL A` / ... one per knob heading, plus
  `Macro details A` / `B` (2026-08-02, seventh pass), the button that opens that line's deep
  view now that A and B on the bar no longer do. **`Macro line A` / `B` no longer exist**: that
  was the macro card's own On toggle, deleted the same pass, replaced by a scrim over the whole
  card rather than a second control. `Macro latch A`, `Macro keys A` and `Macro chain A` went
  with the row controls on 2026-08-02; the per-line band's Play toggle answers to `Arp play`
  (the parameter is still `arpKeys` - the accessible name follows the label here because
  "keys" already means two other things in this window). On the arp bar: `Arp all off` and
  `Arp light keys`.
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
