#include "../src/ArpEngine.h"
#include <juce_core/juce_core.h>

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
    }
};

static ArpEngineTests arpEngineTests;
} // namespace keys::tests
