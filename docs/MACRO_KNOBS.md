# The macro card's second knob row

**Status: proposed and unbuilt (2026-09-01).** Nothing in this file ships. It is the design for
what Owen asked after Density and Legato landed, looking at the harmony strip on a card:

> *"looks great! I feel like we could shrink this area so we can add more knobs. What other
> knobs could we add?"* - then *"document options. accent? target velocity?"*

Read it before building any of the candidates, and read `docs/ARP_DESIGN.md` for the engine
each of them lands in. The Density / Legato round at the top of `CLAUDE.md`'s Architecture
section is the immediate precedent: it is what made the card ten knobs wide and found the
card's second floor.

---

## 1. The room

The harmony strip is two rows. The top row is the two interval dropdowns, each taking half the
card. The row under it is their two CHANCE knobs, one centred under each dropdown - **two knobs
in a row the width of ten**, which is the space Owen is pointing at.

Nothing needs to shrink *vertically*. The knob row already exists at knob height, so turning it
into a full strip costs the card no height and leaves every window minimum where it is (the
folded 1060 px floor was hard-won; see the 2026-08-19 fold entry). What shrinks is the
dropdowns' width, which they never needed: the closed combo shows one interval name, and the
two-column popup opens at its own width regardless.

### The layout

```
 HARMONY 1                            HARMONY 2
 [ - Octave                    v ]    [ + Perfect 5th                v ]
 CHANCE   STACK   DRIFT   RAMP        CHANCE   REPEAT  STRUM   ACCENT  ECHO   CLIMB
   (o)     (o)     (o)     (o)          (o)     (o)     (o)     (o)     (o)    (o)
    0       1       0       0            0       0       0      Off      0      0
```

- **Ten cells, the same ten the top strip has**, laid out by the same arithmetic
  (`arpMacroKnobMinW`, the 6 px gaps, the two ring reservations if any of the new knobs is a
  range knob). That is what keeps `minMacroWidth()` where it is: the second row can never be
  wider than the first.
- **Each dropdown spans four cells** (about 170 px), which clears "+ Octave & 5th", the longest
  entry, with room. Measure it with `GlyphArrangement` in `LayoutTests`, the way the Shape combo
  is measured against its own longest name - `Font::getStringWidth` under-measures, and the
  chord library's `iim7`-drawn-as-`iim` is the record of what that costs.
- **Each CHANCE knob sits at the left foot of its own dropdown**: cell 1 under Harmony 1, cell 5
  under Harmony 2. That is the only arrangement in which the knob is visibly *that dropdown's*
  without a third caption, and it leaves cells 2-4 and 6-10 free: **eight new knobs**.
- Cells 9 and 10 have nothing above them. That is fine - the harmony row is two dropdowns and
  nothing else, and a third harmony voice was never asked for.
- Accessible names follow the existing form: `Macro STACK A`, `Macro REPEAT A`, and so on. The
  two chance knobs keep `Macro harmony 1 chance A` / `Macro harmony 2 chance A`, which is what the
  layout test's `contains("chance")` filter already keys on.
- `LayoutTests`' macro-knob sweep counts `numKnobs * uiArpLines` knobs and filters out titles
  containing "harmony" or "rate". A second row means either a second enum (`Knob2`, its own
  spec table and its own count in the test) or folding these into `numKnobs` with a row index
  in the spec. **Fold them in**: one table, one count, one starvation sweep, and `minMacroWidth()`
  reads one constant. The spec gains a `row` field and `resized()` walks two strips.
- **Every new parameter is appended after `apLegato`**, in the order built, and every one of them
  defaults to *off* or to what the engine does today. That is the `genSource` rule and it is not
  optional: a per-line parameter's index is what a saved session stores it under.

---

## 2. The candidates

Ten, cheapest first. Each says what it does, which parameter it is, where it lands in
`ArpEngine.h`, what it interacts with, and what it costs. Costs are honest estimates including
tests and docs.

### A. STACK - how many octave copies the run reaches through

- **What.** The Play page's **Repeats** (SPREAD group): 1-4 copies of the chord stacked upward,
  so an Up run climbs through two or three octaves before it wraps. OCT beside it on the top
  strip is where the run *sits*; this is how far it *reaches*. The two were separated on
  2026-08-02 for exactly that reason ("how high does it sit" and "how far does it reach" are
  different questions and only the first has a middle).
- **Parameter.** `arpOctaves`, exists, int 1-4, default 1. **No new parameter.**
- **Engine.** Nothing. `buildSequence` already stacks it.
- **Name.** Not OCTAVES - two knobs reading OCT and OCTAVES on one card is the confusion the
  2026-08-02 split was fixing. Not REPEATS either, because candidate D wants REPEAT and two
  cells apart that is the two-names-one-idea mistake in reverse. **STACK** says what it does,
  and the Play page's label should follow it (label only; the id never moves).
