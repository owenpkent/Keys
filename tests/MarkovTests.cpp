// Unit tests for keys::markov (ChordMarkov.h / MarkovData.h). JUCE UnitTest framework,
// self-registering statics only -- tests/KeysTests.cpp owns the runner's main().
#include "ChordGen.h"
#include "ChordMarkov.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <set>
#include <vector>

namespace
{
struct MarkovParseTests : juce::UnitTest
{
    MarkovParseTests() : juce::UnitTest("keys::markov::detail::parseNumeralToken") {}

    void runTest() override
    {
        using namespace keys::markov;

        beginTest("accidentals shift the root and wrap mod 12");
        {
            expectEquals(detail::parseNumeralToken("I", 0).rootPc, 0);
            expectEquals(detail::parseNumeralToken("bII", 0).rootPc, 1);  // II=2, b=-1
            expectEquals(detail::parseNumeralToken("#IV", 0).rootPc, 6);  // IV=5, #=+1
            expectEquals(detail::parseNumeralToken("bI", 0).rootPc, 11);  // 0-1 wraps to 11
        }

        beginTest("root resolves relative to the key, not to C");
        {
            expectEquals(detail::parseNumeralToken("V", 2).rootPc, 9);   // D major: V = A
            expectEquals(detail::parseNumeralToken("i", 9).rootPc, 9);   // A minor: i = A
        }

        beginTest("longest-match numeral parsing (VI must not be read as V + stray I)");
        {
            const auto vi = detail::parseNumeralToken("VI", 0);
            expect(vi.valid);
            expectEquals(vi.rootPc, 9);
            const auto iv = detail::parseNumeralToken("IV", 0);
            expect(iv.valid);
            expectEquals(iv.rootPc, 5);
            const auto vii = detail::parseNumeralToken("vii", 0);
            expect(vii.valid);
            expectEquals(vii.rootPc, 11);
        }

        beginTest("case with an empty suffix decides quality");
        {
            expectEquals(detail::parseNumeralToken("I", 0).type, keys::chordgen::typeIndex("Major"));
            expectEquals(detail::parseNumeralToken("i", 0).type, keys::chordgen::typeIndex("Minor"));
            expectEquals(detail::parseNumeralToken("VI", 0).type, keys::chordgen::typeIndex("Major"));
            expectEquals(detail::parseNumeralToken("vi", 0).type, keys::chordgen::typeIndex("Minor"));
        }

        beginTest("every suffix in the table maps to the right chordgen type");
        {
            using Case = std::pair<const char*, const char*>;
            const std::vector<Case> cases = {
                { "M7", "Major 7th" }, { "m7", "Minor 7th" }, { "7", "Dominant 7th" },
                { "dom7", "Dominant 7th" }, { "dim", "Diminished" }, { "dim7", "Diminished 7th" },
                { "aug", "Augmented" }, { "sus2", "Sus2" }, { "sus4", "Sus4" }, { "add9", "Add9" },
                { "6", "Major 6th" }, { "69", "6/9" }, { "9", "Dominant 9th" }, { "M-5", "Diminished" },
                { "m", "Minor" }, { "M", "Major" },
            };
            for (const auto& [suffix, typeName] : cases)
            {
                const auto p = detail::parseNumeralToken(juce::String("I") + suffix, 0);
                expect(p.valid, juce::String("suffix \"") + suffix + "\" should parse");
                expectEquals(p.type, keys::chordgen::typeIndex(typeName), juce::String("suffix \"") + suffix + "\"");
            }
        }

        beginTest("malformed tokens fail to parse rather than silently reinterpreting");
        {
            expect(! detail::parseNumeralToken("Vxyz", 0).valid, "unknown suffix");
            expect(! detail::parseNumeralToken("Vi", 0).valid, "stray lowercase i after V");
            expect(! detail::parseNumeralToken("VIII", 0).valid, "VII matches, stray I suffix is unknown");
            expect(! detail::parseNumeralToken("Z", 0).valid, "not a numeral at all");
            expect(! detail::parseNumeralToken("", 0).valid, "empty token");
        }
    }
};

static MarkovParseTests markovParseTests;

struct MarkovRealizeTests : juce::UnitTest
{
    MarkovRealizeTests() : juce::UnitTest("keys::markov::detail::realizeToken") {}

