# Controls

Every control is operated with a single left-click, a drag, or the scroll wheel.
Nothing needs the keyboard, a double-click, or a modifier key. The right-click gestures
below are accelerators, and the work each of them does has a left-click path too, bar two
exceptions Owen signed off:

- **Lock**, in a chord pad's menu. It had a clickable chip in the card's corner for a few hours on
  2026-07-30 and lost it at Owen's request: the chip took roughly a quarter of the card away
  from playing it, dragging it and feeding the arp. A locked card still *shows* a corner dot,
  so the state reads without opening the menu; the dot is a marking and not a target.
- **Releasing one note out of a chord the Sustain pedal is holding.** Under Sustain a left
  click on a ringing key strikes it again, by design, so the second click cannot also be the
  release. Right-click is the only way to take that one note out. Under **Latch** the left
  click does release, so this exception is Sustain's alone.

**Send to arp slot** was a third exception until 2026-08-01, on the grounds that binding a
chord to one particular slot needs a target picker. It has one now: **dragging a chord card
onto a slot card** does the same job, and a drag *is* a target picker. The menu item stays as
the accelerator it always was.

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

Right-click can only reach notes **this keyboard** is holding. Keys lit by a chord pad, an
arpeggiator line or MCP are owned elsewhere and are left alone: use the pad, **Hold off** on
the arp bar, or **All Off**.

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
| **Controls** | top | Strum, Dir, and the eight knobs in the row beneath them - the whole band, down to this one row since Size, Octave and Humanize left it on 2026-08-02 for the Keyboard and Pads bars (see those rows below), and the **Knobs** chip that used to fold the knob row off went with them: the row is unconditional now, so it goes with the section rather than folding on its own. **Tempo**, **Sync**, **Root**, **Scale**, **Scale Lock**, **Voices**, **MIDI Ch** and, in Keys Host, the **Instrument** chip live on the bar itself, so they, and the theme swatch beside them, stay put whatever you fold. |
| **Arp** | below the controls | The two arpeggiator lines. **A** and **B** are that line's own On switch now (2026-08-02, seventh pass) as well as a chord-drop target, and neither hides with the fold any more; **Hold off**, **All Off**, **Light keys** and **Launch Quantize** ride the same bar for the same reason - a chord can be held into a folded arp, and letting it go or switching a line off cannot live behind the fold that hid it. **All**, the one navigation control left on this bar, still hides with the fold: it only opens the macro view, and there is nothing to open once the section is off screen. **Detach** rides on it too, but goes with the fold like every other section's. Folding it puts the editor away, never the arpeggiators. |
| **Pads** | below the arp | The sixteen chord pads and the live chord card. The four page buttons ride at the left of the bar and fold with the strip; **Humanize** and its velocity range sit after them and never do (2026-08-02), the same reach-for-it-while-playing case as the generator's **Fill**, **Regen** and **Generator** chips and its **Key** combo further right. Its **Mode** and **Scale Compliance** combos, and the arp's old target-line letter, left this bar on 2026-08-02 (Owen: "remove the scale and percentage and letter b from pads header") - Mode and Scale Compliance are still in the generator's own window, and the arp bar's **A / B** switches no longer name a line at all, since they read On/Off rather than a selection. |
| **Keyboard** | above the keys | The keybed. **Wheels** folds the mod and pitch wheels; **Size**, **Octave**, Exclusive, Sustain, Latch and All Off all stay put - Size and Octave arrived here from the Controls band on 2026-08-02 (Owen: "the size can go down to the header of the keyboard button"). |

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
large as the screen allows. That window carries its own **Size**, **Octave** and **Wheels**
controls alongside Re-dock, because those belong to the keybed rather than to the editor - Size
and Octave simply travel with the bar now (2026-08-02), rather than the second, duplicate Size
combo the detached window used to build for itself back when Size lived in the Controls band.

What stays behind on a bar is whatever belongs to the editor rather than to the section: the
arp's **A** / **B** switches and **Hold off** chip, **Launch Quantize**, the pad page buttons,
the generator's **Fill**, **Regen** and **Generator** chips
with the **Key** combo beside them, **Tempo**, **Sync**, **Root**, **Scale**, **Scale Lock**,
**Voices**, **MIDI Ch**, the **Instrument** chip (Keys Host only), and the theme swatch. All of them keep
working while the section they name is off in a window.

