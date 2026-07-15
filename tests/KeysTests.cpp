// Unit tests for Keys' pure logic. JUCE UnitTest framework, no extra dependency.
// Run: cmake --build build --target Keys_tests && ctest --test-dir build
#include "ChordGen.h"
#include "ChordSuggest.h"
#include "Chords.h"
#include "NoteMath.h"
#include "ScaleModes.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <iostream>

namespace
{
constexpr int cMajor = 0; // "Major" is index 0 in okstudio::scales::all()

struct NoteMathTests : juce::UnitTest
{
    NoteMathTests() : juce::UnitTest("keys::resolveOutputNote") {}

    void runTest() override
    {
        beginTest("no scale-lock: octave transpose, clamped");
        expectEquals(keys::resolveOutputNote(60, false, 0, cMajor, 0), 60);
        expectEquals(keys::resolveOutputNote(60, false, 0, cMajor, 1), 72);
        expectEquals(keys::resolveOutputNote(60, false, 0, cMajor, -1), 48);
        expectEquals(keys::resolveOutputNote(61, false, 0, cMajor, 0), 61); // no snap when off
        expectEquals(keys::resolveOutputNote(120, false, 0, cMajor, 3), 127); // clamp high
        expectEquals(keys::resolveOutputNote(5, false, 0, cMajor, -3), 0);     // clamp low

        beginTest("scale-lock snaps first, then transposes");
        expectEquals(keys::resolveOutputNote(61, true, 0, cMajor, 0), 60);  // C# -> C
        expectEquals(keys::resolveOutputNote(61, true, 0, cMajor, 1), 72);  // C# -> C, +1 octave
        expectEquals(keys::resolveOutputNote(66, true, 0, cMajor, 0), 65);  // F# -> F
        expectEquals(keys::resolveOutputNote(64, true, 0, cMajor, 0), 64);  // E already in scale
    }
};

static NoteMathTests noteMathTests;

struct ChordTests : juce::UnitTest
{
    ChordTests() : juce::UnitTest("keys::chords::detect") {}

    void runTest() override
    {
        beginTest("triads and sevenths from C");
        expectEquals(keys::chords::detect({ 60, 64, 67 }), juce::String("C"));      // C major
        expectEquals(keys::chords::detect({ 60, 63, 67 }), juce::String("Cm"));     // C minor
        expectEquals(keys::chords::detect({ 60, 63, 66 }), juce::String("Cdim"));   // C diminished
        expectEquals(keys::chords::detect({ 60, 64, 68 }), juce::String("Caug"));   // C augmented
        expectEquals(keys::chords::detect({ 60, 65, 67 }), juce::String("Csus4"));  // C sus4
        expectEquals(keys::chords::detect({ 60, 64, 67, 71 }), juce::String("Cmaj7"));
        expectEquals(keys::chords::detect({ 60, 63, 67, 70 }), juce::String("Cm7"));
        expectEquals(keys::chords::detect({ 60, 64, 67, 70 }), juce::String("C7"));
        expectEquals(keys::chords::detect({ 60, 67 }), juce::String("C5"));         // power chord

        beginTest("root detection is rotation-independent");
        expectEquals(keys::chords::detect({ 62, 66, 69 }), juce::String("D"));      // D F# A
        expectEquals(keys::chords::detect({ 64, 67, 72 }), juce::String("C"));      // inverted C major

        beginTest("degenerate inputs");
        expectEquals(keys::chords::detect({}), juce::String());
        expectEquals(keys::chords::detect({ 60 }), juce::String("C"));
    }
};

static ChordTests chordTests;

struct ScaleModeTests : juce::UnitTest
{
    ScaleModeTests() : juce::UnitTest("keys::modes") {}

    void runTest() override
    {
        beginTest("every mode has a quality per degree");
        // The generator indexes qualities by degree; a short list would read out of bounds.
        for (const auto& m : keys::modes::all())
            expectEquals((int) m.qualities.size(), (int) m.intervals.size(), juce::String(m.name));

        beginTest("every mode maps onto a kit scale");
        // Emotion presets move Scale Lock via this mapping; -1 would silently skip it.
        for (int i = 0; i < keys::modes::count(); ++i)
            expect(keys::modes::kitScaleIndex(i) >= 0, juce::String(keys::modes::get(i).name));

        beginTest("emotion presets name modes that exist");
        // indexOf falls back to 0, so a typo would quietly become C major rather than fail.
        for (const auto& e : keys::modes::emotions())
            expect(keys::modes::names().contains(e.mode), juce::String(e.label));
    }
};

static ScaleModeTests scaleModeTests;

struct ChordGenTests : juce::UnitTest
{
    ChordGenTests() : juce::UnitTest("keys::chordgen") {}

