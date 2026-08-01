# Controls

Every control is operated with a single left-click, a drag, or the scroll wheel.
Nothing needs the keyboard, a double-click, or a modifier key. The right-click gestures
below are accelerators, and the work each of them does has a left-click path too, bar three
exceptions Owen signed off:

- **Send to arp slot**, in a chord pad's menu, because binding a chord to one particular
  slot needs a target picker.
- **Lock**, in the same menu. It had a clickable chip in the card's corner for a few hours on
  2026-07-30 and lost it at Owen's request: the chip took roughly a quarter of the card away
  from playing it, dragging it and feeding the arp. A locked card still *shows* a corner dot,
  so the state reads without opening the menu; the dot is a marking and not a target.
- **Releasing one note out of a chord the Sustain pedal is holding.** Under Sustain a left
  click on a ringing key strikes it again, by design, so the second click cannot also be the
  release. Right-click is the only way to take that one note out. Under **Latch** the left
  click does release, so this exception is Sustain's alone.

## The keyboard

| Gesture | Result |
|---------|--------|
| Click a key | Play that note (velocity from the Velocity range) |
| Click and drag | Glide across keys; the previous note releases as the next sounds (monophonic) |
| Right-click a key *(optional)* | Toggle a hold on that one note. A key this keyboard is already holding **lets go**, whether Latch put it there or the pedal caught it; any other key latches on. So a right-click walk along a ringing chord takes it apart a note at a time without lifting Sustain. |
| Click a **C** | Every C is labelled (C1, C2, …) to help you orient |

Chords come from **Latch**, **Sustain** or **right-click**: a single mouse can't hold
several keys at once, so those are how you stack notes. The difference is what a second
click on a ringing key does. Under **Sustain** it strikes the note again (it is a pedal,
so the same key can be played over and over while the chord under it holds). Under
**Latch** it releases that note, so a chord with a wrong note in it can be taken apart
a note at a time. A note the right button latched on releases to a plain left click too,
so the right button is never the only way out of it.

Right-click can only reach notes **this keyboard** is holding. Keys lit by a chord pad, the
arpeggiator or MCP are owned elsewhere and are left alone: use the pad, **Hold off** on the
arp bar, or **All Off**.

### Watching a physical keyboard

Keys also **shows you what is arriving on its MIDI input**. Play a hardware keyboard into
it — in the standalone, tick your device under **Options → Audio/MIDI Settings**; in a DAW,
anything feeding the track — and those keys light up on the on-screen keybed, with the live
chord card naming the chord so you can capture it to a pad. Keys does not intercept any of
it: the stream passes through to your instrument exactly as it always has, and **All Off**
clears the lights if a note-off ever goes missing.

To the **left of the keyboard** are two performance wheels: **Mod**
sends CC1 and stays where you leave it; **Pitch** bends and glides back to centre over
a moment when you let go. Both move by **relative drag** — clicking never jumps the
value, so a stray click can't slam a bend. They are
transient (they don't save with the session) and send on the current MIDI channel.

## Folding the window down

Keys is a stack of four sections, and each one folds away so the plugin can be squeezed
small when the screen is busy. **Click the chevron at a bar's left end, or the caption
beside it**, to fold that section, or to bring a folded one back. That left end is the whole
target: 92 px wide at the shortest caption, the only part of the bar that lights under the
mouse, and a **hairline** is drawn where it ends so you can see how far it reaches. The rest
of the strip belongs to the controls riding on it, and a click that lands in the gaps
between them does nothing at all.

Doing nothing is the point. For three days the whole 34 px strip folded the section, on the
reasoning that a bigger target is a kinder one. Z-order always protected each control's own
rectangle, so a click that landed on **Detach** always reached Detach; what nothing could
protect was the space around them. A click aimed at Detach that missed by a few pixels hit
bar, and the bar folded away the thing you were reaching into. A bigger target is only
kinder when the extra area does what the target does.

The window resizes itself to whatever the folds add up to, and can never be dragged smaller
than the content it is showing.

An open section's bar is a solid ruled band with a bright caption and a tick of accent at
its left end. A folded one goes flat and dim and drops its Detach button. So the shape of
the window reads at a glance, before you have read a single caption.

| Section | Bar | Folds away |
|---------|-----|-----------|
| **Controls** | top | Size, Root, Scale, Octave, Scale Lock, Voices, MIDI Ch, Humanize, Velocity, Strum, Dir, BPM, and the eight knobs in the row beneath them. **Knobs**, at the left end of this bar, folds just that knob row; the theme swatch at the right end stays put whatever you fold. |
| **Arp** | below the controls | The arpeggiator. Its **On** toggle and the **Hold off** chip ride on the bar and stay there folded, so the arp can be switched on and made to let go of a chord with the panel shut. **Detach** rides on it too, but goes with the fold like every other section's. Folding it puts the editor away, never the arpeggiator. It starts folded, because open it is the tallest thing here. |
| **Pads** | below the arp | The sixteen chord pads and the live chord card. The four page buttons ride at the left of the bar and fold with the strip; the generator's **Fill**, **Regen** and **Generator** chips and its **Key**, **Mode** and **Scale Compliance** combo boxes ride at the right and never do. |
| **Keyboard** | above the keys | The keybed. **Wheels** folds the mod and pitch wheels; Exclusive, Sustain, Latch and All Off stay put. |

A bar is a real button, not a painted strip, so it carries an accessible name for screen
readers and UI Automation: *"&lt;caption&gt; section"* (`Controls section`, `Arp section`,
and so on). The name says "section" so that a bar can never collide with a control sitting
on it.

### Detaching a section

**Every open section bar has a Detach button**, at its right-hand end. It puts that section
in a resizable window of its own, which you can put anywhere on the desk and size to suit.
Inside that window a **Re-dock** button sits at the top, and the window's close box does the
same thing; either brings the section home. Each window's position and size are remembered
with the session, and one that ends up off-screen is pulled back on before it opens.

A folded section has no Detach button: unfold it first. There was nothing behind the gesture
anyway — detaching a folded section built a window that opened hidden — and on a bar whose
whole job is to be quiet, it was the loudest thing left.

A detached section takes no height in the main window, so this is also the way to keep a tall
section open and the plugin window small at the same time. Folding a detached section hides
its window rather than its (now empty) slot, so the chevron still means one thing. The bar it
came from says **IN ITS OWN WINDOW** where its controls used to be.

The **keyboard** is the one that gains the most. Docked, key height is a compromise with
everything above it; detached, dragging the window taller genuinely makes the keys taller, as
large as the screen allows. That window carries its own **Size** and **Wheels** controls
alongside Re-dock, because those belong to the keybed rather than to the editor.

What stays behind on a bar is whatever belongs to the editor rather than to the section: the
arp's **On** toggle and **Hold off** chip, the pad page buttons, the generator's **Fill**,
**Regen** and **Generator** chips with the **Key**, **Mode** and **Scale Compliance**
combo boxes beside them, the **Knobs** chip, and the theme swatch. All of them keep working
while the section they name is off in a window.

Folding is the other case, and a stricter one: a folded bar keeps only what still means
something with the section gone. That is the arp's **On** and **Hold off** (so the
arpeggiator runs on behind a closed panel, and can still be made to let go of a chord),
**Fill**, **Regen**, **Generator** and the three combos with them (the whole left-click path
into the chord generator - generating into a folded strip is a fine thing to mean, and the
settings must not fold away with the cards), and the theme swatch (it colours the whole
plugin). Everything else goes with the section, Detach included.

