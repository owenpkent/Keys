#pragma once

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace keys
{
// A row of chord pads plus a live "current chord" card, all mouse-only.
//
//   * Build a chord on the keyboard (Latch on, click the notes) - the card names it.
//   * Drag the card onto a pad to capture the chord there (auto-labelled).
//   * Drag a filled pad onto the card to recall its chord for editing (onRecall).
//   * Click a filled pad to play/stop its chord (Exclusive makes a new pad choke the old).
//   * Drag a pad onto another to move it, or off the row to clear it.
//   * Right-click a pad for its card menu: Edit on keyboard (the editor links the pad
//     to the piano; every latch change writes back live) and Clear. Part of the
//     owner-directed right-click exception in CLAUDE.md.
//
// Sixteen pads per page, laid out as two rows of eight (Octavium parity). The pad
// definitions and playback live in the processor, so they persist with the session and
// keep sounding independent of the editor. This is just the view/controller.
class ChordPads : public juce::Component
{
public:
    explicit ChordPads(KeysProcessor&);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // The keyboard's currently-sounding notes, pushed from the editor's timer.
    void setCurrentChord(const std::vector<int>& notes);

    // Fired when a filled pad is dropped onto the live chord card: hands back that pad's
    // notes so the editor can recall it for editing. The pad itself is left untouched.
    std::function<void(const std::vector<int>&)> onRecall;

    // Fired from a pad's right-click menu: start or stop editing this slot on the
    // keyboard. The editor owns the edit link; the strip only requests and paints it.
    std::function<void(int)> onEditToggle;
    void setEditingSlot(int slot); // absolute slot being edited, or -1

    // "To Arp", a toggle on the Pads section bar. On, a left-click on a chord card holds its
    // chord into the arpeggiator rather than playing it for as long as the button is down:
    // click it again to release, click another to swap. Off, the pads behave exactly as they
    // always have. A visible toggle rather than an implicit "the arp is on" mode, so a pad
    // never quietly does a different thing than it did a minute ago.
    //
    // The flag itself lives on the processor (KeysProcessor::LayoutState::toArp), because the
    // chord it holds outlives this editor and the generator's pad grid has to honour the same
    // mode. This just applies a change and repaints.
    void setToArp(bool);

private:
    juce::Rectangle<float> cardBounds() const;
    juce::Rectangle<float> padBounds(int visibleIndex) const; // 0..padsPerPage-1: row = i/8, col = i%8
    int cellAt(juce::Point<float>) const; // -2 = card, >= 0 = absolute pad slot, -1 = none
    bool sourceIsDraggable() const;
    void showPadMenu(int slot);

    KeysProcessor& processor;
    bool toArp() const; // processor.layout.toArp; see setToArp
    int editingSlot = -1;
    std::vector<int> currentNotes;
    juce::String currentName;

    int dragSource = -1;   // -2 card, 0..N-1 pad, -1 none
    int playing = -1;      // pad held down and sounding (beat-pad momentary play)
    bool playingLive = false; // the live card is held down and sounding its chord
    bool dragging = false;
    juce::Point<float> downPos, dragPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordPads)
};
} // namespace keys
