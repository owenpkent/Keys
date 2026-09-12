#pragma once

#include "../ChordShift.h"
#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>

namespace keys
{
// The keybed's chord, as the content of a window of its own - the ChordGenPanel / TakePanel
// shape: built when the window opens, destroyed when it closes, and never the only copy of
// anything. Every value on screen is one of the seven parameters ChordShift.h names
// (`keyChord`, `keyChordShift1`..`keyChordShift6`), read straight off the processor's APVTS, so
// a session load, a host automating one of the seven, and this window can never disagree.
//
// Not a Section: it never docks, so it has no bar, fold or caption, and it reuses
// DetachedWindow exactly as the generator's and the library's windows do. Unlike those two its
// bounds are not kept in `KeysProcessor::LayoutState` - that struct, and the flag/frame pair the
// other two windows use, belong to the half of this feature that owns PluginProcessor, so this
// window's placement is session-only, the same shape `KeysEditor::takeWindowBounds` already is
// and for the same reason: nothing here is lost by forgetting where it was left.
//
// Three bands, top to bottom, and the chip that opens this window never toggles the feature
// itself - the On switch lives here, not on the chip, because a chip that both toggles and
// opens is two actions asked of one target:
//   * the **On** switch, bound to `keyChord`;
//   * the ten **one-click shapes** (`keys::chordshift::shapes()`), the "one click" the whole
//     feature is named for. Clicking one writes all six shift parameters at once and lights
//     the shape currently loaded, the same radio-row shape ChordGenPanel's Source row uses for
//     a choice with no JUCE attachment of its own;
//   * six **shift rows**, one per voice, for the chord that is not on the shape list. Each row
//     is a `-` / `+` stepper pair and a "+N st" / "0 st" / "-N st" readout - the click-only path
//     CLAUDE.md's own standing rule asks for beside any value, steppers rather than a slider so
//     there is nothing here that is a drag.
class ChordShiftPanel : public juce::Component,
                        private juce::Timer
{
public:
    explicit ChordShiftPanel(KeysProcessor&);
    ~ChordShiftPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // The on-screen Close button. The window's title-bar X runs the same teardown; the editor
    // wires both to one call, the ChordGenPanel/TakePanel contract.
    std::function<void()> onClose;

    // What the layout below actually needs, so the window's minimum is derived rather than
    // guessed - see resized() for the arithmetic each of these adds up.
    static juce::Point<int> contentSize();
    static juce::Point<int> minWindowSize(); // contentSize() plus the window's own furniture
    static juce::Point<int> defaultWindowSize(); // where it opens the first time

private:
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void buildControls();

    void applyShape(int shapeIndex);       // one click writes all six shift parameters
    void nudgeShift(int index, int delta); // a stepper's click-only path to a shift's value

    // Polled rather than pushed: the shifts move from a session load, a host, or a click in
    // this window itself, and none of those owes this class a callback.
    void refreshShapeLights();   // which shape button reads as "currently loaded"
    void refreshShiftReadouts(); // the six "+N st" labels

    KeysProcessor& processor;

    juce::Label title;
    juce::TextButton closeButton { "Close" };

    juce::ToggleButton onButton { "On" };
    std::unique_ptr<ButtonAtt> onAtt;

    juce::Label shapeLabel;
    // One per entry of keys::chordshift::shapes() (Off, 5th, Oct, Maj, Min, Sus2, Sus4, Maj7,
    // Min7, Dom7). The count is written down here because ChordShift.h fixes it as a
    // std::array<Shape, 10> rather than exposing it as a named constant; the constructor
    // jasserts the two sizes agree, the same guard the macro card's arp-line arrays use.
    static constexpr int numShapes = 10;
    std::array<juce::TextButton, numShapes> shapeButtons;
    int lastShapeShown = -2; // -1 is a real answer ("none of the ten"); this is "never polled yet"

    // Six shift rows, one per keys::chordshift::numShifts. Each is a caption, a `-` / `+` pair
    // and a readout - no Slider anywhere in this class, because IncDecButtons needs real width
    // to keep its arrows from stacking into a sub-floor pair (CLAUDE.md's own paid-for trap),
    // and a hand-built pair is the one shape that lets every target be sized without asking
    // JUCE's LookAndFeel to guess.
    struct ShiftRow
    {
        juce::Label caption;
        juce::TextButton down { "-" }, up { "+" };
        juce::Label readout;
    };
    std::array<ShiftRow, keys::chordshift::numShifts> shiftRows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordShiftPanel)
};
} // namespace keys
