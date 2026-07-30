# Keys Chance: design spec (research-backed)

Chance is a probabilistic note source: a generative brain that decides *what to play*
from the notes you are holding, rather than a mask thrown over a pattern you already
wrote. It is the seventh Keys section, decided 2026-07-29 by Owen (name "Chance", its
own section, full v1 scope).

The lineage is Mutable Instruments Marbles' `t` and `X` generators, Music Thing
Modular's Turing Machine, and a small amount of borrowed vocabulary from machine
learning (softmax temperature, Dirichlet pseudo-counts, 1/f melodic contour). None of
it is ported. Marbles' firmware is MIT and porting would be permitted, but the kit's
established precedent is reimplementation from the described algorithm, and that line
holds here too. Grids, whose density morph inspired one function, is GPL and
contributed ideas only.

The distinction that matters throughout: the arp already has a **probability lane**,
shipped in v1. That lane decides whether a step you authored fires. Chance decides
which note exists in the first place. Adding another fire/skip control would be a
duplicate feature; this is not one.

## Placement and contract

A seventh section, appended to `SectionId` in `src/PluginEditor.h` and positioned
visually after the Arp block in `resized()`. Appended rather than inserted so nothing
already numbered shifts. The generic machinery in `KeysEditor::sections` gives it a
fold, a Detach button, a `DetachedWindow` titled "Keys Chance", and a bar for free:
per CLAUDE.md, a section is added by adding a table entry, not by copying a code path.
`sectionHeight()` gains a case, since it is still a switch on purpose.

