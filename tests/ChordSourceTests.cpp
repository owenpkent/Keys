// Unit tests for keys::sources (ChordSources.h). JUCE UnitTest framework, self-registering
// statics only -- tests/KeysTests.cpp owns the runner's main(), same as MarkovTests.cpp.
#include "ChordGen.h"
#include "Chords.h"
#include "ChordSources.h"
#include "ScaleModes.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <set>
#include <vector>

namespace
{
    bool ascendingStrict(const std::vector<int>& v)
    {
        for (size_t i = 1; i < v.size(); ++i)
            if (v[i] <= v[i - 1])
                return false;
        return true;
    }

    bool allInRange(const std::vector<int>& v)
    {
        for (int n : v)
            if (n < 0 || n > 127)
                return false;
        return true;
    }

    std::vector<int> pcsOf(const std::vector<int>& notes)
    {
        std::vector<int> pcs;
        for (int n : notes)
            pcs.push_back(((n % 12) + 12) % 12);
        std::sort(pcs.begin(), pcs.end());
        return pcs;
    }
} // namespace

namespace
{
struct ChordSourceShapeTests : juce::UnitTest
{
    ChordSourceShapeTests() : juce::UnitTest("keys::sources generator shapes") {}

    void checkChords(const std::vector<keys::chordgen::Chord>& chords, int expectedCount, const juce::String& label)
    {
        expectEquals((int) chords.size(), expectedCount, label + " count");
        for (const auto& c : chords)
        {
            expect((int) c.notes.size() >= 3, label + " has at least 3 notes");
            expect(allInRange(c.notes), label + " notes in MIDI range");
            expect(ascendingStrict(c.notes), label + " notes strictly ascending");
        }
    }

    void runTest() override
    {
        using namespace keys;

        for (int count : { 1, 4, 16 })
        {
            juce::Random rng(1000 + count);

            beginTest("circleOfFifths returns count=" + juce::String(count));
            checkChords(sources::circleOfFifths(0, 0, 4, count, 1, rng), count, "circleOfFifths");

            beginTest("circleOfFifths flat-ward direction also returns count=" + juce::String(count));
            checkChords(sources::circleOfFifths(0, 0, 4, count, -1, rng), count, "circleOfFifths (flat-ward)");

            beginTest("neoRiemannian returns count=" + juce::String(count));
            checkChords(sources::neoRiemannian(0, 0, 4, count, 33, 33, 34, rng), count, "neoRiemannian");

            beginTest("progressions returns count=" + juce::String(count));
            checkChords(sources::progressions(0, 0, 4, count, -1, rng), count, "progressions");

            beginTest("negativeHarmony (generator overload) returns count=" + juce::String(count));
            checkChords(sources::negativeHarmony(0, 0, 4, count, rng), count, "negativeHarmony");

            beginTest("planing diatonic returns count=" + juce::String(count));
            checkChords(sources::planing(0, 0, 4, count, true, rng), count, "planing(diatonic)");

            beginTest("planing chromatic returns count=" + juce::String(count));
            checkChords(sources::planing(0, 0, 4, count, false, rng), count, "planing(chromatic)");
        }
    }
};
static ChordSourceShapeTests chordSourceShapeTests;

struct PLRCorrectnessTests : juce::UnitTest
{
    PLRCorrectnessTests() : juce::UnitTest("keys::sources PLR transforms") {}

    static std::set<int> triadPcs(const keys::sources::detail::Triad& t)
    {
        return { ((t.root % 12) + 12) % 12, ((t.third % 12) + 12) % 12, ((t.fifth % 12) + 12) % 12 };
    }

