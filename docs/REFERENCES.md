# Reference manuals

Nine manuals feed this file. They are not decoration: nine features in Keys were built from a
specific page of one of them, and **four times now a manual has corrected a guess** that had
already been coded and looked right on screen. This file records what each one contributes, what
Keys took, and what it deliberately did not - so the next person does not re-derive a decision
that was already made, or re-invent one that was already rejected.

The Scaler 3 entry arrived in two halves and is the clearest case for the rule at the bottom of
this file. The **vocabulary** came first, as two CSVs off Owen's disk, and the library was designed
against it. The **manual** came after and corrected two things - including one that no amount of
staring at the CSVs would have shown, since Scaler uses the word "Mood" for two unrelated controls
and only one of them is a tag.

**Eleven more PDFs are in `manuals/` and are not in this file.** They arrived 2026-08-17 and
are surveyed in `docs/SEQUENCER_LANDSCAPE.md` instead, because this file's contract is what Keys
**took** from a manual and nothing has been taken from them yet. Move an entry here the day
something ships from it, and not before.

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

**Also taken, 2026-08-18** - and this is the round where the *Note graph itself* came over, which
is what Owen was pointing at when he sent screenshots of these pages:

- **Per-step shapes** (p23-24). *"The top-half of the graph is various arpeggiator patterns, which
  act like a typical arpeggiator, where the note output varies consecutively one step after
  another."* Eight of them - up, down, up/down, down/up, up and down, down and up, fingered bottom
  and fingered top - appended to the Note lane above the Kirnu modes, in Cthulhu's own
  bottom-to-top order. **This is the thing that makes the graph an arpeggiator you draw** rather
  than a list of note numbers, and Keys had exactly one shape (the line's) before it. The "shares
  one walk" half is taken too, and is the part that is easy to get wrong: a lane of mixed shapes
  advances one cursor, so Up then Down comes back down the line it went up.
- **`fingered top` / `fingered bottom`** (p24) as shapes in their own right: *"every 2nd note is
  the high note of the chord"*. Keys' `Direction` had neither, so both were appended and the line's
  own Shape combo gained them as well.
- **Markers at a height, not filled bars.** Copied from the picture rather than the text, and it is
  a real distinction: Cthulhu's Note graph draws a small block at the value's height because the
  value is a *name*. Keys drew every lane as a bar filled up to its value, which reads a Note of 5
  as "more than 4". Only the Note lane changed; every other lane really is a magnitude.
- **Position Reset** (p25): *"the arpeggiator will reset on this step to play the first note of the
  arpeggiator pattern"*. Built as `laneReset`. It restarts the **walk** and not the lanes, which
  the manual's own example is careful about - it is about which note of the chord comes out.
  Cthulhu reaches it by alt-clicking the Note graph; Keys' right-click list is closed and a
  modifier is not a gesture it may require, so it is a lane - which is exactly what this file
  predicted a per-step version would have to be.

**Not taken:**
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

**Also taken, 2026-08-18** - the step sequencer pass, and Cream is where all of it came from:

- **Per-control loop points and Loop direction** (p11): *"Every step control have their own loop
  control. This enables playing different controls independently from other controls"*, and Up /
  Down / Up alt / Down alt beside it. Keys had per-lane *length* alone, which is polymeter without
  phrasing - every lane started at step 0 and walked forwards, so three lanes of eight could only
  ever be three lanes of eight in step. Built as `Lanes::loopFrom` / `loopTo` / `dir`, with the
  window as a bar under the grid on the same cells. Kirnu's own click rule came with it (*"Loop
  points follow mouse click... the pointer closest to the mouse is moved"*), which is already a
  left-click-only path and is why the window needed no steppers beside it. **Not taken from it:**
  the right-button-moves-the-far-handle half, which the closed right-click list forbids; the
  nearer handle always is the whole rule here.
- **The per-control on/off** (p12): *"Toggle On/Off: toggles selected pattern control on/off. When
  control is off all it's values are ignored."* Built as `Lanes::on`; the lane returns its default
  and the drawing is untouched. Keys had no way to take a lane out at all - Reset flattens it,
  which sounds the same and loses the work.
- **The tab marks** (p12): a corner mark for whether the control is on, and a second one meaning
  *"that control has input values"*. Copied as a strike-through and a dot on each lane tab, and
  they are the single biggest readability win of the pass: eleven of twelve lanes are invisible at
  any moment, and until this nothing said which of them were doing anything.
- **Copy and Paste** (p8), the last two of the tool palette. **Clear was deliberately not taken**:
  its job is *"set values to default"* over the selection, and Keys' Reset already narrows to the
  Select span and already means exactly that.

**Not taken, and worth considering:**
- **An enable row per *step*, per lane**, as distinct from the whole-lane switch above: *"First row
  from bottom can be used to turn selected data section steps on or off. When step is off the value
  in step is ignored."* Keys' MUTE row is the Note lane's alone. A per-lane version would let you
  disable one step's Octave without flattening it to 0.
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

- **Probability per cell** - Keys' Chance lane, multiplied by the global Chance slider in the
  Play page's PLAYBACK group (2026-08-18: the per-line macro-card knob that used to carry this
  multiplier became Mutate and Lock instead - see `docs/ARP_DESIGN.md`, "Mutate and Lock").
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
`Direction` picked which side it reaches - **retired 2026-08-19**, along with the one-sided band
itself (see below).

