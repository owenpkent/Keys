# Architecture

Keys is a small JUCE plugin. It makes no sound; it turns mouse gestures on an
on-screen piano into MIDI. Everything shared with the rest of the line comes from
[`../okstudio-juce-kit`](../../okstudio-juce-kit).

## Files

```
src/
├── PluginProcessor.{h,cpp}   # AudioProcessor: params, MIDI output, chord pads, state
├── PluginEditor.{h,cpp}      # folding/detaching sections, layout, updater button
├── NoteMath.h                # pure note resolution (snap + transpose), unit-tested
├── Chords.h                  # pure chord detector (names a note set), unit-tested
├── ScaleModes.h              # 12 modes + a chord quality per degree; emotion lines
├── ChordGen.h                # pure chord generation (weighted pool), unit-tested
├── ChordSuggest.h            # pure "what could follow this chord", unit-tested
├── ChordMarkov.h             # pure Markov progression source, unit-tested
├── MarkovData.h              # the bundled progression corpus ChordMarkov walks
├── ChordSources.h            # circle of fifths, Neo-Riemannian PLR, progression templates,
│                             # negative harmony, planing, voice-leading as a post-pass
│                             # (tests/ChordSourceTests.cpp). Wired to the UI since 2026-08-01
├── ChordLibrary.h            # 355 named progressions tagged by mood, genre and function,
│                             # stored as roman numerals (tests/ChordLibraryTests.cpp, which
│                             # spellchecks the table on every build). The eighth Source, and
│                             # the only one that looks a sequence up rather than computing it
├── ChordNumerals.h           # the roman numeral for a chord, in one place: the numeral the
│                             # cards print in their corner and the Progressions diagram draws.
│                             # A *library* pad asks chordlib::numeralAt instead - `degree` is an
│                             # index into the mode a chord was generated in, and a library row is
│                             # generated against its own
├── ChordVoicing.h            # the three voicing passes (applyMajorMinorBias, fitVoicing,
│                             # applyVoicingPipeline) as free functions, so the ordering
│                             # trap is pinned by a test rather than reproduced in one
├── EuclidGen.h               # Bjorklund, for the Euclid strip's fill of the Chance lane
├── ArpEngine.h               # pure arpeggiator core, unit-tested; the one playhead
│                             # reader in Keys, and only while its rate is in Sync.
│                             # The processor holds *three* of these since 2026-08-01,
│                             # one per line; the file itself never knew how many of it
│                             # there were and did not change (docs/ARP_DESIGN.md)
├── ui/
│   ├── NoteSurface.{h,cpp}   # shared note bookkeeping every playable surface derives
│   ├── PianoKeyboard.{h,cpp} # the piano surface (geometry + paint over NoteSurface);
│   │                         # built by Keys and Keys Host
│   ├── KnobBank.{h,cpp}      # eight assignable CC rotary knobs, the bottom row of the
│   │                         # Controls section
│   ├── CCMenu.h              # the one-click CC picker the knob row uses
│   ├── ChordPads.{h,cpp}     # chord-pad rows + live chord card (capture / recall)
│   ├── ChordGenMenu.{h,cpp}  # the chord generator's brain, all eight sources plus voice
│   │                         # leading, and the audition path (one chord, or a whole
│   │                         # progression walked a chord at a time). Draws nothing; a
│   │                         # member of the editor, so it outlives every view
│   ├── ChordGenPanel.{h,cpp} # a view onto it, the content of a window of its own. Built
│   │                         # when that window opens, destroyed when it closes
│   ├── ChordLibraryPanel.{h,cpp} # the library you browse: twelve rows a page, < >, click a
│   │                         # row to hear the progression, two buttons to place it, a star to
│   │                         # keep it, and Follows for what could come after the pads. Its own
│   │                         # window off the Pads bar; same view-never-owner split
│   ├── SourceViz.{h,cpp}     # read-only diagram of the current source, under its button
│   │                         # row in that window (2026-08-01). Click-through, no state
│   ├── ChordTray.{h,cpp}     # 4x4 grid of candidate chords inside that window (2026-08-01),
│   │                         # plus ChordRefCard, one seed chord the tray's own actions cannot
│   │                         # touch; both belong to no pad and are thrown away when the
│   │                         # window closes
│   ├── ArpPanel.{h,cpp}      # the arp section: Shape gates a tabbed lane editor,
│   │                         # plus the control band and twelve launchable slots. Which
│   │                         # of the four lines it edits is chosen by that line's own
│   │                         # Details button in the macro view since 2026-08-02, second
│   │                         # pass - the letters on the editor's arp bar are each line's
│   │                         # own On switch now, not a way to pick one; the macro view
│   │                         # (all four lines in a 2x2 grid, four lines from 2026-08-19)
│   │                         # is the extra choice on the bar. The card and the grid are
│   │                         # files of their own since 2026-09-02, below - do not look
│   │                         # for MacroRow or LaneGrid in here
│   ├── MacroRow.{h,cpp}      # the macro card and its layout constants (arpMacroModsW and
│   │                         # the rest), lifted out of ArpPanel.cpp
│   ├── LaneGrid.{h,cpp}      # the lane grid, the loop bar and the MUTE strip, lifted out
│   │                         # of ArpPanel.cpp
│   ├── ArpRateMode.h         # the rate dial's Sync/Hz attachment swap, written once for
│   │                         # the two surfaces that turn one (the band and every card)
│   ├── TakePanel.{h,cpp}     # the take before it leaves: a view of KeysProcessor::takeNotes(),
│   │                         # built from the same sequence the written file holds. Never
│   │                         # an editor - editing a take belongs in Lattice
│   ├── SectionBar.h          # the fold/unfold header above a section of the editor
│   ├── RangeSlider.h         # two-value slider whose band drags as one (velocity, strum)
│   ├── StepComboBox.h        # a combo that reports every pick, including one already
│   │                         # showing. Unused since its one caller, the Pads bar's Scale
│   │                         # Compliance box, left for the generator's window alone on
│   │                         # 2026-08-02; kept for whichever future control needs to show
│   │                         # a continuous parameter as coarse steps
│   ├── DetachedWindow.h      # a section popped out into its own resizable window (any
│   │                         # of them since 2026-07-27; also the generator's window)
│   └── KeysLookAndFeel.{h,cpp} # the skin: tokens, raised fills, accent glow
├── host/                     # Keys Host only (docs/KEYS_HOST_DESIGN.md)
│   ├── KeysHostProcessor.{h,cpp} # KeysProcessor + one hosted instrument VST3
│   └── KeysHostEditor.{h,cpp}    # instrument picker and floating instrument window; no
│                                 # bar of its own since 2026-08-02 - see KeysEditor's
│                                 # onBuildInstrumentMenu below
└── mcp/
    └── KeysMcp.{h,cpp}       # MCP tool registrations; every handler runs on the
                              # message thread (docs/MCP.md)
```

## Keys consumes no audio

Every part of Keys produces MIDI and none of it reads any. There was a **Transcribe**
section for a while, built on the kit's basic-pitch engine, and it was removed on
2026-07-30: `TranscribePanel`, `AudioCapture`, the `KEYS_TRANSCRIBE` option and everything
they dragged in are gone from this repo.

