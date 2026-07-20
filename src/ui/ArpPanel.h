#pragma once

#include "../ArpEngine.h"
#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>

namespace keys
{
// The arpeggiator editor (docs/ARP_DESIGN.md): an overlay opened from the tabs row,
// same contract as ChordGenPanel. Six per-parameter step lanes (Cthulhu architecture)
// sit under a globals row, with pattern recall/copy/randomize along the bottom.
//
// Lane data lives in processor.arp.lanes as arrays of std::atomic<int>, written here
// on the message thread and read on the audio thread; no locking, so every edit is a
// direct store(). Globals are ordinary APVTS-attached controls.
class ArpPanel : public juce::Component,
                 private juce::Timer
{
public:
    explicit ArpPanel(KeysProcessor&);
    ~ArpPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override; // swallow clicks so the editor below is inert

    std::function<void()> onClose;

    // LaneGrid and MuteRow are implementation detail, but public: their member
    // functions are defined out-of-line in ArpPanel.cpp, which needs to name them,
    // and a private nested class cannot be named outside ArpPanel's own members.

    // One lane's step grid: `length` cells span the full width (so they widen as the
    // pattern shortens), each cell a bar sized to its value within [loVal, hiVal].
    // Plain left-click sets the clicked step; a left-drag paints every step it
    // crosses, live, with a value readout following the cursor (no modifiers, no
    // commit step, per the mouse-only contract).
    class LaneGrid : public juce::Component
    {
    public:
        LaneGrid(KeysProcessor&, ArpEngine::Lane, int loVal, int hiVal);

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;

    private:
        int currentLength() const;
        int stepAtX(float x) const;
        int valueAtY(float y) const;
        void paintStepFromMouse(const juce::MouseEvent&);
        juce::String cellText(int value) const;

        KeysProcessor& processor;
        ArpEngine::Lane lane;
        int loVal, hiVal;
        bool dragging = false;
        juce::Point<float> cursorPos;
        int cursorValue = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LaneGrid)
    };

    // The note lane's mute row: one button per step, flipping that step's note value
    // between -1 (muted) and 0 (follow). A dedicated row per the spec, rather than the
    // drag-below-range accelerator Octavium hid the same toggle behind.
    class MuteRow : public juce::Component
    {
    public:
        explicit MuteRow(KeysProcessor&);

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;

    private:
        int currentLength() const;
        int stepAtX(float x) const;
        void applyAtX(float x);

        KeysProcessor& processor;
        bool dragging = false;
        int paintValue = 0; // the value (-1 or 0) this drag is painting

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MuteRow)
    };

private:
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // One lane's full row: name, the grid, and the length/clock-div controls to its
    // right. Bundled so buildLanes() can loop instead of repeating six times.
    struct LaneRow
    {
        juce::Label name;
        std::unique_ptr<LaneGrid> grid;
        juce::Label lengthReadout;
        juce::TextButton lenMinus { "-" }, lenPlus { "+" };
        juce::TextButton clockDiv;
    };

    void timerCallback() override;
    void buildControls();
    void buildLaneRow(LaneRow&, ArpEngine::Lane, const juce::String& name, int loVal, int hiVal);
    void cycleClockDiv(ArpEngine::Lane);
    void refreshLaneReadouts();
    void refreshPatternButtons();
    void recallOrCopy(int index);

    KeysProcessor& processor;

    juce::Label title;
    juce::ToggleButton onButton { "On" };
    juce::TextButton closeButton { "Close" };

    juce::ComboBox rateBox, directionBox;
    juce::Label rateLabel, directionLabel;
    juce::ToggleButton dotButton { "Dot" }, tripButton { "Trip" }, anchorButton { "Anchor" };
    juce::Slider octavesSlider, swingSlider;
    juce::Label octavesLabel, swingLabel;
    juce::ToggleButton latchButton { "Latch" }, retriggerButton { "Retrigger" };

    std::array<LaneRow, ArpEngine::numLanes> laneRows;
    std::unique_ptr<MuteRow> muteRow;
    juce::Label muteRowLabel;

    std::array<juce::TextButton, KeysProcessor::numArpPatterns> patternButtons;
    juce::TextButton copyButton { "Copy" };
    juce::TextButton cancelButton { "Cancel" };
    juce::TextButton randomizeButton { "Randomize" };
    bool copyArmed = false;
    int copyFromIndex = -1;

    std::unique_ptr<ButtonAtt> onAtt, dotAtt, tripAtt, anchorAtt, latchAtt, retriggerAtt;
    std::unique_ptr<ComboAtt> rateAtt, directionAtt;
    std::unique_ptr<SliderAtt> octavesAtt, swingAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpPanel)
};
} // namespace keys
