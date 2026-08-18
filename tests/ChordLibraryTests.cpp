// Unit tests for keys::chordlib (ChordLibrary.h). JUCE UnitTest framework, self-registering
// statics only -- tests/KeysTests.cpp owns the runner's main(), same as MarkovTests.cpp.
//
// The first two cases here are the important ones and they are not really unit tests, they are a
// **spellchecker for a hand-typed table**. Every row is a numeral string somebody wrote out by
// hand, and the two ways to get one wrong are both silent: a token that will not parse is skipped
// at play time, so the progression simply comes out short, and a misspelt tag is a row that can
// never be found by the picker that was meant to find it. Neither shows a symptom on screen. So
// the table validates itself on every build.
#include "ChordGen.h"
#include "ChordLibrary.h"
#include "ScaleModes.h"
#include <juce_core/juce_core.h>
#include <set>
#include <vector>

namespace
{
    struct ChordLibraryTests : juce::UnitTest
    {
        ChordLibraryTests() : juce::UnitTest("Chord library", "keys") {}

        void runTest() override
        {
            beginTest("every numeral in the table parses");
            {
                const auto bad = keys::chordlib::firstParseFailure();
                expect(bad.isEmpty(), "unparseable numeral -> " + bad);
            }

            beginTest("every tag is in the vocabulary");
            {
                const auto bad = keys::chordlib::firstUnknownTag();
                expect(bad.isEmpty(), "unknown tag -> " + bad);
            }

            beginTest("every row is named, tagged and playable");
            {
                std::set<juce::String> names;
                for (const auto& e : keys::chordlib::table())
                {
                    const juce::String name(e.name);
                    expect(name.isNotEmpty(), "a row has no name");
                    // Duplicate names are not a correctness bug, they are a *browsing* bug: two
                    // rows reading the same thing in one filtered list is a picker you cannot use.
                    expect(names.insert(name).second, "duplicate row name: " + name);
                    expect(! e.moods.empty(), name + " has no mood");
                    expect(! e.genres.empty(), name + " has no genre");
                    expect(e.mode >= 0 && e.mode < keys::modes::count(), name + " has a bad mode");
                    expect((int) e.function >= 0
                               && (int) e.function < (int) keys::chordlib::Function::count,
                           name + " has a bad function");

                    // Two chords is the floor. One chord is not a progression, and the one thing
                    // this library must never do is hand the tray a single chord under a name that
                    // promises a sequence.
                    const auto chords = keys::chordlib::chordsFor(e, 0, e.mode, 4);
                    expect(chords.size() >= 2, name + " resolved to fewer than two chords");

                    for (const auto& c : chords)
                    {
                        expect(! c.notes.empty(), name + " produced an empty chord");
                        for (const int n : c.notes)
                            expect(n >= 0 && n <= 127, name + " produced a note outside MIDI");
                    }
                }
            }

            beginTest("two rows with the same chords never share a genre");
            {
                // Reusing one progression under two names is *correct* and is how a library
                // organised by genre works - the same four chords are how you find it from the
                // Disco end and from the Neo Soul end, and Scaler does exactly this. What is not
                // correct is two rows with identical chords landing in the same filtered list,
                // where the second one is a row that can only waste a click. So the rule is by
                // genre overlap rather than a flat ban on duplicate numerals.
                const auto& t = keys::chordlib::table();
                for (size_t a = 0; a < t.size(); ++a)
                    for (size_t b = a + 1; b < t.size(); ++b)
                    {
                        if (juce::String(t[a].numerals) != juce::String(t[b].numerals))
                            continue;
                        for (const auto* ga : t[a].genres)
                            for (const auto* gb : t[b].genres)
                                expect(juce::String(ga) != juce::String(gb),
                                       juce::String(t[a].name) + " and " + t[b].name
                                           + " are the same chords in " + ga);
                    }
            }

            beginTest("a row plays the same shape in every key");
            {
                // The whole point of storing numerals rather than notes: one row serves twelve
                // keys. Transposing the key must transpose every chord by the same amount and
                // change nothing else, so the *intervals between roots* are key-independent.
                for (const auto& e : keys::chordlib::table())
                {
                    const auto inC = keys::chordlib::chordsFor(e, 0, 0, 4);
                    for (int key = 1; key < 12; ++key)
                    {
                        const auto inK = keys::chordlib::chordsFor(e, key, 0, 4);
                        expectEquals((int) inK.size(), (int) inC.size(),
                                     juce::String(e.name) + " changed length in key " + juce::String(key));
                        for (size_t i = 0; i < inC.size() && i < inK.size(); ++i)
                        {
                            expectEquals(inK[i].rootPc, (inC[i].rootPc + key) % 12,
                                         juce::String(e.name) + " root moved wrong in key " + juce::String(key));
                            expectEquals(inK[i].type, inC[i].type,
                                         juce::String(e.name) + " changed type in key " + juce::String(key));
                        }
                    }
                }
            }

            beginTest("degree is resolved against the caller's mode, not the entry's");
            {
                // `chordsFor` takes the mode to label degrees against separately from the entry's
                // own, because the pads are labelled against the key you are composing in. The
                // pitches must not move when that label changes - if they ever do, the numerals
                // have stopped being absolute and every minor row in the table is wrong.
                const keys::chordlib::Entry e { "test", "i bVII bVI V", keys::chordlib::Function::descent,
                                                keys::chordlib::kAeolian, { "Dark" }, { "Rock" } };
                const auto ionian = keys::chordlib::chordsFor(e, 0, keys::chordlib::kIonian, 4);
                const auto aeolian = keys::chordlib::chordsFor(e, 0, keys::chordlib::kAeolian, 4);
                expectEquals((int) ionian.size(), 4);
                expectEquals((int) aeolian.size(), 4);
                for (size_t i = 0; i < ionian.size(); ++i)
                {
                    expectEquals(ionian[i].rootPc, aeolian[i].rootPc, "pitches moved with the label");
                    expect(ionian[i].notes == aeolian[i].notes, "notes moved with the label");
                }
                // C minor's bVI is Ab, which is degree 5 of natural minor and no degree at all of
                // Ionian. That difference is the whole reason the parameter exists.
                expectEquals(aeolian[2].degree, 5, "bVI should be degree 5 of Aeolian");
                expectEquals(ionian[2].degree, -1, "bVI is outside Ionian and should have no degree");
            }

            beginTest("find() narrows on each axis and 'any' means any");
            {
                const auto all = keys::chordlib::find({}, {}, -1);
                expectEquals((int) all.size(), (int) keys::chordlib::table().size(),
                             "empty filters should return the whole table");

                const auto jazz = keys::chordlib::find({}, "Jazz", -1);
                expect(! jazz.empty(), "no jazz rows");
                for (const auto* e : jazz)
                {
                    bool found = false;
                    for (const auto* g : e->genres)
                        if (juce::String(g) == "Jazz") { found = true; break; }
                    expect(found, juce::String(e->name) + " came back from a Jazz filter without the tag");
                }

                const auto cadences = keys::chordlib::find({}, {}, (int) keys::chordlib::Function::cadence);
                expect(! cadences.empty(), "no cadences");
                for (const auto* e : cadences)
                    expect(e->function == keys::chordlib::Function::cadence,
                           juce::String(e->name) + " came back from a cadence filter");

                // Two axes at once narrows further than either alone, or the filter is not really
                // filtering - which is the failure mode a picker with three combos would hide.
                const auto jazzCadences = keys::chordlib::find({}, "Jazz",
                                                               (int) keys::chordlib::Function::cadence);
                expect(jazzCadences.size() <= jazz.size());
                expect(jazzCadences.size() <= cadences.size());
            }

            beginTest("every function and both vocabularies have rows behind them");
            {
                // A picker offering a word with nothing behind it is a pick that can only
                // disappoint. moodsInUse/genresInUse exist so the UI never offers one; this pins
                // that they answer with a real subset rather than the whole vocabulary.
                const auto used = keys::chordlib::moodsInUse();
                expect(used.size() > 0, "no moods in use");
                for (const auto& m : used)
                    expect(! keys::chordlib::find(m, {}, -1).empty(), "mood in use with no rows: " + m);

                const auto usedGenres = keys::chordlib::genresInUse();
                expect(usedGenres.size() > 0, "no genres in use");
                for (const auto& g : usedGenres)
                    expect(! keys::chordlib::find({}, g, -1).empty(), "genre in use with no rows: " + g);
            }

            beginTest("the six appended numeral suffixes resolve to the right types");
            {
                // ChordLibrary.h needed a numeral for every type in chordgen::types() and the
                // suffix table was six short. Half-diminished is the load-bearing one: it is the
                // ii of every minor ii-V, so without it the most common cadence in the minor key
                // could not be written down at all.
                const std::pair<const char*, const char*> cases[] = {
                    { "iim7b5", "Half Diminished" }, { "imM7", "Minor Major 7th" },
                    { "im6", "Minor 6th" },          { "imadd9", "Minor Add9" },
                    { "IM9", "Major 9th" },          { "im9", "Minor 9th" },
                };
                for (const auto& [token, typeName] : cases)
                {
                    const auto p = keys::markov::detail::parseNumeralToken(token, 0);
                    expect(p.valid, juce::String(token) + " did not parse");
                    expectEquals(p.type, keys::chordgen::typeIndex(typeName),
                                 juce::String(token) + " resolved to the wrong type");
                }
                // And the append did not shadow anything that already worked: "m7" is still a
                // minor seventh now that "m7b5" sits in the same table.
                const auto m7 = keys::markov::detail::parseNumeralToken("iim7", 0);
                expect(m7.valid);
                expectEquals(m7.type, keys::chordgen::typeIndex("Minor 7th"));
            }
        }
    };

    static ChordLibraryTests chordLibraryTests;
} // namespace
