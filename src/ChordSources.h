#pragma once

#include "ChordGen.h"
#include "ScaleModes.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

// Five more ways to fill a page of chords, plus a post-pass any of them (or ChordGen.h's
// own weighted-pool generate(), or ChordMarkov.h's walk) can run through afterward. Pure
// logic, no UI, so it unit-tests like ChordGen.h and ChordMarkov.h.
//
// Every generator here returns keys::chordgen::Chord rather than inventing its own chord
// type: the pad grid, the arp slots and the card menu all already speak that struct, and a
// second chord type would just be one more place a note-count or MIDI-range bug could hide.
// Two things chordgen::Chord's regular generator promises that these sources don't try to
// re-promise: these are not scale-compliance-gated (a circle-of-fifths walk or a negative
// harmony mirror is allowed to leave the key on purpose, that's the point of picking it) and
// they don't take a `lockedTypes` bias (a source like this replaces a whole page in one
// call; per-chord lock influence belongs to the weighted-pool generator, which regenerates
// one degree at a time).
namespace keys::sources
{
    namespace detail
    {
        // A degree lookup already exists in chordgen::detail::degreeOf, built and tested
        // against the exact modular arithmetic every other part of chord generation uses
        // (accidentals, wraparound at pc 0/11). Reimplementing it here to avoid reaching
        // into another header's detail namespace would be exactly the kind of redefinition
        // the port spec warns against -- one copy drifting from the other is how a chord
        // reads as "outside the scale" in one place and "degree 3" in another.
        using chordgen::detail::degreeOf;

        // The extended type for a quality whose note count matches what the caller asked
        // for (planing's constant-structure walk needs the *same size* chord on every
        // degree, not just "the diatonic chord"). Falls back to the plain triad when a
        // degree's quality has nothing of that size -- a diminished vii has no diminished
        // 7-note extended voicing in extendedTypes(), and the honest answer is the triad,
        // not silently picking an unrelated size.
        //
        // Sizes 5 and 6 (2026-11-02) are the 9th and 11th/13th shapes: a major degree gets the
        // first 5-note type (Major 9th) or 6-note type (Major 13th), a minor degree Minor 9th or
        // Minor 11th. A diminished degree has no 5- or 6-note type of its own, and the nearest
        // sensible answer is Half Diminished, the seventh chord the degree carries in every mode
        // that has one, rather than a triad that would quietly shrink the chord.
        inline int typeForDegreeSize(modes::Quality q, size_t noteCount)
        {
            for (int t : chordgen::extendedTypes(q))
                if (chordgen::types()[(size_t) t].ivals.size() == noteCount)
                    return t;
            if (q == modes::Quality::diminished && noteCount >= 5)
                return chordgen::typeIndex("Half Diminished");
            return chordgen::baseType(q);
        }

        // The label for a chord that diatonic planing built by stacking scale steps. The notes
        // are what sound; this only names them, so it first looks for a type whose intervals are
        // exactly the notes' (an Ionian I stacked in thirds to the 9th *is* Major 9th), and only
        // then falls back on the nearest type for the degree's quality and the shape asked for.
        inline int typeForStack(const std::vector<int>& notes, modes::Quality q, int shapeType)
        {
            const auto& all = chordgen::types();
            std::vector<int> iv;
            for (int n : notes)
                iv.push_back(n - notes.front());
            for (int t = 0; t < (int) all.size(); ++t)
                if (all[(size_t) t].ivals == iv)
                    return t;

            const auto name = juce::String(all[(size_t) shapeType].name);
            if (name == "Quartal" || name == "Quintal")
                return shapeType; // quality-blind shapes: the stack is the identity
            if (name == "Add9" || name == "Minor Add9")
            {
                const bool minorish = q == modes::Quality::minor || q == modes::Quality::diminished;
                return chordgen::typeIndex(minorish ? "Minor Add9" : "Add9");
            }
            return typeForDegreeSize(q, all[(size_t) shapeType].ivals.size());
        }

        // ---- Neo-Riemannian PLR -------------------------------------------------------
        //
        // A triad tracked as three *absolute* MIDI notes rather than a root pitch class,
        // because P/L/R are defined by which single voice moves and by how little -- the
        // entire musical point of the PLR group is that two of the three notes never move
        // at all. Losing the register would mean rebuilding the triad from a bare root
        // pitch class every step, which is indistinguishable from a chord progression that
        // happens to share PLR's pitch-class math but throws away the voice leading that
        // makes PLR worth having a source for.
        //
        // root/third/fifth name the roles the *current* quality assigns to each field, not
        // fixed physical voices: a transform can hand the role of "root" to what used to be
        // the third (L on a major triad does exactly that), and the struct is reassigned
        // accordingly rather than tagged by original identity.
        struct Triad
        {
            int root, third, fifth;
            bool major;
        };

