#include "PluginProcessor.h"
#include "ChordSources.h" // the Progression picker's item list is that table's own
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
    // Strum is a *range*, like the humanize velocity beside it: each chord takes a random
    // spread between the two ends, so repeated stabs do not all rake at exactly the same
    // speed. "chordStrum" is the low end and keeps its old id, so a session saved with a
    // single strum value loads with that value as its minimum and nothing moves.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "chordStrum", 1 }, "Chord Strum", 0, 200, 0));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "chordStrumMax", 1 }, "Chord Strum Max", 0, 200, 0));
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
    // genTriads / genSevenths / genNinths were here until 2026-08-01. They picked which chord
    // *types* the weighted pool could draw from, by note count, and the three tick boxes that
    // drove them became the Notes range. They are deleted rather than left unread: a parameter no
    // control can reach but generation still obeys is the worst of both, and note count is now
    // decided after the fact by `fitVoicing` for every source rather than by type filtering for
    // one. An old session simply carries three entries nothing looks at, which APVTS ignores.
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
    // Velocity, same story. The fixed slider only ever applied while Humanize was off, so
    // it and the Humanize range were one control in two costumes; the range absorbed it
    // (baseVelocity01).
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "velocity", 1 }, "Velocity", 1, 127, 100));
    // Latch is read again (2026-07-30). It was briefly a retained-but-dead parameter, on the
    // reasoning that a left click on a held note releases it and so a whole mode earned
    // nothing; that made Sustain a per-note toggle, which is not what a pedal does. Sustain
    // restrikes now, Latch is the toggle, and each has a button on the Keyboard bar.
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
    // Seven brains now, and the five after Markov are `sources::` (2026-08-01). They are
    // **appended**, which is what makes this safe for a session saved before them: APVTS stores a
    // choice parameter's denormalised value, so a saved 1 is still Markov whatever the list grew
    // to. Never reorder or insert into this list - that is what would silently reopen a session
    // on the wrong brain, and there is no migration hook for it the way `migrateRateMode` covers
    // the arp's clock.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "genSource", 1 }, "Generator Source",
                                                      juce::StringArray { "Algorithmic", "Markov",
                                                                          "Circle of Fifths", "Neo-Riemannian",
                                                                          "Progressions", "Negative Harmony",
                                                                          "Planing" },
                                                      0));

    // Per-source settings. Each is dead under every source but its own, and the window hides it
    // rather than greying it: a band that means nothing to the brain that is up says so more
    // plainly by not being there (the same call applySource has always made for Markov).
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "genCircleDir", 1 }, "Circle Direction",
                                                      juce::StringArray { "Flat-ward (down a 5th)",
                                                                          "Sharp-ward (up a 5th)" },
                                                      0));
    // How often each Neo-Riemannian transform is taken. Relative weights rather than
    // probabilities, so all-zero is meaningless and the generator reads it as equal thirds.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "genPlrP", 1 }, "PLR Parallel", 0, 100, 34));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "genPlrL", 1 }, "PLR Leading-tone", 0, 100, 33));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "genPlrR", 1 }, "PLR Relative", 0, 100, 33));
    {
        // Straight off the table in ChordSources.h, so the picker and the generator can never
        // disagree about which progression index means what. Entry 0 is "Random".
        juce::StringArray names;
        for (const auto& n : sources::progressionNames())
            names.add(n);
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "genProgression", 1 },
                                                          "Progression", names, 0));
    }
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "genPlaningDiatonic", 1 },
                                                    "Planing Diatonic", true));
    // Voice leading is not a source: it is a pass over whatever a source produced, so it stays on
    // screen under all seven. 0 leaves every voicing alone, 100 always takes the smoothest.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "genSmooth", 1 }, "Voice Leading", 0, 100, 0));

    // How many notes a chord has, and which registers it may sit in. Both are **ranges**, both
    // are post-passes over whatever a source produced, and both therefore apply to all seven
    // (Owen, 2026-08-01: "all of their options should have the option for how many notes and what
    // inversion, and I'd actually like how many notes to go from two all the way up to 11, and an
    // octave range").
    //
    // Two to eleven, not the 3/4/5 tick boxes these replace. Below three you get dyads, which are
    // a real voicing and not a broken chord; above five the stack keeps climbing in thirds
    // through the scale, so eleven is a chord covering every degree and then some. `genOctave`
    // became a pair for the same reason: one octave puts sixteen chords in one register, and a
    // range lets a page breathe. Nothing enforces min <= max here, because a parameter cannot see
    // its sibling; the reader swaps them (see `noteCountRange` / `octaveRange`).
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "genNotesMin", 1 }, "Notes Min", 2, 11, 3));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "genNotesMax", 1 }, "Notes Max", 2, 11, 4));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "genOctaveMax", 1 }, "Octave Max", 2, 6, 4));

    // Lean the chords major or minor, whatever brain made them and whatever mode they are in.
    // Zero is neutral and means "leave every third alone", which is why this one needs no tick
    // box beside it: its off position is already a value on the dial.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "genMajMin", 1 }, "Major / Minor", -100, 100, 0));

    // The tick boxes (Owen, 2026-08-01: "check marks for the different sliders and options that
    // enable or disable them for the generation process"). Ticked, the setting constrains
    // generation; unticked, the generator is free and rolls that choice itself.
    //
    // Only the six settings where "free" is genuinely different from "zero" get one. Lock
    // Influence, Smooth Voicing and Major/Minor already have an off position on their own dial,
    // so a tick box beside them would be a second control for a thing the first one does.
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "genUseKey", 1 }, "Constrain Key", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "genUseMode", 1 }, "Constrain Mode", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "genUseOctave", 1 }, "Constrain Octave", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "genUseNotes", 1 }, "Constrain Note Count", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "genUseInversions", 1 }, "Constrain Inversions", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "genUseCompliance", 1 }, "Constrain Scale Compliance", true));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "markovMode", 1 }, "Markov Mode",
                                                      juce::StringArray { "Major", "Minor", "Modal" }, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { "markovTemp", 1 }, "Markov Temperature",
                                                     NormalisableRange<float>(0.3f, 2.0f, 0.01f), 1.0f));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "markovLength", 1 }, "Markov Length", 4, 16, 4));

    // Retained for session compatibility only, same as surface/padChannel/xyCC*
    // above: Keys is a single view now, so there is nothing left to switch between.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "uiLayout", 1 }, "Layout",
                                                      StringArray { "Classic", "Performer" }, 0));

    // Arpeggiator globals, three lines' worth (docs/ARP_DESIGN.md). See addArpLineParams.
    for (int line = 0; line < numArpLines; ++line)
        addArpLineParams(layout, line);

    // Launch Quantize, after Ableton's transport-bar Quantization (2026-08-01, Owen's ask:
    // "if you start a new note or something that goes into the next sequence, so it sounds good
    // always"). Off fires a chord the instant you click it, which is what Keys has always done.
    // Anything else holds the click until the next boundary, so a card can only ever land on
    // the grid - which is what makes three lines at three rates performable rather than a race
    // against your own mouse.
    //
    // **Global, not per line.** The whole value of it is that the three lines land *together*;
    // three separate quantize settings would be three ways to miss each other.
    //
    // It quantizes the gestures that *fire* something - a chord card, a slot launch, a drag
    // onto a line tab, a chain step - and never the keybed. Playing a note is playing an
    // instrument, and an instrument that waits half a bar before it sounds is broken.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "arpQuantize", 1 },
                                                      "Arp Launch Quantize",
                                                      StringArray { "Off", "1/16", "1/8", "1/4",
                                                                    "1/2", "1 Bar", "2 Bars" }, 0));

    // Tempo for everything that is timed in beats - which today is the arpeggiator alone.
    // A host that is *playing* always wins: this is what Keys runs at when there is no
    // transport to follow, which is every moment in the standalone and every stopped
    // transport in a DAW.
    //
    // Appended rather than slotted in beside the arp's other controls, to keep the shuffling
    // of this layout to a minimum. It is only a tidiness argument: Keys ships VST3 and
    // Standalone, and JUCE derives a VST3 parameter's id by hashing its string id, so saved
    // state and existing automation follow the id and not the position. What position still
    // decides is the order a host's generic parameter list shows - and chordStrumMax above
    // does insert mid-list, so this branch moves that order regardless.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "bpm", 1 }, "BPM", 40, 240, 120));

    return layout;
}

