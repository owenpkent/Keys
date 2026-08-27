#include "PluginProcessor.h"
#include "ChordSources.h" // the Progression picker's item list is that table's own
#include "EuclidGen.h"
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
    // **On by default since 2026-08-23** (Owen: "I want the default strum up, humanize,
    // velocity, and H.TIME to have the range on and enabled by default"). Keys opened with every
    // chord stamped out at one velocity, which is the sound this control exists to get away
    // from, and the switch is the lamp on a knob in the pad strip - so the only way to find it
    // was to already know it was there. A default change and nothing else: a saved session
    // stores this bool and keeps whatever it said.
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "humanize", 1 }, "Humanize", true));
    // **0..127, MIDI's own range** (2026-08-18, Owen: "the velocity ranges ... should go from zero
    // to one twenty seven or one twenty eight, whatever the standard is"). The standard is 0-127,
    // 128 values, and the floor was 1 because a note-on at velocity **0 is a note-off** - the one
    // value MIDI will not let a sounding note carry. So the number reaches 0 here and the wire
    // never does: noteOn clamps what it emits to velocity 1, and 0 means "as quiet as MIDI can
    // say", not silence. Every saved session is unaffected - APVTS stores an int parameter's
    // plain value, so 64 still reads 64 - but a host *automation* lane shifts by one unit at the
    // bottom, since normalised 0.0 used to mean 1 and now means 0.
    // **56..96 since 2026-08-23**, opened out from 64..88 alongside the switch above. The
    // **midpoint is still 76**, and that is load-bearing rather than tidy: Humanize *off* plays
    // the band's midpoint (baseVelocity01), so widening around the centre changes what a
    // variation sounds like and can never change what "off" plays. migrateVelLevel reads that
    // same 76 to convert an old session's arp level, and is untouched by this.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "humanizeVelMin", 1 }, "Velocity Min", 0, 127, 56));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "humanizeVelMax", 1 }, "Velocity Max", 0, 127, 96));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "chordExclusive", 1 }, "Chord Exclusive", false));
    // Strum is a *range*, like the humanize velocity beside it: each chord takes a random
    // spread between the two ends, so repeated stabs do not all rake at exactly the same
    // speed. "chordStrum" is the low end and keeps its old id, so a session saved with a
    // single strum value loads with that value as its minimum and nothing moves.
    //
    // **30..80 ms since 2026-08-23**, where both ends were 0 (Owen, same ask as Humanize above).
    // Zero is a chord landing all at once, and the knob's lamp reads the *face* - the pair's
    // centre - so at 0/0 the control opened dark and unlit, which is a feature you can only
    // find by already knowing about the satellite that switches it on. The direction is
    // unchanged: "Up" has been the default since the day it was a parameter, and it is what
    // Owen asked for by name. migrateStrumRange is untouched, since its tell is the *absence*
    // of chordStrumMax in a saved tree and never its value.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "chordStrum", 1 }, "Chord Strum", 0, 200, 30));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "chordStrumMax", 1 }, "Chord Strum Max", 0, 200, 80));
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
    //
    // **Retained, and the reason is stronger than "older sessions carry it"** (2026-08-16, after
    // Owen found this in a parameter listing and asked why there was "a separate velocity knob":
    // there is no knob, and has not been since the range absorbed it). A VST3 host addresses
    // parameters by *index*, so deleting one silently renumbers every parameter after it and
    // repoints every automation lane in every saved Live set onto the wrong control. The three
    // dead parameters above and this one cost an atomic each and nothing else; the delete costs
    // somebody's automation. Same append-only rule the `genSource` choice list follows, read from
    // the other end.
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
    // Eight brains now: the five after Markov are `sources::` (2026-08-01) and **Library** is
    // `chordlib::` (2026-08-18). They are **appended**, which is what makes this safe for a
    // session saved before them: APVTS stores a choice parameter's denormalised value, so a saved
    // 1 is still Markov whatever the list grew to. Never reorder or insert into this list - that
    // is what would silently reopen a session on the wrong brain, and there is no migration hook
    // for it the way `migrateRateMode` covers the arp's clock.
    //
    // Library is the odd one out and worth naming as such: the other seven *compute* a chord
    // sequence, and it looks one up. Everything downstream is the same either way, because they
    // all hand back `chordgen::Chord`.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "genSource", 1 }, "Generator Source",
                                                      juce::StringArray { "Algorithmic", "Markov",
                                                                          "Circle of Fifths", "Neo-Riemannian",
                                                                          "Progressions", "Negative Harmony",
                                                                          "Planing", "Library" },
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
    // **One to eleven** (from two until 2026-08-21, Owen: "I also like to allow one note to show
    // up in the chord pad and the chord preview"), not the 3/4/5 tick boxes these replace. Below
    // three you get dyads, which are a real voicing and not a broken chord; at one you get a bare
    // note, useful for bass lines and pedal tones; above five the stack keeps climbing in thirds
    // through the scale, so eleven is a chord covering every degree and then some. `genOctave`
    // became a pair for the same reason: one octave puts a whole tray in one register, and a
    // range lets a page breathe. Nothing enforces min <= max here, because a parameter cannot see
    // its sibling; the reader swaps them (see `noteCountRange` / `octaveRange`).
    //
    // Nothing downstream needed two - fitVoicing's shrink already guarded `want >= 1`, and
    // applyInversion and applySpread both return a one-note chord unchanged.
    //
    // **Widening an int parameter is safe for sessions and lossy for host automation, and the
    // two are worth keeping apart.** A saved session stores the denormalised value, so every
    // value one of these could previously hold is still in range and still means what it said -
    // no migration, unlike a reordered choice list. A *DAW* stores automation normalised, so a
    // lane written against 2..11 is re-read against 1..11 and lands about one note lower: a
    // point recorded at 3 was 0.111, which now denormalises to 2. That is unfixable from here
    // (nothing distinguishes an old lane from a new one) and small enough that it was Owen's
    // call to take, but it is a real change to an existing session's automation and the
    // changelog says so rather than calling the widening safe outright.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "genNotesMin", 1 }, "Notes Min", 1, 11, 3));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "genNotesMax", 1 }, "Notes Max", 1, 11, 4));
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

    // Arpeggiator globals, every line's worth (docs/ARP_DESIGN.md). See addArpLineParams.
    for (int line = 0; line < numArpLines; ++line)
        addArpLineParams(layout, line);

    // Launch Quantize, after Ableton's transport-bar Quantization (2026-08-01, Owen's ask:
    // "if you start a new note or something that goes into the next sequence, so it sounds good
    // always"). Off fires a chord the instant you click it, which is what Keys has always done.
    // Anything else holds the click until the next boundary, so a card can only ever land on
    // the grid - which is what makes two lines at two rates performable rather than a race
    // against your own mouse.
    //
    // **Global, not per line.** The whole value of it is that the lines land *together*;
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

    // Tempo Sync (2026-08-02, Owen: "we need a BPM sync toggle to sync with DAW"). Keys
    // already followed the host's tempo whenever the transport rolled and reported one - the
    // "host that is playing always wins" comment above this parameter is describing that -
    // so this does not add following, it adds an *escape hatch* from it. True reproduces
    // today's behaviour exactly: the host wins while it plays a valid tempo, "bpm" above
    // otherwise. False pins the engine to "bpm" even with the host rolling.
    //
    // Appended last, same reasoning as "bpm" itself just above: keep the shuffling of this
    // layout to a minimum, since a VST3 id is a hash of the string id and does not move, but
    // a host's generic parameter list order does.
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "bpmSync", 1 }, "Tempo Sync", true));

    // **Track MIDI -> arp, and it is shut by default** (2026-08-27, Owen: "why do I start
    // recording in Ableton? It starts playing in something different", then "we need it to be
    // easier to turn it off, and it's so unclear where that's hiding").
    //
    // Per-line "Play" (apKeys) defaults ON and always has, for a good reason: a line you just
    // switched on that does nothing until you find a second toggle reads as broken. But that
    // one switch was answering two questions at once. It says "Play", its tooltip says "what
    // you play on the keyboard", and the stream it lifts is the keybed AND whatever the DAW
    // sends the track. Chord pads were split out of that stream on 2026-08-18 and the track
    // input never was. So six Keys in one Live set, none of them touched, all had every line
    // listening to the track - and pressing record started four of them arpeggiating at once,
    // on tracks nobody was looking at, with nothing on screen saying why. That is the same
    // afternoon docs/MCP.md is written about.
    //
    // So the keybed keeps its per-line switch, and the *track* input gets one of its own:
    // global, because it is one door into the instance and a door shut for A and open for C is
    // not shut (Scale Lock's own reasoning), and on the arp bar, because a control you reach
    // for to make something stop cannot be two clicks inside a line's detail view.
    //
    // **Default false, and that is a behaviour change with teeth.** A new parameter is absent
    // from every saved session, so every existing set takes this default and a clip driving an
    // arp goes quiet until the chip is switched back on. That is deliberate and was Owen's
    // call when shown both: the surprise is silent and the fix is one click, where the old
    // default was silent in the other direction and the fix was seven toggles across four
    // windows. Appended last, the usual rule.
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "arpTrackMidi", 1 },
                                                    "Track MIDI to Arp", false));

    return layout;
}

