#pragma once

#include "../PluginProcessor.h"
#include "ChordDrag.h"
#include "ChordGenMenu.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace keys
{
// Sixteen candidate chords, four by four, inside the generator's window (Owen, 2026-08-01: "I
// have four by four pad where you can audition new chords. We want to be able to try a bunch
// out").
//
// **This is not a second set of chord cards, and the distinction is the whole reason it is
// allowed to exist.** The 4x4 grid the old generator panel drew came out on 2026-07-30 because
// it was the same sixteen pads of the same page as the strip below it, written through the same
// KeysProcessor::setChordPad - two views of one thing, and the cards downstairs were the better
// view. These cards are chords the page has never seen and may never see: they belong to no
// slot, they are not in the session, and closing this window throws them away. That is what
// makes the tray worth the height. Auditioning a chord used to cost a pad, so trying eight
// meant either filling the page with seven you did not want or rerolling one slot eight times
// and losing each candidate as you looked at the next.
//
// Two things happen on a card, both left button, which is what keeps this inside the mouse-only
// contract without adding a right-click path to a closed list:
//
//   * **click** - audition it. The notes go out through ChordGenMenu, whose 800 ms timer
//     releases them. This class never calls noteOn, for exactly the reason ChordGenPanel does
//     not: the brain outlives every window, so a close cannot strand a preview note;
//   * **drag onto a pad** - commit it there. A drag is the only gesture that can name a slot,
//     which is why it and not a second click is the commit.
//
// The drag crosses windows, and JUCE does that for free - which is the opposite of what this
// comment said until 2026-08-02. `DragAndDropContainer::startDragging` takes a fourth parameter,
// `allowDraggingToExternalWindows`; pass true and the drop is delivered to a target in any other
// JUCE window, the pad strip included. See ChordDrag.h for the mechanism. The container is
// ChordGenPanel, this window's content, and it is a container for no other reason.
//
// The ghost follows the cursor out of this window and over the strip, because the drag image is
// a desktop window of its own rather than a child of anything here.
//
// A committed card **leaves its cell empty**, which is the one piece of state the tray keeps:
// the hole is how you see which candidates you have already taken, and it is what gives Fill
// something to do. Fill it back up, reroll what you have not used, and keep going until the page
// is what you wanted. Nothing here is ever lost by rerolling, which is also why the panel's
// timer refills the whole tray whenever a generator setting moves: the tray is a view of what
// the settings would produce, and sixteen answers to the old Key are worth nothing.
class ChordTray : public juce::Component
{
public:
    ChordTray(KeysProcessor&, ChordGenMenu&);

    static constexpr int rows = 4;
    static constexpr int cols = 4;
    static constexpr int numCells = rows * cols;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    // The tray's own three actions, which are the generator window's whole action row (Owen,
    // 2026-08-01: "when you click on regenerate unlocked, I don't want it to regenerate the ones
    // in the host window, only in the card generator window"). They read as the page-wide
    // buttons they replaced, and deliberately so - the split between the safe one and the
    // destructive one is the same split, moved off the pads and onto the candidates:
    //
    //   * **fill** writes the empty cells and only the empty cells;
    //   * **regen** rerolls the cells that already carry a candidate;
    //   * **clear** empties the whole tray.
    //
    // Nothing here can lose work, which is the difference from the buttons of the same names on
    // the Pads bar: a tray card is not in the session and is one drag away from being on a pad
    // if you want it. That is why clear needs no lock to respect and no confirmation.
    void fill();
    void regen();
    void clear();

    // What each would find to do, for the window to grey its buttons on. Same shape as
    // ChordGenMenu::pageHasEmptyPads / pageHasRegeneratablePads, and for the same reason: a
    // button that would do nothing should say so without a tooltip.
    bool hasEmptyCells() const;
    bool hasFilledCells() const;

    // Replace the whole tray with a given list, short lists leaving blanks. This is how the
    // seeded answers land - Similar and Could follow beside the reference card, and the same two
    // on a tray card's own menu - so a set of candidates that came from somewhere other than the
    // Source setting still arrives through one door.
    void setAll(const std::vector<KeysProcessor::ChordPad>& candidates);

    // What is in the tray right now, for the source diagram to draw the walk that produced it.
    // Read-only and by reference: SourceViz is a picture of this, never a second copy of it.
    const std::vector<KeysProcessor::ChordPad>& candidates() const { return cells; }

    // Whether a generator setting has moved since the tray was last filled, so the window can
    // *say* the candidates are stale rather than acting on it.
    //
    // **Changing a setting generates nothing** (Owen, 2026-08-01: "I don't want it to auto
    // generate when you change a source"). The tray rerolled itself on any settings change for
    // part of that day, on the reasoning that sixteen answers to the old Key are worth nothing.
    // That reasoning was right about the candidates and wrong about who decides: sweeping Source
    // to read the seven of them threw away the tray six times on the way past, and a control you
    // cannot explore without destroying your work is a control you stop touching. Generating is
    // Fill and Regen and nothing else.
    //
    // A poll rather than a listener, because the settings are APVTS parameters and can move from
    // the Pads bar, the host or a session load as well as from this window. The page's locked
    // chords are deliberately *not* in the signature: they feed Lock Influence, so including them
    // would mark the tray stale every time you committed a card.
    bool settingsMovedSinceFill() const;

    // "Send to first empty pad" on the card menu. It is the menu's one *placing* item and it
    // exists because a drag needs a hand steady enough to land on one card of sixteen in another
    // window; this is the same commit with the aim taken out. Returns false when the page is
    // full, which is what greys the item.
    std::function<bool(const KeysProcessor::ChordPad&)> onSendToFirstEmpty;
    std::function<bool()> onPageHasEmptyPad;

private:
    juce::Rectangle<float> cellBounds(int index) const;
    int cellAt(juce::Point<float>) const;
    juce::String settingsSignature() const;
    void writeInto(const std::vector<int>& cellIndices); // the one call that asks for candidates
    void showCardMenu(int index);
    void reshapeCell(int index, const std::vector<int>& notes); // an edit to one candidate
    void beginDrag(const juce::MouseEvent&);
    void endDrag();

    KeysProcessor& processor;
    ChordGenMenu& gen;

    // numCells entries once generated; an entry with empty `notes` is a blank the generator
    // could not fill. Held as ChordPad rather than chordgen::Chord because a commit is
    // `setChordPad(slot, cell)` and nothing in between - the Markov source and the algorithmic
    // one disagree about which metadata a chord carries (numeral vs degree), and this is the
    // struct that already holds either.
    std::vector<KeysProcessor::ChordPad> cells;

    juce::String lastSignature;
    int pressed = -1; // card under the button, or -1
    int hovered = -1; // card under the mouse, for the hover lift
    bool dragging = false;
    juce::Point<float> downPos;

    // The chord currently in the air, kept so the answer that comes back on it can be read when
    // the button goes up. Held by the drag itself as well, so this pointer going stale is not a
    // way the payload can die.
    chorddrag::Payload::Ptr inFlight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordTray)
};

