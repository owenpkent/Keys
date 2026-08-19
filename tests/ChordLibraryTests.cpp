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

            beginTest("couldFollow gates on structure and orders by the join");
            {
                using keys::chordlib::Function;
                for (const auto& from : keys::chordlib::table())
                {
                    const auto after = keys::chordlib::couldFollow(from, false, 8);
                    for (const auto* e : after)
                    {
                        expect(e != &from, juce::String(from.name) + " could follow itself");

                        // The function gate is a gate, not a weight: a row that does not belong
                        // after this one must not appear at all, however well its first chord
                        // joins on. That is the line between a suggestion list and a shuffle.
                        bool allowed = false;
                        for (const auto f : keys::chordlib::functionsAfter(from.function))
                            if (f == e->function) { allowed = true; break; }
                        expect(allowed, juce::String(e->name) + " is not a legal move after "
                                            + from.name);
                    }
                }
            }

            beginTest("nothing follows an Open with another Open");
            {
                // The one asymmetry in the grammar worth pinning by hand, because it is the one a
                // careless edit would flatten: an Open has asked a question, so what follows it
                // answers. Two Opens in a row is a section that never lands.
                for (const auto f : keys::chordlib::functionsAfter(keys::chordlib::Function::open))
                    expect(f != keys::chordlib::Function::open, "an Open may follow an Open");

                // And every function has somewhere to go, or a progression of that kind is a dead
                // end and "Follows" silently answers nothing after it.
                for (int i = 0; i < (int) keys::chordlib::Function::count; ++i)
                    expect(! keys::chordlib::functionsAfter((keys::chordlib::Function) i).empty(),
                           juce::String(keys::chordlib::functionName((keys::chordlib::Function) i))
                               + " leads nowhere");
            }

            beginTest("the join scores a falling fifth above a repeat");
            {
                // The ranking that orders the list. Pinned by construction rather than by picking
                // rows out of the table, so it keeps testing the rule when the table moves.
                const keys::chordlib::Entry endsOnV { "t1", "I V", keys::chordlib::Function::open,
                                                      keys::chordlib::kIonian, { "Calm" }, { "Pop" } };
                const keys::chordlib::Entry startsOnI { "t2", "I IV", keys::chordlib::Function::loop,
                                                        keys::chordlib::kIonian, { "Calm" }, { "Pop" } };
                const keys::chordlib::Entry startsOnV { "t3", "V IV", keys::chordlib::Function::loop,
                                                        keys::chordlib::kIonian, { "Calm" }, { "Pop" } };
                // V -> I is up a fourth, the falling-fifth resolution; V -> V is a repeat.
                expect(keys::chordlib::joinScore(endsOnV, startsOnI)
                           > keys::chordlib::joinScore(endsOnV, startsOnV),
                       "a resolution did not beat standing still");
            }

            beginTest("byName finds a row and refuses a name that is not one");
            {
                // What a pad's stored `progression` uses to find its way home. Empty in must be
                // null out, because most pads carry exactly that.
                const auto& first = keys::chordlib::table().front();
                expect(keys::chordlib::byName(first.name) == &first);
                expect(keys::chordlib::byName({}) == nullptr);
                expect(keys::chordlib::byName("not a progression") == nullptr);
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

            beginTest("every chord's progressionStep names its own numeral, across the table");
            {
                // `chordsFor` skips empty and unparseable tokens while `numeralAt` used its own
                // tokenisation of the raw string, so a row with a double space, a trailing space
                // or a typo'd suffix shifted every later chord's numeral by one - drawn on the
                // strip under a bracket correctly naming the progression. Both go through
                // `playableNumerals` now; this pins that they agree for all 355 rows rather than
                // for the tidy ones, which is the whole point of the shared tokenisation.
                int checked = 0;
                for (const auto& e : keys::chordlib::table())
                {
                    const auto tokens = keys::chordlib::playableNumerals(e, 0);
                    const auto chords = keys::chordlib::chordsFor(e, 0, e.mode, 4);
                    expectEquals((int) chords.size(), tokens.size(),
                                 "a chord per playable numeral in " + juce::String(e.name));
                    for (const auto& c : chords)
                    {
                        expect(c.progression == juce::String(e.name),
                               "the row stamped its name on " + juce::String(e.name));
                        expect(c.progressionStep >= 0, "and a real step index");
                        expectEquals(keys::chordlib::numeralAt(e.name, c.progressionStep),
                                     tokens[c.progressionStep],
                                     "numeralAt agrees with the step in " + juce::String(e.name));
                        ++checked;
                    }
                }
                expect(checked > 1000, "the whole table was walked, not a corner of it");
            }
        }
    };

    static ChordLibraryTests chordLibraryTests;
} // namespace