    void runTest() override
    {
        juce::Random rng(0x5eed); // fixed seed: generation is random, the tests are not
        const int cIonian = keys::modes::indexOf("Major (Ionian)");

        beginTest("inversion lifts the lowest notes an octave");
        expect(keys::chordgen::applyInversion({ 60, 64, 67 }, 0) == std::vector<int> { 60, 64, 67 });
        expect(keys::chordgen::applyInversion({ 60, 64, 67 }, 1) == std::vector<int> { 64, 67, 72 });
        expect(keys::chordgen::applyInversion({ 60, 64, 67 }, 2) == std::vector<int> { 67, 72, 76 });
        // Never lifts every note: that would just be the same chord an octave up.
        expect(keys::chordgen::applyInversion({ 60, 64, 67 }, 9) == std::vector<int> { 67, 72, 76 });

        beginTest("strict diatonic fill is the seven triads of the key, in degree order");
        keys::chordgen::Options strict; // defaults: compliance 1.0, root position, all sizes
        strict.noteCounts = { 3 };
        const auto seven = keys::chordgen::generate(0, cIonian, 7, strict, {}, rng);
        expectEquals((int) seven.size(), 7);
        const char* want[] = { "C", "Dm", "Em", "F", "G", "Am", "Bdim" };
        for (int i = 0; i < 7; ++i)
        {
            expectEquals(keys::chords::detect(seven[(size_t) i].notes), juce::String(want[i]));
            expectEquals(seven[(size_t) i].degree, i); // degree is tracked, so Regen can reuse it
        }

        beginTest("full compliance never leaves the key");
        const std::vector<int> cMajorPcs { 0, 2, 4, 5, 7, 9, 11 };
        const auto filled = keys::chordgen::generate(0, cIonian, 16, strict, {}, rng);
        for (const auto& c : filled)
            for (int n : c.notes)
                expect(std::find(cMajorPcs.begin(), cMajorPcs.end(), keys::chords::pitchClass(n))
                           != cMajorPcs.end(),
                       "out-of-key note in " + keys::chords::detect(c.notes));

        beginTest("note-count filter is honoured");
        keys::chordgen::Options sevenths;
        sevenths.noteCounts = { 4 };
        for (const auto& c : keys::chordgen::generate(0, cIonian, 12, sevenths, {}, rng))
            expectEquals((int) c.notes.size(), 4);

        beginTest("zero compliance reaches outside the key");
        // The whole point of the slider: at 0 the pool must be able to leave C major.
        keys::chordgen::Options loose;
        loose.scaleCompliance = 0.0f;
        bool sawChromatic = false;
        for (const auto& c : keys::chordgen::generate(0, cIonian, 16, loose, {}, rng))
            for (int n : c.notes)
                if (std::find(cMajorPcs.begin(), cMajorPcs.end(), keys::chords::pitchClass(n))
                    == cMajorPcs.end())
                    sawChromatic = true;
        expect(sawChromatic);

        beginTest("regenerating a degree keeps the root and finds a different chord");
        keys::chordgen::Options anySize; // the default: triads, 7ths and 9ths all allowed
        for (int degree = 0; degree < 7; ++degree)
        {
            const auto& mode = keys::modes::get(cIonian);
            const int currentType = keys::chordgen::baseType(mode.qualities[(size_t) degree]);
            const auto c = keys::chordgen::generateSingle(0, cIonian, degree, currentType, anySize, {}, rng);
            expectEquals(c.rootPc, mode.intervals[(size_t) degree] % 12);
            expect(c.type != currentType, "regenerate returned the same chord type");
        }

        beginTest("regenerate honours the note-count filter, even with no alternative");
        // A diminished degree with only triads allowed has exactly one legal chord. Keeping
        // it is correct; quietly returning a 4-note chord instead would not be.
        for (int degree = 0; degree < 7; ++degree)
        {
            const int currentType = keys::chordgen::baseType(keys::modes::get(cIonian).qualities[(size_t) degree]);
            const auto c = keys::chordgen::generateSingle(0, cIonian, degree, currentType, strict, {}, rng);
            expectEquals((int) c.notes.size(), 3);
        }

        beginTest("generate never returns more than asked, and never repeats a chord");
        const auto many = keys::chordgen::generate(0, cIonian, 8, strict, {}, rng);
        expect((int) many.size() <= 8);
        std::set<std::pair<int, int>> seen;
        for (const auto& c : many)
            expect(seen.insert({ c.rootPc, c.type }).second, "duplicate chord in one fill");
    }
};

static ChordGenTests chordGenTests;

struct SuggestTests : juce::UnitTest
{
    SuggestTests() : juce::UnitTest("keys::suggest") {}