- **Interactions.** Distance decides *what* is stacked (fixed intervals or scale degrees). Scale
  Lock snaps the result in `addHit` like everything else.
- **Cost.** An afternoon: a spec-table row, the Play page relabel, a `StateTests` line that the
  card and the page bind one id.

### B. DRIFT - the machine wandering

- **What.** The Play page's **Drift** (FEEL group): strays either side of the drawn lane value
  *while it plays*, so a part never repeats exactly and the lane on screen never changes.
  Bipolar around the drawn value where Humanize is one-sided, because a machine wandering is a
  different thing from a player rushing. Confined by `laneDrifts` to the lanes that decide *how*
  a step plays - octave, velocity, gate, lateness, chance - never *which* note.
- **Parameter.** `arpDrift`, exists, int 0-100, default 0. **No new parameter.**
- **Engine.** Nothing. `driftedLane` reads it.
- **Interactions.** With Density: Drift wanders the Chance lane too, so a Density at 100 over a
  drifted lane still drops the odd step. Worth a line in the tooltip.
- **Cost.** An afternoon.

### C. RAMP - a swell or fade over a held chord

- **What.** The Play page's **Ramp** (FEEL): the line's velocity climbs or falls over the first
  N beats after a chord lands, so a line comes in under the others and rises, or stabs and
  decays. Positive swells, negative fades, zero is off.
- **Parameters.** `arpVelRamp`, exists, int -100..100, default 0; `arpRampBeats`, exists, int
  1-32, default 8. **No new parameter.**
- **Engine.** Nothing. `rampScale` is sampled once per block in `process()`.
- **Where the time goes.** Not on the ring. A `RangeKnob`'s ring is a *band* on every knob that
  has one - VEL, H.TIME, the pads' Strum and Humanize - and a ring meaning "how long" would be
  the one ring in Keys that lies. The time stays on the Play page, or takes a cell of its own
  (**TIME**, 1-32 beats) if Owen wants it on the card. Recommend the page: eight beats is a
  usable default and the amount is the knob you reach for.
- **Cost.** An afternoon.

### D. REPEAT - odds that a step ratchets

- **What.** The "more notes" half of the density idea, which Density deliberately did not take
  (Density thins; it never adds). Per step, a roll: at REPEAT 0 nothing changes, at 100 every
  step ratchets. A ratcheted step plays its hits twice inside the step, which is what the Ratchet
  lane already does per step by hand.
- **Parameter.** `arpRepeat`, **new**, int 0-100, default 0.
- **Engine.** In `fireStep`, `ratchets = max(laneRatchet, rolled ? 2 : 1)`. **The knob never
  subtracts**: a lane cell drawn at 3 stays 3 whatever the knob says, so a drawn pattern keeps
  its meaning under the knob. Roll off the same (step, era) hash Mutate and Stray use, so **LOCK
  holds it** - a stutter the machine found can harden into the part, the same rule as a stray.
  Two rather than a random 2-4: a roll that also chooses the count is two questions on one knob,
  and the lane is there for anyone who wants a triplet ratchet on a particular step.
- **Interactions.** Legato holds only a ratchet's *last* sub-hit, already. Harmony voices copy
  every sub-hit, already. `pending[]` has room for it (96 carried hits).
- **Name.** REPEAT, with the Play page's Repeats renamed STACK (candidate A) so the word means one
  thing. Not RATCHET: that is the lane's name, and the lane is a per-step *count* where this is a
  per-line *odds* - the same reason the knob that thins a line is Density and not Chance.
- **Cost.** A day. `ArpTests` pins: the count at 0 and at 100, the lane winning over the knob,
  Lock holding the roll.

### E. STRUM - rake the notes of a chord step

- **What.** A step that plays several notes at once - the **Chord** shape, a Chord-lane step, a
  harmony voice on top of its source - fires them dead flat today. The pads have had a strum
  since they were pads; the arp's chords do not. This spreads a step's hits over a few
  milliseconds, lowest first.
- **Parameter.** `arpStrum`, **new**, int 0-100 ms, default 0.
- **Engine.** In `fireStep`'s ratchet loop, sort the step's hits by pitch and offset hit *i* by
  `i * strum / (hitCount - 1)` samples, **capped at 40 % of the sub-length** - the same cap
  Humanize's lateness carries and for the same reason: a rake that carries one hit past the next
  sub-hit is two hits of one pitch arriving out of order, which `emitHit`'s close-what-you-land-on
  rule cannot survive. A single-note step is untouched (a strum of one is nothing), so on Up,
  Down and the other walks this knob is inert and the tooltip should say so.