// One arpeggiator line's parameters. Dot/Trip are separate toggles rather than entries in the
// rate list so automating the rate stays on even divisions (Serum's documented rationale);
// Anchor picks bar-affixed vs free-running.
//
// Called three times (2026-08-01, the polyrhythm lines). Line 0 registers under exactly the
// ids and names it always has - "arpRate", "Arp Rate" - so every saved session, every
// automation lane and every MCP script still lands on the arpeggiator it was written for; B
// and C are this same list again under "arp2Rate" / "arp3Rate", named "Arp B" / "Arp C".
//
// One function rather than three copies, so a control cannot exist on one line and not
// another and the ranges and defaults are provably identical. Order inside it matters as much
// as it ever did: these are appended, never inserted, and the choice parameters among them
// (Rate, Direction, Distance, Retrigger Every) store a plain index.
void KeysProcessor::addArpLineParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int line)
{
    using namespace juce;

    const auto id = [line](const char* suffix) { return arpParamId(line, suffix); };
    const String nm = line == 0 ? String("Arp")
                                : "Arp " + String::charToString((juce_wchar) ('A' + line));

    layout.add(std::make_unique<AudioParameterBool>(ParameterID { id("On"), 1 }, nm, false));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id("Rate"), 1 }, nm + " Rate",
                                                      StringArray { "16 bars", "8 bars", "4 bars", "2 bars", "1 bar",
                                                                    "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" }, 8));
    // The same rate as a free-running frequency, and a switch between the two. Both added
    // 2026-07-30, both additive: the choice list above is byte-identical, so every saved
    // session and every automation lane still means what it did, and a session from before
    // these two loads with Free off - which is exactly today's behaviour.
    //
    // The range is not a round number by choice. It is what the eleven divisions above span
    // at 120 bpm: "1/64" is 32 steps a second, "16 bars" is one step per 32 seconds. Anything
    // narrower would make Hz reach less than the list beside it. Those ends are ten octaves
    // apart, so a linear dial would spend nine tenths of its travel between 3 and 32 Hz and
    // put everything from "1 bar" down inside the last two degrees.
    //
    // The mapping is therefore exponential outright, not a skew: value = lo * (hi/lo)^t, so
    // every octave gets exactly a tenth of the travel and one degree of the dial is the same
    // *ratio* wherever you are on it. A skew is a power law, which is a different curve with
    // a superficially similar shape - setSkewForCentre(1.0f) on these ends works out at ~0.198
    // and spends 25.3% of the dial between 0.031 and 0.062 Hz against 12.9% between 16 and 32,
    // so adjacent octaves came out four times apart and a quarter of the whole control was the
    // gap between its two slowest settings. 1 Hz still lands at the centre (it is the
    // geometric mean of the ends, and "1/2" at 120 bpm), now as a consequence rather than as
    // the one point the curve was fitted through.
    //
    // Default 8 Hz = 1/16 at 120 bpm, matching arpRate's own default, so flipping the switch
    // at a default tempo changes the sound not at all - only what the rate is tied to.
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { id("RateFree"), 1 }, nm + " Rate Free", false));
    {
        // JUCE clamps the proportion going in and the result coming out, so these two only
        // have to guard the log against a value at or below zero.
        NormalisableRange<float> hzRange {
            (float) ArpEngine::minRateHz, (float) ArpEngine::maxRateHz,
            [](float lo, float hi, float t) { return lo * std::pow(hi / lo, t); },
            [](float lo, float hi, float v) { return std::log(jlimit(lo, hi, v) / lo) / std::log(hi / lo); }
        };
        layout.add(std::make_unique<AudioParameterFloat>(
            ParameterID { id("RateHz"), 1 }, nm + " Rate Hz", hzRange, 8.0f,
            AudioParameterFloatAttributes()
                // No .withLabel("Hz"): the suffix is already in the text below, and a host
                // that renders value-plus-unit printed "8.00 Hz Hz". The in-plugin readout is
                // this string, so the suffix has to stay in it rather than move to the label.
                // Decimals by decade, one copy of the rule (see ArpEngine::rateHzText): 0.031
                // and 32.0 both have to read as themselves, and a fixed 2 would print the
                // bottom of the range as "0.03" for a whole octave of the dial.
                .withStringFromValueFunction([](float v, int) {
                    return ArpEngine::rateHzText(v) + " Hz";
                })
                .withValueFromStringFunction([](const String& s) { return s.getFloatValue(); })));
    }
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { id("Dot"), 1 }, nm + " Dotted", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { id("Trip"), 1 }, nm + " Triplet", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { id("Anchor"), 1 }, nm + " Anchor", true));
    // The four shapes after "Reversed" were appended in 2026-07-30 and had to go on the end:
    // this is a choice parameter, and inserting anywhere else renumbers what every saved
    // session and automation lane already holds. The Shape combo lists them in this order and
    // puts "Pattern" last, which is a display decision the panel makes, not this list.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id("Direction"), 1 }, nm + " Direction",
                                                      StringArray { "Up", "Down", "Up-Down", "Down-Up",
                                                                    "Up & Down", "Down & Up", "As Played", "Reversed",
                                                                    "Random", "Random Other", "Random Once", "Chord" }, 0));
    // Added after the arp shipped, both additive so an older session still loads: a
    // missing parameter falls back to its default here rather than shifting any
    // existing parameter's range. Note the default: arpPattern off means a session
    // that had per-step lane edits now plays as a plain shape until Shape is set back
    // to "Pattern". That is deliberate (the step grid was the confusing part) and is
    // called out in the changelog.
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { id("Pattern"), 1 }, nm + " Pattern", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { id("LinkLanes"), 1 }, nm + " Link Lanes", true));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("Octaves"), 1 }, nm + " Octaves", 1, 4, 1));
    // Swing goes both ways from a centred zero (2026-07-27, Owen's ask). Positive delays the
    // offbeats, the shuffle everyone means by "swing"; negative pulls them early, which is
    // the rushed, on-top-of-the-beat feel you cannot get by delaying anything. The stored
    // value is absolute, not normalised, so a session saved on the old 0..0.75 range keeps
    // exactly the swing it had.
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id("Swing"), 1 }, nm + " Swing",
                                                     NormalisableRange<float>(-0.75f, 0.75f, 0.01f), 0.0f));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { id("Latch"), 1 }, nm + " Latch", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { id("Retrigger"), 1 }, nm + " Retrigger", true));
    // Gate and Chance as globals as well as step lanes: the lanes only exist while Shape is
    // "Pattern", so on a plain shape there was no way to shorten the notes or thin the run
    // out. They multiply the lane value, so the defaults leave an edited pattern untouched.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("Gate"), 1 }, nm + " Gate", 5, 200, 100));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("Chance"), 1 }, nm + " Chance", 0, 100, 100));

    // The 2026-07-30 expansion, all appended and all defaulting to what the arp did before
    // them, so an older session sounds identical until one of them is moved.
    //
    // Distance is Octaves' other half: Octaves says how many times the chord repeats up the
    // keyboard, Distance says how far each repeat goes. It defaulted to an octave forever
    // because that was hardcoded. The scale-relative entries are the ones no stock arp has -
    // "a third" that follows Root and Scale rather than a fixed three or four semitones.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id("Distance"), 1 }, nm + " Distance",
                                                      StringArray { "Octave", "5th", "4th", "Maj 3rd", "min 3rd",
                                                                    "Scale 2nd", "Scale 3rd", "Scale 5th", "Scale 7th" }, 0));
    // Where the pattern starts. Rotates the lane reads and the direction walk together, so
    // "the same run, heard from its third note" is one control rather than a re-drawn lane.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("Offset"), 1 }, nm + " Offset", 0, 31, 0));
    // Retrigger's other half, after Ableton: the toggle restarts on a new chord, this
    // restarts on the clock, so a 5-step lane can still land on the bar.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id("RetrigBars"), 1 }, nm + " Retrigger Every",
                                                      StringArray { "Off", "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars" }, 0));
    // Velocity ramp: over Ramp Time from the moment a chord starts, velocity scales toward
    // (100 + Ramp)%. Negative fades a held chord out, positive swells it.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("VelRamp"), 1 }, nm + " Velocity Ramp", -100, 100, 0));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("RampBeats"), 1 }, nm + " Ramp Time", 1, 32, 8));
    // One knob of "played, not programmed": nudges each hit late and takes a little off its
    // velocity. The arp has never been humanized - Humanize proper lives in noteOn, which the
    // arp's own notes never pass through - so this is the first thing that touches its feel.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("Humanize"), 1 }, nm + " Humanize", 0, 100, 0));

    // The two the lines brought with them, and the only parameters an older session sees
    // appear on line 0. Both default to what Keys did before there were lines, so a session
    // saved without them opens sounding the same.
    //
    // Keys: does this line arpeggiate what you play, or only the chords you hand it? On for
    // all three, because a line you have just switched on doing nothing until you find a
    // second toggle is a line that reads as broken. Turn it off and that line becomes a
    // card player, which is what makes a chord card on B independent of the keybed on A.
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { id("Keys"), 1 }, nm + " Keys", true));
    // Where this line's notes go. Global is the old behaviour and the one to keep for a
    // single instrument downstream; naming a channel is for a multitimbral rack, where the
    // three lines can drive three different sounds. Note this buys nothing in Keys Host until
    // the hosted instrument is itself multitimbral - it is for the DAW case.
    {
        StringArray channels { "Global" };
        for (int ch = 1; ch <= 16; ++ch)
            channels.add(String(ch));
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id("Channel"), 1 },
                                                          nm + " Channel", channels, 0));
    }
}

// The id suffix of each per-line parameter, one table so the audio thread's cached pointers,
// the UI's attachments and createLayout's registrations cannot drift apart. These strings are
// the parameter ids: renaming one loses that setting out of every saved session.
const char* KeysProcessor::arpParamSuffix(int which)
{
    static const char* const suffixes[numArpParams] = {
        "On", "Rate", "RateFree", "RateHz", "Dot", "Trip", "Anchor", "Direction", "Pattern",
        "LinkLanes", "Octaves", "Swing", "Latch", "Retrigger", "Gate", "Chance", "Distance",
        "Offset", "RetrigBars", "VelRamp", "RampBeats", "Humanize", "Keys", "Channel"
    };
    return suffixes[(size_t) juce::jlimit(0, (int) numArpParams - 1, which)];
}

float KeysProcessor::arpParam(int line, ArpParam which) const
{
    const auto* p = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].param[(size_t) which];
    return p != nullptr ? p->load() : 0.0f;
}

// Line 0 is the arpeggiator Keys has always had, so its ids are the bare ones every saved
// session already carries. B and C take a digit after "arp" - "arp2Rate" - which is a name no
// earlier version ever wrote, so nothing collides and nothing has to be migrated.
juce::String KeysProcessor::arpParamId(int line, juce::StringRef suffix)
{
    const int n = juce::jlimit(0, numArpLines - 1, line);
    return n == 0 ? "arp" + juce::String(suffix)
                  : "arp" + juce::String(n + 1) + juce::String(suffix);
}

ArpEngine& KeysProcessor::arpLine(int line)
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].engine;
}

const ArpEngine& KeysProcessor::arpLine(int line) const
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].engine;
}

bool KeysProcessor::arpLineOn(int line) const
{
    return apvts.getRawParameterValue(arpParamId(line, "On"))->load() > 0.5f;
}

bool KeysProcessor::cardsFeedArp() const
{
    for (int n = 0; n < numArpLines; ++n)
        if (arpLineOn(n))
            return true;
    return false;
}

int KeysProcessor::arpCurrentLine() const
{
    return juce::jlimit(0, numArpLines - 1, layout.arpLine);
}

void KeysProcessor::setArpCurrentLine(int line)
{
    layout.arpLine = juce::jlimit(0, numArpLines - 1, line);
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
    // Resolve every line's parameters once. The audio thread reads through these pointers and
    // therefore never builds an id string or hashes one; see ArpParam.
    for (int n = 0; n < numArpLines; ++n)
        for (int p = 0; p < numArpParams; ++p)
        {
            lines[(size_t) n].param[(size_t) p] = apvts.getRawParameterValue(arpParamId(n, arpParamSuffix(p)));
            jassert(lines[(size_t) n].param[(size_t) p] != nullptr); // a suffix with no parameter behind it
        }

    // 50 Hz. It only ever notices things, so the rate is about how late a chord change is
    // allowed to be: 20 ms is comfortably inside a 1/16 step at any tempo anyone plays at,
    // and the arp itself stays anchored to the bar grid regardless.
    heartbeat.tick = [this] { heartbeatTick(); };
    heartbeat.startTimerHz(50);

    // Last thing the constructor does: everything else this processor owns already
    // exists by the time the MCP bridge can be reached from another thread.
    mcpBridge = std::make_unique<KeysMcp>(*this);
}

