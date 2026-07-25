#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ScaleModes.h"
#include "mcp/KeysMcp.h"
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
    // Default 49 keys (was 61): at the default window a 61-key bed leaves ~24 px per
    // white key, too narrow to click accurately mouse-only. 49 keys ≈ 30 px whites;
    // the Size combo still goes up to 88 when range matters more than width. A
    // default change only, not a layout change: saved sessions keep their value.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "size", 1 }, "Keyboard Size", sizeNames(), 1));
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

    // Retained for session compatibility only: Keys went from five tabbed surfaces
    // (Keys/Hex/Pads/Faders/XY) to one compile-time-selected playing surface plus the
    // knob row below (CHANGELOG). Nothing in the UI reads these three any more, but
    // dropping them would break older saved sessions that carry them.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "surface", 1 }, "Surface",
                                                      juce::StringArray { "Keys", "Hex", "Pads", "Faders", "XY" }, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "padChannel", 1 }, "Pad Grid Channel",
                                                      channelNames(), 9));

    // Knob-row CC assignments (Octavium defaults: Mod, Volume, Cutoff, Pan, Resonance,
    // Attack, Expression, Reverb). Values are transient performance state like the
    // wheels; only the assignments persist. The parameter ids (faderCC*) predate the
    // knob row and are unchanged so old sessions still bind to the right controller.
    static constexpr int faderDefaults[8] = { 1, 7, 74, 10, 71, 73, 11, 91 };
    for (int i = 0; i < 8; ++i)
        layout.add(std::make_unique<AudioParameterInt>(ParameterID { "faderCC" + juce::String(i + 1), 1 },
                                                       "Fader " + juce::String(i + 1) + " CC",
                                                       0, 127, faderDefaults[i]));
    // Retained for session compatibility only, same as surface/padChannel above: the
    // XY pad they belonged to is gone.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "xyCCX", 1 }, "XY Pad CC X", 0, 127, 1));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "xyCCY", 1 }, "XY Pad CC Y", 0, 127, 74));

    // Second chord-generator source: Markov walks bundled progression tables instead of
    // weighting a candidate pool. Its settings only apply when the source is Markov.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "genSource", 1 }, "Generator Source",
                                                      juce::StringArray { "Algorithmic", "Markov" }, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "markovMode", 1 }, "Markov Mode",
                                                      juce::StringArray { "Major", "Minor", "Modal" }, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { "markovTemp", 1 }, "Markov Temperature",
                                                     NormalisableRange<float>(0.3f, 2.0f, 0.01f), 1.0f));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "markovLength", 1 }, "Markov Length", 4, 16, 4));

    // Retained for session compatibility only, same as surface/padChannel/xyCC*
    // above: Keys is a single view now, so there is nothing left to switch between.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "uiLayout", 1 }, "Layout",
                                                      StringArray { "Classic", "Performer" }, 0));

    // Arpeggiator globals (docs/ARP_DESIGN.md). Dot/Trip are separate toggles rather
    // than entries in the rate list so automating the rate stays on even divisions
    // (Serum's documented rationale); Anchor picks bar-affixed vs free-running.
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "arpOn", 1 }, "Arp", false));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "arpRate", 1 }, "Arp Rate",
                                                      StringArray { "16 bars", "8 bars", "4 bars", "2 bars", "1 bar",
                                                                    "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" }, 8));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "arpDot", 1 }, "Arp Dotted", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "arpTrip", 1 }, "Arp Triplet", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "arpAnchor", 1 }, "Arp Anchor", true));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "arpDirection", 1 }, "Arp Direction",
                                                      StringArray { "Up", "Down", "Up-Down", "Down-Up",
                                                                    "Up & Down", "Down & Up", "As Played", "Reversed" }, 0));
    // Added after the arp shipped, both additive so an older session still loads: a
    // missing parameter falls back to its default here rather than shifting any
    // existing parameter's range. Note the default: arpPattern off means a session
    // that had per-step lane edits now plays as a plain shape until Shape is set back
    // to "Pattern". That is deliberate (the step grid was the confusing part) and is
    // called out in the changelog.
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "arpPattern", 1 }, "Arp Pattern", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "arpLinkLanes", 1 }, "Arp Link Lanes", true));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "arpOctaves", 1 }, "Arp Octaves", 1, 4, 1));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { "arpSwing", 1 }, "Arp Swing",
                                                     NormalisableRange<float>(0.0f, 0.75f, 0.01f), 0.0f));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "arpLatch", 1 }, "Arp Latch", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "arpRetrigger", 1 }, "Arp Retrigger", true));

    return layout;
}