    void runTest() override
    {
        using namespace keys::sources::detail;

        // C major triad (C4 E4 G4) and A minor triad (A3 C4 E4) -- the two textbook
        // starting points the port spec's examples are all written against.
        const Triad cMajor { 60, 64, 67, true };
        const Triad aMinor { 57, 60, 64, false };

        beginTest("P: C major -> C minor -> C major, root never moves");
        {
            const auto cMinor = applyP(cMajor);
            expect(triadPcs(cMinor) == std::set<int> { 0, 3, 7 }, "C major -P-> C minor");
            expect(! cMinor.major);
            expectEquals(((cMinor.root % 12) + 12) % 12, 0);

            const auto backToMajor = applyP(cMinor);
            expect(triadPcs(backToMajor) == std::set<int> { 0, 4, 7 }, "C minor -P-> C major");
            expect(backToMajor.major);
        }

        beginTest("P: A minor -> A major, root never moves");
        {
            const auto aMajor = applyP(aMinor);
            expect(triadPcs(aMajor) == std::set<int> { 9, 1, 4 }, "A minor -P-> A major (A C# E)");
            expect(aMajor.major);
        }

        beginTest("L: C major -> E minor (C E G -> B E G)");
        {
            const auto eMinor = applyL(cMajor);
            expect(triadPcs(eMinor) == std::set<int> { 4, 7, 11 }, "E minor pitch classes");
            expect(! eMinor.major);
            expectEquals(((eMinor.root % 12) + 12) % 12, 4, "the old third becomes the new root");
        }

        beginTest("L: A minor -> F major (A C E -> A C F)");
        {
            const auto fMajor = applyL(aMinor);
            expect(triadPcs(fMajor) == std::set<int> { 5, 9, 0 }, "F major pitch classes");
            expect(fMajor.major);
            expectEquals(((fMajor.root % 12) + 12) % 12, 5, "the old fifth, raised a semitone, becomes the new root");
        }

        beginTest("R: C major -> A minor (C E G -> C E A)");
        {
            const auto aMinorResult = applyR(cMajor);
            expect(triadPcs(aMinorResult) == std::set<int> { 0, 4, 9 }, "A minor pitch classes");
            expect(! aMinorResult.major);
            expectEquals(((aMinorResult.root % 12) + 12) % 12, 9, "the old fifth, raised a tone, becomes the new root");
        }

        beginTest("R: A minor -> C major (A C E -> G C E, i.e. C major's own notes)");
        {
            const auto cMajorResult = applyR(aMinor);
            expect(triadPcs(cMajorResult) == std::set<int> { 0, 4, 7 }, "C major pitch classes");
            expect(cMajorResult.major);
            expectEquals(((cMajorResult.root % 12) + 12) % 12, 0, "the old third becomes the new root");
        }

        beginTest("triadToChord reports the same pitch classes and a sorted, in-range note list");
        {
            const auto eMinor = applyL(cMajor);
            const auto c = triadToChord(eMinor);
            expect(ascendingStrict(c.notes));
            expect(allInRange(c.notes));
            expectEquals(c.rootPc, 4);
            expectEquals(c.type, keys::chordgen::typeIndex("Minor"));
        }
    }
};
static PLRCorrectnessTests plrCorrectnessTests;

struct NegativeHarmonyTests : juce::UnitTest
{
    NegativeHarmonyTests() : juce::UnitTest("keys::sources::negativeHarmony") {}

    void runTest() override
    {
        using namespace keys;

        beginTest("mirrorPc matches the two known cases directly, in the key of C");
        {
            expectEquals(sources::detail::mirrorPc(0, 0), 7);  // C -> G
            expectEquals(sources::detail::mirrorPc(4, 0), 3);  // E -> Eb
            expectEquals(sources::detail::mirrorPc(7, 0), 0);  // G -> C
        }

        beginTest("C major mirrors to C minor in the key of C");
        {
            chordgen::Chord cMajor;
            cMajor.rootPc = 0;
            cMajor.type = chordgen::typeIndex("Major");
            cMajor.notes = chordgen::chordNotes(0, cMajor.type, 4);

            const auto mirrored = sources::negativeHarmony(std::vector<chordgen::Chord> { cMajor }, 0);
            expectEquals((int) mirrored.size(), 1);
            expectEquals(mirrored[0].rootPc, 0);
            expectEquals(mirrored[0].type, chordgen::typeIndex("Minor"));
        }

        beginTest("G major mirrors to F minor in the key of C");
        {
            chordgen::Chord gMajor;
            gMajor.rootPc = 7;
            gMajor.type = chordgen::typeIndex("Major");
            gMajor.notes = chordgen::chordNotes(7, gMajor.type, 4);

            const auto mirrored = sources::negativeHarmony(std::vector<chordgen::Chord> { gMajor }, 0);
            expectEquals((int) mirrored.size(), 1);
            expectEquals(mirrored[0].rootPc, 5);
            expectEquals(mirrored[0].type, chordgen::typeIndex("Minor"));
        }

        beginTest("the mirrored chord lands near the original register, not an octave away");
        {
            chordgen::Chord cMajor;
            cMajor.rootPc = 0;
            cMajor.type = chordgen::typeIndex("Major");
            cMajor.notes = chordgen::chordNotes(0, cMajor.type, 5); // C5 E5 G5

            const auto mirrored = sources::negativeHarmony(std::vector<chordgen::Chord> { cMajor }, 0);
            for (size_t i = 0; i < cMajor.notes.size(); ++i)
                expect(std::abs(mirrored[0].notes[i] - cMajor.notes[i]) <= 7,
                       "mirrored note strayed more than a fifth from the note it reflects");
        }
    }
};
static NegativeHarmonyTests negativeHarmonyTests;

struct VoiceLeadingTests : juce::UnitTest
{
    VoiceLeadingTests() : juce::UnitTest("keys::sources::applyVoiceLeading") {}

