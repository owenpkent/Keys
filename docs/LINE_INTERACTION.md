# Lines that listen to each other

**Status: proposed and unbuilt (2026-09-01).** Nothing in this file ships. Owen:

> *"can we get the arpeggiators to interact with each other, like the step sequencers, so we
> can get interesting variations."*

"Like the step sequencers" is read here the modular way: hardware step sequencers become
interesting when one is patched into another - a reset input, a clock input, a gate that mutes,
a CV that transposes - and Keys has four sequencers in one window that today never hear each
other. (If the dictation was "Stochas", its chain conditions are candidate **B** below; the rest
of the file still applies.) Read `docs/ARP_DESIGN.md` for the engine and
`docs/SEQUENCER_LANDSCAPE.md` for where each idea comes from before building any of it.

---

## 1. What the four lines share today, and what they do not

Four `ArpEngine`s, each with its own rate, shape, lanes, chord and clock. They share exactly two
things: the **bar grid** (`HostClock::hasGrid`, so anchored lines walk in lockstep) and the
**output merge** (`ArpMerge` folds them under one note-on per pitch). Neither is an interaction.
A line cannot tell whether another line fired, wrapped, or what note it is on. Every variation
you can hear between lines today is a coincidence of two independent clocks - which is exactly
the polyrhythm the All view was built for, and exactly why it never *responds*.

`runArpLines` runs the lines in letter order on the audio thread, every block. That ordering
is the whole opportunity: by the time line B runs, everything line A decided in this block is
already known. The keybed lights already lean on it (`arpNoteLines` is single-writer *because*
the lines run in order). The same fact makes a line able to read another.

---

## 2. The plumbing, built once: the line bus

Every mechanism below is one rule reading one of five signals. So the engine grows a small
per-line record, written by each engine as it runs and readable by every engine that runs
**after it in the same block**:

```
struct LineBus
{
    int  firedAt[maxStepsPerBlock];  // sample offsets of the steps that sounded this block
    int  firedCount;
    bool lastStepFired;              // the Chain lane's own bit, published
    bool wrapped;                    // the walk came round to pass zero this block
    int  lastNote;                   // the pitch the last step landed on (-1 if none)
    int  lastVelocity;               // 1..127
    bool sounding;                   // holding a chord and switched on
};
```

- **Written in letter order, read downward only.** A follows nobody; B may follow A; C may
  follow A or B; D any of the three. A line following a *later* letter would read the previous
  block's record - a step late at worst, and a loop at worst-worst (A ducks to B ducks to A).
  Downward-only is a constraint and a feature: nothing can loop, and the letter order on the
  bar becomes the signal flow, top to bottom, which is how a modular patch reads too.
- **Per block, recomputed.** The bus is not state the transport has to reconstruct: it is what
  happened in this buffer. The engine's "stateless from the playhead" rule is untouched.
- **One source per line.** `arpFollow` (choice: Off / A / B / C, appended, default Off) names the
  line this line listens to. Every mechanism reads *that* source. One combo rather than one per
  mechanism, or a card that wants to duck to A and reset from B has three pickers and a diagram.
  If that turns out to be the ask, it is a second combo later, not a redesign.
- **A line following a line that is off or holding nothing** hears silence: no fires, no wrap,
  no note. Every mechanism defines what silence means for it (below), and every one of them
  reduces to "as today" - so switching the source line off never strands the follower.

Cost of the bus alone: half a day, no audible change, one `ArpTests` block pinning that the
record says what the events say.

---

## 3. The mechanisms

Each: what it does, its parameter, the rule, what silence from the source means, cost. All
parameters are appended after the last one that exists on the day they are built, and every
default is off.

### A. DUCK - skip my step when the source fired

- **What.** The hocket. Where A plays, B does not; two lines on one rate and one chord
  interlock into one part that neither is playing. At 100 it is exact; at 50 it is a tendency;
  the two knobs against each other (B ducks A at 100, C ducks B at 60) is a three-voice
  conversation from three Up runs.
- **Parameter.** `arpDuck`, int 0-100, default 0: the odds this step is skipped if the source
  fired a step **since my previous step**. On equal rates that is "the same step"; on unequal
  rates it is "any hit in my window", which is the reading that stays musical when B runs at
  1/8 under A's 1/16 - B ducks whenever A was busy.
- **Rule.** In `fireStep`, after chain, mute and rest and *before* the chance draw: if the
  source's `firedAt` has an entry in `(myPreviousStep, thisStep]`, roll against DUCK; skipped
  means `lastStepFired = false` and return, exactly as a failed chance. Rolled off the same
  (step, era) hash Mutate uses, so **LOCK holds the hocket** and a found interlock can harden.
