# Controls

Every control is operated with a single left-click, a drag, or the scroll wheel.
Nothing needs the keyboard, a right-click, a double-click, or a modifier key.

## The keyboard

| Gesture | Result |
|---------|--------|
| Click a key | Play that note (velocity from the Velocity control + Curve) |
| Click and drag | Glide across keys; the previous note releases as the next sounds (monophonic) |
| Click a **C** | Every C is labelled (C1, C2, …) to help you orient |

Chords come from Latch or Sustain, below — a single mouse can't hold several keys at
once, so those are how you stack notes.

To the **left of the keyboard** are two performance wheels: **Mod** sends CC1 and stays
where you leave it; **Pitch** bends and springs back to centre when you let go. Both are
transient (they don't save with the session) and send on the current MIDI channel.

## Top bar

| Control | Type | What it does |
|---------|------|--------------|
| **Size** | dropdown | 25 / 49 / 61 / 73 / 76 / 88 keys. The keyboard re-lays out immediately. |
| **Root** | dropdown | Tonic used by Scale Lock (C … B) |
| **Scale** | dropdown | Scale used by Scale Lock (Major, Natural/Harmonic/Melodic Minor, the modes, pentatonics, Blues, Whole Tone, Chromatic) |
| **Scale Lock** | toggle | On: each played note snaps to the nearest note in (Root, Scale); out-of-scale keys are dimmed so you see the shape. You cannot play a wrong note. |
| **Octave** | inc/dec | Transpose the whole keyboard, -5..+5 octaves. Click the arrows or scroll. |
| **Voices** | dropdown | Polyphony limit: **Off** (unlimited) or **1–8** notes. Playing past the limit steals the oldest note. |
| **Velocity** | slider | Base note velocity (1–127), used when Humanize is off. Click or scroll. |
| **Curve** | dropdown | Shapes the velocity response: **Soft** (reach high velocities easily), **Linear**, **Hard** (stays quiet until you push) |
| **MIDI Ch** | dropdown | Output channel, 1–16 |
| **Sustain** | toggle | On: notes keep sounding after you release the mouse, like a sustain pedal. With the pedal down a glide leaves a trail. Turn off (or click All Off) to release. |
| **Latch** | toggle | On: clicking a key toggles it on or off and holds it. Drag to paint several on. Build and hold a chord with one finger. |
| **Humanize** | toggle | On: each note gets a random velocity within the Humanize **Velocity** range plus a small timing offset, so repeats and chords don't sound machine-perfect. |
| Humanize **Velocity** | two-handle slider | The Min/Max velocity each note is drawn from when Humanize is on (shown as "Velocity 64–88"). Grab either handle. |
| Humanize **Timing** | slider | Micro-timing spread, 0–30 ms. |
| **Excl** | toggle | Exclusive chord mode: playing a chord pad chokes the previously-playing pad, so only one pad chord sounds at a time. |
| **Strum** | slider | Spread a chord pad's notes over 0–200 ms (a strum) instead of playing them together. |
| **Dir** | dropdown | Strum direction: **Up** (low→high), **Down** (high→low), or **Random**. |
| **All Off** | button | Panic. Stops every note on every channel. |
| **Update to vX.Y.Z** | button | Appears only when a newer signed release exists. One click downloads, verifies, and launches the installer. |

## Notes on Sustain vs Latch

- **Sustain** is momentary-feeling: notes you play while it's on are caught and held;
  turning it off releases everything it caught.
- **Latch** is a toggle set: each key you click flips on or off and stays. Turning
  Latch off clears the latched notes.

Both persist with the DAW session, along with every other control here.

## Chord pads

A row of eight pads and a live chord card sit between the controls and the keyboard.
They let you keep a palette of chords a single click away.

1. **Build a chord.** Turn **Latch** on and click the notes you want. The card names
   the chord it hears (for example `Cm7`).
2. **Capture it.** Drag the card onto a pad. The pad stores that chord, auto-labelled.
3. **Play it, beat-pad style.** Press and hold a filled pad to sound its chord; release
   to stop. Turn **Sustain** on to keep it ringing after you let go, and **Excl** on so
   a new pad chokes the previous chord.
4. **Rearrange or clear.** Drag a pad onto another to move it, or drag a pad off the
   row to empty it.

Pad chords play through the same output as the keys, so **Humanize** gives each chord
tone its own velocity and the **Strum** control spreads them into a strum. The pads
save with the DAW session.