KeysProcessor::~KeysProcessor()
{
    // Stop taking MCP calls before anything else tears down.
    mcpBridge.reset();
    heartbeat.stopTimer();
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
    // Whichever line is holding it; a card can only be feeding one at a time.
    if (const int line = arpLineHoldingPad(i); line >= 0)
        releaseArpChord(line);
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
    // Both directions on every line: two lines can be holding the two cards being swapped.
    for (auto& ln : lines)
    {
        if (ln.padSlot == from)
            ln.padSlot = to;
        else if (ln.padSlot == to)
            ln.padSlot = from;
    }
}

void KeysProcessor::stopChordPad(int i)
{
    if (i < 0 || i >= numChordPads)
        return;
    releaseNotes(chordPadOn[(size_t) i], i);
}

void KeysProcessor::releaseNotes(std::vector<int>& sounding, int tag, int dest)
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
            noteOff(n, 0, 0.0, dest); // the stream its note-on went into, or the reference leaks
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
    for (int n = 0; n < numArpLines; ++n)
        releaseArpChord(n);
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

std::vector<int> KeysProcessor::fireChord(const std::vector<int>& source, int tag, int dest)
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

    // One spread per chord, not per note: a strum is a single rake, and re-rolling inside it
    // would scramble the order the direction just decided.
    const double strumLo = apvts.getRawParameterValue("chordStrum")->load();
    const double strumHi = apvts.getRawParameterValue("chordStrumMax")->load();
    const double strumMs = strumLo == strumHi
                               ? strumLo
                               : juce::jmin(strumLo, strumHi)
                                     + rng.nextDouble() * std::abs(strumHi - strumLo);
    const int count = (int) order.size();
    for (int k = 0; k < count; ++k)
    {
        // Spread across the whole strum time, first note now. Scheduled rather than
        // stamped: see scheduleNoteOn for why noteOn's own delay could not do this.
        const double delayMs = (count > 1 && strumMs > 0.0)
                                   ? strumMs * (double) k / (double) (count - 1)
                                   : 0.0;
        scheduleNoteOn(order[(size_t) k], vel, 0, delayMs, tag, dest); // noteOn adds Humanize per note
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

void KeysProcessor::scheduleNoteOn(int note, float vel01, int channel, double delayMs, int padSlot,
                                   int dest)
{
    if (delayMs <= 0.0)
    {
        noteOn(note, vel01, 0.0, channel, dest);
        return;
    }

    const double at = juce::Time::getMillisecondCounterHiRes() + delayMs;
    const DeferredNote d { note, vel01, channel, at, padSlot, dest };
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
    // Launch Quantize rides this timer rather than the 50 Hz heartbeat: a 20 ms tick is a sixth
    // of a 1/16 at 120 bpm, which is exactly the sloppiness this feature exists to remove.
    firePendingLaunches(now);
    size_t due = 0;
    while (due < deferred.size() && deferred[due].atMs <= now)
        ++due;

    if (due > 0)
    {
        const std::vector<DeferredNote> firing(deferred.begin(), deferred.begin() + (long) due);
        deferred.erase(deferred.begin(), deferred.begin() + (long) due);
        for (const auto& n : firing)
            noteOn(n.note, n.vel01, 0.0, n.channel, n.dest);
    }

    if (deferred.empty() && pendingLaunches.empty())
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

juce::MidiMessageCollector& KeysProcessor::collectorFor(int dest)
{
    if (dest >= 1 && dest <= numArpLines)
        return lines[(size_t) (dest - 1)].collector;
    return collector;
}

void KeysProcessor::noteOn(int midiNote, float velocity01, double delaySeconds, int channelOverride,
                           int dest)
{
    if (midiNote < 0 || midiNote > 127)
        return;
    dest = juce::jlimit(0, numArpLines, dest);
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
    //
    // The count is per *destination* since the arp lines arrived: the rule is about one
    // stream, and a pitch held into line B has nothing to say about the same pitch being
    // played to the track output. See the declaration of noteRefs.
    const bool alreadySounding = noteRefs[(size_t) dest][(size_t) midiNote].fetch_add(1) > 0;
    if (! alreadySounding)
    {
        auto m = juce::MidiMessage::noteOn(channel, midiNote, juce::jlimit(0.04f, 1.0f, velocity01));
        m.setTimeStamp(when);
        collectorFor(dest).addMessageToQueue(m);
    }
    soundingGen.fetch_add(1);
}

void KeysProcessor::noteOff(int midiNote, int channelOverride, double delaySeconds, int dest)
{
    if (midiNote < 0 || midiNote > 127)
        return;
    dest = juce::jlimit(0, numArpLines, dest);
    const int channel = (channelOverride >= 1 && channelOverride <= 16) ? channelOverride : midiChannel();

    // The other half of the ownership rule in noteOn: the pitch ends when the LAST owner
    // lets go, not the first. Clamp at zero, because a note-off with no matching note-on
    // (panic, a pad released twice) must not push the count negative and leave the key lit
    // forever - and must not emit a stray note-off either.
    auto& ref = noteRefs[(size_t) dest][(size_t) midiNote];
    int cur = ref.load();
    while (cur > 0 && ! ref.compare_exchange_weak(cur, cur - 1)) {}
    if (cur == 1) // this owner was the last one
    {
        auto m = juce::MidiMessage::noteOff(channel, midiNote);
        m.setTimeStamp(nowSeconds() + delaySeconds);
        collectorFor(dest).addMessageToQueue(m);
    }
    soundingGen.fetch_add(1);
}

bool KeysProcessor::isNoteSounding(int midiNote) const
{
    if (midiNote < 0 || midiNote > 127)
        return false;
    // Notes arriving on the input count as sounding for display: Keys passes them through
    // to the same instrument its own notes go to, so on screen they *are* sounding, and the
    // keybed lights them through exactly the path a chord pad's notes already take.
    //
    // Any destination, not only the track output: a chord held into an arp line is being
    // played, and the card that holds it and the keybed under it both have to say so. It is
    // the arp's own notes that are not counted here, and never have been - they never pass
    // through noteOn at all.
    for (const auto& dest : noteRefs)
        if (dest[(size_t) midiNote].load() > 0)
            return true;
    return inputNoteOn[(size_t) midiNote].load();
}

void KeysProcessor::watchInputNotes(const juce::MidiBuffer& midi)
{
    // Audio thread, and deliberately the first thing processBlock does: after the collector
    // drains, the buffer also holds the notes this plugin is playing, and lighting those
    // here would double-count what noteRefs already tracks.
    bool changed = false;
    const auto set = [&](int note, bool on)
    {
        if (note < 0 || note > 127 || inputNoteOn[(size_t) note].load() == on)
            return;
        inputNoteOn[(size_t) note].store(on);
        changed = true;
    };

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            set(m.getNoteNumber(), true);
        else if (m.isNoteOff())
            set(m.getNoteNumber(), false);
        else if (m.isAllNotesOff() || m.isAllSoundOff() || m.isResetAllControllers())
            for (int n = 0; n < 128; ++n)
                set(n, false);
    }

    if (changed)
        soundingGen.fetch_add(1); // the surface polls this and repaints only when it moves
}

void KeysProcessor::clearInputNotes()
{
    bool changed = false;
    for (auto& n : inputNoteOn)
        if (n.exchange(false))
            changed = true;
    if (changed)
        soundingGen.fetch_add(1);
}

std::vector<int> KeysProcessor::inputNotes() const
{
    std::vector<int> out;
    for (int n = 0; n < 128; ++n)
        if (inputNoteOn[(size_t) n].load())
            out.push_back(n);
    return out; // ascending by construction
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
    // Every stream, not only the track output. An arp line's input queue can be holding a
    // chord this panic is meant to end, and its engine only lets go when a note-off reaches
    // it - so a panic that skipped the line collectors would silence the output while three
    // engines carried on arpeggiating chords nothing could release.
    for (int dest = 0; dest <= numArpLines; ++dest)
    {
        auto& queue = collectorFor(dest);
        for (int ch = 1; ch <= 16; ++ch)
        {
            for (int note = 0; note < 128; ++note)
            {
                auto off = juce::MidiMessage::noteOff(ch, note);
                off.setTimeStamp(t);
                queue.addMessageToQueue(off);
            }
            auto m = juce::MidiMessage::allNotesOff(ch);
            m.setTimeStamp(t);
            queue.addMessageToQueue(m);
        }
    }

    for (auto& dest : noteRefs)
        for (auto& ref : dest)
            ref.store(0);
    soundingGen.fetch_add(1);

    // All Off clears the input lights too. Keys cannot make someone's physical keyboard let
    // go, but if a note-off went missing the lit key it left behind is exactly the kind of
    // stuck thing this button exists to clear; the next key they press lights again.
    clearInputNotes();

    // The chord held into the arp is the one thing here that outlives a note-off, so a
    // panic has to forget it too - otherwise All Off silences it while the launched slot
    // still paints as playing and the next launch tries to release notes already gone.
    // All three lines: a panic that left B holding is a panic that did not happen.
    for (auto& l : lines)
    {
        l.chordOn.clear();
        l.chordName = {};
        l.launchedSlot = -1;
        l.padSlot = -1;
    }
    // And anything Launch Quantize is still holding back. A panic that let a queued chord land
    // half a bar later would be a panic you have to press twice.
    pendingLaunches.clear();

    // And the Chain, for the same reason releaseArpHold() stops it: forgetting the chord is
    // only true until the next bar line, when heartbeatTick() launches the following slot and
    // the progression comes back out of a button whose whole job is silence. Last, not first,
    // so the clears above have already emptied every chordOn: stopChain() ends in
    // releaseArpChord(), which would otherwise emit note-offs for references the panic loop
    // has just zeroed.
    for (int n = 0; n < numArpLines; ++n)
        stopChain(n);
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
    // Every buffer the arp stage touches is sized here and never grown on the audio thread.
    // Seven of them now rather than one: three inputs, three outputs, and the keybed's notes
    // lifted out of the merged stream for the lines that listen to it.
    keyNotes.ensureSize(8192);
    streamRest.ensureSize(8192);
    for (auto& l : lines)
    {
        l.collector.reset(sampleRate);
        l.engine.prepare(sampleRate);
        l.in.ensureSize(8192);
        l.out.ensureSize(8192);
    }
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

    // Before anything is added to the buffer: note what came *in*, so the keybed can show
    // someone playing a physical keyboard (or a clip) through Keys. Display only.
    watchInputNotes(midi);

    // Drain queued UI note events into the outgoing buffer. Anything already on the
    // track's MIDI (a clip, another device) is left in place and passes through.
    collector.removeNextBlockOfMessages(midi, buffer.getNumSamples());

    // Arp stage: three lines, each consuming its own note stream and emitting its own; CCs
    // pass through. The engines read the host playhead (the one deliberate exception to Keys'
    // old never-reads-the-playhead rule; see docs/ARP_DESIGN.md) and free-run on an internal
    // clock at the last-known tempo when the transport is stopped.
    runArpLines(midi, buffer.getNumSamples());

    advanceChainClock(buffer.getNumSamples());
}

namespace
{
// Merge one arp line's notes into the outgoing stream, on the channel that line names. The
// restamp covers the *whole* buffer rather than the note-ons alone: a note that ends on a
// channel it never started on is a note that never ends, since the instrument downstream is
// matching pitch and channel. Channel 0 means "leave it alone", which is the global channel
// the notes already carry and by far the common case, so it costs a branch and no copy.
void mergeArpOut(juce::MidiBuffer& dest, const juce::MidiBuffer& src, int channel)
{
    if (channel <= 0)
    {
        dest.addEvents(src, 0, -1, 0);
        return;
    }
    for (const auto meta : src)
    {
        auto m = meta.getMessage();
        if (m.getChannel() > 0) // 0 = a message with no channel of its own; leave those be
            m.setChannel(juce::jlimit(1, 16, channel));
        dest.addEvent(m, meta.samplePosition);
    }
}
} // namespace

// Audio thread. One line's worth of engine parameters, read through the cached pointers so
// nothing here builds a string or takes a lock.
void KeysProcessor::runArpLines(juce::MidiBuffer& midi, int numSamples)
{
    // Which lines want the keys you play. A line that is off gets nothing: its input still
    // arrives (a chord held to it sustains, below), but there is no engine running to hand
    // the keybed to.
    bool anyListens = false;
    bool listens[numArpLines] = {};
    for (int n = 0; n < numArpLines; ++n)
    {
        listens[(size_t) n] = arpLineOn(n) && arpParam(n, apKeys) > 0.5f;
        anyListens = anyListens || listens[(size_t) n];
    }

    // Each line's own queue first. These are the chords handed to it - by a card, a slot or a
    // chain - and they belong to that line whether or not it is also listening to the keys.
    for (auto& l : lines)
    {
        l.in.clear();
        l.collector.removeNextBlockOfMessages(l.in, numSamples);
    }

    // The keybed, the live card, a pad played straight, a clip on the track: all of it arrives
    // in the merged stream, and any line that listens gets a copy. Lifting the notes out is
    // what makes the arp replace them rather than double them, and it is skipped entirely when
    // nobody is listening - which is exactly the behaviour of the arp being off.
    if (anyListens)
    {
        keyNotes.clear();
        streamRest.clear();
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn() || m.isNoteOff())
                keyNotes.addEvent(m, meta.samplePosition);
            else
                streamRest.addEvent(m, meta.samplePosition);
        }
        // Copied back rather than swapped. `swapWith` is the cheaper move and was what this did,
        // but it hands our `ensureSize`d storage to the host's buffer and keeps the host's in
        // `streamRest` - so the one buffer prepareToPlay sized for this is the one we give away
        // on the first block, and every block after that partitions into whatever capacity the
        // host happened to have. `clear()` does not shrink, so it settles, but "settles" is not
        // the guarantee the no-allocation-on-the-audio-thread rule asks for. The copy is of the
        // non-note events alone (CCs, bend, clock), which is a handful of bytes a block.
        midi.clear();
        midi.addEvents(streamRest, 0, -1, 0);
        for (int n = 0; n < numArpLines; ++n)
            if (listens[(size_t) n])
                lines[(size_t) n].in.addEvents(keyNotes, 0, numSamples, 0);
    }

    for (int n = 0; n < numArpLines; ++n)
    {
        auto& l = lines[(size_t) n];
        const bool arpOn = arpLineOn(n);
        l.out.clear();

        // 0 = the global channel, 1..16 = this line's own. Read once a block, like everything
        // else here, so a host automating it is seen at the next boundary.
        const int channel = (int) arpParam(n, apChannel);

        if (arpOn != l.lastOn)
        {
            l.lastOn = arpOn;
            if (arpOn)
                l.engine.hardReset();
            else
                l.engine.flushInto(l.out); // nothing may ring after bypassing
        }
        // A change of channel is a change of where the notes are going, so what is still
        // ringing has to be closed on the channel it started on. Flushed and merged under the
        // *old* channel before lastChannel moves, because restamping this line's whole output
        // with the new one would send those note-offs somewhere the notes never sounded.
        if (channel != l.lastChannel)
        {
            l.engine.flushInto(l.out);
            mergeArpOut(midi, l.out, l.lastChannel);
            l.out.clear();
            l.lastChannel = channel;
        }

        if (! arpOn)
        {
            // Off: whatever was handed to this line simply sustains, which is the honest
            // reading of holding a chord into an arpeggiator that is not running. Its own
            // queue passes straight through; it never saw the keybed.
            mergeArpOut(midi, l.in, channel);
            mergeArpOut(midi, l.out, channel);
            continue;
        }

        ArpEngine::Params ap;
        ap.enabled = true;
        ap.rateIndex = (int) arpParam(n, apRate);
        // Free: the rate is a frequency and the engine free-runs at it whatever the transport
        // is doing. Both read every block like every other arp global, so the mode can be
        // automated and the engine sees the change on the next boundary.
        ap.rateFree = arpParam(n, apRateFree) > 0.5f;
        ap.rateHz = (double) arpParam(n, apRateHz);
        ap.dotted = arpParam(n, apDot) > 0.5f;
        ap.triplet = arpParam(n, apTrip) > 0.5f;
        ap.anchored = arpParam(n, apAnchor) > 0.5f;
        ap.direction = (ArpEngine::Direction) (int) arpParam(n, apDirection);
        ap.usePattern = arpParam(n, apPattern) > 0.5f;
        ap.octaveRange = (int) arpParam(n, apOctaves);
        ap.swing = arpParam(n, apSwing);
        ap.latch = arpParam(n, apLatch) > 0.5f;
        ap.retrigger = arpParam(n, apRetrigger) > 0.5f;
        ap.gate = (int) arpParam(n, apGate);
        ap.chance = (int) arpParam(n, apChance);
        ap.offset = (int) arpParam(n, apOffset);
        ap.velRamp = (int) arpParam(n, apVelRamp);
        ap.rampBeats = (double) (int) arpParam(n, apRampBeats);
        ap.humanize = (int) arpParam(n, apHumanize);
        ap.chords = &l.chordTable; // what the Chord lane calls up, this line's slots

        // Distance: what each repeat past the first adds. The list names intervals rather
        // than numbers because "5th" is the thing you want and "+7 semitones" is the way you
        // would have had to ask for it; the scale-relative half of the list is the part no
        // stock arp offers, and it costs Keys nothing because Root and Scale are already here.
        {
            static constexpr int dist[]  = { 12, 7, 5, 4, 3, 1, 2, 4, 6 };
            static constexpr bool degs[] = { false, false, false, false, false, true, true, true, true };
            const int di = juce::jlimit(0, (int) std::size(dist) - 1,
                                        (int) arpParam(n, apDistance));
            ap.spread = dist[di];
            ap.spreadDegrees = degs[di];
        }
        // Restart every N beats, on top of the restart a new chord asks for.
        {
            static constexpr double bars[] = { 0.0, 1.0, 2.0, 4.0, 8.0, 16.0 };
            ap.retrigBeats = bars[juce::jlimit(0, (int) std::size(bars) - 1,
                                               (int) arpParam(n, apRetrigBars))];
        }
        // The scale, as a mask of pitch classes, for a Distance counted in scale degrees.
        // Built here rather than in the engine so ArpEngine.h stays free of the scale tables
        // and its tests can state a scale as a number.
        ap.rootPc = (int) apvts.getRawParameterValue("root")->load();
        {
            const int scaleIdx = (int) apvts.getRawParameterValue("scale")->load();
            unsigned int mask = 0;
            for (int k = 0; k < 12; ++k)
                if (okstudio::scales::isInScale(ap.rootPc + k, ap.rootPc, scaleIdx))
                    mask |= 1u << k;
            ap.scaleMask = mask != 0 ? mask : 0xFFFu; // an empty table would silence degree walking
        }

        ArpEngine::HostClock hc;
        if (auto* playHead = getPlayHead())
            if (auto pos = playHead->getPosition())
            {
                hc.playing = pos->getIsPlaying();
                if (auto bpm = pos->getBpm())
                    hc.bpm = *bpm;
                if (auto ppq = pos->getPpqPosition())
                {
                    hc.ppq = *ppq;
                    hc.hasPpq = true;
                }
            }
        // No transport to follow: run at the BPM control in the Controls section. It used to
        // be the host's last-known tempo, which was unreachable in the standalone (where
        // there is no host at all) and unchangeable everywhere.
        ap.fallbackBpm = (double) apvts.getRawParameterValue("bpm")->load();

        // The engine's input is this line's buffer alone, never the merged stream: that is the
        // whole of the routing. Its output goes into midi with everything the other lines and
        // the pass-through left there, so three lines at three rates simply sum.
        l.engine.process(ap, hc, numSamples, l.in, l.out);
        mergeArpOut(midi, l.out, channel);
    }
}