// One chord that the tray's own actions cannot touch (Owen, 2026-08-01: "I think we should have
// another box for the reference chord where we can drag in something from the main window or one
// of the other chords. So when you regenerate everything, it doesn't erase your reference
// chord").
//
// This is the fixed point the tray needed. Everything above is disposable by design, which is
// what makes it safe to Regen sixteen chords at a time - and also what made the chord you were
// working *from* the one thing you could not keep. Seed the tray from a candidate and the seed
// went with the answer; the chord you liked was gone the moment it told you what came next.
//
// It fills from either direction, and both are drags:
//
//   * a **tray card** dropped on it, which never leaves this window;
//   * a **pad from the main window** dropped on it, the mirror of the commit drag. Dropping a
//     pad here **copies** it: dragging a card off the strip normally clears it, so this card
//     sets `taken` on the payload and the strip reads that as "somebody has it, leave the card
//     alone". A gesture that reached for the reference box and deleted a chord instead would be
//     the worst bug in the window, which is the whole reason that flag exists.
//
// It takes the drop itself, as an ordinary `DragAndDropTarget`, rather than being offered a
// screen position by the editor: a drag that has reached this card is over this card, and JUCE's
// own target search answers "which window is on top here" better than a bounds test could - the
// reference box used to light up through a window sitting over it.
//
// Left-click auditions it, the same as a tray card. It is not a drag source: it is where chords
// come to be kept, and the pads are one click away through Similar / Could follow rather than a
// second commit path nobody asked for.
class ChordRefCard : public juce::Component,
                     public juce::DragAndDropTarget
{
public:
    ChordRefCard(KeysProcessor&, ChordGenMenu&);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    // A chord card from the tray beside it or from the pad strip in another window. The live
    // card is refused: it is what is under your hand on the keyboard, not a chord you have kept.
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

    void setChord(const KeysProcessor::ChordPad&);
    void clearChord();
    bool hasChord() const { return ! held.notes.empty(); }
    const KeysProcessor::ChordPad& chord() const { return held; }

    // Whether a drag currently over the window is offering this card a chord. Painted as the
    // same accent ring a pad wears, because it means the same thing.
    void setDropHighlight(bool);

private:
    KeysProcessor& processor;
    ChordGenMenu& gen;
    KeysProcessor::ChordPad held;
    bool pressed = false, hovered = false, dropHighlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordRefCard)
};
} // namespace keys
