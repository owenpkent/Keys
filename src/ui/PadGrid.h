#pragma once

#include "NoteSurface.h"

namespace keys
{
// Octavium's 4x4 pad grid: sixteen note pads from C1 (MIDI 36), ascending left to
// right, bottom to top. It sends on its own MIDI channel (default 10) so drums land
// where drum instruments listen while the keyboard plays elsewhere. Pads transpose
// with the Octave control but ignore Scale Lock: a drum map is not a scale, and
// snapping would silently swap which drum a pad hits.
class PadGrid : public NoteSurface
{
public:
    explicit PadGrid(KeysProcessor&);

    void paint(juce::Graphics&) override;

protected:
    int drawnAt(juce::Point<float>) const override; // cell index 0..15, or -1
    int outputNote(int cell) const override;        // base + cell + octave, clamped
    int noteChannel() const override;               // the padChannel parameter

private:
    static constexpr int columns = 4, rows = 4, baseNote = 36;

    juce::Rectangle<float> cellBounds(int cell) const;
    juce::Rectangle<float> gridArea() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadGrid)
};
} // namespace keys