// One arpeggiator line's parameters. Dot and Tuplet are separate controls rather than entries
// in the rate list so automating the rate stays on even divisions (Serum's documented
// rationale); Anchor picks bar-affixed vs free-running.
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
                                                                    "Random", "Random Other", "Random Once", "Chord",
                                                                    "Fingered Bottom", "Fingered Top" }, 0));
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
    //
    // **11 since 2026-08-23**, where it was 0 (Owen: "... and H.TIME to have the range on and
    // enabled by default", then, holding up a card reading "0-22": "want default arp settings").
    // The knob is the *centre* of the wander and HumanizeSpan below is already fully open by
    // default, so 11 draws as **0-22** on the card and plays 0 to about 5 ms late - the reach
    // stops at the low rail, which is what keeps a hit from ever landing early. Every line takes
    // it, but B, C and D are off by default, so what a fresh instance actually hears is line A a
    // few milliseconds behind the grid. Enough that a run is not machine-stiff, little enough
    // that it never reads as sloppy against the grid.
    //
    // It read **24** (0-48, up to 12 ms) for a few hours the same day, between that first ask
    // and the second. Nothing shipped on it; the number is here only so a changelog or a
    // screenshot from that window can be placed.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("Humanize"), 1 }, nm + " Humanize", 0, 100, 11));

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
    // the lines can drive different sounds. Note this buys nothing in Keys Host until
    // the hosted instrument is itself multitimbral - it is for the DAW case.
    {
        StringArray channels { "Global" };
        for (int ch = 1; ch <= 16; ++ch)
            channels.add(String(ch));
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id("Channel"), 1 },
                                                          nm + " Channel", channels, 0));
    }

    // Appended 2026-08-02, both defaulting to what the arp did before them.
    //
    // Octave: transposes the whole run, centred at 0 so it goes down as readily as up. This is
    // *not* Octaves, which stacks copies of the chord upward and can only ever widen the run -
    // "how high does it sit" and "how far does it reach" are different questions, and only the
    // first one has a middle. It is what the macro row's OCT knob drives; Octaves stays on the
    // per-line tab, where the rest of the stacking controls (Distance) already live.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("OctShift"), 1 }, nm + " Octave", -3, 3, 0));
    // Volume: the plain output level an arpeggiator wants and this one never had. With two
    // lines running, balancing them used to mean playing one of them softer.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("Volume"), 1 }, nm + " Volume", 0, 100, 100));
    // The velocity half of Humanize, split out (Owen, 2026-08-02: "I don't know if we wanted
    // to randomize velocity and timing. Maybe we could split it up into two knobs"). Humanize
    // above is timing-only from the same day; a session saved before the split keeps its
    // Humanize amount as the timing half and this defaults to none.
    // 0..127 since 2026-08-18, in **MIDI velocity units**: it is how far under VelLevel a hit may
    // fall, so the knob and its ring read as one band in one unit. Widened rather than replaced -
    // every value a session could already hold (0..100) is still in range and 0 still means no
    // wander. What a *set* value means does change, since the whole knob's
    // meaning did; a host automation lane for it rescales.
    //
    // **20 since 2026-08-23**, where it was 0 (Owen, the same ask as H.TIME above). It reaches
    // either side of VelLevel, and the reach stops at the nearer rail - min(level, 127 - level)
    // - so the level beside it is what decides how far this can ever reach. It moved alongside a
    // level that went 100 -> 42 in the same stroke, and the two are one decision: at the old 100
    // the ceiling was +/-27 however far the ring was wound, and at 42 it is +/-42, so the knob
    // has somewhere to go. The number to remember before widening it further is still that
    // ceiling - past `min(level, 127 - level)` this stops doing anything at all. (It read **18**
    // for a few hours between the two asks; nothing shipped on it.)
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("HumanVel"), 1 }, nm + " Human Velocity", 0, 127, 20));
    // The bipolar velocity control that replaced VOL on the macro row (Owen, same day: "it
    // should start in the middle so you can turn it up or down"). 0 plays velocities as they
    // came, +100 doubles them, -100 mutes. Volume above stays registered for old sessions,
    // which migrateVelTrim folds into this on load; nothing in the UI writes Volume now.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("VelTrim"), 1 }, nm + " Velocity", -100, 100, 0));
    // Tuplet, the general form of the Trip toggle above (Owen, 2026-08-03: "what if I want 1/5
    // or other division?"). Trip could say one thing, 3-in-the-space-of-2; this says any of the
    // odd divisions a beat is worth dividing into. Off is the default, so a session saved before
    // it plays straight - which is what a session with Trip off did - and migrateTuplet turns a
    // session with Trip *on* into a 3 so it plays the same too.
    //
    // A choice rather than an int 1..9: the even numbers are not tuplets. 4 in the space of 4 is
    // straight and 6 in the space of 4 is the same length as a triplet at half the division, so
    // an int would have spent half its travel on values that duplicate a division the dial can
    // already reach. Appending to the list is safe; inserting is not (see tupletChoices).
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id("Tuplet"), 1 }, nm + " Tuplet",
                                                      tupletChoices(), 0));
    // The Humanize spans (2026-08-03, Owen: "a serum style knob where you can set a range in
    // the knob"). Each Humanize control was "random between nothing and the knob"; the span
    // says how far under the knob the draw may fall, so the knob keeps meaning "the most this
    // ever does" and the range **travels with it** - which is the behaviour Serum's mod ring
    // has and the half of this Owen asked for by name. A line can then be *always* a little
    // late and a little softer with variation on top, rather than everything anchored to zero.
    //
    // Default 100 is what makes them safe to append: a span of the whole scale puts the floor
    // at zero wherever the knob sits, which is exactly what these two did alone, so a session
    // that never heard of them plays the same. The engine clamps the floor to its own ceiling
    // (see ArpEngine::Params), so nothing here has to police the pair.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("HumanizeSpan"), 1 },
                                                   nm + " Human Time Range", 0, 100, 100));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("HumanVelSpan"), 1 },
                                                   nm + " Human Velocity Range", 0, 100, 100));

    // Drift (2026-08-14, Owen: "there should be, like, a more random feature in the drawing,
    // like cthulu"). Roll rerolls a lane once and you can see what it did; this strays from
    // whatever is drawn *while it plays*, so the part never repeats exactly and the lane on
    // screen never changes. Appended, default 0, which is the engine exactly as it was.
    //
    // It only ever touches the lanes ArpEngine::laneDrifts allows - the ones that decide *how*
    // a step plays, never *which* note it plays. See that table for why that split lets this be
    // one knob instead of ten.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("Drift"), 1 },
                                                   nm + " Drift", 0, 100, 0));

    // Mutate and Lock (2026-08-18, Owen: "explore the chance knob being a drift instead where
    // it explores other patterns and notes ... could be multiple knobs. want notes. mutations").
    //
    // Drift above is forbidden to touch which note plays, and that rule is not being reversed
    // here - it is being met. The fear behind it was a machine wandering onto notes nobody
    // aimed at, and to the knob's halfway point **Mutate cannot leave the held chord**: it
    // moves the run to a different note *of the chord you are holding*. Past halfway it can -
    // in-scale neighbours first, chromatic near the top - and that is Owen's own ask
    // (2026-08-19: "higher values can go out of scale"), a reach you dial into rather than a
    // machine deciding for you; see ArpEngine::mutatedPitch for the zones. `laneRand` is still
    // the only thing allowed to change a note you drew, because you drew it there; this
    // changes which note the *run* lands on.
    //
    // Lock is the Turing Machine's own control (docs/SEQUENCER_LANDSCAPE.md ranks it as the
    // one randomness Keys lacks): at 0 the variation is redrawn every pass, at 100 the first
    // one found repeats forever, and in between it holds for a while and then moves on. That
    // is what makes Mutate a composer rather than a noise source - a wander you can keep.
    //
    // Both are stateless from the playhead, like everything here except laneChain: the
    // variation is a hash of (step, era), not a register the engine carries between blocks,
    // so a transport jump lands on the same variation it would have walked to.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("Mutate"), 1 },
                                                   nm + " Mutate", 0, 100, 0));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("MutateLock"), 1 },
                                                   nm + " Mutate Lock", 0, 100, 0));

    // **VEL as MIDI velocity** (2026-08-18, Owen on the macro card's readout reading "-31 ~20":
    // "still wrong", having just asked for velocity ranges to span 0-127). VelTrim above was a
    // bipolar *percentage* trim on the velocity that arrived, which put percentages on a control
    // called VEL beside a pads knob that had just become an absolute 0-127 band.
    //
    // This is the band's top, in MIDI velocity; HumanVel is how far under it a hit may fall, in
    // the same units. 0 mutes the line, exactly as VelTrim -100 did. Appended, so a session saved
    // before it opens with VelTrim still holding its level and migrateVelLevel converting it.
    //
    // **42 since 2026-08-23**, where it was 100 (Owen, of the band a card was showing at the
    // time: "want default arp settings"). 100 was chosen as "loud without being pinned", and it
    // carries a cost the halo redesign introduced and nobody re-measured: Humanize Velocity
    // reaches equally both ways and stops at the nearer rail, so at a level of 100 the widest
    // band it can ever draw is +/-27 however far the ring is wound. At 42 it can reach +/-42, so
    // the ring is a control with room to work in rather than one clamped by its neighbour, and a
    // line leaves headroom over the instrument it drives instead of arriving at full tilt.
    // A session that predates this parameter does not take the default at all - migrateVelLevel
    // computes a level that plays that session at the loudness it was saved at.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("VelLevel"), 1 },
                                                   nm + " Velocity Level", 0, 127, 42));

    // Two fixed harmony voices per line (2026-08-19, Owen holding up BigSky's shimmer
    // interval list: "2 harmony drop down like the photo. and each of those has a chance knob
    // which effects the harmony probability"). Each is an interval in semitones added to every
    // note the line plays - the note Mutate actually landed on, so the voice follows the run -
    // and a chance saying how often it fires, rolled per step per voice. Chromatic on purpose:
    // the list names intervals, and a Major 3rd means four semitones whatever the scale says,
    // which is what makes this the shimmer control rather than a third copy of the Harmony
    // lane's chord-tone counting. Off and 100 is the engine exactly as it was, which is what
    // makes the four safe to append.
    for (int voice = 1; voice <= 2; ++voice)
    {
        const auto v = juce::String(voice);
        layout.add(std::make_unique<AudioParameterChoice>(
            ParameterID { id(("Harm" + v).toRawUTF8()), 1 },
            nm + " Harmony " + v, harmonyChoices(), 0));
        layout.add(std::make_unique<AudioParameterInt>(
            ParameterID { id(("Harm" + v + "Chance").toRawUTF8()), 1 },
            nm + " Harmony " + v + " Chance", 0, 100, 100));
    }

    // **Stray** (2026-08-21, Owen: "the mutate doesn't really work the way I want ... it's
    // adding additional notes in the arpeggiator ... it should just change the existing ones").
    //
    // Mutate carried this on its own upper half from 2026-08-19, and what Owen heard was
    // exactly what that stage does: pitches arriving that are in no chord he played. The two
    // halves were answering different questions - Mutate moves the run to another note of the
    // chord you are holding, this puts a note somewhere you did not - and one dial could not
    // offer the first without eventually forcing the second. So this is the second question,
    // asked separately, and Mutate is confined to the chord at every setting again.
    //
    // Default **0**, which is off: no session that predates this parameter can acquire a note
    // it was not already playing, and off is a position you can stay at while turning Mutate
    // all the way up. Zero is its own off switch, so it takes no toggle beside it - the same
    // reading that leaves Strum, Lock Influence and Lean without one.
    //
    // Its own two zones: to 50 a stray is an in-scale neighbour, past 50 a growing share are
    // chromatic, all of them at 100. Lock still holds them, so a wrong note worth keeping
    // hardens into the part exactly as a mutated one does.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("Stray"), 1 },
                                                   nm + " Stray", 0, 100, 0));
}

// The N a choice index means. Off is 0 rather than 1 so "is there a tuplet at all" is one test
// against zero everywhere, and ArpEngine::tupletFactor treats both as straight.
int KeysProcessor::tupletFor(int choiceIndex)
{
    static constexpr int values[] = { 0, 3, 5, 7, 9 };
    return values[(size_t) juce::jlimit(0, (int) (sizeof(values) / sizeof(values[0])) - 1, choiceIndex)];
}

// BigSky's shimmer list (the photo Owen held up), minus its two cents rows - MIDI semitones
// cannot say ten cents. Ordered as the pedal orders it, descending intervals then ascending,
// so anyone who knows the pedal reads this list as the same list.
//
// **One table with three columns, not three tables that must agree** (2026-08-21, in review).
// It was a StringArray and two `int[]`s, each of which had to be appended to together, with a
// `jassert` in each of the two comparing its length against the StringArray's. That is the
// `buildLaneRow`-versus-`laneRange` shape CLAUDE.md already logs: three tables that must agree
// is three tables that will not, and a comment naming the hazard does not remove it. Here the
// name and both intervals sit in one row, so appending is a single edit that cannot be half
// done, and `harmonyChoices()` is built *from* this rather than kept beside it.
//
// The jasserts went with it, and that is a fix rather than a loss: they called `harmonyChoices()`,
// which builds a 27-entry StringArray of heap Strings - and `harmonySemisFor` is called from
// `runArpLines`, on the **audio thread**, four times a line every block. A Debug build was
// allocating sixteen StringArrays a block to check a drift that is now impossible by
// construction. Nothing may allocate there; see the invariant in CLAUDE.md.
namespace
{
    struct HarmonyEntry
    {
        const char* name;
        int semis;  // the interval the voice carries
        int second; // a second interval for the one entry that names a pair, 0 for none
    };

    // The one entry with a second interval is "+ Octave & 5th": an octave *and* a fifth, two
    // notes off the note being harmonised, which is what the ampersand says and what the pedal
    // means. **The fifth is the one above the octave (19), not below it (7)**, because the list
    // is ordered by ascending interval and this row sits between "+ Octave" and "+ 2 Octaves".
    // It read 12 and 7 for an afternoon, which made the pair an exact duplicate of two rows the
    // list already has and made the entry reach *lower* than the one above it.
    constexpr HarmonyEntry harmonyTable[] = {
        { "Off",             0,   0 },
        { "- Octave",      -12,   0 }, { "- Major 7th",   -11,  0 },
        { "- minor 7th",   -10,   0 }, { "- Major 6th",    -9,  0 },
        { "- minor 6th",    -8,   0 }, { "- Perfect 5th",  -7,  0 },
        { "- Tritone",      -6,   0 }, { "- Perfect 4th",  -5,  0 },
        { "- Major 3rd",    -4,   0 }, { "- minor 3rd",    -3,  0 },
        { "- Major 2nd",    -2,   0 }, { "- minor 2nd",    -1,  0 },
        { "+ minor 2nd",     1,   0 }, { "+ Major 2nd",     2,  0 },
        { "+ minor 3rd",     3,   0 }, { "+ Major 3rd",     4,  0 },
        { "+ Perfect 4th",   5,   0 }, { "+ Tritone",       6,  0 },
        { "+ Perfect 5th",   7,   0 }, { "+ minor 6th",     8,  0 },
        { "+ Major 6th",     9,   0 }, { "+ minor 7th",    10,  0 },
        { "+ Major 7th",    11,   0 }, { "+ Octave",       12,  0 },
        { "+ Octave & 5th", 12,  19 }, { "+ 2 Octaves",    24,  0 },
    };

    constexpr int harmonyEntryCount = (int) std::size(harmonyTable);

    // Not constexpr: juce::jlimit is not, in this JUCE. It does not need to be - the *table*
    // is constexpr, which is the half that matters, and this is an indexed read either way.
    inline const HarmonyEntry& harmonyEntry(int choiceIndex) noexcept
    {
        return harmonyTable[(size_t) juce::jlimit(0, harmonyEntryCount - 1, choiceIndex)];
    }
} // namespace

juce::StringArray KeysProcessor::harmonyChoices()
{
    juce::StringArray out;
    for (const auto& e : harmonyTable)
        out.add(e.name);
    return out;
}

// Called from runArpLines on the audio thread: a plain indexed read of a constexpr table, no
// allocation and no call into harmonyChoices().
int KeysProcessor::harmonySemisFor(int choiceIndex)
{
    return harmonyEntry(choiceIndex).semis;
}

// The **second** interval a voice may carry, 0 for none (2026-08-21, Owen: "when you select
// octave plus fifth, it looks like it only just does octave"). A second interval per slot
// rather than two more voices: it is still one voice, so it shares its slot's chance roll and
// either both pitches fire or neither.
int KeysProcessor::harmonySemisSecondFor(int choiceIndex)
{
    return harmonyEntry(choiceIndex).second;
}

// The id suffix of each per-line parameter, one table so the audio thread's cached pointers,
// the UI's attachments and createLayout's registrations cannot drift apart. These strings are
// the parameter ids: renaming one loses that setting out of every saved session.
const char* KeysProcessor::arpParamSuffix(int which)
{
    static const char* const suffixes[numArpParams] = {
        "On", "Rate", "RateFree", "RateHz", "Dot", "Trip", "Anchor", "Direction", "Pattern",
        "LinkLanes", "Octaves", "Swing", "Latch", "Retrigger", "Gate", "Chance", "Distance",
        "Offset", "RetrigBars", "VelRamp", "RampBeats", "Humanize", "Keys", "Channel",
        "OctShift", "Volume", "HumanVel", "VelTrim", "Tuplet", "HumanizeSpan", "HumanVelSpan",
        "Drift", "VelLevel", "Mutate", "MutateLock",
        "Harm1", "Harm1Chance", "Harm2", "Harm2Chance",
        "Stray"
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
    // The one choke point for "does this line exist". A line the UI does not show has no way to
    // be switched off either, so it must not be able to sound: a session saved while a hidden
    // line was running would otherwise arpeggiate forever with no control anywhere on screen.
    // Answering false here makes it inert everywhere at once - runArpLines skips its engine and
    // its keys, cardsFeedArp stops counting it, and the bar's Hold off greys correctly - while
    // its stored parameter keeps whatever value it had. All four lines show today (2026-08-19),
    // so the guard is only the range check; it stays because uiArpLines can move again.
    if (line < 0 || line >= uiArpLines)
        return false;
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
    // Clamped to what the UI shows, not to what exists: a session saved with C current has to
    // come back pointing at a line the letter chip can name and a card click can reach.
    return juce::jlimit(0, uiArpLines - 1, layout.arpLine);
}

void KeysProcessor::setArpCurrentLine(int line)
{
    layout.arpLine = juce::jlimit(0, uiArpLines - 1, line);
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

    // The generator's key and mode drive the keyboard's Root and Scale, so the keybed greys to
    // the key you are generating in (see parameterChanged). Registered after everything else
    // exists, since the callback reaches back into apvts.
    apvts.addParameterListener("genRoot", this);
    apvts.addParameterListener("genMode", this);

    // Last thing the constructor does: everything else this processor owns already
    // exists by the time the MCP bridge can be reached from another thread.
    mcpBridge = std::make_unique<KeysMcp>(*this);
}

void KeysProcessor::parameterChanged(const juce::String&, float)
{
    // Which of the two moved does not matter - both are re-applied together, so one listener
    // body covers them and a session load that changes both settles on one pass.
    pendingGenKeyMirror.set(1);
}

void KeysProcessor::mirrorGenKeyToScale()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (pendingGenKeyMirror.exchange(0) == 0)
        return;

    // Written through setValueNotifyingHost so a host sees the move and any attached combo
    // follows it, and only when it actually differs: writing an unchanged value would push a
    // gesture-less parameter change at the host on every heartbeat this fires from.
    const auto setChoice = [this](const char* id, int index)
    {
        auto* param = apvts.getParameter(id);
        if (param == nullptr || index < 0)
            return;
        const float wanted = param->convertTo0to1((float) index);
        if (std::abs(param->getValue() - wanted) > 1.0e-4f)
            param->setValueNotifyingHost(wanted);
    };

    setChoice("root", (int) apvts.getRawParameterValue("genRoot")->load());
    setChoice("scale", modes::kitScaleIndexFor((int) apvts.getRawParameterValue("genMode")->load()));
}

