#pragma once

#include "ChordGen.h"
#include "MarkovData.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <map>
#include <set>
#include <vector>

// Markov chord-progression generation, ported from Octavium (app/chord_progression.py).
// Pure logic, no UI, so it unit-tests like ChordGen.h and ChordSuggest.h.
//
// The shape of the idea: each corpus entry is a real progression spelled as roman-numeral
// tokens (e.g. "I V vi IV"). Build a per-mode table of which token follows which (a bigram
// transition table), then generate new progressions by walking it -- optionally biased by a
// mood tag, a chosen start chord, and a "temperature" that sharpens the walk toward the most
// common transitions or flattens it toward anything-goes.
//
// Octavium's own corpus for this feature never shipped -- see MarkovData.h's header comment
// (and the port spec, section A0) for the full story. That file is freshly authored; this
// file is the ported algorithm, which is independently verifiable against the spec section
// A3, A5-A10 regardless of where the data comes from.
//
// One deliberate deviation from Octavium, flagged here rather than buried: Octavium's
// suffix lookup falls back to case-deciding the quality (upper = Major, lower = Minor) for
// *any* unrecognised suffix, typo or not, so a mis-typed suffix like "dmi" would silently
// become a plain triad instead of failing. That makes typos in a hand-authored corpus
// invisible. Here, only a genuinely *empty* suffix falls back to case; a non-empty suffix
// that isn't in the table is a parse failure. This is what makes the corpus lint test in
// MarkovTests.cpp meaningful, and it matches this codebase's general stance (see
// ChordGen.h's note-count fallback) of not silently reinterpreting something that doesn't
// fit rather than reproducing the bug that let it through.
namespace keys::markov
{
    struct Chord
    {
        int rootPc = 0;
        int type = 0;
        std::vector<int> notes;
        juce::String name;     // e.g. "G Dominant 7th"
        juce::String numeral;  // the token this came from, e.g. "V7"
    };

    namespace detail
    {
        // Lexicographic order so std::map<juce::String, ...> works: juce::String has no
        // operator<, only compare().
        struct StringLess
        {
            bool operator() (const juce::String& a, const juce::String& b) const noexcept
            {
                return a.compare(b) < 0;
            }
        };

        using Counts = std::map<juce::String, int, StringLess>;

        // Sharps-only note names, local to this file: ChordMarkov.h is deliberately kept to
        // juce_core + std + ChordGen.h + MarkovData.h (no Chords.h), so Chord::name builds
        // its own label the same way keys::chords::noteName does rather than reaching out.
        inline const char* noteName(int pc)
        {
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            return names[((pc % 12) + 12) % 12];
        }

        // --- A3: roman numeral -> (root pitch class, chord type) ------------------------

        struct NumeralDegree
        {
            const char* symbol;
            int semitone;
            bool isUpper;
        };

        // Longest-match order, mirroring Octavium's regex alternation
        // ^([b#]?)(VII|VI|IV|V|III|II|I|vii|vi|iv|v|iii|ii|i)(.*)$ -- e.g. "VI" must be tried
        // before "V", and "IV"/"III"/"II" before "I", or the shorter symbol would match first
        // and leave a stray character in what should have been the suffix.
        inline const std::vector<NumeralDegree>& numeralDegrees()
        {
            static const std::vector<NumeralDegree> d = {
                { "VII", 11, true }, { "VI", 9, true }, { "IV", 5, true }, { "V", 7, true },
                { "III", 4, true }, { "II", 2, true }, { "I", 0, true },
                { "vii", 11, false }, { "vi", 9, false }, { "iv", 5, false }, { "v", 7, false },
                { "iii", 4, false }, { "ii", 2, false }, { "i", 0, false },
            };
            return d;
        }

