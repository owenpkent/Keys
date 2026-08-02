#pragma once

#include "../PluginProcessor.h"
#include "../ScaleModes.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace keys
{
// A read-only picture of what the currently selected chord-generation source is doing (Owen,
// 2026-08-01: "a visualization for the generation source so people understand what it's
// doing"). It draws the shape a source walks - the fifths wheel, the PLR triangle, the mirror
// clock - and, when the audition tray holds chords, highlights the actual walk that produced
// them on top of it.
//
// This is a diagram, not a control surface: it generates nothing, plays nothing, and writes no
// parameter. Everything it needs is pushed in from outside (source, key, the tray's current
// chords), and it only ever answers by repainting. `setInterceptsMouseClicks(false, false)` in
// the constructor is load-bearing for the same reason it is everywhere else in Keys - a
// component that takes no input can never eat a click meant for something behind it.
class SourceViz : public juce::Component
{
public:
    SourceViz();

    void paint(juce::Graphics&) override;

    // All pushed in from the panel; this class asks nothing and owns nothing. Each setter is a
    // no-op below repaint() when the value hasn't actually changed, since the panel polls all
    // three from a 15 Hz timer whether or not anything moved.
    void setSource(int sourceIndex); // 0..6, the genSource order (Algorithmic, Markov, Circle
                                      // of Fifths, Neo-Riemannian, Progressions, Negative
                                      // Harmony, Planing - see ChordGenPanel::sourceButtons)
    void setKey(int rootPc, int mode);
    void setChords(const std::vector<KeysProcessor::ChordPad>& chords); // what the tray holds

    // What the panel should give it. 112 px: enough for a two-line diagram (a 9 px micro-caps
    // caption plus a wheel/strip beneath it wide enough to read at arm's length) without
    // costing the window a whole extra control row.
    static int preferredHeight();

private:
    void paintCircleOfFifths(juce::Graphics&, juce::Rectangle<float>) const;
    void paintNeoRiemannian(juce::Graphics&, juce::Rectangle<float>) const;
    void paintProgressions(juce::Graphics&, juce::Rectangle<float>) const;
    void paintNegativeHarmony(juce::Graphics&, juce::Rectangle<float>) const;
    void paintPlaning(juce::Graphics&, juce::Rectangle<float>) const;
    void paintAlgorithmic(juce::Graphics&, juce::Rectangle<float>) const;
    void paintMarkov(juce::Graphics&, juce::Rectangle<float>) const;

    int source = 0;
    int rootPc = 0;
    int mode = 0;
    std::vector<KeysProcessor::ChordPad> chords;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourceViz)
};
} // namespace keys
