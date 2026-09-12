// Unit tests for keys::chordshift (ChordShift.h). JUCE UnitTest framework, self-registering
// statics only -- tests/KeysTests.cpp owns the runner's main(), same as ChordLibraryTests.cpp.
//
// ChordShift.h is pure and header-only exactly so it can be tested without a processor or a
// surface anywhere nearby: everything here calls straight into the free functions the keybed's
// note path and its chip both read.
#include "ChordShift.h"
#include "PluginProcessor.h"
#include "ui/NoteSurface.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>

namespace
{
    using keys::chordshift::numShifts;

    // A six-shift array from a short list, the rest padded with 0 (off) - shorter to read at
    // each call site than six positional arguments, and the padding is the same "off" every
    // real parameter set defaults to.
    std::array<int, numShifts> shiftsOf(std::initializer_list<int> vals)
    {
        std::array<int, numShifts> out {};
        int i = 0;
        for (const int v : vals)
            out[(size_t) i++] = v;
        return out;
    }

    struct ChordShiftTests : juce::UnitTest
    {
        ChordShiftTests() : juce::UnitTest("Chord shift", "keys") {}

        void runTest() override
        {
            using namespace keys::chordshift;

            beginTest("voicesFor drops a shift of zero");
            {
                const auto voices = voicesFor(60, shiftsOf({ 7, 0, 0, 0, 0, 0 }));
                expectEquals((int) voices.size(), 2, "only the played note and the +7 shift");
                expect(std::find(voices.begin(), voices.end(), 67) != voices.end(),
                       "the +7 voice is present");
            }

            beginTest("voicesFor with every shift off is just the played note");
            {
                const auto voices = voicesFor(60, shiftsOf({ 0, 0, 0, 0, 0, 0 }));
                expectEquals((int) voices.size(), 1);
                expectEquals(voices.front(), 60);
            }

            beginTest("voicesFor drops a duplicate pitch");
            {
                // Two shifts of the same value land on the same note; the second is a second
                // owner of a pitch that already sounds, and ChordShift.h's own comment says it
                // is dropped rather than doubling a voice's velocity onto one MIDI note.
                const auto voices = voicesFor(60, shiftsOf({ 12, 12, 0, 0, 0, 0 }));
                expectEquals((int) voices.size(), 2, "the played note plus one +12, not two");
            }

            beginTest("voicesFor drops a voice that runs off the top of the keyboard");
            {
                const auto voices = voicesFor(127, shiftsOf({ 12, 0, 0, 0, 0, 0 }));
                expectEquals((int) voices.size(), 1,
                             "127 + 12 is off the top and has to be dropped, not clamped onto 127");
                expectEquals(voices.front(), 127, "the played note itself is always kept");
            }

            beginTest("voicesFor drops a voice that runs off the bottom of the keyboard");
            {
                const auto voices = voicesFor(0, shiftsOf({ -12, 0, 0, 0, 0, 0 }));
                expectEquals((int) voices.size(), 1,
                             "0 - 12 is off the bottom and has to be dropped, not clamped onto 0");
                expectEquals(voices.front(), 0);
            }

            beginTest("voicesFor puts the played note first even when a shift lands below it");
            {
                const auto voices = voicesFor(60, shiftsOf({ -12, 0, 0, 0, 0, 0 }));
                expectEquals((int) voices.size(), 2);
                expectEquals(voices.front(), 60, "the played note leads even though the shift is negative");
                expectEquals(voices[1], 48);
            }

            beginTest("voicesFor leads with the played note for every named shape");
            {
                for (const auto& shape : shapes())
                {
                    const auto voices = voicesFor(60, shape.shifts);
                    expect(! voices.empty(), juce::String(shape.name) + " produced no voices at all");
                    expectEquals(voices.front(), 60,
                                 juce::String(shape.name) + " did not lead with the played note");
                }
            }

            beginTest("shapeIndexFor round-trips every entry of shapes()");
            {
                const auto& table = shapes();
                for (int i = 0; i < (int) table.size(); ++i)
                    expectEquals(shapeIndexFor(table[(size_t) i].shifts), i,
                                 juce::String("shape \"") + table[(size_t) i].name + "\" did not round-trip");
            }

            beginTest("shapeIndexFor returns -1 for a set that is none of the shapes");
            {
                // +1 semitone alone is not a row in the table (checked below, so this is not an
                // accident of what the table happens to hold), so it is a safe probe for "none
                // of the above".
                expectEquals(shapeIndexFor(shiftsOf({ 1, 0, 0, 0, 0, 0 })), -1);
            }

            beginTest("no two shapes share the same shifts");
            {
                // shapeIndexFor is an exact-match linear search, so two rows with identical
                // shifts would make it ambiguous which name chipText() and the shape picker
                // land on - and the second of the pair would never be reachable by its own name.
                const auto& table = shapes();
                for (size_t a = 0; a < table.size(); ++a)
                    for (size_t b = a + 1; b < table.size(); ++b)
                        expect(table[a].shifts != table[b].shifts,
                               juce::String(table[a].name) + " and " + table[b].name
                                   + " are the same shifts");
            }

            beginTest("chipText when the feature is off");
            {
                expectEquals(chipText(false, shiftsOf({ 4, 7, 0, 0, 0, 0 })), juce::String("Chord"),
                             "off prints the bare word whatever the shifts say underneath it");
            }

            beginTest("chipText when on but every voice is off");
            {
                expectEquals(chipText(true, shiftsOf({ 0, 0, 0, 0, 0, 0 })), juce::String("Chord"),
                             "on with nothing to name still has to say something rather than look broken");
            }

            beginTest("chipText names a shape");
            {
                expectEquals(chipText(true, shiftsOf({ 4, 7, 0, 0, 0, 0 })), juce::String("Maj"));
            }

            beginTest("chipText counts voices when the shifts spell no shape");
            {
                // +1 and +7 together are not on the table (the "no two shapes share shifts" test
                // above already rules out a coincidental match), so this exercises the fallback:
                // the played note plus those two is three voices.
                expectEquals(chipText(true, shiftsOf({ 1, 7, 0, 0, 0, 0 })), juce::String("3 notes"));
            }

            beginTest("every allChipTexts() caption is non-empty");
            {
                for (const auto& s : allChipTexts())
                    expect(s.isNotEmpty(), "a chip caption was blank");
            }
        }
    };