        // major -> minor: the third drops a semitone (C E G -> C Eb G).
        // minor -> major: the third rises a semitone (C Eb G -> C E G).
        // Root and fifth never move -- P is the smallest possible PLR step.
        inline Triad applyP(const Triad& t)
        {
            return t.major ? Triad { t.root, t.third - 1, t.fifth, false }
                            : Triad { t.root, t.third + 1, t.fifth, true };
        }

        // major -> minor: the root drops a semitone; what was the third becomes the new
        // root and what was the fifth becomes the new third (C E G -> B E G, read as E
        // minor: E=root, G=third, B=fifth).
        // minor -> major: the fifth rises a semitone and becomes the new root, with the old
        // root and third sliding into the third/fifth roles (A C E -> A C F, read as F
        // major: F=root, A=third, C=fifth).
        inline Triad applyL(const Triad& t)
        {
            return t.major ? Triad { t.third, t.fifth, t.root - 1, false }
                            : Triad { t.fifth + 1, t.root, t.third, true };
        }

        // major -> relative minor: the fifth rises a whole step and becomes the new root;
        // old root and third become the new third/fifth (C E G -> C E A, read as A minor).
        // minor -> relative major: the root drops a whole step... except the *resulting*
        // pitch-class set restacks with the old third as the new root (A C E -> G C E,
        // which is C major's own notes reordered: C=root, E=third, G=fifth). That's the
        // standard relative-major-is-a-minor-third-above relationship, arrived at here by
        // tracking which voice actually moved rather than by naming the interval.
        inline Triad applyR(const Triad& t)
        {
            return t.major ? Triad { t.fifth + 2, t.root, t.third, false }
                            : Triad { t.third, t.fifth, t.root - 2, true };
        }

        // A per-note clamp would turn a minor third into something else the moment one
        // voice crossed 0 or 127 while its neighbours didn't; shifting the whole triad by
        // an octave preserves every interval in it, which is the one thing this transform
        // group is not allowed to change.
        inline void keepTriadInRange(Triad& t)
        {
            while (t.root < 0 || t.third < 0 || t.fifth < 0)
            {
                t.root += 12;
                t.third += 12;
                t.fifth += 12;
            }
            while (t.root > 127 || t.third > 127 || t.fifth > 127)
            {
                t.root -= 12;
                t.third -= 12;
                t.fifth -= 12;
            }
        }

        inline chordgen::Chord triadToChord(const Triad& t)
        {
            chordgen::Chord c;
            c.rootPc = ((t.root % 12) + 12) % 12;
            c.type = chordgen::typeIndex(t.major ? "Major" : "Minor");
            c.degree = -1; // a PLR walk roams off the diatonic degree grid by design
            c.notes = { t.root, t.third, t.fifth };
            std::sort(c.notes.begin(), c.notes.end());
            return c;
        }

        // ---- Negative harmony -----------------------------------------------------------

        // The standard axis: for a key rooted at rootPc, the mirror line sits midway
        // between the tonic and its dominant. Verified against the two cases the port spec
        // hands us -- C major -> C minor and G major -> F minor, both in the key of C --
        // in ChordSourceTests.cpp rather than trusted on the algebra alone.
        inline int mirrorPc(int pc, int rootPc)
        {
            return ((7 + rootPc * 2 - pc) % 12 + 12) % 12;
        }

        // Which chordgen type (and which of its own notes is the root) a pitch-class set
        // matches, by trying each note as the root and comparing interval sets. Negative
        // harmony's mirror doesn't preserve chord *type* the way P/L/R do -- a 7th chord's
        // mirror is not reliably another named 7th chord -- so unlike triadToChord this has
        // to search rather than compute the answer directly. Falls back to the lowest
        // pitch class as root and a plain Major label when nothing matches exactly, which
        // only bites on chords wider than a triad; every triad this file mirrors (the whole
        // point of the two examples above) always finds an exact match.
        inline void identifyType(const std::vector<int>& pcs, int& outRootPc, int& outType)
        {
            const auto& allTypes = chordgen::types();
            for (int root : pcs)
            {
                std::set<int> want;
                for (int pc : pcs)
                    want.insert(((pc - root) % 12 + 12) % 12);
                for (int ti = 0; ti < (int) allTypes.size(); ++ti)
                {
                    std::set<int> have;
                    for (int iv : allTypes[(size_t) ti].ivals)
                        have.insert(((iv % 12) + 12) % 12);
                    if (have == want)
                    {
                        outRootPc = root;
                        outType = ti;
                        return;
                    }
                }
            }
            outRootPc = pcs.empty() ? 0 : pcs.front();
            outType = chordgen::typeIndex("Major");
        }

