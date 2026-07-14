#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <okstudio/Scales.h>
#include <okstudio/StateHelpers.h>
#include <cmath>
#include <utility>

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
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "humanizeVelMin", 1 }, "Velocity Min", 1, 127, 64));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "humanizeVelMax", 1 }, "Velocity Max", 1, 127, 88));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "humanizeTime", 1 }, "Timing Spread", 0, 30, 8));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "chordExclusive", 1 }, "Chord Exclusive", false));

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

float KeysProcessor::baseVelocity01() const
{
    const float v = apvts.getRawParameterValue("velocity")->load();
    const float pos = juce::jlimit(0.0f, 1.0f, (v - 1.0f) / 126.0f);
    switch ((int) apvts.getRawParameterValue("curve")->load())
    {
        case 0:  return std::pow(pos, 0.6f); // Soft: easier to reach high velocities
        case 2:  return std::pow(pos, 1.7f); // Hard: leans quiet until you push
        default: return pos;                 // Linear
    }
}

bool KeysProcessor::chordPadActive(int i) const
{
    return i >= 0 && i < numChordPads && ! chordPadOn[(size_t) i].empty();
}

void KeysProcessor::setChordPad(int i, const std::vector<int>& notes, const juce::String& name)
{
    if (i < 0 || i >= numChordPads)
        return;
    chordPads[(size_t) i] = { notes, name };
}

void KeysProcessor::clearChordPad(int i)
{
    if (i < 0 || i >= numChordPads)
        return;
    stopChordPad(i);
    chordPads[(size_t) i] = {};
}

void KeysProcessor::moveChordPad(int from, int to)
{
    if (from < 0 || from >= numChordPads || to < 0 || to >= numChordPads || from == to)
        return;
    stopChordPad(from);
    stopChordPad(to);
    std::swap(chordPads[(size_t) from], chordPads[(size_t) to]);
}

void KeysProcessor::stopChordPad(int i)
{
    if (i < 0 || i >= numChordPads)
        return;
    for (int n : chordPadOn[(size_t) i])
        noteOff(n);
    chordPadOn[(size_t) i].clear();
}

void KeysProcessor::stopAllChordPads()
{
    for (int i = 0; i < numChordPads; ++i)
        stopChordPad(i);
}

void KeysProcessor::toggleChordPad(int i)
{
    if (i < 0 || i >= numChordPads)
        return;
    const bool wasOn = ! chordPadOn[(size_t) i].empty();
    if (apvts.getRawParameterValue("chordExclusive")->load() > 0.5f)
        stopAllChordPads();      // choke every pad (incl. this one) before the new chord
    else if (wasOn)
    {
        stopChordPad(i);         // non-exclusive: clicking a sounding pad turns it off
        return;
    }
    if (wasOn)
        return;                  // exclusive path already turned this pad off

    const auto& notes = chordPads[(size_t) i].notes;
    if (notes.empty())
        return;
    const float vel = baseVelocity01();
    for (int n : notes)
        noteOn(n, vel);          // noteOn adds Humanize (per-note velocity + micro-timing)
    chordPadOn[(size_t) i] = notes;
}

void KeysProcessor::noteOn(int midiNote, float velocity01)
{
    if (midiNote < 0 || midiNote > 127)
        return;

    // Humanize (Octavium logic): pick a uniform-random velocity within the [min, max]
    // range per note, and nudge the note-on slightly late so simultaneous (latched or
    // dragged) notes stop landing perfectly quantized. Note-offs are never delayed, so
    // a note can never release before it has sounded.
    double when = nowSeconds();
    if (apvts.getRawParameterValue("humanize")->load() > 0.5f)
    {
        const int a = (int) apvts.getRawParameterValue("humanizeVelMin")->load();
        const int b = (int) apvts.getRawParameterValue("humanizeVelMax")->load();
        const int lo = juce::jmin(a, b), hi = juce::jmax(a, b);
        velocity01 = (float) rng.nextInt(juce::Range<int>(lo, hi + 1)) / 127.0f;

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
    juce::ValueTree pads { "chordPads" };
    for (int i = 0; i < numChordPads; ++i)
    {
        const auto& p = chordPads[(size_t) i];
        if (p.notes.empty())
            continue;
        juce::StringArray ns;
        for (int n : p.notes)
            ns.add(juce::String(n));
        juce::ValueTree pad { "pad" };
        pad.setProperty("slot", i, nullptr);
        pad.setProperty("notes", ns.joinIntoString(","), nullptr);
        pad.setProperty("name", p.name, nullptr);
        pads.appendChild(pad, nullptr);
    }
    okstudio::state::save(apvts, "KEYS", destData, { pads });
}

void KeysProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    okstudio::state::load(apvts, data, sizeInBytes, [this](const juce::ValueTree& root)
    {
        stopAllChordPads();
        for (auto& p : chordPads)
            p = {};
        const auto pads = root.getChildWithName("chordPads");
        if (! pads.isValid())
            return;
        for (int c = 0; c < pads.getNumChildren(); ++c)
        {
            const auto pad = pads.getChild(c);
            const int slot = (int) pad.getProperty("slot", -1);
            if (slot < 0 || slot >= numChordPads)
                continue;
            std::vector<int> notes;
            for (const auto& s : juce::StringArray::fromTokens(pad.getProperty("notes").toString(), ",", ""))
                if (s.trim().isNotEmpty())
                    notes.push_back(s.trim().getIntValue());
            chordPads[(size_t) slot] = { notes, pad.getProperty("name").toString() };
        }
    });
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
