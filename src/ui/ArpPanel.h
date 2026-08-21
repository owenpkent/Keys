#pragma once

#include "../ArpEngine.h"
#include "../PluginProcessor.h"
#include "ChordDrag.h"
#include "RangeKnob.h"
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
                 public juce::DragAndDropTarget,
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

    // The narrowest this panel can be drawn without a control being starved, in the macro
    // view - which is the default view and the widest-hungry one, because it puts two macro
    // cards side by side. Static and public because the *windows* have to ask it: the editor
    // floors itself well above this, but the detached Arp window has a minimum of its own and
    // had been set by hand, so the ninth knob fitted the docked case and overflowed the
    // detached one with nothing on screen to say so (2026-08-21).
    static int minMacroWidth();

    // The same question for the panel as a whole - the macro view's requirement against the
    // deep pages', whichever is larger. This is the one a *window* wants; minMacroWidth() is
    // exposed beside it because it is the derived half and the tests check it on its own.
    static int minPanelWidth();

    // Which of the arpeggiator lines everything on this panel is editing: the band, the
    // step lanes, the twelve slots, Bars and Chain. One row of controls, the lines behind
    // it, chosen by the A/B/All tabs on the ARP section bar (editor-owned since 2026-08-02).
    // It is the processor's state rather than the panel's, because the Pads bar's letter chip
    // and the per-card Send to arp slot read it with this panel folded away.
    int editLine() const;

    // Which lane the Draw page is showing. Public because the loop bar and the grids are
    // handed the panel rather than an index, for the same reason `editLine` is: the tabs
    // change it under them, and a copy taken at construction would go stale.
    int selectedLaneIndex() const { return selectedLane; }
    // `leaveMacroView` false sets the line without changing what is on screen. A drop passes
    // false: it is routing a chord, not navigating, and in the macro view all three lines are
    // in front of you already, so there is nothing to switch to.
    void setEditLine(int line, bool leaveMacroView = true);
    // The macro view: all three lines at once, in place of the band and the step editor. It is
    // a *view*, not a fourth line - the current line stays whatever it was, so a chord card
    // click still has one unambiguous target while all three are on screen.
    bool isMacroView() const { return macroView; }
    void setMacroView(bool);

    // **The All view's bottom row of cards, collapsed to a strip** (2026-08-19, Owen: "maybe you
    // should be able to minimize bottom arps"). Four lines in a 2x2 grid is two card rows, and a
    // card is 323 px, so the view alone sets a 1349 px minimum window. Collapsed it is 34 px and
    // the minimum falls to 1060.
    //
    // The lines themselves are untouched - they keep their chords, their patterns and their
    // output, exactly as an off line keeps its card's controls live behind the scrim. The state
    // lives on the processor (LayoutState::arpMacroBottomFolded), because this panel is destroyed
    // every time the section folds.
    bool bottomRowFolded() const;
    void setBottomRowFolded(bool);

    // A line's deep view is three pages (2026-08-14, Owen: "can we simplify the detail view or
    // organize into pages"). Un-paged it was the band, the lane editor, the twelve slots and
    // the action row all at once - 612 px against the macro view's 240, so Details grew the
    // *window* by 372 px and All shrank it back again. Paging is what fixed the size: the three
    // pages come apart at Draw 298 / Setup 208 / Cards 124, so the tallest is eighteen over the
    // macro view rather than 372 over it.
    //
    // **Each page is now its own height** (2026-08-16, Owen: "fix arp"). They shared one for two
    // days, which stopped the window moving at all but made every page but the tallest carry the
    // difference as dead panel - 174 px of it on Cards. See ArpPanel::contentHeight for the
    // accounting, and for the one line that pins them back together if paging ever feels
    // unsettled.
    //
    // The split is by what you are doing, not by what fits: Steps is the lane editor, Slots is
    // the twelve cards and everything that acts on them, Setup is the band's two rows.
    enum class Page { steps = 0, slots = 1, setup = 2 };
    Page currentPage() const;
    void setPage(Page p);
    // Steps has nothing to show outside Pattern shape - the lane editor is what Pattern *is* -
    // so its tab greys there and the page falls back to Setup. The other two are always live.
    bool pageAvailable(Page p) const;
    // Told when a tab is clicked, so the editor can move the Pads bar's letter chip with it.
    std::function<void()> onEditLineChanged;
    // ...and when the page changes, so the bar's own page tabs can light the right one. The
    // editor owns those (they must survive this panel being destroyed by a fold), so the two
    // have to be told about each other in both directions.
    std::function<void()> onPageChanged;

    // Fired when this line's shape crosses into or out of Pattern, because that is what decides
    // whether the **Draw** page exists at all - and the tab that says so lives on the section bar,
    // which is the editor's, not the panel's (2026-08-18, Owen: "how do we get to the part where
    // we add harmony and stuff like that?", with Shape already reading Pattern and Draw greyed).
    //
    // refreshShape() runs on every 10 Hz tick and knew this the whole time; nothing carried it
    // across. The tab's enabled state was written only by refreshArpBarTabs(), which runs when the
    // line or the page changes - so setting Shape to Pattern left Draw greyed until you happened
    // to visit another page and come back, and the way to the lane editor looked broken.
    std::function<void()> onShapeChanged;

    // What a chord card dropped on this panel does, once JUCE has said where it landed. The
    // slot cards, line tabs and macro rows are each a `DragAndDropTarget` of their own and call
    // one of these; the panel owns the actions because both of them touch more than one card.
    //
    // There used to be a pair of screen-coordinate hit tests up here - `externalDropSlotAt` and
    // `externalDropLineAt`, the second of them a near-copy of ChordPads' own, all three walking
    // `Desktop::findComponentAt` by hand. They existed because the strip and this panel can be in
    // different top-level windows and the code believed JUCE could not deliver a drop across two
    // of them. It can: see ChordDrag.h. Walking *up* from the component under the point, which is
    // what made the whole macro row a target including the knobs sitting on it, is exactly what
    // JUCE's own `findTarget` does, so the behaviour survived the deletion.
    void takeChordOnSlot(int slot, const chorddrag::Payload&);
    // By pad slot rather than by payload: the only thing a line ever wanted out of a drag was
    // which pad it came from, and taking the slot lets a pad's own "Send to arp A" menu row
    // (2026-08-16) reach this same method instead of growing a second copy of it.
    //
    // **It routes and does not navigate**, for both callers alike (2026-08-18). A drop used to
    // re-point the panel at the line it landed on, which wrote nothing but made every per-line
    // readout - STEPS, Tuplet, Shape, the rate - jump to that line's own settings under the hand
    // that was routing a chord, and a view change reads as a data change when you are watching
    // numbers. See the definition for the reason the aim used to follow, and why it expired.
    void takeChordOnLine(int line, int padSlot);

    // **The panel itself takes a chord, anywhere on it** (2026-08-14, Owen: "need to be able to
    // drag chords to not just the main arp window"). Paging the deep view is what made this
    // necessary: the slot cards moved to the Cards page and the macro cards only exist in the
    // All view, so on Play or Draw the only target left was a 40 px letter on the bar.
    //
    // JUCE walks *up* from whatever is under the point, so this catches every pixel of the
    // panel that a more specific target does not - a slot card still wins on the Cards page,
    // and a macro card still wins in the All view, because they are deeper in the tree. That
    // is the same forgiveness RangeKnob's margin gives its satellite: aim if you like, or drop
    // on the panel and let it land on the line you are editing.
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
    void paintOverChildren(juce::Graphics&) override;

    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // One arpeggiator line, as one card: the settings that decide how it sits against the
    // other line, what it is holding, and a way to start it. These cards are the **macro
    // view**, which is what the fourth tab selects (2026-08-01, Owen: "a fourth option for a
    // simplified version that shows a little bit of all of them ... the goal is to be able to
    // create complex polyrhythms from one view"). Side by side since 2026-08-02 (Owen:
    // "parallel to each other instead of one on top of the other"), which is why a card is
    // three stacked lines: half the panel's width cannot hold the single row this used to be.
    //
    // Each row's attachments are bound to its own line for the row's whole life, unlike the
    // band above, which rebinds every time the tabs move. That is the point of the row: three
    // lines on screen at once cannot each be "the current line".
    class MacroRow : public juce::Component,
                     public juce::DragAndDropTarget
    {
    public:
        MacroRow(ArpPanel&, KeysProcessor&, int line);

        void paint(juce::Graphics&) override;
        // Scrims the card body (not the LINE A / LINE B caption strip) when this line is off.
        // Drawn over the children rather than gating setEnabled(false) on them: every control
        // has to stay live and clickable even while the line is off, both to dial in a rate
        // before switching it on and because a chord dropped onto an off line is load-bearing
        // (CLAUDE.md: "A line that is off still takes chords in") - a disabled component takes
        // no mouse events, which would kill the drop target along with everything else.
        void paintOverChildren(juce::Graphics&) override;
        void resized() override;
        // Readouts that no attachment drives: the rate text (it spans two parameters and two
        // units), the shape, and the chord this line is holding. Called by the panel's timer.
        void refresh();

        // A chord card dragged out of the pad strip. The row is the line here, laid out large,
        // so it is a far easier target than the tab that names it - and because JUCE walks up
        // from whatever is under the point, the knobs sitting on the row are part of it.
        bool isInterestedInDragSource(const SourceDetails&) override;
        void itemDragEnter(const SourceDetails&) override;
        void itemDragExit(const SourceDetails&) override;
        void itemDropped(const SourceDetails&) override;

        // The knobs a row carries, left to right. One table so the labels, the
        // parameters and the layout cannot drift apart; the headings are drawn once, on the
        // top row, and every row reserves the same strip so the columns line up.
        // OCT is the *transpose* (centred at zero, down as readily as up) rather than the
        // upward-only stacking range, which stays on the per-line tab beside Distance, the
        // rest of that same feature. Seven since the merge below: VEL is the bipolar velocity
        // trim that replaced VOL (centred, up boosts, down cuts) and now carries Humanize
        // Velocity as its own outer ring (2026-08-17, Owen: "I only want one velocity knob,
        // and I want this humanize section to be the outer ring") - H.VEL is gone as a
        // separate knob, so there is one loudness control per line rather than two two cells
        // apart. H.TIME stays its own RangeKnob for the timing nudge; Ramp and Time still
        // live on the per-line tab.
        // Eight again as of 2026-08-18: CHANCE became MUTATE and LOCK joined it. The strip was
        // eight until H.VEL folded into VEL's ring on 2026-08-17, so this is a width the row
        // has already carried - and the reserve-before-Shape rule below is what made that safe.
        // **Nine from 2026-08-21**, STRAY between MUTATE and LOCK: the three read left to
        // right as one sentence - how hard the run explores, how far outside the chord it may
        // go, how long it keeps what it finds - which is why STRAY is inserted rather than
        // appended to the end of the row. This enum is UI indexing and nothing stores it, so
        // inserting here is free; the *parameter* was appended, which is the order that is
        // not free (see KeysProcessor::apStray).
        enum Knob { kOctShift = 0, kGate, kMutate, kStray, kLock, kSwing, kOffset, kVel, kHTime,
                    numKnobs };

    private:
        void applyShape();
        void stepShape(int delta);
        void stepRate(int delta);
        // The rate readout under the dial, which says what the engine is actually playing
        // rather than the bare division. The combo drives itself through its attachment; this
        // exists because the readout depends on three parameters and is bound to one. Cached,
        // since it runs off the 10 Hz timer.
        void refreshTuplet();
        void refreshDice();
        // Point the Shape combo at what this line's parameters actually say. One reader,
        // called from the constructor as well as from refresh(), because everything that
        // keys off the combo's selection - the dice's greying above all - reads -1 until
        // something selects an item, and addItemList selects nothing.
        void syncShapeBox();
        // The dial's readout, reinstalled after every attachment swap - see ArpPanel's.
        void installRateText();
        // Rate is one knob over two parameters and two units, exactly as the band's is: which
        // attachment exists depends on Sync or Hz, and the swap has to wait out an open drag.
        void refreshRateMode();

        KeysProcessor& processor;
        int line;

        // LTCH, PLAY and Chain were on the row until 2026-08-02, when Owen had the rows
        // slimmed to what you reach for while two lines are running. All three still live
        // with the line - Latch and PLAY on its tab's band, Chain on the action row under its
        // slots. The line switch itself (onButton) left on 2026-08-02 too, the day the A/B
        // chips on the ARP section bar became the per-line On toggles: two on-switches for the
        // same parameter, one of them buried in a card, was a control to get wrong twice.
        okstudio::RotaryKnob rateKnob;
        juce::TextButton ratePrev { "<" }, rateNext { ">" };
        juce::TextButton rateModeButton { "Sync" };
        // The rate's three modifiers, the same three the band carries and greyed by the same
        // question (2026-08-02, Owen: "I need to have options for dots and triplets as well").
        // They sit on the card's bottom line with the held chord, at the full 34 px hit
        // height: putting them beside the rate would drive the knobs under the mouse-only
        // minimum, and height is the cheap axis inside a card.
        juce::ToggleButton dotButton { "Dot" }, anchorButton { "Anchor" };
        // Tuplet is a combo, not a tick: it picks one of five, and a check box that cycled its
        // own text was a control lying about its own shape (2026-08-03, Owen: "it's a check box
        // but it changes"). A combo is what Keys already means by "pick from a list" - Shape,
        // Distance and Retrigger are all one - so it needs no explaining, and it takes an
        // ordinary ComboBoxAttachment where a button could not bind a choice at all.
        juce::ComboBox tupletBox;
        // Opens this line's detailed view (the band and, on Pattern, the step editor). Added
        // beside Anchor once the A/B chips stopped navigating anything: with the tabs gone,
        // this button is the only way back from the macro cards to the deep view.
        juce::TextButton detailsButton { "Details" };
        juce::ComboBox shapeBox;
        juce::TextButton shapePrev { "<" }, shapeNext { ">" };

        // **The dice** (2026-08-21, Owen: "I use the random ones a lot, and I'd like to have a
        // dice button when those are active nearby to regenerate their pattern"). Drawn rather
        // than lettered or iconned: the same self-drawn-chrome rule SectionBar's fold chevron
        // and the settings gear follow, and there is no word for this that fits in 34 px.
        //
        // It greys outside Random Once instead of vanishing. Random and Random Other draw fresh
        // every step and have no stored order for a dice to deal again, so a button that stayed
        // lit on them would promise something it cannot do - and the house rule is that a
        // control which would reflow its neighbours greys rather than disappears. Its cell is
        // reserved on every shape for exactly that reason.
        struct DiceButton : juce::Button
        {
            DiceButton() : juce::Button("dice") {}
            void paintButton(juce::Graphics&, bool highlighted, bool down) override;
        };
        DiceButton diceButton;
        // Five of the seven are plain rotaries. H.TIME and VEL are RangeKnobs, because each
        // of them is a random draw and a draw has two ends (2026-08-03) - `ranges` holds one
        // for those two indices and nullptr for the rest, and `knobFace()` is what everything
        // else walks so the layout, the headings and the attachments stay one loop. VEL's ring
        // is Humanize Velocity (2026-08-17), not a span of its own value the way H.TIME's is -
        // see the wiring in the constructor for why the two range knobs are not symmetric.
        std::array<juce::Slider, numKnobs> knobs;
        std::array<std::unique_ptr<RangeKnob>, numKnobs> ranges;
        juce::Component& knobCell(int k);
        juce::Slider& knobFace(int k);
        static bool isRangeKnob(int k) { return k == kHTime || k == kVel; }
        std::array<juce::Label, numKnobs> knobLabels;
        // The two fixed harmony voices (2026-08-19, Owen holding up BigSky's shimmer list:
        // "2 harmony drop down like the photo. and each of those has a chance knob"). An
        // interval combo and a chance knob each, on their own strip between the knobs and the
        // rate's modifiers - per card because Owen asked for them "in the arps", and a combo
        // because "pick from a list" is a combo in Keys. Labels are HARMONY 1 / CHANCE /
        // HARMONY 2 / CHANCE, in that order.
        //
        // **The dropdown opens as two columns**, descending intervals on the left and
        // ascending on the right, which is how the BigSky panel lays the same list out and
        // what "make harmony 2 columns" turned out to mean (2026-08-19; a first reading put
        // the *card's* controls in two columns, and Owen, shown the popup: "still one
        // column"). A ComboBox builds its own single-column menu internally, so the subclass
        // rebuilds it with a column break - same items, same ids, nothing else changed.
        struct HarmonyBox : juce::ComboBox
        {
            void showPopup() override;
        };
        std::array<HarmonyBox, 2> harmBoxes;
        std::array<juce::Slider, 2> harmChanceKnobs;
        std::array<juce::Label, 4> harmLabels;
        std::array<std::unique_ptr<ComboAtt>, 2> harmAtts;
        std::array<std::unique_ptr<SliderAtt>, 2> harmChanceAtts;
        // RATE and SHAPE, over the top line's two stepper groups (2026-08-02, Owen: "the
        // arrows to adjust certain parameters are not clear as to what they're adjusting"):
        // two flanked `< >` pairs side by side read as one puzzle without names above them.
        juce::Label rateHeadLabel, shapeHeadLabel;
        juce::Label chordLabel;

        std::unique_ptr<ButtonAtt> rateModeAtt;
        std::unique_ptr<ButtonAtt> dotAtt, anchorAtt;
        std::unique_ptr<ComboAtt> tupletAtt;
        std::array<std::unique_ptr<SliderAtt>, numKnobs> knobAtts;
        void setDropTarget(bool);
        // What VEL's ring puts back when its lamp switches Humanize Velocity back on - the
        // same shape ChordPads' lastStrumMax uses for Strum's own zero-is-off lamp. UI-only
        // and deliberately not persisted: a session saved with it off should open off.
        double lastHumanVelAmount = 20.0;

        ArpPanel& owner;
        // Exactly one of these is ever non-null; refreshRateMode owns that invariant.
        std::unique_ptr<SliderAtt> rateSyncAtt, rateHzAtt;
        int lastRateFree = -1;      // -1 = no attachment installed yet
        bool rateDragging = false;  // an open gesture; the swap defers until it closes
        bool dropTarget = false;
        // The scrim's own cache, compared in refresh() (driven by the panel's 10 Hz timer while
        // the macro view is up) so repaint() is only called on an actual change rather than
        // every tick.
        bool lastLineOn = true;
        // The same trick for the rate readout, which no attachment drives: -1 = nothing drawn yet.
        int lastTuplet = -1, lastDotted = -1;

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
        // Non-const owner, unlike MuteRow's: in Select mode this writes the span back into
        // the panel, which owns it (Roll and Reset are the panel's, not the grid's).
        LaneGrid(KeysProcessor&, ArpPanel& owner, ArpEngine::Lane, int loVal, int hiVal);

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;

    private:
        int currentLength() const;
        int stepAtX(float x) const;
        int valueAtY(float y) const;
        // step < 0 = take it from x (the press); otherwise edit that step alone (the drag).
        void paintStepFromMouse(const juce::MouseEvent&, int step);
        juce::String cellText(int value) const;
        // The pitch a Note lane index currently names, or empty for every other lane and for
        // the values that ask the chord a question rather than counting into it.
        juce::String noteNameFor(int value) const;

        KeysProcessor& processor;
        ArpPanel& owner;
        ArpEngine::Lane lane;
        int loVal, hiVal;
        bool dragging = false;
        int selAnchor = 0; // where a Select drag started
        int paintStep = 0; // the step a draw gesture is locked to
        juce::Point<float> cursorPos;
        int cursorValue = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LaneGrid)
    };

    // Kirnu's loop control (its manual p11), one per lane, drawn under the grid on the same
    // cell grid so a window means the steps it is sitting over. Click or drag: "Loop points
    // follow mouse click... the pointer closest to the mouse is moved" is Kirnu's own rule and
    // it is already a click-only path, so this needs no steppers beside it.
    class LoopBar : public juce::Component,
                    public juce::SettableTooltipClient
    {
    public:
        LoopBar(KeysProcessor&, const ArpPanel& owner);

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;

    private:
        int stepAtX(float x) const;
        void moveNearestHandle(float x);

        KeysProcessor& processor;
        const ArpPanel& owner;
        int grabbed = -1; // 0 = the left handle, 1 = the right, -1 = not dragging

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopBar)
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
    class SlotCard : public juce::Button,
                     public juce::DragAndDropTarget
    {
    public:
        SlotCard(ArpPanel&, KeysProcessor&, int index);

        void paintButton(juce::Graphics&, bool over, bool down) override;
        void mouseDown(const juce::MouseEvent&) override;

        std::function<void()> onRightClick;

        // A chord card dropped here binds that chord to this slot: the left-click twin *Send to
        // arp slot* never had, and the reason that menu item was allowed to be right-click-only
        // is that naming one slot of twelve needs a target picker. JUCE tells this card about the
        // drop directly, mouse capture and window boundaries notwithstanding - the comment that
        // used to sit here saying otherwise was wrong (ChordDrag.h).
        bool isInterestedInDragSource(const SourceDetails&) override;
        void itemDragEnter(const SourceDetails&) override;
        void itemDragExit(const SourceDetails&) override;
        void itemDropped(const SourceDetails&) override;

    private:
        void setDropTarget(bool);

        ArpPanel& owner;
        KeysProcessor& processor;
        int index;
        bool dropTarget = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlotCard)
    };

    // The A/B/All tabs left this class on 2026-08-02 (Owen: "move the bpm and the a b and
    // all into the header also"): they ride the ARP section bar now, owned by the editor
    // (KeysEditor::ArpBarTab), because the bar outlives the panel and the tabs are how you
    // come back to it. The panel keeps setEditLine/setMacroView as the entry points the bar
    // calls.

