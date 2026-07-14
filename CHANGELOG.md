# Changelog

All notable changes to Keys are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/); versions are semver.

## [Unreleased]

## [0.1.0] - 2026-07-14

### Added
- Initial Keys plugin: mouse-only playable MIDI keyboard, VST3 + Standalone.
- On-screen piano at 25 / 49 / 61 / 73 / 76 / 88 keys; click to play, drag to glide.
- **Latch** (toggle notes on/off to hold a chord) and **Sustain** (pedal) as
  on-screen toggles, not modifier keys.
- **Scale Lock**: snap played notes to the nearest note in a chosen root and scale;
  out-of-scale keys dimmed.
- Octave shift (-3..+3), velocity with Soft / Linear / Hard curve, MIDI channel 1–16.
- **Humanize**: each note gets a random velocity within a Min/Max range plus a
  micro-timing offset, an on-screen toggle, so latched or dragged chords feel played
  rather than quantized. With the pedal down, a glide leaves a sustained trail.
- **Chord pads**: build a chord (Latch on, click the notes), drag the live chord card
  onto one of eight pads to capture it (auto-named, e.g. `Cm7`), then click a pad to
  play or stop it. Drag a pad to rearrange, or off the row to clear. Exclusive mode
  chokes the previous chord; pads persist with the session. A Strum control (Octavium
  "Drift") spreads a pad's notes over 0-200ms, Up / Down / Random.
- Humanize velocity is a two-handle Min/Max range slider (was two separate sliders).
- **All Off** panic across every channel.
- State (size, scale-lock, root, scale, octave, channel, velocity, curve, sustain,
  latch) persists with the DAW session.
- Fail-closed auto-updater from the shared kit (pinned OK Studio EV cert).
- Build script, Inno Setup installer, and CI (Windows + macOS) off the OK Studio line.
- Unit tests (JUCE UnitTest + ctest) for note resolution; CI builds and runs them.
- `src/NoteMath.h`: note-resolution logic factored out of the keyboard widget so it
  is unit-testable without a UI.

[Unreleased]: https://github.com/owenpkent/Keys/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/owenpkent/Keys/releases/tag/v0.1.0