Folding is the other case, and a stricter one: a folded bar keeps only what still means
something with the section gone. That is **A**, **B** and **Hold off** (so the arpeggiators run
on behind a closed panel, and can still be made to let go of a chord - A and B are that line's
own On switch now, 2026-08-02, so they mean exactly as much folded as open), plus **Launch
Quantize**, which is a plain setting rather than a way into the panel. **All** does not
survive: it exists to open the macro view, and there is nothing to open once the section is
gone, so it hides with the section - the one navigation control left on this bar, and the one
bar control here that folds. **Fill**, **Regen**, **Generator** and the **Key** combo with them
(the whole left-click path into the chord generator - generating into a folded strip is a fine
thing to mean, and the setting must not fold away with the cards), **Tempo**,
**Sync**, **Root**, **Scale**, **Scale Lock**, **Voices**, **MIDI Ch**, **Size** and **Octave**
(parameters you reach for while playing, same as Sustain and Latch on the Keyboard bar), the
**Instrument** chip, and the theme swatch (it colours the whole plugin). Everything else goes
with the section, Detach included.

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
what makes them 60 px across. There is no way to hide just this row any more: the **Knobs**
chip that used to fold it off the Controls bar is gone (2026-08-02, Owen: "remove the knobs
button and make the knobs visible when you open controls"), so the row shows whenever the
Controls section itself is open. A session saved with the row folded away opens with it
visible again - there is no control left on screen that could turn it back off, so the
persisted setting is ignored on load rather than honoured.

## Top bar

Six of these sit on the *Controls bar* itself rather than in the section's band (moved there
2026-08-02, Owen's ask), so they stay reachable however you fold the section, the same as the
theme swatch always has: **Tempo**, **Root**, **Scale**, **Scale Lock**, **Voices** and **MIDI
Ch**. A seventh, the **Instrument** chip, joined them the same day but only shows up in Keys
Host. An eighth, **Sync**, joined the same day too, beside Tempo. Size and Octave left the band
entirely later the same day, for the Keyboard bar; Humanize and its Velocity range left for the
Pads bar. **Strum** and **Dir** followed them into the *pads strip* on 2026-08-03, which left
the band with nothing in it: this section is the CC knob bank alone now, plus everything on its
bar.

| Control | Type | What it does |
|---------|------|--------------|
| **Tempo** | number, `<` `>` steppers | 40–240 (Ableton style: drag the number up or down, or click the steppers). The tempo anything timed in beats runs at - today that is the arpeggiator alone - when there is no transport to follow, or when **Sync** (below) is off. Its own caption, **BPM**, sits to its left. On the *Controls bar*, so it stays reachable with that section folded. |
| **Sync** | toggle | Tempo Sync (`bpmSync`, default on). On: a host that is *playing* always wins, matching Keys' behaviour before this toggle existed. Off: the arp and the progression chain stay on the Tempo field above even while the host rolls, the escape hatch for someone who wants Keys' own clock regardless of the DAW's transport. While Sync is on and a host tempo is actually live, the Tempo field shows the host's own number and greys out - the field and its `<` `>` steppers cannot change anything in that state. No effect in the standalone, which has no host transport to defer to, and no effect on the arp rate while it is in Hz, which never reads a transport at all. On the *Controls bar*, beside Tempo. |
| **Root** | dropdown | Tonic used by Scale Lock (C … B). On the *Controls bar*. |
| **Scale** | dropdown | Scale used by Scale Lock (Major, Natural/Harmonic/Melodic Minor, the modes, pentatonics, Blues, Whole Tone, Chromatic). On the *Controls bar*. |
| **Scale Lock** | toggle | On: each played note snaps to the nearest note in (Root, Scale); out-of-scale keys are dimmed so you see the shape. You cannot play a wrong note. Its on-screen text is just "Lock" (the bar has no room for both words); the accessible name stays "Scale Lock". On the *Controls bar*. |
| **Voices** | dropdown | Polyphony limit: **Off** (unlimited) or **1–8** notes. Playing past the limit steals the oldest note. On the *Controls bar*. |
| **MIDI Ch** | dropdown | Output channel, 1–16. Its on-screen caption is "CH". On the *Controls bar*. |
| **Instrument** | chip → menu | Keys Host only (2026-08-02, Owen: "the load instrument section with all that should go in the controls submenu"): Load instrument…, Show/Hide instrument GUI, and Eject, with the loaded instrument's name as the chip's own caption. Invisible in plain Keys, which never wires it up - the chip and its gap simply aren't reserved. The one *elastic* control on this bar: it gets whatever width the tempo group and the Root…MIDI Ch group leave over. |
| **Strum** | range knob, in the *pads strip* | Spread a chord's notes instead of playing them together, over a time drawn from a 0–200 ms band — so repeated stabs do not all rake at the same speed. The knob is the longest it ever takes, the ring reaches back from it, and the **lamp** beside it switches strum off and on (off is simply zero: the chord lands all at once). Applies to chord pads and the live chord card. |
| **Dir** | `<` `>` by the caption | Strum direction: **Up** (low→high), **Down** (high→low), or **Random**. The caption reads the live one — `STRUM UP`, `STRUM DOWN`, `STRUM RAND` — and the arrows wrap. |
| **Theme** | swatch | Colours this instance (Cyan, Amber, Lime, Violet, Magenta, Orange, Rose, Ice), so you can tell it from Keys on your other tracks. Per instance, saved with the session. Sits on the *Controls bar*, so it stays reachable with that section folded. |
| **Update to vX.Y.Z** | button | Appears only when a newer signed release exists. One click downloads, verifies, and launches the installer. |

These sit on the **Keyboard bar** rather than in a section, so folding anything away never
takes them with it — they are what you reach for while playing. Size and Octave arrived here
from the Controls band on 2026-08-02 (Owen: "the size can go down to the header of the keyboard
button"):

| Control | Type | What it does |
|---------|------|--------------|
| **Size** | dropdown | 25 / 49 / 61 / 73 / 76 / 88 keys. The keyboard re-lays out immediately. |
| **Octave** | `<` value `>` | Transpose the whole keyboard, -5..+5 octaves, reading "+2" / "0" / "-3". Click the arrows only - no slider, no scroll: a bar control is 24 px tall, and the arrows on a slider styled this way would stack to 12 px each, under the mouse-only floor. |
| **Exclusive** | toggle | Playing a chord pad chokes the previously-playing pad, so only one pad chord sounds at a time. |
| **Sustain** | toggle | On: notes keep sounding after you release the mouse, like a sustain pedal. With the pedal down a glide leaves a trail, and clicking a key that is already ringing **strikes it again** — the pedal never turns a key into a switch. Turn Sustain off (or click All Off) to release everything. |
| **Latch** | toggle | On: clicking a key holds it, clicking it again releases it. This is the one to use to build a chord note by note, or to take one apart. Turning Latch off releases everything it was holding. |
| **All Off** | button | Panic. Stops every note on every channel, and drops anything a strum still had queued. |

And these sit on the **Pads bar**, after the four page buttons and before the chord generator's
own controls (2026-08-02 - Owen wasn't sure where Humanize belonged: "maybe that could go in
the pad header", then "make smaller to fit" once it landed there):

| Control | Type | What it does |
|---------|------|--------------|
| **Humanize** | range knob, in the *pads strip* | How hard Keys plays. The knob is the hardest a note ever lands and the ring reaches back from it, so each note takes a random velocity inside that band and a part stops sounding typed in. The **lamp** beside it is the on/off: lit, notes are drawn from the band; unlit, the arc collapses to an ordinary one and every note plays the band's **midpoint**, which is the single number the readout then shows. (It had a separate tick box until 2026-08-03; the lamp says the same thing without a second control.) |

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
gesture in Keys with no left-click twin besides Lock, and Owen asked for it: without it,
taking one wrong note out of a sustained chord meant lifting Sustain and losing the lot.
**All Off** is still the blunt way out.

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
   the greyed-out **Clear pad** on its menu gives. What you drag is the card itself, at full
   size, so its lock dot travels with it and says which of the two this drag is doing. (Until
   2026-08-02 the ghost was a small chip that faded once you were over nothing; the chip is gone
   because the real card can now follow the cursor out of the window entirely, which is what
   makes the generator's reference box something you can aim at.)
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

There are **four pages** of twelve pads (sixteen until 2026-08-03), picked by the four numbered buttons on the
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
| **Send to arp slot** | Park a copy of this chord in one of the current line's twelve arp slots, to launch later. Its left-click twin is dragging this card onto the slot you mean |

**One of these has no left-click twin**, and it is deliberate: **Lock**, which had a clickable
chip in the card's corner for a few hours and lost it at Owen's request. The chip took roughly
a quarter of the card, and every bit of that quarter had stopped playing the chord, starting a
drag or feeding the arpeggiator. The whole card is the card again. (**Send to arp slot** was
the other until 2026-08-01; dragging the card onto a slot does the same job now, and a drag is
the target picker the menu item existed to be.)

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

The generator is **three chips, one combo box, a window (with its own tray, reference
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
  out. **Changing a generator setting generates nothing** (2026-08-01, Owen: "I don't want
  it to auto generate when you change a source"): the tray rerolled on any settings change
  for part of that day, and sweeping Source to compare the seven of them threw the tray
  away six times on the way past - a control you cannot explore without destroying your
  work is a control you stop touching. The tray's caption now just says the candidates are
  stale ("settings changed since these were generated. Regen for new ones.") and waits for
  a press of **Fill** or **Regen**.
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
- **Key**, a combo box beside the chips. It is the one setting you change most while
  auditioning a page, so it is on the bar as well as in the window: one click opens the list,
  one picks, and a change in one place shows in the other immediately. **Mode** and **Scale
  Compliance** used to sit beside it here too, but left the bar on 2026-08-02 (Owen: "remove
  the scale and percentage and letter b from pads header") - they are still both in the
  generator's window, which holds every setting it has, so nothing became unreachable, only
  slower to change mid-page.
- All four of those stay clickable when the Pads section is folded away. Folding the strip must
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

1. **Pick a key.** Set **Key** from the combo box on the Pads bar, and **Mode** from the
   generator's window (Mode left the bar on 2026-08-02; Key is the only one of the two still
   there). Both are also in the window if you would rather change them there together. These
   two are the generator's own, separate from the **Root** and **Scale** on the *Controls* bar
   that drive Scale Lock, so move those to match if you want Scale Lock to agree with the
   chords you're about to get.
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
outright. **Changing a generator setting no longer touches the tray** - it only makes the
tray's caption say the candidates are stale, and Fill or Regen is what actually generates.
**Right-click a card** for Send to
first empty pad, the two Fill-tray-from-this-seed options, the three shaping edits, or New
chord here / Clear this card - see the chord generator overview above. The **reference card**
holds one chord none of that can touch, filled by dragging a tray card or a main-window pad
onto it, with its own Similar / Could follow / Clear. Closing the window loses whatever you
never dragged onto a pad - the tray (and the reference card) were never part of the session,
which is what lets you audition a dozen chords for the one you keep.

### Generator settings

All of these live in the generator's window, opened by the **Generator** chip on the Pads bar,
and reworked 2026-08-01 into a shape that stays put as you switch source, top to bottom:

1. **Key and Mode**, each with a tick box, then **Brightness** and **Lean** - two sliders
   beside them.
2. **Source**: seven always-visible buttons, not a dropdown - see the table below. Directly
   under them, a read-only **diagram** of what the current source is doing.
3. A fixed row of **Notes**, **Inversions** and **Octave**, each with its own tick box.
4. A second fixed row of **Scale Compliance** (ticked), **Lock Influence** and **Smooth
   Voicing** (neither of the last two has a box - see below).
5. **The band**: one row of settings that swaps with Source - the chain controls for
   **Markov**, Direction for **Circle of Fifths**, P/L/R for **Neo-Riemannian**, and so on.
   Algorithmic and Negative Harmony have **no band at all**: the row collapses to zero height
   and the freed space goes to the tray, so the window never resizes as you switch source.
6. The reference card and the 4x4 audition tray, with **Fill**, **Regen** and **Clear** on
   the tray's own header row - none of the three writes a pad any more, they act on the tray
   (see above).

**Key** is also a combo box on the Pads bar, the fast way to reach the one of these you change
most mid-page. Both places drive the same setting. **Mode** and **Scale Compliance** used to
sit beside it there too, but left the bar 2026-08-02 (Owen: "remove the scale and percentage
and letter b from pads header") - this window is the only place left to reach them.

#### Source is seven always-visible buttons

**Source** stopped being a dropdown 2026-08-01 (Owen: "maybe instead of the source being a
drop down and the direction being a drop down, maybe those can be, like, always visible") -
one click instead of two, and all seven answers on screen instead of six hidden behind the
first, for a setting whose whole point is comparison. **Circle Direction**, the one
Circle-of-Fifths setting, got the same treatment: two buttons rather than a combo. Neither has
an `AudioProcessorValueTreeState` attachment, because JUCE has none for a row of buttons on
one choice parameter; a click writes `genSource` / `genCircleDir` directly and the window
polls the parameter back onto the tick mark, so nothing underneath changed and the Pads bar's
own **Key** combo still agrees with the window exactly as it did before.

| Source | What it does |
|--------|--------------|
| **Algorithmic** | The weighted pool: gated by Scale Compliance and re-weighted toward locked chords by Lock Influence. No band of its own |
| **Markov** | Real-progression chains per Major / Minor / Modal, with **Chain**, **Mood**, **Start**, **Temperature** and **Length** |
| **Circle of Fifths** | Walks the circle from the tonic with a **Direction** (flat-ward or sharp-ward), landing on each degree's own diatonic quality where it's in the key |
| **Neo-Riemannian** | Moves the tonic triad by P, L or R, weighted by three sliders - the smoothest, most key-ambiguous of the seven, since each move changes exactly one note |
| **Progressions** | Transposes a named **Progression** template (ii-V-I, the axis, 12-bar blues, Andalusian, Royal Road, rhythm changes, Coltrane's major-third cycle, or Random) to your key |
| **Negative Harmony** | Mirrors the key about the tonic/dominant axis (C major becomes C minor). No band of its own - Key, Mode and Octave are enough |
| **Planing** | Slides one chord shape up or down, diatonically or (**Diatonic** off) chromatically - the constant-structure sound |

The list is *appended to*, never reordered - the parameter stores a plain choice index, so a
session saved as Markov still opens as Markov, and reordering the list would silently reopen
every saved session on a different brain.

**A diagram under the Source buttons** (`SourceViz`, 2026-08-01, Owen: "a visualization for
the generation source so people understand what it's doing") draws the shape the current
source walks - a circle-of-fifths wheel, the Neo-Riemannian P/L/R triangle with the actual
transform sequence as chips, roman-numeral strips for Progressions and Markov, a mirror-axis
clock for Negative Harmony, sliding note-stacks for Planing, degree columns for Algorithmic -
and highlights the walk that produced whatever is currently in the tray. It is a picture and
nothing else: click-through, takes no input, writes nothing, and it draws its static figure
even with an empty tray, so it explains the source before you have generated anything.

#### Always-visible settings

| Setting | Box | What it does |
|---------|:---:|--------------|
| **Key** | Y | The tonic the chords are built from. Feeds every source |
| **Mode** | Y | Feeds every source except Markov, which has no scale in it at all - see below |
| **Brightness** | - | Sweeps the seven diatonic modes bright to dark: Lydian, Major, Mixolydian, Dorian, Minor, Phrygian, Locrian - the circle-of-fifths ordering. A **view onto Mode**, not a second parameter; greys and holds its last position when Mode is one of the off-axis scales (harmonic minor, melodic minor, blues, the two pentatonics) |
| **Lean** | - | -100 to 100, 0 = neutral. Moves generated chords' **thirds** major or minor whatever the mode. The size is the *probability* a given chord gets pushed, not how far - only the third ever moves |
| **Notes** | Y | A range, 2 to 11 notes (steppers, not a slider - a slider is a drag target). Below 3 you get dyads; above 5 the stack keeps climbing in thirds **through the mode**, so 11 is a chord covering every degree and still in the key |
| **Inversions** | Y | **Root**, **1st**, **2nd**, **3rd** tick boxes. **Replaces** the rotation a chord arrived in rather than compounding with it - root position first, then invert - so ticking only Root gives root position even from a source that had already inverted one |
| **Octave** | Y | A range (steppers), which register the generated chords may land in |
| **Scale Compliance** | Y | Algorithmic only: how adventurous the chords are. At 100% every note stays in the key. Lower it and the generator borrows from related modes, then reaches for secondary dominants, then for anything at all |
| **Lock Influence** | - | Algorithmic only: how much the chords you locked steer the new ones. No box - 0 already means off |
| **Smooth Voicing** | - | All seven sources: renamed from "Voice Leading" 2026-08-01 (Owen: "I don't understand what the voice reading does"). Chooses which octave each note sits in so consecutive chords stay close on the keyboard - never which chords you get or which notes they contain. No box - 0% already means off |

**Notes and Inversions moved out of the Algorithmic band** the same day: both are facts about
the *voicing* a chord arrives in, not about which chord it is, so they are applied as
post-passes to whatever any of the seven sources produced rather than living inside the
weighted pool. **Notes and Octave replace the old 3/4/5 note-count tick boxes** with a
continuous range.

**Six tick boxes** let generation off the leash a setting at a time (2026-08-01, Owen: "check
marks for the different sliders and options that enable or disable them"). Ticked, the setting
constrains generation; unticked, the generator rolls that choice itself - an unticked Key
wanders, an unticked Notes range rolls anywhere in 2 to 11. **Key and Mode roll once per
generation, not per chord**, because every source takes a single root and mode for a whole
batch (a circle walk, a chain step, a progression transposed), and a free Mode picks only from
the seven diatonic modes. **Lock Influence, Smooth Voicing and Lean have no box**: each already
has an off position on its own dial, so a box beside it would be a second control for what
zero already says.

**Mode greys out for Markov only**, not for every other source - a Markov chain walks bigram
tables rather than a scale, so Mode means nothing to it, but Circle of Fifths, Neo-Riemannian,
Progressions, Negative Harmony and Planing all still read whatever Mode was last set to (it
decides the quality of each degree Circle of Fifths and Progressions land on, which triad
Neo-Riemannian starts from, and the scale Negative Harmony and Planing reflect or slide
through). **Scale Compliance and Lock Influence are what's actually Algorithmic-only** - none
of the other six weigh a pool against either - which is a different question from Mode's and
is why the two grey on a different condition. Both live only in this window now (they left the
Pads bar 2026-08-02); **Key**, which is still on that bar too, stays live either place, since
every source transposes to it.
The **Mood** list follows whichever chain is up. **Mood** and **Start** are choices about the
progression you are generating right now rather than session settings, so they are not saved -
but they do survive closing and reopening the window.

**Changing any of this generates nothing** (2026-08-01, Owen: "I don't want it to auto
generate when you change a source"). Every control above only sets up what the *next* Fill or
Regen will produce; the tray's own caption says when what's on screen no longer matches the
settings, and generating is Fill and Regen and nothing else - see "Auditioning before you
commit" above.

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

### Two lines

There are **two arpeggiators**, A and B, and they run at once. Each has its own rate, shape,
step pattern, twelve slots, chord and chain, so 1/8 against a 1/8 triplet is a polyrhythm out
of one plugin rather than two.

**A and B on the Arp bar are that line's own On switch** (2026-08-02, seventh pass, Owen: "the A
and B on the left side of the header, I want those to be on and off buttons to turn on or off
the ARP"). They live on the bar rather than inside the panel so folding it away leaves the
arpeggiators running and still switchable, and they are also where you drop a chord card to
hand it to that line. **B starts off**, and with it off Keys behaves exactly as it did when
there was one arpeggiator — which is also what a session saved before the lines does when you
open it. A and B used to have a second job too - choosing which line the panel edited - with a
separate lettered On chip doing the actual switching a few pixels away, near Hold off. Owen
called that redundant ("we can remove the a and b check mark on the right side of the header")
and the chip is gone: A and B are the switch, full time, which is also why they no longer hide
when the section folds - a power switch is something you reach for while playing, the same
case Hold off and Quantize already made for staying on a folded bar.

Beside them: **Hold off** lets go of every held chord and stops every chain, leaving the lines
running. **All Off** does that *and* switches the lines off, which is the difference — release
alone hands the engines straight back to whatever the keybed is holding. **Light keys** lights
the on-screen keyboard for the notes the arp is playing; it is display only and changes nothing
you hear.

**The panel edits one line at a time**, chosen now by that line's own **Details** button on its
macro card (2026-08-02, seventh pass, Owen: "maybe we can add another button on the bottom by
anchor, like details, and that can open up the detailed arpeggiator view") - A and B stopped
doing this the day they became the On switch. Everything on the panel follows whichever line
Details last opened: the rate, the shape, the step lanes, the twelve slot cards, Bars and
Chain. A small **LINE A** / **LINE B** caption in the panel's own top margin says which one you
are looking at, since nothing on the arp bar names it any more.

### The macro view: both at once

**All**, on the Arp bar, is where a polyrhythm gets built, and it is the view Keys opens in. It
replaces the per-line band and the step editor with **one boxed card per line, side by side**
(2026-08-02, Owen: "having the arpeggiators parallel to each other instead of one on top of the
other"), each drawing its own captioned, ruled frame - **LINE A**, **LINE B** - because a frame
around both cards together was what made two arpeggiators read as one ("we need a bit more
clear delineation between the two arpeggiators. They kinda look like one right now"). A card
holds:

| | |
|---|---|
| **Rate** | a knob, detented onto the divisions, with `<` `>` beside it and a **Sync / Hz** switch. A time division in Sync, a frequency in Hz. The readout is the step length as a plain fraction of a bar, modifiers included: `1/8`, `1/8.` dotted, `1/10` in fives. This is where a polyrhythm comes from: put one line on 1/8 and another on a 1/8 triplet |
| **< shape >** | its shape, including Pattern (whose step editor is on that line's own Details view) |
| **Dot / Tuplet / Anchor** | on a strip under the rate. Dotted steps, tuplet steps, and whether the run locks to the host's bar grid. Tuplet is a list — Straight, Triplet, 5-tuplet, 7-tuplet, 9-tuplet — fitting that many steps into the space a power of two would take; the rate readout shows the result, so 1/4 in fives reads `1/5`. All three grey out in Hz, where there is no beat to divide and no grid to lock to |
| **Details** | opens that line's deep view - the band, and the step editor on Pattern shape. The only way there from this view (2026-08-02, seventh pass); A and B on the Arp bar used to do it and are that line's On switch now instead |
| **Oct** | transposes that line's whole run up or down, centred at zero. (The upward-only stacking *range*, `arpOctaves`, is on the line's own Details view beside Distance - "how far does it reach" is a different question from "how high does it sit") |
| **Gate** | how much of each step its notes fill. Short gates let the other line through |
| **Chance** | how often a step fires at all. Thin one line out and the other shows through |
| **Swing** | shifts its offbeats late or early. The quickest way to stop two lines landing on top of each other |
| **Offset** | starts its pattern from a different foot. Two lines on the same rate and different offsets are out of phase rather than in unison |
| **Vel** | how loud that line plays, bipolar around "as played": centre is unchanged, right boosts, left cuts, hard left mutes exactly. Squared rather than linear, so the fader's change is even across its travel instead of crammed into the last few degrees |
| **H.Time** | nudges each hit a little late, at random. At 0 the line is dead on the grid. A **range knob**: the knob is the most it ever nudges, and the ring around it is how far under that a hit can fall. Drag the little dial at its top left — or anywhere on the ring — to open and close it. Wide open it draws from nothing, which is what this did before it had a ring; closed it is a fixed nudge with no randomness left. Turn the knob and the whole range moves with it |
| **H.Vel** | shaves each hit's velocity, at random - independent of Vel and of H.Time, so level and humanize no longer fight over one knob. A **range knob** like H.Time: knob for the most, ring for how far under it. Closed is a fixed cut, wide open is a draw from nothing |
| the chord | what that line is holding, or `...` while a quantized launch is waiting |

Eight knobs, a rate with its three modifiers, a shape, Dot/Tuplet/Anchor and Details: every
setting a regular arpeggiator has, both lines deep, on one screen. Latch, **Play** and Chain
still exist per line, just not on this card - Latch and Play live on that line's own Details
view (the band), Chain on its action row under the twelve slots. The card's own On switch is
gone outright (2026-08-02, seventh pass): A and B on the Arp bar are that line's actual On
switch now, and two of them for one parameter was a control to get wrong twice.

**A line that is off scrims its whole card, rather than losing a control from it** (2026-08-02,
seventh pass, Owen: "if it's turned off, gray it out below"). A translucent grey fills the card
body under the **LINE A** / **LINE B** caption, which stays legible, and every knob and the
rate dial stay fully clickable even while it is greyed - both so you can dial a rate in before
switching the line on, and because a chord dropped onto an off line still has to land.

There is nothing shared under the cards any more: the **Tempo** and **Launch Quantize** that
used to sit there moved out on 2026-08-02, Tempo to the *Controls* bar and Quantize to the
*Arp* bar itself, alongside **A**, **B** and **All**. The macro view is now just the two cards,
side by side.

The knobs are the same rotary the band above uses for the same settings, with each column
heading written once at the top rather than repeated down every row. Rate is a knob too, but it
keeps its `<` `>` steppers: a knob is a drag target, and those are the click-only path to every
division. Clicking **All** selects the view; a card's own **Details** button takes you into
that line's deep controls.

**All is a view, not another line.** Whichever line Details last opened stays the edited one,
so a chord card drag still has one unambiguous target while both lines are on screen. The
panel is exactly as tall in this view as in any other, because the cards take the band's space
rather than joining it.

**A click on a chord card never feeds a line any more** (2026-08-02, Owen: "when an
arpeggiator's running and you click on a pad, I don't want it to send it to the arpeggiator
unless you drag it"). Feeding a line is a **drag** now - onto a line's card in the macro view,
onto A or B on the Arp bar, or onto a slot - and a click just plays the pad, whatever the lines
are doing. The Pads bar's old letter chip, which used to name which line a click would feed,
left with it: the current line is shown only by the panel's own **LINE A** / **LINE B** caption
now, since A and B read as On/Off rather than as a selection. A card feeding a line still wears
that line's letter in its ring.

**Detach** is on the Arp bar too, but it hides with the fold the way every other section's
does. The section starts folded, so **A**, **B** and Hold off are usually all of it you can
see.

**Hold off releases every line**, and stops every **Chain** that is running. It is one button
on purpose: a release that only let go of the line whose Details view happened to be open would
leave the other one droning with nothing on a folded bar to stop it. The arpeggiators
themselves keep running and go back to arpeggiating whatever you play. It is greyed out when
nothing is held and nothing is chaining, and it is the way to stop a hold outright: clicking
the lit pad restrikes the chord rather than letting it go. A running chain counts as something
to let go of even in the gap where no chord happens to be sounding, because it will fire the
next one at the coming bar line.

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
| **A** / **B** | Arp bar | One per arpeggiator line: that line's own On switch, and a chord-drop target - drop a card straight onto the letter to hand that line the chord. Everything else stays editable while a line is off. B defaults to off. They used to be a navigation tab that hid with the section fold and left the actual switching to a separate chip nearby; the chip is gone (2026-08-02, seventh pass) and A / B never hide now, the same case Hold off and Quantize already had. |
| **Details** | Macro card | Opens that line's deep view - band, step lanes, twelve slots, Bars, Chain - in the spot A and B used to reach it from (2026-08-02, seventh pass). One per macro card, beside Anchor. A small **LINE A** / **LINE B** caption in the panel's own top margin says which line Details opened. |
| **Keys** | Playback | Does this line arpeggiate what you *play*, or only the chords you hand it? On for both by default. Turn it off and that line becomes a card player, independent of the keybed. |
| **Quantize** | Arp bar | **Off**, or 1/16, 1/8, 1/4, 1/2, 1 Bar, 2 Bars. Off fires a chord the instant you click it. Anything else holds the click until the next boundary, so a card can only ever land on the grid - Ableton's Quantization, for the arp. It holds the whole gesture: a slot's pattern, shape, rate and chord all arrive together. The line's row shows `...` while one is waiting. **It never delays the keys you play.** Moved to the Arp bar 2026-08-02, from the macro view's shared row; it stays put through a fold, unlike **All** beside it, which still only opens the macro view and hides with the section. (The **Tempo** that used to sit next to it moved out the same day too, to the *Controls* bar - see the Top bar table above.) |
| **Channel** | Playback | Where this line's notes go: **Global** (the plugin's own channel, and the old behaviour) or 1–16, for driving three different sounds in a multitimbral rack. It buys nothing in Keys Host until the hosted instrument is itself multitimbral. |
| **Shape** | Pattern | Up, Down, Up-Down, Down-Up, Up & Down, Down & Up, As Played, Reversed, **Random**, **Random Other** (never the same note twice running), **Random Once** (a shuffled order, kept for as long as the chord is held), **Chord** (every note of the chord on every step, so the arp plays rhythm instead of a run), or **Pattern** (opens the step editor). |
| **Rate** | Pattern | A dial. In **Sync**, step length from 16 bars down to 1/64, detented onto the eleven divisions. In **Hz**, a free-running 0.031 to 32 Hz, which is the same span those divisions cover at 120 bpm. The Hz dial is exponential: ten octaves, a tenth of the travel each, so 1 Hz sits at the centre and a degree of the dial is the same *ratio* wherever you are on it. |
| **Sync** / **Hz** | Pattern | Which unit the dial is in. Sync follows the host tempo and its bar grid; Hz ignores both and runs whether the transport rolls or not. The chip reads the live unit and lights in Hz. |
| **Dot** / **Tuplet** | Pattern | Dot lengthens each step by half, and reads as a dot on the rate: `1/8.`. Tuplet is a list — **Straight / Triplet / 5-tuplet / 7-tuplet / 9-tuplet** — fitting that many steps into the space the power of two below it would take, so five quintuplet 1/16s fill exactly the span four straight ones do. The rate readout shows the result as a plain fraction of a bar: 1/8 in threes is `1/12`, in fives `1/10`, and 1/4 in fives is `1/5`. They are different questions and stack. Both greyed out in Hz: they subdivide a beat, and there is no beat there. |
| **Swing** | Playback | −0.75 – +0.75, starting centred. Shifts the offbeat steps: right delays them for a shuffle, left pulls them early to rush the beat, centre is dead straight. |
| **Gate** | Playback | 5–200%. Note length as a share of the step; over 100% ties into the next one. Works on **any** shape, and multiplies the Gate lane when you are using one. |
| **Chance** | Playback | 0–100%. How likely each step is to fire — turn it down to thin a run out. Works on any shape, and multiplies the Probability lane. |
| **Retrigger** | Playback | When the pattern starts over: **Off**, **Note** (a new chord restarts it), or a clock window — 1 or 2 beats, 1, 2 or 4 bars. A clock window is what makes a five-step lane still land on the bar. |
| **Anchor** | Playback | On: steps lock to the host's bar grid, so the arp lines up after a jump. Off: free-running, never jumps, may drift. Greyed out in Hz, alongside Dot and Tuplet: a free-running rate follows no bar grid, so there is nothing there to anchor to. |
| **Latch** | Playback | Keep arpeggiating after you let go, until a new chord arrives. |
| **Repeats** | Spread | 1–4. How many times the chord is stacked up the keyboard before the run repeats. (This was "Octaves", back when an octave was the only thing it could stack by.) |
| **Distance** | Spread | How far each repeat goes: **Octave**, **5th**, **4th**, **Maj 3rd**, **min 3rd**, or the scale-relative **Scale 2nd / 3rd / 5th / 7th**. The scale entries count degrees of Root and Scale, so a third stays a third *of this key* — C lifts to E, D lifts to F — which is the one thing the stock arps cannot do. |
| **Offset** | Spread | 0–31. Start the run further in. Rotates the step lanes and the walk together, so the same pattern can be heard from a different foot without redrawing it. |
| **Ramp** | Feel | −100 – +100%. Velocity change over **Time**, counted from the moment a chord starts. Left fades a held chord away, right swells it, centre is flat. |
| **Time** | Feel | 1–32 beats. How long the Ramp takes. |
| **Human Time** | Feel | 0-100%. Nudges each hit a little late, by a different amount every time. At 0 the arp is dead on the grid. |
| **Human Vel** | Feel | 0-100%. Takes a little off each hit's velocity, by a different amount every time. Late and quieter, never early and never louder - a *player* wandering. |
| **Drift** | Feel | 0-100%. Strays from what the lanes hold **while it plays**, either side of the drawn value, so the part never repeats exactly and the lane on screen never changes. Octave, velocity, gate, lateness and chance wander; the notes never do. A *machine* wandering, which is why it is bipolar where Humanize is one-sided. |

### The three pages of a line (2026-08-14)

A line's deep view is three pages, picked on the ARP bar beside **All**. It used to be every
block at once, which made the window jump 372 px whenever you opened it; each page fits inside
one fixed panel height now, so the window does not move between views at all.

| Page | What is on it |
|------|----------------|
| **Play** | How the line plays: Rate, Shape, Tuplet/Dot, Swing, Gate, Chance, Retrigger, Spread and Feel. Most of what you want is here. |
| **Cards** | The twelve slot cards, plus Copy, Clear, Stop, Randomize, Euclid, Clocks and Chain. |
| **Draw** | The step lanes. Pattern shape only - its tab greys out otherwise, and leaving Pattern with it up drops you back to Play. |

**All** is the way back out to both lines side by side. It sits at the head of the same group,
which is what makes it read as the fourth view rather than a third letter beside A and B.

### The step editor (Draw page, Shape -> Pattern)

Twelve lanes, shown **one at a time** through the tabs: pick a tab, edit that lane. A readout
follows the cursor. Nothing needs a modifier, a double-click or the keyboard.

**A drag edits the step it started on, and only that one.** Move up and down to set the value;
sideways travel is ignored, so a hand drifting on its way to a height cannot rewrite the
neighbours it crosses. The **Mute** row below is the exception and paints across steps, because
there the value is a toggle and a swipe genuinely means "all of these".

| Lane | Range | Meaning |
|------|-------|---------|
| **Note** | rest, follow, 1-8, P/H/L/R | Which note of the held chord this step plays. `X` rests, the dot follows the shape, 1-8 pick a fixed one. **P** repeats whatever last sounded, **H** and **L** take the highest and lowest note of the chord, **R** picks any of them - those four ask the chord a question rather than counting into it, so they keep meaning the same thing when the chord changes. |
| **Octave** | -3 - +3 | Octaves added to this step. |
| **Velocity** | 10-200% | Scales the velocity you played at. |
| **Gate** | 5-200% | Note length as a share of the step. Over 100% ties into the next step. |
| **Ratchet** | 1-4 | Sub-hits packed into this step. |
| **Chance** | 0-100% | Odds this step fires at all, multiplied by the CHANCE knob on the Play page. Called **Prob** until 2026-08-14; one word for one idea. |
| **Transpose** | -7 - +7 | Scale **degrees**, not semitones: +2 lifts each note a third *of your key*, so it can never land outside it. |
| **Late** | 0-90% | Pushes this step later by that share of a step. Draw a little on the offbeats for a lazy feel, or a lot on one step to make it stumble. Late only - Swing is the control that can also rush. |
| **Harmony** | 0-7 | Adds a second voice this many chord tones above the note the step plays. Off at 0. |
| **Chord** | 1-12, or off | This step plays the chord stored in that **arp slot** instead of a note of what you are holding. Draw four of them across a lane and the arp plays a progression on its own. Off (the dot) is a normal step, and a slot with no chord in it is left alone rather than silenced. |
| **Rand** | -8 - +8 | How far this step's note selection may stray, and which way. Drawn per step, so step 3 can be locked and step 7 wide open. Only acts on a fixed 1-8; a step following the shape is left to walk. |
| **Chain** | 0, 1, 2 | Play this step only on a condition. `0` always, `1` only if the step before it sounded, `2` only if it did not. Chance says *maybe*; this says *only if*. |

The **Mute** row under the grid silences individual steps **without disturbing their values**,
so you can take a step out and put it back unchanged. It is its own lane as of 2026-08-14 - it
used to overwrite the Note lane, which destroyed whatever the step held. A Note of `X` is still
a *drawn* rest, which is a different thing and still there.

### Select, Reset and Roll (the lane tools)

Their own strip under the lane tabs, acting on the lane you are looking at.

**Roll** rerolls that lane, straying from what is drawn by the amount beside it - a nudge at
20%, a uniform scramble at 100. **Reset** puts the lane back to its default across its whole
length, which is the way back from a roll you did not want; there is no undo anywhere in Keys.

**Select** turns the drag into a span: with it lit, dragging on the grid marks steps instead of
drawing on them, and Roll and Reset narrow to that span. Click it again to go back to drawing,
which also drops the span. It is a mode rather than a modifier because the mouse-only contract
has no Alt-drag to offer.

**Voice** sits at the right end of the same strip, but only while the **Harmony** tab is
selected - it is the panel's one control that depends on which lane you are looking at.
It reads "Voice: Chord": the Harmony lane's second voice plays chord tones above the note the
step plays. Click it for "Voice: Sub" and that voice plays the subharmonic series below the
same note instead, an undertone rather than an overtone. Best heard with Scale Lock off, since
it deliberately leaves the key.

### Lane lengths, and Link

**Steps** and **Speed** on the Play page set the length and clock divider of the lane you are
editing. **Link** on (the default) means every lane shares one length - the simple case, where
a pattern is just "sixteen steps". Off is polymeter: each lane keeps its own, and lanes of
different lengths drift against each other.

With Link on the lanes are kept in step continuously, not only when you nudge - a lane added by
an update arrives at its own default and would otherwise draw a different number of cells than
its neighbours with nothing to say why.

### Euclid and Clocks

Two buttons on the **Cards** page action row, beside Randomize and Chain, each opening a strip
of steppers above the row rather than a dialog. Opening a strip previews nothing on its own -
only a stepper click writes - and the two strips are mutually exclusive, so the panel never
grows by more than one of them at a time.

**Euclid** (Pattern shape only) spreads a Euclidean rhythm into the **Chance** lane: **Hits**
beats spread as evenly as possible across **Steps**, shifted by **Rotate**. Every click writes
immediately and re-sizes the Chance lane to match Steps, which is also why the strip
closes itself if you leave Pattern shape - there is no lane on screen left to write into.

**Clocks** (every shape, since the dividers act regardless of what Shape draws) holds four
independent rhythm dividers layered under the pattern, **Div 1** through **Div 4**. Each runs
0-16; 0 reads "Off" and is silent, same contract as Rhythm Div everywhere else in Keys. The
button itself retitles to **Clocked** whenever any of the four is running, the same way Chain
retitles to Chaining.

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

### Getting a chord into a line

**A click never feeds a line any more** (2026-08-02, Owen: "when an arpeggiator's running and
you click on a pad, I don't want it to send it to the arpeggiator unless you drag it"). A click
always just plays the pad for a short audition, whatever the lines are doing - clicking a card
that is already feeding a line no longer retriggers the arp's hold, it just auditions the chord
on top of it. Feeding a line is a drag, and there are three targets, all from the Pads section
under the panel. Each one answers "which line?" differently.

- **Drag a chord card onto a line's card in the macro view.** The whole card lights while the
  chord is over it, anywhere on it, knobs included. This is the way to build a polyrhythm out
  of chord cards - two cards on screen, drop a different chord on each. The view does not move
  when you let go; you dropped onto the line itself, and being thrown into that line's deep
  controls is not what the gesture asked for.
- **Drag a chord card onto A or B on the Arp bar.** It goes to *that* line, whichever letter
  you dropped it on, and that line becomes the current one - you aimed at it - whether the
  letter is lit on or off.
- **Drag a chord card onto a slot card.** It binds the chord to that slot, in whichever line's
  detail view is currently open, ready to launch later. **Send to arp slot**, in a pad's
  right-click menu, is the aimless twin of this: it parks a copy of that chord in one of the
  twelve slots without needing a target to drag onto.

Landing a chord on a line replaces whatever it was holding - drop **another** card on the same
target to swap it. To stop a hold outright, use **Hold off** on the arp bar (or **Stop** in the
panel, which is the same button); switching a line off releases whatever it was holding, too.
A card that fed a line still wears that line's letter in a bright ring while it holds it, and
**a click on a cleared card that is still feeding a line releases it** - there is nothing left
to audition, so the click means the only other thing it can, and that is the ring's own way out.

None of these drags can lose a chord *by landing*. Dragging a card off the strip clears it, so
all three are drags off the strip - what saves them is that a drop on a tab, a slot, a macro
card or the generator's reference box **copies** the chord and leaves the card exactly where it
was. A drop that lands on none of those is an ordinary drag off the strip and still clears the
card, which is the gesture working, not failing. Lock the pad if you want it to survive a miss:
a locked card dropped anywhere off the strip does nothing at all.
