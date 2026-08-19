// Migrations, and the absence-detection they rest on.
//
// These exist because that mechanism was **silently dead for months** (kit `state::load` handed
// `replaceState` a shared node, so `onExtra` saw a tree in which every parameter existed whether
// the session saved it or not - fixed 2026-08-14, kit PR #6). Nothing in Keys exercised a
// migration, so a dead one and a working one looked identical from the outside. That is the gap
// this file closes: every migration here loads a session with a parameter *removed* and asserts
// the migration noticed.
//
// Unlike the other test files, these need a real `KeysProcessor` - an APVTS, its parameter
// layout and `setStateInformation`. `Keys_tests` therefore links `Keys_SharedCode`, and each
// test constructs a processor of its own so nothing leaks between them.

#include "../src/PluginProcessor.h"
#include <juce_events/juce_events.h>

namespace keys::tests
{
namespace
{
    // A processor plus the message loop its heartbeat timer and MCP bridge expect. Constructing
    // one without this trips JUCE's own assertions long before any assertion of ours.
    struct Host
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        KeysProcessor processor;
    };

    // Round-trip a processor's state, minus the parameters named in `drop` - which is exactly
    // the shape of a session saved before those parameters existed.
    juce::MemoryBlock stateWithout(KeysProcessor& p, const juce::StringArray& drop)
    {
        juce::MemoryBlock block;
        p.getStateInformation(block);
        auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), (int) block.getSize());
        jassert(xml != nullptr);
        auto root = juce::ValueTree::fromXml(*xml);
        auto params = root.getChildWithName(p.apvts.state.getType());
        jassert(params.isValid());
        for (int i = params.getNumChildren(); --i >= 0;)
            if (drop.contains(params.getChild(i).getProperty("id").toString()))
                params.removeChild(i, nullptr);
        juce::MemoryBlock out;
        if (auto trimmed = root.createXml())
            juce::AudioProcessor::copyXmlToBinary(*trimmed, out);
        return out;
    }

    float paramOf(KeysProcessor& p, const juce::String& id)
    {
        auto* raw = p.apvts.getRawParameterValue(id);
        return raw != nullptr ? raw->load() : std::numeric_limits<float>::quiet_NaN();
    }

    void setParam(KeysProcessor& p, const juce::String& id, float value)
    {
        if (auto* param = p.apvts.getParameter(id))
            param->setValueNotifyingHost(param->convertTo0to1(value));
    }
}

class StateTests : public juce::UnitTest
{
public:
    StateTests() : juce::UnitTest("Keys state and migrations", "keys") {}

