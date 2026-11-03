#pragma once

#include "Chords.h"
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

// A chord symbol typed as text ("Cmaj7", "F#m7b5", "Bb/D", "G7sus4") turned into notes. The
// inverse of Chords.h's detect(), and pure for the same reason: a parser is the kind of thing a
// unit test should be able to sweep (2026-11-02, Owen: "it would be cool if you could type the
// chord you wanted. example, Cmaj7").
//
// Grammar: a root letter A..G in either case, an optional accidental (# or b, and their Unicode
// twins), a quality suffix from the table below matched exactly first and case-insensitively
// second (so "CM7" is a major seventh and "cm7" a minor one, while "CMAJ7" still lands), and an
// optional slash bass that has to read as a note name - "C6/9" keeps its 9 because a 9 is not a
// note. Spaces are ignored. Anything else is a refusal, and the field shows it rather than
// guessing, because a chord you did not ask for on a pad is worse than no chord.
namespace keys::chordsym
{
    struct Parsed
    {
        int rootPc = 0;             // pitch class of the root
        int bassPc = -1;            // slash bass pitch class, or -1 for none
        std::vector<int> intervals; // semitones above the root, root (0) included, may exceed 11
        juce::String name;          // the canonical spelling the pad prints, e.g. "Bbmaj7/D"
    };

    struct Quality
    {
        const char* suffix;          // the canonical suffix the name carries
        std::vector<int> intervals;  // semitones above the root
        std::vector<const char*> aliases; // other spellings that mean the same thing
    };

    // Append-only is not required here - nothing stores an index into this table - but keep the
    // canonical suffixes the same as Chords.h's where both name a quality, so a typed chord and a
    // detected one print alike.
    inline const std::vector<Quality>& qualities()
    {
        static const std::vector<Quality> q = {
            { "",        { 0, 4, 7 },               { "maj", "M", "major", "ma" } },
            { "m",       { 0, 3, 7 },               { "min", "minor", "mi", "-" } },
            { "dim",     { 0, 3, 6 },               { "o" } },
            { "aug",     { 0, 4, 8 },               { "+" } }, // not "#5": "D#5" is a D# power chord
            { "sus2",    { 0, 2, 7 },               {} },
            { "sus4",    { 0, 5, 7 },               { "sus" } },
            { "5",       { 0, 7 },                  {} },
            { "6",       { 0, 4, 7, 9 },            { "maj6", "M6", "add6" } },
            { "m6",      { 0, 3, 7, 9 },            { "min6", "-6" } },
            { "6/9",     { 0, 4, 7, 9, 14 },        { "69", "6add9", "maj6/9" } },
            { "maj7",    { 0, 4, 7, 11 },           { "M7", "ma7", "major7" } },
            { "m7",      { 0, 3, 7, 10 },           { "min7", "mi7", "-7" } },
            { "7",       { 0, 4, 7, 10 },           { "dom7", "dom" } },
            { "dim7",    { 0, 3, 6, 9 },            { "o7" } },
            { "m7b5",    { 0, 3, 6, 10 },           { "min7b5", "-7b5", "m7-5", "h7" } },
            { "mMaj7",   { 0, 3, 7, 11 },           { "mM7", "minMaj7", "m(maj7)", "-maj7", "-M7" } },
            { "add9",    { 0, 4, 7, 14 },           { "add2", "(add9)" } },
            { "madd9",   { 0, 3, 7, 14 },           { "m(add9)", "minadd9", "madd2" } },
            { "add11",   { 0, 4, 7, 17 },           { "add4" } },
            { "9",       { 0, 4, 7, 10, 14 },       { "dom9" } },
            { "maj9",    { 0, 4, 7, 11, 14 },       { "M9", "ma9" } },
            { "m9",      { 0, 3, 7, 10, 14 },       { "min9", "-9" } },
            { "7sus4",   { 0, 5, 7, 10 },           { "7sus", "sus7" } },
            { "9sus4",   { 0, 5, 7, 10, 14 },       { "9sus" } },
            { "11",      { 0, 4, 7, 10, 14, 17 },   {} },
            { "m11",     { 0, 3, 7, 10, 14, 17 },   { "min11", "-11" } },
            { "13",      { 0, 4, 7, 10, 14, 21 },   {} },
            { "maj13",   { 0, 4, 7, 11, 14, 21 },   { "M13" } },
            { "m13",     { 0, 3, 7, 10, 14, 21 },   { "min13", "-13" } },
            { "7b9",     { 0, 4, 7, 10, 13 },       { "7-9" } },
            { "7#9",     { 0, 4, 7, 10, 15 },       { "7+9" } },
            { "7b5",     { 0, 4, 6, 10 },           { "7-5" } },
            { "7#5",     { 0, 4, 8, 10 },           { "aug7", "+7", "7+5" } },
            { "maj7#11", { 0, 4, 7, 11, 18 },       { "M7#11", "maj7+11" } },
            { "dim9",    { 0, 3, 6, 9, 14 },        { "o9" } },
        };
        return q;
    }

