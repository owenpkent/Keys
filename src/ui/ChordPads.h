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
//     piano; every latch change writes back live), Clear pad, Lock, Octave down/up, Next
//     voicing, New chord, what could follow this one, and Send to arp slot. Part of the
//     owner-directed right-click exception in CLAUDE.md.
//
// The card surface itself is *entirely* play, drag and feed-the-arp: there is no corner that
// means something else. A lock chip sat in the top-right for a few hours on 2026-07-30 and
// came straight back out at Owen's request; a locked card wears a dot, which is a mark and not
// a target, and Lock is set from the card menu alone. The generator's own settings left this
// menu at the same time and live in a window of their own (ChordGenPanel).
//
// Sixteen pads per page, two rows of eight, and every card reads the same way: the chord's
// name, and under it the notes a press of it plays, with octave numbers. The live card at the
// left says the same about what is under your hand.
//
// One arrangement, not two. A Big switch on the Pads bar gave four rows of four with a note
// list and a mini keyboard on each - the tall card the chord generator used to draw over the
// top of these same pads, before that duplicate grid went on 2026-07-30. It came out the next
// day (Owen): the note list is the part worth reading, it fits a short card, so the only thing
// 190 px of extra section height still bought was the mini keyboard.
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

    // Extra items for a pad's right-click menu, from whoever can service them - today the
    // chord generator, whose per-card actions (New chord, Next: could follow) reach the cards
    // through here. Ids 200 and up belong to the supplier; everything below is this class's own.
    //
    // One hook, not the two there were until 2026-07-30. The second, `onExtraPageItems`, added
    // a **this page** group at the foot of the menu - Clear page and a Generator settings
    // submenu holding every setting the generator has. All of that moved into the generator's
    // own window that day (Owen: "the chord generator should just pop out a new window instead
    // of being in the right click menu"), which left the hook with nothing to add.
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

    // The two chord-shaping actions on that menu, both acting on the *stored* chord of one
    // pad. Menu-only by Owen's call: they are edits, not performance, and the cards have no
    // room for three more targets.
    void shiftPadOctave(int slot, int semitones);
    void nextPadVoicing(int slot);
    // and what both go through, so a chord that is sounding or held into the arp moves with
    // its card instead of being stranded. See the definition.
    void rewritePadChord(int slot, const std::vector<int>& notes);
    int padRootPc(int slot) const; // the root a pad's chord is built on, generated or analysed

    KeysProcessor& processor;
    // Whether a click on a chord card feeds the arpeggiator instead of playing the chord
    // for as long as the button is down. It is the arp's own On state (see
    // KeysProcessor::cardsFeedArp), asked rather than cached so the pads can never be in a
    // different mode than the arp itself.
    bool toArp() const;
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
