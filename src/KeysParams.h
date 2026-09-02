#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <iterator>

// Keys' parameter layout and the migrations that keep older sessions playing what they were
// saved playing.
//
// It left KeysProcessor because none of it ever touched an instance: `createLayout` is called
// from the member initialiser list, before there *is* a processor to touch, and every migration
// is a walk over a saved tree writing into the APVTS it is handed. Six hundred lines of ids,
// ranges, defaults and the reasons behind them are now next to each other rather than threaded
// between the audio stage and the chord pads.
//
// **Nothing here may be reordered, and that is the whole contract.** A VST3 host addresses a
// parameter by its index and a saved session stores a choice parameter's plain index, so the
// order `createLayout` adds in and the order the item lists carry are both load-bearing:
// appending is safe, inserting and reordering silently repoint every automation lane and every
// saved session onto the wrong control. tests/StateTests.cpp holds the golden list.
//
// KeysProcessor keeps `createLayout`, `harmonyChoices`, `harmonySemisFor`,
// `harmonySemisSecondFor` and `tupletFor` as forwarders, because the editor, the MCP bridge and
// the tests call them under those names; `arpParamId` / `arpParamSuffix` and the `ArpParam` enum
// stay on the processor outright, since they are what the audio thread's cached pointer table is
// built from.

namespace keys::keysparams
{
// The whole layout, in the one order every saved session and automation lane depends on: the
// globals, then the four arp lines in letter order (addArpLineParams, once per line), then the
// globals appended after them.
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// One arpeggiator line's parameters. Called once per line by createLayout; see its definition
// for why line 0 registers under the bare ids every saved session already carries.
void addArpLineParams(juce::AudioProcessorValueTreeState::ParameterLayout&, int line);

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
//
// The table and the two indexed reads are in the header rather than the .cpp for that last
// reason: `harmonySemisFor` stays a plain read of a constexpr table that the compiler can see
// through, exactly as it was when it lived beside its one caller.
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
inline constexpr HarmonyEntry harmonyTable[] = {
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

inline constexpr int harmonyEntryCount = (int) std::size(harmonyTable);

// Not constexpr: juce::jlimit is not, in this JUCE. It does not need to be - the *table*
// is constexpr, which is the half that matters, and this is an indexed read either way.
inline const HarmonyEntry& harmonyEntry(int choiceIndex) noexcept
{
    return harmonyTable[(size_t) juce::jlimit(0, harmonyEntryCount - 1, choiceIndex)];
}

// Called from runArpLines on the audio thread: a plain indexed read of a constexpr table, no
// allocation and no call into harmonyChoices().
inline int harmonySemisFor(int choiceIndex) { return harmonyEntry(choiceIndex).semis; }

// The **second** interval a voice may carry, 0 for none (2026-08-21, Owen: "when you select
// octave plus fifth, it looks like it only just does octave"). A second interval per slot
// rather than two more voices: it is still one voice, so it shares its slot's chance roll and
// either both pitches fire or neither.
inline int harmonySemisSecondFor(int choiceIndex) { return harmonyEntry(choiceIndex).second; }

// The names, built from the same table so they cannot drift from the intervals.
juce::StringArray harmonyChoices();

// The N a choice index means. Off is 0 rather than 1 so "is there a tuplet at all" is one test
// against zero everywhere, and ArpEngine::tupletFactor treats both as straight. Audio-thread
// safe for the same reason harmonySemisFor is: an indexed read of a constant table.
inline int tupletFor(int choiceIndex)
{
    static constexpr int values[] = { 0, 3, 5, 7, 9 };
    return values[(size_t) juce::jlimit(0, (int) (sizeof(values) / sizeof(values[0])) - 1, choiceIndex)];
}

// --- Migrations -----------------------------------------------------------------------------
// Every one of these is the same shape and it is worth learning once: **an absent parameter is
// not a reset**. APVTS creates a child for it on the spot and flushes the *live* value into it,
// so a session saved before a parameter existed inherits whatever the instance was last playing
// with rather than the default. The tell is therefore the absence of an id in the saved tree,
// and the repair is to write something - a default, or the older parameter folded forward -
// explicitly. They read the tree rather than the live parameters (the state is still landing)
// and repair through setValueNotifyingHost so the host and the UI both learn the new value.
//
// Order matters between two of them: migrateVelTrim runs before migrateVelLevel, and
// migrateVelLevel reads the trim **live** because of it. restoreSharedState is where that order
// is written down.
void migrateStrumRange(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root);
void migrateRateMode(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root);
void migrateVelTrim(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root);
void migrateVelLevel(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root);
void migrateBpmSync(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root);
void migrateTuplet(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root);
void migrateHumanSpans(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root);
void migrateStray(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root);
} // namespace keys::keysparams
