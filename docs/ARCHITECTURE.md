# Architecture

Keys is a small JUCE plugin. It makes no sound; it turns mouse gestures on an
on-screen piano into MIDI. Everything shared with the rest of the line comes from
[`../okstudio-juce-kit`](../../okstudio-juce-kit).

## Files

```
src/
├── PluginProcessor.{h,cpp}   # AudioProcessor: params, MIDI output, chord pads, state
├── PluginEditor.{h,cpp}      # controls + layout + updater button
├── NoteMath.h                # pure note resolution (snap + transpose), unit-tested
├── Chords.h                  # pure chord detector (names a note set), unit-tested
└── ui/
    ├── PianoKeyboard.{h,cpp} # the playable keyboard widget
    └── ChordPads.{h,cpp}     # chord-pad row + live chord card (capture / recall)
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

## Note bookkeeping: one union, one diff

Chords and holds make "which notes should sound" non-trivial. The widget keeps three
sets of **drawn** key numbers:

- `pressed` — under the active mouse gesture (a click, or the current key in a glide),
- `latched` — toggled on in Latch mode,
- `sustained` — captured by the Sustain pedal when the mouse released.

`refresh()` computes `want = pressed ∪ latched ∪ sustained`, diffs it against
`sounding` (drawn note → the output MIDI note currently on), and emits exactly the
delta: note-offs for keys that left `want`, note-ons for keys that entered it. Every
gesture mutates the sets then calls `refresh()`, so notes never double-fire or stick,
and panic is just "clear all three, refresh". When a polyphony limit is set, `refresh()`
first steals the oldest voices (FIFO `voiceOrder`) until `want` fits the cap. Changing
MIDI channel panics, so a note can't stick on the channel it was played on.

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

Eight pads live in the processor (`ChordPad { notes, name }`), so they persist and keep
sounding independent of the editor; `ChordPads` is just the view. Build a chord on the
keyboard (Latch), drag the live card onto a pad to `setChordPad` the sounding notes
(named by `keys::chords::detect`), then click to `toggleChordPad` (play/stop, honouring
the `chordExclusive` choke). Playback reuses `noteOn`, so Humanize colours each tone,
and `chordStrum` / `chordStrumDir` order the notes and stagger their note-ons into a
strum (via `noteOn`'s `delaySeconds`). Detection (`Chords.h`) rotates a pitch-class set
over 12 candidate roots and scores each against a template library — root mandatory,
3rd and 5th omittable — so it is unit-testable with no UI.

## Parameters and state

All settings are `AudioProcessorValueTreeState` parameters (`size`, `root`, `scale`,
`scaleLock`, `octave`, `channel`, `velocity`, `curve`, `polyphony`, `sustain`, `latch`,
the Humanize set `humanize` / `humanizeVelMin` / `humanizeVelMax` / `humanizeTime`, and
the chord-pad settings `chordExclusive` / `chordStrum` / `chordStrumDir`). The Mod and
Pitch wheels are transient performance controls with no parameters (they don't persist);
Pitch springs back to centre on release. The editor binds controls
with attachments — except the two-handle velocity range slider, which has no attachment
(two values) and is synced to its two params by hand. A 30 Hz timer pushes derived
config into the keyboard and the live chord into the pads. `getStateInformation` /
`setStateInformation` persist the APVTS via `okstudio::state`, plus the captured chord
pads as an extra state tree, so the whole setup saves with the DAW session.

## Editor

`KeysEditor` owns the controls, the `PianoKeyboard`, the `ChordPads` row, and the update
button. It sets the shared `LookAndFeel` (retinted locally toward Octavium's neutral
grey), wires both the keyboard and the pads to `KeysProcessor::baseVelocity01` (velocity
slider through the curve), pushes the keyboard's sounding notes into the pads each timer
tick, and on construction fires `okstudio::updater::checkAsync`; if a newer signed
release exists, a one-click "Update to vX.Y.Z" button appears.