        // ---- Progressions -----------------------------------------------------------

        struct ProgressionStep
        {
            int semitonesFromTonic;
            int type; // a chordgen::types() index
        };
    } // namespace detail

    // ---- 1. Circle of fifths --------------------------------------------------------
    //
    // Walk the circle from the tonic, landing on each pitch class's own diatonic quality
    // where the landing is actually in the key, and major otherwise (a circle walk visits
    // pitch classes the key doesn't contain as often as it doesn't, especially flat-ward,
    // and there's no diatonic quality to borrow for those). `direction` +1 climbs by
    // fifths (sharp-ward: C -> G -> D -> ...); -1 climbs by fourths, which is the same
    // walk read backwards (flat-ward: C -> F -> Bb -> ...).
    inline std::vector<chordgen::Chord> circleOfFifths(int rootPc, int mode, int octave, int count,
                                                        int direction, juce::Random& rng)
    {
        const auto& modeObj = modes::get(mode);
        const int step = direction >= 0 ? 7 : 5; // up a fifth, or up a fourth (= down a fifth mod 12)

        std::vector<chordgen::Chord> out;
        out.reserve((size_t) count);
        int pc = ((rootPc % 12) + 12) % 12;

        for (int i = 0; i < count; ++i)
        {
            const int degree = detail::degreeOf(rootPc, modeObj, pc);
            const int type = degree >= 0 ? chordgen::baseType(modeObj.qualities[(size_t) degree])
                                          : chordgen::typeIndex("Major");
            chordgen::Chord c;
            c.rootPc = pc;
            c.type = type;
            c.degree = degree;
            c.notes = chordgen::chordNotes(pc, type, octave);
            out.push_back(c);

            // A mechanical one-lap walk is 16 chords that read as a scale run, not a
            // progression. Occasionally doubling the step or reversing it keeps the
            // landing points from being fully predictable without abandoning the circle.
            int thisStep = step;
            const float r = rng.nextFloat();
            if (r < 0.15f)
                thisStep *= 2;
            else if (r < 0.25f)
                thisStep = -thisStep;
            pc = ((pc + thisStep) % 12 + 12) % 12;
        }
        return out;
    }

    // ---- 2. Neo-Riemannian PLR --------------------------------------------------------
    //
    // Starts from the tonic triad and takes `count - 1` PLR steps, each one chosen by the
    // three weights (0..100, all-zero treated as equal). See detail::applyP/L/R above for
    // exactly which voice moves in each transform -- that's the part worth getting right
    // and the part ChordSourceTests.cpp checks against the textbook definitions.
    inline std::vector<chordgen::Chord> neoRiemannian(int rootPc, int mode, int octave, int count, int weightP,
                                                       int weightL, int weightR, juce::Random& rng)
    {
        const auto& modeObj = modes::get(mode);

        // PLR is only defined on major/minor triads. Every mode in this table starts on a
        // major or minor tonic except Locrian (diminished): there's no strict "major or
        // minor" triad to seed from there, so it takes the minor side -- a diminished
        // triad is a minor triad with the fifth flatted, one semitone away, the closer of
        // the two by shared interval content.
        const bool major = modeObj.qualities[0] == modes::Quality::major
                         || modeObj.qualities[0] == modes::Quality::augmented;

        const int base = octave * 12 + ((rootPc % 12) + 12) % 12;
        detail::Triad t { base, major ? base + 4 : base + 3, base + 7, major };

        int wp = juce::jmax(0, weightP), wl = juce::jmax(0, weightL), wr = juce::jmax(0, weightR);
        if (wp + wl + wr <= 0)
            wp = wl = wr = 1;

        std::vector<chordgen::Chord> out;
        out.reserve((size_t) count);
        if (count > 0)
            out.push_back(detail::triadToChord(t));

        for (int i = 1; i < count; ++i)
        {
            const int total = wp + wl + wr;
            const int r = rng.nextInt(total);
            if (r < wp)
                t = detail::applyP(t);
            else if (r < wp + wl)
                t = detail::applyL(t);
            else
                t = detail::applyR(t);
            detail::keepTriadInRange(t);
            out.push_back(detail::triadToChord(t));
        }
        return out;
    }

