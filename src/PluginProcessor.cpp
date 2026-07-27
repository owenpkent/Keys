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
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "sustain", 1 }, "Sustain", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "humanize", 1 }, "Humanize", false));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "humanizeVelMin", 1 }, "Velocity Min", 1, 127, 64));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "humanizeVelMax", 1 }, "Velocity Max", 1, 127, 88));
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
    // Velocity Curve, likewise retained but no longer read. It shaped the Velocity
    // slider's own constant, so it only ever remapped one fixed number to another - which
    // is what moving the slider does. Between it, the slider and the Humanize range there
    // were three overlapping ways to set velocity; this is the one that earned nothing.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "curve", 1 }, "Velocity Curve",
                                                      juce::StringArray { "Soft", "Linear", "Hard" }, 1));
    // Velocity and Latch, same story. The fixed Velocity slider only ever applied while
    // Humanize was off, so it and the Humanize range were one control in two costumes;
    // the range absorbed it (baseVelocity01). Latch-as-a-mode went once a left click
    // released a held note, which left right-click-to-hold as the only path worth having.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "velocity", 1 }, "Velocity", 1, 127, 100));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "latch", 1 }, "Latch", false));
    // Timing Spread, retained but no longer read. It rode the same broken path Strum did
    // (see scheduleNoteOn), and even fixed it earned nothing: a random 0-30 ms nudge is
    // inaudible on a single clicked note, and on a chord it is Strum's job, done better.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "humanizeTime", 1 }, "Timing Spread", 0, 30, 8));
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
    // Gate and Chance as globals as well as step lanes: the lanes only exist while Shape is
    // "Pattern", so on a plain shape there was no way to shorten the notes or thin the run
    // out. They multiply the lane value, so the defaults leave an edited pattern untouched.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "arpGate", 1 }, "Arp Gate", 5, 200, 100));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "arpChance", 1 }, "Arp Chance", 0, 100, 100));

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
    stopTimer();
    deferred.clear();
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

float KeysProcessor::baseVelocity01() const
{
    // The Humanize range *is* the velocity control. There used to be two: a fixed Velocity
    // slider that only applied while Humanize was off, and this range that only applied
    // while it was on - the same control wearing different clothes, which is what made
    // them feel redundant. Now Humanize on picks a random value inside the range per note
    // (see noteOn), and off plays its midpoint. Collapsing the band onto a single value
    // therefore gives a plain fixed velocity, which is exactly what the slider did.
    const float a = apvts.getRawParameterValue("humanizeVelMin")->load();
    const float b = apvts.getRawParameterValue("humanizeVelMax")->load();
    const float mid = (juce::jmin(a, b) + juce::jmax(a, b)) * 0.5f;
    return juce::jlimit(0.0f, 1.0f, (mid - 1.0f) / 126.0f);
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
    // If this card is the one feeding the arp, let its chord go with it. Left holding, it
    // rang on with no owner: the strip only draws the "feeding the arp" ring on a *filled*
    // pad, and only a filled pad accepts the click that releases it, so the chord became
    // unreachable except through All Off.
    if (i == arpPadSlot)
        releaseArpChord();
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
    // The "feeding the arp" ring belongs to the card, not to the slot it happens to sit in.
    if (arpPadSlot == from)
        arpPadSlot = to;
    else if (arpPadSlot == to)
        arpPadSlot = from;
}

void KeysProcessor::stopChordPad(int i)
{
    if (i < 0 || i >= numChordPads)
        return;
    releaseNotes(chordPadOn[(size_t) i], i);
}