- **Interactions.** Legato's release rides the step's *first* hit; under a strum that is the
  lowest note, which is right - the held note lets go as the rake begins. Harmony voices are
  raked with their sources by pitch, which reads as a chord, not a flam. Direction: up only.
  The pads' Dir (Up / Down / Random) is a second control, and nobody asked for it here.
- **Cost.** A day.

### F. ACCENT - the velocity the downbeat lands at

Owen's question: *"accent? target velocity?"* Yes: a **target**, and that is the design decision
in this candidate.

- **What.** The first step of every bar lands at the velocity this knob names, instead of at
  VEL. It is the 303's other trick, and the fastest way to make a run feel like it has a
  downbeat rather than a grid.
- **Parameter.** `arpAccent`, **new**, int 0-127, default 0, **and 0 reads "Off"** - the
  Stray rule: zero is its own off switch, so no toggle beside it. Velocity 0 is a note-off in
  MIDI, so no accent can ever *mean* 0; the readout saying Off there is honest, not a fudge.
- **Target, not boost, and why.** A boost is an amount added to VEL: +30 on a VEL of 100 clips at
  127 and reads the same as +27, +30 on a VEL of 30 is barely audible, and either way the knob
  means something different at every VEL setting. A target lands the downbeat at the same
  loudness wherever VEL sits, which is what a player does - and it can sit *below* VEL, which is
  a ghosted downbeat, a real thing (a reggae skank is exactly a quiet one). One knob, one MIDI
  number, the unit VEL already uses.
- **Engine.** In `fireStep`, on an accented step the **level** handed to `humanisedVelocity` is
  the target instead of `p.velLevel`. Everything that already modifies the level still applies:
  the Humanize ring wanders around the target exactly as it wanders around VEL (clamped to the
  nearer rail, its own rule), the Velocity lane and the Ramp still multiply, harmony voices still
  take their source's draw. The accent is a *second level*, not a bypass of programmed dynamics.
- **Which steps.** The first step of each **bar**: in Sync, `boundaryBeats` a multiple of four
  (Keys reads no time signature and never has; every bar in Keys is four beats). In Hz there is
  no bar - a beat is a second there - so the accent lands every four seconds, which is what
  every beat-measured control does in Hz and is the honest reading rather than a special case.
  A period (1 beat / 2 beats / 1 bar / 2 bars, the Retrigger list) is the obvious second
  control and belongs on the Play page if it is ever wanted; it is not part of this candidate.
- **Open question, decided one way for now.** Should Density skip the accented step? A
  downbeat that drops out is disorienting. But Kirnu and Cthulhu special-case nothing, the
  Chance lane already lets a pattern protect its downbeat by hand, and a knob that quietly
  exempts one step from another knob is the kind of coupling the RangeKnob round paid for. **No
  exemption.** Revisit if it grates in use.
- **Cost.** A day, most of it the tests: the accented step at the target with VEL elsewhere, the
  ring wandering around it, a target below VEL, the Velocity lane still multiplying, Off leaving
  every velocity byte-identical.

### G. ECHO - repeats after the step, decaying

- **What.** Each hit is played again one to four times after the step, quieter each time: a
  MIDI delay inside the arp. On a slow ambient line it is most of the texture.
- **Parameter.** `arpEcho`, **new**, int 0-4, default 0. Decay is fixed at 0.7 per repeat; a
  **DECAY** knob is the obvious second cell if the fixed number is wrong, but ship the one first.
- **Spacing.** One step - the step length after Dot and Tuplet, so an echo lands on the grid
  the line is already on. A dotted-eighth delay (the 3/16 that every guitar pedal defaults to)
  is a division picker on the Play page, later, not part of this.
- **Engine.** In `fireStep`, after the ratchet loop, park each echo in `pending[]` at
  `offset + n * stepSamples` with `vel * 0.7^n`. Echoes carry `hold = false` and
  `closeHeld = false`: they are copies, and Legato's rules belong to the step's own hits. An echo
  landing on a step that plays the same pitch is closed by `emitHit`'s tie branch before the new
  note-on, the rule that already handles gate > 100 %. **Capacity is the real constraint**:
  `pending[]` holds 96, and a Chord-shape step of eight notes with two harmony voices and four
  echoes is 96 exactly - so echoes are dropped, never mistimed, when the carry is full, the rule
  `fireStep` already states for ratchets. Echoes do not themselves ratchet, strum, or roll
  against Density: an echo is what the step already did, later.
- **Interactions.** Light keys lights an echo like any hit. Scale Lock has already snapped it.
- **Cost.** A day and a half; the capacity and the tie cases want tests of their own.