    void runTest() override
    {
        using namespace keys::markov;

        beginTest("realized chord is root position at the requested octave");
        {
            const auto c = detail::realizeToken("V7", 0, 5); // G7 at octave 5
            const int wantRoot = 7;
            const int wantType = keys::chordgen::typeIndex("Dominant 7th");
            expectEquals(c.rootPc, wantRoot);
            expectEquals(c.type, wantType);
            expect(c.notes == keys::chordgen::chordNotes(wantRoot, wantType, 5));

            auto sorted = c.notes;
            std::sort(sorted.begin(), sorted.end());
            expect(c.notes == sorted, "Markov chords are never inverted");
            expectEquals(c.notes.front() / 12, 5);
            expectEquals(c.notes.front() % 12, wantRoot);
        }

        beginTest("the octave parameter is honoured (Octavium hardcodes 4; Keys does not)");
        {
            for (int octave : { 2, 4, 6 })
                expectEquals(detail::realizeToken("I", 0, octave).notes.front() / 12, octave);
        }

        beginTest("Chord::name and Chord::numeral");
        {
            const auto c = detail::realizeToken("V7", 0, 4);
            expectEquals(c.name, juce::String("G Dominant 7th"));
            expectEquals(c.numeral, juce::String("V7"));
        }

        beginTest("a token that fails to parse falls back to the tonic major rather than crashing");
        {
            const auto c = detail::realizeToken("NotAToken", 2, 4); // key = D
            expectEquals(c.rootPc, 2);
            expectEquals(c.type, keys::chordgen::typeIndex("Major"));
        }
    }
};

static MarkovRealizeTests markovRealizeTests;

struct MarkovTableTests : juce::UnitTest
{
    MarkovTableTests() : juce::UnitTest("keys::markov::detail::buildTable") {}

    void runTest() override
    {
        using namespace keys::markov;

        // A small synthetic corpus so dedupe/mood-filter can be checked exactly, rather than
        // cross-referencing the real ~90-entry corpus by hand.
        const std::vector<Progression> synthetic = {
            { kMajor, { "Hopeful" },           { "I", "V", "IV" } },
            { kMajor, { "Hopeful" },           { "I", "V", "IV" } },  // exact duplicate: must collapse
            { kMajor, { "Joyful" },            { "I", "IV", "V" } },
            { kMajor, { "Hopeful", "Tender" }, { "vi", "IV" } },
            { kMinor, { "Dark" },              { "i", "iv" } },       // different mode: must not leak in
        };

        beginTest("dedupe: an exact-duplicate numeral sequence is counted once");
        {
            const auto t = detail::buildTable(synthetic, kMajor, "");
            int totalStarts = 0;
            for (const auto& [token, n] : t.startCounts)
                totalStarts += n;
            expectEquals(totalStarts, 3); // 4 Major entries, one is a duplicate -> 3 survive
            expectEquals(t.transitions.at("I").at("V"), 1); // only the surviving copy counted
        }

        beginTest("mood filter narrows the table before counting, not after");
        {
            const auto hopeful = detail::buildTable(synthetic, kMajor, "Hopeful");
            expect(hopeful.vocabulary.count("IV") > 0);
            expect(hopeful.vocabulary.count("vi") > 0);
            expect(hopeful.vocabulary.count("V") > 0);
            expect(hopeful.vocabulary.count("i") == 0); // wrong mode entirely

            const auto joyful = detail::buildTable(synthetic, kMajor, "Joyful");
            expect(joyful.vocabulary.count("vi") == 0); // that entry is tagged Hopeful/Tender, not Joyful
        }

        beginTest("an unmatched mood produces an empty table, not a fallback to Any");
        {
            const auto empty = detail::buildTable(synthetic, kMajor, "NoSuchTag");
            expect(empty.vocabulary.empty());
            expect(empty.startCounts.empty());
            expect(empty.transitions.empty());
        }

        beginTest("mode filter keeps modes separate");
        {
            const auto major = detail::buildTable(synthetic, kMajor, "");
            expect(major.vocabulary.count("i") == 0);
            expect(major.vocabulary.count("iv") == 0);
            const auto minor = detail::buildTable(synthetic, kMinor, "");
            expectEquals((int) minor.vocabulary.size(), 2); // "i", "iv"
        }

        beginTest("the bundled corpus builds a non-empty table for every mode");
        {
            for (int mode : { kMajor, kMinor, kModal })
                expect(! detail::buildTable(mode, "").vocabulary.empty());
        }
    }
};

static MarkovTableTests markovTableTests;

struct MarkovTemperatureTests : juce::UnitTest
{
    MarkovTemperatureTests() : juce::UnitTest("keys::markov::detail::weightedChoice") {}

