# Keys Arp: design spec (research-backed)

Distilled from a deep-research pass (2026-07-19) over the Xfer Cthulhu v1.1 manual,
Xfer Serum manual, Kirnu Cream and Devicemeister Stepic reviews and vendor docs, and
the JUCE ArpeggiatorPluginDemo source. Confidence notes and gaps at the bottom.

Shape "Up": no step editor exists, because most of the time you do not want one. The
control band, the twelve launchable slots and the chord cards below are there either way.

![The arpeggiator on a shape](../assets/screenshots/arpeggiator-shape.png)

Shape "Pattern": the STEPS group joins the band, and lane tabs, the selected lane and its
mute row appear between the band and the slots.

![The arpeggiator in Pattern shape](../assets/screenshots/arpeggiator.png)

The **All** tab, and the view Keys opens in: both lines at once, over the tempo and the Launch
Quantize they share. Shorter than a shape, because two rows take less than the band they
replace, and that slack goes to the chord strip below.

![The macro view, both lines at once](../assets/screenshots/arpeggiator-macro.png)

> Both lines are in **Hz** in that shot, which is why Dot and Tuplet are greyed on each row: they
> subdivide a beat, and a free-running rate has none. Click **Hz** to go back to Sync and they
> come alive. (That shot predates 2026-08-03, when Trip became the Tuplet chip.)

## Placement and contract

A new stage in the MIDI path, in this order (Cthulhu's proven pipeline order):

```
surfaces / chord pads  ->  [chord resolution]  ->  ARP  ->  hosted instrument (Keys Host)
                                                        ->  track MIDI out (all products)
```

The arp consumes whatever is sounding (held, latched, or a ringing chord pad) and
must handle both single notes and full chords. It is bypassable; bypassed, the path
is exactly today's.

**Contract change, deliberate:** the arp engine reads the host playhead (bpm,
ppqPosition, isPlaying). Keys historically never reads the playhead; the arp stage
follows the Contour/Lattice model instead. `CLAUDE.md` carries the amendment: the
arp is the only playhead consumer in Keys.

## Two lines (2026-08-02)

Owen: *"I only wanna view two arpeggiators in this window, and I wanna be able to drag a chord
from below to each one."*

**The section below describes three, and everything in it still holds - it is where the
machinery is written down.** What changed a day later is how many of it the product has: line C
came out, and the count now lives in one place, `KeysProcessor::uiArpLines`.

- **Two constants, not one.** `numArpLines` is still 3: the engines, the storage and the `arp3*`
  parameter ids are untouched, because dropping parameters from the layout is what breaks every
  saved session. `uiArpLines` is 2 and is what the UI counts off - no chip, no tab, no macro row
  is built for a line past it.
- **`arpLineOn()` is the one gate.** It answers false above `uiArpLines`, which makes line C
  inert everywhere at once: `runArpLines` skips its engine and its keys, `cardsFeedArp` stops
  counting it, Hold off greys correctly. A session saved with C running opens with C silent,
  which is the right trade against an arpeggiator no control on screen can stop.
- **`arpCurrentLine()` clamps to `uiArpLines`**, so a session saved with C current opens on B -
  a current line nothing on screen can point at would leave a pad's **Send to arp slot** menu
  item with no line to default to. The arp bar's A/B tabs no longer read it at all (2026-08-02,
  seventh pass): they became each line's own On switch, so there is nothing left on that bar for
  a "current line" to mean.
- **`layout.arpMacro` defaults true**, so a fresh instance opens with both lines on screen over
  the strip you drag from. Each macro card's own **Details** button is how you reach a line's
  step lanes and twelve slots, which are per-line and have nowhere to live in a row.

Raising `uiArpLines` back to 3 is all it would take to bring C back.

## Three lines (2026-08-01)

Owen: *"three arpeggiators so we can get polyrhythms and keep keeping what we currently have,
but having three of them, and then being able to feed cards into different lines."*

There are three of everything above: three `ArpEngine`s, each with its own rate, shape, step
lanes, twelve slots, held chord and chain. **`ArpEngine.h` did not change.** It never knew how
many of it there were - it is handed a buffer, a `Params` and a clock, and it owns nothing
global - so three of them cost a routing layer above and no engine work at all.

**Line A is the arpeggiator that was already here, down to its parameter ids.** `arpRate`,
`arpSwing`, `arpDirection` and the rest register unchanged; B and C are the same list again
under `arp2*` / `arp3*`, appended and defaulting to off. That is the whole story for saved
sessions: one opens with its arp intact and two silent lines beside it.

### Routing is by queue, not by mask

Each line has its own `juce::MidiMessageCollector`. A chord handed to line B is fired through
the ordinary note path - so it lights the keybed, honours Exclusive and the Voices cap - but
queued into *that line's* collector, and only its engine drains it. `KeysProcessor::runArpLines` then:

1. asks which lines have **Keys** on (do they arpeggiate what you play);
2. if any do, lifts the note on/offs out of the merged stream into `keyNotes` and copies them
   into each listening line's input, leaving CCs and everything else to pass through. If none
   do, the stream is untouched - which is exactly the behaviour of the arp being off;
3. runs **every** line into its own output buffer and merges them all back, restamping the
   channel if that line names one. `Params::enabled` carries the line's own switch, and it
   gates only whether steps *fire*: `noteArrived` is outside it, so a line that is off still
   takes chords in.

There is no fourth step. There used to be - a disabled line's input was merged straight
through, so a chord held to a line that was off simply sustained - and it went on 2026-08-02;
see **A line that is off holds its chord** below.

**The alternative was a per-pitch ownership mask** - publish which line owns which pitch, and
let the audio thread route the merged stream by looking it up. It races: the message thread can
clear a pitch's owner before the matching note-off has been drained, and that note is then
stranded in an engine's held set with nothing left that can release it. A queue cannot get this
wrong, because the note-off is physically in the same queue its note-on went into.

**`noteRefs` is per destination stream now.** The old rule - one note-on per sounding pitch,
released by the last owner - is a statement about *one stream*, because downstream one note-off
ends a pitch for everybody. An arp line's input is a different stream with a different consumer
(its engine, which counts owners itself in `ArpEngine::Held::ons`), so a pitch held into line B
must not suppress the same pitch played to the track output. With one shared counter it did, and
the note vanished from the output while the key lit up.

### Known edge, since fixed

Switching a line on while a chord was already ringing used to leave that chord's note-off to be
eaten by the engine, so the pitch hung downstream until All Off. The cause was `hardReset()` on
the off-to-on transition: it dropped the held set, and the note-off that arrived later matched
nothing. The 2026-08-02 change below replaced that call with `restart()`, which keeps the held
set, so the note-off matches the note-on that is still there. Recorded because the shape of the
bug is worth knowing, not because it is still live.

### All Off, and Light keys (2026-08-02)

Two chips joined Hold off on the arp bar, both at Owen's request, and both stay reachable with
the section folded for the same reason Hold off does.

**All Off** (`KeysProcessor::allArpOff`) switches every line off, then releases every hold,
stops every chain and drops every pending quantized launch. Switching off is the half that makes
it more than a second Hold off: release the chords without it and the engines simply pick back
up on whatever the keybed is holding, so the button would silence the room for a sixteenth note.
It is **always enabled**, unlike Hold off, because a stop button that greys itself out is one
you have to read before you can trust it.

**Light keys** (`layout.arpLights`) lights the on-screen keybed for the notes the arp is
*playing*. `arpNoteOn` is a flag per pitch written on the audio thread from each line's `out`
buffer - never off the merged stream, where the arp's notes are indistinguishable from the
pass-through beside them. Three things are load-bearing:

- **It is layout state, not a parameter.** It changes what is drawn and nothing that is heard,
  so there is nothing here for a host to automate.
- **`keybedLit()` is the keybed's own question**, not `isNoteSounding()`. That answer feeds the
  live chord card too, and an arpeggio is a run of single notes: folding the arp into it would
  rewrite the "current chord" as whichever note the arp happened to be on.
- **With the option on, a running line's held chord is not lit.** That chord is the run's
  *input*, so lighting it holds down every pitch the arp is chewing and the arpeggio inside it
  is invisible. Owen's report on the first cut was *"it just shows the chords that are being
  played"*. Hiding the input is what makes the output visible.

**PLAY, not KEYS.** The per-line `arpKeys` switch is labelled PLAY on its row. The id is
unchanged - ids are never renamed - but "KEYS" collided head-on with a control a few pixels away
that is about the keybed *lighting up*, and Owen asked what the difference was. Routing and
display had the same word in them; each label now names what it touches.

### A line that is off holds its chord (2026-08-02)

Owen: *"when you drag your chord onto an arp, I don't want it to play the chord sound when you
release"*, and *"when you turn on the arp, it should start playing whatever card is loaded ...
right now it only plays when you drop a new line on."*

Two reports, one cause. `runArpLines` had a bypass branch that merged a disabled line's input
straight into the output: the chord sustained like a pad, which is the sound on release, and the
engine never saw it, so switching the line on found nothing to play.

`ArpEngine::process` consumes note-ons in `noteArrived` **outside** the `p.enabled` gate - that
flag has only ever gated the step-firing block. So the branch is gone, the engine runs every
block, and `ap.enabled` carries the line's switch. A line that is off is a silent holder; the
switch starts what it is already holding.

Two things this deliberately does not change:

- **`restart()`, not `hardReset()`, on the off-to-on edge.** hardReset drops the held set, which
  is precisely the chord that is waiting. restart is the same call minus that, so the scheduler
  starts clean and the chord survives.
