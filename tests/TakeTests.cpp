// The take: Keys recording itself.
//
// This exists because Ableton cannot record a plugin's own MIDI onto that plugin's own track,
// so arming the Keys track and pressing record captures nothing (2026-08-17, Owen: "host in
// ableton does not record midi"). Keys keeps its own take instead - see
// KeysProcessor::setRecording.
//
// Everything here drives the *real* processBlock, because the whole claim being made is about
// what leaves it. Notes are fed in on the incoming MidiBuffer rather than through noteOn: the
// UI note path goes via a juce::MidiMessageCollector, which timestamps against the wall clock
// and would make these tests depend on how long the test runner took to get here. Keys passes
// its input through untouched, so an input note is on the outgoing stream at a sample position
// this file chose, which is what makes the timing assertions worth making at all.
//
// Nothing here writes a file. That is why stopping and saving are two calls on the processor
// (setRecording then writeTake) rather than one: a test of the capture must not leave takes in
// the user's Documents folder.

#include "../src/PluginProcessor.h"
#include <juce_events/juce_events.h>

namespace keys::tests
{
namespace
{
    // A processor plus the message loop its heartbeat timer and MCP bridge expect, prepared at
    // a sample rate that makes the arithmetic below readable: 480 samples is 10 ms.
    struct Host
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        KeysProcessor processor;

        static constexpr double sampleRate = 48000.0;
        static constexpr int blockSize = 480;

        Host() { processor.prepareToPlay(sampleRate, blockSize); }

        // One block, with whatever `midi` holds arriving on the input.
        void run(juce::MidiBuffer& midi)
        {
            juce::AudioBuffer<float> audio(2, blockSize);
            processor.processBlock(audio, midi);
        }

        void silentBlocks(int count)
        {
            for (int i = 0; i < count; ++i)
            {
                juce::MidiBuffer empty;
                run(empty);
            }
        }
    };

    // The take's first (and only) track, or an empty sequence if there is no take.
    juce::MidiMessageSequence takeTrack(KeysProcessor& p)
    {
        juce::MidiFile file;
        if (! p.buildTakeMidiFile(file) || file.getNumTracks() < 1)
            return {};
        return *file.getTrack(0);
    }

    int countNoteOns(const juce::MidiMessageSequence& seq)
    {
        int n = 0;
        for (int i = 0; i < seq.getNumEvents(); ++i)
            if (seq.getEventPointer(i)->message.isNoteOn())
                ++n;
        return n;
    }
}

class TakeTests : public juce::UnitTest
{
public:
    TakeTests() : juce::UnitTest("Keys take capture", "keys") {}

    void runTest() override
    {
        beginTest("a fresh processor has no take, and building one fails rather than throwing");
        {
            Host h;
            juce::MidiFile file;
            expect(! h.processor.buildTakeMidiFile(file));
            expect(! h.processor.isRecording());
            expectEquals(h.processor.capturedEventCount(), 0);
        }

        beginTest("nothing is captured until REC is pressed");
        {
            Host h;
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 240);
            h.run(midi);
            h.processor.setRecording(false); // drains; a no-op here, and must stay one
            expectEquals(h.processor.capturedEventCount(), 0);
        }

        beginTest("a note played while recording lands in the take as a matched pair");
        {
            Host h;
            h.processor.setRecording(true);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 240);
            h.run(midi);
            h.processor.setRecording(false);

            expectEquals(h.processor.capturedEventCount(), 2);
            auto seq = takeTrack(h.processor);
            expectEquals(countNoteOns(seq), 1);

