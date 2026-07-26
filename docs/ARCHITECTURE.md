# Architecture

Keys is a small JUCE plugin. It makes no sound; it turns mouse gestures on an
on-screen piano into MIDI. Everything shared with the rest of the line comes from
[`../okstudio-juce-kit`](../../okstudio-juce-kit).

## Files

```
src/
├── PluginProcessor.{h,cpp}   # AudioProcessor: params, MIDI output, chord pads, state
├── PluginEditor.{h,cpp}      # folding sections, centre views, layout, updater button
├── NoteMath.h                # pure note resolution (snap + transpose), unit-tested
├── Chords.h                  # pure chord detector (names a note set), unit-tested
├── ScaleModes.h              # 12 modes + a chord quality per degree; Feel presets
├── ChordGen.h                # pure chord generation (weighted pool), unit-tested
├── ChordSuggest.h            # pure "what could follow this chord", unit-tested
├── ChordMarkov.h             # pure Markov progression source, unit-tested
├── MarkovData.h              # the bundled progression corpus ChordMarkov walks
├── ArpEngine.h               # pure arpeggiator core, unit-tested; the one playhead
│                             # reader in Keys (docs/ARP_DESIGN.md)
├── AudioCapture.{h,cpp}      # records from an audio input Keys opens itself, for the
│                             # Transcribe section. Built only with KEYS_TRANSCRIBE
├── ui/
│   ├── NoteSurface.{h,cpp}   # shared note bookkeeping every playable surface derives
│   ├── PianoKeyboard.{h,cpp} # the piano surface (geometry + paint over NoteSurface);
│   │                         # built by Keys and Keys Host
│   ├── KnobBank.{h,cpp}      # eight assignable CC rotary knobs above the playing surface
│   ├── CCMenu.h              # the one-click CC picker the knob row uses
│   ├── ChordPads.{h,cpp}     # chord-pad rows + live chord card (capture / recall)
│   ├── ChordGenPanel.{h,cpp} # the chord generator centre view (algorithmic + Markov)
│   ├── ArpPanel.{h,cpp}      # the arp section: Shape gates a tabbed lane editor,
│   │                         # plus the control band and twelve launchable slots
│   ├── TranscribePanel.{h,cpp} # the Transcribe section: input picker, waveform, piano
│   │                         # roll of the transcribed notes, MIDI drag-out
│   ├── SectionBar.h          # the fold/unfold header above a section of the editor
│   ├── RangeSlider.h         # two-value slider whose band drags as one (velocity range)
│   ├── DetachedWindow.h      # a section popped out into its own resizable window
│   │                         # (the keybed, and the arp; was KeyboardWindow.h)
│   └── KeysLookAndFeel.{h,cpp} # the skin: tokens, raised fills, accent glow
├── host/                     # Keys Host only (docs/KEYS_HOST_DESIGN.md)
│   ├── KeysHostProcessor.{h,cpp} # KeysProcessor + one hosted instrument VST3
│   └── KeysHostEditor.{h,cpp}    # top bar, instrument picker, floating instrument window
└── mcp/
    └── KeysMcp.{h,cpp}       # MCP tool registrations; every handler runs on the
                              # message thread (docs/MCP.md)
```

## Transcription

The Transcribe section records audio and turns it into notes. Three pieces, in a deliberate
order:

- **`AudioCapture`** owns a `juce::AudioDeviceManager` of its own and records to one
  preallocated mono buffer. Keys is an instrument: hosts send it MIDI and never audio, so
  there is no track input to record and opening a device directly is the only way in. That
  it is *our* device, not the host's, is what makes the section behave identically in the
  plugin and in the standalone, and it means choosing an input never touches the host's
  setup. The buffer is preallocated because the alternative is allocating on the device
  thread; recording stops itself at `AudioCapture::maxSeconds` rather than growing forever.
- **`okstudio::transcribe::Transcriber`** (the kit, `okstudio/Transcribe.h`) is the engine:
  basic-pitch, ported from NeuralNote. It lives in the kit because Undertow, Beatform and
  Contour would all have use for it. See the kit's `docs/TRANSCRIPTION.md`.
- **`TranscribePanel`** is the UI, and owns the threading. The model runs for a good
  fraction of the recording's length, so it runs on a `juce::Thread` and posts its notes
  back with `MessageManager::callAsync`; the keyboard keeps playing throughout. Moving the
  Sensitivity slider calls `retranscribe()`, which reruns only the note-event stage and is
  cheap enough to follow the slider live.