    // ---- 3. Named progressions ---------------------------------------------------------

    struct Progression
    {
        const char* name;
        std::vector<detail::ProgressionStep> steps;
    };

    namespace detail
    {
        // Every step is a semitone offset from the tonic plus an explicit chordgen type,
        // rather than a scale degree resolved through `mode`: these are named progressions
        // with their own fixed characters (a ii-V-I's ii is minor 7th and its V is
        // dominant, full stop), not a re-derivation of whatever `mode` happens to hold on
        // that degree. `mode` still matters for the returned Chord::degree, which is
        // resolved separately below against whatever mode the caller passed, so the UI can
        // still show "that's degree iv in this key" when it happens to line up.
        inline const std::vector<Progression>& progressionLibrary()
        {
            const auto idx = [](const char* n) { return chordgen::typeIndex(n); };
            static const std::vector<Progression> lib = {
                { "ii-V-I", {
                    { 2, idx("Minor 7th") }, { 7, idx("Dominant 7th") }, { 0, idx("Major 7th") },
                } },
                { "I-V-vi-IV (Axis)", {
                    { 0, idx("Major") }, { 7, idx("Major") }, { 9, idx("Minor") }, { 5, idx("Major") },
                } },
                // Quick-change-less 12-bar blues: I I I I / IV IV I I / V IV I I. Real blues
                // sets add a quick change in bar 2 and a turnaround in bar 12; this is the
                // form every player learns first, not the only form there is.
                { "12-Bar Blues", {
                    { 0, idx("Dominant 7th") }, { 0, idx("Dominant 7th") }, { 0, idx("Dominant 7th") }, { 0, idx("Dominant 7th") },
                    { 5, idx("Dominant 7th") }, { 5, idx("Dominant 7th") }, { 0, idx("Dominant 7th") }, { 0, idx("Dominant 7th") },
                    { 7, idx("Dominant 7th") }, { 5, idx("Dominant 7th") }, { 0, idx("Dominant 7th") }, { 0, idx("Dominant 7th") },
                } },
                // i-VII-VI-V: natural minor for i/VII/VI, then a major V borrowed from
                // harmonic minor for the cadential pull down to the tonic.
                { "Andalusian Cadence (i-VII-VI-V)", {
                    { 0, idx("Minor") }, { 10, idx("Major") }, { 8, idx("Major") }, { 7, idx("Major") },
                } },
                { "Royal Road (IV-V-iii-vi)", {
                    { 5, idx("Major") }, { 7, idx("Major") }, { 4, idx("Minor") }, { 9, idx("Minor") },
                } },
                { "I-vi-ii-V (Rhythm changes A)", {
                    { 0, idx("Major 7th") }, { 9, idx("Minor 7th") }, { 2, idx("Minor 7th") }, { 7, idx("Dominant 7th") },
                } },
                // The full Giant Steps machinery (secondary dominants chasing each key
                // center) is a lot of special-cased motion for one preset; what actually
                // defines "Coltrane changes" for a picker like this one is the major-third
                // root cycle itself, so that's what's here: three major 7ths a major third
                // apart, which is also exactly one full trip round the octave (3 x 4 = 12).
                { "Coltrane Changes", {
                    { 0, idx("Major 7th") }, { 4, idx("Major 7th") }, { 8, idx("Major 7th") },
                } },
            };
            return lib;
        }
    } // namespace detail

    // Display names for a picker, "Random" first so index 0 always means "let the seed
    // decide" without the caller needing to know how many templates exist. Note the
    // off-by-one against `progressions()`'s own templateIndex: that parameter indexes
    // progressionLibrary() directly (no Random entry in it), so a UI wiring this list to a
    // combo box passes `comboIndex - 1` through, and comboIndex 0 becomes templateIndex -1.
    inline std::vector<juce::String> progressionNames()
    {
        std::vector<juce::String> names;
        names.push_back("Random");
        for (const auto& p : detail::progressionLibrary())
            names.push_back(p.name);
        return names;
    }

