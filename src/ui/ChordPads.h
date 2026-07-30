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
//   * Right-click a pad for its card menu: Edit on keyboard (the editor links the pad to the
//     piano; every latch change writes back live), Clear, Lock, Send to arp slot, and the
//     whole chord generator - New chord, what could follow this one, and its settings. Part
//     of the owner-directed right-click exception in CLAUDE.md.
//
// Sixteen pads per page, as two rows of eight or (Big) four rows of four with the full chord
// card on each: its notes with octave numbers and a mini keyboard of the shape under your
// hand. The tall arrangement is the one the chord generator used to draw over the top of
// these same pads, before that duplicate grid was removed on 2026-07-30; it lives here now,
// so it is available whatever else is on screen.
//
// The pad definitions and playback live in the processor, so they persist with the session
// and keep sounding independent of the editor. This is just the view/controller.
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

    // Two rows of eight, or four rows of four with room for the full chord card (the note
    // list and a mini keyboard of what is held). The tall arrangement is the one the chord
    // generator used to draw itself, over the top of these same sixteen pads; it belongs to
    // the pads, so it is available whatever else is on screen.
    void setBigCards(bool);
    static int rowsFor(bool big) { return big ? 4 : 2; }

    // Extra items for a pad's right-click menu, from whoever can service them - today the
    // chord generator, which has no surface of its own and reaches the cards through here.
    // Ids 200 and up belong to the supplier; everything below is this class's own.
    static constexpr int extraMenuIdBase = 200;
    std::function<void(int slot, juce::PopupMenu&)> onExtraMenuItems;
    std::function<void(int slot, int itemId)> onExtraMenuChoice;

private:
    juce::Rectangle<float> cardBounds() const;
    juce::Rectangle<float> padBounds(int visibleIndex) const; // 0..padsPerPage-1, row-major

    // The tick that ends a keyboard edit, on the pad being edited. Only that one pad has
    // one, and only while the link lasts; a full-height strip at its right end rather than
    // a corner chip, because the mouse-only floor is 34 px and a corner badge that size
    // would sit exactly where the chord name is. Static: it is pure geometry.
    static juce::Rectangle<float> saveBadgeBounds(juce::Rectangle<float> pad);
    int cellAt(juce::Point<float>) const; // -2 = card, >= 0 = absolute pad slot, -1 = none
    bool sourceIsDraggable() const;
    void showPadMenu(int slot);

    KeysProcessor& processor;
    // Whether a click on a chord card feeds the arpeggiator instead of playing the chord
    // for as long as the button is down. It is the arp's own On state (see
    // KeysProcessor::cardsFeedArp), asked rather than cached so the pads can never be in a
    // different mode than the arp itself.
    bool toArp() const;
    int editingSlot = -1;
    bool bigCards = false;
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