    void runTest() override
    {
        using namespace keys::markov;

        beginTest("temperature is clamped to Octavium's UI range (0.3-2.0)");
        {
            expectEquals(detail::clampTemperature(0.01f), 0.3f);
            expectEquals(detail::clampTemperature(50.0f), 2.0f);
            expectEquals(detail::clampTemperature(1.0f), 1.0f);
        }

        // Skewed candidate set: raw proportions are A=62.5%, B=25%, C=12.5%.
        detail::Counts counts;
        counts["A"] = 5;
        counts["B"] = 2;
        counts["C"] = 1;
        const int trials = 500;

        beginTest("0.3 sharpens the walk toward the most common bigram");
        {
            juce::Random rng(12345);
            int countA = 0;
            for (int i = 0; i < trials; ++i)
                if (detail::weightedChoice(counts, 0.3f, rng) == "A")
                    ++countA;
            expect(countA > (trials * 70) / 100,
                   "low temperature should push A's share well above its raw 62.5% proportion");
        }

        beginTest("2.0 flattens the walk toward uniform");
        {
            juce::Random rng(54321);
            int countA = 0;
            for (int i = 0; i < trials; ++i)
                if (detail::weightedChoice(counts, 2.0f, rng) == "A")
                    ++countA;
            expect(countA < (trials * 55) / 100,
                   "high temperature should pull A's share down from its raw 62.5% proportion");
        }

        beginTest("an empty candidate set returns an empty token, never crashes");
        {
            juce::Random rng(1);
            expect(detail::weightedChoice({}, 1.0f, rng).isEmpty());
        }
    }
};

static MarkovTemperatureTests markovTemperatureTests;

struct MarkovFillTests : juce::UnitTest
{
    MarkovFillTests() : juce::UnitTest("keys::markov::detail::fillNumerals") {}

    void runTest() override
    {
        using namespace keys::markov;

        beginTest("fillTo <= 0 returns exactly the generated sequence, unlooped");
        {
            const std::vector<juce::String> seq = { "I", "V", "vi", "IV", "I" };
            expect(detail::fillNumerals(seq, 0) == seq);
            expect(detail::fillNumerals(seq, -1) == seq);
        }

        beginTest("repeat-and-truncate: length 5 filled to 16 is 5+5+5+1");
        {
            const std::vector<juce::String> seq = { "I", "V", "vi", "IV", "iii" };
            const auto filled = detail::fillNumerals(seq, 16);
            expectEquals((int) filled.size(), 16);
            for (int i = 0; i < 16; ++i)
                expectEquals(filled[(size_t) i], seq[(size_t) (i % 5)]);
        }

        beginTest("fillTo smaller than the sequence truncates cleanly");
        {
            const std::vector<juce::String> seq = { "I", "V", "vi", "IV", "iii" };
            const std::vector<juce::String> want = { "I", "V", "vi" };
            expect(detail::fillNumerals(seq, 3) == want);
        }

        beginTest("an empty sequence stays empty regardless of fillTo");
        {
            expect(detail::fillNumerals({}, 16).empty());
        }
    }
};

static MarkovFillTests markovFillTests;

struct MarkovGenerateTests : juce::UnitTest
{
    MarkovGenerateTests() : juce::UnitTest("keys::markov::generate") {}

    void runTest() override
    {
        using namespace keys::markov;

        beginTest("startTokens() is Octavium's fixed 9-entry list, in order");
        {
            const std::vector<juce::String> want = { "I", "i", "IV", "iv", "V", "vi", "VI", "ii", "iii" };
            const auto& got = startTokens();
            expectEquals((int) got.size(), (int) want.size());
            for (size_t i = 0; i < want.size(); ++i)
                expectEquals(juce::String(got[i]), want[i]);
        }

        beginTest("a start token in vocabulary is used verbatim");
        {
            // "vi" appears in several Major progressions (e.g. the Axis progression).
            juce::Random rng(777);
            for (int trial = 0; trial < 20; ++trial)
            {
                const auto chords = generate(kMajor, 0, 4, 4, 1.0f, "", "vi", 0, rng);
                expect(! chords.empty());
                expectEquals(chords.front().numeral, juce::String("vi"));
            }
        }

        beginTest("a start token outside vocabulary falls back to sampling start_counts");
        {
            juce::Random rng(778);
            const auto chords = generate(kMajor, 0, 4, 4, 1.0f, "", "NotARealToken", 0, rng);
            expect(! chords.empty());
            expect(chords.front().numeral != juce::String("NotARealToken"));
        }

        beginTest("length is honoured when fillTo <= 0");
        {
            juce::Random rng(99);
            for (int len : { 1, 4, 8, 16 })
                expectEquals((int) generate(kMinor, 0, 4, len, 1.0f, "", "", 0, rng).size(), len);
        }

        beginTest("generate() honours fillTo end to end (repeat-and-truncate wiring)");
        {
            juce::Random rng(0x51ee0);
            const auto chords = generate(kMajor, 0, 4, 5, 1.0f, "", "I", 16, rng);
            expectEquals((int) chords.size(), 16);
            for (int i = 0; i < 5; ++i)
                expectEquals(chords[(size_t) i].numeral, chords[(size_t) (i + 5)].numeral);
        }

        beginTest("moodsFor returns unique, sorted tags actually used by that mode");
        {
            for (int mode : { kMajor, kMinor, kModal })
            {
                const auto moods = moodsFor(mode);
                expect(! moods.isEmpty());
                expect(! moods.contains(""), "\"Any\" is a UI sentinel, not a real tag");
                for (int i = 1; i < moods.size(); ++i)
                    expect(moods[i - 1].compare(moods[i]) < 0, "moodsFor should be sorted with no duplicates");
            }
        }
    }
};

static MarkovGenerateTests markovGenerateTests;

struct MarkovRegenerateTests : juce::UnitTest
{
    MarkovRegenerateTests() : juce::UnitTest("keys::markov::regenerateSingle") {}