KeysProcessor::KeysProcessor()
   #if defined(KEYS_MIDI_EFFECT) && KEYS_MIDI_EFFECT
    // MIDI-effect variant: JUCE requires a MIDI effect to declare no audio buses.
    : AudioProcessor(BusesProperties()),
   #else
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
   #endif
      apvts(*this, nullptr, "PARAMS", createLayout())
{
    // Last thing the constructor does: everything else this processor owns already
    // exists by the time the MCP bridge can be reached from another thread.
    mcpBridge = std::make_unique<KeysMcp>(*this);
}

KeysProcessor::~KeysProcessor()
{
    // Stop taking MCP calls before anything else tears down.
    mcpBridge.reset();
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

void KeysProcessor::noteOn(int midiNote, float velocity01, double delaySeconds, int channelOverride)
{
    if (midiNote < 0 || midiNote > 127)
        return;
    const int channel = (channelOverride >= 1 && channelOverride <= 16) ? channelOverride : midiChannel();

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

    auto m = juce::MidiMessage::noteOn(channel, midiNote, juce::jlimit(0.04f, 1.0f, velocity01));
    m.setTimeStamp(when);
    collector.addMessageToQueue(m);

    noteRefs[(size_t) midiNote].fetch_add(1);
    soundingGen.fetch_add(1);
}

void KeysProcessor::noteOff(int midiNote, int channelOverride, double delaySeconds)
{
    if (midiNote < 0 || midiNote > 127)
        return;
    const int channel = (channelOverride >= 1 && channelOverride <= 16) ? channelOverride : midiChannel();
    auto m = juce::MidiMessage::noteOff(channel, midiNote);
    m.setTimeStamp(nowSeconds() + delaySeconds);
    collector.addMessageToQueue(m);

    // Clamp at zero: a note-off with no matching note-on (panic, a pad released twice)
    // must not push the count negative and leave the key lit forever.
    auto& ref = noteRefs[(size_t) midiNote];
    int cur = ref.load();
    while (cur > 0 && ! ref.compare_exchange_weak(cur, cur - 1)) {}
    soundingGen.fetch_add(1);
}

bool KeysProcessor::isNoteSounding(int midiNote) const
{
    if (midiNote < 0 || midiNote > 127)
        return false;
    return noteRefs[(size_t) midiNote].load() > 0;
}

void KeysProcessor::allNotesOff()
{
    // Per-note offs for every note on every channel, then CC123 (All Notes Off) for
    // synths that prefer the controller form. Notes end through their normal release
    // envelopes. Octavium also sent CC120 (All Sound Off), but that chokes tails
    // that are still releasing, which reads as a glitch rather than a stop, so it
    // is deliberately gone.
    const double t = nowSeconds();
    for (int ch = 1; ch <= 16; ++ch)
    {
        for (int note = 0; note < 128; ++note)
        {
            auto off = juce::MidiMessage::noteOff(ch, note);
            off.setTimeStamp(t);
            collector.addMessageToQueue(off);
        }
        auto m = juce::MidiMessage::allNotesOff(ch);
        m.setTimeStamp(t);
        collector.addMessageToQueue(m);
    }

    for (auto& ref : noteRefs)
        ref.store(0);
    soundingGen.fetch_add(1);
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
    arp.prepare(sampleRate);
    arpScratch.ensureSize(8192);
}

bool KeysProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
   #if defined(KEYS_MIDI_EFFECT) && KEYS_MIDI_EFFECT
    juce::ignoreUnused(layouts); // no audio buses in the MIDI-effect variant
    return true;
   #else
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
   #endif
}

void KeysProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear(); // Keys makes no sound.

    // Drain queued UI note events into the outgoing buffer. Anything already on the
    // track's MIDI (a clip, another device) is left in place and passes through.
    collector.removeNextBlockOfMessages(midi, buffer.getNumSamples());

    // Arp stage: consumes the note stream, emits its own; CCs pass through. The
    // engine reads the host playhead (the one deliberate exception to Keys' old
    // never-reads-the-playhead rule; see docs/ARP_DESIGN.md) and free-runs on an
    // internal clock at the last-known tempo when the transport is stopped.
    const bool arpOn = apvts.getRawParameterValue("arpOn")->load() > 0.5f;
    if (arpOn != lastArpOn)
    {
        lastArpOn = arpOn;
        if (arpOn)
            arp.hardReset();
        else
            arp.flushInto(midi); // nothing may ring after bypassing
    }
    if (arpOn)
    {
        ArpEngine::Params ap;
        ap.enabled = true;
        ap.rateIndex = (int) apvts.getRawParameterValue("arpRate")->load();
        ap.dotted = apvts.getRawParameterValue("arpDot")->load() > 0.5f;
        ap.triplet = apvts.getRawParameterValue("arpTrip")->load() > 0.5f;
        ap.anchored = apvts.getRawParameterValue("arpAnchor")->load() > 0.5f;
        ap.direction = (ArpEngine::Direction) (int) apvts.getRawParameterValue("arpDirection")->load();
        ap.usePattern = apvts.getRawParameterValue("arpPattern")->load() > 0.5f;
        ap.octaveRange = (int) apvts.getRawParameterValue("arpOctaves")->load();
        ap.swing = apvts.getRawParameterValue("arpSwing")->load();
        ap.latch = apvts.getRawParameterValue("arpLatch")->load() > 0.5f;
        ap.retrigger = apvts.getRawParameterValue("arpRetrigger")->load() > 0.5f;

        ArpEngine::HostClock hc;
        if (auto* playHead = getPlayHead())
            if (auto pos = playHead->getPosition())
            {
                hc.playing = pos->getIsPlaying();
                if (auto bpm = pos->getBpm())
                {
                    hc.bpm = *bpm;
                    lastKnownBpm = *bpm;
                }
                if (auto ppq = pos->getPpqPosition())
                {
                    hc.ppq = *ppq;
                    hc.hasPpq = true;
                }
            }
        ap.fallbackBpm = lastKnownBpm;

        arpScratch.clear();
        arp.process(ap, hc, buffer.getNumSamples(), midi, arpScratch);
        midi.swapWith(arpScratch);
    }
}

juce::ValueTree KeysProcessor::chordPadsToTree() const
{
    juce::ValueTree pads { "chordPads" };
    // Written since pads went 16-a-page; loaders remap older 8-a-page slots by it.
    pads.setProperty("padsPerPage", padsPerPage, nullptr);
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
        if (p.numeral.isNotEmpty())
            pad.setProperty("numeral", p.numeral, nullptr);
        pads.appendChild(pad, nullptr);
    }
    return pads;
}

void KeysProcessor::chordPadsFromTree(const juce::ValueTree& root)
{
    stopAllChordPads();
    for (auto& p : chordPads)
        p = {};
    const auto pads = root.getChildWithName("chordPads");
    if (! pads.isValid())
        return;
    // Sessions saved when pages held 8 pads store slots in 8-a-page terms; keep each
    // pad on the page it was on by re-basing its slot into the current page width.
    const int savedPerPage = (int) pads.getProperty("padsPerPage", 8);
    for (int c = 0; c < pads.getNumChildren(); ++c)
    {
        const auto pad = pads.getChild(c);
        int slot = (int) pad.getProperty("slot", -1);
        if (slot >= 0 && savedPerPage > 0 && savedPerPage != padsPerPage)
            slot = (slot / savedPerPage) * padsPerPage + (slot % savedPerPage);
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
        p.numeral = pad.getProperty("numeral").toString(); // absent in pre-Markov sessions
        chordPads[(size_t) slot] = p;
    }
}

void KeysProcessor::storeActiveArpPattern()
{
    auto& pat = arpPatterns[(size_t) activeArpPattern];
    for (int l = 0; l < ArpEngine::numLanes; ++l)
    {
        for (int s = 0; s < ArpEngine::maxSteps; ++s)
            pat.value[(size_t) l][(size_t) s] = arp.lanes.value[(size_t) l][(size_t) s].load();
        pat.length[(size_t) l] = arp.lanes.length[(size_t) l].load();
        pat.clockDiv[(size_t) l] = arp.lanes.clockDiv[(size_t) l].load();
    }
}

void KeysProcessor::recallArpPattern(int index)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    storeActiveArpPattern();
    activeArpPattern = index;
    const auto& pat = arpPatterns[(size_t) index];
    for (int l = 0; l < ArpEngine::numLanes; ++l)
    {
        for (int s = 0; s < ArpEngine::maxSteps; ++s)
            arp.lanes.value[(size_t) l][(size_t) s].store(pat.value[(size_t) l][(size_t) s]);
        arp.lanes.length[(size_t) l].store(juce::jlimit(1, ArpEngine::maxSteps, pat.length[(size_t) l]));
        arp.lanes.clockDiv[(size_t) l].store(juce::jlimit(0, 2, pat.clockDiv[(size_t) l]));
    }
}

