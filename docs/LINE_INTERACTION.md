# Lines that listen to each other

**Status (2026-09-01): Phases 0 to 2 are built - the bus, the From picker, DUCK, RESET and
NEIGHBOUR.** CLOCK and everything after it is proposed and unbuilt. Section 4 below records where the two built
controls actually went, which is not where this file first put them. Owen:

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

**Where phase one actually put them (built 2026-09-01).** The Play page is exactly full at its
floor: PLAYBACK's two rows spend every pixel at `arpDeepPageMinW` and FEEL's five sliders are at
their 120 px minimum, so the picker beside Retrigger and the knob beside Drift both meant raising
that floor and re-weighting the band's groups. The card had the room instead - its floor is the
bottom strip's, and the knob strip needed 486 of that 598 for eleven - so **DUCK is the eleventh
knob beside DENSITY** and **From is the sixth chip on the bottom strip beside Legato**, worded
"From A" because the strip has no caption. The letters a line may not pick are greyed in the
popup rather than removed (the attachment maps index to item). The detached Arp window's floor
rose to 1288 px for the chip; nothing else moved. The `< A` caption mark is not built: the chip
says it. **RESET (phase two) is the Follow entry of the Play page's Retrigger list**, not a
chip beside Legato: with From on the strip there was no seventh cell at the docked floor, and
"when does the pattern start over" already had its control, so the list grew one answer.
**NEIGHBOUR** landed exactly as designed. The paragraphs below are the design as first written
and stand for the mechanisms not yet built.

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

---

## 6. Build plan

Owen: *"just plan it out."* Four phases, each a PR of its own, in the order section 5
recommends. Every anchor below is a real line in the code on 2026-09-01; they will drift, the
shapes will not.

### Phase 0 - the bus, and the picker (no audible change)

**Engine, `src/ArpEngine.h`.**

- A public POD the engine writes about itself every block:

  ```cpp
  struct LineRecord
  {
      long long firedBefore;        // steps fired in every block before this one
      int       firedAt[maxStepsPerBlock]; // this block's step fire offsets, in order
      int       firedCount;
      long long pass;               // the Note lane's walk pass this step is in
      bool      lastStepFired;      // the Chain lane's own bit, published
      int       lastNote;           // -1 until a step has sounded
      int       lastVelocity;       // 1..127
      double    stepSamples;        // the source's step length, for CLOCK's gate later
      bool      sounding;           // enabled and holding a chord
  };
  LineRecord record {};
  ```

  `firedAt` is **one entry per step, not per hit**: a ratcheted or chord-shape step is one
  fire. `maxStepsPerBlock` is a small constant (16 covers a 1/64 at 300 bpm in a 4096 buffer)
  and the array is fixed, so the audio thread allocates nothing.
- `process()` zeroes `firedAt`/`firedCount` at the top and sets `record.sounding`. `fireStep`
  writes `firedAt` where it publishes `uiRelStep` today (one line above the `fireStep` call in
  the step loop is the cleaner spot, since a step the chord could not fill is not a fire: write
  it where `lastStepFired = true` is set). `lastNote` / `lastVelocity` are written beside the
  first `emitHit` of the step. `firedBefore += firedCount` in `advanceBlock`.
- `pass` comes from the expression `mutateCell` already computes (`ArpEngine.h:1336`, the
  `pass` local at ~:1350). Factor it into `long long walkPass(const Params&, long long
  globalStep) const` and call it from both, so LOCK's era and the bus's pass can never disagree
  about where a pass begins.
- `Params::follow`, `const LineRecord* follow = nullptr;` - the `chords` precedent at
  `ArpEngine.h:683`: null means nobody, and every mechanism tests it.
- `restart()` and `hardReset()` zero the record.

**Processor, `src/PluginProcessor.cpp`.**

- `arpFollow`, appended (after whatever is last on the day - `apLegato` today): a choice
  parameter, list `Off / A / B / C`, default Off. **Append-only forever** once a session stores
  an index into it; D is not on it because nothing runs after D.
