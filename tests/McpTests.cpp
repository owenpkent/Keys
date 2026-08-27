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

    int lowestHeld(const std::vector<int>& v)
    {
        return v.empty() ? -1 : *std::min_element(v.begin(), v.end());
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
    }
};

static McpTests mcpTests;
} // namespace keys::tests
