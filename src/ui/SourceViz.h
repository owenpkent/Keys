#pragma once

#include "../PluginProcessor.h"
#include "../ScaleModes.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace keys
{
// A read-only picture of what the currently selected chord-generation source is doing (Owen,
// 2026-08-01: "a visualization for the generation source so people understand what it's
// doing"). It draws the shape a source walks - the fifths wheel, the PLR chain, the mirror
// clock - and, when the audition tray holds chords, draws the actual walk that produced them on
// top of it, plus a one-line legend saying in words what the picture shows (2026-08-17: every
// diagram used to give the picture a fixed square and spend the rest of its width on a row of
// chips restating chord names the tray below already lists - gone now, in favour of the legend
// and a diagram that gets the full band).
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
    void setSource(int sourceIndex); // 0..7, the genSource order (Algorithmic, Markov, Circle
                                      // of Fifths, Neo-Riemannian, Progressions, Negative
                                      // Harmony, Planing, Library - see ChordGenPanel::sourceButtons)
    void setKey(int rootPc, int mode);

    // The Library row the last generation landed on, for that source's caption. Pushed in like
    // everything else here: this class asks nothing and owns nothing.
    void setLibraryEntry(const juce::String&);
    void setChords(const std::vector<KeysProcessor::ChordPad>& chords); // what the tray holds

    // What the panel should give it. 160 px (up from 112, 2026-08-17): a 9 px micro-caps caption
    // row plus the diagram itself at the full width of the window. Every source used to spend
    // most of that width on a row of chips restating chord names the tray below already shows;
    // those are gone, so the whole band goes to the picture instead.
    static int preferredHeight();

private:
    void paintCircleOfFifths(juce::Graphics&, juce::Rectangle<float>) const;
    void paintNeoRiemannian(juce::Graphics&, juce::Rectangle<float>) const;
    // Shared by Progressions and Library: both are "a written-down sequence, repeated", and the
    // only thing that differs is what to call it. Passing the caption in is cheaper and safer than
    // a second copy of the repeat-period search and the numeral strip that reads it.
    void paintProgressions(juce::Graphics&, juce::Rectangle<float>, const char* title,
                           const juce::String& subtitle) const;
    void paintNegativeHarmony(juce::Graphics&, juce::Rectangle<float>) const;
    void paintPlaning(juce::Graphics&, juce::Rectangle<float>) const;
    void paintAlgorithmic(juce::Graphics&, juce::Rectangle<float>) const;
    void paintMarkov(juce::Graphics&, juce::Rectangle<float>) const;

    int source = 0;
    int rootPc = 0;
    int mode = 0;
    std::vector<KeysProcessor::ChordPad> chords;
    juce::String libraryEntry;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourceViz)
};
} // namespace keys
