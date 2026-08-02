#pragma once

#include "../ArpEngine.h"
#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <okstudio/RotaryKnob.h>
#include <array>
#include <functional>
#include <memory>

namespace keys
{
// The arpeggiator editor (docs/ARP_DESIGN.md). A band of captioned control groups, the
// ten per-parameter step lanes (Cthulhu architecture) when Shape is "Pattern", and a row
// of twelve launchable slots along the bottom.
//
// The band and the slot row follow a hardware-arp layout Owen asked for (2026-07-25):
// controls gathered into ruled, captioned groups rather than strung across two loose
// rows, and the pattern memories turned from lettered buttons into cards that show what
// they will play - a chord name, a shape and a rate - with a launch triangle. Launching a
// slot is the one-click "pass a card into the arpeggiator" gesture: it installs the
// pattern, applies the slot's shape and rate, and holds the slot's chord into the arp.
//
// It lives inline, as the content of the editor's Arp section: unfolding that section
// makes room for it between the pads and the keybed, instead of throwing a dimmed sheet
// over the whole plugin. The old behaviour hid the keyboard behind the panel, which is
// backwards for a plugin you play while you edit the arp. setInlineMode(false) restores
// the overlay look (no caller does today; kept because the class is shared with Keys Host).
//
// Folding the section destroys this object, so nothing on it can be the only way to reach
// a running arpeggiator: On and Hold off ride the section bar for exactly that reason, and
// the Stop button below is the panel's own copy of the second of them.
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

    // Which of the three arpeggiator lines everything on this panel is editing: the band, the
    // step lanes, the twelve slots, Bars and Chain. One row of controls, three lines behind
    // it, chosen by the A/B/C tabs at the left of the slot row. It is the processor's state
    // rather than the panel's, because a click on a chord card feeds the same line and the
    // Pads bar has to be able to say so with this panel folded away.
    int editLine() const;
    // `leaveMacroView` false sets the line without changing what is on screen. A drop passes
    // false: it is routing a chord, not navigating, and in the macro view all three lines are
    // in front of you already, so there is nothing to switch to.
    void setEditLine(int line, bool leaveMacroView = true);
    // The macro view: all three lines at once, in place of the band and the step editor. It is
    // a *view*, not a fourth line - the current line stays whatever it was, so a chord card
    // click still has one unambiguous target while all three are on screen.
    bool isMacroView() const { return macroView; }
    void setMacroView(bool);
    // Told when a tab is clicked, so the editor can move the Pads bar's letter chip with it.
    std::function<void()> onEditLineChanged;

    // A drop target for a chord card dragged out of the pad strip, in screen coordinates:
    // the slot card under `screenPos`, or -1. Mirrors ChordPads::externalDropSlotAt, and for
    // the same reason - the two surfaces can be in different top-level windows, so the editor
    // that holds both passes screen positions between them.
    int externalDropSlotAt(juce::Point<int> screenPos) const;
    // Same, for the line tabs: the line under `screenPos`, or -1. A card dropped on a tab is
    // handed to that line there and then, without going through a slot.
    int externalDropLineAt(juce::Point<int> screenPos) const;
    // Paint the slot or tab a drag is currently over (-1 = none). Set by the editor while a
    // card is being dragged, so the target lights up before the mouse is released.
    void setExternalDropTarget(int slot, int lineTab);

    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // One arpeggiator line, in one row: the four settings that decide how it sits against the
    // other two, what it is holding, and a way to start it. Three of these are the **macro
    // view**, which is what the fourth tab on the slot row selects (2026-08-01, Owen: "a fourth
    // option for a simplified version that shows a little bit of all of them ... the goal is to
    // be able to create complex polyrhythms from one view").
    //
    // Each row's attachments are bound to its own line for the row's whole life, unlike the
    // band above, which rebinds every time the tabs move. That is the point of the row: three
    // lines on screen at once cannot each be "the current line".
    class MacroRow : public juce::Component
    {
    public:
        MacroRow(KeysProcessor&, int line);

        void paint(juce::Graphics&) override;
        void resized() override;
        // Readouts that no attachment drives: the rate text (it spans two parameters and two
        // units), the shape, and the chord this line is holding. Called by the panel's timer.
        void refresh();
        // Lit while a chord card dragged out of the pad strip is over this row. The row is the
        // line here, laid out large, so it is a far easier target than the tab that names it.
        void setDropTarget(bool);

        // The eight knobs a row carries, left to right. One table so the labels, the
        // parameters and the layout cannot drift apart; the headings are drawn once, on the
        // top row, and every row reserves the same strip so the columns line up.
        enum Knob { kOctaves = 0, kGate, kChance, kSwing, kOffset, kRamp, kRampTime, kHuman,
                    numKnobs };