**A second departure, later than the first (2026-08-19):** Serum's own depth band is one-sided,
reaching only away from the face. Owen's reading of the halo drag disagreed with that shape on
sight - *"the halo scroll is not intuitive. it should expand in both directions. up is more"* -
and the first attempt at a fix still moved the face under the drag, which was not what he meant
either: *"moving the halo shouldn't move knob. should be equal from center."* Keys' `RangeKnob`
now centres the band on the face - the halo (or the wheel over it) opens the range equally in
both directions and the face itself never moves under that gesture - which is a deliberate
departure from the manual's own model, not a correction of a misread. The satellite and the
margin-drag fallback above are otherwise unchanged.

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

## Arturia Acid V (`Acid-V-Manual.pdf`, ~100pp)

Added 2026-08-16, at Owen's ask, to spec an acid line for Keys. Chapter 4 is the whole
sequencer and is the only chapter that matters here (chapters 3, 5 and 6 are a synth Keys does
not have). `docs/ACID_DESIGN.md` is the spec that came out of it; the short version:

- **The sequence owns its pitch.** This is the one thing Keys' arp cannot express: its Note
  lane picks degrees of a chord you hand it, and a 303 is handed nothing. One new lane.
- **Rows are the twelve pitch classes, octave is a separate per-step slider.** Not a
  conventional piano roll, and the reason a 303 grid fits in a panel that a four-octave roll
  does not. Load-bearing for Keys' fixed-height arp panel.
- **Accent is binary per step with one global amount**, because on the hardware it moves VCA
  level and filter decay together (3.2.8). A continuous velocity lane says it worse.
- **Slide is portamento into the step from the one before**, and *"if you place a slide on step
  1 the pitch will glide from the final step in the pattern"* - a wrap that is easy to miss.
- **Polymetric mode** is five independent lane lengths plus a **realign count**. Keys has the
  first half already (Link Lengths off); the reset count is new and cheap.
- **The dice are per-lane live probability**, which is Keys' Drift, not its Roll and not its
  Rand lane. Acid V puts its loudest dice on pitch and octave, where `laneDrifts` currently
  forbids them. That exception is per-mode, not global.
- **Density** thins a pattern by muting steps non-destructively; turning it up restores them.
  It must never write the Mute lane or it stops being reversible.
- Not taken: the sequence browser (Keys has twelve slots), MIDI-file export by drag, the
  hover-reveal dice (a discovery problem on a mouse-only surface), and right-click to remove a
  note (left-click-to-clear does it, and the right-click exception list stays closed).

## Scaler 3 (`Scaler-3_User_Guide.pdf`, 113pp)

The reference for the **chord library**, and the one product on this list that solves the same
problem Keys' library solves: a large collection of progressions you find by how they feel.