    // Nearest-neighbour total: for every note in chord i, the distance to the closest note
    // in chord i-1. This is exactly the metric applyVoiceLeading's per-pitch-class search
    // minimises, so it's the honest way to check "amount=1 does at least as well as doing
    // nothing" rather than grading it against some other metric it was never asked to
    // optimise.
    static int totalMovement(const std::vector<keys::chordgen::Chord>& chords)
    {
        int total = 0;
        for (size_t i = 1; i < chords.size(); ++i)
        {
            const auto& prev = chords[i - 1].notes;
            if (prev.empty())
                continue;
            for (int n : chords[i].notes)
            {
                int best = std::abs(n - prev.front());
                for (int p : prev)
                    best = juce::jmin(best, std::abs(n - p));
                total += best;
            }
        }
        return total;
    }

    void runTest() override
    {
        using namespace keys;

        juce::Random rng(2222);
        const auto chords = sources::progressions(0, 0, 4, 8, -1, rng);

        std::vector<std::vector<int>> pcsBefore;
        for (const auto& c : chords)
            pcsBefore.push_back(pcsOf(c.notes));
        const int movementBefore = totalMovement(chords);

        auto smoothed = chords;
        sources::applyVoiceLeading(smoothed, 1.0f);

        beginTest("pitch classes are unchanged after voice leading");
        {
            for (size_t i = 0; i < chords.size(); ++i)
                expect(pcsOf(smoothed[i].notes) == pcsBefore[i],
                       "chord " + juce::String((int) i) + " pitch classes changed");
        }

        beginTest("amount=1 never increases total nearest-neighbour movement");
        {
            const int movementAfter = totalMovement(smoothed);
            expect(movementAfter <= movementBefore,
                   "movement went from " + juce::String(movementBefore) + " to " + juce::String(movementAfter));
        }

        beginTest("amount=0 leaves the chords untouched");
        {
            auto untouched = chords;
            sources::applyVoiceLeading(untouched, 0.0f);
            for (size_t i = 0; i < chords.size(); ++i)
                expect(untouched[i].notes == chords[i].notes, "amount=0 should be a no-op");
        }

        beginTest("every note stays within MIDI 0..127 at every intermediate amount");
        {
            for (float amount : { 0.25f, 0.5f, 0.75f, 1.0f })
            {
                auto partial = chords;
                sources::applyVoiceLeading(partial, amount);
                for (const auto& c : partial)
                {
                    expect(allInRange(c.notes), "amount=" + juce::String(amount));
                    expect(ascendingStrict(c.notes), "amount=" + juce::String(amount) + " notes should stay sorted");
                }
            }
        }

        beginTest("blending at 0.5 still lands on the original pitch classes");
        {
            auto half = chords;
            sources::applyVoiceLeading(half, 0.5f);
            for (size_t i = 0; i < chords.size(); ++i)
                expect(pcsOf(half[i].notes) == pcsBefore[i],
                       "chord " + juce::String((int) i) + " pitch classes changed at amount=0.5");
        }
    }
};
static VoiceLeadingTests voiceLeadingTests;

struct PlaningTests : juce::UnitTest
{
    PlaningTests() : juce::UnitTest("keys::sources::planing") {}

