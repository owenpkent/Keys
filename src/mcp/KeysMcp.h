#pragma once

#include <okstudio/Mcp.h>
#include <juce_events/juce_events.h>
#include <vector>

namespace keys
{
class KeysProcessor;

// Embeds the kit's MCP server (okstudio::mcp::Server) so Claude Code, or any other
// local MCP client, can drive Keys directly: read/set parameters, play notes and
// phrases, capture and fire chord pads, and read/write arp patterns. See docs/MCP.md.
//
// Every tool's `run` lambda below is called on the MESSAGE THREAD (the server
// marshals it there before invoking it), the same thread the editor lives on, so
// tool bodies call straight into KeysProcessor/APVTS exactly the way the UI does.
// No locking, no thread-safety wrapper needed here.
//
// Also owns a small 5ms-poll Timer that fires everything this bridge schedules for
// later: delayed chord-pad releases, and the note-ons/note-offs of play_notes and
// play_sequence.
//
// Note scheduling CANNOT go through the collector's own message timestamping, which
// is what it used to do. juce::MidiMessageCollector is built for live input:
// removeNextBlockOfMessages() empties its whole queue into the current block every
// callback and clamps each event with jlimit(0, numSamples - 1, pos), so a
// future-stamped message is not held, it is dragged into the block that happens to be
// playing. A play_notes note-on and its note-off therefore landed microseconds apart
// (silent), and a whole play_sequence phrase collapsed into a single buffer. Anything
// that must happen later has to be held here and emitted at real time instead.
class KeysMcp : private juce::Timer
{
public:
    explicit KeysMcp(KeysProcessor& processor);
    ~KeysMcp() override;

    // One JSON-RPC line in, the response line out, straight through to the embedded server.
    // This is the seam `tests/McpTests.cpp` drives: no socket, no client, no second server,
    // and - the point of going through here rather than reaching for the tool table - the
    // schema, the registration and the JSON-RPC layer are all under test alongside the tool
    // body. A tool whose params are malformed, or that never reached addTool, fails here
    // where calling its `run` directly could not notice either. The kit documents the same
    // reasoning on Server::handleLine itself.
    juce::String handleLine(const juce::String& line) { return server.handleLine(line); }

private:
    // The tool table, built but not registered; the constructor hands it straight to the
    // server. Private, because handleLine above is the seam and a second public route into
    // the tools would be a table under test that nothing had registered.
    std::vector<okstudio::mcp::Tool> buildTools();

    void timerCallback() override;
    void wake(); // start the poll timer if a queue just gained work; see the constructor
    void cancelPendingRelease(int slot);
    void scheduleNote(double atMs, int note, int channel, float vel01, bool isOn);
    static juce::String productSlug();

    okstudio::mcp::Tool toolGetState();
    okstudio::mcp::Tool toolListParams();
    okstudio::mcp::Tool toolSetParams();
    okstudio::mcp::Tool toolPlayNotes();
    okstudio::mcp::Tool toolPlaySequence();
    okstudio::mcp::Tool toolAllNotesOff();
    okstudio::mcp::Tool toolGetChordPads();
    okstudio::mcp::Tool toolSetChordPad();
    okstudio::mcp::Tool toolClearChordPad();
    okstudio::mcp::Tool toolPressChordPad();
    okstudio::mcp::Tool toolReleaseChordPad();
    okstudio::mcp::Tool toolHoldArpChord();
    okstudio::mcp::Tool toolReleaseArpChord();
    okstudio::mcp::Tool toolGetArpPattern();
    okstudio::mcp::Tool toolSetArpPattern();
    okstudio::mcp::Tool toolRecallArpPattern();
    okstudio::mcp::Tool toolStoreArpPattern();
    okstudio::mcp::Tool toolApplyEuclid();

    KeysProcessor& processor;
    okstudio::mcp::Server server;

    // Chord pads have no note-off-style delay parameter, so a timed press_chord_pad
    // release is tracked here and polled by the Timer instead.
    struct PendingRelease
    {
        double releaseAtMs;
        int slot;
    };
    std::vector<PendingRelease> pendingReleases;

    // Notes that play_notes / play_sequence scheduled for later, kept sorted by
    // atMs and emitted by timerCallback once their time arrives. Relative timing
    // survives exactly (every event is measured from one base taken when the tool
    // ran), so the poll interval costs each event up to one tick of lateness rather
    // than accumulating drift across a phrase.
    struct PendingNote
    {
        double atMs;   // absolute juce::Time::getMillisecondCounterHiRes() stamp
        int note;
        int channel;   // 0 = follow the MIDI Channel control, as noteOn/noteOff expect
        float vel01;
        bool isOn;
    };
    std::vector<PendingNote> pendingNotes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysMcp)
};
} // namespace keys
