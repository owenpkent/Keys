#include "HarmonicTable.h"
#include "../NoteMath.h"
#include <cmath>
#include <okstudio/Scales.h>

namespace keys
{
HarmonicTable::HarmonicTable(KeysProcessor& p) : NoteSurface(p)
{
}

int HarmonicTable::cellNote(int cell)
{
    // Octavium's exact mapping (harmonic_table.py _recompute_grid): up a row = +7, and
    // odd columns sit half a hex LOWER, making them the -3 (lower-right) neighbours of
    // the column to their left; +1 per column pair. From any hex that still yields
    // up = +7, upper-right = +4, upper-left = +3. Base note 24 at the bottom-left.
    const int col = cell % columns;
    const int row = cell / columns;
    const int colTerm = (col % 2 == 1) ? (col / 2 - 3) : (col / 2);
    return baseNote + 7 * row + colTerm;
}

int HarmonicTable::outputNote(int cell) const
{
    return resolveOutputNote(cellNote(cell), scaleLock, rootPc, scaleIndex, processor.octaveShift());
}

int HarmonicTable::drawnForOutputNote(int note) const
{
    // The layout repeats pitches; prefer the copy nearest the middle of the grid so a
    // recalled chord lands as one visible cluster.
    const int wanted = note - processor.octaveShift() * 12;
    int best = -1, bestDist = 1 << 30;
    for (int cell = 0; cell < columns * rows; ++cell)
    {
        if (cellNote(cell) != wanted)
            continue;
        const int col = cell % columns, row = cell / columns;
        const int d = std::abs(col - columns / 2) * 2 + std::abs(row - rows / 2) * 3;
        if (d < bestDist)
        {
            bestDist = d;
            best = cell;
        }
    }
    return best;
}

void HarmonicTable::resized()
{
    // Fit columns horizontally (flat-top: columns advance by 1.5r) and rows vertically
    // (row height = hex height = sqrt(3)*r), whichever is tighter.
    const auto area = getLocalBounds().toFloat().reduced(8.0f);
    if (area.isEmpty())
        return;
    const float byWidth = area.getWidth() / (1.5f * (float) (columns - 1) + 2.0f);
    const float byHeight = area.getHeight() / (std::sqrt(3.0f) * ((float) rows + 0.5f));
    hexRadius = juce::jmin(byWidth, byHeight);

    const float gridW = 1.5f * (float) (columns - 1) * hexRadius + 2.0f * hexRadius;
    const float rowH = std::sqrt(3.0f) * hexRadius;
    const float gridH = rowH * ((float) rows + 0.5f);
    origin = { area.getCentreX() - gridW * 0.5f + hexRadius,
               area.getCentreY() + gridH * 0.5f - rowH };
}

juce::Point<float> HarmonicTable::hexCentre(int cell) const
{
    const int col = cell % columns;
    const int row = cell / columns;
    const float rowH = std::sqrt(3.0f) * hexRadius;
    // Odd columns sit half a hex lower (Octavium's stagger; cellNote depends on it).
    const float y = origin.y - (float) row * rowH + (col % 2 == 1 ? rowH * 0.5f : 0.0f);
    const float x = origin.x + (float) col * 1.5f * hexRadius;
    return { x, y };
}

juce::Path HarmonicTable::hexPath(int cell) const
{
    const auto c = hexCentre(cell);
    const float r = hexRadius * 0.96f; // hairline gap between hexes
    juce::Path p;
    for (int i = 0; i < 6; ++i)
    {
        const float a = juce::MathConstants<float>::pi / 3.0f * (float) i; // flat-top
        const juce::Point<float> pt { c.x + r * std::cos(a), c.y + r * std::sin(a) };
        if (i == 0)
            p.startNewSubPath(pt);
        else
            p.lineTo(pt);
    }
    p.closeSubPath();
    return p;
}

int HarmonicTable::drawnAt(juce::Point<float> pos) const
{
    if (hexRadius <= 0.0f)
        return -1;
    // Nearest centre, accepted if inside the hex's inradius-ish reach. Cheap and exact
    // enough that the hairline gaps between hexes still hit the nearer one.
    int best = -1;
    float bestDist = hexRadius * hexRadius * 1.1f;
    for (int cell = 0; cell < columns * rows; ++cell)
    {
        const auto c = hexCentre(cell);
        const float dx = pos.x - c.x, dy = pos.y - c.y;
        const float d = dx * dx + dy * dy;
        if (d < bestDist)
        {
            bestDist = d;
            best = cell;
        }
    }
    return best;
}

void HarmonicTable::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1b1b1b));

    // Duplicate-note highlight: every hex resolving to a sounding note lights, not
    // just the one under the mouse.
    std::set<int> onNotes;
    for (const auto& kv : sounding)
        onNotes.insert(kv.second);

    for (int cell = 0; cell < columns * rows; ++cell)
    {
        const int note = cellNote(cell);
        const int out = outputNote(cell);
        const bool lit = onNotes.count(out) > 0;
        const bool self = pressed.count(cell) > 0 || latched.count(cell) > 0 || sustained.count(cell) > 0;
        const bool dim = ! lit && scaleLock && ! okstudio::scales::isInScale(note, rootPc, scaleIndex);

        // Idle tint follows the octave (Octavium's per-octave palette, toned to theme).
        const float hue = 0.55f + 0.045f * (float) ((note / 12) % 5);
        juce::Colour fill = juce::Colour::fromHSV(hue, 0.25f, dim ? 0.13f : 0.22f, 1.0f);
        if (lit)
            fill = self ? juce::Colour(0xff2f82e6) : juce::Colour(0xff2b7ade);

        const auto path = hexPath(cell);
        g.setColour(fill);
        g.fillPath(path);
        g.setColour(lit ? juce::Colour(0xff6bb8ff) : juce::Colour(0xff30353c));
        g.strokePath(path, juce::PathStrokeType(1.0f));

        if (hexRadius >= 13.0f)
        {
            // Name + octave like Octavium's hex labels, on Keys' C4=60 convention
            // (Octavium used two conflicting conventions in one file; we pick one).
            const auto name = okstudio::scales::noteNames()[((note % 12) + 12) % 12]
                              + juce::String(note / 12 - 1);
            g.setColour(lit ? juce::Colours::white.withAlpha(0.9f)
                            : juce::Colour(0xff8a919a).withAlpha(dim ? 0.4f : 0.8f));
            g.setFont(juce::Font(juce::FontOptions(juce::jmin(12.0f, hexRadius * 0.55f))));
            const auto c = hexCentre(cell);
            g.drawText(name, juce::Rectangle<float>(c.x - hexRadius, c.y - 9.0f, hexRadius * 2.0f, 18.0f),
                       juce::Justification::centred);
        }
    }
}
} // namespace keys
