#pragma once

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>

namespace keys
{
// The Chance editor (docs/CHANCE_DESIGN.md): a probabilistic note source sitting beside the
// arpeggiator, laid out the way ArpPanel is - captioned, ruled control groups rather than
// controls strung across loose rows. Chance has no shape that changes its control count (the
// arp's Pattern step editor has no equivalent here), so unlike ArpPanel this panel's height
// never changes and preferredHeight() is a constant.
//
// It lives inline as the Chance section's content, parented into KeysEditor's chanceHolder
// (or that holder's detached window). No title, On or Close here: the Chance section bar
// above carries the name, the On toggle (which also survives the section folding, per
// CLAUDE.md's arp-On precedent) and Detach.
//
// Every control is an ordinary APVTS attachment except the two Mode rows, which are plain
// juce::TextButtons in a radio group writing straight to the choice parameter (see
// docs/CHANCE_DESIGN.md, "Modes": three states each, and the mouse-only contract prefers a
// visible set of buttons to a dropdown for that).
class ChancePanel : public juce::Component,
                    private juce::Timer
{
public:
    explicit ChancePanel(KeysProcessor&);
    ~ChancePanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Chance's control count never changes, so this is a fixed number - no
    // onPreferredHeightChanged callback the way ArpPanel needs one for its step editor.
    int preferredHeight() const;

private:
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void buildControls();
    void stepCombo(juce::ComboBox&, int delta); // the < > pair beside Length, ArpPanel's idiom
    void setTMode(int index);                   // Mode row buttons -> chanceTMode, by hand
    void setXMode(int index);                   // Mode row buttons -> chanceXMode, by hand
    void refreshModeButtons();                  // parameters -> which Mode button is lit
    void refreshPhraseButtons();                // Freeze's label + enabled state

    // The captioned, ruled group boxes the band is drawn as (ArpPanel's idiom exactly):
    // a hairline frame with a gap punched through the top rule for the caption. Filled in
    // by resized(), painted by paint().
    struct Group
    {
        juce::String caption;
        juce::Rectangle<int> bounds;
    };
    std::array<Group, 5> groups; // Rhythm, Pitch, Key, Mode, Phrase

    KeysProcessor& processor;

    // Rhythm: the t generator. Deja Vu is centre-detented at 50 (the frozen loop); per
    // CLAUDE.md there is no double-click in this plugin, so there is no
    // setDoubleClickReturnValue reset gesture - the centre is only ever found by looking at
    // the value readout, which is why TextBoxBelow stays on rather than being hidden.
    juce::Slider densitySlider, dejaVuSlider, jitterSlider;
    juce::Label densityLabel, dejaVuLabel, jitterLabel;
    juce::ComboBox loopLenBox;
    juce::Label loopLenLabel;
    juce::TextButton loopLenPrev { "<" }, loopLenNext { ">" };

    // Pitch: the X generator.
    juce::Slider spreadSlider, biasSlider, tempSlider, wanderSlider;
    juce::Label spreadLabel, biasLabel, tempLabel, wanderLabel;

    // Key: the two scale-adherence axes. Horizontal linear sliders, deliberately not knobs,
    // so they read as a different kind of control from the two generators either side of them.
    juce::Slider keySlider, chordPullSlider;
    juce::Label keyLabel, chordPullLabel;

    // Mode: two independent tri-state selectors, each a left-click radio group of
    // juce::TextButtons (not combo boxes - a visible set beats a dropdown here, and each
    // group writes its choice parameter directly rather than through an attachment).
    juce::Label rhythmModeLabel, voiceModeLabel;
    juce::TextButton tCoinButton { "Coin" }, tEuclidButton { "Euclid" }, tBurstsButton { "Bursts" };
    juce::TextButton xLineButton { "Line" }, xDuetButton { "Duet" }, xClusterButton { "Cluster" };

    // Phrase: the three actions. Freeze's text and enabled state follow the processor (there
    // is nothing to freeze until Chance has actually played something), refreshed on the timer.
    juce::TextButton generateButton { "Generate" };
    juce::ToggleButton learnButton { "Learn" };
    juce::TextButton freezeButton { "Freeze" };

    std::unique_ptr<SliderAtt> densityAtt, dejaVuAtt, jitterAtt;
    std::unique_ptr<ComboAtt> loopLenAtt;
    std::unique_ptr<SliderAtt> spreadAtt, biasAtt, tempAtt, wanderAtt;
    std::unique_ptr<SliderAtt> keyAtt, chordPullAtt;
    std::unique_ptr<ButtonAtt> learnAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChancePanel)
};
} // namespace keys