KeysProcessor::~KeysProcessor()
{
    apvts.removeParameterListener("genRoot", this);
    apvts.removeParameterListener("genMode", this);
    // A take in progress is written here rather than lost. "Stopping writes the file, so a take
    // is never a thing a click can lose" is the feature's own contract, and closing the set,
    // deleting the plugin from the track or quitting the host is exactly the click that lost it:
    // nothing else on the way down calls writeTake. Cheap, and it runs before the timers stop
    // because writeTake needs neither.
    if (recording.load(std::memory_order_relaxed))
    {
        setRecording(false); // drains the last blocks the audio thread wrote
        writeTake();
    }

    // Stop taking MCP calls before anything else tears down.
    mcpBridge.reset();
    heartbeat.stopTimer();
    stopTimer();
    deferred.clear();
}

void KeysProcessor::updateTrackProperties(const TrackProperties& props)
{
    // **Merge, never replace, and that is the entire trick.** Ableton does not hand a plugin
    // its track once: it makes three calls per instance as a set loads - an empty name with a
    // default colour, then the real name with a default colour, then an empty name with the
    // real colour. So the obvious implementation (store what you were given) is handed the
    // name in call two and throws it away in call three, and the symptom is a track name that
    // works until something else on the track changes and then silently goes blank.
    //
    // JUCE hands over a freshly constructed TrackProperties each call, filling only the fields
    // the host actually provided (juce_audio_plugin_client_VST3.cpp, setChannelContextInfos),
    // so an absent field is nullopt. But Live's own empty calls report a *present, empty*
    // string, which is why nullopt alone is not enough of a test and the emptiness is checked
    // as well. The colour is guarded the same way: a default-constructed juce::Colour is
    // transparent black, and Live's placeholder is transparent, so alpha is what separates a
    // real colour from a spacer.
    if (props.name.has_value() && props.name->isNotEmpty())
        trackName = *props.name;
    if (props.colour.has_value() && props.colour->getAlpha() != 0)
        trackColour = props.colour->toDisplayString(false);
}

void KeysProcessor::resetAllParameters()
{
    // Silence first, and it is not politeness. A reset moves Root, Octave, Scale Lock and the
    // arp's whole routing underneath whatever is currently sounding, and the played note is
    // resolved at press time and remembered - so a note held across this would be released
    // against settings that no longer exist, or not released at all. allNotesOff() is the one
    // choke point that clears every source: the pads, the live card, each line's held chord,
    // the chain and anything waiting on a quantize boundary.
    allNotesOff();

    // setValueNotifyingHost, not a direct write: the host has to see these move or its
    // automation lane and its own generic editor go on showing the old values, and every
    // attachment in the editor is listening for exactly this notification. The value is
    // normalised 0..1, which is what getDefaultValue already returns.
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
            ranged->setValueNotifyingHost(ranged->getDefaultValue());
}