Size, following the Arp entry's shape: `minSize { 900, 300 }`, `defaultSize
{ 1100, 460 }`. Nine continuous controls, two mode selectors and three buttons need
the room, and the Arp panel is already dense at 1100x520 with twelve slot cards and
six lane rows.

**Chance On lives on the bar and survives folding**, the same exception CLAUDE.md
grants arp On, and for the same reason: you must be able to stop it without unfolding
it. Turning Chance On also turns arp On if it is off. Chance is a note source for the
arp's clock, so an On that produces silence because a different section is off is a
dead state, and dead states are exactly what the mouse-only contract is meant to
prevent. One click, one audible result.

## One clock, one scheduler

CLAUDE.md names the arp stage as the only sanctioned playhead consumer in Keys, and
the standing decision is that every future melody feature reuses that scheduler rather
than adding a second clock. Chance obeys this literally.

- `src/ChanceEngine.h` is a new pure header: UI-free, lock-free, no allocation, no
  JUCE GUI dependency, unit-tested in `tests/ChanceTests.cpp` beside `ArpTests.cpp`.
- It has one entry point, `advance()`, called once per step whether or not that step
  sounds, and it emits no MIDI. It returns a `Decision`: whether the step fires (the `t`
  generator), which pool members sound (the `X` generator), and that hit's velocity,
  gate, ratchet count and timing nudge. Called once per step *including* silent ones,
  because the loop has to stay in phase with the grid either way.
- `ArpEngine::fireStep()` routes to it as a third note source beside the `Direction`
  walk and the pattern lanes, gated the way `Params::usePattern` already gates lane
  data.

Everything downstream stays untouched: `emitHit()`, the `pending[]` ratchet overflow,
`retireDue()`, and the `Held::ons` refcount. Those paths are where every stuck-note
bug in this repo has lived. Not reimplementing them is the single largest risk
reduction available, and it is worth more than any architectural tidiness a standalone
engine would buy.

**Selection, never invention.** Every pitch Chance can return is already in the arp's
candidate pool, which is the held set crossed with the octave range. It chooses among
those; it does not synthesise a pitch and then correct it. That has three consequences
worth stating plainly, because the first draft of this document had it the other way
round and was wrong:

- It cannot produce an out-of-key note, so it never calls `snapToScale` and
  `keys::resolveOutputNote` in `src/NoteMath.h` gains no second call site. Whatever
  Scale Lock did at the input surfaces still holds, untouched.
- The Key control therefore re-weights the pool rather than quantizing a free value.
  That is the same musical gesture (collapse toward the notes that matter) with none of
  the risk.
- It is truer to the product. Keys is played. A generator that invented pitches would
  be authoring, which is Contour and Lattice's job.

## Determinism is the feature, not a nicety

The arp's existing chance draw uses a `std::mt19937` fixed-seeded once, whose live
variation comes from real-time call ordering. That is unreplayable by construction,
and it is fine for what it does.

Chance cannot use it. Loop-locking *requires* replayable draws: "play that again" is
meaningless if the draw depends on when the audio callback happened to run. So every
draw comes from `okstudio::poly::hash01(seed, lane, step)` in the kit's
`Polyrhythm.h`, a splitmix64-derived pure function of its coordinates. Same seed, same
step, same note, forever, on any block size.

**One seed expands into the whole step.** This is the detail that makes Marbles feel
musical rather than merely random, and it is easy to miss. Each of its sixteen ring
slots stores a 32-bit seed, not a value, expanded by a small LCG
(`word = word * 1664525 + 1013904223`) into six correlated sub-decisions. Recycling
one seed therefore replays a step's entire *character*: its gate, its pitch, its
velocity, its jitter, together. Chance does the same, deriving all of a step's
decisions from one hash. Lock a loop and you get a figure you could hum, not a
statistic you could measure. Strata's `docs/GENERATIVE.md` reached the same conclusion
independently and states it in those words.

The seed is 64-bit state, not an automatable parameter, so it serializes next to the
arp slots and chord pads the way lane data already does.

## The t generator: Density, Deja Vu, Jitter

**Density** (0-100%) drives *k* in a Euclidean E(k,n) via the kit's `densityPulses`,
a Grids-style morph. It redistributes pulses rather than thinning them, so sparse
settings stay musical instead of merely empty. Marbles reaches sparseness through
BIAS and its pattern tables; Euclid is the better fit here because the kit already
has it and Keys' rate/step vocabulary is already grid-shaped.

**Deja Vu** (0-100%, centre-detented at 50) is the loop lock, and its curve is a
parabola, not a ramp: mutate probability `p = (2d - 1)²`. So `p = 0` at *centre*, a
frozen loop repeating exactly, and `p = 1` at *both* extremes. What a mutation means
differs by side:

- Below centre, it writes a fresh value into the ring and reads it straight back.
  New material. At 0%, nothing ever repeats.
- Above centre, it jumps the read pointer to a random position inside the last
  Length writes. Old material reordered, never regenerated. At 100%, every step is a
  permutation of one fixed set.

**Length** (1/2/3/4/6/8/12/16, default 8) sets how far back the loop reaches. Those
values are both Marbles' and the Turing Machine's tap lengths, and they match the
arp's existing choice-combo idiom.

Default Deja Vu is centre, so switching Chance on gives you a repeating figure
immediately. That is the comprehensible default: you hear a riff, and either
direction on the knob makes it evolve. Note that the Turing Machine's equivalent knob
is inverted (centre is maximum randomness, extremes lock); Marbles' polarity is the
better one to copy because the musically useful setting sits at a detent you can find
without looking.

**Jitter** (0-100%, default 0) is timing feel, shaped as Marbles shapes it: a quartic
amount curve on a fat-tailed symmetric bell. The quartic keeps the first half of the
knob's travel nearly inaudible, which is a feature rather than waste on a 0-100 integer
control, and the bell puts most steps near the grid while occasionally throwing one
well off it.

Marbles also pulls its phase back toward the unjittered position each tick, and **that
part is not reproduced, because there is nothing here for it to correct.** Marbles
jitters a free-running oscillator, so its error would accumulate without a restoring
force. The arp re-derives every fire time from `ppqPosition` each block and never
accumulates a counter, so a nudge is relative to that step's own grid position and the
next step starts clean. Copying the pull-back would have been cargo cult.

**Jitter does not conflict with Humanize, and needs no bypass.** Humanize is applied
inside `KeysProcessor::noteOn` on the message thread, a path the arp's output never
takes, and its timing half is already retained-but-unread (`humanizeTime`, "Timing
Spread"). Humanize is a velocity range and nothing else. Arp step timing has never
been humanized. So Jitter is the only timing control in the chain by construction.

Velocity composes the same way without a special case: Chance writes a *scalar* on
`Held::velocity`, exactly as the existing velocity lane does, and Humanize keeps
setting the base velocity at press time. Two controls, two jobs, no interaction to
document.

## The X generator: Spread, Bias, Temperature, Wander

**Spread** (0-100%) and **Bias** (-100..+100) jointly parameterize a beta-distribution
inverse-CDF over the held pool and the octave range, as Marbles does with a 9x5 binned
table. Both degenerate ends come free and both are useful: below roughly 5% Spread
collapses to one fixed note equal to Bias, and above roughly 95% it becomes a coin
flip between the two extremes of the range, weighted by Bias. Between, Spread sweeps
from a narrow bell around Bias to fully uniform.

**Temperature** (0-100%, default 35) is the machine-learning knob:
`P_i ∝ w_i^(1/T)`, the softmax reparameterized over the pool's existing weights. At 0
it provably collapses to the single highest-weighted candidate, a deterministic pick
of the root or the strongest chord tone. At maximum it is uniform and all weighting is
erased. This is what makes Chance feel like a model rather than dice, and it sits on
the precedent already set by `scaleCompliance` in `src/ChordGen.h`, whose four
weighted tiers open as compliance drops and whose roulette pick never hard-excludes
anything.

**Wander** (0-100%, default 60) crossfades independent draws against a correlated walk.
Voss and Clarke measured real melodic lines at approximately 1/f, between flat white
noise and the 1/f² of a plain Brownian walk, and the difference is audible in exactly
the way the spectra predict: white noise is jagged and directionless, a Brownian walk
drifts aimlessly across a phrase, 1/f is correlated over short spans without long-range
drift.

**Not literal Voss-McCartney, deliberately.** That algorithm redraws register *k* every
2^k steps off a monotone counter, and a monotone counter is exactly the thing that must
not exist here: it would keep advancing while the deja-vu loop was locked, so a locked
loop would not repeat, which is the one promise this module cannot break. Instead the
walk is three one-pole filters with time constants of 2, 8 and 32 steps, each fed the
step bundle's own white draw. Summing poles at octave-spaced time constants is the same
construction in filter form, it approximates the same band, and because its only input
is the bundle it stays inside the lock: when the loop repeats, so does the contour.

## Key and Chord Pull: two adherence axes

**Key** (0-100%, default 70) is Marbles' quantizer reimplemented, and it is a better
mechanism than a linear blend between chromatic and diatonic. Each scale degree
carries a 0-255 weight. Eight hysteresis states apply rising thresholds
`{0, 16, 32, 64, 128, 192, 255}`; any degree whose weight falls below the current
threshold is masked, and the drawn pitch snaps to the nearest survivor. Turning the
slider up therefore collapses the available material in stages: all twelve notes, then
scale tones, then the strong degrees, then root and fifth, then octaves. Marbles
specifically promotes the second-highest-weighted degree (usually the fifth) into the
second-to-last state once its weight exceeds 192, which is the actual reason its
high-end behaviour sounds deliberate rather than degenerate. Worth copying.

Degree weights come from `keys::modes` in `src/ScaleModes.h`, which already answers
"what quality sits on this degree" and cross-references the kit's scale table via
`kitScaleIndex(modeIndex)`. That header exists precisely because generation needs a
quality per degree where Scale Lock only needs a membership test, so it is the right
home for a weight per degree too.

**Chord Pull** (0-100%, default 50) is a second, independent axis: how strongly the
draw prefers tones of the currently held or locked chord over other in-key tones.
Keys has a live chord where Marbles has none, from the pads, the live card, and a
chord held into the arp. This is the one control in the set with no hardware
antecedent, and it is the most Keys-specific thing here.

## Learn

Marbles' scale recorder is online histogram clustering of pitches actually played:
fold each note to a single octave, merge it into an existing degree if within about
28 cents, open a new degree otherwise, and set each weight to
`round(255 x count / max_count)`. That is the Dirichlet pseudo-count idea from the
machine-learning literature wearing different clothes, and in twelve-tone-equal
temperament the clustering step disappears entirely: it is a twelve-bin histogram.

Keys is unusually well placed for this. `watchInputNotes()` already records which
pitches the incoming MIDI stream turns on, and there is an on-screen keybed. So Learn
is: arm it, play a few bars on the keybed or a physical keyboard, and the histogram
becomes the degree weights that Key quantizes against and Temperature samples from.
Counts decay before each increment (`α_i *= d`, `0 < d < 1`) so the model tracks what
you are playing now rather than everything you have ever played. Strict
Dirichlet-multinomial conjugacy has no decay term; this is a deliberate departure,
and the reason is that a player is not a stationary process.

Learn is genuinely learning, costs a twelve-element array, and is the answer to "where
is the machine learning" that does not involve shipping a network. The learned table
is state, serialized with the seed.

## Freeze to slot

The arp's twelve slots already carry lane data *and* a chord, a shape and a rate.
Freeze captures the generated phrase into a slot, so what Chance just improvised
becomes a deterministic pattern you can launch, edit, and keep.

This is the strongest idea in the plan and the one no hardware module can do. Marbles
can lock a loop but cannot hand it to you as an editable object. Here the generative
brain becomes a phrase composer feeding an arp that already knows how to play,
serialize and launch phrases, and the whole thing is one left-click.

**The mapping is exact, which is the pleasant part.** `seq[i]` is
`{ sorted-held-index i % heldCount, octave (i / heldCount) * 12 }`, so a chosen pool
index decomposes into precisely the note lane's 1..8 chord-tone index and the octave
lane's added octaves. Velocity, gate and ratchet land in their own lanes as percentages.
Nothing about the phrase is approximated on the way in, and a test decodes the captured
lanes back and compares them against the pitches that actually sounded.

A rest is captured as a probability of 0, not as a muted note, so the note the step
*would* have played survives the freeze and lifting that lane brings it back.

The capture is indexed by the deja-vu loop length rather than by a rolling cursor, so
freezing a locked loop yields exactly that loop with step 0 of the pattern at step 0 of
the phrase.

**It writes to the active slot, and says so.** The button reads "Freeze to 3" rather
than arming a slot click the way Copy and Clear do. Binding to a chosen slot needs a
target picker, and a picker is the thing that turned Send to arp slot into this
plugin's one right-click-only path; a button that names its destination is one click and
needs no such exception. A picker can follow if choosing the slot turns out to matter.

## Modes

Two selectors, both big buttons rather than combo boxes, since they have three states
each and the mouse-only contract prefers a visible set to a dropdown.

**Rhythm**: Coin / Euclid / Bursts, named for what they do here rather than for what
Marbles calls them.

- **Coin** is Bernoulli per step at Density. Marbles' Complementary Bernoulli collapses
  to exactly this when there is one output rather than two, which is the case here.
- **Euclid** is E(k, 16) with *k* morphed by Density, so it stays even at every setting.
- **Bursts** is Euclid whose onsets may ratchet into 2-4 sub-hits, which is what Marbles'
  Clusters mode is really doing when it makes integer multiples of the clock.

Marbles' third mode, Drums, replays eighteen stored eight-step kick and snare tables.
That has no meaning for a melodic arpeggiator and is not reproduced. Note also that the
Marbles *manual* numbers rather than names its modes, and its firmware carries an
inactive Independent Bernoulli, a plain Divider and an unused `MARKOV` mode that never
reach the panel: firmware enums and panel behaviour are not the same list.

**Voice**: Line / Duet / Cluster. Marbles' IDENTICAL, BUMP and TILT are about
distributing three CV outputs, using a per-channel multiplier
`0.5 + (param - 0.5) x amount`. Keys has notes, not CV, so the same idea recast as
voice count: one line, two notes mirrored about Bias, or a chord burst.

## Mouse-only interaction

Nine continuous controls follow the ArpPanel idiom exactly, which is a plain
`juce::Slider` in `RotaryVerticalDrag` with `TextBoxBelow` at 52x16, bound by a
`SliderAttachment`, styled by the global `KeysLookAndFeel`. Not `KnobBank`'s
`okstudio::RotaryKnob`: that is a different pattern for a different job.

Every control is a single left-click or drag, targets at 34 px or larger, no
double-click, no modifiers, no keyboard. Three buttons: **Generate** (new seed),
**Learn** (arm the histogram), **Freeze** (capture to a slot, arming a slot click the
way the arp's Copy and Clear already arm). Generate, Learn and Freeze belong to the
content and travel into the detached window; Chance On belongs to the editor and stays
on the bar.

Deja Vu and Bias are the two bipolar controls and both get a centre detent, since in
both cases the centre is the musically load-bearing position.

No right-click paths. The chord-pad and arp-slot menus are the negotiated exceptions
and this section adds none.

## Parameters

Appended to the end of the layout in `src/PluginProcessor.cpp`, following the
precedent comment already there for `bpm`: appending leaves every existing id
unnumbered-shifted and saved sessions load unchanged. Percentage controls are
`AudioParameterInt` over 0-100, matching `arpChance` and `arpGate`; no skews, since
nothing in this file uses one.

| ID | Type | Range | Default |
|---|---|---|---|
| `chanceOn` | Bool | | false |
| `chanceDensity` | Int | 0..100 | 50 |
| `chanceDejaVu` | Int | 0..100 | 50 |
| `chanceLoopLen` | Choice | 1,2,3,4,6,8,12,16 | 8 |
| `chanceJitter` | Int | 0..100 | 0 |
| `chanceSpread` | Int | 0..100 | 50 |
| `chanceBias` | Int | -100..100 | 0 |
| `chanceTemp` | Int | 0..100 | 35 |
| `chanceWander` | Int | 0..100 | 60 |
| `chanceKey` | Int | 0..100 | 70 |
| `chanceChord` | Int | 0..100 | 50 |
| `chanceTMode` | Choice | Coin, Euclid, Bursts | Coin |
| `chanceXMode` | Choice | Line, Duet, Cluster | Line |
| `chanceLearn` | Bool | | false |

Deja Vu is encoded 0..100 with the frozen loop at 50, not as a signed -100..100. The
engine's curve `p = (2d - 1)²` is written against a unipolar 0..1, and a signed
parameter would only move that conversion somewhere less obvious. It is still a
centre-detented control on screen.

Seed and the learned weight table are not parameters. They are state, in their own
`chance` child of the session tree beside the arp's.

**The naming collision, fixed.** The arp's global `arpChance` knob was labelled
"Chance", and a section named Chance beside a knob named Chance is confusing. It now
reads "Trig". Display name only: the id is unchanged.

**Appending is safer than the invariant suggests.** "Parameter-layout changes break
saved sessions" is about removing or renaming ids. JUCE derives a VST3 parameter's id by
hashing its string id, so appending moves only the order a host's generic parameter list
shows. The comment already in `createLayout()` about `bpm` says so, and it is worth
knowing before anyone refuses to add a control.

## What this deliberately does not do

- **No second clock.** Stated above, restated here because it is the invariant most
  likely to be violated by a well-meaning refactor.
- **No neural network.** Nothing ships weights. Piano Genie is worth naming as an
  influence, since compressing a performance to a handful of discrete inputs is
  mouse-only-shaped thinking, but its cheap approximation is a cursor over the chord's
  pitch list, which is what Spread and Bias already are.
- **No top-k or top-p sampling.** Top-p is the better musical knob of the two, since
  it self-narrows when one chord tone dominates and widens when harmony is ambiguous,
  but Temperature plus the Key quantizer already covers that ground and a third
  sampling control would not earn its panel space.
- **No `Y` channel.** Marbles' fourth stream is reached by holding a button while
  turning other knobs, which is precisely the hidden modifier gesture the Keys
  contract forbids.
- **No conditional step chaining.** Stochas' Chain ("if this step plays, always play
  that one") is real prior art from another JUCE plugin and a genuinely distinct
  concept, but it belongs to an authored pattern, not a generative pool.
- **No scale learning by pitch-CV jack.** Obviously, but noted because it is how
  Marbles does it and the mechanism is what Learn borrows.

## Prerequisites and risks

**The kit merge is a blocker, not a detail.** `../okstudio-juce-kit` is currently
checked out on branch `strata-engine`, which carries `include/okstudio/Polyrhythm.h`
and three commits absent from `main`. Keys compiles against that working tree locally
but CI would fail the moment `ChanceEngine.h` includes it. That merge also brings the
Obsidian look-and-feel promotion and an ASIO cmake change affecting the whole product
line, so it is its own reviewed step and lands first.

**Parameter layout changes break saved sessions**, per the invariant. Appending is the
mitigation; a loud `CHANGELOG.md` entry under `[Unreleased]` is the obligation.

**Determinism is testable and must be tested.** `tests/ChanceTests.cpp` pins: same
seed and step give the same note at every block size; Deja Vu at centre repeats
exactly over Length steps; Deja Vu at 100% is a permutation of the frozen set, never
new material; Key at maximum yields only root and fifth; Temperature at 0 is
single-valued. Strata's `tests/BrainTests.cpp` already asserts the seed-replay
property and is the model to copy.

**Stuck notes are the failure mode to fear.** Chance adds a note *source*, and
CLAUDE.md is explicit that any new chord source has to go through
`noteOn`/`noteOff` and be reachable by `Exclusive` via `stopAllChordPads`. Chance
rides the arp's existing emission path rather than opening a new one, which is why,
but the regression test belongs in the suite regardless.

## Build order, and where it got to

Built on branch `chance-probabilistic-module`.

1. **Done.** `src/ChanceEngine.h` and `tests/ChanceTests.cpp`, wired into
   `ArpEngine::fireStep()` as a third note source, with integration tests in
   `tests/ArpTests.cpp`.
2. **Done.** The fourteen parameters and the processor wiring.
3. **Done.** Wander, Jitter, Learn.
4. **Done.** Freeze to slot, including the capture round-trip test.
5. **Done.** `ChancePanel`, the section table entry, the bar and its On.
6. **Done.** MCP tools, so the thing can be driven headlessly.
7. **Done.** `CHANGELOG.md` and this document.
8. **Still owed.** The kit merge (see Prerequisites): CI pins the kit's `strata-engine`
   branch on this branch as a stopgap, with a `TEMPORARY PIN` comment on the step. That
   pin must go before this merges. `docs/CONTROLS.md` and screenshots via
   `scripts/capture-window.ps1 -WindowTitle "Keys Chance"` are also outstanding.

## What changed between the plan and the build

Recorded because the reasons generalise, and because a design doc that quietly agrees
with whatever got built is worth nothing.

- **Selection replaced invention**, which is the big one. See the section above. It
  removed a whole risk surface rather than managing it.
- **The Humanize conflict did not exist.** The plan resolved a clash between Chance's
  Jitter and the global Humanize. There was none to resolve: Humanize is applied inside
  `KeysProcessor::noteOn`, a path arp output never takes, and its timing half
  (`humanizeTime`, "Timing Spread") is retained-but-unread. Humanize is a velocity range
  and arp step timing has never been humanized, so Jitter is the only timing control in
  the chain by construction and needed no bypass.
- **Jitter's pull-back and Wander's Voss-McCartney registers were both dropped**, each
  because it assumed a free-running counter this architecture does not have. Details in
  their own sections.
- **Marbles' Drums mode went**, and the `t` modes are named for what they do here.
- **The Key ladder dropped Marbles' hysteresis.** Eight hysteretic states exist to stop a
  wobbling CV chattering across a threshold. Key is an integer knob and cannot wobble, so
  the hysteresis would only make a control whose position did not fully determine its
  behaviour, which is worse than useless here.
- **A transport-jump resync was added**, which the plan did not anticipate. Marbles never
  needs one because hardware has no transport. A plugin does, or a looped bar walks on
  instead of replaying, so `ArpEngine::process()` resyncs Chance where it already
  detects a jump to flush owed note-offs.
- **The Euclid mask keys off the host step index, not an internal counter.** Counting
  calls internally would start the pattern wherever the arp happened to be switched on
  and drift on every loop. "Grid-locked" has to mean locked to the host's grid.
- **Harmony is built per block**, with no timer, lock or seqlock. The plan assumed
  handing a message-thread-built table to the audio thread would need one. It does not:
  the interval table is static and taken by reference, the result is thirteen bytes by
  value, and the loops run over seven degrees and twelve pitch classes.
- **The engine primes itself at construction.** A default-constructed `DejaVuSequence`
  has an all-zero ring and hands back the same value every step, which looks like a dead
  engine rather than an unseeded one.
- **Freeze targets the active slot** rather than arming a slot click. See its section.
- **Learn became a plain toggle**: on follows what you play, off follows the key, one
  click either way and no invisible state.

## Research caveats carried forward

- The Marbles manual numbers its three `t` modes rather than naming them; the names
  used above come from the firmware enum, which also contains modes not wired to the
  panel. Do not treat panel behaviour and firmware enums as the same list.
- Marbles' DEJA VU reads as linear in the manual's prose and is quadratic in the
  firmware. The firmware is the authority.
- Marbles' `Y` clock divider default reads as 1/8 in the firmware; some secondary
  sources claim 1/16. Unverified, and irrelevant here since `Y` is out of scope.
- Qu-Bit Bloom's exact branch-count ceiling, Nerdseq's manufacturer attribution, and
  whether Stochas implements Elektron-style Nth-pass conditions are all unverified.
  None affects anything specified above.
- Ports from other people's designs are not transcriptions, per CLAUDE.md's standing
  note about Octavium. Check any borrowed logic against the invariants before
  trusting it.

## Sources

Marbles manual, <https://pichenettes.github.io/mutable-instruments-documentation/modules/marbles/manual/>.
Marbles firmware (MIT), <https://github.com/pichenettes/eurorack/tree/master/marbles/random/>,
specifically `t_generator.{h,cc}`, `random_sequence.h`, `distributions.h`,
`output_channel.cc`, `x_y_generator.{h,cc}`, `quantizer.{h,cc}`, `scale_recorder.h`,
`marbles.cc`, `settings.{h,cc}`.
Turing Machine, <https://www.musicthing.co.uk/Turing-Machine/> and
<https://github.com/TomWhitwell/TuringMachine>.
Voss and Clarke, "1/f noise in music and speech", Nature 258, 317-318 (1975),
<https://www.nature.com/articles/258317a0>.
Voss-McCartney implementation notes, <https://www.firstpr.com.au/dsp/pink-noise/>.
Softmax temperature, <https://jdhao.github.io/2022/02/27/temperature_in_softmax/>.
Nucleus sampling, Holtzman et al., <https://arxiv.org/abs/1904.09751>.
Piano Genie, Donahue, Simon and Dieleman, IUI 2019, <https://arxiv.org/abs/1810.05246>.
GrooVAE, Gillick et al., ICML 2019, <https://arxiv.org/abs/1905.06118>.
Dirichlet conjugacy, <https://stephentu.github.io/writeups/dirichlet-conjugate-prior.pdf>.
Ableton chance and velocity range,
<https://www.ableton.com/en/live-manual/11/editing-midi-notes-and-velocities/>.
Cthulhu Rand Sel, <https://medias.audiofanzine.com/files/cthulhu-manual-475318.pdf>.
Stochas, <https://stochas.org/stochas/>.
