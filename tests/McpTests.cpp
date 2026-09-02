// The MCP tool layer, which had no test coverage at all until 2026-08-26.
//
// It went untested for the usual reason: every tool is a thin lambda over a KeysProcessor
// method, so the interesting behaviour looked like it lived elsewhere. The afternoon that
// produced this file argues otherwise. A script built a whole patch into Keys and heard
// nothing, three times over, because `press_chord_pad` cannot feed an arpeggiator line -
// correct by design, undocumented, and indistinguishable from success, since every call
// returned a result and every value read back exactly as written. What was missing was not
// a working processor; it was a tool that says what it did, and a test pinning which
// gestures reach a line and which do not.
//
// So these tests are mostly about the seam rather than the arithmetic: argument validation,
// which line a call lands on, and above all the pad-versus-hold distinction, which is easy
// to "fix" into feeding lines and must not be.
//
// Tools are exercised by calling `run` directly, through the bridge the processor already
// owns (`processor.mcp()`), so no socket, client or second server is involved.

#include "../src/PluginProcessor.h"
#include "../src/mcp/KeysMcp.h"
#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>
#include <algorithm>

namespace keys::tests
{
namespace
{
    // Same shape as StateTests' Host, and for the same reason: the heartbeat timer and the
    // MCP bridge both want a message loop to exist before the processor is built.
    struct Host
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        KeysProcessor processor;
    };

    // One JSON-RPC round trip against the bridge the processor already owns. `error` comes
    // back empty on success and carries the tool's own message otherwise.
    //
    // This goes through the server rather than calling a tool's `run` directly, and the
    // difference is the point: the schema, the registration and the JSON-RPC layer are all
    // exercised on the way past, so a tool with malformed params, a duplicate name addTool
    // refused, or a tool that never reached the table fails here. Calling `run` could not
    // notice any of those. It is also why a renamed tool now fails one test instead of
    // aborting the binary: the old helper jassert'd a null std::function and then called it,
    // and jassert compiles out in Release, which is how Keys_tests is built.
    juce::var call(KeysProcessor& p, const juce::String& tool, const juce::var& toolArgs,
                   juce::String& error)
    {
        error.clear();

        auto* params = new juce::DynamicObject();
        params->setProperty("name", tool);
        params->setProperty("arguments", toolArgs);
        auto* req = new juce::DynamicObject();
        req->setProperty("jsonrpc", "2.0");
        req->setProperty("id", 1);
        req->setProperty("method", "tools/call");
        req->setProperty("params", juce::var(params));

        const auto responseLine = p.mcp()->handleLine(juce::JSON::toString(juce::var(req), true));
        const auto response = juce::JSON::parse(responseLine);

        if (response.hasProperty("error"))
        {
            error = response["error"]["message"].toString();
            if (error.isEmpty())
                error = "JSON-RPC error with no message: " + responseLine;
            return {};
        }

        // A tools/call result is a single text block holding the tool's own JSON.
        auto* content = response["result"]["content"].getArray();
        if (content == nullptr || content->isEmpty())
        {
            error = "malformed tools/call response: " + responseLine;
            return {};
        }
        return juce::JSON::parse((*content)[0]["text"].toString());
    }

    // tools/list, for the tests that want the registered table rather than one tool's answer.
    juce::var toolTable(KeysProcessor& p)
    {
        auto* req = new juce::DynamicObject();
        req->setProperty("jsonrpc", "2.0");
        req->setProperty("id", 1);
        req->setProperty("method", "tools/list");
        const auto response = juce::JSON::parse(p.mcp()->handleLine(juce::JSON::toString(juce::var(req), true)));
        return response["result"]["tools"];
    }

    juce::var call(KeysProcessor& p, const juce::String& tool, const juce::var& toolArgs)
    {
        juce::String ignored;
        return call(p, tool, toolArgs, ignored);
    }

    juce::var args(std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto* o = new juce::DynamicObject();
        for (const auto& entry : kv)
            o->setProperty(entry.first, entry.second);
        return juce::var(o);
    }

    juce::var noteArray(std::initializer_list<int> notes)
    {
        juce::Array<juce::var> a;
        for (int n : notes)
            a.add(n);
        return juce::var(a);
    }

    // `count` copies of `value`, for the set_arp_pattern oversized-array test: the interesting
    // property is the length, not what any one step holds.
    juce::var filledIntArray(int count, int value)
    {
        juce::Array<juce::var> a;
        for (int i = 0; i < count; ++i)
            a.add(value);
        return juce::var(a);
    }

