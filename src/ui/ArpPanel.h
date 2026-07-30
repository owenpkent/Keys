#pragma once

#include "../ArpEngine.h"
#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>

namespace keys
{
// The arpeggiator editor (docs/ARP_DESIGN.md). A band of captioned control groups, the
// six per-parameter step lanes (Cthulhu architecture) when Shape is "Pattern", and a row
// of twelve launchable slots along the bottom.
//
// The band and the slot row follow a hardware-arp layout Owen asked for (2026-07-25):
// controls gathered into ruled, captioned groups rather than strung across two loose
// rows, and the pattern memories turned from lettered buttons into cards that show what
// they will play - a chord name, a shape and a rate - with a launch triangle. Launching a
// slot is the one-click "pass a card into the arpeggiator" gesture: it installs the
// pattern, applies the slot's shape and rate, and holds the slot's chord into the arp.
//
// It lives inline, as the editor's centre view: picking Arp swaps it in where the knob
// bank sat, instead of throwing a dimmed sheet over the whole plugin. The old behaviour
// hid the keyboard behind the panel, which is backwards for a plugin you play while you
// edit the arp. setInlineMode(false) restores the overlay look (no caller does today;
// kept because the class is shared with Keys Host).
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

    // Inline: draw as a plain card filling our bounds, with no scrim behind it.
    void setInlineMode(bool);

    // How tall the panel needs to be to show everything at full-size targets. Changes
    // with Shape (a direction shape has two rows; Pattern adds the whole step editor),
    // so the editor is told when it moves.
    int preferredHeight() const;
    std::function<void()> onPreferredHeightChanged;

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

    // One launchable slot. It paints what it will play - the chord it holds, the shape and
    // the rate it will install - so a row of twelve reads as a progression rather than as
    // twelve identical letters. Left-click launches it. Right-click opens its menu, an
    // accelerator only: everything in there has a left-click path on the buttons below the
    // row (the same arrangement the chord pads use, per the CLAUDE.md exception).
    class SlotCard : public juce::Button
    {
    public:
        SlotCard(KeysProcessor&, int index);

        void paintButton(juce::Graphics&, bool over, bool down) override;
        void mouseDown(const juce::MouseEvent&) override;

        std::function<void()> onRightClick;

    private:
        KeysProcessor& processor;
        int index;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlotCard)
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
    void launchSlot(int index);   // left-click on a slot card
    void showSlotMenu(int index); // right-click accelerator; everything in it is also a button
    void stepCombo(juce::ComboBox&, int delta); // the < > pair beside Shape and Rate

    // The captioned, ruled group boxes the band is drawn as. Filled in by resized() and
    // painted by paint(), because a caption and a rule are two lines of Graphics each and
    // do not justify six more child components.
    struct Group
    {
        juce::String caption;
        juce::Rectangle<int> bounds;
        bool visible = true;
    };
    std::array<Group, 5> groups;

    bool patternMode() const; // Shape == "Pattern": the step editor is in play
    void applyShapeChoice();  // combo -> parameters
    void refreshShape();      // parameters -> combo, and show/hide the step editor
    // Retrigger spans two parameters the same way Shape does (a bool for "on a new chord"
    // and a choice for "every N beats"), so it cannot be a plain attachment either. One
    // combo, because the two are alternatives in practice and Ableton proved the list.
    void applyRetrigChoice();
    void refreshRetrig();

    // The card the panel draws, sized to the controls actually in it. The overlay still
    // covers the editor (it dims what's behind and swallows clicks), but on a shape
    // there are two rows to show and no reason to draw a full-height empty box.
    juce::Rectangle<int> cardBounds() const;

    bool inlineMode = false;
    int lastPatternMode = -1; // -1 = not yet laid out; else the last bool seen

    KeysProcessor& processor;

    // No title, On or Close: the Arp section bar above the panel carries all three, and two
    // On toggles bound to the same parameter is just a thing to get wrong.