    void runTest() override
    {
        using namespace keys;

        beginTest("chromatic planing preserves the interval stack exactly across all chords");
        {
            juce::Random rng(4242);
            const auto chords = sources::planing(0, 0, 4, 16, false, rng);
            expectEquals((int) chords.size(), 16);

            std::vector<int> firstIntervals;
            for (int n : chords.front().notes)
                firstIntervals.push_back(n - chords.front().notes.front());

            for (const auto& c : chords)
            {
                std::vector<int> ivals;
                for (int n : c.notes)
                    ivals.push_back(n - c.notes.front());
                expect(ivals == firstIntervals, "interval stack drifted mid-walk");
            }
        }

        beginTest("diatonic planing keeps the same note count on every chord");
        {
            juce::Random rng(4243);
            const auto chords = sources::planing(0, 0, 4, 16, true, rng);
            const size_t n0 = chords.front().notes.size();
            for (const auto& c : chords)
                expectEquals((int) c.notes.size(), (int) n0, "diatonic planing note count should stay constant");
        }
    }
};
static PlaningTests planingTests;

// The wide shapes added 2026-11-02, and the six chord types behind them.
struct WidePlaningTests : juce::UnitTest
{
    WidePlaningTests() : juce::UnitTest("keys::sources::planing wide shapes") {}

    void runTest() override
    {
        using namespace keys;
        const auto& shapes = sources::planingShapes();
        const int firstNew = 7; // the original seven keep their quality-matched types

        beginTest("the planing vocabulary only grows at the end");
        {
            const char* golden[] = { "Major", "Minor", "Sus2", "Sus4", "Major 7th", "Minor 7th", "Dominant 7th",
                                     "Major 9th", "Minor 9th", "Add9", "Minor 11th", "Quartal", "Quintal" };
            expectEquals((int) shapes.size(), (int) (sizeof(golden) / sizeof(golden[0])));
            for (size_t i = 0; i < shapes.size(); ++i)
                expectEquals(juce::String(shapes[i]), juce::String(golden[i]));
        }

        beginTest("diatonic planing with a new shape never leaves the scale, every mode and root");
        {
            for (int shape = firstNew; shape < (int) shapes.size(); ++shape)
                for (int mode = 0; mode < modes::count(); ++mode)
                    for (int root = 0; root < 12; ++root)
                    {
                        juce::Random rng(1000 + shape * 100 + mode);
                        const auto& m = modes::get(mode);
                        std::set<int> inKey;
                        for (int iv : m.intervals)
                            inKey.insert((root + iv) % 12);
                        const auto chords = sources::planing(root, mode, 4, 24, true, rng, shape);
                        expectEquals((int) chords.size(), 24);
                        const size_t want = chords.front().notes.size();
                        for (const auto& c : chords)
                        {
                            const auto ctx = juce::String(shapes[(size_t) shape]) + " mode " + juce::String(mode)
                                             + " root " + juce::String(root);
                            expectEquals((int) c.notes.size(), (int) want, "note count drifted, " + ctx);
                            expect(ascendingStrict(c.notes) && allInRange(c.notes), "bad register, " + ctx);
                            for (int note : c.notes)
                                expect(inKey.count(((note % 12) + 12) % 12) == 1, "out-of-key note, " + ctx);
                        }
                    }
        }

        beginTest("chromatic planing of every shape preserves the shape's exact intervals");
        {
            for (int shape = 0; shape < (int) shapes.size(); ++shape)
            {
                juce::Random rng(2000 + shape);
                const auto want = chordgen::types()[(size_t) chordgen::typeIndex(shapes[(size_t) shape])].ivals;
                for (const auto& c : sources::planing(0, 0, 4, 24, false, rng, shape))
                {
                    std::vector<int> ivals;
                    for (int note : c.notes)
                        ivals.push_back(note - c.notes.front());
                    expect(ivals == want, juce::String("chromatic ") + shapes[(size_t) shape] + " drifted");
                    expect(allInRange(c.notes), "chromatic planing ran off the keyboard");
                }
            }
        }

        beginTest("diatonic quartal is scale-step fourths and quintal scale-step fifths");
        {
            // Every step the stack climbs is 3 degrees (a fourth) or 4 (a fifth) of the mode.
            for (int mode = 0; mode < modes::count(); ++mode)
                for (int which = 0; which < 2; ++which)
                {
                    const int shape = which == 0 ? 11 : 12;
                    const int stride = which == 0 ? 3 : 4;
                    const auto& m = modes::get(mode);
                    const int n = (int) m.intervals.size();
                    juce::Random rng(3000 + mode);
                    for (const auto& c : sources::planing(0, mode, 4, 24, true, rng, shape))
                    {
                        expectEquals((int) c.notes.size(), which == 0 ? 4 : 3);
                        for (size_t k = 1; k < c.notes.size(); ++k)
                        {
                            const int lo = sources::detail::degreeOf(0, m, c.notes[k - 1]);
                            const int hi = sources::detail::degreeOf(0, m, c.notes[k]);
                            expect(lo >= 0 && hi >= 0 && (hi - lo + n) % n == stride % n,
                                   "not a scale-step stack in mode " + juce::String(mode));
                        }
                    }
                }
            // In plain C major that gives the textbook quartal chord on I: C F B E, not C F Bb Eb.
            juce::Random rng(1);
            const auto q = sources::planing(0, 0, 4, 1, true, rng, 11);
            expectEquals(q.front().degree, 0);
            expect(q.front().notes == std::vector<int>({ 48, 53, 59, 64 }), "I quartal in C major");
        }

        beginTest("original shapes behave as they did: constant count when diatonic");
        {
            for (int shape = 0; shape < firstNew; ++shape)
            {
                juce::Random rng(4000 + shape);
                const auto chords = sources::planing(0, 0, 4, 16, true, rng, shape);
                for (const auto& c : chords)
                    expectEquals((int) c.notes.size(), (int) chords.front().notes.size());
            }
        }

        beginTest("typeForDegreeSize: 9ths and 11ths by quality, diminished falls to Half Diminished");
        {
            using Q = modes::Quality;
            const auto name = [](int t) { return juce::String(chordgen::types()[(size_t) t].name); };
            expectEquals(name(sources::detail::typeForDegreeSize(Q::major, 5)), juce::String("Major 9th"));
            expectEquals(name(sources::detail::typeForDegreeSize(Q::minor, 5)), juce::String("Minor 9th"));
            expectEquals(name(sources::detail::typeForDegreeSize(Q::minor, 6)), juce::String("Minor 11th"));
            expectEquals(name(sources::detail::typeForDegreeSize(Q::major, 6)), juce::String("Major 13th"));
            expectEquals(name(sources::detail::typeForDegreeSize(Q::diminished, 5)), juce::String("Half Diminished"));
            expectEquals(name(sources::detail::typeForDegreeSize(Q::diminished, 3)), juce::String("Diminished"));
        }
    }
};
static WidePlaningTests widePlaningTests;

struct ChordTypeTableTests : juce::UnitTest
{
    ChordTypeTableTests() : juce::UnitTest("keys::chordgen::types (append-only)") {}