None of this touches the audio thread that `processBlock` runs on. The capture callback is a
*different* device's thread, and writes only to its own buffer.

The whole thing is behind `KEYS_TRANSCRIBE`, on by default. Off, `AudioCapture` and
`TranscribePanel` are not compiled, the section reports zero height, and the kit's engine is
never built — which also drops a multi-gigabyte ONNX Runtime download and the static MSVC
runtime it forces on the binary.

## Threading: UI → audio note path

The keyboard runs on the message (UI) thread. It must not write to the outgoing
`MidiBuffer` directly. Instead:

- `PianoKeyboard` calls `KeysProcessor::noteOn/noteOff/allNotesOff`.
- Those build a `juce::MidiMessage`, stamp it with `Time::getMillisecondCounterHiRes()`,
  and hand it to a `juce::MidiMessageCollector`.
- **One note-on per sounding pitch.** `noteRefs` counts how many sources own each pitch,
  and MIDI is emitted only on the 0→1 transition and released only on 1→0. Four sources
  can want the same pitch at once (a chord pad, the live chord card, a chord held into the
  arpeggiator, and the keybed); emitting a second note-on for a pitch already sounding
  means one source's release ends it for everybody, which left keys lit with nothing
  sounding and leaked the arpeggiator's held set. Any new chord source must go through
  these two functions rather than the collector.
- `processBlock` calls `collector.removeNextBlockOfMessages(midi, numSamples)`, which
  drops the queued events into the block at the right offsets. Any MIDI already on
  the track (a clip, another device) passes through untouched.

The audio thread does nothing else: `buffer.clear()` (silence) then drain the
collector. No allocation, no locks.

## Playing surface: one product, one piano

The **playing surface** is not a choice: `KeysEditor` builds a `PianoKeyboard` (which
derives from `NoteSurface`), and that is the only surface Keys and Keys Host ship. There
used to be five tabbed *surfaces* (Keys/Hex/Pads/Faders/XY, switched by a `surface`
parameter); the Pad Grid was cut outright (drums belong to Beatform), the Faders and XY
surfaces were replaced by the knob row, and the Hex surface moved out to its own repo
(`../Hex`) along with Hex Host. Their parameters are still registered — see **Parameters
and state**.

The tabs that exist now are a different thing: they pick which **centre view** occupies
the middle of the editor (Perform or Chords), not which surface you play on. See
**Folding layout, and the centre view**.

## Note bookkeeping: one union, one diff (NoteSurface)

Chords and holds make "which notes should sound" non-trivial. `NoteSurface` keeps
three sets of **drawn** ids (a MIDI note for the piano, a cell index for the grids):

- `pressed` — under the active mouse gesture (a click, or the current key in a glide),
- `latched` — toggled on by right-click (or by the forced latch during pad editing),
- `sustained` — captured by the Sustain pedal when the mouse released.

A left click on a key already in `latched` or `sustained` **releases** it. That is what
retired the Latch toggle: once a plain click both holds and releases, a whole mode for it
earned nothing. The `latch` member survives for pad editing, which still forces it on.

`refresh()` computes `want = pressed ∪ latched ∪ sustained`, diffs it against
`sounding` (drawn id → the output MIDI note currently on), and emits exactly the
delta: note-offs for keys that left `want`, note-ons for keys that entered it. Every
gesture mutates the sets then calls `refresh()`, so notes never double-fire or stick,
and panic is just "clear all three, refresh". When a polyphony limit is set, `refresh()`
first steals the oldest voices (FIFO `voiceOrder`) until `want` fits the cap. Changing
MIDI channel panics, so a note can't stick on the channel it was played on.