    // `templateIndex < 0` picks one of the library's progressions at random. Whatever
    // template is chosen loops to fill `count` -- a 3-chord ii-V-I asked for 16 chords
    // just repeats four times and change.
    inline std::vector<chordgen::Chord> progressions(int rootPc, int mode, int octave, int count,
                                                      int templateIndex, juce::Random& rng)
    {
        const auto& lib = detail::progressionLibrary();
        int idx = templateIndex;
        if (idx < 0 || idx >= (int) lib.size())
            idx = rng.nextInt((int) lib.size());
        const auto& prog = lib[(size_t) idx];
        const auto& modeObj = modes::get(mode);

        std::vector<chordgen::Chord> out;
        out.reserve((size_t) count);
        for (int i = 0; i < count; ++i)
        {
            const auto& step = prog.steps[(size_t) (i % (int) prog.steps.size())];
            const int cr = ((rootPc + step.semitonesFromTonic) % 12 + 12) % 12;
            chordgen::Chord c;
            c.rootPc = cr;
            c.type = step.type;
            c.notes = chordgen::chordNotes(cr, step.type, octave);
            c.degree = detail::degreeOf(rootPc, modeObj, cr);
            out.push_back(c);
        }
        return out;
    }

    // ---- 4. Negative harmony -----------------------------------------------------------

    // Mirror an already-built progression about the tonic/dominant axis. Each note is
    // reflected as a pitch class, then placed in whichever octave of that pitch class is
    // closest to the note it came from -- a straight pitch-class mirror with no register
    // awareness would routinely land the reflected chord an octave or more away from the
    // original, which reads as a register jump rather than harmonic reflection.
    inline std::vector<chordgen::Chord> negativeHarmony(const std::vector<chordgen::Chord>& input, int rootPc)
    {
        std::vector<chordgen::Chord> out;
        out.reserve(input.size());

        for (const auto& in : input)
        {
            std::vector<int> pcs;
            std::vector<int> mirrored;
            pcs.reserve(in.notes.size());
            mirrored.reserve(in.notes.size());

            for (int n : in.notes)
            {
                const int pc = ((n % 12) + 12) % 12;
                const int mpc = detail::mirrorPc(pc, rootPc);
                pcs.push_back(mpc);

                const int sameOctave = (n / 12) * 12 + mpc;
                int best = sameOctave;
                for (int alt : { sameOctave - 12, sameOctave + 12 })
                    if (std::abs(alt - n) < std::abs(best - n))
                        best = alt;
                mirrored.push_back(juce::jlimit(0, 127, best));
            }

            std::sort(mirrored.begin(), mirrored.end());
            chordgen::Chord c;
            c.notes = mirrored;
            detail::identifyType(pcs, c.rootPc, c.type);
            c.degree = -1; // the mirror routinely lands outside the source scale/degree frame
            out.push_back(c);
        }
        return out;
    }

    // Build a plain diatonic progression with ChordGen.h's own weighted-pool generator
    // (scale compliance pinned to 1.0, no lock bias -- the point here is a clean, in-key
    // source to reflect, not a second scale-compliance knob) and mirror it.
    inline std::vector<chordgen::Chord> negativeHarmony(int rootPc, int mode, int octave, int count,
                                                         juce::Random& rng)
    {
        chordgen::Options opts;
        opts.octave = octave;
        opts.scaleCompliance = 1.0f;
        const auto diatonic = chordgen::generate(rootPc, mode, count, opts, {}, rng);
        return negativeHarmony(diatonic, rootPc);
    }

