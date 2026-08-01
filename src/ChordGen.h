#pragma once

#include "ScaleModes.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <set>
#include <vector>

// Chord generation, ported from Octavium (app/chord_autofill.py). Pure logic, no UI, so
// it unit-tests like NoteMath.h and Chords.h.
//
// The shape of the idea: build a *weighted pool* of candidate chords for a key and mode,
// then sample from it. Scale Compliance decides how far outside the key the pool reaches
// (diatonic -> borrowed -> secondary dominants -> chromatic), and Lock Influence re-weights
// the pool toward the chord families you already locked, so regeneration stays in the
// character of what you kept.
//
// Two of Octavium's three generator sources are not ported: the MIDI Library source and
// the Markov source both read its external ~30 MB chord pack, which a VST3 has no business
// shipping or hunting for on disk. This is the algorithmic source, which needs nothing.
namespace keys::chordgen
{
    // Which broad family a type belongs to. Lock Influence works at this granularity:
    // locking three 7th chords biases new chords toward 7ths, not toward those exact chords.
    enum class Family
    {
        triad,
        seventh,
        sixth,
        add,
        extended
    };

    struct Type
    {
        const char* name;         // display name, e.g. "Minor 7th"
        std::vector<int> ivals;   // semitones from the root
        Family family;
    };

    // Octavium's CHORD_INTERVALS. Order is load-bearing only in that saved pads store the
    // type index, so append new types at the end rather than inserting.
    inline const std::vector<Type>& types()
    {
        static const std::vector<Type> t = {
            // Triads (3 notes)
            { "Major",           { 0, 4, 7 },          Family::triad },
            { "Minor",           { 0, 3, 7 },          Family::triad },
            { "Diminished",      { 0, 3, 6 },          Family::triad },
            { "Augmented",       { 0, 4, 8 },          Family::triad },
            { "Sus2",            { 0, 2, 7 },          Family::triad },
            { "Sus4",            { 0, 5, 7 },          Family::triad },
            // 4-note chords
            { "Major 7th",       { 0, 4, 7, 11 },      Family::seventh },
            { "Minor 7th",       { 0, 3, 7, 10 },      Family::seventh },
            { "Dominant 7th",    { 0, 4, 7, 10 },      Family::seventh },
            { "Diminished 7th",  { 0, 3, 6, 9 },       Family::seventh },
            { "Half Diminished", { 0, 3, 6, 10 },      Family::seventh },
            { "Minor Major 7th", { 0, 3, 7, 11 },      Family::seventh },
            { "Major 6th",       { 0, 4, 7, 9 },       Family::sixth },
            { "Minor 6th",       { 0, 3, 7, 9 },       Family::sixth },
            { "Add9",            { 0, 4, 7, 14 },      Family::add },
            { "Minor Add9",      { 0, 3, 7, 14 },      Family::add },
            // 5-note chords
            { "Major 9th",       { 0, 4, 7, 11, 14 },  Family::extended },
            { "Minor 9th",       { 0, 3, 7, 10, 14 },  Family::extended },
            { "Dominant 9th",    { 0, 4, 7, 10, 14 },  Family::extended },
            { "6/9",             { 0, 4, 7, 9, 14 },   Family::extended },
        };
        return t;
    }

    inline int typeIndex(const juce::String& name)
    {
        const auto& t = types();
        for (int i = 0; i < (int) t.size(); ++i)
            if (name == t[(size_t) i].name)
                return i;
        return 0;
    }

