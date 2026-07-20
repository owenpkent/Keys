# Keys Arp: design spec (research-backed)

Distilled from a deep-research pass (2026-07-19) over the Xfer Cthulhu v1.1 manual,
Xfer Serum manual, Kirnu Cream and Devicemeister Stepic reviews and vendor docs, and
the JUCE ArpeggiatorPluginDemo source. Confidence notes and gaps at the bottom.

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
follows the Contour/Lattice model instead. `CLAUDE.md` must be amended when this
ships.

## Engine (build from scratch; JUCE's demo is not a reference)

Verified: the official ArpeggiatorPluginDemo has no tempo sync at all (free-running
sample counter, one unsynced 0..1 knob, roughly 25-275 ms steps) and its offset
math was adversarially refuted as a pattern to copy. Requirements:

- Derive step boundaries from `ppqPosition`/bpm fresh each block; never accumulate
  counters, so tempo changes and transport jumps self-correct.
- Emit note on/off at computed sample offsets within the block.
- Track owed note-offs across block boundaries (ratchets, ties, pattern-length
  boundaries); a transport jump mid-ratchet must flush owed offs, never leak them.
- Clock spec follows Serum's documented design: a division list (1 bar .. 1/64,
  default 1/16) with **separate Dot and Trip toggles** (kept out of the division
  list so automation stays on even divisions) and an **Anchor toggle**: anchored =
  affixed to the host bar cycle (position may jump on rate change), free = no jump,
  may drift off the bar.
- **Transport stopped / standalone:** fall back to an internal clock (last-known or
  set BPM) so the arp keeps sounding while auditioning. Cthulhu goes silent with the
  transport stopped and that is a known annoyance. (Decided by Owen, 2026-07-19.)
- Engine is a pure class (`ArpEngine.h`, UI-free, unit-tested like ChordGen):
  inputs = sounding-note set + params + (ppq, bpm, numSamples); output = timestamped
  note events.

## Lanes (the core of "world-class")

Per-parameter step lanes, Cthulhu architecture. Each lane: 1-32 steps, its own
length, plus a per-lane clock divider (1x, 1/2, 1/4 speed) for polymeter, with a
**Link Lengths** toggle for the simple case.

Ranked essential (v1):

| Lane | Range / values | Default |
|------|----------------|---------|
| Note | chord-note index 1..8, or "follow direction mode"; drag below 1 = step mute | follow |
| Octave | -3..+3 | 0 |
| Velocity | 1..127 scale | as played |
| Gate | 5%..200%; >=100% into next step = tie | 100% |
| Ratchet | 1..4 sub-hits per step (Stepic's step-divider) | 1 |
| Probability | 0..100% chance the step fires | 100% |

(Probability promoted from v2 to v1 by Owen, 2026-07-19.)

Global (not per-step) in v1: direction mode (up, down, up/down, down/up,
up-and-down, down-and-up, fingered top, fingered bottom), octave range 1..4 for
directional modes, swing (applied to offbeat steps), latch (on-screen toggle:
ignore note-offs until a new chord), retrigger (restart at step 1 on new note),
rate + dot/trip + anchor.

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

## Patterns

8 named patterns (A-H) per session, one-click switching, with on-screen **Copy**,
**Paste**, and **Randomize** buttons (Cthulhu hides copy behind alt-drag; that is a
modifier gesture and is banned here). Per-pattern rate and lane data. Patterns
persist in the session state (a `ValueTree` next to the chord pads) and are
MCP-addressable later.

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
- Pattern operations: Copy/Paste/Randomize as buttons.
- Grid snap and edit modes: on-screen toggles, never modifier keys.
- Lane length: drag a handle at the lane's right edge, or - / + buttons.

## UI placement (decided: overlay panel)

An **Arp** button (tabs row, next to Chords) opens the lane editor as an overlay,
exactly the chord generator's pattern; a compact on/off toggle stays always
visible beside it. (Decided by Owen, 2026-07-19.)

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
