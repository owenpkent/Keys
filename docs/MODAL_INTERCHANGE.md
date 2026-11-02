# Modal interchange source design spec

Status: **proposed, unbuilt.** Written 2026-11-02, from item 1 of the ranked gaps in
`docs/CHORD_LANDSCAPE.md` section 6, with the dance case from `docs/DANCE_HARMONY.md` section 3.8
(Owen: "how do people find chords?", then choosing to design this one first).

## What is being proposed

A **ninth generator source, Borrowed**, appended to the `genSource` list. It writes a progression
in the session's key and mode, and swaps a chosen number of its chords for chords **borrowed from a
parallel mode**: the iv, bVI and bVII a major song steals from minor, the Dorian IV and the harmonic
V a minor song steals back. Every borrowed card says what it is (`bVI`) instead of printing nothing.

## Why a source, when the weighted pool already borrows

It is the first thing to know, and it is why this is a source rather than a tweak.

`ChordGen.h::buildPool` tier 2 is modal interchange already: under Scale Compliance 0.95 it adds
**every** chord of every mode in `modes::parallelModes(mode)` at weight `(1 - compliance) * 0.8`.
That is a blur, not a borrowing:

- **It cannot be aimed.** There is no way to ask for "one bVI, please". Turning compliance down to
  get borrowed chords also opens tier 3 (secondary dominants, under 0.70) and tier 4 (any chromatic
  root, under 0.40), so the more you want, the more you get things you did not ask for.
- **Most of what it adds is not borrowed.** Aeolian shares four of its seven triads with Ionian, so
  much of tier 2 is the diatonic pool again at a lower weight.
- **It has no position.** A borrowed chord means something *where it lands*: iv before I is the
  plagal sigh, bVII before I is the backdoor, bVI then bVII then I is the lift dance music runs on.
  The pool draws chords independently, so none of those moves is more likely than its reverse.
- **It cannot be read.** A borrowed chord resolves to no degree, so `ChordNumerals::forChord`
  returns empty and the card's corner is blank, by design (a `?` on every out-of-key card was worse).

The pool stays as it is. Borrowed is the deliberate version of the same idea.

## The design

### 1. Candidates are derived from `ScaleModes.h`, never typed in

A candidate is a degree of a parallel mode whose triad is **not** diatonic in the home mode. That
test runs on the mode tables at generation time, so a Mixolydian session never "borrows" the bVII it
already owns, and a mode added to `ScaleModes.h` borrows correctly with no second edit. (CLAUDE.md:
two tables that must agree will drift.)

The only hand-written table is a **weight per numeral**, because "which borrowings are common" is a
fact about music and not about scales. Unlisted numerals get a low default, so nothing is silently
excluded:

| Home | Numeral | From | Function | Weight |
| --- | --- | --- | --- | --- |
| Major | `iv` | Aeolian, Dorian | S | 1.0 |
| Major | `bVI` | Aeolian | S | 1.0 |
| Major | `bVII` | Aeolian, Mixolydian | D | 1.0 |
| Major | `bIII` | Aeolian | T | 0.6 |
| Major | `v` | Mixolydian | D | 0.4 |
| Major | `II` | Lydian | S | 0.4 |
| Major | `ii°` | Aeolian | S | 0.4 |
| Major | `bII` | Phrygian (Neapolitan) | S | 0.3 |
| Minor | `IV` | Dorian (the deep house chord) | S | 1.0 |
| Minor | `V` | Harmonic Minor | D | 1.0 |
| Minor | `ii` | Dorian | S | 0.5 |
| Minor | `bII` | Phrygian | S | 0.5 |
| Minor | `I` | Ionian (Picardy) | T, last chord only | 0.4 |

Numerals are in Keys' fixed major-scale convention (`datasets/README.md`, the trap), so `bVI` is
always 8 semitones up, in any mode.

`parallelModes()` today gives Ionian no Phrygian, so the pool can never offer a major song the
Neapolitan `bII`, and gives Dorian no Harmonic Minor. The Borrowed source uses its **own** From list (below) rather than editing
`parallelModes()`, because that function also drives the pool's tier 2 and changing it would move
every saved session generated in Weighted mode.

### 2. A progression with slots, so a borrowing lands where it means something

1. Lay down a diatonic skeleton of `count` chords as a function walk: T first, then S, D, back to
   T, repeating (`T S D T` for four, `T S S D T` style for longer). Chords come from the home mode.
2. Pick `Borrowed` of the slots and fill each with a candidate whose **function matches the slot**,
   drawn by weight. The Picardy `I` may only take the last slot.