    // The types worth reaching for on a degree of each base quality (Octavium _EXTENDED_TYPES).
    // This is what turns a plain diatonic triad set into something you'd actually play.
    inline const std::vector<int>& extendedTypes(modes::Quality q)
    {
        const auto idx = [](const char* n) { return typeIndex(n); };
        static const std::vector<int> maj = { idx("Major"), idx("Major 7th"), idx("Major 9th"), idx("Add9"),
                                              idx("Sus2"), idx("Sus4"), idx("Major 6th"), idx("6/9"),
                                              idx("Dominant 7th"), idx("Dominant 9th") };
        static const std::vector<int> min = { idx("Minor"), idx("Minor 7th"), idx("Minor 9th"), idx("Minor Add9"),
                                              idx("Minor 6th"), idx("Minor Major 7th"), idx("Sus2"), idx("Sus4") };
        static const std::vector<int> dim = { idx("Diminished"), idx("Diminished 7th"), idx("Half Diminished") };
        static const std::vector<int> aug = { idx("Augmented"), idx("Major 7th"), idx("Dominant 7th") };
        switch (q)
        {
            case modes::Quality::minor:      return min;
            case modes::Quality::diminished: return dim;
            case modes::Quality::augmented:  return aug;
            default:                         return maj;
        }
    }

    // The plain triad type for a degree quality, used for the strict diatonic seed set.
    inline int baseType(modes::Quality q)
    {
        switch (q)
        {
            case modes::Quality::minor:      return typeIndex("Minor");
            case modes::Quality::diminished: return typeIndex("Diminished");
            case modes::Quality::augmented:  return typeIndex("Augmented");
            default:                         return typeIndex("Major");
        }
    }

    inline std::vector<int> chordNotes(int rootPc, int type, int octave)
    {
        const auto& t = types();
        const auto& iv = t[(size_t) juce::jlimit(0, (int) t.size() - 1, type)].ivals;
        const int base = octave * 12 + ((rootPc % 12) + 12) % 12;
        std::vector<int> out;
        out.reserve(iv.size());
        for (int i : iv)
            out.push_back(base + i);
        return out;
    }

    // inversion 0 = root position; N moves the N lowest notes up an octave.
    inline std::vector<int> applyInversion(std::vector<int> notes, int inversion)
    {
        if (inversion <= 0 || notes.empty())
            return notes;
        std::sort(notes.begin(), notes.end());
        const int inv = juce::jmin(inversion, (int) notes.size() - 1);
        for (int i = 0; i < inv; ++i)
            notes[(size_t) i] += 12;
        std::sort(notes.begin(), notes.end());
        return notes;
    }

    // ---- Voicings -------------------------------------------------------------------------
    //
    // The arrangements one chord can sit in, which is what the pad menu's "Next voicing" walks:
    // root position, one inversion per note above the root, then a spread, then round to root
    // again. Same pitch classes throughout - a voicing changes where the notes are, never which
    // notes they are. The vocabulary is the generator's own: `applyInversion` above and the
    // genInv0..genInv3 parameters that feed `Options::inversions` name the first four steps, and
    // the spread is the one addition, because it is the voicing those four cannot express.

    // Spread: the voice just above the bass opens up an octave, leaving the root where it is.
    // Below three notes there is no inner voice to move, so the chord comes back as it went in.
    //
    // Opening upwards rather than dropping the second voice from the top (the jazz "drop 2")
    // is what keeps the cycle in one register: the root stays the lowest note, so
    // rootPosition() below reads the same base octave back out of a spread chord and the walk
    // never creeps an octave down each time round.
    inline std::vector<int> applySpread(std::vector<int> notes)
    {
        if (notes.size() < 3)
            return notes;
        std::sort(notes.begin(), notes.end());
        notes[1] += 12;
        std::sort(notes.begin(), notes.end());
        return notes;
    }

