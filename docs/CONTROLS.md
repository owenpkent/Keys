# Controls

Every control is operated with a single left-click, a drag, or the scroll wheel.
Nothing needs the keyboard, a double-click, or a modifier key, and nothing *requires*
a right-click: the one right-click gesture below is an optional shortcut with an
on-screen equivalent.

## The keyboard

| Gesture | Result |
|---------|--------|
| Click a key | Play that note (velocity from the Velocity range) |
| Click and drag | Glide across keys; the previous note releases as the next sounds (monophonic) |
| Right-click a key *(optional)* | Hold that one note — Octavium's per-note latch. **Left-click it again** to release it, or **All Off**. |
| Click a **C** | Every C is labelled (C1, C2, …) to help you orient |

Chords come from **Sustain** or **right-click** — a single mouse can't hold several
keys at once, so those are how you stack notes. Either way, **clicking a held key
releases it**, so a chord with a wrong note in it can be taken apart a note at a time.

To the **left of the keyboard** are two performance wheels: **Mod**
sends CC1 and stays where you leave it; **Pitch** bends and glides back to centre over
a moment when you let go. Both move by **relative drag** — clicking never jumps the
value, so a stray click can't slam a bend. They are
transient (they don't save with the session) and send on the current MIDI channel.

## Folding the window down

Keys is a stack of sections, and each one folds away so the plugin can be squeezed small
when the screen is busy. **Click the chevron at the left end of a section bar** — only
that end folds it, so a click that misses a control next to it can't collapse the section
by accident. The window resizes itself to whatever the folds add up to, and can never be
dragged smaller than the content it is showing.

| Section | Bar | Folds away |
|---------|-----|-----------|
| **Controls** | top | Size, Root, Scale, Octave, Scale Lock, Voices, MIDI Ch, Velocity, Humanize, Strum, Dir |
| **Perform / Chords** | middle | Whichever centre view is showing. Both tabs stay visible while it is folded, so picking one both unfolds and switches. **Knobs** folds the knob bank inside Perform. |
| **Arp** | below the centre | The arpeggiator. Its **On** toggle and **Detach** ride on the bar, so the arp can be switched on, off and detached with the panel folded shut — folding it puts the editor away, never the arpeggiator. It starts folded, because open it is the tallest thing here. |
| **Pads** | below the arp | The chord-pad strip, on screen under either centre view — so a chord stays reachable while you edit the generator or the arp. Its page buttons and the **To Arp** toggle ride on the bar. |
| **Keyboard** | above the keys | The keybed. **Wheels** folds the mod and pitch wheels; Exclusive, Sustain and All Off stay put. |

### Detaching the keyboard, and the arp

**Detach** puts the keybed in its own resizable window, so you can make the keys as large
as the screen allows without stretching the rest of the plugin. Docked, key height is a
compromise with everything above it; detached, dragging the window taller genuinely makes
the keys taller. That window carries its own **Size**, **Wheels** and **Re-dock** controls,
and its close button re-docks. Its position and size are remembered with the session.

The **Arp** bar has a Detach of its own, and it works the same way: the arpeggiator moves
into a resizable window, its close button re-docks it, and its position is remembered. A
detached section takes no height in the main window, so this is the way to keep the
arpeggiator open and the plugin window small at the same time.

## Playing surface

Keys is one view, no tabs: the piano fills the playing area in both Keys and Keys
Host. (The hex-grid Harmonic Table and Hex Host live in their own repo, `../Hex`.)

Sustain, Voices, Octave, and Humanize all apply to it, and All Off silences
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
| **MIDI Ch** | dropdown | Output channel, 1–16 |
| **Velocity** | two-handle slider | How hard Keys plays. With **Humanize** off, every note plays the band's midpoint (the readout shows it). With Humanize on, each note takes a random value inside the band. Drag an end to resize it, or **drag the middle to move the whole band**. Collapse it onto one value for a plain fixed velocity. |
| **Humanize** | toggle | On: each note takes a random velocity from inside the Velocity band, so repeats and chords don't sound machine-perfect. Off: the band's midpoint. |
| **Strum** | slider | Spread a chord's notes over 0–200 ms instead of playing them together. Applies to chord pads and the live chord card. |
| **Dir** | dropdown | Strum direction: **Up** (low→high), **Down** (high→low), or **Random**. |
| **Theme** | swatch | Colours this instance (Cyan, Amber, Lime, Violet, Magenta, Orange, Rose, Ice), so you can tell it from Keys on your other tracks. Per instance, saved with the session. Sits on the *Controls bar*, so it stays reachable with that section folded. |
| **Update to vX.Y.Z** | button | Appears only when a newer signed release exists. One click downloads, verifies, and launches the installer. |

These sit on the **Keyboard bar** rather than in a section, so folding anything away never
takes them with it — they are what you reach for while playing:

| Control | Type | What it does |
|---------|------|--------------|
| **Exclusive** | toggle | Playing a chord pad chokes the previously-playing pad, so only one pad chord sounds at a time. |
| **Sustain** | toggle | On: notes keep sounding after you release the mouse, like a sustain pedal. With the pedal down a glide leaves a trail. Click a held key to release just that note; turn Sustain off (or click All Off) to release everything. |
| **All Off** | button | Panic. Stops every note on every channel, and drops anything a strum still had queued. |

## Holding notes

A single mouse can't hold several keys, so there are two ways to stack them:

- **Sustain** catches every note you play while it is on.
- **Right-click** a key to hold that one note (an optional accelerator).

Either way, **left-clicking a held key releases it**. That is why there is no Latch
toggle any more: once one click both holds and releases, a whole mode for holding notes
earned nothing.

Everything here persists with the DAW session.

## Chord pads

Two rows of eight pads (sixteen a page) and a live chord card sit between the controls
and the playing area. They let you keep a palette of chords a single click away.

1. **Build a chord.** Turn **Sustain** on (or right-click) and click the notes you want.
   The card names the chord it hears (for example `Cm7`).
2. **Hear it as a chord.** Press and hold the card. Holding a chord sounds the keys you
   are holding; the card fires them as one chord, so you hear it strummed, humanized and
   capped by Voices — the way a pad plays it. Release to stop.
3. **Capture it.** Drag the card onto a pad. The pad stores that chord, auto-labelled.
   (A drag beats the press, so capturing never leaves a note ringing.)
4. **Play it, beat-pad style.** Press and hold a filled pad to sound its chord; release
   to stop. Turn **Sustain** on to keep it ringing after you let go, and **Exclusive** on
   so a new pad chokes the previous chord.
5. **Rearrange, clear, or recall.** Drag a pad onto another to move it, drag a pad off
   the rows to empty it, or drag a pad onto the live card to bring its notes back onto
   the keyboard (held) for editing — capture in reverse.

Pad chords play through the same output as the keys, so **Humanize** gives each chord
tone its own velocity and the **Strum** control spreads them into a strum. A pad also
respects the **Voices** limit: if a chord has more notes than the cap allows, its lowest
notes are the ones that sound. The pads save with the DAW session.

There are **four pages** of sixteen pads, picked by the four numbered buttons directly
under the strip. A chord left ringing on one page keeps sounding while you work on
another, so you can hold a bass chord on page 1 and play page 2 over it.

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

## Arpeggiator

A section of its own, between the centre view and the pads. It takes whatever is currently
sounding (keyboard, a held note, a chord pad) and plays it one note at a time. The chord
pads sit directly below it, so a chord is always one click away, and the knobs or the
generator stay on screen above.

**On** and **Detach** live on the Arp bar rather than inside the panel, so folding the
panel away leaves the arpeggiator running and still switchable. The section starts folded.

**Shape decides how much of the panel exists.** The eight directions are plain
arpeggios and show nothing but the controls below. The ninth entry, **Pattern**, opens
the step editor and adds the STEPS group to the band.

Controls are grouped: **PATTERN** is what it plays, **PLAYBACK** is how it behaves, and
**STEPS** (Pattern shape only) drives the step editor. The `<` and `>` buttons beside Shape
and Rate step to the next entry without opening the menu; they stop at the ends rather than
wrapping round.

| Control | Group | What it does |
|---------|-------|--------------|
| **On** | header | Arp on or off. Everything else stays editable while it is off. |
| **Shape** | Pattern | Up, Down, Up-Down, Down-Up, Up & Down, Down & Up, As Played, Reversed, or **Pattern** (opens the step editor). |
| **Rate** | Pattern | Step length, 16 bars down to 1/64. |
| **Dot** / **Trip** | Pattern | Dotted or triplet feel on the rate. |
| **Swing** | Playback | 0–0.75. Delays the offbeat steps. |
| **Gate** | Playback | 5–200%. Note length as a share of the step; over 100% ties into the next one. Works on **any** shape, and multiplies the Gate lane when you are using one. |
| **Chance** | Playback | 0–100%. How likely each step is to fire — turn it down to thin a run out. Works on any shape, and multiplies the Probability lane. |
| **Octaves** | Playback | 1–4. How many octaves a direction shape climbs before repeating. |
| **Anchor** | Playback | On: steps lock to the host's bar grid, so the arp lines up after a jump. Off: free-running, never jumps, may drift. |
| **Latch** | Playback | Keep arpeggiating after you let go, until a new chord arrives. |
| **Retrigger** | Playback | Restart at step 1 when a note arrives on an empty set. |

### The step editor (Shape → Pattern)

Six lanes, shown **one at a time** through the tabs: pick a tab, edit that lane. Click a
step to set it, or drag across the grid to paint several in one gesture; a readout
follows the cursor. Nothing needs a modifier, a double-click or the keyboard.

| Lane | Range | Meaning |
|------|-------|---------|
| **Note** | 1–8, or follow | Which note of the held chord this step plays. "Follow" (the dot) leaves it to the shape. |
| **Octave** | −3 – +3 | Octaves added to this step. |
| **Velocity** | 10–200% | Scales the velocity you played at. |
| **Gate** | 5–200% | Note length as a share of the step. Over 100% ties into the next step. |
| **Ratchet** | 1–4 | Sub-hits packed into this step. |
| **Probability** | 0–100% | Chance this step fires at all. |

The **Mute** row under the grid silences individual steps without disturbing their
values, so you can take a step out and put it back unchanged.

**Steps** sets how many steps the pattern runs before repeating (1–32), and **Speed**
runs the lane at full, half or quarter rate. Both apply to the lane you are looking at.

**Link** (on by default) keeps every lane the same length and speed, which is the usual
case. Turn it off and each lane keeps its own: a 4-step note lane against a 3-step octave
lane gives you polymeter, patterns that take several bars to come back around.

### The twelve slots

The row of cards along the bottom is on screen in **both** shapes. Each slot holds a
pattern, and can also hold a chord with the shape and rate that were up when you sent it
there — the card shows you all three. **Click a slot to launch it**: it installs the
pattern, sets Shape and Rate back to what it remembers, and starts arpeggiating its chord.
Click the launched slot again to release it, or press **Stop**. A slot with no chord shows
"--", launches the pattern alone, and arpeggiates whatever you are already holding.

A soft ring means *this slot's lanes are the ones the step editor is editing*. A bright ring
and a lit triangle mean *this slot's chord is what you are hearing*. They are different
things and often belong to different slots.

| Button | What it does |
|--------|--------------|
| **Copy** | Arms a copy from the live pattern: click it, then click the slot to copy into. |
| **Clear** | Arms a clear: click it, then click a slot to take its chord away. The pattern stays. |
| **Cancel** | Disarms Copy or Clear. Only appears while one of them is armed. |
| **Stop** | Releases the chord a slot is holding, without the blunt instrument of All Off. |
| **Randomize** | Rerolls the live pattern's lanes. Pattern shape only, since a plain shape has no lanes. |

Right-clicking a slot offers Launch, Clear chord, Copy and Randomize, if you prefer a menu.

### Getting a chord into the arp

Two ways, both from the Pads section under the panel:

- **To Arp** on the Pads bar. Lit, clicking a pad hands its chord to the arp and leaves it
  there: the pad wears a bright ring while it is the one feeding the arp, so click the lit
  pad to release it, or another to swap. The generator's chord grid obeys the mode too, so
  a chord can reach the arp while you are still filling the page. The live chord card hands
  its chord over the same way, but takes no ring and no second click — use **Stop**, or
  another card, to move off it. Unlit, the pads play exactly as they always have. The mode
  is remembered with the session, because the chord it holds outlives the window.
- **Send to arp slot**, in a pad's right-click menu, parks a copy of that chord in one of
  the twelve slots to launch later.