void KeysProcessor::copyArpPattern(int from, int to)
{
    if (from < 0 || from >= numArpPatterns || to < 0 || to >= numArpPatterns || from == to)
        return;
    storeActiveArpPattern();
    arpPatterns[(size_t) to] = arpPatterns[(size_t) from];
    if (to == activeArpPattern)
        recallArpPattern(to);
}

const KeysProcessor::ArpPattern& KeysProcessor::arpPatternSlot(int index) const
{
    static const ArpPattern empty {};
    if (index < 0 || index >= numArpPatterns)
        return empty;
    return arpPatterns[(size_t) index];
}

void KeysProcessor::setArpPatternSlot(int index, const ArpPattern& pattern)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    arpPatterns[(size_t) index] = pattern;
    if (index != activeArpPattern)
        return;
    // Refresh the live lanes from the slot just written. Not recallArpPattern(index):
    // that snapshots the live lanes into arpPatterns[activeArpPattern] first, which
    // would clobber the pattern we just wrote before reading it back.
    for (int l = 0; l < ArpEngine::numLanes; ++l)
    {
        for (int s = 0; s < ArpEngine::maxSteps; ++s)
            arp.lanes.value[(size_t) l][(size_t) s].store(pattern.value[(size_t) l][(size_t) s]);
        arp.lanes.length[(size_t) l].store(juce::jlimit(1, ArpEngine::maxSteps, pattern.length[(size_t) l]));
        arp.lanes.clockDiv[(size_t) l].store(juce::jlimit(0, 2, pattern.clockDiv[(size_t) l]));
    }
}

void KeysProcessor::randomizeActiveArpPattern()
{
    // Musical randomize, not white noise: mostly direction-following steps, gentle
    // octave jumps, occasional ratchets and rests.
    for (int s = 0; s < ArpEngine::maxSteps; ++s)
    {
        arp.lanes.value[ArpEngine::laneNote][(size_t) s].store(rng.nextInt(10) == 0 ? -1 : 0);
        arp.lanes.value[ArpEngine::laneOctave][(size_t) s].store(rng.nextInt(5) == 0 ? rng.nextInt(3) - 1 : 0);
        arp.lanes.value[ArpEngine::laneVelocity][(size_t) s].store(70 + rng.nextInt(60));
        arp.lanes.value[ArpEngine::laneGate][(size_t) s].store(40 + rng.nextInt(80));
        arp.lanes.value[ArpEngine::laneRatchet][(size_t) s].store(rng.nextInt(8) == 0 ? 2 : 1);
        arp.lanes.value[ArpEngine::laneProbability][(size_t) s].store(rng.nextInt(6) == 0 ? 60 : 100);
    }
}

juce::ValueTree KeysProcessor::layoutToTree() const
{
    juce::ValueTree tree { "layout" };
    tree.setProperty("controls", layout.controls, nullptr);
    tree.setProperty("centre", layout.centre, nullptr);
    tree.setProperty("knobs", layout.knobs, nullptr);
    tree.setProperty("pads", layout.pads, nullptr);
    tree.setProperty("wheels", layout.wheels, nullptr);
    tree.setProperty("keyboard", layout.keyboard, nullptr);
    tree.setProperty("detached", layout.detached, nullptr);
    tree.setProperty("view", layout.view, nullptr);
    tree.setProperty("accent", layout.accent, nullptr);
    tree.setProperty("detachedBounds", layout.detachedBounds.toString(), nullptr);
    return tree;
}

void KeysProcessor::layoutFromTree(const juce::ValueTree& root)
{
    const auto tree = root.getChildWithName("layout");
    if (! tree.isValid())
        return; // sessions from before folding sections: everything open, as it was
    const auto flag = [&tree](const char* id, bool fallback)
    { return (bool) tree.getProperty(id, fallback); };
    layout.controls = flag("controls", true);
    layout.centre = flag("centre", true);
    layout.knobs = flag("knobs", true);
    layout.pads = flag("pads", true);
    layout.wheels = flag("wheels", true);
    layout.keyboard = flag("keyboard", true);
    layout.detached = flag("detached", false);
    // "view" briefly carried a fourth value meaning "folded away" before the centre got
    // its own chevron like the other sections. Map it onto the flag it became.
    const int storedView = (int) tree.getProperty("view", 0);
    if (storedView == 3)
        layout.centre = false;
    layout.view = juce::jlimit(0, 2, storedView);
    layout.accent = juce::jlimit(0, 7, (int) tree.getProperty("accent", 0));

    const auto bounds = juce::Rectangle<int>::fromString(tree.getProperty("detachedBounds").toString());
    if (! bounds.isEmpty())
        layout.detachedBounds = bounds;
}