### H. CLIMB - Metropolix's accumulator

- **What.** Every pass through the pattern transposes the whole run by N semitones, wrapping
  back after four passes: `+2` climbs a tone per bar and comes home on the fifth. **Movement
  without randomness**, which `docs/SEQUENCER_LANDSCAPE.md` ranked as one of the three unbuilt
  ideas worth having, and the only one of them that is a knob.
- **Parameter.** `arpClimb`, **new**, int -12..12, default 0 (bipolar, centred, zero off - the
  OCT shape). Wrap fixed at four passes; `arpClimbWrap` (1-8) is the second parameter if the
  fixed number is wrong, appended later.
- **Engine.** Stateless from the playhead, the engine's standing rule: `pass = relStep /
  walkLength`, `climb = (pass % wrap) * semis`, added in `addHit` beside OctShift and before the
  Scale Lock snap. **The pass is measured over the window the Note lane walks**, the way LOCK's
  era already is, so a four-step loop climbs every four steps and a sixteen-step lane every
  sixteen. A Retrigger restart is a new pass zero, which falls out of `stepBase` for free.
  Metropolix's hold and bounce limit modes are not taken; wrap is the one that reads as a
  progression.
- **Name.** CLIMB rather than ACCUM: "accumulator" is the manual's word and says nothing to a
  mouse user reading a 9 px caption; CLIMB +2 says what will happen. ACCUM stays in the tooltip
  for anyone who knows the module.
- **Cost.** Two days, mostly in `ArpTests` (the pass boundary, the wrap, the retrigger reset,
  Lock snapping a climbed note).

### I. GLIDE - portamento time, for Legato

- **What.** Legato makes the overlap a synth needs to slide; this tells it how long the slide
  is, by sending MIDI portamento time.
- **Parameter.** `arpGlide`, **new**, int 0-127, default 0 (Off: no CCs are sent).
- **Engine.** Emit CC 65 (portamento on/off) on Legato's edge and CC 5 (portamento time) when
  the knob moves, on the line's channel. Both are message-thread events crossing to the audio
  thread: park a "CCs pending" flag on the line the way `rerollRequest` crosses, and stamp them
  at the top of the next block.
- **The honest caveat, and it is the whole reason this is last.** It works only on synths that
  read CC 5 and CC 65 - many hardware-modelled ones do, many soft synths ignore both and keep
  glide as a panel setting. On those it is a knob that does nothing, which is the class of
  control the settings menu's UI-scale entry was removed for. Ship it with a tooltip that says
  which, or not at all.
- **Cost.** A day.

### J. RANGE - how many notes of the chord the run may use

- **What.** Torso T-1's Range: the walk sees only the lowest N entries of the sorted chord, so
  a four-note chord under RANGE 2 arpeggiates its bottom two notes and Mutate explores within
  them. Deterministic where Mutate's reach is random.
- **Parameter.** `arpRange`, **new**, int 1-8, default 8 (all).
- **Engine.** `seqCount = min(seqCount, range)` in `buildSequence`, **before** the octave
  stacking so STACK still stacks the window.
- **Why it is last of the musical ones.** It overlaps two things Keys has: the Note lane's fixed
  indices already pin a step to an entry, and Distance / STACK already shape what the sequence
  holds. It earns a cell only if Owen wants to play the *top* of a chord and its bottom on two
  lines fed the same card - which is a real use, and a bottom/top switch would then be the
  second control.
- **Cost.** Half a day.

---

## 3. The row, if the suggested eight are built

```
 CHANCE   STACK   DRIFT   RAMP   |  CHANCE   REPEAT  STRUM   ACCENT  ECHO   CLIMB
```

Reading left to right under each dropdown: how far it reaches, how much it wanders, how it
swells; then how often it doubles, how it rakes, where the downbeat lands, what trails it, and
where it goes next bar. GLIDE and RANGE are held back: one is synth-dependent, the other
overlaps two controls that exist.

Three of the eight (STACK, DRIFT, RAMP) are faces on parameters that exist and can land in one
afternoon together. The other five are one parameter each, appended in the order built, each
with its own `ArpTests` block - and the frame in `ArpEngineTests::runTest` is already the reason
`Keys_tests` links with an 8 MB stack, so five more blocks of engine locals is worth a glance at
that number.

---

## 4. What this file is not

It is not a commitment to ten knobs. The room is eight cells and Owen picks which; a cell left
empty is empty, not a promise. It is also not the place for the Play page: a knob that gets a
face here keeps its slider there, the way Density kept its Play page slider, because the deep
view is where a control you set once and leave belongs and the card is where you sit and turn.
