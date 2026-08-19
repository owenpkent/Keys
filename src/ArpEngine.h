#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <limits>
#include <random>
#include <utility>

namespace keys
{
// The arpeggiator core: pure, UI-free, and allocation/lock-free on the audio
// thread (see docs/ARP_DESIGN.md). It consumes note on/off from the block's merged
// MIDI stream as its input (keyboard, latch, chord pads all arrive the same way),
// holds the sounding set, and emits its own stream; everything else in the stream
// passes through untouched.
//
// Lane data is written by the editor on the message thread and read here on the
// audio thread; every cell is an atomic int, so there is never a lock and never a
// torn read that matters (a half-updated pattern is just a pattern mid-edit).
//
// Timing: with a playing host transport, step boundaries are derived fresh every
// block from ppqPosition and bpm, so tempo changes and transport jumps self-correct
// (no accumulating counters). Anchored mode affixes steps to the host bar grid;
// free mode (and the internal clock used when the transport is stopped or absent)
// advances its own phase and is allowed to drift, by design.
class ArpEngine
{
public:
    static constexpr int maxSteps = 32;
    static constexpr int maxHeld = 16;
    // Sounding arp notes awaiting their note-off. Raised from 64 with the Chord shape and
    // the Harmony lane, which between them can put a spread chord's every note on one step.
    static constexpr int maxActive = 128;
    static constexpr int numLanes = 14;

    // The four after laneProbability arrived 2026-07-30. Appended, like everything else in
    // that round: a slot's lane data is serialized by index, so inserting would silently
    // reinterpret every pattern in every saved session.
    // **Append only.** A lane's index is what a saved session stores it under (see
    // arpLineToTree), so inserting one would silently move every older session's lane data.
    // Lanes 10 and 11 arrived 2026-08-14 and an older session simply has no child for them,
    // which reads back as ArpPattern's laneDefaults - inert, both of them.
    enum Lane { laneNote = 0, laneOctave, laneVelocity, laneGate, laneRatchet, laneProbability,
                laneTranspose, laneLate, laneHarmony, laneChord,
                // Cthulhu's "Rand Sel" (its manual, p25): **how random each step is**, drawn
                // per step rather than set by one knob for the whole line. Bipolar - above zero
                // the played entry may come out higher than the one drawn in the Note lane,
                // below zero lower, and the size is how far. Zero is exactly what is drawn.
                //
                // This is the one randomness in Keys that *is* allowed to change which note
                // plays, and the reason is that you drew it on that step: it is intent, where
                // Drift is a knob wandering over a part you did not aim at. See laneDrifts.
                laneRand,
                // The mute row's own lane. It used to write -1 straight into the Note lane,
                // which destroyed whatever that step held - Cthulhu's manual names preserving
                // it as the point of having mute buttons at all ("you can experiment with
                // mutes, without losing the set-value of the steps"). Note keeps its value now
                // and this says whether the step is heard. Note = -1 is still a *drawn* rest,
                // which is a different thing and stays: Cthulhu has both too.
                laneMute,
                // Stochas' chain dependency (its manual p3: a cell "will play or not play
                // depending on whether another cell has played"), reduced to the one form that
                // needs no second coordinate: 0 always, 1 only if the step before it fired,
                // 2 only if the step before it did not. Chance says "maybe"; this says "only
                // if", which is what turns a probabilistic pattern into one that answers itself.
                //
                // **It is the one thing in the engine that is not stateless from the playhead.**
                // Everything else here is computed from the step index alone so a transport jump
                // lands right without having walked there; a condition has to remember one bit
                // about the step before. It self-corrects within a single step, which is the
                // cheapest possible break of that rule and is why this form was chosen over
                // Stochas' arbitrary cell-to-cell reference.
                laneChain,
                // Cthulhu's **Position Reset** (its manual p25): *"the arpeggiator will reset on
                // this step to play the first note of the arpeggiator pattern"*. There it is an
                // alt-click marker on the Note graph; Keys' right-click list is closed and alt is
                // not a gesture it may require, so it is a lane - which is what this file already
                // said a per-step version would have to be.
                //
                // It resets the **walk**, not the lanes. `dirCursor = 0` only: the manual's own
                // example is about which note of the chord comes out, and zeroing `stepBase` here
                // would rebase the lanes onto this step, so the lane would read its own reset cell
                // for ever and the pattern would never move again.
                laneReset };

    // The value each lane holds when it is doing nothing. Also what every lane reads as
    // while Params::usePattern is false, which is how "Shape: Up" behaves like a plain
    // arpeggiator even after the step lanes have been edited.
    static constexpr int laneDefaults[numLanes] = { 0, 0, 100, 100, 1, 100, 0, 0, 0, 0, 0, 0, 0, 0 };

    // What each lane can hold, low and high. **One copy** (2026-08-14): the grid that draws a
    // lane, the reroll that randomizes one and the drift that strays from one all need these,
    // and three tables that must agree is three tables that will not. The numbers are the ones
    // ArpPanel's lane rows were built with; ChordTable::numSlots for the Chord lane, since that
    // is what it indexes.
    // The Note lane's vocabulary (2026-08-14, from Kirnu Cream's ORDER lane - its manual p9:
    // "Prev - Arp plays same note it played in previous step... Hi... Low... Rnd"). Keys' own
    // -1 and 0 keep their meanings and the modes were **appended above 8**, not below -1, for
    // two reasons: every saved session's values stay exactly what they were, and dragging a
    // cell to the bottom of the grid still reaches the rest rather than landing on a mode.
    static constexpr int noteRest = -1;      // a drawn rest; the step is silent
    static constexpr int noteFollow = 0;     // whatever the Shape's walk says next
    static constexpr int noteMaxFixed = 8;   // 1..8 are fixed sequence entries
    static constexpr int notePrev = 9;       // play what the last step that sounded played
    static constexpr int noteHi = 10;        // the highest note of the held chord, whatever it is
    static constexpr int noteLow = 11;       // ...and the lowest
    static constexpr int noteRnd = 12;       // any entry, drawn fresh each time
    // **Per-step shapes** (2026-08-18), from Cthulhu's Note graph - the feature that makes that
    // graph an arpeggiator you draw rather than a list of note numbers. Its manual p23: *"The
    // top-half of the graph is various arpeggiator patterns, which act like a typical
    // arpeggiator, where the note output varies consecutively one step after another."*
    //
    // Keys had exactly one shape, the line's own, and `noteFollow` to defer to it. These eight
    // let a step name a shape of its own and still advance the same walk, so a lane can run four
    // steps up, jump to the top note, and come back down - drawn, visible, and not random.
    // Ordered as Cthulhu lists them bottom-to-top above the fixed indices, so a drag up the
    // lane meets them in the manual's own order.
    static constexpr int noteShapeFirst = 13; // 13..20 name a Direction; see shapeForNoteValue
    static constexpr int noteShapeLast = 20;

    struct LaneRange { int lo, hi; };
    static constexpr LaneRange laneRanges[numLanes] = {
        { -1, 20 },  // Note: -1 rest, 0 follow the shape, 1..8 a fixed entry, 9..12 a mode, 13..20 a shape
        { -3, 3 },   // Octave
        { 10, 200 }, // Velocity, as a percentage of what was played
        { 5, 200 },  // Gate
        { 1, 4 },    // Ratchet
        { 0, 100 },  // Chance, per step - multiplied by the line's own Chance knob
        { -7, 7 },   // Transpose, in scale degrees
        { 0, 90 },   // Late, as a percentage of the step
        { 0, 7 },    // Harmony
        { 0, 12 },   // Chord: 0 is off, 1..12 call up that slot's chord
        { -8, 8 },   // Rand: how far this step's note selection may stray, and which way
        { 0, 1 },    // Mute: 1 silences the step without touching what it holds
        { 0, 2 },    // Chain: 0 always, 1 only after a step that fired, 2 only after one that did not
        { 0, 1 },    // Reset: 1 restarts the shape's walk on this step
    };
    // Stray from `value` by up to `reach`, staying inside `r`. `u01` is a draw in [0, 1).
    //
    // **The window slides; the result is never clamped** (2026-08-14, Owen: "0 value seems to
    // ignore roll"). Both Roll and Drift used to build [value - reach/2, value + reach/2] and
    // clamp whatever came out, which is fine in the middle of a lane and broken at its edges:
    // a lane sitting at 0 (Late, Harmony and Chord all default there, and Chance sits at its
    // *top*) had half of every draw fall outside the range and clamp straight back to the value
    // it started from. So half the steps did not move and the rest only moved one way, which is
    // exactly what "seems to ignore" looks like. Sliding the window keeps the full width of the
    // draw wherever the value sits, and only narrows it if the reach is wider than the lane.
    static int strayWithin(int value, double reach, LaneRange r, double u01) noexcept
    {
        double lo = (double) value - reach * 0.5;
        double hi = (double) value + reach * 0.5;
        if (lo < r.lo) { hi += (double) r.lo - lo; lo = (double) r.lo; }
        if (hi > r.hi) { lo -= hi - (double) r.hi; hi = (double) r.hi; }
        lo = juce::jmax(lo, (double) r.lo); // both, for a reach wider than the whole lane
        hi = juce::jmin(hi, (double) r.hi);
        return juce::jlimit(r.lo, r.hi, (int) std::llround(lo + u01 * (hi - lo)));
    }

    static LaneRange laneRange(int lane) noexcept
    {
        // ChordTable is declared below this point, so its count cannot appear in the
        // initializer above - a static-member initializer is not delayed the way a member
        // function body is. Asserting it here, where the body *is* delayed, is what keeps the
        // literal 12 honest if the table ever grows.
        static_assert(laneRanges[laneChord].hi == ChordTable::numSlots,
                      "the Chord lane's range must be the number of slots it indexes");
        return laneRanges[(size_t) juce::jlimit(0, numLanes - 1, lane)];
    }

    // Which lanes **Drift** is allowed to touch (2026-08-14, Owen: "there should be, like, a
    // more random feature in the drawing, like cthulu"). The rule is one line and it is the
    // whole design: **drift changes how a step plays, never which note it plays.** So the feel
    // lanes wander - octave, velocity, gate, lateness, transpose, chance - and the four that
    // choose *content* do not: Note picks the sequence entry, Ratchet subdivides it, Harmony
    // adds a voice and Chord calls up a whole chord, and a knob that quietly rewrote any of
    // those would be editing your part rather than performing it.
    //
    // Same split the chord generator already draws between Lean (which notes) and the voicing
    // passes (where they sit), and it is why Drift can be one knob instead of ten.
    static constexpr bool laneDrifts[numLanes] = {
        false, // Note
        true,  // Octave
        true,  // Velocity
        true,  // Gate
        false, // Ratchet
        true,  // Chance
        false, // Transpose - scale degrees, so this picks a different note
        true,  // Late
        false, // Harmony
        false, // Chord
        false, // Rand - drift must not rewrite how random you drew a step
        false, // Mute - nor silence a step you did not silence
        false, // Chain - a condition is structure, not feel
        false, // Reset - a restart you placed; a machine must not move it
    };

    // The Hz mode's range, which is not a round number by choice: it is exactly what the
    // eleven synced divisions span at 120 bpm. "1/64" is 32 steps a second and "16 bars" is
    // one step per 32 seconds, so a Hz mode narrower than this would reach less than the
    // list it sits beside. Ten octaves, and the parameter maps them exponentially, so each
    // one gets a tenth of the dial's travel (1 Hz - "1/2" at 120 bpm - lands dead centre as a
    // consequence, not as the thing being aimed at). Declared here rather than in the
    // parameter layout so the engine's clamp and the dial's ends are the same two numbers.
    static constexpr double minRateHz = 0.03125;
    static constexpr double maxRateHz = 32.0;

    // How a rate in Hz is written, wherever it is written: decimals by decade, so 0.031 Hz
    // and 32.0 Hz both read as themselves. A fixed 2 prints the bottom octave of the range as
    // "0.03", and a fixed 1 collapses the bottom *two* octaves onto "0.0" - which is how the
    // slot cards came to label a running arp as a stopped one. One copy of the rule, spent by
    // the arpRateHz parameter's text function and by ArpPanel's slot card; the unit suffix is
    // the caller's, since the two do not space it the same way.
    static juce::String rateHzText(double hz)
    {
        return juce::String(hz, hz < 1.0 ? 3 : (hz < 10.0 ? 2 : 1));
    }

    // A tuplet is "N steps in the space of M", and M is the largest power of two at or below N:
    // 3 in the space of 2 - the plain triplet this generalised on 2026-08-03 - then 5 in 4, 7 in
    // 4, 9 in 8. That is the notation convention, and it is why a quintuplet is measured against
    // four and not six: five steps fill exactly the time four straight ones would. Which also
    // makes the number alone enough to name one, and what lets rateSyncText below print the
    // result as a plain fraction instead of the division plus a marker.
    // Anything under 2 is straight, which is how the choice list's "Off" arrives here.
    static int tupletSpace(int n) { return n >= 8 ? 8 : (n >= 4 ? 4 : 2); }
    static double tupletFactor(int n) { return n < 2 ? 1.0 : (double) tupletSpace(n) / (double) n; }
    // Written out rather than pulled from <numeric>: one loop, and the header stays as light
    // as it is. Only rateSyncText uses it, to put a fraction in lowest terms.
    static int gcdOf(int a, int b)
    {
        while (b != 0)
        {
            const int t = a % b;
            a = b;
            b = t;
        }
        return a;
    }

