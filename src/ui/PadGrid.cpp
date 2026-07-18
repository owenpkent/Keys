#include "PadGrid.h"
#include <okstudio/Scales.h>

namespace keys
{
PadGrid::PadGrid(KeysProcessor& p) : NoteSurface(p)
{
}

int PadGrid::noteChannel() const
{
    return processor.padGridChannel();
}

int PadGrid::outputNote(int cell) const
{
    return juce::jlimit(0, 127, baseNote + cell + processor.octaveShift() * 12);
}

juce::Rectangle<float> PadGrid::gridArea() const
{
    // Largest centred square that fits, so pads stay square as the window resizes.
    auto area = getLocalBounds().toFloat().reduced(6.0f);
    const float side = juce::jmin(area.getWidth(), area.getHeight());
    return area.withSizeKeepingCentre(side, side);
}

juce::Rectangle<float> PadGrid::cellBounds(int cell) const
{
    const auto grid = gridArea();
    const float w = grid.getWidth() / (float) columns;
    const float h = grid.getHeight() / (float) rows;
    const int col = cell % columns;
    const int row = cell / columns; // row 0 = bottom (ascending upward, like Octavium)
    return juce::Rectangle<float>(grid.getX() + (float) col * w,
                                  grid.getBottom() - (float) (row + 1) * h, w, h)
        .reduced(4.0f);
}

int PadGrid::drawnAt(juce::Point<float> pos) const
{
    for (int c = 0; c < columns * rows; ++c)
        if (cellBounds(c).contains(pos))
            return c;
    return -1;
}

void PadGrid::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1b1b1b));

    for (int c = 0; c < columns * rows; ++c)
    {
        const auto b = cellBounds(c);
        const bool active = pressed.count(c) > 0;
        const bool held = ! active && (latched.count(c) > 0 || sustained.count(c) > 0);

        // Octavium's pad chrome: 10px radius, 2px border, #2b2f36 idle / #2f82e6 lit.
        juce::Colour fill = juce::Colour(0xff2b2f36);
        if (active)    fill = juce::Colour(0xff2f82e6);
        else if (held) fill = juce::Colour(0xff2b7ade);
        g.setColour(fill);
        g.fillRoundedRectangle(b, 10.0f);
        g.setColour(active || held ? juce::Colour(0xff2a6fc2) : juce::Colour(0xff3b4148));
        g.drawRoundedRectangle(b, 10.0f, 2.0f);

        // Note name in the corner for orientation (pads are otherwise identical).
        const int note = outputNote(c);
        const auto label = okstudio::scales::noteNames()[note % 12] + juce::String(note / 12 - 1);
        g.setColour(active || held ? juce::Colours::white.withAlpha(0.85f)
                                   : juce::Colour(0xff70767d));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(label, b.reduced(7.0f, 5.0f), juce::Justification::bottomLeft);
    }
}
} // namespace keys
