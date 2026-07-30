#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <okstudio/Polyrhythm.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace keys
{
// The probabilistic note chooser (see docs/CHANCE_DESIGN.md). Pure, UI-free, and
// allocation/lock-free on the audio thread, like ArpEngine beside it.
//
// It decides *what to play* out of the notes you are holding: whether a step fires,
// which member of the arp's candidate pool sounds, and that hit's velocity, gate,
// ratchet count and timing nudge. It emits no MIDI and owns no clock. ArpEngine calls
// advance() once per step and plays the answer, so there is exactly one scheduler in
// Keys and all the note-off bookkeeping stays where it already works.
//
// SELECTION, NOT INVENTION. Every pitch it can return is a note already in the arp's
// pool, which is the held set crossed with the octave range. So it can never produce
// an out-of-key note, never needs snapToScale, and needs no scale resolution of its
// own: whatever Scale Lock did at the input surfaces still holds. The Key control
// re-weights that pool toward the notes that matter rather than inventing new ones,
// which is both safer and truer to a plugin whose whole premise is that it is played.
//
// DETERMINISM IS THE FEATURE. Deja vu means "play that again", which is meaningless if
// a draw depends on when the audio callback happened to run. So nothing here touches
// ArpEngine's std::mt19937 (whose variation comes from real-time call ordering and is
// unreplayable by construction). Every draw descends from one hash stream keyed by the
// seed, and one 64-bit step seed fans out into the whole step, which is the part that
// makes a locked loop sound like a figure rather than a statistic. Marbles does this
// by storing a seed per loop cell and expanding it through an LCG; the same idea, and
// the reason recycling a step recycles its character and not merely its gate.
class ChanceEngine
{
public:
    static constexpr int maxPool = 64;  // ArpEngine::maxHeld * 4, its whole seq[]
    static constexpr int maxVoices = 3;

    // The grid Density morphs across. Sixteen because the arp's Rate already chooses how
    // fast a step is, so this only has to be a musically even bar's worth of steps; it is
    // not worth a control of its own.
    static constexpr int euclidSteps = 16;

    // What decides whether a step fires.
    //   coin   - Bernoulli per step at Density. Scattered, deliberately ungridded.
    //   euclid - E(k, 16) with k morphed by Density. Even and repeatable at every density,
    //            because densityPulses redistributes pulses instead of thinning them.
    //   bursts - euclid onsets, each of which may ratchet into 2..4 sub-hits.
    //
    // These are not Marbles' three t modes. Its Complementary Bernoulli is coin (one
    // shared draw deciding between two outcomes collapses to a coin when there is one
    // output, which is the case here). Its Clusters mode makes integer multiples of the
    // clock, which is what bursts does with ratchets. Its Drums mode replays stored
    // eight-step kick/snare tables, which has no meaning for a melodic arpeggiator, so it
    // is not reproduced.
    enum class TMode { coin = 0, euclid, bursts };
    enum class XMode { line = 0, duet, cluster };
    static constexpr int numTModes = 3;
    static constexpr int numXModes = 3;

    struct Params
    {
        bool enabled = false;
        int density = 50;      // 0..100
        int dejaVu = 50;       // 0..100; 50 is the frozen loop, both ends disturb it
        int loopLen = 8;       // 1..16
        int jitter = 0;        // 0..100
        int spread = 50;       // 0..100
        int bias = 0;          // -100..100, low register to high
        int temperature = 35;  // 0..100
        int wander = 60;       // 0..100
        int key = 70;          // 0..100
        int chordPull = 50;    // 0..100
        TMode tMode = TMode::coin;
        XMode xMode = XMode::line;
    };

    // Per-pitch-class weight, 0..255, built on the message thread from Root, Mode and the
    // live chord (see buildHarmony). Absolute pitch classes, not root-relative, so the
    // audio thread only ever does weight[pitch % 12].
    struct Harmony
    {
        std::array<juce::uint8, 12> weight { {} };
        bool valid = false;
    };

    struct Decision
    {
        bool fires = false;
        int voices = 0;
        std::array<int, maxVoices> poolIndex { {} };
        float velocityScale = 1.0f;  // multiplies the held note's velocity
        float gateScale = 1.0f;      // fraction of the step
        int ratchets = 1;
        float jitterFrac = 0.0f;     // -0.5..+0.5 of a step
    };

    //==========================================================================
    // The threshold ladder behind the Key control. Any pitch class whose weight falls
    // below the active threshold is masked out of the pool; what survives is what can
    // sound. Turning Key up therefore collapses the material in stages rather than
    // fading between two states, which is Marbles' quantizer idea and the reason its
    // high end sounds deliberate instead of degenerate.
    //
    // Marbles wraps this in an eight-state hysteresis because it is quantizing a CV that
    // can sit and wobble on a boundary. Key is an integer knob and cannot wobble, so the
    // hysteresis is dropped: it would only add a control whose position did not fully
    // determine its behaviour, which is worse than useless in a plugin.
    static constexpr int numKeyStates = 7;
    static constexpr int keyThreshold[numKeyStates] = { 0, 16, 32, 64, 128, 192, 255 };

    // Degree weights, chosen so the ladder above walks a musical path:
    // chromatic, then in-scale, then pentatonic-ish, then the triad, then the root
    // alone. The fifth deliberately outranks 192 so it survives one state longer than
    // the third, which is what produces the familiar root-then-fifth-then-octaves
    // collapse at the top of the control.
    static constexpr juce::uint8 wRoot     = 255;
    static constexpr juce::uint8 wFifth    = 224;
    static constexpr juce::uint8 wThird    = 192;
    static constexpr juce::uint8 wColour   = 128;  // 2nd, 6th
    static constexpr juce::uint8 wTendency = 64;   // 4th, 7th
    static constexpr juce::uint8 wOther    = 96;   // any other in-scale degree
    static constexpr juce::uint8 wOutside  = 8;    // not in the scale at all

    /** Build the pitch-class weights for a key, a mode's intervals, and the live chord.
        Message thread: it allocates nothing but it walks vectors and is not worth doing
        per block. `chordMask` is a bit per pitch class (bit 0 = C); pass 0 for none.
        `chordPull` 0..100 boosts chord tones over the rest of the scale. */
    static Harmony buildHarmony (int rootPitchClass, const std::vector<int>& intervals,
                                 juce::uint16 chordMask, int chordPull)
    {
        Harmony h;
        const int root = ((rootPitchClass % 12) + 12) % 12;

        for (int pc = 0; pc < 12; ++pc)
            h.weight[(size_t) pc] = wOutside;

        for (const auto interval : intervals)
        {
            const int deg = ((interval % 12) + 12) % 12;
            const int pc = (root + deg) % 12;

            juce::uint8 w = wOther;
            switch (deg)
            {
                case 0:            w = wRoot;     break;
                case 7:            w = wFifth;    break;
                case 3: case 4:    w = wThird;    break;
                case 2: case 9:    w = wColour;   break;
                case 5: case 11:   w = wTendency; break;
                default:           w = wOther;    break;
            }
            // A mode can name the same pitch class twice (it cannot in the shipped table,
            // but Blues and the pentatonics show the table is not just seven-note), so
            // keep the strongest reading rather than the last one.
            h.weight[(size_t) pc] = juce::jmax (h.weight[(size_t) pc], w);
        }

        // Chord Pull is the second adherence axis: it lifts the tones of the chord you are
        // actually holding above the rest of the key. Keys has a live chord where a
        // hardware quantizer has none, so this is the one control here with no antecedent.
        const float pull = juce::jlimit (0, 100, chordPull) / 100.0f;
        if (chordMask != 0 && pull > 0.0f)
        {
            for (int pc = 0; pc < 12; ++pc)
            {
                if ((chordMask & (juce::uint16) (1u << pc)) == 0)
                    continue;
                const float w = (float) h.weight[(size_t) pc];
                h.weight[(size_t) pc] = (juce::uint8) juce::jlimit (0.0f, 255.0f,
                                                                    w + (255.0f - w) * pull);
            }
        }

        h.valid = true;
        return h;
    }

    //==========================================================================
    // Primed at construction, because a default-constructed DejaVuSequence has an all-zero
    // ring and would hand back the same value every step: one frozen note forever, which
    // looks like a dead engine rather than an unseeded one.
    ChanceEngine() { prepare (0x5EEDC0DEull, 8); }

    void prepare (juce::uint64 newSeed, int loopLen)
    {
        base = newSeed | 1ull;
        deja.reset (base, 0, laneDejaVu, juce::jlimit (1, 16, loopLen));
        for (auto& p : poles)
            p = 0.5f;
        lastLoopLen = juce::jlimit (1, 16, loopLen);
    }

    /** A new phrase. Everything downstream is a pure function of this, so the same seed
        replays the same phrase exactly: that is what makes Generate and the deja-vu lock
        mean anything. */
    void setSeed (juce::uint64 newSeed) { prepare (newSeed, lastLoopLen); }
    juce::uint64 seed() const noexcept { return base; }

    /** A DAW loop should replay identically, so the caller resets us when it sees a
        transport jump (ArpEngine already detects one to flush its owed note-offs). Marbles
        never needs this because hardware has no transport; a plugin does. */
    void resync() { prepare (base, lastLoopLen); }

    /** One step forward. MUST be called exactly once per step whether or not the step
        fires, or the loop drifts out of phase with the grid.

        `poolPitches` is the arp's candidate pool: ascending MIDI pitches, the held set
        crossed with the octave range. `gridStep` is the host's own step index, which the
        Euclidean modes mask against so their pattern stays locked to the bar: counting calls
        internally instead would start the pattern wherever the arp happened to be switched
        on, and would drift on every DAW loop. Returns which candidates sound and how. */
    Decision advance (const Params& p, const Harmony& harmony,
                      const int* poolPitches, int poolSize, juce::int64 gridStep)
    {
        Decision d;

        if (juce::jlimit (1, 16, p.loopLen) != lastLoopLen)
        {
            // Length is a live control, and the loop's contents are indexed relative to
            // its length, so a change has to rebuild it rather than reinterpret it.
            lastLoopLen = juce::jlimit (1, 16, p.loopLen);
            deja.length = lastLoopLen;
        }

        // One loop-locked draw, expanded into the whole step. Every sub-decision below
        // descends from this, so locking the loop locks the step's entire character.
        const float dv = deja.advance (juce::jlimit (0, 100, p.dejaVu) / 100.0f);
        const juce::uint64 stepSeed = okstudio::poly::mix (
            base ^ (juce::uint64) (dv * 16777216.0f) ^ 0xC1A5E5EEDull);

        if (! p.enabled || poolSize <= 0)
            return d;

        //----------------------------------------------------------------------
        // t: does it fire
        const float density = juce::jlimit (0, 100, p.density) / 100.0f;
        bool onset = false;
        switch (p.tMode)
        {
            case TMode::coin:
                onset = sub01 (stepSeed, laneFire) < density;
                break;
            case TMode::euclid:
            case TMode::bursts:
            {
                // The programmed k is half the grid, so Density 50 is a straight eighth-note
                // feel on a sixteenth grid and the morph has room either side of it.
                const int k = okstudio::poly::densityPulses (euclidSteps / 2, euclidSteps, density);
                onset = okstudio::poly::maskStep (okstudio::poly::euclid (k, euclidSteps),
                                                  euclidSteps, gridStep);
                break;
            }
        }

        if (! onset)
            return d;
        d.fires = true;

        //----------------------------------------------------------------------
        // X: which notes
        // Weight each candidate by its pitch class, masked by the Key ladder, sharpened or
        // flattened by Temperature, then shaped by where Spread and Bias want the register
        // to sit. Roulette over the product, as ChordGen does for chords.
        const int state = juce::jlimit (0, numKeyStates - 1,
                                        juce::jlimit (0, 100, p.key) * (numKeyStates - 1) / 100);
        const int threshold = keyThreshold[state];

        // P_i proportional to w_i^(1/T): at T = 0 this collapses onto the single
        // highest-weighted candidate, at T = 1 every weight is erased and the pool is
        // uniform. The softmax reparameterized over weights we already have.
        const float t01 = juce::jlimit (0, 100, p.temperature) / 100.0f;
        const float invT = 1.0f / juce::jmax (0.02f, t01);

        const int n = juce::jmin (poolSize, maxPool);
        std::array<float, maxPool> w {};
        float total = 0.0f;

        const float posTarget = 0.5f + juce::jlimit (-100, 100, p.bias) / 200.0f;
        const float spread = juce::jlimit (0, 100, p.spread) / 100.0f;

        // Key is applied on its own first, so the two ways the pool can empty stay
        // distinguishable. A mask that removed everything is a real state with a defined
        // answer; a position envelope that lands between candidates is a bug, and folding
        // both into one "total is zero" fallback hid exactly that bug once already.
        std::array<float, maxPool> pitchWeight {};
        int passed = 0;
        for (int i = 0; i < n; ++i)
        {
            const int pc = ((poolPitches[i] % 12) + 12) % 12;
            const float raw = harmony.valid ? (float) harmony.weight[(size_t) pc] : 255.0f;
            if (harmony.valid && (int) raw < threshold)
                continue;  // masked by Key
            pitchWeight[(size_t) i] = std::pow (juce::jmax (raw, 1.0f) / 255.0f, invT);
            ++passed;
        }

        if (passed == 0)
        {
            // Nothing being held is in the key at all. Falling silent would read as a broken
            // control, so sound the pool unweighted instead.
            for (int i = 0; i < n; ++i)
                pitchWeight[(size_t) i] = 1.0f;
        }

        const auto positionOf = [n, posTarget] (int i)
        { return n <= 1 ? posTarget : (float) i / (float) (n - 1); };

        if (spread < 0.05f)
        {
            // Effectively one note: the surviving candidate nearest where Bias points, found
            // by explicit search. A fixed weighting window cannot do this job, because one
            // wide enough for a six-note pool is too narrow for a two-note one, and a window
            // that matches nothing silently widens back to the whole pool.
            int best = 0;
            float bestDist = -1.0f;
            for (int i = 0; i < n; ++i)
            {
                if (pitchWeight[(size_t) i] <= 0.0f)
                    continue;
                const float dist = std::abs (positionOf (i) - posTarget);
                if (bestDist < 0.0f || dist < bestDist)
                {
                    best = i;
                    bestDist = dist;
                }
            }
            w[(size_t) best] = 1.0f;
            total = 1.0f;
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                if (pitchWeight[(size_t) i] <= 0.0f)
                    continue;
                w[(size_t) i] = pitchWeight[(size_t) i] * positionWeight (positionOf (i), posTarget, spread);
                total += w[(size_t) i];
            }

            if (total <= 0.0f)
            {
                // Unreachable while spread >= 0.05 keeps sigma off zero, but a division by
                // total follows, so guard it rather than trust the arithmetic.
                for (int i = 0; i < n; ++i)
                {
                    w[(size_t) i] = pitchWeight[(size_t) i];
                    total += w[(size_t) i];
                }
            }
        }

        // Wander correlates consecutive choices. A white draw leaps aimlessly; integrating
        // it into a plain random walk drifts off and never comes back. Voss and Clarke
        // measured real melodic lines near 1/f, between the two, so this sums three
        // one-pole filters at 2, 8 and 32 steps to approximate that band.
        //
        // Not literal Voss-McCartney, deliberately. That algorithm redraws register k every
        // 2^k steps off a monotone counter, which would keep moving while the deja-vu loop
        // was locked and so would stop a locked loop repeating exactly. Feeding filters
        // from the step bundle instead keeps every source of variation inside the lock.
        const float white = sub01 (stepSeed, lanePitch);
        const float pink = wanderValue (white);
        const float u = juce::jlimit (0.0f, 0.999999f,
                                      juce::jmap (juce::jlimit (0, 100, p.wander) / 100.0f,
                                                  white, pink));

        const int chosen = roulette (w.data(), n, total, u);

        d.poolIndex[0] = chosen;
        d.voices = 1;
        if (p.xMode == XMode::duet && n > 1)
        {
            // Mirrored about the register Bias points at, so a duet opens and closes around
            // the centre rather than tracking it in parallel.
            const int mirror = juce::jlimit (0, n - 1,
                                             (int) std::lround (2.0f * posTarget * (float) (n - 1))
                                                 - chosen);
            if (mirror != chosen)
                d.poolIndex[(size_t) d.voices++] = mirror;
        }
        else if (p.xMode == XMode::cluster)
        {
            for (int k = 1; k < maxVoices && chosen + k < n; ++k)
                d.poolIndex[(size_t) d.voices++] = chosen + k;
        }

        //----------------------------------------------------------------------
        // The rest of the step, all from the same bundle.
        d.velocityScale = 0.55f + 0.45f * sub01 (stepSeed, laneVelocity);
        d.gateScale = 0.35f + 0.75f * sub01 (stepSeed, laneGate);

        if (p.tMode == TMode::bursts)
        {
            const float r = sub01 (stepSeed, laneRatchet) * density;
            d.ratchets = r > 0.66f ? 4 : (r > 0.4f ? 3 : (r > 0.2f ? 2 : 1));
        }

        // Timing feel. jitter^4 keeps the first half of the control nearly inaudible, which
        // is what makes the useful part of its travel usable; the fat-tailed bell (three
        // uniforms averaged, close enough to the Beta(3,3) Marbles draws from) puts most
        // steps near the grid and occasionally pushes one well off it.
        //
        // Marbles has to pull its phase back toward the unjittered position, because it
        // jitters a free-running oscillator and the error would otherwise accumulate.
        // Nothing accumulates here: the arp re-derives every fire time from ppq each block,
        // so the nudge is relative to the step's own grid position and self-corrects.
        const float j01 = juce::jlimit (0, 100, p.jitter) / 100.0f;
        if (j01 > 0.0f)
        {
            const float amount = j01 * j01 * j01 * j01;
            const float bell = (sub01 (stepSeed, laneJitter) + sub01 (stepSeed, laneJitter + 1)
                                + sub01 (stepSeed, laneJitter + 2)) / 3.0f;
            d.jitterFrac = (bell - 0.5f) * amount;
        }

        return d;
    }

private:
    // Independent streams off one step seed. Spaced by the 64-bit golden ratio so two
    // lanes never land on adjacent inputs to the mixer.
    static constexpr juce::uint32 laneDejaVu = 0;
    static constexpr juce::uint32 laneFire = 1;
    static constexpr juce::uint32 lanePitch = 2;
    static constexpr juce::uint32 laneVelocity = 3;
    static constexpr juce::uint32 laneGate = 4;
    static constexpr juce::uint32 laneRatchet = 5;
    static constexpr juce::uint32 laneJitter = 6;  // and +1, +2

    static float sub01 (juce::uint64 stepSeed, juce::uint32 lane) noexcept
    {
        const auto h = okstudio::poly::mix (stepSeed + (juce::uint64) lane * 0x9E3779B97F4A7C15ull);
        return (float) (h >> 40) * (1.0f / 16777216.0f);
    }

    /** Where in the pool the register wants to sit: a bell around Bias that widens with
        Spread and hands over to the two ends of the range at the top of its travel. Both of
        those degenerate ends are Marbles' and both are musically useful.

        Only called with spread >= 0.05. The collapse-to-one-note end is handled by an
        explicit nearest-candidate search at the call site, because it cannot be expressed as
        a weighting window without knowing how far apart the candidates are. */
    static float positionWeight (float pos, float target, float spread) noexcept
    {
        const float sigma = 0.05f + spread * 0.55f;
        const float dz = (pos - target) / sigma;
        const float bell = std::exp (-0.5f * dz * dz);

        if (spread <= 0.7f)
            return bell;

        // Past three quarters of travel, hand over to the ends of the range, so the top of
        // the control is a coin flip between the extremes weighted by Bias.
        const float edge = std::abs (pos - 0.5f) * 2.0f;                 // 0 centre, 1 ends
        const float lean = pos > 0.5f ? target : 1.0f - target;          // Bias picks a side
        const float edges = juce::jmax (0.02f, edge * edge * lean * 2.0f);
        return juce::jmap ((spread - 0.7f) / 0.3f, bell, edges);
    }

    /** Three one-poles at 2, 8 and 32 steps, summed. Fed the step bundle's own white draw
        so it stays inside the deja-vu lock (see the note at the call site). */
    float wanderValue (float white) noexcept
    {
        static constexpr float coeff[numPoles] = { 0.5f, 0.875f, 0.96875f }; // 1 - 1/tau
        float sum = 0.0f;
        for (int k = 0; k < numPoles; ++k)
        {
            poles[(size_t) k] = coeff[k] * poles[(size_t) k] + (1.0f - coeff[k]) * white;
            sum += poles[(size_t) k];
        }
        // The poles share a mean of 0.5 but each has a smaller variance than the white
        // input, so the sum is re-centred and re-spread rather than merely averaged.
        const float mean = sum / (float) numPoles;
        return juce::jlimit (0.0f, 1.0f, 0.5f + (mean - 0.5f) * 2.4f);
    }

    static int roulette (const float* w, int n, float total, float u) noexcept
    {
        float acc = 0.0f;
        const float target = u * total;
        for (int i = 0; i < n; ++i)
        {
            acc += w[i];
            if (target < acc)
                return i;
        }
        return n - 1;
    }

    static constexpr int numPoles = 3;

    okstudio::poly::DejaVuSequence deja {};
    std::array<float, numPoles> poles { { 0.5f, 0.5f, 0.5f } };
    juce::uint64 base = 1;
    int lastLoopLen = 8;
};
} // namespace keys
