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
//     That is the complete path: every setting, plus the audition tray and its own Fill, Regen
//     and Clear, none of which touch a pad. It opens from
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
    // **There is no clearPage.** There was one until 2026-08-01, and it lived in the generator's
    // window rather than on the bar because it wiped sixteen pads with no undo behind it and
    // wanted to be somewhere you went on purpose. That window stopped writing pads at all that
    // day (Owen: "I don't want it to regenerate the ones in the host window, only in the card
    // generator window"), which left the one destructive page action with nowhere to live that
    // was not the bar it had already been moved off. It was deleted rather than rehoused: a card
    // at a time is Clear pad on its own menu or a drag off the strip, and replacing the whole
    // page wholesale is what Regen is. Do not restore it without asking.
    void fillPage();
    void regeneratePage();

    // Loose chords for the generator window's audition tray (ChordTray, 2026-08-01): generated
    // from the current settings and written to no pad at all. Every other entry point here
    // *places* what it makes, because until the tray existed there was nowhere to put a chord
    // the page had not accepted, and hearing one therefore cost a slot. This is the one that
    // hands them back instead. Lock Influence still applies, so the tray leans the way Fill
    // does; short of `count`, the caller gets what there was.
    std::vector<KeysProcessor::ChordPad> generateCandidates(int count);

    // The two seeded variants, for the tray's card menu (Owen, 2026-08-01: "when you right click
    // on a chord in there, I want you to have a whole bunch of options about trying to find
    // similar ones or what might come next"). Both take one candidate and answer with a trayful
    // built *from* it, which is what turns the tray from a bag of sixteen unrelated chords into
    // something you can explore: hear one you like, then ask for its neighbours.
    //
    //   * **similarTo** keeps the root and varies the colour: the same chord as a seventh, a
    //     ninth, a sus, a different inversion, the parallel major or minor;
    //   * **couldFollow** answers the other question, and leans on `suggest::all` rather than
    //     inventing a second opinion - that table is Octavium's, it is already what the pad card
    //     menu offers, and two different answers to "what comes after this" would be a bug
    //     wearing two hats.
    //
    // Short of `count` they return what there was; the tray leaves the rest of its cells alone.
    std::vector<KeysProcessor::ChordPad> similarTo(const std::vector<int>& seed, int count);
    std::vector<KeysProcessor::ChordPad> couldFollow(const std::vector<int>& seed, int count);

    // The audition, opened up for the tray. The note path stays on this side rather than moving
    // into the window for the reason the class comment gives: it calls noteOn with no pad behind
    // it and is released by this object's 800 ms timer, and this object outlives every window,
    // so no close can strand a preview note. ChordTray and ChordGenPanel both go through here
    // and neither of them ever calls noteOn.
    void auditionChord(const std::vector<int>& notes) { previewChord(notes); }
    void stopAudition() { stopPreview(); }

    // What each of those two would find to do, for the bar and the window to grey their
    // buttons on.
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

    // The pad linked to the keyboard for editing, or -1. The editor owns that link and tells
    // this as well as ChordPads, because both items above write a pad's chord and the keybed
    // rewrites the pad it is editing on every latch change. New chord greys on that card and
    // a suggestion lands past it, which is the rule Octave down/up and Next voicing already
    // follow on the same menu.
    void setEditingSlot(int slot) { editingSlot = slot; }

    // Whether the current source reads Mode and Scale Compliance at all. The Markov brain
    // walks a table of transitions rather than a scale, so to it those two mean nothing.
    // The Pads bar and the generator's window both grey them on this one answer, so the same
    // setting is never live in one place and dead in the other. Key is not included: the
    // chains do transpose to it.
    bool readsScaleSettings() const; // Scale Compliance and Lock Influence: the pool's own dials
    bool readsMode() const;          // everything but Markov, which has no scale in it at all

    // Which of the seven brains is up, as the raw `genSource` index. Public because the window
    // shows a different band of settings per source and has to ask. The order is the parameter's,
    // and appending is the only safe way to grow it (see the parameter's own comment).
    int sourceIndex() const;

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

    // Every source except Markov produces `chordgen::Chord`, so one call covers Algorithmic and
    // the five that arrived on 2026-08-01, and every caller that used to reach straight for
    // `chordgen::generate` goes through here instead. Voice leading is applied on the way out,
    // because it is a pass over whatever a source produced rather than a source of its own.
    // Markov keeps its own three paths: its chords carry a numeral that ChordGen has no field
    // for, and regenerating one pad steps the chain from its left neighbour, which is behaviour
    // worth more than the symmetry of folding it in here.
    std::vector<chordgen::Chord> generateChords(int count);

    // The two voicing post-passes, applied to every source for the reason voice leading is:
    // how many notes a chord has and which register it sits in are facts about the voicing, not
    // about which chord it is, so seven brains honouring them separately would be seven places to
    // get it wrong (Owen, 2026-08-01: "all of their options should have the option for how many
    // notes and what inversion"). Both read ranges, and both swap the ends if they cross.
    std::pair<int, int> noteCountRange() const; // 2..11
    std::pair<int, int> octaveRange() const;
    void fitVoicing(std::vector<chordgen::Chord>& chords);
    void fitPads(std::vector<KeysProcessor::ChordPad>& pads); // the Markov half of that

    // Lean every chord's third major or minor, whatever produced it. A third pass over the
    // output rather than a seventh brain, for the reason the other two are: it is a question
    // about the chords you got, not about how to get them.
    void applyMajorMinorBias(std::vector<chordgen::Chord>& chords);

    // The tick boxes. `constrains(id)` is the one reader, so a box that is not wired anywhere
    // reads as ticked rather than silently freeing a setting nobody meant to free.
    bool constrains(const char* paramId) const;

    // Key and Mode are chosen once per *generation* when their box is unticked, not once per
    // chord. Every source takes a single root and mode for a whole batch (a circle walk, a chain
    // step, a progression transposed), so a per-chord roll would mean sixteen unrelated
    // one-chord walks rather than one wandering progression. `rollFreeChoices` picks them; -1
    // means "the parameter is in charge", which is what genRoot / genMode look for.
    void rollFreeChoices();
    int rolledRoot = -1, rolledMode = -1;
    void smoothPads(std::vector<KeysProcessor::ChordPad>& pads) const; // the Markov half of that

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
    // Mutable because `currentOptions() const` rolls a free Scale Compliance when its tick box is
    // clear, and that call site is const for good reasons elsewhere. Safe: ChordGenMenu is
    // message-thread only, so there is no second reader to race.
    mutable juce::Random rng;

    // Mood and Start are transient (like the performance wheels): they are choices about the
    // progression you are generating right now, not session state, and empty means Any. They
    // live here rather than in the window's combo boxes so they outlive it being closed.
    juce::String mood, start;

    // The suggestion list the last card menu offered, and where a pick would land. Held
    // between addPadMenuItems and handlePadMenuChoice, which ChordPads calls in that order.
    std::vector<suggest::Suggestion> lastSuggestions;
    int lastSuggestTarget = -1;

    std::vector<int> previewNotes; // suggestion audition currently sounding

    int editingSlot = -1; // pad the keyboard is editing, pushed from the editor; see the setter

    JUCE_DECLARE_WEAK_REFERENCEABLE(ChordGenMenu)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordGenMenu)
};
} // namespace keys