- **The keybed is untouched.** `listens[n]` is `arpLineOn(n) && Keys`, so a line that is off
  never lifts notes out of the merged stream and what you play sounds exactly as before. Playing
  the instrument is never gated on an arp switch.

### On screen

- **A and B on the arp bar are that line's own On switch** (2026-08-02, seventh pass, Owen: "the
  A and B on the left side of the header, I want those to be on and off buttons to turn on or
  off the ARP ... we can remove the a and b check mark on the right side of the header"). They
  used to be a pure navigation tab - selecting which line the panel edited - with a separate
  lettered On chip doing the actual switching a few pixels away, near Hold off: two controls for
  one job, one of them a checkmark easy to miss. The chip is gone; clicking A or B now toggles
  that line's `arpOn` / `arp2On` through an ordinary `ButtonAttachment`, the same shape every
  other APVTS-backed bar toggle in this file uses. Being a power switch rather than a navigation
  control changes what folding the section means for them too: they **never hide with the
  fold**, the same "reach for it while playing" case BPM and Quantize already had on this bar,
  and they no longer select a line for editing at all - there is no `onClick` left on them.
  They stay chord drop targets regardless: dropping a card on a letter still hands it to that
  line whether it is on or off. **Hold off stays one button** and releases every line and every
  chain. Beside it since 2026-08-02: **All Off**, which switches every line off *and* lets go of
  everything, and **Light keys**, a display toggle - see their own sections below.
- **Each macro card's own Details button opens that line's deep view** - band, step lanes, the
  twelve slots, Bars, Chain - now that A and B no longer do (2026-08-02, seventh pass, Owen:
  "maybe we can add another button on the bottom by anchor, like details, and that can open up
  the detailed arpeggiator view"). It sits beside Anchor in the card's bottom sub-row and calls
  the same `setEditLine` a tab click used to call. Changing the edited line still tears down
  every APVTS attachment in the single-line band and rebuilds it against the new line's ids -
  the same move `refreshRateMode` has always made for the rate dial's two units, guard against
  swapping under an open drag included - only the control that triggers it moved.
- **The Pads bar's letter chip, which used to say which line a chord-card click fed, is gone**
  (2026-08-02, same pass that moved the tabs). A click stopped feeding a line at all earlier the
  same day (see **A click never feeds a line** below), which left the chip naming nothing but a
  right-click menu's default target - Owen asked for it removed along with Mode and Scale
  Compliance ("remove the scale and percentage and letter b from pads header"). The current line
  is set only by a drop or a Details click now; nothing on the arp bar shows it, since A and B
  read as On/Off rather than as a selection.
- **Drag a chord card onto a slot** to bind it there, **onto a tab**, or **onto a line's row in
  the macro view**, to hand it over now. Stock `juce::DragAndDropTarget` on each of the three
  (2026-08-02): the slot cards, the line tabs and the macro rows take the drop themselves, and
  the pair of screen-position hit tests the editor used to mediate - `externalDropSlotAt` and
  `externalDropLineAt`, the second a near-copy of `ChordPads`' own - are gone with the belief
  that made them necessary (see `src/ui/ChordDrag.h` and the chord generator section of
  `ARCHITECTURE.md`: JUCE delivers across two top-level windows, it just needs telling to).
  Walking *up* from whatever is under the point is what makes the whole macro row a target
  including the knobs sitting on it - the knob is found first, and its parent is the line - and
  that is precisely what JUCE's own `findTarget` does, so it survived the deletion rather than
  being reimplemented. A drop sets the current line but never changes the view: it is routing a chord, not
  navigating, and in the macro view the line it landed on is already in front of you.

### The macro view (the fourth tab)

Owen: *"a fourth option for a simplified version that shows a little bit of all of them ... the
goal is to be able to create complex polyrhythms from one view."*

**All** swaps the per-line band and the step editor for one card per line, **side by side**
(2026-08-02, Owen: *"parallel to each other instead of one on top of the other"*), and that is
the whole view: the **A/B/All tabs** and **Launch Quantize** ride the ARP
section bar (fourth pass, same day: *"move the bpm and the a b and all into the header also.
remove the 'lines' text"* - they are editor-owned there, because the bar outlives the panel),
the **tempo** is a plain number on the *Controls* bar one pass later (*"like the bpm in
ableton, just a number"* - it is the plugin's clock rather than the arp's, which is why
Quantize stayed behind), and the twelve slots and the Copy / Clear / Stop / Chain action row
belong to the per-line tabs. Each card draws its own captioned ruled frame - **LINE A**, **LINE B**, filled, with the
old outer LINES box gone, since a frame around both was what made two arpeggiators read as one
(*"we need a bit more clear delineation"*). A card is three stacked lines, because half the
panel's width cannot hold what used to be one full-width row: a detented rate knob with `<`
`>` and its Sync/Hz switch, and the shape with steppers of its own, under **RATE / SHAPE**
micro-caps so the two stepper pairs read as belonging to their words; **eight knobs** under
their own headings - Oct, Gate, Chance, Swing, Offset, Vel, H.Time, H.Vel, the last two being
**range knobs** since 2026-08-03 (face for the most a draw ever does, the ring around it for how
far under that it may fall, arc between them the range, and the whole range travels with the
face - see `src/ui/RangeKnob.h`, and the note below on the satellite that opens it); and the
rate's
**Dot / Tuplet / Anchor**, a **Details** button and the held chord along the bottom. Owen's
brief when the first cut carried three lines: *"what other knobs can we have? should be like
regular arp settings."* The row carried Latch, PLAY, Chain and its own On switch for a day;
Latch and **Play** still live on the band (Play beside Retrigger, the same `arpKeys`) and Chain
on the action row, but the card's own On switch (`onButton`) is gone outright (2026-08-02,
seventh pass) - the day the A/B tabs on the bar became that line's actual On switch, two
on-switches for the same parameter, one of them buried in a card, was a control to get wrong
twice. **A line that is off scrims its card instead of losing a control.**
`paintOverChildren` fills the card body (not the LINE A / LINE B caption strip, which stays
legible) with a translucent grey, skipped while the card is a drop target, and touches no
control's `setEnabled` - every knob, the rate dial and the card itself as a drop target stay
fully live, both so a rate can be dialled in before switching the line on and because a chord
dropped onto an off line has to land (see **A line that is off holds its chord**, above).
**Details**, added the same day beside Anchor, is now the only way from a macro card back to
that line's deep view, since A and B stopped navigating anything. And since the fourth pass,
**a click on a chord card never feeds a line** - the drag (onto a card, onto A or B, or onto a
slot) is the only way in, and a click just plays the pad.

Four of those knobs are not what the first cut had (both passes 2026-08-02). **Oct** is
`arpOctShift`, a transpose centred at zero, not `arpOctaves`, which stacks copies upward and
has no middle; the stacking range stays on the per-line tab beside Distance, the other half of
the same feature. **Vel** is `arpVelTrim`, bipolar around "as played" (up boosts, down cuts,
full left mutes) and **squared** - `((100+VEL)/100)^2`, applied after the engine's 0.05
audibility floor so a deep cut reaches MIDI velocity 1 rather than pinning at 6, and a chord
handed to a line skips the keyboard Humanize range so the reference level holds still (all
three 2026-08-02, Owen: "I was at negative 96, and it was still pretty loud"). It replaced
**Vol** (`arpVolume`, cut-only, misnamed for what it touched -
the parameter survives for old sessions and `migrateVelTrim` folds it in exactly), which had
itself taken the place of **Ramp *and* Time** together - they are one feature between them,
and a row carrying Time with no Ramp would be a control with nothing to time. **H.Time** and
**H.Vel** are Humanize split into its halves (`arpHumanize`, now timing-only, and
`arpHumanVel`), so the late-nudge and the velocity shave randomize independently. Ramp, Time
and the split Human pair all live on the per-line tab's FEEL group.

The knobs are the band's own machined rotary rather than sliders, and each column heading is
written once on the top row while every row reserves the same strip, so the columns line up
without three copies of the same word. Rate is a knob as well, but keeps its steppers: a knob is
a drag target and those are the click-only path to every division.

Four decisions worth keeping:

1. **It is a view, not a fourth line.** `editedLine` is untouched by it, so a chord card drag
   still has one unambiguous target while both lines are on screen. A "line D" that meant
   "all of them" would have made that drag ambiguous and the arp bar's A/B tabs a lie.
2. **The panel does not grow.** `arpMacroTotalH` replaces the two band rows rather than joining
   them. A fourth band would have taken Pattern shape past the default window height, which is
   the whole reason this is a tab and not a section.
3. **Each row's attachments are bound to its own line for good**, unlike the band's, which
   rebind whenever the edited line changes - a Details click now, where a tab click used to do
   it. Two lines at once cannot each be "the current line", so the two cannot share a mechanism
   - and the rows are built once and hidden rather than created on demand, so nothing churns
   when the edited line moves.

4. **The knob strip is reserved out of the row before Shape takes its cut.** Laying the knobs
   last and giving the last one "whatever remains" starved it to nothing as soon as the row got
   tight - eight knobs drew as seven, with no other symptom. Shape absorbs the slack instead,
   because a narrower combo is still a combo and a zero-width knob is a bug. The column headings
   are placed from the control they name rather than by walking a second copy of the layout, so
   there is one source of truth for where a column is.

### Launch Quantize

Owen, asking for something he could not name: *"there's a setting in Ableton where the
arpeggiator, if you start a new note or something that goes into the next sequence, so it
sounds good always."* It is the transport bar's **Quantization**, and `arpQuantize` is it:
Off, 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars.

- **It defers the gesture, not the notes.** A slot launch moves that line's Shape and Rate as
  well as its chord; all of it has to land on the boundary together, or the parameters jump
  when you click and the chord arrives half a bar later. So `PendingLaunch` carries what was
  asked for, and `fireLaunchNow` is the single description of what a launch *does*.
- **Public entry points defer; the `*Now` twins do not.** `launchArpSlotNow` is what the chain
  calls (it is on a bar line by construction) and what a slot launch calls for its own chord
  (that launch has already waited). Deferring either would drift.
- **One pending launch per line**, replaced rather than queued: a second click before the
  boundary is you changing your mind.
- **The deadline is wall clock.** `arpBeats` - the beat position the audio thread publishes
  once a block, the host's own while it rolls and an internal count otherwise - turns "beats
  until the boundary" into milliseconds, and the 1 ms strum timer waits it out. Not the 50 Hz
  heartbeat: 20 ms is a sixth of a 1/16 at 120 bpm, which is the sloppiness this removes. And
  not the audio thread, which cannot move parameters or fire notes.
- **Global, not per line.** The value of it is that the three land together.
- **Never the keybed.** Playing a note is playing an instrument.
- A panic and Hold off both clear what is pending: a chord on its way is a chord to let go of.

## Engine (build from scratch; JUCE's demo is not a reference)

Verified: the official ArpeggiatorPluginDemo has no tempo sync at all (free-running
sample counter, one unsynced 0..1 knob, roughly 25-275 ms steps) and its offset
math was adversarially refuted as a pattern to copy. Requirements:

- Derive step boundaries from `ppqPosition`/bpm fresh each block; never accumulate
  counters, so tempo changes and transport jumps self-correct.
- Emit note on/off at computed sample offsets within the block.
- **A step is scheduled by its fire time, not by its boundary** (fixed 2026-07-27). Swing
  moves the offbeats off their own boundaries, so "the boundary falls in this block" and
  "the note sounds in this block" stop being the same question. The old loop walked the
  boundaries inside the block and gave up on any step whose swing carried it past the end,
  which at a realistic 512-sample buffer silenced most of the offbeats, and it could never
  fire an early one at all, since a step pulled in front of its boundary belongs to the
  block *before* it. The loop starts one step back and tests each step's fire time instead:
  one extra iteration at most, because |swing| < 1 keeps fire times monotonic and each step
  therefore still fires in exactly one block. `tests/ArpTests.cpp` pins both directions,
  including at 512 samples.
- **`active[].samplesLeft` is an offset from the start of the block being processed.** That is
  the whole contract, and it is stated above `retireDue()`. `advanceBlock()` rebases the
  survivors exactly once, at the end of `process()`. Getting this frame wrong shipped every
  note-off one buffer late for the whole of v1 (fixed 2026-07-25).
- **Owed note-offs are closed per hit, not per block**, inside `fireStep()` immediately before
  each note-on: anything already due ends at its own offset, and the pitch being retriggered,
  if still owed past this hit (a tie at gate > 100%), is pulled back to `on - 1`. Draining the
  whole block up front instead is the obvious-looking fix and it is wrong: the tie's note-off
  then lands *after* the note-on that superseded it, giving two note-ons for one pitch with
  nothing between them. `tests/ArpTests.cpp` pins this.
- **Every note is parked in `active[]`, however short.** Emitting a short note's off straight
  into the buffer is the obvious optimisation and it is wrong: it hides the note from the close
  loop, so a same-pitch hit later in the *same* block finds nothing to close and stacks a second
  note-on. It needs a tie plus a buffer long enough for two hits of one pitch — an ordinary 2048
  samples at a fast rate — and the note-off is only correct at some buffer sizes, which is the
  same class of bug as the one being fixed. Nothing is lost by parking: a later hit ends the
  note at its own gate, or pulls it back if it is a tie, and the end-of-block drain does the
  rest. `tests/ArpTests.cpp` pins this with a tie whose two hits share one buffer, which is the
  shape the rest of the suite structurally cannot reach.
- The end-of-block drain sits **outside** the "arp enabled / notes held" guard, so a note owed
  by the last step before the keys came up still ends on time.
- The regression tests cover uneven block sizes, since a host may change `numSamples` between
  calls.
- Track owed note-offs across block boundaries (ratchets, ties, pattern-length
  boundaries); a transport jump mid-ratchet must flush owed offs, never leak them.
- Clock spec follows Serum's documented design: a division list (16 bars .. 1/64,
  default 1/16) with a **separate Dot toggle and Tuplet chip** (kept out of the division
  list so automation stays on even divisions) and an **Anchor toggle**: anchored =
  affixed to the host bar cycle (position may jump on rate change), free = no jump,
  may drift off the bar.