        // Octavium's _SUFFIX_TO_CHORD_TYPE, verbatim, mapped to ChordGen.h's exact type
        // names. Every one of these is an exact name match in chordgen::types() (checked by
        // hand against ChordGen.h) -- no fuzzy substitution needed anywhere in this table.
        inline const std::vector<std::pair<const char*, const char*>>& suffixTable()
        {
            static const std::vector<std::pair<const char*, const char*>> s = {
                { "M7", "Major 7th" }, { "m7", "Minor 7th" }, { "dom7", "Dominant 7th" }, { "7", "Dominant 7th" },
                { "dim7", "Diminished 7th" }, { "dim", "Diminished" }, { "aug", "Augmented" },
                { "sus2", "Sus2" }, { "sus4", "Sus4" }, { "add9", "Add9" },
                { "69", "6/9" }, { "6", "Major 6th" }, { "9", "Dominant 9th" },
                { "M-5", "Diminished" }, { "m", "Minor" }, { "M", "Major" },
                // Appended 2026-08-18 for ChordLibrary.h, which needs a numeral for every type in
                // chordgen::types() and was six short. Half-diminished is the one whose absence
                // was not cosmetic: it is the ii of every minor ii-V, so the single most common
                // cadence in the minor key could not be written down at all.
                //
                // **Appending here is safe, unlike almost everywhere else in Keys.** The lookup
                // below is `suffix == suf`, an exact string compare over the whole table, so a new
                // row can neither shadow an existing one nor move it - where chordgen::types(),
                // genSource and the lane indices are all append-only precisely because a saved
                // session stores their *index*. Nothing stores an index into this table.
                { "m7b5", "Half Diminished" }, { "mM7", "Minor Major 7th" },
                { "m6", "Minor 6th" }, { "madd9", "Minor Add9" },
                { "M9", "Major 9th" }, { "m9", "Minor 9th" },
            };
            return s;
        }

        struct ParsedNumeral
        {
            int rootPc = 0;
            int type = 0;
            bool valid = false;
        };

        // Accidental (+/-1, then %12) then longest-match numeral then suffix lookup. An
        // empty suffix falls back to case (upper = Major, lower = Minor); a non-empty suffix
        // that isn't in the table is a parse failure -- see the file header for why that's a
        // deliberate change from Octavium rather than a faithfulness gap.
        inline ParsedNumeral parseNumeralToken(const juce::String& token, int keyRootPc)
        {
            juce::String s = token;
            int accidental = 0;
            if (s.startsWith("b"))      { accidental = -1; s = s.substring(1); }
            else if (s.startsWith("#")) { accidental = 1;  s = s.substring(1); }

            for (const auto& d : numeralDegrees())
            {
                if (! s.startsWith(d.symbol))
                    continue;

                const juce::String suffix = s.substring((int) juce::String(d.symbol).length());
                ParsedNumeral out;
                out.rootPc = ((keyRootPc + d.semitone + accidental) % 12 + 12) % 12;

                if (suffix.isEmpty())
                {
                    out.type = chordgen::typeIndex(d.isUpper ? "Major" : "Minor");
                    out.valid = true;
                    return out;
                }

                for (const auto& [suf, typeName] : suffixTable())
                {
                    if (suffix == suf)
                    {
                        out.type = chordgen::typeIndex(typeName);
                        out.valid = true;
                        return out;
                    }
                }
                return {}; // unrecognised suffix: invalid, not a silent case-fallback
            }
            return {}; // no numeral matched at all
        }

        // The bare accidental+numeral part of a token, quality stripped. Used by the
        // successor fallback (A5): "vim7" -> "vi".
        inline juce::String stripSuffix(const juce::String& token)
        {
            juce::String s = token;
            juce::String accidental;
            if (s.startsWith("b") || s.startsWith("#"))
            {
                accidental = s.substring(0, 1);
                s = s.substring(1);
            }
            for (const auto& d : numeralDegrees())
                if (s.startsWith(d.symbol))
                    return accidental + d.symbol;
            return token; // not a recognised numeral; nothing to strip
        }

        // --- A4: numeral -> realized Chord -----------------------------------------------

        // A corpus token that fails to parse is a data bug the lint test in MarkovTests.cpp
        // is meant to catch, not a condition release code should throw on; fall back to the
        // tonic major triad so a bad build never crashes a session over it.
        inline Chord realizeToken(const juce::String& token, int keyRootPc, int octave)
        {
            const auto parsed = parseNumeralToken(token, keyRootPc);

            Chord c;
            c.numeral = token;
            c.rootPc = parsed.valid ? parsed.rootPc : (((keyRootPc % 12) + 12) % 12);
            c.type = parsed.valid ? parsed.type : chordgen::typeIndex("Major");
            // Octavium's get_chord_notes hardcodes octave 4 (chord_progression.py:414,448);
            // honouring the caller's octave here is a deliberate improvement, not a gap.
            c.notes = chordgen::chordNotes(c.rootPc, c.type, octave);
            c.name = juce::String(noteName(c.rootPc)) + " " + chordgen::types()[(size_t) c.type].name;
            return c;
        }

        // --- A5: transition table -----------------------------------------------------

        struct Table
        {
            std::set<juce::String, StringLess> vocabulary;
            Counts startCounts;
            Counts endCounts;
            std::map<juce::String, Counts, StringLess> transitions;
        };

        inline bool moodMatches(const Progression& p, const juce::String& mood)
        {
            if (mood.isEmpty()) // "Any"
                return true;
            for (const auto* m : p.moods)
                if (mood == m)
                    return true;
            return false;
        }