    void runTest() override
    {
        using namespace keys;
        struct Row { const char* name; std::vector<int> ivals; };
        const std::vector<Row> golden = {
            { "Major", { 0, 4, 7 } }, { "Minor", { 0, 3, 7 } }, { "Diminished", { 0, 3, 6 } },
            { "Augmented", { 0, 4, 8 } }, { "Sus2", { 0, 2, 7 } }, { "Sus4", { 0, 5, 7 } },
            { "Major 7th", { 0, 4, 7, 11 } }, { "Minor 7th", { 0, 3, 7, 10 } },
            { "Dominant 7th", { 0, 4, 7, 10 } }, { "Diminished 7th", { 0, 3, 6, 9 } },
            { "Half Diminished", { 0, 3, 6, 10 } }, { "Minor Major 7th", { 0, 3, 7, 11 } },
            { "Major 6th", { 0, 4, 7, 9 } }, { "Minor 6th", { 0, 3, 7, 9 } },
            { "Add9", { 0, 4, 7, 14 } }, { "Minor Add9", { 0, 3, 7, 14 } },
            { "Major 9th", { 0, 4, 7, 11, 14 } }, { "Minor 9th", { 0, 3, 7, 10, 14 } },
            { "Dominant 9th", { 0, 4, 7, 10, 14 } }, { "6/9", { 0, 4, 7, 9, 14 } },
            // 2026-11-02
            { "Minor 11th", { 0, 3, 7, 10, 14, 17 } }, { "Major 7th #11", { 0, 4, 7, 11, 18 } },
            { "Dominant 13th", { 0, 4, 7, 10, 14, 21 } }, { "Major 13th", { 0, 4, 7, 11, 14, 21 } },
            { "Quartal", { 0, 5, 10, 15 } }, { "Quintal", { 0, 7, 14 } },
        };

        beginTest("the type table is append-only: saved pads store the index");
        {
            const auto& t = chordgen::types();
            expectEquals((int) t.size(), (int) golden.size());
            for (size_t i = 0; i < golden.size() && i < t.size(); ++i)
            {
                expectEquals(juce::String(t[i].name), juce::String(golden[i].name));
                expect(t[i].ivals == golden[i].ivals, juce::String("intervals changed for ") + golden[i].name);
            }
        }

        beginTest("the namer reads every type back, at every root");
        {
            // Pitch classes decide a name, so the two symmetric chords (diminished 7th repeats at
            // the minor third, augmented at the major third) can be named from any of their notes
            // and only the suffix is checked. Quintal is the sus2 pitch-class set by construction.
            const std::map<std::string, std::string> suffix = {
                { "Major", "" }, { "Minor", "m" }, { "Diminished", "dim" }, { "Augmented", "aug" },
                { "Sus2", "sus2" }, { "Sus4", "sus4" }, { "Major 7th", "maj7" }, { "Minor 7th", "m7" },
                { "Dominant 7th", "7" }, { "Diminished 7th", "dim7" }, { "Half Diminished", "m7b5" },
                { "Minor Major 7th", "mMaj7" }, { "Major 6th", "6" }, { "Minor 6th", "m6" },
                { "Add9", "add9" }, { "Major 9th", "maj9" }, { "Minor 9th", "m9" },
                { "Dominant 9th", "9" }, { "6/9", "6/9" },
                { "Minor 11th", "m11" }, { "Major 7th #11", "maj7#11" }, { "Dominant 13th", "13" },
                { "Major 13th", "maj13" }, { "Quartal", "quartal" }, { "Quintal", "sus2" },
            };
            for (int t = 0; t < (int) chordgen::types().size(); ++t)
            {
                const juce::String tname = chordgen::types()[(size_t) t].name;
                if (tname == "Minor Add9")
                    continue; // no template of its own, and none was added
                const auto it = suffix.find(tname.toStdString());
                expect(it != suffix.end(), "no expected suffix for " + tname);
                if (it == suffix.end())
                    continue;
                // At C the name is exact. At the other roots a pitch-class set can be two chords
                // (Am7 is C6, Csus4 is Fsus2, Am11 is Cmaj13), so the check there is a round
                // trip: whatever root and suffix come back must rebuild the same pitch classes.
                // Only the new types whose sets are unambiguous are held to the exact name at every root (Minor 11th and Major 13th are one set, a minor third apart, so only C is exact).
                expectEquals(chords::detect(chordgen::chordNotes(0, t, 4)),
                             juce::String("C") + it->second.c_str(), tname + " at C");
                const bool isNew = t >= 20 && tname != "Quintal" && tname != "Minor 11th" && tname != "Major 13th";
                for (int root = 0; root < 12; ++root)
                {
                    const auto notes = chordgen::chordNotes(root, t, 4);
                    const auto got = chords::detect(notes);
                    if (isNew)
                        expectEquals(got, juce::String(chords::noteName(root)) + it->second.c_str(),
                                     tname + " at root " + juce::String(root));

                    const int rootLen = got.length() > 1 && got[1] == '#' ? 2 : 1;
                    int gotRoot = -1;
                    for (int pc = 0; pc < 12; ++pc)
                        if (got.substring(0, rootLen) == chords::noteName(pc))
                            gotRoot = pc;
                    const auto sfx = got.substring(rootLen);
                    std::set<int> rebuilt, played;
                    for (const auto& tpl : chords::templates())
                        if (sfx == tpl.suffix)
                            for (int p : tpl.pcs)
                                rebuilt.insert((gotRoot + p) % 12);
                    for (int note : notes)
                        played.insert(chords::pitchClass(note));
                    expect(gotRoot >= 0 && rebuilt == played, tname + " at root " + juce::String(root) + " read " + got);
                }
            }
        }

        beginTest("played voicings of the new chords are named sensibly");
        {
            expectEquals(chords::detect({ 60, 63, 67, 70, 74, 77 }), juce::String("Cm11"));
            expectEquals(chords::detect({ 60, 63, 70, 74, 77 }), juce::String("Cm11")); // no fifth
            expectEquals(chords::detect({ 60, 65, 70, 75 }), juce::String("Cquartal")); // C F Bb Eb
            expectEquals(chords::detect({ 60, 64, 67, 71, 78 }), juce::String("Cmaj7#11"));
            expectEquals(chords::detect({ 60, 64, 67, 70, 74, 81 }), juce::String("C13"));
            expectEquals(chords::detect({ 60, 64, 67, 71, 74, 81 }), juce::String("Cmaj13"));
            expectEquals(chords::detect({ 60, 67, 74 }), juce::String("Csus2")); // stacked fifths
        }
    }
};
static ChordTypeTableTests chordTypeTableTests;

struct ProgressionNamesTests : juce::UnitTest
{
    ProgressionNamesTests() : juce::UnitTest("keys::sources::progressionNames") {}