// Audio thread. Counting the chain's bars belongs here because this is the only place with
// a tempo, and outside the arpOn block because a progression is a chord player first: it
// keeps moving whether or not anything is arpeggiating what it hands over. The *launch*
// cannot happen here - it moves host parameters and fires notes - so this only ever raises
// a flag for the heartbeat to act on.
void KeysProcessor::advanceChainClock(int numSamples)
{
    // Tempo and bar length are the same question whichever line is asking, so they are asked
    // once and the three chains are stepped against the same answer.
    double bpm = (double) apvts.getRawParameterValue("bpm")->load();
    double beatsPerBar = 4.0;
    if (auto* playHead = getPlayHead())
        if (auto pos = playHead->getPosition())
        {
            if (pos->getIsPlaying())
                if (auto hostBpm = pos->getBpm(); hostBpm && *hostBpm > 0.0)
                    bpm = *hostBpm;
            // A bar is four beats only in four-four. Asking the host costs nothing and is
            // the difference between a chain that lands on the bar in 3/4 and one that does
            // not; with no host to ask, four it is.
            if (auto sig = pos->getTimeSignature(); sig && sig->denominator > 0)
                beatsPerBar = 4.0 * (double) sig->numerator / (double) sig->denominator;
        }

    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const double blockBeats = bpm / 60.0 / sr * numSamples;

    // Publish where we are, in beats, for Launch Quantize to measure the next boundary from.
    // The host's own position while it is rolling, so a quantized launch lands on the *host's*
    // bar line and not on one of our own invention; otherwise a count of our own, which is the
    // only clock there is in the standalone. Both are read on the message thread, which turns
    // "beats until the boundary" into a wall-clock deadline and waits that out on a 1 ms timer -
    // so this only has to be right to within a block, not to a sample.
    {
        bool haveHostPos = false;
        if (auto* playHead = getPlayHead())
            if (auto pos = playHead->getPosition())
                if (pos->getIsPlaying())
                    if (auto ppq = pos->getPpqPosition())
                    {
                        arpBeats.store(*ppq, std::memory_order_relaxed);
                        haveHostPos = true;
                    }
        if (! haveHostPos)
            arpBeats.store(arpBeats.load(std::memory_order_relaxed) + blockBeats,
                           std::memory_order_relaxed);
        arpBeatsBpm.store(bpm, std::memory_order_relaxed);
    }

    for (auto& ln : lines)
    {
        if (! ln.chainActive.load(std::memory_order_relaxed))
        {
            ln.chainBeatsPlayed = 0.0;
            continue;
        }

        const int epoch = ln.chainEpoch.load(std::memory_order_acquire);
        if (epoch != ln.chainSeenEpoch)
        {
            ln.chainSeenEpoch = epoch;
            ln.chainBeatsPlayed = 0.0; // a slot was just launched: its bars start now
        }

        ln.chainBeatsPlayed += blockBeats;
        const double target = ln.chainTargetBeats.load(std::memory_order_relaxed) * beatsPerBar / 4.0;
        if (ln.chainBeatsPlayed >= target)
        {
            ln.chainBeatsPlayed = 0.0; // the epoch bump that follows will zero it again, harmlessly
            ln.chainAdvance.store(true, std::memory_order_release);
        }
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

// Everything below takes a line index. Line 0 is the arpeggiator Keys has always had, and
// every one of these defaults to it, so a caller that has not learned about lines yet still
// drives the same arp it always did.

void KeysProcessor::storeActiveArpPattern(int line)
{
    auto& ln = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)];
    auto& pat = ln.patterns[(size_t) ln.activePattern];
    for (int l = 0; l < ArpEngine::numLanes; ++l)
    {
        for (int s = 0; s < ArpEngine::maxSteps; ++s)
            pat.value[(size_t) l][(size_t) s] = ln.engine.lanes.value[(size_t) l][(size_t) s].load();
        pat.length[(size_t) l] = ln.engine.lanes.length[(size_t) l].load();
        pat.clockDiv[(size_t) l] = ln.engine.lanes.clockDiv[(size_t) l].load();
    }
}

