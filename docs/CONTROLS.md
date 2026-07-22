# Controls

Every control is operated with a single left-click, a drag, or the scroll wheel.
Nothing needs the keyboard, a double-click, or a modifier key, and nothing *requires*
a right-click: the one right-click gesture below is an optional shortcut with an
on-screen equivalent.

## The keyboard

| Gesture | Result |
|---------|--------|
| Click a key | Play that note (velocity from the Velocity control + Curve) |
| Click and drag | Glide across keys; the previous note releases as the next sounds (monophonic) |
| Right-click a key *(optional)* | Toggle that one note held, without turning Latch mode on — Octavium's per-note latch. **All Off**, or turning **Latch** off, releases it. |
| Click a **C** | Every C is labelled (C1, C2, …) to help you orient |

Chords come from Latch, Sustain, or right-click — a single mouse can't hold several
keys at once, so those are how you stack notes.

To the **left of the keyboard** are two performance wheels: **Mod**
sends CC1 and stays where you leave it; **Pitch** bends and glides back to centre over
a moment when you let go. Both move by **relative drag** — clicking never jumps the
value, so a stray click can't slam a bend. They are
transient (they don't save with the session) and send on the current MIDI channel.

## Playing surface

Keys is one view, no tabs: the piano fills the playing area in both Keys and Keys
Host. (The hex-grid Harmonic Table and Hex Host live in their own repo, `../Hex`.)

Latch, Sustain, Voices, Octave, and Humanize all apply to it, and All Off silences
it. The 4x4 note pad grid and the XY pad from earlier builds are gone (drums belong
to Beatform; the XY pad's two CCs are covered by the knob row below).

## Knob row

Eight rotary CC knobs sit above the playing area (Octavium's fader window and XY pad,
collapsed into one strip): centred at 64, the button under each knob shows its CC and
reassigns it in one click. Positions send only while you drag (relative drag, no
click-jump), and nothing is sent until you move one.

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
| **All Off** | button | Panic. Stops every note on every channel (per-note offs, then CC120 All Sound Off + CC123 All Notes Off). |
| **Update to vX.Y.Z** | button | Appears only when a newer signed release exists. One click downloads, verifies, and launches the installer. |

## Notes on Sustain vs Latch

- **Sustain** is momentary-feeling: notes you play while it's on are caught and held;
  turning it off releases everything it caught.
- **Latch** is a toggle set: each key you click flips on or off and stays. Turning
  Latch off clears the latched notes.

Both persist with the DAW session, along with every other control here.

## Chord pads

Two rows of eight pads (sixteen a page) and a live chord card sit between the controls
and the playing area. They let you keep a palette of chords a single click away.

1. **Build a chord.** Turn **Latch** on and click the notes you want. The card names
   the chord it hears (for example `Cm7`).
2. **Capture it.** Drag the card onto a pad. The pad stores that chord, auto-labelled.
3. **Play it, beat-pad style.** Press and hold a filled pad to sound its chord; release
   to stop. Turn **Sustain** on to keep it ringing after you let go, and **Excl** on so
   a new pad chokes the previous chord.
4. **Rearrange, clear, or recall.** Drag a pad onto another to move it, drag a pad off
   the rows to empty it, or drag a pad onto the live card to bring its notes back onto
   the keyboard (latched) for editing — capture in reverse.

Pad chords play through the same output as the keys, so **Humanize** gives each chord
tone its own velocity and the **Strum** control spreads them into a strum. A pad also
respects the **Voices** limit: if a chord has more notes than the cap allows, its lowest
notes are the ones that sound. The pads save with the DAW session.

There are **four pages** of sixteen pads. `<` and `>` move between them, and the label
between shows where you are. A chord left ringing on one page keeps sounding while you
work on another, so you can hold a bass chord on page 1 and play page 2 over it.

## Chord generator

**Chords** opens the generator over the plugin. It works on the page of pads you are
looking at, so each page can be a different key. **Close** puts it away.

### Filling a page

1. **Pick a feel.** The **Feel** row (Happy, Sad, Dreamy, Dark, Jazzy, Bluesy, Epic,
   Chill, Mysterious, Smooth) sets a key and mode in one click, and moves **Root** and
   **Scale** to match so **Scale Lock** agrees with the chords you're about to get.
   Or set **Key** and **Mode** yourself — each mode shows the character it carries.
2. **Fill Page.** Every unlocked pad gets a chord. The seven chords that belong to the
   key come first, in order, then the remaining pads get something richer from the key.
3. **Play them.** Press a chord in the grid to hear it, or close the panel and play the
   pads.

### Shaping what you get

| Control | What it does |
|---------|--------------|
| **Scale Compliance** | How adventurous the chords are. At 100% every note stays in the key. Lower it and the generator borrows chords from related modes, then reaches for secondary dominants, then for anything at all. |
| **Notes** | Which chord sizes to build: **3** triads, **4** 7ths and 6ths, **5** 9ths and extensions. |
| **Inversions** | **R** is root position; **1st** / **2nd** / **3rd** let a chord sit with its lower notes moved up an octave, so a progression moves less. |
| **Octave** | Which register the generated chords land in. |

### Keeping what you like

**Lock** a chord you want to keep. **Regen Unlocked** gives every other pad a new chord;
locked ones stay. **Clear Page** empties the unlocked pads.

**Lock Influence** decides how much the locked chords steer the new ones. At a high
setting, locking three 7th chords biases what you get toward 7ths — it copies the
*character* of what you kept, not the chords themselves.

### Finding the next chord

**New** on a pad gives you a different chord for that pad's place in the scale — same
role in the key, different colour.

**Next** asks what could follow that chord, and offers four kinds of answer:

- **Neo-Riemannian** — the moves that change as little as possible: swap major for minor
  (P), or slide to a chord that shares two of its notes (L, R, N, S, H).
- **Circle of Fifths** — the dominant and subdominant, the pulls that make a progression
  feel like it's going somewhere.
- **Diatonic** — the other chords of the key (ii, iii, vi, vii).
- **Chromatic** — the borrowed and jazz moves (tritone substitution, minor plagal,
  Neapolitan, augmented sixth).

Every suggestion row has a **play** button that auditions it for a moment without
closing the menu, so you can shop by ear. Clicking the row itself takes it: your pick
goes into the next empty pad on the page, so you can build a progression left
to right by asking for one chord at a time.

### The Markov source

Switch **Source** from **Algorithmic** to **Markov** and Fill walks chains learned
from real progressions instead of weighting a candidate pool:

| Control | What it does |
|---------|--------------|
| **Chain** | Major, Minor, or Modal chain tables |
| **Temperature** | 0.30–2.00. Low sticks to the most common moves (conservative); high flattens toward anything the corpus has ever done (adventurous). |
| **Length** | How many unique chords are generated (4–16) before the sequence loops to fill the page. |
| **Mood** | Only learn from progressions tagged with this mood (or **Any**). |
| **Start** | Force the first chord (I, i, IV, V, vi, …) or let it pick. |

**New** on a Markov pad steps the chain again from the pad to its left, avoiding the
chord it replaces. The note-count, inversion, compliance, and lock-influence controls
don't apply to Markov chords and grey out (Octavium left them clickable and silently
ignored them). Locked pads are never overwritten, same as the algorithmic source.