private:
    // One lane: the tab that selects it and the grid it shows. Length and clock
    // division used to live here, per lane, which meant six copies of both on screen at
    // once with no room left to label any of them. With one lane visible there is one
    // of each, shared, below the grid.
    // A lane tab that reports on the lane behind it. Twelve identical buttons over eleven
    // invisible lanes is the Draw page's oldest readability hole: whichever lane you are not
    // looking at could be flat, could be the reason the part sounds wrong, and says nothing
    // either way. Kirnu marks its own control tabs for exactly this (its manual p12: a corner
    // mark for whether the control is on, and a second one meaning "this control has input
    // values"), and both marks are copied here.
    class LaneTab : public juce::TextButton
    {
    public:
        void paintButton(juce::Graphics&, bool over, bool down) override;
        bool laneOn = true;    // struck through when off
        bool laneHasData = false; // a dot when the lane holds anything but its default
    };

    struct LaneRow
    {
        LaneTab tab;
        std::unique_ptr<LaneGrid> grid;
        // Not every lane gets a tab: Mute is drawn by the MUTE row under the grid, so it has a
        // lane but nothing to click. Without this the loops below laid out and counted its
        // default-constructed, empty button anyway, which ate a cell of the tab row and pushed
        // the last real tab (Chain) off the end - visible as a missing tab and nothing else.
        bool hasTab = false;
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
    // No lo/hi here (2026-08-18): the grid reads ArpEngine::laneRange, which is documented as
    // the one copy. This took a pair of arguments per lane and they were a second copy of that
    // table - and the day the Note lane grew its per-step shapes, the engine accepted 13..20
    // while every grid still stopped at 12, so the values existed and could not be drawn or set.
    void buildLaneRow(LaneRow&, ArpEngine::Lane, const juce::String& name);
    void selectLane(int lane);
    void nudgeLength(int delta); // selected lane, or every lane while Link is on
    void cycleClockDiv();
    void refreshLaneReadouts();
    // Link on: push the Note lane's length and speed onto every other lane. See the definition
    // for why nudgeLength doing it too is not enough.
    void enforceLinkedLengths();
    void refreshPatternButtons();
    void recallOrCopy(int index);
    void launchSlot(int index);   // left-click on a slot card
    void showSlotMenu(int index); // right-click accelerator; everything in it is also a button
    void stepCombo(juce::ComboBox&, int delta); // the < > pair beside Shape
    // Rate spans two parameters and two units, so its < > pair cannot be stepCombo: in Sync it
    // walks the division list, in Hz it multiplies the frequency. See stepRate().
    void stepRate(int delta);
    // The rate readout under the dial, which has to say what the engine is actually playing
    // ("1/10") rather than the division on its own. Not the combo, which drives itself: this
    // exists because the readout is a function of three parameters and bound to one. Cached
    // against the last call, since the 10 Hz timer drives it.
    void refreshTuplet();
    // Installs the readout above onto the dial. Called after every attachment swap, because
    // SliderParameterAttachment writes textFromValueFunction in its own constructor and would
    // otherwise put the bare division back.
    void installRateText();
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
    int contentHeight() const; // the fixed height the panel reserves, whatever is showing
    int pageHeight() const;    // what the current view or page actually needs; see cardBounds()

    // Which controls belong to which page. Three explicit lists rather than a flag per
    // component or three parent Components to reparent into: the lists are built once, in one
    // readable block at the end of buildControls(), and adding a control means naming it in
    // exactly one place. Nothing here ever turns a control *on* - refreshShape() is still the
    // only thing that does, on its own Shape and lane gates - this only hides what is off the
    // current page, and so must run last. See applyPageVisibility().
    std::vector<juce::Component*> pageSteps, pageSlots, pageSetup;
    void buildPageLists();
    void applyPageVisibility();
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

    // Is a chord card hovering over the panel right now? Drawn as an outline round the
    // whole card in paintOverChildren, so "drop anywhere" is visible.
    bool panelDropTarget = false;

    bool inlineMode = false;
    // The line every control on this panel is bound to. Seeded from the processor, which is
    // where it lives (the panel is destroyed every time the section folds).
    int editedLine = 0;
    int lastPatternMode = -1; // -1 = not yet laid out; else the last bool seen
    int lastRateFree = -1;    // same trick for the rate mode: -1 = no attachment installed yet
    int lastTuplet = -1, lastDotted = -1; // ... and for the chip and the readout it decorates
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
    juce::ToggleButton dotButton { "Dot" }, anchorButton { "Anchor" };
    // Tuplet is a combo box, not a toggle: it picks one of five, and it writes a choice
    // parameter. See MacroRow's twin, and ArpEngine::rateSyncText for what the dial then says.
    juce::ComboBox tupletBox;
    juce::Label tupletLabel;
    juce::Slider octavesSlider, swingSlider, gateSlider, chanceSlider;
    juce::Label octavesLabel, swingLabel, gateLabel, chanceLabel;
    juce::ToggleButton latchButton { "Latch" };
    // PLAY's home on the band since the macro rows slimmed down (2026-08-02): whether this
    // line arpeggiates what you play on the keybed. Same parameter the macro rows carried
    // (arpKeys); the word is Play because "Keys" collided with the bar's Light keys.
    juce::ToggleButton keysBandButton { "Play" };
    // The second band row (2026-07-30). SPREAD is Repeats + Distance + Offset - how far the
    // chord is stacked and where the run starts; FEEL is the three that decide whether it
    // sounds played. Horizontal sliders rather than the band's rotaries: a knob column spans
    // both rows of a group and this row is one row tall, which is what keeps the panel from
    // growing by a whole band.
    // humanSlider is the timing half and humanVelSlider the velocity half of what was one
    // Humanize control until 2026-08-02. The macro card folded its own velocity half into
    // VEL's outer ring on 2026-08-17; this band keeps both as separate sliders regardless -
    // it was not part of that merge.
    // driftSlider joined FEEL on 2026-08-14. It belongs beside Humanize rather than on the
    // Draw page where it was asked for: Humanize is a *player* wandering (late and quieter,
    // never early and never louder) and Drift is a *machine* wandering (either way, on the
    // lanes that decide how a step plays), so the two are the same question asked twice. It
    // also works on a plain shape, where there is no Draw page at all.
    juce::Slider offsetSlider, rampSlider, rampTimeSlider, humanSlider, humanVelSlider, driftSlider;
    juce::Label offsetLabel, rampLabel, rampTimeLabel, humanLabel, humanVelLabel, driftLabel;

    std::array<LaneRow, ArpEngine::numLanes> laneRows;
    int selectedLane = (int) ArpEngine::laneNote;
    std::unique_ptr<MuteRow> muteRow;
    juce::Label muteRowLabel;
    std::unique_ptr<LoopBar> loopBar;

    // The selected lane's own shape, on the page the lane is drawn on (2026-08-18). Steps,
    // Speed and Link used to sit in the STEPS band group on the *Play* page, so changing how
    // long the lane you are drawing runs meant leaving the page you were drawing it on. They
    // are per-lane controls; they belong beside the lane.
    juce::TextButton laneOnButton { "On" };
    juce::TextButton dirPrev { "<" }, dirNext { ">" };
    juce::Label dirLabel, dirReadout;

    // Kirnu's remaining palette tools (its manual p8: Draw / Select / Random / Copy / Paste /
    // Clear). Keys had the first three; Select is what these three were waiting for, since
    // each of them needs a span to aim at. The clipboard is one lane's worth of steps and
    // lives here rather than in the processor: it is a UI convenience, not session state, and
    // Kirnu's own rule is that "only same control steps can be copied/pasted".
    // Copy and Paste only. Kirnu's palette has a Clear beside them ("set values to default"),
    // and Keys' Reset already *is* that once it narrows to the Select span - a second button
    // doing what the one next to it does is the trap this file warns about twice elsewhere.
    juce::TextButton copyStepsButton { "Copy" }, pasteStepsButton { "Paste" };
    std::vector<int> stepClipboard;
    int stepClipboardLane = -1;
    void copySteps();   // the Select span, or the whole lane when nothing is selected
    void pasteSteps();  // tiles the clipboard across the span, so 2 steps fill 8
    void refreshStepTools();
    void nudgeLaneDir(int delta);
    void toggleLaneOn();
    void refreshLaneStrip();

    // The shared length / clock-division controls for whichever lane is showing.
    juce::Label stepsLabel, speedLabel, stepsReadout;
    juce::TextButton stepsMinus { "-" }, stepsPlus { "+" }, speedButton;
    juce::ToggleButton linkButton { "Link" }; // "Link lanes" no longer fits the STEPS group

    // Twelve slot cards, alive in both shapes: launching a chord is as useful on a plain
    // "Up" as it is on an edited pattern, so unlike the lane editor these never hide.
    std::array<std::unique_ptr<SlotCard>, KeysProcessor::numArpPatterns> slotCards;
    bool macroView = false;
    // The 34 px strip that stands in for the bottom row of macro cards while it is collapsed.
    // It names the lines that are hidden, in their own accent colours and dimmed when the line
    // is off, and the whole strip expands the row again - a big target, which is the point on a
    // surface driven with one mouse.
    //
    // **It carries no On switches.** The arp bar's letter switches are those, they stay reachable
    // with the whole section folded, and a second control bound to one parameter is the mistake
    // that deleted MacroRow's own On toggle on 2026-08-02.
    class FoldedRowStrip : public juce::Component,
                           public juce::SettableTooltipClient
    {
    public:
        FoldedRowStrip(ArpPanel& o, KeysProcessor& p) : owner(o), processor(p)
        {
            setTitle("Expand arp lines C and D");
            setTooltip("Show the bottom row of arpeggiator cards again. Collapsed or not, "
                       "these lines keep playing.");
        }
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override { owner.setBottomRowFolded(false); }
        bool hitTest(int, int) override { return true; }

    private:
        ArpPanel& owner;
        KeysProcessor& processor;
    };
    std::unique_ptr<FoldedRowStrip> foldedRowStrip;

    std::array<std::unique_ptr<MacroRow>, KeysProcessor::numArpLines> macroRows;
    // The tabs, BPM and Launch Quantize all moved to the ARP section bar on 2026-08-02
    // (editor-owned; see KeysEditor), so the macro view is nothing but the two cards.
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

    // Euclid: a non-destructive preview strip (2026-08-14 generative round, see
    // docs/ARP_DESIGN.md). Hits/steps/rotate are panel-level browse state, never persisted -
    // opening the strip applies nothing; only a stepper click writes, straight into the
    // probability lane via KeysProcessor::applyEuclidToActiveArpPattern. Visible only under
    // the same condition as Randomize (slots view, Pattern shape); see refreshShape().
    juce::TextButton euclidButton { "Euclid" };
    bool euclidStripOpen = false;
    int euclidHits = 3, euclidSteps = 8, euclidRotate = 0;
    juce::Label euclidHitsLabel, euclidStepsLabel, euclidRotateLabel;
    juce::Label euclidHitsReadout, euclidStepsReadout, euclidRotateReadout;
    juce::TextButton euclidHitsMinus { "-" }, euclidHitsPlus { "+" };
    juce::TextButton euclidStepsMinus { "-" }, euclidStepsPlus { "+" };
    juce::TextButton euclidRotateMinus { "-" }, euclidRotatePlus { "+" };
    void openEuclidStrip(bool open); // toggles the strip; closes Clocks, since only one strip is ever open
    void nudgeEuclid(int which, int delta); // 0 = hits, 1 = steps, 2 = rotate
    void refreshEuclidReadouts();

    // Clocks: the four rhythm dividers (ArpEngine::rhythmDiv, 0 = off), same strip mechanism
    // as Euclid and mutually exclusive with it. Visible whenever the slot row is (every
    // shape, not just Pattern - the dividers act regardless of what Shape draws).
    juce::TextButton clocksButton { "Clocks" };
    bool clocksStripOpen = false;
    std::array<juce::Label, 4> clockDivLabels, clockDivReadouts;
    std::array<juce::TextButton, 4> clockDivMinus, clockDivPlus;
    void openClocksStrip(bool open);
    void nudgeClockDiv(int index, int delta);
    void refreshClockDivReadouts();

    // Voice: harmony mode (ArpEngine::harmonyMode - chord tones or the subharmonic voice),
    // the panel's first lane-contextual control. Visible only with the Harmony lane selected
    // and the STEPS group itself showing (Pattern shape); see refreshShape() and selectLane().
    juce::TextButton voiceButton;
    void refreshVoiceButton();

    // Reroll, on the Draw page (2026-08-14, Owen: "there should be, like, a more random feature
    // in the drawing, like cthulu"). Acts on the lane you are *looking at*, which is the half
    // that makes it different from Randomize on the Cards page - that one writes six lanes to a
    // musical recipe, this one strays from the one lane in front of you by `rollAmount`.
    //
    // It is also the fix for a regression: Randomize used to sit in the action row directly
    // under the lane grid, and paging the deep view put it on another page, so the lane and the
    // button that rerolls it were never on screen together.
    //
    // `rollAmount` is panel state, not a parameter - the same call Euclid's three steppers make.
    // Nothing it does is heard until you click Roll, so there is nothing for a host to automate.
    juce::TextButton rollButton { "Roll" };
    // Roll is destructive and Keys has no undo anywhere, so it needs a way back. Reset writes
    // the lane's own default (ArpEngine::laneDefaults) across its whole length, which is the
    // state a lane that has never been touched is in - so "Reset then Roll" is repeatable, and
    // a roll you did not like costs one click rather than a redraw.
    juce::TextButton resetButton { "Reset" };
    // Select: the missing primitive (2026-08-14, from Kirnu Cream's tool palette - its manual
    // p8 has Draw / Select / Random / Copy / Paste / Clear, and its Random tool acts on
    // "selected steps"). Roll and Reset acted on a whole lane because there was nothing smaller
    // to act on. With this lit, a drag on the grid marks a span instead of painting it, and
    // both buttons narrow to that span.
    //
    // A **mode**, not a modifier: the mouse-only contract has no Alt-drag to offer, and Kirnu
    // itself models this as a tool you pick rather than a chord you hold. Lit is the whole
    // affordance, the same way Copy and Clear already arm.
    juce::TextButton selectButton { "Select" };
    bool selectMode = false;
    int selFrom = -1, selTo = -1; // inclusive step span; selFrom < 0 means the whole lane
    void clearSelection();
    juce::TextButton rollMinus { "-" }, rollPlus { "+" };
    juce::Label rollReadout;
    int rollAmount = 35; // percent of the lane's range; 100 is a uniform scramble
    void nudgeRollAmount(int delta);
    void rollSelectedLane();
    void resetSelectedLane();

    // Copy and Clear both need a slot to act on, and neither may be right-click-only (the
    // mouse-only contract wants a left-click path for everything). Both arm: click the
    // button, then click the slot. One state, not two flags, so arming one disarms the
    // other rather than leaving two half-armed modes fighting over the next click.
    enum Armed { armNone = 0, armCopy, armClear };
    Armed armed = armNone;
    void setArmed(Armed, int fromIndex = -1);
    int copyFromIndex = -1;

    std::unique_ptr<ButtonAtt> dotAtt, anchorAtt, latchAtt, keysBandAtt, linkAtt, rateModeAtt;
    std::unique_ptr<ComboAtt> distanceAtt, tupletAtt;
    // Exactly one of these two is ever non-null; refreshRateMode() owns that invariant.
    std::unique_ptr<SliderAtt> rateSyncAtt, rateHzAtt;
    std::unique_ptr<SliderAtt> octavesAtt, swingAtt, gateAtt, chanceAtt;
    std::unique_ptr<SliderAtt> offsetAtt, rampAtt, rampTimeAtt, humanAtt, humanVelAtt, driftAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpPanel)
};
} // namespace keys