int KeysProcessor::arpActivePattern(int line) const
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].activePattern;
}

void KeysProcessor::recallArpPattern(int index, int line)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    storeActiveArpPattern(line);
    auto& ln = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)];
    ln.activePattern = index;
    const auto& pat = ln.patterns[(size_t) index];
    for (int l = 0; l < ArpEngine::numLanes; ++l)
    {
        for (int s = 0; s < ArpEngine::maxSteps; ++s)
            ln.engine.lanes.value[(size_t) l][(size_t) s].store(pat.value[(size_t) l][(size_t) s]);
        ln.engine.lanes.length[(size_t) l].store(juce::jlimit(1, ArpEngine::maxSteps, pat.length[(size_t) l]));
        ln.engine.lanes.clockDiv[(size_t) l].store(juce::jlimit(0, 2, pat.clockDiv[(size_t) l]));
    }
}

bool KeysProcessor::arpQuantizeOn() const
{
    return apvts.getRawParameterValue("arpQuantize")->load() > 0.5f;
}

double KeysProcessor::arpQuantizeDelayMs() const
{
    // Off, 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars - in beats. Four to the bar, which is the same
    // assumption `chainTargetBeats` makes; a chain in 3/4 is corrected on the audio thread by
    // the host's time signature, and this is not, because a launch waiting one beat too long is
    // a launch that still landed on a beat. Worth revisiting if anyone works in 7/8.
    static constexpr double beats[] = { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
    const int idx = juce::jlimit(0, (int) std::size(beats) - 1,
                                 (int) apvts.getRawParameterValue("arpQuantize")->load());
    const double div = beats[idx];
    if (div <= 0.0)
        return 0.0;

    const double now = arpBeats.load(std::memory_order_relaxed);
    const double bpm = juce::jmax(1.0, arpBeatsBpm.load(std::memory_order_relaxed));
    // The next boundary strictly ahead of us. `+ 1e-9` before the floor rather than a plain
    // ceil, so a click that lands exactly on a boundary fires now instead of waiting a whole
    // division for the next one.
    const double next = (std::floor(now / div + 1.0e-9) + 1.0) * div;
    return juce::jmax(0.0, (next - now) / bpm * 60000.0);
}

int KeysProcessor::arpPendingSlot(int line) const
{
    for (const auto& p : pendingLaunches)
        if (p.line == line)
            return p.slot;
    return -1;
}

bool KeysProcessor::arpLaunchPending(int line) const
{
    for (const auto& p : pendingLaunches)
        if (p.line == line)
            return true;
    return false;
}

// The gesture, with quantize already settled. Called either straight from deferLaunch (off) or
// from the timer at the boundary (on), so there is exactly one description of what a launch
// does and the wait cannot change it.
void KeysProcessor::fireLaunchNow(const PendingLaunch& p)
{
    if (p.slot >= 0)
        launchArpSlotNow(p.slot, p.line);
    else if (p.padSlot >= 0)
        holdArpChordFromPadNow(p.padSlot, p.line);
    else
        holdArpChordNow(p.notes, p.name, p.line);
}

bool KeysProcessor::deferLaunch(PendingLaunch p)
{
    const double wait = arpQuantizeDelayMs();
    if (wait <= 0.0)
    {
        fireLaunchNow(p);
        return false;
    }

    // One pending launch per line. A second click before the boundary *replaces* the first
    // rather than queueing behind it: you changed your mind, and two chords landing on one
    // boundary is not something anybody asked for by clicking twice.
    pendingLaunches.erase(std::remove_if(pendingLaunches.begin(), pendingLaunches.end(),
                                         [&p](const PendingLaunch& q) { return q.line == p.line; }),
                          pendingLaunches.end());
    p.atMs = juce::Time::getMillisecondCounterHiRes() + wait;
    pendingLaunches.push_back(std::move(p));
    if (! isTimerRunning())
        startTimer(1);
    return true;
}

void KeysProcessor::firePendingLaunches(double nowMs)
{
    if (pendingLaunches.empty())
        return;
    // Copied out before firing: a launch moves parameters and fires notes, and anything it
    // touches could in principle come back through deferLaunch and reallocate this vector.
    std::vector<PendingLaunch> due;
    for (auto& p : pendingLaunches)
        if (p.atMs <= nowMs)
            due.push_back(p);
    if (due.empty())
        return;
    pendingLaunches.erase(std::remove_if(pendingLaunches.begin(), pendingLaunches.end(),
                                         [nowMs](const PendingLaunch& q) { return q.atMs <= nowMs; }),
                          pendingLaunches.end());
    for (const auto& p : due)
        fireLaunchNow(p);
}

void KeysProcessor::holdArpChord(const std::vector<int>& notes, const juce::String& name, int line)
{
    if (notes.empty())
    {
        holdArpChordNow(notes, name, line); // an empty hold is a release; never worth waiting for
        return;
    }
    PendingLaunch p;
    p.line = juce::jlimit(0, numArpLines - 1, line);
    p.notes = notes;
    p.name = name;
    deferLaunch(std::move(p));
}

void KeysProcessor::holdArpChordNow(const std::vector<int>& notes, const juce::String& name, int line)
{
    line = juce::jlimit(0, numArpLines - 1, line);
    releaseArpChord(line);
    if (notes.empty())
        return;
    // Exclusive works in both directions or it does not work: handing a card to the arp has
    // to choke a sounding pad exactly the way pressing a pad now chokes the arp hold. It
    // chokes the other two lines with it, which is what "one chord at a time, whichever
    // surface started it" has to mean once there are three surfaces that can hold one.
    if (apvts.getRawParameterValue("chordExclusive")->load() > 0.5f)
        stopAllChordPads();
    auto& ln = lines[(size_t) line];
    ln.chordName = name;
    // Fired into this line's own queue, not the track output: only its engine sees it. Note
    // it still goes through fireChord, so the Voices cap, Strum and Humanize all apply and
    // the keybed lights up for it exactly as before.
    ln.chordOn = fireChord(notes, arpChordTagFor(line), line + 1);
}

void KeysProcessor::releaseArpChord(int line)
{
    line = juce::jlimit(0, numArpLines - 1, line);
    auto& ln = lines[(size_t) line];
    // No Sustain check, unlike a pad: this chord is held on purpose until something
    // replaces it, so the pedal has nothing to say about when it stops.
    releaseNotes(ln.chordOn, arpChordTagFor(line), line + 1);
    ln.chordName = {};
    ln.launchedSlot = -1;
    ln.padSlot = -1;
}

void KeysProcessor::releaseArpHold()
{
    // Every line, because this is one button and it means "let go". A Hold off that released
    // only the line the panel happened to be showing would leave the other two droning, with
    // nothing on a folded bar to stop them.
    // Anything waiting on a quantize boundary is let go of too: it has not sounded yet, but it
    // is a chord on its way, and Hold off means "nothing is coming".
    pendingLaunches.clear();
    for (int n = 0; n < numArpLines; ++n)
    {
        // The chain goes first, and for the same reason the heartbeat stops it when the arp
        // goes off: releasing the chord without it only wins until the next bar boundary,
        // when heartbeatTick() launches the following slot and hands the arp another one.
        // stopChain() is a no-op when nothing is chaining, and releases the chord it was
        // holding when it is.
        stopChain(n);
        // Whatever a card or a lone slot launch left behind. Idempotent after stopChain().
        releaseArpChord(n);
    }
}

const std::vector<int>& KeysProcessor::arpHeldNotes(int line) const
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].chordOn;
}

