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
│                             # (tests/ChordSourceTests.cpp). Wired to the UI since 2026-08-01:
│                             # Source is seven choices now (Algorithmic, Markov and these five)
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
│   ├── ChordGenMenu.{h,cpp}  # the chord generator's brain, all seven sources plus voice
│   │                         # leading. Draws nothing; a member of the editor, so it
│   │                         # outlives every view
│   ├── ChordGenPanel.{h,cpp} # a view onto it, the content of a window of its own. Built
│   │                         # when that window opens, destroyed when it closes
│   ├── SourceViz.{h,cpp}     # read-only diagram of the current source, under its button
│   │                         # row in that window (2026-08-01). Click-through, no state
│   ├── ChordTray.{h,cpp}     # 4x4 grid of candidate chords inside that window (2026-08-01),
│   │                         # plus ChordRefCard, one seed chord the tray's own actions cannot
│   │                         # touch; both belong to no pad and are thrown away when the
│   │                         # window closes
│   ├── ArpPanel.{h,cpp}      # the arp section: Shape gates a tabbed lane editor,
│   │                         # plus the control band and twelve launchable slots.
│   │                         # A/B tabs at the left of that slot row choose which of
│   │                         # the two lines everything on it edits; All is the macro view
│   ├── SectionBar.h          # the fold/unfold header above a section of the editor
│   ├── RangeSlider.h         # two-value slider whose band drags as one (velocity, strum)
│   ├── StepComboBox.h        # a combo that reports every pick, including one already
│   │                         # showing (the Pads bar's Scale Compliance steps)
│   ├── DetachedWindow.h      # a section popped out into its own resizable window (any
│   │                         # of them since 2026-07-27; also the generator's window)
│   └── KeysLookAndFeel.{h,cpp} # the skin: tokens, raised fills, accent glow
├── host/                     # Keys Host only (docs/KEYS_HOST_DESIGN.md)
│   ├── KeysHostProcessor.{h,cpp} # KeysProcessor + one hosted instrument VST3
│   └── KeysHostEditor.{h,cpp}    # top bar, instrument picker, floating instrument window
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
two lines now: it drains each line's own queue, hands the keybed's notes to the lines with
**Keys** on, runs **every** engine into its own buffer and merges them all back - the engine's
`enabled` flag gates only whether steps fire, so a line that is off still takes chords in and
holds them silently until you switch it on. Routing is
a queue per line rather than a per-pitch ownership mask, because a mask lets the message thread
clear a pitch's owner before the matching note-off is drained and strands that note in an
engine's held set forever. No allocation, no locks: every buffer is sized in `prepareToPlay`
and every parameter is read through a pointer cached at construction.

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
four pages of sixteen (`padsPerPage` × `numPadPages`, Octavium's 4x4 per page; drawn as two
rows of eight, each card carrying the chord's name and its notes underneath). The alternate
4x4 arrangement, a full card and a mini keyboard per pad, was the Pads section's **Big**
switch, and it went on 2026-07-31 once the note list fit under the name on the ordinary card
too. The strip shows the page `padPage`
selects and indexes by **absolute slot**, so a chord left ringing on another page keeps
sounding and a drag can't land on the wrong pad. Sessions saved when pages held eight
carry a `padsPerPage` marker (absent = 8) and each slot is re-based on load, so every
pad stays on the page it was on. Build a chord on the
keyboard (Sustain or right-click), drag the live card onto a pad to `setChordPad` the notes
(named by `keys::chords::detect`), then play it beat-pad style: mouse-down calls
`pressChordPad` (fire, honouring the `chordExclusive` choke) and mouse-up calls
`releaseChordPad` (stop, unless Sustain is holding it — the editor releases held pad
chords when the pedal lifts). Dropping a pad on the live card runs the other
direction: `onRecall` hands its notes to the active note surface, which latches the
drawn keys that produce them (`recallOutputNotes`), so a stored chord comes back for
editing. Playback reuses `noteOn`, so Humanize colours each tone,
and `chordStrum` / `chordStrumMax` / `chordStrumDir` order the notes and stagger their
note-ons into a strum: the two ends are a band, and each chord draws one spread from it
(via `scheduleNoteOn`, for the reason in **Scheduling** above), so repeated stabs do not
all rake at exactly the same speed. Detection (`Chords.h`) rotates a pitch-class set
over 12 candidate roots and scores each against a template library — root mandatory,
3rd and 5th omittable — so it is unit-testable with no UI.

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
which chord it is, so `ChordGenMenu::fitVoicing` now applies them as a post-pass over
whatever any of the seven sources produced. `fitPads` is the same pass for the Markov
path, which arrives as pads rather than `chordgen::Chord`s.

Growing a chord stacks further thirds **through the mode**, so an eleven-note chord is
still in key; shrinking one drops from the top, which keeps the root and the third (the
part that makes a chord recognisable) however far it shrinks. Inversions **replace** the
rotation a chord arrived in rather than compounding with it (root position first, then
invert), so ticking only "R" gives root position even from a source that had already
inverted one. `genNotesMin` / `genNotesMax` replaced the old 3/4/5 tick boxes with a
2 to 11 range, and `genOctave` / `genOctaveMax` do the same for register.

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

Sitting over all seven is **Smooth Voicing** (`genSmooth`, renamed from "Voice Leading"
2026-08-01, Owen: "I don't understand what the voice reading does"; the parameter id
did not move), a post-pass rather than a source of its own: each chord after the first
has each pitch class placed in whichever octave sits closest to the previous chord,
blended by the percentage. Blending in octave counts rather than raw semitones is
deliberate, two notes sharing a pitch class are always a whole number of octaves apart,
so every intermediate amount still lands on the source chord's own notes; it never
chooses which chords you get or which notes they contain, only which octave each note
sits in. `smoothPads()` runs it over the Markov path; the other six get it inline in
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
into a window of its own: **Controls** (the two header rows plus the knob bank under them,
which has its own Knobs sub-fold), the **Arp**, the **Pads**, and the **Keyboard** (with the
wheels as a sub-fold). It was six until 2026-07-30, when the centre view and Transcribe both
went; the centre's knob bank became the bottom row of Controls rather than a section of its
own, because it is two rows of settings and eight knobs, not a view you switch to.

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
**A**, **B** and **C** toggles — one per arpeggiator line, where a single **On** sat until
2026-08-01 — the **Hold off** chip and a **Detach**; everything but Detach survives folding
the panel away, because folding it destroys the view and never the arpeggiator, and a chord
held into a folded arp needs a way out that is still on screen (see `docs/ARP_DESIGN.md`).
**Hold off is deliberately still one button**: it releases every line and stops every
chain, because a per-line release would leave the other droning with nothing on a folded bar
to stop them. **All Off** beside it does that *and* switches the lines off, and **Light keys**
beside that is a display toggle. Which line the *panel* edits is chosen by the tabs at the left
of its slot row, and that same choice is mirrored by a letter chip on the Pads bar (below).

A **fourth tab, All**, is the macro view: the band and the step editor give way to three rows,
one per line, each with that line's switch, Latch and Keys, a detented rate knob, its shape and
eight knobs (Oct, Gate, Chance, Swing, Offset, Ramp, Time, Human), over a shared row holding the
BPM knob and Launch Quantize. It is a *view* rather than a fourth line - `editedLine` is
untouched by it, so a chord card click keeps one unambiguous target - and it takes the band's
space rather than adding to it, so the panel does not grow. Each row's attachments bind to its
own line for the row's life, where the band's rebind on every tab change: two lines on screen
at once cannot each be "the current line".

The **chord pads are a section of their own** too, below the arp. They used to live inside
the centre view, which meant the arpeggiator (the one panel whose whole job is to chew on a
chord) was also the one place you could not reach a chord. Their page buttons ride on the
Pads bar from the left, and the generator's **Fill**, **Regen** and
**Generator** chips, its **Key** / **Mode** / **Scale Compliance** combos and the arp's
**target-line** chip come off the right end (the pages hide with the strip, that whole
right-hand group never does). What a card click *means* is the arp's own On state
(`KeysProcessor::cardsFeedArp`, true when *any* line is on): with the arp running, a click
hands that chord over and leaves it there instead of playing it while the button is down, and
a click on the card *already* feeding the arp retriggers it.

**Which** line it goes to is the target chip: one letter, clicked to cycle A→B→C. It is the
same state as the arp panel's tabs, so either moves both, and it lives on the Pads bar because
it is a fact about the cards rather than about the arp — and because with the arp section
folded the tabs are off the screen while the cards are not. A card feeding a line wears that
line's letter inside its ring. Dragging a card onto an arp slot binds the chord there, and
onto a line tab hands it over immediately; both are mediated by the editor in screen
coordinates, the same machinery the audition tray needs, and both suppress the strip's
drag-off-to-clear so a gesture aimed at the arp can never delete a chord.

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

The keybed was the first to do this and keeps two extras. Detached, `PianoKeyboard`'s 185 px
key-height cap comes off: dragging that window is meant to resize the keys, which is the whole
point of the feature for a player working with one mouse. And the **Wheels** chip and a second
**Size** selector travel with it (`Section::travellers`), because they are the keybed's, not
the editor's. Every detached window carries the button that undoes the detach on a strip at
the top, so the control that re-docks a section is never in the window you are not looking at.

Controls that belong to the *editor* rather than to the content stay behind on the bar: the
arp's **A** / **B** / **C** line switches and **Hold off**, the pads' page buttons, the
generator's **Fill** / **Regen** / **Generator** chips and its three combos, the arp's
target-line chip beside them, the Controls bar's **Knobs** chip and theme swatch. Paging a strip that is off in a window of its own is one click either
way, so the pages are no more the content's than the swatch is. A bar whose section is away
says so, in the space its own controls did not use.

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
  candidates, wrong about who decides: sweeping Source to compare the seven of them threw the
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
  **pad in the main window** dropped on it - the latter is why `ChordPads` grew `onDragOutside`
  / `onDropOutside`: a drop on the reference card **copies**, and `onDropOutside` returning
  true suppresses the ordinary "drag off the strip clears the pad" behaviour, so reaching for
  the reference box can never delete the chord you were trying to keep. Left-click auditions it
  the same as a tray card. Beside it, **Similar** and **Could follow** call
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
  in the tray on top of a static figure of the shape - a circle-of-fifths wheel;
  the Neo-Riemannian P/L/R triangle with the transform sequence as chips; roman-numeral
  strips for Progressions and Markov; a mirror-axis clock for Negative Harmony;
  sliding note-stacks for Planing; degree columns for Algorithmic. It takes no input,
  writes no parameter and generates nothing - `setInterceptsMouseClicks(false, false)` in
  the constructor, same as every other click-through diagram in Keys - and its
  `preferredHeight()` is 112 px. Fed from the panel's 15 Hz timer, since everything it
  draws (source, key, the tray's contents) can move without the class being told.
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
- **The drag crosses two top-level windows**, which JUCE gives nothing for: a
  `DragAndDropContainer` only ever sees a drop inside its own window, and the tray lives in
  the generator's `DetachedWindow` while the pads live in the main editor or a `DetachedWindow`
  of their own. `ChordGenPanel::onCandidateDragOver` / `onCandidateDropped` /
  `onCandidateDragEnd` hand the editor a **screen** position - the one space the two windows
  share - and the editor forwards it to `ChordPads::externalDropSlotAt(screenPos)` /
  `setExternalDropSlot(slot)` / `dropExternalChord(screenPos, pad)`. The hit test is
  `juce::Desktop::findComponentAt`, so a generator window sitting over the strip reads as "not
  over a pad," and a folded Pads section finds nothing at all - occlusion is the target's
  problem, same as every other drag in Keys. A drop **refuses a locked pad** (the lock that
  protects a chord from generation protects it from a stray drag too) and calls
  `clearChordPad` before `setChordPad`, so a sounding or arp-held pad releases its old notes
  properly instead of having the chord swapped out from under them.
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
  bar**, and **Key**, **Mode** and **Scale Compliance** are three 24 px combo boxes beside
  them: the bulk actions and the settings that get changed while a page is being auditioned,
  one click to open and one to pick. **The bar is the fast path and the window is the complete
  one**, and there is one parameter under each pair, so neither place has to know the other
  exists. Key and Mode are `ComboBoxAttachment`s and hold the same set of values in both
  places. **Compliance is the one that reads differently in the two, on purpose**: the
  parameter is a continuous 0-100, the window's slider steps by 1, and the bar offers five
  steps - so **the bar shows the step nearest the value**, and at 60 it reads "50 %". That
  rounding is why this one box is *not* an attachment. A `ComboBoxAttachment` finishes through
  `ComboBox::setSelectedId`, which returns early when the id has not moved, so picking the step
  already showing wrote nothing and 50 was unreachable from the bar - a dead click on a lit
  control. It is a `keys::StepComboBox` instead, which overrides the virtual `showPopup()` and
  reports every pick; a plain `juce::ParameterAttachment` reads the parameter back onto it, and
  `setValueAsCompleteGesture` writes it as one begin/set/end so no pick can leave a host
  gesture open. All six controls stay live with the Pads section folded, so folding the strip
  cannot take the card menu and the bar together, which would be the whole generator. They cost
  the window no height and 502 px of bar (540 with the gaps between them), which is what moved
  `minWidthForView()` from 1010 to 1070 - a number Keys Host now asks for rather than copying.
- **Clear page is removed** (2026-08-01). It had already moved once, from a chip on the Pads
  bar to a button in the generator's window (2026-07-30), because it wiped every unlocked pad
  on the page with no `juce::UndoManager` anywhere in Keys to catch a slip. The tray gave the
  window a destructive action that costs nothing - **Clear**, on the tray's own header - and
  once that existed, a button that could still erase all sixteen live pads at once had no
  reason left to be a click away inside the same window. Nothing in Keys now empties a whole
  page in one gesture: per-pad clearing is still **Clear pad** on that pad's menu or dragging
  its card off the strip, and the page can still be replaced wholesale, one pad at a time as
  it decides each, by **Regen** on the Pads bar.
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
did, over all seven sources), `genMajMin` (Lean, -100..100, new the same day), the six
`genUseKey` / `genUseMode` / `genUseOctave` / `genUseNotes` / `genUseInversions` /
`genUseCompliance` toggles (new the same day, all default true), the knob row's
`faderCC1`-`faderCC8` CC assignments, `genSource` (the seven-way choice itself), the
Markov set `markovMode`, `markovTemp`, `markovLength`, and the five sources' own bands:
`genCircleDir`, `genPlrP` / `genPlrL` / `genPlrR`, `genProgression`,
`genPlaningDiatonic`. Negative Harmony has none, Key, Mode and Octave are all it reads,
and neither does Algorithmic any more now that Notes, Inversions, Compliance and Lock
Influence sit on the two fixed rows above every source's band.

The arp's own set is `arpOn`, `arpRate` / `arpRateFree` / `arpRateHz` / `arpDot` /
`arpTrip` / `arpAnchor`, `arpDirection` (twelve shapes) + `arpPattern`, `arpOctaves`
(Repeats) + `arpDistance`, `arpOffset`, `arpSwing`, `arpLatch`, `arpRetrigger` +
`arpRetrigBars`, `arpGate`, `arpChance`, `arpVelRamp` + `arpRampBeats`, `arpHumanize`,
`arpLinkLanes`, and — from 2026-08-01 — `arpKeys` and `arpChannel`. The six after
`arpChance` arrived on 2026-07-30 and are appended; the rate's two arrived the same day and
sit beside `arpRate` instead, which costs nothing, because what a session and an automation
lane follow is a parameter's string id and not its position (JUCE hashes that id for VST3).
What is load-bearing is what lives *inside* a parameter (a choice's list of values, an int's
range), and `arpRate`'s eleven divisions are byte-identical, so nothing about it moved.

**Two arp parameters are deliberately not per line**, because they are about the lines
together: `bpm` (the tempo they run at when there is no transport to follow) and `arpQuantize`
(Launch Quantize - Off, or the boundary a chord card, a slot launch or a drag onto a line waits
for before it lands). A quantize setting per line would be one more way for the lines to miss
each other, which is the opposite of what it is for.

**That whole set exists three times**, once per arpeggiator line. `createLayout` calls
`addArpLineParams` three times rather than writing it out three times, so a control cannot
exist on one line and not another and the ranges and defaults are provably identical. **Line 0
registers under the bare ids above** — `arpRate` is line A's rate and always was — and B and C
take a digit: `arp2Rate`, `arp3Direction`. That is the entire session-compatibility story, and
it is why the ids are built by `KeysProcessor::arpParamId(line, suffix)` from one table
(`arpParamSuffix`) shared by the layout, the UI's attachments and the audio thread's cached
pointers. The suffix strings *are* the ids: renaming one loses that setting out of every saved
session. `arpKeys` and `arpChannel` are the only two an older session sees appear on line A,
and both default to what Keys did before there were lines.

The audio thread never builds one of those ids. Each line caches a
`std::atomic<float>*` per parameter at construction (`ArpLine::param`, indexed by the
`ArpParam` enum); resolving twenty-six ids by string on every line every block would be dozens of
`juce::String` allocations a block on the one thread that may not allocate at all.

**The rate has two units.** `arpRateFree` picks between them and `arpRateHz` holds the
second: 0.03125 to 32 Hz, mapped exponentially rather than skewed, which is exactly what the
eleven divisions span at 120 bpm. In Hz the engine pins its own clock to 60 bpm, so one step
is one period and every quantity measured as a fraction of a step keeps its meaning with no
second code path; it reads nothing from the playhead there, and Dot, Trip and Anchor mean
nothing without a beat to subdivide or a bar to anchor to (the panel greys all three). See
`docs/ARP_DESIGN.md`.

Neither parameter exists in a session saved before that day, and **an absent parameter is
not a reset**: APVTS creates the child on the spot and flushes whatever the live instance is
currently holding into it, so loading an old preset while the dial was in Hz left the arp
free-running under a panel showing a division. `migrateRateMode` reads the incoming tree,
spots the absence, and writes both defaults explicitly, which brings such a session back in
Sync. It loops over all three lines now, because B and C's rate parameters are absent from
every session saved before the lines for exactly the same reason and want exactly the same
repair. `migrateStrumRange` repairs the same shape for the strum band.

Two pieces of arp state are deliberately **not** parameters. Lane data and the twelve slots
live in the `arp` ValueTree beside the chord pads (they are arrays, not knobs); a slot
carries its chord, its shape and its rate, and since the rate gained a unit it carries that
and the Hz value too, or launching one would drop you into Sync at whatever division it
happened to hold. And the chain's running state is transient: it starts stopped, because a
session that reopens already playing a progression is a session that surprises you.

**All of that is per line too, and the tree says so by shape.** Line 0's twelve slots and its
live lanes sit directly on the `arp` node, exactly where they always have; B and C hang off a
`line` child each. So a session written here still loads into a build that predates the lines,
and — the point that matters — every session written *by* those builds loads here with no
migration at all: no `line` children means B and C keep their defaults, which with both
switched off is precisely the arpeggiator that session was saved from.

`bpm` (40..240, default 120) is registered last, though it stopped being the newest when the
rate's two arrived. Last is tidiness and not compatibility: JUCE derives a VST3 parameter's
id by hashing its string id, so saved state and existing automation follow that id rather
than the position, and all a position still decides is the order a host's generic list comes
out in. `chordStrumMax` inserts mid-list regardless. It is the tempo anything timed in beats
runs at when there is no transport to follow, which is every
moment in the standalone and every stopped transport in a DAW; a host that is *playing*
still wins, and the arp in Hz follows neither. It replaced the arp's last-known-host-tempo
fallback, which nothing in the standalone could ever reach and nobody anywhere could change.

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
knobs move by relative drag only (no click-jump), Octavium's deliberate feel. The
editor binds controls
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
