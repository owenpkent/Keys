# Acid line design spec

Status: **proposed, unbuilt.** Written 2026-08-16 from `Acid-V-Manual.pdf` (Arturia Acid V
1.1.1, chapter 4 in full, plus 3.2.8 and 3.4). Read that chapter before touching this.

## What is being proposed

A **third kind of arp line**: a monophonic 303-style step sequencer that owns its own pitch,
sitting beside the two chord-driven lines Keys already has. Not a new plugin, not a new
section, and not a rewrite of `ArpEngine`. An **Acid mode on a line**.

## Why it fits Keys, and the one place it does not

Keys' arp is a *pattern over a chord you hand it*: the Note lane picks the 1st, 3rd, 5th
of whatever is held, and the whole design assumes an input chord. A 303 is the opposite:
the sequence **is** the pitch, and nothing is held. That is the only real gap, and it is one
lane wide.

Everything else the Acid V sequencer does, Keys already has:

| Acid V | Keys today |
| --- | --- |
| 64 steps, blue length handle, Steps field | `ArpPattern` per-lane lengths, `nudgeLength` |
| Polymetric mode, five independent handles | Link Lengths **off** is exactly this, already built |
| Rate with BPM / Bars / Triplet / Dotted | `arpRate` + Sync/Hz + `arpTuplet` + `arpDot` |
| Swing 50..75% | `arpSwing` |
| Gate time | Gate lane + the band's gate |
| Playback: Forward / Backward / Fwd-Bwd / Random | Shape: Up / Down / UpDown / Random |
| Scales, Acid and Classic | `ScaleModes.h` + Root + Scale Lock |
| Randomizers (the dice) | Rand lane, Roll, Drift: three of them, see below |
| Sequence browser, save, load | the twelve arp slots |
| Undo after Clear | `pushUndo` / `arpToTree`, built 2026-08-14 |

So the build is: **one new lane meaning, three new binary lanes, a MIDI translation layer,
and a piano-roll page.** The clock, the polymeter, the swing, the slots, the undo and the
scale table are all reuse.

The one place it does not fit: Keys makes no sound. Accent, slide and vibrato on a 303 are
*analogue circuit* behaviours (VCA level, filter decay, portamento, LFO depth). Keys has to
express all three as MIDI a downstream instrument can act on. Section 4 is that translation
and it is the part most likely to disappoint if it is designed casually.

---

## 1. The lanes

`numLanes` goes 13 -> 17. **Appended, never inserted.** A lane's index is what a saved
session stores it under, the standing rule this repo has paid for twice (`genSource`, the
2026-08-14 lane round).

| # | Lane | Range | Notes |
| --- | --- | --- | --- |
| 13 | **Pitch** | -24..+24 semitones from Root, -1 = rest | The 303's own note. New meaning, new lane. |
| 14 | **Slide** | 0/1 | Portamento into this step from the one before. |
| 15 | **Accent** | 0/1 | Binary, with a global amount. Not the Velocity lane. |
| 16 | **Vibrato** | 0/1 | Binary, with global Speed and Amount. |

**Pitch is a new lane and not a reinterpreted Note lane.** Two reasons. A session can then
carry a chord pattern *and* an acid pattern on the same line and switch between them without
either destroying the other, which is what makes the mode switch safe to explore. And the
Note lane's vocabulary (1..8, plus Prev/Hi/Lo/Random at 9..12, plus -1 rest) has no room for
a signed semitone offset, so overloading it would mean a second, disagreeing reading of the
same stored integer.

**Octave is the existing Octave lane.** Acid V gives each step a four-octave range on a
vertical slider above its column; Keys already stores that per step. Nothing new.

**Accent is binary with a global amount, not a Velocity lane draw.** This is the reference's
own architecture and it is right: the 303's accent knob moves two things at once (manual
3.2.8, "the level of the volume (VCA) envelope, and the decay time of the filter envelope"),
so the *per step* decision is on/off and the *how much* is one knob for the whole pattern.
A continuous Velocity lane would say the same thing worse, and the Velocity lane still exists
underneath for anyone who wants it.

**Rest is Pitch = -1**, the drawn rest the Note lane already has. **Tie** is Gate at or above
100%: the step sustains into the next. Both stay distinct from the **Mute** lane, which
preserves the value under it (Cthulhu's rule, logged 2026-08-14).

### Length, on load

Four appended lanes arrive at `ArpPattern`'s default 8 while the rest of the pattern may be
at 16 or 32. `ArpPanel::enforceLinkedLengths` already repairs that on every readout refresh
when Link is on. **This is the debt every appended lane owes**, and it is already paid; just
do not skip it.

---

## 2. The three randomnesses, and Acid V's fourth

Keys has three and the differences are the design (2026-08-14): **Roll** rerolls the lane
you are looking at, once, visibly. **Reset** puts it back. **Drift** strays while it plays
and never changes the lane on screen. Acid V's five dice (notes, octaves, slide, accent,
vibrato) are **Drift, per lane**: a live probability that this step comes out different, with
the drawn value untouched.

