# Changelog

All notable changes to Keys are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/); versions are semver.

## [Unreleased]

### Added
- **Chord generator** (`Chords` button): fills the chord pads for a key and mode,
  ported from Octavium's Autofill and Options dialogs. Everything Octavium reached by
  right-click is an on-screen button, so it stays mouse-only.
  - 12 **scale modes** with a chord quality per degree (Ionian, Dorian, Phrygian,
    Lydian, Mixolydian, Aeolian, Locrian, Harmonic/Melodic Minor, Blues, and both
    pentatonics), each showing the character it carries.
  - 10 **Feel presets** (Happy, Sad, Dreamy, Dark, Jazzy, Bluesy, Epic, Chill,
    Mysterious, Smooth): one click sets the generator's key and mode *and* moves Root
    and Scale to match, so Scale Lock agrees with the pads.
  - **Scale Compliance** (0-100%): how far outside the key the generator may reach.
    100% stays diatonic; lower opens up modal interchange, then secondary dominants,
    then any chromatic root.
  - **Lock** a pad to keep it through a regenerate. **Lock Influence** (0-100%) steers
    new chords toward the character of what you locked.
  - **New** gives a pad a different chord for the same scale degree; **Next** offers
    chords that could follow it (Neo-Riemannian P/L/R/N/S/H, circle-of-fifths moves,
    diatonic degrees, and chromatic substitutions) and drops the pick into the next
    free pad.
  - Note-count filter (triads / 7ths / 9ths), inversions (root / 1st / 2nd / 3rd),
    and a generator octave. Press a chord in the grid to audition it.
- **Chord pad pages**: four pages of eight pads (was a single row of eight), with
  `<` / `>` navigation. Chords left ringing on another page keep sounding.
- **All Off** flashes blue when clicked, matching Octavium.

### Fixed
- **All Off** left the chord pads drawn as active: it silenced the notes but never
  cleared the pads' own state.
- Chord pads ignored the **Voices** limit entirely, since voice stealing lived only in
  the keyboard. A pad now drops its highest notes to fit the cap, keeping the lowest.
- The chord generator's diatonic tier is genuinely diatonic. Octavium offers Sus2 and
  Add9 on every major/minor degree without checking the added note is in the key (E
  Sus2 in C major wants F#), so its "strictly diatonic" setting was not. Ported with
  that filtered, since Keys is built on never playing a wrong note.
- Regenerating a chord no longer breaks the note-count filter. Octavium drops the
  filter when a degree has no alternative, which could return a chord of the wrong size
  or the same chord it was asked to replace.

### Changed
- **Parameter layout changed**: 13 parameters added (`padPage`, and the `gen*` set for
  the generator). Sessions saved with 0.1.0 load and keep their settings; the new
  parameters take their defaults.
- Unit tests now print their failures. JUCE's default logger writes to the debugger,
  so a failing test used to exit non-zero and say nothing.
- `src/ScaleModes.h`, `src/ChordGen.h`, `src/ChordSuggest.h`: generation and suggestion
  logic kept free of UI, so it unit-tests like `NoteMath.h` and `Chords.h`.

### Not ported
- Octavium's **MIDI Library** and **Markov** chord sources. Both read its external
  ~30 MB MIDI chord pack, which a plugin has no business shipping or hunting for on
  disk. The algorithmic source needs nothing and is the one here.

## [0.1.0] - 2026-07-14

### Added
- Initial Keys plugin: mouse-only playable MIDI keyboard, VST3 + Standalone.
- On-screen piano at 25 / 49 / 61 / 73 / 76 / 88 keys; click to play, drag to glide.
- **Latch** (toggle notes on/off to hold a chord) and **Sustain** (pedal) as
  on-screen toggles, not modifier keys.
- **Scale Lock**: snap played notes to the nearest note in a chosen root and scale;
  out-of-scale keys dimmed.
- Octave shift (-5..+5), velocity with Soft / Linear / Hard curve, MIDI channel 1–16.
- **Mod wheel** (CC1) and **Pitch bend** wheels left of the keyboard; the pitch wheel
  springs back to centre when released.
- **Polyphony** limit (Off / 1–8 voices) with oldest-note voice stealing.
- **Humanize**: each note gets a random velocity within a Min/Max range plus a
  micro-timing offset, an on-screen toggle, so latched or dragged chords feel played
  rather than quantized. With the pedal down, a glide leaves a sustained trail.
- **Chord pads**: build a chord (Latch on, click the notes), drag the live chord card
  onto one of eight pads to capture it (auto-named, e.g. `Cm7`), then play a pad
  beat-pad style: press to fire, release to stop (Sustain holds it). Drag a pad to
  rearrange, or off the row to clear. Exclusive mode
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