    private:
        void applyShape();
        void stepShape(int delta);
        void stepRate(int delta);
        // Rate is one knob over two parameters and two units, exactly as the band's is: which
        // attachment exists depends on Sync or Hz, and the swap has to wait out an open drag.
        void refreshRateMode();

        KeysProcessor& processor;
        int line;

        juce::ToggleButton onButton, latchButton, keysButton;
        okstudio::RotaryKnob rateKnob;
        juce::TextButton ratePrev { "<" }, rateNext { ">" };
        juce::TextButton rateModeButton { "Sync" };
        juce::ComboBox shapeBox;
        juce::TextButton shapePrev { "<" }, shapeNext { ">" };
        std::array<juce::Slider, numKnobs> knobs;
        std::array<juce::Label, numKnobs> knobLabels;
        juce::Label latchLabel, keysLabel;
        juce::Label chordLabel;
        juce::TextButton chainButton { "Chain" };

        std::unique_ptr<ButtonAtt> onAtt, latchAtt, keysAtt, rateModeAtt;
        std::array<std::unique_ptr<SliderAtt>, numKnobs> knobAtts;
        // Exactly one of these is ever non-null; refreshRateMode owns that invariant.
        std::unique_ptr<SliderAtt> rateSyncAtt, rateHzAtt;
        int lastRateFree = -1;      // -1 = no attachment installed yet
        bool rateDragging = false;  // an open gesture; the swap defers until it closes
        bool dropTarget = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroRow)
    };

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
        // `owner` is asked which line is being edited on every read and write, rather than
        // being handed an index: the tabs change it under these grids, and a copy taken at
        // construction would leave them drawing whichever line was up when the panel opened.
        LaneGrid(KeysProcessor&, const ArpPanel& owner, ArpEngine::Lane, int loVal, int hiVal);

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
        const ArpPanel& owner;
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
        MuteRow(KeysProcessor&, const ArpPanel& owner); // `owner` names the line; see LaneGrid

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;

    private:
        int currentLength() const;
        int stepAtX(float x) const;
        void applyAtX(float x);

        KeysProcessor& processor;
        const ArpPanel& owner;
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
        SlotCard(KeysProcessor&, const ArpPanel& owner, int index);

        void paintButton(juce::Graphics&, bool over, bool down) override;
        void mouseDown(const juce::MouseEvent&) override;

        std::function<void()> onRightClick;
        // Lit while a chord card dragged out of the pad strip is over this slot. The drop
        // itself is the editor's to deliver: the drag never leaves the strip's mouse capture,
        // so this card is never told about it by JUCE.
        void setDropTarget(bool);

    private:
        KeysProcessor& processor;
        const ArpPanel& owner;
        int index;
        bool dropTarget = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlotCard)
    };

    // One of the three A/B/C tabs at the left of the slot row. It selects the line the whole
    // panel edits, and says what that line is doing: lit when the line is on, and carrying the
    // name of the chord it is holding, so three tabs read as three arpeggiators at a glance.
    // A chord card can also be dropped straight onto one.
    class LineTab : public juce::Button
    {
    public:
        // `line` < 0 is the macro tab: the fourth one, which selects the all-three view
        // rather than a line. One class for both because they are one row of targets and have
        // to look like one.
        LineTab(KeysProcessor&, const ArpPanel& owner, int line);

        void paintButton(juce::Graphics&, bool over, bool down) override;
        void setDropTarget(bool);

    private:
        KeysProcessor& processor;
        const ArpPanel& owner;
        int line;
        bool dropTarget = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LineTab)
    };