3. Two cadences get a small bonus because they are the moves people borrow for: `bVI bVII I`
   (major) and `bVI bVII i` (minor), and `iv I` at the end.
4. Write `degree` for diatonic chords as every source does, and write `numeral` (the field only
   Markov writes today) for borrowed ones, so `forChord` prints `bVI` with no change to
   `ChordNumerals.h`.

Then the voicing pipeline runs as it does for every source (Lean, `fitVoicing`,
`applyVoiceLeading`), so note counts, inversions and smoothing need nothing new.

### 3. Two settings, both click-only

On the Borrowed band of the generator window, in the same place Planing's Diatonic tick sits:

- **From**: Auto, Aeolian, Dorian, Mixolydian, Lydian, Phrygian, Harmonic Minor, Ionian. Auto
  draws from every mode that yields a candidate. A combo, so one click opens it and one click picks.
- **Borrowed**: how many chords are borrowed, 1 to 4, on a `-`/`+` stepper pair at 34 px, never a
  slider alone (CLAUDE.md: a slider is a drag). Clamped to `count - 1`, so the tonic stays home.

Both follow the generator's gate rule: unticked means the generator rolls the setting itself.

That is **two appended parameters** (`genBorrowFrom`, `genBorrowCount`), so a **PARAMETER LAYOUT
CHANGE**: `StateTests`' golden list grows by two at the end, and the changelog says so loudly.

### 4. The diagram

`SourceViz` gets a `paintBorrowed`: the home mode's seven degrees as a row of cells, the From mode's
seven below it, and the borrowed degrees drawn lifted from the lower row into their slot above,
in the accent. Read-only, like every other source diagram.

## What it deliberately does not do

- **It does not touch Scale Compliance.** Compliance is a Weighted-only setting
  (`readsScaleSettings()` is `sourceIndex() == 0`), and Borrowed is out of key on purpose, the way
  `ChordSources.h`' own header says every source may be.
- **It does not change Scale Lock.** One consequence is worth saying plainly: a borrowed chord
  dragged onto an arp line with Lock on has its outside notes snapped into the line's key by
  `snapToMask`, so a bVI comes out as something diatonic. Pads are untouched, as they are today. The
  fix is the line's own KEY chip (Lock off for that line), not a special case in the engine.
- **No secondary dominants.** V/V and friends are borrowed from another *key*, not another mode.
  They are idea 2 in `CHORD_LANDSCAPE.md` and get their own source or a setting on this one later.

## Touch points

From a code map taken the same day; line numbers will drift.

- `src/KeysParams.cpp`: append "Borrowed" to `genSource`; append the two parameters after the
  generator block.
- `tests/StateTests.cpp`: extend the `genSource` golden array and the parameter golden list.
- `src/ChordSources.h`: `keys::sources::borrowed(rootPc, mode, octave, count, from, nBorrowed, rng)`,
  pure, same shape as `planing`.
- `src/ui/ChordGenMenu.cpp`: `case 8` in `generateChords`.
- `src/ui/ChordGenPanel.cpp`: the ninth source button (check the row still fits; measure, do not
  assume), the band in `bandFor(8)`, the two attachments.
- `src/ui/SourceViz.{h,cpp}`: `case 8: paintBorrowed`; the header's "0..7" comment.
- `tests/ChordSourceTests.cpp`: see below.
- Docs that say "eight sources": `CLAUDE.md`, `README.md`, `docs/CONTROLS.md`,
  `docs/ARCHITECTURE.md`, `docs/CHORD_LANDSCAPE.md`; plus `CHANGELOG.md`.
- MCP: nothing. `KeysMcp.cpp` does not expose the generator source.

## Tests

- Returns `count` chords for every mode and every From value.
- Exactly `Borrowed` chords are outside the home mode, and every one of them is inside its From
  mode. Swept over all modes and roots, not spot-checked.
- No borrowed chord is ever diatonic in the home mode (the Mixolydian `bVII` case).
- Every borrowed chord carries a non-empty `numeral`, and that numeral's root, read through
  `numeralDegrees()`, is the chord's actual root (the label must not lie).
- The first chord is the tonic; the Picardy `I` only ever appears last.

## Open questions for Owen

1. **What it makes.** A progression with borrowed chords placed in it (this spec), or a palette:
   the tray filled only with the borrowed chords available in this key, for you to place yourself?
2. **The name on the button.** "Borrowed" (short, plain) or "Modal Interchange" (the textbook term,
   wider on a row that is already full)?