bool KeysProcessor::arpTrackMidiOn() const
{
    return apvts.getRawParameterValue("arpTrackMidi")->load() > 0.5f;
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
    return juce::jlimit(0.0f, 1.0f, mid / 127.0f); // 0..127 straight through; see the layout
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

void KeysProcessor::clearChordPadPage()
{
    // One entry for the whole page, not one per pad: this is a single gesture and the twelve
    // cards it empties are what you would want back together. UndoGesture absorbs the nested
    // pushes clearChordPad's own callers would otherwise make, the same way Fill and Regen use
    // it. Nothing is pushed at all when there is nothing to clear, so a click on a row that
    // should have been greyed cannot bury a real entry under an empty one.
    if (! pageHasClearablePads())
        return;

    const UndoGesture undoable { *this, "Clear page", UndoScope::pads };
    const int offset = padPageOffset();
    for (int v = 0; v < padsPerPage; ++v)
        if (! chordPads[(size_t) (offset + v)].locked)
            clearChordPad(offset + v); // releases a ringing card and any line it is feeding
}

bool KeysProcessor::pageHasClearablePads() const
{
    const int offset = padPageOffset();
    for (int v = 0; v < padsPerPage; ++v)
    {
        const auto& pad = chordPads[(size_t) (offset + v)];
        if (! pad.locked && ! pad.notes.empty())
            return true;
    }
    return false;
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

void KeysProcessor::stopAllChordPads(bool includeArpHolds, bool includeKeybed)
{
    for (int i = 0; i < numChordPads; ++i)
        stopChordPad(i);
    // The keybed's own holds, through the editor, because they are the one chord source the
    // processor does not own (2026-08-16). `includeKeybed` false has exactly one caller and it
    // is pressLiveChord: that chord *is* what the keybed is holding, so clearing it there would
    // unlatch the keys in the same breath as firing them and leave the card playing a chord the
    // surface has just forgotten.
    if (includeKeybed && releaseKeybedHolds)
        releaseKeybedHolds();
    // Every chord source, not just the pads. Exclusive is a rule about *sources* - one chord
    // at a time, whichever surface started it - so it has to reach the live card and the
    // chord held into the arp as well, or a lit Exclusive quietly does nothing in one
    // direction and the chords pile up.
    releaseLiveChord(true);
    if (includeArpHolds)
        for (int n = 0; n < numArpLines; ++n)
            releaseArpChord(n);
}

// What a scheduling tag is still sounding, or nullptr if the tag names no chord source. The
// tags are the ones fireChord already takes, so this is a lookup rather than a second notion of
// which sources exist.
const std::vector<int>* KeysProcessor::soundingForTag(int tag) const
{
    if (tag >= 0 && tag < numChordPads)
        return &chordPadOn[(size_t) tag];
    if (tag == liveChordTag)
        return &liveChordOn;
    if (const int line = arpChordTag - tag; tag <= arpChordTag && line < numArpLines)
        return &arpHeldNotes(line);
    return nullptr;
}

// The read half of the same list. See the header for why this is not isNoteSounding().
//
// **One chord, not the union of every source** (2026-08-16, Owen: "the currently held chord
// should disappear when you play a new chord pad"). It unioned all of them for a few hours,
// which is wrong on the plainest reading of its own name: with Sustain down, or Exclusive off,
// pressing a second pad left the card naming the pile of both rather than the chord you just
// played. "The currently held chord" is singular, so this answers with one.
std::vector<int> KeysProcessor::heldChordNotes() const
{
    // The last source to start, while it is still sounding.
    if (const auto* v = soundingForTag(lastChordSource); v != nullptr && ! v->empty())
        return *v;

    // It has been released, so fall back to anything still holding a chord - a pad left ringing
    // by Sustain is genuinely still held, and going empty here would be a card that forgets a
    // chord you can still hear. Pads first, then the live card, then the lines; ordering only
    // decides which of several *older* holds wins, and there is no recency left to consult.
    for (int i = 0; i < numChordPads; ++i)
        if (! chordPadOn[(size_t) i].empty())
            return chordPadOn[(size_t) i];
    if (! liveChordOn.empty())
        return liveChordOn;
    // Not gated on arpLineOn: a line that is off still takes chords in and still holds what it
    // was handed (see holdArpChord), and the card is asking what is *held*. A chord you dropped
    // onto a line before switching it on is one you must still be able to pick back up.
    for (int n = 0; n < numArpLines; ++n)
        if (! arpHeldNotes(n).empty())
            return arpHeldNotes(n);
    return {};
}

void KeysProcessor::pressChordPad(int i)
{
    if (i < 0 || i >= numChordPads)
        return;
    if (chordPads[(size_t) i].notes.empty())
        return;

    // **A pad always chokes the other pads** (2026-08-16, Owen: "when you click a pad it should
    // clear other presses"). It used to stop only the pad being re-pressed unless Exclusive was
    // lit, so with Exclusive off - or with Sustain holding them - clicking round a page stacked
    // chord on chord into a pile that is neither of them and cannot be named or dragged as
    // either. Pads are one surface and one voice: the strip is a palette you pick *from*.
    //
    // Exclusive keeps its job and it is now a sharper one: whether a pad also chokes the *other*
    // sources - the live card's own gesture and the chord held into each arp line. Those are
    // different instruments, so stacking them is a real thing to want; stacking two pads was not.
    if (apvts.getRawParameterValue("chordExclusive")->load() > 0.5f)
    {
        // Every source at once - except the arp lines while **Keep arp running** is ticked
        // (2026-08-26). See LayoutState::padsKeepArpRunning: a press on this strip is playing a
        // chord, and a line's held chord is the input to a machine rather than something you
        // are playing, so leaning on a card to hear it should not stop the lines. A *drop* on a
        // line still replaces that line's chord, and still chokes the pads and the live card.
        stopAllChordPads(/*includeArpHolds*/ ! layout.padsKeepArpRunning);
    }
    else
    {
        for (int j = 0; j < numChordPads; ++j)
            stopChordPad(j); // including `i` itself, so re-pressing a sounding pad re-triggers
    }

    // Honour the Voices cap. The keyboard steals oldest-first across its own notes; a pad
    // fires as one gesture, so there is no "oldest" within it — drop the highest notes and
    // keep the lowest, matching how the keyboard resolves a too-big simultaneous chord.
    // (The cap applies per source: a pad and the keyboard each fit under it separately.)
    chordPadOn[(size_t) i] = fireChord(chordPads[(size_t) i].notes, i);
    lastChordSource = i; // the live card names *this* chord now, not the pile of every pad
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
    // **Nothing bound for a line's queue is raked** (2026-08-23, forced by Strum's own default
    // going nonzero). `dest` > 0 means these notes are the *input* to an arp line: only that
    // engine sees them, so a rake there is inaudible by construction and all it does is stagger
    // when the engine learns each note. At 30-80 ms that is most of a 1/16 at 120 bpm, so the
    // first steps of a run would fire on half a chord. Exactly the rule the Humanize velocity
    // range already follows in noteOn, and for the same reason: a line has its own feel controls
    // (Swing, H.TIME) and its input is not the place to apply the strip's.
    const double strumLo = dest == 0 ? apvts.getRawParameterValue("chordStrum")->load() : 0.0;
    const double strumHi = dest == 0 ? apvts.getRawParameterValue("chordStrumMax")->load() : 0.0;
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
        // asChord true: this is a chord, not something played on the keys, so on the track
        // output it takes the queue a listening arp line cannot lift. It says nothing when
        // `dest` names a line - that chord was routed to it on purpose. See noteOn.
        scheduleNoteOn(order[(size_t) k], vel, 0, delayMs, tag, dest, /*asChord*/ true); // noteOn adds Humanize per note
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
        stopAllChordPads(/*includeArpHolds*/ ! layout.padsKeepArpRunning, /*includeKeybed*/ false);
    liveChordOn = fireChord(notes, liveChordTag);
    lastChordSource = liveChordTag;
}

void KeysProcessor::releaseLiveChord(bool force)
{
    if (! force && apvts.getRawParameterValue("sustain")->load() > 0.5f)
        return; // pedal down: leave it ringing, same as a pad
    releaseNotes(liveChordOn, liveChordTag);
}

void KeysProcessor::scheduleNoteOn(int note, float vel01, int channel, double delayMs, int padSlot,
                                   int dest, bool asChord)
{
    if (delayMs <= 0.0)
    {
        noteOn(note, vel01, 0.0, channel, dest, asChord);
        return;
    }

    const double at = juce::Time::getMillisecondCounterHiRes() + delayMs;
    const DeferredNote d { note, vel01, channel, at, padSlot, dest, asChord };
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
            noteOn(n.note, n.vel01, 0.0, n.channel, n.dest, n.asChord);
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

// The same answer, split one finer: the track output has two queues and `asChord` picks between
// them. A line's own input has one, so `asChord` says nothing there - a chord handed to a line is
// meant for that line and has already been routed by `dest`.
juce::MidiMessageCollector& KeysProcessor::chordQueueFor(int dest, bool asChord)
{
    if (dest == 0 && asChord)
        return chordCollector;
    return collectorFor(dest);
}

void KeysProcessor::noteOn(int midiNote, float velocity01, double delaySeconds, int channelOverride,
                           int dest, bool asChord)
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
    // ...but never for a note bound for an arp line's queue (2026-08-02, Owen: "is it
    // passing it through the humanized volume range? ... this might need its own separate
    // thing"). A line has H.VEL for randomness and VEL for level now, and re-randomizing
    // its input velocity here made VEL's "as played" reference wander per note. dest 0 is
    // the track output - actually playing - and keeps the Humanize range as ever; what the
    // keybed feeds a line keeps it too, because those notes pass through here as playing
    // before runArpLines lifts them.
    if (dest == 0 && apvts.getRawParameterValue("humanize")->load() > 0.5f)
    {
        const int a = (int) apvts.getRawParameterValue("humanizeVelMin")->load();
        const int b = (int) apvts.getRawParameterValue("humanizeVelMax")->load();
        const int lo = juce::jmin(a, b), hi = juce::jmax(a, b);
        const int rnd = rng.nextInt(juce::Range<int>(lo, hi + 1));
        velocity01 = (float) rnd / 127.0f;
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
        // **Never 0, and never lifted either.** A note-on at velocity 0 is a note-off, so the
        // floor is one MIDI unit and not zero. It was 0.04 - about velocity 5 - which quietly
        // lifted the whole bottom of the Humanize band: the control said 1 and you heard 5, and
        // with the band now reaching 0 that lie would have covered its lowest six values. The
        // range *is* the velocity control here, so it has to be taken literally.
        auto m = juce::MidiMessage::noteOn(channel, midiNote,
                                           juce::jlimit(1.0f / 127.0f, 1.0f, velocity01));
        m.setTimeStamp(when);
        // Which of dest 0's two queues this pitch is opening on, recorded before it is sent so
        // the matching note-off can find it again. Only the note-on that actually sounds writes
        // it: a second owner of a pitch already ringing emits nothing, so it neither needs nor
        // gets a say in where the release goes. See chordStream.
        if (dest == 0)
            chordStream[(size_t) midiNote].store(asChord);
        chordQueueFor(dest, asChord).addMessageToQueue(m);
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
        // Down whichever queue the note-on went, not whichever the *releasing* source would
        // choose. The last owner is often not the first, and a note-off that took the other
        // queue would strand the note in a listening line's engine - see chordStream.
        chordQueueFor(dest, dest == 0 && chordStream[(size_t) midiNote].load())
            .addMessageToQueue(m);
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

bool KeysProcessor::keybedLit(int midiNote) const
{
    if (midiNote < 0 || midiNote > 127)
        return false;
    // The track output and the MIDI input are what they are, whatever the arp is doing.
    if (noteRefs[0][(size_t) midiNote].load() > 0 || inputNoteOn[(size_t) midiNote].load())
        return true;
    // A chord handed to an arp line: lit unless that line is running it with Light keys on,
    // in which case it is the run's input and showing it would drown the run. See the header.
    for (int n = 0; n < numArpLines; ++n)
        if (noteRefs[(size_t) (1 + n)][(size_t) midiNote].load() > 0
            && ! (layout.arpLights && arpLineOn(n)))
            return true;
    return arpNoteLit(midiNote);
}

unsigned int KeysProcessor::arpLitLineMask(int midiNote) const
{
    if (midiNote < 0 || midiNote > 127 || ! layout.arpLights)
        return 0u;
    return arpNoteLines[(size_t) midiNote].load();
}

bool KeysProcessor::arpNoteLit(int midiNote) const
{
    return arpLitLineMask(midiNote) != 0u;
}

int KeysProcessor::arpLitLine(int midiNote) const
{
    const unsigned int mask = arpLitLineMask(midiNote);
    if (mask == 0u)
        return -1;
    // Lowest set bit: A over B over C over D. Deterministic and stable while the note is held,
    // which matters more than which line "deserves" it - a key that changed colour as lines
    // came and went on the same pitch would read as the arp having moved, not as an overlap.
    for (int n = 0; n < numArpLines; ++n)
        if ((mask & (1u << n)) != 0u)
            return n;
    return -1;
}

// Audio thread, on one arp line's output buffer just before it is merged. Same shape as
// watchInputNotes and for the same reasons: a flag per pitch rather than a count, because a
// missed note-off would leak a refcount into a key lit forever, and a `changed` bump so the
// surface repaints only when something actually moved.
//
// **One bit per line since 2026-08-22**, which is what lets the keybed paint the key in that
// line's colour. It also fixes what the old single flag had to accept: two lines on one pitch
// shared it, so whichever released first put the key out under the line still playing it.
// Each line clears only its own bit now. Within a line it is still a flag and not a count -
// two harmony voices on one pitch, first note-off wins - the same trade at a smaller scope.
//
// `runArpLines` walks the lines in order on this thread, so no two *lines* race with each other
// - but the message thread does more than read: `clearArpNotes()` writes zeroes from there. The
// per-bit updates below are therefore atomic read-modify-writes rather than load-modify-stores;
// see the note on `set` for what the difference buys.
void KeysProcessor::watchArpNotes(const juce::MidiBuffer& midi, int line)
{
    if (line < 0 || line >= numArpLines)
        return;
    const unsigned int bit = 1u << line;
    bool changed = false;
    const auto set = [&](int note, bool on)
    {
        if (note < 0 || note > 127)
            return;
        auto& cell = arpNoteLines[(size_t) note];
        // **fetch_or / fetch_and, not load-modify-store.** `clearArpNotes()` runs on the
        // *message* thread - `allNotesOff()`, so the All Off chip, a panic and the MCP tool -
        // and stores 0 into every cell. A read, a clear landing between, and a write back would
        // resurrect the bits this line read a moment ago: bits belonging to *other* lines,
        // whose engines the same panic is about to flush, so no further note-off for that pitch
        // would ever arrive and the key would stay lit for the rest of the session. An atomic
        // read-modify-write has no window for the clear to land in, so a clear can only be
        // followed by a line setting its own bit for a note that genuinely is sounding.
        //
        // The previous value comes back from the same call, so `changed` costs no extra load.
        const unsigned int was = on ? cell.fetch_or(bit) : cell.fetch_and(~bit);
        changed = changed || (on ? (was & bit) == 0u : (was & bit) != 0u);
    };

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            set(m.getNoteNumber(), true);
        else if (m.isNoteOff())
            set(m.getNoteNumber(), false);
    }

    if (changed)
        soundingGen.fetch_add(1);
}

void KeysProcessor::clearArpNotes()
{
    for (auto& f : arpNoteLines)
        f.store(0u);
    soundingGen.fetch_add(1);
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
    // dest 0 twice: the track output has two queues since 2026-08-18 and a panic that flushed
    // only one would leave whatever the other is holding to sound on unreleased.
    for (int pass = 0; pass <= numArpLines + 1; ++pass)
    {
        const int dest = pass <= numArpLines ? pass : 0;
        auto& queue = chordQueueFor(dest, /*asChord*/ pass > numArpLines);
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
    for (auto& q : chordStream)
        q.store(false); // nothing is sounding, so no pitch has a queue to go back to
    // ...and the arp lines' shared output counts, which the audio thread clears on its next
    // block. See ArpMerge::reset.
    arpOutClear.store(true, std::memory_order_relaxed);
    soundingGen.fetch_add(1);

    // All Off clears the input lights too. Keys cannot make someone's physical keyboard let
    // go, but if a note-off went missing the lit key it left behind is exactly the kind of
    // stuck thing this button exists to clear; the next key they press lights again.
    clearInputNotes();
    // ...and the arp's own lights, for the same reason: the engine is about to be silenced
    // from under them, so whatever they are showing is already not true.
    clearArpNotes();

    // The chord held into the arp is the one thing here that outlives a note-off, so a
    // panic has to forget it too - otherwise All Off silences it while the launched slot
    // still paints as playing and the next launch tries to release notes already gone.
    // Every line: a panic that left B holding is a panic that did not happen.
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
    chordCollector.reset(sampleRate);
    // Every buffer the arp stage touches is sized here and never grown on the audio thread.
    // Seven of them now rather than one: three inputs, three outputs, and the keybed's notes
    // lifted out of the merged stream for the lines that listen to it.
    keyNotes.ensureSize(8192);
    trackMidiAside.ensureSize(8192);
    streamRest.ensureSize(8192);
    arpMerged.ensureSize(8192);
    arpOut.reset(); // nothing is sounding across a prepare
    arpOutClear.store(false, std::memory_order_relaxed);
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
    // **The track's MIDI is a door of its own, and the chip on the arp bar is the handle.**
    // Held aside here rather than gated inside runArpLines, because by the time that runs the
    // collector has merged and a note from the keybed is indistinguishable from one the DAW
    // sent - the same reason `dest` exists, and the same shape as the chordCollector split one
    // step further down. Everything goes aside, note-offs included: keeping only the note-ons
    // back would let the arp lift a note-off out of the stream whose note-on passed straight
    // through, and the instrument downstream would hang that note for good.
    const bool trackMidiToArp = arpTrackMidiOn();
    trackMidiJustClosed = lastTrackMidiToArp && ! trackMidiToArp;
    lastTrackMidiToArp = trackMidiToArp;

    trackMidiAside.clear();
    if (! trackMidiToArp)
    {
        trackMidiAside.addEvents(midi, 0, -1, 0);
        midi.clear();
    }

    collector.removeNextBlockOfMessages(midi, buffer.getNumSamples());

    // Arp stage: one engine per line, each consuming its own note stream and emitting its own; CCs
    // pass through. The engines read the host playhead (the one deliberate exception to Keys'
    // old never-reads-the-playhead rule; see docs/ARP_DESIGN.md) and free-run on an internal
    // clock at the last-known tempo when the transport is stopped.
    runArpLines(midi, buffer.getNumSamples());

    // Back into the stream, after the arp has taken its copy of what was played and before the
    // chords join it. Untouched and in sample order, so a clip on this track reaches the
    // instrument exactly as it always did - the chip decides what the *arpeggiator* hears, not
    // what the track plays.
    if (! trackMidiToArp)
        midi.addEvents(trackMidiAside, 0, -1, 0);

    // Chords last, and that is the whole of "a click never feeds the arpeggiator" (2026-08-18).
    // A pad, the live card and the generator's audition queue here instead of into `collector`,
    // so by the time they join the outgoing stream every line has already taken its copy of what
    // was played on the keys. They are otherwise ordinary output: same buffer, same channel, same
    // instrument downstream, and captureBlock below still records them as part of the take.
    chordCollector.removeNextBlockOfMessages(midi, buffer.getNumSamples());

    advanceChainClock(buffer.getNumSamples());

    // Last, so the take holds the stream that actually leaves: arpeggiated where a line is
    // running, strummed where a pad strummed, on whichever channel the line sent it on.
    // Recording what the *UI* asked for instead would capture the chord you clicked and not
    // the arpeggio you heard, which is the wrong take by exactly the interesting part.
    // Acquire, pairing with the release store in setRecording: it is what guarantees this thread
    // sees the zeroed captureSamples that arming wrote just before raising the flag.
    if (recording.load(std::memory_order_acquire))
        captureBlock(midi, buffer.getNumSamples());
}

// Audio thread. Writes into the ring and publishes one index; allocates nothing (the ring is
// sized in the constructor) and takes no lock.
void KeysProcessor::captureBlock(const juce::MidiBuffer& midi, int numSamples)
{
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    auto w = captureWrite.load(std::memory_order_relaxed);
    // Read once into a local: this is the only thread that advances it, and the message thread
    // only ever zeroes it before publishing `recording` with release ordering.
    const auto elapsed = captureSamples.load(std::memory_order_relaxed);

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int size = m.getRawDataSize();
        // Three bytes or fewer covers every channel voice message. Sysex, MTC and the rest are
        // not what a played take is made of, and admitting them would mean a variable-size slot
        // and an allocation somewhere on this thread.
        if (size <= 0 || size > 3 || ! m.getChannel())
            continue;

        auto& e = captureRing[(size_t) (w % (juce::uint32) captureCapacity)];
        e.atSec = (double) (elapsed + meta.samplePosition) / sr;
        e.size = (juce::uint8) size;
        const auto* raw = m.getRawData();
        for (int i = 0; i < size; ++i)
            e.bytes[i] = raw[i];
        ++w;
    }

    captureWrite.store(w, std::memory_order_release); // slots filled before the index admits them
    captureSamples.store(elapsed + numSamples, std::memory_order_relaxed);
}

// Message thread, off the 50 Hz heartbeat.
void KeysProcessor::drainCapture()
{
    const auto w = captureWrite.load(std::memory_order_acquire);
    if (w == captureRead)
        return;

    // Lapped: the audio thread has overwritten slots we never read. Cannot happen at any rate a
    // keyboard produces, but reading them anyway would hand back torn events, so skip to the
    // oldest slot still intact and lose the gap rather than the take.
    //
    // The margin is not decoration. `w - captureCapacity` lands on `w % captureCapacity`, which is
    // the exact slot the audio thread is about to fill, so recovering to it read the one torn
    // event this branch exists to skip. `captureLapMargin` blocks' worth of slots past it puts the
    // reader clear of anything a writer could still be inside.
    if (w - captureRead > (juce::uint32) captureCapacity)
        captureRead = w - (juce::uint32) captureCapacity + captureLapMargin;

    for (; captureRead != w; ++captureRead)
        capturedTake.push_back(captureRing[(size_t) (captureRead % (juce::uint32) captureCapacity)]);
}

void KeysProcessor::setRecording(bool shouldRecord)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (shouldRecord == recording.load(std::memory_order_relaxed))
        return;

    if (shouldRecord)
    {
        // Arming starts a new take. Nothing is lost by that: the previous one was written to
        // disk the moment it stopped (see writeTake), so the only thing being cleared here is a
        // copy of a file that already exists.
        //
        // No drain first. There was one for a build, immediately above a `clear()` of the vector
        // it had just filled and a re-read of the cursor it had just advanced - up to 32768
        // push_backs, and the reallocations behind them, to be thrown away one statement later.
        // Skipping to the writer's published index is the whole of what arming needs.
        capturedTake.clear();
        captureRead = captureWrite.load(std::memory_order_acquire);
        captureSamples.store(0, std::memory_order_relaxed);
        // Frozen here rather than read at build time: the file is written once, at stop, and a
        // host tempo that moved afterwards would make every later preview disagree with the
        // bytes already on disk. This is also the tempo you actually played to.
        takeBpm = juce::jlimit(20.0, 999.0, currentTempo());
        // **Release, not relaxed.** Everything above has to be visible to the audio thread before
        // the flag that lets it start writing is: a relaxed store orders nothing, so the zeroed
        // captureSamples could land after it and the first block of a new take would be stamped
        // at the previous take's elapsed time.
        recording.store(true, std::memory_order_release);
        return;
    }

    recording.store(false, std::memory_order_relaxed);
    drainCapture(); // the last block or two the audio thread wrote before the flag went down
}

// Where the take actually begins: the first captured **note-on**. Not the first event - Keys' own
// mod wheel and pitch bend land on the same stream, so a wheel nudged before you played would
// otherwise be the take's zero and push every note that far off the top of the clip.
// Returns nullptr when nothing has been played yet, which is what "no take" means.
const KeysProcessor::CapturedEvent* KeysProcessor::firstCapturedNote() const
{
    for (const auto& e : capturedTake)
        if (juce::MidiMessage(e.bytes, (int) e.size).isNoteOn())
            return &e;
    return nullptr;
}

bool KeysProcessor::capturedHasNotes() const
{
    return firstCapturedNote() != nullptr;
}

double KeysProcessor::capturedSeconds() const
{
    const auto* first = firstCapturedNote();
    if (first == nullptr)
        return 0.0;
    // First note to last event, so arming and then thinking for a minute costs the take nothing -
    // the same offset buildTakeMidiFile shifts away.
    return juce::jmax(0.0, capturedTake.back().atSec - first->atSec);
}

double KeysProcessor::takeTicksPerSecond() const
{
    // One place, so the file and the preview drawn from it can never disagree about time.
    return (double) takeTicksPerQuarter * takeBpm / 60.0;
}

std::vector<KeysProcessor::TakeNote> KeysProcessor::takeNotes() const
{
    std::vector<TakeNote> out;

    // Built from the file's own sequence rather than from `capturedTake`, so the trim, the
    // pairing and the supplied note-offs are applied once and the picture is the bytes.
    juce::MidiFile file;
    if (! buildTakeMidiFile(file) || file.getNumTracks() < 1)
        return out;

    const double ticksPerSecond = takeTicksPerSecond();
    if (ticksPerSecond <= 0.0)
        return out;

    const auto& seq = *file.getTrack(0);
    out.reserve((size_t) seq.getNumEvents() / 2);
    for (int i = 0; i < seq.getNumEvents(); ++i)
    {
        const auto* ev = seq.getEventPointer(i);
        if (! ev->message.isNoteOn())
            continue;

        const double startTicks = ev->message.getTimeStamp();
        const double endTicks = ev->noteOffObject != nullptr ? seq.getTimeOfMatchingKeyUp(i)
                                                             : startTicks;
        // The true length, with no floor under it. A short note has to stay *visible*, but that
        // is a question about drawing and belongs in Roll::paint, which already floors the bar
        // at 2 px. Putting the floor here instead made the preview disagree with the file by up
        // to 10 ms on every short note - which is the one thing this function must never do,
        // and is exactly what the "the preview is the file" test caught.
        out.push_back({ startTicks / ticksPerSecond,
                        (endTicks - startTicks) / ticksPerSecond,
                        ev->message.getNoteNumber(),
                        ev->message.getChannel(),
                        ev->message.getFloatVelocity() });
    }
    return out;
}

bool KeysProcessor::buildTakeMidiFile(juce::MidiFile& out) const
{
    // The take starts when you **played**, not when you armed and not when you brushed a wheel.
    // No note means no take: a file of nothing but controller moves is not something to write,
    // and it is what stops an accidental arm-and-stop overwriting the take you meant to keep.
    const auto* first = firstCapturedNote();
    if (first == nullptr)
        return false;

    constexpr short ticksPerQuarter = takeTicksPerQuarter;
    const double bpm = takeBpm;
    const double ticksPerSecond = takeTicksPerSecond();
    const double zero = first->atSec;

    juce::MidiMessageSequence seq;
    seq.addEvent(juce::MidiMessage::tempoMetaEvent((int) std::llround(60000000.0 / bpm)), 0.0);
    seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
    for (const auto& e : capturedTake)
        // Clamped at zero rather than dropped: a controller move made before the first note is a
        // value the take should open with, and a negative tick would put the sequence out of
        // order and pair the wrong events in updateMatchedPairs.
        seq.addEvent(juce::MidiMessage(e.bytes, (int) e.size),
                     juce::jmax(0.0, (e.atSec - zero) * ticksPerSecond));

    seq.updateMatchedPairs();

    // Anything still ringing when recording stopped has no note-off in the take. Left alone
    // that is a hanging note in the clip - Live holds it until the next stop, which sounds like
    // Keys emitted a stuck note. Give each one an end just past the last event instead.
    const double end = seq.getEndTime() + (double) ticksPerQuarter / 4.0;
    for (int i = seq.getNumEvents(); --i >= 0;)
    {
        const auto* ev = seq.getEventPointer(i);
        if (ev->message.isNoteOn() && ev->noteOffObject == nullptr)
            seq.addEvent(juce::MidiMessage::noteOff(ev->message.getChannel(),
                                                    ev->message.getNoteNumber()), end);
    }
    seq.updateMatchedPairs();

    out.setTicksPerQuarterNote(ticksPerQuarter);
    out.addTrack(seq);
    return true;
}

juce::File KeysProcessor::takeFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("OK Studio")
               .getChildFile("Keys Takes");
}

// Write the take and, **only on success, make it the one the UI offers.**
//
// Every failure here leaves `lastTake` exactly as it was, and that is deliberate in both
// directions. It used to be neither: three of the paths below returned an empty File without
// touching `lastTake`, so a failed write left the chip enabled, captioned with the *new* take's
// duration, and dragging it handed the host the **previous** take - a wrong file reported as the
// right one, which is worse than an error. And the one path that did assign
// (`return lastTake = {}` when there was nothing to record) threw away a good take already on
// disk because you armed REC by accident and stopped again.
//
// So: a take you cannot write does not replace the take you have. `takeWriteFailed` is what the
// editor reads to say so out loud, since silence would be the same bug one step quieter.
juce::File KeysProcessor::writeTake()
{
    JUCE_ASSERT_MESSAGE_THREAD
    takeWriteFailed = false;

    juce::MidiFile file;
    if (! buildTakeMidiFile(file))
        return lastTake; // nothing was played; not a failure, and not a reason to forget a take

    const auto fail = [this]
    {
        takeWriteFailed = true;
        return lastTake;
    };

    auto folder = takeFolder();
    if (! folder.createDirectory().wasOk())
        return fail();

    const auto stamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d-%H%M%S");
    auto out = folder.getChildFile("Keys take " + stamp + ".mid").getNonexistentSibling();

    juce::FileOutputStream stream(out);
    if (! stream.openedOk())
        return fail();
    if (! file.writeTo(stream))
        return fail();
    stream.flush();
    return lastTake = out;
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
// The dice on a line's macro card (2026-08-21, Owen: "I use the random ones a lot, and I'd like
// to have a dice button when those are active nearby to regenerate their pattern"). All it does
// is bump a counter; runArpLines below notices and asks that engine for a new order, so nothing
// on the message thread ever writes engine state. See ArpEngine::rerollRandomOrder.
void KeysProcessor::rerollArpRandom(int line)
{
    if (line < 0 || line >= numArpLines)
        return;
    lines[(size_t) line].rerollRequest.fetch_add(1, std::memory_order_relaxed);
}

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
    arpMerged.clear();
    for (auto& l : lines)
    {
        l.in.clear();
        l.collector.removeNextBlockOfMessages(l.in, numSamples);
    }

    // The keybed and a clip on the track: what arrives in the merged stream by now is what was
    // *played*, and any line that listens gets a copy. Lifting the notes out is what makes the
    // arp replace them rather than double them, and it is skipped entirely when nobody is
    // listening - which is exactly the behaviour of the arp being off.
    //
    // **A pad's chord is not in this stream** (2026-08-18, Owen: "as soon as you click a chord in
    // the pad, it automatically sends it to the arpeggiator ... we only want the arpeggiator to
    // go if you drag a chord on top of it"). It used to be, along with the live card and the
    // generator's audition, and a line with Play on lifted all three - so clicking a pad fed the
    // arpeggiator exactly as pressing a key did, which is the one thing a click has not been
    // allowed to do since 2026-08-02. Play means the keys you play; a chord reaches a line by
    // being dragged onto it, or through the pad menu's Send to arp rows, and either way it goes
    // to that line's own queue above. Those three sources now drain from `chordCollector` in
    // processBlock, after this runs, which is what puts them out of reach here.
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

    // The falling edge of the Track MIDI chip, and it is the one thing that switch cannot be
    // implemented without. A line that had already taken a clip's notes in is holding pitches
    // whose note-offs are now being routed around it for good, so it would arpeggiate them
    // forever under a switch that says the door is shut. Their releases are synthesised here,
    // into the lines' own input and nowhere else: the real note-offs still travel down the
    // output stream when the clip lets go, so the instrument downstream never notices. Matching
    // is by pitch (ArpEngine::noteReleased ignores the channel), and a note-off for a pitch no
    // engine holds costs a scan and does nothing, which is what makes this safe to fire wide.
    if (trackMidiJustClosed)
        for (int n = 0; n < numArpLines; ++n)
            if (listens[(size_t) n])
                for (int note = 0; note < 128; ++note)
                    if (inputNoteOn[(size_t) note].load())
                        lines[(size_t) n].in.addEvent(juce::MidiMessage::noteOff(1, note), 0);

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
                // restart(), not hardReset(): the chord handed to this line while it was off
                // is sitting in the engine's held set, and it is the whole reason there is
                // something to play the instant the switch goes on (2026-08-02, Owen: "when
                // you turn on the arp, it should start playing whatever card is loaded ...
                // right now it only plays when you drop a new line on"). hardReset() threw
                // that chord away, which is exactly what made a freshly switched-on line sit
                // there silent.
                l.engine.restart();
            else
                l.engine.flushInto(l.out); // nothing may ring after bypassing
        }
        // A change of channel is a change of where the notes are going, so what is still
        // ringing has to be closed on the channel it started on. Flushed and merged under the
        // *old* channel before lastChannel moves, because restamping this line's whole output
        // with the new one would send those note-offs somewhere the notes never sounded.
        //
        // Into `arpMerged`, never straight into `midi`: every note-on this line emitted was
        // counted by `arpOut`, so its note-offs have to be counted back down by the same
        // refcounts. Writing them past the merge left `held[ch][note]` stuck above zero for
        // good, and a count that never returns to 0 suppresses the *real* note-off of every
        // later hit on that pitch - a note hung on the instrument for the rest of the session.
        // The flush emits at sample 0, and MidiBuffer keeps its events in sample order, so
        // these still close ahead of anything this block goes on to play.
        if (channel != l.lastChannel)
        {
            l.engine.flushInto(l.out);
            watchArpNotes(l.out, n);
            mergeArpOut(arpMerged, l.out, l.lastChannel);
            l.out.clear();
            l.lastChannel = channel;
        }

        // **There is no bypass branch.** The engine runs every block and `ap.enabled` decides
        // only whether it *fires* steps - `noteArrived` is outside that gate, so a line that is
        // off still takes the chord in and remembers it. That one fact answers both halves of
        // what Owen asked for on 2026-08-02: dropping a card on a line that is off makes no
        // sound ("I don't want it to play the chord sound when you release"), because those
        // note-ons are consumed by the engine instead of passing through to the output; and
        // switching that line on starts it arpeggiating what it is already holding.
        //
        // What this replaces: an `if (! arpOn)` branch that merged `l.in` straight into the
        // output, so the chord sustained like a pad and the engine never saw it. That was the
        // honest reading while a line was a thing you switched *between*; with two of them fed
        // by dragging cards on, "hand it over now, start it when I say" is the gesture.
        //
        // The keybed is unaffected either way: `listens[n]` is false with the line off, so
        // notes you play are never lifted out of the merged stream and sound as they always
        // have. Playing the instrument is never gated on an arp switch.
        ArpEngine::Params ap;
        ap.enabled = arpOn;
        ap.rateIndex = (int) arpParam(n, apRate);
        // Free: the rate is a frequency and the engine free-runs at it whatever the transport
        // is doing. Both read every block like every other arp global, so the mode can be
        // automated and the engine sees the change on the next boundary.
        ap.rateFree = arpParam(n, apRateFree) > 0.5f;
        ap.rateHz = (double) arpParam(n, apRateHz);
        ap.dotted = arpParam(n, apDot) > 0.5f;
        // apTrip is not read here any more: migrateTuplet folds it into Tuplet on load, and
        // nothing on screen writes it. It stays registered so a saved session still parses.
        ap.tuplet = tupletFor((int) arpParam(n, apTuplet));
        ap.humanizeSpan = (int) arpParam(n, apHumanizeSpan);
        ap.humanVelSpan = (int) arpParam(n, apHumanVelSpan);
        ap.drift = (int) arpParam(n, apDrift);
        ap.mutate = (int) arpParam(n, apMutate);
        ap.mutateLock = (int) arpParam(n, apMutateLock);
        ap.mutateSeed = n; // so two lines at the same Mutate never explore in lockstep
        // The two fixed harmony voices, handed over as semitones: the choice index means
        // nothing to the engine, and keeping the interval table out of it is the same
        // decision as the scale mask below.
        ap.harmSemis[0] = harmonySemisFor((int) arpParam(n, apHarm1));
        ap.harmSemisB[0] = harmonySemisSecondFor((int) arpParam(n, apHarm1));
        ap.harmChance[0] = (int) arpParam(n, apHarm1Chance);
        ap.harmSemis[1] = harmonySemisFor((int) arpParam(n, apHarm2));
        ap.harmSemisB[1] = harmonySemisSecondFor((int) arpParam(n, apHarm2));
        ap.harmChance[1] = (int) arpParam(n, apHarm2Chance);
        ap.stray = (int) arpParam(n, apStray);
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
        ap.humanVel = (int) arpParam(n, apHumanVel);
        ap.octShift = (int) arpParam(n, apOctShift);
        ap.volume = (int) arpParam(n, apVolume);
        ap.velLevel = (int) arpParam(n, apVelLevel);
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
        // Scale Lock reaches the line's output as well as the keybed's (2026-08-26). Global,
        // like Root and Scale beside it and unlike everything else in this loop: it is the
        // keybed's own toggle on the Controls bar, and a lock that held on one line and not
        // another would not be a lock. See ArpEngine::snapToMask.
        ap.scaleLock = apvts.getRawParameterValue("scaleLock")->load() > 0.5f;

        ArpEngine::HostClock hc;
        if (auto* playHead = getPlayHead())
            if (auto pos = playHead->getPosition())
            {
                hc.playing = pos->getIsPlaying();
                if (auto bpm = pos->getBpm(); bpm && *bpm > 0.0)
                {
                    hc.bpm = *bpm;
                    hc.hasBpm = true; // an answer from the host, not HostClock's own default
                }
                if (auto ppq = pos->getPpqPosition())
                {
                    hc.ppq = *ppq;
                    hc.hasPpq = true;
                }
            }
        // Every line anchors to one grid, and there is always a grid (2026-08-18). While the host
        // rolls that is its own position, read fresh above; otherwise it is `arpBeats`, the count
        // this processor keeps for Launch Quantize to measure from - the host's position while it
        // has one, its own beats when it does not, which is the only clock the standalone has.
        //
        // Read *before* advanceChainClock adds this block to it, so it is the position at the
        // start of the block, which is what `ppq` means to the engine. All three lines are handed
        // the same number in the same block, which is the entire point: it is what lets two
        // anchored lines walk in lockstep with no transport to follow. `anchored` off still opts a
        // line out into a free-running phase of its own.
        hc.hasGrid = true;
        if (! (hc.playing && hc.hasPpq))
            hc.ppq = arpBeats.load(std::memory_order_relaxed);
        // No transport to follow: run at the BPM control in the Controls section. It used to
        // be the host's last-known tempo, which was unreachable in the standalone (where
        // there is no host at all) and unchangeable everywhere.
        ap.fallbackBpm = (double) apvts.getRawParameterValue("bpm")->load();
        // Tempo Sync: off pins every line to fallbackBpm above even with the host rolling.
        ap.followHost = apvts.getRawParameterValue("bpmSync")->load() > 0.5f;

        // The dice, picked up here so every write to the engine's own state stays on this
        // thread (2026-08-21). Compared rather than tested-and-cleared: the counter is the
        // message thread's alone to write and this side only ever asks whether it moved, so
        // neither thread has to write the other's variable.
        //
        // **It does not mean two clicks in one block are two rerolls** - the comparison fires
        // once however far the counter jumped, and it is right to. rerollRandomOrder() only
        // sets permDirty, so it is idempotent: dealing twice before a step reads the order is
        // indistinguishable from dealing once, and the second deal would be a shuffle nobody
        // could ever hear. What the counter buys over a flag is only the single-writer split
        // above.
        if (const int req = l.rerollRequest.load(std::memory_order_relaxed); req != l.rerollSeen)
        {
            l.rerollSeen = req;
            l.engine.rerollRandomOrder();
        }

        // The engine's input is this line's buffer alone, never the merged stream: that is the
        // whole of the routing. Its output goes into midi with everything the other lines and
        // the pass-through left there, so two lines at two rates simply sum.
        l.engine.process(ap, hc, numSamples, l.in, l.out);
        watchArpNotes(l.out, n);
        // Into the shared buffer rather than straight out: the lines have to be interleaved in
        // time before the overlap rule below can run over them. MidiBuffer keeps its events
        // sorted by sample position, and equal positions stay in insertion order, so a line's
        // note-off and a later line's note-on at the same offset settle in line order.
        mergeArpOut(arpMerged, l.out, channel);
    }

    mergeArpLines(midi);
}

