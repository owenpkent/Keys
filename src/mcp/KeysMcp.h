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
// Also owns a small 30ms-poll Timer, used only to fire a delayed chord-pad release:
// releaseChordPad() has no delay parameter (unlike noteOff, which now does). Note
// scheduling itself (play_notes/play_sequence) goes through the collector's own
// message timestamping instead, no timer involved.
class KeysMcp : private juce::Timer
{
public:
    explicit KeysMcp(KeysProcessor& processor);
    ~KeysMcp() override;

private:
    void timerCallback() override;
    void cancelPendingRelease(int slot);
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
    okstudio::mcp::Tool toolGetArpPattern();
    okstudio::mcp::Tool toolSetArpPattern();
    okstudio::mcp::Tool toolRecallArpPattern();
    okstudio::mcp::Tool toolStoreArpPattern();

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysMcp)
};
} // namespace keys
