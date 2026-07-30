#pragma once

#include "../ChordGen.h"
#include "../ChordSuggest.h"
#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <okstudio/Theme.h>
#include <array>
#include <functional>
#include <memory>

namespace keys
{
// The chord generator, ported from Octavium's Autofill + Options dialogs and its
// right-click card menu (app/chord_autofill.py, app/chord_suggestions.py).
//
// It is inline rather than a dialog: a plugin editor has no business opening OS windows,
// and the centre view keeps every target inside the one surface the mouse is already in.
// Picking Chords swaps it in above the pads, so the keyboard stays playable while you
// generate. It fills the *current pad page*, so the four pages can hold four different keys.
//
// **It has no cards of its own** (2026-07-30). It used to draw a 4x4 grid of the sixteen
// pads on the current page - the same pads, written through the same
// KeysProcessor::setChordPad - because it was built when the generator was a full-screen
// overlay and the pads had no section of their own. They have had one since 2026-07-25, on
// screen under every centre view, so the grid was the same page drawn twice. This is the
// brain; the cards are the Pads section's, and it can be switched to the tall arrangement
// this grid used to have.
//
// Per-pad actions (New chord / Next suggestions) are offered through
// addPadMenuItems/handlePadMenuChoice, which ChordPads calls while this panel exists; Lock
// went to the card itself, needing nothing from here. That right-click card menu restores
// Octavium's, at Owen's request, and the page-wide left-click buttons (Fill / Regen / Clear)
// remain the bulk path. It is the one deliberate exception to the "right-click is only an
// accelerator" rule - see the Invariants section of CLAUDE.md.
class ChordGenPanel : public juce::Component,
                      private juce::Timer
{
public:
    explicit ChordGenPanel(KeysProcessor&);
    ~ChordGenPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override; // swallow clicks so the editor below is inert

    std::function<void()> onClose; // dismiss the panel

    // Inline: draw as a plain card filling our bounds, with no scrim behind it.
    void setInlineMode(bool);

    // The generator's own items on a pad's card menu, and what to do with a choice from
    // them. The cards belong to the Pads section (see the note in buildControls), so it
    // builds the menu and calls these while this panel is alive.
    static constexpr int idNewChord = 200;    // ChordPads::extraMenuIdBase
    static constexpr int idSuggestBase = 210;
    void addPadMenuItems(int slot, juce::PopupMenu&);
    void handlePadMenuChoice(int slot, int id);

    // Header rows only, since the pad grid moved out: the cards are the Pads section's.
    static constexpr int preferredHeight = 16 + 24 + (28 + 8) + (44 + 4) + (44 + 6) + (36 + 8);

private:
    bool inlineMode = false;

    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void buildControls();
    void fillPage(bool onlyUnlocked);
    void clearPage();
    void regeneratePad(int slot);
    void newChordFor(int slot); // regenerate a filled pad, or conjure one for an empty slot
    void writeChord(int slot, const chordgen::Chord& c);
    chordgen::Options currentOptions() const;
    std::vector<int> lockedTypesOnPage() const;
    int genRoot() const;
    int genMode() const;

    // The Markov source (Source: Markov). Same page mechanics, different brain.
    bool markovActive() const;
    juce::String moodArg() const;  // "" = Any
    juce::String startArg() const; // "" = Any
    void fillPageMarkov(bool onlyUnlocked);
    void regeneratePadMarkov(int slot);
    void refreshMoodItems();

    // Suggestion audition: play a chord for a moment without closing the menu.
    void previewChord(const std::vector<int>& notes);
    void stopPreview();

    KeysProcessor& processor;
    juce::Random rng;

    juce::Label title, modeEmotion, pageLabel;
    juce::TextButton closeButton { "Close" };

    juce::ComboBox rootBox, modeBox;
    juce::Label rootLabel, modeLabel;
    juce::Slider octaveSlider, complianceSlider, lockInfluenceSlider;
    juce::Label octaveLabel, complianceLabel, lockInfluenceLabel;

    juce::Label notesLabel, invLabel;
    juce::ToggleButton triadsButton { "3" }, seventhsButton { "4" }, ninthsButton { "5" };
    juce::ToggleButton inv0Button { "R" }, inv1Button { "1st" }, inv2Button { "2nd" }, inv3Button { "3rd" };

    juce::TextButton fillButton { "Fill Page" };
    juce::TextButton regenButton { "Regen Unlocked" };
    juce::TextButton clearButton { "Clear Page" };

    // The Markov source's controls; visible only while Source is Markov. Mood and
    // Start are transient (like the wheels): performance choices, not session state.
    juce::ComboBox sourceBox, chainBox, moodBox, startBox;
    juce::Label sourceLabel, chainLabel, moodLabel, startLabel;
    juce::Slider tempSlider, lengthSlider;
    juce::Label tempLabel, lengthLabel;

    // The suggestion list the last card menu offered, and where a pick would land. Held
    // between addPadMenuItems and handlePadMenuChoice, which ChordPads calls in that order.
    std::vector<suggest::Suggestion> lastSuggestions;
    int lastSuggestTarget = -1;

    std::unique_ptr<ComboAtt> rootAtt, modeAtt, sourceAtt, chainAtt;
    std::unique_ptr<SliderAtt> octaveAtt, complianceAtt, lockInfluenceAtt, tempAtt, lengthAtt;
    std::unique_ptr<ButtonAtt> triadsAtt, seventhsAtt, ninthsAtt;
    std::unique_ptr<ButtonAtt> inv0Att, inv1Att, inv2Att, inv3Att;

    int lastChainMode = -1;      // rebuild the Mood list when the chain mode changes
    bool markovShown = false;    // last visibility applied, to relayout on source change
    std::vector<int> previewNotes; // suggestion audition currently sounding
    juce::uint32 previewEndMs = 0; // when to stop it (0 = nothing playing)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordGenPanel)
};
} // namespace keys