    // One play_sequence step: {note, startMs, durationMs}.
    juce::var seqStep(int note, double startMs, double durationMs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("note", note);
        o->setProperty("startMs", startMs);
        o->setProperty("durationMs", durationMs);
        return juce::var(o);
    }

    juce::var varArray(std::initializer_list<juce::var> items)
    {
        juce::Array<juce::var> a;
        for (auto& v : items)
            a.add(v);
        return juce::var(a);
    }

    int lowestHeld(const std::vector<int>& v)
    {
        return v.empty() ? -1 : *std::min_element(v.begin(), v.end());
    }

    // arpLines[0] out of a get_state reply, or a void var when it is not there. Returned by
    // value on purpose: juce::UnitTest::expect records a failure and *returns*, it does not
    // abort, so `expect(ptr != nullptr, ...)` followed by a deref turns the one regression
    // these tests exist to catch - a field being renamed or dropped - into a segfault that
    // takes the whole binary down having printed nothing. Handing back a var means a missing
    // field reads as void and asserts as a failure.
    juce::var firstArpLine(const juce::var& state)
    {
        if (auto* lines = state["arpLines"].getArray())
            if (! lines->isEmpty())
                return (*lines)[0];
        return {};
    }

    // Set line A up, send Am7 at the *track* input, and run the engine. Shared by the two
    // arpKeys tests below so they differ in exactly one parameter: with two moving, a failure
    // in the second does not uniquely implicate arpKeys. arpRate is set in both rather than
    // left to its default, so the window below stays what this comment says it is even if the
    // default moves.
    void driveTrackMidi(KeysProcessor& p, bool arpKeysOn)
    {
        p.prepareToPlay(44100.0, 512);
        // arpTrackMidi is the door the *track's* MIDI comes through, and it is off by default
        // since 2026-08-27; these tests inject at the track input, so they open it explicitly.
        // arpKeys is still the per-line switch under test.
        call(p, "set_params", args({ { "values", args({
            { "arpOn", true }, { "arpPattern", true }, { "arpRate", "1/16" },
            { "arpTrackMidi", true }, { "arpKeys", arpKeysOn } }) } }));

        const int chans = juce::jmax(1, p.getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf (chans, 512);

        juce::MidiBuffer midi;
        for (int n : { 45, 48, 52, 55 })              // Am7 arriving at the track input
            midi.addEvent(juce::MidiMessage::noteOn(1, n, 0.8f), 0);
        buf.clear();
        p.processBlock(buf, midi);

        // 64 blocks of 512 at 44100 is about 740 ms, against a 1/16 of 125 ms at the 120 bpm
        // fallback. Deliberately not a tight window: `sequence` is published only from
        // fireStep, and process() clears it on any block where no step is eligible, so a test
        // that only just reaches its first step would report a timing regression as a missing
        // field. The caller asserts the playhead moved, so if it ever does go quiet the
        // failure says which of the two it was.
        for (int i = 0; i < 64; ++i)
        {
            juce::MidiBuffer none;
            buf.clear();
            p.processBlock(buf, none);
        }
    }
} // namespace

class McpTests : public juce::UnitTest
{
public:
    McpTests() : juce::UnitTest("MCP tools", "mcp") {}