void KeysProcessor::releaseNotes(std::vector<int>& sounding, int tag)
{
    // Drop what this source still has queued, and release the rest — but *only* the rest.
    //
    // `sounding` is what fireChord was asked to play, which during a strum is ahead of what
    // it has actually played: the notes still sitting in `deferred` never reached noteOn and
    // so own no reference to release. Calling noteOff for one of those takes a reference that
    // belongs to somebody else, and since a pitch now ends when its last owner lets go (see
    // noteOn), that emits a real note-off and silences another source's note while its key
    // stays lit. Releasing a pad mid-strum while the keybed held one of the same pitches did
    // exactly that - the failure this refcount rewrite exists to remove, arriving by a
    // different door.
    //
    // Matched one-for-one rather than by set membership, so a chord carrying the same pitch
    // twice still ends up with one note-off per reference actually taken.
    auto cancelled = cancelScheduledNotes(tag);
    for (int n : sounding)
    {
        const auto it = std::find(cancelled.begin(), cancelled.end(), n);
        if (it != cancelled.end())
            cancelled.erase(it); // never fired; there is no reference here to give back
        else
            noteOff(n);
    }
    sounding.clear();
}

void KeysProcessor::stopAllChordPads()
{
    for (int i = 0; i < numChordPads; ++i)
        stopChordPad(i);
    // Every chord source, not just the pads. Exclusive is a rule about *sources* - one chord
    // at a time, whichever surface started it - so it has to reach the live card and the
    // chord held into the arp as well, or a lit Exclusive quietly does nothing in one
    // direction and the chords pile up.
    releaseLiveChord(true);
    releaseArpChord();
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
    chordPadOn[(size_t) i] = fireChord(chordPads[(size_t) i].notes, i);
}

std::vector<int> KeysProcessor::fireChord(const std::vector<int>& source, int tag)
{
    // Honour the Voices cap. The keyboard steals oldest-first across its own notes; a chord
    // fires as one gesture, so there is no "oldest" within it — drop the highest notes and
    // keep the lowest, matching how the keyboard resolves a too-big simultaneous chord.
    // (The cap applies per source: a chord and the keyboard each fit under it separately.)
    std::vector<int> notes = source;
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
        // Spread across the whole strum time, first note now. Scheduled rather than
        // stamped: see scheduleNoteOn for why noteOn's own delay could not do this.
        const double delayMs = (count > 1 && strumMs > 0.0)
                                   ? strumMs * (double) k / (double) (count - 1)
                                   : 0.0;
        scheduleNoteOn(order[(size_t) k], vel, 0, delayMs, tag); // noteOn adds Humanize per note
    }
    return notes;
}

void KeysProcessor::pressLiveChord(const std::vector<int>& notes)
{
    // The live card fires the chord you are holding as one gesture, so you hear it the way
    // a pad would play it — strummed, humanized, capped — rather than as the sum of the
    // individual keys you happen to be holding down. Re-pressing re-triggers.
    if (notes.empty())
        return;
    releaseLiveChord(true);
    if (apvts.getRawParameterValue("chordExclusive")->load() > 0.5f)
        stopAllChordPads();
    liveChordOn = fireChord(notes, liveChordTag);
}

void KeysProcessor::releaseLiveChord(bool force)
{
    if (! force && apvts.getRawParameterValue("sustain")->load() > 0.5f)
        return; // pedal down: leave it ringing, same as a pad
    releaseNotes(liveChordOn, liveChordTag);
}

void KeysProcessor::scheduleNoteOn(int note, float vel01, int channel, double delayMs, int padSlot)
{
    if (delayMs <= 0.0)
    {
        noteOn(note, vel01, 0.0, channel);
        return;
    }

    const double at = juce::Time::getMillisecondCounterHiRes() + delayMs;
    const DeferredNote d { note, vel01, channel, at, padSlot };
    // Keep sorted by due time, so timerCallback only ever inspects the front.
    deferred.insert(std::upper_bound(deferred.begin(), deferred.end(), at,
                                     [](double t, const DeferredNote& n) { return t < n.atMs; }),
                    d);
    if (! isTimerRunning())
        startTimer(1); // 1 ms: a strum step can be as short as 200/8 = 25 ms
}

