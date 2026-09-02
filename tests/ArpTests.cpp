#include "../src/ArpEngine.h"
#include "../src/ArpRateText.h"
#include "../src/EuclidGen.h"
#include <juce_core/juce_core.h>
#include <map>
#include <set>

namespace keys::tests
{
namespace
{
    struct Event
    {
        bool on;
        int note;
        int sample;
    };

    std::vector<Event> collect(const juce::MidiBuffer& out)
    {
        std::vector<Event> v;
        for (const auto meta : out)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
                v.push_back({ true, m.getNoteNumber(), meta.samplePosition });
            else if (m.isNoteOff())
                v.push_back({ false, m.getNoteNumber(), meta.samplePosition });
        }
        return v;
    }

    juce::MidiBuffer chordOn(std::initializer_list<int> notes)
    {
        juce::MidiBuffer b;
        for (int n : notes)
            b.addEvent(juce::MidiMessage::noteOn(1, n, 0.8f), 0);
        return b;
    }
} // namespace

class ArpEngineTests : public juce::UnitTest
{
public:
    ArpEngineTests() : juce::UnitTest("ArpEngine") {}

    void runTest() override
    {
        // 120 bpm, 48 kHz: one 1/16 step = 0.25 beats = 6000 samples.
        constexpr double sr = 48000.0;
        constexpr int block = 6000;

        ArpEngine::Params p;
        p.enabled = true;
        p.rateIndex = 8; // 1/16
        p.direction = ArpEngine::Direction::up;
        ArpEngine::HostClock clock;
        clock.playing = true;
        clock.hasPpq = true;
        clock.bpm = 120.0;

        beginTest("up direction fires one note per step, ascending, sample-aligned");
        {
            ArpEngine e;
            e.prepare(sr);
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(p, clock, block, chordOn({ 64, 60, 67 }), out); // unsorted input
            auto ev = collect(out);
            expect(! ev.empty() && ev[0].on && ev[0].note == 60 && ev[0].sample == 0,
                   "first step is the lowest note at offset 0");
            juce::MidiBuffer out2;
            clock.ppq = 0.25;
            e.process(p, clock, block, {}, out2);
            auto ev2 = collect(out2);
            bool sawOn = false;
            for (auto& x : ev2)
                if (x.on) { expectEquals(x.note, 64); sawOn = true; }
            expect(sawOn, "second step fires the middle note");
        }

        // Step data is only read when the shape is "Pattern"; with usePattern off every
        // lane reads as its default. The lane tests below therefore need it on.
        auto lp = p;
        lp.usePattern = true;

        // **Legato** (2026-09-01, Owen: "a legato button. So when the density is lower or a
        // note is skipped, it continues nicely"). Six cases, and the second is the one that
        // pins the design: a gated note still ends at its gate when the next step *fires*,
        // which is only possible because fireStep looks one step ahead. Without the lookahead
        // the engine could hold through a skip or honour the gate, never both - and "every note
        // held to the next note-on" is the reading Owen turned down.
        beginTest("legato holds a note through a skipped step and lets it go after the next note-on");
        {
            // The Note lane names the entries outright, so the pitches do not depend on how a
            // skipped step moves the walk: step 0 plays 60, step 2 plays 64, and the Chance
            // lane silences step 1 for certain.
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneNote][0].store(1);
            e.lanes.value[ArpEngine::laneNote][2].store(2);
            e.lanes.value[ArpEngine::laneProbability][1].store(0);
            auto gp = lp;
            gp.legato = true;
            gp.gate = 50; // without Legato, 60 would end 3000 samples in
            juce::MidiBuffer b0, b1, b2;
            clock.ppq = 0.0;
            e.process(gp, clock, block, chordOn({ 60, 64 }), b0);
            clock.ppq = 0.25;
            e.process(gp, clock, block, {}, b1);
            clock.ppq = 0.5;
            e.process(gp, clock, block, {}, b2);
            const auto ev0 = collect(b0);
            const auto ev1 = collect(b1);
            const auto ev2 = collect(b2);
            expectEquals((int) ev0.size(), 1, "one note-on and nothing else where the held note starts");
            expect(! ev0.empty() && ev0[0].on && ev0[0].note == 60, "step 0 plays 60");
            expect(ev1.empty(), "the skipped step is silent and releases nothing");
            expectEquals((int) ev2.size(), 3, "the next fired step: its note-on, the release, its own gate end");
            if (ev2.size() == 3)
            {
                expect(ev2[0].on && ev2[0].note == 64 && ev2[0].sample == 0, "the new note-on comes first");
                expect(! ev2[1].on && ev2[1].note == 60 && ev2[1].sample == 1,
                       "the held note is released one sample *after* it, so the two overlap");
                expect(! ev2[2].on && ev2[2].note == 64, "the new note ends at its own gate");
                expectWithinAbsoluteError(ev2[2].sample, 3000, 2);
            }
        }

        beginTest("legato leaves the gate alone when the next step fires");
        {
            ArpEngine e;
            e.prepare(sr);
            auto gp = lp;
            gp.legato = true;
            gp.gate = 50;
            juce::MidiBuffer b0;
            clock.ppq = 0.0;
            e.process(gp, clock, block, chordOn({ 60, 64 }), b0);
            int offs = 0;
            for (auto& x : collect(b0))
                if (! x.on && x.note == 60) { ++offs; expectWithinAbsoluteError(x.sample, 3000, 2); }
            expectEquals(offs, 1, "a note whose successor fires ends at its gate, Legato or not");
        }