- In `runArpLines`' main loop (`:2329`), where `ap` is built (`:2391`):
  ```cpp
  const int src = (int) arpParam(n, apFollow) - 1;
  ap.follow = (src >= 0 && src < n) ? &lines[(size_t) src].engine.record : nullptr;
  ```
  **`src < n` is enforced here, not only in the UI**: an MCP client or a host automation lane
  can write any index, and a line reading a later letter's record would read last block's,
  which is the loop the rule exists to forbid. Silently nobody, and `get_state` says so.
- `get_state` (`src/mcp/KeysMcp.cpp:358`): `follows` (the letter or `""`) beside
  `soundingNoteCount`, and `sounding` from the record.

**UI, `src/ui/ArpPanel.cpp`.**

- The picker is a combo on the Play page's PLAYBACK row A, beside Retrigger. **Reserve its
  cell off the right first** (the comment at `:4380` already says why: Retrigger is the elastic
  one with a 120 px floor, Play's 64 px cell is taken first for that reason, and this is a
  second fixed cell taken the same way). Caption `Follows`, entries only the letters above this
  line - on line A it shows `Off` greyed. `LayoutTests`' starvation sweep at `arpDeepPageMinW`
  (970) is the check; if Retrigger drops under its floor there, the floor rises.
- The card says who it listens to: a small `< A` in the source's own colour at the left end of
  the caption strip whose right end draws `LINE B` (`:2649`; the deep view's twin at `:4126`).
  First time one line's colour appears on another's card, and it means exactly this.
- Accessible names: `Arp follows` on the page.

**Tests.** `tests/ArpTests.cpp` gains its first two-engine block: run A then B in the same
"block" (two `process` calls on two engines), B with `p.follow = &a.record`, and assert the
record says what A's events say - `firedCount` and offsets against `collect(outA)`, `lastNote`
against the last note-on, `pass` stepping once per walk length, `firedBefore` accumulating
across three blocks of uneven size. `tests/StateTests.cpp`: `arpFollow` appended last, Off by
default, and a value naming a *later* letter leaves `follow` null (a processor-level test, the
`processBlock` shape at `StateTests.cpp:854`).

**Cost.** A day, with the tests. Ships inside Phase 1's PR - a bus nobody reads is not a
release.

### Phase 1 - DUCK

- `arpDuck`, appended, int 0-100, default 0. `Params::duck`.
- In `fireStep`, after the rest test and **before** `chanceFails`:
  ```cpp
  if (p.follow != nullptr && p.duck > 0)
  {
      const long long seenNow = p.follow->firedBefore + countAtOrBefore(*p.follow, offset);
      const bool sourceFired = seenNow > seenAtMyLastStep;
      seenAtMyLastStep = seenNow;
      if (sourceFired && rollsCell(p, globalStep, p.duck)) { lastStepFired = false; return; }
  }
  ```
  `countAtOrBefore` counts the source's `firedAt` entries at or before this step's offset -
  the source ran first, so its record may hold fires *later* in this block than this step,
  and those have not happened yet from here. `firedBefore` is what makes the window exact
  across blocks the follower had no step in. `seenAtMyLastStep` is one `long long` of state,
  zeroed in `restart()`/`hardReset()`. `rollsCell` is the (step, era) hash Mutate and Stray
  roll on, so **LOCK holds the hocket**.
- Before `chanceFails` on purpose: a ducked step must not spend a chance draw, the same rule the
  chain condition states at the top of `fireStep`.
- `prerollNext` (Legato's lookahead) does **not** ask about ducking - the source's next step is
  not decided yet. Documented limit; tooltip says "a ducked step is a gap under Legato when the
  note before it had a gate under 100".
- UI: a `bar()` slider **Duck** in the Play page's FEEL group beside Drift for now (`:3140` is
  Drift's), and a cell on the card's second row when `docs/MACRO_KNOBS.md` is built. Not a chip:
  it is an amount.
- Tests: equal rates, Chance lane on A silencing steps 1 and 3, DUCK 100 - B fires exactly
  where A did not; DUCK 0 byte-identical; unequal rates (A at 1/16, B at 1/8) - B ducks the step
  after any window with an A hit in it; LOCK 100 holds the same hocket across two passes;
  source off - B plays as today.
- Docs: CHANGELOG (**PARAMETER LAYOUT CHANGE**, two parameters), CONTROLS.md rows for Follows
  and Duck, ARP_DESIGN.md a "Lines that listen" section, CLAUDE.md the round's bullet.
- **PR 1 = Phase 0 + Phase 1.** Two days.

### Phase 2 - RESET and NEIGHBOUR

- **RESET.** `arpResetFollow`, appended, bool, default off. `Params::resetFollow`. In the step
  loop in `process()`, immediately before `if (pendingRetrig)`:
  ```cpp
  if (p.resetFollow && p.follow != nullptr && p.follow->pass != seenSourcePass)
  {
      seenSourcePass = p.follow->pass;
      pendingRetrig = true;
  }
  ```
  so a reset *is* a Retrigger restart (`stepBase = stepIndex; dirCursor = 0;`) and Launch
  Quantize, Offset and every lane see the restart they already understand. `seenSourcePass` is
  initialised to the source's pass on the first block it is read, not to zero, or switching
  RESET on resets the line at once. A step late at worst when both lines land in one block
  with the follower's offset before the source's; equal-rate anchored lines share offsets, and
  the source ran first.
  UI: a chip on the card's bottom strip after Legato, on-screen word `Reset`, accessible name
  `Macro reset follow A`. `arpMacroModsW` grows by a cell and a gap (76 + 8), which the docked
  card's 614 px still holds (584 needed); the detached floor rises to what the arithmetic says.
  Tests: seven against sixteen, RESET on - B's step 1 coincides with A's step 1 every sixteen
  steps for four passes; RESET off - they drift exactly as today; switching RESET on mid-run
  does not reset.
- **NEIGHBOUR.** `laneRanges[laneChain]` `{0, 2}` -> `{0, 4}` (`ArpEngine.h:135`); lane data,
  no parameter, an old session's 0-2 unchanged. In `fireStep`'s chain test and in
  `prerollNext` (both, or Legato's lookahead disagrees with the step): `3` is
  `p.follow ? p.follow->lastStepFired : true`, `4` its negation, and with no source both read
  as `0`. `LaneGrid::cellText` (`:299`) keeps digits - a cell is too narrow for a word - and the
  tooltip at `:3238` grows two clauses. The lane's tab dot already lights for any non-default
  value.
  Tests: two engines, A's Chance lane alternating, B's Chain lane all 3 - B fires only after
  A's fires; all 4 - only after A's skips; source off - every step.
- **PR 2 = Phase 2.** A day and a half.

### Phase 3 - CLOCK

- `arpClockFollow`, appended, bool, default off. `Params::clockFollow`. The record already
  carries `stepSamples`.
- In `process()`, the step loop is bypassed when `clockFollow && follow != nullptr`: for each
  `follow->firedAt[i]`, `stepIndex = follow->firedBefore + i` (the `firedCountBefore` shape the
  dividers use), then the same retrigger handling, `firePendingBefore(offset, ...)`, the
  `uiRelStep` publish and `fireStep(p, stepIndex, offset, follow->stepSamples, ...)`. Swing and
  the Late lane are the source's, since the offsets are the source's; this line's Rate, Swing,
  Dot, Tuplet and Anchor are greyed on the card and the page (`refreshRateMode`'s greying
  already answers a question of this shape for Hz).
- With no source, or a silent one: no steps. That is what a sequencer without a clock is.
- Tests: B advances one lane cell per A fire; A's ratchets do not double-step B; A silent, B
  silent; B's gate at 50 % is half of A's step, not B's own rate.
- **PR 3 = Phase 3.** Two to three days. Build after PR 1 has been played with.

### Later, in this order

SHADOW (a day; listen first - it is the one that changes pitches), LOCK SYNC (an afternoon,
global), VEL FOLLOW (half a day), HANDOVER (needs the runtime-state design; write that up
before any code).

### What every phase repeats

Append the parameter, default off, flag **PARAMETER LAYOUT CHANGE** in the changelog; a
`StateTests` line that the parameter is last and off; the accessible name in CLAUDE.md's
screenshots section; the rule that **a source off or silent leaves the follower as today**
pinned by a test in every phase, because it is the rule most easily broken by the next one.