**Right-click latch** is an optional accelerator on every note surface, and the
accessibility contract is still satisfied because a left click releases what it holds:
right-click is never the only way out. It toggles the drawn id in the same `latched` set,
so panic clears it. Octavium kept right-click latches in a private set no panic ever
cleared; that was its worst stuck-note bug, not a behaviour to keep.

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
four pages of sixteen (`padsPerPage` × `numPadPages`, Octavium's 4x4 per page; the
strip draws each page as two rows of eight). The strip shows the page `padPage`
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
and `chordStrum` / `chordStrumDir` order the notes and stagger their note-ons into a
strum (via `noteOn`'s `delaySeconds`). Detection (`Chords.h`) rotates a pitch-class set
over 12 candidate roots and scores each against a template library — root mandatory,
3rd and 5th omittable — so it is unit-testable with no UI.

A pad fires as one gesture, so there is no "oldest" voice within it: `pressChordPad`
honours the Voices cap by dropping its highest notes and keeping the lowest, which is
how `refresh()` resolves a too-big simultaneous chord on the keyboard. The cap applies
per source — a pad and the keyboard each fit under it separately.

## Chord generation

`ChordGen.h` builds a **weighted pool** of candidate chords for a key and mode, then
samples it. That one idea carries both sliders in the generator:

- **Scale Compliance** decides which tiers enter the pool: diatonic (always, weight 1),
  then borrowed from parallel modes, then secondary dominants, then any chromatic root,
  each fading in as compliance drops and weighted by how far it opened.
- **Lock Influence** re-weights the whole pool toward the *families* (triad / seventh /
  sixth / add / extended) of the chords you locked — so regenerating keeps their
  character without copying them.

A fill seeds the plain diatonic chord on each degree in order, then weighted-samples the
rest and shuffles only that tail. Each chord remembers the `degree` it came from, which
is what lets **New** hand back a different chord *for the same degree*.

The modes (`ScaleModes.h`) are deliberately **not** the kit's `okstudio::scales`. That
table answers "is this note in the scale", which is all Scale Lock needs; generation also
needs a chord quality per degree. They stay separate rather than one pretending to be the
other, and `kitScaleIndex` pairs them by comparing intervals — so a rename on either side
cannot silently mis-pair them. That mapping is what lets a Feel preset move Root and
Scale along with the generator's own key.

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

All these headers are pure logic with no UI, so they unit-test like `NoteMath.h`.

## Folding layout, and the centre view

The editor is a stack of sections, each of which folds away so the window can be squeezed
small when the screen is busy: **Controls** (the three header rows), the **centre view**,
the **Arp**, the **Pads**, **Transcribe**, and the **Keyboard** (with the wheels as a
sub-fold).
`SectionBar` is the affordance — a `juce::Button` so the mouse-only contract and the
accessible name come for free, with the section's own small controls laid out as its
siblings in `contentArea()`.

**Transcribe** is the odd one out: it is the only part of Keys that consumes audio rather
than producing MIDI, and the only section whose panel owns a device. Like the arp's, its
panel is built when the section opens and destroyed when it folds — it holds an open audio
input and a neural network's weights, neither worth keeping warm behind a folded bar. See
"Transcription" below.

The middle of the editor is a *view*, not a stack of overlays. `Perform` is the knob bank
and `Chords` swaps `ChordGenPanel` into the same slot, with both tabs riding on the centre's
own `SectionBar`. Only the view on show exists — the generator builds sixteen chord cards
and is not worth keeping warm behind the knobs, and a folded centre holds neither.

The **arpeggiator is a section of its own** rather than a third centre view (changed
2026-07-25). Competing with the knobs and the generator was backwards for a panel that runs
while you play, and the arp is the one thing you want on screen *next to* a chord. Its bar
carries the **On** toggle and a **Detach**, so both survive folding the panel away: folding
it destroys the view, never the arpeggiator.

The **chord pads are a section of their own** too, below the arp, so they are on screen
under either centre view. They used to live inside Perform, which meant the arpeggiator —
the one panel whose whole job is to chew on a chord — was also the one place you could not
reach a chord. Their page buttons ride on the Pads bar, alongside the **To Arp** toggle
that turns a card click into "hand this chord to the arp and leave it there".

The bars are full-width translucent Buttons and the controls on them are *siblings*, not
children (a `SectionBar` has to stay clickable end to end). Z-order therefore matters:
they are sent `toBack()` after construction, or each bar paints over its own tabs.

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

This replaced full-editor overlays that dimmed and covered everything, including the
keyboard. Editing an arp while unable to play a note was backwards for an instrument you
perform. The panels keep an overlay mode (`setInlineMode(false)`) but nothing uses it.

`KeysEditor::idealHeight()` is the single source of truth for what the folds add up to;
`resized()` spends exactly the same constants, so the window a fold asks for and the
layout it gets cannot drift. Standalone and in a DAW the editor resizes itself to that
height; embedded in Keys Host it reports the number through `onIdealHeightChanged` and the
host grows to fit, because a tall centre view would otherwise push the keybed off the end.

The keybed and the wheels live inside one `KeybedHolder`, so **Detach** is a single
re-parent into a `DetachedWindow`. Detached, `PianoKeyboard`'s 185 px key-height cap comes
off: dragging that window is meant to resize the keys, which is the whole point of the
feature for a player working with one mouse. The window borrows the holder and owns
nothing, so `~KeysEditor` tears it down explicitly before anything else.

The arp plays the same trick through an `ArpHolder`, and `DetachedWindow` (which was
`KeyboardWindow` until the arp wanted it too) is parameterised by title and minimum size
for the two of them. A detached section contributes no height to the main window, so
`arpHeight()` returns 0 while it is out, exactly as the keybed does.

All of it (folds, current view, detached window bounds) is in `KeysProcessor::LayoutState`
rather than the editor, so it survives the window closing, and it is saved in the session
tree rather than as parameters: none of it changes a note, and exposing it to host
automation would only add ways to break a session.

## Chord generator panel

`ChordGenPanel` is a centre view, not a dialog: a plugin editor has no business opening OS
windows, and staying inside the editor keeps every target on the surface the mouse is
already in. It fills the **current page**, so the four pages can hold four different keys.

Its pad grid repeats the strip at full size on purpose. Everything Octavium reached by
right-click (lock, regenerate, suggest) is a real on-screen button here, and at strip
size those targets would be far under the 34 px minimum. Selecting the view grows the
editor to fit rather than shrinking them. Auditioning a chord in the grid reuses
`pressChordPad` / `releaseChordPad`, so it is the same code path as playing the strip.

## Parameters and state

All settings are `AudioProcessorValueTreeState` parameters (`size`, `root`, `scale`,
`scaleLock`, `octave`, `channel`, `polyphony`, `sustain`,
the Humanize set `humanize` / `humanizeVelMin` / `humanizeVelMax`, the
chord-pad settings `chordExclusive` / `chordStrum` / `chordStrumDir` / `padPage`, the
generator's `gen*` set — `genRoot`, `genMode`, `genOctave`, the note-count and inversion
toggles, `genCompliance`, `genLockInfluence` — the knob row's `faderCC1`-`faderCC8`
CC assignments, and the Markov set `genSource`, `markovMode`, `markovTemp`,
`markovLength`.

A growing set is **registered but no longer read**, kept only so a session (and any host
automation) saved with them loads without error: `surface`, `uiLayout`, `padChannel`,
`xyCCX` / `xyCCY` from the old five-tab arrangement, and `velocity`, `curve`, `latch`,
`humanizeTime` from the controls this branch retired. Adding to that list is the standing
convention here — removing a parameter outright would shift automation in projects that
already exist.

The folding layout (which sections are open, which centre view, the detached window's
bounds) and the instance's accent colour are **not** parameters: they change no note, and
exposing them to automation would only add ways to break a session. They live in
`KeysProcessor::LayoutState` and ride along in the session tree. The Mod and
Pitch wheels, knob positions, and the Markov Mood and
Start pickers are transient performance controls with no parameters (they don't
persist); Pitch glides back to centre over ~160 ms on release, and the wheels and
knobs move by relative drag only (no click-jump), Octavium's deliberate feel. The
editor binds controls
with attachments — except the two-handle velocity range slider, which has no attachment
(two values) and is synced to its two params by hand. A 30 Hz timer pushes derived
config into every note surface and the live chord into the pads. `getStateInformation` /
`setStateInformation` persist the APVTS via `okstudio::state`, plus the captured chord
pads as an extra state tree (notes, name, lock, the generator metadata a pad carries,
and the Markov `numeral` when it has one),
so the whole setup saves with the DAW session. Pads saved before the generator existed
load fine: the missing metadata reads back as -1, which means "hand-captured", and the
suggestion menu works the chord out from its notes instead.

## Editor

`KeysEditor` owns the controls, the knob row, the playing surface, the
`ChordPads` rows, and the update
button. It sets the shared `LookAndFeel` (retinted locally toward Octavium's neutral
grey), wires the playing surface and the pads to `KeysProcessor::baseVelocity01` (the
midpoint of the velocity range), pushes the surface's sounding notes into the pads
each timer tick, panics the surface on a channel change (so notes can't strand),
animates the pitch wheel home after release, and on construction fires
`okstudio::updater::checkAsync`; if a newer signed
release exists, a one-click "Update to vX.Y.Z" button appears. The wheels column
always shows, next to the playing surface, as in Octavium.
