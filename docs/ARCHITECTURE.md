# Architecture

Keys is a small JUCE plugin. It makes no sound; it turns mouse gestures on an
on-screen piano into MIDI. Everything shared with the rest of the line comes from
[`../okstudio-juce-kit`](../../okstudio-juce-kit).

## Files

```
src/
├── PluginProcessor.{h,cpp}   # AudioProcessor: params, MIDI output, chord pads, state
├── PluginEditor.{h,cpp}      # controls + surface tabs + layout + updater button
├── NoteMath.h                # pure note resolution (snap + transpose), unit-tested
├── Chords.h                  # pure chord detector (names a note set), unit-tested
├── ScaleModes.h              # 12 modes + a chord quality per degree; Feel presets
├── ChordGen.h                # pure chord generation (weighted pool), unit-tested
├── ChordSuggest.h            # pure "what could follow this chord", unit-tested
├── ChordMarkov.h             # pure Markov progression source, unit-tested
├── MarkovData.h              # the bundled progression corpus ChordMarkov walks
├── ArpEngine.h               # pure arpeggiator core, unit-tested; the one playhead
│                             # reader in Keys (docs/ARP_DESIGN.md)
├── ui/
│   ├── NoteSurface.{h,cpp}   # shared note bookkeeping every playable surface derives
│   ├── PianoKeyboard.{h,cpp} # the piano surface (geometry + paint over NoteSurface);
│   │                         # built by Keys and Keys Host
│   ├── KnobBank.{h,cpp}      # eight assignable CC rotary knobs above the playing surface
│   ├── CCMenu.h              # the one-click CC picker the knob row uses
│   ├── ChordPads.{h,cpp}     # chord-pad rows + live chord card (capture / recall)
│   ├── ChordGenPanel.{h,cpp} # the chord generator centre view (algorithmic + Markov)
│   ├── ArpPanel.{h,cpp}      # the arp centre view: Shape gates a tabbed lane editor
│   ├── SectionBar.h          # the fold/unfold header above a section of the editor
│   ├── KeyboardWindow.h      # the keybed popped out into its own resizable window
│   └── KeysLookAndFeel.{h,cpp} # the skin: tokens, raised fills, accent glow
├── host/                     # Keys Host only (docs/KEYS_HOST_DESIGN.md)
│   ├── KeysHostProcessor.{h,cpp} # KeysProcessor + one hosted instrument VST3
│   └── KeysHostEditor.{h,cpp}    # top bar, instrument picker, floating instrument window
└── mcp/
    └── KeysMcp.{h,cpp}       # MCP tool registrations; every handler runs on the
                              # message thread (docs/MCP.md)
```

## Threading: UI → audio note path

The keyboard runs on the message (UI) thread. It must not write to the outgoing
`MidiBuffer` directly. Instead:

- `PianoKeyboard` calls `KeysProcessor::noteOn/noteOff/allNotesOff`.
- Those build a `juce::MidiMessage`, stamp it with `Time::getMillisecondCounterHiRes()`,
  and hand it to a `juce::MidiMessageCollector`.
- `processBlock` calls `collector.removeNextBlockOfMessages(midi, numSamples)`, which
  drops the queued events into the block at the right offsets. Any MIDI already on
  the track (a clip, another device) passes through untouched.

The audio thread does nothing else: `buffer.clear()` (silence) then drain the
collector. No allocation, no locks.

## Playing surface: one view, the piano

Keys is one view, no tabs: header controls, the knob row, the chord-pad strip, then
the playing surface — `KeysEditor` builds a `PianoKeyboard`, which derives from
`NoteSurface`. There used to be five tabbed surfaces (Keys/Hex/Pads/Faders/XY,
switched by a `surface` parameter); the Pad Grid was cut outright (drums belong to
Beatform), the Faders and XY surfaces were replaced by the knob row below, and the
Hex surface moved out to its own repo (`../Hex`) along with Hex Host. The `surface`,
`uiLayout`, `padChannel`, and `xyCC*` parameters are still registered so old sessions
load without error, but nothing in the UI reads them any more.

## Note bookkeeping: one union, one diff (NoteSurface)

Chords and holds make "which notes should sound" non-trivial. `NoteSurface` keeps
three sets of **drawn** ids (a MIDI note for the piano, a cell index for the grids):