std::vector<int> KeysProcessor::cancelScheduledNotes(int padSlot)
{
    // A note-on that fires *after* its note-off is a stuck note that nothing clears, so
    // every path that stops sound has to drop what is still queued.
    //
    // `padSlot < 0` used to mean "everything", which was true only while -1 was the sole
    // negative tag. The live card (-2) and the chord held into the arp (-3) are sources like
    // any other, so releasing either wiped every other source's un-fired strum notes too: a
    // pad mid-strum lost the rest of its chord. Only the panic tag means all.
    //
    // Returns the pitches actually dropped, because a caller about to release the chord needs
    // to know which of its notes never sounded; see releaseNotes.
    std::vector<int> cancelled;
    const auto mine = [padSlot](const DeferredNote& n)
    { return padSlot == panicTag || n.padSlot == padSlot; };

    for (const auto& n : deferred)
        if (mine(n))
            cancelled.push_back(n.note);

    deferred.erase(std::remove_if(deferred.begin(), deferred.end(), mine), deferred.end());

    if (deferred.empty())
        stopTimer();
    return cancelled;
}

void KeysProcessor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    size_t due = 0;
    while (due < deferred.size() && deferred[due].atMs <= now)
        ++due;

    if (due > 0)
    {
        const std::vector<DeferredNote> firing(deferred.begin(), deferred.begin() + (long) due);
        deferred.erase(deferred.begin(), deferred.begin() + (long) due);
        for (const auto& n : firing)
            noteOn(n.note, n.vel01, 0.0, n.channel);
    }

    if (deferred.empty())
        stopTimer();
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
        velocity01 = (float) (rnd - 1) / 126.0f;
    }

    // One note-on per *pitch*, not per owner. Four sources can ask for the same pitch at
    // once (a pad, the live card, a chord held into the arp, and the keybed), and emitting a
    // second note-on for a pitch already sounding is what made Exclusive and Sustain look
    // broken: downstream, one note-off ends the pitch for everyone, so releasing whichever
    // owner happens to go first silenced the others while noteRefs still counted them - keys
    // lit on the keybed with nothing sounding. Worse, the arpeggiator sits downstream of this
    // stream and counts note-ons it never gets matching note-offs for, which leaked its held
    // set and left chords arpeggiating forever (see ArpEngine::noteArrived).
    //
    // Re-pressing a source that already owns the pitch still retriggers, because every such
    // path releases before it fires (pressChordPad -> stopChordPad, holdArpChord ->
    // releaseArpChord), taking the count to zero on the way.
    //
    // The count is per pitch, NOT per (pitch, channel): two sources holding one pitch on two
    // different channels collapse to a single note on whichever channel got there first, and
    // the eventual note-off goes out on whichever channel released last. Nothing on screen can
    // reach that today - no NoteSurface overrides noteChannel(), so everything here is on the
    // global channel - and the MCP bridge is the only caller that passes one (see
    // KeysMcp.cpp). Anything that gives a surface a channel of its own has to make noteRefs
    // per channel first, or it will silently eat the second source's notes.
    const bool alreadySounding = noteRefs[(size_t) midiNote].fetch_add(1) > 0;
    if (! alreadySounding)
    {
        auto m = juce::MidiMessage::noteOn(channel, midiNote, juce::jlimit(0.04f, 1.0f, velocity01));
        m.setTimeStamp(when);
        collector.addMessageToQueue(m);
    }
    soundingGen.fetch_add(1);
}

