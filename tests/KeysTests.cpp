// Unit tests for Keys' pure logic. JUCE UnitTest framework, no extra dependency.
// Run: cmake --build build --target Keys_tests && ctest --test-dir build
#include "ChordGen.h"
#include "ChordNumerals.h"
#include "ChordSuggest.h"
#include "Chords.h"
#include "NoteMath.h"
#include "ScaleModes.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <iostream>
#include <set> // used below; ChordGen.h happens to pull it in, which is not a contract

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

        beginTest("root position is read back out of any arrangement, in the same register");
        const std::vector<int> cRoot { 60, 64, 67 };
        expect(keys::chordgen::rootPosition(cRoot, 0) == cRoot);
        expect(keys::chordgen::rootPosition({ 64, 67, 72 }, 0) == cRoot);        // 1st inversion
        expect(keys::chordgen::rootPosition({ 67, 72, 76 }, 0) == cRoot);        // 2nd inversion
        expect(keys::chordgen::rootPosition({ 60, 67, 76 }, 0) == cRoot);        // spread
        expect(keys::chordgen::rootPosition({ 64, 60, 67 }, 0) == cRoot);        // unsorted input
        // A doubled pitch class collapses, and the register survives it. Stacking the double an
        // octave up instead read {60,64,67,72} back as {60,72,76,79}, which is neither the same
        // register nor a voicing of anything - and two hands on the keybed produce a doubled
        // root constantly.
        expect(keys::chordgen::rootPosition({ 60, 64, 67, 72 }, 0) == cRoot);   // doubled root
        expect(keys::chordgen::rootPosition({ 60, 64, 67, 76 }, 0) == cRoot);   // doubled third
        expect(keys::chordgen::rootPosition({ 48, 55, 60, 64, 67 }, 0)
               == std::vector<int> { 48, 52, 55 });                             // two-handed C

        beginTest("the voicing cycle walks root, the inversions, the spread, then round again");
        // Every step is the same pitch classes in a different arrangement, and the walk stays
        // in one register: this is what "Next voicing" on a pad menu does, over and over.
        const auto walk = [](std::vector<int> notes, int rootPc, int steps)
        {
            for (int i = 0; i < steps; ++i)
                notes = keys::chordgen::applyVoicing(keys::chordgen::rootPosition(notes, rootPc),
                                                     keys::chordgen::voicingOf(notes, rootPc) + 1);
            return notes;
        };
        expect(walk(cRoot, 0, 1) == std::vector<int> { 64, 67, 72 });
        expect(walk(cRoot, 0, 2) == std::vector<int> { 67, 72, 76 });
        expect(walk(cRoot, 0, 3) == std::vector<int> { 60, 67, 76 }); // spread: root stays in the bass
        expect(walk(cRoot, 0, 4) == cRoot);                           // and round, with no octave drift
        expect(walk(cRoot, 0, 12) == cRoot);

        beginTest("every voicing is a distinct arrangement of the same pitch classes");
        const std::vector<int> maj7 { 60, 64, 67, 71 };
        std::set<std::vector<int>> shapes;
        const std::set<int> maj7Pcs { 0, 4, 7, 11 };
        for (int v = 0; v < keys::chordgen::voicingCount(4); ++v)
        {
            const auto arranged = keys::chordgen::applyVoicing(maj7, v);
            expectEquals((int) arranged.size(), 4);
            for (int n : arranged)
                expect(maj7Pcs.count(keys::chords::pitchClass(n)) == 1, "re-voicing changed a note");
            expect(shapes.insert(arranged).second, "two voicings came out identical");
            expectEquals(keys::chordgen::voicingOf(arranged, 0), v); // and each one identifies itself
        }
        expectEquals((int) shapes.size(), 5); // root, three inversions, spread

        beginTest("a chord that is none of the voicings restarts the cycle at root position");
        // A hand-built spacing off the keyboard - C major with both upper voices an octave up -
        // is no voicing in the cycle, so voicingOf says -1 and the next step is root position.
        const std::vector<int> wide { 60, 76, 79 };
        expectEquals(keys::chordgen::voicingOf(wide, 0), -1);
        expect(walk(wide, 0, 1) == cRoot);

        beginTest("a doubled pitch class walks without drifting an octave or repeating a note");
        // Two hands on the keybed is the ordinary way to build one of these. Keeping the double
        // read {60,64,67,72} as the 2nd inversion of {60,72,76,79}, so the first press wrote
        // {72,79,84,88} - an octave up - and it climbed again on every press until the chord ran
        // off the keyboard and the item greyed for good with no way back to root position. The
        // other half of the same bug: {60,72,76,79} read as root position, whose 1st inversion
        // is {72,72,76,79}, the same MIDI note twice on one pad.
        const std::vector<int> doubledRoot { 60, 64, 67, 72 };
        expect(walk(doubledRoot, 0, 1) == cRoot); // the double collapses on the way in
        expect(walk(doubledRoot, 0, 5) == cRoot); // and round from there, in the same register
        expect(walk({ 60, 72, 76, 79 }, 0, 1) == cRoot);
        for (int step = 1; step <= 12; ++step)
        {
            const auto v = walk(doubledRoot, 0, step);
            const auto label = " at step " + juce::String(step);
            expect(std::adjacent_find(v.begin(), v.end()) == v.end(), "duplicate MIDI note" + label);
            expect(v.front() >= 60 && v.back() <= 76, "the walk drifted out of its register" + label);
            for (int n : v)
                expect(std::set<int> { 0, 4, 7 }.count(keys::chords::pitchClass(n)) == 1,
                       "re-voicing changed a note" + label);
        }

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

        beginTest("a 16-slot page fill is the same seven degrees in order, then nine more");
        // Octavium parity: a full page is 16 pads. The seed is degree-order regardless of
        // page size; only the sampled tail's length follows `count`.
        const auto full = keys::chordgen::generate(0, cIonian, 16, strict, {}, rng);
        expectEquals((int) full.size(), 16);
        for (int i = 0; i < 7; ++i)
        {
            expectEquals(keys::chords::detect(full[(size_t) i].notes), juce::String(want[i]));
            expectEquals(full[(size_t) i].degree, i);
        }
        std::set<std::pair<int, int>> fillSeen;
        for (const auto& c : full)
        {
            expect(! c.notes.empty(), "every fill slot is a valid chord");
            expect(fillSeen.insert({ c.rootPc, c.type }).second, "duplicate chord in a 16-slot fill");
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

struct ChordNumeralTests : juce::UnitTest
{
    ChordNumeralTests() : juce::UnitTest("keys::numerals") {}

    void runTest() override
    {
        using namespace keys;
        const int major = modes::indexOf("Major (Ionian)");
        const int naturalMinor = modes::indexOf("Natural Minor (Aeolian)");

        beginTest("a degree resolves to its cased roman numeral, in the mode's own casing");
        // C major's qualities are major, minor, minor, major, major, minor, diminished for
        // degrees 0..6 - I ii iii IV V vi vii.
        expectEquals(numerals::forChord({}, 0, -1, 0, major), juce::String("I"));
        expectEquals(numerals::forChord({}, 1, -1, 0, major), juce::String("ii"));
        expectEquals(numerals::forChord({}, 5, -1, 0, major), juce::String("vi"));

        beginTest("a diminished degree carries the small degree sign");
        // Built from the code point rather than typed, matching forDegree's own reasoning: the
        // literal stays plain ASCII regardless of source encoding.
        const juce::String degreeSign = juce::String::charToString((juce::juce_wchar) 0x00B0);
        expectEquals(numerals::forChord({}, 6, -1, 0, major), juce::String("vii") + degreeSign);

        beginTest("a numeral already on the chord wins outright, degree and root ignored");
        // This is how a Markov chord or a ChordLibrary row's step carries a seventh or a
        // borrowed degree the plain I..VII table can't spell on its own: forChord never invents
        // an accidental or a "7", it only ever passes one through. The degree passed alongside
        // "bVII" (3, which would otherwise resolve to "IV") is deliberately wrong, to pin that
        // the numeral wins outright rather than merely being consulted first.
        expectEquals(numerals::forChord("V7", -1, 7, 0, major), juce::String("V7"));
        expectEquals(numerals::forChord("bVII", 3, 0, 0, major), juce::String("bVII"));

        beginTest("no degree: resolved from the root against the current key");
        // The third route - a hand-captured or hand-edited pad, which carries a root but no
        // degree at all.
        expectEquals(numerals::forChord({}, -1, 7, 0, major), juce::String("V"));        // G in C major
        expectEquals(numerals::forChord({}, -1, 9, 9, naturalMinor), juce::String("i")); // A in A minor

        beginTest("a root outside the key resolves nothing");
        // C# in C major is not a member of the scale, so the lookup never finds a degree to
        // fall back on. Empty, not a "?", is the deliberate answer - see the header comment.
        expectEquals(numerals::forChord({}, -1, 1, 0, major), juce::String());

        beginTest("no numeral, no degree, no root: nothing resolves either");
        expectEquals(numerals::forChord({}, -1, -1, 0, major), juce::String());
    }
};

static ChordNumeralTests chordNumeralTests;
} // namespace

int main()
{
    // JUCE's default logger writes to the debugger, not the console, so a failing test
    // would exit non-zero and tell you nothing. Print the results ourselves.
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();

    int failures = 0, cases = 0, checks = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult(i);
        if (r == nullptr)
            continue;
        ++cases;
        checks += r->passes + r->failures;
        failures += r->failures;
        if (r->failures > 0)
        {
            std::cout << "FAIL  " << r->unitTestName << " / " << r->subcategoryName << "\n";
            for (const auto& m : r->messages)
                std::cout << "        " << m << "\n";
        }
    }
    // The counts, always: "All tests passed" alone cannot tell a full suite from one that
    // silently stopped registering itself.
    std::cout << (failures == 0 ? "All tests passed." : "Failures: " + juce::String(failures).toStdString())
              << "  (" << cases << " cases, " << checks << " checks)" << std::endl;
    return failures == 0 ? 0 : 1;
}