- **A tuplet is N in the space of the power of two below N** (2026-08-03, Owen: *"what if I
  want 1/5 or other division?"*). `arpTuplet` is a choice over Straight / Triplet / 5-tuplet /
  7-tuplet / 9-tuplet, and
  `ArpEngine::tupletFactor` turns it into the one multiplier the engine applies:
  `tupletSpace(N) / N`, so Triplet gives the 2/3 the old Trip toggle hard-coded, 5 gives 4/5, 7
  gives 4/7 and 9 gives 8/9. The convention is what makes the number alone enough to name
  one - five quintuplet 1/16s fill exactly the span four straight 1/16s do - and it is why 5
  is measured against 4 rather than against 6. It is also what lets the readout be a plain
  fraction: see "The readout says what is played" below.
  **Dot is a separate axis, not a sixth entry.** It lengthens a step by half; a tuplet divides
  a span into N. They compose (a dotted 1/16 quintuplet is `9000 * 4/5` samples at 120 bpm),
  and collapsing them into one list would have meant enumerating the product.
  **The even numbers are not in the list on purpose**: 4-in-the-space-of-4 is straight, and
  6-in-the-space-of-4 is a triplet at the next division down, so an int 1..9 would have spent
  half its travel on rates the dial can already reach.
  `arpTrip` is still registered and read by nothing; `KeysProcessor::migrateTuplet` folds it
  into Tuplet 3 on load and returns it to its default.
- **The rate is a dial with two units** (2026-07-30). `arpRateFree` picks between the
  division list and `arpRateHz`, a free-running 0.03125 to 32 Hz, and the panel swaps which
  of the two the dial is attached to rather than formatting anything itself: the parameter
  brings the range, the detents (eleven, one per division, in Sync), the skew and the
  readout text with it. See "The rate dial" below. **The range is not a round number by
  choice.** It is exactly what the eleven divisions span at 120 bpm - "1/64" is 32 steps a
  second and "16 bars" is one step per 32 seconds - so Hz reaches everything the list beside
  it reaches and nothing less. `ArpEngine::minRateHz`/`maxRateHz` hold the two numbers, so
  the engine's clamp and the dial's ends cannot drift apart.
- **Hz is a second timebase, not a relabelling, and there is still one scheduler.** In Hz
  `process()` pins the tempo to 60 bpm, so one "beat" of everything downstream is one second
  and the step is simply the period, `1 / rateHz`. Every quantity measured as a *fraction of
  a step* - swing, gate, ratchet spacing, the Late lane - therefore keeps its meaning with no
  second code path. The two measured in beats outright, **Retrigger Every** and the velocity
  ramp's **Ramp Time**, read as seconds instead, which is the honest reading: there is no bar
  to restart on when nothing is following a transport. The playhead is not read for step
  timing at all, since the bar-affixed branch is taken only on `clock.playing && clock.hasPpq
  && p.anchored && ! p.rateFree` - so Hz sounds the same whether the transport rolls, is
  stopped, or was never there, and Dot, Tuplet and Anchor are all skipped for the one reason
  that there is no beat and no bar grid to apply them to.
- **A change of unit is handled as a transport jump.** Seconds and beats are different
  timelines, so the phase carried across means nothing on the far side and the step in flight
  belongs to a timeline that no longer exists. `process()` watches `rateFree` change, flushes
  everything owed at offset 0, and restarts the clock from zero (`freePhaseBeats`, `stepBase`,
  `havePrevPpq`), so no note-on is left stranded without its off.
- **The Hz clamp is load-bearing for liveness, not only for range.** `stepLengthBeats()`
  returns `1.0 / jlimit(minRateHz, maxRateHz, p.rateHz)`, on the audio thread. At a rate of 0
  the period is +inf, the Late lane's `stepBeats * lane` term goes NaN, every comparison
  against it is false, the computed offset is INT_MIN on every pass and the step loop's
  `offset >= numSamples` break never trips. Mutation-tested on 2026-07-30 by deleting the
  `jlimit`: the test binary hung and had to be killed. A negative rate is harmless by
  comparison, since `stepBeats > 0.0` catches it and simply fires nothing. So the clamp stays
  in the engine rather than being delegated to the parameter's range - the engine takes
  whatever a session, a host automation lane or an MCP client hands it. Nine cases in
  `tests/ArpTests.cpp` pin the timebase, including this one.
- **Transport stopped / standalone:** fall back to an internal clock so the arp keeps
  sounding while auditioning. Cthulhu goes silent with the transport stopped and that is a
  known annoyance. (Decided by Owen, 2026-07-19.) The fallback is the **`bpm`** parameter
  (40..240, default 120) since 2026-07-27; it used to be the host's last-known tempo, which the
  standalone never has and nothing could change. A host that is *playing* still wins. On
  screen it was a labelled slider in the Controls section's band until 2026-08-02, when it
  became **Tempo**, a plain draggable number with `<` `>` steppers on the *Controls bar* itself
  (Owen: "the bpm should live in the controls header. I want it to be like the bpm in ableton,
  just a number") - it stays reachable with that section folded now, where the slider did not.
  **A host that is playing still wins only while Tempo Sync is on** (`bpmSync`, appended
  2026-08-02, default on - Owen: "we need a BPM sync toggle to sync with DAW"). Keys had no
  opt-out from the host's tempo until this parameter existed; on reproduces exactly the
  behaviour above, off pins the arp (and the progression chain, `advanceChainClock`) to the
  Tempo field even while the host rolls, for someone who wants Keys' own clock regardless of
  the DAW's transport. `ArpEngine::Params::followHost` carries it into the engine, replacing
  the bare `clock.playing && clock.bpm > 0` check the branch used to make. A **Sync** chip
  beside Tempo is the on-screen switch; while it is on and a host tempo is actually live
  (`KeysProcessor::hostTempoLive()`), the Tempo field shows the host's own number and greys out,
  since none of the field or its steppers can change anything in that state. Read only in Sync
  mode - the Hz free-rate path below never looked at the host clock to begin with, so the
  toggle changes nothing there.
- Engine is a pure class (`ArpEngine.h`, UI-free, unit-tested like ChordGen):
  inputs = sounding-note set + params + (ppq, bpm, numSamples); output = timestamped
  note events.

## Lanes (the core of "world-class")

Per-parameter step lanes, Cthulhu architecture. Each lane: 1-32 steps, its own
length, plus a per-lane clock divider (1x, 1/2, 1/4 speed) for polymeter, with a
link toggle for the simple case. Shipped as **Link lanes**, and it covers speed as
well as length, since a lane at half speed drifts against the others exactly the way a
lane of a different length does.

**"Cthulhu architecture" means tabs, and v1 got this wrong** (fixed 2026-07-24). Cthulhu
puts its eight graphs in a tab bar and shows exactly one at a time, which is why its
clock divider is documented as acting on "the currently-selected graph" and why it needs
only one of each control. Keys v1 stacked all six lanes instead, and the cost was
structural, not cosmetic: six copies of the length and speed controls crowded onto the
right edge with no room to label any of them, and the panel wanted ~750 px of height
against a 660 px default editor (less again in Keys Host, which spends some on its top
bar), so the Probability lane and the whole pattern row sat below the window edge until
you enlarged it. Lanes are tabs now, with one labelled Steps control, one Speed control,
and the Link lanes switch this spec asked for from the start.

**Shape gates the whole editor** (Serum 2's model). Shape holds the directions (eight in v1,
twelve since the 2026-07-30 round below) plus
"Pattern"; the step editor exists only in "Pattern", and `ArpEngine::Params::usePattern`
carries it into the engine. `laneValue()` is the single place lane data is read, so it is
the single place the gate lives: with it off, every lane reads as `laneDefaults` and the
arp runs as a plain shape while edited step data sits untouched, waiting.

Ranked essential (v1):

| Lane | Range / values | Default |
|------|----------------|---------|
| Note | chord-note index 1..8, or "follow direction mode"; drag below 1 = step mute | follow |
| Octave | -3..+3 | 0 |
| Velocity | 10..200% of the played velocity | 100 |
| Gate | 5%..200%; >=100% into next step = tie | 100% |
| Ratchet | 1..4 sub-hits per step (Stepic's step-divider) | 1 |
| Probability | 0..100% chance the step fires | 100% |

(Probability promoted from v2 to v1 by Owen, 2026-07-19.)

Global (not per-step) in v1: direction mode (up, down, up/down, down/up,
up-and-down, down-and-up, as played, reversed), octave range 1..4 for
directional modes, swing (-0.75..0.75 of a step, applied to the offbeat steps:
positive delays them into a shuffle, negative pulls them early to rush the beat,
and the default 0 is straight), latch (on-screen toggle: ignore note-offs until a
new chord), retrigger (restart at step 1 on new note), rate + dot/trip + anchor.

## The 2026-07-30 expansion (research round two)

A second research pass, over the Ableton Live 12 Arpeggiator and Arpeggiate transformation,
the Kirnu Cream manual, Cthulhu, Scaler 3's Motions and humanize, and the NDLR. Everything
below is additive and defaults to what the arp did before it, so an untouched session sounds
identical.

**Four more shapes**, appended to `arpDirection` (which is a choice parameter: inserting
anywhere but the end renumbers every saved session).

| Shape | What it does | Prior art |
|-------|--------------|-----------|
| Random | a note of the chord at random each step | every arp has one; Keys did not |
| Random Other | random, but never the same note twice running | Ableton's Random Other |
| Random Once | a shuffled order, locked for as long as the chord is held | Ableton's Random Once |
| Chord | *every* note of the chord on every step | Ableton's Chord Trigger |

Chord is the one that changes what the arp is for: with it the arp stops being a run and
becomes a rhythm engine, which is what makes gate, ratchet, probability and swing into a
comping part rather than an ornament. It is a direction only in the sense that it answers
the same question ("which note next"); `fireStep` plays the whole resolved sequence for the
step instead of one entry of it, and a fixed Note-lane index still overrides it, so an
edited pattern does not silently turn into block chords.

**Spread: Repeats + Distance.** `octaveRange` always meant "stack the chord N times", and
the interval it stacked by was hardcoded at twelve semitones. Distance names it, and half its
list counts **scale degrees** instead of semitones, resolved through a 12-bit mask of the
current Root/Scale passed into `Params`. That is the differentiator the original spec called
out and never spent: a Scale 3rd lifts C to E and D to F, where a fixed +4 would give F# and
leave the key. The engine keeps no scale tables of its own - the mask arrives already built,
so `ArpEngine.h` stays dependency-free and a test can state a scale as a number.

**Offset** rotates the lane read index and the direction walk together, so a pattern can be
heard from its third note without being redrawn (Ableton's Pattern Offset).

**Retrigger became a list**: Off / Note / 1 or 2 beats / 1, 2 or 4 bars (Ableton's
Off/Note/Beat). Two consequences worth knowing. The clock windows are what let a five-step
lane still land on the bar. And **the lanes restart now**: retrigger used to reset only the
direction cursor, because lane reads were derived from the absolute step index, so a control
whose tooltip said "restart at step 1" never restarted the steps. Lane reads are relative to
`stepBase` (the step index of the last restart) since this round.

**Octave shift** (`octShift`, -3..+3) transposes the whole run, and is **not** `octaveRange`
beside it, which stacks copies of the chord upward and can only widen. "How high does it sit"
and "how far does it reach" are different questions and only the first one has a middle, which
is why the macro row's centred OCT knob drives this one (2026-08-02, Owen: *"the octave should
start in the middle so you can go up or down"*). It folds into the Octave lane's own per-step
shift rather than overriding it: the knob says where the run sits, the lane says how a
particular step departs from that.

**Volume** (`volume`, 0..100) scales the whole line's output velocity, alongside the Velocity
lane and the ramp in the one `velScale`. The plain level control an arpeggiator wants and this
one never had - with two lines running, balancing them used to mean playing one of them softer.

**Velocity ramp** (Ableton's Decay/Target, restated in beats): over `rampBeats` from the
moment a chord starts, velocity scales toward (100 + `velRamp`)%. It rides on `heldBeats`,
which counts only while something is held and resets with each fresh chord, and it is
sampled once per block - the shortest useful ramp is a bar and the longest block is a few
milliseconds, so the stair is far under the 1/127 velocity is quantized to anyway.

**Humanize** is the first thing in Keys to touch the arp's feel at all: `Humanize` proper
lives in `KeysProcessor::noteOn`, which the arp's own notes never pass through, so arp steps
have always been exactly on the grid. One control nudges each hit late (up to 25 ms) and
takes up to 30% off its velocity. Late-only and quieter-only, on purpose: a nudge that can
also rush is what Swing is for, and a velocity that can also rise makes an edited Velocity
lane mean less than it says. **The nudge is clamped to 40% of the gap to the next sub-hit**,
because unbounded it can carry one ratchet sub-hit past the next, and two hits of one pitch
arriving out of order is the one thing `emitHit`'s close-what-you-land-on rule cannot
survive. At 0 the engine draws no random numbers at all, which is also what keeps the older
tests deterministic.

### Four more lanes (the same round)

Six lanes became ten. They are appended for the same reason the shapes are: a slot's lane
data is serialized by lane index, so inserting would reinterpret every pattern in every
saved session.

| Lane | Range | Notes |
|------|-------|-------|
| Transpose | -7..+7 | **Scale degrees.** Everyone else's transpose lane is chromatic, which makes it a machine for leaving the key; the degree version reuses the same `shiftByDegrees` + scale mask the Distance control brought in, and a chromatic mask makes it chromatic again for free. |
| Late | 0..90% | Per-step delay, Cthulhu's lane and Kirnu's Shift. **Late only.** An early half would mean two steps could swap order, and out-of-order hits of one pitch are the one thing `emitHit`'s close-what-you-land-on rule cannot survive. Swing already offers early. |
| Harmony | 0..7 | A second voice that many *sequence entries* above the note played, so it stays inside the chord; running off the top adds an octave rather than folding onto a note already sounding. Cthulhu's harmony. |
| Chord | 0..12 | The step plays the chord in that arp **slot** instead of a note of the held set. Kirnu Cream's Chordmem, except the memories are the twelve slots Keys already has, so a progression can be drawn into a lane without a second copy of it. |

The Late lane is why the step loop now looks **three** steps back rather than one. One
sufficed while swing was the only thing that moved a step (|swing| < 1, so a step could
only ever be pulled into the block before its own); a step can now fire up to 1.65 steps
after its boundary (0.9 late plus 0.75 swing), and the loop has to still be walking it.
The cost is two extra iterations a block that skip on a negative offset.

The Chord lane is the only part of the engine that reads anything outside itself. Slot
chords live on the message thread in `std::vector`s, so `KeysProcessor` keeps an
`ArpEngine::ChordTable` mirror of atomics beside them and `syncArpChordTable()` rebuilds it
whole from every path that can change a slot's chord. There is no single choke point for
those writes, so **the call sites are the contract** (set, clear, copy, whole-slot write,
session load). The count is stored last and with release ordering, so a half-written chord
is never reachable: the notes are in place before the count that admits them.

Found while adding them: `ArpPattern` zero-initialized its lane arrays, so a slot nobody had
ever stored recalled as *every lane at zero* - velocity 0 clamps to a near-silent 0.05 and
gate 0 to 5%, so launching an untouched slot made the arp whisper rather than doing nothing.
It fills from `laneDefaults` now.

### Progression mode: the chain

**Chain** walks the slots that hold a chord, giving each the number of bars its card shows,
and launches each in turn. One click plays the row as a twelve-chord song, which is what a
row of cards showing chord names has looked like it should do since the slots stopped being
eight lettered buttons. It is **per line**: each of the three has its own chain over its own
twelve slots, its own bar count and its own position, so three progressions can run against
each other at three rates. The button starts the chain of whichever line's detail view is open
- reached by that card's **Details** button now, where a bar tab used to point at it. `Bars`
(1..16, on the action row) edits the **active** slot - the one
whose lanes the editor is showing - which a click on a card already makes it, so setting a
length is click the card, click the plus. A card shows `x2` and up; twelve cards each saying
`x1` would be twelve pieces of noise for the one case where the answer does not matter.

Slots with no chord are skipped. A pattern-only slot is a place to keep a rhythm, not a step
of a progression, and walking through one would leave the previous chord ringing under a
pattern that says nothing about it.

**The clock is split across two threads on purpose.** Counting bars belongs on the audio
thread, the only place with a tempo (and the only place that can ask the host for a time
signature - a bar is four beats only in four-four). Launching a slot cannot happen there: it
moves host parameters and fires notes. So `advanceChainClock()` accumulates beats and raises
one atomic flag, and the heartbeat below acts on it. An epoch counter runs the other way, so
a launch tells the audio thread to count the new slot from zero rather than from wherever the
old one ended, and the chain never drifts.

### The heartbeat

`KeysProcessor` runs a second timer at 50 Hz, separate from the 1 ms strum scheduler (which
stops itself the moment nothing is queued). It exists because two things need a pulse that
outlives the editor:

- The chain, above.
- **Releasing a chord held into the arp when the arp is switched off.** This check used to
  live in the editor's timer with two holes it could not close: it was gated on the chord
  having come from a *pad*, so a chord handed over from the live card was never released, and
  with no window open nothing polled at all - so a host or an MCP client writing `arpOn`
  false left the chord droning with no click left to stop it. Both close here, because the
  processor owns the chord and runs whether or not anyone is looking. A chord an arp *slot*
  launched is still left alone on purpose: its lit card is on screen and still releases it.

50 Hz is not a note clock. It only decides how late a chord change may be - 20 ms, comfortably
inside a 1/16 step at any tempo anyone plays at, and the arp itself stays anchored to the bar
grid regardless.

### A trap this round walked into

`ArpPattern::shape` is a direction index where `numDirections` *itself* means "Pattern" - so
the number that means Pattern moves every time a shape is added, and the four new shapes
silently turned every stored Pattern slot into Random. Sessions now record `shapeBase` (what
Pattern was numbered when they were written) and `arpFromTree` remaps it. Anything else that
stores an enum whose end is a sentinel needs the same treatment.

What is left of the v2 list, and what became of the rest, is the table below.

**Gate and Chance are global as well as per-step** (added 2026-07-25). The lanes are gated
behind Shape being "Pattern", so on a plain shape there was no way to shorten a note or
thin a run out at all - the two most reached-for arp controls on any hardware unit were
unreachable on the default shape. `Params::gate` and `Params::chance` **multiply** the lane
value in `fireStep`, so 100 leaves an edited pattern exactly as drawn, and each control
means the same thing in both shapes.

v2 and later (nice-to-have per the research ranking), with what became of each:

| Wanted | Status |
|--------|--------|
| probability / random-select lane | shipped in v1 (Owen promoted it) |
| timing-offset (Late) lane | shipped 2026-07-30 |
| harmony lane (second note within ±1 octave) | shipped 2026-07-30, counted in chord tones rather than octaves |
| semitone pitch lane gated by scale-degree enables, with root detection | shipped 2026-07-30 as the Transpose lane, which counts scale degrees outright - the gating was a way to keep a chromatic lane in key, and counting degrees is that idea without the gate |
| pattern chaining | shipped 2026-07-30 as **Chain**, over the slots rather than over patterns |
| per-pitch-class block/redirect keyboard | not built |
| chord mode (inversion stacking) | not built. The Chord *shape* plays the held chord every step, which is the rhythmic half of this; stacking inversions is still open |
| per-step CC lanes | not built |
| arp-on-note-count | not built |

## The generative round (2026-08-14)

A third research round, sourced from hardware rather than the v2 ranking: the Moog
Subharmonicon (its manual sits in the repo root), whose whole design is division - pitch
from the undertone series, rhythm from OR-ed clock dividers. Three additions, engine and
MCP first; the UI pass landed after, in `src/ui/ArpPanel.{h,cpp}`, following the mouse-only
contract like everything else.

Euclid and Clocks each open as a strip above the action row - the same "height is the cheap
axis" rule the macro view's own growth already follows, applied here for the first time to a
per-line control rather than a whole view: opening one closes the other, so the panel never
grows by more than one strip's height at once, and the strip's own steppers are laid out
regardless of which is open (the closed one just gets a zero-width row, which is harmless
since its components are invisible too). Euclid is gated to Pattern shape, since it writes
into a lane that only exists there, and closes its own strip if Shape leaves Pattern out from
under it; Clocks stays open in every shape, since the dividers act regardless of what Shape
draws. Voice, in a third row of the STEPS group below Steps and Speed/Link, is the panel's
first **lane-contextual** control - visible only with the Harmony lane itself selected, which
needed a visibility rule of its own (`refreshShape()` and `selectLane()` both gate it,
independently, since a lane click cannot change Shape and a Shape change cannot change the
lane) rather than reusing an existing one. Fitting Voice into that group's already-tight width
at the editor's 1280 px floor was the one real layout squeeze in this pass: the STEPS group
grows a third row for it (Pattern shape only) rather than shrinking Link below its own
working width, the same "grow height, not width" call the two strips make.

- **Euclidean generator** (`EuclidGen.h`, `apply_euclid` over MCP,
  `applyEuclidToActiveArpPattern` on the processor). Writes hit/rest into the
  **probability lane** - 100 on a hit, 0 on a rest - and sets the lane's length. Writing
  probability rather than muting notes keeps the melodic content intact, and the global
  `chance` still multiplies the result, so one continuous controller can thin a Euclidean
  pattern the same way it thins anything else. Scoped to the probability lane on purpose:
  no other lane has a meaning for "off" that a generator should guess at.

- **Rhythm dividers** (`ArpEngine::rhythmDiv`, four per line, 1..16, 0 = off, stored with
  the slot). The Subharmonicon's rhythm-generator behaviour: with any divider enabled, a
  step boundary fires only when its index is a multiple of **at least one** of them, so
  {3, 4} fires on 0, 3, 4, 6, 8, 9, 12 - an OR of clocks, not a shared modulus. A
  suppressed boundary fires nothing and advances nothing. The position a firing boundary
  reads its lanes by is `firedCountBefore(g)`, computed from the global step alone by
  inclusion-exclusion over the enabled divisors - stateless the same way the rest of the
  clock is, so transport jumps self-correct and two takes match. All zeros is bit-identical
  to the engine before the feature existed, and a test holds that equality. Note the
  difference from the per-lane `clockDiv`: that slows a lane's *read* while every step
  still fires; this decides *whether a step fires at all*.

- **Subharmonic harmony mode** (`ArpEngine::harmonyMode`, stored with the slot, default 0).
  Mode 1 draws the Harmony lane's second voice from the undertone series **below** the
  played note - f/2 to f/8 quantized to 12-TET, `{-12, -19, -24, -28, -31, -34, -36}` -
  instead of chord tones above it. It deliberately leaves the chord, so it is meant to be
  heard with Scale Lock off (Lock upstream will re-quantize it). A voice that clamps onto
  the note it was meant to harmonize is dropped, not wrapped: a wrapped low note reads as a
  new attack rather than a silence.

All three serialize append-only on the `"pattern"` node (`rhythmDivs`, `harmonyMode`) with
absence reading back as off, per the standing rule that old sessions must not be able to
tell a new feature exists.

## Scale awareness

Keys already owns Root/Scale. The arp editor flags out-of-key results visually
(Stepic's red-flag convention) and, because Keys has Scale Lock upstream, the arp
output can never leave the scale when Lock is on. This is a differentiator the
stock arps lack; it comes almost free here.

## Patterns, which became slots

Originally 8 lettered patterns (A-H) per session. **Twelve launchable slots since
2026-07-25**, at Owen's request, after a reference layout where the pattern memories are
cards you fire rather than letters you recall.

There are twelve **per line** since 2026-08-01, so thirty-six in a session. A slot belongs to
one arpeggiator, which is what lets the single row on screen be whichever line's detail view is
open rather than a shared pool the lines fight over.

A slot carries its lane data *and* a chord, a shape and a rate - the rate meaning the
division, the unit it was captured in and, when that unit was Hz, the frequency. Launching it
(one left click, anywhere on the card) installs the pattern, moves the Shape, Rate and rate-mode
parameters through the host the way the combo boxes do, and holds the chord into the arp. Clicking the
launched slot again releases it (`ArpPanel::launchSlot` still toggles per slot, which is what
makes a slot card a launcher rather than a pad). **Stop** does the same without reaching for
All Off, and it goes through `releaseArpHold()`, so it also stops the Chain: it is the same
button as **Hold off** on the section bar and has to behave like it. A slot with no chord
launches the pattern alone and arpeggiates whatever is already sounding.

The card paints what it will play - chord name, shape, rate - so a row of twelve reads as a
progression. Two lit states, deliberately distinct: *active* (a soft ring) means these are
the lanes the step editor is editing; *launched* (a bright ring and a lit triangle) means
this slot's chord is what the arp is chewing on. They are different things and are often
true of different slots.

The slot row is on screen in **both** shapes. Hiding it outside Pattern was what made the
old A-H buttons read as an appendix to the step editor rather than as the way you drive the
arp.

On-screen **Copy**, **Clear**, **Cancel** and **Randomize** (Cthulhu hides copy behind
alt-drag; that is a modifier gesture and is banned here). Copy and Clear arm and then take
a slot click, which keeps both on a pure left-click path. A slot's right-click menu offers
Launch, Clear chord, Copy and Randomize, as an accelerator only; Randomize is greyed there
outside Pattern shape, because that is where its button lives.

Slots persist in the session state (a `ValueTree` next to the chord pads) and are
MCP-addressable. Slots 9-12 read as empty in a session saved with eight.

## Chords into the arp

The engine has always taken its input from the block's merged MIDI stream, so a chord pad
already fed the arp - but the arp was a centre view and picking it put the pads away, and a
pad was momentary anyway. Paths that close that were added from 2026-07-25 on; what feeds a
line today is a **drag**, and that took the last of them off the left click 2026-08-02.

- **A drag onto a line's card in the macro view, its tab on the Arp bar, or a slot.** Each
  calls `KeysProcessor::holdArpChordFromPad` (through `ArpPanel::takeChordOnLine` for the
  first two), which emits the note-ons and never the note-offs until the next call.
  `holdArpChordFromPad` goes through `holdArpChord`, which releases the previous hold first
  (`releaseNotes` on `arpChordTag`, so the refcounts and the arp's held set both unwind) and
  then fires, applying Exclusive to the new one - so dropping the same card on a line it is
  already feeding is a restrike and never a second owner of the same pitches.

  **A click never feeds a line any more** (2026-08-02, Owen: "when an arpeggiator's
  running and you click on a pad, I don't want it to send it to the arpeggiator unless you
  drag it"). Until that day a plain click, while any line was on, handed the card to the
  **current** line the same way a drag does now, and a second click on the card already
  feeding it was the restrike described above; `ChordPads::mouseUp` dropped that branch
  entirely; a click now auditions the pad exactly as it would with every line off; see **A
  chord card sounds on release, never on press** in `CLAUDE.md` for the audition side of the
  same change. One click-only case survives, because a drag cannot reach it: a card that was
  **cleared** while still feeding a line wears the ring with no notes behind it, there is
  nothing left to audition, so a click on it calls `releaseArpChord()` instead - that is the
  ring's own way out, and the reason a cleared card is still drawn with one.

  Stopping a *filled* card's hold outright is **Hold off** on the arp bar (below).

  **Handing a chord over used to need arming.** It was its own **To Arp** toggle on the Pads
  bar until 2026-07-27,
  on the reasoning that a pad must not quietly do something different than it did a minute
  ago. In practice the separate arming read as a button that did nothing: with the arp off,
  handing it a chord looks exactly like sustaining one, and that is the state the toggle was
  most often found in. Owen had it removed, and the arp's own **On** is the mode now
  (`KeysProcessor::cardsFeedArp`, which every surface showing a chord card asks - a rule with
  only one surface left to obey it since the generator's duplicate grid went on 2026-07-30).
  Switching the arp off releases a chord a card was holding, or it would drone with nothing
  arpeggiating it and no click left to release it.

  **That release used to live in the editor's timer, and missed two edges** (found in the
  2026-07-27 sweep, closed 2026-07-30). It was gated on `arpHeldPad() >= 0`, so a chord
  handed over from the *live* card, which leaves `arpPadSlot` at -1, was never released. And
  with no editor open there was nothing polling at all, so host automation or an MCP client
  writing `arpOn` false left the chord sounding. That was the hazard the old To Arp flag was
  put on the processor to avoid, in a new place: a chord held into the arp outlives the
  window, so what releases it has to as well. It is on the processor's heartbeat now (see
  **The heartbeat**, below), which is also what the chain needed. A chord an arp *slot*
  launched is still left alone deliberately: its lit card is on screen and still releases it.
- **Send to arp slot**, in the pad's card menu, which copies the chord into a slot for
  later. A copy and not a reference, so regenerating the pad page cannot silently rewrite
  what a slot plays. It is the aimless twin of dragging a card onto a slot directly - no
  target to drag onto, so it goes to the slot machinery without one.
- **Dragging a chord card onto a slot card** (2026-08-01), which is the same thing with a
  target picker instead of a submenu, and is what retired that menu item's status as a
  right-click-only path. **Dragging onto a line's tab, or its card in the macro view**, hands
  the chord to that line there and then, without going through a slot at all.

Every one of these names a line. The two menu items go to the **current** line
(`arpCurrentLine` - it left the Pads bar with the letter chip 2026-08-02, and since the second
2026-08-02 pass it is shown by the per-line panel's own **LINE A** / **LINE B** caption rather
than the arp bar, now that A and B read as On/Off instead of a selection); a drag goes to the
line whose tab, macro card or slot it landed on, and makes that line current, because you
aimed at it.

The held chord is tagged `arpChordTag - line` (-3, -4, -5) so it never collides with pad or
live-card scheduling *or with another line's hold* - each is released independently, and
`cancelScheduledNotes` matches the exact tag, so one shared tag would have letting go of B
drop A's un-fired strum notes. `allNotesOff()` forgets all three - otherwise a panic silences
the chord while the launched slot still paints as playing.

### Hold off, and `releaseArpHold()`

Held means held: nothing follows the note-ons until something replaces them. With the card
click turned into a retrigger, the way to stop a hold outright is a control of its own, so
**Hold off** is a 24 px chip on the **arp bar**, next to the line switches (2026-07-30). It is
on the bar and not in the panel for the same reason those are: a chord can be held into a
folded arp, and the only exit from it cannot be inside the section that is folded away.

**It releases every line, and stops every chain.** With more than one line that is a decision rather
than an accident: a Hold off that let go of only the line whose panel happened to be open would
leave the other one droning, and the arp bar's A/B switches say nothing about which panel that
is any more - they read On/Off, not a selection. One button, one meaning. The editor's timer enables it whenever *any*
line has something to let go of (`processor.anyArpHold()`, plus a launched slot on any line)
and greys it otherwise, so it never reads as a dead target. Its accessible name is "Arp hold off", because "Hold off"
alone says nothing to a script driving the plugin through UI Automation.

Both it and the panel's **Stop** button call one new processor method:

```cpp
void KeysProcessor::releaseArpHold()   // stopChain(), then releaseArpChord()
```

**The order and the pairing are the point.** `releaseArpChord()` alone is not "stop": with
the Chain running it drops the chord and wins only until the next bar boundary, when
`heartbeatTick()` launches the following slot and hands the arp another one. So the chain
goes first. `stopChain()` is a no-op when nothing is chaining and releases the chord it was
holding when it is, which leaves `releaseArpChord()` idempotent after it, and picking up
whatever a card or a lone slot launch left behind. Anything that means "let go of the arp's
chord, whatever put it there" should call `releaseArpHold()` and not either half.

It is deliberately *not* All Off: the arp keeps running and goes back to arpeggiating
whatever you play, which is the difference between letting go of a chord and panicking.
`ArpPanel::launchSlot` is also unchanged and still **toggles per slot**: clicking the slot
card that is already launched calls `stopArpSlot()`. A slot card is a launcher with a lit
state, so one control starting and stopping it is what its ring already promises; a chord
pad is a pad, and pads re-fire.

## Mouse-only interaction (the part nobody else got right)

Verified: Serum's *feel* is the model, its *gestures* are not. Serum's editor
depends on double-click, shift-click, alt-drag, and right-click menus; Cthulhu
hides position reset behind alt-click and pattern copy behind alt-drag. All banned
by the Keys contract. What transfers is the feel: continuous single-drag painting
with immediate feedback, live value readouts under the cursor, no commit step, and
on-screen affordances instead of keyboard modifiers ("the arrow simply saves you
from having to touch the computer keyboard", Serum manual, on its drag arrow).

Concrete remaps:

- Lane editing: plain left-drag paints values across steps in one gesture; a value
  readout follows the cursor. Click sets a single step.
- Step mute: a dedicated mute-button row under the lane (>=34 px), not a hidden
  drag zone (Cthulhu's drag-below-1 stays as an accelerator, never required).
- Pattern operations: Copy/Clear/Randomize as buttons, arming and then taking a slot
  click where they need a target. (Shipped without a Paste: an arm-then-pick Copy is the
  same two clicks and needs no clipboard to explain.)
- Grid snap and edit modes: on-screen toggles, never modifier keys.
- Lane length: - / + buttons either side of a readout, in the STEPS group. (Shipped
  without the right-edge drag handle this spec also asked for.)

## UI placement (decided: a section of its own)

The arp is a foldable **section**, between the Controls section and the chord pads, with its
line switches (**A**, **B** on screen - a single **On** until 2026-08-01, three lettered
switches for a day after that, down to two when line C left the UI 2026-08-02), a **Hold off**
chip, **All Off**, **Light keys**, **Launch Quantize** and a **Detach** button on its own bar.
Folding the section destroys the editor, never the arpeggiators, which is why the switches live
on the bar rather than inside the panel, and why they, Hold off and Quantize stay put when
their section folds. They are not alone in that any more: the theme swatch, Tempo, Sync, Root,
Scale, Scale Lock, Voices, MIDI Ch and the Instrument chip on the Controls bar, and Fill,
Regen, Generator, Key and Humanize on the Pads bar, all outlive their fold for the same kind of
reason. **Mode**, **Scale Compliance** and the arp's old target-line letter chip left the Pads
bar 2026-08-02 for the generator's window and the arp bar's own A/B tabs respectively, and the
tabs themselves changed jobs the same day, seventh pass: they used to be the one addition to
this bar that did **not** outlive the fold, since all they did was say which line the (then
hidden) panel below was showing. Owen called that redundant with the separate lettered On chip
sitting a few pixels away ("we can remove the a and b check mark on the right side of the
header") - that chip is gone, the tabs are the switch it used to be now, and so they outlive
the fold too. **All** is the one navigation control left on this bar, and it alone still hides
with the fold. What hides with a fold is what would be a control with nothing behind it - the
pad pages, Wheels, and All. The Knobs chip that used to hide with the Controls fold is deleted
outright (2026-08-02): the row it hid is unconditional now, so there is nothing left of it to
hide. Detach hides with its own section too.
Detach moves the whole panel into a resizable window (`DetachedWindow`, shared with every
other section since 2026-07-27); a detached section takes no height in the main window, and
the Re-dock button travels into the window with it.

**Only the left end of the bar folds it** (2026-07-30). `SectionBar::hitTest` narrows the
button to `foldZone()`, the chevron and the caption, about 92 px at the narrowest caption,
with a painted hairline marking where the target ends. For three days the whole strip folded,
and on this bar in particular that was expensive: a click aimed at a line switch or Hold off
that missed by a few pixels hid the arp instead. See `docs/ARCHITECTURE.md`, **Folding layout**. Folding
is still one click and still leaves the arpeggiator running behind a single dim strip.

It got there in three steps, all at Owen's request. A full-editor overlay first, changed to
a centre view on 2026-07-25 because the overlay dimmed and covered the keyboard you are
meant to be playing while you edit. Then a section later the same day, because as one of
three centre views it competed with the knobs and the generator: picking the arp put away
the chord cards it exists to chew on. There is no centre view at all now: it went on
2026-07-30 along with the chord generator's panel, leaving four sections stacked (Controls,
Arp, Pads, Keyboard).

**The chord pads are their own section** (2026-07-25), directly below the arp, so a chord
card is on screen whatever else is open. This is the layout change the whole "cards into the
arp" feature rests on: the arpeggiator's job is to chew on a chord, and it was the one view
from which you could not reach one.

## Control band layout

Three ruled, captioned groups, after the hardware-arp arrangement Owen asked for:

| Group | Holds | Visible |
|-------|-------|---------|
| PATTERN  | Rate (dial, spans both rows), Shape + `<` `>`, Rate `<` `>`, Sync/Hz, Tuplet, Dot | always |
| PLAYBACK | Swing, Gate, Chance (knobs), Retrigger, Play, Latch, Anchor | always |
| STEPS    | Steps, Speed, Link | Pattern shape only |
| SPREAD   | Repeats, Distance, Offset | always |
| FEEL     | Ramp, Time, Human Time, Human Vel | always (the band only; the macro cards carry Vel and the two Human knobs) |

The last two are a **second band row**, added 2026-07-30 with the controls above. It is one
control row tall where the first band is two, which is what kept eight new controls to 64 px
of a panel that is already the tallest thing in the editor: a knob column spans both rows of
a group, so FEEL uses horizontal sliders instead. Anchor moved down beside Latch in the same
change - Retrigger grew from a toggle into a list, and at 128 px next to Anchor's 83 the
PLAYBACK group ran over and ellipsised the *toggle*, which is the one thing on the band with
no width to lose. Two later arrivals paid for themselves the same way: **Play** (`arpKeys`,
which came down from the macro rows on 2026-08-02) is reserved off Retrigger's right end
*first*, because Retrigger is the elastic one and an elastic control with a floor must never
be asked to leave room for anything; and **Human Vel**, the velocity half of the Humanize
split the same day, cost FEEL four points of group weight from SPREAD, which still fits its
three cells exactly at the editor's minimum width.

The `<` `>` pairs matter more than they look: stepping to the next shape is the commonest
thing you do to an arp and it used to cost a click, a travel down a menu and a second click.
They **clamp** rather than wrap, so one click too many on "Up" cannot land on "Pattern" and
throw the step editor open under you.

### The rate dial

Rate stopped being a combo box on 2026-07-30, when the Hz mode arrived: 0.03125 to 32 Hz is a
continuum, and a list of it would be either a menu of guesses or a number to type. It is the
kit's rotary (`okstudio/RotaryKnob.h`, the same one `KnobBank` uses), in a knob column that
spans both rows of the PATTERN group, and it costs the panel no height at all.

What carries it:

- **Two attachments, one alive.** `refreshRateMode()` destroys one and builds the other on a
  mode change, so the dial follows `arpRate` in Sync and `arpRateHz` in Hz. That is what makes
  the range, the interval (eleven detents in Sync, so it cannot land between two divisions),
  the skew and the readout text all come from the parameter: the panel formats nothing, and a
  change from any source (a host, the MCP bridge, a slot launch) shows on the dial. The mode
  itself is derived from `arpRateFree` on the 10 Hz timer as well as from the button, the same
  way Shape is, because a host can automate it.
- **The Sync / Hz chip** beside it reads the unit that is live, not the one a click would
  pick, and lights in Hz. A dial position means two different things in the two modes, so the
  readout under the dial ("1/8" against "4.00 Hz") says it a second time in its own units.
- **The readout says what is played, not what the parameter holds** (2026-08-03, Owen: *"when
  triplet mode is enabled the division text should reflect"*). In Sync it is
  `ArpEngine::rateSyncText`, **the step length as an exact fraction of a bar**: `1/8` straight,
  `1/12` in threes, `1/10` in fives, `1/5` for a quarter in fives, `1/8.` dotted, `1/10.` for
  both. Straight, it reproduces the division names byte for byte, so it is not a second copy of
  the rate list that can drift from it. The attachment's own text function is the bare division
  (it comes from the choice parameter, which knows nothing about the two modifiers), and
  `SliderParameterAttachment` writes `textFromValueFunction` in its constructor - so
  `installRateText()` has to run *after* every attachment swap, not once at construction. In Hz
  nothing is added: the engine ignores both modifiers there, so "4.00 Hz" is already the whole
  truth.
- **Why a fraction and not `1/16T`.** The universal DAW convention - Reaper, Serum, Bitwig,
  Cubase, Studio One - is a note value plus a letter, `1/16T` for triplets and `1/16D` or a dot
  for dotted. It has **no form at all for a quintuplet**, which is why the first cut of this
  invented `1/4:5` and Owen bounced it (*"fraction confusing too. shouldn't it just be 1/5 not
  1/4:5?"*). The fraction needs no letters, because the arithmetic already names it: a
  quarter-note quintuplet is five in the space of four quarters, four fifths of a beat, one
  fifth of a bar. So it is "1/5". FL Studio's grid ("1/3 beat", "1/6 beat") is the same system,
  and 4/4 is assumed here exactly as much as it already was - "1/4" has always meant a quarter
  of a bar in this list. **Dot keeps its dot** rather than folding in: a dotted 1/8 is 3/16 of
  a bar, and "1/8." is read instantly where "3/16" has to be worked out. The tuplet folds
  because it has no such symbol to keep.
- **The `<` `>` pair is not a convenience here, it is the contract.** A dial is a *drag*
  target and drag precision is the hardest thing for this instrument's owner, so the steppers
  are the click-only path to every value the dial can hold, in both units. In Sync a click is
  one division. In Hz it is a quarter of an octave on a ladder anchored at 1 Hz, so four
  clicks halve or double the rate (the same jump one entry of the Sync list makes, four times
  finer), both ends of the range and every power of two are rungs, and repeated clicks always
  land on the same forty values. Those two and the chip are laid out at 34 px tall rather than
  the band's 28 for the same reason.
- **Dot, Tuplet and Anchor grey out in Hz**, because the engine ignores all three there. Dot
  and Tuplet subdivide a beat and there is no beat, so a dotted 8 Hz would only make the number
  on the dial a lie; Anchor is skipped by `process()`'s `&& ! p.rateFree`, since a
  free-running rate has no bar grid to affix itself to. A control that does nothing greys out
  rather than sitting lit.
- **A range knob is Serum's mod ring, read out of the manual rather than off the picture**
  (2026-08-03, Owen: *"a serum style knob where you can set a range in the knob. In serum they
  have like a little light next to it that sets the range"*, then *"when the outer ring is
  enabled, moving the dial moves the outer ring with it"*). The face sets one end, the span
  reaches back from it, and the whole range **travels with the face**.
  **The knob's own arc is the range, and there is no second ring** (*"it looks like there's two
  rings around the knob ... everything should be reflected on that single ring"*). A concentric
  ring outside the face was built first and was one too many. What replaced it is one line in
  the skin: `KeysLookAndFeel::drawRotarySlider` already works out where a lit arc starts, so a
  slider can override that proportion through `skin::arcFromProperty`, and `RangeKnob` sets it
  to the range's bottom. Nothing is subclassed and no copy of the knob's look is kept in step.
  Masking it afterwards was tried and fails: Keys draws a value arc as **three** strokes - a
  halo at 2.1x the line width, a body at 1.15, a hot core at 0.55 - so a mask sized to the line
  leaves the halo showing (*"a shadow of blue on the inner ring that isn't just the range"*).
  The manual is worth quoting, because a first cut read the screenshot as a dot on the ring and
  was wrong (`Serum 2 User Guide.pdf` p195): *"A smaller blue halo appears to the top left of
  the knob. Hovering over this small halo displays an Up/Down arrow control. Click and drag the
  arrow control to change the modulation depth amount. As you drag the arrow, notice how the
  halo shrinks or expands to show the range of modulation."* A **satellite at the top left,
  dragged vertically** - which is the detail that makes it buildable under a 34 px floor, since
  a satellite is a component of its own and can be as big as it needs to be, in the corner a
  circle leaves empty in a rectangle. It is a child *above* the face in z-order: a Slider takes
  every press inside its rectangle, corners included, so anything merely drawn there is dead.
  It sits **outside the ring, not on it**, at about a quarter of the face's size, joined back
  by a hairline stem - Serum's proportions, and two builds' worth of getting it wrong (Owen:
  "the satellite should not be on the wheel"). Placed toward the dial box's *corner* rather
  than along a 45 degree line, since the box is wider than it is tall; the distance clears the
  ring's stroke and is clamped so a narrow column never pushes the dot out of its own cell.
  Drawn small, hit large: the component is 8 px bigger than the dot, and that padding only ever
  overlaps the ring, where a press does this same job anyway.
  **Two departures, both forced.** Serum's fallback for the fiddly satellite is
  Option/Alt-click-drag on the knob body; a modifier is not a gesture Keys may require, so the
  fallback here is that the whole margin around the face drags the span too - every pixel the
  face does not cover, corners included. The satellite is the affordance, the margin is the
  forgiveness; it is a plain lit LED, since a mini-arc on it was the span drawn twice and an
  outline with a pip read as a tiny knob. And
  there is no negative span: Serum flips the halo's hue for an inverted depth, but a range has
  nothing to invert into, so `Direction` picks which side of the value it reaches instead.
  The component owns no parameter: the span comes in through `setSpan()` and goes out through
  three callbacks, so the consumer keeps the parameter and the gesture brackets. In Keys those
  are `arpHumanizeSpan` and `arpHumanVelSpan`, default 100 - a span of the whole scale, which
  puts the floor at zero wherever the knob sits and is what Humanize did before it had a ring.
  The engine, not the layout, clamps the floor to its own ceiling, since either can be
  automated past the other.
- **Tuplet is a combo box, and was briefly not.** For one build it was a `ToggleButton` that
  cycled its own text through five values, and Owen's reply was *"confusing UI. it's a check
  box but it changes"* - a check box is a promise of two states, and a control whose shape
  lies about its own behaviour costs more than the pixels it saves. A combo is what Keys
  already means by "pick from a list" (Shape, Distance, Retrigger all are one), so it needs no
  explaining, and it takes an ordinary `ComboBoxAttachment` where a button could not bind a
  choice parameter at all. The entries **name themselves** - "Triplet", not "3" - because the
  macro sub-row is a single 34 px strip with no caption anywhere on it; the band's copy is
  captioned as well, since it sits in a group where Shape above it is. `refreshTuplet()`
  survives only for the readout, which is a function of three parameters where an attachment
  binds one.
- **Folding the tuplets into the rate list is the other design, and it is not built.** The dial
  would walk 1/4, 1/5, 1/6, 1/8, 1/10, 1/12 as one ordered list and the second control would
  disappear entirely - the cleanest reading of *"shouldn't it just be 1/5"*, and the readout
  notation above is already exactly that list. What stops it is the click-only path: the list
  runs to about two dozen entries, so 1/4 to 1/64 goes from four stepper clicks to fifteen, on
  the one control whose `< >` pair exists precisely because the dial cannot be trusted to a
  drag. It would also need a new rate parameter, since `arpRate`'s choice list cannot be
  reordered or inserted into, plus a migration for `ArpPattern::rate`'s stored indices. If it
  is ever wanted, that is the shape: a new appended parameter, `arpRate` and `arpTuplet` both
  retired into it the way `arpTrip` was retired here.
- **The Hz mapping is exponential, not skewed.** `value = lo * (hi/lo)^t`, written out as the
  parameter's two conversion functions, so each of the ten octaves gets a tenth of the travel
  and one degree of the dial is the same *ratio* at either end. `setSkewForCentre(1.0f)` was
  tried first and is a power law, which is a different curve: its exponent works out at ~0.198
  on these ends, which spent 25.3% of the dial between 0.03125 and 0.0625 Hz against 12.9%
  between 16 and 32. 1 Hz still lands at the centre, now as a consequence.
- **Swapping the two attachments waits out a drag.** `SliderParameterAttachment` opens a
  parameter gesture on `sliderDragStarted` and closes it on `sliderDragEnded`, and its
  destructor only removes the listener. `refreshRateMode()` runs off the 10 Hz timer, so a
  Chain launching slots on bar lines (or a host, or an MCP client) could otherwise destroy the
  live attachment mid-drag and strand a begin with no end. The panel holds the swap while
  `rateKnob` is down and `onDragEnd` applies it on the mouse-up.
- **A slot carries the unit, not just the number.** `ArpPattern` stores `rateFree` and
  `rateHz` beside `rate`, or launching a slot captured in Hz would silently drop you back into
  Sync. Only a slot captured in Hz writes its Hz value on the way out: `arpFromTree`
  synthesises 8.0 for every slot in a session saved before the mode existed, so writing it
  unconditionally meant opening an old session, dialling 0.5 Hz and clicking any slot at all
  reset the rate to a fabricated 8. A Sync slot leaves the Hz control exactly where it found
  it.
- **`migrateRateMode()` brings an old session back to Sync.** An absent parameter is not a
  reset: APVTS creates the adapter's child on the spot and flushes the *current* value into
  it, which is the default on a fresh instance and whatever you were last playing with on a
  live one. So recalling a pre-Hz preset while the dial sat in Hz restored and displayed
  `arpRate` while the engine carried on free-running at the Hz value from before the load.
  The tell is the absence and the repair is to write the missing parameter's
  `getDefaultValue()` explicitly - the same shape as `migrateStrumRange`, and checked for each
  of the two independently, since a tree carrying one and not the other is malformed rather
  than old.
- **`migrateTuplet()` retires Trip into it** (2026-08-03), same tell and same repair, plus one
  fold: a session with Trip set becomes Triplet, which plays note for note identically because
  `tupletFactor(3)` is the 2/3 the old branch multiplied by. Trip goes back to its default in
  the same pass - two parameters saying the same thing, only one of them written, is a state
  that drifts the moment a host automates the dead one. That is the `migrateVelTrim` shape,
  which retired Volume into VelTrim the same way.

The dial column takes 72 px off the group's two rows, and `groupWeights` hands about 37 of
them back (36/42/22 became 40/42/18). All of that 4 points is STEPS' - it had about 50 px
spare in each of its two rows - and none of it is PLAYBACK's, whose second row is the one
place on the band with nothing left to give. The rest came from the two rows themselves: the
Shape cell went from 234 px to 200, and Trip and Dot from 58/54 to 56/52, all still clear of
their text. Nothing overlaps and the panel is not a pixel taller.

Sizing note for anyone editing `ArpPanel::resized()`: every number in there is a *logical*
pixel and the panel is only about 950 of them wide at the editor's minimum. On a 150%
display a screenshot is 1.5x that, which is exactly how the first pass ended up with widths
three times too generous and a row of controls clipped to ellipses. Measure with UI
Automation (`BoundingRectangle`, divided by the display scale), not with a ruler on a PNG.

## v1 implementation notes

- `ArpEngine.h`: pure, allocation-free on the audio thread (fixed 32-step, ten-lane
  arrays; fixed-capacity event output; mt19937 seeded once for probability). Lane
  data crosses UI -> audio as arrays of atomics; no locks anywhere. The processor holds
  three instances of it; the file itself is unaware of that and did not change.
- Each line consumes note on/off from **its own** input buffer, holds the set
  (latch = ignore note-offs), and emits its own stream; CCs pass through untouched at the
  stage above. Until 2026-08-01 that input was the block's merged stream directly; it is now
  that line's queue plus a copy of the keybed's notes when its **Keys** switch is on, which is
  the same thing for line A on its own.
- Lane/pattern data persists as an "arp" ValueTree next to the chord pads, in the
  base KeysProcessor state, so all three products carry it identically. Line A's twelve slots
  sit on that node as they always have; B and C hang off a `line` child each, so a session
  from before the lines needs no migration and loses nothing.

## Research caveats carried forward

Cthulhu/Serum details are from primary manuals (high confidence; Cthulhu v1.1,
Serum 1, so Serum 2's redesigned editor is uncovered). Cream and Stepic rest on
secondary reviews (medium). Nothing survived verification on BlueARP, Sugar Bytes,
or the stock Ableton/Logic arps, so the stock-vs-third-party gap is inferred from
what reviewers praise in the third-party tools. No practitioner timing-engine
claims survived either (one was refuted), so the engine section is first-principles
plus Serum's documented Anchor model. Open questions logged: swing math consensus,
tied notes across pattern boundaries, transport-stop policy, Serum 2 gestures.