private:
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
    // Every APVTS attachment on the panel, bound to the current line's parameter ids. Called
    // once from the constructor and again whenever a tab changes the line: an attachment names
    // one parameter for its whole life, so switching lines means building new ones. The rate
    // dial has done exactly this for its two units since the Hz mode landed (refreshRateMode),
    // including the guard against swapping one out from under an open drag.
    void buildAttachments();
    // This line's id for a parameter, e.g. "arpRate" on A and "arp2Rate" on B.
    juce::String paramId(KeysProcessor::ArpParam) const;
    void buildLaneRow(LaneRow&, ArpEngine::Lane, const juce::String& name, int loVal, int hiVal);
    void selectLane(int lane);
    void nudgeLength(int delta); // selected lane, or every lane while Link is on
    void cycleClockDiv();
    void refreshLaneReadouts();
    void refreshPatternButtons();
    void recallOrCopy(int index);
    void launchSlot(int index);   // left-click on a slot card
    void showSlotMenu(int index); // right-click accelerator; everything in it is also a button
    void stepCombo(juce::ComboBox&, int delta); // the < > pair beside Shape
    // Rate spans two parameters and two units, so its < > pair cannot be stepCombo: in Sync it
    // walks the division list, in Hz it multiplies the frequency. See stepRate().
    void stepRate(int delta);
    // Which of the two rate parameters the dial is attached to, plus everything that has to
    // say which unit is live. Driven off arpRateFree, so a host automating it lands here too.
    void refreshRateMode();

    // The captioned, ruled group boxes the band is drawn as. Filled in by resized() and
    // painted by paint(), because a caption and a rule are two lines of Graphics each and
    // do not justify five more child components.
    struct Group
    {
        juce::String caption;
        juce::Rectangle<int> bounds;
        bool visible = true;
    };
    std::array<Group, 5> groups;

    bool patternMode() const; // Shape == "Pattern": the step editor is in play
    int contentHeight() const; // one answer for the macro, shape and pattern views
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
    // The line every control on this panel is bound to. Seeded from the processor, which is
    // where it lives (the panel is destroyed every time the section folds).
    int editedLine = 0;
    int lastPatternMode = -1; // -1 = not yet laid out; else the last bool seen
    int lastRateFree = -1;    // same trick for the rate mode: -1 = no attachment installed yet
    // Is the rate dial being dragged right now? A drag is an open parameter gesture, and the
    // attachment that opened it cannot be destroyed until it closes; refreshRateMode() defers
    // the swap while this is set, and rateKnob.onDragEnd calls it back on the mouse-up.
    bool rateDragging = false;

    KeysProcessor& processor;

    // No title, On or Close: the Arp section bar above the panel carries all three, and two
    // On toggles bound to the same parameter is just a thing to get wrong.

    // Shape carries the eight directions plus "Pattern", after Serum 2, whose step
    // editor only exists while SHAPE is "Pattern". It cannot be a plain APVTS
    // attachment because it spans two parameters (arpDirection + arpPattern).
    juce::ComboBox shapeBox, distanceBox, retrigBox;
    juce::Label rateLabel, shapeLabel, distanceLabel, retrigLabel;
    // Rate is a dial, and which parameter it turns depends on the mode: in Sync it detents
    // through the eleven divisions of arpRate, in Hz it sweeps arpRateHz. One attachment is
    // alive at a time (refreshRateMode swaps them), which is what makes the dial's range, its
    // detents, its skew and its readout all come from the parameter rather than from here.
    okstudio::RotaryKnob rateKnob;
    // The switch between the two, reading the mode that is live. A dial position means two
    // different things in the two modes, so this says which one you are looking at, and the
    // readout under the dial says it a second time in its units.
    juce::TextButton rateModeButton { "Sync" };
    // The < > pairs beside Shape and Rate. Not decoration: stepping to the next shape is
    // the commonest thing you do to an arp, and a button is one click where the combo is a
    // click, a travel and a second click. Beside the rate dial they are load-bearing rather
    // than a convenience - a dial is a *drag* target, and these are the click-only path to
    // every value it can hold, in both modes.
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
    // The three line tabs, at the left of that same row. They cost no height: the slot row is
    // 58 px and a tab is the mouse-only 34, centred in it.
    std::array<std::unique_ptr<LineTab>, KeysProcessor::numArpLines> lineTabs;
    // The fourth tab. It selects a *view*, not a line: the current line stays whatever it was,
    // so a chord card click still has somewhere unambiguous to go while all three are on screen.
    std::unique_ptr<LineTab> macroTab;
    bool macroView = false;
    std::array<std::unique_ptr<MacroRow>, KeysProcessor::numArpLines> macroRows;
    // Shared by all three lines, and the reason the macro view is more than three rows: one
    // tempo they all run at, and one quantize that lands their changes together.
    okstudio::RotaryKnob bpmKnob;
    juce::TextButton bpmPrev { "<" }, bpmNext { ">" };
    juce::Label bpmLabel, quantizeLabel;
    juce::ComboBox quantizeBox;
    std::unique_ptr<SliderAtt> bpmAtt;
    std::unique_ptr<ComboAtt> quantizeAtt;
    void nudgeBpm(int delta);
    void refreshMacro();
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

    std::unique_ptr<ButtonAtt> dotAtt, tripAtt, anchorAtt, latchAtt, linkAtt, rateModeAtt;
    std::unique_ptr<ComboAtt> distanceAtt;
    // Exactly one of these two is ever non-null; refreshRateMode() owns that invariant.
    std::unique_ptr<SliderAtt> rateSyncAtt, rateHzAtt;
    std::unique_ptr<SliderAtt> octavesAtt, swingAtt, gateAtt, chanceAtt;
    std::unique_ptr<SliderAtt> offsetAtt, rampAtt, rampTimeAtt, humanAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpPanel)
};
} // namespace keys
