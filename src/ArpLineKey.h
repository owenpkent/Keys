#pragma once

// A line's own key: the three-way resolution of Root, Scale and Lock between the Controls bar's
// global setting and a per-line override, plus the text a macro card's KEY chip prints for it.
//
// Pure and header-only for the reason `ArpEngine.h` is: the resolution is read on the audio
// thread (`runArpLines` builds each line's `rootPc` / `scaleMask` / `scaleLock` from it) *and* on
// the message thread (the chip's own caption and lit state), and two copies of a rule with one
// copy on each thread is the drift this file exists to prevent - the same move `arpLineIsClocked`
// made for CLOCK. It is this header rather than `ArpEngine.h` because the mask needs the kit's
// scale table, which the engine header is deliberately free of, and `ArpRateText.h` is the
// precedent for a formatter living beside the engine instead of inside it.
//
// **Absence is "follow the global key", and that is what makes twelve appended parameters safe.**
// Index 0 of all three choice lists is Global, so a session saved before they existed takes the
// default and plays exactly as it did, and no migration has anything to backfill. It is the one
// kind of parameter append that costs a saved session nothing.

#include <okstudio/Scales.h>

#include <juce_core/juce_core.h>

#include <utility>

namespace keys::arpkey
{

// Index 0 of every per-line list. Named rather than written as 0 at each site, because what the
// zero means (follow the bar) is the whole design and a bare 0 says none of it.
inline constexpr int followGlobal = 0;

// Lock's three states. A bool cannot say "follow", which is why this one parameter is a choice
// where the bar's own `scaleLock` is a bool: the bar answers "is the keybed locked", a line
// answers "am I locked, unlocked, or whatever the bar says".
enum LockChoice { lockGlobal = 0, lockOn = 1, lockOff = 2 };

inline juce::StringArray rootChoices()
{
    juce::StringArray out { "Global" };
    out.addArray(okstudio::scales::noteNames());
    return out;
}

inline juce::StringArray scaleChoices()
{
    juce::StringArray out { "Global" };
    out.addArray(okstudio::scales::names());
    return out;
}

inline juce::StringArray lockChoices()
{
    return { "Global", "On", "Off" };
}

// What a line is actually playing in, once the overrides have been read over the bar.
struct Key
{
    int rootPc = 0;
    int scaleIndex = 0;
    bool lock = false;
    // All three at Global: the line is wearing the bar's key, so its chip says so rather than
    // printing a key the bar already prints. Kept here rather than recomputed by the caller,
    // since it is the same three comparisons the resolution just made.
    bool follows = true;
};

inline Key resolve(int globalRootPc, int globalScaleIndex, bool globalLock,
                   int lineRoot, int lineScale, int lineLock)
{
    Key k;
    k.rootPc = lineRoot == followGlobal ? globalRootPc : lineRoot - 1;
    k.scaleIndex = lineScale == followGlobal ? globalScaleIndex : lineScale - 1;
    k.lock = lineLock == lockGlobal ? globalLock : lineLock == lockOn;
    k.follows = lineRoot == followGlobal && lineScale == followGlobal && lineLock == lockGlobal;
    return k;
}

// Bit k set = the pitch class (rootPc + k) % 12 is in this scale. The engine's `scaleMask`, built
// here because the engine may not see the kit's tables; an empty answer falls back to all twelve,
// since a mask of zero would silence degree walking rather than free it.
inline unsigned int maskFor(int rootPc, int scaleIndex)
{
    unsigned int mask = 0;
    for (int k = 0; k < 12; ++k)
        if (okstudio::scales::isInScale(rootPc + k, rootPc, scaleIndex))
            mask |= 1u << k;
    return mask != 0 ? mask : 0xFFFu;
}

// A scale's name shortened to fit a chip, **matched by name with an algorithmic fallback** rather
// than indexed by a table parallel to the kit's. Two tables that must agree are two tables that
// will drift, and this is the drift that would be silent: the kit's scale list is append-only, so
// an index table would keep working while printing the wrong name for everything after the
// insertion point. A kit scale this table has never heard of gets its first four letters, which
// is wrong-looking at worst and never wrong. `ArpTests` sweeps the whole kit list and
// asserts the answers are unique, because two scales abbreviating to one word is the one failure
// a chip cannot show.
inline juce::String scaleAbbrev(const juce::String& name)
{
    static const std::pair<const char*, const char*> table[] = {
        { "Major",            "MAJ"  },
        { "Natural Minor",    "MIN"  },
        { "Harmonic Minor",   "HARM" },
        { "Melodic Minor",    "MEL"  },
        { "Dorian",           "DOR"  },
        { "Phrygian",         "PHRY" },
        { "Lydian",           "LYD"  },
        { "Mixolydian",       "MIX"  },
        { "Locrian",          "LOCR" },
        { "Major Pentatonic", "MAJ5" },
        { "Minor Pentatonic", "MIN5" },
        { "Blues",            "BLU"  },
        { "Whole Tone",       "WHL"  },
        { "Chromatic",        "CHR"  },
    };
    for (const auto& [full, shortForm] : table)
        if (name == full)
            return shortForm;
    return name.substring(0, 4).toUpperCase();
}

// What the chip prints: the word while the line follows the bar, the key itself once anything on
// it has been overridden. A line that has overridden only Lock still prints the bar's key, and
// that is right - it is the key that line is locked to, whoever chose it.
inline juce::String chipText(const Key& k)
{
    if (k.follows)
        return "KEY";

    const auto roots = okstudio::scales::noteNames();
    const auto scaleNames = okstudio::scales::names();
    const auto root = roots[juce::jlimit(0, roots.size() - 1, k.rootPc)];
    const auto scale = scaleAbbrev(scaleNames[juce::jlimit(0, scaleNames.size() - 1, k.scaleIndex)]);
    return root + " " + scale;
}

// Every caption the chip can ever print, for the one measurement that sets its width. A width
// written down goes stale in silence, so the card asks for the longest of these instead - and a
// test measures the same list, so a kit scale with a longer abbreviation widens the chip rather
// than drawing "..." in it.
inline juce::StringArray allChipTexts()
{
    juce::StringArray out { "KEY" };
    const auto roots = okstudio::scales::noteNames();
    const auto scaleNames = okstudio::scales::names();
    for (const auto& r : roots)
        for (const auto& s : scaleNames)
            out.add(r + " " + scaleAbbrev(s));
    return out;
}

} // namespace keys::arpkey