    // Every note re-stacked upward from the root, in the register the chord already sits in.
    // This is the base every voicing is built from, which is what makes the cycle stable
    // however the chord arrived (generated, captured, or already inverted).
    //
    // A repeated pitch class collapses to one note. It used to be kept and stacked an octave
    // higher, on the reasoning that re-voicing should never change how many notes a chord has,
    // and that broke the cycle in two ways that two hands on the keybed produce constantly.
    // The register was not read back: {60,64,67,72} came back as {60,72,76,79}, so "Next
    // voicing" wrote the chord an octave *up* and climbed again on every press until it ran off
    // the keyboard and the menu item greyed for good. And a doubled note cannot survive the
    // walk anyway - the last inversion of a doubled root is literally root position an octave
    // up, and an inversion that lifts one copy onto the other emits the same MIDI note twice,
    // which makes the polyphony cap count two voices and chords::detect name four notes in a
    // three-note chord. So the double is dropped once, on the first press, and in exchange the
    // walk closes in one register and no arrangement can ever repeat a pitch.
    inline std::vector<int> rootPosition(const std::vector<int>& notes, int rootPc)
    {
        if (notes.empty())
            return {};
        const int pc = ((rootPc % 12) + 12) % 12;
        std::vector<int> offsets;
        offsets.reserve(notes.size());
        for (const int n : notes)
            offsets.push_back(((((n % 12) + 12) % 12) - pc + 12) % 12);
        std::sort(offsets.begin(), offsets.end());
        offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());

        const int lowest = *std::min_element(notes.begin(), notes.end());
        int base = (lowest / 12) * 12 + pc; // the root, in the chord's own octave
        if (base > lowest)
            base -= 12;

