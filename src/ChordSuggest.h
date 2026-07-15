#pragma once

#include "ChordGen.h"
#include "Chords.h"
#include <juce_core/juce_core.h>
#include <vector>

// "Where could this chord go next?", ported from Octavium (app/chord_suggestions.py).
//
// Four families of move: Neo-Riemannian transforms (the smooth voice-leading ones),
// circle-of-fifths motion, diatonic degrees, and chromatic substitutions. Octavium writes
// each as its own function; every one is the same shape (shift the root by an interval,
// maybe flip the quality), so here they are one table.
//
// Every transform's target quality depends on whether the source chord is major- or
// minor-flavoured, which is read off the chord's third rather than a hard-coded name list.
namespace keys::suggest
{
    struct Suggestion
    {
        int rootPc = 0;
        int type = 0;
        std::vector<int> notes;
        juce::String name;      // e.g. "Ab Minor"
        const char* transform;  // e.g. "H (Hexatonic Pole)"
        const char* category;   // menu grouping
    };

    // Major-flavoured = has a major third. Reading the interval beats Octavium's name list:
    // it stays right when a new type is added to the table.
    inline bool isMajorish(int type)
    {
        const auto& iv = chordgen::types()[(size_t) type].ivals;
        return std::find(iv.begin(), iv.end(), 4) != iv.end();
    }

    namespace detail
    {
        // What quality a transform lands on. `same` keeps the source chord's type.
        enum class Target
        {
            flip,      // major <-> minor
            keepType,  // carry the source type across
            major,
            minor,
            dim,
            aug,
            dom7
        };

        struct Rule
        {
            const char* transform;
            const char* category;
            int shiftIfMajor;   // semitones to move the root when the source is major-flavoured
            int shiftIfMinor;
            Target target;
        };

        inline const std::vector<Rule>& rules()
        {
            static const std::vector<Rule> r = {
                // --- Neo-Riemannian: the classic voice-leading transforms ---
                { "P (Parallel)",        "Neo-Riemannian", 0, 0,  Target::flip },
                { "L (Leading-tone)",    "Neo-Riemannian", 4, 8,  Target::flip },
                { "R (Relative)",        "Neo-Riemannian", 9, 3,  Target::flip },
                { "N (Nebenverwandt)",   "Neo-Riemannian", 5, 5,  Target::flip },
                { "S (Slide)",           "Neo-Riemannian", 1, 11, Target::flip },
                { "H (Hexatonic Pole)",  "Neo-Riemannian", 8, 4,  Target::flip },

                // --- Circle of fifths ---
                { "V (Dominant)",        "Circle of Fifths", 7, 7, Target::keepType },
                { "IV (Subdominant)",    "Circle of Fifths", 5, 5, Target::keepType },
                { "V7 (Dominant 7th)",   "Circle of Fifths", 7, 7, Target::dom7 },
                { "V/V (Secondary Dom)", "Circle of Fifths", 2, 2, Target::dom7 },

                // --- Diatonic degrees ---
                { "ii (Supertonic)",     "Diatonic", 2,  2,  Target::flip },
                { "iii (Mediant)",       "Diatonic", 4,  4,  Target::flip },
                { "vi (Submediant)",     "Diatonic", 9,  9,  Target::flip },
                { "vii (Leading Tone)",  "Diatonic", 11, 11, Target::dim },

                // --- Chromatic / jazz ---
                { "Tritone Sub",         "Chromatic", 6, 6, Target::dom7 },
                { "iv (Minor Plagal)",   "Chromatic", 5, 5, Target::minor },
                { "bII (Neapolitan)",    "Chromatic", 1, 1, Target::major },
                { "Aug6 (Approach)",     "Chromatic", 8, 8, Target::aug },
            };
            return r;
        }

        inline int resolveType(Target t, int sourceType, bool sourceMajorish)
        {
            switch (t)
            {
                case Target::keepType: return sourceType;
                case Target::major:    return chordgen::typeIndex("Major");
                case Target::minor:    return chordgen::typeIndex("Minor");
                case Target::dim:      return chordgen::typeIndex("Diminished");
                case Target::aug:      return chordgen::typeIndex("Augmented");
                case Target::dom7:     return chordgen::typeIndex("Dominant 7th");
                case Target::flip:
                default:
                    return chordgen::typeIndex(sourceMajorish ? "Minor" : "Major");
            }
        }
    } // namespace detail

    // Every suggestion for a chord, in table order (Neo-Riemannian, fifths, diatonic,
    // chromatic). `octave` places the result; pass the source chord's own octave so the
    // suggestion lands in the same register.
    inline std::vector<Suggestion> all(int rootPc, int sourceType, int octave)
    {
        const bool majorish = isMajorish(sourceType);
        const int root = ((rootPc % 12) + 12) % 12;
        std::vector<Suggestion> out;
        out.reserve(detail::rules().size());

        for (const auto& r : detail::rules())
        {
            const int newRoot = (root + (majorish ? r.shiftIfMajor : r.shiftIfMinor)) % 12;
            const int newType = detail::resolveType(r.target, sourceType, majorish);
            Suggestion s;
            s.rootPc = newRoot;
            s.type = newType;
            s.notes = chordgen::chordNotes(newRoot, newType, octave);
            s.name = juce::String(chords::noteName(newRoot)) + " " + chordgen::types()[(size_t) newType].name;
            s.transform = r.transform;
            s.category = r.category;
            out.push_back(std::move(s));
        }
        return out;
    }

    // The (root, type) a set of played notes most nearly is, so a hand-built pad chord can
    // be fed into the transforms. Chords::detect already solves the hard half of this
    // (which rotation is the root), so this reuses its label rather than scoring again.
    inline std::pair<int, int> analyse(const std::vector<int>& notes)
    {
        if (notes.empty())
            return { 0, chordgen::typeIndex("Major") };

        // Match the played pitch-class set against each type on each root, best coverage wins.
        std::set<int> pcs;
        for (int n : notes)
            pcs.insert(chords::pitchClass(n));

        int bestRoot = chords::pitchClass(*std::min_element(notes.begin(), notes.end()));
        int bestType = chordgen::typeIndex("Major");
        double bestScore = -1e9;
        for (int root = 0; root < 12; ++root)
        {
            if (pcs.count(root) == 0)
                continue; // the root must actually be sounding
            for (int t = 0; t < (int) chordgen::types().size(); ++t)
            {
                std::set<int> tpc;
                for (int i : chordgen::types()[(size_t) t].ivals)
                    tpc.insert((root + i) % 12);
                int covered = 0, extras = 0, missing = 0;
                for (int p : tpc)
                    if (pcs.count(p)) ++covered; else ++missing;
                for (int p : pcs)
                    if (tpc.count(p) == 0) ++extras;
                const double score = 2.0 * covered - 1.0 * extras - 1.5 * missing;
                if (score > bestScore)
                {
                    bestScore = score;
                    bestRoot = root;
                    bestType = t;
                }
            }
        }
        return { bestRoot, bestType };
    }
} // namespace keys::suggest
