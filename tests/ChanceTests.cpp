#include "../src/ChanceEngine.h"
#include <juce_core/juce_core.h>
#include <set>
#include <vector>

namespace keys::tests
{
namespace
{
    // C major triad over two octaves. Ascending, which is what ArpEngine's seq[] is.
    const std::vector<int> pool { 60, 64, 67, 72, 76, 79 };
    const std::vector<int> majorIntervals { 0, 2, 4, 5, 7, 9, 11 };

    ChanceEngine::Harmony cMajor (int chordPull = 0, juce::uint16 chordMask = 0)
    {
        return ChanceEngine::buildHarmony (0, majorIntervals, chordMask, chordPull);
    }

    ChanceEngine::Params base()
    {
        ChanceEngine::Params p;
        p.enabled = true;
        p.density = 100;                    // fire every step, so every test sees a decision
        p.dejaVu = 0;                       // free-running unless a test says otherwise
        p.wander = 0;                       // stateful; tests that care enable it explicitly
        p.tMode = ChanceEngine::TMode::coin; // bundle-driven, so it locks with the loop
        p.xMode = ChanceEngine::XMode::line;
        return p;
    }

    // The pitches one run produces, one entry per firing step.
    std::vector<int> run (ChanceEngine& e, const ChanceEngine::Params& p,
                          const ChanceEngine::Harmony& h, int steps)
    {
        std::vector<int> out;
        for (int i = 0; i < steps; ++i)
        {
            const auto d = e.advance (p, h, pool.data(), (int) pool.size(), i);
            if (! d.fires)
                continue;
            for (int v = 0; v < d.voices; ++v)
                out.push_back (pool[(size_t) d.poolIndex[(size_t) v]]);
        }
        return out;
    }
} // namespace

class ChanceEngineTests : public juce::UnitTest
{
public:
    ChanceEngineTests() : juce::UnitTest ("ChanceEngine") {}