    void runTest() override
    {
        const int major = keys::chordgen::typeIndex("Major");
        const int minor = keys::chordgen::typeIndex("Minor");

        beginTest("major/minor flavour is read off the third");
        expect(keys::suggest::isMajorish(major));
        expect(! keys::suggest::isMajorish(minor));
        expect(keys::suggest::isMajorish(keys::chordgen::typeIndex("Dominant 7th")));
        expect(! keys::suggest::isMajorish(keys::chordgen::typeIndex("Minor 7th")));

        beginTest("the Neo-Riemannian transforms land where theory says");
        const auto find = [](const std::vector<keys::suggest::Suggestion>& all, const char* t)
        {
            for (const auto& s : all)
                if (juce::String(s.transform).startsWith(t))
                    return s;
            return keys::suggest::Suggestion {};
        };
        const auto fromC = keys::suggest::all(0, major, 4); // C major
        expectEquals(find(fromC, "P").name, juce::String("C Minor"));   // parallel
        expectEquals(find(fromC, "R").name, juce::String("A Minor"));   // relative
        expectEquals(find(fromC, "L").name, juce::String("E Minor"));   // leading-tone
        expectEquals(find(fromC, "N").name, juce::String("F Minor"));   // Nebenverwandt
        expectEquals(find(fromC, "S").name, juce::String("C# Minor"));  // slide
        expectEquals(find(fromC, "H").name, juce::String("G# Minor"));  // hexatonic pole

        const auto fromAm = keys::suggest::all(9, minor, 4); // A minor
        expectEquals(find(fromAm, "P").name, juce::String("A Major"));
        expectEquals(find(fromAm, "R").name, juce::String("C Major"));  // relative major
        expectEquals(find(fromAm, "L").name, juce::String("F Major"));

        beginTest("fifths and chromatic moves");
        expectEquals(find(fromC, "V (").name, juce::String("G Major"));         // dominant
        expectEquals(find(fromC, "IV").name, juce::String("F Major"));          // subdominant
        expectEquals(find(fromC, "V7").name, juce::String("G Dominant 7th"));
        expectEquals(find(fromC, "Tritone").name, juce::String("F# Dominant 7th"));
        expectEquals(find(fromC, "bII").name, juce::String("C# Major"));        // Neapolitan

        beginTest("suggestions land in the register of the chord they follow");
        for (const auto& s : keys::suggest::all(0, major, 2))
            expect(! s.notes.empty() && s.notes.front() / 12 == 2);

        beginTest("a played chord is analysed back to its root and type");
        expect(keys::suggest::analyse({ 60, 64, 67 }) == std::make_pair(0, major));
        expect(keys::suggest::analyse({ 60, 63, 67 }) == std::make_pair(0, minor));
        // Inverted: the root is E-less rotation but the chord is still C major.
        expect(keys::suggest::analyse({ 64, 67, 72 }) == std::make_pair(0, major));
        expect(keys::suggest::analyse({ 62, 65, 69, 72 })
               == std::make_pair(2, keys::chordgen::typeIndex("Minor 7th")));
    }
};

static SuggestTests suggestTests;
} // namespace

int main()
{
    // JUCE's default logger writes to the debugger, not the console, so a failing test
    // would exit non-zero and tell you nothing. Print the results ourselves.
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult(i);
        if (r == nullptr)
            continue;
        failures += r->failures;
        if (r->failures > 0)
        {
            std::cout << "FAIL  " << r->unitTestName << " / " << r->subcategoryName << "\n";
            for (const auto& m : r->messages)
                std::cout << "        " << m << "\n";
        }
    }
    std::cout << (failures == 0 ? "All tests passed." : "Failures: " + juce::String(failures).toStdString())
              << std::endl;
    return failures == 0 ? 0 : 1;
}