    void runTest() override
    {
        using namespace keys::markov;

        beginTest("avoids the excluded numeral when an alternative exists");
        {
            // "I" has many distinct successors in the Major table (V, IV, vi, iii, ii, ...),
            // so excluding one of them should never come back excluded.
            juce::Random rng(0xC0FFEE);
            for (int trial = 0; trial < 100; ++trial)
            {
                const auto c = regenerateSingle(kMajor, 0, 4, "I", "V", 1.0f, "", rng);
                expect(c.numeral != juce::String("V"), "regenerated chord should not be the excluded one");
            }
        }

        beginTest("slot 0 (empty predecessor) samples from start_counts");
        {
            juce::Random rng(42);
            const auto c = regenerateSingle(kMajor, 0, 4, "", "", 1.0f, "", rng);
            expect(! c.numeral.isEmpty());
            expect(detail::buildTable(kMajor, "").startCounts.count(c.numeral) > 0);
        }

        beginTest("result is always a validly realized, root-position chord at the requested octave");
        {
            juce::Random rng(9001);
            for (int mode : { kMajor, kMinor, kModal })
            {
                const auto c = regenerateSingle(mode, 0, 4, "", "", 1.0f, "", rng);
                expect(! c.notes.empty());
                expectEquals(c.notes.front() / 12, 4);
            }
        }
    }
};

static MarkovRegenerateTests markovRegenerateTests;

struct MarkovCorpusLintTests : juce::UnitTest
{
    MarkovCorpusLintTests() : juce::UnitTest("keys::markov corpus (MarkovData.h)") {}

    void runTest() override
    {
        using namespace keys::markov;

        beginTest("every corpus token parses (catches typos in the hand-authored corpus)");
        {
            int checked = 0;
            for (const auto& p : corpus())
                for (const auto* tok : p.numerals)
                {
                    expect(detail::parseNumeralToken(tok, 0).valid, juce::String("unparseable token \"") + tok + "\"");
                    ++checked;
                }
            expect(checked > 0);
        }

        beginTest("every progression has 3-8 tokens and 1-3 moods");
        {
            for (const auto& p : corpus())
            {
                expect(p.numerals.size() >= 3 && p.numerals.size() <= 8, "progression length out of [3,8]");
                expect(p.moods.size() >= 1 && p.moods.size() <= 3, "mood count out of [1,3]");
            }
        }

        beginTest("every mood tag is legal for its mode");
        {
            static const std::set<juce::String, detail::StringLess> majorMoods = {
                "Hopeful", "Romantic", "Joyful", "Triumphant", "Nostalgic", "Peaceful",
                "Playful", "Relaxed", "Tender", "Spiritual", "Excited", "Empowered",
            };
            static const std::set<juce::String, detail::StringLess> minorMoods = {
                "Dark", "Mysterious", "Melancholic", "Dramatic", "Tense", "Rebellious",
                "Haunting", "Suspenseful", "Nostalgic", "Empowered", "Triumphant",
            };
            static const std::set<juce::String, detail::StringLess> modalMoods = {
                "Mysterious", "Dark", "Haunting", "Peaceful", "Playful", "Relaxed", "Spiritual",
                "Triumphant", "Nostalgic", "Empowered", "Dreamy", "Cinematic",
            };
            for (const auto& p : corpus())
            {
                const auto& legal = p.mode == kMajor ? majorMoods : (p.mode == kMinor ? minorMoods : modalMoods);
                for (const auto* m : p.moods)
                    expect(legal.count(m) > 0, juce::String("illegal mood tag \"") + m + "\"");
            }
        }

        beginTest("corpus is 75-100 entries, roughly a third per mode");
        {
            int counts[3] = { 0, 0, 0 };
            for (const auto& p : corpus())
                ++counts[p.mode];
            const int total = counts[0] + counts[1] + counts[2];
            expect(total >= 75 && total <= 100, "corpus size out of the requested 75-100 range");
            for (int n : counts)
                expect(n >= total / 4, "a mode is badly under-represented relative to the others");
        }
    }
};

static MarkovCorpusLintTests markovCorpusLintTests;
} // namespace
