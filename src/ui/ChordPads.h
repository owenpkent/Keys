#pragma once

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace keys
{
// A row of chord pads plus a live "current chord" card, all mouse-only.
//
//   * Build a chord on the keyboard (Latch on, click the notes) - the card names it.
//   * Drag the card onto a pad to capture the chord there (auto-labelled).
//   * Click a filled pad to play/stop its chord (Exclusive makes a new pad choke the old).
//   * Drag a pad onto another to move it, or off the row to clear it.
//
// The pad definitions and playback live in the processor, so they persist with the
// session and keep sounding independent of the editor. This is just the view/controller.
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

private:
    juce::Rectangle<float> cardBounds() const;
    juce::Rectangle<float> padBounds(int i) const;
    int cellAt(juce::Point<float>) const; // -2 = card, 0..N-1 = pad, -1 = none
    bool sourceIsDraggable() const;

    KeysProcessor& processor;
    std::vector<int> currentNotes;
    juce::String currentName;

    int dragSource = -1;   // -2 card, 0..N-1 pad, -1 none
    int playing = -1;      // pad held down and sounding (beat-pad momentary play)
    bool dragging = false;
    juce::Point<float> downPos, dragPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordPads)
};
} // namespace keys