// Audio thread. The rule itself is ArpMerge, beside the engine and free of this class so it can be
// driven from a test with two hand-built buffers; all this does is hand it the block.
void KeysProcessor::mergeArpLines(juce::MidiBuffer& midi)
{
    if (arpOutClear.exchange(false, std::memory_order_relaxed))
        arpOut.reset();
    arpOut.merge(arpMerged, midi);
}

void KeysProcessor::advanceChainClock(int numSamples)
{
    // Tempo and bar length are the same question whichever line is asking, so they are asked
    // once and the three chains are stepped against the same answer.
    double bpm = (double) apvts.getRawParameterValue("bpm")->load();
    // Tempo Sync: off keeps the parameter above even with the host rolling, the same escape
    // hatch ArpEngine::process reads via Params::followHost. hostBpmLive is published below
    // for the Controls bar to show and to grey its own stepper: neither the parameter nor a
    // stepper drag can change anything while the host is the one actually setting the tempo.
    const bool followHost = apvts.getRawParameterValue("bpmSync")->load() > 0.5f;
    bool hostBpmLive = false;
    double beatsPerBar = 4.0;
    if (auto* playHead = getPlayHead())
        if (auto pos = playHead->getPosition())
        {
            // Not gated on getIsPlaying() (2026-08-16, Owen: "bpm isn't syncing with daw"). A
            // DAW's tempo is its tempo stopped or rolling, and following it only while rolling
            // meant the number on the bar disagreed with Live's for exactly as long as you were
            // setting up - which is when you look at it. The *position* read below keeps its
            // `playing` test, because a position genuinely means nothing while stopped.
            if (followHost)
                if (auto hostBpm = pos->getBpm(); hostBpm && *hostBpm > 0.0)
                {
                    bpm = *hostBpm;
                    hostBpmLive = true;
                }
            // A bar is four beats only in four-four. Asking the host costs nothing and is
            // the difference between a chain that lands on the bar in 3/4 and one that does
            // not; with no host to ask, four it is.
            if (auto sig = pos->getTimeSignature(); sig && sig->denominator > 0)
                beatsPerBar = 4.0 * (double) sig->numerator / (double) sig->denominator;
        }
    arpHostBpmLive.store(hostBpmLive, std::memory_order_relaxed);

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

// --- Undo -------------------------------------------------------------------------------
//
// See the header for why this is content-only and why an entry is a whole-subtree snapshot
// rather than a hand-written inverse of each action.

juce::ValueTree KeysProcessor::snapshotFor(UndoScope scope) const
{
    return scope == UndoScope::pads ? chordPadsToTree() : arpToTree();
}

void KeysProcessor::restore(const UndoEntry& e)
{
    // Both of these want the *root* the session file uses, and each snapshot is already that
    // subtree - so wrap it back up in a root of the right shape before handing it over.
    juce::ValueTree root { "KEYS" };
    root.appendChild(e.before.createCopy(), nullptr);
    if (e.scope == UndoScope::pads)
        chordPadsFromTree(root);
    else
        arpFromTree(root);
}

void KeysProcessor::pushUndo(const juce::String& label, UndoScope scope)
{
    if (undoGestureDepth > 0)
        return; // inside an open gesture: the outermost push already took the "before"

    undoStack.push_back({ label, scope, snapshotFor(scope) });
    if ((int) undoStack.size() > maxUndoDepth)
        undoStack.erase(undoStack.begin());

    // A new edit ends the redo branch, which is what every undo stack does and what people
    // expect: once you change something after undoing, the future you undid is gone.
    redoStack.clear();
    undoGen.fetch_add(1, std::memory_order_relaxed);
}

KeysProcessor::UndoGesture::UndoGesture(KeysProcessor& p, const juce::String& label, UndoScope scope)
    : processor(p)
{
    p.pushUndo(label, scope);   // the outermost one; any nested push is absorbed
    ++p.undoGestureDepth;
}

KeysProcessor::UndoGesture::~UndoGesture()
{
    --processor.undoGestureDepth;
}

void KeysProcessor::undo()
{
    if (undoStack.empty())
        return;
    auto entry = undoStack.back();
    undoStack.pop_back();

    // The current state becomes the redo entry, taken *before* the restore overwrites it.
    redoStack.push_back({ entry.label, entry.scope, snapshotFor(entry.scope) });
    if ((int) redoStack.size() > maxUndoDepth)
        redoStack.erase(redoStack.begin());

    // Undoing must never leave a note ringing that nothing owns any more. Restoring pads can
    // rewrite the chord a sustained card is holding, and restoring the arp can rewrite the
    // lanes under a running line, so let go of everything first - the same choke point an
    // audition uses, and for the same reason.
    stopAllChordPads();
    restore(entry);
    undoGen.fetch_add(1, std::memory_order_relaxed);
}

void KeysProcessor::redo()
{
    if (redoStack.empty())
        return;
    auto entry = redoStack.back();
    redoStack.pop_back();

    undoStack.push_back({ entry.label, entry.scope, snapshotFor(entry.scope) });
    if ((int) undoStack.size() > maxUndoDepth)
        undoStack.erase(undoStack.begin());

    stopAllChordPads();
    restore(entry);
    undoGen.fetch_add(1, std::memory_order_relaxed);
}

void KeysProcessor::clearUndoHistory()
{
    undoStack.clear();
    redoStack.clear();
    undoGen.fetch_add(1, std::memory_order_relaxed);
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
        // Written only when there is one, like `numeral` above: most pads are not part of a named
        // progression, and a property on every pad saying so would be noise in the session file.
        if (p.progression.isNotEmpty())
        {
            pad.setProperty("progression", p.progression, nullptr);
            pad.setProperty("progressionStep", p.progressionStep, nullptr);
        }
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
    // A session stores its slots in its own page width; keep each pad on the page it was on by
    // re-basing that slot into the current width.
    //
    // **Narrowing drops the overflow, and must.** This was written for 8 -> 16, where every old
    // position still had a home; 16 -> 12 (2026-08-03) does not, and the old formula wrapped
    // positions 12..15 onto the *front of the next page*, where they overwrote that page's own
    // pads as the loop went on - silent, and worse than the loss it was trying to avoid. A pad
    // past the end of its page now has nowhere to go and is dropped, which is Owen's call
    // (2026-08-03) and is called out in the changelog: it is not reversible.
    const int savedPerPage = (int) pads.getProperty("padsPerPage", 8);
    for (int c = 0; c < pads.getNumChildren(); ++c)
    {
        const auto pad = pads.getChild(c);
        int slot = (int) pad.getProperty("slot", -1);
        if (slot >= 0 && savedPerPage > 0 && savedPerPage != padsPerPage)
        {
            const int pos = slot % savedPerPage;
            if (pos >= padsPerPage)
                continue; // the page got shorter under it
            slot = (slot / savedPerPage) * padsPerPage + pos;
        }
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
        p.progression = pad.getProperty("progression").toString(); // absent in pre-library sessions
        p.progressionStep = (int) pad.getProperty("progressionStep", -1);
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
        pat.on[(size_t) l] = ln.engine.lanes.on[(size_t) l].load();
        pat.loopFrom[(size_t) l] = ln.engine.lanes.loopFrom[(size_t) l].load();
        pat.loopTo[(size_t) l] = ln.engine.lanes.loopTo[(size_t) l].load();
        pat.dir[(size_t) l] = ln.engine.lanes.dir[(size_t) l].load();
    }
    for (int i = 0; i < 4; ++i)
        pat.rhythmDivs[(size_t) i] = ln.engine.rhythmDiv[(size_t) i].load();
    pat.harmonyMode = ln.engine.harmonyMode.load();
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
        ln.engine.lanes.on[(size_t) l].store(pat.on[(size_t) l] != 0 ? 1 : 0);
        ln.engine.lanes.loopFrom[(size_t) l].store(juce::jlimit(0, ArpEngine::maxSteps - 1, pat.loopFrom[(size_t) l]));
        ln.engine.lanes.loopTo[(size_t) l].store(juce::jlimit(0, ArpEngine::maxSteps - 1, pat.loopTo[(size_t) l]));
        ln.engine.lanes.dir[(size_t) l].store(juce::jlimit(0, (int) ArpEngine::numLaneDirs - 1, pat.dir[(size_t) l]));
    }
    for (int i = 0; i < 4; ++i)
        ln.engine.rhythmDiv[(size_t) i].store(juce::jlimit(0, 16, pat.rhythmDivs[(size_t) i]));
    ln.engine.harmonyMode.store(juce::jlimit(0, 1, pat.harmonyMode));
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
    // Exclusive still reaches the pads and the live card from here: handing a card to the arp
    // is a *drop*, and a drop replaces what the line was chewing, so the chord it displaces has
    // to go quiet.
    //
    // **The other direction is no longer symmetric, as of 2026-08-26**, and that asymmetry is
    // the feature rather than an oversight. A press on the strip leaves a running line alone
    // while **Keep arp running** is ticked (LayoutState::padsKeepArpRunning, default on),
    // because pressing a card is playing a chord and a line's held chord is not something you
    // are playing. Untick it and the old both-directions reading comes back.
    //
    // **It does not reach the other arp lines** (2026-08-02, Owen: "I want each arpeggiator to
    // play different chords"). It used to, on the reading that Exclusive means one chord at a
    // time whichever surface started it - which was right while the lines were something you
    // switched between, and is wrong now that they are two instruments you feed side by side.
    // Handing B a chord silently took A's away, so the second drag undid the first and a
    // polyrhythm could not be built at all: the feature and the rule wanted opposite things,
    // and the feature is the reason the lines exist.
    //
    // Nothing collides by allowing it. Each line's chord is fired into that line's own queue
    // (`dest` is line + 1) and `noteRefs` is per destination stream, so two lines holding the
    // same pitch are two independent references and neither release touches the other. This
    // line's own previous hold is already gone: releaseArpChord(line), above, is unconditional.
    if (apvts.getRawParameterValue("chordExclusive")->load() > 0.5f)
        stopAllChordPads(/*includeArpHolds*/ false);
    auto& ln = lines[(size_t) line];
    ln.chordName = name;
    // Fired into this line's own queue, not the track output: only its engine sees it. Note
    // it still goes through fireChord, so the Voices cap applies and the keybed lights up for
    // it exactly as before. The two feel controls do **not**: Humanize's velocity range has
    // skipped a note bound for a line since 2026-08-02, and Strum has skipped one since
    // 2026-08-23 - see fireChord and noteOn for why neither is audible on this path and both
    // cost the engine something.
    ln.chordOn = fireChord(notes, arpChordTagFor(line), line + 1);
    lastChordSource = arpChordTagFor(line);
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

void KeysProcessor::releaseArpHold(int line)
{
    // What "let go" means for one line, in the order that makes it stick.
    // Anything of this line's still waiting on a quantize boundary goes first: it has not
    // sounded yet, but it is a chord on its way, and letting go means "nothing is coming".
    pendingLaunches.erase(std::remove_if(pendingLaunches.begin(), pendingLaunches.end(),
                                         [line](const PendingLaunch& p) { return p.line == line; }),
                          pendingLaunches.end());
    // The chain next, and for the same reason the heartbeat stops it when the arp goes off:
    // releasing the chord without it only wins until the next bar boundary, when
    // heartbeatTick() launches the following slot and hands the arp another one. stopChain()
    // is a no-op when nothing is chaining, and releases the chord it was holding when it is.
    stopChain(line);
    // Whatever a card or a lone slot launch left behind. Idempotent after stopChain().
    releaseArpChord(line);
}

void KeysProcessor::releaseArpHold()
{
    // Every line, because this is one button and it means "let go". A Hold off that released
    // only the line the panel happened to be showing would leave the other three droning, with
    // nothing on a folded bar to stop them.
    for (int n = 0; n < numArpLines; ++n)
        releaseArpHold(n);
}

void KeysProcessor::allArpOff()
{
    // The switches first, then the letting go. This order is what makes one click enough:
    // with a line still on, releasing its chord only hands the engine back to whatever the
    // keybed is holding, and the run carries on under a button that said Off. Switched off
    // first, the engine is flushed by runArpLines on the very next block (the arpOn edge in
    // there calls flushInto), so nothing is left ringing to release.
    for (int n = 0; n < uiArpLines; ++n)
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(
                apvts.getParameter(arpParamId(n, arpParamSuffix(apOn)))))
        {
            p->beginChangeGesture();
            *p = false;
            p->endChangeGesture();
        }

    // ...and everything a line was holding on to, which the switch alone does not undo: a
    // chord handed over by a card, a chain mid-progression, a launch still waiting out
    // Launch Quantize. This is Hold off's whole job, so it is Hold off that does it.
    releaseArpHold();
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
    // The generator's key follows through to the keyboard's here rather than inside
    // parameterChanged: that callback can arrive on the audio thread from host automation, and
    // writing another parameter from there is not something to do mid-block. 20 ms late is
    // invisible for a control that only greys keys.
    mirrorGenKeyToScale();

    // Move the take out of the audio thread's ring while it is still being played into, so the
    // ring never has to hold a whole performance and the chip's duration is live rather than
    // arriving all at once when recording stops.
    if (recording.load(std::memory_order_relaxed))
        drainCapture();

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
        ln.engine.lanes.on[(size_t) l].store(pattern.on[(size_t) l] != 0 ? 1 : 0);
        ln.engine.lanes.loopFrom[(size_t) l].store(juce::jlimit(0, ArpEngine::maxSteps - 1, pattern.loopFrom[(size_t) l]));
        ln.engine.lanes.loopTo[(size_t) l].store(juce::jlimit(0, ArpEngine::maxSteps - 1, pattern.loopTo[(size_t) l]));
        ln.engine.lanes.dir[(size_t) l].store(juce::jlimit(0, (int) ArpEngine::numLaneDirs - 1, pattern.dir[(size_t) l]));
    }
    for (int i = 0; i < 4; ++i)
        ln.engine.rhythmDiv[(size_t) i].store(juce::jlimit(0, 16, pattern.rhythmDivs[(size_t) i]));
    ln.engine.harmonyMode.store(juce::jlimit(0, 1, pattern.harmonyMode));
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

