#pragma once

#include "../PluginProcessor.h"
#include "ChordDrag.h"
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
//
// **A card sounds on release, never on press** (2026-08-02, Owen: "the chord shouldn't play
// right away when you click it. You should be able to drag it"). Every gesture that starts on a
// card - play it, hand it to an arp line, drag it onto a pad, an arp slot or an arp row - now
// begins with a completely silent mouseDown, and mouseUp is where it is decided which of them
// this one was. Pressing to play and releasing to stop was the older, more instrument-like
// reading, and it cost too much: starting a drag blurted the chord every time, and with an arp
// line switched on the press *consumed* the click and cleared `dragSource`, so a card could not
// be dragged at all in the one mode where dragging it onto a line is the whole point.
//
// **Every drag on this strip is stock JUCE drag-and-drop** (2026-08-02), including the ones that
// leave the window. See ChordDrag.h: the container is the section holder this component is
// parented into, which is what makes the machinery survive the Pads section being popped out
// into a window of its own.
class ChordPads : public juce::Component,
                  public juce::DragAndDropTarget,
                  private juce::Timer
{
public:
    explicit ChordPads(KeysProcessor&);
    // An audition sounds for 800 ms with the button already up, so unlike the press-and-hold
    // it replaced there is a window in which this component can be destroyed - the window
    // closed, the section folded - while a chord it started is still ringing. The processor
    // outlives the editor and keeps playing, and nothing would be left owning those notes:
    // one note-on per sounding pitch, released by the last owner, and the last owner has to
    // still be here to do it. This is that owner leaving properly.
    ~ChordPads() override;

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
    // a **this page** group at the foot of the menu - a Clear page item and a Generator settings
    // submenu holding every setting the generator has. All of that moved into the generator's
    // own window that day (Owen: "the chord generator should just pop out a new window instead
    // of being in the right click menu"), which left the hook with nothing to add.
    static constexpr int extraMenuIdBase = 200;
    std::function<void(int slot, juce::PopupMenu&)> onExtraMenuItems;
    std::function<void(int slot, int itemId)> onExtraMenuChoice;

    // Every drop this strip takes, wherever the chord came from: a pad moved to another pad, a
    // pad dropped on the live card to recall it, the live card captured onto a pad, and a
    // candidate dragged in from the generator's audition tray in a window of its own.
    //
    // One entry point for all four, because to a pad they are the same event. The old
    // arrangement had two - the strip's own mouseUp for the internal cases and a screen-position
    // call from the editor for the tray - on the belief that JUCE could not deliver a drop across
    // two top-level windows. It can (ChordDrag.h). Occlusion, the folded section and the detached
    // section are all answered by JUCE's own target search, which is a better hit test than the
    // one that was here: it cannot light a pad through a window sitting over it.
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

    // The aimless twin of that drop, for "Send to first empty pad" on a tray card's menu. It
    // takes the first blank slot on the *current page*, left to right, and refuses when there is
    // none - which is what greys the item rather than silently overwriting something. Empty
    // means empty, locked or not, the same definition Fill on the Pads bar uses.
    int firstEmptyPadOnPage() const;
    bool sendChordToFirstEmptyPad(const KeysProcessor::ChordPad& pad);

    // The drag going the *other* way needs no hooks at all now. A card leaving this strip is
    // offered to the generator's reference box and to the arp panel's slots, tabs and macro rows
    // by JUCE, because each of those is a `DragAndDropTarget`; the three `std::function`s the
    // editor used to forward screen positions through are gone, and so is the bug class they
    // came with, where a highlight lit on the way out had to be put back out by hand on every
    // path a drag could end (including the far window being closed mid-gesture).
    //
    // What did *not* go is the veto. Dragging a card off the row clears it, and reaching for the
    // reference box means dragging a card off the row, so a taker has to be able to say "I have
    // it, leave the card alone" or the one gesture that keeps a chord would be the one that
    // deletes it. JUCE has no opinion about that, so it rides on the payload: see
    // `chorddrag::Payload::taken`, which every target here sets and this strip reads once the
    // drop has been delivered.

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

    // Hand the card under the press to JUCE, ghost and all.
    void beginChordDrag(const juce::MouseEvent&);
    // Which cell a drop of this chord would actually land on: the same `cellAt` answer with the
    // refusals applied, which differ by where the chord came from. -2 is the live card, >= 0 an
    // absolute pad slot, -1 nothing. Kept apart from `cellAt` because "over a card of this strip"
    // and "this drop would do something" are different questions and only the first one decides
    // whether the drag counts as having left the row.
    int dropCellFor(const chorddrag::Payload&, juce::Point<int> local) const;

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

    // A click sounds the chord for a fixed length and then lets it go, because the button is
    // already up by the time the note starts and there is no second event coming to end it.
    // 800 ms is the generator's own audition length (ChordGenMenu), so hearing a chord is the
    // same gesture and the same duration wherever you do it. Sustain and Latch still apply:
    // this calls releaseChordPad, exactly as the old mouse-up did, and they decide what that
    // means. Clicking anything else cuts a running audition short first.
    static constexpr int auditionMs = 800;
    void timerCallback() override;
    void endAudition();

    // The cell a drag - anyone's, from either window - is currently offering a chord to, or -1.
    // One field for both, because to a pad they mean the same thing: let go here and this card
    // takes that chord.
    int dropCell = -1;
    int dragSource = -1;   // -2 card, 0..N-1 pad, -1 none
    int playing = -1;      // pad sounding out its audition, and lit while it does
    bool playingLive = false; // the live card is sounding its chord
    bool dragging = false;
    juce::Point<float> downPos;

    // The chord this strip currently has in the air, so the answer the targets write on it can
    // be read once the drop has been delivered. Null except during one of this strip's own drags.
    chorddrag::Payload::Ptr inFlight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordPads)
};
} // namespace keys
