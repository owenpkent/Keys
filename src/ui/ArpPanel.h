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

    // One lane: the tab that selects it and the grid it shows. Length and clock
    // division used to live here, per lane, which meant six copies of both on screen at
    // once with no room left to label any of them. With one lane visible there is one
    // of each, shared, below the grid.
    struct LaneRow
    {
        juce::TextButton tab;
        std::unique_ptr<LaneGrid> grid;
    };

    void timerCallback() override;
    void buildControls();
    void buildLaneRow(LaneRow&, ArpEngine::Lane, const juce::String& name, int loVal, int hiVal);
    void selectLane(int lane);
    void nudgeLength(int delta); // selected lane, or every lane while Link is on
    void cycleClockDiv();
    void refreshLaneReadouts();
    void refreshPatternButtons();
    void recallOrCopy(int index);

    bool patternMode() const; // Shape == "Pattern": the step editor is in play
    void applyShapeChoice();  // combo -> parameters
    void refreshShape();      // parameters -> combo, and show/hide the step editor

    // The card the panel draws, sized to the controls actually in it. The overlay still
    // covers the editor (it dims what's behind and swallows clicks), but on a shape
    // there are two rows to show and no reason to draw a full-height empty box.
    juce::Rectangle<int> cardBounds() const;

    int lastPatternMode = -1; // -1 = not yet laid out; else the last bool seen

    KeysProcessor& processor;

    juce::Label title;
    juce::ToggleButton onButton { "On" };
    juce::TextButton closeButton { "Close" };

    // Shape carries the eight directions plus "Pattern", after Serum 2, whose step
    // editor only exists while SHAPE is "Pattern". It cannot be a plain APVTS
    // attachment because it spans two parameters (arpDirection + arpPattern).
    juce::ComboBox rateBox, shapeBox;
    juce::Label rateLabel, shapeLabel;
    juce::ToggleButton dotButton { "Dot" }, tripButton { "Trip" }, anchorButton { "Anchor" };
    juce::Slider octavesSlider, swingSlider;
    juce::Label octavesLabel, swingLabel;
    juce::ToggleButton latchButton { "Latch" }, retriggerButton { "Retrigger" };

    std::array<LaneRow, ArpEngine::numLanes> laneRows;
    int selectedLane = (int) ArpEngine::laneNote;
    std::unique_ptr<MuteRow> muteRow;
    juce::Label muteRowLabel;

    // The shared length / clock-division controls for whichever lane is showing.
    juce::Label stepsLabel, speedLabel, stepsReadout;
    juce::TextButton stepsMinus { "-" }, stepsPlus { "+" }, speedButton;
    juce::ToggleButton linkButton { "Link lanes" };

    std::array<juce::TextButton, KeysProcessor::numArpPatterns> patternButtons;
    juce::TextButton copyButton { "Copy" };
    juce::TextButton cancelButton { "Cancel" };
    juce::TextButton randomizeButton { "Randomize" };
    bool copyArmed = false;
    int copyFromIndex = -1;

    std::unique_ptr<ButtonAtt> onAtt, dotAtt, tripAtt, anchorAtt, latchAtt, retriggerAtt, linkAtt;
    std::unique_ptr<ComboAtt> rateAtt;
    std::unique_ptr<SliderAtt> octavesAtt, swingAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpPanel)
};
} // namespace keys