const juce::String& KeysProcessor::arpHeldName(int line) const
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].chordName;
}

bool KeysProcessor::anyArpHold() const
{
    for (int n = 0; n < numArpLines; ++n)
        if (! lines[(size_t) n].chordOn.empty() || lines[(size_t) n].chainOn)
            return true;
    return false;
}

void KeysProcessor::holdArpChordFromPad(int padSlot, int line)
{
    if (padSlot < 0 || padSlot >= numChordPads)
        return;
    if (chordPads[(size_t) padSlot].notes.empty())
        return;
    PendingLaunch p;
    p.line = juce::jlimit(0, numArpLines - 1, line);
    p.padSlot = padSlot;
    deferLaunch(std::move(p));
}

void KeysProcessor::holdArpChordFromPadNow(int padSlot, int line)
{
    if (padSlot < 0 || padSlot >= numChordPads)
        return;
    const auto& pad = chordPads[(size_t) padSlot];
    if (pad.notes.empty())
        return;
    line = juce::jlimit(0, numArpLines - 1, line);
    holdArpChordNow(pad.notes, pad.name, line); // clears this line's previous holder, slot or pad
    lines[(size_t) line].padSlot = padSlot;
}

int KeysProcessor::arpHeldPad(int line) const
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].padSlot;
}

int KeysProcessor::arpLineHoldingPad(int padSlot) const
{
    if (padSlot < 0)
        return -1;
    for (int n = 0; n < numArpLines; ++n)
        if (lines[(size_t) n].padSlot == padSlot)
            return n;
    return -1;
}

void KeysProcessor::launchArpSlot(int index, int line)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    PendingLaunch p;
    p.line = juce::jlimit(0, numArpLines - 1, line);
    p.slot = index;
    deferLaunch(std::move(p));
}

// The whole gesture, and it is a gesture rather than a setting: the pattern, the shape, the
// rate and the chord all land together, which is why Launch Quantize defers this and not just
// the note-ons inside it.
void KeysProcessor::launchArpSlotNow(int index, int line)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    line = juce::jlimit(0, numArpLines - 1, line);

    recallArpPattern(index, line); // snapshots the outgoing slot's lanes first
    const auto& slot = lines[(size_t) line].patterns[(size_t) index];

    // Shape and Rate are ordinary parameters, so a launch has to move them through the
    // host the way the combo boxes do - otherwise automation and the UI disagree about
    // what is playing. Gestures bracket each one; see ArpPanel::applyShapeChoice.
    // They are this *line's* parameters: launching a slot on B must not move A's rate.
    const auto setChoice = [this](const juce::String& id, int value)
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
        if (auto* pat = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(arpParamId(line, apPattern))))
        {
            pat->beginChangeGesture();
            *pat = slot.shape >= ArpEngine::numDirections;
            pat->endChangeGesture();
        }
        if (slot.shape < ArpEngine::numDirections)
            setChoice(arpParamId(line, apDirection), slot.shape); // "Pattern" leaves the direction alone
    }
    if (slot.rate >= 0)
    {
        setChoice(arpParamId(line, apRate), slot.rate);
        // The mode travels with the rate, and both move through the host for the same reason
        // the choice above does. A slot saved before Hz existed reads back rateFree false, so
        // this writes the mode it already had.
        if (auto* free = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(arpParamId(line, apRateFree))))
        {
            free->beginChangeGesture();
            *free = slot.rateFree;
            free->endChangeGesture();
        }
        // Only a slot captured in Hz brings a Hz value with it. `rateHz` is not a value every
        // slot has: arpFromTree synthesises 8.0 for every slot in a session saved before the
        // mode existed, so writing it unconditionally meant opening an old session, dialling
        // 0.5 Hz and clicking any slot silently reset the rate to a fabricated 8 Hz. A Sync
        // slot leaves the Hz control exactly where it found it, which is what the struct
        // comment beside ArpPattern::rateHz already promises.
        if (slot.rateFree)
            if (auto* hz = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(arpParamId(line, apRateHz))))
            {
                hz->beginChangeGesture();
                *hz = slot.rateHz;
                hz->endChangeGesture();
            }
    }

    // Hold last, so the chord starts against the pattern the slot just installed. The *Now
    // path, because this launch has already served whatever wait Launch Quantize asked for -
    // deferring again here would make a quantized slot land a whole division late.
    if (! slot.chordNotes.empty())
        holdArpChordNow(slot.chordNotes, slot.chordName, line);
    else
        releaseArpChord(line); // a pattern-only slot arpeggiates whatever you are already holding
    lines[(size_t) line].launchedSlot = index;
}

void KeysProcessor::stopArpSlot(int line)
{
    releaseArpChord(line); // clears launchedSlot too
}

int KeysProcessor::arpLaunchedSlot(int line) const
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].launchedSlot;
}

int KeysProcessor::nextChainSlot(int from, int line) const
{
    // The chain plays the slots that hold a chord. A pattern-only slot is a place to keep a
    // rhythm, not a step of a progression, and walking through one would leave the previous
    // chord ringing under a pattern that says nothing about it.
    const auto& pats = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].patterns;
    for (int i = 1; i <= numArpPatterns; ++i)
    {
        const int idx = (from + i) % numArpPatterns;
        if (! pats[(size_t) idx].chordNotes.empty())
            return idx;
    }
    return from >= 0 && ! pats[(size_t) from].chordNotes.empty() ? from : -1;
}

void KeysProcessor::startChain(int line)
{
    line = juce::jlimit(0, numArpLines - 1, line);
    const int first = nextChainSlot(numArpPatterns - 1, line); // wraps to the lowest filled slot
    if (first < 0)
        return; // nothing to play: the button does not stick on for an empty row
    auto& ln = lines[(size_t) line];
    ln.chainOn = true;
    ln.chainIndex = first;
    launchArpSlotNow(first, line); // the chain owns its own timing; see heartbeatTick
    ln.chainTargetBeats.store(4.0 * juce::jmax(1, ln.patterns[(size_t) first].bars));
    ln.chainEpoch.fetch_add(1); // tells the audio thread to count this slot from zero
    // Clear the advance flag *before* arming the clock. stopChain() clears it too, but the
    // audio thread can raise it once more in the block that straddles the stop, and nothing
    // consumes it while chainOn is false - so a stale true survived into the next chain and
    // stepped it to its second slot on the first heartbeat. Ordered last but one so there is
    // no window where chainActive is set and the flag is still whatever it was.
    ln.chainAdvance.store(false);
    ln.chainActive.store(true);
}

void KeysProcessor::stopChain(int line)
{
    line = juce::jlimit(0, numArpLines - 1, line);
    auto& ln = lines[(size_t) line];
    if (! ln.chainOn)
        return;
    ln.chainOn = false;
    ln.chainIndex = -1;
    ln.chainActive.store(false);
    ln.chainAdvance.store(false);
    stopArpSlot(line); // the chord the chain was holding is the chain's to release
}

bool KeysProcessor::chainRunning(int line) const
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].chainOn;
}

int KeysProcessor::chainSlot(int line) const
{
    const auto& ln = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)];
    return ln.chainOn ? ln.chainIndex : -1;
}

void KeysProcessor::setArpSlotBars(int index, int bars, int line)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    auto& ln = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)];
    ln.patterns[(size_t) index].bars = juce::jlimit(1, 16, bars);
    if (ln.chainOn && index == ln.chainIndex)
        ln.chainTargetBeats.store(4.0 * ln.patterns[(size_t) index].bars); // takes effect now
}

int KeysProcessor::arpSlotBars(int index, int line) const
{
    if (index < 0 || index >= numArpPatterns)
        return 1;
    return juce::jlimit(1, 16, lines[(size_t) juce::jlimit(0, numArpLines - 1, line)]
                                   .patterns[(size_t) index].bars);
}

void KeysProcessor::heartbeatTick()
{
    // Releasing a chord held into the arp when the arp goes off. This lived in the editor's
    // timer and was gated on the chord having come from a *pad*, so a chord handed over from
    // the live card was never released - and with no editor open nothing polled at all, so
    // automation or an MCP client writing arpOn false left it sounding with no way to stop
    // it but All Off. Both edges close here: the processor owns the chord, so the processor
    // is what should notice. Per line, because each line has a switch of its own.
    for (int n = 0; n < numArpLines; ++n)
    {
        auto& ln = lines[(size_t) n];
        const bool arpOn = arpLineOn(n);
        if (! arpOn && ln.lastOnHeartbeat)
        {
            // The chain goes first: a progression cycling with nothing arpeggiating it is the
            // same drone-with-no-owner this whole check exists to prevent, and its Chain button
            // is on the arp panel, so switching the arp off is switching the chain off.
            stopChain(n);
            // What is left is a chord a *card* handed over - a pad, or the live card, which is
            // the case the editor's version of this missed entirely. A chord an arp *slot*
            // launched is left alone on purpose: the lit card is still on screen and still
            // releases it on a click, so it has an owner and this does not have to be one.
            if (ln.launchedSlot < 0 && ! ln.chordOn.empty())
                releaseArpChord(n);
        }
        ln.lastOnHeartbeat = arpOn;

        if (ln.chainOn && ln.chainAdvance.exchange(false))
        {
            const int next = nextChainSlot(ln.chainIndex, n);
            if (next < 0)
            {
                stopChain(n); // every chord was cleared out from under it
                continue;
            }
            ln.chainIndex = next;
            launchArpSlotNow(next, n); // already on a bar boundary; quantizing it again would drift
            ln.chainTargetBeats.store(4.0 * juce::jmax(1, ln.patterns[(size_t) next].bars));
            ln.chainEpoch.fetch_add(1);
        }
    }
}

