#pragma once

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>

namespace keys
{
// Octavium's fader window: eight vertical CC faders. The assignments are parameters
// (faderCC1..8, defaults Mod/Volume/Cutoff/Pan/Resonance/Attack/Expression/Reverb)
// and persist with the session; the fader positions are transient performance state
// like the wheels, and nothing is sent until a fader is actually moved.
//
// Octavium hid reassignment in a menu-bar dialog; here the label under each fader is
// a button that opens the CC picker, so it stays one click and mouse-only.
class FaderBank : public juce::Component
{
public:
    explicit FaderBank(KeysProcessor&);

    void resized() override;
    void paint(juce::Graphics&) override;

    // Called from the editor's timer: keep the assignment labels in sync with the params.
    void refreshAssignments();

private:
    static constexpr int numFaders = 8;

    int assignedCC(int fader) const;

    KeysProcessor& processor;
    std::array<std::unique_ptr<juce::Slider>, numFaders> faders;
    std::array<std::unique_ptr<juce::TextButton>, numFaders> ccButtons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaderBank)
};
} // namespace keys