            // The note-off has to be the one that was played, not one supplied at the end by
            // the hanging-note repair - so the pair is matched and the gap is the 240 samples
            // (5 ms) it was played with, which at the default 120 bpm is 5ms * 960 * 2 ticks.
            int noteOnIndex = -1;
            for (int i = 0; i < seq.getNumEvents(); ++i)
                if (seq.getEventPointer(i)->message.isNoteOn())
                    noteOnIndex = i;
            expect(noteOnIndex >= 0);
            expect(seq.getEventPointer(noteOnIndex)->noteOffObject != nullptr);
            const double heldTicks = seq.getTimeOfMatchingKeyUp(noteOnIndex)
                                   - seq.getEventPointer(noteOnIndex)->message.getTimeStamp();
            expectWithinAbsoluteError(heldTicks, 0.005 * 960.0 * (h.processor.currentTempo() / 60.0), 1.0);
        }

        beginTest("the take is trimmed to the first note, so arming early costs it nothing");
        {
            Host h;
            h.processor.setRecording(true);
            h.silentBlocks(100); // a full second of armed silence before anything is played
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8) 100), 0);
            midi.addEvent(juce::MidiMessage::noteOff(1, 64), 240);
            h.run(midi);
            h.processor.setRecording(false);

            auto seq = takeTrack(h.processor);
            for (int i = 0; i < seq.getNumEvents(); ++i)
            {
                const auto& m = seq.getEventPointer(i)->message;
                if (m.isNoteOn())
                    expectWithinAbsoluteError(m.getTimeStamp(), 0.0, 1.0);
            }
        }

        beginTest("a note still ringing when recording stops is given an end");
        {
            Host h;
            h.processor.setRecording(true);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8) 100), 0);
            h.run(midi); // no note-off, ever
            h.processor.setRecording(false);

            auto seq = takeTrack(h.processor);
            expectEquals(countNoteOns(seq), 1);
            for (int i = 0; i < seq.getNumEvents(); ++i)
                if (seq.getEventPointer(i)->message.isNoteOn())
                {
                    // Left alone this is a hanging note in the clip, which Live holds until the
                    // next stop and which reads as Keys emitting a stuck note.
                    expect(seq.getEventPointer(i)->noteOffObject != nullptr);
                    expect(seq.getTimeOfMatchingKeyUp(i)
                           > seq.getEventPointer(i)->message.getTimeStamp());
                }
        }

        beginTest("arming again starts a new take rather than appending to the last");
        {
            Host h;
            h.processor.setRecording(true);
            juce::MidiBuffer first;
            first.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            first.addEvent(juce::MidiMessage::noteOff(1, 60), 240);
            h.run(first);
            h.processor.setRecording(false);
            expectEquals(h.processor.capturedEventCount(), 2);

            h.processor.setRecording(true);
            expectEquals(h.processor.capturedEventCount(), 0);
            juce::MidiBuffer second;
            second.addEvent(juce::MidiMessage::noteOn(1, 72, (juce::uint8) 100), 0);
            second.addEvent(juce::MidiMessage::noteOff(1, 72), 240);
            h.run(second);
            h.processor.setRecording(false);

            expectEquals(h.processor.capturedEventCount(), 2);
            auto seq = takeTrack(h.processor);
            expectEquals(countNoteOns(seq), 1);
            for (int i = 0; i < seq.getNumEvents(); ++i)
                if (seq.getEventPointer(i)->message.isNoteOn())
                    expectEquals(seq.getEventPointer(i)->message.getNoteNumber(), 72);
        }

        beginTest("the take carries the tempo, so a clip lands on the grid it was played to");
        {
            Host h;
            if (auto* bpm = h.processor.apvts.getParameter("bpm"))
                bpm->setValueNotifyingHost(bpm->convertTo0to1(90.0f));
            h.silentBlocks(2); // advanceChainClock publishes the tempo the take will be built at

            h.processor.setRecording(true);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 240);
            h.run(midi);
            h.processor.setRecording(false);

            juce::MidiFile file;
            expect(h.processor.buildTakeMidiFile(file));
            expectEquals((int) file.getTimeFormat(), 960); // ticks per quarter, positive = not SMPTE

            bool sawTempo = false;
            auto seq = takeTrack(h.processor);
            for (int i = 0; i < seq.getNumEvents(); ++i)
                if (seq.getEventPointer(i)->message.isTempoMetaEvent())
                {
                    sawTempo = true;
                    expectWithinAbsoluteError(
                        60.0 / seq.getEventPointer(i)->message.getTempoSecondsPerQuarterNote(),
                        h.processor.currentTempo(), 0.5);
                }
            expect(sawTempo);
        }
    }
};

static TakeTests takeTests;
} // namespace keys::tests
