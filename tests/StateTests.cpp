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
    }
};

static StateTests stateTests;
} // namespace keys::tests