        // Filter by mode+mood, dedupe by full numeral sequence (first wins), then raw bigram
        // counts. No smoothing anywhere, matching A5. Octavium's dedupe order depends on
        // filesystem iteration (Path.iterdir(), OS-defined); ours depends on `source`'s
        // array order, which is deterministic -- a strict improvement, not a behavioural
        // change, since "first wins" was always filesystem-order-dependent and thus
        // arbitrary there. Takes the corpus as a parameter (rather than reaching for
        // corpus() directly) so MarkovTests.cpp can exercise dedupe/mood-filter against a
        // small synthetic corpus instead of cross-referencing the real one by hand.
        inline Table buildTable(const std::vector<Progression>& source, int modeIndex, const juce::String& mood)
        {
            Table table;
            std::set<juce::String, StringLess> seenSequences;

            for (const auto& p : source)
            {
                if (p.mode != modeIndex || p.numerals.empty() || ! moodMatches(p, mood))
                    continue;

                juce::String key;
                for (const auto* n : p.numerals)
                    key << n << "|";
                if (! seenSequences.insert(key).second)
                    continue; // duplicate numeral sequence: first occurrence wins

                table.startCounts[p.numerals.front()]++;
                table.endCounts[p.numerals.back()]++;
                for (const auto* n : p.numerals)
                    table.vocabulary.insert(n);
                for (size_t i = 0; i + 1 < p.numerals.size(); ++i)
                    table.transitions[p.numerals[i]][p.numerals[i + 1]]++;
            }
            return table;
        }

        // The bundled corpus, i.e. what every real call site (generate, regenerateSingle)
        // actually uses.
        inline Table buildTable(int modeIndex, const juce::String& mood)
        {
            return buildTable(corpus(), modeIndex, mood);
        }

        // get_successors (A5): the token's own recorded successors, or (if none) the
        // successors of its bare numeral, or (if still none) nullptr.
        inline const Counts* getSuccessors(const Table& table, const juce::String& token)
        {
            auto it = table.transitions.find(token);
            if (it != table.transitions.end())
                return &it->second;

            const auto stripped = stripSuffix(token);
            if (stripped != token)
            {
                auto it2 = table.transitions.find(stripped);
                if (it2 != table.transitions.end())
                    return &it2->second;
            }
            return nullptr;
        }

        // --- A6: temperature-weighted choice ---------------------------------------------

        inline float clampTemperature(float t) { return juce::jlimit(0.3f, 2.0f, t); }

        // _weighted_choice: skip pow() entirely at T==1 (raw counts), otherwise
        // weight = count^(1/T). T is clamped to Octavium's own UI range (0.3-2.0) first,
        // which bounds the exponent to [0.5, 3.33] and keeps pow() from the overflow/extreme
        // -skew risk the spec flags Octavium as not guarding against.
        inline juce::String weightedChoice(const Counts& counts, float temperature, juce::Random& rng)
        {
            if (counts.empty())
                return {};

            const float t = clampTemperature(temperature);
            std::vector<std::pair<juce::String, double>> weights;
            weights.reserve(counts.size());
            double total = 0.0;
            for (const auto& [token, count] : counts)
            {
                double w = (double) count;
                if (t != 1.0f)
                    w = std::pow(w, 1.0 / (double) t);
                weights.push_back({ token, w });
                total += w;
            }

            const double r = rng.nextDouble() * total;
            double cum = 0.0;
            for (const auto& [token, w] : weights)
            {
                cum += w;
                if (cum >= r)
                    return token;
            }
            return weights.back().first;
        }

        // --- A7/A9: the generation walk --------------------------------------------------

        inline std::vector<juce::String> generateNumerals(int modeIndex, int length, float temperature,
                                                           const juce::String& mood, const juce::String& startToken,
                                                           juce::Random& rng)
        {
            std::vector<juce::String> seq;
            if (length <= 0)
                return seq;

            const auto table = buildTable(modeIndex, mood);

            // Fully empty table (e.g. a mood filter that matches nothing): Octavium's own
            // fallback is `["I"] * length`.
            if (table.vocabulary.empty())
            {
                for (int i = 0; i < length; ++i)
                    seq.push_back("I");
                return seq;
            }

            juce::String first = startToken;
            if (first.isEmpty() || table.vocabulary.count(first) == 0)
                first = weightedChoice(table.startCounts, temperature, rng);
            if (first.isEmpty())
                first = "I";
            seq.push_back(first);

            while ((int) seq.size() < length)
            {
                const auto* successors = getSuccessors(table, seq.back());
                juce::String next = (successors != nullptr) ? weightedChoice(*successors, temperature, rng)
                                                              : juce::String();
                if (next.isEmpty())
                    next = weightedChoice(table.startCounts, temperature, rng); // Octavium's mid-walk fallback
                if (next.isEmpty())
                    next = "I";
                seq.push_back(next);
            }
            return seq;
        }