    static ChordShiftTests chordShiftTests;

    // A NoteSurface with nothing drawn on it: `outputNote` is the identity, so a "drawn id" in
    // these tests is just a MIDI note and the fan-out is the only thing under test. `press` and
    // `release` reach the protected `pressed` set that a real mouse gesture writes, which is the
    // only way to drive refresh() without a live component hierarchy.
    struct BareSurface : keys::NoteSurface
    {
        explicit BareSurface(keys::KeysProcessor& p) : keys::NoteSurface(p) {}
        int outputNote(int drawn) const override { return drawn; }
        // The inverse of outputNote, which the base class leaves as "no such key" (-1) for a
        // surface that draws nothing. recallOutputNotes() maps every note it is handed through
        // this to find the key to latch, so without it a recall silently latches nothing and the
        // edit-session test passes for the wrong reason - it did exactly that first time round.
        int drawnForOutputNote(int note) const override { return note; }
        // No geometry here, so nothing is ever under the mouse. These tests drive the sounding
        // sets directly and never hit-test, which is the point of a surface with nothing drawn.
        int drawnAt(juce::Point<float>) const override { return -1; }

        void press(int note)   { pressed.insert(note); refresh(); }
        void release(int note) { pressed.erase(note);  refresh(); }
        // Recalled keys arrive in `latched`, not `pressed` - recallOutputNotes() latches them so
        // a stored chord keeps ringing with no button held - so letting one go is a different
        // set. Getting this wrong makes the release a no-op and the test pass for the wrong
        // reason, which is worth the second method.
        void unlatch(int note) { latched.erase(note); refresh(); }
    };

    // The fan-out through a real surface and a real processor, rather than through ChordShift.h
    // alone. Everything here is about *when* the shifts apply, which is the half of the feature
    // the pure header cannot see.
    struct KeybedChordSurfaceTests : juce::UnitTest
    {
        KeybedChordSurfaceTests() : juce::UnitTest("Keybed chord surface", "keys") {}

        struct Harness
        {
            juce::ScopedJuceInitialiser_GUI juceInit;
            keys::KeysProcessor processor;
        };

        static void setShifts(keys::KeysProcessor& p, std::initializer_list<int> vals)
        {
            p.apvts.getParameter("keyChord")->setValueNotifyingHost(1.0f);
            int i = 1;
            for (const int v : vals)
            {
                auto* param = p.apvts.getParameter("keyChordShift" + juce::String(i++));
                param->setValueNotifyingHost(param->convertTo0to1((float) v));
            }
        }

        void runTest() override
        {
            beginTest("a key press fans out into the shifted voices");
            {
                Harness h;
                setShifts(h.processor, { 4, 7 });
                BareSurface s { h.processor };
                s.press(60);
                const auto notes = s.soundingOutputNotes();
                expectEquals((int) notes.size(), 3, "C plus its third and fifth");
                expect(std::find(notes.begin(), notes.end(), 64) != notes.end(), "the +4 voice");
                expect(std::find(notes.begin(), notes.end(), 67) != notes.end(), "the +7 voice");
            }

            beginTest("releasing a key releases every voice it opened, not what the shifts say now");
            {
                // The regression this is really for: the release list is stored at press time.
                // Moving a shift while the key is still held must not change which notes the
                // release turns off, or whichever voice it forgets is stranded sounding with
                // nothing left to release it.
                Harness h;
                setShifts(h.processor, { 4, 7 });
                BareSurface s { h.processor };
                s.press(60);
                setShifts(h.processor, { 1, 2 }); // moved mid-hold
                s.release(60);
                expect(s.soundingOutputNotes().empty(), "a voice was left ringing after release");
            }

            beginTest("an edit session ends when the surface falls silent");
            {
                // recallOutputNotes() suppresses the fan-out so that editing a stored chord
                // cannot thicken it. The bug this pins: the suppression used to be cleared only
                // by panic(), and the *other* recall path - dragging a pad onto the live chord
                // card - never panics, so one such drag left the keybed silently unable to make
                // a chord for the rest of the session.
                Harness h;
                setShifts(h.processor, { 4, 7 });
                BareSurface s { h.processor };

                s.recallOutputNotes({ 60, 63, 67 });
                expectEquals((int) s.soundingOutputNotes().size(), 3,
                             "a recalled chord must come back exactly as stored");

                // Let everything go the way releasing the recalled keys does - no panic() in
                // sight, which is the whole point.
                for (const int n : { 60, 63, 67 })
                    s.unlatch(n);
                expect(s.soundingOutputNotes().empty(), "the recall did not clear");

                s.press(72);
                expectEquals((int) s.soundingOutputNotes().size(), 3,
                             "the next key after an edit must chord again");
            }
        }
    };

    static KeybedChordSurfaceTests keybedChordSurfaceTests;
} // namespace