    namespace detail
    {
        // A note name at the head of `s`: the letter, then an accidental if one follows. Returns
        // how many characters it consumed (0 for none) and leaves the pitch class and the
        // spelling (as the user wrote it, letter upper-cased, Unicode accidentals folded to
        // ASCII) in the out-params.
        inline int readNote(const juce::String& s, int& pcOut, juce::String& spellingOut)
        {
            if (s.isEmpty())
                return 0;
            const auto letter = juce::CharacterFunctions::toUpperCase(s[0]);
            static const char* letters = "CDEFGAB";
            static const int   pcs[]   = { 0, 2, 4, 5, 7, 9, 11 };
            int pc = -1;
            for (int i = 0; i < 7; ++i)
                if ((juce::juce_wchar) letters[i] == letter)
                    pc = pcs[i];
            if (pc < 0)
                return 0;
            spellingOut = juce::String::charToString(letter);
            int used = 1;
            if (s.length() > 1)
            {
                const auto acc = s[1];
                if (acc == '#' || acc == 0x266F) { pc += 1;  spellingOut += "#"; used = 2; }
                else if (acc == 'b' || acc == 0x266D) { pc -= 1; spellingOut += "b"; used = 2; }
            }
            pcOut = ((pc % 12) + 12) % 12;
            return used;
        }

        inline const Quality* matchQuality(const juce::String& suffix)
        {
            for (const auto& q : qualities())
            {
                if (suffix == q.suffix)
                    return &q;
                for (const char* a : q.aliases)
                    if (suffix == a)
                        return &q;
            }
            for (const auto& q : qualities())
            {
                if (suffix.equalsIgnoreCase(q.suffix))
                    return &q;
                for (const char* a : q.aliases)
                    if (suffix.equalsIgnoreCase(a))
                        return &q;
            }
            return nullptr;
        }
    }

    inline std::optional<Parsed> parse(const juce::String& text)
    {
        const auto s = text.removeCharacters(" \t");
        Parsed out;
        juce::String rootSpelling;
        const int rootLen = detail::readNote(s, out.rootPc, rootSpelling);
        if (rootLen == 0)
            return std::nullopt;

        auto rest = s.substring(rootLen);

        // A slash bass is the text after the *last* slash, and only if that text is a whole note
        // name on its own; otherwise the slash belongs to the suffix ("6/9").
        juce::String bassSpelling;
        const int slash = rest.lastIndexOfChar('/');
        if (slash >= 0)
        {
            const auto tail = rest.substring(slash + 1);
            int pc = -1;
            juce::String spelling;
            if (detail::readNote(tail, pc, spelling) == tail.length() && tail.isNotEmpty())
            {
                out.bassPc = pc;
                bassSpelling = spelling;
                rest = rest.substring(0, slash);
            }
        }

        const auto* q = detail::matchQuality(rest);
        if (q == nullptr)
            return std::nullopt;

        out.intervals = q->intervals;
        out.name = rootSpelling + q->suffix;
        if (out.bassPc >= 0)
            out.name += "/" + bassSpelling;
        return out;
    }

    // The chord as MIDI notes with the root in `octave` (chordgen's convention: octave 4 puts a
    // C root at 48). A slash bass that is already a chord tone is that tone moved below the
    // root, an inversion; any other bass is one note added underneath.
    inline std::vector<int> notes(const Parsed& p, int octave)
    {
        const int base = octave * 12 + p.rootPc;
        std::vector<int> out;
        out.reserve(p.intervals.size() + 1);
        for (int i : p.intervals)
            out.push_back(base + i);

        if (p.bassPc >= 0 && p.bassPc != p.rootPc)
        {
            const int bass = base - ((p.rootPc - p.bassPc + 12) % 12);
            // Drop the chord's own copy of that tone, if it has one, so the inversion does not
            // double it.
            for (auto it = out.begin(); it != out.end(); ++it)
                if (chords::pitchClass(*it) == p.bassPc)
                {
                    out.erase(it);
                    break;
                }
            out.insert(out.begin(), bass);
        }

        for (auto& n : out)
            n = juce::jlimit(0, 127, n);
        return out;
    }
}
