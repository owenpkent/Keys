#include "KeysParams.h"
#include "ArpRateText.h" // arptext::rateHzText, for the arpRateHz parameter's own text function
#include "ChordSources.h" // the Progression picker's item list is that table's own
#include "PluginProcessor.h" // the ArpParam enum, arpParamId and numArpLines; see KeysParams.h
#include "ScaleModes.h"
#include <okstudio/Scales.h>
#include <cmath>

namespace keys::keysparams
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
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
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
    for (int line = 0; line < KeysProcessor::numArpLines; ++line)
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
void addArpLineParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int line)
{
    using namespace juce;

    const auto id = [line](const char* suffix) { return KeysProcessor::arpParamId(line, suffix); };
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
                // Decimals by decade, one copy of the rule (see ArpRateText.h's rateHzText):
                // 0.031 and 32.0 both have to read as themselves, and a fixed 2 would print the
                // bottom of the range as "0.03" for a whole octave of the dial.
                .withStringFromValueFunction([](float v, int) {
                    return arptext::rateHzText(v) + " Hz";
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
    // Named Density since 2026-09-01, when it became the macro card's DENSITY knob; the id is
    // still "Chance", because the id is what every saved session stores it under.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("Chance"), 1 }, nm + " Density", 0, 100, 100));

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
                                                      KeysProcessor::tupletChoices(), 0));
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

    // **Legato** (2026-09-01, Owen: "a legato button. So when the density is lower or a note
    // is skipped, it continues nicely"). A step that does not fire - Density turned down, a
    // Chance-lane cell, a mute, a drawn rest, a Chain condition - is silence today: the note
    // before it ends at its own gate and nothing sounds until the next step that fires. On,
    // that note is held open through the gap instead and released just *after* the next fired
    // step's note-on, which is the overlap a synth's legato or glide mode needs to slide rather
    // than restart. Gate still means what it says on a step whose successor fires - the engine
    // looks one step ahead to know which - so this narrows nothing but the silence.
    //
    // Default **off**, which is what the engine did before the parameter existed, so a session
    // that predates it opens sounding exactly as it was saved. It sits on the macro card's
    // bottom strip beside Dot, Tuplet and Anchor: a per-line switch you flip while listening.
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { id("Legato"), 1 },
                                                    nm + " Legato", false));

    // **The line bus, phase one** (2026-09-01, Owen: "can we get the arpeggiators to interact
    // with each other, like the step sequencers, so we can get interesting variations").
    // Follow names the line this one listens to; Duck is the hocket - how often this line skips
    // a step when the line it follows just played one. Four lines, one source each, and only a
    // letter above this one: runArpLines hands the engine nothing for any other value, so a host
    // lane or a script writing "From C" into line B gets a line that follows nobody rather than
    // one that reads last block's record. Both default off, which is four lines exactly as deaf
    // to each other as they were. The rest of the mechanisms - Reset, Neighbour, Clock - are
    // designed in docs/LINE_INTERACTION.md and not built.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id("Follow"), 1 }, nm + " Follow",
                                                      KeysProcessor::followChoices(), 0));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id("Duck"), 1 },
                                                   nm + " Duck", 0, 100, 0));
    // **Reset from the line it follows** (phase two, same day): when the source's walk comes
    // round, this line goes back to step 1 through the restart Retrigger owns - so a seven-step
    // lane against a sixteen-step one drifts for a bar and snaps home. Surfaced as the Follow
    // entry of the Play page's Retrigger list rather than a control of its own: "when does the
    // pattern start over" is one question, and that list is where it was already answered.
    // Default off.
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { id("ResetFollow"), 1 },
                                                    nm + " Reset Follow", false));
}

// The names the harmony combos show, built from the one table in KeysParams.h that also holds
// both semitone columns, so appending an interval is a single edit that cannot be half done.
juce::StringArray harmonyChoices()
{
    juce::StringArray out;
    for (const auto& e : harmonyTable)
        out.add(e.name);
    return out;
}

void migrateStrumRange(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root)
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

void migrateRateMode(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root)
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
    for (int line = 0; line < KeysProcessor::numArpLines; ++line)
    {
        const auto freeId = KeysProcessor::arpParamId(line, KeysProcessor::apRateFree);
        const auto hzId = KeysProcessor::arpParamId(line, KeysProcessor::apRateHz);
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

void migrateVelTrim(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root)
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

    for (int line = 0; line < KeysProcessor::numArpLines; ++line)
    {
        const auto trimId = KeysProcessor::arpParamId(line, KeysProcessor::apVelTrim);
        const auto hVelId = KeysProcessor::arpParamId(line, KeysProcessor::apHumanVel);
        const auto volId = KeysProcessor::arpParamId(line, KeysProcessor::apVolume);
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

void migrateVelLevel(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root)
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

    for (int line = 0; line < KeysProcessor::numArpLines; ++line)
    {
        const auto levelId = KeysProcessor::arpParamId(line, KeysProcessor::apVelLevel);
        const auto trimId = KeysProcessor::arpParamId(line, KeysProcessor::apVelTrim);
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

void migrateBpmSync(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root)
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

void migrateTuplet(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root)
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

    for (int line = 0; line < KeysProcessor::numArpLines; ++line)
    {
        const auto tupId = KeysProcessor::arpParamId(line, KeysProcessor::apTuplet);
        const auto tripId = KeysProcessor::arpParamId(line, KeysProcessor::apTrip);
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

void migrateHumanSpans(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root)
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

    for (int line = 0; line < KeysProcessor::numArpLines; ++line)
        for (const auto which : { KeysProcessor::apHumanizeSpan,
                                  KeysProcessor::apHumanVelSpan,
                                  KeysProcessor::apDrift })
        {
            const auto wanted = KeysProcessor::arpParamId(line, which);
            bool saw = false;
            for (int i = 0; i < params.getNumChildren() && ! saw; ++i)
                saw = params.getChild(i).getProperty("id").toString() == wanted;
            if (! saw)
                if (auto* param = apvts.getParameter(wanted))
                    param->setValueNotifyingHost(param->getDefaultValue());
        }
}

void migrateStray(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root)
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

    for (int line = 0; line < KeysProcessor::numArpLines; ++line)
    {
        const auto strayId = KeysProcessor::arpParamId(line, KeysProcessor::apStray);
        const auto harmId = KeysProcessor::arpParamId(line, KeysProcessor::apHarm1);
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

        // Absent, and inside the window: fold Mutate's own upper half forward. Read live rather
        // than out of the tree, and through the APVTS rather than the engine's cached pointer:
        // it is the same atomic either way, and replaceState has already pushed this session's
        // own value into it by the time any of these run.
        const auto* mutateVal = apvts.getRawParameterValue(
            KeysProcessor::arpParamId(line, KeysProcessor::apMutate));
        const int mutate = mutateVal != nullptr ? (int) mutateVal->load() : 0;
        const int folded = juce::jlimit(0, 100, (mutate - 50) * 2);
        stray->setValueNotifyingHost(stray->convertTo0to1((float) folded));
    }
}

} // namespace keys::keysparams