    // Rhythm dividers (2026-08-14, Subharmonicon-style): whether raw step g is a hit for ANY
    // enabled divisor (value > 0) in `divs` - an OR of clocks, not a shared modulus, so {3, 4}
    // fires on 0, 3, 4, 6, 8, 9, 12... not only their common multiple. Pure and stateless in g,
    // so a transport jump lands on the right answer without having walked every step since 0.
    static bool dividerFires(long long g, const std::array<int, 4>& divs) noexcept
    {
        for (const int d : divs)
            if (d > 0 && g % d == 0)
                return true;
        return false;
    }

    // How many boundaries strictly before g are divider hits (dividerFires above), computed
    // from g alone - the same "stateless from ppq" contract the rest of the engine's timing
    // keeps (see the class comment). Inclusion-exclusion over the enabled divisors: for every
    // non-empty subset, count multiples of its lcm in [0, g) and sign the count by the parity
    // of the subset's size. At most 4 divisors means at most 15 subsets, walked with a bitmask
    // rather than recursion - allocation-free, as everything the audio thread reaches must be.
    static long long firedCountBefore(long long g, const std::array<int, 4>& divs) noexcept
    {
        if (g <= 0)
            return 0;
        long long total = 0;
        for (unsigned mask = 1; mask < 16u; ++mask)
        {
            long long l = 1;
            int bits = 0;
            bool valid = true;
            for (int i = 0; i < 4 && valid; ++i)
            {
                if ((mask & (1u << i)) == 0)
                    continue;
                if (divs[(size_t) i] <= 0)
                {
                    valid = false; // a subset touching a disabled slot is not a real subset
                    break;
                }
                const int g2 = gcdOf((int) l, divs[(size_t) i]);
                l = l / g2 * divs[(size_t) i];
                ++bits;
            }
            if (! valid || bits == 0)
                continue;
            const long long count = (g - 1) / l + 1;
            total += (bits & 1) ? count : -count;
        }
        return total;
    }

    // How a tempo-synced rate is written under the dial: **the step length as an exact fraction
    // of a bar**, one copy of the rule for the band and for every macro card.
    //
    // This is the one notation that survives tuplets (2026-08-03, Owen: "shouldn't it just be
    // 1/5 not 1/4:5?", and he is right - `1/4:5` was invented here, which is the tell). The
    // convention every DAW uses, `1/16T` and `1/16D`, has a letter for triplets and dotted and
    // *no form at all* for a quintuplet; the fraction needs no letters, because a quarter-note
    // quintuplet is five in the space of four quarters, which is four fifths of a beat, which
    // is one fifth of a bar. So it is simply "1/5". FL Studio's grid ("1/3 beat", "1/6 beat")
    // is the same system.
    //
    // 4/4 is assumed exactly as much as it already was: "1/4" has always meant a quarter of a
    // bar here. Straight, this reproduces the division names byte for byte, which is why the
    // dial reads the same as it ever did until a modifier is set.
    //
    // Dot stays a dot rather than folding in. It *could* - a dotted 1/8 is 3/16 of a bar - but
    // "1/8." is universal and instantly read, where "3/16" has to be worked out. The tuplet is
    // folded because it has no such symbol.
    static juce::String rateSyncText(int rateIndex, bool dotted, int tuplet)
    {
        static constexpr int barsNum[11] = { 16, 8, 4, 2, 1, 1, 1, 1, 1, 1, 1 };
        static constexpr int barsDen[11] = {  1, 1, 1, 1, 1, 2, 4, 8, 16, 32, 64 };
        const int i = juce::jlimit(0, 10, rateIndex);
        int num = barsNum[i], den = barsDen[i];
        if (tuplet >= 2)
        {
            num *= tupletSpace(tuplet);
            den *= tuplet;
        }
        // Reduce, or an 1/8 in threes prints as "2/24" instead of "1/12". Most combinations
        // come back to a unit fraction; a few honestly do not (a 1/2 in fives is two fifths of
        // a bar) and print as "2/5", which is exact and still reads as a length.
        if (const int g = gcdOf(num, den); g > 1)
        {
            num /= g;
            den /= g;
        }
        juce::String s = den == 1 ? juce::String(num) + (num == 1 ? " bar" : " bars")
                                  : juce::String(num) + "/" + juce::String(den);
        if (dotted)
            s << ".";
        return s;
    }

    // The chords the twelve slots hold, mirrored into atomics so a step can call one up on
    // the audio thread. The processor owns it and refreshes it on the message thread
    // whenever a slot's chord changes; the engine only ever reads.
    struct ChordTable
    {
        static constexpr int numSlots = 12;
        static constexpr int maxNotes = 8;
        std::array<std::array<std::atomic<int>, maxNotes>, numSlots> note {};
        std::array<std::atomic<int>, numSlots> count {};
    };

    // Per-step values, editor-writable. Meanings per lane:
    //   note:        0 = follow the direction mode, 1..8 = fixed chord-note index,
    //                -1 = muted step
    //   octave:      -3..+3 (added octaves)
    //   velocity:    10..200 (% of the played velocity)
    //   gate:        5..200 (% of the step length; >100 overlaps into the next step)
    //   ratchet:     1..4 sub-hits within the step
    //   probability: 0..100 (% chance the step fires)
    // Kirnu's Loop direction (its manual p11): "Up: from note list start to end", "Down: end
    // to start", "Up alt: start to end and back", "Down alt: end to start and back". The alt
    // pair does not repeat its turning points - the bounce period is 2*span - 2 - because a
    // doubled step at each end is audible as a stutter and is not what a bounce means.
    enum LaneDir { dirUp = 0, dirDown, dirUpAlt, dirDownAlt, numLaneDirs };

    // Everything that decides where a lane reads, gathered so the engine and the UI can be
    // handed the same thing rather than seven loose ints in an order either could get wrong.
    struct LaneShape
    {
        int len = 8;
        int div = 0;
        int loopFrom = 0;
        int loopTo = maxSteps - 1;
        int dir = dirUp;
    };

    struct Lanes
    {
        std::array<std::array<std::atomic<int>, maxSteps>, numLanes> value;
        std::array<std::atomic<int>, numLanes> length;   // 1..32
        std::array<std::atomic<int>, numLanes> clockDiv; // 0 = every step, 1 = every 2nd, 2 = every 4th

        // Kirnu Cream's per-control on/off (its manual p12: "when control is off all it's
        // values are ignored"). 1 = read, 0 = the lane returns its default and the drawing is
        // left exactly where it is. Keys had no way to take a lane out: Reset flattens it,
        // which is the same sound and loses the work - the very trap Cthulhu's mute-preserves-
        // value rule already fixed one level down, on a single step. Same contract as the rest
        // of this struct: message thread writes, audio thread reads, plain atomics, no lock.
        std::array<std::atomic<int>, numLanes> on;

        // Kirnu Cream's per-control loop (its manual p11: "Every step control have their own
        // loop control. This enables playing different controls independently from other
        // controls"), plus its Loop direction. Keys had length alone - every lane started at
        // step 0 and walked forwards - and length alone is polymeter without phrasing: three
        // lanes of eight can only ever be three lanes of eight in step. A window and a
        // direction are what make them say something different on every pass while staying
        // completely predictable by ear, which is the whole reason Cream sounds composed.
        //
        // `loopTo` defaults past the end on purpose: clamped against the length at read time,
        // maxSteps - 1 means "to whatever the end is now", so nudging a lane's length does not
        // have to rewrite its loop points and a lane that has never been touched behaves
        // exactly as it did before any of this existed.
        std::array<std::atomic<int>, numLanes> loopFrom;
        std::array<std::atomic<int>, numLanes> loopTo;
        std::array<std::atomic<int>, numLanes> dir; // LaneDir

        Lanes() { resetToDefaults(); }

        LaneShape shapeOf(int l) const
        {
            const auto li = (size_t) l;
            LaneShape sh;
            sh.len      = juce::jlimit(1, maxSteps, length[li].load(std::memory_order_relaxed));
            sh.div      = juce::jlimit(0, 2, clockDiv[li].load(std::memory_order_relaxed));
            sh.loopFrom = loopFrom[li].load(std::memory_order_relaxed);
            sh.loopTo   = loopTo[li].load(std::memory_order_relaxed);
            sh.dir      = dir[li].load(std::memory_order_relaxed);
            return sh;
        }

        void resetToDefaults()
        {
            for (int l = 0; l < numLanes; ++l)
            {
                for (auto& v : value[(size_t) l])
                    v.store(laneDefaults[l], std::memory_order_relaxed);
                length[(size_t) l].store(8, std::memory_order_relaxed);
                clockDiv[(size_t) l].store(0, std::memory_order_relaxed);
                on[(size_t) l].store(1, std::memory_order_relaxed);
                loopFrom[(size_t) l].store(0, std::memory_order_relaxed);
                loopTo[(size_t) l].store(maxSteps - 1, std::memory_order_relaxed);
                dir[(size_t) l].store(0, std::memory_order_relaxed);
            }
        }
    };

    // The four after asPlayedReverse were appended in 2026-07-30, deliberately at the end:
    // `arpDirection` is a choice parameter and inserting anywhere else would renumber the
    // shapes every saved session already carries. `chord` is the odd one out - it is not a
    // walk order at all but "play the whole held chord on every step", which turns the arp
    // into a comping engine (Ableton calls it Chord Trigger).
    // fingeredBottom / fingeredTop appended 2026-08-18, from Cthulhu's Note graph (its manual
    // p24): *"fingered top - every 2nd note is the high note of the chord"*, and its mirror.
    // Appending is safe and is the only safe direction: a slot stores its shape as this index
    // and `shapeBase` in the arp tree records what numDirections was when it was written.
    enum class Direction { up = 0, down, upDown, downUp, upAndDown, downAndUp, asPlayed, asPlayedReverse,
                           random, randomOther, randomOnce, chord, fingeredBottom, fingeredTop };
    static constexpr int numDirections = 14; // keep in step with Direction; the Shape combo lists these then "Pattern"

    // Which Direction a Note lane value 13..20 names. Cthulhu's own order read upward from the
    // fixed indices (its manual p24, listed there top-to-bottom): up, down, up/down, down/up,
    // up and down, down and up, fingered bottom, fingered top. Keys' `upDown` sounds each
    // extreme once and `upAndDown` sounds it twice, which is exactly the distinction Cthulhu
    // draws between "up/down" and "up and down", so the two vocabularies line up with no
    // translation.
    static Direction shapeForNoteValue(int v) noexcept
    {
        static constexpr Direction table[] = {
            Direction::up, Direction::down, Direction::upDown, Direction::downUp,
            Direction::upAndDown, Direction::downAndUp,
            Direction::fingeredBottom, Direction::fingeredTop,
        };
        static_assert(sizeof(table) / sizeof(table[0]) == noteShapeLast - noteShapeFirst + 1,
                      "every per-step shape value needs a Direction");
        return table[(size_t) juce::jlimit(noteShapeFirst, noteShapeLast, v) - noteShapeFirst];
    }