void KeysProcessor::rerollArpLane(int line, int laneIndex, int amountPct, int fromStep, int toStep)
{
    // Reroll **one** lane, by an amount (2026-08-14, Owen: "there should be, like, a more
    // random feature in the drawing, like cthulu"). randomizeActiveArpPattern above is the
    // other kind and stays: it writes six lanes at once to a musical recipe, which is a way to
    // get a whole part you did not have. This is the one you reach for while looking at a lane
    // you already like - so it is scoped to that lane, and `amountPct` is how far it may stray
    // from what is drawn rather than an all-or-nothing reroll.
    //
    // At 100 the draw is uniform across the lane's whole range, which is the scramble; below
    // that it is a nudge around each step's current value. Either way it only ever writes
    // inside laneRange, so no reroll can put a value in a lane that the lane cannot hold.
    const auto lane = juce::jlimit(0, ArpEngine::numLanes - 1, laneIndex);
    const auto range = ArpEngine::laneRange(lane);
    const int span = range.hi - range.lo;
    if (span <= 0)
        return;
    const double amt = juce::jlimit(0, 100, amountPct) / 100.0;
    auto& lanes = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].engine.lanes;
    // Its own length, not maxSteps: rerolling past the end would quietly rewrite steps the
    // pattern is not playing, and they would appear later if the length ever grew.
    const int len = juce::jlimit(1, ArpEngine::maxSteps, lanes.length[(size_t) lane].load());
    // A span narrows it; -1 on either end means the whole lane. Clamped into the lane's own
    // length, so a span marked before the length shrank cannot write past the end.
    const int lo = fromStep < 0 ? 0 : juce::jlimit(0, len - 1, fromStep);
    const int hi = toStep < 0 ? len - 1 : juce::jlimit(lo, len - 1, toStep);
    for (int s = lo; s <= hi; ++s)
    {
        const int cur = lanes.value[(size_t) lane][(size_t) s].load();
        // The window slides rather than the result clamping - see ArpEngine::strayWithin for
        // why, and for the bug that taught it. At 100% the reach is the whole lane, so the
        // window covers it wherever the value sits and the draw is uniform across it.
        lanes.value[(size_t) lane][(size_t) s].store(
            ArpEngine::strayWithin(cur, span * amt, range, rng.nextDouble()));
    }
}

