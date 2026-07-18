#pragma once

#include "NoteSurface.h"

namespace keys
{
// Octavium's isomorphic hex surface: flat-top hexagons in staggered columns, where
// every chord shape is the same everywhere. From any hex: straight up is +7 (a
// fifth), upper-right +4 (major third), upper-left +3 (minor third), so a major
// triad is one tight cluster and a glide walks harmonies, not semitones.
//
// Hexes sharing a MIDI note (the layout repeats pitches) light together while it
// sounds. Scale Lock snaps and dims here exactly as on the piano; Octavium's table
// had no scale awareness, which just meant more wrong notes, not a look to keep.
class HarmonicTable : public NoteSurface
{
public:
    explicit HarmonicTable(KeysProcessor&);

    void paint(juce::Graphics&) override;
    void resized() override;

protected:
    int drawnAt(juce::Point<float>) const override; // cell index, or -1
    int outputNote(int cell) const override;        // scale-lock + octave applied
    int drawnForOutputNote(int note) const override; // one matching hex (several repeat it)

private:
    static constexpr int columns = 18, rows = 9, baseNote = 24;

    static int cellNote(int cell);              // the un-transposed note a cell draws
    juce::Path hexPath(int cell) const;
    juce::Point<float> hexCentre(int cell) const;

    float hexRadius = 0.0f;   // centre to corner (flat-top: width = 2r)
    juce::Point<float> origin; // centre of the bottom-left hex

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonicTable)
};
} // namespace keys
