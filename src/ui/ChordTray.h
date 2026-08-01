#pragma once

#include "../PluginProcessor.h"
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
// The drag crosses windows, and nothing in JUCE does that for free. The pad strip is in the
// main editor window and this is in a DetachedWindow of its own, so a `DragAndDropContainer`
// would never see the drop and `mouseUp` arrives here with coordinates local to a component the
// pads know nothing about. The two hooks below hand the editor a *screen* position, which is
// the one space the two windows share; the editor holds both ends and closes the gap. Occlusion
// is the target's problem, not this one's (see ChordPads::externalDropSlotAt).
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

    // Reroll only if a generator setting has moved since the last look. The panel's 15 Hz timer
    // calls this; it is a poll rather than a listener because the settings are APVTS parameters
    // and can move from the Pads bar, the host or a session load as well as from this window.
    // The page's locked chords are deliberately *not* part of the signature: they feed Lock
    // Influence, so including them would reroll the whole tray every time you committed a card.
    void refreshForSettings();

    // The drag's two ends plus its cleanup, all in *screen* coordinates - see the class comment
    // for why there is no other space these two windows share. Unset, the tray still auditions,
    // which is what makes the click the half of the gesture that cannot break.
    std::function<void(juce::Point<int> screenPos)> onDragOver;
    std::function<bool(juce::Point<int> screenPos, const KeysProcessor::ChordPad&)> onDrop;
    std::function<void()> onDragEnd;

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
//   * a **pad from the main window** dropped on it, which is the mirror of the commit drag and
//     the reason ChordPads grew `onDropOutside`. Dropping a pad here **copies** it: dragging a
//     card off the strip normally clears it, and a gesture that reached for the reference box
//     and deleted a chord instead would be the worst bug in the window.
//
// Left-click auditions it, the same as a tray card. It is not a drag source: it is where chords
// come to be kept, and the pads are one click away through Similar / Could follow rather than a
// second commit path nobody asked for.
class ChordRefCard : public juce::Component
{
public:
    ChordRefCard(KeysProcessor&, ChordGenMenu&);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

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