void KeysProcessor::resetArpLane(int line, int laneIndex, int fromStep, int toStep)
{
    // Its whole length, not maxSteps, for the reason rerollArpLane gives: the steps past the
    // end are not being played, and rewriting them would surface later if the length grew.
    const auto lane = juce::jlimit(0, ArpEngine::numLanes - 1, laneIndex);
    auto& lanes = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].engine.lanes;
    const int len = juce::jlimit(1, ArpEngine::maxSteps, lanes.length[(size_t) lane].load());
    const int lo = fromStep < 0 ? 0 : juce::jlimit(0, len - 1, fromStep);
    const int hi = toStep < 0 ? len - 1 : juce::jlimit(lo, len - 1, toStep);
    for (int s = lo; s <= hi; ++s)
        lanes.value[(size_t) lane][(size_t) s].store(ArpEngine::laneDefaults[lane]);
}

bool KeysProcessor::applyEuclidToActiveArpPattern(int line, int hits, int steps, int rotation, int laneIndex)
{
    // Only the probability lane has a meaningful hit/rest mapping (100 fires the step as
    // written, 0 never does); any other lane's "on" value depends on what that lane means, so
    // this stays scoped to probability rather than guessing at one.
    if (laneIndex != ArpEngine::laneProbability)
        return false;
    steps = juce::jlimit(1, ArpEngine::maxSteps, steps);
    auto& lanes = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].engine.lanes;
    for (int s = 0; s < steps; ++s)
        lanes.value[ArpEngine::laneProbability][(size_t) s].store(keys::euclidHit(s, hits, steps, rotation) ? 100 : 0);
    lanes.length[ArpEngine::laneProbability].store(steps);
    return true;
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
    migrateVelTrim(root);
    migrateVelLevel(root);
    migrateBpmSync(root);
    migrateTuplet(root);
    migrateHumanSpans(root);
    migrateStray(root);
    chordPadsFromTree(root);
    arpFromTree(root);
    layoutFromTree(root);

    // **A restore is not a move, so the Key/Mode mirror must not fire off one.** replaceState
    // pushes genRoot and genMode through parameterChanged like any other write, which raised
    // the pending flag and had the next heartbeat mirror them onto `root` and `scale` - over
    // the values this very session had just restored. Root and Scale are still ordinary
    // parameters you can set on the Controls bar after choosing a generator key, so that
    // silently threw away a saved keybed setting on every single load, and pushed two
    // gesture-less parameter writes at the host ~20 ms in for good measure. The mirror is
    // for the user turning the generator's Key or Mode; dropped here, it stays that.
    pendingGenKeyMirror.set(0);
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

void KeysProcessor::migrateVelTrim(const juce::ValueTree& root)
{
    // VOL became VEL on 2026-08-02: "arpVelTrim" is bipolar, centred at 0, and the macro
    // knob writes it where it used to write "arpVolume". A session saved before that carries
    // its line levels in Volume and no VelTrim at all - and, as ever, an absent parameter
    // keeps the live instance's current value rather than resetting (see migrateRateMode).
    // The repair is exact, not approximate: volume% and 1 + (volume-100)/100 are the same
    // multiplier, so VelTrim = Volume - 100 (and Volume back to its default 100) plays the
    // session note-for-note identically. HumanVel gets its default written for the same
    // absence reason; its old sessions' Humanize value stays as the timing half.
    const auto params = root.getChildWithName(apvts.state.getType());
    if (! params.isValid())
        return;

    for (int line = 0; line < numArpLines; ++line)
    {
        const auto trimId = arpParamId(line, apVelTrim);
        const auto hVelId = arpParamId(line, apHumanVel);
        const auto volId = arpParamId(line, apVolume);
        bool sawTrim = false, sawHVel = false;
        double savedVolume = 100.0; // the parameter's default, for sessions that predate it
        for (int i = 0; i < params.getNumChildren(); ++i)
        {
            const auto child = params.getChild(i);
            const auto id = child.getProperty("id").toString();
            sawTrim = sawTrim || id == trimId;
            sawHVel = sawHVel || id == hVelId;
            if (id == volId)
                savedVolume = (double) child.getProperty("value", 100.0);
        }

        if (! sawHVel)
            if (auto* param = apvts.getParameter(hVelId))
                param->setValueNotifyingHost(param->getDefaultValue());

        if (! sawTrim)
        {
            if (auto* param = apvts.getParameter(trimId))
            {
                // Through the knob's curve (scale = ((100+trim)/100)^2), so the old
                // volume% lands at the trim that plays the same level: trim =
                // 100*(sqrt(volume%) - 1). Rounding to the int parameter costs at most
                // ~1% of velocity, under the 1/127 the velocity is quantized to anyway.
                const double frac = juce::jlimit(0.0, 100.0, savedVolume) / 100.0;
                const float trim = (float) juce::jlimit(-100.0, 100.0,
                                                        std::round((std::sqrt(frac) - 1.0) * 100.0));
                param->setValueNotifyingHost(param->convertTo0to1(trim));
            }
            if (auto* param = apvts.getParameter(volId))
                param->setValueNotifyingHost(param->getDefaultValue());
        }
    }
}

void KeysProcessor::migrateVelLevel(const juce::ValueTree& root)
{
    // VEL became an absolute 0..127 velocity band on 2026-08-18 (see the parameter's own note).
    // A session saved before that carries its line levels in the bipolar VelTrim and has no
    // VelLevel at all - and, as ever, an absent parameter keeps the live instance's current value
    // rather than resetting, so the default never lands on its own (see migrateRateMode).
    //
    // The repair cannot be exact the way migrateVelTrim's was, and the reason is the whole point
    // of the change: a trim multiplied whatever velocity arrived, and a level replaces it, so the
    // two only agree once you say what "whatever arrived" was. It is not a guess - every chord
    // Keys fires leaves at `baseVelocity01()`, the midpoint of the pads' Humanize band, and 76 is
    // that midpoint at its own defaults (64..88). So a session that never touched VEL lands on
    // 76 and plays at the velocity it always did.
    const auto params = root.getChildWithName(apvts.state.getType());
    if (! params.isValid())
        return;

    constexpr double asPlayed = 76.0; // the pads' default velocity; see above

    for (int line = 0; line < numArpLines; ++line)
    {
        const auto levelId = arpParamId(line, apVelLevel);
        const auto trimId = arpParamId(line, apVelTrim);
        bool sawLevel = false;
        for (int i = 0; i < params.getNumChildren(); ++i)
            sawLevel = sawLevel || params.getChild(i).getProperty("id").toString() == levelId;
        if (sawLevel)
            continue;

        // **The trim is read live, not out of the tree, and the order is why.** migrateVelTrim
        // runs immediately before this one and, for a session old enough to predate VelTrim
        // too, *synthesises* the trim from that session's Volume and writes it to the live
        // parameter - it never goes back into the saved tree. Scanning the tree here therefore
        // saw no trim child on exactly the oldest sessions, fell back to the 0 default, and
        // handed them "as played" instead of the level they were saved at, which is the one
        // thing this chain of migrations exists to preserve. replaceState has already pushed
        // the tree's own value into the parameter by now, so a live read answers both cases.
        double savedTrim = 0.0; // the parameter's default: "as played"
        if (auto* trim = apvts.getRawParameterValue(trimId))
            savedTrim = (double) trim->load();

        if (auto* param = apvts.getParameter(levelId))
        {
            // Through the curve VelTrim actually had: scale = ((100+trim)/100)^2, squared because
            // hearing is logarithmic. -100 lands on 0, which is the silence that trim meant.
            const double t = (100.0 + juce::jlimit(-100.0, 100.0, savedTrim)) / 100.0;
            const float level = (float) juce::jlimit(0.0, 127.0, std::round(asPlayed * t * t));
            param->setValueNotifyingHost(param->convertTo0to1(level));
        }
    }
}