It arrived in two parts. First the **vocabulary**, which Owen had on disk at
`E:\Ableton\Scaler 3 Moods and Genres\` as two CSVs - **41 moods** and **40 genres** - and which
is what the library was designed against. The manual came after, and confirmed the design while
correcting two things about it.

**Taken:**

- **Both vocabularies, near-verbatim**, as `chordlib::moods()` and `chordlib::genres()` (their
  "Uplifiting" typo fixed). A producer who owns both products should read one set of words.
- **The five words Keys keeps that Scaler has no equivalent for** - Haunting, Nostalgic,
  Rebellious, Spiritual, Tender - which `MarkovData.h` had been tagging with since it was written.
  Added rather than mapped onto near neighbours: longing is wanting something, nostalgia is having
  had it, and folding one into the other loses a distinction the original author drew.
- **"Cinematic" moves axis.** Keys had it as a mood; on Scaler's list, and now on Keys', it is a
  genre. Cinematic is a place the music is going, not a feeling it has.
- **The definition of the unit.** *"A chord set is a collection of chords representing a
  progression or song saved in Scaler"* (p42). Keys' `chordlib::Entry` is the same object under
  another name, which is why "To pads" writes several pads and not one.

**Corrected a guess - three times, and the third is the useful one:**

1. The design started from a search-results guess that Scaler tagged on **mood alone**. The CSVs
   settled it in one look: two axes.
2. The two CSVs implied moods were a chord-set filter only. The manual (p41) shows **Moods filters
   scales as well** - *"Browse scales based on their emotion or feel"* - and defines the tag on a
   scale as *"a descriptive label that categorizes the scale based on its tonal quality, emotion,
   or feel"* (p48). Keys' `modes::Mode::emotion` field is the same idea and predates all of this.
3. **Scaler uses the word "Mood" for two unrelated controls**, and only the manual shows it. On
   the Browse page it is the 41-word tag. On the Create page's Explore it is
   *"Adjust the mood of the genre preset to your taste by applying a bright, dark, or neutral
   mood"* (p62) - a three-way brightness axis, not a tag at all. **Keys already has that second
   control and has it finer**: the generator's **Brightness (Major / Minor)** slider sweeps the
   seven diatonic modes Lydian to Locrian, which is the same axis at seven stops instead of three.
   Worth knowing before anyone "adds Scaler's Mood knob".

**What the manual confirms Scaler does *not* have, which is why Keys' third axis exists:**

The Browse page's chord-set filters are **collection** (Common Progressions, Uncommon Progressions,
Artists, Genres), **Moods**, and **Favourites** (p41-43). There is no axis for what a progression
*does*. The tell is on Scaler's own mood list, where **Inconclusive** and **Resolved** sit among
forty emotions while being no such thing - they are structural. That is where Keys' **Function**
axis came from (Loop, Cadence, Turnaround, Vamp, Lift, Descent, Turn, Open), and it is the
difference between "sad" returning forty candidates and "sad, and it loops" returning the four you
meant.

Section roles are not metadata in Scaler either: **Scenes** are an *arrangement* container -
*"sections that can be freely arranged, duplicated, and triggered ... building verses, choruses,
or bridges"* - rather than a tag on a progression. So the Section idea in
`docs/CHORD_LIBRARY.md` §7 is not borrowed from here; it is unbuilt, and the open Chordonomicon
corpus is where its data would come from, since that one annotates structural parts.

**Deliberately not taken:**

- **Scaler's chord sets.** Progressions are not copyrightable, but a *curated list* of them can
  attract thin copyright in its selection and arrangement, and in the EU a database right can
  attach to the compiler's effort. A word list is a taxonomy; a thousand-row library is a
  compilation. Keys' 355 rows are authored from the named canon, modal vamps, jazz turnarounds and
  film-score mediants - **written out from music-theory knowledge, not measured against a corpus**
  (see `ChordLibrary.h`'s own note, which says so at the point somebody would extend the table).
  Nothing is copied across, and that line is worth restating whenever the table grows.
- **The count.** Scaler ships 1,000+ chord sets, a large share of them artist and genre packs whose
  value is the name attached. 355 tagged on three axes beats a flat 1,000, and 355 is a number that
  can be verified by ear and by theory one row at a time - which a scraped 5,000 cannot, and which
  is the whole reason `tests/ChordLibraryTests.cpp` can spellcheck the table on every build.
- **Common vs Uncommon Progressions as a *collection*.** A crude popularity axis, and Keys already
  encodes the same information better: the table is ranked against corpus statistics on the way in,
  so the common ones are simply the ones that are there.

**On the table, and cheap:**

- **Favourites** (p43, the heart button beside a chord set). Keys' library window has 355 rows and
  no way to keep the six you actually use. It is the one thing in Scaler's browser that Keys'
  has no answer for at all, and it would need a `LayoutState` set of row names and one more
  column on the row.

See `docs/CHORD_LIBRARY.md` for the design this fed.

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

In rough order of what would change the instrument most. This list is **within the eight manuals
above**; `docs/SEQUENCER_LANDSCAPE.md` ranks a wider six from the eleven added on 2026-08-17, and
its number one (Hapax's Curve and Flip over a Select span) is a better answer than the Copy /
Paste / Clear ranked first here.

1. ~~**Copy / Paste / Clear over a selection** (Kirnu)~~ - Copy and Paste built 2026-08-18. Clear
   was not, and that is the interesting half: Reset already narrows to the Select span and already
   sets defaults, so the third tool of the palette turned out to be a control Keys had had for a
   month under another name.
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
