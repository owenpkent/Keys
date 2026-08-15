# Reference manuals

Seven manuals sit in the repo root. They are not decoration: eight features in Keys were built
from a specific page of one of them, and at least twice a manual **corrected a guess** that had
already been coded and looked right on screen. This file records what each one contributes, what
Keys took, and what it deliberately did not - so the next person does not re-derive a decision
that was already made, or re-invent one that was already rejected.

Read the manual before designing the feature, not after. Owen's instruction, 2026-08-14: *"look
at reference manuals"*, given while looking at a randomness feature that had been built as a
global knob when the reference it was named after does it per step.

---

## Cthulhu (`Cthulhu_Manual_v1_1.pdf`, 34pp)

The closest thing to Keys' arp there is: a chord module feeding a step-lane arpeggiator. Keys'
whole per-parameter step-lane architecture is Cthulhu's - ten of them when it was copied,
twelve now.

**Taken:**

- **Per-parameter step lanes** (Note, Octave, Velocity, ...) - the architecture itself.
- **Link Lengths** (p25): one switch making every lane share a length, off for polymeter.
  Keys' `apLinkLanes` is the same control, and the manual is where the default came from.
- **The Rand lane** (p25-26, "Rand Sel"). *"a graph where you can set an amount of random-step
  selection for each step... The default is halfway up the graph, which is no randomizing...
  if the Note Sel step is set to 2 and the random value is set to 2 above middle, the Arp will
  output 2, 3, or 4."* Built 2026-08-14 as `laneRand`. **This corrected a built feature**: Keys
  had Roll (one global reroll) and Drift (one global wander), and neither is what Cthulhu does.
  Randomness that is *drawn per step* is a different instrument from randomness on a knob.
- **Mute preserves the step's value** (p25). *"you can experiment with mutes, without losing the
  set-value of the steps, in case you wish to undo this action."* Keys' mute row wrote -1 into
  the Note lane and destroyed whatever was there; `laneMute` fixed that the same day. Note = -1
  is still a *drawn* rest, which Cthulhu also has as a separate thing (drag below 1).

**Not taken:**

- **Position Reset** (p25): alt-click a step to make the arp restart its walk there. Keys has
  Retrigger, which is the same idea on a clock rather than on a step. A per-step version would
  be a lane; no one has asked.
- 128 chord slots keyed to incoming MIDI note. Keys' chord pads are a grid you click, not a
  keyboard-triggered memory - a different product decision, not an oversight.

## Kirnu Cream (`Kirnu-Cream-Manual.pdf`, 18pp)

Four-track arpeggiator. The richest per-step vocabulary of any of these, and the best source of
**unbuilt** ideas for Keys.

**Taken:**

- **Chord memory per step** (`CHRDMEM`): a step can call up a stored chord. Keys' Chord lane,
  where the memories are the twelve arp slots it already had.
- **Rate in Hz as an alternative to divisions** (p7): *"If free mode is selected values are
  specified in hz."* Keys' `arpRateFree` / `arpRateHz`.
- **Reset to default over a whole lane** (p7): *"Right mouse click pops up the reset menu which
  can be used to reset all the step bars to default value in current control view."* Keys' Reset
  button does exactly this - as a **button**, because right-click-only paths are a closed list
  here and a left-click path is the contract.

**Also taken, 2026-08-14:**

