#pragma once

#include <juce_core/juce_core.h>
#include <set>
#include <vector>

// Chord naming, ported from Octavium (app/chord_selector.py). Pure logic, no UI, so it
// can be unit-tested like NoteMath.h. Given the sounding MIDI notes it returns a compact
// chord label such as "Cm7" or "Gsus4": it tries each of the 12 pitch classes as the
// root, rotates the note set so the candidate root becomes 0, scores every template, and
// keeps the best (root mandatory; 3rd and 5th may be omitted for a small penalty).
namespace keys::chords
{
    inline int pitchClass(int note) { return ((note % 12) + 12) % 12; }

    inline const char* noteName(int pc)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[((pc % 12) + 12) % 12];
    }

    struct Template
    {
        const char* suffix;    // compact label appended to the root, e.g. "m7" ("" = major)
        std::vector<int> pcs;  // pitch classes relative to root, root (0) included
    };

    inline const std::vector<Template>& templates()
    {
        // Subset of Octavium's _RAW_CHORDS, the qualities worth naming for a played keyboard.
        static const std::vector<Template> t = {
            { "",      { 0, 4, 7 } },        // Major
            { "m",     { 0, 3, 7 } },        // Minor
            { "dim",   { 0, 3, 6 } },        // Diminished
            { "aug",   { 0, 4, 8 } },        // Augmented
            { "sus2",  { 0, 2, 7 } },        // Sus2
            { "sus4",  { 0, 5, 7 } },        // Sus4
            { "6",     { 0, 4, 7, 9 } },     // Major 6th
            { "m6",    { 0, 3, 7, 9 } },     // Minor 6th
            { "maj7",  { 0, 4, 7, 11 } },    // Major 7th
            { "m7",    { 0, 3, 7, 10 } },    // Minor 7th
            { "7",     { 0, 4, 7, 10 } },    // Dominant 7th
            { "dim7",  { 0, 3, 6, 9 } },     // Diminished 7th
            { "m7b5",  { 0, 3, 6, 10 } },    // Half Diminished
            { "mMaj7", { 0, 3, 7, 11 } },    // Minor Major 7th
            { "add9",  { 0, 2, 4, 7 } },     // Add9
            { "9",     { 0, 2, 4, 7, 10 } }, // Dominant 9th
            { "maj9",  { 0, 2, 4, 7, 11 } }, // Major 9th
            { "m9",    { 0, 2, 3, 7, 10 } }, // Minor 9th
            { "5",     { 0, 7 } },           // Power
        };
        return t;
    }

    // Octavium's score_match: 2*covered - extras - 1.5*essentialMissing - 0.25*optionalMissing.
    // The root (0) must be played; the 3rd (3/4) and 5th (6/7/8) may be omitted cheaply.
    inline double scoreMatch(const std::set<int>& rel, const std::vector<int>& tpl, int& sizeOut)
    {
        const std::set<int> tset(tpl.begin(), tpl.end());
        sizeOut = (int) tset.size();
        if (rel.count(0) == 0)
            return -1.0; // this rotation's root isn't sounding

        int covered = 0, extras = 0, essMissing = 0, optMissing = 0;
        for (int t : tset)
            if (rel.count(t)) ++covered;
        for (int r : rel)
            if (tset.count(r) == 0) ++extras;
        for (int t : tset)
        {
            if (rel.count(t)) continue;
            if (t == 3 || t == 4)                 ++optMissing; // 3rd
            else if (t == 6 || t == 7 || t == 8)  ++optMissing; // 5th
            else                                  ++essMissing;
        }
        return 2.0 * covered - 1.0 * extras - 1.5 * essMissing - 0.25 * optMissing;
    }

    // Compact chord label for the given notes, or "" for none. A lone pitch class returns
    // its note name; two-plus notes are named by the best-scoring template, tie broken
    // toward the larger chord (matching Octavium).
    inline juce::String detect(const std::vector<int>& notes)
    {
        std::set<int> pcs;
        for (int n : notes)
            pcs.insert(pitchClass(n));
        if (pcs.empty())
            return {};
        if (pcs.size() == 1)
            return juce::String(noteName(*pcs.begin()));

        double bestScore = 0.0;
        int bestSize = 0, bestRoot = -1;
        const Template* best = nullptr;
        for (int root = 0; root < 12; ++root)
        {
            std::set<int> rel;
            for (int p : pcs)
                rel.insert(((p - root) % 12 + 12) % 12);
            for (const auto& tpl : templates())
            {
                int sz = 0;
                const double s = scoreMatch(rel, tpl.pcs, sz);
                if (s > bestScore || (s == bestScore && sz > bestSize))
                {
                    bestScore = s;
                    bestSize = sz;
                    bestRoot = root;
                    best = &tpl;
                }
            }
        }
        if (best != nullptr && bestRoot >= 0 && bestScore > 0.0)
            return juce::String(noteName(bestRoot)) + best->suffix;

        // Fallback: lowest note's name + note count.
        int lowest = notes.front();
        for (int n : notes)
            lowest = juce::jmin(lowest, n);
        return juce::String(noteName(pitchClass(lowest))) + "(" + juce::String((int) pcs.size()) + ")";
    }
}
