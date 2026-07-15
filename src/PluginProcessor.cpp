#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ScaleModes.h"
#include <okstudio/Scales.h>
#include <okstudio/StateHelpers.h>
#include <algorithm>
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
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "octave", 1 }, "Octave", -5, 5, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "size", 1 }, "Keyboard Size", sizeNames(), 2));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "polyphony", 1 }, "Polyphony",
                                                      juce::StringArray { "Off", "1", "2", "3", "4", "5", "6", "7", "8" }, 0));
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
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "chordStrum", 1 }, "Chord Strum", 0, 200, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "chordStrumDir", 1 }, "Chord Strum Dir",
                                                      juce::StringArray { "Up", "Down", "Random" }, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "padPage", 1 }, "Chord Pad Page",
                                                      juce::StringArray { "1", "2", "3", "4" }, 0));

    // Chord generator settings. Key/mode are the generator's own, separate from the Root
    // and Scale that drive Scale Lock: those come from the kit's scale table, which has no
    // per-degree chord qualities to generate from. The emotion presets set both, so they
    // agree when it matters.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "genRoot", 1 }, "Generator Key",
                                                      okstudio::scales::noteNames(), 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "genMode", 1 }, "Generator Mode",
                                                      modes::names(), 0));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "genOctave", 1 }, "Generator Octave", 2, 6, 4));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "genTriads", 1 }, "Generate Triads", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "genSevenths", 1 }, "Generate 7ths", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "genNinths", 1 }, "Generate 9ths", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "genInv0", 1 }, "Inversion Root", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "genInv1", 1 }, "Inversion 1st", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "genInv2", 1 }, "Inversion 2nd", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "genInv3", 1 }, "Inversion 3rd", false));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "genCompliance", 1 }, "Scale Compliance", 0, 100, 100));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "genLockInfluence", 1 }, "Lock Influence", 0, 100, 50));

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

int KeysProcessor::polyphonyCap() const
{
    return (int) apvts.getRawParameterValue("polyphony")->load(); // 0 = unlimited
}

int KeysProcessor::padPage() const
{
    return juce::jlimit(0, numPadPages - 1, (int) apvts.getRawParameterValue("padPage")->load());
}

float KeysProcessor::curved(float pos01) const
{
    const float pos = juce::jlimit(0.0f, 1.0f, pos01);
    switch ((int) apvts.getRawParameterValue("curve")->load())
    {
        case 0:  return std::pow(pos, 0.6f); // Soft: easier to reach high velocities
        case 2:  return std::pow(pos, 1.7f); // Hard: leans quiet until you push
        default: return pos;                 // Linear
    }
}

float KeysProcessor::baseVelocity01() const
{
    const float v = apvts.getRawParameterValue("velocity")->load();
    return curved((v - 1.0f) / 126.0f);
}

bool KeysProcessor::chordPadActive(int i) const
{
    return i >= 0 && i < numChordPads && ! chordPadOn[(size_t) i].empty();
}

void KeysProcessor::setChordPad(int i, const std::vector<int>& notes, const juce::String& name)
{
    if (i < 0 || i >= numChordPads)
        return;
    // A hand-captured chord: keep the lock, drop any generator metadata the slot carried,
    // since the notes no longer come from that degree.
    const bool wasLocked = chordPads[(size_t) i].locked;
    chordPads[(size_t) i] = {};
    chordPads[(size_t) i].notes = notes;
    chordPads[(size_t) i].name = name;
    chordPads[(size_t) i].locked = wasLocked;
}

void KeysProcessor::setChordPad(int i, const ChordPad& pad)
{
    if (i < 0 || i >= numChordPads)
        return;
    chordPads[(size_t) i] = pad;
}

void KeysProcessor::clearChordPad(int i)
{
    if (i < 0 || i >= numChordPads)
        return;
    stopChordPad(i);
    chordPads[(size_t) i] = {};
}

