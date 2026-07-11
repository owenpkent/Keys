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

## Top bar

| Control | Type | What it does |
|---------|------|--------------|
| **Size** | dropdown | 25 / 49 / 61 / 73 / 76 / 88 keys. The keyboard re-lays out immediately. |
| **Root** | dropdown | Tonic used by Scale Lock (C … B) |
| **Scale** | dropdown | Scale used by Scale Lock (Major, Natural/Harmonic/Melodic Minor, the modes, pentatonics, Blues, Whole Tone, Chromatic) |
| **Scale Lock** | toggle | On: each played note snaps to the nearest note in (Root, Scale); out-of-scale keys are dimmed so you see the shape. You cannot play a wrong note. |
| **Octave** | inc/dec | Transpose the whole keyboard, -3..+3 octaves. Click the arrows or scroll. |
| **Velocity** | slider | Base note velocity (1–127). Click or scroll. |
| **Curve** | dropdown | Shapes the velocity response: **Soft** (reach high velocities easily), **Linear**, **Hard** (stays quiet until you push) |
| **MIDI Ch** | dropdown | Output channel, 1–16 |
| **Sustain** | toggle | On: notes keep sounding after you release the mouse, like a sustain pedal. Turn off (or click All Off) to release them. |
| **Latch** | toggle | On: clicking a key toggles it on or off and holds it. Drag to paint several on. Build and hold a chord with one finger. |
| **All Off** | button | Panic. Stops every note on every channel. |
| **Update to vX.Y.Z** | button | Appears only when a newer signed release exists. One click downloads, verifies, and launches the installer. |

## Notes on Sustain vs Latch

- **Sustain** is momentary-feeling: notes you play while it's on are caught and held;
  turning it off releases everything it caught.
- **Latch** is a toggle set: each key you click flips on or off and stays. Turning
  Latch off clears the latched notes.

Both persist with the DAW session, along with every other control here.