The engine itself is not lost. It stays in the kit (`okstudio/Transcribe.h`, its own
`okstudio_basicpitch` target, the kit's `docs/TRANSCRIPTION.md`) for whichever product wants
it next; Keys simply stops asking for it. What Keys sheds with it is that target's cost: a
multi-gigabyte ONNX Runtime download on the first configure, and the static MSVC runtime that
library forces on the *whole* binary. Keys is back on the default dynamic CRT, and `build/` is
cheap to delete again. See `docs/BUILD.md`.

## Threading: UI → audio note path

The keyboard runs on the message (UI) thread. It must not write to the outgoing
`MidiBuffer` directly. Instead:

- `PianoKeyboard` calls `KeysProcessor::noteOn/noteOff/allNotesOff`.
- Those build a `juce::MidiMessage`, stamp it with `Time::getMillisecondCounterHiRes()`,
  and hand it to a `juce::MidiMessageCollector`.
- **One note-on per sounding pitch, per destination stream.** `noteRefs` counts how many
  sources own each pitch, and MIDI is emitted only on the 0→1 transition and released only
  on 1→0. Four sources can want the same pitch at once (a chord pad, the live chord card, a
  chord held into an arpeggiator, and the keybed); emitting a second note-on for a pitch
  already sounding means one source's release ends it for everybody, which left keys lit
  with nothing sounding and leaked the arpeggiator's held set. Any new chord source must go
  through these two functions rather than the collector.
  The count is per *destination* since the three arp lines arrived (2026-08-01): the rule is
  a statement about one stream, because downstream one note-off ends a pitch for everybody.
  An arp line's input is a different stream with a different consumer — its engine, which
  counts owners itself — so a pitch held into line B must not suppress the same pitch played
  to the track output. With one shared counter it did, and the note vanished from the output
  while the key lit up. `noteOn`/`noteOff` take a `dest` (0 = the track output, 1..3 = a
  line's own queue) and `isNoteSounding` answers for any of them.
- `processBlock` calls `collector.removeNextBlockOfMessages(midi, numSamples)`, which
  drops the queued events into the block at the right offsets. Any MIDI already on
  the track (a clip, another device) passes through untouched.
- Before that drain, `watchInputNotes()` notes which pitches the *incoming* stream turns
  on and off (`inputNoteOn`, a flag per pitch, never a count — a missed note-off would
  leak a refcount into a key lit forever). It has to run first: afterwards the same buffer
  also holds this plugin's own notes, which `noteRefs` already tracks. Nothing is
  consumed or altered; `isNoteSounding()` simply also answers true for those pitches, so
  the keybed lights up for someone playing a physical keyboard through Keys and the live
  chord card can name what is under their hands. Added 2026-07-27.

The audio thread does nothing else: `buffer.clear()` (silence), watch the input, drain the
collector, and run the arp stage over what came out (`docs/ARP_DESIGN.md`). That stage is
four lines now: it drains each line's own queue, hands the keybed's notes to the lines with
**Keys** on, runs **every** engine into its own buffer and merges them all back - the engine's
`enabled` flag gates only whether steps fire, so a line that is off still takes chords in and
holds them silently until you switch it on. Routing is
a queue per line rather than a per-pitch ownership mask, because a mask lets the message thread
clear a pitch's owner before the matching note-off is drained and strands that note in an
engine's held set forever. No allocation, no locks: every buffer is sized in `prepareToPlay`
and every parameter is read through a pointer cached at construction.

**The track's own MIDI needs an invitation** (`arpTrackMidi`, global, default off).
`processBlock` holds it aside before the collector drains - by the time `runArpLines` runs the
collector has merged, and a clip's C4 and a clicked C4 are the same `MidiMessage` - and puts it
back afterwards, untouched and in sample order, so the chip decides what the *arpeggiator* hears
and never what the track plays. `trackHeldByLine`, a per-line pitch mask set only where a note
was actually handed to a line, is what lets the chip's falling edge release the right things: it
has to synthesise note-offs for whatever a line took in while the door was open, and firing one
for every pitch the *track* is holding would let `ArpEngine::Held::ons` (a refcount matched on
pitch alone) decrement whichever owner is there, stranding a keybed note under your hand if it
shared a pitch with a clip note the line never received.

## Playing surface: one product, one piano

The **playing surface** is not a choice: `KeysEditor` builds a `PianoKeyboard` (which
derives from `NoteSurface`), and that is the only surface Keys and Keys Host ship. There
used to be five tabbed *surfaces* (Keys/Hex/Pads/Faders/XY, switched by a `surface`
parameter); the Pad Grid was cut outright (drums belong to Beatform), the Faders and XY
surfaces were replaced by the knob row, and the Hex surface moved out to its own repo
(`../Hex`) along with Hex Host. Their parameters are still registered — see **Parameters
and state**.

There are no tabs left anywhere in the editor. The centre view that used to sit between the
Controls and the Arp went with the chord generator's panel on 2026-07-30; the knob bank it
held is the bottom row of the Controls section now, and what is left is a plain stack of
four sections. See **Folding layout**.

## Note bookkeeping: one union, one diff (NoteSurface)

Chords and holds make "which notes should sound" non-trivial. `NoteSurface` keeps
three sets of **drawn** ids (a MIDI note for the piano, a cell index for the grids):

- `pressed` — under the active mouse gesture (a click, or the current key in a glide),
- `latched` — toggled on by the Latch button or right-click (or by the forced latch
  during pad editing),
- `sustained` — captured by the Sustain pedal when the mouse released.

**Sustain and Latch differ only in what a second click does**, and that difference is the
whole reason both exist. Latch is a switch: a click on a key already in `latched`
**releases** it. Sustain is a pedal: a click on a key already in `sustained` **strikes it
again**, which `mouseDown` gets by dropping it from `sustained`, calling `refresh()` to
emit the note-off, and only then pressing it (`refresh()` emits deltas, so without the
first call nothing would go out at all). Gliding back over a sustained key does the same.

The Latch button was briefly retired, on the reasoning that a plain click already both
held and released — which is true, but it made the pedal behave like a switch, so a
repeated note under a held chord was impossible. Restored 2026-07-30 as its own toggle on
the Keyboard bar, reading the `latch` parameter that had survived for pad editing (which
still forces it on regardless of the button).

`refresh()` computes `want = pressed ∪ latched ∪ sustained`, diffs it against
`sounding` (drawn id → the output MIDI note currently on), and emits exactly the
delta: note-offs for keys that left `want`, note-ons for keys that entered it. Every
gesture mutates the sets then calls `refresh()`, so notes never double-fire or stick,
and panic is just "clear all three, refresh". When a polyphony limit is set, `refresh()`
first steals the oldest voices (FIFO `voiceOrder`) until `want` fits the cap. Changing
MIDI channel panics, so a note can't stick on the channel it was played on.

**Right-click latch** is an optional accelerator on every note surface, and it works on the
same three sets, so panic clears whatever it did. Octavium kept right-click latches in a
private set no panic ever cleared; that was its worst stuck-note bug, not a behaviour to
keep.

What it does depends on whether the key is already ringing *on this surface*, and **release
beats latch**: a right-click on a key held in either `latched` or `sustained` erases it from
both and lets it go; only a silent key latches. Erasing both unconditionally is also what
fixes a key caught by both, which used to leave one set and keep sounding out of the other.
The point of the sustained half is that a chord the pedal is holding comes apart a note at a
time without lifting Sustain, which is a thing one mouse could not otherwise do. Nothing
here can release a key lit by a chord pad, the arp or MCP: those never enter these sets, so
`refresh()` finds no `sounding` entry and their refcounts are left alone.

**One sanctioned exception to the left-click twin rule** (Owen, 2026-07-30): the latched
case has one, because a left click on a latched key releases it too (that path is keyed on
`latched`, not on Latch mode, so it works with every button off), but the *sustained* case
does not. Under Sustain a left click on a ringing key strikes it again by design, so a
left-click twin for "let this one pedal note go" would have to overwrite the behaviour the
pedal exists for.

A subclass provides only geometry: `drawnAt` (hit test), `outputNote` (resolution),
and optionally `noteChannel` (defaults to the global channel param; no built-in
surface overrides it any more since the Pad Grid was cut, and the Hex surface moved
to its own repo).

## Note resolution: snap then transpose, remembered

When a key starts sounding, `outputNote(drawnKey)` applies scale-lock
(`okstudio::scales::snapToScale`) then the octave shift, clamped to 0..127. The
result is stored in `sounding[drawnKey]`. Note-off uses that stored value, so a note
turns off correctly even if you change octave or scale while it is held.

## Humanize

The velocity range is the only velocity control. `KeysProcessor::noteOn` draws a
uniform-random velocity in `[humanizeVelMin, humanizeVelMax]` per note when Humanize is on;
with it off, `baseVelocity01` returns the band's midpoint. There used to be a fixed
Velocity slider *as well*, but it only ever applied while Humanize was off, so the two
were one control in two costumes. Collapsing the band onto a single value is now how you
ask for a fixed velocity. A `juce::Random`, touched only on the message thread, drives it.

The timing half of Humanize is gone. It nudged the message timestamp, which
`MidiMessageCollector` flattens (see **Scheduling** below), so it never worked; and even
working it earned nothing that Strum does not do better.

## Scheduling: why a delay cannot go through the collector

`noteOn`'s `delaySeconds` is not a scheduler. `juce::MidiMessageCollector` empties its
**entire** queue into the current block on every callback and clamps each event into it,
so anything beyond one buffer (~10 ms at 512 samples) lands on the end of that buffer.

Chord-pad Strum asks for up to 200 ms and was doing exactly that: every chord arrived as a
block however far the slider was pushed. `scheduleNoteOn` holds the note and emits it when
it comes due, on the message thread, the same way the MCP bridge defers notes. Its timer
runs only while something is pending. Every path that stops sound (`stopChordPad`,
`allNotesOff`) also calls `cancelScheduledNotes`, because a note-on that fires after its
note-off is a stuck note nothing clears.

`pressChordPad` and `pressLiveChord` share `fireChord`, so the polyphony cap, the strum
ordering and the scheduling are one implementation rather than two that drift.

## Chord pads

Pads live in the processor (`ChordPad`), so they persist and keep
sounding independent of the editor; `ChordPads` is just the view. They are arranged as
four pages of twelve (`padsPerPage` × `numPadPages`; drawn as two rows of six since
2026-08-03, each card carrying the chord's name and its notes underneath — the two columns
that freed up carry Strum and Humanize as `RangeKnob`s). The alternate
4x4 arrangement, a full card and a mini keyboard per pad, was the Pads section's **Big**
switch, and it went on 2026-07-31 once the note list fit under the name on the ordinary card
too. The strip shows the page `padPage`
selects and indexes by **absolute slot**, so a chord left ringing on another page keeps
sounding and a drag can't land on the wrong pad. Sessions carry a `padsPerPage` marker
(absent = 8) and each slot is re-based on load, so every pad stays on the page it was on —
**except when the page gets shorter**, which 16 → 12 was: a pad past the end of its page has
nowhere to go and is dropped, where the widening the re-base was written for never lost one.
Build a chord on the
keyboard (Sustain or right-click), drag the live card onto a pad to `setChordPad` the notes
(named by `keys::chords::detect`), then play it beat-pad style:
**What fills the live card is every chord source, not just the keybed** (2026-08-16). It read
`NoteSurface::soundingOutputNotes()` plus `KeysProcessor::inputNotes()`, and the first of those
answers only for keys clicked on the *surface itself* - so a chord fired from a pad, or held
into an arp line, went straight through `KeysProcessor::noteOn`, lit the keybed (which reads
`isNoteSounding`) and left the card empty. Two views of one chord disagreeing, and only the card
is draggable, so `ChordPads::sourceIsDraggable` refused a gesture the user could see a reason
for. `KeysProcessor::heldChordNotes()` is now merged in as well: it is the **read half of
`stopAllChordPads`**, and that symmetry is the definition - what Exclusive can choke as "a
chord" is what the card can show as one. The pads (`chordPadOn`), the live card's own gesture
(`liveChordOn`), and each line's `arpHeldNotes`, the last of those ungated by `arpLineOn`
because a line that is off still takes chords in. Deliberately not `isNoteSounding()`, which
counts the arp's *output* and would rewrite the current chord as whichever step the arp is on -
the distinction `keybedLit()` already draws for the keybed lights. Also not the generator's
800 ms audition, which keeps its notes in `ChordGenMenu` and is a monitor rather than a hold.
Sustain leaving those vectors populated is correct rather than a leak: the chord really is
still ringing, and that is the mouse-only route to capturing a chord built one click at a time.
**One chord, never the union.** It shipped as a union of every source for a few hours and was
wrong on the plainest reading of its own name. `lastChordSource` holds the last tag to *fire* -
written where a chord starts and never where one is released, since a source going quiet does not
make an older one newer - and `soundingForTag()` maps a tag back to its notes, so `heldChordNotes()`
returns the most recent source while it still sounds and otherwise scans for anything still
holding one. `KeysEditor::timerCallback` repeats the same choice one level up, between that answer
and the keybed, on which of the two last *changed*; the loser is used when the winner is empty, so
releasing the keys over a sustained pad shows the pad instead of blanking the card.
**Pads also choke pads unconditionally now**: `pressChordPad` stops every pad, not just the one
being re-pressed, and `chordExclusive` decides only whether it reaches the *other* sources (the
live card and the arp holds). Different instruments are worth stacking; two pads were not.

**Exclusive reaches the keybed's own holds too** (2026-08-16, Owen: "sustained or latched notes
aren't cleared when pad played"). `latched` and `sustained` live in `NoteSurface`, on the
message thread, and the processor cannot reach them directly, so `stopAllChordPads()` used to
choke every chord source it owns and leave a pedalled or latched key on the keybed ringing
straight through a pad's chord. `KeysProcessor::releaseKeybedHolds` is a `std::function<void()>`
the editor supplies, the one hook the processor calls outward through for the one chord source
it does not own; it must be cleared in the editor's destructor, since it captures the editor and
the processor outlives it. `pressLiveChord` passes `includeKeybed = false`: the chord it chokes
*is* what the keybed is holding, so releasing the keybed there would unlatch the keys in the same
breath as firing them.

**A pad is hold-to-play, when Play is on** (2026-08-16, restored 2026-08-22, Owen: "when the
play mode is checked on the pads, I want it to trigger as soon as you click on it and stay held
until you let go"). Pressing calls `pressChordPad` (fire, honouring the `chordExclusive` choke)
and releasing calls `releaseChordPad` (stop, unless Sustain is holding it: the editor releases
held pad chords when the pedal lifts).

**`LayoutState::padsPlayOnClick` - the Pads bar's Play toggle - gates the whole of it.** Off, a
click makes no sound at either end and the strip is drag-only. That trade is what made the
release own the sound between 2026-08-18 and 2026-08-22, behind a `padHoldToPlay` tick that no
longer exists - one switch answers it now.

**`LayoutState::padsKeepArpRunning` - the Keep arp toggle beside it - answers the other half**
(2026-08-26, default on). Turning Play *off* used to be the way to drag cards into the
arpeggiator, because firing a chord chokes the other chord sources and with Exclusive on that
reached each line's held chord: a press that turned out to be a drag had already stopped a
running line. But the choke, not the sound, was the problem, and giving up the sound to avoid it
meant you could not hear what you were building. Ticked, `pressChordPad` and `pressLiveChord`
pass `includeArpHolds = false`, so a press on the strip never releases a line's chord however
Exclusive is set. **A drop on a line still replaces that line's chord** and still chokes the pads
and the live card: pressing a card is playing a chord, and a line's held chord is not something
you are playing - it is what the machine is chewing.

A drag does **not** stop the chord (2026-08-22): with the press owning the note, cutting it on
travel made the length of a chord depend on the hand staying inside a small circle, which is the
one thing a mouse-only surface must not require. The note runs to mouseUp whatever the gesture
became, and the drag threshold is 14 px rather than 6 for the same reason. Nothing on this strip
runs on a timer any more and `ChordPads` is not a `juce::Timer`; **the generator's own audition
tray keeps its fixed `auditionMs`** on purpose, since a tray card is a candidate you are sampling
and a pad is an instrument you are playing. Dropping a pad on the live card runs the other
direction: `onRecall` hands its notes to the active note surface, which latches the
drawn keys that produce them (`recallOutputNotes`), so a stored chord comes back for
editing. Playback reuses `noteOn`, so Humanize colours each tone,
and `chordStrum` / `chordStrumMax` / `chordStrumDir` order the notes and stagger their
note-ons into a strum: the two ends are a band, and each chord draws one spread from it
(via `scheduleNoteOn`, for the reason in **Scheduling** above), so repeated stabs do not
all rake at exactly the same speed. Detection (`Chords.h`) rotates a pitch-class set
over 12 candidate roots and scores each against a template library — root mandatory,
3rd and 5th omittable — so it is unit-testable with no UI.

**Copy chord / Paste chord and Save chord as MIDI** join the card menu (2026-08-17, Owen: "need
to be able to copy paste chords"). The clipboard (`ChordPads::clipboard`, an
`std::optional<KeysProcessor::ChordPad>`) is UI-only, never the session tree: it needs no
migration and simply outlives a page flip, which is most of what it is for, since the four
pages are twelve pads apart, further than a drag reaches. `locked` is stripped at copy time
rather than checked at paste time: a lock protects the *slot* a chord sits in, not the chord
passing through it, and carrying the flag forward would silently lock whatever pad it later
landed on. Paste goes through the same choke a drop does, `clearChordPad` then `setChordPad`
inside one `UndoGesture`, so a pad left ringing by Sustain or feeding an arp line gives its old
notes up before the pasted chord lands rather than stranding them. **Save chord as MIDI** writes
one bar of the pad's notes (on together at tick zero, off together a bar later), at
`baseVelocity01()`, the same level `pressChordPad` already plays that pad at, into
`KeysProcessor::takeFolder()` and reveals the file in Explorer, because Ableton's own clipboard
is internal to Live and will not accept a paste from the Windows one: a dropped `.mid` file is
the only route a chord built here has into a Live clip, and a menu row cannot start a drag
itself. It is its own small writer (`oneBarChordMidiFile`) rather than a call into
`KeysProcessor::buildTakeMidiFile`, which plays back a *recorded performance* from
`capturedTake` and has no note list in it to hand a chord through.

A pad fires as one gesture, so there is no "oldest" voice within it: `pressChordPad`
honours the Voices cap by dropping its highest notes and keeping the lowest, which is
how `refresh()` resolves a too-big simultaneous chord on the keyboard. The cap applies
per source — a pad and the keyboard each fit under it separately.

## Chord generation

`ChordGen.h` builds a **weighted pool** of candidate chords for a key and mode, then
samples it. That one idea carries both sliders that are still the pool's own:

- **Scale Compliance** decides which tiers enter the pool: diatonic (always, weight 1),
  then borrowed from parallel modes, then secondary dominants, then any chromatic root,
  each fading in as compliance drops and weighted by how far it opened.
- **Lock Influence** re-weights the whole pool toward the *families* (triad / seventh /
  sixth / add / extended) of the chords you locked — so regenerating keeps their
  character without copying them.

Note count and inversions **used to be pool properties too, and are not any more**
(2026-08-01, Owen: "all of their options should have the option for how many notes and
what inversion"). Both are facts about the *voicing* a chord arrives in rather than about
which chord it is, so `fitVoicing` now applies them as a post-pass over whatever any of
the eight sources produced. `fitPads` is the same pass for the Markov path, which arrives
as pads rather than `chordgen::Chord`s.

**The three passes live in `src/ChordVoicing.h`** (`keys::chordvoicing`), not on the
generator: `applyMajorMinorBias`, `fitVoicing` and `applyVoicingPipeline` are free
functions taking their settings as arguments, so a test reaches them without a live
`KeysProcessor`. `ChordGenMenu::fitVoicing` and `applyMajorMinorBias` are still there and
are still what the generator calls, but they now do one job only: resolve the settings off
the APVTS and hand them to the pure pass. Any doc saying the ordering can only be pinned
by reproducing its shape in a test, because `fitVoicing` is private to a class needing a
live processor, is describing the arrangement before that move.

Growing a chord stacks further thirds **through the mode**, so an eleven-note chord is
still in key; shrinking one drops from the top, which keeps the root and the third (the
part that makes a chord recognisable) however far it shrinks. Inversions **replace** the
rotation a chord arrived in rather than compounding with it (root position first, then
invert), so ticking only "R" gives root position even from a source that had already
inverted one. `genNotesMin` / `genNotesMax` replaced the old 3/4/5 tick boxes with a
1 to 11 range (2 to 11 until 2026-08-21), and `genOctave` / `genOctaveMax` do the same for
register.

**`genMajMin` ("Lean")** runs before `fitVoicing`, biasing generated chords' thirds major
or minor whatever the mode. Its magnitude is the *probability* that a given chord gets
pushed, and it only ever touches the third, so a major ninth leaned minor is still a ninth.

A fill seeds the plain diatonic chord on each degree in order, then weighted-samples the
rest and shuffles only that tail. Each chord remembers the `degree` it came from, which
is what lets **New** hand back a different chord *for the same degree*.

The modes (`ScaleModes.h`) are deliberately **not** the kit's `okstudio::scales`. That
table answers "is this note in the scale", which is all Scale Lock needs; generation also
needs a chord quality per degree. They stay separate rather than one pretending to be the
other, and `kitScaleIndex` pairs them by comparing intervals — so a rename on either side
cannot silently mis-pair them. That mapping is what let a Feel preset move Root and
Scale along with the generator's own key; the Feel row went in the 2026-07-22 generator
redesign, so today `modes::emotions()` and `kitScaleIndex` have no caller but the test
that keeps every mode pairable, and they are kept for whatever brings the presets back.

`ChordSuggest.h` answers "what could follow this chord". Octavium writes each transform
as its own function; they are all the same shape (shift the root by an interval, maybe
flip the quality), so here they are one table. Whether a transform lands on major or
minor is read off the source chord's third rather than a name list, so it stays right as
types are added.

`ChordMarkov.h` is the second generator source: bigram transition tables built from
`MarkovData.h`'s progression corpus per mode (Major / Minor / Modal), sampled with a
temperature (`count^(1/T)`, clamped 0.3-2.0), an optional mood pre-filter, and an
optional start token; the walk repeats-and-truncates to fill the page. Per-pad
regeneration follows the chain from the previous pad's roman numeral (stored on the
pad as `numeral`), avoiding the chord it replaces when alternatives exist. The
algorithm is Octavium's, verified line-by-line; the corpus is authored for Keys,
because Octavium's was never in its repo or installer — its shipped Markov source
degenerated to I-I-I-I for every user. Realisation is root-position through
`chordgen::chordNotes` at the generator octave (Octavium hardcoded octave 4).

`ChordSources.h` added five more brains on 2026-08-01, taking Source from two choices to
seven: **Circle of Fifths** walks the circle from the tonic, taking each degree's diatonic
quality where the landing is in the key and major otherwise, with an occasional doubled or
reversed step so a lap doesn't read as one mechanical scale run; its one setting is
direction (flat-ward, the falling fifth most progressions are built on, or sharp-ward).
**Neo-Riemannian** starts on the tonic triad and takes P/L/R steps, each moving exactly one
voice and holding the other two in place, weighted by three sliders (all zero reads as equal
thirds). **Progressions** transposes a named template (ii-V-I, the axis, 12-bar blues,
Andalusian, Royal Road, rhythm changes, Coltrane's major-third cycle) to the current key and
loops it to fill the page; index 0 is Random. **Negative Harmony** mirrors a plain diatonic
progression about the tonic/dominant axis (C major becomes C minor) and is the one source
with no band of its own, since Key, Mode and Octave are the whole of what a reflection needs.
**Planing** takes one chord shape and slides it, diatonically (the quality bends to fit the
scale) or chromatically (the shape is preserved exactly, the Debussy sound). All five return
plain `chordgen::Chord`s, are not scale-compliance-gated and take no lock bias — a source
like this replaces a whole page in one call, and per-chord lock influence stays the weighted
pool's job. `ChordGenMenu::generateChords(count)` is the one dispatcher for Algorithmic plus
these five; Markov keeps its own three paths, because its chords carry a numeral these don't
and its per-pad regeneration steps the chain from the left neighbour.

Two folders outside `src/` belong to the library and are gitignored payload plus a manifest, the
arrangement `manuals/` already used:

```
datasets/                     # chord corpora and audio-feature dumps, per-source licences in
                              # datasets/README.md. Never redistributed with Keys
scripts/corpus/               # what was actually run against them: the MIT pack comparison, the
                              # ranking against Chordonomicon, and the mood-tag check
```

`ChordLibrary.h` added the eighth on 2026-08-18 and it is the odd one out: **Library** does not
compute a chord sequence, it looks one up. 355 named progressions, each stored as a roman-numeral
string in the grammar `ChordMarkov.h` already parses (so one row serves twelve keys, and the
storage format is the notation the cards print in their corner) and tagged on three axes: mood and
genre, which are Scaler 3's own vocabularies plus five words Keys already used, and **function** -
Loop, Cadence, Turnaround, Vamp, Lift, Descent, Turn, Open - which is the axis that separates "sad,
and it loops" from "sad, and it ends". A pick is three *filters*, so what comes back is a shortlist;
generation draws from it shuffled and without replacement and lays **whole progressions end to
end** rather than looping one, which is what makes a Vamp filter give eight vamps to compare and a
12-Bar Blues fill the tray on its own. Degrees resolve against the row's own mode rather than the
session's - the opposite of what every other source wants, and right here, because a minor row read
against a major session resolves nothing and labels half the tray `?`. Everything downstream is
unchanged, because it hands back plain `chordgen::Chord`s like the rest.

The library has a **second surface**, `ChordLibraryPanel`, a window of its own off a Library chip
on the Pads bar: twelve rows a page with `<` `>`, the whole row a Hear button that walks the
progression a chord at a time, and two buttons per row that send it to the generator's tray or onto
the page's empty pads. Each row also carries a **star** (`LayoutState::libraryFavourites`, kept by
name and per session) and the window a **Follows** toggle, which replaces the three filters with
`chordlib::couldFollow` on whatever progression the pads end with. Both surfaces share one piece of
state on `ChordGenMenu`, so a mood picked in either is the mood Fill obeys.

A pad that came from the library remembers it: `ChordPad::progression` and `progressionStep` carry
the row's *name* and position, the strip draws a bracket under a run of them, and the numeral on
such a pad comes from `chordlib::numeralAt` rather than from `degree` - see ChordNumerals.h's entry
above for why. `docs/CHORD_LIBRARY.md` is the design and the paper trail, including §10 and §11 on
what the corpora were actually measured to say and what they cannot.

Sitting over all eight is **Smooth Voicing** (`genSmooth`, renamed from "Voice Leading"
2026-08-01, Owen: "I don't understand what the voice reading does"; the parameter id
did not move), a post-pass rather than a source of its own: each chord after the first
has each pitch class placed in whichever octave sits closest to the previous chord,
blended by the percentage. Blending in octave counts rather than raw semitones is
deliberate, two notes sharing a pitch class are always a whole number of octaves apart,
so every intermediate amount still lands on the source chord's own notes; it never
chooses which chords you get or which notes they contain, only which octave each note
sits in. `smoothPads()` runs it over the Markov path; the other seven get it inline in
`generateChords`.

`ChordGenMenu::readsScaleSettings()` and `readsMode()` answer two different questions
and are easy to conflate. **Scale Compliance and Lock Influence are the weighted pool's
alone**, `readsScaleSettings()` is `sourceIndex() == 0`, true for Algorithmic only, since
none of the other six weighs a pool against either dial. **Mode is read by every source
except Markov**, `readsMode()` is `sourceIndex() != 1`, because Circle of Fifths,
Neo-Riemannian, Progressions, Negative Harmony and Planing all still need a scale to read
qualities off, walk within, mirror about or slide through; only a chain of bigram
transitions has no scale in it at all. Mode does **not** grey outside Algorithmic.

Six tick boxes (`genUseKey`, `genUseMode`, `genUseOctave`, `genUseNotes`,
`genUseInversions`, `genUseCompliance`, all default true) let generation off the leash a
setting at a time (2026-08-01, Owen: "check marks for the different sliders and options
that enable or disable them"). Ticked, the setting constrains generation; unticked, the
generator rolls that choice itself, `ChordGenMenu::constrains(paramId)` is the one
predicate both `fitVoicing` and the pool read, and an absent parameter reads as ticked so
a box that was never wired behaves as it always did. Key and Mode roll **once per
generation, not per chord**, because every source takes a single root and mode for a
whole batch; a free Mode picks only from the seven diatonic modes. Lock Influence, Smooth
Voicing and Lean (`genMajMin`) have no box: each already has an off position on its own
dial, so a box beside it would be a second control for what zero already says.

The new sources are *appended* to the `genSource` list, and APVTS stores a choice
parameter's plain index, so a session saved as Markov still reopens as Markov — the list must
never be reordered or inserted into, only appended to, or a saved session would silently
reopen on a different brain.

All these headers are pure logic with no UI, so they unit-test like `NoteMath.h`.

## Folding layout

The editor is a stack of **four** sections, each of which folds away so the window can be
squeezed small when the screen is busy, and, since 2026-07-27, each of which also detaches
into a window of its own: **Controls** (the CC knob bank alone since 2026-08-03, when Strum
and its direction left for the pads strip and the header row had nothing left in it; the row
is unconditional, the Knobs chip that used to fold it having gone in 2026-08-02), the
**Arp**, the **Pads**, and the **Keyboard** (with the wheels, Size and Octave as bar controls
that never fold with it). It was six until 2026-07-30, when the centre view and Transcribe both
went; the centre's knob bank became the bottom row of Controls rather than a section of its
own, because it is a row of settings and eight knobs, not a view you switch to. The Controls
band was two rows of settings for most of its life; Size, Octave and Humanize left it on
2026-08-02 for the Keyboard and Pads bars respectively (Owen: "the size can go down to the
header of the keyboard button ... remove the knobs button and make the knobs visible when you
open controls"), which is what took it down to one.

`SectionBar` is the fold affordance: a `juce::Button`, so the mouse-only contract and the
accessible name come for free. It calls `setTitle(caption + " section")`, which means the
capture script's UI Automation path *can* fold and unfold a section (a bar answers to
"Arp section", never to the bare caption a control riding on it might share). The section's
own small controls are laid out as its siblings in `contentArea()`. One trap:
`captionWidth()` feeds both the caption's own text box and `contentArea()`, so it and
`paintButton()` have to measure with the same `captionFont()`. Measure narrower than you
draw and the longest caption ellipsises; vary the font with the fold state and every control
on the bar shifts when the section folds.

The **arpeggiator is a section of its own** rather than a centre view (changed 2026-07-25).
Competing with the knobs and the generator was backwards for a panel that runs while you
play, and the arp is the one thing you want on screen *next to* a chord. Its bar carries the
**A**, **B**, **C** and **D** buttons - one per arpeggiator line, each in that line's own fixed
colour (cyan, magenta, amber, lime; `skin::lineAccent(line)`) so a glance tells them apart
across the bar, the macro cards and the Draw grid's playhead. Line C sat inert in the engine
and the parameter layout for session compatibility from 2026-08-02 until 2026-08-19
(`KeysProcessor::uiArpLines` was what kept it off this bar and everywhere else), and a new line
D was appended alongside it that day; `numArpLines` and `uiArpLines` are both 4 now, see
`docs/ARP_DESIGN.md`. Beside the letters, the **Hold off** chip and a **Detach**; everything but
Detach survives folding the panel away, because folding it destroys the view and never the
arpeggiator, and a chord held into a folded arp needs a way out that is still on screen. **Hold
off is deliberately still one button**: it releases every line and stops every chain, because a
per-line release would leave the others droning with nothing on a folded bar to stop it. **All
Off** beside it does that *and* switches every line off, and **Light keys** beside that is a
display toggle.

**Each letter is that line's own On switch** (2026-08-02, seventh pass, Owen: "the A and B on
the left side of the header, I want those to be on and off buttons to turn on or off the ARP ...
we can remove the a and b check mark on the right side of the header"). They used to be a pure
navigation tab choosing which line the *panel* edited, moved onto this bar 2026-08-02 (Owen:
"move the bpm and the a b and all into the header also") from the panel's own slot row, with a
separate lettered On toggle doing the actual switching a few pixels away near Hold off - two
controls for one job. The toggle is deleted; each letter is bound via `ButtonAttachment` straight
to that line's On parameter, which means they no longer select a line for editing (no
`onClick`) and, being a power switch rather than a navigation control, they never hide with the
section fold any more - the same case Hold off and Quantize already made for staying on a
folded bar. They remain a `DragAndDropTarget` apiece: dropping a chord card on a letter still
hands it to that line, on or off.

Which line the *panel* edits is chosen by that line's own **Details** button instead, in the
macro view (2026-08-02, seventh pass, Owen: "maybe we can add another button on the bottom by
anchor, like details, and that can open up the detailed arpeggiator view"), and the panel
paints a small **LINE A** / **LINE B** / **LINE C** / **LINE D** caption, in that line's own
colour, in its own top margin so something on screen still says which line you are looking at,
now that the letters no longer do.

The **macro view (All)** is the extra choice on the bar - **All** is the one navigation control
left there, and the only one that still hides with the section fold, since it is the only one
still pointing at a panel that a fold takes off screen. It replaces the band and the step
editor with a **2x2 grid of boxed cards** (A and B on top, C and D below, since 2026-08-19 -
two columns rather than four across is load-bearing, since a card's knob strip needs roughly
430 px and a fourth line in one row would have squeezed every card, where a grid only ever adds
rows), each with a detented rate knob, its shape, **eleven** knobs (Oct, Gate, **Density**,
**Duck**, Mutate, Stray, Lock, Swing, Offset, Vel, H.Time - Chance became Mutate and Lock
2026-08-18, Stray split out of Mutate 2026-08-21, H.Vel folded into Vel's own ring 2026-08-17,
Density gave Chance a face on the card and Duck joined it 2026-09-01, `docs/ARP_DESIGN.md` has
the mechanism), two **Harmony** interval
dropdowns each with its own **Chance** knob beneath it (appended 2026-08-19, from BigSky's
shimmer list) and, since the seventh 2026-08-02 pass, a **Details** button beside Anchor - the
card's own On toggle is gone the same way the bar's separate chip is, and an off line scrims
the whole card body instead (`paintOverChildren`, skipping every control's `setEnabled` so a
chord can still be dropped on it and a rate still dialled in before switching it on). **Launch
Quantize** rides the arp's section bar alongside the four letters and All; the **tempo** rides
the *Controls* bar instead, one build earlier (both 2026-08-02; see `docs/ARP_DESIGN.md` for
the passes). The macro view is a *view* rather than a fifth line - `editedLine` is untouched by
it, so a chord card drag keeps one unambiguous target - and it takes the band's space rather
than adding to it, so the panel does not grow. Each row's attachments bind to its own line for
the row's life, unlike the band's, which rebind whenever the edited line changes (a Details
click, where a tab click used to do it): four lines on screen at once cannot each be "the
current line".

**Keybed** (`MacroRow::keybedButton`, bound to `arpKeys`) sits on the card's top row after the
dice, reserved out of the row before Shape takes its cut so it costs no width at either floor and
no height. It is the same parameter the line's own On used to be read as answering for; both
surfaces say **Keybed** now rather than "Play" (which read as the line's own On switch) or "Keys"
(which collided with the bar's Light keys) - whether the keys you play on the keybed reach this
line at all.

**Clock** (`MacroRow::clockButton`, bound to `arpClockFollow`) sits right after Keybed, the two
switches read together as a pair - what reaches this line from outside it, and what clocks it.
Reserved the same way, before Shape takes its cut, so no floor moved for it either; see **CLOCK**
below for what it does.

**The four lines can hear each other** (2026-09-01; `docs/LINE_INTERACTION.md` is the design,
and the bus itself, DUCK, RESET, NEIGHBOUR and CLOCK are built so far). Each `ArpEngine` keeps a
`LineRecord` (`record`) that it writes as it runs - the steps it fired, its running total, the
walk pass, the Chain lane's own bit, the note it landed on - and a later line reads it through
`Params::follow`, a plain pointer. `runArpLines` walks the lines in letter order on the audio
thread and only ever hands a line's `arpFollow` choice (Off / From A / From B / From C) a pointer
to a line whose index is lower than its own, whatever a host lane or a script writes into the
parameter - the rule that keeps the bus one-way, A to D, with nothing to loop through. The card's
own row greys the letters a line may not follow; the processor enforces the rule regardless.
**DUCK** (`arpDuck`, 0-100) reads that record in `fireStep`, before the chance draw: a step is
skipped when the source has fired a step since this line's own previous one and a per-cell roll
says so, salted apart from Mutate and Lock so the two never draw the same number on one cell. The
first step after Follow changes, or after DUCK starts, never ducks - there is no "since my last
step" yet to compare against. **RESET** (`arpResetFollow`) is the Follow entry of the card's
Retrigger list rather than a chip of its own (`ArpPanel::applyRetrigChoice`): picking it clears
the clock-window parameters the same way a clock window clears the note retrigger. A source
publishes `record.pass` at the top of `fireStep`, before any condition can return, so a muted or
ducked boundary step still turns a follower's page. **NEIGHBOUR** widened the Chain lane's range
to 0-4: values 3 and 4 read `follow->lastStepFired` (and its negation) inside `chainAllows`, so a
lane can be made to fire with, or against, another line's most recent step; with no source both
allow, so a lane drawn for a switched-off source still plays.

**CLOCK** (`arpClockFollow`, phase three, 2026-09-02) replaces the step loop rather than reading
the record like the other three: on, with From naming a source in effect, `process()` runs
`clockedStepsInBlock` instead of `scanStepsInBlock`, firing one step of this line for every entry
in `follow->firedAt` - one per *step*, not per hit, so a source ratcheting three sub-hits still
advances this line by a single lane cell. Swing, the Late lane and Anchor are the source's, since
the offsets already are; Gate is measured against `follow->stepSamples`, the source's own step
length, rather than a rate this line no longer reads. This line's Rate, its steppers, Sync/Hz,
Dot, Tuplet, Anchor and Swing grey on the card and the Play page, and the Cards page's four
rhythm dividers grey with them, since a divider divides a clock that is not running;
`arpLineIsClocked` in `MacroRow.h` is the one test the card, the panel and `get_state`'s
`clocked` field all share, composed into `refreshRateMode` alongside the Hz greying rather than a
second function, so a control greyed by either question stays greyed whichever set it. A clocked
line whose source is off or silent plays nothing - the one mechanism where that reads the
opposite of the bus's usual rule, because CLOCK removes the follower's clock rather than adding a
condition to it. DUCK still runs underneath: a clocked step sits exactly on a source fire, so
DUCK at 100 silences every step after the warm-up one. Switching CLOCK off mid-run hands the line
back to its own clock exactly where it would have got to, with no catch-up burst, because the
free-run phase and the ppq tracking are left running underneath the whole time CLOCK is on.

**Legato** (`arpLegato`, per line, default off) holds a note through the steps that would
otherwise leave a gap - Density, the Chance lane, a mute, a rest, a failed Chain condition -
instead of ending it at its gate: the note before a skip stays open and closes one sample after
the next fired step's note-on (`emitHit`'s `closeHeld`), the opposite order from the tie branch
on purpose, since a tie is the same pitch and must go off first while this is a different pitch
that must not gap. It works by looking one step ahead: `fireStep` asks `prerollNext` the same
four questions (chain, mute, rest, chance) one step early and keeps the chance draw, so
`chanceFails` hands the identical answer back when that step actually arrives. `Active::legato`
marks a held entry - no due time, skipped by the ordinary due path - and `releaseLegato` closes
every such entry when the flag goes off or nothing is left to hold it open (the chord released,
the line switched off).

**Density** is `arpChance` given a face on the macro card (`kDensity` in `ArpPanel`'s `Knob`
enum) rather than a new parameter; the Play page's own Chance slider reads the identical value,
the card simply puts it where you reach for it while playing.

The **chord pads are a section of their own** too, below the arp. They used to live inside
the centre view, which meant the arpeggiator (the one panel whose whole job is to chew on a
chord) was also the one place you could not reach a chord. Their page buttons ride on the
Pads bar from the left, **Humanize** and its velocity range sit after them since 2026-08-02
(moved off the Controls band, Owen: "make smaller to fit"), and the generator's **Fill**,
**Regen** and **Generator** chips with its **Key** combo come off the right end (the pages hide
with the strip, everything else on this bar never does). **Mode**, **Scale Compliance** and the
arp's old target-line letter chip left this end of the bar on 2026-08-02 (Owen: "remove the
scale and percentage and letter b from pads header"); Mode and Compliance are still in the
generator's window, which holds every setting it has, and the arp bar's letter switches no
longer name which line a card feeds, since they read On/Off rather than a selection - the
panel's own **LINE A** / **LINE B** / **LINE C** / **LINE D** caption does that now.

**A click never hands a chord to a line any more** (2026-08-02, Owen: "when an arpeggiator's
running and you click on a pad, I don't want it to send it to the arpeggiator unless you drag
it"): `ChordPads::mouseUp` plays the pad for a short audition exactly as it would with every
line off, regardless of what `KeysProcessor::cardsFeedArp` says. Feeding a line is a **drag** -
onto a card in the macro view, onto a letter on the arp bar, or onto a slot - and each names a
different "which line": a letter or a slot's own line makes that line current, a macro card is
already labelled with its own. Dragging a card onto an arp slot binds the chord there, and onto
a letter or a macro card hands it over immediately; both are ordinary JUCE drag-and-drop targets
rather than something the editor mediates in screen coordinates by hand (`src/ui/ChordDrag.h`,
2026-08-02 - see the chord generator section below for why that mattered), the same mechanism
the audition tray's own drags use, and both suppress the strip's drag-off-to-clear so a gesture
aimed at the arp can never delete a chord.

**Only the left end of a bar folds it** (2026-07-30, Owen's ask). `SectionBar::hitTest`
narrows the button to `foldZone()`, the chevron and the caption, 92 px wide at the narrowest
caption; a hairline is painted where that target ends, and only that end lights under the
mouse. The bars are still full-width Buttons sent `toBack()` after construction, and the
controls riding them are siblings sitting in front, so z-order has always meant that a click
landing *on* Detach reaches Detach.

This **reverses** the 2026-07-27 change that removed the same override. For three days the
whole strip folded, on the reasoning that a 34 px-tall full-width band is a bigger target and
that z-order already protected the controls. The second half was true and still is; what it
missed is that z-order only defends each control's own rectangle. It says nothing about the
gaps around them, and on a bar whose right end is mostly gap, a click aimed at Detach that
missed by a few pixels hit bar, and the bar hid the thing being reached into. That cost is
asymmetric, so bigger is only kinder when the extra area does what the target does. Docs
elsewhere in the line that still say "the whole bar is the target" are describing that
three-day window.

Open and folded bars are painted at deliberately different weights (the open one a solid
ruled band with an accent tick, the folded one flat and dim), so a stack of four reads as a
shape before any caption is read. The Detach button hides with its section for the same
reason, and because detaching a folded section only ever built a window that opened hidden.

## The accent is per instance

Keys used to have exactly one accent, the OK Studio cyan. It is now one of eight, chosen
per instance, because a session with Keys on two tracks gave two identical windows.

This is deliberately **not** a global. A DAW loads every instance into one process, so a
mutable global would repaint every track's Keys together. The live triple (base / hot /
deep) hangs off each editor's `KeysLookAndFeel`; components call `skin::accentOf(*this)`,
which resolves through the LookAndFeel chain JUCE already walks up to the editor. Paint
loops hoist that call rather than paying a `dynamic_cast` per key or per step.

Two things are easy to miss when adding a colour path. The kit bakes several JUCE
`ColourId`s from the accent at construction (tick marks, slider tracks, the popup
highlight), so `setAccent` re-applies every one of them. And `wheelLnf` is a *second*
LookAndFeel instance with its own copy, so it has to be re-tinted alongside `lnf`.

**`skin::textDim` and `skin::textFaint` were brightened 2026-08-01** (Owen: "hard to read
some text, too dark"). They had been picked by eye against `skin::text`, which is the
wrong comparison: nearly everything wearing them is 9-11 px letter-spaced uppercase (the
section captions, the note list under every chord card), and small letterforms need far
more contrast than large ones to read at the same effort. `skin::text` itself is
unchanged - it was never the problem.

This replaced full-editor overlays that dimmed and covered everything, including the
keyboard. Editing an arp while unable to play a note was backwards for an instrument you
perform. The panels keep an overlay mode (`setInlineMode(false)`) but nothing uses it.

`KeysEditor::idealHeight()` is the single source of truth for what the folds add up to;
`resized()` spends exactly the same constants, so the window a fold asks for and the
layout it gets cannot drift. Standalone and in a DAW the editor resizes itself to that
height; embedded in Keys Host it reports the number through `onIdealHeightChanged` and the
host grows to fit, because an open arp would otherwise push the keybed off the end.

### Every section detaches

Each section's content lives in a `Holder` of its own rather than directly in the editor, so
**Detach** is a single re-parent into a `DetachedWindow` — the holder's parent becomes the
window's content slot and nothing else changes. The window borrows the holder and owns
nothing, so `~KeysEditor` tears every one of them down explicitly before anything else.

`KeysEditor::sections` is the table that makes this generic: one `Section` per bar, holding
its holder, its Detach button, the window it is currently in, the two `LayoutState` flags it
reads (open, detached) and the frame it remembers. `idealHeight()` and
`syncSectionControls()` walk it rather than naming sections, so a detached section
contributes no height to the main window and a folded one hides its window instead of its
slot: one control means one thing wherever the section happens to be. The two that do not
walk it are the two where each section genuinely costs something different, and they read
the table instead. `sectionHeight()` is a switch (Controls adds the knob row when it is
unfolded, the arp asks its panel; Pads is a fixed `padRowH` now that Big is gone), and
`resized()` lays each bar out in a block of its own, because what rides each bar differs.

The keybed was the first to do this and keeps one extra. Detached, `PianoKeyboard`'s 185 px
key-height cap comes off: dragging that window is meant to resize the keys, which is the whole
point of the feature for a player working with one mouse. And the **Wheels** chip travels with
it (`Section::travellers`), because it is the keybed's, not the editor's. Size and Octave used
to need a second, `detachedOnly` traveller of their own for exactly the same reason - the
keybed's key count lived in the Controls section, which is precisely what you fold away before
detaching the keyboard - but since 2026-08-02 both live directly on the Keyboard bar itself
(Owen: "the size can go down to the header of the keyboard button"), so they simply travel with
the bar like Wheels always has; `Section::Traveller::detachedOnly` and the second Size combo it
existed for (`detachedSizeBox`, accessible name "Keybed size") are both deleted. Every detached
window carries the button that undoes the detach on a strip at the top, so the control that
re-docks a section is never in the window you are not looking at.

Controls that belong to the *editor* rather than to the content stay behind on the bar: the
arp's **A** / **B** switches, **Hold off** and **Launch Quantize**, the pads' page buttons,
**Humanize** and its velocity range, the generator's **Fill** / **Regen** / **Generator** chips
and its **Key** combo, and the Controls bar's **Tempo**, **Sync**, **Root**, **Scale**, **Scale
Lock**, **Voices**, **MIDI Ch**, **Instrument** chip (Keys Host only) and theme swatch. Tempo
through MIDI Ch moved from the Controls band onto this bar 2026-08-02, alongside the generator's
**Mode** and **Scale Compliance** leaving the Pads bar for its window alone, and the arp's old
target-line chip leaving it for the arp bar's own A/B; Size, Octave and Humanize left the
Controls band the same day for the Keyboard and Pads bars, and the **Knobs** chip that used to
fold the Controls knob row is deleted outright rather than moved, since that row is
unconditional now. Paging a strip that is off in a window of its own is one click either
way, so the pages are no more the content's than the swatch is. A bar whose section is away
says so, in the space its own controls did not use.

**A host embedding `KeysEditor` can add to a bar too, since 2026-08-02.** `onBuildInstrumentMenu`,
`instrumentName` and `refreshInstrumentChip()` are public hooks a host sets to get an
**Instrument** chip on the Controls bar - Keys Host is the one that does, reproducing its old
Load/Show-Hide/Eject controls as a popup menu off the chip instead of a bar of its own (see
`docs/KEYS_HOST_DESIGN.md`). Plain Keys (the VST3, the plain Standalone) never sets these, so
the chip stays invisible and their Controls bar is unchanged. This is the first extension point
`KeysEditor` has ever exposed to something embedding it - the same functional shape
`ChordPads::onExtraMenuItems` already used internally - and it is the one *elastic* control on
the Controls bar: `resized()` measures the tempo group and the Root…MIDI Ch group first (both
fixed-width), and the chip gets whatever space is left over, clamped to a readable range, so a
long instrument name can never push the fixed groups around.

All of it (folds, detached window bounds) is in `KeysProcessor::LayoutState` rather than the
editor, so it survives the window closing, and it is saved in the session tree rather than as
parameters: none of it changes a note, and exposing it to host automation would only add ways
to break a session.

## The chord generator: one brain, three surfaces

`ChordGenMenu` is a plain value member of `KeysEditor` (`chordGen`) and draws nothing at all.
Its controls are on the Pads bar, in a window of their own (`ChordGenPanel`), and on a pad's
card menu, and **none of those owns it**. It works on the **current page**, so the four pages
can hold four different keys.

**It lost its cards first** (2026-07-30). `ChordGenPanel` drew a 4x4 grid of the sixteen pads
on the current page (the same pads, through the same `setChordPad`), because it was written
when the generator was a full-screen overlay and the pads had no section of their own. They
have had one since 2026-07-25, so the grid was the same page drawn twice, at two sizes, one
of which could set a pad's lock state and one of which could only paint the dot for it. The
tall arrangement became the Pads section's **Big** switch (`layout.padsBig`, four rows of
four), so the large card (chord name, its notes with octave numbers, a mini keyboard of what
is held) was available whatever else was open. **Big went on 2026-07-31**: every pad, in the
ordinary two rows of eight, now carries the chord name with its notes underneath (octave
numbers included, no mini keyboard), so the tall arrangement had nothing left to show that the
short one didn't, and `layout.padsBig` and `padBigRowH` came out with it.

**Then it lost the panel too**, in the same round that removed the centre view, and **got it
back as a window a few hours later** (Owen: "I think the chord generator should just pop out a
new window instead of being in the right click menu"). The intermediate arrangement put every
setting on a pad's right-click menu as a submenu of ticked discrete values, because a
`PopupMenu` cannot hold a slider; that reached 23 rows and roughly 820 px, which is more menu
than fits above a pad near the bottom of the window. What the round through the menu settled
for good is the **split between the brain and its surfaces**, and that survived the window
coming back:

- `ChordGenMenu` is the brain. It is a plain value member of `KeysEditor`, alive for the
  editor's whole life, and it draws nothing. It never was the panel's, which is why the panel
  could be a full-screen overlay, an inline band, a menu and now a window without the
  generation half changing a line.
- `ChordGenPanel` is a **view onto it**, built when its window opens and destroyed when it
  closes. It holds a 15 Hz display timer and nothing else: no note, no preview, no device.
  Every control is an `AudioProcessorValueTreeState` attachment, and the two picks that are not
  parameters (Markov **Mood** and **Start**) live on `ChordGenMenu`, so closing the window
  loses nothing and reopening it shows the same state.
- **New chord** and **Next: could follow** stay on a pad's card menu, through
  `addPadMenuItems` / `handlePadMenuChoice`. They are questions about the card under the
  mouse, and they are offered on **every pad on every page, always** (New chord greys on a
  locked card, which is the lock doing its one job and not the window doing anything). The
  panel used to gate them on being alive, so the generator's own actions vanished from a card
  whenever its view was closed - that is the bug the brain/view split exists to prevent, and it
  is why the window must never own `ChordGenMenu`.
- **The window grew a 4x4 tray of candidates on 2026-08-01, and it is not the grid that left
  on 2026-07-30.** That earlier one drew the current *page* - the same sixteen pads, through
  the same `setChordPad` - so it was the Pads section drawn a second time. `ChordTray` draws
  sixteen chords that belong to **no pad**: not on any page, not in the session, gone when the
  window closes (Owen, 2026-08-01: "I have four by four pad where you can audition new chords.
  We want to be able to try a bunch out"). A click auditions one for 800 ms through
  `ChordGenMenu::auditionChord`, which forwards to the same `previewChord` the suggestion menu
  already used, so `ChordTray` and `ChordGenPanel` still never call `noteOn` between them.
  `previewChord` calls `processor.stopAllChordPads()` before sounding the audition (fixed
  2026-08-01, same day: Owen found that a ringing pad under Sustain made an audition of the
  same chord silent, or made an overlapping one sound like it had fewer notes than it listed,
  because Keys emits a note-on only on the 0->1 transition of `noteRefs`. An audition is a
  monitor rather than a performance, so it takes the room; the cost is that it stops a
  deliberately sustained pad and releases a chord held into the arp). A drag onto a pad
  commits it there, and **a committed card leaves its cell empty** rather than refilling
  itself - the hole is how you see which candidates you have already taken, and it is what
  gives **Fill** something to do. **Fill**, **Regen** and **Clear**, on the tray's own header
  row, replace the **Reroll** button that used to sit there: Fill writes the empty cells only,
  Regen rerolls the ones that already carry a candidate, Clear empties the tray outright, and
  none of the three can lose work because a tray card is not state - Fill greys when the tray
  is full, Regen and Clear grey when it is empty. **Changing a setting generates nothing**
  (2026-08-01, Owen: "I don't want it to auto generate when you change a source"). The tray
  rerolled itself on any settings change for part of that day, on the reasoning that sixteen
  answers to the old Key are worth nothing once the Key has changed - right about the
  candidates, wrong about who decides: sweeping Source to compare the eight of them threw the
  tray away six times on the way past, and a control you cannot explore without destroying
  your work is a control you stop touching. `ChordTray::settingsMovedSinceFill()` polls the
  same signature (the generator's APVTS parameters plus Mood and Start, deliberately *not* the
  page's locked chords, which feed Lock Influence and would mark the tray stale on every
  commit) but only tells the window to *say* the candidates are stale - the caption reads
  "settings changed since these were generated. Regen for new ones." Generating is **Fill**
  and **Regen** and nothing else. `ChordGenMenu::generateCandidates(int count)` is the entry
  point behind both: the one generator call that builds chords and writes them to
  no pad. Elsewhere "the generator draws no cards of its own" and "there is exactly one set of
  chord cards" (`docs/CONTROLS.md`, `README.md`) mean one set of **pads** - a tray candidate is
  not a pad and does not become one without a drag or a menu pick.
- **`ChordRefCard` is the one chord the tray's own actions cannot touch** (Owen, 2026-08-01:
  "I think we should have another box for the reference chord where we can drag in something
  from the main window or one of the other chords. So when you regenerate everything, it
  doesn't erase your reference chord"). It fills from a **tray card** dropped on it, or from a
  **pad in the main window** dropped on it. `ChordRefCard` is a `juce::DragAndDropTarget` and
  takes both directly. A drop on it **copies**: it sets `taken` on the payload and never
  `consumed`, which suppresses the ordinary "drag off the strip clears the pad" behaviour for a
  pad and leaves a tray candidate in its cell, so reaching for the reference box can never
  delete the chord you were trying to keep. Left-click auditions it
  the same as a tray card.
  **It is a drag *source* as well from 2026-08-16** (Owen: "I'm not able to drag the currently
  held chord into the chord pad"). It had been drop-only, on the reading that **Similar** and
  **Could follow** were route enough to the pads - but those two fill the *tray*, and neither
  puts the reference chord itself anywhere, so the one card in the window that visibly held a
  chord was the one card that could not give it up. Dragging it off commits it to a pad, the
  same gesture and the same 6 px threshold `ChordTray::mouseDrag` uses. One difference, and it
  is the card's whole reason for existing: it **copies**. The reference is the tray's fixed
  point, so it keeps its chord however many pads it fills.
  That is `chorddrag::Payload::From::refCard`, a kind of its own rather than a reuse of
  `trayCell`. The two are identical to every target except on `consumed` - and `consumed` is
  exactly what would have emptied the box the first time you dragged out of it, which is the
  opposite of a fixed point. `ChordPads::dropCellFor` groups `refCard` with `trayCell` for the
  locked-pad refusal (both replace a pad's chord outright); `ChordPads::itemDropped` sets
  `consumed` only for `trayCell`. The card refuses its own kind in
  `isInterestedInDragSource`, since it has nowhere to land but where it already is, and the
  arp's targets take `padSlot` alone and so refuse it too, unchanged. Beside it, **Similar** and **Could follow** call
  `ChordGenMenu::similarTo` / `couldFollow` with the reference chord as seed and write a fresh
  trayful (`similarTo` keeps the root and varies the colour; `couldFollow` reuses
  `suggest::all`, the same table the pad card menu's own "Next: could follow" offers, rather
  than inventing a second opinion); **Clear** empties the reference card alone. All three grey
  when the card is empty.
- **Source and Circle Direction are always-visible button rows, not combo boxes**
  (2026-08-01, Owen: "maybe instead of the source being a drop down and the direction
  being a drop down, maybe those can be, like, always visible"). One click instead of
  two, and seven answers on screen instead of six hidden behind the first, for a setting
  whose whole point is comparison. JUCE has no attachment for a row of buttons on one
  choice parameter, which is the one place in `ChordGenPanel` that hand-syncs rather than
  binding: `setSourceParam(index)` / `setCircleDirParam(index)` write `genSource` /
  `genCircleDir` on a click, and `refreshRadioStates()`, run from the panel's existing
  15 Hz timer, polls the parameter back onto the tick mark. They write the same
  parameters the combo boxes did, so nothing downstream changed.
- **Scale Compliance, Lock Influence and Smooth Voicing sit on one fixed row; Notes,
  Inversions and the Octave range sit on another** (2026-08-01) - neither leaves the
  screen as Source changes, which fixed a standing mistake: Notes and Inversions are
  facts about the *voicing*, never about which chord it is, so they were never the
  weighted pool's property and had no business living inside its band. **Algorithmic and
  Negative Harmony now have no band at all** - Algorithmic because everything that was
  its own moved to those fixed rows, Negative Harmony because a reflection only ever
  needed Key, Mode and Octave. The band row collapses to zero height for both, and the
  freed height goes to the tray, so the window does not resize as you switch source.
- **`SourceViz` draws a read-only diagram of the current source** under the button row
  that picks it (2026-08-01, Owen: "a visualization for the generation source so people
  understand what it's doing"), and highlights the actual walk that produced whatever is
  in the tray on top of a static figure of the shape. It takes no input,
  writes no parameter and generates nothing - `setInterceptsMouseClicks(false, false)` in
  the constructor, same as every other click-through diagram in Keys. Fed from the panel's
  15 Hz timer, since everything it draws (source, key, the tray's contents) can move without
  the class being told.
  **Rebuilt 2026-08-16** (Owen: "reexamine the graphic visuals on the chord generator. They
  don't make any sense"), and the diagnosis is worth keeping because it is a layout failure
  that reads as seven separate ones. Each paint branch gave the *diagram* a square the height
  of the 112 px box - ~80 px - and spent the remaining ~1500 px on a chip row restating the
  chord names already on the sixteen tray cards a few hundred pixels below. Informative half
  tiny, redundant half enormous. That geometry is what produced the two hard failures: the two
  wheels computed `radius = jmax(20, wheelBox.getHeight()*0.5 - 16)` = **24 px** and then drew
  twelve labels at `radius + 10`, which is ~17 px of arc for text needing ~18, so adjacent
  labels overlapped; and the Neo-Riemannian triad was clipped by its own frame while its chips
  read `P P P P L L L L` with no chord names attached.
  The fix deleted every chip row, raised `preferredHeight()` to **160**, and gave each source
  the full width plus a right-aligned one-line legend beside its caption. The two wheel sources
  anchor the wheel left and use the freed width for a pill-and-arrow chain in the same visual
  language as Markov and Neo-Riemannian - **the arrow carries the relationship**, which is the
  whole distinction from the deleted chip rows: signed step distance round the circle for
  Circle of Fifths (`-1` a fifth flat-ward, `-2` a leap), `root → mirror` pairs for Negative.
  One trap worth recording: **Progressions must not read `ChordPad::numeral`**, which only the
  Markov branch of `generateCandidates()` ever writes - every other source leaves it empty and
  sets `degree` instead. Reading it directly drew sixteen `?`, and because every numeral then
  compared equal the repeat-period search found period 1 and drew one degenerate bracket per
  card. `progressionNumeral()` resolves numeral → `degree` → `degree` re-derived from `rootPc`
  against the key, casing each by the mode's own per-degree quality from `ScaleModes.h` (which
  is the reason that file exists rather than the kit's scale table), and only then `?`.
- **A tray card's right-click menu** is a new entry on the closed owner-directed list in
  `CLAUDE.md` (Owen, 2026-08-01: "when you right click on a chord in there, I want you to have
  a whole bunch of options about trying to find similar ones or what might come next"):
  `ChordTray::showCardMenu` builds eight items in four groups - Send to first empty pad; Fill
  tray with similar chords, Fill tray with what could follow; Octave down, Octave up, Next
  voicing; New chord here, Clear this card. **Opening it makes no sound**, and neither do the
  three shaping edits - it auditioned the card for a few minutes on the same day and Owen had
  that taken out, since the left click is already how you hear a card, and right-clicking one
  you just auditioned (or right-clicking on the way to Clear) played it again for no reason.
  Send to first empty pad is the drag with the aim taken out (`ChordPads::firstEmptyPadOnPage` /
  `sendChordToFirstEmptyPad`), and it is the one *placing* item, greyed by `onPageHasEmptyPad`
  when the current page has no room.
- **The drag crosses two top-level windows, and JUCE gives that for free** (2026-08-02). It was
  hand-rolled on `mouseDown` / `mouseDrag` / `mouseUp` plus `juce::Desktop::findComponentAt`
  until then, on the stated belief that a `DragAndDropContainer` only ever sees a drop inside
  its own window. **That belief was false and this document asserted it as settled fact.**
  `DragAndDropContainer::startDragging` takes a fourth parameter,
  `allowDraggingToOtherJuceWindows`, defaulting to false; pass **true** and the drag image is
  added to the *desktop* instead of to the container, which makes `getParentComponent()` null
  inside JUCE's own `findTarget` and routes the lookup through `findDesktopComponentBelow` -
  every desktop component in z-order, walking up each parent chain for an interested
  `DragAndDropTarget`. That is the same hit test the workaround was doing by hand, and it was
  there the whole time (verified against JUCE 8.0.8; a docs PR is open upstream as
  juce-framework/JUCE#1692). See `src/ui/ChordDrag.h`.
  So: **the tray is an ordinary drag source and every taker is an ordinary
  `DragAndDropTarget`.** The containers are `ChordGenPanel` (for the tray) and
  `KeysEditor::Holder` (for the pad strip - the holder rather than the editor, because it is the
  one ancestor a section keeps when it is popped out into a window of its own). The ghost now
  follows the cursor out of one window and across the other, which the hand-rolled version
  explicitly could not do. Occlusion, a folded Pads section and a detached one are all answered
  by JUCE's search, better than before: the reference box used to light up through a window
  sitting over it. A drop **refuses a locked pad** (the lock that protects a chord from
  generation protects it from a stray drag too) and calls `clearChordPad` before `setChordPad`,
  so a sounding or arp-held pad releases its old notes properly instead of having the chord
  swapped out from under them.
- **Two things JUCE has no opinion about ride on the payload** (`chorddrag::Payload`, a
  `ReferenceCountedObject` boxed in the `var` that `startDragging` takes). Boxing rather than
  passing an index is deliberate: a tray candidate belongs to no slot and is not in the session,
  so there is no index the far end could look it up by.
  - **`taken`** is the veto. Dragging a card off the pad strip clears it, and reaching for the
    reference box *means* dragging a card off the strip, so without an answer the one gesture
    that keeps a chord would be the one that deletes it. Every target sets it;
    `ChordPads::itemDropped` sets it for any release that lands on the strip at all, refused or
    not, because "landed here" and "did something" are different questions and only the first
    decides whether the drag left the row.
  - **`consumed`** is the *other* ownership answer, and it is why one flag is not enough. A tray
    candidate dropped on a pad is committed and its cell goes empty; the same candidate dropped
    on the reference box is copied and the cell stays, because a reference is a copy of a chord
    you like. Same gesture, opposite outcome.
  Both are read one message-loop turn after the button comes up
  (`chorddrag::whenDragSettles`), not in `DragAndDropContainer::dragOperationEnded`. A source's
  own `mouseUp` is too early - JUCE dispatches a component's `mouseUp` before its mouse
  *listeners*, and the drag image is a listener, so `itemDropped` has not run yet - while
  `dragOperationEnded` fires from `~DragImageComponent` after a 120 ms dismissal animation and a
  timer, which is a third of a second of a card that still looks like it is there. Posting from
  `mouseUp` lands after the same event's listener dispatch and before the next frame.
- **The window is not a `Section`.** It never docks, so it has no bar, no fold, no caption and
  no Detach button, and every one of those is something `KeysEditor::sections` walks. What it
  does share is `DetachedWindow` (the skinned 38 px title bar with mouse-only-sized buttons,
  resize limits, the frame remembered as it is dragged, and `ensureWindowReachable`) and the
  remember-where-it-was-left contract: `LayoutState::chordGen` and `chordGenBounds` sit beside
  the sections' own flags and frames and persist with the session. Its minimum size is
  **derived** - `ChordGenPanel::contentSize()` adds up the same row widths and heights
  `resized()` lays out, and `minWindowSize()` adds the title bar and border.
- **It closes two ways and tears down once.** The panel's Close button and the title bar's X
  both run `KeysEditor::setChordGenWindowOpen(false)`, deferred one message-loop turn because
  each of them is inside the object that call destroys.
- **The card menu keeps its budget, and it is rows.** It is anchored to a pad near the bottom
  of a 699 px window and shown at `withStandardItemHeight(okstudio::ui::minHitPx)`, so each row
  costs 34 px of screen measured *upwards* from there, and a separator 17. JUCE answers a menu
  taller than the space it has by splitting it into columns (`insertColumnBreaks`) or making it
  hover-scroll, and a scrolling popup cannot be operated with one mouse at all: hovering the
  arrow scrolls and moving to click scrolls the item away. It is **9 rows and 2 separators,
  340 px**, down from the 23 rows the settings had taken it to. Section headers are not used at
  all - a rule says the same thing at half the height, and a JUCE section header is not a row
  but an item and a half, 51 px here, since `HeaderItemComponent` asks the LookAndFeel for an
  item size and then adds half of it again.
- **Octave down / Octave up / Next voicing** act on one pad's stored chord (menu-only, Owen's
  call). `chordgen::rootPosition` / `applyVoicing` / `voicingOf` in `ChordGen.h` are the
  voicing cycle: root position, one inversion per note above the root (the same inversions
  `genInv0..genInv3` name), then a spread, then round again. Nothing is remembered on the
  card - `voicingOf` reads the arrangement back off the notes by shape, which is what keeps
  the cycle right for a chord captured from the keyboard. `rootPosition` **collapses a
  repeated pitch class**, which two hands on the keybed produce constantly: keeping it read
  the chord back in the wrong register (so every press climbed an octave until the chord left
  the keyboard) and let an inversion stack one copy onto the other, which is the same MIDI
  note twice on one pad. No arrangement of a doubled note survives the walk - the last
  inversion of a doubled root *is* root position an octave up - so the double goes once, on
  the first press. `ChordPads::rewritePadChord` is the one way any of them writes: it goes
  through `holdArpChordFromPad` for a chord held into the arp and `pressChordPad` for one left
  ringing, so every note-on gives its reference back before the new one takes it. In that
  order, and with **Exclusive** on only the hold is restored: both calls choke every chord
  source, so doing both meant firing, killing and firing again, two strum rolls apart, and
  ending in neither state. A locked pad accepts all three, deliberately: a lock protects a
  chord from *generation*, not from its owner. The **card being edited** does not: all three
  grey out while `slot == editingSlot`, because they write the stored chord and cannot reach
  the keybed, and the edit link would write the un-shifted set back on the next latched note.
- **Fill**, **Regen** and **Generator** are three 24 px chips at the **right end of the Pads
  bar**, with **Key** beside them as a fourth control, a 24 px combo box: the bulk actions and
  the one generator setting worth reaching for while a page is being auditioned, one click to
  open and one to pick. **Mode** and **Scale Compliance** sat beside Key here too until
  2026-08-02, when Owen asked to "remove the scale and percentage and letter b from pads
  header" - both are still in the generator's own window, which holds every setting it has, so
  nothing on the bar stands in for them now. **The bar is the fast path and the window is the
  complete one**, and Key still has one parameter under both, so neither place has to know the
  other exists; it is a plain `ComboBoxAttachment`, holding the same set of values in both
  places. **`keys::StepComboBox` existed for Compliance alone**: the parameter is a continuous
  0-100, the window's slider steps by 1, and the bar used to offer five - so the bar showed the
  step nearest the value, and a `ComboBoxAttachment` finishes through `ComboBox::setSelectedId`,
  which returns early when the id has not moved, so picking the step already showing wrote
  nothing there - a dead click on a lit control. `StepComboBox` overrode `showPopup()` to
  report every pick regardless, with a plain `juce::ParameterAttachment` reading the parameter
  back onto it. That wiring left with Compliance; `StepComboBox.h` is unused now, kept for
  whichever future control needs the same trick (see **Files** above). All four remaining
  controls stay live with the Pads section folded, so folding the strip cannot take the card
  menu and the bar together, which would be the whole generator. **The Pads bar answers for its
  own width now, and so does every other one**: `minWidthForView()` is no longer a hand sum
  ending in a literal (1070, then 1280) but the max over each bar's own `contentWidth()` - the
  `controlsbar` / `arpbar` / `padbar` / `keyboardbar` namespaces at the head of
  `PluginEditor.cpp`, off the same constants `resized()` spends, with Controls measured at its
  *tight* cell set because that is the layout it has to be able to reach - together with
  `ArpPanel::minPanelWidth()` plus the editor's two margins, and a 1320 shipped-floor term.
  The floor stays a term rather than the answer so that the day a bar genuinely outgrows it,
  the bar wins instead of a literal. Two costs sit deliberately outside the Controls figure,
  the update button's 170 px and the Instrument chip, since most instances show neither.
  Keys Host asks for that number rather than copying it.
- **Clear page lives on a pad's card menu**, alone in a group at the foot of the right-click
  list. `KeysProcessor::clearChordPadPage()` empties every unlocked pad on the current page in
  one undo entry - see **Undo** below - which is what makes the gesture affordable: a click
  that could erase up to twelve live pads at once is one click back rather than gone for good.
  It is the only row on that menu that acts on anything but the card it was opened from, which
  is why it sits apart rather than beside **Clear pad**, whose name it would otherwise read as
  the plural of. It is deliberately not a method on `ChordGenMenu`: a page wipe is data work on
  the pad table and has no business living on the thing that generates chords. Per-pad clearing
  is still **Clear pad** on that pad's own menu or dragging its card off the strip, and the page
  can still be replaced wholesale, one pad at a time as it decides each, by **Regen** on the
  Pads bar.
- **Fill never overwrites** (2026-07-30, Owen: "new generations shouldn't overwrite
  existing"). `fillPage()` writes the *empty* pads and only those, locked or not - a blank
  needs no protection. `regeneratePage()` is the destructive one and the only one: it rerolls
  the pads that already carry a chord and skips the locked ones, which is what "regenerate"
  means and what the lock is for. They were one function with an `onlyUnlocked` flag, and the
  split is the point rather than a tidy-up: a flag on a shared path is exactly how the safe
  button ended up being the one that could lose sixteen chords. Each chip greys itself out
  when its list of targets is empty (`pageHasEmptyPads` / `pageHasRegeneratablePads`, polled
  from the editor's timer and from the panel's), so which of the two is which is readable
  without a tooltip.
- **The lock is an indicator on the card and an item on the menu, and nothing else**
  (2026-07-30, Owen: "I don't want the lock button to be visible. I only want it to be in right
  click"). A filled, locked card paints a 5 px dot in its top-right corner; an unlocked one
  paints nothing, and the card being edited paints nothing either, since the tick that ends the
  edit owns that end. A **clickable chip** occupied that corner for a few hours earlier the same
  day - `lockBadgeBounds` sized it to the card and `drawLockBadge` filled it - and it was
  removed at Owen's request: it took roughly a quarter of a docked card, and the click was
  tested ahead of every other branch in `mouseDown`, so that quarter answered neither play nor
  drag nor feed-the-arp. The whole card surface means the card again. This is a **closed
  owner-directed decision**; see the right-click exceptions in `CLAUDE.md` before reinstating a
  target there.
- **The lock stops every path that destroys a chord, not just the menu item.** "Clear pad" has
  always greyed on a locked card; **dragging one off the strip** cleared it anyway until
  2026-07-30, which is a wider gesture quietly overriding the item it sits beside. It now does
  nothing. The **drag itself is still allowed**, because `moveChordPad` swaps two slots and
  destroys nothing: a locked card still has to be arrangeable, and rearranging a page is not
  what a lock protects against. The card says which of the two it is doing - the drag ghost
  carries the same corner dot the card does, and fades to 45% once the pointer is over nothing,
  the spot where an unlocked card would be wiped.

Auditioning a chord reuses `pressChordPad` / `releaseChordPad`. It always did, and now there
is one card doing it rather than two.

## Undo

Content only: chord pads, arp lanes, arp slots. **Not parameters** - a knob you can always turn
back, and if every dial sweep filled the stack the pad you actually wanted would be pushed off
the end of it. That is a design decision, not a limitation to be lifted later.

An entry is a **snapshot of the affected subtree before the edit**, taken with the same
`chordPadsToTree()` / `arpToTree()` the session file uses, and restored with their `...FromTree`
twins wrapped back in a `KEYS` root. So no action has a hand-written inverse and none can have a
*wrong* one; anything added later is undoable the moment its data lands in one of those two
trees. `UndoScope` picks which tree an entry holds.

**One entry per gesture.** `pushUndo` at the gesture site, and `KeysProcessor::UndoGesture` as
an RAII guard that absorbs nested pushes - a drop that clears a pad and then sets it is one
entry, and a lane drag pushes on the press and never again. Without that a single stroke across
a lane buries everything under it. Depth 32, oldest dropped first; a new edit clears the redo
branch.

`undo()` and `redo()` call `stopAllChordPads()` before restoring, the same choke point an
audition uses and for the same reason: restoring pads can rewrite the chord a sustained card is
holding, and restoring the arp can rewrite the lanes under a running line.

`undoGeneration()` is a counter the editor polls, the `soundingGeneration()` pattern - undo
entries are created all over the UI, and one reader is far less to get wrong than every writer
remembering to call back.

## Take

Ableton cannot record a plugin's own MIDI onto that plugin's own track: Live records what
arrives at a track's *input*, and Keys' notes are made downstream of that, inside the plugin
itself, so arming the track and pressing record captures an empty clip. Keys keeps its own take
instead (`KeysProcessor::setRecording`, 2026-08-17).

`captureBlock` runs at the very end of `processBlock`, into a lock-free ring only the audio
thread advances, and captures the stream **leaving** the plugin - after the arp, after strum, on
whichever channel each line sent a note out on - so a take holds what you heard rather than what
you clicked. `recording` gates it, set from the message thread with a release store paired with
an acquire load so the audio thread never starts writing on a cursor a previous take left behind.
Arming freezes the tempo: `takeBpm` is read from `currentTempo()` once, in `setRecording(true)`,
and not touched again - the file is written once, at stop, and a host tempo drifting afterwards
would otherwise make every later preview disagree with the bytes already on disk.

Stopping writes the file immediately (`writeTake()`, into `KeysProcessor::takeFolder()`, kept as
a separate call from `setRecording` so the capture logic is testable without touching the user's
Documents folder), trimmed to the first captured **note** rather than the first event - Keys' own
wheels emit CC and pitch bend onto the same stream, so a nudge before the first note would
otherwise become the take's zero. `buildTakeMidiFile` supplies a note-off for anything still
ringing at stop and writes a type-0 file at the frozen tempo; `takeNotes()` (`TakeNote`) is built
from that same written sequence rather than from the raw capture, so `TakePanel`'s preview cannot
disagree with what actually sits on disk.

REC and the **Last take** chip sit on the Keyboard bar, after Octave, and stay live through a
fold - a stop button that folded away mid-take would not be one. The chip opens `TakePanel` in
its own **Keys Take** window (`KeysEditor::setTakeWindowOpen`), offering Save as, Show in
Explorer and a direct drag onto a track - the last of those because Live's own clipboard will not
accept a paste from the Windows one, so a dropped `.mid` file is the only route a take has into a
clip. `tests/TakeTests.cpp` covers the trim to the first note, the frozen tempo, and the note-offs
supplied for anything still ringing when recording stops.

## Parameters and state

All settings are `AudioProcessorValueTreeState` parameters (`size`, `root`, `scale`,
`scaleLock`, `octave`, `channel`, `polyphony`, `sustain`, `latch`,
the Humanize set `humanize` / `humanizeVelMin` / `humanizeVelMax`, the
chord-pad settings `chordExclusive` / `chordStrum` / `chordStrumMax` / `chordStrumDir` /
`padPage`, the generator's `gen*` set, `genRoot`, `genMode`, `genOctave` / `genOctaveMax`
(a range since 2026-08-01), `genInv0`-`genInv3`, `genNotesMin` / `genNotesMax` (a 2-11
range, added 2026-08-01, replacing the three note-count tick boxes these numbers count
from), `genCompliance`, `genLockInfluence`, `genSmooth` (Smooth Voicing on screen since
2026-08-01, still `genSmooth` underneath, the parameter id did not move when the name
did, over all eight sources), `genMajMin` (Lean, -100..100, new the same day), the six
`genUseKey` / `genUseMode` / `genUseOctave` / `genUseNotes` / `genUseInversions` /
`genUseCompliance` toggles (new the same day, all default true), the knob row's
`faderCC1`-`faderCC8` CC assignments, `genSource` (the eight-way choice itself), the
Markov set `markovMode`, `markovTemp`, `markovLength`, and the five sources' own bands:
`genCircleDir`, `genPlrP` / `genPlrL` / `genPlrR`, `genProgression`,
`genPlaningDiatonic`. Negative Harmony has none, Key, Mode and Octave are all it reads,
and neither does Algorithmic any more now that Notes, Inversions, Compliance and Lock
Influence sit on the two fixed rows above every source's band.

The arp's own set is `arpOn`, `arpRate` / `arpRateFree` / `arpRateHz` / `arpDot` /
`arpTrip` (retained; folded into `arpTuplet` on every load by `migrateTuplet` since
2026-08-03) / `arpAnchor`, `arpDirection` (twelve shapes) + `arpPattern`, `arpOctaves`
(Repeats) + `arpDistance`, `arpOffset`, `arpSwing`, `arpLatch`, `arpRetrigger` +
`arpRetrigBars`, `arpGate`, `arpChance`, `arpVelRamp` + `arpRampBeats`, `arpHumanize`
(timing-only since 2026-08-02, when `arpHumanVel` took the velocity half), `arpLinkLanes`,
from 2026-08-01 `arpKeys` and `arpChannel`, and from 2026-08-02 `arpOctShift`, `arpVolume`
(retained; folded into its replacement on every load by `migrateVelTrim`), `arpHumanVel` and
`arpVelTrim`, and from 2026-08-03 `arpTuplet` (Straight / Triplet / 5-tuplet / 7-tuplet /
9-tuplet, the general form of the `arpTrip` toggle it retired) plus `arpHumanizeSpan` and
`arpHumanVelSpan` (the two range knobs' reach: originally how far under the knob a Humanize
draw could fall, so a range that travelled with its knob rather than "nothing up to the knob";
from 2026-08-19 the range knobs are centred on the knob's own value instead, opening equally
either side of it, and `arpHumanVelSpan` is registered but no longer read by the engine at all
- Vel's ring reads `arpHumanVel` directly - while `arpHumanizeSpan` still drives H.Time's ring,
default 100 = the whole scale) - every one of them appended.

**`arpTrackMidi` (2026-08-27) is appended after all of those and is not per line**: whether MIDI
arriving on the track reaches the arpeggiator at all. **Default false, which is a behaviour
change with teeth** - a new parameter is absent from every saved session, so every existing set
takes it and a clip that was driving an arp goes quiet until the **Track MIDI** chip on the arp
bar is switched on. It needs no migration for exactly that reason: there is nothing to convert,
only a default to accept. Per-line `arpKeys` keeps the other half of the question, whether the
*keybed* feeds that line, and still defaults on.
The six after
`arpChance` arrived on 2026-07-30 and are appended; the rate's two arrived the same day and
sit beside `arpRate` instead, which costs nothing, because what a session and an automation
lane follow is a parameter's string id and not its position (JUCE hashes that id for VST3).
What is load-bearing is what lives *inside* a parameter (a choice's list of values, an int's
range), and `arpRate`'s eleven divisions are byte-identical, so nothing about it moved.

**Three arp parameters are deliberately not per line**, because they are about the lines
together: `bpm` (the tempo they run at when there is no transport to follow), `arpQuantize`
(Launch Quantize - Off, or the boundary a chord card, a slot launch or a drag onto a line waits
for before it lands) and, from 2026-08-27, `arpTrackMidi`. A quantize setting per line would be
one more way for the lines to miss each other, which is the opposite of what it is for; and
Track MIDI is one door into the *instance*, where a door shut for A and open for C is not shut
- Scale Lock's own reasoning.

**That whole set exists four times**, once per arpeggiator line, since line D was appended
2026-08-19. `createLayout` calls `addArpLineParams` four times rather than writing it out four
times, so a control cannot exist on one line and not another and the ranges and defaults are
provably identical. **Line 0 registers under the bare ids above** - `arpRate` is line A's rate
and always was - and B, C and D take a digit: `arp2Rate`, `arp3Direction`, `arp4Gate`. That is
the entire session-compatibility story, and it is why the ids are built by
`KeysProcessor::arpParamId(line, suffix)` from one table (`arpParamSuffix`) shared by the
layout, the UI's attachments and the audio thread's cached pointers. The suffix strings *are*
the ids: renaming one loses that setting out of every saved session. `arpKeys` and
`arpChannel` are the only two an older session sees appear on line A, and both default to what
Keys did before there were lines. **Harm1, Harm1Chance, Harm2 and Harm2Chance are the newest
suffixes** (2026-08-19), appended after every other line parameter for the same reason the
rate's two units and the Vel/Humanize spans were: appending is the only direction that leaves
an id's meaning untouched for a session saved before it existed.

The audio thread never builds one of those ids. Each line caches a
`std::atomic<float>*` per parameter at construction (`ArpLine::param`, indexed by the
`ArpParam` enum); resolving twenty-six ids by string on every line every block would be dozens of
`juce::String` allocations a block on the one thread that may not allocate at all.

**The rate has two units.** `arpRateFree` picks between them and `arpRateHz` holds the
second: 0.03125 to 32 Hz, mapped exponentially rather than skewed, which is exactly what the
eleven divisions span at 120 bpm. In Hz the engine pins its own clock to 60 bpm, so one step
is one period and every quantity measured as a fraction of a step keeps its meaning with no
second code path; it reads nothing from the playhead there, and Dot, Tuplet and Anchor mean
nothing without a beat to subdivide or a bar to anchor to (the panel greys all three). See
`docs/ARP_DESIGN.md`.

Neither parameter exists in a session saved before that day, and **an absent parameter is
not a reset**: APVTS creates the child on the spot and flushes whatever the live instance is
currently holding into it, so loading an old preset while the dial was in Hz left the arp
free-running under a panel showing a division. `migrateRateMode` reads the incoming tree,
spots the absence, and writes both defaults explicitly, which brings such a session back in
Sync. It loops over all four lines now, because B, C and D's rate parameters are absent from
every session saved before the lines for exactly the same reason and want exactly the same
repair. `migrateStrumRange` repairs the same shape for the strum band.

Two pieces of arp state are deliberately **not** parameters. Lane data and the twelve slots
live in the `arp` ValueTree beside the chord pads (they are arrays, not knobs); a slot
carries its chord, its shape and its rate, and since the rate gained a unit it carries that
and the Hz value too, or launching one would drop you into Sync at whatever division it
happened to hold. And the chain's running state is transient: it starts stopped, because a
session that reopens already playing a progression is a session that surprises you.

**All of that is per line too, and the tree says so by shape.** Line 0's twelve slots and its
live lanes sit directly on the `arp` node, exactly where they always have; B, C and D hang off a
`line` child each. So a session written here still loads into a build that predates the lines,
and — the point that matters — every session written *by* those builds loads here with no
migration at all: no `line` children means B, C and D keep their defaults, which with all three
switched off is precisely the arpeggiator that session was saved from.

`bpm` (40..240, default 120) was registered last, though it stopped being the newest when the
rate's two arrived, until `bpmSync` (`AudioParameterBool`, "Tempo Sync", default true) was
appended right after it 2026-08-02, so `bpmSync` holds that spot now. Last is tidiness and not
compatibility: JUCE derives a VST3 parameter's id by hashing its string id, so saved state and
existing automation follow that id rather than the position, and all a position still decides
is the order a host's generic list comes out in. `chordStrumMax` inserts mid-list regardless.
`bpm` is the tempo anything timed in beats
runs at when there is no host tempo to follow at all - in practice the standalone, which has no
playhead to ask - or whenever `bpmSync` is off; a host that reports a tempo wins while `bpmSync`
is on (the default), and the arp in Hz follows neither one. `bpm` replaced the arp's
last-known-host-tempo fallback, which nothing in the standalone could ever reach and nobody
anywhere could change; `bpmSync` is the opt-out from the host tempo that `bpm` alone never had.
It is threaded into `ArpEngine::Params::followHost` and `KeysProcessor::advanceChainClock`
(the progression chain reads the same escape hatch), and `migrateBpmSync` backfills a session
saved before it to the default, the same shape `migrateRateMode` uses for the rate's own two
appended parameters above.

**A host that is *playing* only had to win until 2026-08-16** (Owen: "bpm isn't syncing with
daw"). Both call sites used to gate the tempo itself on `clock.playing`, on top of `bpmSync`
being on - so a host's own tempo, set up and sitting still before the transport ever rolled,
was ignored, and Keys held its own `bpm` for exactly as long as you were setting up, which is
when you look at it and notice the disagreement. `clock.playing` came back out of both tests;
what still reads it is the *position* each of them advances alongside the tempo (`ppq`), because
a position genuinely means nothing while the transport is stopped, only the tempo does not.
`ArpEngine::HostClock` gained its own `hasBpm` flag for the tempo half of this rather than a
`bpm > 0` test on the struct's existing field: `HostClock::bpm` defaults to **120, not 0**, so a
`> 0` test would have read that default as a real host answer and, in the standalone especially,
quietly stopped following the `bpm` parameter it was supposed to fall back to.

A growing set is **registered but no longer read**, kept only so a session (and any host
automation) saved with them loads without error: `surface`, `uiLayout`, `padChannel`,
`xyCCX` / `xyCCY` from the old five-tab arrangement, and `velocity`, `curve`,
`humanizeTime` from the controls this branch retired. Adding to that list is the standing
convention here: an id nothing registers is an id nothing can load, so removing a parameter
outright orphans whatever a project already automated onto it and drops the value out of
every session that held one. `latch` came back off the list on 2026-07-30, which is the
other reason to keep dead parameters registered: a retired control is sometimes only resting.

The folding layout (which of the four sections are open, whether the knobs and the wheels
are, and where each detached window was left) and the instance's accent colour are **not**
parameters: they change no note, and
exposing them to automation would only add ways to break a session. They live in
`KeysProcessor::LayoutState` and ride along in the session tree. The Mod and
Pitch wheels, knob positions, and the Markov Mood and
Start pickers are transient performance controls with no parameters (they don't
persist); Pitch glides back to centre over ~160 ms on release, and the wheels and
knobs move by relative drag only (no click-jump), Octavium's deliberate feel.

The settings menu (2026-08-17, reached from the gear on the Controls bar) is four more
`LayoutState` fields for the same reason: `holdVisualsOnSustain`, `dragWhileSustain` and
`sustainProposesChords` decide how the keybed paints and what the live card reads, never a
note, and `uiScalePercent` persists a chosen zoom level that nothing yet reads back into the
window (it was built and left unwired rather than shipped half-working: see CLAUDE.md). All
four are absent-means-default on load, the same shape every other `LayoutState` flag already
uses. `NoteSurface::proposedChordNotes(sustainProposesChords)` is the one function the live
card and the keyboard-edit link now call instead of `soundingOutputNotes()`, so a note held
only by the pedal stops rewriting the proposed chord when the flag is off (the default).

The editor binds controls
with attachments — except the two two-handle range sliders (velocity, strum), which take
no attachment (two values each) and are synced to their pair of params by hand.
A 30 Hz timer pushes derived
config into every note surface and the live chord into the pads. `getStateInformation` /
`setStateInformation` persist the APVTS via `okstudio::state`, plus the captured chord
pads as an extra state tree (notes, name, lock, the generator metadata a pad carries,
and the Markov `numeral` when it has one),
so the whole setup saves with the DAW session. Pads saved before the generator existed
load fine: the missing metadata reads back as -1, which means "hand-captured", and the
suggestion menu works the chord out from its notes instead.

## Editor

`KeysEditor` owns the controls, the knob row (the bottom band of the Controls section,
`knobRowH` 110, which is what makes each knob 60 px square), the playing surface, the
`ChordPads` rows, the `ChordGenMenu` and the update
button. It sets the shared `LookAndFeel` (retinted locally toward Octavium's neutral
grey), wires the playing surface and the pads to `KeysProcessor::baseVelocity01` (the
midpoint of the velocity range), pushes the surface's sounding notes into the pads
each timer tick, panics the surface on a channel change (so notes can't strand),
animates the pitch wheel home after release, and on construction fires
`okstudio::updater::checkAsync`; if a newer signed
release exists, a one-click "Update to vX.Y.Z" button appears. The wheels column shows
next to the playing surface, as in Octavium, unless the Keyboard bar's Wheels chip
folds it away.