    // ---- 5. Planing (constant structure) -----------------------------------------------
    //
    // One chord shape, chosen once, slides up or down. `diatonic` picks between the two
    // things "planing" means in practice: sliding the shape's *size* through the scale
    // degrees so the quality bends to fit the key (diatonic planing, common in pop/gospel
    // parallel harmony), or preserving the exact interval stack and sliding it
    // chromatically regardless of key (Debussy-style planing, where the constant structure
    // is the entire point and bending it to the scale would defeat it).
    //
    // The vocabulary grew on 2026-11-02 from seven shapes to thirteen: Major 9th, Minor 9th, Add9,
    // Minor 11th, Quartal and Quintal at the end (docs/IMPRESSIONIST_HARMONY.md part 3: planing
    // 9ths and fourths is most of what Debussy and McCoy Tyner do). The first seven behave exactly
    // as before. The six new ones are the "wide" shapes the comment below warns about, which is
    // true of them planed chromatically and is why diatonic planing builds them differently:
    // **by stacking scale steps from each degree** (thirds are every 2nd degree, fourths every
    // 3rd, fifths every 4th) rather than by quality-matched interval tables. A ninth chord on
    // every degree of Ionian by interval table has an F# in it on iii; stacked through the scale
    // it never leaves the key, and in a pentatonic scale it is still a stack of that scale's own
    // notes. Chromatic planing keeps the exact interval stack, which is the point of it.
    inline const std::vector<const char*>& planingShapes()
    {
        static const std::vector<const char*> s = { "Major", "Minor", "Sus2", "Sus4", "Major 7th", "Minor 7th",
                                                    "Dominant 7th",
                                                    "Major 9th", "Minor 9th", "Add9", "Minor 11th",
                                                    "Quartal", "Quintal" };
        return s;
    }

    namespace detail
    {
        // Scale steps above the root that a shape stacks in diatonic planing, or empty for the
        // original seven, which keep their quality-matched types.
        inline std::vector<int> planingSteps(const juce::String& shape)
        {
            if (shape == "Major 9th" || shape == "Minor 9th") return { 0, 2, 4, 6, 8 };
            if (shape == "Add9")                              return { 0, 2, 4, 8 };
            if (shape == "Minor 11th")                        return { 0, 2, 4, 6, 8, 10 };
            if (shape == "Quartal")                           return { 0, 3, 6, 9 };
            if (shape == "Quintal")                           return { 0, 4, 8 };
            return {};
        }
    }

    // `forceShape` indexes planingShapes(); -1 (the default) picks one at random, which is what
    // the generator does. It exists so a test can sweep every shape instead of hoping the rng
    // lands on each.
    inline std::vector<chordgen::Chord> planing(int rootPc, int mode, int octave, int count, bool diatonic,
                                                juce::Random& rng, int forceShape = -1)
    {
        const auto& modeObj = modes::get(mode);
        const int n = (int) modeObj.intervals.size();

        // A small, playable vocabulary rather than the whole chordgen::types() table: a
        // wide 9th-chord planed chromatically across 16 steps turns to mush fast, and
        // planing's character comes from the shape being recognisable every time it lands.
        const auto& shapeNames = planingShapes();
        const int shapeIdx = forceShape >= 0 ? juce::jmin(forceShape, (int) shapeNames.size() - 1)
                                             : rng.nextInt((int) shapeNames.size());
        const int shapeType = chordgen::typeIndex(shapeNames[(size_t) shapeIdx]);
        const size_t shapeSize = chordgen::types()[(size_t) shapeType].ivals.size();
        const auto steps = detail::planingSteps(shapeNames[(size_t) shapeIdx]);

        // The pitch `k` scale steps above the anchor, k any integer: the scale repeated every
        // octave, so a stack that runs past the top of the scale carries on up it.
        const auto scaleAt = [&] (int k)
        {
            const int deg = ((k % n) + n) % n;
            return modeObj.intervals[(size_t) deg] + ((k - deg) / n) * 12;
        };

        std::vector<chordgen::Chord> out;
        out.reserve((size_t) count);

        const int anchor = octave * 12 + ((rootPc % 12) + 12) % 12;
        int pos = 0; // signed step count from the start; read mod n as a degree (diatonic)
                     // or directly as a semitone offset (chromatic)
        int dir = rng.nextBool() ? 1 : -1;

        for (int i = 0; i < count; ++i)
        {
            chordgen::Chord c;
            if (diatonic)
            {
                const int degree = ((pos % n) + n) % n;
                const int octaveShift = (pos - degree) / n; // exact: pos - degree is a multiple of n
                const auto quality = modeObj.qualities[(size_t) degree];
                const int absRoot = anchor + modeObj.intervals[(size_t) degree] + octaveShift * 12;
                c.rootPc = ((anchor + modeObj.intervals[(size_t) degree]) % 12 + 12) % 12;
                c.degree = degree;
                if (steps.empty())
                {
                    c.type = detail::typeForDegreeSize(quality, shapeSize);
                    for (int iv : chordgen::types()[(size_t) c.type].ivals)
                        c.notes.push_back(absRoot + iv);
                }
                else
                {
                    for (int st : steps)
                        c.notes.push_back(anchor + scaleAt(pos + st));
                    c.type = detail::typeForStack(c.notes, quality, shapeType);
                }
            }
            else
            {
                const int absRoot = anchor + pos;
                c.rootPc = ((absRoot % 12) + 12) % 12;
                c.type = shapeType;
                c.degree = -1;
                for (int iv : chordgen::types()[(size_t) shapeType].ivals)
                    c.notes.push_back(absRoot + iv);
            }
            out.push_back(c);

            // Steer away from the edges of the keyboard before a step would run off it
            // (margins wide enough for the widest shape in the vocabulary above: 17 semitones
            // for a Minor 11th, up to 24 for the same stack in a pentatonic scale, where a
            // single step of drift in the top note is under 7), otherwise the occasional
            // reversal that keeps a long run from reading as one scale sliding off the top.
            if (! c.notes.empty() && c.notes.back() >= 116)
                dir = -1;
            else if (! c.notes.empty() && c.notes.front() <= 11)
                dir = 1;
            else if (rng.nextFloat() < 0.2f)
                dir = -dir;
            pos += dir;
        }
        return out;
    }