So Acid mode's dice are `laneDrifts` entries, and this is the one place the existing rule
bends. `laneDrifts` deliberately confines Drift to the lanes that decide *how* a step plays,
because "a knob wandering over a part you did not aim at" is the wrong feel for pitch. Acid V
puts its loudest dice on exactly pitch and octave, and it is right for this mode: an acid line
that mutates its own notes is the genre. **Pitch and Octave drift are enabled in Acid mode
only**, off everywhere else, so the chord lines keep the old guarantee.

**The Rand lane still applies** and still means the other thing: how random *this particular
step* is, because you drew it there.

### Custom scale weights

Acid V's custom scale is twelve toggles plus twelve probability sliders, and the manual's own
analogy is worth keeping: the dice decide *whether* a different note plays, the sliders decide
*which*. Keys' generator already thinks in weighted pools (`ChordGen.h` tiers), so this is a
`std::array<float,12>` beside the scale selection, feeding the Pitch drift's draw. Build the
toggles first; the weights are a second pass and the feature is useful without them.

**One trap already paid for elsewhere**: `ArpEngine::strayWithin` slides the window rather
than clamping the result, because a value at the edge of its lane otherwise ignores Drift
entirely. Pitch drift must use it. A rest at -1 and an accent at 0 are both edge values, and
both are the common case here.

---

## 3. Playback, shift and the generative controls

- **Shape is the play order** in Acid mode: Up = Forward, Down = Backward, UpDown = Forward
  Backward exclusive, Random = Random. Same four the reference lists, same four Keys already
  draws. No new control.
- **Shift**, new, on the lane tool row beside Select / Reset / Roll: `< >` rotates every lane
  one step (step 1 becomes step 2, the last wraps to first) and `-`/`+` transposes the Pitch
  lane by a semitone. Four click-only targets at the 34 px floor. Rotation is the one of the
  four that cannot be done any other way and it is the one that makes a written pattern feel
  found rather than typed.
- **x2**, new: duplicate the pattern and append it, doubling the length. One click, and the
  cheapest idea in the chapter.
- **Generate** (Acid V calls it Transmutation), new: reroll pitch, octave, slide, accent and
  vibrato together, with an **amount** for how far. This is Roll across five lanes at once,
  so it is a button over machinery that exists. Undo already covers it (`arpToTree`).
- **Density**, new, a global 0..1: probabilistically mutes steps without deleting them, and
  turning it back up restores them. Non-destructive by construction, which is why it needs no
  undo entry and why it should be a knob and not a lane. It reads through the Mute lane's own
  gate at play time; it must never *write* the Mute lane, or it stops being reversible.

Deliberately **not** built: the Sequence Browser as a separate thing, and MIDI-file export by
drag. The twelve arp slots are Keys' sequence browser already, and export is a real feature
that deserves its own pass rather than a corner of this one.

---

## 4. What comes out of the MIDI port

Keys emits MIDI and nothing else, so this section is the spec. Every one of these goes
through `KeysProcessor::noteOn` / `noteOff` like every other note source: one note-on per
sounding pitch, released by the last owner. An Acid line is monophonic, so it never collides
with itself, but it must not be allowed to bypass the refcount.

**Accent** -> velocity, plus an optional CC.
`velocity = base + accentAmount` where base is the line's own level and `accentAmount` is the
global knob (0..127 headroom, clamped, floor of 1, never 0, which is a note-off in disguise).
Because the 303's accent also shortens the filter decay and MIDI has no word for that, an
**Accent CC** picker (default *none*, suggested CC16) fires a high value on accented steps and
a low one otherwise, so a downstream patch can be macro-mapped to it. Same honest limit worth
telling Owen up front as with VEL: a patch with no velocity sensitivity flattens this, and only
the CC route reaches it.

**Slide** -> overlap plus portamento CCs. Three parts, all needed:
1. The previous note's note-off is held until **after** the new note-on (an overlap of
   `slideMs`, default about 60 ms, 0..250). Legato overlap is what makes a mono synth glide
   at all.
2. **CC65** (Portamento On/Off) is sent 127 just before the accented note and 0 just after,
   so patches with a portamento switch follow per step rather than gliding everywhere.
3. **CC5** (Portamento Time) carries a **Slide Time** knob.
   A slide on step 1 glides from the **final step of the pattern**, per the manual. That wrap
   is a real behaviour and easy to forget.