- `pressed` — under the active mouse gesture (a click, or the current key in a glide),
- `latched` — toggled on in Latch mode, or by right-click,
- `sustained` — captured by the Sustain pedal when the mouse released.

`refresh()` computes `want = pressed ∪ latched ∪ sustained`, diffs it against
`sounding` (drawn id → the output MIDI note currently on), and emits exactly the
delta: note-offs for keys that left `want`, note-ons for keys that entered it. Every
gesture mutates the sets then calls `refresh()`, so notes never double-fire or stick,
and panic is just "clear all three, refresh". When a polyphony limit is set, `refresh()`
first steals the oldest voices (FIFO `voiceOrder`) until `want` fits the cap. Changing
MIDI channel panics, so a note can't stick on the channel it was played on.

**Right-click latch** is an optional accelerator on every note surface (the on-screen
Latch toggle remains the left-click path, per the accessibility contract): it toggles
the drawn id in the same `latched` set, so Latch-off and panic clear it. Octavium kept
right-click latches in a private set no panic ever cleared; that was its worst stuck-
note bug, not a behaviour to keep.

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

`KeysProcessor::noteOn` applies Humanize when it is on: it draws a uniform-random
velocity in `[humanizeVelMin, humanizeVelMax]` per note and adds a random `0..humanizeTime`
ms to the message timestamp, so simultaneous notes spread. Note-offs are never delayed,
so a note can never release before it sounds. A `juce::Random`, touched only on the
message thread, drives it.

## Chord pads

Pads live in the processor (`ChordPad`), so they persist and keep
sounding independent of the editor; `ChordPads` is just the view. They are arranged as
four pages of sixteen (`padsPerPage` × `numPadPages`, Octavium's 4x4 per page; the
strip draws each page as two rows of eight). The strip shows the page `padPage`
selects and indexes by **absolute slot**, so a chord left ringing on another page keeps
sounding and a drag can't land on the wrong pad. Sessions saved when pages held eight
carry a `padsPerPage` marker (absent = 8) and each slot is re-based on load, so every
pad stays on the page it was on. Build a chord on the
keyboard (Latch), drag the live card onto a pad to `setChordPad` the sounding notes
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
and the **Keyboard** (with the wheels as a sub-fold). `SectionBar` is the affordance — a
`juce::Button` so the mouse-only contract and the accessible name come for free, with the
section's own small controls laid out as its siblings in `contentArea()`.

The middle of the editor is a *view*, not a stack of overlays. `Perform` is the knob bank
plus the chord-pad strip; `Chords` and `Arp` swap `ChordGenPanel` / `ArpPanel` into the
same slot, and the three tabs ride on the centre's own `SectionBar`. Only the view on show
exists — both panels are heavy and neither is worth keeping warm behind the other, and a
folded centre holds neither.

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
re-parent into a `KeyboardWindow`. Detached, `PianoKeyboard`'s 185 px key-height cap comes
off: dragging that window is meant to resize the keys, which is the whole point of the
feature for a player working with one mouse. The window borrows the holder and owns
nothing, so `~KeysEditor` tears it down explicitly before anything else.

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
`scaleLock`, `octave`, `channel`, `velocity`, `curve`, `polyphony`, `sustain`, `latch`,
the Humanize set `humanize` / `humanizeVelMin` / `humanizeVelMax` / `humanizeTime`, the
chord-pad settings `chordExclusive` / `chordStrum` / `chordStrumDir` / `padPage`, the
generator's `gen*` set — `genRoot`, `genMode`, `genOctave`, the note-count and inversion
toggles, `genCompliance`, `genLockInfluence` — the knob row's `faderCC1`-`faderCC8`
CC assignments, and the Markov set `genSource`, `markovMode`, `markovTemp`,
`markovLength`. `surface`, `uiLayout`, `padChannel`, `xyCCX` / `xyCCY` are also still
registered — they named the old five-tab arrangement — but are dead weight now, kept
only so a session saved with them loads without error. The Mod and
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
grey), wires the playing surface and the pads to `KeysProcessor::baseVelocity01` (velocity
slider through the curve), pushes the surface's sounding notes into the pads
each timer tick, panics the surface on a channel change (so notes can't strand),
animates the pitch wheel home after release, and on construction fires
`okstudio::updater::checkAsync`; if a newer signed
release exists, a one-click "Update to vX.Y.Z" button appears. The wheels column
always shows, next to the playing surface, as in Octavium.