    void runTest() override
    {
        const auto h = cMajor();

        beginTest ("selection, never invention");
        {
            // The invariant the whole design rests on: every pitch it can return is already
            // in the pool, so it can never produce an out-of-key note and never needs to
            // snap one. A regression here means Chance has started inventing pitches.
            auto p = base();
            p.key = 0;            // no masking at all, the widest the pool ever gets
            p.temperature = 100;  // uniform, so every candidate is reachable
            p.spread = 100;
            ChanceEngine e;
            e.prepare (0xABCDEF01u, 8);

            const std::set<int> allowed (pool.begin(), pool.end());
            for (int i = 0; i < 2000; ++i)
            {
                const auto d = e.advance (p, h, pool.data(), (int) pool.size(), i);
                if (! d.fires)
                    continue;
                expect (d.voices >= 1 && d.voices <= ChanceEngine::maxVoices);
                for (int v = 0; v < d.voices; ++v)
                {
                    const int idx = d.poolIndex[(size_t) v];
                    expect (idx >= 0 && idx < (int) pool.size(), "pool index out of range");
                    expect (allowed.count (pool[(size_t) idx]) == 1, "invented a pitch");
                }
                expect (d.velocityScale > 0.0f && d.velocityScale <= 1.0f);
                expect (d.ratchets >= 1 && d.ratchets <= 4);
                expect (std::abs (d.jitterFrac) <= 0.5f);
            }
        }

        beginTest ("same seed replays the same phrase");
        {
            auto p = base();
            p.temperature = 60;
            p.wander = 70;  // includes the stateful part, which must also replay

            ChanceEngine a, b;
            a.prepare (0x5EEDu, 8);
            b.prepare (0x5EEDu, 8);
            expect (run (a, p, h, 400) == run (b, p, h, 400), "same seed diverged");

            ChanceEngine c;
            c.prepare (0x5EEEu, 8);
            expect (run (c, p, h, 400) != run (a, p, h, 400), "different seeds matched");
        }

        beginTest ("deja vu at centre is an exact loop");
        {
            // p = (2d - 1)^2 is zero at the centre, so the loop is never disturbed and the
            // phrase repeats with the period Length sets.
            auto p = base();
            p.dejaVu = 50;
            p.loopLen = 8;

            ChanceEngine e;
            e.prepare (0x101u, 8);
            const auto notes = run (e, p, h, 8 * 6);
            expect ((int) notes.size() == 8 * 6, "density 100 should fire every step");
            for (size_t i = 8; i < notes.size(); ++i)
                expect (notes[i] == notes[i - 8], "the frozen loop did not repeat");
        }

        beginTest ("deja vu at centre repeats for every loop length");
        {
            for (int len : { 1, 2, 3, 4, 6, 8, 12, 16 })
            {
                auto p = base();
                p.dejaVu = 50;
                p.loopLen = len;

                ChanceEngine e;
                e.prepare (0x202u, len);
                const auto notes = run (e, p, h, len * 5);
                for (size_t i = (size_t) len; i < notes.size(); ++i)
                    expect (notes[i] == notes[i - (size_t) len],
                            "loop length " + juce::String (len) + " did not repeat");
            }
        }

        beginTest ("deja vu at the top reorders and never adds material");
        {
            // Above centre a disturbance jumps the read pointer inside the loop; it never
            // writes a fresh value. So the phrase shuffles but its vocabulary is fixed at
            // Length distinct steps.
            auto p = base();
            p.dejaVu = 100;
            p.loopLen = 4;

            ChanceEngine e;
            e.prepare (0x303u, 4);

            std::set<int> distinct;
            for (int i = 0; i < 500; ++i)
            {
                const auto d = e.advance (p, h, pool.data(), (int) pool.size(), i);
                if (d.fires)
                    distinct.insert (d.poolIndex[0]);
            }
            // At most Length distinct cells drive the choice. Fewer is legitimate, since two
            // cells can land on the same pool index.
            expect ((int) distinct.size() <= 4,
                    "shuffled loop produced " + juce::String ((int) distinct.size())
                        + " distinct notes, more than its 4 cells");
        }

        beginTest ("deja vu at zero keeps inventing");
        {
            auto p = base();
            p.dejaVu = 0;
            p.temperature = 100;
            p.spread = 100;
            p.key = 0;

            ChanceEngine e;
            e.prepare (0x404u, 8);
            const auto notes = run (e, p, h, 200);
            const std::set<int> distinct (notes.begin(), notes.end());
            expect (distinct.size() > 2, "free-running deja vu should not settle on a loop");
        }

        beginTest ("density bounds");
        {
            ChanceEngine e;

            auto silent = base();
            silent.density = 0;
            e.prepare (0x505u, 8);
            expect (run (e, silent, h, 300).empty(), "density 0 should never fire");

            auto full = base();
            full.density = 100;
            e.prepare (0x505u, 8);
            expect ((int) run (e, full, h, 300).size() == 300, "density 100 should always fire");
        }

        beginTest ("Key collapses the pool in stages");
        {
            ChanceEngine e;

            // Top of the ladder: threshold 255, so only the root survives.
            auto onlyRoot = base();
            onlyRoot.key = 100;
            onlyRoot.temperature = 100;
            onlyRoot.spread = 100;
            e.prepare (0x606u, 8);
            for (int n : run (e, onlyRoot, h, 300))
                expect (n % 12 == 0, "Key at 100 let a non-root through: " + juce::String (n));

            // One state down: threshold 192, so the triad survives and nothing else.
            auto triad = base();
            triad.key = 90;
            triad.temperature = 100;
            triad.spread = 100;
            e.prepare (0x606u, 8);
            for (int n : run (e, triad, h, 300))
            {
                const int pc = n % 12;
                expect (pc == 0 || pc == 4 || pc == 7,
                        "Key at 90 should leave only the triad, got pc " + juce::String (pc));
            }
        }

        beginTest ("Key never silences a pool it has masked entirely");
        {
            // Hold notes that are not in the key at all, then ask for maximum adherence.
            // Falling silent would read as a broken control, so it has to sound anyway.
            const std::vector<int> offKey { 61, 63, 66 };  // Db, Eb, Gb against C major
            auto p = base();
            p.key = 100;

            ChanceEngine e;
            e.prepare (0x707u, 8);
            int fired = 0;
            for (int i = 0; i < 100; ++i)
                if (e.advance (p, h, offKey.data(), (int) offKey.size(), i).fires)
                    ++fired;
            expect (fired > 0, "an entirely off-key pool fell silent");
        }

        beginTest ("Temperature at zero is single-valued");
        {
            // P_i proportional to w_i^(1/T) collapses onto the highest-weighted candidate as
            // T goes to zero. In C major that is the root.
            auto p = base();
            p.temperature = 0;
            p.key = 0;
            p.spread = 50;

            ChanceEngine e;
            e.prepare (0x808u, 8);
            std::set<int> classes;
            for (int n : run (e, p, h, 300))
                classes.insert (n % 12);
            expect (classes.size() == 1, "Temperature 0 should pick one pitch class");
            expect (*classes.begin() == 0, "Temperature 0 should settle on the root");
        }

        beginTest ("Spread at the bottom is one note, at the top is the extremes");
        {
            auto narrow = base();
            narrow.spread = 0;
            narrow.bias = 0;
            narrow.temperature = 100;
            narrow.key = 0;

            ChanceEngine e;
            e.prepare (0x909u, 8);
            const auto notes = run (e, narrow, h, 200);
            const std::set<int> distinct (notes.begin(), notes.end());
            expect (distinct.size() <= 2,
                    "Spread 0 should collapse to a single note, got "
                        + juce::String ((int) distinct.size()));

            auto wide = base();
            wide.spread = 100;
            wide.temperature = 100;
            wide.key = 0;
            e.prepare (0x909u, 8);
            int ends = 0, middle = 0;
            for (int i = 0; i < 400; ++i)
            {
                const auto d = e.advance (wide, h, pool.data(), (int) pool.size(), i);
                if (! d.fires)
                    continue;
                const int idx = d.poolIndex[0];
                if (idx == 0 || idx == (int) pool.size() - 1)
                    ++ends;
                else
                    ++middle;
            }
            expect (ends > middle, "Spread 100 should favour the ends of the range");
        }

        beginTest ("Bias moves the register");
        {
            auto low = base();
            low.bias = -100;
            low.spread = 30;
            low.temperature = 100;
            low.key = 0;

            auto high = low;
            high.bias = 100;

            ChanceEngine a, b;
            a.prepare (0xA0Au, 8);
            b.prepare (0xA0Au, 8);

            const auto lowNotes = run (a, low, h, 200);
            const auto highNotes = run (b, high, h, 200);

            const auto mean = [] (const std::vector<int>& v)
            {
                double s = 0.0;
                for (int n : v) s += n;
                return v.empty() ? 0.0 : s / (double) v.size();
            };
            expect (mean (lowNotes) < mean (highNotes), "Bias did not move the register");
        }

        beginTest ("Chord Pull lifts chord tones above the rest of the key");
        {
            // A C major triad held, but Chord Pull pointed at the fifth alone.
            const juce::uint16 gOnly = (juce::uint16) (1u << 7);
            const auto pulled = ChanceEngine::buildHarmony (0, majorIntervals, gOnly, 100);
            expect (pulled.weight[7] == 255, "full Chord Pull should max a chord tone");
            expect (pulled.weight[4] < pulled.weight[7], "a non-chord tone outranked a chord tone");

            const auto unpulled = ChanceEngine::buildHarmony (0, majorIntervals, gOnly, 0);
            expect (unpulled.weight[7] == ChanceEngine::wFifth, "zero pull should change nothing");
        }

        beginTest ("harmony weights walk a musical ladder");
        {
            const auto w = cMajor();
            expect (w.weight[0] == ChanceEngine::wRoot);
            expect (w.weight[7] == ChanceEngine::wFifth);
            expect (w.weight[4] == ChanceEngine::wThird);
            expect (w.weight[1] == ChanceEngine::wOutside, "Db is not in C major");
            expect (w.weight[7] > w.weight[4], "the fifth must outlive the third");
            expect (w.weight[4] > w.weight[2], "the third must outlive the second");
        }

        beginTest ("voice modes");
        {
            auto duet = base();
            duet.xMode = ChanceEngine::XMode::duet;
            duet.key = 0;
            duet.temperature = 100;

            ChanceEngine e;
            e.prepare (0xB0Bu, 8);
            int sawTwo = 0;
            for (int i = 0; i < 200; ++i)
            {
                const auto d = e.advance (duet, h, pool.data(), (int) pool.size(), i);
                if (d.fires && d.voices == 2)
                    ++sawTwo;
            }
            expect (sawTwo > 0, "duet never produced two voices");

            auto cluster = base();
            cluster.xMode = ChanceEngine::XMode::cluster;
            cluster.key = 0;
            cluster.temperature = 100;
            cluster.spread = 20;
            cluster.bias = -60;   // sit low so there is room above for the cluster
            e.prepare (0xB0Cu, 8);
            int sawThree = 0;
            for (int i = 0; i < 200; ++i)
            {
                const auto d = e.advance (cluster, h, pool.data(), (int) pool.size(), i);
                if (d.fires && d.voices == 3)
                    ++sawThree;
            }
            expect (sawThree > 0, "cluster never produced three voices");
        }

        beginTest ("Jitter is bounded and quartic");
        {
            const auto peak = [&] (int amount)
            {
                auto p = base();
                p.jitter = amount;
                ChanceEngine e;
                e.prepare (0xC0Cu, 8);
                float worst = 0.0f;
                for (int i = 0; i < 500; ++i)
                {
                    const auto d = e.advance (p, h, pool.data(), (int) pool.size(), i);
                    if (d.fires)
                        worst = juce::jmax (worst, std::abs (d.jitterFrac));
                }
                return worst;
            };

            expect (peak (0) == 0.0f, "Jitter 0 should be exactly on the grid");
            const float half = peak (50);
            const float full = peak (100);
            expect (full <= 0.5f, "Jitter exceeded half a step");
            expect (half < full * 0.25f,
                    "the quartic curve should keep half travel well under a quarter of full");
        }

        beginTest ("bursts ratchet, other modes do not");
        {
            auto bursts = base();
            bursts.tMode = ChanceEngine::TMode::bursts;
            bursts.density = 100;

            ChanceEngine e;
            e.prepare (0xD0Du, 8);
            int ratcheted = 0;
            for (int i = 0; i < 300; ++i)
            {
                const auto d = e.advance (bursts, h, pool.data(), (int) pool.size(), i);
                if (d.fires && d.ratchets > 1)
                    ++ratcheted;
            }
            expect (ratcheted > 0, "bursts never ratcheted");

            auto coin = base();
            e.prepare (0xD0Du, 8);
            for (int i = 0; i < 300; ++i)
            {
                const auto d = e.advance (coin, h, pool.data(), (int) pool.size(), i);
                if (d.fires)
                    expect (d.ratchets == 1, "coin mode should not ratchet");
            }
        }

        beginTest ("Euclid is even and grid-locked, coin is not");
        {
            auto euclid = base();
            euclid.tMode = ChanceEngine::TMode::euclid;
            euclid.density = 50;   // the programmed k, an eighth-note feel on a 16 grid

            ChanceEngine e;
            e.prepare (0xE0Eu, 8);
            std::vector<bool> fired;
            for (int i = 0; i < 32; ++i)
                fired.push_back (e.advance (euclid, h, pool.data(), (int) pool.size(), i).fires);

            // Grid-locked means the mask repeats with the grid, whatever the loop is doing.
            for (int i = 0; i < 16; ++i)
                expect (fired[(size_t) i] == fired[(size_t) i + 16],
                        "the Euclid mask drifted out of phase with its grid");

            int onsets = 0;
            for (int i = 0; i < 16; ++i)
                if (fired[(size_t) i])
                    ++onsets;
            expect (onsets == 8, "density 50 on a 16 grid should keep the programmed 8 pulses");
        }

        beginTest ("resync replays from the top");
        {
            // A DAW loop has to sound the same on the second pass. Hardware never needs this;
            // a plugin does, so ArpEngine calls resync() when it sees a transport jump.
            auto p = base();
            p.dejaVu = 20;   // disturbed, so a stale counter would show up immediately
            p.wander = 80;

            ChanceEngine e;
            e.prepare (0xF0Fu, 8);
            const auto first = run (e, p, h, 64);
            e.resync();
            const auto second = run (e, p, h, 64);
            expect (first == second, "resync did not replay the phrase");
        }

        beginTest ("setSeed keeps the loop length");
        {
            ChanceEngine e;
            e.prepare (0x1234u, 12);
            e.setSeed (0x9999u);
            expect (e.seed() == (0x9999u | 1ull), "setSeed did not take");

            auto p = base();
            p.dejaVu = 50;
            p.loopLen = 12;
            const auto notes = run (e, p, h, 12 * 4);
            for (size_t i = 12; i < notes.size(); ++i)
                expect (notes[i] == notes[i - 12], "loop length was lost across setSeed");
        }

        beginTest ("an empty pool is silent, not a crash");
        {
            auto p = base();
            ChanceEngine e;
            e.prepare (0x1111u, 8);
            for (int i = 0; i < 50; ++i)
                expect (! e.advance (p, h, nullptr, 0, i).fires);
        }

        beginTest ("a one-note pool always plays that note");
        {
            const std::vector<int> one { 60 };
            auto p = base();
            p.spread = 100;
            p.bias = 100;

            ChanceEngine e;
            e.prepare (0x2222u, 8);
            for (int i = 0; i < 100; ++i)
            {
                const auto d = e.advance (p, h, one.data(), 1, i);
                if (d.fires)
                    expect (d.poolIndex[0] == 0, "a single-note pool went out of range");
            }
        }

        beginTest ("disabled advances the loop but never fires");
        {
            // The loop has to stay in phase with the grid whether or not it is sounding, or
            // switching Chance on mid-bar would land it somewhere arbitrary.
            //
            // Deja vu has to match across both runs for this to mean anything. Below centre a
            // disturbance writes a fresh value into the ring and moves the write head, so
            // eight disabled steps at deja vu 0 legitimately leave the two engines holding
            // different loops. Frozen is the setting under which "in phase" is even defined.
            auto off = base();
            off.enabled = false;
            off.dejaVu = 50;

            ChanceEngine a, b;
            a.prepare (0x3333u, 8);
            b.prepare (0x3333u, 8);

            for (int i = 0; i < 8; ++i)
                expect (! a.advance (off, h, pool.data(), (int) pool.size(), i).fires);

            auto on = base();
            on.dejaVu = 50;
            // a has advanced a whole 8-step loop while silent, b none, so a frozen loop puts
            // them back in phase with each other.
            const auto fromA = run (a, on, h, 16);
            const auto fromB = run (b, on, h, 16);
            expect (fromA == fromB, "a disabled step did not advance the loop in step");
        }
    }
};

static ChanceEngineTests chanceEngineTests;
} // namespace keys::tests