    void runTest() override
    {
        beginTest("non-empty, and entry 0 is Random");
        {
            const auto names = keys::sources::progressionNames();
            expect(! names.empty());
            expectEquals(names[0], juce::String("Random"));
        }

        beginTest("every other entry matches a real template's name");
        {
            const auto names = keys::sources::progressionNames();
            const auto& lib = keys::sources::detail::progressionLibrary();
            expectEquals((int) names.size(), (int) lib.size() + 1);
            for (size_t i = 0; i < lib.size(); ++i)
                expectEquals(names[i + 1], juce::String(lib[i].name));
        }
    }
};
static ProgressionNamesTests progressionNamesTests;

// The trap that `ChordGenMenu::fitVoicing` has to order itself around, pinned here because
// fitVoicing itself is private to a class that needs a live KeysProcessor and cannot be reached
// from a console test.
//
// `rootPosition` is the normalisation that makes an inversion *replace* the rotation a chord
// arrived in rather than compound with it, so fitVoicing has to call it. It also collapses
// repeated pitch classes and restacks what survives inside a single octave, which makes it
// destructive to a note count above the mode's own size - so it must run **before** the grow
// loop, never after. It ran after for a few hours on 2026-08-01 and every request for 8 to 11
// notes silently returned 7 (5 under a pentatonic mode).
struct RootPositionCollapseTests : juce::UnitTest
{
    RootPositionCollapseTests() : juce::UnitTest("chordgen::rootPosition collapse") {}

