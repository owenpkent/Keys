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

        beginTest("the Hz step is one over the rate, and Dot and Trip do not touch it");
        {
            // Sync divides a beat, so Dot and Trip are subdivisions of one. Hz has no beat to
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
            trip.triplet = true;
            auto both = hzp;
            both.dotted = both.triplet = true;
            expectWithinAbsoluteError(period(dot, 4), 6000, 2, "Dot is not applied in Hz");
            expectWithinAbsoluteError(period(trip, 4), 6000, 2, "and neither is Trip");
            expectWithinAbsoluteError(period(both, 4), 6000, 2, "nor the two together");

            // ...and both still mean exactly what they always did on the synced side.
            auto syncDot = p;
            syncDot.dotted = true;
            auto syncTrip = p;
            syncTrip.triplet = true;
            expectWithinAbsoluteError(period(syncDot, 4), 9000, 2, "a dotted 1/16 is a step and a half");
            expectWithinAbsoluteError(period(syncTrip, 4), 4000, 2, "a triplet 1/16 is two thirds of one");
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
    }
};

static ArpEngineTests arpEngineTests;
} // namespace keys::tests