juce::ValueTree KeysProcessor::arpToTree() const
{
    // The live lanes are the active pattern; snapshot them so the tree is current.
    const_cast<KeysProcessor*>(this)->storeActiveArpPattern();
    juce::ValueTree tree { "arp" };
    tree.setProperty("active", activeArpPattern, nullptr);
    for (int pIndex = 0; pIndex < numArpPatterns; ++pIndex)
    {
        const auto& pat = arpPatterns[(size_t) pIndex];
        juce::ValueTree pt { "pattern" };
        pt.setProperty("index", pIndex, nullptr);
        for (int l = 0; l < ArpEngine::numLanes; ++l)
        {
            juce::StringArray vals;
            for (int s = 0; s < ArpEngine::maxSteps; ++s)
                vals.add(juce::String(pat.value[(size_t) l][(size_t) s]));
            juce::ValueTree lt { "lane" };
            lt.setProperty("index", l, nullptr);
            lt.setProperty("values", vals.joinIntoString(","), nullptr);
            lt.setProperty("length", pat.length[(size_t) l], nullptr);
            lt.setProperty("clockDiv", pat.clockDiv[(size_t) l], nullptr);
            pt.appendChild(lt, nullptr);
        }
        tree.appendChild(pt, nullptr);
    }
    return tree;
}

void KeysProcessor::arpFromTree(const juce::ValueTree& root)
{
    const auto tree = root.getChildWithName("arp");
    if (! tree.isValid())
        return; // sessions from before the arp: defaults stand
    for (int c = 0; c < tree.getNumChildren(); ++c)
    {
        const auto pt = tree.getChild(c);
        const int pIndex = (int) pt.getProperty("index", -1);
        if (pIndex < 0 || pIndex >= numArpPatterns)
            continue;
        auto& pat = arpPatterns[(size_t) pIndex];
        for (int lc = 0; lc < pt.getNumChildren(); ++lc)
        {
            const auto lt = pt.getChild(lc);
            const int l = (int) lt.getProperty("index", -1);
            if (l < 0 || l >= ArpEngine::numLanes)
                continue;
            const auto vals = juce::StringArray::fromTokens(lt.getProperty("values").toString(), ",", "");
            for (int s = 0; s < ArpEngine::maxSteps && s < vals.size(); ++s)
                pat.value[(size_t) l][(size_t) s] = vals[s].getIntValue();
            pat.length[(size_t) l] = (int) lt.getProperty("length", 8);
            pat.clockDiv[(size_t) l] = (int) lt.getProperty("clockDiv", 0);
        }
    }
    // Recall the active pattern by hand (recallArpPattern would first snapshot the
    // live lanes over the data we just loaded).
    activeArpPattern = juce::jlimit(0, numArpPatterns - 1, (int) tree.getProperty("active", 0));
    const auto& pat = arpPatterns[(size_t) activeArpPattern];
    for (int l = 0; l < ArpEngine::numLanes; ++l)
    {
        for (int s = 0; s < ArpEngine::maxSteps; ++s)
            arp.lanes.value[(size_t) l][(size_t) s].store(pat.value[(size_t) l][(size_t) s]);
        arp.lanes.length[(size_t) l].store(juce::jlimit(1, ArpEngine::maxSteps, pat.length[(size_t) l]));
        arp.lanes.clockDiv[(size_t) l].store(juce::jlimit(0, 2, pat.clockDiv[(size_t) l]));
    }
}

void KeysProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    okstudio::state::save(apvts, "KEYS", destData, { chordPadsToTree(), arpToTree(), layoutToTree() });
}

void KeysProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    okstudio::state::load(apvts, data, sizeInBytes, [this](const juce::ValueTree& root)
    {
        chordPadsFromTree(root);
        arpFromTree(root);
        layoutFromTree(root);
    });
}

juce::AudioProcessorEditor* KeysProcessor::createEditor()
{
    return new KeysEditor(*this);
}
} // namespace keys

// The Keys Host target compiles these same sources with KEYS_HOST=1 and provides its
// own createPluginFilter (returning KeysHostProcessor) in src/host.
#if ! (defined(KEYS_HOST) && KEYS_HOST)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new keys::KeysProcessor();
}
#endif