void KeysProcessor::noteOff(int midiNote, int channelOverride, double delaySeconds)
{
    if (midiNote < 0 || midiNote > 127)
        return;
    const int channel = (channelOverride >= 1 && channelOverride <= 16) ? channelOverride : midiChannel();

    // The other half of the ownership rule in noteOn: the pitch ends when the LAST owner
    // lets go, not the first. Clamp at zero, because a note-off with no matching note-on
    // (panic, a pad released twice) must not push the count negative and leave the key lit
    // forever - and must not emit a stray note-off either.
    auto& ref = noteRefs[(size_t) midiNote];
    int cur = ref.load();
    while (cur > 0 && ! ref.compare_exchange_weak(cur, cur - 1)) {}
    if (cur == 1) // this owner was the last one
    {
        auto m = juce::MidiMessage::noteOff(channel, midiNote);
        m.setTimeStamp(nowSeconds() + delaySeconds);
        collector.addMessageToQueue(m);
    }
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
    cancelScheduledNotes(panicTag); // nothing queued may fire after a panic
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

    // The chord held into the arp is the one thing here that outlives a note-off, so a
    // panic has to forget it too - otherwise All Off silences it while the launched slot
    // still paints as playing and the next launch tries to release notes already gone.
    arpChordOn.clear();
    arpChordName = {};
    launchedSlot = -1;
    arpPadSlot = -1;
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
        ap.gate = (int) apvts.getRawParameterValue("arpGate")->load();
        ap.chance = (int) apvts.getRawParameterValue("arpChance")->load();

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

void KeysProcessor::holdArpChord(const std::vector<int>& notes, const juce::String& name)
{
    releaseArpChord();
    if (notes.empty())
        return;
    // Exclusive works in both directions or it does not work: handing a card to the arp has
    // to choke a sounding pad exactly the way pressing a pad now chokes the arp hold.
    if (apvts.getRawParameterValue("chordExclusive")->load() > 0.5f)
        stopAllChordPads();
    arpChordName = name;
    arpChordOn = fireChord(notes, arpChordTag);
}

void KeysProcessor::releaseArpChord()
{
    // No Sustain check, unlike a pad: this chord is held on purpose until something
    // replaces it, so the pedal has nothing to say about when it stops.
    releaseNotes(arpChordOn, arpChordTag);
    arpChordName = {};
    launchedSlot = -1;
    arpPadSlot = -1;
}

void KeysProcessor::holdArpChordFromPad(int padSlot)
{
    if (padSlot < 0 || padSlot >= numChordPads)
        return;
    const auto& pad = chordPads[(size_t) padSlot];
    if (pad.notes.empty())
        return;
    holdArpChord(pad.notes, pad.name); // clears any previous holder, slot or pad
    arpPadSlot = padSlot;
}

void KeysProcessor::launchArpSlot(int index)
{
    if (index < 0 || index >= numArpPatterns)
        return;

    recallArpPattern(index); // snapshots the outgoing slot's lanes first
    const auto& slot = arpPatterns[(size_t) index];

    // Shape and Rate are ordinary parameters, so a launch has to move them through the
    // host the way the combo boxes do - otherwise automation and the UI disagree about
    // what is playing. Gestures bracket each one; see ArpPanel::applyShapeChoice.
    const auto setChoice = [this](const char* id, int value)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id)))
        {
            p->beginChangeGesture();
            *p = value;
            p->endChangeGesture();
        }
    };
    if (slot.shape >= 0)
    {
        if (auto* pat = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("arpPattern")))
        {
            pat->beginChangeGesture();
            *pat = slot.shape >= ArpEngine::numDirections;
            pat->endChangeGesture();
        }
        if (slot.shape < ArpEngine::numDirections)
            setChoice("arpDirection", slot.shape); // "Pattern" leaves the direction alone
    }
    if (slot.rate >= 0)
        setChoice("arpRate", slot.rate);

    // Hold last, so the chord starts against the pattern the slot just installed.
    if (! slot.chordNotes.empty())
        holdArpChord(slot.chordNotes, slot.chordName);
    else
        releaseArpChord(); // a pattern-only slot arpeggiates whatever you are already holding
    launchedSlot = index;
}

void KeysProcessor::stopArpSlot()
{
    releaseArpChord(); // clears launchedSlot too
}

