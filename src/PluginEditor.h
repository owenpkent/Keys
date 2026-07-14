#pragma once

#include "PluginProcessor.h"
#include "ui/PianoKeyboard.h"
#include <okstudio/Theme.h>
#include <okstudio/Updater.h>

namespace keys
{
class KeysEditor : public juce::AudioProcessorEditor,
                   private juce::Timer
{
public:
    explicit KeysEditor(KeysProcessor&);
    ~KeysEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    float currentVelocity01() const;
    void addCombo(juce::ComboBox&, juce::Label&, const juce::String& text, const juce::StringArray& items,
                  const juce::String& paramID, std::unique_ptr<ComboAtt>&);
    void showUpdate(const okstudio::updater::UpdateInfo&);

    KeysProcessor& processor;
    okstudio::theme::LookAndFeel lnf;

    juce::Label title;
    PianoKeyboard keyboard;

    juce::ComboBox sizeBox, rootBox, scaleBox, channelBox, curveBox;
    juce::Label sizeLabel, rootLabel, scaleLabel, channelLabel, curveLabel;
    juce::Slider velocitySlider, octaveSlider;
    juce::Label velocityLabel, octaveLabel;

    juce::ToggleButton scaleLockButton { "Scale Lock" };
    juce::ToggleButton sustainButton { "Sustain" };
    juce::ToggleButton latchButton { "Latch" };
    juce::ToggleButton humanizeButton { "Humanize" };
    juce::TextButton panicButton { "All Off" };
    juce::TextButton updateButton;

    juce::Slider humanizeVelMinSlider, humanizeVelMaxSlider, humanizeTimeSlider;
    juce::Label humanizeVelMinLabel, humanizeVelMaxLabel, humanizeTimeLabel;

    std::unique_ptr<ComboAtt> sizeAtt, rootAtt, scaleAtt, channelAtt, curveAtt;
    std::unique_ptr<SliderAtt> velocityAtt, octaveAtt, humanizeVelMinAtt, humanizeVelMaxAtt, humanizeTimeAtt;
    std::unique_ptr<ButtonAtt> scaleLockAtt, sustainAtt, latchAtt, humanizeAtt;

    okstudio::updater::Config updaterConfig;
    okstudio::updater::UpdateInfo pendingUpdate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysEditor)
};
} // namespace keys
