#pragma once

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace keys
{
// Octavium's XY fader: one drag sends two CCs (X defaults to CC1 Mod, Y to CC74
// Cutoff; both assignable, persisted as xyCCX / xyCCY). Up is more: top of the pad
// is Y=127. The knob moves by relative drag only, never jumping to a click, so a
// stray click can't slam both CCs; Reset recentres to 64/64 (and says so in MIDI),
// and Lock X / Lock Y freeze an axis while you work the other. The knob holds its
// position on release, and a value is only sent when it actually changes.
class XYPad : public juce::Component
{
public:
    explicit XYPad(KeysProcessor&);

    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

    // Called from the editor's timer: keep the assignment labels in sync with the params.
    void refreshAssignments();

private:
    int assignedCC(bool yAxis) const;
    void sendChanged();
    juce::Rectangle<float> padArea() const;

    KeysProcessor& processor;
    juce::TextButton xButton, yButton;
    juce::TextButton lockXButton { "Lock X" }, lockYButton { "Lock Y" }, resetButton { "Reset" };
    float posX = 0.5f, posY = 0.5f;   // normalised knob position, y up (64/64 at launch)
    float refX = 0.5f, refY = 0.5f;   // position when the drag started
    juce::Point<float> refPos;        // where the drag started
    int lastSentX = -1, lastSentY = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XYPad)
};
} // namespace keys