void KeysProcessor::setChordPadLocked(int i, bool locked)
{
    if (i < 0 || i >= numChordPads)
        return;
    chordPads[(size_t) i].locked = locked;
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

void KeysProcessor::pressChordPad(int i)
{
    if (i < 0 || i >= numChordPads)
        return;
    if (chordPads[(size_t) i].notes.empty())
        return;

    if (apvts.getRawParameterValue("chordExclusive")->load() > 0.5f)
        stopAllChordPads();      // choke every pad before the new chord
    else
        stopChordPad(i);         // re-pressing a sounding pad re-triggers it

    // Honour the Voices cap. The keyboard steals oldest-first across its own notes; a pad
    // fires as one gesture, so there is no "oldest" within it — drop the highest notes and
    // keep the lowest, matching how the keyboard resolves a too-big simultaneous chord.
    // (The cap applies per source: a pad and the keyboard each fit under it separately.)
    std::vector<int> notes = chordPads[(size_t) i].notes;
    std::sort(notes.begin(), notes.end());
    const int cap = polyphonyCap();
    if (cap > 0 && (int) notes.size() > cap)
        notes.resize((size_t) cap);

    const float vel = baseVelocity01();

    // Strum (Octavium "Drift"): spread the note-ons over `chordStrum` ms in a direction.
    std::vector<int> order = notes; // low -> high
    const int dir = (int) apvts.getRawParameterValue("chordStrumDir")->load();
    if (dir == 1)
        std::reverse(order.begin(), order.end());              // Down: high -> low
    else if (dir == 2)
        for (int k = (int) order.size() - 1; k > 0; --k)       // Random: Fisher-Yates
            std::swap(order[(size_t) k], order[(size_t) rng.nextInt(k + 1)]);

    const double strumMs = apvts.getRawParameterValue("chordStrum")->load();
    const int count = (int) order.size();
    for (int k = 0; k < count; ++k)
    {
        const double delay = (count > 1 && strumMs > 0.0)
                                 ? (strumMs * (double) k / (double) (count - 1)) * 0.001
                                 : 0.0;
        noteOn(order[(size_t) k], vel, delay); // noteOn also adds Humanize per note
    }
    chordPadOn[(size_t) i] = notes;
}

void KeysProcessor::releaseChordPad(int i)
{
    if (i < 0 || i >= numChordPads)
        return;
    if (apvts.getRawParameterValue("sustain")->load() > 0.5f)
        return;                  // pedal down: leave the chord ringing until Sustain lifts
    stopChordPad(i);
}

void KeysProcessor::noteOn(int midiNote, float velocity01, double delaySeconds)
{
    if (midiNote < 0 || midiNote > 127)
        return;

    // Humanize (Octavium logic): pick a uniform-random velocity within the [min, max]
    // range per note, and nudge the note-on slightly late so simultaneous (latched or
    // dragged) notes stop landing perfectly quantized. Note-offs are never delayed, so
    // a note can never release before it has sounded. delaySeconds adds the strum offset.
    double when = nowSeconds() + delaySeconds;
    if (apvts.getRawParameterValue("humanize")->load() > 0.5f)
    {
        const int a = (int) apvts.getRawParameterValue("humanizeVelMin")->load();
        const int b = (int) apvts.getRawParameterValue("humanizeVelMax")->load();
        const int lo = juce::jmin(a, b), hi = juce::jmax(a, b);
        const int rnd = rng.nextInt(juce::Range<int>(lo, hi + 1));
        velocity01 = curved((float) (rnd - 1) / 126.0f); // same curve as the fixed velocity

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

void KeysProcessor::sendCC(int controller, int value)
{
    auto m = juce::MidiMessage::controllerEvent(midiChannel(), controller, juce::jlimit(0, 127, value));
    m.setTimeStamp(nowSeconds());
    collector.addMessageToQueue(m);
}

void KeysProcessor::sendPitchBend(int value14)
{
    auto m = juce::MidiMessage::pitchWheel(midiChannel(), juce::jlimit(0, 16383, value14));
    m.setTimeStamp(nowSeconds());
    collector.addMessageToQueue(m);
}

void KeysProcessor::prepareToPlay(double sampleRate, int)
{
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
        pad.setProperty("locked", p.locked, nullptr);
        pad.setProperty("rootPc", p.rootPc, nullptr);
        pad.setProperty("type", p.type, nullptr);
        pad.setProperty("degree", p.degree, nullptr);
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
            ChordPad p;
            for (const auto& s : juce::StringArray::fromTokens(pad.getProperty("notes").toString(), ",", ""))
                if (s.trim().isNotEmpty())
                    p.notes.push_back(s.trim().getIntValue());
            p.name = pad.getProperty("name").toString();
            p.locked = (bool) pad.getProperty("locked", false);
            p.rootPc = (int) pad.getProperty("rootPc", -1); // absent in pre-pages sessions
            p.type = (int) pad.getProperty("type", -1);
            p.degree = (int) pad.getProperty("degree", -1);
            chordPads[(size_t) slot] = p;
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
