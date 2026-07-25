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
            ArpEngine e;
            e.prepare(sr);
            e.lanes.value[ArpEngine::laneGate][0].store(50);
            juce::MidiBuffer out;
            clock.ppq = 0.0;
            e.process(lp, clock, block, chordOn({ 60 }), out);
            juce::MidiBuffer out2;
            clock.ppq = 0.25;
            e.process(lp, clock, block, {}, out2);
            bool offSeen = false;
            for (auto& x : collect(out))
                if (! x.on && x.note == 60) { offSeen = true; expectWithinAbsoluteError(x.sample, 3000, 2); }
            for (auto& x : collect(out2))
                if (! x.on && x.note == 60) { offSeen = true; }
            expect(offSeen, "note-off arrives");
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
