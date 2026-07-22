#pragma once

#include "../ChordGen.h"
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
// It is an overlay rather than a dialog: a plugin editor has no business opening OS
// windows, and an overlay keeps every target inside the one surface the mouse is already
// in. It fills the *current pad page*, so the four pages can hold four different keys.
//
// The pad grid is repeated at full size (not the strip) so each pad is a generous
// play target. Per-pad actions (Lock / New chord / Next suggestions) live in the
// pad's right-click menu, restoring Octavium's card menu at Owen's request; the
// page-wide left-click buttons (Fill / Regen / Clear) remain the bulk path. This is
// the one deliberate exception to the "right-click is only an accelerator" rule —
// see the Invariants section of CLAUDE.md.
class ChordGenPanel : public juce::Component,
                      private juce::Timer
{
public:
    explicit ChordGenPanel(KeysProcessor&);
    ~ChordGenPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override; // swallow clicks so the editor below is inert

    std::function<void()> onClose; // dismiss the overlay

private:
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // The pad itself: left-click press-and-hold auditions (plain Button behaviour),
    // right-click opens the pad's action menu. Paints a full chord card — name, the
    // note list with octave numbers, a mini keyboard of what's held, the lock dot —
    // so the grid needs no extra widgets.
    struct PadButton : juce::TextButton
    {
        std::function<void()> onRightClick;
        bool locked = false;
        std::vector<int> notes; // what the card renders; pushed from the panel timer

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
            {
                if (onRightClick)
                    onRightClick();
                return; // not a press: never auditions, never shows the down state
            }
            juce::TextButton::mouseDown(e);
        }

        void paintButton(juce::Graphics&, bool over, bool down) override; // ChordGenPanel.cpp
    };

    struct PadRow
    {
        PadButton play;        // shows the chord name; press-and-hold auditions it
        bool playHeld = false; // edge-detects the press, so audition fires and stops once
    };

    void timerCallback() override;
    void buildControls();
    void fillPage(bool onlyUnlocked);
    void clearPage();
    void regeneratePad(int slot);
    void newChordFor(int slot); // regenerate a filled pad, or conjure one for an empty slot
    void showPadMenu(int slot); // the right-click card menu: Lock / New chord / Next
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