        beginTest("legato closes a held pitch before the same pitch retriggers");
        {
            // One note, so step 2 lands on the pitch step 0 is still holding. That is the tie
            // branch's case and it must win: off first, then on, or the voice stacks.
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneProbability][1].store(0);
            auto gp = lp;
            gp.legato = true;
            gp.gate = 50;
            juce::MidiBuffer b0, b1, b2;
            clock.ppq = 0.0;
            e.process(gp, clock, block, chordOn({ 60 }), b0);
            clock.ppq = 0.25;
            e.process(gp, clock, block, {}, b1);
            clock.ppq = 0.5;
            e.process(gp, clock, block, {}, b2);
            const auto ev2 = collect(b2);
            expect(ev2.size() >= 2 && ! ev2[0].on && ev2[0].note == 60 && ev2[0].sample == 0,
                   "the held 60 is released first");
            expect(ev2.size() >= 2 && ev2[1].on && ev2[1].note == 60 && ev2[1].sample == 0,
                   "then 60 retriggers");
            int ons = 0;
            for (auto& x : ev2)
                if (x.on) ++ons;
            expectEquals(ons, 1, "one note-on for one pitch");
        }

        beginTest("releasing the chord releases what legato is holding");
        {
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneProbability][1].store(0);
            auto gp = lp;
            gp.legato = true;
            gp.gate = 50;
            juce::MidiBuffer b0, b1;
            clock.ppq = 0.0;
            e.process(gp, clock, block, chordOn({ 60, 64 }), b0);
            juce::MidiBuffer up;
            up.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            up.addEvent(juce::MidiMessage::noteOff(1, 64), 0);
            clock.ppq = 0.25;
            e.process(gp, clock, block, up, b1);
            int offs = 0;
            for (auto& x : collect(b1))
                if (! x.on && x.note == 60)
                {
                    ++offs;
                    expectEquals(x.sample, 0, "at the top of the block the keys came up in");
                }
            expectEquals(offs, 1, "a held note does not outlive the chord");
        }

        beginTest("switching legato off releases what it was holding");
        {
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneProbability][1].store(0);
            auto gp = lp;
            gp.legato = true;
            gp.gate = 50;
            juce::MidiBuffer b0, b1;
            clock.ppq = 0.0;
            e.process(gp, clock, block, chordOn({ 60, 64 }), b0);
            gp.legato = false;
            clock.ppq = 0.25;
            e.process(gp, clock, block, {}, b1);
            int offs = 0;
            for (auto& x : collect(b1))
                if (! x.on && x.note == 60)
                {
                    ++offs;
                    expectEquals(x.sample, 0, "at the top of the block the flag went down in");
                }
            expectEquals(offs, 1, "a note held by a switch that is now off is let go");
        }

        beginTest("legato holds only the last sub-hit of a ratchet");
        {
            // Two sub-hits at gate 50: the first ends at its own gate so the ratchet still reads
            // as one; only the second, which would otherwise leave the gap, is held open.
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneRatchet][0].store(2);
            e.lanes.value[ArpEngine::laneProbability][1].store(0);
            auto gp = lp;
            gp.legato = true;
            gp.gate = 50;
            juce::MidiBuffer b0;
            clock.ppq = 0.0;
            e.process(gp, clock, block, chordOn({ 60 }), b0);
            int ons = 0, offs = 0;
            for (auto& x : collect(b0))
            {
                if (x.on) ++ons;
                else { ++offs; expectWithinAbsoluteError(x.sample, 1500, 2); }
            }
            expectEquals(ons, 2, "both sub-hits sound");
            expectEquals(offs, 1, "only the first ends inside the step");
        }

        // **The line bus** (2026-09-01, docs/LINE_INTERACTION.md). Two engines in one "block":
        // A runs, then B with B's `follow` pointing at A's record, which is exactly the order
        // runArpLines runs them in. The first two-engine tests in this file.
        beginTest("a line's record says what its events say");
        {
            ArpEngine a;
            a.prepare(sr);
            a.lanes.value[ArpEngine::laneProbability][1].store(0); // step 1 never fires
            a.lanes.value[ArpEngine::laneNote][0].store(1); // pin the pitches: 60, then 64
            a.lanes.value[ArpEngine::laneNote][2].store(2);
            juce::MidiBuffer b0, b1, b2;
            clock.ppq = 0.0;
            a.process(lp, clock, block, chordOn({ 60, 64, 67 }), b0);
            expectEquals(a.record.firedCount, 1, "one step fired in block 0");
            expectEquals(a.record.firedAt[0], 0, "at offset 0");
            expectEquals(a.record.lastNote, 60, "and the record says which note");
            expect(a.record.lastVelocity >= 1 && a.record.lastVelocity <= 127);
            expect(a.record.sounding, "enabled and holding a chord");
            expectEquals((long long) a.record.firedBefore, 0LL, "nothing before block 0");
            clock.ppq = 0.25;
            a.process(lp, clock, block, {}, b1);
            expectEquals(a.record.firedCount, 0, "the skipped step is not a fire");
            expectEquals((long long) a.record.firedBefore, 1LL, "block 0's fire rolled into the total");
            expect(! a.record.lastStepFired, "the Chain bit says the last step did not fire");
            clock.ppq = 0.5;
            a.process(lp, clock, block, {}, b2);
            expectEquals(a.record.firedCount, 1);
            expectEquals((long long) a.record.firedBefore, 1LL, "the total counts fires, not steps");
            expect(a.record.lastStepFired);
            expectEquals(a.record.lastNote, 64, "the walk moved on");
            expectWithinAbsoluteError(a.record.stepSamples, 6000.0, 1.0, "a 1/16 at 120 bpm, 48 kHz");
        }

        beginTest("duck at 100 is the hocket: B plays exactly where A does not");
        {
            ArpEngine a, b;
            a.prepare(sr);
            b.prepare(sr);
            a.lanes.value[ArpEngine::laneProbability][1].store(0); // A fires on even steps only
            a.lanes.value[ArpEngine::laneProbability][3].store(0);
            a.lanes.value[ArpEngine::laneProbability][5].store(0);
            a.lanes.value[ArpEngine::laneProbability][7].store(0);
            auto bp = lp;
            bp.follow = &a.record;
            bp.duck = 100;
            bp.mutateSeed = 1;
            for (int step = 0; step < 8; ++step)
            {
                juce::MidiBuffer outA, outB;
                clock.ppq = 0.25 * step;
                a.process(lp, clock, block, step == 0 ? chordOn({ 60 }) : juce::MidiBuffer(), outA);
                b.process(bp, clock, block, step == 0 ? chordOn({ 72 }) : juce::MidiBuffer(), outB);
                int onsA = 0, onsB = 0;
                for (auto& x : collect(outA)) if (x.on) ++onsA;
                for (auto& x : collect(outB)) if (x.on) ++onsB;
                expectEquals(onsA, step % 2 == 0 ? 1 : 0, "A's own pattern at step " + juce::String(step));
                if (step == 0)
                    expectEquals(onsB, 1, "the first step after Follow comes up never ducks: nothing to compare with");
                else
                    expectEquals(onsB, step % 2 == 0 ? 0 : 1, "B fills A's gaps at step " + juce::String(step));
            }
        }

        beginTest("duck's window spans the blocks the follower had no step in");
        {
            // B fires half a step late (Late lane at 50), so every B step lands in the block
            // *after* A's. The hocket has to hold anyway, which only firedBefore can do: by the
            // time B's step runs, A's fire has rolled out of firedAt and into the total.
            ArpEngine a, b;
            a.prepare(sr);
            b.prepare(sr);
            for (int s : { 1, 3, 5, 7 })
                a.lanes.value[ArpEngine::laneProbability][s].store(0);
            for (int s = 0; s < 8; ++s)
                b.lanes.value[ArpEngine::laneLate][s].store(50);
            auto bp = lp;
            bp.follow = &a.record;
            bp.duck = 100;
            bp.mutateSeed = 1;
            constexpr int half = 3000;
            int onsB[16] = {};
            for (int blk = 0; blk < 16; ++blk)
            {
                juce::MidiBuffer outA, outB;
                clock.ppq = 0.125 * blk;
                a.process(lp, clock, half, blk == 0 ? chordOn({ 60 }) : juce::MidiBuffer(), outA);
                b.process(bp, clock, half, blk == 0 ? chordOn({ 72 }) : juce::MidiBuffer(), outB);
                for (auto& x : collect(outB)) if (x.on) ++onsB[blk];
            }
            // B's step n lands in block 2n + 1. Step 0 is the warm-up; A fired at step 2 (block
            // 4), so B's step 2 (block 5) ducks; A was silent at steps 1 and 3, so B's steps 1
            // and 3 (blocks 3 and 7) play.
            expectEquals(onsB[1], 1, "B's step 0 plays (warm-up)");
            expectEquals(onsB[3], 1, "B's step 1 plays: A was silent at step 1");
            expectEquals(onsB[5], 0, "B's step 2 ducks: A fired at step 2, a block earlier");
            expectEquals(onsB[7], 1, "B's step 3 plays");
            expectEquals(onsB[9], 0, "B's step 4 ducks");
            expectEquals(onsB[11], 1, "B's step 5 plays");
        }

        beginTest("lock holds the hocket duck found");
        {
            // A's pattern repeats every eight steps and B's duck at 50 rolls on Mutate's cell;
            // with LOCK at 100 the era never moves, so the second pass ducks exactly the steps
            // the first did.
            ArpEngine a, b;
            a.prepare(sr);
            b.prepare(sr);
            for (int s : { 2, 5 })
                a.lanes.value[ArpEngine::laneProbability][s].store(0);
            auto bp = lp;
            bp.follow = &a.record;
            bp.duck = 50;
            bp.mutateLock = 100;
            bp.mutateSeed = 2;
            int ons[16] = {};
            for (int step = 0; step < 16; ++step)
            {
                juce::MidiBuffer outA, outB;
                clock.ppq = 0.25 * step;
                a.process(lp, clock, block, step == 0 ? chordOn({ 60 }) : juce::MidiBuffer(), outA);
                b.process(bp, clock, block, step == 0 ? chordOn({ 72 }) : juce::MidiBuffer(), outB);
                for (auto& x : collect(outB)) if (x.on) ++ons[step];
            }
            for (int step = 1; step < 8; ++step)
                expectEquals(ons[step + 8], ons[step],
                             "step " + juce::String(step) + " ducks the same way on the second pass");
        }

        beginTest("a source that is off, or duck at zero, leaves the follower playing as today");
        {
            ArpEngine a, b;
            a.prepare(sr);
            b.prepare(sr);
            auto ap = lp;
            ap.enabled = false; // A holds its chord silently: no fires on the bus
            auto bp = lp;
            bp.follow = &a.record;
            bp.duck = 100;
            int onsB = 0;
            for (int step = 0; step < 8; ++step)
            {
                juce::MidiBuffer outA, outB;
                clock.ppq = 0.25 * step;
                a.process(ap, clock, block, step == 0 ? chordOn({ 60 }) : juce::MidiBuffer(), outA);
                b.process(bp, clock, block, step == 0 ? chordOn({ 72 }) : juce::MidiBuffer(), outB);
                for (auto& x : collect(outB)) if (x.on) ++onsB;
            }
            expectEquals(onsB, 8, "nothing to duck to: every step plays");

            ArpEngine c, d;
            c.prepare(sr);
            d.prepare(sr);
            auto dp = lp;
            dp.follow = &c.record;
            dp.duck = 0;
            int onsD = 0;
            for (int step = 0; step < 8; ++step)
            {
                juce::MidiBuffer outC, outD;
                clock.ppq = 0.25 * step;
                c.process(lp, clock, block, step == 0 ? chordOn({ 60 }) : juce::MidiBuffer(), outC);
                d.process(dp, clock, block, step == 0 ? chordOn({ 72 }) : juce::MidiBuffer(), outD);
                for (auto& x : collect(outD)) if (x.on) ++onsD;
            }
            expectEquals(onsD, 8, "duck at zero: following changes nothing by itself");
        }

        // **Phase two of the line bus** (2026-09-01): RESET and NEIGHBOUR.
        beginTest("reset from the line it follows bounds a polymeter");
        {
            // A walks sixteen steps, B seven. B's Note lane plays 60 on its own step 1 and 64
            // everywhere else, so where 60 lands is where B's walk begins. Free-running, that is
            // every seven steps; reset from A, B also starts over every time A does.
            const auto runB = [&](bool reset, bool sourceOn) -> std::vector<int>
            {
                ArpEngine a, b;
                a.prepare(sr);
                b.prepare(sr);
                a.lanes.length[ArpEngine::laneNote].store(16);
                b.lanes.length[ArpEngine::laneNote].store(7);
                b.lanes.value[ArpEngine::laneNote][0].store(1);
                for (int s = 1; s < 7; ++s)
                    b.lanes.value[ArpEngine::laneNote][s].store(2);
                auto ap = lp;
                ap.enabled = sourceOn;
                auto bp = lp;
                bp.follow = &a.record;
                bp.resetFollow = reset;
                std::vector<int> sixtyAt;
                for (int step = 0; step < 40; ++step)
                {
                    juce::MidiBuffer outA, outB;
                    clock.ppq = 0.25 * step;
                    a.process(ap, clock, block, step == 0 ? chordOn({ 60, 64 }) : juce::MidiBuffer(), outA);
                    b.process(bp, clock, block, step == 0 ? chordOn({ 60, 64 }) : juce::MidiBuffer(), outB);
                    for (auto& x : collect(outB))
                        if (x.on && x.note == 60)
                            sixtyAt.push_back(step);
                }
                return sixtyAt;
            };
            const std::vector<int> freeRun { 0, 7, 14, 21, 28, 35 };
            const std::vector<int> bounded { 0, 7, 14, 16, 23, 30, 32, 39 };
            expect(runB(false, true) == freeRun, "without Reset, B's walk comes round every seven steps");
            expect(runB(true, true) == bounded, "with Reset, B also starts over wherever A does");
            expect(runB(true, false) == freeRun, "a silent source never resets anybody");
        }

        beginTest("neighbour: chain 3 plays with the line it follows, 4 against it");
        {
            // A fires on even steps only. B's Chain lane at 3 everywhere fires exactly where A
            // did - for two lines on one rate the source's last step is the same step, since A
            // ran first - and at 4 exactly where A did not.
            const auto runB = [&](int chain, bool follow) -> std::vector<int>
            {
                ArpEngine a, b;
                a.prepare(sr);
                b.prepare(sr);
                for (int s : { 1, 3, 5, 7 })
                    a.lanes.value[ArpEngine::laneProbability][s].store(0);
                for (int s = 0; s < 8; ++s)
                    b.lanes.value[ArpEngine::laneChain][s].store(chain);
                auto bp = lp;
                bp.follow = follow ? &a.record : nullptr;
                std::vector<int> firedAt;
                for (int step = 0; step < 8; ++step)
                {
                    juce::MidiBuffer outA, outB;
                    clock.ppq = 0.25 * step;
                    a.process(lp, clock, block, step == 0 ? chordOn({ 60 }) : juce::MidiBuffer(), outA);
                    b.process(bp, clock, block, step == 0 ? chordOn({ 72 }) : juce::MidiBuffer(), outB);
                    for (auto& x : collect(outB))
                        if (x.on)
                            firedAt.push_back(step);
                }
                return firedAt;
            };
            expect(runB(3, true) == std::vector<int> { 0, 2, 4, 6 }, "3: only where A sounded");
            expect(runB(4, true) == std::vector<int> { 1, 3, 5, 7 }, "4: only where A did not");
            expect(runB(3, false) == std::vector<int> { 0, 1, 2, 3, 4, 5, 6, 7 },
                   "with nobody to follow, 3 reads as always");
            expect(runB(4, false) == std::vector<int> { 0, 1, 2, 3, 4, 5, 6, 7 },
                   "and so does 4");
        }

        beginTest("lanes are ignored unless usePattern is on");
        {
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneOctave][0].store(2);
            e.lanes.value[ArpEngine::laneRatchet][0].store(4);
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(p, clock, block, chordOn({ 60 }), out); // p.usePattern is false
            int ons = 0;
            for (auto& x : collect(out))
                if (x.on)
                {
                    ++ons;
                    expectEquals(x.note, 60, "octave lane must not transpose a plain shape");
                }
            expectEquals(ons, 1, "ratchet lane must not multiply the hit on a plain shape");
        }

        beginTest("gate 50 percent schedules the note-off half a step later");
        {
            // A half-length note at 1/16 ends 3000 samples in, which is inside the very
            // block it started in - so the note-off must be in *that* buffer, not the next.
            // It used to land a whole block late; this asserted only that an off eventually
            // arrived, which is what let that hide.
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneGate][0].store(50);
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(lp, clock, block, chordOn({ 60 }), out);
            int offs = 0;
            for (auto& x : collect(out))
                if (! x.on && x.note == 60) { ++offs; expectWithinAbsoluteError(x.sample, 3000, 2); }
            expectEquals(offs, 1, "exactly one note-off, in the block the note started in");
        }

        beginTest("note-off is not a block late: gate 100 percent ends exactly one step on");
        {
            // The regression this guards: active[].samplesLeft is measured from the start
            // of the NEXT block, because retireActive() runs before any step fires. Get the
            // frame wrong and every note-off in the engine slides one buffer late.
            ArpEngine e;
            e.prepare(sr);
            juce::MidiBuffer out1, out2;
            clock.ppq = 0.0;
            e.process(p, clock, block, chordOn({ 60, 64, 67, 72 }), out1);
            clock.ppq = 0.25;
            e.process(p, clock, block, {}, out2);
            for (auto& x : collect(out1))
                expect(! (! x.on && x.note == 60), "the first note does not end inside its own step");
            int offs = 0;
            for (auto& x : collect(out2))
                if (! x.on && x.note == 60) { ++offs; expectWithinAbsoluteError(x.sample, 0, 2); }
            expectEquals(offs, 1, "it ends at the top of the very next block, not the one after");
        }

        beginTest("short notes retire correctly across uneven block sizes");
        {
            // Same note length, three different buffer sizes: a host is allowed to change
            // numSamples between calls, and the re-basing has to survive it.
            ArpEngine e;
            e.prepare(sr);
            auto gp = p;
            gp.gate = 25; // 1500 samples at 1/16
            int absoluteOff = -1, consumed = 0;
            const int sizes[] = { 512, 480, 1024, 6000 };
            auto c = clock;
            for (int b = 0; b < 4; ++b)
            {
                juce::MidiBuffer out;
                c.ppq = (double) consumed / 24000.0; // 24000 samples per beat at 120 bpm
                e.process(gp, c, sizes[b], b == 0 ? chordOn({ 60, 64, 67, 72 }) : juce::MidiBuffer {}, out);
                for (auto& x : collect(out))
                    if (! x.on && x.note == 60 && absoluteOff < 0)
                        absoluteOff = consumed + x.sample;
                consumed += sizes[b];
            }
            expectWithinAbsoluteError(absoluteOff, 1500, 2);
        }

        beginTest("ratchet 2 fires two on/off pairs inside one step");
        {
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneRatchet][0].store(2);
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(lp, clock, block, chordOn({ 60 }), out);
            int ons = 0;
            for (auto& x : collect(out))
                if (x.on && x.note == 60)
                    ++ons;
            expectEquals(ons, 2);
        }

        // A ratchet subdivides one step, and a step is normally longer than a buffer: at 1/16
        // and 120 bpm a step is 6000 samples against a 512-sample block, so three of a
        // ratchet-4's four sub-hits belong to a later block than the one that decided the
        // step. They used to be stamped at their raw offset regardless - a note-on at sample
        // 4500 of a 512-sample buffer - so ratchets did nothing at any buffer a host actually
        // uses. The test above only passed because its block is exactly one step long.
        const auto ratchetRun = [&](int ratchets, int blockSize, int blocks)
        {
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneRatchet][0].store(ratchets);
            std::vector<int> onsets;
            int outOfRange = 0;
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer out;
                clock.ppq = (double) (b * blockSize) / 24000.0; // 120 bpm at 48 kHz
                e.process(lp, clock, blockSize, b == 0 ? chordOn({ 60 }) : juce::MidiBuffer {}, out);
                for (auto& x : collect(out))
                {
                    if (x.sample < 0 || x.sample >= blockSize)
                        ++outOfRange;
                    if (x.on)
                        onsets.push_back(b * blockSize + x.sample);
                }
            }
            return std::make_pair(onsets, outOfRange);
        };

        beginTest("ratchet sub-hits survive a buffer shorter than a step");
        {
            // 16 x 512 = 8192 samples: step 0 ratchets into four, step 1 is a plain hit.
            const auto [onsets, outOfRange] = ratchetRun(4, 512, 16);
            expectEquals(outOfRange, 0, "no event may be stamped past the end of its buffer");
            expectEquals((int) onsets.size(), 5, "four sub-hits then the next step - none lost "
                                                 "to the buffer its own offset fell past");
            if (onsets.size() == 5)
            {
                expectWithinAbsoluteError(onsets[0], 0, 2);
                expectWithinAbsoluteError(onsets[1], 1500, 2);
                expectWithinAbsoluteError(onsets[2], 3000, 2);
                expectWithinAbsoluteError(onsets[3], 4500, 2);
                expectWithinAbsoluteError(onsets[4], 6000, 2); // step 1, ratchet 1
            }
        }

        beginTest("a ratchet lands identically however the buffer is cut");
        {
            // Same timeline three ways. A carried sub-hit has to fire where it would have
            // fired had the block been big enough to hold it.
            const auto small = ratchetRun(4, 512, 16).first;
            const auto odd = ratchetRun(4, 480, 18).first;
            const auto whole = ratchetRun(4, 8192, 1).first;
            expectEquals((int) odd.size(), (int) small.size(), "block size must not change the count");
            expectEquals((int) whole.size(), (int) small.size());
            for (size_t i = 0; i < small.size() && i < odd.size(); ++i)
                expectWithinAbsoluteError(odd[i], small[i], 2);
            for (size_t i = 0; i < small.size() && i < whole.size(); ++i)
                expectWithinAbsoluteError(whole[i], small[i], 2);
        }

        beginTest("probability 0 silences a step; muted note step silences too");
        {
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneProbability][0].store(0);
            e.lanes.value[ArpEngine::laneNote][1].store(-1);
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(lp, clock, 2 * block, chordOn({ 60 }), out); // two steps in one block
            for (auto& x : collect(out))
                expect(! x.on, "no note-ons from a 0-probability and a muted step");
        }

        // When the first note-off for `note` lands, in samples from the very first block.
        // Measured across blocks on purpose: the engine retires owed offs at the *start* of
        // a block, so an off is always reported one block after the arithmetic says, and an
        // absolute figure would bake that constant in. Every assertion below compares two
        // of these, which cancels it.
        const auto firstOffAt = [&](const ArpEngine::Params& params, int laneGateValue, int note)
        {
            ArpEngine e;
            e.prepare(sr);
            if (laneGateValue > 0)
                e.lanes.value[ArpEngine::laneGate][0].store(laneGateValue);
            auto c = clock;
            // Four held notes, so the note under test fires once and is not closed early by
            // its own retrigger on the next step.
            juce::MidiBuffer in = chordOn({ 60, 64, 67, 72 });
            for (int b = 0; b < 6; ++b)
            {
                juce::MidiBuffer out;
                c.ppq = 0.25 * b;
                e.process(params, c, block, b == 0 ? in : juce::MidiBuffer {}, out);
                for (auto& x : collect(out))
                    if (! x.on && x.note == note)
                        return b * block + x.sample;
            }
            return -1;
        };

        beginTest("global gate shortens the note on a plain shape, with no lanes in play");
        {
            // The point of the global: usePattern is false, so the Gate lane reads as its
            // 100 default and only Params::gate can shorten anything.
            auto full = p;
            auto half = p;
            half.gate = 50;
            const int tFull = firstOffAt(full, 0, 60);
            const int tHalf = firstOffAt(half, 0, 60);
            expect(tFull > 0 && tHalf > 0, "both note-offs arrive");
            expectWithinAbsoluteError(tFull - tHalf, 3000, 2); // half a 1/16 step at 120 bpm
        }

        beginTest("global gate multiplies the gate lane rather than replacing it");
        {
            auto full = lp;
            auto half = lp;
            half.gate = 50;
            const int tLaneOnly = firstOffAt(full, 50, 60);  // lane 50%, global 100%
            const int tBoth = firstOffAt(half, 50, 60);      // lane 50%, global 50% = 25%
            expect(tLaneOnly > 0 && tBoth > 0, "both note-offs arrive");
            expectWithinAbsoluteError(tLaneOnly - tBoth, 1500, 2); // 50% -> 25% of a step
        }

        beginTest("global chance 0 silences every step; 100 leaves the lane alone");
        {
            ArpEngine e;
            e.prepare(sr);
            auto cp = p;
            cp.chance = 0;
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(cp, clock, 4 * block, chordOn({ 60 }), out);
            for (auto& x : collect(out))
                expect(! x.on, "nothing fires at 0% chance");

            ArpEngine e2;
            e2.prepare(sr);
            auto cp2 = p;
            cp2.chance = 100;
            juce::MidiBuffer out2;
            clock.ppq = 0.0;
            e2.process(cp2, clock, block, chordOn({ 60 }), out2);
            int ons = 0;
            for (auto& x : collect(out2))
                if (x.on)
                    ++ons;
            expectEquals(ons, 1, "100% chance is the same as no chance control at all");
        }

        beginTest("a tie is closed by its own retrigger, not left stacking note-ons");
        {
            // Gate over 100% overlaps into the next step (docs/ARP_DESIGN.md). On a single
            // held note that next step is the same pitch, so the owed note-off has to be
            // pulled back to just before the retrigger. Drain the block up front instead and
            // it lands *after* the note-on that superseded it: two note-ons for one pitch
            // with nothing between them, and a hung voice on any synth that allocates per
            // note-on. Every gate from 101 to 200 hits this.
            ArpEngine e;
            e.prepare(sr);
            auto tie = p;
            tie.gate = 150; // 9000 samples of a 6000-sample 1/16
            juce::MidiBuffer out1, out2;
            clock.ppq = 0.0;
            e.process(tie, clock, block, chordOn({ 60 }), out1);
            clock.ppq = 0.25;
            e.process(tie, clock, block, {}, out2);
            const auto ev = collect(out2);
            expectEquals((int) ev.size(), 2, "exactly one off then one on in the second block");
            expect(! ev[0].on && ev[0].note == 60 && ev[0].sample == 0, "the tie is closed first");
            expect(ev[1].on && ev[1].note == 60 && ev[1].sample == 0, "then the pitch retriggers");
        }

        beginTest("a tie with both hits inside one buffer still cannot stack note-ons");
        {
            // The tie test above uses block == one step, so a tie always crosses the block
            // edge. That is the ONLY shape the rest of the suite exercises, and it hides the
            // interesting case: a buffer long enough to hold two hits of the same pitch AND
            // the first note's whole gate. Emit that first note-off straight into the buffer
            // instead of parking it and the second hit has nothing to close, so it stacks a
            // note-on. Trigger in the wild is just blockSize > gate% x stepSamples - an
            // ordinary 2048-sample buffer at a fast rate with Gate over 100%.
            ArpEngine e;
            e.prepare(sr);
            auto tie = p;
            tie.gate = 150; // 9000 samples of a 6000-sample 1/16
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(tie, clock, 2 * block, chordOn({ 60 }), out); // two steps in one buffer
            int sounding = 0, stacked = 0;
            for (auto& x : collect(out))
            {
                if (x.on && ++sounding > 1)
                    ++stacked;
                else if (! x.on)
                    --sounding;
            }
            expectEquals(stacked, 0, "one pitch never has two note-ons with nothing between");
        }

        beginTest("a tie under a different pitch keeps ringing into the next step");
        {
            // The other half of the contract: closing early is only ever right for the pitch
            // being retriggered. A tie under a different note is the whole point of gate>100.
            ArpEngine e;
            e.prepare(sr);
            auto tie = p;
            tie.gate = 150;
            juce::MidiBuffer out1, out2;
            clock.ppq = 0.0;
            e.process(tie, clock, block, chordOn({ 60, 64 }), out1);
            clock.ppq = 0.25;
            e.process(tie, clock, block, {}, out2);
            const auto ev = collect(out2);
            expectEquals((int) ev.size(), 2);
            expect(ev[0].on && ev[0].note == 64 && ev[0].sample == 0, "the next step fires first");
            expect(! ev[1].on && ev[1].note == 60 && ev[1].sample == 3000,
                   "note 60 keeps ringing 3000 samples into the next step");
        }

        beginTest("an owed note-off precedes the next step's note-on at the same sample");
        {
            // Gate 100% - the shipped default - ends a note exactly where the next one
            // starts, so both land on sample 0 of the next block. The off must be written
            // first: juce::MidiBuffer keeps insertion order at equal timestamps, and a mono
            // or legato instrument downstream glides instead of retriggering if the on wins.
            ArpEngine e;
            e.prepare(sr);
            juce::MidiBuffer out1, out2;
            clock.ppq = 0.0;
            e.process(p, clock, block, chordOn({ 60, 64, 67, 72 }), out1);
            clock.ppq = 0.25;
            e.process(p, clock, block, {}, out2);
            const auto ev = collect(out2);
            expectEquals((int) ev.size(), 2);
            expect(! ev[0].on && ev[0].note == 60 && ev[0].sample == 0, "note 60's off comes first");
            expect(ev[1].on && ev[1].note == 64 && ev[1].sample == 0, "then note 64 starts");
        }

        beginTest("a same-pitch retrigger cannot stretch a note whose gate already ended");
        {
            // Two steps of one repeating pitch inside a single buffer, as an offline bounce
            // hands you. The first note must end at its gate, not be dragged out to the
            // sample before the retrigger by the tie close.
            ArpEngine e;
            e.prepare(sr);
            auto gp = p;
            gp.gate = 50;
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(gp, clock, 2 * block, chordOn({ 60 }), out);
            const auto ev = collect(out);
            expectEquals((int) ev.size(), 4, "two complete on/off pairs inside one block");
            expect(ev[0].on && ev[0].sample == 0);
            expect(! ev[1].on && ev[1].sample == 3000, "ends at its gate, not at the retrigger");
            expect(ev[2].on && ev[2].sample == 6000);
            expect(! ev[3].on && ev[3].sample == 9000);
        }

        beginTest("an owed note-off still drains after the arp stops firing");
        {
            // The drain is the last thing process() does and sits outside the "is the arp
            // running" guard on purpose. Move it inside and a note owed by the last step
            // before the keys came up rings until the next flush.
            ArpEngine e;
            e.prepare(sr);
            juce::MidiBuffer out1;
            clock.ppq = 0.0;
            e.process(p, clock, block, chordOn({ 60 }), out1); // gate 100%: off owed at 6000
            auto stopped = p;
            stopped.enabled = false;
            juce::MidiBuffer out2;
            clock.ppq = 0.25;
            e.process(stopped, clock, block, {}, out2);
            const auto ev = collect(out2);
            expectEquals((int) ev.size(), 1, "nothing new fires, but the owed off still lands");
            expect(! ev[0].on && ev[0].note == 60 && ev[0].sample == 0);
        }

        beginTest("two owners of one pitch: latch still resets on a fresh chord");
        {
            // The engine sits downstream of a merged stream where a chord pad, the live card,
            // a chord held into the arp and the keybed can each ask for the same pitch. Two
            // note-ons for one pitch used to leak physicallyHeld above zero permanently, which
            // disabled latch's fresh-chord reset: the old set never cleared and every chord
            // after it stacked on top, arpeggiating forever with no way back but All Off.
            ArpEngine e;
            e.prepare(sr);
            auto lat = p;
            lat.latch = true;
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(lat, clock, block, chordOn({ 60 }), out); // owner A takes 60
            juce::MidiBuffer in2;
            in2.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0); // owner B takes it too
            clock.ppq = 0.25;
            e.process(lat, clock, block, in2, out);
            expectEquals(e.heldNoteCount(), 1, "one entry for one pitch, however many owners");

            juce::MidiBuffer off1, off2;
            off1.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            clock.ppq = 0.5;
            e.process(lat, clock, block, off1, off1);
            expectEquals(e.heldNoteCount(), 1, "latch keeps it while an owner remains");
            off2.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            clock.ppq = 0.75;
            e.process(lat, clock, block, off2, off2);
            expectEquals(e.heldNoteCount(), 1, "latch still holds it after the last release");

            // The real assertion: a fresh chord now REPLACES rather than stacks.
            clock.ppq = 1.0;
            juce::MidiBuffer out3;
            e.process(lat, clock, block, chordOn({ 67, 71 }), out3);
            expectEquals(e.heldNoteCount(), 2, "the new chord replaced the latched one");
        }

        beginTest("two owners of one pitch: without latch it survives the first release");
        {
            ArpEngine e;
            e.prepare(sr);
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(p, clock, block, chordOn({ 60, 64, 67 }), out);
            juce::MidiBuffer dup;
            dup.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
            clock.ppq = 0.25;
            e.process(p, clock, block, dup, out);
            expectEquals(e.heldNoteCount(), 3);

            juce::MidiBuffer rel;
            rel.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            clock.ppq = 0.5;
            e.process(p, clock, block, rel, rel);
            expectEquals(e.heldNoteCount(), 3, "60 has another owner, so it stays in the chord");

            juce::MidiBuffer rel2;
            rel2.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            clock.ppq = 0.75;
            e.process(p, clock, block, rel2, rel2);
            expectEquals(e.heldNoteCount(), 2, "the last owner lets go and it leaves");
        }

        beginTest("latch holds through note-off; fresh chord replaces the set");
        {
            ArpEngine e;
            e.prepare(sr);
            auto pl = p;
            pl.latch = true;
            juce::MidiBuffer in = chordOn({ 60 });
            in.addEvent(juce::MidiMessage::noteOff(1, 60), 10);
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(pl, clock, block, in, out);
            expectEquals(e.heldNoteCount(), 1); // still held despite the off
            juce::MidiBuffer out2;
            clock.ppq = 0.25;
            e.process(pl, clock, block, chordOn({ 72 }), out2);
            expectEquals(e.heldNoteCount(), 1); // replaced, not accumulated
            bool saw72 = false;
            for (auto& x : collect(out2))
                if (x.on)
                    saw72 = x.note == 72;
            expect(saw72, "latched set was replaced by the new chord");
        }

        beginTest("transport jump flushes owed note-offs instead of leaking them");
        {
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneGate].front().store(200); // long tails
            for (auto& v : e.lanes.value[ArpEngine::laneGate])
                v.store(200);
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(lp, clock, block, chordOn({ 60, 64 }), out);
            juce::MidiBuffer out2;
            clock.ppq = 32.0; // loop jump
            e.process(lp, clock, block, {}, out2);
            bool offAtZero = false;
            for (auto& x : collect(out2))
                if (! x.on && x.sample == 0)
                    offAtZero = true;
            expect(offAtZero, "owed note-offs were flushed at the jump");
        }

        beginTest("upDown plays endpoints once per cycle");
        {
            ArpEngine e;
            e.prepare(sr);
            auto pd = p;
            pd.direction = ArpEngine::Direction::upDown;
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(pd, clock, 4 * block, chordOn({ 60, 64, 67 }), out); // one full cycle: 60 64 67 64
            std::vector<int> ons;
            for (auto& x : collect(out))
                if (x.on)
                    ons.push_back(x.note);
            expect(ons == std::vector<int>({ 60, 64, 67, 64 }), "exclusive ping-pong order");
        }

        beginTest("octave lane transposes by twelve per unit");
        {
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneOctave][0].store(2);
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(lp, clock, block, chordOn({ 60 }), out);
            bool saw = false;
            for (auto& x : collect(out))
                if (x.on)
                {
                    expectEquals(x.note, 84);
                    saw = true;
                }
            expect(saw);
        }

        // Swing moves the odd steps off their own boundary, which is exactly the case where
        // "the boundary is in this block" and "the note fires in this block" stop being the
        // same question. Both directions are checked over several blocks: one block here is
        // one whole step, so any shift at all puts the offbeat in a different buffer than
        // its boundary - the shape that used to drop the note entirely.
        const auto swungOnsetsOfSize = [&](float swing, int blockSize, int blocks)
        {
            auto sp = p;
            sp.swing = swing;
            ArpEngine e;
            e.prepare(sr);
            std::vector<int> onsets; // absolute sample of every note-on
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer out;
                clock.ppq = (double) (b * blockSize) / 24000.0; // 120 bpm at 48 kHz
                e.process(sp, clock, blockSize, b == 0 ? chordOn({ 60, 64 }) : juce::MidiBuffer {}, out);
                for (auto& x : collect(out))
                    if (x.on)
                        onsets.push_back(b * blockSize + x.sample);
            }
            return onsets;
        };
        const auto swungOnsets = [&](float swing, int blocks)
        { return swungOnsetsOfSize(swing, block, blocks); };

        beginTest("positive swing delays the offbeats without dropping them");
        {
            // Four blocks, so four steps' worth of timeline. Half a step is 3000 samples.
            const auto onsets = swungOnsets(0.5f, 4);
            expectEquals((int) onsets.size(), 4, "every step still fires - none lost to the "
                                                 "block its swing pushed it into");
            if (onsets.size() == 4)
            {
                expectWithinAbsoluteError(onsets[0], 0, 2);
                expectWithinAbsoluteError(onsets[1], 9000, 2);  // 6000 + 3000 late
                expectWithinAbsoluteError(onsets[2], 12000, 2);
                expectWithinAbsoluteError(onsets[3], 21000, 2); // 18000 + 3000 late
            }
        }

        beginTest("negative swing pulls the offbeats early");
        {
            const auto onsets = swungOnsets(-0.5f, 4);
            expectEquals((int) onsets.size(), 4, "an early step belongs to the block before "
                                                 "its own boundary, and must still fire");
            if (onsets.size() == 4)
            {
                expectWithinAbsoluteError(onsets[0], 0, 2);
                expectWithinAbsoluteError(onsets[1], 3000, 2);  // 6000 - 3000 early
                expectWithinAbsoluteError(onsets[2], 12000, 2);
                expectWithinAbsoluteError(onsets[3], 15000, 2); // 18000 - 3000 early
            }
        }

        beginTest("swing survives a buffer far shorter than a step");
        {
            // The real host case, and the one that used to break: at 512 samples a boundary
            // usually lands mid-buffer, so a swung note fires several buffers later than the
            // one its boundary was in. The old scheduler looked only at boundaries inside
            // the current buffer and gave up on any note whose swing carried it past the
            // end, so every offbeat it could not fire immediately was lost for good.
            // 48 x 512 = 24576 samples, four steps and a bit.
            const auto late = swungOnsetsOfSize(0.5f, 512, 48);
            expectEquals((int) late.size(), 5, "no offbeat may be dropped between buffers");
            if (late.size() == 5)
            {
                expectWithinAbsoluteError(late[0], 0, 2);
                expectWithinAbsoluteError(late[1], 9000, 2);
                expectWithinAbsoluteError(late[2], 12000, 2);
                expectWithinAbsoluteError(late[3], 21000, 2);
                expectWithinAbsoluteError(late[4], 24000, 2);
            }

            const auto early = swungOnsetsOfSize(-0.5f, 512, 48);
            expectEquals((int) early.size(), 5, "and none early, either");
            if (early.size() == 5)
            {
                expectWithinAbsoluteError(early[0], 0, 2);
                expectWithinAbsoluteError(early[1], 3000, 2);
                expectWithinAbsoluteError(early[2], 12000, 2);
                expectWithinAbsoluteError(early[3], 15000, 2);
                expectWithinAbsoluteError(early[4], 24000, 2);
            }
        }

        beginTest("zero swing is dead straight");
        {
            const auto onsets = swungOnsets(0.0f, 4);
            expectEquals((int) onsets.size(), 4);
            for (int i = 0; i < (int) onsets.size(); ++i)
                expectWithinAbsoluteError(onsets[(size_t) i], i * 6000, 2);
        }

        // ---------------------------------------------------------------------------------
        // The 2026-07-30 expansion. Each of these pins a default as much as a feature: every
        // parameter below is additive and has to leave an untouched arp sounding as it did.
        // ---------------------------------------------------------------------------------

        // Walk one step per block and hand back the note-ons of each, which is the shape
        // most of these tests want and none of the older helpers provide.
        const auto stepNotes = [&](ArpEngine& e, ArpEngine::Params sp, int steps,
                                   const juce::MidiBuffer& first)
        {
            std::vector<std::vector<int>> perStep;
            auto c = clock;
            for (int i = 0; i < steps; ++i)
            {
                juce::MidiBuffer out;
                c.ppq = 0.25 * i;
                e.process(sp, c, block, i == 0 ? first : juce::MidiBuffer {}, out);
                std::vector<int> ons;
                for (const auto meta : out)
                    if (meta.getMessage().isNoteOn())
                        ons.push_back(meta.getMessage().getNoteNumber());
                perStep.push_back(std::move(ons));
            }
            return perStep;
        };

        beginTest("Chord shape plays the whole held chord on every step");
        {
            ArpEngine e;
            e.prepare(sr);
            auto cp = p;
            cp.direction = ArpEngine::Direction::chord;
            const auto steps = stepNotes(e, cp, 3, chordOn({ 60, 64, 67 }));
            for (int i = 0; i < 3; ++i)
            {
                expectEquals((int) steps[(size_t) i].size(), 3, "every step is the whole chord");
                if (steps[(size_t) i].size() == 3)
                {
                    auto s = steps[(size_t) i];
                    std::sort(s.begin(), s.end());
                    expect(s[0] == 60 && s[1] == 64 && s[2] == 67, "and it is the chord as played");
                }
            }
        }

        beginTest("Distance in semitones stacks each repeat by that interval");
        {
            ArpEngine e;
            e.prepare(sr);
            auto sp = p;
            sp.octaveRange = 2;
            sp.spread = 7; // a fifth, not the octave this used to hardcode
            const auto steps = stepNotes(e, sp, 4, chordOn({ 60, 64 }));
            expectEquals(steps[0].empty() ? -1 : steps[0][0], 60);
            expectEquals(steps[1].empty() ? -1 : steps[1][0], 64);
            expectEquals(steps[2].empty() ? -1 : steps[2][0], 67);
            expectEquals(steps[3].empty() ? -1 : steps[3][0], 71);
        }

        beginTest("Distance in scale degrees follows the key, not a fixed interval");
        {
            ArpEngine e;
            e.prepare(sr);
            auto sp = p;
            sp.octaveRange = 2;
            sp.spread = 2; // two degrees: a third of the scale
            sp.spreadDegrees = true;
            sp.rootPc = 0;
            sp.scaleMask = 0b101010110101u; // C major
            const auto steps = stepNotes(e, sp, 4, chordOn({ 60, 62 }));
            expectEquals(steps[0].empty() ? -1 : steps[0][0], 60);
            expectEquals(steps[1].empty() ? -1 : steps[1][0], 62);
            // C lifts a major third to E, D lifts a *minor* third to F. That difference is
            // the whole point: a fixed +4 would have given F# and left the key.
            expectEquals(steps[2].empty() ? -1 : steps[2][0], 64);
            expectEquals(steps[3].empty() ? -1 : steps[3][0], 65);
        }

        beginTest("Offset rotates the lane read");
        {
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneOctave][1].store(1); // only step 2 jumps an octave
            auto sp = lp;
            sp.offset = 1;
            const auto steps = stepNotes(e, sp, 1, chordOn({ 60 }));
            expectEquals(steps[0].empty() ? -1 : steps[0][0], 72,
                         "with Offset 1 the run starts on what was step 2");
        }

        beginTest("beat retrigger restarts the lanes on its window");
        {
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneOctave][0].store(1); // step 1 of the lane only
            auto sp = lp;
            sp.retrigBeats = 1.0; // one beat = four 1/16 steps
            const auto steps = stepNotes(e, sp, 5, chordOn({ 60 }));
            expectEquals(steps[0].empty() ? -1 : steps[0][0], 72, "step 1 is the lane's first");
            expectEquals(steps[1].empty() ? -1 : steps[1][0], 60);
            expectEquals(steps[4].empty() ? -1 : steps[4][0], 72,
                         "and the next beat starts the lane over");
        }

        beginTest("Random Once locks its order for as long as the chord is held");
        {
            ArpEngine e;
            e.prepare(sr);
            auto sp = p;
            sp.direction = ArpEngine::Direction::randomOnce;
            const auto steps = stepNotes(e, sp, 6, chordOn({ 60, 64, 67 }));
            for (int i = 0; i < 3; ++i)
            {
                expect(! steps[(size_t) i].empty() && ! steps[(size_t) (i + 3)].empty(),
                       "every step fires");
                if (! steps[(size_t) i].empty() && ! steps[(size_t) (i + 3)].empty())
                    expectEquals(steps[(size_t) (i + 3)][0], steps[(size_t) i][0],
                                 "the second time round is the same order");
            }
        }

        beginTest("Random Other never repeats a note back to back");
        {
            ArpEngine e;
            e.prepare(sr);
            auto sp = p;
            sp.direction = ArpEngine::Direction::randomOther;
            const auto steps = stepNotes(e, sp, 24, chordOn({ 60, 64, 67 }));
            int prev = -1;
            for (const auto& s : steps)
                if (! s.empty())
                {
                    expect(s[0] != prev, "consecutive steps differ");
                    prev = s[0];
                }
        }

        beginTest("the velocity ramp falls away over its time and not before");
        {
            const auto firstVelocity = [](const juce::MidiBuffer& b)
            {
                for (const auto meta : b)
                    if (meta.getMessage().isNoteOn())
                        return meta.getMessage().getFloatVelocity();
                return -1.0f;
            };
            ArpEngine e;
            e.prepare(sr);
            auto sp = p;
            sp.velRamp = -100;  // fade to nothing
            sp.rampBeats = 1.0; // over one beat, which is four steps here
            float first = -1.0f, last = -1.0f;
            for (int i = 0; i < 5; ++i)
            {
                juce::MidiBuffer out;
                clock.ppq = 0.25 * i;
                e.process(sp, clock, block, i == 0 ? chordOn({ 60 }) : juce::MidiBuffer {}, out);
                const float v = firstVelocity(out);
                if (i == 0)
                    first = v;
                if (i == 4)
                    last = v;
            }
            expect(first > 0.7f, "the first hit is the velocity it was played at");
            expect(last >= 0.0f && last < 0.15f, "and a beat later it has faded out");
        }

        beginTest("the Transpose lane counts scale degrees, not semitones");
        {
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneTranspose][0].store(2); // a third of the scale
            e.lanes.value[ArpEngine::laneTranspose][1].store(2);
            auto sp = lp;
            sp.rootPc = 0;
            sp.scaleMask = 0b101010110101u; // C major
            const auto steps = stepNotes(e, sp, 2, chordOn({ 60, 62 }));
            expectEquals(steps[0].empty() ? -1 : steps[0][0], 64, "C lifts a major third to E");
            expectEquals(steps[1].empty() ? -1 : steps[1][0], 65, "D lifts a minor third to F");
        }

        beginTest("the Late lane delays a step and nothing else");
        {
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneLate][1].store(50); // half a step late
            auto sp = lp;
            std::vector<int> onsets;
            for (int i = 0; i < 3; ++i)
            {
                juce::MidiBuffer out;
                clock.ppq = 0.25 * i;
                e.process(sp, clock, block, i == 0 ? chordOn({ 60 }) : juce::MidiBuffer {}, out);
                for (const auto meta : out)
                    if (meta.getMessage().isNoteOn())
                        onsets.push_back(i * block + meta.samplePosition);
            }
            expectEquals((int) onsets.size(), 3, "every step still fires exactly once");
            if (onsets.size() == 3)
            {
                expectWithinAbsoluteError(onsets[0], 0, 2);
                expectWithinAbsoluteError(onsets[1], 9000, 2); // 6000 + half a 6000-sample step
                expectWithinAbsoluteError(onsets[2], 12000, 2);
            }
        }

        beginTest("the Harmony lane adds a second note from inside the chord");
        {
            ArpEngine e;
            e.prepare(sr);
            for (int s = 0; s < 4; ++s)
                e.lanes.value[ArpEngine::laneHarmony][(size_t) s].store(2); // two chord tones up
            const auto steps = stepNotes(e, lp, 2, chordOn({ 60, 64, 67 }));
            expectEquals((int) steps[0].size(), 2, "one note becomes two");
            if (steps[0].size() == 2)
            {
                auto s = steps[0];
                std::sort(s.begin(), s.end());
                expect(s[0] == 60 && s[1] == 67, "the root and the note two tones above it");
            }
        }

        beginTest("the Chord lane calls up a slot's chord for that step");
        {
            ArpEngine::ChordTable table;
            table.note[3][0].store(53); // slot 4 holds F major
            table.note[3][1].store(57);
            table.note[3][2].store(60);
            table.count[3].store(3);

            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneChord][1].store(4); // step 2 only
            auto sp = lp;
            sp.chords = &table;
            const auto steps = stepNotes(e, sp, 3, chordOn({ 72 }));
            expectEquals(steps[0].empty() ? -1 : steps[0][0], 72, "step 1 is what is held");
            expectEquals((int) steps[1].size(), 3, "step 2 is the stored chord");
            if (steps[1].size() == 3)
            {
                auto s = steps[1];
                std::sort(s.begin(), s.end());
                expect(s[0] == 53 && s[1] == 57 && s[2] == 60, "and it is that chord");
            }
            expectEquals(steps[2].empty() ? -1 : steps[2][0], 72, "step 3 is held again");
        }

        beginTest("an empty Chord-lane slot leaves the step alone");
        {
            ArpEngine::ChordTable table; // nothing stored anywhere
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneChord][0].store(7);
            auto sp = lp;
            sp.chords = &table;
            const auto steps = stepNotes(e, sp, 1, chordOn({ 72 }));
            expectEquals(steps[0].empty() ? -1 : steps[0][0], 72,
                         "a slot with no chord in it is not a silent step");
        }

        beginTest("Humanize is late-only, bounded, and does nothing at zero");
        {
            const auto onsetsWith = [&](int human)
            {
                ArpEngine e;
                e.prepare(sr);
                auto sp = p;
                sp.humanize = human;
                std::vector<int> onsets;
                for (int i = 0; i < 8; ++i)
                {
                    juce::MidiBuffer out;
                    clock.ppq = 0.25 * i;
                    e.process(sp, clock, block, i == 0 ? chordOn({ 60, 64 }) : juce::MidiBuffer {}, out);
                    for (const auto meta : out)
                        if (meta.getMessage().isNoteOn())
                            onsets.push_back(meta.samplePosition);
                }
                return onsets;
            };
            for (const int at : onsetsWith(0))
                expectEquals(at, 0, "at zero every step lands exactly on its boundary");
            const auto loose = onsetsWith(100);
            bool sawLate = false;
            for (const int at : loose)
            {
                expect(at >= 0, "never early");
                expect(at <= 1200, "and never later than 25 ms"); // 0.025 * 48000
                sawLate = sawLate || at > 0;
            }
            expect(sawLate, "something actually moved");
        }

        beginTest("Humanize velocity is its own knob, and VEL is an absolute 0-127 band");
        {
            // One Humanize drove both halves until 2026-08-02; now `humanize` is the timing
            // nudge alone and `humanVel` the velocity shave alone. `velLevel` replaced the
            // bipolar `velTrim` on 2026-08-18 (Owen: "still wrong", on a VEL readout showing
            // "-31 ~20" right after asking for velocity ranges to span 0-127): it is MIDI
            // velocity outright rather than a percentage trim on the velocity that arrived, and
            // `humanVel` is how far under it a hit may fall, in the same units. Each question
            // below fails if the split leaks or the units drift apart again.
            const auto velsWith = [&](int human, int humanVel, int level)
            {
                ArpEngine e;
                e.prepare(sr);
                auto sp = p;
                sp.humanize = human;
                sp.humanVel = humanVel;
                sp.velLevel = level;
                std::vector<float> vels;
                for (int i = 0; i < 8; ++i)
                {
                    juce::MidiBuffer out;
                    clock.ppq = 0.25 * i;
                    e.process(sp, clock, block, i == 0 ? chordOn({ 60, 64 }) : juce::MidiBuffer {}, out);
                    for (const auto meta : out)
                        if (meta.getMessage().isNoteOn())
                            vels.push_back(meta.getMessage().getFloatVelocity());
                }
                return vels;
            };

            // **The knob is the velocity, whatever the chord arrived at.** chordOn() feeds every
            // note at 0.8, and the run below plays at 100/127 regardless - which is the whole
            // change, and the one thing "as played" used to mean that no longer applies.
            const auto base = velsWith(0, 0, 100);
            expect(! base.empty(), "the reference run plays");
            for (const float v : base)
                expectWithinAbsoluteError(v, 100.0f / 127.0f, 0.012f,
                                          "VEL 100 plays at MIDI velocity 100, not at the input's");

            const auto quiet = velsWith(0, 0, 40);
            for (const float v : quiet)
                expectWithinAbsoluteError(v, 40.0f / 127.0f, 0.012f, "and VEL 40 at velocity 40");

            const auto timingOnly = velsWith(100, 0, 100);
            for (size_t i = 0; i < timingOnly.size() && i < base.size(); ++i)
                expectWithinAbsoluteError(timingOnly[i], base[i], 0.005f,
                                          "full H.TIME leaves every velocity alone");

            // The ring is in velocity units too, and it reaches **either side of the level**
            // since 2026-08-19 (Owen, on the halo: "should be equal from center"): 40 around a
            // level of 60 is a band of 20..100, and both halves of it actually get used.
            const auto ringed = velsWith(0, 40, 60);
            bool sawQuieter = false, sawLouder = false;
            for (const float v : ringed)
            {
                expect(v <= 100.0f / 127.0f + 0.012f, "never further above the level than the ring");
                expect(v >= 20.0f / 127.0f - 0.012f, "and never further below it");
                sawQuieter = sawQuieter || v < 60.0f / 127.0f - 0.005f;
                sawLouder = sawLouder || v > 60.0f / 127.0f + 0.005f;
            }
            expect(sawQuieter && sawLouder, "a ring of 40 lands on both sides of the level");

            // Equal from centre at the rails too: a level of 120 leaves only 7 of room above,
            // so the band is 113..127 however wide the ring is turned.
            for (const float v : velsWith(0, 40, 120))
                expect(v >= 113.0f / 127.0f - 0.012f && v <= 1.0f + 0.005f,
                       "at the rail the band stays equal, not lopsided");

            // The bottom of the band is taken literally now: no 0.05 audibility floor lifting it,
            // because the level *is* the velocity and a band drawn low is meant to be quiet. The
            // clamp still stops at one MIDI step, since zero would be a note-off in disguise.
            const auto deep = velsWith(0, 0, 1);
            expect(! deep.empty(), "a level of 1 still plays");
            for (const float v : deep)
                expectWithinAbsoluteError(v, 1.0f / 127.0f, 0.006f,
                                          "velocity 1 leaves as velocity 1");
            expect(velsWith(0, 0, 0).empty(), "a level of 0 is a mute, exactly as VOL 0 was");
        }

        // ---------------------------------------------------------------------------------
        // Hz mode: the second timebase. `rateFree` swaps the beat grid for a free-running
        // frequency - process() pins the tempo to 60 so that one "beat" is one second and the
        // step is 1/hz of it, and the playhead stops being read for step timing at all.
        // Everything below is a question about *when* a step fires, so it all goes through
        // one helper: n buffers, each block's ppq chosen by the caller, and back come the
        // absolute samples of every note-on.
        // ---------------------------------------------------------------------------------
        const auto onsetsOf = [&](const ArpEngine::Params& sp, ArpEngine::HostClock c,
                                  int blockSize, int blocks, auto ppqAt)
        {
            ArpEngine e;
            e.prepare(sr);
            std::vector<int> onsets;
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer out;
                c.ppq = ppqAt(b);
                e.process(sp, c, blockSize, b == 0 ? chordOn({ 60, 64 }) : juce::MidiBuffer {}, out);
                for (auto& x : collect(out))
                    if (x.on)
                        onsets.push_back(b * blockSize + x.sample);
            }
            return onsets;
        };

        // 8 Hz is a step every 6000 samples at 48 kHz, which is deliberately the same speed as
        // the 1/16 at 120 bpm the rest of this suite runs at: the two timebases can then be
        // read against each other onset for onset.
        auto hzp = p;
        hzp.rateFree = true;
        hzp.rateHz = 8.0;

        const auto steady = [](int b) { return 0.25 * b; }; // an honest 120 bpm playhead
        const auto wild = [](int b)
        {
            // Loop points, relocates and a scrub backwards, one per block, and none of them on
            // a step boundary - a jump that happens to land on one would move a synced arp
            // nowhere either, and prove nothing about a free one.
            static constexpr double jumps[] = { 0.0, 7.1, 3.33, 128.2, 0.5, 64.07, 0.125, 96.4 };
            return jumps[(size_t) (b % 8)];
        };

        beginTest("Hz mode never reads the playhead");
        {
            // Anchored is on and the transport is rolling, which in Sync is exactly the case
            // that affixes steps to the host bar grid. In Hz the anchored branch is skipped
            // outright, so a loop, a relocate and a scrub have to leave every onset where it
            // was: the free phase is the only clock there is.
            const auto straight = onsetsOf(hzp, clock, block, 8, steady);
            const auto jumping = onsetsOf(hzp, clock, block, 8, wild);
            expectEquals((int) straight.size(), 8, "8 Hz is one step per 6000-sample block");
            expect(jumping == straight, "a jumping playhead moves nothing in Hz mode");
            for (int i = 0; i < (int) straight.size(); ++i)
                expectWithinAbsoluteError(straight[(size_t) i], i * 6000, 2);
        }

        beginTest("Sync mode does follow the playhead, on the same clock and chord");
        {
            // The other half of the claim above: what differs is the mode, not the helper.
            // Half a step of ppq offset moves every synced onset half a step, and the wild
            // playhead that Hz sat still through moves it about at will.
            const auto onGrid = onsetsOf(p, clock, block, 8, steady);
            const auto shifted = onsetsOf(p, clock, block, 8, [](int b) { return 0.25 * b + 0.125; });
            expectEquals((int) onGrid.size(), 8);
            expectEquals((int) shifted.size(), 8);
            for (int i = 0; i < 8 && i < (int) shifted.size(); ++i)
                expectWithinAbsoluteError(shifted[(size_t) i], i * 6000 + 3000, 2);
            expect(onsetsOf(p, clock, block, 8, wild) != onGrid,
                   "a synced arp goes where the transport goes");
        }

        beginTest("the Hz step is one over the rate, and Dot and Tuplet do not touch it");
        {
            // Sync divides a beat, so Dot and Tuplet are subdivisions of one. Hz has no beat to
            // subdivide, and all a dotted 8 Hz could do is make the number on the dial a lie.
            const auto period = [&](const ArpEngine::Params& sp, int blocks)
            {
                const auto on = onsetsOf(sp, clock, block, blocks, steady);
                return on.size() >= 2 ? on[1] - on[0] : -1;
            };
            auto slow = hzp;
            slow.rateHz = 4.0; // 12000 samples
            auto fast = hzp;
            fast.rateHz = 16.0; // 3000 samples
            expectWithinAbsoluteError(period(hzp, 4), 6000, 2);
            expectWithinAbsoluteError(period(slow, 6), 12000, 2);
            expectWithinAbsoluteError(period(fast, 4), 3000, 2);

            auto dot = hzp;
            dot.dotted = true;
            auto trip = hzp;
            trip.tuplet = 3;
            auto both = hzp;
            both.dotted = true;
            both.tuplet = 3;
            expectWithinAbsoluteError(period(dot, 4), 6000, 2, "Dot is not applied in Hz");
            expectWithinAbsoluteError(period(trip, 4), 6000, 2, "and neither is Tuplet");
            expectWithinAbsoluteError(period(both, 4), 6000, 2, "nor the two together");

            // ...and both still mean exactly what they always did on the synced side.
            auto syncDot = p;
            syncDot.dotted = true;
            auto syncTrip = p;
            syncTrip.tuplet = 3;
            expectWithinAbsoluteError(period(syncDot, 4), 9000, 2, "a dotted 1/16 is a step and a half");
            expectWithinAbsoluteError(period(syncTrip, 4), 4000, 2, "a triplet 1/16 is two thirds of one");
        }

        beginTest("the Humanize knob is the centre of its range, and the ring reaches both ways");
        {
            // 2026-08-03 built the ring; 2026-08-19 centred it (Owen, on the halo: "moving the
            // halo shouldn't move knob. should be equal from center"). The draw is the knob
            // plus or minus the ring, equal on both sides, with the reach stopped where a rail
            // is nearer - the knob itself on the low side, so a hit is never early, and 100
            // less the knob on the high side, so the band never goes lopsided. The halves
            // pinned here are the ones that are easy to build the other way round.
            const auto onsets = [&](int amount, int spanPct)
            {
                auto sp = p;
                sp.humanize = amount;
                sp.humanizeSpan = spanPct;
                sp.anchored = false;
                return onsetsOf(sp, clock, block, 12, steady);
            };

            // A 1/16 is 6000 samples here, and Humanize at 100 is 25 ms = 1200 samples - under
            // the engine's 40% ordering cap at this rate, so 1200 is the scale in force.
            const auto closed = onsets(100, 0);
            expect(closed.size() >= 4, "the run has to fire before this proves anything");
            // A ring of zero collapses the range onto the knob: no randomness left, every hit
            // exactly 1200 late, so the gaps are all one step.
            for (size_t i = 1; i < closed.size(); ++i)
                expectWithinAbsoluteError((double) (closed[i] - closed[i - 1]), 6000.0, 2.0,
                                          "a closed range is a fixed offset, not a draw");
            // ... and it is pinned to the *knob*, not to zero.
            expectWithinAbsoluteError((double) closed[0], 1200.0, 2.0,
                                      "every hit is a full 25 ms late");

            // The knob at its top has no room above, so equal-from-centre means no room below
            // either: a maxed knob is the same fixed offset whatever the ring says.
            const auto maxed = onsets(100, 100);
            expect(maxed.size() >= 4);
            for (const auto o : maxed)
                expectWithinAbsoluteError((double) (o % 6000), 1200.0, 2.0,
                                          "at the rail the band has nowhere to open");

            // Mid-knob, ring wide open: the full reach both ways, nothing early, up to 2x the
            // knob late, and actually random.
            const auto open = onsets(50, 100);
            expect(open.size() >= 4);
            expect(open != closed, "an open ring still randomizes");
            bool sawBelow = false, sawAbove = false;
            for (const auto o : open)
            {
                expect(o % 6000 <= 1201, "never past twice the knob");
                sawBelow = sawBelow || (o % 6000) < 599;
                sawAbove = sawAbove || (o % 6000) > 601;
            }
            expect(sawBelow && sawAbove, "the draw lands on both sides of the knob");

            // **The range travels with the knob.** Halve the knob with the ring closed and
            // every hit lands at half the offset - the proof that the range is measured from
            // the knob rather than up from zero.
            const auto halfClosed = onsets(50, 0);
            expect(halfClosed.size() >= 4);
            expectWithinAbsoluteError((double) halfClosed[0], 600.0, 2.0,
                                      "the closed range moved with the dial");

            // A narrower ring is a narrower band either side of the knob: 50 +/- 20 is 30% to
            // 70% of 25 ms, and nothing outside it.
            const auto narrow = onsets(50, 20);
            expect(narrow.size() >= 4);
            for (const auto o : narrow)
            {
                expect(o % 6000 >= 359, "every hit is at least as late as the band's bottom");
                expect(o % 6000 <= 841, "and no later than its top");
            }

            // Humanize itself off means the ring does nothing: there is no draw to open, and a
            // ring that nudged on its own would make a knob at zero audible.
            const auto off = onsets(0, 0);
            expect(off.size() >= 4);
            expectWithinAbsoluteError((double) off[0], 0.0, 2.0,
                                      "a closed ring under a Humanize of zero is still zero");
        }

        beginTest("the rate readout is the step length as a fraction of a bar");
        {
            // 2026-08-03, Owen: "shouldn't it just be 1/5 not 1/4:5?" - it should, and this is
            // what makes it so. The invariant worth pinning is that the *straight* readings are
            // byte-identical to the division names the parameter carries, since that is what
            // stops this from being a second, drifting copy of the rate list.
            const auto text = [](int i, bool dot, int tup) { return arptext::rateSyncText(i, dot, tup); };
            expectEquals(text(0, false, 0), juce::String("16 bars"));
            expectEquals(text(4, false, 0), juce::String("1 bar"), "singular at one");
            expectEquals(text(5, false, 0), juce::String("1/2"));
            expectEquals(text(7, false, 0), juce::String("1/8"));
            expectEquals(text(10, false, 0), juce::String("1/64"));

            // Owen's own example, and the reason the notation works: five steps in the space of
            // four quarters is four fifths of a beat, which is one fifth of a bar.
            expectEquals(text(6, false, 5), juce::String("1/5"), "a quarter in fives is 1/5");
            expectEquals(text(7, false, 3), juce::String("1/12"), "an 1/8 in threes is 1/12");
            expectEquals(text(7, false, 5), juce::String("1/10"), "an 1/8 in fives is 1/10");
            expectEquals(text(8, false, 5), juce::String("1/20"), "a 1/16 in fives is 1/20");
            expectEquals(text(6, false, 7), juce::String("1/7"), "a quarter in sevens is 1/7");
            expectEquals(text(7, false, 9), juce::String("1/9"), "an 1/8 in nines is 1/9");

            // Not every combination reduces to a unit fraction, and the honest answer is the
            // one printed rather than a rounded note value.
            expectEquals(text(5, false, 5), juce::String("2/5"), "a 1/2 in fives is two fifths");

            // Dot stays a dot: universal, and instantly read where "3/16" would have to be
            // worked out. It applies to whatever the tuplet already produced.
            expectEquals(text(7, true, 0), juce::String("1/8."));
            expectEquals(text(7, true, 5), juce::String("1/10."));
        }

        beginTest("a tuplet is N steps in the space of the power of two below N");
        {
            // 2026-08-03, the day Trip became Tuplet (Owen: "what if I want 1/5 or other
            // division?"). The whole feature is one multiplier, so this pins the multiplier:
            // a 1/16 is 6000 samples at this tempo, and N of them have to fill exactly the
            // span that tupletSpace(N) straight ones would.
            const auto period = [&](const ArpEngine::Params& sp, int blocks)
            {
                const auto on = onsetsOf(sp, clock, block, blocks, steady);
                return on.size() >= 2 ? on[1] - on[0] : -1;
            };
            const auto withTuplet = [&p](int n) { auto q = p; q.tuplet = n; return q; };
            // Doubles throughout: 7 in the space of 4 does not land on a whole sample, and
            // expectWithinAbsoluteError takes all three of its arguments as one type.
            const auto gap = [&](int n) { return (double) period(withTuplet(n), 4); };

            // Off and 1 are both straight - the choice list's "Off" arrives here as 0, and 1
            // is "one in the space of one", which is the same statement.
            expectWithinAbsoluteError(gap(0), 6000.0, 2.0, "0 is straight");
            expectWithinAbsoluteError(gap(1), 6000.0, 2.0, "and so is 1");
            expectWithinAbsoluteError(gap(3), 4000.0, 2.0, "3 in the space of 2");
            expectWithinAbsoluteError(gap(5), 4800.0, 2.0, "5 in the space of 4");
            expectWithinAbsoluteError(gap(7), 3428.57, 2.0, "7 in the space of 4");
            expectWithinAbsoluteError(gap(9), 5333.33, 2.0, "9 in the space of 8");

            // The two axes compose rather than colliding: Dot lengthens a step by half, a
            // tuplet divides a span into N. A dotted 1/16 quintuplet is 9000 * 4/5.
            auto dotFive = p;
            dotFive.dotted = true;
            dotFive.tuplet = 5;
            expectWithinAbsoluteError((double) period(dotFive, 4), 7200.0, 2.0,
                                      "dotted and in fives at once");

            // And the span is the point: five 1/16 quintuplet steps take exactly as long as
            // four straight 1/16s, which is what makes it playable against another line.
            expectWithinAbsoluteError(gap(5) * 5.0, 6000.0 * 4.0, 16.0,
                                      "five quintuplet steps fill four straight ones");
            expectWithinAbsoluteError(gap(7) * 7.0, 6000.0 * 4.0, 16.0,
                                      "and seven septuplet steps do too");
        }

        beginTest("a rate-mode flip mid-note closes everything it owed, at offset 0");
        {
            // A change of timebase is treated exactly like a transport jump: the step in
            // flight belongs to a timeline that no longer exists, so what is owed is closed at
            // the top of the block rather than left to land somewhere on the new clock. Gate
            // 200% keeps two pitches ringing across the flip, which is the leak this guards.
            // Both directions, because what is watched is the flag, not the way it moved.
            const auto flipCheck = [&](bool startFree, const juce::String& what)
            {
                ArpEngine e;
                e.prepare(sr);
                auto before = p;
                before.gate = 200; // every note is owed two steps past its own
                before.rateFree = startFree;
                auto after = before;
                after.rateFree = ! startFree;

                std::array<int, 128> sounding {};
                auto c = clock;
                for (int b = 0; b < 3; ++b)
                {
                    juce::MidiBuffer out;
                    c.ppq = 0.25 * b;
                    e.process(before, c, block,
                              b == 0 ? chordOn({ 60, 64, 67, 72 }) : juce::MidiBuffer {}, out);
                    for (auto& x : collect(out))
                        sounding[(size_t) x.note] += x.on ? 1 : -1;
                }
                int owed = 0;
                for (int n : sounding)
                    owed += n;
                expect(owed > 0, what + ": something is still ringing when the mode flips");

                juce::MidiBuffer out;
                c.ppq = 0.75;
                e.process(after, c, block, {}, out);
                const auto ev = collect(out);
                int firstOn = (int) ev.size();
                for (int i = 0; i < (int) ev.size(); ++i)
                    if (ev[(size_t) i].on)
                    {
                        firstOn = i;
                        break;
                    }
                expect(firstOn < (int) ev.size(), what + ": the new clock fires in this very block, "
                                                         "so there is something to sort against");

                std::array<int, 128> fired {};
                for (int i = 0; i < (int) ev.size(); ++i)
                {
                    const auto& x = ev[(size_t) i];
                    if (! x.on)
                    {
                        expectEquals(x.sample, 0, what + ": every owed note-off is flushed at the "
                                                         "top of the block");
                        expect(i < firstOn, what + ": and before any note-on at the same sample");
                    }
                    else
                        ++fired[(size_t) x.note];
                    sounding[(size_t) x.note] += x.on ? 1 : -1;
                }

                int leaked = 0;
                for (int n = 0; n < 128; ++n)
                    if (sounding[(size_t) n] != fired[(size_t) n])
                        ++leaked;
                expectEquals(leaked, 0, what + ": every pitch that had a note-on has its note-off; "
                                               "nothing hangs on the far side of the timebase");
            };
            flipCheck(false, "Sync to Hz");
            flipCheck(true, "Hz to Sync");
        }

        beginTest("swing is a fraction of the Hz step, not of a beat");
        {
            // Under the 60 bpm pinning a "beat" is a second, so everything measured in steps
            // has to scale with the Hz period instead. Half a step of swing is 6000 samples at
            // 4 Hz and 3000 at 8 Hz; a swing that had leaked into real beats would move both
            // offbeats by the same amount.
            auto slow = hzp;
            slow.rateHz = 4.0;
            slow.swing = 0.5f;
            auto fast = hzp;
            fast.swing = 0.5f; // 8 Hz
            const auto a = onsetsOf(slow, clock, block, 8, steady);
            expectEquals((int) a.size(), 4, "no offbeat lost to the block its swing pushed it into");
            if (a.size() == 4)
            {
                expectWithinAbsoluteError(a[0], 0, 2);
                expectWithinAbsoluteError(a[1], 18000, 2); // 12000 + half of a 12000-sample step
                expectWithinAbsoluteError(a[2], 24000, 2);
                expectWithinAbsoluteError(a[3], 42000, 2);
            }
            const auto b = onsetsOf(fast, clock, block, 8, steady);
            expectEquals((int) b.size(), 8);
            if (b.size() == 8)
            {
                expectWithinAbsoluteError(b[1], 9000, 2); // 6000 + half of a 6000-sample step
                expectWithinAbsoluteError(b[3], 21000, 2);
            }
        }

        beginTest("gate measures the note against the Hz step, not against a beat");
        {
            const auto firstOffOf = [&](double hz, int gate, int blocks)
            {
                auto sp = hzp;
                sp.rateHz = hz;
                sp.gate = gate;
                ArpEngine e;
                e.prepare(sr);
                auto c = clock;
                // Four held notes, so the note under test fires once and is not closed early
                // by its own retrigger on the next step.
                for (int i = 0; i < blocks; ++i)
                {
                    juce::MidiBuffer out;
                    c.ppq = 0.25 * i;
                    e.process(sp, c, block,
                              i == 0 ? chordOn({ 60, 64, 67, 72 }) : juce::MidiBuffer {}, out);
                    for (auto& x : collect(out))
                        if (! x.on && x.note == 60)
                            return i * block + x.sample;
                }
                return -1;
            };
            // Half a step is half of 1/hz seconds, and nothing else.
            expectWithinAbsoluteError(firstOffOf(8.0, 50, 4), 3000, 2);
            expectWithinAbsoluteError(firstOffOf(4.0, 50, 4), 6000, 2);
            expectWithinAbsoluteError(firstOffOf(16.0, 50, 4), 1500, 2);
        }

        beginTest("the Late lane delays by a fraction of the Hz step");
        {
            // Two rates, because one proves nothing: a lane that had quietly gone on measuring
            // itself against a beat would still look right at whichever rate happens to match
            // the synced division underneath it.
            const auto lateRun = [&](double hz, int blocks)
            {
                ArpEngine e;
                e.prepare(sr);
                e.lanes.value[ArpEngine::laneLate][1].store(50); // step 2, half a step late
                auto sp = hzp;
                sp.usePattern = true;
                sp.rateHz = hz;
                std::vector<int> onsets;
                auto c = clock;
                for (int i = 0; i < blocks; ++i)
                {
                    juce::MidiBuffer out;
                    c.ppq = 0.25 * i;
                    e.process(sp, c, block, i == 0 ? chordOn({ 60 }) : juce::MidiBuffer {}, out);
                    for (auto& x : collect(out))
                        if (x.on)
                            onsets.push_back(i * block + x.sample);
                }
                return onsets;
            };

            const auto fast = lateRun(8.0, 3); // a 6000-sample step, so half of one is 3000
            expectEquals((int) fast.size(), 3, "every step still fires exactly once");
            if (fast.size() == 3)
            {
                expectWithinAbsoluteError(fast[0], 0, 2);
                expectWithinAbsoluteError(fast[1], 9000, 2); // 6000 + half a 6000-sample step
                expectWithinAbsoluteError(fast[2], 12000, 2);
            }

            const auto slow = lateRun(4.0, 6); // a 12000-sample step, so half of one is 6000
            expectEquals((int) slow.size(), 3, "and at half the rate, still exactly once");
            if (slow.size() == 3)
            {
                expectWithinAbsoluteError(slow[0], 0, 2);
                expectWithinAbsoluteError(slow[1], 18000, 2); // twice the shift, for twice the step
                expectWithinAbsoluteError(slow[2], 24000, 2);
            }
        }

        beginTest("an out-of-range Hz is clamped; zero neither divides nor hangs");
        {
            // The step length is 1/hz, so an unclamped zero is an infinite step - and a NaN
            // boundary on the very next iteration of the step loop - while an unclamped
            // negative is a negative step length, which the scheduler reads as "never fire".
            // Both are one hand-edited session away.
            const auto countAt = [&](double hz, int blocks)
            {
                auto sp = hzp;
                sp.rateHz = hz;
                return (int) onsetsOf(sp, clock, block, blocks, steady).size();
            };
            // 300 blocks is 1.8 M samples and the slowest rate is one step per 1.536 M: two.
            expectEquals(countAt(ArpEngine::minRateHz, 300), 2, "the slowest rate still fires");
            expectEquals(countAt(0.0, 300), 2, "zero reads as the slowest rate, not as forever");
            expectEquals(countAt(-8.0, 300), 2, "and so does a negative");
            expectEquals(countAt(ArpEngine::maxRateHz, 1), 4, "32 Hz is four steps in a 6000-sample block");
            expectEquals(countAt(1.0e6, 1), 4, "and anything past the top reads as 32 Hz");
        }

        beginTest("Hz free-runs with the transport stopped, and with no tempo at all");
        {
            // The point of a rate in Hz: an arp that runs whether or not anything is playing.
            // Sync falls back to an internal clock when the transport stops; Hz was never on
            // the transport to begin with, so stopped and rolling have to be the same run.
            ArpEngine::HostClock stopped;
            stopped.playing = false;
            stopped.hasPpq = false;
            stopped.bpm = 0.0;
            const auto still = onsetsOf(hzp, stopped, block, 8, steady);
            const auto rolling = onsetsOf(hzp, clock, block, 8, steady);
            expectEquals((int) still.size(), 8, "one step per 6000-sample block, transport or not");
            expect(still == rolling, "stopped and rolling are the same clock in Hz mode");

            // And no tempo reaches it either way: 60 bpm is pinned, so neither the host's bpm
            // nor the internal clock's fallback can stretch the period.
            auto odd = hzp;
            odd.fallbackBpm = 200.0;
            auto slowHost = clock;
            slowHost.bpm = 45.0;
            expect(onsetsOf(odd, stopped, block, 8, steady) == still, "fallbackBpm is not read");
            expect(onsetsOf(odd, slowHost, block, 8, steady) == still, "and neither is the host's bpm");
        }

        // ---------------------------------------------------------------------------------
        // Tempo Sync (bpmSync, Job 1): `followHost` is the escape hatch from a rolling host's
        // tempo, not the thing that adds following - Sync already read the host whenever it
        // was playing with a valid bpm, and followHost defaults true to reproduce exactly
        // that. Off pins every line to fallbackBpm even while the host rolls.
        //
        // `anchored` is forced off in every case below. Anchored+playing+hasPpq reads
        // `clock.ppq` straight off the playhead for step *position* - "Sync mode does follow
        // the playhead" above already pins that - and `steady`'s ppq is bpm-independent, so
        // testing followHost through the anchored branch would prove nothing about which bpm
        // fed it. Free-running (anchored off) is the branch that actually turns bpm into a
        // step period (`blockBeats = bpm/60/sr*numSamples`), which is the resolution this
        // parameter changes.
        // ---------------------------------------------------------------------------------
        beginTest("followHost on: a rolling host with a valid bpm overrides fallbackBpm");
        {
            auto sp = p;
            sp.anchored = false;
            sp.followHost = true;
            sp.fallbackBpm = 90.0;
            auto hostRolling = clock;
            hostRolling.bpm = 150.0;
            hostRolling.hasBpm = true; // "a valid bpm" is exactly what this flag says
            const auto on = onsetsOf(sp, hostRolling, block, 4, steady);
            // 1/16 at 150 bpm: 0.25 beat * 60/150 s = 0.1 s = 4800 samples at 48 kHz, so five
            // onsets land inside 4 * 6000 = 24000 samples (0, 4800, ..., 19200).
            expectEquals((int) on.size(), 5);
            for (int i = 1; i < (int) on.size(); ++i)
                expectWithinAbsoluteError(on[(size_t) i] - on[(size_t) (i - 1)], 4800, 2,
                                          "sync on steps at the host's bpm, not fallbackBpm");
        }

        beginTest("followHost off: a rolling host is ignored in favour of fallbackBpm");
        {
            auto sp = p;
            sp.anchored = false;
            sp.followHost = false;
            sp.fallbackBpm = 90.0;
            auto hostRolling = clock;
            hostRolling.bpm = 150.0;
            hostRolling.hasBpm = true; // a real host tempo, so followHost is the only thing refusing it
            const auto on = onsetsOf(sp, hostRolling, block, 4, steady);
            // 1/16 at 90 bpm: 0.25 beat * 60/90 s = 1/6 s = 8000 samples, so three onsets land
            // inside 4 * 6000 = 24000 samples (0, 8000, 16000).
            expectEquals((int) on.size(), 3);
            for (int i = 1; i < (int) on.size(); ++i)
                expectWithinAbsoluteError(on[(size_t) i] - on[(size_t) (i - 1)], 8000, 2,
                                          "sync off steps at fallbackBpm even though the host is rolling");
        }

        beginTest("followHost on, transport stopped: the host's tempo still wins");
        {
            // **This asserted the opposite until 2026-08-16** - "a stopped transport reads
            // fallbackBpm whatever followHost says" - and Owen caught it from the outside:
            // "bpm isn't syncing with daw". A DAW's tempo is its tempo stopped or rolling;
            // Ableton reads 120 with everything parked. Gating the *tempo* on the transport
            // meant Keys disagreed with Live for exactly as long as you were setting up.
            // The *position* still needs a rolling transport (the anchored branch, pinned
            // above); only the tempo was over-gated.
            auto sp = p;
            sp.anchored = false;
            sp.followHost = true;
            sp.fallbackBpm = 100.0;
            ArpEngine::HostClock stoppedHost;
            stoppedHost.playing = false;
            stoppedHost.hasPpq = false;
            stoppedHost.bpm = 150.0;
            stoppedHost.hasBpm = true; // the host answered, it just is not rolling
            const auto on = onsetsOf(sp, stoppedHost, block, 4, steady);
            // 1/16 at the host's 150 bpm: 4800 samples, so five onsets inside 24000.
            expectEquals((int) on.size(), 5);
            for (int i = 1; i < (int) on.size(); ++i)
                expectWithinAbsoluteError(on[(size_t) i] - on[(size_t) (i - 1)], 4800, 2,
                                          "a stopped host that reports a tempo still sets the clock");
        }

        beginTest("followHost on, no host tempo at all: fallbackBpm runs the clock");
        {
            // The standalone, where there is no playhead to ask. This is what `hasBpm` exists
            // for and why it could not be a `bpm > 0` test: HostClock::bpm defaults to **120**,
            // not 0, so reading that default as an answer would pin the standalone at 120 and
            // make its own BPM control do nothing - silently, and only outside a DAW.
            auto sp = p;
            sp.anchored = false;
            sp.followHost = true;
            sp.fallbackBpm = 100.0;
            ArpEngine::HostClock noHost; // bpm left at its 120 default, hasBpm false
            noHost.playing = false;
            noHost.hasPpq = false;
            const auto on = onsetsOf(sp, noHost, block, 4, steady);
            expectEquals((int) on.size(), 4);
            // 1/16 at 100 bpm: 0.25 beat * 60/100 s = 0.15 s = 7200 samples - fallbackBpm, and
            // pointedly not the 120 sitting in HostClock's default.
            for (int i = 1; i < (int) on.size(); ++i)
                expectWithinAbsoluteError(on[(size_t) i] - on[(size_t) (i - 1)], 7200, 2,
                                          "with no host tempo the BPM control runs the clock");
        }

        beginTest("Hz mode is deaf to followHost, exactly as it is to fallbackBpm and the host bpm");
        {
            auto hostRolling = clock;
            hostRolling.bpm = 45.0;
            auto spOn = hzp;
            spOn.followHost = true;
            auto spOff = hzp;
            spOff.followHost = false;
            const auto onA = onsetsOf(spOn, hostRolling, block, 8, steady);
            const auto onB = onsetsOf(spOff, hostRolling, block, 8, steady);
            expect(onA == onB, "followHost changes nothing while the rate is in Hz");
            expectEquals((int) onA.size(), 8, "still one step per 6000-sample block at 8 Hz");
        }

        // Two anchored lines must walk the same grid even with no transport to follow
        // (2026-08-18, Owen: "I think that the notes should be playing at the same time, but
        // they're not"). The engine's own free-running phase is zeroed by restart(), so a line
        // switched on later used to start counting from its own zero and stay out of phase for
        // good; HostClock::hasGrid is the processor's answer to that, and this pins that a line
        // joining late lands on the shared grid rather than on a grid of its own.
        beginTest("with no transport, an anchored line joining late still lands on the shared grid");
        {
            ArpEngine::HostClock noTransport;   // stopped, no host position at all...
            noTransport.playing = false;
            noTransport.hasPpq = false;
            noTransport.hasGrid = true;         // ...but the processor always supplies a grid
            noTransport.bpm = 120.0;
            noTransport.hasBpm = true;

            auto ap = p;
            ap.anchored = true;

            ArpEngine early, late;
            early.prepare(sr);
            late.prepare(sr);

            // The grid is one running beat count both lines are handed in the same block, exactly
            // as runArpLines hands them `arpBeats`. Half a step of offset (0.125 beat) so a line
            // counting from its own zero would be visibly off the beat rather than accidentally
            // on it.
            std::vector<int> earlyOnsets, lateOnsets;
            for (int b = 0; b < 8; ++b)
            {
                noTransport.ppq = 0.125 + 0.25 * (double) b;

                juce::MidiBuffer outE;
                early.process(ap, noTransport, block, b == 0 ? chordOn({ 60, 64 }) : juce::MidiBuffer {}, outE);
                for (auto& x : collect(outE))
                    if (x.on)
                        earlyOnsets.push_back(b * block + x.sample);

                // The late line is handed its chord four blocks in - the switch going on, or a
                // card dropped onto it - and from then on must fire alongside the early one.
                juce::MidiBuffer outL;
                late.process(ap, noTransport, block, b == 4 ? chordOn({ 60, 64 }) : juce::MidiBuffer {}, outL);
                for (auto& x : collect(outL))
                    if (x.on)
                        lateOnsets.push_back(b * block + x.sample);
            }

            expect(! lateOnsets.empty(), "the late line plays at all");
            for (int onset : lateOnsets)
                expect(std::find(earlyOnsets.begin(), earlyOnsets.end(), onset) != earlyOnsets.end(),
                       "every onset of the late line coincides with one of the early line's");
        }

        beginTest("anchored off keeps a line's own phase, so it drifts on purpose");
        {
            ArpEngine::HostClock noTransport;
            noTransport.playing = false;
            noTransport.hasPpq = false;
            noTransport.hasGrid = true;
            noTransport.bpm = 120.0;
            noTransport.hasBpm = true;

            auto freeRun = p;
            freeRun.anchored = false;

            // Same shared grid, offset half a step off the beat. Free-running, the line counts
            // from its own start instead, so its onsets land on the block boundaries the grid's
            // own offset would have moved them off - which is the whole difference Anchor names.
            ArpEngine e;
            e.prepare(sr);
            std::vector<int> onsets;
            for (int b = 0; b < 4; ++b)
            {
                noTransport.ppq = 0.125 + 0.25 * (double) b;
                juce::MidiBuffer out;
                e.process(freeRun, noTransport, block, b == 0 ? chordOn({ 60, 64 }) : juce::MidiBuffer {}, out);
                for (auto& x : collect(out))
                    if (x.on)
                        onsets.push_back(b * block + x.sample);
            }
            expect(! onsets.empty(), "a free-running line still plays");
            expectEquals(onsets[0], 0, "and starts at its own zero, not at the grid's offset");
        }

        // Two lines meeting on one pitch (2026-08-18, Owen: "how does it handle when there's an
        // overlap in a note that's being played, and how should it handle that?"). ArpMerge is the
        // answer and lives beside the engine precisely so it can be driven like this, with two
        // lines' events hand-built into one buffer in sample order.
        beginTest("two lines sharing a pitch: one note-on, re-struck, released by the last line");
        {
            keys::ArpMerge merge;
            juce::MidiBuffer in, out;
            // Line A takes C4 at 0 and holds it past line B's whole note. B strikes the same
            // pitch at 100 and lets go at 200 - entirely inside A's note.
            in.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
            in.addEvent(juce::MidiMessage::noteOn(1, 60, 0.5f), 100);
            in.addEvent(juce::MidiMessage::noteOff(1, 60), 200);
            in.addEvent(juce::MidiMessage::noteOff(1, 60), 300);
            merge.merge(in, out);

            auto ev = collect(out);
            expectEquals((int) ev.size(), 4, "B's attack is heard, and only one note-off leaves");
            expect(ev[0].on && ev[0].sample == 0, "A's note-on goes out as it is");
            expect(! ev[1].on && ev[1].sample == 100, "B's attack closes the sounding note first");
            expect(ev[2].on && ev[2].sample == 100, "...and re-strikes it at the same offset");
            expect(! ev[3].on && ev[3].sample == 300,
                   "the pitch ends when the LAST line lets go, not the first");
        }

        beginTest("a pitch only one line is playing passes through untouched");
        {
            keys::ArpMerge merge;
            juce::MidiBuffer in, out;
            in.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
            in.addEvent(juce::MidiMessage::noteOff(1, 60), 100);
            in.addEvent(juce::MidiMessage::noteOn(1, 64, 0.8f), 100); // a different pitch, no overlap
            in.addEvent(juce::MidiMessage::noteOff(1, 64), 200);
            merge.merge(in, out);
            expectEquals((int) collect(out).size(), 4, "nothing added, nothing swallowed");
        }

        beginTest("the same pitch on two channels is two notes, and never collides");
        {
            keys::ArpMerge merge;
            juce::MidiBuffer in, out;
            // A line with a Channel of its own is the other answer to an overlap, and the two
            // must not interfere: these are different notes to the instrument downstream.
            in.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
            in.addEvent(juce::MidiMessage::noteOn(2, 60, 0.8f), 0);
            in.addEvent(juce::MidiMessage::noteOff(1, 60), 100);
            in.addEvent(juce::MidiMessage::noteOff(2, 60), 200);
            merge.merge(in, out);
            auto ev = collect(out);
            expectEquals((int) ev.size(), 4, "no re-strike and no suppression across channels");
            expect(ev[0].on && ev[1].on, "both attacks stand");
            expect(! ev[2].on && ! ev[3].on, "and both releases do");
        }

        beginTest("a panic clears the counts, so the next note-off is not swallowed");
        {
            keys::ArpMerge merge;
            juce::MidiBuffer stranded, out;
            stranded.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
            stranded.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 10); // two lines holding it
            merge.merge(stranded, out);

            // A panic silences the instrument directly and leaves the engines to catch up. Without
            // the reset the count stays at 2, and the single note-off that eventually arrives is
            // swallowed as "another line still holds it" - a stuck note.
            merge.reset();
            juce::MidiBuffer after, out2;
            after.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
            after.addEvent(juce::MidiMessage::noteOff(1, 60), 100);
            merge.merge(after, out2);
            auto ev = collect(out2);
            expectEquals((int) ev.size(), 2, "a clean note pair after the panic");
            expect(ev[0].on && ! ev[1].on, "and the note-off reaches the instrument");
        }

        // --- Three lines ------------------------------------------------------------------
        // The engine has never known how many of it there are, which is why three of them cost
        // nothing to build. What these check is that being three of them changes none of it:
        // independent clocks, independent held sets, independent lanes.
        beginTest("three engines at three rates hold a polyrhythm");
        {
            // Two bars, one 6000-sample block at a time. 1/4 against 1/8 against a 1/8 triplet
            // is 8 against 16 against 24 hits - three against two against six per beat.
            ArpEngine a, b, c;
            a.prepare(sr);
            b.prepare(sr);
            c.prepare(sr);

            auto pa = p;
            pa.rateIndex = 6; // 1/4
            auto pb = p;
            pb.rateIndex = 7; // 1/8
            auto pc = p;
            pc.rateIndex = 7;
            pc.tuplet = 3; // 1/8 triplet

            int onsA = 0, onsB = 0, onsC = 0;
            auto held = chordOn({ 60, 64, 67 });
            for (int i = 0; i < 8; ++i) // 8 blocks of 6000 = 2 bars at 120 bpm
            {
                clock.ppq = 0.25 * i;
                const juce::MidiBuffer in = i == 0 ? held : juce::MidiBuffer {};
                juce::MidiBuffer oa, ob, oc;
                a.process(pa, clock, block, in, oa);
                b.process(pb, clock, block, in, ob);
                c.process(pc, clock, block, in, oc);
                for (const auto& e : collect(oa)) onsA += e.on ? 1 : 0;
                for (const auto& e : collect(ob)) onsB += e.on ? 1 : 0;
                for (const auto& e : collect(oc)) onsC += e.on ? 1 : 0;
            }
            expectEquals(onsA, 2, "1/4 fires twice a bar");
            expectEquals(onsB, 4, "1/8 fires four times a bar");
            expectEquals(onsC, 6, "and a 1/8 triplet six times - three against two");
        }

        beginTest("a note handed to one line never reaches another");
        {
            // The routing rule, stated at the level the engine can see it: an engine's held set
            // is built from the buffer it was given and nothing else. The processor's job is to
            // give each line its own buffer (see KeysProcessor::runArpLines); this is the half
            // that says a shared engine would have been wrong.
            ArpEngine a, b;
            a.prepare(sr);
            b.prepare(sr);
            juce::MidiBuffer oa, ob;
            clock.ppq = 0.0;
            a.process(p, clock, block, chordOn({ 60, 64, 67 }), oa);
            b.process(p, clock, block, {}, ob);
            expectEquals(a.heldNoteCount(), 3, "the line that was handed the chord holds it");
            expectEquals(b.heldNoteCount(), 0, "the one that was not, holds nothing");
            expect(collect(ob).empty(), "and plays nothing");

            // Releasing on one line leaves the other's held set alone, which is the failure a
            // pitch-ownership mask would have produced: one note-off, two engines, both let go.
            juce::MidiBuffer offB;
            offB.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            juce::MidiBuffer oa2, ob2;
            clock.ppq = 0.25;
            b.process(p, clock, block, offB, ob2);
            a.process(p, clock, block, {}, oa2);
            expectEquals(a.heldNoteCount(), 3, "a note-off on B does not unhold A");
        }

        beginTest("each line's lanes are its own");
        {
            // Lanes live on the engine, so three engines is three patterns with no extra work.
            // Worth a check all the same: the panel writes lanes through whichever line its
            // tabs have selected, and one shared Lanes would have made every tab draw the same.
            ArpEngine a, b;
            a.prepare(sr);
            b.prepare(sr);
            a.lanes.value[ArpEngine::laneNote][0].store(-1); // A mutes its first step
            b.lanes.value[ArpEngine::laneNote][0].store(0);

            auto pat = p;
            pat.usePattern = true;
            juce::MidiBuffer oa, ob;
            clock.ppq = 0.0;
            a.process(pat, clock, block, chordOn({ 60 }), oa);
            b.process(pat, clock, block, chordOn({ 60 }), ob);
            expect(collect(oa).empty(), "A's first step is muted");
            expect(! collect(ob).empty(), "B's is not");
        }

        // --- EuclidGen.h: pure, header-only, no engine involved -------------------------

        beginTest("euclid: E(3,8) is the tresillo x..x..x.");
        {
            const bool expected[8] = { true, false, false, true, false, false, true, false };
            for (int i = 0; i < 8; ++i)
                expectEquals((int) keys::euclidHit(i, 3, 8, 0), (int) expected[i],
                             "step " + juce::String(i));
        }

        beginTest("euclid: E(5,8)");
        {
            const bool expected[8] = { true, false, true, false, true, true, false, true };
            for (int i = 0; i < 8; ++i)
                expectEquals((int) keys::euclidHit(i, 5, 8, 0), (int) expected[i],
                             "step " + juce::String(i));
        }

        beginTest("euclid: E(4,16) is evenly spaced every 4 steps");
        {
            for (int i = 0; i < 16; ++i)
                expectEquals((int) keys::euclidHit(i, 4, 16, 0), (int) (i % 4 == 0 ? 1 : 0),
                             "step " + juce::String(i));
        }

        beginTest("euclid: rotation wraps, including negative rotation");
        {
            // A rotated hit at i must equal the unrotated hit at (i + rotation) mod steps -
            // checked against a plain modulo computed in the test, independent of the
            // implementation's own wrap.
            for (int rot = -20; rot <= 20; ++rot)
                for (int i = 0; i < 8; ++i)
                {
                    const int j = ((i + rot) % 8 + 8) % 8;
                    expectEquals((int) keys::euclidHit(i, 3, 8, rot), (int) keys::euclidHit(j, 3, 8, 0),
                                 "rotation " + juce::String(rot) + " at step " + juce::String(i));
                }
        }

        beginTest("euclid: degenerate hits=0 and hits=steps (and hits>steps clamps)");
        {
            for (int i = 0; i < 8; ++i)
            {
                expect(! keys::euclidHit(i, 0, 8, 0), "hits=0 is silence");
                expect(keys::euclidHit(i, 8, 8, 0), "hits=steps is every step");
                expect(keys::euclidHit(i, 99, 8, 0), "hits>steps clamps to steps");
            }
        }

        // --- Rhythm dividers (ArpEngine::dividerFires / firedCountBefore, and process()) -

        beginTest("firedCountBefore matches a brute-force count, for several divisor sets");
        {
            auto bruteForce = [](long long g, const std::array<int, 4>& divs) -> long long
            {
                long long count = 0;
                for (long long k = 0; k < g; ++k)
                    if (ArpEngine::dividerFires(k, divs))
                        ++count;
                return count;
            };
            const std::array<std::array<int, 4>, 5> sets { {
                { 3, 0, 0, 0 },
                { 3, 4, 0, 0 },
                { 2, 3, 5, 0 },
                { 4, 6, 0, 0 },
                { 3, 5, 7, 11 },
            } };
            for (const auto& divs : sets)
                for (const long long g : { 0LL, 1LL, 5LL, 16LL, 37LL, 100LL })
                    expectEquals(ArpEngine::firedCountBefore(g, divs), bruteForce(g, divs),
                                 "divs mismatch at g=" + juce::String(g));
        }

        // --- Drift (2026-08-14) ---------------------------------------------------------

        beginTest("drift 0: bit-identical to the feature never existing");
        {
            ArpEngine e1, e2;
            e1.prepare(sr);
            e2.prepare(sr);
            auto p1 = p, p2 = p;
            p1.usePattern = p2.usePattern = true;
            p2.drift = 0; // explicit, rather than relying on the default
            for (int i = 0; i < 6; ++i)
            {
                juce::MidiBuffer in = (i == 0) ? chordOn({ 60, 64, 67 }) : juce::MidiBuffer {};
                juce::MidiBuffer o1, o2;
                clock.ppq = 0.25 * i;
                e1.process(p1, clock, block, in, o1);
                e2.process(p2, clock, block, in, o2);
                auto a = collect(o1), b = collect(o2);
                expectEquals((int) a.size(), (int) b.size(), "same count, block " + juce::String(i));
                for (size_t k = 0; k < a.size() && k < b.size(); ++k)
                {
                    expectEquals(a[k].note, b[k].note, "same note");
                    expectEquals(a[k].sample, b[k].sample, "same offset");
                }
            }
        }

        beginTest("drift never moves a lane it is not allowed to");
        {
            // The rule the feature rests on: drift changes how a step plays, never which note
            // it plays. Note, Ratchet, Harmony and Chord must be untouched at any amount.
            expect(! ArpEngine::laneDrifts[ArpEngine::laneNote], "Note does not drift");
            expect(! ArpEngine::laneDrifts[ArpEngine::laneRatchet], "Ratchet does not drift");
            expect(! ArpEngine::laneDrifts[ArpEngine::laneHarmony], "Harmony does not drift");
            expect(! ArpEngine::laneDrifts[ArpEngine::laneChord], "Chord does not drift");
            expect(ArpEngine::laneDrifts[ArpEngine::laneVelocity], "Velocity drifts");
            expect(ArpEngine::laneDrifts[ArpEngine::laneGate], "Gate drifts");
        }

        beginTest("drift at full: pitches are unchanged, velocities are not");
        {
            // Same held chord, same pattern, drift hard over. The *notes* must come back in the
            // same order at the same times - only how they are played may wander.
            ArpEngine e1, e2;
            e1.prepare(sr);
            e2.prepare(sr);
            auto p1 = p, p2 = p;
            p1.usePattern = p2.usePattern = true;
            p2.drift = 100;
            std::vector<int> n1, n2;
            bool anyVelDiff = false;
            for (int i = 0; i < 12; ++i)
            {
                juce::MidiBuffer in = (i == 0) ? chordOn({ 60, 64, 67 }) : juce::MidiBuffer {};
                juce::MidiBuffer o1, o2;
                clock.ppq = 0.25 * i;
                e1.process(p1, clock, block, in, o1);
                e2.process(p2, clock, block, in, o2);
                for (const auto& ev : collect(o1))
                    if (ev.on)
                        n1.push_back(ev.note);
                for (const auto& ev : collect(o2))
                    if (ev.on)
                        n2.push_back(ev.note);
            }
            juce::ignoreUnused(anyVelDiff);
            // **Set equality, not positional.** Chance and Late are drifting lanes, so a
            // drifted run legitimately drops steps and slides others across the edge of the
            // twelve blocks this samples - the counts are allowed to differ and did the moment
            // strayWithin stopped half of Late's draw clamping back to zero. What may *not*
            // happen is a pitch class the undrifted run never plays: that is "drift changes how
            // a step plays, never which note it plays" stated exactly, and octave drift is
            // invisible to it by design, since whole octaves keep the class.
            expect(! n2.empty(), "the drifted run still plays");
            std::set<int> classes;
            for (const int n : n1)
                classes.insert(n % 12);
            for (const int n : n2)
                expect(classes.count(n % 12) > 0,
                       "drift invented pitch class " + juce::String(n % 12));
        }

        beginTest("strayWithin: a value at the edge of its lane still moves (Owen's 0 bug)");
        {
            // The bug: Roll and Drift built [v - reach/2, v + reach/2] and clamped the result,
            // so a value sitting at the bottom of its lane had half of every draw fall outside
            // and clamp straight back to itself. Late, Harmony and Chord all default to 0 and
            // Chance sits at its top, so "a lane of zeroes barely moves" was the common case.
            const ArpEngine::LaneRange late { 0, 90 };
            int moved = 0;
            for (int i = 0; i < 100; ++i)
                if (ArpEngine::strayWithin(0, 90 * 0.35, late, i / 100.0) != 0)
                    ++moved;
            // The window is [0, 31.5], so only draws rounding to 0 stay - a handful, not half.
            expect(moved > 90, "a value at the floor moves nearly always, not half the time");

            int movedTop = 0;
            for (int i = 0; i < 100; ++i)
                if (ArpEngine::strayWithin(100, 100 * 0.35, { 0, 100 }, i / 100.0) != 100)
                    ++movedTop;
            expect(movedTop > 90, "a value at the ceiling moves too");

            // ...and it never leaves the lane, wherever it starts and however wide the reach.
            for (int v = -1; v <= 8; ++v)
                for (int i = 0; i < 50; ++i)
                {
                    const int r = ArpEngine::strayWithin(v, 9 * 2.0, { -1, 8 }, i / 50.0);
                    expect(r >= -1 && r <= 8, "stays inside the lane even past full reach");
                }
        }

        beginTest("mute is its own lane and does not touch what the step holds");
        {
            ArpEngine e;
            e.prepare(sr);
            auto dp = p;
            dp.usePattern = true;
            e.lanes.value[ArpEngine::laneNote][2].store(5); // a value worth keeping
            e.lanes.value[ArpEngine::laneMute][2].store(1); // ...and mute that step
            e.lanes.length[ArpEngine::laneNote].store(4);
            e.lanes.length[ArpEngine::laneMute].store(4);

            std::vector<int> fired;
            for (int g = 0; g < 8; ++g)
            {
                juce::MidiBuffer in = (g == 0) ? chordOn({ 60, 62, 64, 65, 67, 69, 71, 72 })
                                               : juce::MidiBuffer {};
                juce::MidiBuffer out;
                clock.ppq = 0.25 * g;
                e.process(dp, clock, block, in, out);
                for (const auto& ev : collect(out))
                    if (ev.on)
                        fired.push_back(g);
            }
            for (const int g : fired)
                expect(g % 4 != 2, "step 2 of every pass is silent");
            // The value survived the mute, which is the whole reason mute left the Note lane.
            expectEquals(e.lanes.value[ArpEngine::laneNote][2].load(), 5, "the step kept its 5");
        }

        beginTest("rand strays the note selection, and only within the lane");
        {
            // Note lane fixed at 1 everywhere, Rand +3 on every step: the played entry must
            // land in 1..4 and nowhere else. An eight-note chord, so every entry is reachable.
            ArpEngine e;
            e.prepare(sr);
            auto dp = p;
            dp.usePattern = true;
            dp.direction = ArpEngine::Direction::up;
            for (int s = 0; s < 8; ++s)
            {
                e.lanes.value[ArpEngine::laneNote][(size_t) s].store(1);
                e.lanes.value[ArpEngine::laneRand][(size_t) s].store(3);
            }
            e.lanes.length[ArpEngine::laneNote].store(8);
            e.lanes.length[ArpEngine::laneRand].store(8);

            std::set<int> heard;
            for (int g = 0; g < 40; ++g)
            {
                juce::MidiBuffer in = (g == 0) ? chordOn({ 60, 62, 64, 65, 67, 69, 71, 72 })
                                               : juce::MidiBuffer {};
                juce::MidiBuffer out;
                clock.ppq = 0.25 * g;
                e.process(dp, clock, block, in, out);
                for (const auto& ev : collect(out))
                    if (ev.on)
                        heard.insert(ev.note);
            }
            // Entries 1..4 of the chord as played: 60, 62, 64, 65.
            for (const int n : heard)
                expect(n == 60 || n == 62 || n == 64 || n == 65,
                       "rand +3 on a fixed 1 stayed inside entries 1..4, heard " + juce::String(n));
            expect(heard.size() > 1, "...and it did actually stray");
        }

        beginTest("rand 0 leaves a fixed note lane exactly as drawn");
        {
            ArpEngine e;
            e.prepare(sr);
            auto dp = p;
            dp.usePattern = true;
            for (int s = 0; s < 8; ++s)
                e.lanes.value[ArpEngine::laneNote][(size_t) s].store(2);
            e.lanes.length[ArpEngine::laneNote].store(8);

            for (int g = 0; g < 12; ++g)
            {
                juce::MidiBuffer in = (g == 0) ? chordOn({ 60, 64, 67 }) : juce::MidiBuffer {};
                juce::MidiBuffer out;
                clock.ppq = 0.25 * g;
                e.process(dp, clock, block, in, out);
                for (const auto& ev : collect(out))
                    if (ev.on)
                        expectEquals(ev.note, 64, "entry 2 every time, with Rand at its default");
            }
        }

        beginTest("note lane: Hi and Low ask the chord, not the index");
        {
            // The point of the modes over the fixed indices: they keep meaning the same thing
            // when the chord under them changes. Hi must be the top note of whatever is held.
            for (const bool wide : { false, true })
            {
                ArpEngine e;
                e.prepare(sr);
                auto dp = p;
                dp.usePattern = true;
                for (int s = 0; s < 4; ++s)
                    e.lanes.value[ArpEngine::laneNote][(size_t) s].store(ArpEngine::noteHi);
                e.lanes.length[ArpEngine::laneNote].store(4);

                const int top = wide ? 79 : 67;
                for (int g = 0; g < 6; ++g)
                {
                    juce::MidiBuffer in = (g != 0) ? juce::MidiBuffer {}
                                        : (wide ? chordOn({ 60, 64, 79 }) : chordOn({ 60, 64, 67 }));
                    juce::MidiBuffer out;
                    clock.ppq = 0.25 * g;
                    e.process(dp, clock, block, in, out);
                    for (const auto& ev : collect(out))
                        if (ev.on)
                            expectEquals(ev.note, top, "Hi is the top of the chord actually held");
                }
            }
        }

        beginTest("note lane: Prev repeats what last sounded");
        {
            ArpEngine e;
            e.prepare(sr);
            auto dp = p;
            dp.usePattern = true;
            // Step 0 fixed on entry 2, steps 1-3 Prev: all four must play the same note.
            e.lanes.value[ArpEngine::laneNote][0].store(2);
            for (int s = 1; s < 4; ++s)
                e.lanes.value[ArpEngine::laneNote][(size_t) s].store(ArpEngine::notePrev);
            e.lanes.length[ArpEngine::laneNote].store(4);

            for (int g = 0; g < 8; ++g)
            {
                juce::MidiBuffer in = (g == 0) ? chordOn({ 60, 64, 67 }) : juce::MidiBuffer {};
                juce::MidiBuffer out;
                clock.ppq = 0.25 * g;
                e.process(dp, clock, block, in, out);
                for (const auto& ev : collect(out))
                    if (ev.on)
                        expectEquals(ev.note, 64, "entry 2, held by Prev");
            }
        }

        beginTest("chain: 'only after a step that fired' is silent behind a rest");
        {
            ArpEngine e;
            e.prepare(sr);
            auto dp = p;
            dp.usePattern = true;
            // Step 0 rests; step 1 asks to play only if the step before it sounded. It cannot.
            e.lanes.value[ArpEngine::laneNote][0].store(ArpEngine::noteRest);
            e.lanes.value[ArpEngine::laneChain][1].store(1);
            e.lanes.length[ArpEngine::laneNote].store(2);
            e.lanes.length[ArpEngine::laneChain].store(2);

            int heard = 0;
            for (int g = 0; g < 8; ++g)
            {
                juce::MidiBuffer in = (g == 0) ? chordOn({ 60, 64, 67 }) : juce::MidiBuffer {};
                juce::MidiBuffer out;
                clock.ppq = 0.25 * g;
                e.process(dp, clock, block, in, out);
                for (const auto& ev : collect(out))
                    if (ev.on)
                        ++heard;
            }
            expectEquals(heard, 0, "a rest, then a step conditional on it, is silence");
        }

        beginTest("chain: 'only after a step that did not fire' fills the gaps");
        {
            ArpEngine e;
            e.prepare(sr);
            auto dp = p;
            dp.usePattern = true;
            e.lanes.value[ArpEngine::laneNote][0].store(ArpEngine::noteRest);
            e.lanes.value[ArpEngine::laneChain][1].store(2); // only if the one before did NOT
            e.lanes.length[ArpEngine::laneNote].store(2);
            e.lanes.length[ArpEngine::laneChain].store(2);

            int heard = 0;
            for (int g = 0; g < 8; ++g)
            {
                juce::MidiBuffer in = (g == 0) ? chordOn({ 60, 64, 67 }) : juce::MidiBuffer {};
                juce::MidiBuffer out;
                clock.ppq = 0.25 * g;
                e.process(dp, clock, block, in, out);
                for (const auto& ev : collect(out))
                    if (ev.on)
                        ++heard;
            }
            expect(heard > 0, "the inverse condition sounds where the plain one does not");
        }

        beginTest("chain 0 everywhere is bit-identical to the feature never existing");
        {
            ArpEngine e1, e2;
            e1.prepare(sr);
            e2.prepare(sr);
            auto dp = p;
            dp.usePattern = true;
            for (int s = 0; s < ArpEngine::maxSteps; ++s)
                e2.lanes.value[ArpEngine::laneChain][(size_t) s].store(0); // explicit
            for (int i = 0; i < 8; ++i)
            {
                juce::MidiBuffer in = (i == 0) ? chordOn({ 60, 64, 67 }) : juce::MidiBuffer {};
                juce::MidiBuffer o1, o2;
                clock.ppq = 0.25 * i;
                e1.process(dp, clock, block, in, o1);
                e2.process(dp, clock, block, in, o2);
                auto a = collect(o1), b = collect(o2);
                expectEquals((int) a.size(), (int) b.size(), "same count, block " + juce::String(i));
                for (size_t k = 0; k < a.size() && k < b.size(); ++k)
                    expectEquals(a[k].note, b[k].note, "same note");
            }
        }

        beginTest("laneRange covers every lane and none of them is empty");
        {
            for (int l = 0; l < ArpEngine::numLanes; ++l)
            {
                const auto r = ArpEngine::laneRange(l);
                expect(r.hi > r.lo, "lane " + juce::String(l) + " has a usable range");
                expect(ArpEngine::laneDefaults[l] >= r.lo && ArpEngine::laneDefaults[l] <= r.hi,
                       "lane " + juce::String(l) + "'s default is inside its own range");
            }
        }

        beginTest("rhythm dividers all zero: bit-identical to the feature never existing");
        {
            ArpEngine e1, e2;
            e1.prepare(sr);
            e2.prepare(sr);
            e2.rhythmDiv[0].store(0);
            e2.rhythmDiv[1].store(0);
            e2.rhythmDiv[2].store(0);
            e2.rhythmDiv[3].store(0); // explicit zero, rather than relying on the default
            for (int i = 0; i < 4; ++i)
            {
                juce::MidiBuffer in = (i == 0) ? chordOn({ 60, 64, 67 }) : juce::MidiBuffer {};
                juce::MidiBuffer o1, o2;
                clock.ppq = 0.25 * i;
                e1.process(p, clock, block, in, o1);
                e2.process(p, clock, block, in, o2);
                auto ev1 = collect(o1);
                auto ev2 = collect(o2);
                expectEquals((int) ev1.size(), (int) ev2.size(), "same event count block " + juce::String(i));
                for (size_t k = 0; k < ev1.size() && k < ev2.size(); ++k)
                {
                    expectEquals(ev1[k].note, ev2[k].note, "same note");
                    expectEquals(ev1[k].sample, ev2[k].sample, "same sample offset");
                    expect(ev1[k].on == ev2[k].on, "same on/off");
                }
            }
        }

        beginTest("rhythm dividers {3,4}: fires only on multiples, lanes advance one step per fire");
        {
            // Eight-note chord, note lane holding a distinct fixed index per step (1..8), so
            // the *pitch* that comes out names which lane slot was read. If lane reads used
            // the raw (uncompressed) step index, the sequence below would repeat and jump
            // around instead of climbing 60..67 in order.
            ArpEngine e;
            e.prepare(sr);
            e.rhythmDiv[0].store(3);
            e.rhythmDiv[1].store(4);
            auto dp = p;
            dp.usePattern = true;
            for (int s = 0; s < 8; ++s)
                e.lanes.value[ArpEngine::laneNote][(size_t) s].store(s + 1); // fixed index 1..8
            e.lanes.length[ArpEngine::laneNote].store(8);

            std::vector<int> firedNotes;
            for (int g = 0; g < 16; ++g)
            {
                juce::MidiBuffer in = (g == 0) ? chordOn({ 60, 61, 62, 63, 64, 65, 66, 67 }) : juce::MidiBuffer {};
                juce::MidiBuffer out;
                clock.ppq = 0.25 * g;
                e.process(dp, clock, block, in, out);
                for (auto& ev : collect(out))
                    if (ev.on)
                        firedNotes.push_back(ev.note);
            }
            // Multiples of 3 or 4 in [0, 16): 0, 3, 4, 6, 8, 9, 12, 15 - eight boundaries.
            expectEquals((int) firedNotes.size(), 8, "exactly the multiples of 3 or 4 fire");
            for (size_t i = 0; i < firedNotes.size(); ++i)
                expectEquals(firedNotes[i], 60 + (int) i,
                             "fire " + juce::String((int) i) + " reads the lane at its compressed index");
        }

        beginTest("rhythm dividers: statelessness across a transport jump");
        {
            // A fresh engine started deep in the timeline must read the same relative pattern
            // as one started at zero - firedCountBefore is a function of g alone, so a jump
            // self-corrects instead of needing every step since 0 to have been walked.
            auto runFromZero = [&](double startPpq) -> std::vector<int>
            {
                ArpEngine e;
                e.prepare(sr);
                e.rhythmDiv[0].store(3);
                e.rhythmDiv[1].store(4);
                auto dp = p;
                dp.usePattern = true;
                for (int s = 0; s < 8; ++s)
                    e.lanes.value[ArpEngine::laneNote][(size_t) s].store(s + 1);
                e.lanes.length[ArpEngine::laneNote].store(8);

                std::vector<int> firedNotes;
                for (int g = 0; g < 24 && (int) firedNotes.size() < 8; ++g)
                {
                    juce::MidiBuffer in = (g == 0) ? chordOn({ 60, 61, 62, 63, 64, 65, 66, 67 })
                                                    : juce::MidiBuffer {};
                    juce::MidiBuffer out;
                    clock.ppq = startPpq + 0.25 * g;
                    e.process(dp, clock, block, in, out);
                    for (auto& ev : collect(out))
                        if (ev.on)
                            firedNotes.push_back(ev.note);
                }
                return firedNotes;
            };

            const auto fromZero = runFromZero(0.0);
            const auto fromDeep = runFromZero(400.0); // beat 400 = raw step 1600
            expectEquals((int) fromZero.size(), 8, "collected 8 fires from zero");
            expectEquals((int) fromDeep.size(), 8, "collected 8 fires from deep in the timeline");
            for (int i = 0; i < 8 && i < (int) fromZero.size() && i < (int) fromDeep.size(); ++i)
                expectEquals(fromZero[(size_t) i], fromDeep[(size_t) i],
                             "fire " + juce::String(i) + " matches regardless of start position");
        }

        // --- Subharmonic harmony mode -----------------------------------------------------

        beginTest("harmonyMode 0 (default) is unchanged: a chord tone above");
        {
            ArpEngine e;
            e.prepare(sr);
            auto hp = p;
            hp.usePattern = true;
            e.lanes.value[ArpEngine::laneHarmony][0].store(1); // one chord tone above
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(hp, clock, block, chordOn({ 60, 64 }), out);
            int sawMain = 0, sawHarmony = 0;
            for (auto& ev : collect(out))
                if (ev.on)
                {
                    if (ev.note == 60) ++sawMain;
                    if (ev.note == 64) ++sawHarmony;
                }
            expectEquals(sawMain, 1, "the played note fires");
            expectEquals(sawHarmony, 1, "a chord tone above it fires too");
        }

        beginTest("harmonyMode 1: h=0 emits nothing extra");
        {
            ArpEngine e;
            e.prepare(sr);
            auto hp = p;
            hp.usePattern = true;
            e.harmonyMode.store(1);
            e.lanes.value[ArpEngine::laneHarmony][0].store(0); // off
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(hp, clock, block, chordOn({ 60 }), out);
            int ons = 0;
            for (auto& ev : collect(out))
                if (ev.on) ++ons;
            expectEquals(ons, 1, "only the main voice");
        }

        beginTest("harmonyMode 1: h=3 adds a voice at -24 semitones (the undertone f/4)");
        {
            ArpEngine e;
            e.prepare(sr);
            auto hp = p;
            hp.usePattern = true;
            e.harmonyMode.store(1);
            e.lanes.value[ArpEngine::laneHarmony][0].store(3);
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(hp, clock, block, chordOn({ 72 }), out);
            int sawMain = 0, sawSub = 0;
            for (auto& ev : collect(out))
                if (ev.on)
                {
                    if (ev.note == 72) ++sawMain;
                    if (ev.note == 48) ++sawSub; // 72 - 24
                }
            expectEquals(sawMain, 1, "the played note fires");
            expectEquals(sawSub, 1, "the subharmonic voice fires at -24");
        }

        beginTest("harmonyMode 1: a voice that clamps onto the played note is dropped, not wrapped");
        {
            ArpEngine e;
            e.prepare(sr);
            auto hp = p;
            hp.usePattern = true;
            e.harmonyMode.store(1);
            e.lanes.value[ArpEngine::laneHarmony][0].store(7); // -36 semitones
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(hp, clock, block, chordOn({ 0 }), out); // already at the MIDI floor
            int ons = 0;
            for (auto& ev : collect(out))
                if (ev.on)
                {
                    ++ons;
                    expectEquals(ev.note, 0, "no note wraps to a high pitch");
                }
            expectEquals(ons, 1, "the collapsed voice is dropped, leaving only the main note");
        }

        // --- Lane on/off, loop window and direction (2026-08-18) --------------------------

        beginTest("a lane switched off reads its default and keeps its drawing");
        {
            auto lp = p;
            lp.usePattern = true;

            ArpEngine e;
            e.prepare(sr);
            for (int st = 0; st < 8; ++st)
                e.lanes.value[ArpEngine::laneOctave][(size_t) st].store(2); // two octaves up
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(lp, clock, block, chordOn({ 60 }), out);
            auto lifted = collect(out);
            expect(! lifted.empty() && lifted[0].note == 60 + 24, "the lane is being read");

            ArpEngine e2;
            e2.prepare(sr);
            for (int st = 0; st < 8; ++st)
                e2.lanes.value[ArpEngine::laneOctave][(size_t) st].store(2);
            e2.lanes.on[ArpEngine::laneOctave].store(0);
            juce::MidiBuffer out2;
            clock.ppq = 0.0;
            e2.process(lp, clock, block, chordOn({ 60 }), out2);
            auto off = collect(out2);
            expect(! off.empty() && off[0].note == 60, "switched off, the lane reads its default");
            expectEquals(e2.lanes.value[ArpEngine::laneOctave][0].load(), 2,
                         "and the drawing is still there - off is not Reset");
        }

        beginTest("laneStepIndex: the loop window, and all four directions");
        {
            ArpEngine::LaneShape sh;
            sh.len = 8;
            sh.loopFrom = 2;
            sh.loopTo = 5; // a four-step window

            const auto walk = [&sh](int dir, int n)
            {
                sh.dir = dir;
                juce::String out;
                for (int i = 0; i < n; ++i)
                    out += juce::String(ArpEngine::laneStepIndex(i, 0, sh));
                return out;
            };
            expectEquals(walk(ArpEngine::dirUp, 8), juce::String("23452345"),
                         "Up wraps inside the window");
            expectEquals(walk(ArpEngine::dirDown, 8), juce::String("54325432"),
                         "Down walks it backwards");
            // 2*span-2 = 6, and neither end is repeated: 2 3 4 5 4 3, then round again.
            expectEquals(walk(ArpEngine::dirUpAlt, 12), juce::String("234543234543"),
                         "Up alt bounces without doubling its turning points");
            expectEquals(walk(ArpEngine::dirDownAlt, 12), juce::String("543234543234"),
                         "Down alt is its mirror");

            sh.dir = ArpEngine::dirUp;
            sh.loopFrom = 3;
            sh.loopTo = 3;
            expectEquals(ArpEngine::laneStepIndex(7, 0, sh), 3, "a one-step window holds that step");
            sh.loopFrom = 0;
            sh.loopTo = ArpEngine::maxSteps - 1;
            expectEquals(ArpEngine::laneStepIndex(9, 0, sh), 1,
                         "the default window is the whole lane, exactly as before it existed");
        }

        // --- Mutate (2026-08-18) ---------------------------------------------------------

        beginTest("Mutate at 0 changes nothing at all");
        {
            ArpEngine e1, e2;
            e1.prepare(sr);
            e2.prepare(sr);
            auto mp = p;
            juce::MidiBuffer o1, o2;
            clock.ppq = 0.0;
            e1.process(mp, clock, block * 4, chordOn({ 60, 64, 67 }), o1);
            mp.mutate = 0;
            mp.mutateLock = 50;
            clock.ppq = 0.0;
            e2.process(mp, clock, block * 4, chordOn({ 60, 64, 67 }), o2);
            const auto a = collect(o1), b = collect(o2);
            expectEquals((int) a.size(), (int) b.size(), "same number of events");
            for (size_t i = 0; i < a.size() && i < b.size(); ++i)
                expectEquals(b[i].note, a[i].note, "same notes");
        }

        beginTest("Mutate never leaves the held chord, at any setting");
        {
            // **The whole knob, since 2026-08-21** (Owen: "it's adding additional notes in the
            // arpeggiator ... it should just change the existing ones"). This swept to 50 only
            // while the dial's upper half carried the out-of-chord stage; that stage is Stray's
            // now, so the promise holds all the way to 100 and this test is what says so.
            // Swept rather than sampled at the ends, because a reach that grows with the amount
            // is exactly the sort of thing that falls off the end at one value and nowhere else.
            for (int amt = 10; amt <= 100; amt += 10)
            {
                ArpEngine e;
                e.prepare(sr);
                auto mp = p;
                mp.mutate = amt;
                juce::MidiBuffer out;
                clock.ppq = 0.0;
                e.process(mp, clock, block * 16, chordOn({ 60, 64, 67 }), out);
                for (auto& ev : collect(out))
                    if (ev.on)
                        expect(ev.note == 60 || ev.note == 64 || ev.note == 67,
                               "Mutate " + juce::String(amt) + " played " + juce::String(ev.note)
                                   + ", which is not in the held chord");
            }
        }

        beginTest("Mutate's reach grows with the knob rather than only past halfway");
        {
            // What the top of the travel buys now that it is not spending itself on strays.
            // Measured as *distinct* pitches reached over a fixed run from a wide chord: a
            // reach of one entry can only ever touch the neighbours of what the walk chose, so
            // a wider reach shows up as more of the chord being visited. Four octaves of triad,
            // because over a plain three-note chord every reach past one wraps onto the same
            // three notes and there is nothing left to measure.
            const auto spread = [&](int amt)
            {
                ArpEngine e;
                e.prepare(sr);
                auto mp = p;
                mp.mutate = amt;
                mp.usePattern = true; // a fixed base index, so the spread is Mutate's, not the walk's
                for (int st = 0; st < 8; ++st)
                    e.lanes.value[ArpEngine::laneNote][(size_t) st].store(6);
                juce::MidiBuffer out;
                clock.ppq = 0.0;
                e.process(mp, clock, block * 48,
                          chordOn({ 48, 52, 55, 60, 64, 67, 72, 76, 79, 84, 88, 91 }), out);
                std::set<int> seen;
                for (auto& ev : collect(out))
                    if (ev.on)
                        seen.insert(ev.note);
                return (int) seen.size();
            };
            expect(spread(100) > spread(30),
                   "the top of the knob visits more of the chord than the bottom does");
        }

        // --- Stray (2026-08-21) -----------------------------------------------------------

        beginTest("Stray at 0 keeps every note in the chord, however high Mutate goes");
        {
            // The default, and the reason Mutate is safe to turn up again. A stronger claim
            // than the Mutate sweep above: there this parameter was merely left at its default,
            // here it is pinned at zero while its neighbour is at maximum.
            ArpEngine e;
            e.prepare(sr);
            auto mp = p;
            mp.mutate = 100;
            mp.stray = 0;
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(mp, clock, block * 32, chordOn({ 60, 64, 67 }), out);
            for (auto& ev : collect(out))
                if (ev.on)
                    expect(ev.note == 60 || ev.note == 64 || ev.note == 67,
                           "Stray 0 played " + juce::String(ev.note) + ", off the held chord");
        }

        beginTest("Stray to 50 stays in scale; only past 50 may it leave the scale");
        {
            // The 2026-08-19 zones, on their own knob and rescaled to its travel - they used to
            // sit at 50 and 75 of Mutate's. C major as the mask: the default 0xFFF is chromatic
            // and would make "in scale" vacuous.
            constexpr unsigned int cMajor = (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5)
                                          | (1u << 7) | (1u << 9) | (1u << 11);
            for (int amt = 10; amt <= 50; amt += 10)
            {
                ArpEngine e;
                e.prepare(sr);
                auto mp = p;
                mp.stray = amt;
                mp.rootPc = 0;
                mp.scaleMask = cMajor;
                juce::MidiBuffer out;
                clock.ppq = 0.0;
                e.process(mp, clock, block * 32, chordOn({ 60, 64, 67 }), out);
                for (auto& ev : collect(out))
                    if (ev.on)
                        expect((cMajor >> (ev.note % 12)) & 1u,
                               "Stray " + juce::String(amt) + " played " + juce::String(ev.note)
                                   + ", out of scale below the chromatic zone");
            }
            // At the top the strays are chromatic but still local: a stray is at most three
            // semitones off a note the walk could have chosen, so nothing lands far from the
            // chord. The bound is what pins the reach; scale membership is deliberately not
            // asserted, because leaving the scale is the feature.
            {
                ArpEngine e;
                e.prepare(sr);
                auto mp = p;
                mp.stray = 100;
                mp.rootPc = 0;
                mp.scaleMask = cMajor;
                juce::MidiBuffer out;
                clock.ppq = 0.0;
                e.process(mp, clock, block * 32, chordOn({ 60, 64, 67 }), out);
                bool leftScale = false;
                for (auto& ev : collect(out))
                    if (ev.on)
                    {
                        int nearest = 127;
                        for (int c : { 60, 64, 67 })
                            nearest = juce::jmin(nearest, std::abs(ev.note - c));
                        expect(nearest <= 4, "Stray 100 played " + juce::String(ev.note)
                                                 + ", further than a stray can reach");
                        leftScale = leftScale || (((cMajor >> (ev.note % 12)) & 1u) == 0);
                    }
                expect(leftScale, "at 100, over 32 steps, at least one stray left the scale");
            }
        }

        // --- Scale Lock reaching the line's output (2026-08-26) ---------------------------
        //
        // Owen: "does the scale lock button at the top apply to arpeggiators and harmonies?"
        // It did not - Root and Scale reached the engine, the *lock* did not. These pin all
        // three routes at once, because they all land in `addHit`.

        beginTest("snapToMask rounds to the nearest degree, ties down, and is idempotent");
        {
            constexpr unsigned int cMajor = (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5)
                                          | (1u << 7) | (1u << 9) | (1u << 11);
            expectEquals(ArpEngine::snapToMask(60, cMajor, 0), 60, "an in-scale note is untouched");
            expectEquals(ArpEngine::snapToMask(61, cMajor, 0), 60, "C# rounds down to C");
            expectEquals(ArpEngine::snapToMask(63, cMajor, 0), 62, "Eb rounds down to D");
            expectEquals(ArpEngine::snapToMask(66, cMajor, 0), 65, "F# rounds down to F");
            // Ties go down, the same way the kit's scales::snapToScale walks (`-d` before `+d`),
            // so a snapped note is stable rather than drifting upward under repeated passes.
            expectEquals(ArpEngine::snapToMask(ArpEngine::snapToMask(63, cMajor, 0), cMajor, 0),
                         62, "snapping is idempotent");
            // The root moves the whole mask with it: in D major, C# is in and C is not.
            constexpr unsigned int major = cMajor;
            expectEquals(ArpEngine::snapToMask(61, major, 2), 61, "C# is the 7th of D major");
            // Chromatic is every pitch class, so there is nothing to snap to. Checked because
            // it is the *default* mask, and a snap that moved notes there would break every
            // session that has never touched Scale.
            for (int n = 48; n < 72; ++n)
                expectEquals(ArpEngine::snapToMask(n, 0xFFFu, 0), n,
                             "a chromatic scale snaps nothing");
        }

        beginTest("Scale Lock snaps a chord the arp was handed, harmony voices included");
        {
            constexpr unsigned int cMajor = (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5)
                                          | (1u << 7) | (1u << 9) | (1u << 11);
            // Cm in C major: the Eb is the note that has to move, and a minor-third harmony
            // voice on every hit is the second route into addHit.
            const auto run = [&](bool lock)
            {
                ArpEngine e;
                e.prepare(sr);
                auto sp = p;
                sp.rootPc = 0;
                sp.scaleMask = cMajor;
                sp.scaleLock = lock;
                sp.harmSemis[0] = 3;
                sp.harmChance[0] = 100;
                juce::MidiBuffer out;
                clock.ppq = 0.0;
                e.process(sp, clock, block * 16, chordOn({ 60, 63, 67 }), out);
                std::set<int> heard;
                for (auto& ev : collect(out))
                    if (ev.on)
                        heard.insert(ev.note);
                return heard;
            };

            for (int n : run(true))
                expect((cMajor >> (n % 12)) & 1u,
                       "Lock on played " + juce::String(n) + ", out of scale");

            // The other half, and the one that stops this passing vacuously: unlocked, the
            // same run genuinely does leave the scale, so the assertion above is about the
            // switch rather than about the notes happening to fit.
            bool leftScale = false;
            for (int n : run(false))
                leftScale = leftScale || (((cMajor >> (n % 12)) & 1u) == 0);
            expect(leftScale, "Lock off still plays the Eb and its harmony, unchanged");
        }

        beginTest("Scale Lock wins over Stray's chromatic zone");
        {
            // Stray at 100 is the one thing in the engine whose whole job is leaving the scale,
            // so it is the sharpest test that the snap is applied last. The two are not in
            // conflict: locking the output is what the toggle says it does.
            constexpr unsigned int cMajor = (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5)
                                          | (1u << 7) | (1u << 9) | (1u << 11);
            ArpEngine e;
            e.prepare(sr);
            auto sp = p;
            sp.rootPc = 0;
            sp.scaleMask = cMajor;
            sp.scaleLock = true;
            sp.stray = 100;
            sp.mutate = 100;
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(sp, clock, block * 32, chordOn({ 60, 64, 67 }), out);
            int notes = 0;
            for (auto& ev : collect(out))
                if (ev.on)
                {
                    ++notes;
                    expect((cMajor >> (ev.note % 12)) & 1u,
                           "Stray 100 under Lock played " + juce::String(ev.note));
                }
            expect(notes > 0, "the run actually sounded");
        }

        beginTest("Scale Lock never leaves a pitch hanging when two notes snap onto one");
        {
            // The snap happens *before* addHit's dedup, which is the half that matters: two
            // hits on one pitch in one step is a hung note, not a doubled one (see addHit).
            // Db and D both round to D in C major, so this step has to come out as one.
            constexpr unsigned int cMajor = (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5)
                                          | (1u << 7) | (1u << 9) | (1u << 11);
            ArpEngine e;
            e.prepare(sr);
            auto sp = p;
            sp.rootPc = 0;
            sp.scaleMask = cMajor;
            sp.scaleLock = true;
            sp.direction = ArpEngine::Direction::chord; // one step, every held note at once
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(sp, clock, block * 8, chordOn({ 61, 62 }), out);
            std::map<int, int> balance;
            for (auto& ev : collect(out))
                balance[ev.note] += ev.on ? 1 : -1;
            for (auto& [note, b] : balance)
                expect(b >= 0 && b <= 1,
                       "note " + juce::String(note) + " left the run " + juce::String(b)
                           + " note-ons deep");
        }

        beginTest("Stray adds no note events, it only replaces them");
        {
            // The complaint the whole split came out of, pinned so it cannot come back: "it's
            // adding additional notes in the arpeggiator". It never did - a step fires no more
            // hits whatever these two say - so what was heard was pitches off the chord rather
            // than extra ones. Both halves are worth holding: how many notes there are is this
            // test, and which pitches they may be is the two above.
            //
            // **Three shapes, because one of them cannot see the failure.** Under Up a step
            // resolves one hit whatever Stray does, so the count is equal by construction and
            // the assertion proves nothing. The path where a stray genuinely *could* move the
            // count is `addHit`'s (note, channel) dedupe: two hits that collided into one
            // before straying need not collide afterwards. Chord shape and a Harmony lane are
            // the two ways to put more than one hit in a step, so they are the two that carry
            // the claim - Up is kept only to show the common case is covered too.
            const auto count = [&](int stray, ArpEngine::Direction dir, int harmony)
            {
                ArpEngine e;
                e.prepare(sr);
                if (harmony > 0)
                    for (int st = 0; st < ArpEngine::maxSteps; ++st)
                        e.lanes.value[ArpEngine::laneHarmony][(size_t) st].store(harmony);
                auto mp = p;
                mp.mutate = 100;
                mp.stray = stray;
                mp.direction = dir;
                juce::MidiBuffer out;
                clock.ppq = 0.0;
                e.process(mp, clock, block * 32, chordOn({ 60, 64, 67 }), out);
                int n = 0;
                for (auto& ev : collect(out))
                    if (ev.on)
                        ++n;
                return n;
            };
            using Dir = ArpEngine::Direction;

            // **Never more** is the claim, and it is deliberately not *exactly the same*. Under
            // a multi-hit shape a stray can move two hits onto one pitch, and `addHit` dedupes
            // on (note, channel), so the step comes out a voice thinner. That is the rule the
            // harmony voices already follow - a hit that collapses onto its source is dropped
            // rather than doubled, because a collapsed interval is a silence - and it points
            // the safe way: the complaint this whole split came from was notes arriving, not
            // notes going missing. Asserting equality here would be asserting something the
            // engine does not promise and should not.
            for (const auto dir : { Dir::up, Dir::chord })
                for (const int harmony : { 0, 2 })
                {
                    const int tame = count(0, dir, harmony);
                    const int strayed = count(100, dir, harmony);
                    expect(strayed <= tame,
                           "a straying line fires no more notes than a tame one: "
                               + juce::String(strayed) + " against " + juce::String(tame));
                }

            // The single-hit shape is the one case where it *is* exact, and worth pinning as
            // such: one hit a step cannot collide with itself, so nothing can thin it.
            expectEquals(count(100, Dir::up, 0), count(0, Dir::up, 0),
                         "Up: one hit a step, note for note whatever Stray does");
        }

        beginTest("Lock at 100 repeats one variation; at 0 it keeps finding new ones");
        {
            const auto run = [&](int lock)
            {
                ArpEngine e;
                e.prepare(sr);
                auto mp = p;
                mp.mutate = 100;
                mp.stray = 60; // Lock holds the out-of-chord finds too, not only the in-chord ones
                mp.mutateLock = lock;
                // A drawn Note lane, not the Up shape: the shape walk carries a cursor, so its
                // base index has period 3 over a triad and is not a function of the step at all.
                // Fixed indices make the base periodic at the lane length and stateless, which
                // is what these two tests are actually about.
                mp.usePattern = true;
                for (int st = 0; st < 8; ++st)
                    e.lanes.value[ArpEngine::laneNote][(size_t) st].store(1 + st % 3);
                juce::MidiBuffer out;
                clock.ppq = 0.0;
                e.process(mp, clock, block * 32, chordOn({ 60, 64, 67 }), out);
                std::vector<int> notes;
                for (auto& ev : collect(out))
                    if (ev.on)
                        notes.push_back(ev.note);
                return notes;
            };

            const auto locked = run(100);
            expect(locked.size() >= 16, "enough steps to compare two passes");
            bool repeats = true;
            for (size_t i = 8; i < locked.size(); ++i)
                if (locked[i] != locked[i - 8])
                    repeats = false;
            expect(repeats, "locked, every pass of the eight-step lane plays the same notes");

            const auto loose = run(0);
            bool differs = false;
            for (size_t i = 8; i < loose.size(); ++i)
                if (loose[i] != loose[i - 8])
                    differs = true;
            expect(differs, "unlocked, a later pass is not the first one over again");
        }

        beginTest("Mutate is stateless from the playhead: a jump lands where a walk would");
        {
            // The same rule the rhythm dividers are pinned against. The era is derived from the
            // step index, so the twelfth step is the twelfth step whether or not you walked to it.
            const auto noteAt = [&](bool jump)
            {
                ArpEngine e;
                e.prepare(sr);
                auto mp = p;
                mp.mutate = 80;
                mp.stray = 60; // both stages hash the same cell, so this pins both of them
                mp.mutateLock = 40;
                mp.usePattern = true; // see the Lock test above for why the shape will not do
                for (int st = 0; st < 8; ++st)
                    e.lanes.value[ArpEngine::laneNote][(size_t) st].store(1 + st % 3);
                juce::MidiBuffer warm;
                clock.ppq = 0.0;
                e.process(mp, clock, block, chordOn({ 60, 64, 67 }), warm); // hold the chord
                if (! jump)
                    for (int i = 1; i < 12; ++i)
                    {
                        juce::MidiBuffer skip;
                        clock.ppq = 0.25 * i;
                        e.process(mp, clock, block, {}, skip);
                    }
                juce::MidiBuffer out;
                clock.ppq = 0.25 * 12;
                e.process(mp, clock, block, {}, out);
                for (auto& ev : collect(out))
                    if (ev.on)
                        return ev.note;
                return -1;
            };
            expectEquals(noteAt(true), noteAt(false), "the same step plays the same note either way");
        }

        // --- The per-line harmony voices (2026-08-19, BigSky's shimmer list) --------------

        beginTest("a harmony voice plays at its source's velocity, not a draw of its own");
        {
            // 2026-08-23, Owen: "harmony same velocity". A voice thickens the note it
            // harmonises, so it has to arrive at that note's loudness. It drew its own share of
            // the Humanize Velocity band until today - the draw was made per emitted hit, in the
            // ratchet loop, and a voice is an ordinary hit by then - so a voice could sit up to
            // `2 * humanVel` from the note it was thickening and read as a second player rather
            // than as part of the first. The draw is made once per source hit now and the voices
            // copy it; see Hit::src.
            ArpEngine e;
            e.prepare(sr);
            auto mp = p;
            mp.velLevel = 64;     // mid, so the band reaches equally either way
            mp.humanVel = 60;     // wide enough that two independent draws could not agree by luck
            mp.harmSemis[0] = 12; // one voice, an octave up
            mp.harmChance[0] = 100;

            std::set<float> distinct;
            int paired = 0;
            for (int i = 0; i < 16; ++i)
            {
                juce::MidiBuffer out;
                clock.ppq = 0.25 * i;
                e.process(mp, clock, block, i == 0 ? chordOn({ 60 }) : juce::MidiBuffer {}, out);
                std::vector<float> vels;
                for (const auto meta : out)
                    if (meta.getMessage().isNoteOn())
                        vels.push_back(meta.getMessage().getFloatVelocity());
                if (vels.size() != 2) // one held note: the hit and its one voice
                    continue;
                ++paired;
                expectWithinAbsoluteError(vels[0], vels[1], 1.0f / 127.0f,
                                          "the voice and the note it thickens came out at "
                                          "different velocities");
                distinct.insert(vels[0]);
            }
            expect(paired >= 4, "the run never produced a note beside its harmony");
            // **And the draw is still a draw.** Were humanVel simply being ignored, every step
            // would play at the level and the pairing above would pass on a dead flat run -
            // which is the shape of green test the rest of this file exists to avoid.
            expect(distinct.size() > 1, "the humanise draw stopped varying between steps");
        }

        beginTest("the Harmony lane's voices share their source's velocity too, in both modes");
        {
            // 2026-08-24, found reviewing the fix above. `Hit::src` reached the two *fixed*
            // per-line voices and stopped there: the Harmony **lane**'s two modes still called
            // addHit without naming a source, so each stayed its own source and went on rolling
            // an independent draw - the reported bug surviving by the one route the fix did not
            // cover. Worse than a plain miss, it was inconsistent: a fixed voice stacked on a
            // lane-harmony hit *did* inherit that hit's velocity, so within one step some voices
            // shared and some did not. One rule, no carve-out.
            const auto pairsAgree = [&](int mode, const juce::String& what)
            {
                ArpEngine e;
                e.prepare(sr);
                e.harmonyMode.store(mode);
                for (int st = 0; st < 8; ++st)
                    e.lanes.value[ArpEngine::laneHarmony][(size_t) st].store(1);
                auto mp = p;
                mp.usePattern = true; // or every lane reads as its default and Harmony is off
                mp.velLevel = 64;     // mid, so the band reaches equally either way
                mp.humanVel = 60;     // wide enough that two independent draws could not agree by luck
                mp.humanize = 0;      // timing out of it: this is the velocity axis alone

                std::set<float> distinct;
                int paired = 0;
                for (int i = 0; i < 16; ++i)
                {
                    juce::MidiBuffer out;
                    clock.ppq = 0.25 * i;
                    e.process(mp, clock, block, i == 0 ? chordOn({ 60, 64, 67 }) : juce::MidiBuffer {}, out);
                    std::vector<float> vels;
                    for (const auto meta : out)
                        if (meta.getMessage().isNoteOn())
                            vels.push_back(meta.getMessage().getFloatVelocity());
                    if (vels.size() != 2) // the hit and its one lane voice
                        continue;
                    ++paired;
                    expectWithinAbsoluteError(vels[0], vels[1], 1.0f / 127.0f,
                                              what + ": the voice and the note it thickens came "
                                                     "out at different velocities");
                    distinct.insert(vels[0]);
                }
                expect(paired >= 4, what + ": the run never produced a note beside its harmony");
                // The same guard the fixed-voice test carries: were humanVel being ignored the
                // pairing above would pass on a dead flat run.
                expect(distinct.size() > 1, what + ": the humanise draw stopped varying");
            };
            pairsAgree(0, "mode 0, a chord tone above");
            pairsAgree(1, "mode 1, the subharmonic below");
        }

        beginTest("the velocity draw is per ratchet: repeats differ, a pair inside one does not");
        {
            // The draw sits **inside** the ratchet loop on purpose, so each repeat of a
            // ratcheted step draws afresh and a roll keeps its life; what is shared is a hit and
            // its harmony *within* one repeat. Hoisting it to once per step is the obvious next
            // simplification - the lambda is already declared outside the loop, so it is a
            // one-line move - and it would flatten every repeat to one velocity, a different
            // feature. Nothing pinned that until this test: the sharing test above runs at
            // ratchets = 1, where the two readings are indistinguishable, and the older ratchet
            // tests assert on onset timing alone.
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneRatchet][0].store(4);
            auto mp = p;
            mp.usePattern = true; // or the Ratchet lane reads as its default of one
            mp.velLevel = 64;
            mp.humanVel = 60;
            mp.humanize = 0;      // so a repeat's two notes share one sample offset exactly
            mp.harmSemis[0] = 12; // one voice, an octave up
            mp.harmChance[0] = 100;

            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(mp, clock, block, chordOn({ 60 }), out);

            // Grouped by onset: with H.TIME out of it, one repeat is exactly the notes sharing a
            // sample position - the hit and its voice.
            std::map<int, std::vector<float>> byOnset;
            for (const auto meta : out)
                if (meta.getMessage().isNoteOn())
                    byOnset[meta.samplePosition].push_back(meta.getMessage().getFloatVelocity());

            expectEquals((int) byOnset.size(), 4, "ratchet 4 did not fire four repeats");
            std::set<float> perRepeat;
            for (const auto& [onset, vels] : byOnset)
            {
                juce::ignoreUnused(onset);
                expectEquals((int) vels.size(), 2, "a repeat did not carry its harmony voice");
                expectWithinAbsoluteError(vels[0], vels[1], 1.0f / 127.0f,
                                          "inside one repeat the voice and its source disagreed");
                perRepeat.insert(vels[0]);
            }
            expect(perRepeat.size() > 1,
                   "every repeat played at one velocity - the draw was hoisted out of the "
                   "ratchet loop, which flattens a roll");
        }

        beginTest("a harmony voice may name two pitches, and they share one chance roll");
        {
            // 2026-08-21, Owen: "in the harmony, when you select octave plus fifth, it looks
            // like it only just does octave". Every entry in the shimmer list names a single
            // interval except one, and that one says "&": "+ Octave & 5th" is an octave *and* a
            // fifth, two notes off each note harmonised. The engine read it as a compound
            // interval instead - one note 19 semitones up - so it played one note where its
            // name promises two, and 19 is a different pitch from either of them.
            //
            // The list itself lives in KeysProcessor, so which entry carries which intervals is
            // pinned in StateTests where a processor is at hand. This is the engine half: a
            // voice with a second interval sounds both, and does so under one roll.
            ArpEngine e;
            e.prepare(sr);
            auto mp = p;
            mp.harmSemis[0] = 12; // the octave...
            mp.harmSemisB[0] = 7; // ...and the fifth, from the one slot
            mp.harmChance[0] = 100;
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(mp, clock, block * 8, chordOn({ 60, 64, 67 }), out);

            std::set<int> seen;
            int ons = 0;
            for (auto& ev : collect(out))
                if (ev.on)
                {
                    seen.insert(ev.note);
                    ++ons;
                }
            expect(seen.count(72) + seen.count(76) + seen.count(79) > 0, "the octave voice sounded");
            // **71 and 74 only, not 67.** The chord is 60-64-67 and the base Up walk plays all
            // three, so counting 67 here made the assertion true whatever the second interval
            // did - delete harmSemisB entirely and it still passed. The fifth above 64 and
            // above 67 are the two pitches that can only have come from this voice.
            expect(seen.count(71) + seen.count(74) > 0, "the fifth voice sounded");

            // One voice, one roll: three hits a step, never two. A slot that half-fired would be
            // the same bug wearing the chance knob.
            const auto plainRun = [&]
            {
                ArpEngine e2;
                e2.prepare(sr);
                auto mp2 = p;
                juce::MidiBuffer o2;
                clock.ppq = 0.0;
                e2.process(mp2, clock, block * 8, chordOn({ 60, 64, 67 }), o2);
                int n = 0;
                for (auto& ev : collect(o2))
                    if (ev.on)
                        ++n;
                return n;
            }();
            expectEquals(ons, plainRun * 3, "every step carried its note plus both of the voice's");

            // A second interval of 0 is the other twenty-six entries, and must be untouched.
            const auto count = [&](int semisB)
            {
                ArpEngine e3;
                e3.prepare(sr);
                auto mp3 = p;
                mp3.harmSemis[0] = 12;
                mp3.harmSemisB[0] = semisB;
                mp3.harmChance[0] = 100;
                juce::MidiBuffer o3;
                clock.ppq = 0.0;
                e3.process(mp3, clock, block * 8, chordOn({ 60, 64, 67 }), o3);
                int n = 0;
                for (auto& ev : collect(o3))
                    if (ev.on)
                        ++n;
                return n;
            };
            expectEquals(count(0), plainRun * 2, "a one-interval voice still doubles and no more");
        }

        beginTest("A harmony voice doubles every note at its interval; chance 0 is silence");
        {
            const auto run = [&](int semis, int chancePct)
            {
                ArpEngine e;
                e.prepare(sr);
                auto mp = p;
                mp.harmSemis[0] = semis;
                mp.harmChance[0] = chancePct;
                juce::MidiBuffer out;
                clock.ppq = 0.0;
                e.process(mp, clock, block * 8, chordOn({ 60, 64, 67 }), out);
                std::vector<int> ons;
                for (auto& ev : collect(out))
                    if (ev.on)
                        ons.push_back(ev.note);
                return ons;
            };

            const auto plain = run(0, 100);
            const auto octaveUp = run(12, 100);
            expectEquals((int) octaveUp.size(), (int) plain.size() * 2,
                         "at chance 100 every note carries its voice");
            for (int n : octaveUp)
                expect(n == 60 || n == 64 || n == 67 || n == 72 || n == 76 || n == 79,
                       "harmony +12 played " + juce::String(n)
                           + ", which is neither the chord nor its octave");

            // Chance 0 is Off by another route, and Off must mean byte-identical.
            const auto silent = run(12, 0);
            expectEquals((int) silent.size(), (int) plain.size(), "chance 0 adds nothing");
            for (size_t i = 0; i < silent.size() && i < plain.size(); ++i)
                expectEquals(silent[i], plain[i], "chance 0 changes nothing");
        }

        beginTest("Both harmony voices stack, and a clamped voice is dropped, not doubled");
        {
            ArpEngine e;
            e.prepare(sr);
            auto mp = p;
            mp.harmSemis[0] = 12;
            mp.harmSemis[1] = -12;
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(mp, clock, block * 4, chordOn({ 60, 64, 67 }), out);
            int ons = 0;
            for (auto& ev : collect(out))
                if (ev.on)
                    ++ons;
            expectEquals(ons % 3, 0, "each step is the note and its two voices");
            expect(ons >= 9, "two voices tripled the steps");

            // A voice pushed past the MIDI range clamps onto its own source and is dropped -
            // the subharmonic rule: a collapsed interval is a silence, not a doubled attack.
            ArpEngine e2;
            e2.prepare(sr);
            auto mp2 = p;
            mp2.harmSemis[0] = 24;
            juce::MidiBuffer out2;
            clock.ppq = 0.0;
            e2.process(mp2, clock, block * 4, chordOn({ 120, 124 }), out2);
            for (auto& ev : collect(out2))
                if (ev.on)
                    expect(ev.note == 120 || ev.note == 124 || ev.note == 127,
                           "a clamped voice either lands clamped or not at all");
        }

        beginTest("Two harmony voices at the same interval cannot hang a note");
        {
            // The regression: a duplicate {note, channel} inside one step is not a doubled
            // attack, it is a hung note. Both hits land at the same offset, so the second goes
            // down emitHit's tie branch and writes a note-off at `on - 1` - before the first
            // one's note-on. Downstream that leaves ArpMerge's refcount pinned above zero and
            // the pitch is never released. Every note-on a step emits must have its own
            // note-off, and no pitch may be opened twice in one step.
            ArpEngine e;
            e.prepare(sr);
            auto mp = p;
            mp.harmSemis[0] = 7; // both voices on the same interval
            mp.harmSemis[1] = 7;
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(mp, clock, block * 8, chordOn({ 60, 64, 67 }), out);

            // Walk the stream in order and count owners per pitch, the way ArpMerge does.
            std::array<int, 128> refs {};
            int maxRefs = 0;
            for (auto& ev : collect(out))
            {
                if (ev.on)
                {
                    ++refs[(size_t) ev.note];
                    maxRefs = juce::jmax(maxRefs, refs[(size_t) ev.note]);
                }
                else if (refs[(size_t) ev.note] > 0)
                {
                    --refs[(size_t) ev.note];
                }
            }
            expectEquals(maxRefs, 1, "no pitch is ever opened twice over");

            // Whatever is still open at the end is what the line is holding, and the engine
            // must be able to let go of all of it.
            juce::MidiBuffer flushed;
            e.flushInto(flushed);
            for (auto& ev : collect(flushed))
                if (! ev.on && refs[(size_t) ev.note] > 0)
                    --refs[(size_t) ev.note];
            for (int n = 0; n < 128; ++n)
                expectEquals(refs[(size_t) n], 0, "every note-on is released");
        }

        // --- Per-step shapes, the fingered pair, and the Reset lane (2026-08-18) ----------

        beginTest("a Note lane step can name its own shape, and they share one walk");
        {
            // Cthulhu's Note graph, p23: the top half of the lane is arpeggiator shapes, and
            // they "vary consecutively one step after another" - one walk, read by whichever
            // shape the step names, not a separate walk per shape.
            ArpEngine e;
            e.prepare(sr);
            auto sp = p;
            sp.usePattern = true;
            sp.direction = ArpEngine::Direction::up;
            // Four steps up, then four down. One cursor, so the second half reflects the walk
            // the first half left rather than starting over.
            for (int st = 0; st < 4; ++st)
                e.lanes.value[ArpEngine::laneNote][(size_t) st].store(ArpEngine::noteShapeFirst);     // up
            for (int st = 4; st < 8; ++st)
                e.lanes.value[ArpEngine::laneNote][(size_t) st].store(ArpEngine::noteShapeFirst + 1); // down

            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(sp, clock, block * 8, chordOn({ 60, 64, 67 }), out);
            std::vector<int> notes;
            for (auto& ev : collect(out))
                if (ev.on)
                    notes.push_back(ev.note);
            expect(notes.size() >= 8, "eight steps sounded");
            // Up over a triad from cursor 0: 60 64 67 60. Then "down" reads the *same* advancing
            // cursor mirrored, n-1-(c%n): cursor 4,5,6,7 -> 64 60 67 64. Not 67 64 60 67, which
            // is what a walk that restarted at the shape change would give - the point of the
            // test is that it does not restart.
            const std::vector<int> want { 60, 64, 67, 60, 64, 60, 67, 64 };
            for (size_t i = 0; i < want.size() && i < notes.size(); ++i)
                expectEquals(notes[i], want[i], "step " + juce::String((int) i));
        }

        beginTest("fingered top and bottom alternate the walk with the chord's extreme");
        {
            const auto run = [&](ArpEngine::Direction d)
            {
                ArpEngine e;
                e.prepare(sr);
                auto fp = p;
                fp.direction = d;
                juce::MidiBuffer out;
                clock.ppq = 0.0;
                e.process(fp, clock, block * 6, chordOn({ 60, 64, 67 }), out);
                std::vector<int> notes;
                for (auto& ev : collect(out))
                    if (ev.on)
                        notes.push_back(ev.note);
                return notes;
            };

            const auto top = run(ArpEngine::Direction::fingeredTop);
            expect(top.size() >= 6, "six steps sounded");
            // "every 2nd note is the high note of the chord" - and the walk between them covers
            // the notes that are *not* the high one, or the shape would repeat the top twice.
            const std::vector<int> wantTop { 60, 67, 64, 67, 60, 67 };
            for (size_t i = 0; i < wantTop.size() && i < top.size(); ++i)
                expectEquals(top[i], wantTop[i], "fingered top step " + juce::String((int) i));

            const auto bot = run(ArpEngine::Direction::fingeredBottom);
            const std::vector<int> wantBot { 64, 60, 67, 60, 64, 60 };
            for (size_t i = 0; i < wantBot.size() && i < bot.size(); ++i)
                expectEquals(bot[i], wantBot[i], "fingered bottom step " + juce::String((int) i));
        }

        beginTest("the Reset lane restarts the walk, and does not rebase the lanes");
        {
            ArpEngine e;
            e.prepare(sr);
            auto rp = p;
            rp.usePattern = true;
            rp.direction = ArpEngine::Direction::up;
            // A two-step reset lane against a three-note chord, so the two cannot coincide by
            // accident: with the reset the walk can only ever reach its first two notes.
            e.lanes.length[ArpEngine::laneReset].store(2);
            e.lanes.value[ArpEngine::laneReset][0].store(1);

            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(rp, clock, block * 8, chordOn({ 60, 64, 67 }), out);
            std::vector<int> notes;
            for (auto& ev : collect(out))
                if (ev.on)
                    notes.push_back(ev.note);
            expect(notes.size() >= 8, "eight steps sounded");
            // The reset zeroes the cursor *before* the step reads it, so the reset step itself
            // plays the first note of the shape - which is what the manual's example describes.
            // 67 never sounds: every other step is a restart, so the walk never gets past its
            // second note. Without the lane this would be 60 64 67 60 64 67 60 64.
            const std::vector<int> want { 60, 64, 60, 64, 60, 64, 60, 64 };
            for (size_t i = 0; i < want.size() && i < notes.size(); ++i)
                expectEquals(notes[i], want[i], "step " + juce::String((int) i));

            // And the lanes kept walking: had the reset rebased them, step 2 would be the reset
            // cell for ever and the Note lane could never reach its own step 3.
            expectEquals(e.lanes.value[ArpEngine::laneReset][0].load(), 1, "the lane is untouched");
        }

        beginTest("every per-step shape value maps to a Direction, and no other value does");
        {
            for (int v = ArpEngine::noteShapeFirst; v <= ArpEngine::noteShapeLast; ++v)
            {
                const auto d = ArpEngine::shapeForNoteValue(v);
                expect((int) d >= 0 && (int) d < ArpEngine::numDirections,
                       "value " + juce::String(v) + " names a real Direction");
            }
            expectEquals(ArpEngine::noteShapeLast - ArpEngine::noteShapeFirst + 1, 8,
                         "eight shapes, as in Cthulhu's Note graph");
            expectEquals(ArpEngine::laneRange((int) ArpEngine::laneNote).hi, ArpEngine::noteShapeLast,
                         "the Note lane's range reaches the last of them");
        }
    }
};


static ArpEngineTests arpEngineTests;
} // namespace keys::tests
