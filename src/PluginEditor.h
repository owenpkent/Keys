#pragma once

#include "PluginProcessor.h"
#include "ui/ChordGenPanel.h"
#include "ui/ChordPads.h"
#include "ui/FaderBank.h"
#include "ui/HarmonicTable.h"
#include "ui/PadGrid.h"
#include "ui/PianoKeyboard.h"
#include "ui/XYPad.h"
#include <okstudio/Theme.h>
#include <okstudio/Updater.h>
#include <array>
#include <memory>

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
    void stepPadPage(int delta);
    void toggleGenPanel();
    int surfaceIndex() const; // 0 Keys, 1 Hex, 2 Pads, 3 Faders, 4 XY
    void applySurfaceVisibility();

    KeysProcessor& processor;
    okstudio::theme::LookAndFeel lnf;

    juce::Label title;

    // The five playing surfaces; the Surface tabs pick which one fills the playing area.
    PianoKeyboard keyboard;
    HarmonicTable hexTable;
    PadGrid padGrid;
    FaderBank faderBank;
    XYPad xyPad;
    std::array<juce::TextButton, 5> surfaceButtons;

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

    // Chord-pad page navigation, and the door to the generator overlay.
    juce::TextButton pagePrevButton { "<" }, pageNextButton { ">" }, chordsButton { "Chords" };
    juce::Label pageLabel;
    std::unique_ptr<ChordGenPanel> genPanel; // only alive while the overlay is open

    juce::Slider humanizeVelSlider, humanizeTimeSlider; // velocity is a two-value range
    juce::Label humanizeVelLabel, humanizeTimeLabel;

    std::unique_ptr<ComboAtt> sizeAtt, rootAtt, scaleAtt, channelAtt, curveAtt, chordStrumDirAtt, polyphonyAtt;
    std::unique_ptr<SliderAtt> velocityAtt, octaveAtt, humanizeTimeAtt, chordStrumAtt;
    std::unique_ptr<ButtonAtt> scaleLockAtt, sustainAtt, latchAtt, humanizeAtt, chordExclusiveAtt;

    okstudio::updater::Config updaterConfig;
    okstudio::updater::UpdateInfo pendingUpdate;
    int lastChannel = -1;    // to panic on MIDI-channel change (avoids notes stuck on the old channel)
    int lastPadChannel = -1; // same idea for the Pad Grid's own channel
    int lastSurface = -1;    // to silence a surface as you switch away from it
    bool lastSustain = false; // to release held pad chords when the sustain pedal lifts
    bool pitchReturning = false; // pitch wheel is gliding back to centre (Octavium's ~160 ms ease)
    float panicFlash = 0.0f;  // 1 -> 0 decay behind the All Off button, on an explicit click only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysEditor)
};
} // namespace keys
