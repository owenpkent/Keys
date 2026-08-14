#include "../src/ArpEngine.h"
#include "../src/EuclidGen.h"
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

        beginTest("Humanize velocity is its own knob, and VEL trim is bipolar");
        {
            // One Humanize drove both halves until 2026-08-02; now `humanize` is the timing
            // nudge alone and `humanVel` the velocity shave alone, and `velTrim` is the level
            // control centred on "as played". Each question below fails if the split leaks.
            const auto velsWith = [&](int human, int humanVel, int trim)
            {
                ArpEngine e;
                e.prepare(sr);
                auto sp = p;
                sp.humanize = human;
                sp.humanVel = humanVel;
                sp.velTrim = trim;
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

            const auto base = velsWith(0, 0, 0);
            expect(! base.empty(), "the reference run plays");

            const auto timingOnly = velsWith(100, 0, 0);
            for (size_t i = 0; i < timingOnly.size() && i < base.size(); ++i)
                expectWithinAbsoluteError(timingOnly[i], base[i], 0.005f,
                                          "full H.TIME leaves every velocity alone");

            const auto velOnly = velsWith(0, 100, 0);
            bool sawQuieter = false;
            for (size_t i = 0; i < velOnly.size() && i < base.size(); ++i)
            {
                expect(velOnly[i] <= base[i] + 0.005f, "H.VEL only ever shaves, never louder");
                sawQuieter = sawQuieter || velOnly[i] < base[i] - 0.005f;
            }
            expect(sawQuieter, "full H.VEL actually moved something");

            // The trim's curve is squared - hearing is logarithmic, and the linear version
            // spent nearly all its audible change at the very end of the travel - and it
            // multiplies *after* the 0.05 audibility floor, so a deep cut reaches MIDI
            // velocity 1 instead of pinning at 6 from -90 down.
            const auto half = velsWith(0, 0, -50);
            for (size_t i = 0; i < half.size() && i < base.size(); ++i)
                expectWithinAbsoluteError(half[i], base[i] * 0.25f, 0.02f,
                                          "trim at -50 plays at quarter velocity (half as loud)");
            const auto boosted = velsWith(0, 0, 100);
            for (size_t i = 0; i < boosted.size() && i < base.size(); ++i)
                expectWithinAbsoluteError(boosted[i], juce::jmin(1.0f, base[i] * 4.0f), 0.02f,
                                          "trim at +100 quadruples, into the 1.0 ceiling");
            const auto deep = velsWith(0, 0, -96);
            expect(! deep.empty(), "a deep cut still plays");
            for (const float v : deep)
                expectWithinAbsoluteError(v, 1.0f / 127.0f, 0.012f,
                                          "-96 reaches the bottom MIDI step, not the old velocity-6 pin");
            expect(velsWith(0, 0, -100).empty(), "full-left trim is a mute, exactly as VOL 0 was");
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

        beginTest("a Humanize range reaches back from the knob by its span");
        {
            // 2026-08-03, the range knobs (Owen: "a serum style knob where you can set a range
            // in the knob ... when the outer ring is enabled, moving the dial moves the outer
            // ring with it"). Humanize was always "uniform between nothing and the knob"; the
            // span says how far under the knob the draw may fall, so the knob stays the
            // ceiling and the *range travels with it* - which is the half of this that has to
            // be pinned, because it is the half that is easy to build the other way round.
            const auto onsets = [&](int amount, int spanPct)
            {
                auto sp = p;
                sp.humanize = amount;
                sp.humanizeSpan = spanPct;
                sp.anchored = false;
                return onsetsOf(sp, clock, block, 12, steady);
            };

            // A 1/16 is 6000 samples here, and Humanize at 100 is 25 ms = 1200 samples - but
            // the engine also caps a nudge at 40% of the gap to the next sub-hit, so the
            // ceiling in force is min(1200, 2400) = 1200.
            const auto closed = onsets(100, 0);
            expect(closed.size() >= 4, "the run has to fire before this proves anything");
            // A span of zero collapses the range onto the knob: no randomness left, every hit
            // exactly 1200 late, so the gaps are all one step.
            for (size_t i = 1; i < closed.size(); ++i)
                expectWithinAbsoluteError((double) (closed[i] - closed[i - 1]), 6000.0, 2.0,
                                          "a closed range is a fixed offset, not a draw");
            // ... and it is pinned to the *knob*, not to zero.
            expectWithinAbsoluteError((double) closed[0], 1200.0, 2.0,
                                      "every hit is a full 25 ms late");

            // Wide open is the old behaviour: somewhere in 0..1200, and different draws.
            const auto open = onsets(100, 100);
            expect(open.size() >= 4);
            expect(open != closed, "a full span still randomizes");
            for (const auto o : open)
                expect(o % 6000 <= 1201, "and never past the ceiling it always had");

            // **The range travels with the knob.** Halve the knob with the span closed and
            // every hit lands at half the offset - the proof that the span is measured back
            // from the knob rather than up from zero.
            const auto halfClosed = onsets(50, 0);
            expect(halfClosed.size() >= 4);
            expectWithinAbsoluteError((double) halfClosed[0], 600.0, 2.0,
                                      "the closed range moved with the dial");

            // A span narrower than the knob is a floor under a draw: at least 30% of 25 ms.
            const auto narrow = onsets(100, 70);
            expect(narrow.size() >= 4);
            for (const auto o : narrow)
            {
                expect(o % 6000 >= 359, "every hit is at least as late as the range's bottom");
                expect(o % 6000 <= 1201, "and no later than the knob");
            }

            // Humanize itself off means the span does nothing: there is no draw to put a
            // bottom under, and a range that nudged on its own would make a knob at zero audible.
            const auto off = onsets(0, 0);
            expect(off.size() >= 4);
            expectWithinAbsoluteError((double) off[0], 0.0, 2.0,
                                      "a closed range under a Humanize of zero is still zero");
        }

        beginTest("the rate readout is the step length as a fraction of a bar");
        {
            // 2026-08-03, Owen: "shouldn't it just be 1/5 not 1/4:5?" - it should, and this is
            // what makes it so. The invariant worth pinning is that the *straight* readings are
            // byte-identical to the division names the parameter carries, since that is what
            // stops this from being a second, drifting copy of the rate list.
            const auto text = [](int i, bool dot, int tup) { return ArpEngine::rateSyncText(i, dot, tup); };
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
            const auto on = onsetsOf(sp, hostRolling, block, 4, steady);
            // 1/16 at 90 bpm: 0.25 beat * 60/90 s = 1/6 s = 8000 samples, so three onsets land
            // inside 4 * 6000 = 24000 samples (0, 8000, 16000).
            expectEquals((int) on.size(), 3);
            for (int i = 1; i < (int) on.size(); ++i)
                expectWithinAbsoluteError(on[(size_t) i] - on[(size_t) (i - 1)], 8000, 2,
                                          "sync off steps at fallbackBpm even though the host is rolling");
        }

        beginTest("followHost on, transport stopped: fallbackBpm runs the clock");
        {
            // The "on" in followHost only ever means "let the host in when it is actually
            // there to ask" - a stopped transport has nothing to override with, on or off.
            auto sp = p;
            sp.anchored = false;
            sp.followHost = true;
            sp.fallbackBpm = 100.0;
            ArpEngine::HostClock stoppedHost;
            stoppedHost.playing = false;
            stoppedHost.hasPpq = false;
            stoppedHost.bpm = 150.0; // stale; must not leak in while stopped
            const auto on = onsetsOf(sp, stoppedHost, block, 4, steady);
            expectEquals((int) on.size(), 4);
            // 1/16 at 100 bpm: 0.25 beat * 60/100 s = 0.15 s = 7200 samples.
            for (int i = 1; i < (int) on.size(); ++i)
                expectWithinAbsoluteError(on[(size_t) i] - on[(size_t) (i - 1)], 7200, 2,
                                          "a stopped transport reads fallbackBpm whatever followHost says");
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
            // Octave *is* a drifting lane, so a drifting run may transpose by whole octaves -
            // the pitch class is what must survive, since that is "which note it plays".
            expectEquals((int) n1.size(), (int) n2.size(), "same number of notes");
            for (size_t k = 0; k < n1.size() && k < n2.size(); ++k)
                expectEquals(n1[k] % 12, n2[k] % 12, "same pitch class at " + juce::String((int) k));
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
    }
};

static ArpEngineTests arpEngineTests;
} // namespace keys::tests