void KeysProcessor::setArpSlotChord(int index, const std::vector<int>& notes, const juce::String& name)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    arpPatterns[(size_t) index].chordNotes = notes;
    arpPatterns[(size_t) index].chordName = name;
    // Capture the shape and rate that are up right now, so launching the slot brings the
    // whole sound back and the card can say what it will play. Nothing else ever wrote
    // these, so every slot painted "--" and a launch left Shape and Rate alone.
    const bool usePattern = apvts.getRawParameterValue("arpPattern")->load() > 0.5f;
    arpPatterns[(size_t) index].shape = usePattern
                                            ? ArpEngine::numDirections
                                            : (int) apvts.getRawParameterValue("arpDirection")->load();
    arpPatterns[(size_t) index].rate = (int) apvts.getRawParameterValue("arpRate")->load();
}

void KeysProcessor::clearArpSlotChord(int index)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    arpPatterns[(size_t) index].chordNotes.clear();
    arpPatterns[(size_t) index].chordName = {};
    if (launchedSlot == index)
        releaseArpChord();
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
    tree.setProperty("arp", layout.arp, nullptr);
    tree.setProperty("toArp", layout.toArp, nullptr);
    tree.setProperty("wheels", layout.wheels, nullptr);
    tree.setProperty("keyboard", layout.keyboard, nullptr);
    tree.setProperty("detached", layout.detached, nullptr);
    tree.setProperty("arpDetached", layout.arpDetached, nullptr);
    tree.setProperty("view", layout.view, nullptr);
    tree.setProperty("accent", layout.accent, nullptr);
    tree.setProperty("detachedBounds", layout.detachedBounds.toString(), nullptr);
    tree.setProperty("arpDetachedBounds", layout.arpDetachedBounds.toString(), nullptr);
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
    layout.arp = flag("arp", false);
    layout.toArp = flag("toArp", false);
    layout.wheels = flag("wheels", true);
    layout.keyboard = flag("keyboard", true);
    layout.detached = flag("detached", false);
    layout.arpDetached = flag("arpDetached", false);
    // "view" is 0 = Perform, 1 = Chords, and used to carry two more values that have each
    // since become a flag of their own. Both legacy values leave no centre view to restore -
    // they replaced it rather than sitting beside it - so both fall back to Perform, which
    // is also what an unreadable value gets.
    //   2, "Arp", before the arp became a section: open the arp section instead, which is
    //      the same thing the user was looking at.
    //   3, "folded away", before the centre got its own chevron like every other section.
    const int storedView = (int) tree.getProperty("view", 0);
    if (storedView == 2)
        layout.arp = true;
    else if (storedView == 3)
        layout.centre = false;
    layout.view = (storedView == 0 || storedView == 1) ? storedView : 0;
    layout.accent = juce::jlimit(0, 7, (int) tree.getProperty("accent", 0));

    const auto bounds = juce::Rectangle<int>::fromString(tree.getProperty("detachedBounds").toString());
    if (! bounds.isEmpty())
        layout.detachedBounds = bounds;
    const auto arpBounds = juce::Rectangle<int>::fromString(tree.getProperty("arpDetachedBounds").toString());
    if (! arpBounds.isEmpty())
        layout.arpDetachedBounds = arpBounds;
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
        // The chord a slot launches, alongside its lanes. Absent in sessions from before
        // the slots carried one, which reads back as a pattern-only slot.
        if (! pat.chordNotes.empty())
        {
            juce::StringArray notes;
            for (int n : pat.chordNotes)
                notes.add(juce::String(n));
            pt.setProperty("chord", notes.joinIntoString(","), nullptr);
            pt.setProperty("chordName", pat.chordName, nullptr);
        }
        pt.setProperty("shape", pat.shape, nullptr);
        pt.setProperty("rate", pat.rate, nullptr);
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
        pat.chordNotes.clear();
        for (const auto& n : juce::StringArray::fromTokens(pt.getProperty("chord").toString(), ",", ""))
            if (n.isNotEmpty())
                pat.chordNotes.push_back(juce::jlimit(0, 127, n.getIntValue()));
        pat.chordName = pt.getProperty("chordName").toString();
        pat.shape = (int) pt.getProperty("shape", -1);
        pat.rate = (int) pt.getProperty("rate", -1);
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