        // --- A7: repeat-and-truncate fill -------------------------------------------------

        // fillTo <= 0 means "no looping": the caller gets exactly `length` chords back.
        // Otherwise pure repeat-and-truncate, which can cut the final repeat mid-cycle
        // (length 5, fillTo 16 -> 5+5+5+1) -- not beat/bar-aware, matching Octavium exactly.
        inline std::vector<juce::String> fillNumerals(const std::vector<juce::String>& seq, int fillTo)
        {
            if (fillTo <= 0 || seq.empty())
                return seq;

            std::vector<juce::String> out;
            out.reserve((size_t) fillTo);
            while ((int) out.size() < fillTo)
                for (const auto& n : seq)
                {
                    if ((int) out.size() >= fillTo)
                        break;
                    out.push_back(n);
                }
            return out;
        }
    } // namespace detail

    // Unique mood tags used anywhere in this mode's corpus, sorted. "Any" (the empty string)
    // is deliberately not a member of this list -- it's a UI-level sentinel, not a real tag.
    inline juce::StringArray moodsFor(int modeIndex)
    {
        std::set<juce::String, detail::StringLess> tags;
        for (const auto& p : corpus())
            if (p.mode == modeIndex)
                for (const auto* m : p.moods)
                    tags.insert(m);

        juce::StringArray out;
        for (const auto& t : tags)
            out.add(t);
        return out;
    }

    // Octavium's fixed 9-entry start-chord picker (chord_autofill.py): not derived from the
    // corpus, and deliberately doesn't include VII/vii. "Any" is the UI's separate 0th entry,
    // represented here (and everywhere in this API) as an empty startToken string.
    inline const std::vector<const char*>& startTokens()
    {
        static const std::vector<const char*> t = { "I", "i", "IV", "iv", "V", "vi", "VI", "ii", "iii" };
        return t;
    }

    // Generate a progression. `length` unique chords are walked from the transition table,
    // then (if fillTo > 0) repeated-and-truncated to fillTo slots -- e.g. a 16-pad grid asks
    // for length 4-16 but always fillTo 16. `mood` and `startToken` empty both mean "Any".
    inline std::vector<Chord> generate(int modeIndex, int keyRootPc, int octave, int length, float temperature,
                                       const juce::String& mood, const juce::String& startToken, int fillTo,
                                       juce::Random& rng)
    {
        const auto numerals = detail::generateNumerals(modeIndex, juce::jmax(0, length), temperature, mood,
                                                        startToken, rng);
        const auto filled = detail::fillNumerals(numerals, fillTo);

        std::vector<Chord> out;
        out.reserve(filled.size());
        for (const auto& n : filled)
            out.push_back(detail::realizeToken(n, keyRootPc, octave));
        return out;
    }

    // One new chord for a single slot (the per-card "Generate new chord" action). `predecessor`
    // is the numeral in the slot to the left (empty for slot 0, which samples start_counts
    // instead of a bigram). `exclude` is the current chord's numeral, dropped from the
    // candidate set when there's another option so regeneration actually moves -- unless it's
    // the *only* candidate, in which case keeping it is the honest answer (same call ChordGen.h's
    // generateSingle makes when a degree has exactly one legal chord).
    inline Chord regenerateSingle(int modeIndex, int keyRootPc, int octave, const juce::String& predecessorNumeral,
                                  const juce::String& excludeNumeral, float temperature, const juce::String& mood,
                                  juce::Random& rng)
    {
        const auto table = detail::buildTable(modeIndex, mood);

        detail::Counts candidates;
        if (predecessorNumeral.isNotEmpty())
            if (const auto* successors = detail::getSuccessors(table, predecessorNumeral))
                candidates = *successors;
        if (candidates.empty())
            candidates = table.startCounts;

        if (excludeNumeral.isNotEmpty() && candidates.size() > 1)
        {
            candidates.erase(excludeNumeral);
            if (candidates.empty())
                candidates = table.startCounts; // dropping it emptied the set: fall back, excluding nothing
        }

        juce::String token = detail::weightedChoice(candidates, temperature, rng);
        if (token.isEmpty())
            token = "I"; // fully empty table

        return detail::realizeToken(token, keyRootPc, octave);
    }
} // namespace keys::markov