    // Shape carries the eight directions plus "Pattern", after Serum 2, whose step
    // editor only exists while SHAPE is "Pattern". It cannot be a plain APVTS
    // attachment because it spans two parameters (arpDirection + arpPattern).
    juce::ComboBox rateBox, shapeBox, distanceBox, retrigBox;
    juce::Label rateLabel, shapeLabel, distanceLabel, retrigLabel;
    // The < > pairs beside Shape and Rate. Not decoration: stepping to the next shape is
    // the commonest thing you do to an arp, and a button is one click where the combo is a
    // click, a travel and a second click.
    juce::TextButton shapePrev { "<" }, shapeNext { ">" }, ratePrev { "<" }, rateNext { ">" };
    juce::ToggleButton dotButton { "Dot" }, tripButton { "Trip" }, anchorButton { "Anchor" };
    juce::Slider octavesSlider, swingSlider, gateSlider, chanceSlider;
    juce::Label octavesLabel, swingLabel, gateLabel, chanceLabel;
    juce::ToggleButton latchButton { "Latch" };
    // The second band row (2026-07-30). SPREAD is Repeats + Distance + Offset - how far the
    // chord is stacked and where the run starts; FEEL is the three that decide whether it
    // sounds played. Horizontal sliders rather than the band's rotaries: a knob column spans
    // both rows of a group and this row is one row tall, which is what keeps the panel from
    // growing by a whole band.
    juce::Slider offsetSlider, rampSlider, rampTimeSlider, humanSlider;
    juce::Label offsetLabel, rampLabel, rampTimeLabel, humanLabel;

    std::array<LaneRow, ArpEngine::numLanes> laneRows;
    int selectedLane = (int) ArpEngine::laneNote;
    std::unique_ptr<MuteRow> muteRow;
    juce::Label muteRowLabel;

    // The shared length / clock-division controls for whichever lane is showing.
    juce::Label stepsLabel, speedLabel, stepsReadout;
    juce::TextButton stepsMinus { "-" }, stepsPlus { "+" }, speedButton;
    juce::ToggleButton linkButton { "Link" }; // "Link lanes" no longer fits the STEPS group

    // Twelve slot cards, alive in both shapes: launching a chord is as useful on a plain
    // "Up" as it is on an edited pattern, so unlike the lane editor these never hide.
    std::array<std::unique_ptr<SlotCard>, KeysProcessor::numArpPatterns> slotCards;
    juce::TextButton copyButton { "Copy" };
    juce::TextButton clearButton { "Clear" };
    juce::TextButton cancelButton { "Cancel" };
    juce::TextButton randomizeButton { "Randomize" };
    juce::TextButton stopButton { "Stop" }; // release the launched chord, without a panic
    // Progression mode: Chain walks the slots that hold a chord, each for its own number of
    // bars. Bars edits the *active* slot - the one whose lanes the editor is showing - which
    // a slot click already makes it, so setting a length is click the card, click the plus.
    juce::TextButton chainButton { "Chain" };
    juce::TextButton barsMinus { "-" }, barsPlus { "+" };
    juce::Label barsReadout;
    void nudgeBars(int delta);

    // Copy and Clear both need a slot to act on, and neither may be right-click-only (the
    // mouse-only contract wants a left-click path for everything). Both arm: click the
    // button, then click the slot. One state, not two flags, so arming one disarms the
    // other rather than leaving two half-armed modes fighting over the next click.
    enum Armed { armNone = 0, armCopy, armClear };
    Armed armed = armNone;
    void setArmed(Armed, int fromIndex = -1);
    int copyFromIndex = -1;

    std::unique_ptr<ButtonAtt> dotAtt, tripAtt, anchorAtt, latchAtt, linkAtt;
    std::unique_ptr<ComboAtt> rateAtt, distanceAtt;
    std::unique_ptr<SliderAtt> octavesAtt, swingAtt, gateAtt, chanceAtt;
    std::unique_ptr<SliderAtt> offsetAtt, rampAtt, rampTimeAtt, humanAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpPanel)
};
} // namespace keys
