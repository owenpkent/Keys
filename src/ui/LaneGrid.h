#pragma once

// The Draw page's three step views: the lane grid you draw on, the loop bar under it and the
// MUTE strip under that.
//
// They lived inline in ArpPanel.cpp until 2026-09-02, when that file was 4,687 lines and every
// arp component in it. These three come out together because they are one drawing: each reads
// the panel for which line and which lane are showing, each writes straight into
// processor.arpLine(...).lanes, and none of them owns anything the others do not.
//
// **The step geometry is the rule they share, and it is one rule.** All three divide their own
// width by the *lane's own length* - `cellW = getWidth() / length`, cell i at `cellW * i` - so
// cells widen as the pattern shortens, and a column in one view sits exactly over the column
// it belongs to in the other two. That only holds because the panel lays all three out off the
// same rectangle (see ArpPanel::resized): give one of them a different x or width and the
// windows, the mutes and the bars slide off the steps they name, with nothing on screen to say
// which of the three is lying. The MUTE strip carries one extra clause of the same rule - it
// is the *Note* lane's companion, so it measures against the Note lane's length whatever lane
// is selected, because the engine wraps every lane read by that lane's own length and a mute
// drawn at step 20 of a 32-step pattern would otherwise be read back modulo 8.
//
// They are still `ArpPanel::LaneGrid` and friends: ArpPanel re-exports all three as member
// aliases, so nothing that names one had to change.

#include "../ArpEngine.h"
#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace keys
{
class ArpPanel;

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

} // namespace keys
