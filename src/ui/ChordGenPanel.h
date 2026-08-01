#pragma once

#include "../PluginProcessor.h"
#include "ChordGenMenu.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>

namespace keys
{
// The chord generator's controls, as the content of a window of its own (2026-07-30, Owen: "I
// think the chord generator should just pop out a new window instead of being in the right
// click menu").
//
// This is a **view onto ChordGenMenu, never its owner.** The brain is a member of the editor
// and lives for the editor's whole life; this is built when the window opens and destroyed
// when it closes. That split is the whole design and it is load-bearing rather than tidy:
// "New chord" and "Next: could follow" are items on a pad's card menu, and while the generator
// *was* a panel those items came and went with it, which is the bug that made it a plain
// member in the first place. Nothing here may become the only copy of anything.
//
// Recovered from the panel that was deleted on 2026-07-30 (`git show
// 7261228^:src/ui/ChordGenPanel.cpp`), which is a layout Owen used and liked, and adapted:
//
//   * every control is an APVTS attachment, exactly as it was, so it and the twin on the Pads
//     bar (Key, Mode, Scale Compliance) read and write the one parameter and can never
//     disagree. There is no hand-syncing anywhere in this class;
//   * the page-wide actions call straight into ChordGenMenu - `fillPage`, `regeneratePage`,
//     `clearPage`. The old panel had its own copies of all three; those are gone, and Fill's
//     "empty pads only" rule (Owen, the same day) came with the move for free;
//   * **Clear page** is here rather than on the Pads bar or a card menu. It empties sixteen
//     pads and Keys has no undo of any kind, so it wants to be somewhere you went on purpose;
//   * the suggestion audition is *not* here. It calls noteOn with no pad behind it and is
//     released by an 800 ms timer, so it stays in ChordGenMenu where the destructor that
//     stops it cannot be closed away (see ~ChordGenMenu). This class never plays a note;
//   * Mood and Start are not held here either. They are transient picks that belong to the
//     progression being generated, and shutting the window must not reset them, so the combo
//     boxes read and write ChordGenMenu's copies.
//
// There is no pad grid. The panel drew a 4x4 copy of the current page until 2026-07-30 - the
// same sixteen pads, through the same KeysProcessor::setChordPad, as the Pads section already
// on screen. The cards are the Pads section's, and each of them names its chord and lists its
// notes, which is what this grid was for.
class ChordGenPanel : public juce::Component,
                      private juce::Timer
{
public:
    ChordGenPanel(KeysProcessor&, ChordGenMenu&);
    ~ChordGenPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // The on-screen Close button. The window's title-bar X runs the same teardown; the editor
    // wires both to one call, so there is exactly one way for this object to die.
    std::function<void()> onClose;

    // What the layout below actually needs, so the window's minimum is derived rather than
    // guessed. Widest row is the algorithmic settings row; tallest is all four rows plus the
    // gaps between them. See resized() for the arithmetic each of these adds up.
    static juce::Point<int> contentSize();
    static juce::Point<int> minWindowSize();     // contentSize + the window's own furniture
    static juce::Point<int> defaultWindowSize(); // where it opens the first time

private:
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void buildControls();
    void refreshMoodItems(); // the Mood list belongs to the chain that is up

    // The two sets of controls that share row B, and the one place that decides which of them
    // is on screen. Returned by value rather than as an initializer_list, whose backing array
    // would not outlive the return.
    std::array<juce::Component*, 13> algorithmicBand();
    std::array<juce::Component*, 10> markovBand();
    void applySource(bool markov);

    KeysProcessor& processor;
    ChordGenMenu& gen;

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

    // The Markov source's controls; visible only while Source is Markov, in the same band as
    // the algorithmic settings they replace. Those settings mean nothing to a chain walk, so
    // the band shows whichever set is live rather than reserving a row that is dead half the
    // time. Sharing the rect makes the visibility swap load-bearing rather than cosmetic:
    // applySource() owns it, and the two sets are never on screen together.
    juce::ComboBox sourceBox, chainBox, moodBox, startBox;
    juce::Label sourceLabel, chainLabel, moodLabel, startLabel;
    juce::Slider tempSlider, lengthSlider;
    juce::Label tempLabel, lengthLabel;

    std::unique_ptr<ComboAtt> rootAtt, modeAtt, sourceAtt, chainAtt;
    std::unique_ptr<SliderAtt> octaveAtt, complianceAtt, lockInfluenceAtt, tempAtt, lengthAtt;
    std::unique_ptr<ButtonAtt> triadsAtt, seventhsAtt, ninthsAtt;
    std::unique_ptr<ButtonAtt> inv0Att, inv1Att, inv2Att, inv3Att;

    int lastChainMode = -1;   // rebuild the Mood list when the chain mode changes
    bool markovShown = false; // last visibility applied, to relayout on source change

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordGenPanel)
};
} // namespace keys