- **A richer Note lane.** Kirnu's `ORDER` per-step values are `Off, Prev, 1st, Last, Hi, Low,
  Rnd` - not just a fixed index. Keys' Note lane took **Prev, Hi, Low and Rnd** as values 9..12,
  appended *above* the fixed indices rather than below -1 so that saved values are untouched and
  a drag to the bottom of the grid still reaches the rest. The point of them is that they ask
  the chord a question rather than counting into it, so they keep meaning the same thing when
  the chord changes.
- **A selection for the grid.** Kirnu's palette is Draw / Select / Random / Copy / Paste /
  Clear, and its Random tool acts on *selected steps*. Keys built **Select**, and Roll and Reset
  narrow to the span. Copy, Paste and Clear-within-a-lane are still unbuilt, and are now cheap.

**Not taken, and worth considering:**
- **An enable row per lane**, not just for notes: *"First row from bottom can be used to turn
  selected data section steps on or off. When step is off the value in step is ignored."* Keys'
  MUTE row is the Note lane's alone. A per-lane enable would let you disable one step's Octave
  without flattening it to 0.
- **Negative SHIFT** (a step played *earlier*). Keys' Late lane is 0..90, positive only, and
  `ArpEngine`'s own comment says why: an early half would need `emitHit`'s
  close-what-you-land-on rule rewritten. Kirnu proves the feature is wanted; the cost is
  understood and unpaid.
- **ACCENT as its own lane** that raises one note and lowers the others around it. Keys has a
  Velocity lane, which is the absolute version of the same idea.
- **Sync** (p7): a new chord is held for N *steps* before it takes. Keys' Launch Quantize is the
  same idea measured in beats.

## Stochas (`stochas_av.pdf`, 17pp)

A probability sequencer. Where Cthulhu randomises *which* note, Stochas randomises *whether*.

**Taken (independently, but it validates the design):**

- **Probability per cell** - Keys' Chance lane, multiplied by a per-line Chance knob.
- **Variance is bipolar**: *"changing the position start, velocity and the length of the note,
  by any value between a + and - of the number chosen."* Keys' Drift wanders either side of the
  drawn value for exactly this reason, where Humanize is deliberately one-sided.
- **Per-layer step counts for polyrhythm** - Keys' per-lane lengths with Link off.

**Also taken, 2026-08-14 - and it was the most interesting idea in any of these:**

- **Chain dependency**: a cell *"will play or not play depending on whether another cell has
  played."* Keys built `laneChain` in the one form that needs no second coordinate: 0 always,
  1 only if the step before sounded, 2 only if it did not. Chance says *maybe*; this says *only
  if*, and that is what turns a probabilistic pattern into one that answers itself. It is also
  the only thing in the engine that is not stateless from the playhead, which is exactly why
  the arbitrary cell-to-cell form was not copied.

**Not taken:**
- **Max poly + Bias**: cap how many notes a step may play, and bias how close to the cap it
  lands. Keys has a global Voices cap, not a per-step one.
- **Right-click a cell to zero it.** Keys cannot: right-click-only paths are closed.

## Serum 2 (`Serum 2 User Guide.pdf`, 354pp)

Not a sequencer reference - a *UI* reference, and it is where `RangeKnob` came from.

**Taken:**

- **The modulation-depth halo** (p195): *"A smaller blue halo appears to the top left of the
  knob... Click and drag the arrow control to change the modulation depth amount. As you drag
  the arrow, notice how the halo shrinks or expands."* Keys' `RangeKnob` satellite. **The manual
  corrected the guess**: a first build read the screenshot as a dot sitting on the ring, which is
  wrong and unbuildable at Keys' target sizes. A satellite in the corner is a component of its
  own and can be made big enough to hit with one mouse.

**Departed from, deliberately:** Serum's fallback for the fiddly satellite is Alt-click-drag on
the knob body. A modifier is not a gesture Keys may require, so the whole margin drags the span
instead. Serum flips the halo's hue for inverted depth; a range has nothing to invert into, so
`Direction` picks which side it reaches.

## Subharmonicon (`Subharmonicon_Manual AMZ.pdf`, 58pp)

Moog's polyrhythmic analog sequencer. Source of the whole 2026-08-14 generative round.

**Taken:**

- **Rhythm generators as an OR of clocks**: four dividers, and a step fires when *any* of them
  hits - so {3, 4} fires on 0, 3, 4, 6, 8, 9, 12, not on their common multiple. Keys'
  `rhythmDiv`.
- **The undertone series** as a second voice - `harmonyMode` 1, f/2..f/8 quantised to 12-TET.

**Not taken:** each of Subharmonicon's four rhythm generators can be **assigned** to either
sequencer or both. Keys gives each line its own four instead, which is the same expressive power
with one fewer concept to explain.

## MatrixBrute (`matrixbrute_Manual_2_0_3_EN.pdf`, 73pp)

Hardware analog sequencer. Two per-step ideas Keys does not have:

- **Ties**: *"Notes can be tied over multiple steps"* - a step that extends the previous note
  rather than restriking it. Keys' Gate lane can exceed 100% and overlap, which is close but not
  the same: a tie is *one* note event, a long gate is two.
- **Slide / accent as per-step toggles**, where slide *"affects the transition into the current
  step"*. Keys emits MIDI only, so slide would have to be a portamento CC - out of scope while
  Keys drives whatever instrument is downstream.

## Numerology 4 (`Numerology4Manual.pdf`, 251pp)

A modular step-sequencing environment. Its vocabulary is the broadest here, and it draws one
distinction Keys does not:

- **Skip vs Mute.** A *muted* step still occupies its slot in time and plays nothing. A
  *skipped* step is passed over entirely, so the sequence is shorter and everything after it
  moves earlier. Keys has mute (`laneMute`); its rhythm dividers are a global, regular version
  of skip, but there is no per-step skip.
- **Random Jump**, **Repeat**, **Divide** and **Glide** as per-step lanes. Divide is Keys'
  Ratchet. The other three are not built.
- *"the first and last steps of the sequence cannot be skipped"* - a guard worth copying if skip
  is ever built, since a pattern that can skip its own boundaries has no length.

---

## What this list is for

Every "not taken" above is a decision, not a gap that nobody noticed. Three of them are ranked
worth building. **All three were ranked here on 2026-08-14 and all three were built the same
day**, which is the argument for keeping this file: the ranking was the plan, and it survived
contact with the code unchanged.

1. ~~**Chain / conditional triggers** (Stochas)~~ - built as `laneChain`. Still the one idea
   here that changed what the sequencer can *express* rather than adding another axis to it.
2. ~~**A richer Note lane** (Kirnu's `Prev / Hi / Low / Rnd`)~~ - built as Note values 9..12.
3. ~~**A selection model for the grid** (Kirnu's tool palette)~~ - built as **Select**, which
   Roll and Reset narrow to. **Copy, Paste and Clear-within-a-lane are still unbuilt** and are
   now cheap: the primitive they were waiting for exists.

### What is still on the table

In rough order of what would change the instrument most:

1. **Copy / Paste / Clear over a selection** (Kirnu). The three remaining tools of that palette,
   now that Select exists to aim them.
2. **Skip, as distinct from Mute** (Numerology). A muted step still occupies its slot; a skipped
   one is passed over, so everything after it moves earlier. Keys' rhythm dividers are a global,
   regular version of it. Copy Numerology's guard if it is built: the first and last steps of a
   sequence cannot be skipped, since a pattern that can skip its own boundaries has no length.
3. **Ties** (MatrixBrute). A step that extends the previous note rather than restriking it.
   Keys' Gate lane can exceed 100% and overlap, which is close but not the same: a tie is *one*
   note event, a long gate is two.
4. **Negative Late** (Kirnu's SHIFT). Wanted, and the cost is understood and unpaid -
   `ArpEngine`'s own comment explains that an early shift needs `emitHit`'s
   close-what-you-land-on rule rewritten.
