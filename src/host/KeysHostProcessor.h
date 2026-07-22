#pragma once

#include "../PluginProcessor.h"
#include <array>
#include <functional>
#include <memory>

namespace keys
{
// Keys Host: the Keys keyboard plus one hosted instrument VST3, in one plugin on one
// track. Keys' own processBlock produces the MIDI (UI clicks via the collector) and
// clears the audio bus; this subclass then runs the hosted instrument on that MIDI
// and fills the bus with its audio. No instrument loaded behaves exactly like Keys.
//
// Threading contract: the audio thread only reads `instrument` and never allocates
// or locks. All loading/ejecting happens on the message thread bracketed by
// suspendProcessing(), so the swap is never concurrent with processBlock.
class KeysHostProcessor : public KeysProcessor,
                          public juce::ChangeBroadcaster
{
public:
    KeysHostProcessor();
    ~KeysHostProcessor() override;

    const juce::String getName() const override { return "Keys Host"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override;

    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Message thread only. Returns an error message, empty on success.
    juce::String loadInstrument(const juce::File& vst3File);
    void ejectInstrument();

    bool hasInstrument() const noexcept { return instrument != nullptr; }
    juce::AudioPluginInstance* instrumentInstance() noexcept { return instrument.get(); }
    juce::String instrumentName() const;
    juce::String lastError() const { return loadError; }

    // KeysProcessor hooks: drive the bound hosted-instrument parameter directly, and
    // report the bound parameter's name so the fader bank can label itself.
    void faderMoved(int faderIndex, float value01) override;
    juce::String faderTargetName(int faderIndex) const override;

    // The editor hosts the instrument's GUI, which must be torn down before the
    // instance it belongs to goes away. Called on the message thread immediately
    // before the instance is swapped or destroyed; the change broadcast follows
    // after the swap.
    std::function<void()> onInstrumentWillChange;

private:
    juce::String loadInstrumentInternal(const juce::File& vst3File, const juce::MemoryBlock* stateToApply);
    void attachInstrument(std::unique_ptr<juce::AudioPluginInstance>, const juce::File&,
                          const juce::MemoryBlock* stateToApply);
    void resizeHostBuffer(int samplesPerBlock);
    void assignFaderParams(); // message thread only; rebuilds the fader -> parameter bindings

    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<juce::AudioPluginInstance> instrument;
    juce::File instrumentFile;
    juce::String loadError;

    static constexpr int numFaders = 8;

    // Fader -> hosted-parameter bindings. Message-thread-only state: written only in
    // assignFaderParams (attach/eject/restore) and read only in faderMoved, both called
    // on the message thread. Deliberately not persisted; recomputed from the freshly
    // loaded instrument's parameter names every time, since a hosted plugin's own
    // parameter indices aren't something worth saving across sessions.
    std::array<int, numFaders> faderParamIndex { -1, -1, -1, -1, -1, -1, -1, -1 };
    std::array<juce::String, numFaders> faderParamName;

    // Scratch the instrument renders into; sized on prepare/load so processBlock can
    // hand the instrument its full channel count while our bus stays stereo.
    juce::AudioBuffer<float> hostBuffer;
    int instrumentOutChannels = 0;

    // The instrument gets a copy of the block's MIDI, not the track's buffer: some
    // synths clear the buffer they're given, and the track's MIDI out must always
    // carry the played notes so "MIDI From: Keys Host" can drive Ableton's own
    // instruments on other tracks. (Deliberate trade-off: a hosted plugin's own MIDI
    // output does not reach the track.)
    juce::MidiBuffer instrumentMidi;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysHostProcessor)
};
} // namespace keys