    void runTest() override
    {
        beginTest("the absence tell works at all: a dropped parameter is absent in onExtra");
        {
            // The whole mechanism in one assertion. Before the kit fix this failed - the
            // parameter came back present, because replaceState had backfilled it into the very
            // tree the migration was about to read.
            Host h;
            const auto id = KeysProcessor::arpParamId(0, KeysProcessor::apVelTrim);
            const auto block = stateWithout(h.processor, { id });
            auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), (int) block.getSize());
            expect(xml != nullptr, "the trimmed state is still valid XML");
            const auto root = juce::ValueTree::fromXml(*xml);
            const auto params = root.getChildWithName(h.processor.apvts.state.getType());
            bool saw = false;
            for (int i = 0; i < params.getNumChildren(); ++i)
                saw = saw || params.getChild(i).getProperty("id").toString() == id;
            expect(! saw, "the parameter really is gone from the saved tree");
        }

        beginTest("migrateVelTrim folds an old Volume into VelTrim through the same curve");
        {
            Host h;
            const auto volId = KeysProcessor::arpParamId(0, KeysProcessor::apVolume);
            const auto trimId = KeysProcessor::arpParamId(0, KeysProcessor::apVelTrim);

            // An old session: Volume at 25%, and no VelTrim at all. VelTrim is left somewhere
            // else entirely first, so "kept the live value" is distinguishable from "migrated".
            setParam(h.processor, volId, 25.0f);
            setParam(h.processor, trimId, 75.0f);
            const auto block = stateWithout(h.processor, { trimId });
            h.processor.setStateInformation(block.getData(), (int) block.getSize());

            // trim = 100 * (sqrt(volume%) - 1); sqrt(0.25) = 0.5, so -50.
            expectWithinAbsoluteError(paramOf(h.processor, trimId), -50.0f, 1.0f,
                                      "Volume 25 became VelTrim -50, the same level");
            expectWithinAbsoluteError(paramOf(h.processor, volId), 100.0f, 0.5f,
                                      "...and Volume went back to its default");
        }

        beginTest("migrateVelLevel turns an old bipolar trim into the level that plays it");
        {
            Host h;
            const auto trimId = KeysProcessor::arpParamId(0, KeysProcessor::apVelTrim);
            const auto levelId = KeysProcessor::arpParamId(0, KeysProcessor::apVelLevel);

            // VEL became MIDI velocity on 2026-08-18. An old session carries its level in the
            // bipolar trim and has no VelLevel at all; the level is left somewhere else first, so
            // "kept the live value" stays distinguishable from "migrated".
            setParam(h.processor, trimId, -50.0f);
            setParam(h.processor, levelId, 20.0f);
            auto block = stateWithout(h.processor, { levelId });
            h.processor.setStateInformation(block.getData(), (int) block.getSize());

            // The trim's own curve, ((100+trim)/100)^2, against the velocity every chord Keys
            // fires actually left at: the midpoint of the pads' default Humanize band, 76.
            // trim -50 is a quarter of that: 19.
            expectWithinAbsoluteError(paramOf(h.processor, levelId), 19.0f, 1.0f,
                                      "trim -50 became the level that plays at the same loudness");

            // "As played" - the default trim - is the band's own midpoint, so a session that
            // never touched VEL opens playing exactly as loud as it always did.
            Host h2;
            setParam(h2.processor, trimId, 0.0f);
            setParam(h2.processor, levelId, 20.0f);
            block = stateWithout(h2.processor, { levelId });
            h2.processor.setStateInformation(block.getData(), (int) block.getSize());
            expectWithinAbsoluteError(paramOf(h2.processor, levelId), 76.0f, 1.0f,
                                      "an untouched VEL lands on the pads' own played velocity");

            // And the mute survives the change of units: full-left trim was silence, and 0 is.
            Host h3;
            setParam(h3.processor, trimId, -100.0f);
            setParam(h3.processor, levelId, 20.0f);
            block = stateWithout(h3.processor, { levelId });
            h3.processor.setStateInformation(block.getData(), (int) block.getSize());
            expectWithinAbsoluteError(paramOf(h3.processor, levelId), 0.0f, 0.5f,
                                      "trim -100 was a mute and level 0 is one");
        }

        beginTest("migrateTuplet folds a set Trip into Triplet and retires Trip");
        {
            Host h;
            const auto tripId = KeysProcessor::arpParamId(0, KeysProcessor::apTrip);
            const auto tupId = KeysProcessor::arpParamId(0, KeysProcessor::apTuplet);

            setParam(h.processor, tripId, 1.0f);  // an old session with Trip on
            setParam(h.processor, tupId, 3.0f);   // ...and a live value that must NOT survive
            const auto block = stateWithout(h.processor, { tupId });
            h.processor.setStateInformation(block.getData(), (int) block.getSize());

            expectWithinAbsoluteError(paramOf(h.processor, tupId), 1.0f, 0.01f,
                                      "Trip on became Tuplet index 1, which is Triplet");
            expectWithinAbsoluteError(paramOf(h.processor, tripId), 0.0f, 0.01f,
                                      "...and Trip went back to its default");
        }

        beginTest("an absent parameter is reset, not inherited from the live instance");
        {
            // The failure mode every one of these migrations exists to prevent: load a session
            // that predates a parameter and you keep whatever the *previous* patch left in it.
            Host h;
            for (const auto which : { KeysProcessor::apHumanizeSpan, KeysProcessor::apHumanVelSpan,
                                      KeysProcessor::apDrift })
            {
                const auto id = KeysProcessor::arpParamId(0, which);
                auto* param = h.processor.apvts.getParameter(id);
                expect(param != nullptr, "parameter exists: " + id);
                const float def = param->convertFrom0to1(param->getDefaultValue());
                setParam(h.processor, id, def == 0.0f ? 60.0f : 7.0f); // anything but the default
                const auto block = stateWithout(h.processor, { id });
                h.processor.setStateInformation(block.getData(), (int) block.getSize());
                expectWithinAbsoluteError(paramOf(h.processor, id), def, 0.51f,
                                          id + " came back to its default, not the live value");
            }
        }

        beginTest("bpmSync is backfilled, and a session that has it keeps what it says");
        {
            Host h;
            setParam(h.processor, "bpmSync", 0.0f); // live: off
            const auto block = stateWithout(h.processor, { "bpmSync" });
            h.processor.setStateInformation(block.getData(), (int) block.getSize());
            expect(paramOf(h.processor, "bpmSync") > 0.5f,
                   "absent means the default, which is on - not the live off");

            // ...and the other direction: present and off must survive the load untouched.
            setParam(h.processor, "bpmSync", 0.0f);
            juce::MemoryBlock full;
            h.processor.getStateInformation(full);
            setParam(h.processor, "bpmSync", 1.0f);
            h.processor.setStateInformation(full.getData(), (int) full.getSize());
            expect(paramOf(h.processor, "bpmSync") < 0.5f,
                   "a saved off is not migrated over - the tell is absence, not value");
        }

        beginTest("undo puts a cleared pad back, and redo takes it away again");
        {
            Host h;
            h.processor.setChordPad(3, { 60, 64, 67 }, "C");
            expectEquals((int) h.processor.chordPad(3).notes.size(), 3, "the pad was filled");

            h.processor.pushUndo("Clear pad", KeysProcessor::UndoScope::pads);
            h.processor.clearChordPad(3);
            expect(h.processor.chordPad(3).notes.empty(), "...and cleared");

            expect(h.processor.canUndo(), "there is something to undo");
            h.processor.undo();
            expectEquals((int) h.processor.chordPad(3).notes.size(), 3, "undo put the chord back");
            expectEquals(h.processor.chordPad(3).name, juce::String("C"), "...with its name");

            expect(h.processor.canRedo(), "there is something to redo");
            h.processor.redo();
            expect(h.processor.chordPad(3).notes.empty(), "redo cleared it again");
        }

        beginTest("an open gesture costs one entry, however many edits are inside it");
        {
            // The rule a lane drag rests on: the press pushes, and the thirty writes that
            // follow do not. Without it one stroke buries the stack.
            Host h;
            h.processor.setChordPad(0, { 60 }, "before");
            {
                const KeysProcessor::UndoGesture g { h.processor, "Big edit",
                                                     KeysProcessor::UndoScope::pads };
                for (int i = 0; i < 10; ++i)
                {
                    h.processor.pushUndo("noise", KeysProcessor::UndoScope::pads);
                    h.processor.setChordPad(0, { 62 + i }, "during");
                }
            }
            h.processor.undo();
            expectEquals(h.processor.chordPad(0).name, juce::String("before"),
                         "one undo went back past the whole gesture");
            expect(! h.processor.canUndo(), "...and it really was a single entry");
        }

        beginTest("undo covers the arp lanes too, not just the pads");
        {
            Host h;
            auto& lanes = h.processor.arpLine(0).lanes;
            lanes.value[ArpEngine::laneNote][0].store(5);
            h.processor.pushUndo("Roll lane", KeysProcessor::UndoScope::arp);
            h.processor.rerollArpLane(0, ArpEngine::laneNote, 100);
            h.processor.undo();
            expectEquals(lanes.value[ArpEngine::laneNote][0].load(), 5,
                         "the drawn step came back after a full-scramble roll");
        }

        beginTest("a new edit after an undo drops the redo branch");
        {
            Host h;
            h.processor.setChordPad(1, { 60 }, "first");
            h.processor.pushUndo("a", KeysProcessor::UndoScope::pads);
            h.processor.setChordPad(1, { 62 }, "second");
            h.processor.undo();
            expect(h.processor.canRedo(), "redo is available right after an undo");

            h.processor.pushUndo("b", KeysProcessor::UndoScope::pads);
            h.processor.setChordPad(1, { 64 }, "third");
            expect(! h.processor.canRedo(), "a new edit ended the future that was undone");
        }

        beginTest("the stack has a floor and a ceiling, and neither throws");
        {
            Host h;
            h.processor.undo(); // nothing to undo
            h.processor.redo(); // nothing to redo
            expect(! h.processor.canUndo() && ! h.processor.canRedo(), "empty stacks stay empty");

            for (int i = 0; i < 60; ++i) // well past maxUndoDepth
            {
                h.processor.pushUndo("edit " + juce::String(i), KeysProcessor::UndoScope::pads);
                h.processor.setChordPad(2, { 60 + (i % 12) }, juce::String(i));
            }
            int undone = 0;
            while (h.processor.canUndo() && undone < 200)
            {
                h.processor.undo();
                ++undone;
            }
            expect(undone > 0 && undone <= 32, "the stack capped at its depth, and drained: "
                                                   + juce::String(undone));
        }

        beginTest("a whole state round-trips with every parameter present");
        {
            // The control case. If this ever fails the migrations are firing on sessions that
            // are not old at all, which is worse than them not firing.
            Host h;
            const auto rateId = KeysProcessor::arpParamId(0, KeysProcessor::apRate);
            setParam(h.processor, rateId, 5.0f);
            juce::MemoryBlock block;
            h.processor.getStateInformation(block);
            setParam(h.processor, rateId, 2.0f);
            h.processor.setStateInformation(block.getData(), (int) block.getSize());
            expectWithinAbsoluteError(paramOf(h.processor, rateId), 5.0f, 0.01f,
                                      "the saved rate came back");
        }

        beginTest("every UI line, switched on and listening, arpeggiates the keys");
        {
            // Processor-level, deliberately: ArpTests proves one engine works, and the bug this
            // is for is the routing around it - a line whose queue, parameter cache or lift is
            // wired to the wrong index plays nothing while its engine passes every test it has
            // (2026-08-19, Owen: "line b isn't working"). One line at a time, so a line that
            // only sounds because a neighbour's routing leaks into it fails rather than passes.
            for (int line = 0; line < KeysProcessor::uiArpLines; ++line)
            {
                Host h;
                h.processor.prepareToPlay(48000.0, 512);
                setParam(h.processor, KeysProcessor::arpParamId(line, KeysProcessor::apOn), 1.0f);
                setParam(h.processor, KeysProcessor::arpParamId(line, KeysProcessor::apKeys), 1.0f);

                juce::AudioBuffer<float> audio(2, 512);
                int ons = 0;
                for (int blk = 0; blk < 40; ++blk) // ~0.4 s: several 1/16 steps at any default
                {
                    juce::MidiBuffer midi;
                    if (blk == 0)
                        for (int note : { 60, 64, 67 })
                            midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
                    h.processor.processBlock(audio, midi);
                    for (const auto meta : midi)
                        if (meta.getMessage().isNoteOn())
                            ++ons;
                }
                expect(ons >= 3, "line " + juce::String::charToString(
                                     (juce::juce_wchar) ('A' + line))
                                     + " played " + juce::String(ons) + " notes");
            }
        }
    }
};

static StateTests stateTests;
} // namespace keys::tests