        std::vector<int> out;
        out.reserve(offsets.size());
        for (const int off : offsets)
            out.push_back(base + off);
        return out;
    }

    // How many arrangements the cycle has: root position, an inversion per note above the root,
    // and the spread.
    inline int voicingCount(int noteCount) { return juce::jmax(2, noteCount + 1); }

    inline const char* voicingName(int voicing, int noteCount)
    {
        const int n = voicingCount(noteCount);
        const int v = ((voicing % n) + n) % n;
        if (v == n - 1)
            return "Spread";
        static const char* const inversions[] = { "Root", "1st inv", "2nd inv", "3rd inv", "4th inv" };
        return inversions[juce::jlimit(0, 4, v)];
    }

    // The chord in one arrangement. `root` is what rootPosition() handed back; `voicing` wraps,
    // so stepping past the last one comes back to root position.
    inline std::vector<int> applyVoicing(const std::vector<int>& root, int voicing)
    {
        if (root.empty())
            return {};
        const int n = voicingCount((int) root.size());
        const int v = ((voicing % n) + n) % n;
        return v == n - 1 ? applySpread(root) : applyInversion(root, v);
    }

    // Which arrangement a set of notes is already in, or -1 for one that is none of them (a
    // chord built by hand on the keyboard). Matched on shape - every note's distance from the
    // lowest - so the answer does not depend on the register, and nothing has to be remembered
    // on the card for "Next voicing" to know where it is in the cycle.
    inline int voicingOf(const std::vector<int>& notes, int rootPc)
    {
        if (notes.empty())
            return -1;
        const auto shape = [](std::vector<int> v)
        {
            std::sort(v.begin(), v.end());
            const int lo = v.front();
            for (auto& x : v)
                x -= lo;
            return v;
        };
        const auto base = rootPosition(notes, rootPc);
        const auto want = shape(notes);
        for (int v = 0; v < voicingCount((int) base.size()); ++v)
            if (shape(applyVoicing(base, v)) == want)
                return v;
        return -1;
    }

    struct Options
    {
        int octave = 4;
        std::vector<int> noteCounts { 3, 4, 5 };  // which chord sizes may be generated
        std::vector<int> inversions { 0 };        // which inversions may be picked (0 = root only)
        float scaleCompliance = 1.0f;             // 1 = strictly diatonic, 0 = fully chromatic
        float lockInfluence = 0.0f;               // how hard locked chords bias new ones
    };

    struct Chord
    {
        int rootPc = 0;
        int type = 0;
        std::vector<int> notes;
        int degree = -1; // scale degree this came from, or -1 if it's from outside the scale
    };

    namespace detail
    {
        struct Candidate
        {
            int rootPc, type;
            std::vector<int> notes;
            double weight;
        };

        inline bool noteCountOk(int type, const std::vector<int>& allowed)
        {
            if (allowed.empty())
                return true;
            const int n = (int) types()[(size_t) type].ivals.size();
            return std::find(allowed.begin(), allowed.end(), n) != allowed.end();
        }

        inline int pickInversion(int noteCount, const std::vector<int>& allowed, juce::Random& rng)
        {
            std::vector<int> valid;
            for (int i : allowed)
                if (i < noteCount)
                    valid.push_back(i);
            if (valid.empty())
                return 0;
            return valid[(size_t) rng.nextInt((int) valid.size())];
        }

        // Is every note of this chord in the key?
        //
        // Octavium doesn't ask, and so its "strictly diatonic" tier isn't: Sus2 and Add9 are
        // offered on every major/minor degree, but the note they add is only sometimes in the
        // scale (E Sus2 in C major wants F#). On a plugin built around never playing a wrong
        // note, 100% compliance has to mean 100%, so the diatonic tier is filtered.
        inline bool inScale(int rootPc, const modes::Mode& mode, int chordRootPc, int type)
        {
            std::set<int> scalePcs;
            for (int iv : mode.intervals)
                scalePcs.insert(((rootPc + iv) % 12 + 12) % 12);
            for (int iv : types()[(size_t) type].ivals)
                if (scalePcs.count(((chordRootPc + iv) % 12 + 12) % 12) == 0)
                    return false;
            return true;
        }

        // The scale degree a pitch class sits on in this key/mode, or -1 if it's outside.
        inline int degreeOf(int rootPc, const modes::Mode& mode, int chordRootPc)
        {
            for (int i = 0; i < (int) mode.intervals.size(); ++i)
                if (((rootPc + mode.intervals[(size_t) i]) % 12 + 12) % 12 == ((chordRootPc % 12) + 12) % 12)
                    return i;
            return -1;
        }

        // Octavium's _build_weighted_pool: four tiers, each opened up by lower compliance.
        inline std::vector<Candidate> buildPool(int rootPc, int modeIndex, const Options& opts, juce::Random& rng)
        {
            const auto& mode = modes::get(modeIndex);
            std::vector<Candidate> pool;
            std::set<std::pair<int, int>> seen;

            const auto add = [&](int cr, int type, double w)
            {
                const int cr12 = ((cr % 12) + 12) % 12;
                if (! seen.insert({ cr12, type }).second)
                    return;
                if (! noteCountOk(type, opts.noteCounts))
                    return;
                auto notes = chordNotes(cr12, type, opts.octave);
                notes = applyInversion(notes, pickInversion((int) notes.size(), opts.inversions, rng));
                pool.push_back({ cr12, type, notes, w });
            };

            // 1. Diatonic: full weight, and genuinely in the key.
            for (int i = 0; i < (int) mode.intervals.size(); ++i)
            {
                const int cr = rootPc + mode.intervals[(size_t) i];
                for (int t : extendedTypes(mode.qualities[(size_t) i]))
                    if (inScale(rootPc, mode, cr, t))
                        add(cr, t, 1.0);
            }

            // 2. Borrowed from parallel modes (modal interchange).
            if (opts.scaleCompliance < 0.95f)
            {
                const double w = (1.0 - (double) opts.scaleCompliance) * 0.8;
                for (int pm : modes::parallelModes(modeIndex))
                {
                    const auto& par = modes::get(pm);
                    for (int i = 0; i < (int) par.intervals.size(); ++i)
                    {
                        const int cr = rootPc + par.intervals[(size_t) i];
                        for (int t : extendedTypes(par.qualities[(size_t) i]))
                            add(cr, t, w);
                    }
                }
            }

            // 3. Secondary dominants: the V of each degree.
            if (opts.scaleCompliance < 0.70f)
            {
                const double w = (0.70 - (double) opts.scaleCompliance) * 0.9;
                for (int interval : mode.intervals)
                {
                    const int dom = (rootPc + interval + 7) % 12;
                    for (const char* n : { "Dominant 7th", "Dominant 9th", "Major" })
                        add(dom, typeIndex(n), w);
                }
            }

            // 4. Fully chromatic roots.
            if (opts.scaleCompliance < 0.40f)
            {
                const double w = (0.40 - (double) opts.scaleCompliance) * 0.6;
                std::set<int> scaleRoots;
                for (int interval : mode.intervals)
                    scaleRoots.insert(((rootPc + interval) % 12 + 12) % 12);
                for (int semi = 0; semi < 12; ++semi)
                    if (scaleRoots.count(semi) == 0)
                        for (const char* n : { "Major", "Minor", "Dominant 7th", "Major 7th", "Minor 7th" })
                            add(semi, typeIndex(n), w);
            }

            return pool;
        }

        // What families the locked chords are made of, as a share of the locked set.
        inline std::vector<double> lockedFamilyPrefs(const std::vector<int>& lockedTypes)
        {
            std::vector<double> prefs(5, 0.0);
            if (lockedTypes.empty())
                return prefs;
            for (int t : lockedTypes)
                prefs[(size_t) types()[(size_t) t].family] += 1.0;
            for (auto& p : prefs)
                p /= (double) lockedTypes.size();
            return prefs;
        }

        // Re-weight the pool toward the locked families. At full influence a chord in the
        // dominant family gets up to 2x weight; nothing is ever fully suppressed.
        inline void applyLockInfluence(std::vector<Candidate>& pool, const std::vector<double>& prefs,
                                       float influence)
        {
            if (influence <= 0.0f)
                return;
            for (auto& c : pool)
            {
                const double pref = prefs[(size_t) types()[(size_t) c.type].family];
                const double boost = juce::jmax(1.0 + (pref * 2.0 - 0.3) * (double) influence, 0.15);
                c.weight *= boost;
            }
        }

        inline int weightedPick(const std::vector<Candidate>& pool, juce::Random& rng)
        {
            double total = 0.0;
            for (const auto& c : pool)
                total += juce::jmax(c.weight, 0.01);
            double r = rng.nextDouble() * total, cum = 0.0;
            for (int i = 0; i < (int) pool.size(); ++i)
            {
                cum += juce::jmax(pool[(size_t) i].weight, 0.01);
                if (cum >= r)
                    return i;
            }
            return pool.empty() ? -1 : (int) pool.size() - 1;
        }
    } // namespace detail

    // Fill `count` slots for a key and mode. Seeds the strict diatonic degrees first (so
    // the basics are always there), then weighted-samples the rest from the pool, then
    // shuffles the sampled tail so the result doesn't read as "diatonic first, extras after".
    inline std::vector<Chord> generate(int rootPc, int modeIndex, int count, const Options& opts,
                                       const std::vector<int>& lockedTypes, juce::Random& rng)
    {
        const auto& mode = modes::get(modeIndex);
        auto pool = detail::buildPool(rootPc, modeIndex, opts, rng);
        if (! lockedTypes.empty())
            detail::applyLockInfluence(pool, detail::lockedFamilyPrefs(lockedTypes), opts.lockInfluence);

        std::vector<Chord> out;
        std::set<std::pair<int, int>> used;

        // Seed: the plain diatonic chord on each degree, in degree order.
        for (int i = 0; i < (int) mode.intervals.size() && (int) out.size() < count; ++i)
        {
            const int type = baseType(mode.qualities[(size_t) i]);
            if (! detail::noteCountOk(type, opts.noteCounts))
                continue;
            const int cr = ((rootPc + mode.intervals[(size_t) i]) % 12 + 12) % 12;
            auto notes = chordNotes(cr, type, opts.octave);
            notes = applyInversion(notes, detail::pickInversion((int) notes.size(), opts.inversions, rng));
            out.push_back({ cr, type, notes, i });
            used.insert({ cr, type });
        }

        // Fill the rest by weighted sampling, never repeating a (root, type).
        const int seeded = (int) out.size();
        while ((int) out.size() < count)
        {
            std::vector<detail::Candidate> avail;
            for (const auto& c : pool)
                if (used.count({ c.rootPc, c.type }) == 0)
                    avail.push_back(c);
            if (avail.empty())
                break;
            const int i = detail::weightedPick(avail, rng);
            if (i < 0)
                break;
            const auto& c = avail[(size_t) i];
            out.push_back({ c.rootPc, c.type, c.notes, detail::degreeOf(rootPc, mode, c.rootPc) });
            used.insert({ c.rootPc, c.type });
        }

        // Shuffle only the sampled tail; the diatonic seed keeps its degree order.
        for (int i = (int) out.size() - 1; i > seeded; --i)
            std::swap(out[(size_t) i], out[(size_t) (seeded + rng.nextInt(i - seeded + 1))]);
        return out;
    }

    // One new chord for a given scale degree, avoiding `currentType` so "regenerate" always
    // moves. Degree -1 (a chord from outside the scale) picks any degree.
    inline Chord generateSingle(int rootPc, int modeIndex, int degree, int currentType,
                                const Options& opts, const std::vector<int>& lockedTypes, juce::Random& rng)
    {
        const auto& mode = modes::get(modeIndex);
        const int deg = degree >= 0 ? degree % (int) mode.intervals.size()
                                    : rng.nextInt((int) mode.intervals.size());
        const int chordRoot = ((rootPc + mode.intervals[(size_t) deg]) % 12 + 12) % 12;

        auto pool = detail::buildPool(rootPc, modeIndex, opts, rng);

        // Prefer candidates on this degree's root; if compliance is loose enough and that
        // root has nothing new, allow the whole pool rather than returning the same chord.
        std::vector<detail::Candidate> cands;
        for (const auto& c : pool)
            if (c.rootPc == chordRoot && c.type != currentType)
                cands.push_back(c);
        if (cands.empty() && opts.scaleCompliance < 0.70f)
            for (const auto& c : pool)
                if (c.type != currentType)
                    cands.push_back(c);

        if (! lockedTypes.empty())
            detail::applyLockInfluence(cands, detail::lockedFamilyPrefs(lockedTypes), opts.lockInfluence);

        if (! cands.empty())
        {
            const int i = detail::weightedPick(cands, rng);
            if (i >= 0)
            {
                const auto& c = cands[(size_t) i];
                return { c.rootPc, c.type, c.notes, detail::degreeOf(rootPc, mode, c.rootPc) };
            }
        }

        // Fallback: another type on this degree that still meets the constraints. Octavium
        // drops the note-count filter when nothing matches, which both breaks the setting and
        // can hand back the very chord it was asked to replace. Some degrees genuinely have
        // no alternative (a diminished degree with only triads allowed offers exactly one
        // chord), and there keeping the chord is the honest answer.
        std::vector<int> alts;
        for (int t : extendedTypes(mode.qualities[(size_t) deg]))
        {
            if (t == currentType || ! detail::noteCountOk(t, opts.noteCounts))
                continue;
            if (opts.scaleCompliance >= 0.95f && ! detail::inScale(rootPc, mode, chordRoot, t))
                continue;
            alts.push_back(t);
        }
        const int type = alts.empty() ? currentType : alts[(size_t) rng.nextInt((int) alts.size())];
        auto notes = chordNotes(chordRoot, type, opts.octave);
        notes = applyInversion(notes, detail::pickInversion((int) notes.size(), opts.inversions, rng));
        return { chordRoot, type, notes, deg };
    }
} // namespace keys::chordgen