    struct Params
    {
        bool enabled = false;
        int rateIndex = 8;        // index into rateInBeats(), default 1/16
        // The rate as a frequency instead of a division. True and the engine free-runs at
        // `rateHz` always - transport rolling or stopped, anchored or not - and neither the
        // playhead, `rateIndex`, `dotted` nor `tuplet` is read for step timing. There is
        // still only one scheduler: the mode picks which clock drives it (see process()).
        bool rateFree = false;
        double rateHz = 8.0;      // 8 Hz is 1/16 at 120 bpm, so the default is the same speed
        bool dotted = false;
        // 0 (or 1) straight, otherwise N-in-the-space-of-tupletSpace(N): 3 is the old triplet
        // toggle, 5 and 7 are what it could never reach. Dotted is a separate axis on purpose -
        // it lengthens a step by half, where a tuplet divides a span into an odd number of them,
        // and the two compose (a dotted 1/8 quintuplet is a legitimate, if unhinged, ask).
        int tuplet = 0;
        bool anchored = true;     // affixed to the host bar grid vs free-running
        Direction direction = Direction::up;
        bool usePattern = false;  // false: plain arpeggiator, the step lanes are not read
        int octaveRange = 1;      // 1..4, for direction modes
        // -0.75..0.75, shifts the odd (offbeat) steps by that fraction of a step: positive
        // late, the usual shuffle; negative early, which rushes them on top of the beat.
        float swing = 0.0f;
        // Gate and Chance as globals, not only as step lanes. The lanes are gated behind
        // Shape == "Pattern", so on a plain shape there was no way to shorten a note or
        // thin a run out at all; these multiply the lane value, so 100 leaves an edited
        // pattern exactly as drawn and the two controls mean the same thing either way.
        int gate = 100;           // 5..200 (% of the lane's own gate)
        int chance = 100;         // 0..100 (% of the lane's own probability)
        bool latch = false;
        bool retrigger = true;    // restart at step 1 when a note arrives on an empty set
        // Restart every N beats as well, 0 = off (Ableton's Retrigger: Beat). Retrigger-on-
        // note answers "a new chord starts the pattern over"; this answers "the pattern is
        // one bar long whatever the lanes say", which is the other half of the same control.
        double retrigBeats = 0.0;
        // Where the pattern starts: rotates both the lane read index and the direction walk
        // by this many steps, so the same lanes can be heard from a different foot.
        int offset = 0;           // 0..31
        // What each repeat past the first adds, in semitones - or in scale degrees when
        // `spreadDegrees` is set, which is the thing the stock arps cannot do and Keys can,
        // because Root and Scale are already its own. 12 semitones is the octave stacking
        // every arp defaults to, and is what `octaveRange` alone used to mean.
        int spread = 12;
        bool spreadDegrees = false;
        int rootPc = 0;           // for degree walking
        // Bit k set = the pitch class (rootPc + k) % 12 is in the current scale. Passed in
        // rather than looked up so the engine stays free of the scale tables (and testable).
        unsigned int scaleMask = 0xFFFu;
        // Velocity ramp: over `rampBeats` from the moment a chord starts, the played
        // velocity scales toward (100 + velRamp)%. Negative fades a run out, positive swells
        // it. 0 is off, and costs nothing.
        int velRamp = 0;          // -100..+100
        double rampBeats = 8.0;
        // "Played, not programmed", split into its two halves on 2026-08-02 (Owen: "maybe we
        // could split it up into two knobs"). `humanize` nudges each hit late; `humanVel`
        // wanders its velocity - each scaled by its own knob, each a different random draw per
        // hit. Before the split one value drove both, so a session saved then keeps its amount
        // as the timing half and gets 0 for the velocity half. **Both knobs are the centre of
        // their wander since 2026-08-19** (Owen, on the halo: "should be equal from center"):
        // the draw lands either side of the knob, equally, with the reach stopped where a rail
        // is nearer - see the two draws in fireStep for the exact arithmetic.
        int humanize = 0;         // 0..100, timing only - the typical lateness, on the 25 ms scale
        // Velocity only, in **MIDI velocity units** since 2026-08-18: how far either side of
        // `velLevel` a hit may land, so the knob and its ring read as one 0-127 band. 0 is no
        // wander, which is what it always meant.
        int humanVel = 0;         // 0..127, velocity units either side of velLevel
        // The spans, appended 2026-08-03 with the range knobs (Owen: "a serum style knob where
        // you can set a range in the knob"). Each hit draws uniformly between `humanize - span`
        // and `humanize` instead of between zero and `humanize`, so the knob keeps meaning "the
        // most this ever does" and the span says how far under it the draw may fall. **The
        // range travels with the knob**, which is the behaviour Serum's mod ring has and the
        // half Owen asked for by name: turn the dial and both ends move together.
        //
        // Default 100 is what makes them safe to append - a span of the whole scale puts the
        // floor at zero for any knob position, which is the behaviour these two had alone.
        int humanizeSpan = 100;   // 0..100
        int humanVelSpan = 100;   // 0..100
        // Drift (2026-08-14, Owen: "there should be, like, a more random feature in the
        // drawing, like cthulu"). Strays from what the lanes hold *while it plays*, by up to
        // this percentage of each lane's own range - so the part never repeats exactly and the
        // lane on screen never changes. Roll is the other half of the same ask and is its
        // opposite in every way: one reroll, visible, permanent.
        //
        // Only the lanes `laneDrifts` allows, which is the whole design in one line: **drift
        // changes how a step plays, never which note it plays.** 0 is the engine bit-identical
        // to before, which is what makes it safe to append.
        int drift = 0;            // 0..100

        // Mutate (2026-08-18): how far the run explores *other notes of the held chord*, and
        // how long it keeps what it finds. See `mutatedIndex`. Both 0 is the engine unchanged.
        //
        // Three zones since 2026-08-19 (Owen: "higher values can go out of scale"): to 50 the
        // knob is exactly the 2026-08-18 control and cannot leave the chord; past 50 a mutated
        // step may stray a scale degree or two off the chord note instead; past 75 some of
        // those strays are chromatic semitones. See `mutatedPitch` for the second stage.
        int mutate = 0;     // 0..100
        int mutateLock = 0; // 0..100; 100 = the first variation found repeats for good
        int mutateSeed = 0; // the line index, so two lines never explore in lockstep
        // The line's two fixed harmony voices (2026-08-19, BigSky's shimmer list): an interval
        // in semitones added to every note the step resolved - Mutate's stray included, so the
        // voice follows the run - and how often it fires, rolled per step per voice off the
        // same stateless hash Mutate draws from. 0 semitones is Off. Chromatic on purpose;
        // the dropdown names intervals, not degrees (see the registration in PluginProcessor).
        int harmSemis[2] = { 0, 0 };
        int harmChance[2] = { 100, 100 };
        // Move the whole run up or down whole octaves. Distinct from `octaveRange`, which
        // *stacks* copies upward and can only widen: this transposes, so it is centred at 0
        // and goes both ways (2026-08-02, Owen: "the octave should start in the middle so you
        // can go up or down"). It folds into the Octave lane's own shift, so a lane that
        // already moves a step keeps doing it relative to wherever this put the run.
        int octShift = 0;         // -3..+3, in octaves
        // Output level for the whole line, as a percentage of the velocity it would have
        // played. The plain volume control an arpeggiator wants and Keys never had: with two
        // lines running, balancing them was previously only possible by playing one softer.
        // Kept for sessions saved before velTrim below; nothing in the UI writes it any more
        // and KeysProcessor::migrateVelTrim folds it into velTrim on load.
        int volume = 100;         // 0..100
        // `velTrim` was the control that replaced it on screen (2026-08-02) and `velLevel`
        // below replaced *it* on 2026-08-18. It has no member here: the engine's two readers
        // (the squared trim in the velocity path, and the velTrim <= -100 mute) both went with
        // that change, so a field would only have been an atomic loaded per line per block to
        // feed nothing - and, worse, a live-looking number the next velocity feature would
        // reasonably assume was wired up. The APVTS parameter stays registered so saved
        // sessions keep round-tripping; KeysProcessor::migrateVelLevel is what reads it, once,
        // on load. Keeping a parameter for compatibility does not mean feeding it to the engine.
        // **The absolute level that replaced it** (2026-08-18, Owen, on the readout reading
        // "-31 ~20": "still wrong", having just asked for velocity ranges to span 0-127).
        // VelTrim was a *trim* on the velocity that arrived, so its numbers were percentages
        // sitting on a control labelled VEL, next to a pads knob that had just become an
        // absolute 0-127 band. Two velocity controls in two units is one too many.
        //
        // This is MIDI velocity outright: the top of the band this line plays at, whatever
        // velocity the chord arrived with. `humanVel` is how far under it a hit can fall, in the
        // same units, so the pair reads as one band the way the pads' Humanize knob does. 0 is
        // silence, exactly as velTrim -100 was.
        //
        // What it costs is "as played" - a line following the velocity of the chord fed to it -
        // and with one mouse that was a constant anyway: every chord Keys fires leaves at the
        // pads' own Humanize velocity. velTrim stays registered for saved sessions and is read by
        // nothing; KeysProcessor::migrateVelLevel folds it into this on load.
        int velLevel = 100;       // 0..127, MIDI velocity
        double fallbackBpm = 120.0; // internal clock when the transport is stopped/absent
        // Tempo Sync (`bpmSync`, KeysProcessor::advanceChainClock/buildArpParams). True
        // reproduces exactly what Keys always did before this parameter existed: a rolling
        // host with a valid bpm wins over fallbackBpm below. False pins the engine to
        // fallbackBpm even while the host is rolling, the escape hatch for someone who wants
        // Keys' own clock regardless of what the DAW's transport says. Read only in Sync -
        // Hz already ignores the host clock outright, so this changes nothing there.
        bool followHost = true;
        // The slot chords, for the Chord lane. Null means the lane does nothing, which is
        // what every caller that has no slots (the tests) wants.
        const ChordTable* chords = nullptr;
    };

    struct HostClock
    {
        bool playing = false;
        bool hasPpq = false;
        double ppq = 0.0;  // position at the start of the block, in quarter notes
        double bpm = 120.0;
        // Whether `bpm` above is the *host's* answer or just this struct's default. It has to be
        // its own flag rather than a `bpm > 0` test, because the default is 120 and not 0: in the
        // standalone there is no playhead to ask, so a `> 0` test would read that 120 as a host
        // tempo and quietly ignore the BPM control. See the tempo choice in process().
        bool hasBpm = false;
        // Whether there is a **bar grid to anchor to**, whatever the transport is doing
        // (2026-08-18, Owen: "I'm not convinced about the quantized arpeggiators ... I think that
        // the notes should be playing at the same time, but they're not").
        //
        // Anchoring used to need a rolling host transport, so with none - the standalone, or a
        // DAW sitting stopped - every line fell back to a phase of its own that `restart()` zeroes
        // when the line is switched on. Two lines switched on a second apart were then a second
        // out of phase for good, and no setting could bring them together: Launch Quantize aligns
        // when a *chord* lands, not where the steps fall. The processor publishes a beat count of
        // its own for exactly this reason already (see `arpBeats`, which Launch Quantize measures
        // from) - the host's position while it rolls, its own count otherwise - so there has
        // always been a grid here, it simply was not offered to the engines.
        //
        // One grid, read by every line in the same block, is what makes two anchored lines walk in
        // lockstep. `anchored` is still the switch: off, a line keeps its own free-running phase
        // and drifts on purpose.
        bool hasGrid = false;
    };

    Lanes lanes;

    // Subharmonicon-style rhythm dividers (2026-08-14): up to four per line, 1..16, 0 = off.
    // All zero (the default) is inert - process() takes the same path it always has, byte for
    // byte - so a session that never touches this feature cannot tell it exists. Same contract
    // as `lanes`: message thread writes, audio thread reads, plain atomics, no lock.
    std::array<std::atomic<int>, 4> rhythmDiv {};

    // The Harmony lane's voicing: 0 = chord tones (today's behaviour, default), 1 = subharmonic
    // (the undertone series - see fireStep). Same contract as `rhythmDiv`.
    std::atomic<int> harmonyMode { 0 };

    // --- Published for the UI: the audio thread writes, the panel reads at 10 Hz -----------
    // The lane grids draw a playhead, and a polymetric page cannot be read without one: every
    // lane wraps by its own length and its own clock divider, so "which step is sounding" has
    // a different answer in each of them and none of those answers is the transport's. What is
    // published is the one number all of them are derived from - the step index relative to the
    // last restart, exactly what `laneValue` indexes by - and `laneStepIndex` below is that
    // arithmetic lifted out, so a grid cannot light a cell the engine did not read.
    // -1 means nothing is running: no lane has a playhead, rather than every lane having one
    // parked on step 0, which would read as a stopped sequencer sitting on its first step.
    std::atomic<long long> uiRelStep { -1 };
    std::atomic<int> uiOffset { 0 };

    // What the Note lane's fixed indices 1..n actually name. The engine is the only thing that
    // knows: the sequence is the held chord sorted by the current Shape and stacked `octaves`
    // high, which is not the chord and not the keybed. A grid can write "E3" where the drawn
    // value says 3, which is the difference between a spreadsheet and a melody.
    std::atomic<int> uiSeqCount { 0 };
    std::array<std::atomic<int>, maxHeld * 4> uiSeq {};

    // Where a lane reads on a given step: the engine's own index arithmetic, lifted out so the
    // UI runs this line rather than a second copy of it that can drift from it. `rel` is the
    // step index relative to the last restart; the divider shift happens before the offset,
    // because Offset starts a lane further into itself and a divider slows it down.
    static int laneStepIndex(long long rel, int offset, const LaneShape& sh) noexcept
    {
        const int len = juce::jlimit(1, maxSteps, sh.len);
        // The window, clamped to what the lane currently is. loopTo defaults past the end and
        // means "the end"; a window dragged backwards is read as the span between its ends
        // rather than as an error, since the two handles are the same kind of thing.
        int from = juce::jlimit(0, len - 1, sh.loopFrom);
        int to   = juce::jlimit(0, len - 1, sh.loopTo);
        if (to < from)
            std::swap(from, to);
        const int span = to - from + 1;

        // Still stateless from the step index - no cursor, no direction bit carried between
        // blocks - which is what lets a transport jump land on the right cell without the
        // engine having walked there. `laneChain` is the only thing here allowed to remember.
        const long long k = (rel >> juce::jlimit(0, 2, sh.div)) + offset;
        const auto wrap = [](long long v, int m) { return (int) ((v % m + m) % m); };
        if (span <= 1)
            return from;

        switch (juce::jlimit(0, (int) numLaneDirs - 1, sh.dir))
        {
            case dirDown:    return to - wrap(k, span);
            case dirUpAlt:
            case dirDownAlt:
            {
                const int period = span * 2 - 2;
                const int m = wrap(k, period);
                const int upFrom = m < span ? m : period - m; // 0..span-1 and back down
                return sh.dir == dirUpAlt ? from + upFrom : to - upFrom;
            }
            default:         return from + wrap(k, span);
        }
    }

    void prepare(double sampleRate)
    {
        sr = sampleRate > 0 ? sampleRate : 44100.0;
        hardReset();
    }

    // Silence everything the arp currently owes, immediately (offset 0 of the next
    // buffer it is given). Call when bypassing or on a transport jump.
    void flushInto(juce::MidiBuffer& out)
    {
        for (int i = 0; i < activeCount; ++i)
            out.addEvent(juce::MidiMessage::noteOff(active[(size_t) i].channel, active[(size_t) i].note), 0);
        activeCount = 0;
        pendingCount = 0; // un-fired ratchet hits must not survive a bypass or a transport jump
    }