    // ---- 6. Voice-leading post-pass ----------------------------------------------------
    //
    // Runs after any of the five sources above, or after ChordGen.h's generate() /
    // ChordMarkov.h's generate() -- both hand back chords in their own default register
    // with no awareness of what came before them, which is exactly the gap this closes.
    //
    // For each chord after the first, each of its pitch classes is independently placed in
    // whichever octave sits closest to *any* note of the previous chord (a per-note nearest
    // search, not a full assignment-problem solve -- good enough for triads and 7ths, and
    // simple enough to reason about when it picks wrong). `amount` then blends the octave
    // *count* between the original placement and that smoothest one, not the raw semitone
    // distance: two notes sharing a pitch class are always a whole number of octaves apart,
    // so interpolating in octaves and multiplying back by 12 guarantees every intermediate
    // amount still lands on the original pitch class, which a raw semitone blend would not
    // (halfway between two notes an octave apart is a tritone away from both).
    inline void applyVoiceLeading(std::vector<chordgen::Chord>& chords, float amount)
    {
        amount = juce::jlimit(0.0f, 1.0f, amount);
        if (amount <= 0.0f)
            return;

        for (size_t i = 1; i < chords.size(); ++i)
        {
            auto& c = chords[i];
            const auto& prev = chords[i - 1].notes;
            if (c.notes.empty())
                continue;

            // Distinct pitch classes only: a chord type in this file never doubles a
            // class (chordgen::types() doesn't either), but placing a doubled note twice,
            // independently, could otherwise send its two copies to two different octaves.
            std::vector<int> pcs;
            for (int nte : c.notes)
            {
                const int pc = ((nte % 12) + 12) % 12;
                if (std::find(pcs.begin(), pcs.end(), pc) == pcs.end())
                    pcs.push_back(pc);
            }

            std::vector<int> smooth;
            smooth.reserve(pcs.size());
            for (int pc : pcs)
            {
                int best = pc; // octave 0 fallback if prev is somehow empty
                int bestCost = -1;
                for (int o = 0; o <= 10; ++o)
                {
                    const int candidate = o * 12 + pc;
                    if (candidate > 127)
                        break;
                    int cost = 0;
                    if (! prev.empty())
                    {
                        cost = std::abs(candidate - prev.front());
                        for (int pn : prev)
                            cost = juce::jmin(cost, std::abs(candidate - pn));
                    }
                    if (bestCost < 0 || cost < bestCost)
                    {
                        bestCost = cost;
                        best = candidate;
                    }
                }
                smooth.push_back(best);
            }

            for (auto& nte : c.notes)
            {
                const int pc = ((nte % 12) + 12) % 12;
                int sm = nte;
                for (size_t k = 0; k < pcs.size(); ++k)
                    if (pcs[(size_t) k] == pc)
                    {
                        sm = smooth[k];
                        break;
                    }
                // sm and nte carry the same pitch class by construction, so their
                // difference is always an exact multiple of 12 -- blending the octave
                // count (not the raw semitone gap) is what keeps every amount in between
                // 0 and 1 on that same pitch class rather than drifting off it.
                const int octaveDiff = (sm - nte) / 12;
                const int blended = (int) std::lround((double) amount * octaveDiff);
                nte = juce::jlimit(0, 127, nte + blended * 12);
            }
            std::sort(c.notes.begin(), c.notes.end());
        }
    }
} // namespace keys::sources
