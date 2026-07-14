#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <vector>

namespace keys
{
// Keys is a played instrument, not a generator: it makes no sound and emits no
// notes on its own. You click the on-screen piano; it sends MIDI note on/off to
// the track output, to drive whatever instrument sits downstream. All settings
// (size, scale-lock, octave, channel, velocity, sustain, latch) persist with the
// DAW session via the APVTS.
class KeysProcessor : public juce::AudioProcessor
{
public:
    KeysProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Keys"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Note I/O — called from the UI thread; queued and drained on the audio thread.
    void noteOn(int midiNote, float velocity01);
    void noteOff(int midiNote);
    void allNotesOff();

    // Live settings read by the editor / keyboard widget.
    int midiChannel() const;   // 1..16
    int octaveShift() const;   // -3..+3, in octaves
    float baseVelocity01() const; // velocity slider through the curve, 0..1 (Humanize aside)

    // Chord pads: capture a chord's notes into a slot, click to play/stop it. The pad
    // definitions persist with the session; playback goes through the same note path.
    struct ChordPad
    {
        std::vector<int> notes; // absolute MIDI notes captured for this pad
        juce::String name;      // detected label, e.g. "Cm7"
    };
    static constexpr int numChordPads = 8;

    const ChordPad& chordPad(int i) const { return chordPads[(size_t) i]; }
    bool chordPadActive(int i) const;
    void setChordPad(int i, const std::vector<int>& notes, const juce::String& name);
    void clearChordPad(int i);
    void moveChordPad(int from, int to); // swap two slots
    void toggleChordPad(int i);          // play if off / stop if on; honours Exclusive
    void stopAllChordPads();

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void stopChordPad(int i);

    juce::MidiMessageCollector collector; // thread-safe UI -> audio message queue
    double currentSampleRate = 44100.0;
    juce::Random rng; // humanize jitter; touched only on the message thread

    std::array<ChordPad, numChordPads> chordPads;          // captured pad definitions
    std::array<std::vector<int>, numChordPads> chordPadOn;  // notes currently sounding per pad

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysProcessor)
};
} // namespace keys