    // Start the run over without forgetting what is held. This is hardReset() minus the held
    // set, and it is what switching a line *on* wants (2026-08-02): a chord handed to a line
    // while it was off is remembered silently by noteArrived - `enabled` gates only the firing
    // below, never the input - and is the whole reason there is something to play the moment
    // the switch goes on. hardReset() there would have thrown that chord away, which is what
    // made a line switched on sit silent until you dropped a new card on it.
    void restart()
    {
        activeCount = 0;
        pendingCount = 0;
        stepCounter = 0;
        dirCursor = 0;
        stepBase = 0;
        lastStepFired = false; // a restart has no step before it
        lastPlayedIdx = -1;
        lastRetrigWindow = std::numeric_limits<long long>::min();
        pendingRetrig = false;
        heldBeats = 0.0;
        rampScale = 1.0f;
        lastPicked = -1;
        permDirty = true;   // permCount stays: the walk is rebuilt from the held set, not cleared
        freePhaseBeats = 0.0;
        havePrevPpq = false;
    }

    void hardReset()
    {
        activeCount = 0;
        pendingCount = 0;
        heldCount = 0;
        physicallyHeld = 0;
        stepCounter = 0;
        dirCursor = 0;
        stepBase = 0;
        lastStepFired = false; // a restart has no step before it
        lastPlayedIdx = -1;
        lastRetrigWindow = std::numeric_limits<long long>::min();
        pendingRetrig = false;
        heldBeats = 0.0;
        rampScale = 1.0f;
        lastPicked = -1;
        permCount = 0;
        permDirty = true;
        freePhaseBeats = 0.0;
        havePrevPpq = false;
    }

    // Process one block. `midi` is the merged stream from the collector (and host);
    // note on/off are consumed as arp input, everything else passes through into
    // `out`. The arp's own notes are added to `out` at sample-accurate offsets.
    // `out` must be a different buffer to `midi`.
    void process(const Params& p, const HostClock& clock, int numSamples,
                 const juce::MidiBuffer& midi, juce::MidiBuffer& out)
    {
        // Consume input notes; pass through the rest.
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
                noteArrived(m.getNoteNumber(), m.getFloatVelocity(), m.getChannel(), p);
            else if (m.isNoteOff())
                noteLeft(m.getNoteNumber(), p);
            else
                out.addEvent(m, meta.samplePosition);
        }

        // Switching between Hz and Sync is a change of *timebase*, not of speed: one counts
        // seconds and the other beats, so the phase carried across means nothing on the far
        // side and the step in flight belongs to a timeline that no longer exists. Treated
        // exactly like a transport jump below - close what is owed at the top of this block,
        // then start the new clock from zero - so nothing is left stranded on note-on.
        if (p.rateFree != lastRateFree)
        {
            lastRateFree = p.rateFree;
            flushInto(out);
            freePhaseBeats = 0.0;
            havePrevPpq = false;
            stepBase = 0;
            lastRetrigWindow = std::numeric_limits<long long>::min();
        }

        // One scheduler, two clocks, and the mode picks which drives it. Hz mode pins the
        // tempo to 60 so that one "beat" of everything below is one second; the step is then
        // 1/hz of that unit, and every quantity measured as a *fraction of a step* (swing,
        // the Late lane, gate, ratchet spacing) keeps its meaning with no second code path.
        // The two things measured in beats outright, Retrigger Every and the velocity ramp's
        // Ramp Time, therefore read as seconds while Hz is on. That is the honest reading:
        // there is no bar to restart on when nothing is following a transport.
        // **Not gated on clock.playing** (2026-08-16, Owen: "bpm isn't syncing with daw"). A
        // DAW's tempo is its tempo whether or not the transport is rolling - Ableton shows 120
        // with everything stopped - so following it only while playing meant Keys sat at its own
        // number for exactly as long as you were setting up, which is when you look at it. The
        // *position* still needs a rolling transport and still checks `playing` (see the anchor
        // branch below); the tempo never did.
        const double bpm = p.rateFree ? 60.0
                                      : ((p.followHost && clock.hasBpm && clock.bpm > 0)
                                             ? clock.bpm
                                             : p.fallbackBpm);
        const double beatsPerSample = bpm / 60.0 / sr;
        const double stepBeats = stepLengthBeats(p);
        const double blockBeats = beatsPerSample * numSamples;

        // Position in beats at the start of this block. Hz mode never takes the anchored
        // branch: following the bar grid is the one thing a free-running rate cannot do.
        //
        // `hasGrid` is the processor's answer and covers the stopped and hostless cases too; the
        // rolling-transport pair is kept beside it so a caller that fills only those - every test
        // in ArpTests, and any host wiring that predates the flag - still anchors. See HostClock.
        const bool onGrid = clock.hasGrid || (clock.playing && clock.hasPpq);
        double pos;
        if (onGrid && p.anchored && ! p.rateFree)
        {
            pos = clock.ppq;
            // A jump (loop, relocate) means owed note-offs' timelines are invalid.
            if (havePrevPpq && std::abs(clock.ppq - expectedNextPpq) > blockBeats + 1.0e-3)
                flushInto(out);
            havePrevPpq = true;
            expectedNextPpq = clock.ppq + blockBeats;
            freePhaseBeats = clock.ppq; // keep free phase seeded for a later stop
        }
        else
        {
            pos = freePhaseBeats;
            freePhaseBeats += blockBeats;
            havePrevPpq = false;
        }

        // Owed note-offs are NOT retired up front. Each hit closes what it lands on top of
        // itself, immediately before its own note-on (see fireStep), because a tie has to be
        // pulled back to just before the pitch retriggers rather than allowed to fire in the
        // middle of the note that replaced it. Draining here instead put a tie's note-off
        // *after* the note-on that superseded it: two note-ons for one pitch with nothing in
        // between, which hangs a voice on any synth that allocates per note-on.
        // The velocity ramp is sampled once per block, not once per hit: the shortest useful
        // ramp is a bar and the longest block is a few milliseconds, so the stair this leaves
        // is far below the 1/127 the velocity is quantized to anyway.
        rampScale = 1.0f;
        if (p.velRamp != 0 && p.rampBeats > 0.0)
        {
            const double t = juce::jlimit(0.0, 1.0, heldBeats / p.rampBeats);
            rampScale = (float) (1.0 + (double) p.velRamp / 100.0 * t);
        }

        // Rhythm dividers, read once per block like every other atomic-backed control. Clamped
        // here rather than trusted from whatever wrote them (an MCP client, a stale slot) - see
        // stepLengthBeats's own clamp for why: this value bounds a loop below, on the audio
        // thread, and an unclamped divisor would only bound that loop by luck.
        const std::array<int, 4> divs = { juce::jlimit(0, 16, rhythmDiv[0].load(std::memory_order_relaxed)),
                                          juce::jlimit(0, 16, rhythmDiv[1].load(std::memory_order_relaxed)),
                                          juce::jlimit(0, 16, rhythmDiv[2].load(std::memory_order_relaxed)),
                                          juce::jlimit(0, 16, rhythmDiv[3].load(std::memory_order_relaxed)) };
        const bool dividersOn = divs[0] > 0 || divs[1] > 0 || divs[2] > 0 || divs[3] > 0;

        if (p.enabled && heldCount > 0 && stepBeats > 0.0)
        {
            // Fire every step whose *fire time* lands inside [pos, pos + blockBeats). Not
            // every boundary in that range: swing shifts the offbeats either way, so with a
            // negative swing a step is pulled in front of its own boundary and can belong to
            // the block before it. Walking from one step back and testing the fire time is
            // what makes early swing possible at all - the old ceil-from-pos loop dropped
            // any step it pulled behind the block start, which silenced every other note.
            // Costs at most one extra iteration per block, since |swing| < 1 keeps fire
            // times monotonic and each step therefore still fires in exactly one block.
            // Three steps back, not one. One was enough while swing was the only thing that
            // moved a step (|swing| < 1, so a step could only ever be pulled into the block
            // before its own). The Late lane can push a step up to 0.9 of a step *past* its
            // boundary, and on an odd step that stacks with a positive swing, so a step can
            // now fire as much as 1.65 steps after the one it is named for. Costs two extra
            // iterations that skip on a negative offset and fire nothing.
            double nextIndexF = std::floor(pos / stepBeats + 1.0e-9) - 3.0;
            for (;;)
            {
                const double boundaryBeats = nextIndexF * stepBeats;
                const long long globalStep = (long long) llround(nextIndexF);

                // With any divider enabled, a boundary fires only if it is a multiple of at
                // least one of them (dividerFires); a suppressed boundary fires no note, no
                // ratchet, and does not advance anything - skip it before it touches a single
                // lane read or the direction walk. All zero (the default) never enters this
                // branch, so behaviour is unchanged bit for bit.
                if (dividersOn && ! dividerFires(globalStep, divs))
                {
                    nextIndexF += 1.0;
                    continue;
                }
                // The index a firing boundary reads its lanes by and restarts its walk from.
                // Raw with no dividers (today's behaviour, untouched); with dividers, the count
                // of firing boundaries before this one (firedCountBefore), computed from
                // globalStep alone - stateless the same way the rest of this clock is - so the
                // pattern advances one lane step per *fired* boundary instead of leaving gaps
                // where a divider skipped a raw step. Only the lane index compresses: the wall-
                // clock position below still comes from the real step grid.
                const long long stepIndex = dividersOn ? firedCountBefore(globalStep, divs) : globalStep;

                double fireBeats = boundaryBeats;
                if ((globalStep & 1) != 0)
                    fireBeats += stepBeats * p.swing; // swing shifts the offbeats
                // Late: this step's own shift, always forwards. Cthulhu calls the lane Late
                // and so does this - it can only delay, which is what keeps fire times in
                // step order however the lane is drawn (step n at n+0.9 still precedes step
                // n+1 at n+1.0). An early half would need the whole close-what-you-land-on
                // rule in emitHit rewritten, for a shift Swing already offers.
                fireBeats += stepBeats * juce::jlimit(0, 90, driftedLane(p, laneLate, stepIndex)) / 100.0;
                const double offsetBeats = fireBeats - pos;
                const int offset = (int) std::floor(offsetBeats / beatsPerSample);
                if (offset >= numSamples)
                    break;
                if (offset >= 0)
                {
                    // Restarts, both kinds, decided here rather than where they are asked for:
                    // "step 1" only means anything at a step boundary, and the note that asks
                    // for a retrigger arrives in the middle of a block. Whichever fires first
                    // after the request becomes step 1, for the lanes (stepBase) and for the
                    // direction walk (dirCursor) alike. Before this the lanes never restarted
                    // at all - they were read straight off the absolute step index, so the
                    // Retrigger toggle only ever reset the walk.
                    if (p.retrigBeats > 0.0)
                    {
                        const long long win = (long long) std::floor(boundaryBeats / p.retrigBeats + 1.0e-9);
                        if (win != lastRetrigWindow)
                        {
                            lastRetrigWindow = win;
                            pendingRetrig = true;
                        }
                    }
                    if (pendingRetrig)
                    {
                        pendingRetrig = false;
                        stepBase = stepIndex;
                        dirCursor = 0;
                    }

                    // Any ratchet sub-hit carried in from an earlier block that is due before
                    // this step goes first, so hits reach emitHit in time order.
                    firePendingBefore(offset, numSamples, out);
                    // The playhead the grids draw, published here rather than from the clock:
                    // a suppressed divider boundary and a step the chord could not fill both
                    // pass through the loop above without reading a lane, and a playhead that
                    // moved on those would point at cells the engine never looked at.
                    uiRelStep.store(stepIndex - stepBase, std::memory_order_relaxed);
                    uiOffset.store(p.offset, std::memory_order_relaxed);
                    fireStep(p, stepIndex, offset, stepBeats / beatsPerSample, numSamples, out);
                }
                nextIndexF += 1.0;
            }

            firePendingBefore(numSamples, numSamples, out); // the rest of this block's sub-hits
        }
        else
        {
            // Bypassed, or the keys came up. A ratchet is one gesture: its remaining sub-hits
            // die with the step that decided them rather than firing into a silence, and
            // dropping them here is also what keeps them from surfacing a block later.
            pendingCount = 0;
            uiRelStep.store(-1, std::memory_order_relaxed); // no playhead, not step 0
            // The note names go with the playhead. uiSeq is written only by buildSequence, and
            // buildSequence only runs from fireStep, so nothing cleared it when the chord came
            // up: the Note lane went on printing "E3" for a chord nobody was holding until the
            // next step fired, and with the line switched off that never came. Both halves of
            // "what is this lane doing right now" go quiet together now, and the grid falls
            // back to the raw number exactly as it does before a chord ever lands.
            uiSeqCount.store(0, std::memory_order_relaxed);
        }

        // Whatever is still owed inside this block, then rebase the survivors onto the next
        // block's timebase. Deliberately outside the guard above: a note owed by the last
        // step before the keys came up, or before the arp was switched off, still has to end
        // on time instead of hanging until the next flush.
        retireDue(numSamples - 1, out);
        advanceBlock(numSamples);

        // How long this chord has been up, which is what the velocity ramp rides on. It
        // counts beats rather than seconds so a ramp written at one tempo means the same
        // thing at another, and it stops counting the moment the keys come up.
        if (heldCount > 0)
            heldBeats += blockBeats;
    }

