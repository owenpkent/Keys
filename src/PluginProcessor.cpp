#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <okstudio/Scales.h>
#include <okstudio/StateHelpers.h>

namespace keys
{
namespace
{
    juce::StringArray sizeNames() { return { "25 keys", "49 keys", "61 keys", "73 keys", "76 keys", "88 keys" }; }

    juce::StringArray channelNames()
    {
        juce::StringArray out;
        for (int i = 1; i <= 16; ++i)
            out.add(juce::String(i));
        return out;
    }

    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout KeysProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "root", 1 }, "Root",
                                                       okstudio::scales::noteNames(), 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "scale", 1 }, "Scale",
                                                       okstudio::scales::names(), 0));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "scaleLock", 1 }, "Scale Lock", false));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "octave", 1 }, "Octave", -3, 3, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "size", 1 }, "Keyboard Size", sizeNames(), 2));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "channel", 1 }, "MIDI Channel", channelNames(), 0));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "velocity", 1 }, "Velocity", 1, 127, 100));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "curve", 1 }, "Velocity Curve",
                                                      juce::StringArray { "Soft", "Linear", "Hard" }, 1));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "sustain", 1 }, "Sustain", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "latch", 1 }, "Latch", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "humanize", 1 }, "Humanize", false));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "humanizeVel", 1 }, "Velocity Randomize", 0, 100, 20));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "humanizeTime", 1 }, "Timing Spread", 0, 30, 8));

    return layout;
}

KeysProcessor::KeysProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout())
{
}

int KeysProcessor::midiChannel() const
{
    return (int) apvts.getRawParameterValue("channel")->load() + 1;
}

int KeysProcessor::octaveShift() const
{
    return (int) apvts.getRawParameterValue("octave")->load();
}

void KeysProcessor::noteOn(int midiNote, float velocity01)
{
    if (midiNote < 0 || midiNote > 127)
        return;

    // Humanize: per note, jitter the velocity and nudge the note-on slightly late so
    // simultaneous (latched/dragged) notes stop landing perfectly quantized. Note-offs
    // are never delayed, so a note can never release before it has sounded.
    double when = nowSeconds();
    if (apvts.getRawParameterValue("humanize")->load() > 0.5f)
    {
        const float velAmt = apvts.getRawParameterValue("humanizeVel")->load() * 0.01f; // 0..1
        if (velAmt > 0.0f)
            velocity01 += (rng.nextFloat() * 2.0f - 1.0f) * velAmt * 0.5f; // +/- up to half-scale

        const float spreadMs = apvts.getRawParameterValue("humanizeTime")->load();
        if (spreadMs > 0.0f)
            when += (double) (rng.nextFloat() * spreadMs) * 0.001; // 0..spread ms later
    }

    auto m = juce::MidiMessage::noteOn(midiChannel(), midiNote, juce::jlimit(0.04f, 1.0f, velocity01));
    m.setTimeStamp(when);
    collector.addMessageToQueue(m);
}

void KeysProcessor::noteOff(int midiNote)
{
    if (midiNote < 0 || midiNote > 127)
        return;
    auto m = juce::MidiMessage::noteOff(midiChannel(), midiNote);
    m.setTimeStamp(nowSeconds());
    collector.addMessageToQueue(m);
}

void KeysProcessor::allNotesOff()
{
    const double t = nowSeconds();
    for (int ch = 1; ch <= 16; ++ch)
    {
        auto m = juce::MidiMessage::allNotesOff(ch);
        m.setTimeStamp(t);
        collector.addMessageToQueue(m);
    }
}

void KeysProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    collector.reset(sampleRate);
}

bool KeysProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void KeysProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear(); // Keys makes no sound.

    // Drain queued UI note events into the outgoing buffer. Anything already on the
    // track's MIDI (a clip, another device) is left in place and passes through.
    collector.removeNextBlockOfMessages(midi, buffer.getNumSamples());
}

void KeysProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    okstudio::state::save(apvts, "KEYS", destData);
}

void KeysProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    okstudio::state::load(apvts, data, sizeInBytes);
}

juce::AudioProcessorEditor* KeysProcessor::createEditor()
{
    return new KeysEditor(*this);
}
} // namespace keys

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new keys::KeysProcessor();
}
