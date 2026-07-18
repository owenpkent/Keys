#pragma once

#include "../ChordGen.h"
#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>

namespace keys
{
// The chord generator, ported from Octavium's Autofill + Options dialogs and its
// right-click card menu (app/chord_autofill.py, app/chord_suggestions.py).
//
// It is an overlay rather than a dialog: a plugin editor has no business opening OS
// windows, and an overlay keeps every target inside the one surface the mouse is already
// in. It fills the *current pad page*, so the four pages can hold four different keys.
//
// Everything Octavium reached by right-click is a real on-screen button here, which is why
// the pad grid is repeated at full size instead of reusing the strip: at strip size, a
// per-pad Lock/Regen/Next target would be far under the 34 px minimum.
class ChordGenPanel : public juce::Component,
                      private juce::Timer
{
public:
    explicit ChordGenPanel(KeysProcessor&);
    ~ChordGenPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override; // swallow clicks so the editor below is inert

    std::function<void()> onClose;      // dismiss the overlay
    std::function<void()> onKeyChanged; // emotion presets move Root/Scale too; editor repaints

private:
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // One pad's row of controls in the grid: the chord itself (press to audition), plus
    // the three actions Octavium hid in a context menu.
    struct PadRow
    {
        juce::TextButton play;   // shows the chord name; press-and-hold auditions it
        juce::TextButton lock;   // keep this chord through a Regenerate, and bias it
        juce::TextButton regen;  // a new chord for this pad's scale degree
        juce::TextButton next;   // "what could follow this?" -> suggestion menu
        bool playHeld = false;   // edge-detects the press, so audition fires and stops once
    };

    void timerCallback() override;
    void buildControls();
    void applyEmotion(int emotionIndex);
    void fillPage(bool onlyUnlocked);
    void clearPage();
    void regeneratePad(int slot);
    void showSuggestions(int slot);
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

    juce::OwnedArray<juce::TextButton> emotionButtons;
    juce::Label emotionLabel;

    juce::TextButton fillButton { "Fill Page" };
    juce::TextButton regenButton { "Regen Unlocked" };
    juce::TextButton clearButton { "Clear Page" };

    // The Markov source's controls; visible only while Source is Markov. Mood and
    // Start are transient (like the wheels): performance choices, not session state.
    juce::ComboBox sourceBox, chainBox, moodBox, startBox;
    juce::Label sourceLabel, chainLabel, moodLabel, startLabel;
    juce::Slider tempSlider, lengthSlider;
    juce::Label tempLabel, lengthLabel;

    std::array<std::unique_ptr<PadRow>, KeysProcessor::padsPerPage> padRows;

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
