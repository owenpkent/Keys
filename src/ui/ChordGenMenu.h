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
// **It has no panel at all** (2026-07-30, Owen's call). It lost its own 4x4 grid of cards
// earlier the same day, that grid being the sixteen pads of the current page drawn a second
// time; what was left was a band of combo boxes and sliders sitting above the cards they
// wrote to, and a whole centre view spent on it. So the band went too, and with it the
// Chords view:
//
//   * the two bulk actions, Fill and Regen, are chips on the **Pads section bar** - a bar is
//     34 px that already exists, so controls riding it cost the window no height at all. They
//     are the only left-click path into generation, which is why they are not on a menu.
//     Clear page is not up there with them: see fillPage/regeneratePage/clearPage below;
//   * the three settings that get reached for constantly - Key, Mode and Scale Compliance -
//     are combo boxes on that same bar, attached to the same parameters this menu writes, so
//     the fast path and the complete one always agree (2026-07-30);
//   * everything else is on a **pad's right-click card menu**: New chord, what could follow
//     this one, and every setting as a submenu of discrete ticked values. A PopupMenu cannot
//     hold a slider, so a continuous control reaches the mouse as the handful of values worth
//     having, one click each, with the live one ticked and repeated in the parent item so the
//     menu doubles as the display the panel used to be.
//
// The settings sit behind a single **Generator settings** wrapper on that menu, with the
// Markov group nested inside it. They were flattened onto the top level earlier the same day
// to save a leg of diagonal hover, and that had to be undone: nine flat settings took the pad
// menu to 23 rows plus four section headers, about 820 px at the mouse-only item height, and
// the menu hangs off a pad near the bottom of a 699 px window. JUCE answers a menu taller than
// the space it has by splitting it into columns or making it hover-scroll, and a scrolling
// popup cannot be used with one mouse at all. The hover the wrapper costs was also aimed at a
// problem already solved in the same session: Key, Mode and Scale Compliance are combo boxes
// on the Pads bar now, so the settings anybody reaches for mid-audition are one click away
// with no menu involved. They are repeated inside the wrapper all the same, because this menu
// is the complete path and the bar is only the fast one.
//
// This is a plain object now rather than a Component, and the editor holds one for its whole
// life. That matters: the pad menu used to ask "does the generator exist?" and offer nothing
// when the Chords view was closed, and with no view left that test would have hidden New
// chord forever.
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
    // clearPage is reached from a pad's card menu instead: it wipes sixteen pads, there is no
    // undo in Keys, and a chip on a bar sat one slip away from the page buttons.
    void fillPage();
    void regeneratePage();
    void clearPage();

    // What each of those two would find to do, for the bar to grey its chips on.
    bool pageHasEmptyPads() const { return ! emptyPadsOnPage().empty(); }
    bool pageHasRegeneratablePads() const { return ! regeneratablePadsOnPage().empty(); }

    // The generator's items on a pad's card menu, and what to do with a choice from them.
    // ChordPads builds the menu and calls these in that order on the message thread:
    // addPadMenuItems, then addPageMenuItems, then handlePadMenuChoice with what was picked.
    // The first of the three rebuilds both id tables, so it is the one that has to run first.
    static constexpr int idNewChord = 200;     // == ChordPads::extraMenuIdBase
    static constexpr int idClearPage = 201;    // Clear page; a bar chip until 2026-07-30
    static constexpr int idSuggestBase = 210;  // one per suggestion; suggest::all() gives 18
    static constexpr int idSettingsBase = 300; // one per entry in lastSettings
    void addPadMenuItems(int slot, juce::PopupMenu&);
    void addPageMenuItems(juce::PopupMenu&);
    void handlePadMenuChoice(int slot, int id);

    // Whether the current source reads Mode and Scale Compliance at all. The Markov brain
    // walks a table of transitions rather than a scale, so to it those two mean nothing.
    // Both the card menu and the Pads bar grey them on this one answer, so the same setting
    // is never live in one place and dead in the other. Key is not included: the chains do
    // transpose to it.
    bool readsScaleSettings() const;

private:
    void timerCallback() override;

    // One item on the settings half of the menu: what a click on it writes. Recorded as the
    // menu is built, so an id is an index into the table and no arithmetic has to decide what
    // it meant. Same contract the suggestion list relies on - the menu is built and its choice
    // reported one call after the other, and a second menu rebuilds the table first.
    struct Setting
    {
        const char* param = nullptr;  // APVTS parameter to write
        float value = 0.0f;           // what to write into it
        bool toggle = false;          // flip it 0/1 instead (Notes, Inversions)
        juce::String* text = nullptr; // or set this transient pick (Mood, Start)
        juce::String textValue;
    };

    // The items of one settings submenu: what each reads, and the value it writes.
    struct Ladder
    {
        juce::StringArray labels;
        std::vector<float> values;
    };
    static Ladder ladder(std::initializer_list<float>, const juce::String& suffix = {}, int decimals = 0);
    static Ladder indexed(const juce::StringArray&);
    static Ladder modeLadder(); // the mode names, each carrying the character it plays in

    // Every setting, added onto the menu it is handed - which is the "Generator settings"
    // submenu addPageMenuItems builds, never the top level. See the class comment.
    void addSettingsItems(juce::PopupMenu&);
    int addSetting(Setting);
    void applySetting(const Setting&);
    void setParam(const char* param, float value);
    // A submenu of discrete values for one parameter, ticked at the live one (the nearest, so
    // a value set by host automation still shows), with that value repeated in the parent
    // item. `shortLabels`, if given, is what the parent says instead of the full item text.
    void addChoice(juce::PopupMenu& parent, const juce::String& name, const char* param,
                   const Ladder&, bool enabled, const juce::StringArray& shortLabels = {});
    // The same, for a set of independent on/off parameters (note counts, inversions), where a
    // click toggles one rather than choosing between them.
    void addToggles(juce::PopupMenu& parent, const juce::String& name,
                    const std::vector<const char*>& params, const juce::StringArray& labels,
                    const juce::String& fallback, bool enabled);
    // And for the two picks that are not parameters at all (see mood/start below).
    void addTextChoice(juce::PopupMenu& parent, const juce::String& name, juce::String& target,
                       const juce::StringArray& choices, bool enabled);

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
    int chainMode() const;
    void fillPageMarkov();
    void regeneratePageMarkov();
    void regeneratePadMarkov(int slot);

    // Suggestion audition: play a chord for a moment without closing the menu.
    void previewChord(const std::vector<int>& notes);
    void stopPreview();

    KeysProcessor& processor;
    juce::Random rng;

    // Mood and Start are transient (like the performance wheels): they are choices about the
    // progression you are generating right now, not session state, and empty means Any.
    juce::String mood, start;

    // The suggestion list the last card menu offered, and where a pick would land. Held
    // between addPadMenuItems and handlePadMenuChoice, which ChordPads calls in that order.
    std::vector<suggest::Suggestion> lastSuggestions;
    int lastSuggestTarget = -1;
    std::vector<Setting> lastSettings;

    std::vector<int> previewNotes; // suggestion audition currently sounding

    JUCE_DECLARE_WEAK_REFERENCEABLE(ChordGenMenu)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordGenMenu)
};
} // namespace keys