    int heldNoteCount() const noexcept { return heldCount; }

private:
    // `ons` counts how many un-matched note-ons this pitch has arrived with. The engine sits
    // downstream of a merged stream where several chord sources can each ask for the same
    // pitch, so "is it still held" is a count, not a flag. It used to be a flag plus a
    // separate physicallyHeld total, and a duplicated pitch leaked that total above zero
    // permanently - which disabled latch's fresh-chord reset and left chords arpeggiating
    // forever, stacking every card you handed it afterwards.
    struct Held { int note; float velocity; int channel; int ons; };
    struct Active { int note; int channel; int samplesLeft; };
    // A ratchet sub-hit whose fire time landed past the end of the block that decided it.
    // `at` is in the same frame as Active::samplesLeft and is rebased by the same
    // advanceBlock(); see the contract note below.
    struct PendingHit { int note; int channel; float velocity; int at; int durSamples; };

    double stepLengthBeats(const Params& p) const
    {
        // Hz mode: process() has pinned the clock to 60 bpm, so a "beat" is a second and the
        // step length is simply the period. Dot and Tuplet are deliberately not applied - they
        // are subdivisions of a beat, and there is no beat here; all a dotted 8 Hz would do
        // is make the number on the dial a lie.
        //
        // The clamp is not only about musical range: it is the only thing keeping the step
        // loop in process() terminating, and this runs on the audio thread. At rateHz 0 the
        // period is +inf, the Late lane's `stepBeats * lane` term goes NaN, every comparison
        // against it is false, the computed offset is INT_MIN on every pass, and the
        // `offset >= numSamples` break never trips. Mutation-tested on 2026-07-30 by deleting
        // this jlimit: the test binary hung and had to be killed. A negative rate is harmless
        // by comparison, since `stepBeats > 0.0` catches it and simply fires nothing. So do
        // not move this clamp to the parameter and trust it: the engine takes whatever a
        // session, a host automation lane or an MCP client hands it.
        if (p.rateFree)
            return 1.0 / juce::jlimit(minRateHz, maxRateHz, p.rateHz);

        static constexpr double base[11] = { 64.0, 32.0, 16.0, 8.0, 4.0,   // 16,8,4,2,1 bars
                                             2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 }; // 1/2..1/64
        double b = base[juce::jlimit(0, 10, p.rateIndex)];
        if (p.dotted) b *= 1.5;
        b *= tupletFactor(p.tuplet); // 1.0 when straight, so the multiply is unconditional
        return b;
    }

    void noteArrived(int note, float vel, int channel, const Params& p)
    {
        // Latch with nothing physically held: a fresh chord starts over.
        if (p.latch && physicallyHeld == 0 && heldCount > 0)
        {
            heldCount = 0;
            dirCursor = 0;
        }
        if (heldCount == 0)
        {
            heldBeats = 0.0; // a new chord restarts the velocity ramp
            if (p.retrigger)
            {
                stepCounter = 0;
                dirCursor = 0;
                pendingRetrig = true; // the next step to fire becomes step 1, lanes included
            }
        }
        permDirty = true; // the locked random order is per chord, and the chord just changed
        for (int i = 0; i < heldCount; ++i)
            if (held[(size_t) i].note == note)
            {
                // Same pitch arriving twice: count it, do not add a second entry. The
                // matching note-off will decrement, so the two stay paired.
                held[(size_t) i].velocity = vel;
                if (held[(size_t) i].ons++ == 0)
                    ++physicallyHeld; // it was latched-but-released; it is physical again
                return;
            }
        if (heldCount < maxHeld)
        {
            held[(size_t) heldCount++] = { note, vel, channel, 1 };
            ++physicallyHeld;
        }
    }

    void noteLeft(int note, const Params& p)
    {
        // Match on pitch whatever its count: matching only entries still marked physical was
        // what dropped the count on the floor when two sources owned one pitch, leaving
        // physicallyHeld stuck above zero for the life of the plugin.
        for (int i = 0; i < heldCount; ++i)
            if (held[(size_t) i].note == note && held[(size_t) i].ons > 0)
            {
                if (--held[(size_t) i].ons > 0)
                    return; // another owner still holds this pitch
                physicallyHeld = juce::jmax(0, physicallyHeld - 1);
                if (! p.latch)
                {
                    for (int j = i; j < heldCount - 1; ++j)
                        held[(size_t) j] = held[(size_t) j + 1];
                    --heldCount;
                    permDirty = true;
                }
                return;
            }
    }

    // The one place lane data is read, so it is also the one place the pattern gate
    // belongs: with usePattern false every lane reads as its default and the arp runs
    // as a plain shape, leaving edited step data untouched and waiting.
    int laneValue(const Params& p, Lane l, long long globalStep) const
    {
        if (! p.usePattern)
            return laneDefaults[l];

        const auto li = (size_t) l;
        if (lanes.on[li].load(std::memory_order_relaxed) == 0)
            return laneDefaults[l]; // switched off: the drawing stays, nothing reads it

        // **Mute walks the Note lane's shape, not its own.** It is the Note lane's companion
        // rather than a polymetric lane of its own - it has no tab, the MUTE strip under the
        // grid *is* its editor, and that strip is drawn and edited against the Note lane's
        // cells. Its length has been kept in step since it arrived, for exactly this reason
        // ("the engine wraps each lane by its own length and a disagreement would silence the
        // wrong step"), but a lane grew a loop window and a direction on 2026-08-18 and only
        // the length was carried across. Set the Note lane's Dir to Down and the cell under
        // the note drawn at step 0 silenced the note drawn at step 7 instead: the strip could
        // not select Mute to fix it, because it has no tab. Borrowing the shape here keeps the
        // whole companion rule in the one place lane data is read.
        const auto shape = lanes.shapeOf(l == laneMute ? (int) laneNote : (int) l);

        // Relative to the last restart, not to the absolute step index, so Retrigger means
        // step 1 for the lanes too; plus Offset, which starts the same lanes further in.
        return lanes.value[li][(size_t) laneStepIndex(globalStep - stepBase, p.offset, shape)]
                   .load(std::memory_order_relaxed);
    }

    // laneValue plus Drift (2026-08-14). Every lane read that feeds *how a step plays* goes
    // through here instead; the four that choose content still call laneValue directly, and
    // `laneDrifts` is the table that says which is which.
    //
    // Non-const where laneValue is const, on purpose: this draws from `rng`, and a lane read
    // that can return a different answer twice is not the same kind of thing as one that
    // cannot. It is called exactly once per lane per fired step, so a step's drift is settled
    // before any of it is heard.
    int driftedLane(const Params& p, Lane l, long long globalStep)
    {
        const int v = laneValue(p, l, globalStep);
        const int amt = juce::jlimit(0, 100, p.drift);
        if (amt <= 0 || ! laneDrifts[l])
            return v;
        // Half the reach either side, so the drawn value stays the centre of what you hear
        // rather than the floor of it - the opposite of Humanize, which is deliberately
        // one-sided (late and quieter, never early and never louder) because it models a
        // player. Drift models a *machine* wandering, and a wander has no preferred direction.
        const auto r = laneRange(l);
        const double reach = (double) (r.hi - r.lo) * (amt / 100.0);
        return strayWithin(v, reach, r, (double) (rng() % 1000u) / 1000.0);
    }

