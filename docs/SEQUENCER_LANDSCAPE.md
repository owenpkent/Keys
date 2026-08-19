# The sequencer landscape

Status: **research, nothing built.** Written 2026-08-17 from eleven manuals downloaded that day,
after Owen's "I think having sequencers is the key. where can we go".

`docs/REFERENCES.md` is the record of what Keys **took** from a manual. This file is the layer
above it: the map of what kinds of sequencer exist at all, which ones Keys already is, and which
ones are one feature away. Nothing here has been taken yet, so nothing here belongs in
REFERENCES.md until it is.

## What is on disk

Nineteen PDFs in `manuals/` now, eighteen products. They are gitignored, so a fresh clone has
none; `manuals/README.md` is the manifest and carries a download URL for every one of them, the
eight older ones included.

The eight that were already here are covered in `docs/REFERENCES.md`. The eleven added
2026-08-17:

| Manual | Archetype it documents |
| --- | --- |
| `Torso-T-1-Manual.pdf` ([B&H](https://www.bhphotovideo.com/lit_files/976203.pdf)) | Knob-driven algorithmic generation |
| `Hapax-Manual.pdf` ([Squarp](https://squarp.net/hapax/manual/)) | Offline transforms over a selection; per-track FX chain |
| `Metropolix-Manual.pdf` ([Intellijel](https://intellijel.com/downloads/manuals/metropolix_manual_v1.4_2022.04.04.pdf)) | Stage sequencing, and the accumulator |
| `Digitakt-II-Manual.pdf` ([Elektron](https://www.elektron.se/support-downloads/digitakt-ii)) | Trig conditions, parameter locks, Fill mode |
| `Deluge-Guidebook.pdf` + `Deluge-Community-Guide.pdf` ([Synthstrom](https://synthstrom.com/product/deluge/)) | Probability and iteration conditions, polymetry |
| `OXI-One-Manual.pdf` ([OXI](https://oxiinstruments.com/oxi-one)) | A sequencer **mode** per track |
| `Rene-2-Manual.pdf` ([Make Noise](https://www.makenoisemusic.com/wp-content/uploads/2024/03/renemanual.pdf)) | Cartesian / non-linear playback order |
| `Turing-Machine-Build-Doc.pdf` ([Thonk](https://www.thonk.co.uk/shop/turingmkii/)) | Shift register: randomness that hardens into a loop |
| `Pamelas-Pro-Workout-Manual.pdf` ([ALM](https://busycircuits.com/pages/alm034)) | Euclid + probability as a clock utility |
| `KeyStep-Pro-Manual.pdf` ([Arturia](https://dl.arturia.net/products/keystep-pro/manual/keystep-pro_Manual_2_5_0_EN.pdf)) | Per-step randomness on a keyboard controller |

Two notes on those files. The **Turing Machine** PDF is Thonk's *build* document, 62 MB of
soldering photographs; the concept is on
[musicthing.co.uk/Turing-Machine](https://www.musicthing.co.uk/Turing-Machine/) and is quoted in
full below, so do not go looking for it in the PDF. **Ableton's Live 12 manual has no PDF
edition** any more, so Follow Actions is cited from
[the web manual](https://www.ableton.com/en/live-manual/12/launching-clips/). **Scaler 2's manual
ships inside the plugin** and is not downloadable; its chord-progression timeline is described
here from its published behaviour, not from the manual, and should be checked before anything is
built on it.

---

## The taxonomy

Twelve archetypes. Keys was six of them when this was written on 2026-08-17 and is
seven now: the shift register was built on 2026-08-18, and section 6 below is the record of
what it became.

| Archetype | Keys today |
| --- | --- |
| Step lanes over a held chord | **Yes.** The whole arp (Cthulhu's architecture) |
| Euclidean rhythm | **Yes.** `EuclidGen.h`, the Euclid strip, into the Chance lane |
| Polymeter / polyrhythm | **Yes.** Per-lane lengths with Link off, plus `rhythmDiv` |
| Probability | **Yes.** Chance lane x the global Chance slider (2026-08-18: the per-line knob it used to read became Mutate and Lock) |
| Conditional trigger | **Partly.** `laneChain` is Elektron's PRE and PRE-not, and nothing else |
| Chord memory per step | **Yes.** The Chord lane over the twelve slots |
| Monophonic pitch sequencer | **No.** Spec'd in `docs/ACID_DESIGN.md`, unbuilt |
| Shift register / randomness that hardens | **Yes**, as of 2026-08-18: MUTATE x LOCK |
| Offline transform over a selection | **Partly.** Roll, Reset, Copy and Paste aim at Select (2026-08-18); Hapax's Flip and Curve do not exist |
| Accumulator | **No.** Every lane is a fixed value the playhead reads |
| Knob-driven generation | **No.** Keys generates *chords* this way, never *parts* |
| Non-linear playback order | **No.** Shape walks a line, never a grid |

The rest are the map. What follows is each one, grounded in the manual, with what it would
cost here.

---

## 1. Offline transforms over a selection (Hapax)

**The highest value for the cost on this list**, and it beats what REFERENCES.md currently ranks
first.

Hapax draws the distinction Keys has not: *"In Hapax, algo (algorithms) are operations that are
not performed in real time, but rather applied 'offline', directly on the sequences you programmed
or recorded."* And: *"Make a selection before applying an algorithm to only alter the selected
zone."*

Keys built **Select** on 2026-08-14 for exactly this and then aimed only two things at it, Roll
and Reset. REFERENCES.md ranks Copy / Paste / Clear as the next three. Hapax's palette is a
better answer than all three:

- **Flip.** *"flips events horizontally (time) or vertically (pitch). The original events can
  either be kept or replaced."* Horizontal flip over a selection is retrograde; vertical is
  inversion. Two of the oldest ideas in composition, and over Keys' lane arrays each is a
  `std::reverse` or a subtract-from-max.
- **Curve.** *"modifies note parameters by applying a curve on their values. The waveform (sine,
  triangle...), its min & max amplitude and its rate can be adjusted... a ramp applied on velocity
  will result in a 'velocity fade in'."* This is the one to notice. Keys has thirteen lanes and no
  way to draw a shape into one except by hand, step at a time, at 34 px a step. A curve tool is
  the mouse-only user's single biggest saving in the whole grid.
- **Shuffle**, with a partial mode: *"there is a probability parameter that controls the
  probability of each note or interval to be shuffled, allowing for partial shuffling."* Shuffling
  a selection at 30% is a variation; at 100% it is a different part.
- **Randomize.** *"replaces the existing notes with a new randomized pattern... it is possible to
  set the amount of events (density %)."* Keys' Roll with a density.

Every one of these is a pure function over `ArpPattern`'s arrays, undo is already a subtree
snapshot (`arpToTree`), and Select already produces the span. The lane tool row is where they go,
and the row is already the home of Select / Reset / Roll.

**Cost:** small, and it is nearly all UI budget rather than engine work.

## 2. The accumulator (Metropolix)

A per-stage transpose that **accrues every time the stage comes round**, bounded, with four
behaviours at the bound: unipolar or bipolar, wrap or pendulum. *"accumulations will accrue in an
upward direction beginning at the stage pitch; rise to the Positive Limit, then reverse direction
and continue accumulating downward until the Negative Limit is reached, then the accumulation
reverses again."*

Keys has no version of this and it is the cheapest way to make a pattern **evolve without
randomness**. Every randomness Keys owns (Roll, Drift, Rand, Chance) buys variation by giving up
control. An accumulator gives movement that is completely determined: the same eight bars every
time, and the line climbs through them.

**The one design trap, and it is the interesting part.** `ArpEngine` is stateless from the
playhead by deliberate design, so a transport jump lands correctly without walking there;
`laneChain` is the single exception and self-corrects within one step. An accumulator looks like
state. It is not: the accrued amount is a function of *how many times the pattern has repeated*,
and the repeat count is derivable from the playhead position the engine already reads. Build it
that way and the rule holds. Build it as a running total and the first loop jump desynchronises
it silently.

**Cost:** one appended lane (the per-step amount) plus a limits control. Small.

## 3. Knob-driven generation (Torso T-1)

The T-1 is the opposite instrument to Keys' grid: you do not draw a part, you dial one. A track is
Steps / Pulses / Rotate / Division (a Euclidean skeleton), then Repeats / Time / Offset / Pace (a
note repeater), then Sustain / Timing / Random (groove), then Pitch / Harmony / Scale / Root, then
the melodic shapers.

Keys has the first three groups in some form. It has **nothing** for the last, and that is where
the T-1 is unlike anything else in this pile:

- **Phrase and Range.** *"A Phrase is a predefined shape for generating melodic passages. Range
  expands the amount of pitch variation within the current scale for the selected Range and also
  controls the phrase rate. Essentially, a Phrase will act as a melody generator and the note
  range is controlled by the Range parameter."* Two knobs that write a melody inside the scale.
- **Style.** *"creates inversions of the pitch chord based on the pitch menu setting... 3 Style
  options... each of which is available for polyphonic notes or monophonic note changes.
  Monophonic options apply arpeggio style note movement."* One control that decides whether a
  chord comes out as stabs or as an arpeggio.
- **Harmony.** *"transposes notes to create new interesting chord inversions with the selected
  scale. Notes are adjusted by 1 tone at each iterative rotation."*

**Phrase + Range is the one to want.** Keys already holds a chord, a root, a mode and a scale
table; a phrase generator is a shape drawn through material Keys already has, and it lands closer
to Keys' chord-first design than the acid line does. It is also the honest answer to "generate
parts you would not have drawn", which is the thing a grid can never do for you.

Worth knowing, since it names a trap: *"All T-1 Parameters have a symbiotic relationship, meaning
what is set on one can affect another and vice versa... changing one of multiple parameters will
ultimately affect the output pattern."* That is a warning, not a feature. Keys' own answer to it
is already written down: changing a generator setting generates nothing, Fill and Regen do.

**Cost:** medium. A phrase-shape table and a generator pass, no new lanes.

## 4. Follow Actions (Ableton Live 12)

Keys has twelve slots a line, Launch Quantize, and a Chain that steps to the next slot. Ableton's
Follow Actions is the general form of exactly that, and Owen already knows the vocabulary because
he works in Live.

Per clip: an action out of **Stop, Play Again, Previous, Next, First, Last, Any, Other, Jump**, a
**time** (*"defined when the Follow Action takes place in bars-beats-sixteenths... The default for
this setting is one bar"*), and **two actions with a chance split**: *"If a clip or scene has
Chance A set to 100% and Chance B set to 0%, Follow Action A will occur every time."* Two of the
actions are worth naming: **Any** *"plays any clip in the group"*, and **Other** is *"similar to
'Any,' but as long as the current clip is not alone in the group, no clip will play
consecutively"* - the difference between those two is the difference between a random slot picker
that stutters and one that does not.

Keys' Chain is Follow Action = Next, hard-coded, with no time and no chance. Generalising it is a
combo and a percentage on each slot card, over `launchArpSlot` and the quantize deadline that
already exist. Twelve slots stop being a list and become a structure that arranges itself.

Live also applies Follow Actions to **scenes**, and *"Follow Actions in scenes always take
precedence once they are triggered."* Keys has no scene: nothing fires slot N on every line at
once. That is the smaller half of the same idea and it is nearly free.

**Cost:** small. Everything underneath it is built.

## 5. Conditional triggers, the rest of the vocabulary (Digitakt II, Deluge)

Keys' `laneChain` is Elektron's **PRE** and **PRE-not** and stops there. The full list:

- **A:B** - *"A sets how many times the pattern plays before the trig condition is true. B sets
  how many times the pattern plays before the count is reset and starts over again."* Deluge has
  the same idea worded for humans, *"'1 of 2' plays the note on the 1st of every 2 bars, '3 of 4'
  plays on the 3rd of each 4 rotations."* This is what makes a four-bar phrase out of a one-bar
  pattern, and Keys cannot express it at all.
- **1ST / LST** - fires only on the first pass of a loop, or only on the last before a change.
- **NEI** - *"trig plays if the most recently evaluated trig condition on the neighbor track was
  true."* Keys has neighbouring lines and no cross-line condition.
- **FILL** - a **mode**, not a lane. *"A trig with FILL set to ON, is active (plays the trig) when
  FILL mode is active"*, held on a button, latched with a gesture, or armed for one pattern cycle.

FILL is the one that is a performance control rather than a pattern edit, and it is the one that
suits Keys best: **one button, held, and the bar rewrites itself.** On a mouse-only surface that is
an enormous amount of expression from a single target. A:B is the one that changes what the
sequencer can *say*.

**Cost:** A:B and 1ST/LST are values appended to the existing Chain lane, which is the cheapest
possible shape. FILL is a lane plus a bar button.

## 6. The shift register (Music Thing Turing Machine)

Confirmed from the source rather than from memory. It is *"a binary sequencer, based around a 16
bit memory circuit called a shift register"*, and *"a sequencer that you can steer in one
direction or another, not one that you can program precisely."* One knob does all of it:

- **12 o'clock:** *"the sequences are random"* - never repeats.
- **Fully round:** *"locks into a repeating sequence."*
- **3 or 9 o'clock:** *"slipping; looping but occasionally changing notes."*
- **7 o'clock:** *"double locks into a repeating sequence twice as long as the 'length' setting."*

That middle setting is the whole reason the module is famous, and it was the thing none of Keys'
four randomnesses could do. Roll rerolls once and stops. Drift wanders and never settles. Rand is
per-step. Nothing in Keys wandered and then hardened.

**Built 2026-08-18 as MUTATE x LOCK**, and this is the first thing on this page to move, so it is
worth saying what changed on the way. **MUTATE itself grew a second and third zone on
2026-08-19** (Owen: "higher values can go out of scale") - see below for what that means for the
"never leaves the chord" claim two paragraphs down. It is not a ring buffer. A register carried between blocks
would have been the second thing in `ArpEngine` that is not stateless from the playhead - the
first, `laneChain`, remembers one bit and self-corrects within a step, and that is documented as
the cheapest possible break of the rule rather than an invitation. So the variation is a **hash of
(step, era)** instead: LOCK stretches an era, and at its top there is one era for good. Same three
positions the knob above describes - random at the bottom, slipping in the middle, locked at the
top - with a transport jump landing on the variation it would have walked to, which a register
cannot promise.

The other departure: it does **not** feed the Note lane. The module's randomness is voltage and can
land anywhere; Keys' has to answer to a held chord, so MUTATE moves the run to another entry of the
sequence built from that chord and can never leave it - **true through the knob's first half (to
50) as of 2026-08-19**. Past 50 a second stage (`ArpEngine::mutatedPitch`) may stray the placed
pitch off the chord tone entirely: scale degrees from 50 to 75, chromatic steps from 75 to 100,
which is what makes "higher values can go out of scale" true of the knob Owen asked for. Both
stages hash through the same `(step, era)` cell, so LOCK holds an out-of-scale find exactly as it
holds an in-chord one. See `docs/ARP_DESIGN.md`'s 2026-08-19 section for the full boundary
detail. The 7 o'clock "double lock" is not built.

## 7. Non-linear playback order (René)

René is *"three-channel, three-dimensional Cartesian sequencer"*: two Snake channels on X and Y
whose clocks drive a third. Keys cannot use the module's shape, and most of its manual is a memory
model (States, Mesh, M-Paste) for a device with no screen.

The idea underneath it does transfer: **a playback order that walks a grid instead of a line.**
In Keys that is a Shape, not an architecture, since Shape already decides the walk. Lowest
priority here, and listed so nobody re-derives it as a big idea.

## 8. A sequencer mode per track (OXI One)

Not a feature so much as a confirmation. OXI ships one box in which each track chooses what kind
of sequencer it is: *"Chord mode is a great tool to write the foundation chord progression of a
song, meanwhile Polyphonic mode offers total freedom up to 7 notes per step. Stochastic and
Matriceal are two types of generative modes."*

That is precisely the shape `docs/ACID_DESIGN.md` proposes for Keys, arrived at independently: an
Acid **mode on a line**, beside the chord-driven ones. A shipping product doing it is the argument
that a line should carry a *type* rather than Keys growing a second panel per idea. Every
archetype above then lands as a mode or as a lane, and never as a new section.

Also worth logging, because Keys already has it and should not rebuild it: OXI's *"auto voicing
engine, in charge of assigning the best possible voicings to every chord in order to get the least
voice movement"* is `applyVoiceLeading` / `genSmooth`.

---

## Deliberately not taken

- **Metropolix's mod lanes as self-modulation.** Eight lanes, each with its own order, length and
  division, routable *"to dozens of internal destinations."* Gorgeous, and it needs a mod matrix
  Keys does not have and could not present with one mouse. A single lane addressed at one named
  parameter is the affordable fraction, and it is a different feature.
- **Hapax's reorderable per-track FX chain** (*"an Harmonizer placed after an Arpeggiator will not
  sound like an Arpeggiator placed after an Harmonizer"*). Keys' order is fixed and documented
  (Lean, then `fitVoicing`, then `applyVoiceLeading`, in that order for stated reasons). Making it
  reorderable trades a load-bearing guarantee for a feature nobody has asked for.
- **René's State / Mesh / M-Paste memory model.** A solution to having no screen.
- **Torso's CV loopback, Metropolix's CV outputs, Pamela's entire output section.** Keys emits
  MIDI.
- **Every hardware workflow in the pile.** Digitakt, Deluge, OXI and KeyStep Pro are all
  shift-and-hold instruments. The *conditions* transfer; the gestures never do.

## Coverage, not candidates

**Pamela's Pro Workout** and **KeyStep Pro** were downloaded for completeness and neither turned up
anything Keys lacks. Pamela's Euclidean and probability work is `EuclidGen.h` and the Chance lane
already; KeyStep Pro's per-step Randomness *"randomly mutes notes/triggers in your sequence"*,
which is the Chance lane again. Keep them as cross-checks, not as sources.

---

## Ranked

By what changes the instrument most for what it costs.

1. **Curve and Flip over a Select span** (Hapax). Cheapest real expressiveness in the file, and
   the mouse-only case for a curve tool is stronger here than on any hardware in this pile.
2. **The Turing knob** (Music Thing). One knob, one buffer, an archetype Keys is missing entirely.
3. **A:B conditions and FILL** (Elektron). A:B is four appended values on a lane that exists; FILL
   is one held button that rewrites the bar.
4. **Follow Actions on the twelve slots, and a scene row** (Ableton). Small, and it is vocabulary
   Owen already has.
5. **The accumulator** (Metropolix). Movement without randomness, if it is built off the repeat
   count rather than as a running total.
6. **Phrase + Range** (Torso). The biggest of these and the only one that writes a melody rather
   than transforming one.

The acid line from `docs/ACID_DESIGN.md` is not on this list because it is already spec'd and
staged; it sits alongside as the second *kind* of line rather than as another idea for the first.

Numbers 1 through 4 are each a day or less and none of them touches `ArpEngine`'s stateless-from-
the-playhead rule. Number 5 touches it and the way through is written above. Number 6 is a week.