    void runTest() override
    {
        beginTest("every registered tool has a name, a description and a usable schema");
        {
            Host h;
            // The table as the *server* holds it, not as buildTools() returns it: a tool that
            // never reached addTool, or one addTool refused, is invisible to the second and
            // caught by the first.
            // The var is held in a named local on purpose: it owns the array, so
            // `toolTable(...).getArray()` would hand back a pointer into a temporary that is
            // already gone by the time the loop reads it.
            const auto table = toolTable(h.processor);
            auto* tools = table.getArray();
            if (tools == nullptr)
            {
                expect(false, "tools/list returned no array");
                return;
            }
            expect(tools->size() >= 18, "the table lost tools");
            juce::StringArray names;
            for (auto& t : *tools)
            {
                const auto name = t["name"].toString();
                expect(name.isNotEmpty(), "a tool has no name");
                expect(t["description"].toString().isNotEmpty(), "tool has no description: " + name);
                expect(! names.contains(name), "duplicate tool name: " + name);
                names.add(name);

                // Every param must declare a type, or a client cannot build a call at all.
                // Only reachable through the schema, which is why the old table test could
                // not see it.
                auto schema = t["inputSchema"];
                expect(schema["type"].toString() == "object", "tool has no object schema: " + name);
                if (auto* props = schema["properties"].getDynamicObject())
                    for (const auto& param : props->getProperties())
                    {
                        const auto where = name + "." + param.name.toString();
                        expect(param.value["type"].toString().isNotEmpty(), "param has no type: " + where);
                        expect(param.value["description"].toString().isNotEmpty(),
                               "param has no description: " + where);
                    }
            }
            // Named explicitly: docs/MCP.md documents these two, and a rename would leave
            // every script written against the docs failing with "unknown tool".
            expect(names.contains("hold_arp_chord"));
            expect(names.contains("release_arp_chord"));
        }

        beginTest("an unknown tool is an error, not a crash");
        {
            Host h;
            juce::String err;
            call(h.processor, "no_such_tool", args({}), err);
            expect(err.isNotEmpty(), "an unknown tool reported no error");
        }

        beginTest("hold_arp_chord holds the notes it was given, and reports them back");
        {
            Host h;
            juce::String err;
            auto r = call(h.processor, "hold_arp_chord",
                          args({ { "notes", noteArray({ 45, 48, 52, 55 }) }, { "name", "Am7" } }), err);
            expect(err.isEmpty(), "unexpected error: " + err);
            expectEquals((int) r["line"], 0);
            expectEquals(r["name"].toString(), juce::String("Am7"));
            expectEquals((int) h.processor.arpHeldNotes(0).size(), 4);
            expectEquals(lowestHeld(h.processor.arpHeldNotes(0)), 45);
            expectEquals(h.processor.arpHeldName(0), juce::String("Am7"));
        }

        beginTest("the line argument lands on that line and no other");
        {
            Host h;
            call(h.processor, "hold_arp_chord",
                 args({ { "notes", noteArray({ 60, 64, 67 }) }, { "name", "C" }, { "line", 1 } }));
            expect(h.processor.arpHeldNotes(0).empty(), "line A picked up line B's chord");
            expectEquals((int) h.processor.arpHeldNotes(1).size(), 3);
            expectEquals(h.processor.arpHeldName(1), juce::String("C"));
        }

        beginTest("a second hold on one line swaps the chord rather than stacking it");
        {
            Host h;
            call(h.processor, "hold_arp_chord", args({ { "notes", noteArray({ 45, 48, 52, 55 }) } }));
            call(h.processor, "hold_arp_chord",
                 args({ { "notes", noteArray({ 62, 65 }) }, { "name", "Dm" } }));
            expectEquals((int) h.processor.arpHeldNotes(0).size(), 2);
            expectEquals(h.processor.arpHeldName(0), juce::String("Dm"));
        }

        beginTest("hold_arp_chord from a padSlot takes the pad's chord and marks the pad");
        {
            Host h;
            call(h.processor, "set_chord_pad",
                 args({ { "slot", 0 }, { "notes", noteArray({ 45, 48, 52 }) }, { "name", "Am" } }));
            juce::String err;
            auto r = call(h.processor, "hold_arp_chord", args({ { "padSlot", 0 }, { "line", 1 } }), err);
            expect(err.isEmpty(), "unexpected error: " + err);
            expectEquals((int) h.processor.arpHeldNotes(1).size(), 3);
            expectEquals(h.processor.arpHeldName(1), juce::String("Am"));
            expectEquals(h.processor.arpHeldPad(1), 0);
            expectEquals((int) r["heldPad"], 0);
        }

        beginTest("hold_arp_chord rejects both notes and padSlot, and neither");
        {
            Host h;
            call(h.processor, "set_chord_pad",
                 args({ { "slot", 0 }, { "notes", noteArray({ 45, 48, 52 }) }, { "name", "Am" } }));
            juce::String err;
            call(h.processor, "hold_arp_chord",
                 args({ { "notes", noteArray({ 45, 48 }) }, { "padSlot", 0 } }), err);
            expect(err.isNotEmpty(), "both notes and padSlot was accepted");
            call(h.processor, "hold_arp_chord", args({ { "line", 0 } }), err);
            expect(err.isNotEmpty(), "neither notes nor padSlot was accepted");
            expect(h.processor.arpHeldNotes(0).empty(), "a rejected call still held something");
        }

        beginTest("hold_arp_chord rejects a bad note, an out-of-range pad and an empty pad");
        {
            Host h;
            juce::String err;
            call(h.processor, "hold_arp_chord", args({ { "notes", noteArray({ 45, 200 }) } }), err);
            expect(err.isNotEmpty(), "note 200 was accepted");
            call(h.processor, "hold_arp_chord", args({ { "notes", noteArray({}) } }), err);
            expect(err.isNotEmpty(), "an empty notes array was accepted");
            call(h.processor, "hold_arp_chord", args({ { "padSlot", 9999 } }), err);
            expect(err.isNotEmpty(), "padSlot 9999 was accepted");
            call(h.processor, "hold_arp_chord", args({ { "padSlot", 5 } }), err);
            expect(err.isNotEmpty(), "an empty pad slot was accepted as a chord");
            expect(h.processor.arpHeldNotes(0).empty(), "a rejected call still held something");
        }

        beginTest("release_arp_chord lets one line go and leaves the others holding");
        {
            Host h;
            call(h.processor, "hold_arp_chord",
                 args({ { "notes", noteArray({ 45, 48 }) }, { "line", 0 } }));
            call(h.processor, "hold_arp_chord",
                 args({ { "notes", noteArray({ 60, 64 }) }, { "line", 1 } }));
            call(h.processor, "release_arp_chord", args({ { "line", 0 } }));
            expect(h.processor.arpHeldNotes(0).empty(), "line A did not let go");
            expect(h.processor.arpHeldName(0).isEmpty(), "line A kept its chord name");
            expectEquals((int) h.processor.arpHeldNotes(1).size(), 2);
        }

        // The bug this pins: release_arp_chord called releaseArpChord(), which
        // PluginProcessor.h documents at its own declaration as NOT what "let go" means -
        // it drops the chord and leaves chainOn set, so heartbeatTick() launches the next
        // slot at the following bar boundary and the chord is back. A release that undoes
        // itself a bar later reads as a tool that did nothing.
        beginTest("release_arp_chord stops the chain, so the chord cannot come back");
        {
            Host h;
            // startChain walks the slots that hold a *chord* and does not stick on for an
            // empty row, so the chain needs somewhere to go before there is anything to stop.
            h.processor.setArpSlotChord(0, { 45, 48, 52, 55 }, "Am7", 0);
            h.processor.setArpSlotChord(1, { 43, 47, 50, 53 }, "G7", 0);
            h.processor.startChain(0);
            expect(h.processor.chainRunning(0), "the chain did not start");
            call(h.processor, "release_arp_chord", args({ { "line", 0 } }));
            expect(! h.processor.chainRunning(0),
                   "release_arp_chord left the chain running, so the next bar hands the line "
                   "another chord");
            expect(h.processor.arpHeldNotes(0).empty(), "line A did not let go");
        }

        // Every other out-of-range argument in these two handlers is a hard error; `line`
        // was silently clamped, so line 7 landed on D and reported success. That is the
        // silent-wrong-target failure the whole tool exists to end.
        beginTest("an out-of-range line is an error, not a clamp onto the wrong arpeggiator");
        {
            Host h;
            juce::String err;
            call(h.processor, "hold_arp_chord",
                 args({ { "notes", noteArray({ 45, 48 }) }, { "line", KeysProcessor::numArpLines } }), err);
            expect(err.isNotEmpty(), "hold_arp_chord accepted a line past the last one");
            for (int n = 0; n < KeysProcessor::numArpLines; ++n)
                expect(h.processor.arpHeldNotes(n).empty(),
                       "a rejected hold still reached line " + juce::String(n));

            call(h.processor, "hold_arp_chord",
                 args({ { "notes", noteArray({ 45, 48 }) }, { "line", -1 } }), err);
            expect(err.isNotEmpty(), "hold_arp_chord accepted line -1");

            call(h.processor, "release_arp_chord", args({ { "line", KeysProcessor::numArpLines } }), err);
            expect(err.isNotEmpty(), "release_arp_chord accepted a line past the last one");
        }

        // A line that is off still takes a chord in, by design, so a hold that worked and a
        // line that will never sound are indistinguishable without this field.
        // The same rule across every tool that takes a line, not just the two that got it
        // first. A clamp is the validation failure nobody can see: the call lands on D and
        // reports success, so a typo is a chord on the wrong arpeggiator with nothing to say
        // so. Swept rather than spot-checked, because the failure mode here is a tool being
        // *missed* when the rule was applied, and a list of names cannot notice its own gaps.
        beginTest("every tool taking a line rejects one that does not exist");
        {
            Host h;
            const juce::StringArray takesLine {
                "hold_arp_chord", "release_arp_chord", "get_arp_pattern", "set_arp_pattern",
                "recall_arp_pattern", "store_arp_pattern", "apply_euclid" };

            // Enough of each tool's other required arguments to get past them to the line.
            auto extras = [](const juce::String& tool) -> juce::var
            {
                if (tool == "hold_arp_chord")     return args({ { "notes", noteArray({ 60, 64 }) } });
                if (tool == "recall_arp_pattern") return args({ { "slot", 0 } });
                if (tool == "apply_euclid")       return args({ { "hits", 3 }, { "steps", 8 } });
                if (tool == "set_arp_pattern")    return args({ { "note", noteArray({ 1, 2, 3, 4 }) } });
                return args({});
            };

            for (const auto& tool : takesLine)
                for (int bad : { -1, KeysProcessor::numArpLines, KeysProcessor::numArpLines + 4 })
                {
                    auto a = extras(tool);
                    a.getDynamicObject()->setProperty("line", bad);
                    juce::String err;
                    call(h.processor, tool, a, err);
                    // The message is checked, not merely its presence: with a tool that
                    // rejects something *else* first, "an error came back" passes while the
                    // line goes unvalidated, which is the vacuous pass this sweep exists to
                    // avoid. set_arp_pattern did exactly that until its payload was filled in.
                    expect(err.contains("line"), tool + " did not reject line "
                                                     + juce::String(bad) + ", it said: " + err);
                }

            // The other half: a line that does exist still works, so the guard above is a
            // guard and not a tool that stopped accepting its argument.
            for (const auto& tool : takesLine)
            {
                auto a = extras(tool);
                a.getDynamicObject()->setProperty("line", KeysProcessor::numArpLines - 1);
                juce::String err;
                call(h.processor, tool, a, err);
                expect(err.isEmpty(), tool + " rejected the last real line: " + err);
            }

            // allLines documents `line` as ignored, so it must not be validated there.
            juce::String err;
            call(h.processor, "release_arp_chord",
                 args({ { "allLines", true }, { "line", 99 } }), err);
            expect(err.isEmpty(), "allLines validated a line it documents as ignored: " + err);
        }

        // Every one of these said "0..1 (A, B)" while Keys has had four lines since
        // 2026-08-19, so a client reading the schema could not know C and D existed.
        beginTest("the line parameter's description names every line that exists");
        {
            Host h;
            const auto table = toolTable(h.processor);
            auto* tools = table.getArray();
            if (tools == nullptr)
            {
                expect(false, "tools/list returned no array");
                return;
            }
            const auto last = juce::String(KeysProcessor::numArpLines - 1);
            int seen = 0;
            for (auto& t : *tools)
            {
                auto line = t["inputSchema"]["properties"]["line"];
                if (! line.isObject())
                    continue;
                ++seen;
                expect(line["description"].toString().contains("0.." + last),
                       t["name"].toString() + " describes line as: " + line["description"].toString());
            }
            expect(seen >= 7, "no tool declared a line parameter, so this swept nothing");
        }

        beginTest("hold_arp_chord reports whether the line it landed on is actually running");
        {
            Host h;
            juce::String err;
            auto off = call(h.processor, "hold_arp_chord",
                            args({ { "notes", noteArray({ 45, 48 }) }, { "line", 1 } }), err);
            expect(err.isEmpty(), "unexpected error: " + err);
            expect(off.hasProperty("lineOn"), "the reply does not carry lineOn");
            expect(! (bool) off["lineOn"], "line B defaults off and the reply said otherwise");

            h.processor.apvts.getParameter("arp2On")->setValueNotifyingHost(1.0f);
            auto on = call(h.processor, "hold_arp_chord",
                           args({ { "notes", noteArray({ 45, 48 }) }, { "line", 1 } }), err);
            expect((bool) on["lineOn"], "line B was switched on and the reply said otherwise");
        }

        beginTest("release_arp_chord allLines lets every line go");
        {
            Host h;
            call(h.processor, "hold_arp_chord",
                 args({ { "notes", noteArray({ 45, 48 }) }, { "line", 0 } }));
            call(h.processor, "hold_arp_chord",
                 args({ { "notes", noteArray({ 60, 64 }) }, { "line", 1 } }));
            call(h.processor, "release_arp_chord", args({ { "allLines", true } }));
            for (int n = 0; n < KeysProcessor::numArpLines; ++n)
                expect(h.processor.arpHeldNotes(n).empty(), "line " + juce::String(n) + " still holding");
            expect(! h.processor.anyArpHold(), "anyArpHold still true after allLines");
        }

        // The regression this whole file was written for. A pad is a chord you are *playing*;
        // a line's held chord is the input to a machine. pressChordPad fires with asChord
        // true precisely so a listening line cannot lift it. If someone "fixes" that, a
        // script's silence comes back and hold_arp_chord loses its reason to exist.
        beginTest("press_chord_pad does NOT hand its chord to a line (by design)");
        {
            Host h;
            call(h.processor, "set_chord_pad",
                 args({ { "slot", 0 }, { "notes", noteArray({ 45, 48, 52, 55 }) }, { "name", "Am7" } }));
            call(h.processor, "press_chord_pad", args({ { "slot", 0 } }));
            for (int n = 0; n < KeysProcessor::numArpLines; ++n)
            {
                expect(h.processor.arpHeldNotes(n).empty(),
                       "a pad press reached line " + juce::String(n) + " held chord");
                expect(h.processor.arpHeldName(n).isEmpty(),
                       "a pad press named line " + juce::String(n) + " held chord");
            }
        }

        // Same distinction from the other side: play_notes is what a *line* lifts, and it
        // deliberately leaves heldChord empty, so an empty heldChord on a line running off
        // play_notes is not evidence of failure. docs/MCP.md says so; this pins it.
        beginTest("play_notes does not populate a line's held chord either");
        {
            Host h;
            call(h.processor, "play_notes",
                 args({ { "notes", noteArray({ 45, 48, 52, 55 }) }, { "durationMs", 5000 } }));
            expect(h.processor.arpHeldNotes(0).empty(),
                   "play_notes populated the handed-chord field");
        }

        // WHAT A LINE IS SOUNDING, as opposed to what was handed to it. This pair is the
        // regression from an afternoon spent hunting a Keys instance that played notes while
        // every field get_state offered read as empty: heldChord blank, chord lane 0, no
        // chain, no launched slot. The notes were arriving at the *track's MIDI input* and
        // arpKeys was feeding them straight to the arp, which nothing reported. The only way
        // to find it was to mute lines one at a time and ask a human what stopped.
        //
        // These drive processBlock directly because uiSeq is published by the audio thread,
        // and they inject MIDI the way the mystery source did rather than via play_notes.
        beginTest("get_state reports notes a line sounds from track MIDI, not just handed chords");
        {
            Host h;
            driveTrackMidi(h.processor, true);

            // The precondition the rest of this test rests on, asserted rather than assumed:
            // `sequence` only exists once a step has fired.
            expect(h.processor.arpLine(0).uiRelStep.load() >= 0,
                   "no step fired in the window, so sequence has nothing to report yet");

            auto st = call(h.processor, "get_state", args({}));
            auto lineA = firstArpLine(st);
            if (! lineA.isObject())
            {
                expect(false, "arpLines missing from get_state");
                return;
            }

            expect(lineA.hasProperty("soundingNoteCount"),
                   "get_state stopped reporting soundingNoteCount");
            expect((int) lineA["soundingNoteCount"] >= 4,
                   "soundingNoteCount did not report the four notes the engine is holding, it said "
                       + lineA["soundingNoteCount"].toString());

            auto* seq = lineA["sequence"].getArray();
            expect(seq != nullptr && seq->size() >= 4,
                   "sequence did not report the pitches the Note lane's indices name");

            // The point of the pair: the handed-chord field is legitimately empty here, and
            // on its own it says the line is holding nothing while it demonstrably is.
            expect(lineA["heldChord"].toString().isEmpty(),
                   "played notes should not populate heldChord");

            // The sequence is the held chord, so every pitch in it must be one we sent (or an
            // octave-stacked copy of one), not an arbitrary note. Guarded rather than
            // dereferenced straight after the expect above, for the reason firstArpLine gives.
            if (seq != nullptr)
                for (auto& v : *seq)
                {
                    const int pitch = (int) v;
                    bool derived = false;
                    for (int n : { 45, 48, 52, 55 })
                        for (int oct = 0; oct <= 3; ++oct)
                            if (pitch == n + 12 * oct)
                                derived = true;
                    expect(derived, "sequence holds a pitch not derived from the held chord: "
                                        + juce::String(pitch));
                }
        }

        // The other half, and the actual fix for the afternoon above: arpKeys is the door.
        // With it shut, the same track MIDI must not reach the line at all. Identical setup
        // apart from that one parameter, so a failure here can mean nothing else.
        beginTest("with arpKeys off, track MIDI does not reach the line");
        {
            Host h;
            driveTrackMidi(h.processor, false);

            auto st = call(h.processor, "get_state", args({}));
            auto lineA = firstArpLine(st);
            if (! lineA.isObject())
            {
                expect(false, "arpLines missing from get_state");
                return;
            }

            // Asserted present before it is read: a missing juce::var casts to 0, so without
            // this the test would go green just as happily if the field were deleted.
            expect(lineA.hasProperty("soundingNoteCount"),
                   "get_state stopped reporting soundingNoteCount");
            expectEquals((int) lineA["soundingNoteCount"], 0,
                         "arpKeys was off and the line still picked up track MIDI");
        }

        beginTest("with Launch Quantize on, the hold waits and the reply says so");
        {
            Host h;
            h.processor.apvts.getParameter("arpQuantize")->setValueNotifyingHost(1.0f);
            expect(h.processor.arpQuantizeOn(), "arpQuantize did not take");
            juce::String err;
            auto r = call(h.processor, "hold_arp_chord",
                          args({ { "notes", noteArray({ 45, 48, 52, 55 }) }, { "name", "Am7" } }), err);
            expect(err.isEmpty(), "unexpected error: " + err);
            expect((bool) r["waitingForQuantize"],
                   "a deferred hold did not report waitingForQuantize");
            // Deferred means not yet sounding. The distinction this reports is the whole
            // point: an empty hold here is "on its way", not "the call failed".
            expect(h.processor.arpHeldNotes(0).empty(), "a quantized hold landed immediately");
        }

        beginTest("with Launch Quantize off, the hold is immediate and says it is not waiting");
        {
            Host h;
            expect(! h.processor.arpQuantizeOn(), "arpQuantize defaults on?");
            auto r = call(h.processor, "hold_arp_chord",
                          args({ { "notes", noteArray({ 45, 48, 52, 55 }) } }));
            expect(! (bool) r["waitingForQuantize"], "an immediate hold claimed to be waiting");
            expectEquals((int) h.processor.arpHeldNotes(0).size(), 4);
        }

        // set_arp_pattern is a thin argument-and-clamp layer over the live lanes or a stored
        // slot; get_arp_pattern is its own separate read path. Round-tripping through both is
        // the only way to pin that a write actually lands rather than merely returning success.
        beginTest("set_arp_pattern clamps a lane's values to its documented range and "
                  "get_arp_pattern reads them back");
        {
            Host h;
            juce::String err;
            auto r = call(h.processor, "set_arp_pattern",
                          args({ { "velocity", noteArray({ 10, 50, 300, -5 }) } }), err);
            expect(err.isEmpty(), "unexpected error: " + err);
            expectEquals((int) r["lanesSet"], 1);

            auto pat = call(h.processor, "get_arp_pattern", args({}));
            auto* velocity = pat["velocity"].getArray();
            if (velocity == nullptr || velocity->size() != 4)
            {
                expect(false, "velocity lane did not come back at length 4");
                return;
            }
            // ArpEngine::laneRanges clamps Velocity to 10..200; 300 and -5 are outside it.
            const int expected[4] = { 10, 50, 200, 10 };
            for (int i = 0; i < 4; ++i)
                expectEquals((int) (*velocity)[i], expected[i],
                             "velocity step " + juce::String(i) + " was not clamped as documented");
            expectEquals((int) pat["lengths"]["velocity"], 4);
        }

        // A slot write must not disturb the live lanes it did not touch, and what it wrote
        // must come back exactly through get_arp_pattern's own slot branch.
        beginTest("set_arp_pattern writes a stored slot without touching the live lanes");
        {
            Host h;
            call(h.processor, "set_arp_pattern", args({ { "note", noteArray({ 1, 2, 3 }) } }));

            juce::String err;
            auto r = call(h.processor, "set_arp_pattern",
                          args({ { "slot", 3 }, { "note", noteArray({ 4, 5, 6, 7 }) } }), err);
            expect(err.isEmpty(), "unexpected error: " + err);
            expectEquals((int) r["slot"], 3);

            auto stored = call(h.processor, "get_arp_pattern", args({ { "slot", 3 } }));
            auto* storedNote = stored["note"].getArray();
            if (storedNote == nullptr || storedNote->size() != 4)
            {
                expect(false, "the stored slot's note lane did not come back at length 4");
                return;
            }
            for (int i = 0; i < 4; ++i)
                expectEquals((int) (*storedNote)[i], i + 4, "stored note step " + juce::String(i));

            // Slot 3 is not line A's active pattern (default 0), so this write must not reach
            // the live lanes captured above.
            auto live = call(h.processor, "get_arp_pattern", args({}));
            auto* liveNote = live["note"].getArray();
            if (liveNote == nullptr || liveNote->size() != 3)
            {
                expect(false, "a slot write changed the live note lane's length");
                return;
            }
            for (int i = 0; i < 3; ++i)
                expectEquals((int) (*liveNote)[i], i + 1, "live note step " + juce::String(i));
        }

        beginTest("set_arp_pattern rejects an oversized lane array and an out-of-range slot");
        {
            Host h;
            juce::String err;
            call(h.processor, "set_arp_pattern",
                 args({ { "note", filledIntArray(ArpEngine::maxSteps + 1, 1) } }), err);
            expect(err.isNotEmpty(), "a " + juce::String(ArpEngine::maxSteps + 1)
                                          + "-element lane array was accepted");
            expect(err.contains("note"), "the oversized-array error did not name the lane: " + err);

            call(h.processor, "set_arp_pattern",
                 args({ { "note", noteArray({ 1, 2 }) }, { "slot", KeysProcessor::numArpPatterns } }),
                 err);
            expect(err.isNotEmpty(), "a slot past the last pattern was accepted");
            expect(err.contains("slot"), "the out-of-range slot error did not say 'slot': " + err);
        }

        // Bjorklund's algorithm for E(3, 8) is the tresillo: hits at steps 0, 3 and 6. Read
        // straight off euclidHit's own formula in EuclidGen.h rather than guessed, since
        // apply_euclid's whole job is writing exactly what that formula says.
        beginTest("apply_euclid writes the exact Bjorklund pattern into the probability lane");
        {
            Host h;
            juce::String err;
            auto r = call(h.processor, "apply_euclid",
                          args({ { "hits", 3 }, { "steps", 8 }, { "rotation", 0 } }), err);
            expect(err.isEmpty(), "unexpected error: " + err);
            expectEquals((int) r["hits"], 3);
            expectEquals((int) r["steps"], 8);
            expectEquals((int) r["rotation"], 0);

            auto pat = call(h.processor, "get_arp_pattern", args({}));
            auto* probability = pat["probability"].getArray();
            if (probability == nullptr || probability->size() != 8)
            {
                expect(false, "the probability lane did not come back at length 8");
                return;
            }
            const int expected[8] = { 100, 0, 0, 100, 0, 0, 100, 0 };
            for (int i = 0; i < 8; ++i)
                expectEquals((int) (*probability)[i], expected[i],
                             "step " + juce::String(i) + " did not match E(3, 8)");
            expectEquals((int) pat["lengths"]["probability"], 8);
        }

        beginTest("apply_euclid rejects steps outside 1..maxSteps");
        {
            Host h;
            juce::String err;
            call(h.processor, "apply_euclid",
                 args({ { "hits", 3 }, { "steps", ArpEngine::maxSteps + 1 } }), err);
            expect(err.isNotEmpty(), "steps past maxSteps was accepted");
            call(h.processor, "apply_euclid", args({ { "hits", 3 }, { "steps", 0 } }), err);
            expect(err.isNotEmpty(), "steps of 0 was accepted");
        }

        // play_sequence's only observable effect on note-on/off runs through KeysMcp's own
        // 5ms poll Timer, which nothing in this suite advances (no test here pumps the JUCE
        // message loop, and a real-time wait would make this test flaky under CI). So this
        // pins the tool's argument validation and its immediate reply instead, not whether a
        // note actually sounds later - see toolPlaySequence in KeysMcp.cpp for the full
        // contract that leaves untested.
        beginTest("play_sequence validates its steps and reports what it scheduled");
        {
            Host h;
            juce::String err;

            call(h.processor, "play_sequence", args({ { "steps", varArray({}) } }), err);
            expect(err.isNotEmpty(), "an empty steps array was accepted");

            juce::Array<juce::var> tooMany;
            for (int i = 0; i < 257; ++i)
                tooMany.add(seqStep(60, 0.0, 10.0));
            call(h.processor, "play_sequence", args({ { "steps", juce::var(tooMany) } }), err);
            expect(err.isNotEmpty(), "257 steps was accepted (max 256)");

            call(h.processor, "play_sequence", args({ { "steps", varArray({ 60 }) } }), err);
            expect(err.isNotEmpty(), "a step that is not an object was accepted");

            call(h.processor, "play_sequence",
                 args({ { "steps", varArray({ seqStep(200, 0.0, 10.0) }) } }), err);
            expect(err.isNotEmpty(), "note 200 was accepted");

            call(h.processor, "play_sequence",
                 args({ { "steps", varArray({ seqStep(60, -1.0, 10.0) }) } }), err);
            expect(err.isNotEmpty(), "a negative startMs was accepted");

            call(h.processor, "play_sequence",
                 args({ { "steps", varArray({ seqStep(60, 0.0, 0.0) }) } }), err);
            expect(err.isNotEmpty(), "a zero durationMs was accepted");

            call(h.processor, "play_sequence",
                 args({ { "steps", varArray({ seqStep(60, 119999.0, 2000.0) }) } }), err);
            expect(err.isNotEmpty(), "a sequence horizon past 120000ms was accepted");

            auto r = call(h.processor, "play_sequence",
                          args({ { "steps", varArray({ seqStep(60, 0.0, 500.0),
                                                        seqStep(64, 250.0, 500.0) }) } }),
                          err);
            expect(err.isEmpty(), "unexpected error: " + err);
            expectEquals((int) r["scheduled"], 2);
            expectWithinAbsoluteError((double) r["horizonMs"], 750.0, 1.0e-6,
                                      "horizonMs did not report max(start + duration) across the steps");
        }
    }
};

static McpTests mcpTests;
} // namespace keys::tests
