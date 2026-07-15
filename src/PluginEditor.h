#pragma once

#include "PluginProcessor.h"
#include "ui/PianoKeyboard.h"
#include "ui/ChordPads.h"
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
    void addCombo(juce::ComboBox&, juce::Label&, const juce::String& text, const juce::StringArray& items,
                  const juce::String& paramID, std::unique_ptr<ComboAtt>&);
    void showUpdate(const okstudio::updater::UpdateInfo&);

    KeysProcessor& processor;
    okstudio::theme::LookAndFeel lnf;

    juce::Label title;
    PianoKeyboard keyboard;
    ChordPads chordPads;

    juce::ComboBox sizeBox, rootBox, scaleBox, channelBox, curveBox, chordStrumDirBox, polyphonyBox;
    juce::Label sizeLabel, rootLabel, scaleLabel, channelLabel, curveLabel, chordStrumDirLabel, polyphonyLabel;
    juce::Slider velocitySlider, octaveSlider, chordStrumSlider;
    juce::Label velocityLabel, octaveLabel, chordStrumLabel;
    juce::Slider modWheel, pitchWheel;  // transient performance wheels (no persistence)
    juce::Label modLabel, pitchLabel;

    juce::ToggleButton scaleLockButton { "Scale Lock" };
    juce::ToggleButton sustainButton { "Sustain" };
    juce::ToggleButton latchButton { "Latch" };
    juce::ToggleButton humanizeButton { "Humanize" };
    juce::ToggleButton chordExclusiveButton { "Excl" };
    juce::TextButton panicButton { "All Off" };
    juce::TextButton updateButton;

    juce::Slider humanizeVelSlider, humanizeTimeSlider; // velocity is a two-value range
    juce::Label humanizeVelLabel, humanizeTimeLabel;

    std::unique_ptr<ComboAtt> sizeAtt, rootAtt, scaleAtt, channelAtt, curveAtt, chordStrumDirAtt, polyphonyAtt;
    std::unique_ptr<SliderAtt> velocityAtt, octaveAtt, humanizeTimeAtt, chordStrumAtt;
    std::unique_ptr<ButtonAtt> scaleLockAtt, sustainAtt, latchAtt, humanizeAtt, chordExclusiveAtt;

    okstudio::updater::Config updaterConfig;
    okstudio::updater::UpdateInfo pendingUpdate;
    int lastChannel = -1; // to panic on MIDI-channel change (avoids notes stuck on the old channel)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysEditor)
};
} // namespace keys