## Playing surface

Keys is one view, no tabs: the piano fills the playing area in both Keys and Keys
Host. (The hex-grid Harmonic Table and Hex Host live in their own repo, `../Hex`.)

Sustain, Voices, Octave, and Humanize all apply to it, and All Off silences
it. The 4x4 note pad grid and the XY pad from earlier builds are gone (drums belong
to Beatform; the XY pad's two CCs are covered by the knob row below).

## Knob row

Eight rotary CC knobs (Octavium's fader window and XY pad, collapsed into one strip):
centred at 64, the button under each knob shows its CC and reassigns it in one click.
Positions send only while you drag (relative drag, no click-jump), and nothing is sent until
you move one.

They are the **bottom row of the Controls section**, not a section of their own, which is
what makes them 60 px across. **Knobs**, the chip at the left end of the Controls bar, folds
just that row and leaves the dropdowns above it alone. A chip riding a bar costs the window
no height, so the knobs gave up a section without giving up the fold. Fold the whole
Controls section and the chip goes with it, since there is then no row for it to hide.

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
| **Strum** | range | Spread a chord's notes instead of playing them together, over a time drawn from this 0–200 ms band — so repeated stabs do not all rake at the same speed. Drag an end to resize the band or the middle to move it; both ends together is a fixed strum. Applies to chord pads and the live chord card. |
| **Dir** | dropdown | Strum direction: **Up** (low→high), **Down** (high→low), or **Random**. |
| **BPM** | slider | 40–240. The tempo anything timed in beats runs at — today that is the arpeggiator alone — when there is no transport to follow: always in the standalone, and whenever the host is stopped. A host that is *playing* always wins, so this never fights tempo sync. |
| **Theme** | swatch | Colours this instance (Cyan, Amber, Lime, Violet, Magenta, Orange, Rose, Ice), so you can tell it from Keys on your other tracks. Per instance, saved with the session. Sits on the *Controls bar*, so it stays reachable with that section folded. |
| **Update to vX.Y.Z** | button | Appears only when a newer signed release exists. One click downloads, verifies, and launches the installer. |

These sit on the **Keyboard bar** rather than in a section, so folding anything away never
takes them with it — they are what you reach for while playing:

| Control | Type | What it does |
|---------|------|--------------|
| **Exclusive** | toggle | Playing a chord pad chokes the previously-playing pad, so only one pad chord sounds at a time. |
| **Sustain** | toggle | On: notes keep sounding after you release the mouse, like a sustain pedal. With the pedal down a glide leaves a trail, and clicking a key that is already ringing **strikes it again** — the pedal never turns a key into a switch. Turn Sustain off (or click All Off) to release everything. |
| **Latch** | toggle | On: clicking a key holds it, clicking it again releases it. This is the one to use to build a chord note by note, or to take one apart. Turning Latch off releases everything it was holding. |
| **All Off** | button | Panic. Stops every note on every channel, and drops anything a strum still had queued. |

## Holding notes

A single mouse can't hold several keys, so there are three ways to stack them:

- **Latch** holds every key you click until you click it again.
- **Sustain** catches every note you play while it is on, and re-plays any key you click
  a second time.
- **Right-click** a key to toggle a hold on that one note (an optional accelerator). A key
  it latched on releases to a plain **left click** as well, so the right button is never the
  only way out of its own gesture.

Right-click is also the one way to release a single note out of a chord the **pedal** is
holding, because under Sustain the left click is a restrike by design. That is the one
gesture in Keys with no left-click twin besides Send to arp slot, and Owen asked for it:
without it, taking one wrong note out of a sustained chord meant lifting Sustain and losing
the lot. **All Off** is still the blunt way out.

Sustain and Latch are two answers to the same question, which is how a note stops. Sustain is
a pedal: it defers the release, and a repeated key is a repeated strike, so you can play a
riff over a chord that is still ringing. Latch is a switch: the second click is the
release. They can both be on, and Latch wins on the keys it holds.

Everything here persists with the DAW session.

## Chord pads

Two rows of eight pads (sixteen a page) and a live chord card sit between the arpeggiator
and the playing area. They let you keep a palette of chords a single click away, and until
2026-08-01 they were the only chord cards Keys drew at all. They are still the only ones
**in the session**: the generator's window added a 4x4 audition tray that day, sixteen
candidate chords you can click to hear, but a candidate belongs to no page and is thrown away
when the window closes, so what you keep is still whatever a pad holds. Every card, filled or live, shows its chord's name with the
notes underneath in octave numbers (for example "C3  E3  G3"), so you can read what a pad
or the live card holds without pressing it. That used to need a bigger card: **Big**, on the
Pads bar, swapped the two rows of eight for four rows of four with a full card and a mini
keyboard on each, and it went on 2026-07-31 once the note list fit under the name here too.

1. **Build a chord.** Turn **Latch** on (or **Sustain**, or right-click) and click the notes you want.
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
   the keyboard (held) for editing — capture in reverse. **A locked pad can still be moved
   and still cannot be emptied**: dropping one off the rows does nothing, the same answer
   the greyed-out **Clear pad** on its menu gives. The ghost fades as you leave the rows to
   say so before you let go.
6. **Edit a pad on the keyboard.** Right-click a pad and pick **Edit on keyboard**: its
   chord latches onto the keys, and every key you add or remove is written straight back
   to the pad, with the name re-detected as you go. That pad wears a **✓** at its
   right-hand end while the link lasts — click it to finish. (The pad is written as you
   play, so the tick ends the edit rather than committing it; before it existed, finishing
   meant going back into the right-click menu, which made the last step the hardest one.
   Folding the Pads section away also ends the edit, since the tick goes with it.)

Pad chords play through the same output as the keys, so **Humanize** gives each chord
tone its own velocity and the **Strum** control spreads them into a strum. A pad also
respects the **Voices** limit: if a chord has more notes than the cap allows, its lowest
notes are the ones that sound. The pads save with the DAW session.

There are **four pages** of sixteen pads, picked by the four numbered buttons on the
**Pads bar** above the strip. A chord left ringing on one page keeps sounding while you
work on another, so you can hold a bass chord on page 1 and play page 2 over it.

### A pad's card menu

Right-click any pad. Everything that can act on that card is here: nine rows in three groups,
separated by rules.

| Item | What it does |
|------|--------------|
| **Edit on keyboard** / **Done editing** | Step 7 above |
| **Clear pad** | Empty this one card. Greyed on an empty or locked pad |
| **Lock** / **Unlock** | Keep this chord through a Regen. **This is the only way to set a lock.** A locked card shows a small dot in its top-right corner, so the state is readable without opening the menu; the dot is a marking and not a button |
| **Octave down** / **Octave up** | Move the whole chord an octave. Greyed when a note would fall off the ends of the keyboard (it never wraps one round to the other end), and while this card is the one linked to the keyboard for editing |
| **Next voicing** | The same chord arranged differently: root position, first inversion, second, (third, on a four-note chord), then a spread with the root left in the bass, then round to root again. The item says which one the card is in now. Greyed while the card is linked to the keyboard, like Octave |
| **New chord** | A different chord for that pad's place in the scale: same role in the key, different colour. Greyed on a locked pad |
| **Next: could follow** | Four submenus of chords that could follow this one, described below. On a filled pad only, since there has to be a chord to follow |
| **Send to arp slot** | Park a copy of this chord in one of the twelve arp slots, to launch later |

**Two of these have no left-click twin**, and both are deliberate: **Send to arp slot**, since
binding a chord to one particular slot needs a target picker, and **Lock**, which had a
clickable chip in the card's corner for a few hours and lost it at Owen's request. The chip
took roughly a quarter of the card, and every bit of that quarter had stopped playing the
chord, starting a drag or feeding the arpeggiator. The whole card is the card again.

**Octave** and **Next voicing** work on a locked pad too. A lock protects a chord from being
*replaced or thrown away* — **Regen**, **Clear pad** and dropping the card off
the rows all leave it alone. It is not a lock against editing: changing a chord you locked, or
moving the card to another slot, is something you asked for by name. If that card is ringing,
or is the one held into the arpeggiator, it changes to the new notes where it stands rather
than being cut off. They do
*not* work on the card currently linked to the keyboard: they write the card and the keyboard
would write it straight back. **Done editing**, at the top of the same menu, frees them.

**Next voicing** on a chord you played with two hands drops the doubled note on the first
press. A voicing is an arrangement of the chord's notes, and a note played twice has no
arrangement of its own: keeping it is what used to send the chord climbing an octave on every
press until it ran off the keyboard.

**The menu is kept short on purpose.** It hangs off a pad near the bottom of the window and
every row is 34 px measured *upwards* from there, so a long menu runs off the top of the
screen and turns into one you have to scroll by hovering, which is unusable with one mouse.
Nine rows is the budget. That is why the four **Next** families sit behind one row, and it is
why the generator's settings are not on this menu at all: they were, for a few hours, and they
took it to 23 rows.

## Chord generator

The generator is **three chips, three combo boxes, a window (with its own tray, reference
card and card menu), and two items on a pad's card menu**:

- **Fill** and **Regen**, at the right-hand end of the **Pads bar**. **Fill is the safe one**:
  it writes a chord to the *empty* pads and never touches one that already has a chord, so you
  can lean on it. **Regen is the one that overwrites**: it gives new chords to the pads that
  already have one, skipping the locked ones, which is what a lock is for. Each greys out when
  it would do nothing - Fill with no blanks left on the page, Regen with nothing unlocked to
  reroll - so the buttons tell you which is which without a hover.
- **Generator**, the chip beside them, opens the generator's **own window**: every setting it
  has, the Markov controls, and, since 2026-08-01, a 4x4 **audition tray** with a reference
  card above it. Click the **Generator** chip
  again while the window is up and it comes to the front rather than opening a second one.
  Close the window with its **Close** button or the X in its title bar; both do the same thing
  and neither loses a setting (a candidate you never dragged out goes with it - the tray was
  never part of the session). Being a window rather than part of the plugin, it can sit
  anywhere on the desk and be sized to suit, and it remembers where you left it and whether it
  was open.
- **The tray's own header row carries Fill, Regen and Clear**, and none of them writes a pad
  any more (changed 2026-08-01, Owen: "when you click on regenerate unlocked, I don't want it
  to regenerate the ones in the host window, only in the card generator window"). They act on
  the sixteen candidates instead: **Fill** generates into the empty cells only, **Regen**
  rerolls the cells that already carry a candidate, **Clear** empties the tray outright. They
  replace the **Reroll** button the tray opened with, and the same safe/destructive split
  survives the move - Fill greys when the tray is full, Regen and Clear grey when it is empty,
  and none of the three can lose a real chord, because a tray card is one drag from a pad if
  you want it kept and nothing until then. **A committed card now leaves its cell empty**
  instead of refilling itself: the hole shows which of the sixteen you have already taken, and
  it is what gives Fill something to do. Click a card to hear it for 800 ms; drag it onto a pad
  to keep it, written the same way capturing a chord from the live card is - and Send to first
  empty pad, on the card's own right-click menu (below), is the same commit with the aim taken
  out. The tray also rerolls itself the instant a generator setting changes, so it never goes
  stale while you're still picking a key.
- **The reference card**, above the tray, is one chord that none of the tray's own actions can
  touch (Owen, 2026-08-01: "so when you regenerate everything, it doesn't erase your reference
  chord"). Drag a **tray card** onto it, or a **pad from the main window** - dropping a pad
  here *copies* it rather than clearing the pad, which is the one thing that makes dragging a
  chord to the reference box safe to try. Left-click auditions it, same as a tray card. Beside
  it: **Similar** (same root, a different colour - a seventh, a ninth, a sus, an inversion, the
  parallel major or minor) and **Could follow** (the same four families "Next: could follow"
  offers on a pad) each fill the tray from that one seed, and **Clear** empties the reference
  card alone. All three grey out while it is empty.
- **Right-click a tray card** for a menu of its own, a new entry on the closed right-click
  list in `CLAUDE.md` (Owen, 2026-08-01: "when you right click on a chord in there, I want you
  to have a whole bunch of options about trying to find similar ones or what might come
  next"): Send to first empty pad, Fill tray with similar chords, Fill tray with what could
  follow, Octave down, Octave up, Next voicing, New chord here, Clear this card. **Opening the
  menu makes no sound, and neither do the shaping edits** - it auditioned the card for a few
  minutes on the day it was built and Owen had that taken out, since the left click is already
  how you hear a card.
- **Key**, **Mode** and **Scale Compliance**, combo boxes on the same bar just left of the
  chips. These are the three you change while you are auditioning a page, so they are on the
  bar as well as in the window: one click opens the list, one picks. **The bar is the fast way
  and the window is the complete one**, and they are the same three settings, so a change in
  one place shows in the other immediately. Scale Compliance is the one place the two read
  differently, and it is not a disagreement: **the bar offers five steps, the window is
  continuous, and the bar shows the step nearest the value**. Set 60 on the window's slider and
  the bar reads "50 %". Picking that "50 %" from the bar sets it to 50, as it should - the bar
  always writes exactly the step you picked, even when it is the one already showing.
- All six of those stay clickable when the Pads section is folded away. Folding the strip must
  not take the generator with it: unfold and the page is written.
- **The pad right-click menu** keeps the two things that are about one card: **New chord** and
  **Next: could follow**. Those work whether the generator's window is open or not.

It works on the page of pads you are looking at, so each page can be a different key. There is
still exactly one set of chord **pads** in Keys - the strip below the arp - and pressing one is
the only way a generated chord makes sound outside a preview. (It used to draw its own
full-size copy of the same sixteen pads, from back when it covered the whole plugin. That
arrangement became the Pads section's **Big** switch, and then went altogether on 2026-07-31,
once every pad showed its own notes without needing to grow.) The audition tray in the
generator's window is not that grid come back: its sixteen cards are candidates rather than
pads, belong to no page and no session, and a click only previews one for 800 ms - hearing a
candidate costs nothing, keeping one still means dragging it onto a pad or picking Send to
first empty pad from its right-click menu.

### Filling a page

1. **Pick a key.** Set **Key** and **Mode** from the combo boxes on the Pads bar, or from the
   same two controls in the generator's window. The bar spells the modes without their aliases, so "Natural Minor (Aeolian)" reads
   **Natural Minor** there; the window has the room for the full names. These two are the
   generator's own, separate from the **Root** and **Scale** that drive Scale Lock, so move
   those to match if you want Scale Lock to agree with the chords you're about to get.
2. **Fill.** One click on the chip. Every *empty* pad gets a chord: the ones that belong
   to the key come first, in order (seven for a seven-note mode, but six for Blues and five
   for the pentatonics, since a mode seeds one per degree it has), then the remaining pads get
   something richer from it.
   Anything already on the page stays exactly as it is, so filling a half-finished page is
   safe. To replace what is there, use **Regen** instead, and lock the cards you want kept.
3. **Play them.** Press a pad to hear it: the notes are already printed under its name, so
   there is nothing extra to turn on to read them. **Octave** and **Next voicing** on a pad's
   right-click menu move one card around without changing what chord it is.

### Auditioning before you commit

The generator's window carries a 4x4 tray, with a reference card above it: sixteen chords the
generator has made that belong to no pad yet (added 2026-08-01). **Click** a card to hear it
for 800 ms - a preview, not a capture, so it costs nothing and never touches a pad. Auditioning
stops every other chord source first (a ringing pad under Sustain, or one held into the arp),
so what you hear is always the whole chord rather than whatever pitches happened not to
collide. **Drag** a card onto a pad to keep it: that pad is written the same way capturing a
chord from the live card is, a locked pad refuses the drop, and the tray cell you took it from
is left empty rather than refilling itself - the hole is how you see which of the sixteen you
have already used. **Fill**, **Regen** and **Clear**, on the tray's own header, act on the
tray alone: Fill tops up the empty cells, Regen rerolls the filled ones, Clear empties it
outright, and changing any generator setting rerolls the whole tray the same way, so it never
shows chords the current settings would no longer make. **Right-click a card** for Send to
first empty pad, the two Fill-tray-from-this-seed options, the three shaping edits, or New
chord here / Clear this card - see the chord generator overview above. The **reference card**
holds one chord none of that can touch, filled by dragging a tray card or a main-window pad
onto it, with its own Similar / Could follow / Clear. Closing the window loses whatever you
never dragged onto a pad - the tray (and the reference card) were never part of the session,
which is what lets you audition a dozen chords for the one you keep.

### Generator settings

All of these live in the generator's window, opened by the **Generator** chip on the Pads bar.
Row A is key, mode, octave, **Source** and, since 2026-08-01, **Voice Leading** - that last
one is not a source, it is a pass over whatever a source produces, so it sits where none of
the seven can hide it. Row B is one band of settings that swaps with Source, all seven sharing
the same rect since only one is ever on screen: the weighted pool's note counts, inversions
and the two weighting sliders for **Algorithmic**; the chain controls for **Markov**; and,
added 2026-08-01, one band each for the five new brains below. Underneath is the reference
card and the 4x4 audition tray, with **Fill**, **Regen** and **Clear** on the tray's own
header row - none of the three writes a pad any more, they act on the tray (see above).

**Key**, **Mode** and **Scale Compliance** are also combo boxes on the Pads bar, which is the
fast way to reach the three you change most. Both places drive the same setting.

**Source is seven choices now** (2026-08-01, up from Algorithmic and Markov): Algorithmic,
Markov, Circle of Fifths, Neo-Riemannian, Progressions, Negative Harmony, Planing. The list is
*appended to*, never reordered - the parameter stores a plain choice index, so a session saved
as Markov still opens as Markov, and reordering the list would silently reopen every saved
session on a different brain.

| Setting | What it does |
|---------|--------------|
| **Source** | Which of the seven brains fills the page - see the table below |
| **Voice Leading** | 0-100%, in row A beside Source rather than in any band. A post-pass over whatever the source produced: each chord is revoiced to move as little as possible from the one before it. Applies to all seven sources, changes which octave notes sit in and never which notes a chord contains |
| **Key** | The tonic the chords are built from. Feeds every source |
| **Octave** | Which register the generated chords land in. Feeds every source |
| **Mode** | 12 modes. Read by Algorithmic only - see below |
| **Notes** | Algorithmic only: which chord sizes to build - **3** triads, **4** 7ths and 6ths, **5** 9ths and extensions |
| **Inversions** | Algorithmic only: **Root** position, and whether **1st** / **2nd** / **3rd** are allowed. An inversion lets a chord sit with its lower notes moved up an octave, so a progression moves less |
| **Scale Compliance** | Algorithmic only: how adventurous the chords are. At 100% every note stays in the key. Lower it and the generator borrows from related modes, then reaches for secondary dominants, then for anything at all |
| **Lock Influence** | Algorithmic only: how much the chords you locked steer the new ones |
| **Chain** | Markov only: Major, Minor or Modal chain tables |
| **Mood** | Markov only: learn only from progressions tagged with this mood |
| **Start** | Markov only: force the first chord (I, i, IV, V, vi, …) or let it pick |
| **Temperature** | Markov only, 0.30 to 2.00. Low sticks to the most common moves; high flattens toward anything the corpus has ever done |
| **Length** | Markov only: how many unique chords are generated (4 to 16) before the sequence loops to fill the page |
| **Direction** | Circle of Fifths only: flat-ward (down a 5th, the falling-fifth motion most progressions are built on) or sharp-ward (up a 5th) |
| **P / L / R** | Neo-Riemannian only: three relative weights, 0-100 each (all zero reads as equal thirds). P swaps major for minor on the same root; L is the leading-tone exchange; R moves to the relative major or minor. Each moves exactly one voice and holds the other two in place |
| **Progression** | Progressions only: a named template (ii-V-I, the axis I-V-vi-IV, 12-bar blues, Andalusian, Royal Road, rhythm changes, Coltrane's major-third cycle) transposed to your key, or **Random** to let it pick. A short template loops to fill the page, so a 3-chord ii-V-I asked for sixteen just repeats |
| **Diatonic** | Planing only, on by default: slides the chosen shape through the scale, bending its quality to fit each degree. Off slides it chromatically instead, preserving the exact shape - the Debussy sound |

**Negative Harmony has no band at all.** It mirrors the key about the axis between tonic and
dominant (C major becomes C minor, G major becomes F minor), and Key, Mode and Octave in row A
are the whole of what a reflection needs - an empty row B is more honest than a control
invented to fill it.

**Mode** greys out for every source except Algorithmic, not only under Markov - but greyed is
not the same as unread. Circle of Fifths, Neo-Riemannian and Progressions still read whatever
Mode was last set to (it decides the quality of each degree Circle of Fifths and Progressions
land on, and which triad Neo-Riemannian starts from); Negative Harmony and Planing take it as
the scale they reflect or slide through. **Scale Compliance** is what's actually dead outside
Algorithmic - none of the other six weigh a pool against it - which is why it greys alongside
Mode rather than the other way around. **Notes**, **Inversions** and **Lock Influence** belong
to the weighted pool alone and live in row B, so they leave the screen entirely under any
other source rather than sitting there clickable and silently ignored (which is what Octavium
did). **Mode** and **Scale Compliance** on the Pads bar grey with their twins here, and **Key** stays live, since
every source transposes to it. The **Mood** list follows whichever chain is up. **Mood** and
**Start** are choices about the progression you are generating right now rather than session
settings, so they are not saved - but they do survive closing and reopening the window.

Known simplifications, worth knowing rather than hiding: the Coltrane entry is the bare
major-third root cycle rather than full Giant Steps machinery, the 12-bar blues has no
quick-change or turnaround, and Locrian's diminished tonic gives Neo-Riemannian no proper
major-or-minor triad to start from, so it starts minor.

### Keeping what you like

**Lock** a chord you want to keep, from that pad's right-click menu. A locked card shows a dot
in its top-right corner. **Regen** then gives every other pad a new chord and leaves the locked
ones alone; **Fill** keeps them too. There is no page-wide clear left to spare it from - Clear
page is gone, and per-pad clearing already greys out on a locked card.

**Lock Influence** decides how much the locked chords steer the new ones. At a high setting,
locking three 7th chords biases what you get toward 7ths: it copies the *character* of what
you kept, not the chords themselves.

### Finding the next chord

**Next: could follow** asks what could follow that pad's chord, and offers four kinds of
answer, one submenu each:

- **Neo-Riemannian**: the moves that change as little as possible. Swap major for minor
  (P), or slide to a chord that shares two of its notes (L, R, N, S, H).
- **Circle of Fifths**: the dominant and subdominant, the pulls that make a progression
  feel like it's going somewhere.
- **Diatonic**: the other chords of the key (ii, iii, vi, vii).
- **Chromatic**: the borrowed and jazz moves (tritone substitution, minor plagal,
  Neapolitan, augmented sixth).

Every suggestion row has a **play** button that auditions it for a moment without
closing the menu, so you can shop by ear. Clicking the row itself takes it: your pick
goes into the first empty pad on the page, so you can build a progression left
to right by asking for one chord at a time. It only ever writes to an empty pad, so
**Next: could follow** greys out on a full page rather than replacing a chord you have.

**New chord** on a Markov pad steps the chain again from the pad to its left, avoiding the
chord it replaces. Locked pads are never overwritten, same as the algorithmic source.

## Arpeggiator

A section of its own, between the controls and the pads. It takes whatever is currently
sounding (keyboard, a held note, a chord pad) and plays it one note at a time. The chord
pads sit directly below it, so a chord is always one click away, and the knobs stay on
screen above.

**On** and **Hold off** live on the Arp bar rather than inside the panel, so folding the
panel away leaves the arpeggiator running, still switchable, and still able to let go of a
chord. **Detach** is on that bar too, but it hides with the fold the way every other
section's does. The section starts folded, so those two are usually all of it you can see.

**Hold off** releases the chord being held into the arp and stops the **Chain** if it is
running. The arp itself keeps running and goes back to arpeggiating whatever you play. It is
greyed out when there is nothing held and nothing chaining, and it is the way to stop a hold
outright: clicking the lit pad restrikes the chord rather than letting it go. A running
chain counts as something to let go of even in the gap where no chord happens to be
sounding, because it will fire the next one at the coming bar line.

**Shape decides how much of the panel exists.** The twelve shapes are plain arpeggios and
show nothing but the controls below. The last entry, **Pattern**, opens the step editor and
adds the STEPS group to the band.

Controls are grouped: **PATTERN** is what it plays, **PLAYBACK** is how it behaves,
**SPREAD** is how wide it reaches and where it starts, **FEEL** is whether it sounds played,
and **STEPS** (Pattern shape only) drives the step editor. The `<` and `>` buttons beside
Shape and Rate step to the next entry without opening the menu; they stop at the ends rather
than wrapping round.

**Rate is a dial with two units.** The chip beside it reads the one that is live, **Sync** or
**Hz**, and one click swaps them. In Sync the dial detents onto the eleven divisions, so it
cannot land between two, and the readout under it says "1/8" or "4 bars". In Hz it sweeps a
frequency and the readout says "4.00 Hz". The `<` and `>` beside it are the click-only path
to every value in both units: in Sync a click is one division, in Hz it is a quarter of an
octave, so four clicks halve or double the rate, which is the same jump one entry of the Sync
list makes. In Hz nothing is following a transport, so a beat is a second there: the two
settings counted in beats, **Retrigger**'s clock windows and the Ramp's **Time**, are counted
in seconds instead.

| Control | Group | What it does |
|---------|-------|--------------|
| **On** | header | Arp on or off. Everything else stays editable while it is off. |
| **Shape** | Pattern | Up, Down, Up-Down, Down-Up, Up & Down, Down & Up, As Played, Reversed, **Random**, **Random Other** (never the same note twice running), **Random Once** (a shuffled order, kept for as long as the chord is held), **Chord** (every note of the chord on every step, so the arp plays rhythm instead of a run), or **Pattern** (opens the step editor). |
| **Rate** | Pattern | A dial. In **Sync**, step length from 16 bars down to 1/64, detented onto the eleven divisions. In **Hz**, a free-running 0.031 to 32 Hz, which is the same span those divisions cover at 120 bpm. The Hz dial is exponential: ten octaves, a tenth of the travel each, so 1 Hz sits at the centre and a degree of the dial is the same *ratio* wherever you are on it. |
| **Sync** / **Hz** | Pattern | Which unit the dial is in. Sync follows the host tempo and its bar grid; Hz ignores both and runs whether the transport rolls or not. The chip reads the live unit and lights in Hz. |
| **Dot** / **Trip** | Pattern | Dotted or triplet feel on the rate. Greyed out in Hz: they subdivide a beat, and there is no beat there. |
| **Swing** | Playback | −0.75 – +0.75, starting centred. Shifts the offbeat steps: right delays them for a shuffle, left pulls them early to rush the beat, centre is dead straight. |
| **Gate** | Playback | 5–200%. Note length as a share of the step; over 100% ties into the next one. Works on **any** shape, and multiplies the Gate lane when you are using one. |
| **Chance** | Playback | 0–100%. How likely each step is to fire — turn it down to thin a run out. Works on any shape, and multiplies the Probability lane. |
| **Retrigger** | Playback | When the pattern starts over: **Off**, **Note** (a new chord restarts it), or a clock window — 1 or 2 beats, 1, 2 or 4 bars. A clock window is what makes a five-step lane still land on the bar. |
| **Anchor** | Playback | On: steps lock to the host's bar grid, so the arp lines up after a jump. Off: free-running, never jumps, may drift. Greyed out in Hz, alongside Dot and Trip: a free-running rate follows no bar grid, so there is nothing there to anchor to. |
| **Latch** | Playback | Keep arpeggiating after you let go, until a new chord arrives. |
| **Repeats** | Spread | 1–4. How many times the chord is stacked up the keyboard before the run repeats. (This was "Octaves", back when an octave was the only thing it could stack by.) |
| **Distance** | Spread | How far each repeat goes: **Octave**, **5th**, **4th**, **Maj 3rd**, **min 3rd**, or the scale-relative **Scale 2nd / 3rd / 5th / 7th**. The scale entries count degrees of Root and Scale, so a third stays a third *of this key* — C lifts to E, D lifts to F — which is the one thing the stock arps cannot do. |
| **Offset** | Spread | 0–31. Start the run further in. Rotates the step lanes and the walk together, so the same pattern can be heard from a different foot without redrawing it. |
| **Ramp** | Feel | −100 – +100%. Velocity change over **Time**, counted from the moment a chord starts. Left fades a held chord away, right swells it, centre is flat. |
| **Time** | Feel | 1–32 beats. How long the Ramp takes. |
| **Human** | Feel | 0–100%. Nudges each hit a little late and a little quieter, by a different amount every time. At 0 the arp is dead on the grid, which is what it always was before this control existed. |

### The step editor (Shape → Pattern)

Ten lanes, shown **one at a time** through the tabs: pick a tab, edit that lane. Click a
step to set it, or drag across the grid to paint several in one gesture; a readout
follows the cursor. Nothing needs a modifier, a double-click or the keyboard.

| Lane | Range | Meaning |
|------|-------|---------|
| **Note** | 1–8, or follow | Which note of the held chord this step plays. "Follow" (the dot) leaves it to the shape. |
| **Octave** | −3 – +3 | Octaves added to this step. |
| **Velocity** | 10–200% | Scales the velocity you played at. |
| **Gate** | 5–200% | Note length as a share of the step. Over 100% ties into the next step. |
| **Ratchet** | 1–4 | Sub-hits packed into this step. |
| **Prob** | 0–100% | Chance this step fires at all. |
| **Transpose** | −7 – +7 | Scale **degrees**, not semitones: +2 lifts each note a third *of your key*, so it can never land outside it. |
| **Late** | 0–90% | Pushes this step later by that share of a step. Draw a little on the offbeats for a lazy feel, or a lot on one step to make it stumble. Late only — Swing is the control that can also rush. |
| **Harmony** | 0–7 | Adds a second voice this many chord tones above the note the step plays. Off at 0. |
| **Chord** | 1–12, or off | This step plays the chord stored in that **arp slot** instead of a note of what you are holding. Draw four of them across a lane and the arp plays a progression on its own. Off (the dot) is a normal step, and a slot with no chord in it is left alone rather than silenced. |

The **Mute** row under the grid silences individual steps without disturbing their
values, so you can take a step out and put it back unchanged.

### Playing the row as a progression

The twelve slot cards hold a chord each. **Chain** (on the button row) plays the ones that
hold a chord, one after another, each for the number of bars on its card — one click and the
row is a twelve-chord song. Click it again to stop.

**Bars** (the `-` `+` beside it) sets how long the **selected** slot lasts, 1 to 16. Clicking
a slot card selects it (as well as launching it), so setting a length is: click the card,
click the plus. A card shows `x2` and up in its top-right corner; a slot of one bar stays
quiet about it. Slots with no chord are skipped — a pattern-only slot is somewhere to keep a
rhythm, not a step of a progression.

Switching the arp **Off** stops the chain and releases whatever it was holding.

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
| **Stop** | Releases the chord being held into the arp and stops the Chain, without the blunt instrument of All Off. The same button as **Hold off** on the section bar, which is the one that is still on screen when the panel is folded. |
| **Randomize** | Rerolls the live pattern's lanes. Pattern shape only, since a plain shape has no lanes. |

Right-clicking a slot offers Launch, Clear chord, Copy and Randomize, if you prefer a menu.

### Getting a chord into the arp

Two ways, both from the Pads section under the panel:

- **Click a chord card while the arp is on.** It hands that chord to the arp and leaves it
  there, and the pad wears a bright ring while it is the one feeding it. Click **another**
  card to swap. Clicking the **lit** one again **strikes the chord afresh**, the way a second
  press on a beat pad re-fires it: it is a retrigger, not a release, and it never doubles up
  on the notes it is already holding. To stop the hold outright, use **Hold off** on the arp
  bar (or **Stop** in the panel, which is the same button). The live chord card hands its
  chord over the same way, and takes no ring. With the arp **off**, the pads play beat-pad
  style exactly as they always have, and switching the arp off while a card is feeding it
  releases that chord. (This used to need arming a separate **To Arp** toggle, which looked
  like it did nothing whenever the arp was off; it went on 2026-07-27.)

  One card does release rather than retrigger: a card you **cleared** while it was still the
  one feeding the arp. It keeps the ring with no notes behind it, so there is nothing to
  re-play, and the click means the only other thing it can mean. That is the ring's own way
  out, and the reason a cleared card is still drawn with one.
- **Send to arp slot**, in a pad's right-click menu, parks a copy of that chord in one of
  the twelve slots to launch later.