void KeysProcessor::migrateBpmSync(const juce::ValueTree& root)
{
    // Tempo Sync joined the tempo control on 2026-08-02: "bpmSync" decides whether a rolling
    // host tempo can override the "bpm" parameter, and true reproduces exactly what Keys
    // always did before this parameter existed. A session saved before it is absent, not
    // "off" - and as ever, an absent parameter keeps the live instance's current value rather
    // than resetting (see migrateRateMode). The repair is the same one line: write the
    // default explicitly.
    const auto params = root.getChildWithName(apvts.state.getType());
    if (! params.isValid())
        return;

    bool saw = false;
    for (int i = 0; i < params.getNumChildren(); ++i)
        if (params.getChild(i).getProperty("id").toString() == "bpmSync")
        {
            saw = true;
            break;
        }

    if (! saw)
        if (auto* param = apvts.getParameter("bpmSync"))
            param->setValueNotifyingHost(param->getDefaultValue());
}

void KeysProcessor::migrateTuplet(const juce::ValueTree& root)
{
    // Trip became Tuplet on 2026-08-03: "arpTuplet" is a choice over Straight / Triplet /
    // 5-tuplet / 7-tuplet / 9-tuplet, and
    // "arpTrip" - a bool that could only ever say 3 - is retired into it. A session saved before
    // this has no Tuplet at all, and as ever an absent parameter keeps the live instance's
    // current value rather than resetting (see migrateRateMode), so a preset load could leave
    // the previous patch's quintuplets running under a session that never asked for one.
    //
    // The fold is exact, not approximate: index 1 is "3" and ArpEngine::tupletFactor(3) is the
    // 2/3 the old triplet branch multiplied by, so an old session plays note for note as it did.
    // Trip goes back to its default in the same breath - the migrateVelTrim shape, and for the
    // same reason: two parameters saying the same thing, only one of them written, is a state
    // that drifts the moment a host automates the dead one.
    const auto params = root.getChildWithName(apvts.state.getType());
    if (! params.isValid())
        return;

    for (int line = 0; line < numArpLines; ++line)
    {
        const auto tupId = arpParamId(line, apTuplet);
        const auto tripId = arpParamId(line, apTrip);
        bool sawTuplet = false, savedTrip = false;
        for (int i = 0; i < params.getNumChildren(); ++i)
        {
            const auto child = params.getChild(i);
            const auto id = child.getProperty("id").toString();
            sawTuplet = sawTuplet || id == tupId;
            if (id == tripId)
                savedTrip = (double) child.getProperty("value", 0.0) > 0.5;
        }

        if (sawTuplet)
            continue; // saved since the change; whatever it says is meant

        if (auto* param = apvts.getParameter(tupId))
            param->setValueNotifyingHost(param->convertTo0to1(savedTrip ? 1.0f : 0.0f));
        if (auto* param = apvts.getParameter(tripId))
            param->setValueNotifyingHost(param->getDefaultValue());
    }
}

void KeysProcessor::migrateHumanSpans(const juce::ValueTree& root)
{
    // The two Humanize spans arrived 2026-08-03 and are absent from every session saved before
    // them. Their default is 100 - the whole scale, so the floor is zero wherever the knob sits
    // - which reproduces exactly what Humanize did alone. But an absent parameter is not a
    // reset (see migrateRateMode), so without this a preset load would leave the previous
    // patch's narrow span pinning every hit late in a session that never asked for one. That
    // is a worse failure here than for most: the default is the *top* of the range, so an
    // absent parameter inherits something quieter than the default rather than louder.
    // Nothing to fold: there is no older parameter that meant this.
    const auto params = root.getChildWithName(apvts.state.getType());
    if (! params.isValid())
        return;

    for (int line = 0; line < numArpLines; ++line)
        for (const auto which : { apHumanizeSpan, apHumanVelSpan, apDrift })
        {
            const auto wanted = arpParamId(line, which);
            bool saw = false;
            for (int i = 0; i < params.getNumChildren() && ! saw; ++i)
                saw = params.getChild(i).getProperty("id").toString() == wanted;
            if (! saw)
                if (auto* param = apvts.getParameter(wanted))
                    param->setValueNotifyingHost(param->getDefaultValue());
        }
}

void KeysProcessor::migrateStray(const juce::ValueTree& root)
{
    // The only migration here that has to *date* a session rather than just notice a hole.
    //
    // Stray is new on 2026-08-21, but the behaviour it carries is not: from 2026-08-19 the top
    // half of the **Mutate** dial did this job, so a session saved in that two-day window holds
    // a Mutate above 50 that meant "and leave the chord". Stray's default is 0 - off, chosen so
    // that nothing predating the *knob* could acquire a note it was not already playing - and
    // for those two days' sessions that default is wrong in the audible direction: the line
    // opens strictly in-chord where it used to wander. A different part, silently.
    //
    // Absence alone cannot tell those sessions from the far larger set saved before 2026-08-19,
    // where Mutate existed (2026-08-18) but never left the chord at any setting. Folding those
    // forward would *add* strays nobody ever heard, which is the same bug pointing the other
    // way. **`apHarm1` is the date stamp**: the two harmony voices and Mutate's stray zones
    // landed together in the 2026-08-19 round, so `Harm1 present && Stray absent` is that
    // window exactly, and `Harm1 absent` is a session that predates the behaviour entirely.
    //
    // The fold is exact rather than approximate, the migrateVelTrim standard. Old Mutate ran
    // its stray stage over (50, 100] - in scale to 75, chromatic above - and Stray runs those
    // same two zones over (0, 100] with its boundary at 50. So `(mutate - 50) * 2` maps 75 onto
    // 50 and 100 onto 100: the zone boundary lands on the zone boundary and every setting in
    // between keeps its share. Mutate itself is left alone - it now means what it always meant
    // below 50, and its upper half reaches further inside the chord instead, which is a change
    // this cannot undo and should not: the note count is identical either way.
    const auto params = root.getChildWithName(apvts.state.getType());
    if (! params.isValid())
        return;

    for (int line = 0; line < numArpLines; ++line)
    {
        const auto strayId = arpParamId(line, apStray);
        const auto harmId = arpParamId(line, apHarm1);
        bool sawStray = false, sawHarm = false;
        for (int i = 0; i < params.getNumChildren(); ++i)
        {
            const auto pid = params.getChild(i).getProperty("id").toString();
            if (pid == strayId) sawStray = true;
            if (pid == harmId)  sawHarm = true;
        }
        if (sawStray)
            continue; // the session names it, so whatever it says is what the user chose.

        auto* stray = apvts.getParameter(strayId);
        if (stray == nullptr)
            continue;

        // Absent, and predating the behaviour: the default is the repair. An absent parameter
        // is not a reset (see migrateRateMode), so it has to be written explicitly.
        if (! sawHarm)
        {
            stray->setValueNotifyingHost(stray->getDefaultValue());
            continue;
        }

        // Absent, and inside the window: fold Mutate's own upper half forward.
        const int mutate = (int) arpParam(line, apMutate);
        const int folded = juce::jlimit(0, 100, (mutate - 50) * 2);
        stray->setValueNotifyingHost(stray->convertTo0to1((float) folded));
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
    tree.setProperty("chordLib", layout.chordLib, nullptr);
    // Newline-joined rather than comma: a row name may contain a comma ("Axis, vi start") and
    // may not contain a newline, so this is the separator that cannot collide with the data.
    tree.setProperty("libraryFavourites", layout.libraryFavourites.joinIntoString(juce::newLine), nullptr);
    tree.setProperty("arpLine", layout.arpLine, nullptr);
    tree.setProperty("arpMacro", layout.arpMacro, nullptr);
    tree.setProperty("arpMacroBottomFolded", layout.arpMacroBottomFolded, nullptr);
    tree.setProperty("arpPage", layout.arpPage, nullptr);
    tree.setProperty("arpLights", layout.arpLights, nullptr);
    tree.setProperty("uiScalePercent", layout.uiScalePercent, nullptr);
    tree.setProperty("holdVisualsOnSustain", layout.holdVisualsOnSustain, nullptr);
    tree.setProperty("dragWhileSustain", layout.dragWhileSustain, nullptr);
    tree.setProperty("sustainProposesChords", layout.sustainProposesChords, nullptr);
    tree.setProperty("padsPlayOnClick", layout.padsPlayOnClick, nullptr);
    tree.setProperty("padsKeepArpRunning", layout.padsKeepArpRunning, nullptr);
    tree.setProperty("accent", layout.accent, nullptr);
    tree.setProperty("detachedBounds", layout.detachedBounds.toString(), nullptr);
    tree.setProperty("arpDetachedBounds", layout.arpDetachedBounds.toString(), nullptr);
    tree.setProperty("controlsDetachedBounds", layout.controlsDetachedBounds.toString(), nullptr);
    tree.setProperty("padsDetachedBounds", layout.padsDetachedBounds.toString(), nullptr);
    tree.setProperty("chordGenBounds", layout.chordGenBounds.toString(), nullptr);
    tree.setProperty("chordLibBounds", layout.chordLibBounds.toString(), nullptr);
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
    // The Knobs chip that folded the knob row off is gone (2026-08-02, Owen: "make the knobs
    // visible when you open controls"): the row is unconditional now, so a session saved with
    // it off (knobs=false, from before the chip left) must not reopen with the knobs hidden -
    // there is no control left on screen that could turn them back on. The field stays so the
    // tree still round-trips cleanly; nothing reads it as false again.
    layout.knobs = true;
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
    layout.chordLib = flag("chordLib", false);
    layout.libraryFavourites.clear();
    if (tree.hasProperty("libraryFavourites"))
        layout.libraryFavourites.addLines(tree.getProperty("libraryFavourites").toString());
    layout.libraryFavourites.removeEmptyStrings();
    // Absent before there were three lines, and line A is the right answer for those: it is
    // the only one a session from then can have anything in.
    layout.arpLine = juce::jlimit(0, numArpLines - 1, (int) tree.getProperty("arpLine", 0));
    layout.arpMacro = flag("arpMacro", true);
    // Absent before the bottom row could collapse (2026-08-19). False is the view every
    // session before it had: all four cards open.
    layout.arpMacroBottomFolded = flag("arpMacroBottomFolded", false);
    // Absent before the deep view was paged (2026-08-14). Play is the right answer for those,
    // and for a fresh instance: see the LayoutState comment for why not Draw.
    layout.arpPage = juce::jlimit(0, 2, (int) tree.getProperty("arpPage", 2));
    layout.arpLights = flag("arpLights", true);
    // Absent before the settings menu existed (2026-08-17). 100% and today's behaviour are
    // both the right defaults for a session that predates the flags entirely, the same
    // absent-means-default rule every field on this struct already follows.
    layout.uiScalePercent = juce::jlimit(1, 400, (int) tree.getProperty("uiScalePercent", 100));
    layout.holdVisualsOnSustain = flag("holdVisualsOnSustain", true);
    layout.dragWhileSustain = flag("dragWhileSustain", true);
    layout.sustainProposesChords = flag("sustainProposesChords", false);
    // padHoldToPlay is retired (2026-08-22): the Pads bar's Play toggle is that behaviour now.
    // An older session's property is simply ignored, which is what an unknown key in this tree
    // has always cost - unlike an APVTS parameter, a layout property carries no index anybody
    // stores, so dropping one needs no migration.
    layout.padsPlayOnClick = flag("padsPlayOnClick", true);
    layout.padsKeepArpRunning = flag("padsKeepArpRunning", true);
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
    frame("chordLibBounds", layout.chordLibBounds);
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
        // Rhythm dividers and the Harmony lane's mode, appended 2026-08-14. Absent in every
        // session saved before them, which reads back as "0,0,0,0" / 0 - inert, exactly what
        // those sessions already played.
        juce::StringArray rd;
        for (int v : pat.rhythmDivs)
            rd.add(juce::String(v));
        pt.setProperty("rhythmDivs", rd.joinIntoString(","), nullptr);
        pt.setProperty("harmonyMode", pat.harmonyMode, nullptr);
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
            // Appended 2026-08-18. Every one of these reads back as what the engine did before
            // it existed when the property is absent, which is what lets a session written by
            // any older build open here with no migration at all.
            lt.setProperty("on", pat.on[(size_t) l], nullptr);
            lt.setProperty("loopFrom", pat.loopFrom[(size_t) l], nullptr);
            lt.setProperty("loopTo", pat.loopTo[(size_t) l], nullptr);
            lt.setProperty("dir", pat.dir[(size_t) l], nullptr);
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
        const auto rd = juce::StringArray::fromTokens(pt.getProperty("rhythmDivs", "0,0,0,0").toString(), ",", "");
        for (int i = 0; i < 4; ++i)
            pat.rhythmDivs[(size_t) i] = i < rd.size() ? juce::jlimit(0, 16, rd[i].getIntValue()) : 0;
        pat.harmonyMode = juce::jlimit(0, 1, (int) pt.getProperty("harmonyMode", 0));
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
            pat.on[(size_t) l] = (int) lt.getProperty("on", 1);
            pat.loopFrom[(size_t) l] = (int) lt.getProperty("loopFrom", 0);
            pat.loopTo[(size_t) l] = (int) lt.getProperty("loopTo", ArpEngine::maxSteps - 1);
            pat.dir[(size_t) l] = (int) lt.getProperty("dir", ArpEngine::dirUp);
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
        ln.engine.lanes.on[(size_t) l].store(pat.on[(size_t) l] != 0 ? 1 : 0);
        ln.engine.lanes.loopFrom[(size_t) l].store(juce::jlimit(0, ArpEngine::maxSteps - 1, pat.loopFrom[(size_t) l]));
        ln.engine.lanes.loopTo[(size_t) l].store(juce::jlimit(0, ArpEngine::maxSteps - 1, pat.loopTo[(size_t) l]));
        ln.engine.lanes.dir[(size_t) l].store(juce::jlimit(0, (int) ArpEngine::numLaneDirs - 1, pat.dir[(size_t) l]));
    }
    for (int i = 0; i < 4; ++i)
        ln.engine.rhythmDiv[(size_t) i].store(pat.rhythmDivs[(size_t) i]);
    ln.engine.harmonyMode.store(pat.harmonyMode);
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
