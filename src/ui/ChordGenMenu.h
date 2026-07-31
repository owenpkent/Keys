#pragma once

#include "../ChordGen.h"
#include "../ChordSuggest.h"
#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace keys
{
// The chord generator, ported from Octavium's Autofill + Options dialogs and its right-click
// card menu (app/chord_autofill.py, app/chord_suggestions.py). It fills the *current pad
// page*, so the four pages can hold four different keys.
//
// **This is the brain, and only the brain.** It is a plain object rather than a Component, it
// draws nothing, and the editor holds one as a member for its whole life. That is load-bearing
// rather than tidy: the per-card actions below (New chord, what could follow this one) are
// offered from a pad's right-click menu, and while this was a panel the menu asked "does the
// generator exist?" and offered nothing whenever that panel was closed. It must not be
// possible to close a window and lose an item on a card menu, so the window cannot own this.
//
// Three surfaces read it, none of them owning it:
//
//   * the **Pads section bar** - Fill and Regen as chips, Key, Mode and Scale Compliance as
//     combo boxes. A bar is 34 px that already exists, so controls riding it cost the window
//     no height at all. This is the fast path, and it survives folding the pads away;
//   * the **generator's own window** (ChordGenPanel, 2026-07-30, Owen: "I think the chord
//     generator should just pop out a new window instead of being in the right click menu").
//     That is the complete path: every setting, plus Fill, Regen and Clear page. It opens from
//     a button on the same bar and is built and destroyed with itself, so it holds nothing
//     while it is shut;
//   * a **pad's card menu**, for the two things that are about one card rather than the page:
//     New chord and the suggestion families.
//
// The settings were items on that card menu for a few hours on 2026-07-30, as submenus of
// discrete ticked values, because there was no panel left to put them on. The window replaced
// all of it (`addPageMenuItems` and the Setting/Ladder machinery under it are gone), which
// takes the menu back from 23 rows to nine.
class ChordGenMenu : private juce::Timer
{
public:
    explicit ChordGenMenu(KeysProcessor&);
    ~ChordGenMenu() override;

    // The page-wide actions, one per chip on the Pads bar. They stopped being one function
    // with a flag on 2026-07-30 (Owen: "new generations shouldn't overwrite existing"), and
    // the split is the point rather than a tidy-up:
    //
    //   * **fillPage** writes the *empty* pads and nothing else. It never replaces a chord
    //     that is already on the page, locked or not, so it is safe to lean on;
    //   * **regeneratePage** is the destructive one: it rerolls the pads that already carry a
    //     chord, skipping the locked ones. Replacing what is there is what "regenerate" means,
    //     and the lock is what says "not this one".
    //
    // Each greys itself out when it would do nothing (see the two queries below), so which
    // button is which is readable from the bar without a tooltip.
    //
    // clearPage is not a chip beside them: it wipes sixteen pads, there is no undo in Keys, and
    // on the bar it sat one slip away from the page buttons. It is a button inside the
    // generator's window, where the other page-wide actions are and where a slip costs an
    // extra click to reach in the first place.
    void fillPage();
    void regeneratePage();
    void clearPage();

    // What each of those two would find to do, for the bar and the window to grey their
    // buttons on. Clear page shares the second: it takes exactly what Regen would.
    bool pageHasEmptyPads() const { return ! emptyPadsOnPage().empty(); }
    bool pageHasRegeneratablePads() const { return ! regeneratablePadsOnPage().empty(); }

    // The generator's items on a pad's card menu, and what to do with a choice from them.
    // ChordPads builds the menu and calls these in that order on the message thread:
    // addPadMenuItems, then handlePadMenuChoice with what was picked. The first rebuilds the
    // suggestion table an id indexes into, so it is the one that has to run first.
    static constexpr int idNewChord = 200;     // == ChordPads::extraMenuIdBase
    static constexpr int idSuggestBase = 210;  // one per suggestion; suggest::all() gives 18
    void addPadMenuItems(int slot, juce::PopupMenu&);
    void handlePadMenuChoice(int slot, int id);

    // Whether the current source reads Mode and Scale Compliance at all. The Markov brain
    // walks a table of transitions rather than a scale, so to it those two mean nothing.
    // The Pads bar and the generator's window both grey them on this one answer, so the same
    // setting is never live in one place and dead in the other. Key is not included: the
    // chains do transpose to it.
    bool readsScaleSettings() const;

    // Mood and Start, the two picks that are not parameters (see the members). The window
    // drives them through here rather than holding them, so switching it off and on again does
    // not quietly reset what the next Fill will generate. Empty is the "Any" sentinel.
    const juce::String& moodChoice() const { return mood; }
    const juce::String& startChoice() const { return start; }
    void setMoodChoice(juce::String s) { mood = std::move(s); }
    void setStartChoice(juce::String s) { start = std::move(s); }
    int chainMode() const; // which Markov corpus is up, for the Mood list that belongs to it

private:
    void timerCallback() override;

    void regeneratePad(int slot);
    void newChordFor(int slot); // regenerate a filled pad, or conjure one for an empty slot
    void writeChord(int slot, const chordgen::Chord& c);
    chordgen::Options currentOptions() const;
    std::vector<int> lockedTypesOnPage() const;
    // The slots on the current page each bulk action is allowed to write: the blanks for Fill,
    // the unlocked pads that already carry a chord for Regen. Absolute slots, so they survive
    // a page flip the way every other index here does.
    std::vector<int> emptyPadsOnPage() const;
    std::vector<int> regeneratablePadsOnPage() const;
    int genRoot() const;
    int genMode() const;

    // The Markov source (Source: Markov). Same page mechanics, different brain.
    bool markovActive() const;
    juce::String moodForChain() const; // `mood`, or Any when it belongs to another chain
    void fillPageMarkov();
    void regeneratePageMarkov();
    void regeneratePadMarkov(int slot);

    // Suggestion audition: play a chord for a moment without closing the menu.
    void previewChord(const std::vector<int>& notes);
    void stopPreview();

    KeysProcessor& processor;
    juce::Random rng;

    // Mood and Start are transient (like the performance wheels): they are choices about the
    // progression you are generating right now, not session state, and empty means Any. They
    // live here rather than in the window's combo boxes so they outlive it being closed.
    juce::String mood, start;

    // The suggestion list the last card menu offered, and where a pick would land. Held
    // between addPadMenuItems and handlePadMenuChoice, which ChordPads calls in that order.
    std::vector<suggest::Suggestion> lastSuggestions;
    int lastSuggestTarget = -1;

    std::vector<int> previewNotes; // suggestion audition currently sounding

    JUCE_DECLARE_WEAK_REFERENCEABLE(ChordGenMenu)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordGenMenu)
};
} // namespace keys
