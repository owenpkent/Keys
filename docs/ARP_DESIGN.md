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
  default 1/16) with **separate Dot and Trip toggles** (kept out of the division
  list so automation stays on even divisions) and an **Anchor toggle**: anchored =
  affixed to the host bar cycle (position may jump on rate change), free = no jump,
  may drift off the bar.
- **Transport stopped / standalone:** fall back to an internal clock so the arp keeps
  sounding while auditioning. Cthulhu goes silent with the transport stopped and that is a
  known annoyance. (Decided by Owen, 2026-07-19.) The fallback is the **BPM** parameter
  (40..240, default 120, a slider in the Controls section) since 2026-07-27; it used to be
  the host's last-known tempo, which the standalone never has and nothing could change. A
  host that is *playing* still wins.
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

**Shape gates the whole editor** (Serum 2's model). Shape holds the eight directions plus
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

**Gate and Chance are global as well as per-step** (added 2026-07-25). The lanes are gated
behind Shape being "Pattern", so on a plain shape there was no way to shorten a note or
thin a run out at all - the two most reached-for arp controls on any hardware unit were
unreachable on the default shape. `Params::gate` and `Params::chance` **multiply** the lane
value in `fireStep`, so 100 leaves an edited pattern exactly as drawn, and each control
means the same thing in both shapes.

v2 and later (nice-to-have per the research ranking): timing-offset (Late) lane,
probability / random-select lane, harmony lane (second note within +/-1 octave),
semitone pitch lane gated by scale-degree enables with automatic root detection
(Cthulhu's inversion-insensitive chord analysis), per-pitch-class block/redirect
keyboard (its 4-state click-cycle model is already mouse-only), chord mode
(inversion stacking), per-step CC lanes, pattern chaining, arp-on-note-count.

## Scale awareness

Keys already owns Root/Scale. The arp editor flags out-of-key results visually
(Stepic's red-flag convention) and, because Keys has Scale Lock upstream, the arp
output can never leave the scale when Lock is on. This is a differentiator the
stock arps lack; it comes almost free here.

## Patterns, which became slots

Originally 8 lettered patterns (A-H) per session. **Twelve launchable slots since
2026-07-25**, at Owen's request, after a reference layout where the pattern memories are
cards you fire rather than letters you recall.

A slot carries its lane data *and* a chord, a shape and a rate. Launching it (one left
click, anywhere on the card) installs the pattern, moves the Shape and Rate parameters
through the host the way the combo boxes do, and holds the chord into the arp. Clicking the
launched slot again releases it; **Stop** does the same without reaching for All Off. A slot
with no chord launches the pattern alone and arpeggiates whatever is already sounding.

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
pad was momentary anyway. Two paths close that, both added 2026-07-25 (the section move
closed the first half):

- **A click on a chord card, while the arp is on.** It calls
  `KeysProcessor::holdArpChordFromPad`, which emits the note-ons and never the note-offs
  until the next call. This was its own **To Arp** toggle on the Pads bar until 2026-07-27,
  on the reasoning that a pad must not quietly do something different than it did a minute
  ago. In practice the separate arming read as a button that did nothing: with the arp off,
  handing it a chord looks exactly like sustaining one, and that is the state the toggle was
  most often found in. Owen had it removed, and the arp's own **On** is the mode now
  (`KeysProcessor::cardsFeedArp`, which every surface showing a chord card asks, so the pads
  and the generator's grid can never disagree). Switching the arp off releases a chord a
  card was holding, or it would drone with nothing arpeggiating it and no click left to
  release it.
- **Send to arp slot**, in the pad's card menu, which copies the chord into a slot for
  later. A copy and not a reference, so regenerating the pad page cannot silently rewrite
  what a slot plays.

The held chord is tagged `arpChordTag` so it never collides with pad or live-card
scheduling, and `allNotesOff()` forgets it - otherwise a panic silences the chord while the
launched slot still paints as playing.

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

The arp is a foldable **section**, between the centre view and the chord pads, with its
**On** toggle and a **Detach** button on its own bar. Folding the section destroys the
editor, never the arpeggiator, which is why On lives on the bar rather than inside the
panel. Detach moves the whole panel into a resizable window (`DetachedWindow`, shared with
every other section since 2026-07-27); a detached section takes no height in the main
window, and the Re-dock button travels into the window with it.

It got there in three steps, all at Owen's request. A full-editor overlay first, changed to
a centre view on 2026-07-25 because the overlay dimmed and covered the keyboard you are
meant to be playing while you edit. Then a section later the same day, because as one of
three centre views it competed with the knobs and the generator: picking the arp put away
the chord cards it exists to chew on. The centre views are Perform and Chords now.

**The chord pads are their own section** (2026-07-25), between the centre view and the
keyboard, so they are on screen under Perform, Chords *and* Arp. This is the layout change
the whole "cards into the arp" feature rests on: the arpeggiator's job is to chew on a
chord, and it was the one view from which you could not reach one.

## Control band layout

Three ruled, captioned groups, after the hardware-arp arrangement Owen asked for:

| Group | Holds | Visible |
|-------|-------|---------|
| PATTERN  | Shape + `<` `>`, Rate + `<` `>`, Trip, Dot | always |
| PLAYBACK | Swing, Gate, Chance (knobs), Octaves, Anchor, Latch, Retrigger | always |
| STEPS    | Steps, Speed, Link | Pattern shape only |

The `<` `>` pairs matter more than they look: stepping to the next shape is the commonest
thing you do to an arp and it used to cost a click, a travel down a menu and a second click.
They **clamp** rather than wrap, so one click too many on "Up" cannot land on "Pattern" and
throw the step editor open under you.

Sizing note for anyone editing `ArpPanel::resized()`: every number in there is a *logical*
pixel and the panel is only about 950 of them wide at the editor's minimum. On a 150%
display a screenshot is 1.5x that, which is exactly how the first pass ended up with widths
three times too generous and a row of controls clipped to ellipses. Measure with UI
Automation (`BoundingRectangle`, divided by the display scale), not with a ruler on a PNG.

## v1 implementation notes

- `ArpEngine.h`: pure, allocation-free on the audio thread (fixed 32-step, 6-lane
  arrays; fixed-capacity event output; mt19937 seeded once for probability). Lane
  data crosses UI -> audio as arrays of atomics; no locks anywhere.
- The arp consumes note on/off from the block's merged MIDI stream as its input
  (so keyboard, latch, and chord pads all feed it uniformly), holds the set
  (latch = ignore note-offs), and emits its own stream; CCs pass through.
- Lane/pattern data persists as an "arp" ValueTree next to the chord pads, in the
  base KeysProcessor state, so all three products carry it identically.

## Research caveats carried forward

Cthulhu/Serum details are from primary manuals (high confidence; Cthulhu v1.1,
Serum 1, so Serum 2's redesigned editor is uncovered). Cream and Stepic rest on
secondary reviews (medium). Nothing survived verification on BlueARP, Sugar Bytes,
or the stock Ableton/Logic arps, so the stock-vs-third-party gap is inferred from
what reviewers praise in the third-party tools. No practitioner timing-engine
claims survived either (one was refuted), so the engine section is first-principles
plus Serum's documented Anchor model. Open questions logged: swing math consensus,
tied notes across pattern boundaries, transport-stop policy, Serum 2 gestures.
