# Architecture

Keys is a small JUCE plugin. It makes no sound; it turns mouse gestures on an
on-screen piano into MIDI. Everything shared with the rest of the line comes from
[`../okstudio-juce-kit`](../../okstudio-juce-kit).

## Files

```
src/
├── PluginProcessor.{h,cpp}   # AudioProcessor: params, MIDI output, state
├── PluginEditor.{h,cpp}      # controls + layout + updater button
└── ui/PianoKeyboard.{h,cpp}  # the playable keyboard widget
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
and panic is just "clear all three, refresh".

## Note resolution: snap then transpose, remembered

When a key starts sounding, `outputNote(drawnKey)` applies scale-lock
(`okstudio::scales::snapToScale`) then the octave shift, clamped to 0..127. The
result is stored in `sounding[drawnKey]`. Note-off uses that stored value, so a note
turns off correctly even if you change octave or scale while it is held.

## Parameters and state

All settings are `AudioProcessorValueTreeState` parameters (`size`, `root`, `scale`,
`scaleLock`, `octave`, `channel`, `velocity`, `curve`, `sustain`, `latch`). The
editor binds controls to them with attachments, and a 30 Hz timer pushes the derived
config (range, scale-lock, sustain, latch) into the keyboard. `getStateInformation` /
`setStateInformation` persist the APVTS via `okstudio::state`, so the whole setup
saves with the DAW session.

## Editor

`KeysEditor` owns the controls, the `PianoKeyboard`, and the update button. It sets
the shared `LookAndFeel`, wires `keyboard.getVelocity` to the velocity+curve, and on
construction fires `okstudio::updater::checkAsync` with the Keys config; if a newer
signed release exists, a one-click "Update to vX.Y.Z" button appears.