    // A cheap avalanche so neighbouring steps and eras do not produce neighbouring answers.
    // Not the engine's `rng`: the same step in the same era has to give the same answer every
    // time it is asked, and a stream cannot promise that once anything else draws from it.
    static unsigned int hash32(unsigned int x) noexcept
    {
        x ^= x >> 16; x *= 0x7feb352du;
        x ^= x >> 15; x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    // **Mutate**: the run explores other notes of the chord being held, and **Lock** decides how
    // long it keeps what it found (2026-08-18, Owen: "explores other patterns and notes...
    // want notes. mutations").
    //
    // This is the counterpart to `driftedLane` above, not a breach of it. Drift is barred from
    // the note because a machine wandering onto notes nobody aimed at is noise; this moves the
    // run to a different entry of **the sequence already built from the held chord**, so every
    // note it can reach is a note the chord contains. There is no setting at which it plays
    // something you did not put there - only a different one of the ones you did.
    //
    // Lock is the Turing Machine (docs/SEQUENCER_LANDSCAPE.md: "randomness that hardens into a
    // loop"). The variation is a function of the step and of which *era* the current pass falls
    // in; Lock stretches an era, so 0 redraws every pass, 100 is a single era and the first
    // variation repeats for good. Deriving it from the step index rather than carrying a shift
    // register is what keeps this stateless from the playhead like everything else here bar
    // laneChain: a transport jump lands on the variation it would have walked to.
    // The (step, era) cell both of Mutate's stages hash. One function so the index walk and
    // the pitch stage below cannot disagree about when a variation changes: the pass is
    // measured over the window the Note lane actually walks, not its length - a loop of four
    // steps inside a lane of sixteen comes round every four, and a variation that changed
    // every sixteen would be heard as changing in the wrong place - and Lock stretches the
    // era for both at once.
    void mutateCell(const Params& p, long long globalStep, int& stepOut, long long& eraOut) const noexcept
    {
        const auto sh = lanes.shapeOf(laneNote);
        const int len = juce::jlimit(1, maxSteps, sh.len);
        int from = juce::jlimit(0, len - 1, sh.loopFrom);
        int to = juce::jlimit(0, len - 1, sh.loopTo);
        if (to < from)
            std::swap(from, to);
        const int span = juce::jmax(1, to - from + 1);

        // **Counted in lane cells, not raw steps.** Both of these used to come off `rel`
        // directly, which is only the same thing while the lane runs at x1 with no Offset:
        // at Speed x2 one pass of an eight-step window takes sixteen raw steps, so the era
        // advanced twice per pass and a "locked" variation audibly changed halfway round the
        // loop - the opposite of what the comment above promises. `k` is laneStepIndex's own
        // walk position (divider first, then Offset, exactly as it orders them), and the step
        // is the cell that function actually reads, so a stored variation belongs to the cell
        // it is drawn on rather than to a raw step index that drifts away from it.
        const long long rel = globalStep - stepBase;
        const long long k = (rel >> juce::jlimit(0, 2, sh.div)) + p.offset;
        const long long pass = (k >= 0 ? k : k - span + 1) / span;
        const int lock = juce::jlimit(0, 100, p.mutateLock);
        // 0 -> a new era every pass; 99 -> one every ~62; 100 -> one era, ever.
        eraOut = lock >= 100 ? 0 : pass / (1 + (long long) lock * lock / 160);
        stepOut = laneStepIndex(rel, p.offset, sh);
    }

    int mutatedIndex(const Params& p, int chosen, long long globalStep, int count) const noexcept
    {
        const int amt = juce::jlimit(0, 100, p.mutate);
        if (amt <= 0 || count <= 1)
            return chosen;

        int step; long long era;
        mutateCell(p, globalStep, step, era);
        const unsigned int h = hash32((unsigned int) step * 2654435761u
                                      ^ (unsigned int) (era * 40503u)
                                      ^ (unsigned int) (p.mutateSeed * 2246822519u));
        if ((int) (h % 100u) >= amt)
            return chosen; // this step keeps what it was given, this era

        // The reach is in chord entries, never in semitones - one step of it is one note of the
        // chord - which is why this stage cannot leave the harmony at any amount. Leaving it is
        // mutatedPitch's job, and only past the knob's halfway point.
        const int reach = 1 + amt * 2 / 100; // 1..3 entries either side
        const int delta = (int) ((h >> 8) % (unsigned) (reach * 2 + 1)) - reach;
        return ((chosen + delta) % count + count) % count;
    }

    // **The out-of-scale half of Mutate** (2026-08-19, Owen: "I want a mutate knob, which
    // effects the notes being played. higher values can go out of scale"). Three zones on the
    // one knob: to 50 this stage does nothing and Mutate is exactly the 2026-08-18 control,
    // confined to the held chord. Past 50 a step may land a scale degree or two away from the
    // note the walk chose - in scale, out of chord. Past 75 a growing share of those strays
    // are chromatic semitones, and by 100 all of them are.
    //
    // Applied to the *placed* pitch, after the index walk and after place(), so a step that
    // strays still strays from the note the run actually reached - and before the per-line
    // harmony voices, which follow it. It hashes the same (step, era) cell as mutatedIndex
    // with a different salt, so Lock holds these variations exactly as it holds the index
    // ones: a wander that hardens, out-of-scale notes included.
    int mutatedPitch(const Params& p, int note, long long globalStep) const noexcept
    {
        const int amt = juce::jlimit(0, 100, p.mutate);
        if (amt <= 50)
            return note;

        int step; long long era;
        mutateCell(p, globalStep, step, era);
        const unsigned int h = hash32((unsigned int) step * 3266489917u
                                      ^ (unsigned int) (era * 668265263u)
                                      ^ (unsigned int) (p.mutateSeed * 2246822519u)
                                      ^ 0x9e3779b9u);
        // How often a step leaves the chord at all: never at 50, every other step by 100.
        if ((int) (h % 100u) >= amt - 50)
            return note;

        const int dir = ((h >> 14) & 1u) != 0 ? 1 : -1;
        // The chromatic share of the strays: none at 75, all of them at 100.
        if (amt > 75 && (int) ((h >> 7) % 100u) < (amt - 75) * 4)
            return juce::jlimit(0, 127, note + dir * (1 + (int) ((h >> 9) % 3u)));
        // In scale, which is the middle zone's whole meaning - and note that with Scale set to
        // **Chromatic** the mask is every pitch class, so "a scale degree or two" *is* one or
        // two semitones and the middle zone reads as the chromatic one. That is the setting
        // being honest rather than the zone leaking: there is no non-chromatic answer to
        // "stay in the chromatic scale". The three zones are three zones under any scale that
        // actually excludes something, which is every other entry in the list.
        return shiftByDegrees(note, dir * (1 + (int) ((h >> 9) % 2u)), p.scaleMask, p.rootPc);
    }

    // Walk `degrees` scale steps from `note`, using the mask of in-scale pitch classes. A
    // note that is not itself in the scale lands on the next one in the direction of travel,
    // which is the same rounding Scale Lock does upstream.
    static int shiftByDegrees(int note, int degrees, unsigned int mask, int rootPc) noexcept
    {
        if (degrees == 0 || (mask & 0xFFFu) == 0)
            return note;
        const int dir = degrees > 0 ? 1 : -1;
        int remaining = degrees > 0 ? degrees : -degrees;
        int n = note;
        for (int guard = 0; guard < 128 && remaining > 0; ++guard)
        {
            n += dir;
            if (n < 0 || n > 127)
                return juce::jlimit(0, 127, n);
            if ((mask >> ((((n - rootPc) % 12) + 12) % 12)) & 1u)
                --remaining;
        }
        return n;
    }

    // Sorted-note order for direction modes (as-played uses arrival order).
    void buildSequence(const Params& p)
    {
        seqCount = 0;
        int order[maxHeld];
        for (int i = 0; i < heldCount; ++i)
            order[i] = i;
        const bool sortByPitch = p.direction != Direction::asPlayed
                              && p.direction != Direction::asPlayedReverse;
        if (sortByPitch)
            for (int i = 1; i < heldCount; ++i)
                for (int j = i; j > 0 && held[(size_t) order[j]].note < held[(size_t) order[j - 1]].note; --j)
                    std::swap(order[j], order[j - 1]);

        // Repeats of the chord, each one `spread` further up. Twelve semitones is the octave
        // stacking this used to hardcode; in degrees it follows Root/Scale, so a spread of a
        // third stays a third of *this* key rather than of the chromatic scale.
        const int octs = juce::jlimit(1, 4, p.octaveRange);
        for (int o = 0; o < octs; ++o)
            for (int i = 0; i < heldCount && seqCount < (int) seq.size(); ++i)
            {
                const int base = held[(size_t) order[i]].note;
                const int shifted = p.spreadDegrees
                                  ? shiftByDegrees(base, o * p.spread, p.scaleMask, p.rootPc)
                                  : base + o * p.spread;
                seq[(size_t) seqCount++] = { order[i], shifted - base };
            }

        // Publish the pitches for the Note lane's cell text. Here rather than in fireStep
        // because this is the one place the sequence changes, and a grid naming notes has to
        // follow the chord the moment it lands, not the next time a step happens to fire.
        for (int i = 0; i < seqCount; ++i)
            uiSeq[(size_t) i].store(held[(size_t) seq[(size_t) i].heldIndex].note
                                        + seq[(size_t) i].semitoneOffset,
                                    std::memory_order_relaxed);
        uiSeqCount.store(seqCount, std::memory_order_relaxed);
    }

    // The sequence is always built ascending (or in arrival order); directions are
    // realized here. Exclusive ping-pong (upDown/downUp) plays the endpoints once
    // per cycle; inclusive (upAndDown/downAndUp) repeats them, Cthulhu-style.
    // The line's own shape. The overload below is the same walk under a shape a *step* named
    // (Note lane 13..20): same cursor, so mixing shapes across a lane advances one walk rather
    // than eight of them - Cthulhu's "varies consecutively one step after another".
    int nextDirectionIndex(const Params& p) { return nextDirectionIndex(p, p.direction); }

    int nextDirectionIndex(const Params& p, Direction dir)
    {
        const int n = seqCount;
        if (n <= 1)
        {
            ++dirCursor;
            return 0;
        }
        // Offset starts the walk further in. Added to the cursor rather than to the result,
        // so a ping-pong starts at the right place *in its cycle* instead of being reflected
        // to some other note.
        const long long cursor = dirCursor++ + p.offset;
        switch (dir)
        {
            case Direction::up:
            case Direction::asPlayed:
                return (int) (cursor % n);
            case Direction::down:
            case Direction::asPlayedReverse:
                return n - 1 - (int) (cursor % n);
            case Direction::upDown:
            case Direction::downUp:
            {
                const int period = 2 * (n - 1);
                const int c = (int) (cursor % period);
                const int i = c < n ? c : period - c;
                return dir == Direction::upDown ? i : n - 1 - i;
            }
            case Direction::upAndDown:
            case Direction::downAndUp:
            {
                const int period = 2 * n;
                const int c = (int) (cursor % period);
                const int i = c < n ? c : period - 1 - c;
                return dir == Direction::upAndDown ? i : n - 1 - i;
            }
            case Direction::random:
                lastPicked = (int) (rng() % (unsigned) n);
                return lastPicked;
            case Direction::randomOther:
            {
                // Draw from the other n-1 notes and skip past the last one, rather than
                // re-drawing until it differs: one draw, always, and no unbounded loop on
                // the audio thread.
                int r = (int) (rng() % (unsigned) (n - 1));
                if (lastPicked >= 0 && lastPicked < n && r >= lastPicked)
                    ++r;
                lastPicked = juce::jlimit(0, n - 1, r);
                return lastPicked;
            }
            case Direction::randomOnce:
                rebuildPermIfNeeded();
                return perm[(size_t) (cursor % n)];
            case Direction::fingeredBottom:
            case Direction::fingeredTop:
            {
                // Cthulhu p24: "every 2nd note is the high note of the chord". So the walk
                // alternates with a fixed extreme - and it walks the notes that are *not* that
                // extreme, which is what makes a triad come out C G E G rather than C G G G.
                //
                // The extreme is **scanned**, never taken as an end of `seq`, for the reason
                // noteHi and noteLow already carry: buildSequence sorts by pitch only for the
                // shapes that walk by pitch, and stacks octaves on top, so neither end is
                // reliably the extreme. Under "As Played" with C4, E4 then G3 pressed in that
                // order, seq[n-1] is G3, and "the high note of the chord" came out as the
                // lowest note actually held.
                const bool top = dir == Direction::fingeredTop;
                const int ext = extremeSeqIndex(top);
                if ((cursor & 1) != 0)
                    return ext;
                // The other n-1 entries, in order, wherever the extreme happens to sit: an
                // index at or above it shifts up by one, which skips exactly that entry.
                const int walk = (int) ((cursor / 2) % (n - 1)); // n > 1 here
                return walk >= ext ? walk + 1 : walk;
            }
            case Direction::chord:
                return (int) (cursor % n); // fireStep plays them all; this is the fallback
        }
        return 0;
    }

    // The entry of `seq` holding the highest (or lowest) sounding pitch. Shared by the two
    // fingered directions; noteHi and noteLow in fireStep do the same scan inline against the
    // step's own seqCount. Both exist because the positional ends of `seq` are only the pitch
    // extremes for the shapes that sorted it by pitch in the first place.
    int extremeSeqIndex(bool top) const
    {
        int best = 0;
        for (int i = 1; i < seqCount; ++i)
        {
            const int a = held[(size_t) seq[(size_t) i].heldIndex].note + seq[(size_t) i].semitoneOffset;
            const int b = held[(size_t) seq[(size_t) best].heldIndex].note + seq[(size_t) best].semitoneOffset;
            if (top ? a > b : a < b)
                best = i;
        }
        return best;
    }

    // A shuffled order held for as long as the chord is: random, but the same random every
    // time round, which is the one random mode that sounds composed rather than sprayed.
    void rebuildPermIfNeeded()
    {
        if (! permDirty && permCount == seqCount)
            return;
        permCount = seqCount;
        for (int i = 0; i < permCount; ++i)
            perm[(size_t) i] = i;
        for (int i = permCount - 1; i > 0; --i) // Fisher-Yates
            std::swap(perm[(size_t) i], perm[(size_t) (rng() % (unsigned) (i + 1))]);
        permDirty = false;
    }

    void fireStep(const Params& p, long long globalStep, int offset, double stepSamplesF,
                  int numSamples, juce::MidiBuffer& out)
    {
        // The chain condition, first of all: a step whose condition fails did not happen, and
        // must not spend a chance draw or advance anything. `lastStepFired` is the one bit of
        // playhead-dependent state in this engine - see laneChain for why that is affordable.
        if (const int chain = laneValue(p, laneChain, globalStep); chain != 0)
            if ((chain == 1) != lastStepFired)
            {
                lastStepFired = false;
                return;
            }

        // The mute lane first, and before anything else is resolved: a muted step costs
        // nothing and must leave no trace. Separate from the Note lane's own -1 on purpose -
        // that is a rest you *drew*, this is a switch you can flip back without having lost
        // what the step held (Cthulhu's manual, p25, names that as the whole point of it).
        if (laneValue(p, laneMute, globalStep) > 0)
        {
            lastStepFired = false;
            return;
        }

        int noteVal = laneValue(p, laneNote, globalStep);
        if (noteVal <= noteRest)
        {
            lastStepFired = false;
            return; // a drawn rest
        }

        // Rand: how far this step's selection may stray from what is drawn, and which way
        // (Cthulhu's "Rand Sel", its manual p25-26 - "if the Note Sel step is set to 2 and the
        // random value is set to 2 above middle, the Arp will output 2, 3, or 4"). Drawn per
        // step, so unlike Drift it is allowed to change which note plays: you aimed it there.
        //
        // Only meaningful on a fixed index. noteVal 0 means "follow the shape", and a shape is
        // already a walk - randomising the *number zero* would silently turn Up into a fixed
        // entry, which is not what drawing on this lane looks like it should do.
        if (const int rand = laneValue(p, laneRand, globalStep);
            rand != 0 && noteVal >= 1 && noteVal <= noteMaxFixed)
        {
            const int lo = juce::jmax(1, rand < 0 ? noteVal + rand : noteVal);
            const int hi = juce::jmin(noteMaxFixed, rand > 0 ? noteVal + rand : noteVal);
            if (hi > lo)
                noteVal = lo + (int) (rng() % (unsigned) (hi - lo + 1));
        }
        const int chance = driftedLane(p, laneProbability, globalStep)
                         * juce::jlimit(0, 100, p.chance) / 100;
        if ((int) (rng() % 100u) >= chance)
        {
            lastStepFired = false;
            return; // 100 always fires, 0 never does
        }
        lastStepFired = true; // everything below this point sounds

        // Position Reset, after the step has survived mute, rest, chain and chance, and before
        // anything asks the walk where it is: a step that did not sound did not reach its reset
        // either, which is what keeps a reset on a low-Chance step from firing on the passes the
        // step itself skipped.
        if (laneValue(p, laneReset, globalStep) > 0)
            dirCursor = 0;

        buildSequence(p);
        if (seqCount == 0)
            return;

        // Which sequence entries this step plays. One, normally. All of them on the Chord
        // shape, which is what makes it a comping engine rather than a walk order - a fixed
        // Note-lane index still means that one note, so an edited pattern keeps its meaning
        // over a chord shape instead of silently turning into block chords.
        int playIdx[maxHeld * 4];
        int playCount = 0;
        if (p.direction == Direction::chord && noteVal == 0)
        {
            for (int i = 0; i < seqCount; ++i)
                playIdx[playCount++] = i;
        }
        else
        {
            // 1..8 name an entry; 9..12 are the modes borrowed from Kirnu's ORDER lane, which
            // ask a question of the chord rather than counting into it - so they keep meaning
            // the same thing when the chord under them changes.
            int chosen;
            switch (noteVal)
            {
                case notePrev:
                    // The last entry that actually sounded, or the shape's next if nothing has
                    // yet - a Prev on the first step of a fresh hold has nothing to repeat.
                    chosen = lastPlayedIdx >= 0 ? juce::jlimit(0, seqCount - 1, lastPlayedIdx)
                                                : nextDirectionIndex(p);
                    break;
                case noteHi:
                case noteLow:
                {
                    // Scanned rather than assumed to be the ends of `seq`: buildSequence sorts
                    // by pitch only for the shapes that walk by pitch, and stacks octaves on
                    // top, so neither end is reliably the extreme.
                    int best = 0;
                    for (int i = 1; i < seqCount; ++i)
                    {
                        const int a = held[(size_t) seq[(size_t) i].heldIndex].note + seq[(size_t) i].semitoneOffset;
                        const int b = held[(size_t) seq[(size_t) best].heldIndex].note + seq[(size_t) best].semitoneOffset;
                        if (noteVal == noteHi ? a > b : a < b)
                            best = i;
                    }
                    chosen = best;
                    break;
                }
                case noteRnd:
                    chosen = (int) (rng() % (unsigned) seqCount);
                    break;
                default:
                    if (noteVal >= noteShapeFirst && noteVal <= noteShapeLast)
                        chosen = nextDirectionIndex(p, shapeForNoteValue(noteVal));
                    else
                        chosen = noteVal >= 1 ? (noteVal - 1) % seqCount // fixed index, wraps politely
                                              : nextDirectionIndex(p);
                    break;
            }
            // Mutate last, so it applies whichever route picked the note - a fixed index, a
            // shape's walk, or one of Kirnu's four questions. Before `lastPlayedIdx`, so a
            // later Prev repeats what was actually heard rather than what was aimed at.
            chosen = mutatedIndex(p, chosen, globalStep, seqCount);
            playIdx[playCount++] = chosen;
            lastPlayedIdx = chosen; // what a later Prev repeats
        }

        // The Octave lane's per-step shift plus the line's own, both in octaves. Summed rather
        // than one overriding the other: the knob says where the run sits, the lane says how a
        // particular step departs from that, and they are different questions.
        const int octaveShift = 12 * (juce::jlimit(-3, 3, driftedLane(p, laneOctave, globalStep))
                                      + juce::jlimit(-3, 3, p.octShift));
        const int transpose = juce::jlimit(-7, 7, laneValue(p, laneTranspose, globalStep));
        const int harmony = juce::jlimit(0, 7, laneValue(p, laneHarmony, globalStep));
        const int chordSel = juce::jlimit(0, ChordTable::numSlots, laneValue(p, laneChord, globalStep));
        // What scales the line's level: the Velocity lane, the ramp, and the retired Volume
        // parameter (pinned at its default by migrateVelTrim, so it is a multiply by one).
        // These are the *programmed* dynamics, and the audibility floor below protects them.
        const float velScale = (float) driftedLane(p, laneVelocity, globalStep) / 100.0f * rampScale
                             * ((float) juce::jlimit(0, 100, p.volume) / 100.0f);
        const int ratchets = juce::jlimit(1, 4, laneValue(p, laneRatchet, globalStep));
        const double gate = juce::jlimit(5, 200, driftedLane(p, laneGate, globalStep))
                          * juce::jlimit(5, 200, p.gate) / 10000.0;

        const double subLen = stepSamplesF / ratchets;
        // Humanize never reorders anything: it only ever pushes a hit late, and never by more
        // than 40% of the gap to the next sub-hit. Unbounded, a 25 ms nudge at a fast ratchet
        // could carry one sub-hit past the next, and two hits of one pitch arriving out of
        // order is exactly the shape emitHit's close-what-you-land-on rule cannot survive.
        // **The knob is the centre of the wander since 2026-08-19** (Owen, on the halo:
        // "should be equal from center"): the draw is humanize +/- the ring, equal both
        // sides, and the reach stops where a rail is nearer - h itself, so the floor never
        // goes early (below zero late), and 100-h, so the band stays equal rather than
        // lopsided against the ceiling. A ring at its default 100 therefore reads [0, 2h]:
        // the knob is the typical lateness, which is what a centre means. The 40% guard is
        // unchanged and still owns note ordering.
        const int hTime = juce::jlimit(0, 100, p.humanize);
        const int hReach = juce::jmin(juce::jlimit(0, 100, p.humanizeSpan),
                                      juce::jmin(hTime, 100 - hTime));
        const int maxLate = hTime > 0
                          ? (int) juce::jmin(0.025 * sr * ((hTime + hReach) / 100.0),
                                             subLen * 0.4)
                          : 0;
        const int minLate = juce::jlimit(0, maxLate,
                                         (int) (0.025 * sr * ((hTime - hReach) / 100.0)));

        // Resolve the step into pitches once, before the ratchet loop repeats them. Three
        // lanes fold in here, and all three want the note *after* the sequence walk has
        // chosen one, not instead of it.
        struct Hit { int note; float vel; int chan; };
        // Three times the base capacity: each of the line's two harmony voices can add a copy
        // of every base hit (2026-08-19). addHit drops on overflow rather than writing past
        // the end, as ever.
        // Value-initialised. Only the first `hitCount` entries are ever read, and addHit is the
        // only writer, so the tail is dead either way - but the dedup scan below reads
        // hits[i].note for i < hitCount, and cppcheck cannot prove those were written, so the
        // CI gate treats it as an uninitialised read. Zeroing about 5 KB once per *fired step*
        // is immaterial next to what the rest of fireStep does, and it costs no allocation and
        // no lock, so the audio-thread rule is untouched. Well-defined beats provably-unread.
        Hit hits[(maxHeld * 8 + ChordTable::maxNotes) * 3] {};
        int hitCount = 0;
        const auto& lead = held[0]; // whose velocity and channel a summoned chord borrows
        // **One hit per pitch per channel per step, and that is a hard rule, not tidiness.**
        // Two identical hits in one step are not a doubled note - they are a hung one. Both
        // land at the same sample offset, so the second goes down emitHit's tie branch: it
        // writes a note-off at `on - 1`, *before* the first one's note-on at `on`, and drops
        // the first from active[]. MidiBuffer sorts by sample position, so ArpMerge then sees
        // off, on, on and only one parked off - the refcount climbs to 2, the real note-off
        // takes it to 1 rather than 0 and is suppressed, and the pitch is never released. It
        // stays pinned above zero for the rest of the session, so every later hit on that
        // pitch hangs too.
        //
        // Reachable without trying: set both harmony voices to the same interval, or give a
        // chord-lane triad a + Perfect 5th voice, and C's fifth is the G already in the step.
        // The harmony loop's own guard only ever caught a copy that clamped onto *its own*
        // source. Deduping here covers every route into hits[] at once, which is where a rule
        // about the whole step belongs.
        const auto addHit = [&](int note, float vel, int chan)
        {
            const int n = juce::jlimit(0, 127, note);
            for (int i = 0; i < hitCount; ++i)
                if (hits[i].note == n && hits[i].chan == chan)
                    return;
            if (hitCount < (int) (sizeof(hits) / sizeof(hits[0])))
                hits[hitCount++] = { n, vel, chan };
        };
        const auto place = [&](int note)
        {
            // Octave lane, then Transpose - which counts *scale degrees*, not semitones.
            // Everyone else's transpose lane is chromatic and is therefore a machine for
            // leaving the key; Keys already owns Root and Scale, so the musical version is
            // the one that costs nothing (and a chromatic scale mask makes it chromatic).
            const int shifted = note + octaveShift;
            return transpose != 0 ? shiftByDegrees(shifted, transpose, p.scaleMask, p.rootPc) : shifted;
        };

        const int chordCount = (chordSel > 0 && p.chords != nullptr)
                             ? juce::jlimit(0, ChordTable::maxNotes,
                                            p.chords->count[(size_t) (chordSel - 1)].load(std::memory_order_relaxed))
                             : 0;
        if (chordCount > 0)
        {
            // The Chord lane calls up one of the twelve slots' chords for this step alone -
            // Kirnu Cream's Chordmem, except the memories are the slots Keys already has, so
            // a progression can be drawn into a lane without storing a second copy of it.
            // What is held decides only the velocity and the channel.
            for (int i = 0; i < chordCount; ++i)
                addHit(place(p.chords->note[(size_t) (chordSel - 1)][(size_t) i].load(std::memory_order_relaxed)),
                       lead.velocity * velScale, lead.channel);
        }
        else
        {
            for (int k = 0; k < playCount; ++k)
            {
                const int idx = juce::jlimit(0, seqCount - 1, playIdx[k]);
                const auto& entry = seq[(size_t) idx];
                const auto& src = held[(size_t) juce::jlimit(0, heldCount - 1, entry.heldIndex)];
                // Mutate's pitch stage lands here, on the placed note (2026-08-19): past the
                // knob's halfway point a step may stray off the chord note the walk chose -
                // in scale first, chromatic at the top. Resolved once, so the subharmonic
                // voice below offsets from the note actually played rather than the one that
                // was aimed at.
                const int played = mutatedPitch(p, place(src.note + entry.semitoneOffset), globalStep);
                addHit(played, src.velocity * velScale, src.channel);

                // Harmony: a second voice, in one of two modes (harmonyMode, 2026-08-14).
                // Mode 0, the original: this many chord tones above the one just played,
                // Cthulhu's lane. Counting in sequence entries rather than semitones is what
                // keeps it inside the chord; running off the top adds an octave instead of
                // folding back onto a note already sounding.
                if (harmony > 0 && harmonyMode.load(std::memory_order_relaxed) == 1)
                {
                    // Mode 1: subharmonic. One voice at the undertone series below the note
                    // just played (f/2..f/8, quantized to 12-TET) instead of a chord tone
                    // above it - meant to be heard with Scale Lock off, since it deliberately
                    // leaves the chord. Clamped to the MIDI range and dropped, not wrapped, if
                    // clamping collapses it onto the note it was meant to harmonize: a wrapped
                    // low note would read as a new attack rather than a silence.
                    static constexpr int kSubharmonicSemis[8] = { 0, -12, -19, -24, -28, -31, -34, -36 };
                    const int playedClamped = juce::jlimit(0, 127, played);
                    const int subClamped = juce::jlimit(0, 127, played + kSubharmonicSemis[juce::jlimit(0, 7, harmony)]);
                    if (subClamped != playedClamped)
                        addHit(subClamped, src.velocity * velScale, src.channel);
                }
                else if (harmony > 0)
                {
                    const int h = idx + harmony;
                    const auto& hEntry = seq[(size_t) (h % seqCount)];
                    const auto& hSrc = held[(size_t) juce::jlimit(0, heldCount - 1, hEntry.heldIndex)];
                    addHit(place(hSrc.note + hEntry.semitoneOffset + 12 * (h / seqCount)),
                           hSrc.velocity * velScale, hSrc.channel);
                }
            }
        }

        // The line's two fixed harmony voices (2026-08-19, BigSky's shimmer list): each adds
        // its interval to every hit the step resolved - chord-lane steps included, and
        // Mutate's stray included, since the voice reads the hits rather than re-deriving
        // them. The chance is rolled per step per voice, a hash of the step so a transport
        // jump lands on the same answer (stateless, the mutatedIndex rule); salted by the
        // voice index and by mutateSeed, so a voice's two slots - and two lines at the same
        // setting - never gate in lockstep. A hit that clamps onto its own source is dropped,
        // not doubled: a collapsed interval is a silence, the subharmonic rule above.
        {
            const int baseHits = hitCount;
            for (int s = 0; s < 2; ++s)
            {
                const int semis = juce::jlimit(-48, 48, p.harmSemis[s]);
                const int chancePct = juce::jlimit(0, 100, p.harmChance[s]);
                if (semis == 0 || chancePct <= 0)
                    continue;
                if (chancePct < 100)
                {
                    const unsigned int h = hash32((unsigned int) (long long) globalStep * 2654435761u
                                                  ^ (unsigned int) (s + 1) * 40503u
                                                  ^ (unsigned int) (p.mutateSeed * 2246822519u));
                    if ((int) (h % 100u) >= chancePct)
                        continue;
                }
                for (int k = 0; k < baseHits; ++k)
                {
                    const int target = juce::jlimit(0, 127, hits[k].note + semis);
                    if (target != hits[k].note)
                        addHit(target, hits[k].vel, hits[k].chan);
                }
            }
        }

        // Volume 0 is a mute, and a mute emits nothing. The 0.05 floor below exists so a
        // Velocity lane at 0, or a hard Humanize draw, stays audible rather than turning a
        // note-on into a note-off - but it must not also make the line's own level
        // un-silenceable, and it did: VOL at the bottom of its travel played the line quietly
        // instead of stopping it, which is the one thing a control called VOL has to do.
        //
        // Dropped here rather than by returning early, so the step is still *resolved*: the RNG
        // draw, the sequence walk and stepCounter have all happened above, and unmuting picks
        // the run up where it would have been rather than restarting it.
        if (p.volume <= 0 || p.velLevel <= 0)
            hitCount = 0;

        for (int r = 0; r < ratchets; ++r)
        {
            const int at = offset + (int) std::floor(subLen * r);
            const int durSamples = juce::jmax(1, (int) std::floor(subLen * gate));

            for (int k = 0; k < hitCount; ++k)
            {
                const auto& hit = hits[k];
                const int note = hit.note;

                int on = at;
                // **The line's own level is the velocity, not the one that arrived**
                // (2026-08-18). `hit.vel` is the source note's velocity times velScale; what
                // replaces it is this line's level times the same velScale, so the Velocity lane,
                // the ramp and Drift all still shape it exactly as they did - only the *base*
                // moved from the incoming chord to the knob. See Params::velLevel.
                float vel = (float) juce::jlimit(0, 127, p.velLevel) / 127.0f * velScale;
                // Late, never early: a nudge that can rush the grid is what Swing is for,
                // and an early hit would need to fire before the step it belongs to. Both
                // wanders are centred on their knob since 2026-08-19 - the draw lands either
                // side of it, equally - so "never louder" retired with the halo redesign:
                // the level is the band's middle now, not its top. Two knobs since
                // 2026-08-02 (humanize is the timing, humanVel the velocity), so each half
                // only runs when its own knob is up.
                if (maxLate > 0)
                    on += minLate + (int) (rng() % (unsigned) (maxLate - minLate + 1));
                if (p.humanVel > 0)
                {
                    // In **MIDI velocity units** since 2026-08-18, and **either side of the
                    // level** since 2026-08-19 (Owen, on the halo: "should be equal from
                    // center"): the knob is the band's centre, the ring is how far a hit may
                    // land above or below it, uniform across the band. The reach stops where
                    // a rail is nearer - the level itself, or 127 less it - so the band stays
                    // equal on both sides rather than piling up against an end. humanVelSpan,
                    // the ring's own former sub-span, is pinned at its default by the UI and
                    // no longer read: a centred band has no ceiling for a sub-span to hang
                    // from.
                    const int level = juce::jlimit(0, 127, p.velLevel);
                    const int reach = juce::jmin(juce::jlimit(0, 127, p.humanVel),
                                                 juce::jmin(level, 127 - level));
                    const double u = (double) (rng() % 1000u) / 1000.0;
                    vel += (float) ((2.0 * u - 1.0) * (double) reach / 127.0) * velScale;
                }
                // The 0.05 floor protects programmed dynamics: a Velocity lane at 0 or a
                // hard H.VEL draw must stay audible rather than turn into a note-off. The
                // line's fader multiplies *after* it, because turning a line down is
                // supposed to approach silence - inside the floor it pinned at velocity 6
                // from about -90 downward, which on a patch with a shallow velocity
                // response was still plainly audible (2026-08-02). The final clamp bottoms
                // at one MIDI step; zero would be a note-off in disguise.
                // The floor is one MIDI step, never zero, which would be a note-off in
                // disguise. The old 0.05 audibility floor and the fader that multiplied after it
                // both went with velTrim (2026-08-18): they existed because a *trim* had to be
                // able to reach silence past a floor protecting programmed dynamics. The level
                // is now the velocity itself, so a band drawn low is meant to be quiet and there
                // is nothing to protect it from - the knob at 0 mutes the line outright, above.
                vel = juce::jlimit(1.0f / 127.0f, 1.0f, vel);

                // A ratchet subdivides one step, and a step is routinely longer than a buffer -
                // at 1/16 and 120 bpm a step is 6000 samples against a 512-sample block - so every
                // sub-hit but the first normally belongs to a *later* block. They used to be
                // stamped at their raw offset anyway, which puts a note-on at sample 4500 of a
                // 512-sample buffer: out of range, dropped or clamped by whatever is downstream,
                // and ratchets therefore silently did nothing at any realistic buffer size. The
                // mistake is deciding an event belongs to this block because the thing that
                // *caused* it did.
                //
                // So park what does not fit and let the next block fire it. The step itself is
                // still decided exactly once, here: the RNG draw, the sequence walk and
                // stepCounter must not be repeated, which is why this parks the resolved hit
                // rather than re-deriving the step later.
                if (on < numSamples)
                    emitHit(note, hit.chan, vel, on, durSamples, numSamples, out);
                else if (pendingCount < maxPending)
                    pending[(size_t) pendingCount++] = { note, hit.chan, vel, on, durSamples };
                // else: out of carry slots, and the hit is dropped rather than mistimed. The
                // capacity is a chord's worth of ratchets over several steps; a step's carry is
                // drained before the next step fires, so nothing that fits reaches it.
            }
        }
        ++stepCounter;
    }

    // One ratchet hit: close what it lands on top of, emit its note-on, and park its note-off
    // in active[]. Shared by fireStep and by the carry-over drain, so a sub-hit that waited a
    // block behaves identically to one that fired immediately.
    void emitHit(int note, int channel, float vel, int on, int durSamples, int numSamples,
                 juce::MidiBuffer& out)
    {
        // Close whatever this hit lands on top of, before its note-on goes in, so a
        // note-off can never sort after a note-on it precedes. Two different closes:
        //   - anything already due by now ends at its own offset, on time;
        //   - the pitch being retriggered, if it is still owed *past* this hit (a tie,
        //     gate > 100%), is pulled back to just before the retrigger instead, so one
        //     pitch never stacks two note-ons with nothing between them.
        for (int i = 0; i < activeCount;)
        {
            auto& a = active[(size_t) i];
            if (a.samplesLeft <= on)
            {
                out.addEvent(juce::MidiMessage::noteOff(a.channel, a.note),
                             juce::jmax(0, a.samplesLeft));
                a = active[(size_t) --activeCount];
            }
            else if (a.note == note && a.channel == channel)
            {
                out.addEvent(juce::MidiMessage::noteOff(a.channel, a.note), juce::jmax(0, on - 1));
                a = active[(size_t) --activeCount];
            }
            else
                ++i;
        }
        out.addEvent(juce::MidiMessage::noteOn(channel, note, vel), on);

        // Where this note ends, as an offset from the start of *this* block, which is
        // the frame active[] is kept in all the way through process(); advanceBlock()
        // rebases the survivors once, at the very end.
        const int offAt = on + durSamples;
        if (activeCount < maxActive)
        {
            // Park it whatever its length. Emitting a short note's off straight into the
            // buffer looks like a harmless shortcut and is not: it hides the note from
            // the close loop above, so a same-pitch hit later in the SAME block finds
            // nothing to close and stacks a second note-on with nothing between them -
            // exactly what that loop exists to prevent. It needs a tie (gate > 100%) and
            // a block long enough to hold two hits of one pitch, which is an ordinary
            // 2048-sample buffer at a fast rate.
            //
            // Nothing is lost by parking: a later hit either ends this note at its own
            // gate (the due branch) or, if it is a tie, pulls it back under the retrigger
            // (the tie branch), and retireDue() at the end of process() emits whatever is
            // still owed inside this block.
            active[(size_t) activeCount++] = { note, channel, offAt };
        }
        else
        {
            // Out of tracking slots: end it no later than the edge of this block, rather
            // than stamping an event past the end of the buffer.
            out.addEvent(juce::MidiMessage::noteOff(channel, note),
                         juce::jmax(0, juce::jmin(offAt, numSamples - 1)));
        }
    }

    // Fire every carried-over ratchet hit due strictly before `limit`, in order. Called from
    // the step loop with the next step's offset, so a sub-hit and a step landing in the same
    // block still reach active[] in time order - which is what the close-what-you-land-on
    // rule in emitHit depends on.
    void firePendingBefore(int limit, int numSamples, juce::MidiBuffer& out)
    {
        int kept = 0;
        for (int i = 0; i < pendingCount; ++i)
        {
            const auto& h = pending[(size_t) i];
            if (h.at < limit)
                emitHit(h.note, h.channel, h.velocity, h.at, h.durSamples, numSamples, out);
            else
                pending[(size_t) kept++] = h; // shift down: pending stays in time order
        }
        pendingCount = kept;
    }

    // THE CONTRACT for active[].samplesLeft: it is the note-off's sample offset measured
    // from the start of the block currently being processed. fireStep writes it in that
    // frame, every read during the block is in that frame, and advanceBlock() rebases the
    // survivors exactly once, at the end of process(). Getting this frame wrong is what
    // shipped every arp note-off one whole buffer late for the life of v1.

    // Emit the note-off for every entry due at or before `limit` (an offset into the block
    // being processed), each at its own offset, and drop it. Entries not yet due are left
    // untouched, so this is safe to call as often as a block needs: an entry is emitted once
    // and removed with it, so there is no double-off and nothing leaks.
    void retireDue(int limit, juce::MidiBuffer& out)
    {
        for (int i = 0; i < activeCount;)
        {
            auto& a = active[(size_t) i];
            if (a.samplesLeft <= limit)
            {
                out.addEvent(juce::MidiMessage::noteOff(a.channel, a.note), juce::jmax(0, a.samplesLeft));
                a = active[(size_t) --activeCount];
            }
            else
                ++i;
        }
    }

    // Rebase the survivors onto the next block's timebase. Called once per block, right
    // after retireDue(numSamples - 1), so every survivor is due at numSamples or later and
    // samplesLeft stays >= 0. numSamples == 0 is a no-op, as it must be.
    void advanceBlock(int numSamples) noexcept
    {
        for (int i = 0; i < activeCount; ++i)
            active[(size_t) i].samplesLeft -= numSamples;
        // Carried ratchet hits live in the same frame and rebase with it. firePendingBefore
        // has already fired everything below numSamples, so every survivor stays >= 0.
        for (int i = 0; i < pendingCount; ++i)
            pending[(size_t) i].at -= numSamples;
    }

    // `semitoneOffset` used to be an octave count times twelve. It is a resolved semitone
    // shift now, because a spread in scale degrees is not a fixed interval: the same repeat
    // is 3 or 4 semitones depending which note of the chord it is lifting.
    struct SeqEntry { int heldIndex; int semitoneOffset; };

    double sr = 44100.0;
    std::array<Held, maxHeld> held {};
    int heldCount = 0;
    int physicallyHeld = 0;
    std::array<Active, maxActive> active {};
    int activeCount = 0;

    // Sub-hits carried into a later block. Sized for the worst case the Chord shape makes
    // reachable - every note of a spread chord ratcheting at once - rather than the single
    // note's three the old sixteen covered.
    static constexpr int maxPending = 96;
    std::array<PendingHit, maxPending> pending {};
    int pendingCount = 0;
    std::array<SeqEntry, maxHeld * 4> seq {};
    int seqCount = 0;
    long long stepCounter = 0;
    // Did the step before this one sound? The chain lane's whole state (see laneChain), and
    // the only thing in this engine that a transport jump cannot reconstruct from the step
    // index. It self-corrects within one step, which is why it is affordable.
    bool lastStepFired = false;
    // Which sequence entry last sounded, for the Note lane's Prev. -1 until something has.
    int lastPlayedIdx = -1;

    long long dirCursor = 0;
    // Where the lanes count from: the step index of the last restart, so Retrigger and the
    // beat-retrigger window both mean "step 1 next", and Offset counts from a known origin.
    long long stepBase = 0;
    long long lastRetrigWindow = std::numeric_limits<long long>::min();
    bool pendingRetrig = false;
    double heldBeats = 0.0;  // how long the current chord has been up, for the velocity ramp
    float rampScale = 1.0f;  // that ramp, resolved once per block
    int lastPicked = -1;     // for Random Other
    std::array<int, maxHeld * 4> perm {}; // the locked order Random Once walks
    int permCount = 0;
    bool permDirty = true;
    double freePhaseBeats = 0.0;
    double expectedNextPpq = 0.0;
    bool havePrevPpq = false;
    // The rate mode the last block ran under, so process() can spot the timebase changing
    // under it. Deliberately not cleared by hardReset(): it is not playback state but a
    // memory of what was read, and zeroing it would report a mode change that never happened
    // (harmless - hardReset has already done the same work - but it would be a lie).
    bool lastRateFree = false;
    std::mt19937 rng { 0xFAB1E5EDu }; // fixed seed: deterministic tests, free variation live
};
// Fold several arpeggiator lines' output into one stream under a single rule: **one note-on per
// sounding pitch, released by the last line holding it**, and a line striking a pitch another one
// already holds re-strikes it rather than doubling it (2026-08-18, Owen: "when there's two
// arpeggiators happening, how does it handle when there's an overlap in a note that's being
// played?").
//
// It did not handle it. Each line's buffer went straight to the output, so two lines on one
// channel sharing a pitch sent two note-ons for it and **whichever released first ended it for
// both**: the other line's note cut short, its own note-off arriving later as a stray. The lines
// are usually fed related chords, so shared pitches are the common case, and it reads as random
// dropouts rather than as a fault.
//
// This is the invariant KeysProcessor::noteRefs keeps on the UI side, applied where the engines
// meet. The re-strike is a note-off at the *same sample offset* immediately before the note-on -
// exactly what ArpEngine::fireStep already does for a tie inside one line, pulled back to just
// before the pitch sounds again rather than left to fire in the middle of the note that replaced
// it. The count is not decremented for it: the other line still owns its reference, and the pitch
// ends when that line lets go.
//
// Lives here, beside the engine and free of the processor, so it can be driven from a test with
// two hand-built buffers - the same reason ChordGen and ScaleModes are UI-free.
struct ArpMerge
{
    // `in` must already hold every line's events interleaved in sample order, which is what a
    // juce::MidiBuffer does for free. Deduplicating one line's whole buffer and then the next
    // would read an event at sample 6000 before one at sample 0, and the rule is a state machine
    // over time.
    void merge(const juce::MidiBuffer& in, juce::MidiBuffer& out)
    {
        for (const auto meta : in)
        {
            const auto m = meta.getMessage();
            const int ch = m.getChannel();
            const bool on = m.isNoteOn();
            // Everything else - the CCs, bend and clock an engine passes through from its own
            // input - goes out untouched, as does anything with no channel, which no note has.
            if (ch < 1 || ch > 16 || ! (on || m.isNoteOff()))
            {
                out.addEvent(m, meta.samplePosition);
                continue;
            }

            auto& refs = held[(size_t) ((ch - 1) * 128 + m.getNoteNumber())];
            if (on)
            {
                if (refs > 0) // another line holds it: close it so this attack is heard
                    out.addEvent(juce::MidiMessage::noteOff(ch, m.getNoteNumber()), meta.samplePosition);
                out.addEvent(m, meta.samplePosition);
                if (refs < 255)
                    ++refs;
            }
            else
            {
                if (refs > 0)
                    --refs;
                if (refs == 0) // the last line let go, so the pitch really does end here
                    out.addEvent(m, meta.samplePosition);
            }
        }
    }

    // Every path that abandons a sounding arp note emits its note-offs first (ArpEngine::flushInto
    // on the bypass edge and on a channel change), so the counts stay honest on their own. A
    // panic is the exception: it silences the instrument directly and leaves the engines to catch
    // up, and a stale count here would suppress a later note-off as "another line still holds it",
    // which is a stuck note - the precise failure these counts exist to prevent.
    void reset() { held.fill(0); }

    std::array<std::uint8_t, 16 * 128> held {};
};

} // namespace keys