// Message thread. The Chord lane reads slot chords on the audio thread, so they live in a
// mirror of atomics; this rebuilds the whole mirror for one line, which is twelve chords of
// eight notes and not worth being clever about. Every path that can change a slot's chord
// ends here, and now has to name the line whose slots it changed - miss one and that line's
// Chord lane plays a stale chord.
void KeysProcessor::syncArpChordTable(int line)
{
    static_assert(ArpEngine::ChordTable::numSlots == numArpPatterns,
                  "the Chord lane addresses slots one for one");
    auto& ln = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)];
    for (int s = 0; s < ArpEngine::ChordTable::numSlots; ++s)
    {
        const auto& notes = ln.patterns[(size_t) s].chordNotes;
        const int n = juce::jlimit(0, ArpEngine::ChordTable::maxNotes, (int) notes.size());
        for (int i = 0; i < ArpEngine::ChordTable::maxNotes; ++i)
            ln.chordTable.note[(size_t) s][(size_t) i].store(i < n ? notes[(size_t) i] : 0,
                                                             std::memory_order_relaxed);
        // Count last, and it is what the engine gates on, so a half-written chord is never
        // reachable: the notes are in place before the count that admits them.
        ln.chordTable.count[(size_t) s].store(n, std::memory_order_release);
    }
}

void KeysProcessor::setArpSlotChord(int index, const std::vector<int>& notes, const juce::String& name, int line)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    line = juce::jlimit(0, numArpLines - 1, line);
    auto& ln = lines[(size_t) line];
    ln.patterns[(size_t) index].chordNotes = notes;
    ln.patterns[(size_t) index].chordName = name;
    syncArpChordTable(line);
    // Capture the shape and rate that are up right now, so launching the slot brings the
    // whole sound back and the card can say what it will play. Nothing else ever wrote
    // these, so every slot painted "--" and a launch left Shape and Rate alone.
    const bool usePattern = arpParam(line, apPattern) > 0.5f;
    ln.patterns[(size_t) index].shape = usePattern
                                            ? ArpEngine::numDirections
                                            : (int) arpParam(line, apDirection);
    ln.patterns[(size_t) index].rate = (int) arpParam(line, apRate);
    // ...and the mode it is in, so a slot captured in Hz launches in Hz.
    ln.patterns[(size_t) index].rateFree = arpParam(line, apRateFree) > 0.5f;
    ln.patterns[(size_t) index].rateHz = arpParam(line, apRateHz);
}

void KeysProcessor::clearArpSlotChord(int index, int line)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    line = juce::jlimit(0, numArpLines - 1, line);
    auto& ln = lines[(size_t) line];
    ln.patterns[(size_t) index].chordNotes.clear();
    ln.patterns[(size_t) index].chordName = {};
    syncArpChordTable(line);
    if (ln.launchedSlot == index)
        releaseArpChord(line);
}

void KeysProcessor::copyArpPattern(int from, int to, int line)
{
    if (from < 0 || from >= numArpPatterns || to < 0 || to >= numArpPatterns || from == to)
        return;
    line = juce::jlimit(0, numArpLines - 1, line);
    storeActiveArpPattern(line);
    auto& ln = lines[(size_t) line];
    ln.patterns[(size_t) to] = ln.patterns[(size_t) from];
    syncArpChordTable(line);
    if (to == ln.activePattern)
        recallArpPattern(to, line);
}

const KeysProcessor::ArpPattern& KeysProcessor::arpPatternSlot(int index, int line) const
{
    static const ArpPattern empty {};
    if (index < 0 || index >= numArpPatterns)
        return empty;
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].patterns[(size_t) index];
}

void KeysProcessor::setArpPatternSlot(int index, const ArpPattern& pattern, int line)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    line = juce::jlimit(0, numArpLines - 1, line);
    auto& ln = lines[(size_t) line];
    ln.patterns[(size_t) index] = pattern;
    syncArpChordTable(line);
    if (index != ln.activePattern)
        return;
    // Refresh the live lanes from the slot just written. Not recallArpPattern(index):
    // that snapshots the live lanes into patterns[activePattern] first, which
    // would clobber the pattern we just wrote before reading it back.
    for (int l = 0; l < ArpEngine::numLanes; ++l)
    {
        for (int s = 0; s < ArpEngine::maxSteps; ++s)
            ln.engine.lanes.value[(size_t) l][(size_t) s].store(pattern.value[(size_t) l][(size_t) s]);
        ln.engine.lanes.length[(size_t) l].store(juce::jlimit(1, ArpEngine::maxSteps, pattern.length[(size_t) l]));
        ln.engine.lanes.clockDiv[(size_t) l].store(juce::jlimit(0, 2, pattern.clockDiv[(size_t) l]));
    }
}

void KeysProcessor::randomizeActiveArpPattern(int line)
{
    // Musical randomize, not white noise: mostly direction-following steps, gentle
    // octave jumps, occasional ratchets and rests.
    auto& lanes = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].engine.lanes;
    for (int s = 0; s < ArpEngine::maxSteps; ++s)
    {
        lanes.value[ArpEngine::laneNote][(size_t) s].store(rng.nextInt(10) == 0 ? -1 : 0);
        lanes.value[ArpEngine::laneOctave][(size_t) s].store(rng.nextInt(5) == 0 ? rng.nextInt(3) - 1 : 0);
        lanes.value[ArpEngine::laneVelocity][(size_t) s].store(70 + rng.nextInt(60));
        lanes.value[ArpEngine::laneGate][(size_t) s].store(40 + rng.nextInt(80));
        lanes.value[ArpEngine::laneRatchet][(size_t) s].store(rng.nextInt(8) == 0 ? 2 : 1);
        lanes.value[ArpEngine::laneProbability][(size_t) s].store(rng.nextInt(6) == 0 ? 60 : 100);
    }
}

void KeysProcessor::restoreSharedState(const juce::ValueTree& root)
{
    // Everything both products restore from a saved session, in one place. Keys Host
    // overrides setStateInformation to add its hosted instrument, and used to repeat this
    // list by hand: the strum migration below was written on 2026-07-27, worked in Keys, and
    // silently did nothing in Keys Host until this became one function. Anything session
    // shaped belongs here, not in either override.
    migrateStrumRange(root);
    migrateRateMode(root);
    chordPadsFromTree(root);
    arpFromTree(root);
    layoutFromTree(root);
}

void KeysProcessor::migrateStrumRange(const juce::ValueTree& root)
{
    // Strum became a range on 2026-07-27: "chordStrum" is the low end now and "chordStrumMax"
    // the high one. A session saved before that has no chordStrumMax, so the new parameter
    // arrives at its default of 0 - which does not mean "unchanged", it means the fixed rake
    // that session asked for silently becomes a random spread between zero and it. Caught on
    // Owen's own session, which came back reading "STRUM 0-68 MS" for a 68 ms strum.
    //
    // The tell is the *absence* of chordStrumMax, and only that. An out-of-order pair looks
    // like the same thing and was briefly treated as one, but it is not: both ends are
    // ordinary automatable parameters, and a host or an MCP client can write max below min
    // deliberately. Collapsing that to a fixed strum on the next load would silently narrow
    // a range the user meant. Absence cannot be produced by anything except a session older
    // than the parameter.
    //
    // Read the tree rather than the live parameters: this runs while the state is still being
    // applied, and the atomics may not have caught up.
    //
    // The repair goes through setValueNotifyingHost rather than poking the atomic, so the host
    // and the UI both learn the new value. That wants the message thread, and every host in
    // practice calls setStateInformation there; the spec does not actually promise it, so if
    // one ever turns up that does not, this is the line to move behind a callAsync.
    const auto params = root.getChildWithName(apvts.state.getType());
    if (! params.isValid())
        return;

    float low = 0.0f;
    bool sawLow = false;
    for (int i = 0; i < params.getNumChildren(); ++i)
    {
        const auto child = params.getChild(i);
        const auto id = child.getProperty("id").toString();
        if (id == "chordStrumMax")
            return;                      // saved since the change; whatever it says is meant
        if (id == "chordStrum")
        {
            low = (float) child.getProperty("value");
            sawLow = true;
        }
    }

    if (! sawLow)
        return;                          // nothing saved either way; the defaults are right

    if (auto* param = apvts.getParameter("chordStrumMax"))
        param->setValueNotifyingHost(param->convertTo0to1(low));
}

void KeysProcessor::migrateRateMode(const juce::ValueTree& root)
{
    // The arp rate gained a second unit on 2026-07-30: "arpRateFree" picks it and "arpRateHz"
    // holds it. Neither exists in any session saved before that, and an absent parameter is
    // not a reset - APVTS creates an adapter's child on the spot and flushes the *current*
    // value into it. On a fresh instance that current value is the default and everything is
    // right; in a live instance it is whatever the user has been playing with. So loading an
    // old preset while the dial was in Hz restored and displayed arpRate while the engine
    // carried on free-running at the Hz value from before the load, with the panel showing a
    // division it was not playing. Exactly the shape migrateStrumRange above repairs, and the
    // same fix: the tell is the absence, and the repair is to write the default explicitly.
    //
    // getDefaultValue() rather than a literal: it is already normalised, and it stays correct
    // if either default ever moves. Reads the tree, not the live parameters, for the reason
    // given above - this runs while the state is still landing.
    const auto params = root.getChildWithName(apvts.state.getType());
    if (! params.isValid())
        return;

    // Three lines' worth. B and C's rate parameters are absent from every session saved
    // before the lines existed, which is the same absence for the same reason, and repaired
    // the same way - so the loop covers the original case rather than sitting beside it.
    for (int line = 0; line < numArpLines; ++line)
    {
        const auto freeId = arpParamId(line, apRateFree);
        const auto hzId = arpParamId(line, apRateHz);
        bool sawFree = false, sawHz = false;
        for (int i = 0; i < params.getNumChildren(); ++i)
        {
            const auto id = params.getChild(i).getProperty("id").toString();
            sawFree = sawFree || id == freeId;
            sawHz = sawHz || id == hzId;
        }

        // Independently, though the two shipped together: a tree carrying one and not the
        // other is malformed rather than old, and there is no reading of it under which the
        // missing one meant anything but its default.
        if (! sawFree)
            if (auto* param = apvts.getParameter(freeId))
                param->setValueNotifyingHost(param->getDefaultValue());
        if (! sawHz)
            if (auto* param = apvts.getParameter(hzId))
                param->setValueNotifyingHost(param->getDefaultValue());
    }
}

