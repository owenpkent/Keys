#pragma once

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <okstudio/RotaryKnob.h>
#include <array>
#include <memory>

namespace keys
{
// Eight assignable CC knobs in a row above the playing surface: what used to be
// Octavium's separate fader window and XY pad, collapsed into one strip once Keys
// went from five tabbed surfaces to a single view. The assignments are parameters
// (faderCC1..8, defaults Mod/Volume/Cutoff/Pan/Resonance/Attack/Expression/Reverb)
// and persist with the session; knob positions are transient performance state like
// the wheels, and nothing is sent until a knob is actually moved.
//
// Octavium hid reassignment in a menu-bar dialog; here the label under each knob is
// a button that opens the CC picker, so it stays one click and mouse-only.
//
// A knob move also calls processor.faderMoved(), a no-op on plain Keys; Keys Host
// overrides it to drive an auto-assigned hosted-instrument parameter directly. When
// such a binding exists, processor.faderTargetName() supplies the button's label
// instead of the CC name, refreshed alongside the CC label in refreshAssignments().
class KnobBank : public juce::Component
{
public:
    explicit KnobBank(KeysProcessor&);

    void resized() override;
    void paint(juce::Graphics&) override;

    // Called from the editor's timer: keep the assignment labels in sync with the params.
    void refreshAssignments();

private:
    static constexpr int numKnobs = 8;

    int assignedCC(int knob) const;

    KeysProcessor& processor;
    std::array<std::unique_ptr<okstudio::RotaryKnob>, numKnobs> knobs;
    std::array<std::unique_ptr<juce::TextButton>, numKnobs> ccButtons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KnobBank)
};
} // namespace keys
