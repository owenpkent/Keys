// Unit tests for keys::sources (ChordSources.h). JUCE UnitTest framework, self-registering
// statics only -- tests/KeysTests.cpp owns the runner's main(), same as MarkovTests.cpp.
#include "ChordGen.h"
#include "ChordSources.h"
#include "ScaleModes.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
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
} // namespace