- **Silence from the source.** No fires means nothing to duck: the line plays as today.
- **Legato.** A ducked step is a skipped step, and Legato holds through it - *except* that
  Legato's one-step lookahead cannot foresee a duck, because the duck depends on the source's
  step that has not been decided yet. So under Legato a ducked step is a gap when the note
  before it had a gate under 100, and a hold when it did not. Honest limit; the fix (running
  every line's lookahead whether or not Legato is on) would change the chance stream of lines
  with Legato off, which "off is byte-for-byte what it was" forbids. Say it in the tooltip.
- **Cost.** A day on top of the bus. The one to build first: it is the mechanism that makes
  "variations" out of two lines that would otherwise be one.

### B. NEIGHBOUR - the Chain lane learns to read another line

- **What.** Digitakt's NEI condition, Stochas's chain against somebody else's step: a *drawn*
  cell that plays only if the source's last step sounded (or did not). DUCK is a knob over the
  whole line; this is the same question one step at a time, drawn where you want it.
- **Parameter.** None. The Chain lane's values grow: 0 always, 1 mine sounded, 2 mine did not
  (today), **3 source sounded, 4 source did not**. Lane values are lane data, not parameters,
  so appending is safe and an old session reads its 0-2 unchanged.
- **Rule.** `fireStep`'s chain test reads `bus[source].lastStepFired` for 3 and 4. With no
  source, 3 and 4 read as 0 - a lane drawn for a source that was switched off plays every step
  rather than none, the safe way round.
- **Silence.** As above: no source, the condition is always true.
- **Cost.** Half a day once the bus exists. The grid already draws Chain; two more values need
  two more glyphs and a tooltip.

### C. RESET - the source coming round restarts me

- **What.** The reset input on every hardware sequencer. B runs a seven-step lane against A's
  sixteen and drifts, as polymeter does - and every time A comes round to its top, B is put back
  to its top too, so the drift is *bounded*: it wanders for a bar and snaps home. That is most of
  what makes a René patch sound composed rather than random.
- **Parameter.** `arpResetFollow`, bool, default off. Not a knob: it is a yes or a no.
- **Rule.** When `bus[source].wrapped` is set, this line's next step becomes step 1 - the same
  `pendingRetrig` path a Retrigger restart already takes (`stepBase = stepIndex`,
  `dirCursor = 0`), so nothing is reimplemented and Launch Quantize, Offset and the lanes all
  see it as the restart they already understand. `wrapped` is set where `laneStepIndex` comes
  round to zero over the Note lane's walk window - the same measurement LOCK's era already uses.
- **Silence.** No wrap means no reset: the line free-runs as today.
- **Cost.** A day. The test that matters is the polymeter one: seven against sixteen, reset on,
  B's step 1 lands with A's on every bar.

### D. CLOCK - the source's hits are my steps

- **What.** B stops reading the grid and steps once per hit of A. A's rhythm becomes B's clock,
  so a swung, ducked, ratcheted A drives B's pattern through its cells in A's time - and B's
  own Rate, Swing and Tuplet go grey, because B has no clock of its own any more. This is what
  turns two sequencers into one instrument with two voices.
- **Parameter.** `arpClockFollow`, bool, default off.
- **Rule.** In `process()`, with it on, the step loop is replaced: for each entry in
  `bus[source].firedAt`, fire one step of this line at that offset. `stepIndex` counts fired
  steps (the `firedCountBefore` shape dividers already use), so the lanes advance one cell per
  source hit. Gate is measured against the *source's* step length, published on the bus, so a
  50 % gate still means half the space to the next hit.
- **Silence.** No hits, no steps: a clocked line following a silent source is silent, which is
  what a sequencer with no clock is. The card's scrim rule says so already for a line that is
  off; the tooltip says it for this.
- **Cost.** Two to three days. The step loop is the most careful code in the engine (early
  swing, the Late lane, three-steps-back), and a second entry into `fireStep` has to respect
  everything `firePendingBefore` and `emitHit` assume about time order. Build after A and C.

### E. SHADOW - transpose me by the source's current note

- **What.** The CV-sum patch: B's run is shifted by the interval A is currently on above the
  root. A walks C-E-G, B plays a fixed figure, and B's figure moves with A's melody - a
  counterpoint that follows without copying. At 100 the whole interval; at 50 half of it,
  rounded to the scale; the knob is how far B leans toward A.
- **Parameter.** `arpShadow`, int 0-100, default 0.
- **Rule.** In `addHit`, `pitch += round(interval * shadow / 100)` where `interval` is
  `bus[source].lastNote - source's root note`, then the existing Scale Lock snap. Applied
  before the snap so a leaning line stays in key under Lock.
- **Silence.** No note means no interval: the line plays unshifted.
- **Cost.** A day. Worth a listen before committing a cell to it: it is the one mechanism
  that changes *pitches*, and Mutate and Stray already own that axis.

### F. LOCK SYNC - variations change together

- **What.** Each line's LOCK holds a variation for its own era and lets go on its own count.
  Synced, every line finds a new variation on the same pass, so the whole arrangement turns a
  corner at once instead of four lines drifting through four ideas.
- **Parameter.** Global, `arpLockSync`, bool, default off - global for Scale Lock's reason: a
  sync that half the lines take is not a sync.
- **Rule.** Every line's era is computed against line A's walk window rather than its own.
- **Silence.** If A is off, every line's era is its own, as today.
- **Cost.** Half a day. Not a bus mechanism at all, strictly, and the cheapest thing in the
  file; listed because "interesting variations" is what it is for.

### G. HANDOVER - Ableton's Follow Actions between lines

- **What.** After N bars, this line goes quiet and hands the floor to the source (or the next
  letter): A for two bars, then B for two, then C, round and round - a song structure from four
  arps, with each line's Chain, slots and lanes still its own.
- **Parameter.** `arpHandoverBars` (int 0-8, 0 off) and a target. **Deferred**, and the reason
  is structural: a line's On is a *parameter*, and a handover that switches lines on and off
  writes a parameter from the audio thread on a clock - the exact two-writers shape that deleted
  the macro card's own On toggle. It needs a runtime "has the floor" state beside On, which is a
  design of its own. Write it up when the bus exists and A, C and D have been lived with.
- **Cost.** Several days, most of it the state question above.

### H. VEL FOLLOW - louder when the source is quiet, or with it

- **What.** B's level rides A's last velocity: with it (a shared crescendo) or against it (B
  fills the space A leaves). The dynamics half of DUCK.
- **Parameter.** `arpVelFollow`, int -100..100, default 0, bipolar, centred off.
- **Rule.** The level handed to `humanisedVelocity` is nudged toward (or away from)
  `bus[source].lastVelocity` by the knob's share. The Humanize ring still wanders around it -
  the same "a second level, not a bypass" rule ACCENT takes in `docs/MACRO_KNOBS.md`.
- **Silence.** No velocity, no nudge.
- **Cost.** Half a day. Low on the list: it is subtle on most patches, and VEL's ring already
  does most of what a listener hears.

---

## 4. Where the controls go

- **The source picker** (`arpFollow`) is a combo on the line's Play page, PLAYBACK group, next
  to Retrigger - a thing you set once. It reads "Follows: Off / A / B / C" and lists only the
  letters above this line, which is how the downward-only rule shows itself instead of being
  a hidden refusal. A line with nothing to follow (line A) shows the combo greyed with Off.
- **DUCK and SHADOW are knobs**, and the card's second row (`docs/MACRO_KNOBS.md`) is where a
  knob you sit and turn belongs. Two of that row's eight cells, if Owen wants them there; the
  Play page otherwise.
- **RESET and CLOCK are chips** on the card's bottom strip beside Legato, since they are a yes
  or a no you flip while listening. The strip has room for one more at the docked floor and not
  two; `arpMacroModsW` is the arithmetic, and the floor rises if both land there.
- **The card says who it follows.** A small "← A" in the card's caption row beside the line's
  letter, in the source's colour, so the signal flow is readable from the All view without
  opening a Play page. The line colours exist to tell lines apart; this is the first time one
  line's colour appears on another's card, and it means exactly "listening to".
- **`get_state` publishes the source and the bus's `sounding`**, so a script can see the graph.

---

## 5. What to build first

**The bus, then A (DUCK) and C (RESET), then B (NEIGHBOUR) because it is nearly free.** That is
three or four days and it is where the "interesting variations" live: interlocking parts and
bounded drift are what two independent lines cannot do at all today. D (CLOCK) is the deep one
and should follow once the bus has been lived with. E and H change pitch and loudness, which
other knobs already own, and want a listen before a cell. F is a global tick and can land any
afternoon. G waits on a state design.

What every mechanism keeps: **a line off or silent leaves its followers playing as today**, and
**signal flows downward, A to D**. Break either and the four lines stop being four instruments
you can reason about one at a time.