juce::ValueTree KeysProcessor::layoutToTree() const
{
    juce::ValueTree tree { "layout" };
    tree.setProperty("controls", layout.controls, nullptr);
    tree.setProperty("knobs", layout.knobs, nullptr);
    tree.setProperty("pads", layout.pads, nullptr);
    tree.setProperty("arp", layout.arp, nullptr);
    tree.setProperty("wheels", layout.wheels, nullptr);
    tree.setProperty("keyboard", layout.keyboard, nullptr);
    tree.setProperty("detached", layout.detached, nullptr);
    tree.setProperty("arpDetached", layout.arpDetached, nullptr);
    tree.setProperty("controlsDetached", layout.controlsDetached, nullptr);
    tree.setProperty("padsDetached", layout.padsDetached, nullptr);
    tree.setProperty("chordGen", layout.chordGen, nullptr);
    tree.setProperty("arpLine", layout.arpLine, nullptr);
    tree.setProperty("arpMacro", layout.arpMacro, nullptr);
    tree.setProperty("accent", layout.accent, nullptr);
    tree.setProperty("detachedBounds", layout.detachedBounds.toString(), nullptr);
    tree.setProperty("arpDetachedBounds", layout.arpDetachedBounds.toString(), nullptr);
    tree.setProperty("controlsDetachedBounds", layout.controlsDetachedBounds.toString(), nullptr);
    tree.setProperty("padsDetachedBounds", layout.padsDetachedBounds.toString(), nullptr);
    tree.setProperty("chordGenBounds", layout.chordGenBounds.toString(), nullptr);
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
    layout.knobs = flag("knobs", true);
    layout.pads = flag("pads", true);
    layout.arp = flag("arp", false);
    layout.wheels = flag("wheels", true);
    layout.keyboard = flag("keyboard", true);
    layout.detached = flag("detached", false);
    layout.arpDetached = flag("arpDetached", false);
    layout.controlsDetached = flag("controlsDetached", false);
    layout.padsDetached = flag("padsDetached", false);
    // Absent before the generator had a window of its own; shut is the right default either
    // way, since it is a settings window rather than something you play from.
    layout.chordGen = flag("chordGen", false);
    // Absent before there were three lines, and line A is the right answer for those: it is
    // the only one a session from then can have anything in.
    layout.arpLine = juce::jlimit(0, numArpLines - 1, (int) tree.getProperty("arpLine", 0));
    layout.arpMacro = flag("arpMacro", false);
    // Older sessions carry keys nothing reads any more, and every one of them is simply
    // ignored: an unread ValueTree property is dropped, so the load cannot throw and the
    // rest of the layout still arrives.
    //   "transcribe" / "transcribeDetached" / "transcribeDetachedBounds", from before the
    //   Transcribe section was removed.
    //   "centre" / "centreDetached" / "centreDetachedBounds" and "view", from before the
    //   centre section was removed (2026-07-30). The knob bank it held is a row of the
    //   Controls band now, folded by "knobs", which those sessions already carry. "view"
    //   was the centre's chosen view and had migrations of its own - 2 meaning "the arp",
    //   from before the arp was a section - retired with the section they restored into.
    layout.accent = juce::jlimit(0, 7, (int) tree.getProperty("accent", 0));

    // A frame is only restored if the session actually carried one; an empty rectangle
    // means "never detached yet", which the editor reads as "centre the window".
    const auto frame = [&tree](const char* id, juce::Rectangle<int>& dest)
    {
        const auto r = juce::Rectangle<int>::fromString(tree.getProperty(id).toString());
        if (! r.isEmpty())
            dest = r;
    };
    frame("detachedBounds", layout.detachedBounds);
    frame("arpDetachedBounds", layout.arpDetachedBounds);
    frame("controlsDetachedBounds", layout.controlsDetachedBounds);
    frame("padsDetachedBounds", layout.padsDetachedBounds);
    frame("chordGenBounds", layout.chordGenBounds);
}

void KeysProcessor::arpLineToTree(juce::ValueTree& dest, int line) const
{
    const auto& ln = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)];
    dest.setProperty("active", ln.activePattern, nullptr);
    for (int pIndex = 0; pIndex < numArpPatterns; ++pIndex)
    {
        const auto& pat = ln.patterns[(size_t) pIndex];
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
        // The rate's mode, written alongside it. Absent in every session saved before Hz
        // existed, which reads back as Sync at the `rate` index above - what those sessions
        // actually played.
        pt.setProperty("rateFree", pat.rateFree, nullptr);
        pt.setProperty("rateHz", (double) pat.rateHz, nullptr);
        pt.setProperty("bars", pat.bars, nullptr); // how long the chain holds this slot
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
        dest.appendChild(pt, nullptr);
    }
}

juce::ValueTree KeysProcessor::arpToTree() const
{
    // The live lanes are the active pattern; snapshot them so the tree is current. All three,
    // since all three have live lanes of their own.
    for (int n = 0; n < numArpLines; ++n)
        const_cast<KeysProcessor*>(this)->storeActiveArpPattern(n);

    juce::ValueTree tree { "arp" };
    // A slot's `shape` is a direction index, with numDirections itself meaning "Pattern" -
    // so the number that means Pattern moves every time a shape is added. Writing it down is
    // what lets a session saved when there were eight shapes still open on twelve; without
    // it, the four shapes added on 2026-07-30 silently turned every Pattern slot into Random.
    // One copy for the whole tree: it is a property of the build that wrote it, not of a line.
    tree.setProperty("shapeBase", ArpEngine::numDirections, nullptr);

    // Line 0's slots sit directly on this node, in exactly the shape and place they have
    // always occupied, so a session written here still loads into a build that predates the
    // lines - and, more to the point, every session written *by* those builds loads here with
    // no migration at all. B and C hang off "line" children, which an older build ignores
    // because it never asks for them.
    arpLineToTree(tree, 0);
    for (int n = 1; n < numArpLines; ++n)
    {
        juce::ValueTree lt { "line" };
        lt.setProperty("index", n, nullptr);
        arpLineToTree(lt, n);
        tree.appendChild(lt, nullptr);
    }
    return tree;
}

void KeysProcessor::arpLineFromTree(const juce::ValueTree& src, int line, int savedShapeBase)
{
    line = juce::jlimit(0, numArpLines - 1, line);
    auto& ln = lines[(size_t) line];
    for (int c = 0; c < src.getNumChildren(); ++c)
    {
        const auto pt = src.getChild(c);
        if (! pt.hasType("pattern"))
            continue; // a "line" child of the root; that line reads itself
        const int pIndex = (int) pt.getProperty("index", -1);
        if (pIndex < 0 || pIndex >= numArpPatterns)
            continue;
        auto& pat = ln.patterns[(size_t) pIndex];
        pat.chordNotes.clear();
        for (const auto& n : juce::StringArray::fromTokens(pt.getProperty("chord").toString(), ",", ""))
            if (n.isNotEmpty())
                pat.chordNotes.push_back(juce::jlimit(0, 127, n.getIntValue()));
        pat.chordName = pt.getProperty("chordName").toString();
        pat.shape = (int) pt.getProperty("shape", -1);
        if (pat.shape == savedShapeBase)
            pat.shape = ArpEngine::numDirections; // "Pattern" is wherever Pattern is now
        pat.shape = juce::jlimit(-1, ArpEngine::numDirections, pat.shape);
        pat.rate = (int) pt.getProperty("rate", -1);
        // Both absent before the Hz mode: false and the default 8 Hz, so an old session's
        // slot launches the division it stored, exactly as it always did.
        pat.rateFree = (bool) pt.getProperty("rateFree", false);
        pat.rateHz = (float) juce::jlimit((double) ArpEngine::minRateHz, (double) ArpEngine::maxRateHz,
                                          (double) pt.getProperty("rateHz", 8.0));
        pat.bars = juce::jlimit(1, 16, (int) pt.getProperty("bars", 1)); // absent before the chain
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
    syncArpChordTable(line); // the Chord lane's view of the slots, after a whole session lands
    // Recall the active pattern by hand (recallArpPattern would first snapshot the
    // live lanes over the data we just loaded).
    ln.activePattern = juce::jlimit(0, numArpPatterns - 1, (int) src.getProperty("active", 0));
    const auto& pat = ln.patterns[(size_t) ln.activePattern];
    for (int l = 0; l < ArpEngine::numLanes; ++l)
    {
        for (int s = 0; s < ArpEngine::maxSteps; ++s)
            ln.engine.lanes.value[(size_t) l][(size_t) s].store(pat.value[(size_t) l][(size_t) s]);
        ln.engine.lanes.length[(size_t) l].store(juce::jlimit(1, ArpEngine::maxSteps, pat.length[(size_t) l]));
        ln.engine.lanes.clockDiv[(size_t) l].store(juce::jlimit(0, 2, pat.clockDiv[(size_t) l]));
    }
}

void KeysProcessor::arpFromTree(const juce::ValueTree& root)
{
    const auto tree = root.getChildWithName("arp");
    if (! tree.isValid())
        return; // sessions from before the arp: defaults stand
    // What "Pattern" was numbered when this session was written. Absent means it predates
    // the four shapes added on 2026-07-30, when there were eight directions and Pattern was
    // eight; every save since says so itself.
    const int savedShapeBase = juce::jlimit(1, ArpEngine::numDirections,
                                            (int) tree.getProperty("shapeBase", 8));

    // Line 0 off the root, where it has always been. This is the whole of the migration: a
    // session saved before the lines existed has no "line" children at all, so B and C keep
    // their defaults - twelve empty slots and lanes that have never been drawn - which with
    // both switched off is precisely the arpeggiator that session was saved from.
    arpLineFromTree(tree, 0, savedShapeBase);
    for (int c = 0; c < tree.getNumChildren(); ++c)
    {
        const auto lt = tree.getChild(c);
        if (! lt.hasType("line"))
            continue;
        const int n = (int) lt.getProperty("index", -1);
        if (n >= 1 && n < numArpLines)
            arpLineFromTree(lt, n, savedShapeBase);
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
        restoreSharedState(root);
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
