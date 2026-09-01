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

    // Round `note` onto the nearest in-scale pitch class, ties going *down* - the same walk
    // the kit's `scales::snapToScale` does on the keybed, expressed against the mask so the
    // engine stays free of the scale tables (which is the whole reason `scaleMask` is passed
    // in). An in-scale note comes back untouched, and so does every note under a **chromatic**
    // scale, where the mask is all twelve and there is nothing to snap to - checked explicitly
    // rather than left to the `in(note)` early-out, because the answer is a property of the
    // scale and saying so once is cheaper than proving it per note.
    //
    // The result can sit up to six semitones outside 0..127; `addHit` clamps, as it always has.
    static int snapToMask(int note, unsigned int mask, int rootPc) noexcept
    {
        mask &= 0xFFFu;
        if (mask == 0 || mask == 0xFFFu)
            return note;
        const auto in = [=](int n)
        { return ((mask >> ((((n - rootPc) % 12) + 12) % 12)) & 1u) != 0u; };
        if (in(note))
            return note;
        for (int d = 1; d <= 6; ++d)
        {
            if (in(note - d))
                return note - d;
            if (in(note + d))
                return note + d;
        }
        return note;
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

    // **The line bus** (2026-09-01, Owen: "can we get the arpeggiators to interact with each
    // other, like the step sequencers, so we can get interesting variations"). What this line
    // did, written as it runs and readable by the lines that run *after* it in the same block:
    // runArpLines walks the lines in letter order on the audio thread, so by the time B runs
    // everything A decided this block is already here. That ordering is the whole mechanism -
    // the same one arpNoteLines leans on - and it is why a line may only follow a letter above
    // it (see Params::follow). Per block and recomputed, so a transport jump has nothing to
    // reconstruct; the one thing that persists is firedBefore, a running count, so a follower
    // can tell "did the source fire since my last step" across blocks it had no step in.
    // `firedAt` is one entry per *step*, not per hit: a ratchet or a chord-shape step is one
    // fire. Design and the rest of the mechanisms over it: docs/LINE_INTERACTION.md.
    static constexpr int maxStepsPerBlock = 32; // a 9-tuplet 1/64 at 300 bpm in a 4096 buffer is 15
    struct LineRecord
    {
        long long firedBefore = 0;   // steps fired in every block before this one
        std::array<int, maxStepsPerBlock> firedAt {}; // this block's step fire offsets, in order
        int firedCount = 0;
        long long pass = 0;          // the Note lane's walk pass the last fired step was in
        bool lastStepFired = false;  // the Chain lane's own bit, as this block left it
        int lastNote = -1;           // the pitch the last fired step landed on
        int lastVelocity = 0;        // 1..127
        double stepSamples = 0.0;    // this line's step length, for a clocked follower later
        bool sounding = false;       // enabled and holding a chord
    };
    LineRecord record {};

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
        // Scale Lock, the keybed's own toggle, reaching the line's output (2026-08-26, Owen:
        // "does the scale lock button at the top apply to arpeggiators and harmonies?" - it did
        // not). It snaps in `addHit`, the one place every emitted pitch passes through, so it
        // covers the walk, the octave stacking, a chord-lane slot, Stray's chromatic zone and
        // the harmony voices in one rule rather than five. Root and Scale have always reached
        // the engine whatever this says; what was missing is the *lock*.
        bool scaleLock = false;
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
        // **One meaning across the whole knob again since 2026-08-21** (Owen: "it's adding
        // additional notes in the arpeggiator ... it should just change the existing ones").
        // Mutate had carried two stages on one dial from 2026-08-19: an index walk inside the
        // chord below 50 and, above it, a second stage that shoved the resolved pitch off the
        // chord entirely. That second stage is what a listener hears as notes appearing that
        // nobody put there, and it is a different question from the first - so it moved out to
        // `stray` below, its own control, defaulting to off. Mutate cannot leave the held
        // chord at any setting; what the top of its travel buys now is *reach*, in chord
        // entries, which is the axis it never spent its upper half on.
        int mutate = 0;     // 0..100
        int mutateLock = 0; // 0..100; 100 = the first variation found repeats for good
        int mutateSeed = 0; // the line index, so two lines never explore in lockstep
        // **Stray** (2026-08-21): how far a step is allowed to land *outside* the held chord,
        // and the only thing in the engine that can put a pitch there without having been
        // drawn on that step. 0 - the default, and what every session before it opens with -
        // is the promise that the line plays your chord and nothing else. Two zones on its own
        // travel: to 50 a stray is an in-scale neighbour a degree or two away, past 50 a
        // growing share are chromatic semitones, all of them at 100. Independent of Mutate on
        // purpose, so a plain Up run can pick up wrong notes without its order changing, but
        // held by the same Lock: a wrong-note lick the machine finds can still harden into the
        // part. See `mutatedPitch`.
        int stray = 0;      // 0..100
        // **Legato** (2026-09-01, Owen: "a legato button. So when the density is lower or a
        // note is skipped, it continues nicely"). A step that does not fire - Density (the
        // `chance` above), the Chance lane, a mute, a rest, a Chain condition - is silence with
        // this off: the note before it ends at its own gate. On, that note is held open through
        // the gap and released just after the next fired step's note-on, the overlap a synth's
        // legato or glide mode needs to slide rather than restart. Gate is still honoured on a
        // step whose successor fires; fireStep looks one step ahead to know which. Off is
        // byte-for-byte what the engine did before the flag existed.
        bool legato = false;
        // The line's two fixed harmony voices (2026-08-19, BigSky's shimmer list): an interval
        // in semitones added to every note the step resolved - Mutate's stray included, so the
        // voice follows the run - and how often it fires, rolled per step per voice off the
        // same stateless hash Mutate draws from. 0 semitones is Off. Chromatic on purpose;
        // the dropdown names intervals, not degrees (see the registration in PluginProcessor).
        int harmSemis[2] = { 0, 0 };
        // **A voice may be two pitches** (2026-08-21, Owen: "in the harmony, when you select
        // octave plus fifth, it looks like it only just does octave"). Every entry in the
        // shimmer list names one interval bar one, and that one says "&": "+ Octave & 5th" is
        // an octave *and* a fifth, two notes, which is how the pedal's own list reads it. The
        // engine had it as a single compound interval of 19 semitones - an octave plus a fifth
        // measured from the note rather than two voices off it - so the entry played one note
        // where its name promises two.
        //
        // A second interval per slot rather than a third and fourth voice: this is still one
        // voice, so it shares its slot's chance roll and either both pitches fire or neither.
        // 0 means the slot has only its first interval, which is every other entry in the list.
        int harmSemisB[2] = { 0, 0 };
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
        // **Who this line listens to** - another line's record, or null for nobody, which is
        // what every mechanism tests first. The `chords` shape above: a read-only pointer into
        // something the processor owns. runArpLines hands over only a line that ran *before*
        // this one (docs/LINE_INTERACTION.md, "signal flows downward"), so a record here is
        // always this block's, never last block's.
        const LineRecord* follow = nullptr;
        // **DUCK**: the odds this step is skipped when the source fired a step since this
        // line's previous one - the hocket. 0 is off and the default. Rolled on the same
        // (step, era) cell as Mutate, so LOCK holds an interlock the machine found.
        int duck = 0;             // 0..100
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

    // How many notes the engine is holding, for anything off the audio thread that wants to
    // know whether this line is sounding at all. `heldCount` itself is a plain int written by
    // noteArrived/noteReleased/the latch reset on the audio thread, so reading *that* from the
    // message thread is a data race however harmless the value looks; this is the published
    // copy, beside the two atomics that already exist for exactly this. Written wherever
    // heldCount changes, through setHeldCount().
    std::atomic<int> uiHeldCount { 0 };

    // The sequence as a snapshot, taken the way it was published. Both readers - the Draw
    // page's grid and the MCP get_state tool - go through here rather than each running their
    // own load-count-then-index loop over the atomics: one copy of the handshake, so the
    // memory ordering below is stated once and a future change to how the sequence is
    // published (a seqlock, a double buffer, a generation counter) has one place to land.
    //
    // The count is loaded acquire against buildSequence's release, which is what makes the
    // entries visible with it. Relaxed on both sides gave no happens-before at all, so a
    // reader could legally see a new, larger count against the previous chord's entries - or,
    // on the very first chord, against the zero-initialised array, which reads out as a line
    // holding C-1 four times.
    int uiSequence(std::array<int, maxHeld * 4>& out) const noexcept
    {
        const int n = juce::jlimit(0, (int) uiSeq.size(), uiSeqCount.load(std::memory_order_acquire));
        for (int i = 0; i < n; ++i)
            out[(size_t) i] = uiSeq[(size_t) i].load(std::memory_order_relaxed);
        return n;
    }

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
        prerollStep = noPreroll;
        nextSkips = false;
        seenValid = false;
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

    // **Deal Random Once a new order** (2026-08-21, Owen: "I use the random ones a lot, and I'd
    // like to have a dice button when those are active nearby to regenerate their pattern").
    //
    // The whole feature is this one bit. Random Once shuffles the sequence into `perm` and then
    // walks it, rebuilding only when the chord changes or the line restarts - which is exactly
    // what makes it a *pattern* rather than a coin flip, and exactly why it needed a way to be
    // dealt again without disturbing anything else.
    //
    // The cursor is deliberately left alone: it is the phase of the walk, so zeroing it here
    // would jolt the line back to the top of its bar as well as changing the order, and only
    // one of those two things was asked for. Random and Random Other draw fresh every step and
    // have no stored order at all, so this does nothing for them - which is why the button that
    // calls it greys outside Random Once rather than lying about what it can do.
    //
    // Called from the audio thread only (runArpLines, off an atomic the UI bumps), so it can
    // touch permDirty directly rather than being another atomic on this engine.
    void rerollRandomOrder() noexcept { permDirty = true; }

    void hardReset()
    {
        activeCount = 0;
        pendingCount = 0;
        setHeldCount(0);
        physicallyHeld = 0;
        record = LineRecord {};
        stepCounter = 0;
        dirCursor = 0;
        stepBase = 0;
        lastStepFired = false; // a restart has no step before it
        prerollStep = noPreroll;
        nextSkips = false;
        seenValid = false;
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

        // The bus rolls over here, at the top, and not at the end: a follower running later in
        // this same block reads firedBefore + this block's firedAt, so this block's fires must
        // not be in both. `sounding` is read after the input loop, so a chord that arrived in
        // this block counts.
        record.firedBefore += record.firedCount;
        record.firedCount = 0;
        record.sounding = p.enabled && heldCount > 0;

        // Legato switched off mid-hold: what it was holding ends now, at the top of this block,
        // rather than waiting for a step that will never release it with the flag down.
        if (! p.legato)
            releaseLegato();

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
        record.stepSamples = stepBeats / beatsPerSample;

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
            // A note Legato is holding waits for the next step that fires, and there is no
            // next step: the chord came up or the line went off. End it here, or it rings for
            // good - the one hang the held-open state makes possible, closed at its one exit.
            releaseLegato();
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
        record.lastStepFired = lastStepFired; // as this block leaves it, for a follower's Chain lane

        // How long this chord has been up, which is what the velocity ramp rides on. It
        // counts beats rather than seconds so a ramp written at one tempo means the same
        // thing at another, and it stops counting the moment the keys come up.
        if (heldCount > 0)
            heldBeats += blockBeats;
    }

    // Audio thread only: `heldCount` is a plain int this thread owns. Anything off it wants
    // uiHeldCount, the published copy above.
    int heldNoteCount() const noexcept { return heldCount; }

private:
    // `ons` counts how many un-matched note-ons this pitch has arrived with. The engine sits
    // downstream of a merged stream where several chord sources can each ask for the same
    // pitch, so "is it still held" is a count, not a flag. It used to be a flag plus a
    // separate physicallyHeld total, and a duplicated pitch leaked that total above zero
    // permanently - which disabled latch's fresh-chord reset and left chords arpeggiating
    // forever, stacking every card you handed it afterwards.
    struct Held { int note; float velocity; int channel; int ons; };
    // `legato`: held open by Legato until the next step that fires closes it (see emitHit's
    // closeHeld and fireStep's lookahead). While set, `samplesLeft` is not a due time - neither
    // retireDue nor emitHit's due branch may end the note, and advanceBlock leaves it alone -
    // so a note held for an hour cannot count its way into the negative.
    struct Active { int note; int channel; int samplesLeft; bool legato; };
    // A ratchet sub-hit whose fire time landed past the end of the block that decided it.
    // `at` is in the same frame as Active::samplesLeft and is rebased by the same
    // advanceBlock(); see the contract note below.
    // `hold` / `closeHeld` travel with a carried sub-hit exactly as they would have reached
    // emitHit directly: a step's first hit closes what Legato was holding *when it fires*, and
    // if that hit is parked for a later block the release has to wait with it.
    struct PendingHit { int note; int channel; float velocity; int at; int durSamples;
                        bool hold; bool closeHeld; };

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
            setHeldCount(0);
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
            held[(size_t) heldCount] = { note, vel, channel, 1 };
            setHeldCount(heldCount + 1);
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
                    setHeldCount(heldCount - 1);
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
        const long long rel = globalStep - stepBase;
        // The pass is walkPass's - one function since 2026-09-01, because the line bus publishes
        // the same number for a follower's RESET to watch, and two copies of "where does a
        // pass begin" would be two answers.
        const long long pass = walkPass(p, globalStep);
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
        // chord - which is why this stage cannot leave the harmony at any amount. Leaving the
        // chord is `mutatedPitch`'s job and Stray's alone.
        //
        // **Spread over the whole knob since 2026-08-21.** It used to be `1 + amt * 2 / 100`,
        // which is one entry until 50 and three only at exactly 100 - because the dial's upper
        // half was spent on the out-of-chord stage that has since moved to Stray. With that
        // gone the reach is what the top of the travel is for, so it grows the whole way up:
        // one entry at the bottom, four at the top. Capped at the sequence, since reaching
        // further than the chord is long only wraps back onto notes nearer reaches already
        // offer, which reads as the knob doing less the higher it goes.
        // `count - 1` needs no jmax: the early return above leaves count >= 2 here, so the
        // cap is at least 1 by construction. Guarding it again would read as if a one-note
        // sequence could reach this line, which is the one case that cannot.
        const int reach = juce::jmin(1 + amt * 3 / 100, count - 1); // 1..4
        const int delta = (int) ((h >> 8) % (unsigned) (reach * 2 + 1)) - reach;
        return ((chosen + delta) % count + count) % count;
    }

    // **Stray**: the one thing in the engine allowed to put a pitch outside the held chord
    // (2026-08-19 as Mutate's upper half; its own control from 2026-08-21, Owen: "it's adding
    // additional notes in the arpeggiator ... it should just change the existing ones").
    //
    // It was the top half of the Mutate dial for two days, and the split is the lesson rather
    // than a change of mind: "explore the chord harder" and "leave the chord" are two
    // questions, and folding them onto one knob meant you could not ask the first without
    // eventually being answered the second. Off is now a position you can *stay* at, which is
    // what makes the wander safe to turn up.
    //
    // Two zones on its own travel: to 50 a stray lands a scale degree or two from the note the
    // walk chose - in scale, out of chord; past 50 a growing share are chromatic instead, all
    // of them at 100. How *often* a step strays is the knob itself, so 100 is every step.
    //
    // Applied to the *placed* pitch, after the index walk and after place(), so a step that
    // strays still strays from the note the run actually reached - and before the per-line
    // harmony voices, which follow it. It hashes the same (step, era) cell as mutatedIndex
    // with a different salt, so Lock holds these variations exactly as it holds the index
    // ones: a wander that hardens, out-of-scale notes included.
    // `hitIndex` is which of the step's notes this is, and it is load-bearing rather than
    // tidiness (2026-08-21): a step under Chord shape resolves several hits and calls this once
    // for each, so without it every note of the step drew the same roll - one direction, one
    // degree count, applied to all of them. A held C-E-G came out as a parallel D-F-A, which is
    // the line changing key, not "a step lands on a note outside your chord". Salted, each note
    // strays on its own and the chord comes apart the way the knob's own description promises.
    // Still stateless from the playhead and still inside one `mutateCell`, so **Lock holds these
    // finds exactly as before**: the cell is (step, era) and the salt only picks a voice within
    // it, so a transport jump lands on the same answer note for note.
    int mutatedPitch(const Params& p, int note, long long globalStep, int hitIndex) const noexcept
    {
        const int amt = juce::jlimit(0, 100, p.stray);
        if (amt <= 0)
            return note;

        int step; long long era;
        mutateCell(p, globalStep, step, era);
        const unsigned int h = hash32((unsigned int) step * 3266489917u
                                      ^ (unsigned int) (era * 668265263u)
                                      ^ (unsigned int) (p.mutateSeed * 2246822519u)
                                      // 0x85ebca6b, not the 2654435761 the other salts here
                                      // use: that constant *is* 0x9e3779b1, one bit off the
                                      // avalanche word on the next line, so at hitIndex 0 -
                                      // every shape but Chord, i.e. almost every step - the
                                      // two collapsed to 0x8 and the fixed term this hash was
                                      // written to carry silently was not there. The two
                                      // constants were picked independently and happened to be
                                      // the same golden-ratio word. **Salts XORed against each
                                      // other have to be checked, not just chosen.**
                                      ^ (unsigned int) (hitIndex + 1) * 0x85ebca6bu
                                      ^ 0x9e3779b9u);
        // How often a step leaves the chord at all: never at 0, every step at 100.
        if ((int) (h % 100u) >= amt)
            return note;

        const int dir = ((h >> 14) & 1u) != 0 ? 1 : -1;
        // The chromatic share of the strays: none at 50, all of them at 100.
        if (amt > 50 && (int) ((h >> 7) % 100u) < (amt - 50) * 2)
            return juce::jlimit(0, 127, note + dir * (1 + (int) ((h >> 9) % 3u)));
        // In scale, which is the lower zone's whole meaning - and note that with Scale set to
        // **Chromatic** the mask is every pitch class, so "a scale degree or two" *is* one or
        // two semitones and the lower zone reads as the chromatic one. That is the setting
        // being honest rather than the zone leaking: there is no non-chromatic answer to
        // "stay in the chromatic scale". The two zones are two zones under any scale that
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
        // Release, paired with the acquire in uiSequence(): everything stored above becomes
        // visible to a reader that sees this count, which is the whole handshake.
        uiSeqCount.store(seqCount, std::memory_order_release);
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
        // **DUCK** (2026-09-01): skip this step if the line we follow fired a step since our
        // previous one. Counted, not flagged, so the window is exact across blocks this line
        // had no step in: the source's running total at the top of the block plus the fires it
        // has made so far *at or before this offset* - the source ran first, so its record may
        // hold fires later in this block than this step, and those have not happened yet from
        // here. Before the chance draw, like the chain condition: a ducked step did not happen
        // and must not spend a draw. The first step after Follow or DUCK comes up never ducks
        // (there is no "since my last step" yet), which is one step of warm-up and no more.
        if (p.follow != nullptr && p.duck > 0)
        {
            const long long seenNow = p.follow->firedBefore + countAtOrBefore(*p.follow, offset);
            const bool sourceFired = seenValid && seenNow > seenAtMyLastStep;
            seenAtMyLastStep = seenNow;
            seenValid = true;
            if (sourceFired && rollsCell(p, globalStep, p.duck, duckSalt))
            {
                lastStepFired = false;
                return;
            }
        }
        else
            seenValid = false;

        if (chanceFails(p, globalStep))
        {
            lastStepFired = false;
            return; // 100 always fires, 0 never does
        }
        lastStepFired = true; // everything below this point sounds

        // **Legato looks one step ahead** (2026-09-01). Whether this step's notes are held open
        // or end at their gate depends on what the *next* step does, and by the time the next
        // step is decided a gated note has already ended - so the decision has to be made now.
        // The deterministic parts (chain, mute, rest) are simply read early; the chance draw is
        // made early and kept, and chanceFails() hands the same answer back when that step
        // arrives, so the note is held for exactly the skip that was foreseen. Only with Legato
        // on: off, no draw is made ahead and the step is decided where it always was.
        nextSkips = p.legato && prerollNext(p, globalStep);

        // Position Reset, after the step has survived mute, rest, chain and chance, and before
        // anything asks the walk where it is: a step that did not sound did not reach its reset
        // either, which is what keeps a reset on a low-Chance step from firing on the passes the
        // step itself skipped.
        if (laneValue(p, laneReset, globalStep) > 0)
            dirCursor = 0;

        buildSequence(p);
        if (seqCount == 0)
            return;

        // On the bus: this step fired. After the empty-sequence return above, since a step that
        // sounds nothing is not something a follower should duck to.
        record.pass = walkPass(p, globalStep);
        if (record.firedCount < maxStepsPerBlock)
            record.firedAt[(size_t) record.firedCount++] = offset;

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
        // `src` is the index of the hit this one harmonises, or its own index if it is not a
        // harmony voice - which is what lets a voice take the same velocity draw as the note it
        // is thickening (2026-08-23, Owen: "harmony same velocity"). `vel` is **decided in the
        // ratchet loop below and not here**: it used to carry the incoming note's velocity,
        // which has been dead weight since `velLevel` replaced it (2026-08-18) - written at
        // every call site, copied by the harmony voices, and then ignored by the one place that
        // emits a note. That dead copy is exactly why the voice loop already looked as though it
        // shared its source's loudness. Now the field means something and it does.
        struct Hit { int note; int chan; int src; float vel; };
        // Five times the base capacity: the line has two harmony voices and a voice may name
        // **two** intervals (2026-08-19; the second interval 2026-08-21), so between them they
        // can add four copies of every base hit. The comment said three while a voice could
        // only add one copy each, and the number moved with it rather than being left to be
        // rediscovered. In practice addHit dedups on (note, channel), so a step can hold at
        // most 128 entries per channel and the ceiling is never the binding constraint - but
        // the array is sized from what the writers can *attempt*, because the dedup is a
        // property of the notes that happen to be held and the capacity must not be. addHit
        // drops on overflow rather than writing past the end, as ever.
        // Value-initialised. Only the first `hitCount` entries are ever read, and addHit is the
        // only writer, so the tail is dead either way - but the dedup scan below reads
        // hits[i].note for i < hitCount, and cppcheck cannot prove those were written, so the
        // CI gate treats it as an uninitialised read. Zeroing about 11 KB once per *fired step*
        // is immaterial next to what the rest of fireStep does, and it costs no allocation and
        // no lock, so the audio-thread rule is untouched. Well-defined beats provably-unread.
        // (680 entries at **16** bytes - about 10.6 KB - up from 408 entries when the capacity
        // was *3, and from 12 bytes before Hit carried `src` on 2026-08-23. That field grew the
        // per-step memset by a third and this line did not move with it for a day, which is the
        // standing lesson wearing different clothes: **a size asserted in a comment goes stale
        // silently.** It is still the number to weigh if a third harmony voice is proposed.)
        Hit hits[(maxHeld * 8 + ChordTable::maxNotes) * 5] {};
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
        // `src` defaults to "this hit is its own source"; only the harmony voices pass one.
        // Returns the index of the hit this pitch now occupies - the existing one when the
        // dedup above claims it, the new one otherwise, and -1 only on overflow. A harmony
        // voice needs that index to name its source, and on the dedup path it must name the
        // hit that is *actually sounding* rather than the one that was refused: thickening a
        // hit that was dropped would leave the voice reading a velocity nobody drew.
        // **Scale Lock lands here and nowhere else.** Every pitch the line emits is written
        // through this lambda, so snapping on the way in covers the direction walk, Octave and
        // Transpose, a chord called up by the Chord lane, Stray's chromatic zone and both
        // harmony voices at once - and it snaps *before* the dedup, which is the half that
        // matters: two notes that round onto one pitch have to collapse to one hit, and a
        // harmony voice that lands on its own source has to be dropped, exactly as an interval
        // that clamped there already is. Snapping is idempotent, so a keybed note that arrived
        // pre-snapped passes through untouched.
        //
        // Locking Keys' output to the scale is what the toggle says, so it wins over Stray's
        // upper zone rather than the other way about: with Lock on, Stray's chromatic strays
        // round back into the key and its two zones read as one. That is the switch doing its
        // job, not a collision - untick Lock to hear the wrong notes it exists to prevent.
        const auto addHit = [&](int note, int chan, int src = -1)
        {
            const int n = juce::jlimit(
                0, 127, p.scaleLock ? snapToMask(note, p.scaleMask, p.rootPc) : note);
            for (int i = 0; i < hitCount; ++i)
                if (hits[i].note == n && hits[i].chan == chan)
                    return i;
            if (hitCount < (int) (sizeof(hits) / sizeof(hits[0])))
            {
                hits[hitCount] = { n, chan, src >= 0 ? src : hitCount, 0.0f };
                return hitCount++;
            }
            return -1;
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
                       lead.channel);
        }
        else
        {
            for (int k = 0; k < playCount; ++k)
            {
                const int idx = juce::jlimit(0, seqCount - 1, playIdx[k]);
                const auto& entry = seq[(size_t) idx];
                const auto& src = held[(size_t) juce::jlimit(0, heldCount - 1, entry.heldIndex)];
                // Stray lands here, on the placed note (2026-08-19; its own knob from
                // 2026-08-21): above zero a step may leave the chord note the walk chose -
                // in scale first, chromatic at the top. It is the only stage that can, which
                // is why Mutate above it is safe to turn up.
                //
                // **The two harmony modes part here, on purpose.** The subharmonic voice
                // (mode 1) offsets from `played`, the note actually sounding, because it is a
                // fixed semitone interval and an interval measured from a note nobody heard
                // is not the interval. The chord-tone voice (mode 0) counts from `idx`, the
                // *un-strayed* index, because it counts sequence entries rather than
                // semitones and that counting is the whole of what keeps it inside the chord
                // - a strayed note is by definition not a chord tone, so "two chord tones
                // above it" has no answer to give. So a straying step under mode 0 is one
                // note off the chord against a harmony still in it, which is the reading that
                // makes Stray a wrong note rather than a key change. Do not "fix" the mode-0
                // branch to read `played`; that is what the line below deliberately does not
                // do.
                const int played = mutatedPitch(p, place(src.note + entry.semitoneOffset),
                                                globalStep, k);
                // The index this pitch landed at, so the Harmony *lane*'s voice below can name
                // it as its source and take its velocity draw (2026-08-24). The fixed per-line
                // voices further down have done this since the draw was shared; the lane's two
                // modes were left passing the default, so they went on rolling their own number
                // and a lane harmony could still arrive `2 * humanVel` from the note it was
                // thickening - the reported bug surviving by the one route the fix did not
                // reach. **One rule, no carve-out**: every voice that thickens a hit reads that
                // hit's velocity, whether the interval came from a lane or from a card.
                const int srcHit = addHit(played, src.channel);

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
                        addHit(subClamped, src.channel, srcHit);
                }
                else if (harmony > 0)
                {
                    const int h = idx + harmony;
                    const auto& hEntry = seq[(size_t) (h % seqCount)];
                    const auto& hSrc = held[(size_t) juce::jlimit(0, heldCount - 1, hEntry.heldIndex)];
                    addHit(place(hSrc.note + hEntry.semitoneOffset + 12 * (h / seqCount)),
                           hSrc.channel, srcHit);
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
                const int semisB = juce::jlimit(-48, 48, p.harmSemisB[s]);
                const int chancePct = juce::jlimit(0, 100, p.harmChance[s]);
                // **Both intervals, not just the first.** A slot is silent only when it names
                // no interval at all; testing `semis` alone would skip a slot whose first
                // interval is 0 and whose second is not, and such an entry would register in
                // all three parallel tables, pass every jassert, show up in the dropdown and
                // play nothing - with no symptom anywhere to say why.
                if ((semis == 0 && semisB == 0) || chancePct <= 0)
                    continue;
                if (chancePct < 100)
                {
                    const unsigned int h = hash32((unsigned int) (long long) globalStep * 2654435761u
                                                  ^ (unsigned int) (s + 1) * 40503u
                                                  ^ (unsigned int) (p.mutateSeed * 2246822519u));
                    if ((int) (h % 100u) >= chancePct)
                        continue;
                }
                // Both of the slot's intervals, inside the one chance roll above: a voice that
                // names two pitches is one voice, so it must not half-fire.
                for (const int iv : { semis, semisB })
                {
                    if (iv == 0)
                        continue;
                    for (int k = 0; k < baseHits; ++k)
                    {
                        const int target = juce::jlimit(0, 127, hits[k].note + iv);
                        if (target != hits[k].note)
                            addHit(target, hits[k].chan, k);
                    }
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

        // The line's level plus this draw's share of the Humanize Velocity band. Lifted out of
        // the hit loop so a hit and its harmony voices can be handed **one** answer rather than
        // each rolling its own - see where it is called, and Hit::src.
        const auto humanisedVelocity = [&]
        {
            // **The line's own level is the velocity, not the one that arrived** (2026-08-18).
            // The Velocity lane, the ramp and Drift all still shape it through velScale exactly
            // as they did; only the *base* moved from the incoming chord to the knob. See
            // Params::velLevel.
            float v = (float) juce::jlimit(0, 127, p.velLevel) / 127.0f * velScale;
            if (p.humanVel > 0)
            {
                // In **MIDI velocity units** since 2026-08-18, and **either side of the level**
                // since 2026-08-19 (Owen, on the halo: "should be equal from center"): the knob
                // is the band's centre, the ring is how far a hit may land above or below it,
                // uniform across the band. The reach stops where a rail is nearer - the level
                // itself, or 127 less it - so the band stays equal on both sides rather than
                // piling up against an end. humanVelSpan, the ring's own former sub-span, is
                // pinned at its default by the UI and no longer read: a centred band has no
                // ceiling for a sub-span to hang from.
                const int level = juce::jlimit(0, 127, p.velLevel);
                const int reach = juce::jmin(juce::jlimit(0, 127, p.humanVel),
                                             juce::jmin(level, 127 - level));
                const double u = (double) (rng() % 1000u) / 1000.0;
                v += (float) ((2.0 * u - 1.0) * (double) reach / 127.0) * velScale;
            }
            return v;
        };

        for (int r = 0; r < ratchets; ++r)
        {
            const int at = offset + (int) std::floor(subLen * r);
            const int durSamples = juce::jmax(1, (int) std::floor(subLen * gate));
            // Legato holds only the *last* sub-hit of a ratchet: the earlier repeats end at
            // their own gate so the ratchet still reads as one, and it is the last one that
            // would otherwise leave a silence before the next step that fires.
            const bool hold = nextSkips && r == ratchets - 1;

            for (int k = 0; k < hitCount; ++k)
            {
                auto& hit = hits[k];
                const int note = hit.note;

                int on = at;
                // Late, never early: a nudge that can rush the grid is what Swing is for, and
                // an early hit would need to fire before the step it belongs to. So H.TIME's
                // band is the one that is *not* centred on its knob - the low rail is
                // zero-late - where the velocity half genuinely is (2026-08-19). That half is
                // drawn in humanisedVelocity above and explained there; this branch is the
                // timing alone, and only runs when its own knob is up.
                if (maxLate > 0)
                    on += minLate + (int) (rng() % (unsigned) (maxLate - minLate + 1));
                // **A hit and its harmony voices share one velocity draw** (2026-08-23, Owen:
                // "harmony same velocity"). A hit that is its own source draws; a harmony voice
                // reads the answer its source already stored. Voices are always appended after
                // the hit they copy and this loop runs forward, so the source is decided by the
                // time a voice asks for it.
                //
                // **Per ratchet, deliberately.** This sits inside the ratchet loop, so each
                // repeat of a ratcheted step still draws afresh and a roll keeps its life -
                // deciding it once per step would have flattened every repeat to one velocity,
                // which is a different feature and not the one that was asked for. What is
                // shared is a hit and its harmony *within* one repeat.
                if (hit.src == k)
                    hit.vel = humanisedVelocity();
                // Every voice is appended after the hit it copies, so its source is always at a
                // lower index and has already drawn on this ratchet. Asserted rather than left
                // in prose: a voice derived from a voice would read a source that has not drawn
                // yet, which on r == 0 is the value-initialised 0.0f clamped to 1/127 - an
                // inaudible note and no error anywhere.
                jassert(hit.src >= 0 && hit.src <= k);
                float vel = hits[hit.src].vel;
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
                // The step's first hit is what releases whatever Legato was holding: the
                // release lands just after this note-on, so the two overlap by a sample and a
                // synth in legato mode glides across the join instead of restarting.
                const bool closeHeld = r == 0 && k == 0;
                if (closeHeld) // the step's first hit is what the bus calls "the note it landed on"
                {
                    record.lastNote = note;
                    record.lastVelocity = juce::jmax(1, juce::roundToInt(vel * 127.0f));
                }
                if (on < numSamples)
                    emitHit(note, hit.chan, vel, on, durSamples, numSamples, out, hold, closeHeld);
                else if (pendingCount < maxPending)
                    pending[(size_t) pendingCount++] = { note, hit.chan, vel, on, durSamples,
                                                         hold, closeHeld };
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
                 juce::MidiBuffer& out, bool hold = false, bool closeHeld = false)
    {
        // Close whatever this hit lands on top of, before its note-on goes in, so a
        // note-off can never sort after a note-on it precedes. Two different closes:
        //   - anything already due by now ends at its own offset, on time;
        //   - the pitch being retriggered, if it is still owed *past* this hit (a tie,
        //     gate > 100%), is pulled back to just before the retrigger instead, so one
        //     pitch never stacks two note-ons with nothing between them.
        // A note Legato is holding has no due time (see Active::legato), so the due branch
        // skips it; the tie branch does not, because a held pitch retriggered on itself is
        // still two note-ons for one pitch unless the first is closed before the second.
        for (int i = 0; i < activeCount;)
        {
            auto& a = active[(size_t) i];
            if (! a.legato && a.samplesLeft <= on)
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

        // Legato's release: every note it was holding ends one sample *after* this note-on -
        // the other way round from the tie branch above, and on purpose. A tie is the same
        // pitch, where the off must come first or the voice stacks; this is a different pitch,
        // where the on must come first or a synth in legato mode hears a gap and restarts its
        // envelope instead of gliding. Clamped to the buffer's last sample, where insertion
        // order still puts the off after the on. Runs before this hit is parked, so a hit that
        // is itself held (the next step skips too) is never released by its own arrival.
        if (closeHeld)
        {
            const int offAt = juce::jmin(on + 1, numSamples - 1);
            for (int i = 0; i < activeCount;)
            {
                auto& a = active[(size_t) i];
                if (a.legato)
                {
                    out.addEvent(juce::MidiMessage::noteOff(a.channel, a.note), juce::jmax(0, offAt));
                    a = active[(size_t) --activeCount];
                }
                else
                    ++i;
            }
        }

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
            active[(size_t) activeCount++] = { note, channel, hold ? 0 : offAt, hold };
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
                emitHit(h.note, h.channel, h.velocity, h.at, h.durSamples, numSamples, out,
                        h.hold, h.closeHeld);
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
            if (! a.legato && a.samplesLeft <= limit)
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
            if (! active[(size_t) i].legato) // a held note has no due time to rebase
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
    // heldCount and its published copy move together, always: two writers for one number is
    // exactly how the copy goes stale, and a stale "is this line sounding" is worse than not
    // reporting one. Every write to heldCount goes through here.
    void setHeldCount(int n) noexcept
    {
        heldCount = n;
        uiHeldCount.store(n, std::memory_order_relaxed);
    }
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

    // **Legato's lookahead** (2026-09-01). `prerollStep` names the step whose chance draw has
    // already been made, `prerollFails` is that draw, and `nextSkips` is what fireStep decided
    // about the step after the one it is firing: hold this step's notes open, or let them end
    // at their gate. The draw is made ahead only with Legato on, and a preroll for a step that
    // never arrives (a transport jump) is simply discarded - chanceFails() rolls afresh for any
    // step it has no answer for. This is the second thing in the engine, after `lastStepFired`,
    // that is not stateless from the playhead, and like it the damage of being wrong is bounded
    // to one step: a note held for a skip that fired is closed by that step's own note-on.
    static constexpr long long noPreroll = std::numeric_limits<long long>::min();
    long long prerollStep = noPreroll;
    bool prerollFails = false;
    bool nextSkips = false;

    // **DUCK's window** (2026-09-01): the source's fire count as this line last saw it at one of
    // its own steps. `seenValid` is false until a step has looked, so the first step after
    // Follow or DUCK comes up compares against nothing and never ducks.
    long long seenAtMyLastStep = 0;
    bool seenValid = false;
    static constexpr unsigned int duckSalt = 0x9E3779B9u; // so DUCK and Mutate never roll the same number on one cell

    // Which pass of the Note lane's walk window `globalStep` is in. **Counted in lane cells,
    // not raw steps.** This used to live inside mutateCell and came off `rel` directly, which
    // is only the same thing while the lane runs at x1 with no Offset: at Speed x2 one pass of
    // an eight-step window takes sixteen raw steps, so the era advanced twice per pass and a
    // "locked" variation audibly changed halfway round the loop. `k` is laneStepIndex's own
    // walk position (divider first, then Offset, exactly as it orders them). Since 2026-09-01
    // it is also what the line bus publishes as `record.pass`, so LOCK's era and a follower's
    // RESET measure a pass the same way.
    long long walkPass(const Params& p, long long globalStep) const noexcept
    {
        const auto sh = lanes.shapeOf(laneNote);
        const int len = juce::jlimit(1, maxSteps, sh.len);
        int from = juce::jlimit(0, len - 1, sh.loopFrom);
        int to = juce::jlimit(0, len - 1, sh.loopTo);
        if (to < from)
            std::swap(from, to);
        const int span = juce::jmax(1, to - from + 1);
        const long long rel = globalStep - stepBase;
        const long long k = (rel >> juce::jlimit(0, 2, sh.div)) + p.offset;
        return (k >= 0 ? k : k - span + 1) / span;
    }

    // A percentage roll on this step's (step, era) cell - Mutate's own cell, so LOCK holds the
    // answer - salted so two rolls on one cell are two numbers.
    bool rollsCell(const Params& p, long long globalStep, int pct, unsigned int salt) const noexcept
    {
        int step; long long era;
        mutateCell(p, globalStep, step, era);
        const unsigned int h = hash32((unsigned int) step * 2654435761u
                                      ^ (unsigned int) (era * 40503u)
                                      ^ (unsigned int) (p.mutateSeed * 2246822519u)
                                      ^ salt);
        return (int) (h % 100u) < juce::jlimit(0, 100, pct);
    }

    // How many of the source's fires this block landed at or before `offset` - the ones that
    // have happened from the point of view of a step at that offset.
    static int countAtOrBefore(const LineRecord& r, int offset) noexcept
    {
        int n = 0;
        for (int i = 0; i < r.firedCount; ++i)
            if (r.firedAt[(size_t) i] <= offset)
                ++n;
        return n;
    }

    // The step's chance draw, or the one made ahead of it by prerollNext when Legato is on.
    bool chanceFails(const Params& p, long long globalStep)
    {
        if (prerollStep == globalStep)
        {
            prerollStep = noPreroll;
            if (p.legato)
                return prerollFails;
        }
        const int chance = driftedLane(p, laneProbability, globalStep)
                         * juce::jlimit(0, 100, p.chance) / 100;
        return (int) (rng() % 100u) >= chance;
    }

    // Will the step after `globalStep` skip? The same four questions fireStep asks at its top,
    // in the same order, read one step early: chain against the step now firing (so
    // lastStepFired is already this step's answer), mute, rest, then the chance draw - which is
    // kept for chanceFails() to hand back, so the step that was foreseen to skip does skip.
    bool prerollNext(const Params& p, long long globalStep)
    {
        const long long next = globalStep + 1;
        if (const int chain = laneValue(p, laneChain, next); chain != 0)
            if ((chain == 1) != lastStepFired)
                return true;
        if (laneValue(p, laneMute, next) > 0)
            return true;
        if (laneValue(p, laneNote, next) <= noteRest)
            return true;
        const int chance = driftedLane(p, laneProbability, next)
                         * juce::jlimit(0, 100, p.chance) / 100;
        prerollFails = (int) (rng() % 100u) >= chance;
        prerollStep = next;
        return prerollFails;
    }

    // Turn every note Legato is holding into one due at the top of this block; retireDue at
    // the end of process() then ends it. Called when the flag goes off and when nothing is
    // left to release it (the chord released, the line switched off).
    void releaseLegato() noexcept
    {
        for (int i = 0; i < activeCount; ++i)
            if (active[(size_t) i].legato)
            {
                active[(size_t) i].legato = false;
                active[(size_t) i].samplesLeft = 0;
            }
    }
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