    // fitVoicing's grow loop: stack scale thirds above the top note until the chord is `want`
    // notes. Reproduced here rather than shared, because the point of the test is the shape of
    // the interaction, and a copy that drifts still demonstrates it.
    static std::vector<int> growByThirds(std::vector<int> notes, int want, int root, int modeIndex)
    {
        const auto& scale = keys::modes::get(modeIndex).intervals;
        while ((int) notes.size() < want)
        {
            const int top = notes.back();
            int next = top + 3;
            for (int i = 0; i < 12; ++i)
            {
                const int norm = ((((next + i) - root) % 12) + 12) % 12;
                if (std::find(scale.begin(), scale.end(), norm) != scale.end())
                {
                    next = next + i;
                    break;
                }
            }
            if (next > 127 || next <= top)
                break;
            notes.push_back(next);
        }
        return notes;
    }

    void runTest() override
    {
        beginTest("a third-stack really does reach eleven notes");
        {
            // C major triad in octave 4, grown to eleven. Nothing here is clamped or deduped, so
            // this is the count fitVoicing is supposed to deliver.
            const auto grown = growByThirds(keys::chordgen::chordNotes(0, keys::chordgen::typeIndex("Major"), 4),
                                            11, 0, 0);
            expectEquals((int) grown.size(), 11, "the grow loop itself can reach eleven notes");
            expect(ascendingStrict(grown), "a grown stack is strictly ascending");
            expect(allInRange(grown), "a grown stack stays on the keyboard");
            expect(grown.back() - grown.front() > 12, "eleven notes cannot fit inside one octave");
        }

        beginTest("rootPosition after the grow loop throws most of it away");
        {
            const auto grown = growByThirds(keys::chordgen::chordNotes(0, keys::chordgen::typeIndex("Major"), 4),
                                            11, 0, 0);
            const auto collapsed = keys::chordgen::rootPosition(grown, 0);
            // Seven, because a seven-note mode has seven distinct pitch classes and stacking
            // thirds through it returns to the root's own on the eighth note.
            expectEquals((int) collapsed.size(), 7,
                         "rootPosition collapses an eleven-note third-stack to the mode's size");
            expect(collapsed.back() - collapsed.front() < 12,
                   "and restacks what is left inside one octave");
        }

        beginTest("what survives is exactly the distinct pitch classes, under every mode");
        {
            // The general statement of it, and the one worth pinning: rootPosition keeps one
            // note per pitch class, so the ceiling on a grown chord is however many distinct
            // classes the third-stack happened to visit - never the count that was asked for.
            //
            // That ceiling is *not* simply the mode's size. Stacking +3-and-search through a
            // five-note scale from a major triad cycles through only three classes, because the
            // step skips the degrees a third does not land on. So a pentatonic mode caps at
            // three, not five: worse than the seven-note case, not better.
            for (int m = 0; m < keys::modes::count(); ++m)
            {
                const auto grown =
                    growByThirds(keys::chordgen::chordNotes(0, keys::chordgen::typeIndex("Major"), 4), 11, 0, m);
                const auto collapsed = keys::chordgen::rootPosition(grown, 0);
                const auto pcs = pcsOf(grown); // one vector, not two temporaries
                const std::set<int> distinct(pcs.begin(), pcs.end());
                expectEquals((int) collapsed.size(), (int) distinct.size(),
                             "rootPosition keeps exactly one note per pitch class, mode "
                                 + juce::String(m));
                expect((int) collapsed.size() <= 11, "and never more than was asked for");
            }
        }

        beginTest("every voicing pass is a no-op on a single note");
        {
            // The floor of `genNotesMin` / `genNotesMax` moved to 1 on 2026-08-21, and the
            // changelog's claim that nothing downstream needed two is what this pins. Each of
            // these runs over whatever a source produced, so a one-note chord meets all of
            // them - and the interesting failure is not a crash but a *silent* one: a pass
            // that quietly returns two notes turns "a single note" back into a dyad, and the
            // only place that shows up is on screen.
            const std::vector<int> one { 60 };

            for (int inv = 0; inv < 5; ++inv)
            {
                const auto out = keys::chordgen::applyInversion(one, inv);
                expectEquals((int) out.size(), 1, "applyInversion left one note alone, inv "
                                                      + juce::String(inv));
                if (out.size() == 1)
                    expectEquals(out.front(), 60, "...and did not move it");
            }

            const auto spread = keys::chordgen::applySpread(one);
            expectEquals((int) spread.size(), 1, "applySpread left one note alone");
            if (spread.size() == 1)
                expectEquals(spread.front(), 60, "...and did not move it");

            const auto rooted = keys::chordgen::rootPosition(one, 0);
            expectEquals((int) rooted.size(), 1, "rootPosition left one note alone");

            // The voicing *cycle* still has to offer something to walk, or "Next voicing" on a
            // one-note card would step onto nothing. voicingCount's floor of 2 is what covers
            // that, and canRevoice is what stops the walk being offered in the first place.
            expect(! keys::chordgen::canRevoice(one), "a single note cannot be re-voiced");
            expect(keys::chordgen::canRevoice({ 60, 64 }), "...and two notes can");

            // fitVoicing's own shrink is `want >= 1`, mirrored here the way the grow loop above
            // is: asking a triad down to one note keeps the root and drops from the top.
            auto shrunk = keys::chordgen::chordNotes(0, keys::chordgen::typeIndex("Major"), 4);
            while ((int) shrunk.size() > 1)
                shrunk.pop_back();
            expectEquals((int) shrunk.size(), 1, "a triad shrinks to a single note");
            if (shrunk.size() == 1)
                expectEquals(shrunk.front() % 12, 0, "...and the note it keeps is the root");
        }
    }
};
static RootPositionCollapseTests rootPositionCollapseTests;
} // namespace