**Vibrato** -> **CC1** ramped across the step at the global Speed and Amount, returning to 0
at the step's end. A **pitch-bend LFO** alternative is worth an option (more universal, since
every synth bends) but it is not the default: bend is a channel-wide resource and Keys already
shares its output stream. Vibrato in the reference is "intentionally subtle" (3.4) and the
default depth should be too.

**Rate, gate and swing** need nothing new: the line's own clock already does all three.

---

## 5. On screen

The Acid line is a line, so it gets the A/B/All bar treatment for free. The work is one page.

**A fourth page? No. The Draw page becomes a piano roll when the line is in Acid mode.** The
page budget is already spent (Play 208 / Cards 124 / Draw 258, and the panel is one fixed
height for every view so the window never moves). A piano roll fits Draw's 258 px only because
of the reference's own trick: **rows are the twelve pitch classes, and octave is a separate
slider row above.** Twelve rows at 16 px is 192, the octave strip is 22, and the Slide / Accent
/ Vibrato toggles are three 14 px rows under it. That is 256. It fits, and it fits *only* in
that arrangement: a conventional four-octave piano roll is 48 rows and does not go in this
panel at any height it is allowed to have.

Gestures, all left button:

- **Click an empty cell** to place the note. **Click the lit cell again** to clear it.
  Acid V uses right-click to remove; Keys does not need to, and adding a right-click-only
  path here would be a new entry on a closed list. This is a deliberate divergence, not an
  oversight.
- **Sweep to draw**, like `MuteRow` and unlike `LaneGrid`. The rule from 2026-08-14 is that a
  gesture setting a *height* is captured to one step and a gesture setting a *toggle* paints
  across. A piano-roll cell is a toggle. Same reasoning, opposite answer, and both are right.
- The octave sliders and the three toggle rows are per column, at the column's width. If a
  column ever falls under the mouse-only floor, **reduce the visible step window and scroll**;
  do not shrink the target. Acid V scrolls its 64 steps for the same reason.
- **Every dice is a drag**, so each needs a click-only twin. Put the five drift amounts on the
  Play page as ordinary knobs rather than as hover-reveal dice: hover-to-reveal is a discovery
  problem on a mouse-only surface, and the tray's invisible empty cell taught that lesson on
  2026-08-16.

The mode switch itself (Chord / Acid) belongs on the line's macro card beside Details, since
it changes what the whole line is.

---

## 6. Parameters and migration

Appended, all of them, at the end of the layout, defaulting to today's behaviour:

`arpAcid` (bool, per line, default off), `arpAccentAmt`, `arpAccentCc`, `arpSlideMs`,
`arpSlideTime`, `arpVibSpeed`, `arpVibAmt`, `arpVibMode` (CC1 / bend), `arpDensity`,
`arpDriftPitch`, `arpDriftOct`, `arpDriftSlide`, `arpDriftAccent`, `arpDriftVib`,
`arpPolyReset` (the polymetric realign count).

`migrateAcid` backfills every absent one explicitly, the `migrateRateMode` /
`migrateVelTrim` shape. **That shape only works because the kit's `state::load` copies now**
(2026-08-02): before that fix every absence-detecting migration in `PluginProcessor.cpp` was
silently dead. Check what the root actually contains before trusting a tell.

Line A keeps every id it has ever had. Nothing is reordered.

---

## 7. What to build first

Four passes, each of which leaves something playable:

1. **Pitch lane + Acid mode + the piano roll page.** A monophonic step sequencer with rests
   and ties, using the clock, swing, gate and slots that already exist. This is most of the
   value and it touches no MIDI translation at all.
2. **Slide and Accent.** The two lanes plus section 4's output layer. This is where it starts
   to sound like a 303 rather than like a step sequencer.
3. **The dice, Density, Shift, x2, Generate.** All small, all independently useful.
4. **Vibrato, custom scale weights, polymetric reset.** The tail.

Tests: `ArpTests.cpp` pins the engine. The cases that matter are the slide wrap from the last
step to step 1, accent velocity at both clamps, Pitch drift at the lane edges (the
`strayWithin` trap), and Density being reversible.

---

## 8. What was deliberately not taken from the reference

- **The Sequence Browser and its import folder.** Keys has twelve slots and a session file.
- **MIDI-file export by drag.** Real, wanted, separate.
- **Hover-reveal dice.** Discovery problem on a mouse-only surface.
- **Right-click to remove a note.** Left-click-to-clear does the job, and the right-click
  exception list stays closed.
- **The synth itself.** Filter, envelope, sub oscillator, distortion. Keys makes no sound and
  this changes nothing about that.
