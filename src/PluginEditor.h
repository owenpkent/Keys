#pragma once

#include "PluginProcessor.h"
#include "ui/ArpPanel.h"
#include "ui/ChordGenPanel.h"
#include "ui/ChordPads.h"
#include "ui/KeysLookAndFeel.h"
#include "ui/KnobBank.h"
#include <okstudio/Updater.h>
#include <memory>

// The playing surface, the piano, is picked at compile time, not by a tab: one
// product, one surface; see CHANGELOG for why the old five-tab arrangement is gone.
#include "ui/PianoKeyboard.h"

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
    void parentHierarchyChanged() override; // skins the standalone wrapper's window chrome

    // True when this editor lives inside another editor (Keys Host). An embedded
    // editor must never setSize() itself; the parent owns geometry and reacts to
    // layout changes on its own.
    void setEmbedded(bool b) { embedded = b; }

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
    void toggleArpPanel();
    void toggleEditPad(int slot); // link a chord pad to the keyboard for editing
    void endPadEdit();

    KeysProcessor& processor;
    KeysLookAndFeel lnf;

    // The default LookAndFeel_V4 linear slider caps its track at ~6 px no matter how
    // wide the component is; the performance wheels want a hardware-wheel look — a
    // wide groove with a chunky grab bar.
    struct WheelLookAndFeel : KeysLookAndFeel
    {
        void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                              float minPos, float maxPos, juce::Slider::SliderStyle,
                              juce::Slider&) override;
    };
    WheelLookAndFeel wheelLnf;

    juce::Label title;
    juce::Rectangle<int> titleCaption; // "OK STUDIO" wordmark, painted under the title
    juce::Component::SafePointer<juce::DocumentWindow> styledWindow; // standalone chrome we skinned

    // Nothing was displaying the setTooltip text scattered through the arp and chord
    // panels: JUCE only shows tooltips if a TooltipWindow exists, and there was none, so
    // 19 explanations across the plugin were dead code. Parented to the editor rather
    // than the desktop so it stays inside the plugin window in a host, and one instance
    // only (Keys Host embeds exactly one KeysEditor). Short delay: the point is to
    // answer "what is this" quickly, and holding a mouse still is work here.
    juce::TooltipWindow tooltips { this, 450 };

    // The one playing surface this product builds: the piano, for Keys and Keys Host.
    PianoKeyboard keyboard;

    KnobBank knobBank; // eight CC knobs, replaces the old Fader/XY surfaces
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
    juce::ToggleButton chordExclusiveButton { "Exclusive" };
    juce::TextButton panicButton { "All Off" };
    juce::TextButton updateButton;

    // Chord-pad page navigation, and the door to the generator overlay.
    juce::TextButton pagePrevButton { "<" }, pageNextButton { ">" }, chordsButton { "Chords" };
    juce::Label pageLabel;
    std::unique_ptr<ChordGenPanel> genPanel; // only alive while the overlay is open

    // The arpeggiator overlay's door (docs/ARP_DESIGN.md); only one overlay is ever
    // open at a time, see toggleGenPanel()/toggleArpPanel().
    juce::TextButton arpButton { "Arp" };
    std::unique_ptr<ArpPanel> arpPanel;

    juce::Slider humanizeVelSlider, humanizeTimeSlider; // velocity is a two-value range
    juce::Label humanizeVelLabel, humanizeTimeLabel;

    std::unique_ptr<ComboAtt> sizeAtt, rootAtt, scaleAtt, channelAtt, curveAtt, chordStrumDirAtt, polyphonyAtt;
    std::unique_ptr<SliderAtt> velocityAtt, octaveAtt, humanizeTimeAtt, chordStrumAtt;
    std::unique_ptr<ButtonAtt> scaleLockAtt, sustainAtt, latchAtt, humanizeAtt, chordExclusiveAtt;

    okstudio::updater::Config updaterConfig;
    okstudio::updater::UpdateInfo pendingUpdate;
    int editingPad = -1;              // pad slot linked to the keyboard for editing, or -1
    std::vector<int> lastEditNotes;   // last content written back, to detect changes cheaply
    int lastChannel = -1;    // to panic on MIDI-channel change (avoids notes stuck on the old channel)
    bool embedded = false;   // see setEmbedded()
    bool lastSustain = false; // to release held pad chords when the sustain pedal lifts
    bool pitchReturning = false; // pitch wheel is gliding back to centre (Octavium's ~160 ms ease)
    float panicFlash = 0.0f;  // 1 -> 0 decay behind the All Off button, on an explicit click only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysEditor)
};
} // namespace keys
