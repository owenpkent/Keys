#include "ChordPads.h"
#include "../Chords.h"
#include <okstudio/MouseOnly.h>
#include <okstudio/Theme.h>

namespace keys
{
namespace
{
    constexpr float kCardW = 108.0f;
    constexpr float kCardHGrid = 46.0f; // card height when the pads are a 4x4 column
    constexpr float kGap = 6.0f;
    constexpr float kRadius = 6.0f;
    constexpr int kRows = 2; // two rows of eight, Octavium parity
    bool isChord(const std::vector<int>& n) { return n.size() >= 2; }
} // namespace

ChordPads::ChordPads(KeysProcessor& p) : processor(p)
{
    okstudio::ui::makeMouseOnly(*this);
}

void ChordPads::setGridLayout(bool fourByFour)
{
    if (grid == fourByFour)
        return;
    grid = fourByFour;
    repaint();
}

juce::Rectangle<float> ChordPads::cardBounds() const
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    if (grid)
        return r.removeFromTop(kCardHGrid);
    return r.removeFromLeft(kCardW);
}

juce::Rectangle<float> ChordPads::padBounds(int visibleIndex) const
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    int rows = kRows;
    int cols = KeysProcessor::padsPerPage / kRows; // 16 / 2 = 8 across
    if (grid)
    {
        r.removeFromTop(kCardHGrid + 8.0f); // card + separation
        rows = 4;
        cols = 4;
    }
    else
    {
        r.removeFromLeft(kCardW + 10.0f); // card + separation
    }
    const int row = visibleIndex / cols;
    const int col = visibleIndex % cols;
    const float w = (r.getWidth() - kGap * (float) (cols - 1)) / (float) cols;
    const float h = (r.getHeight() - kGap * (float) (rows - 1)) / (float) rows;
    return { r.getX() + (float) col * (w + kGap), r.getY() + (float) row * (h + kGap), w, h };
}

int ChordPads::cellAt(juce::Point<float> pos) const
{
    if (cardBounds().contains(pos))
        return -2;
    for (int i = 0; i < KeysProcessor::padsPerPage; ++i)
        if (padBounds(i).contains(pos))
            return processor.padPageOffset() + i; // absolute slot, so drags survive a page flip
    return -1;
}

bool ChordPads::sourceIsDraggable() const
{
    if (dragSource == -2)
        return isChord(currentNotes);
    if (dragSource >= 0)
        return ! processor.chordPad(dragSource).notes.empty();
    return false;
}

void ChordPads::setCurrentChord(const std::vector<int>& notes)
{
    if (notes == currentNotes)
        return;
    currentNotes = notes;
    currentName = notes.empty() ? juce::String() : chords::detect(notes);
    repaint();
}

void ChordPads::paint(juce::Graphics& g)
{
    using namespace okstudio;
    const juce::Colour accent = theme::accent;
    const juce::Colour cardBg { 0xff20242b };
    const juce::Colour padBg { 0xff191d23 };
    const juce::Colour line { 0xff3b4148 };

    const int offset = processor.padPageOffset();
    const int hovered = dragging ? cellAt(dragPos) : -1;

    // Live chord card. While a filled pad is being dragged over it, it highlights to offer
    // the recall gesture (drop to pull that pad's chord back for editing).
    {
        const auto b = cardBounds();
        const bool has = isChord(currentNotes);
        const bool recallHover = dragging && dragSource >= 0 && hovered == -2;
        g.setColour(cardBg);
        g.fillRoundedRectangle(b, kRadius);
        g.setColour(has ? accent : line);
        g.drawRoundedRectangle(b, kRadius, has ? 2.0f : 1.0f);
        g.setColour(has ? theme::text : theme::textDim);
        g.setFont(juce::Font(juce::FontOptions(has ? 15.0f : 11.0f, has ? juce::Font::bold : juce::Font::plain)));
        g.drawText(has ? currentName : juce::String("hold a chord"), b.reduced(6.0f),
                   juce::Justification::centred);
        if (recallHover)
        {
            g.setColour(accent.withAlpha(0.6f));
            g.drawRoundedRectangle(b, kRadius, 2.0f);
        }
    }

    // Pads: the current page's slice, drawn two rows of eight.
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
    {
        const int i = offset + v;
        const auto b = padBounds(v);
        const auto& pad = processor.chordPad(i);
        const bool filled = ! pad.notes.empty();
        const bool active = processor.chordPadActive(i);
        const bool dropHere = dragging && hovered == i
                              && ((dragSource == -2 && isChord(currentNotes)) || (dragSource >= 0 && dragSource != i));

        g.setColour(active ? accent.withAlpha(0.85f) : padBg);
        g.fillRoundedRectangle(b, kRadius);

        if (filled)
        {
            g.setColour(active ? accent : line);
            g.drawRoundedRectangle(b, kRadius, active ? 2.0f : 1.5f);
            g.setColour(active ? juce::Colours::black.withAlpha(0.85f) : theme::text);
            g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
            g.drawText(pad.name, b.reduced(4.0f), juce::Justification::centred);
        }
        else
        {
            juce::Path outline;
            outline.addRoundedRectangle(b.reduced(0.5f), kRadius);
            const float dashes[] = { 4.0f, 3.0f };
            juce::Path dashed;
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
            g.setColour(line.withAlpha(0.7f));
            g.fillPath(dashed);
        }

        // Locked pads carry a corner dot. It is an indicator, not a target: the toggle
        // lives in the Chords panel, where it can be a full-size button.
        if (filled && pad.locked)
        {
            g.setColour(active ? juce::Colours::black.withAlpha(0.7f) : theme::good);
            g.fillEllipse(b.getRight() - 10.0f, b.getY() + 4.0f, 5.0f, 5.0f);
        }

        if (dropHere)
        {
            g.setColour(accent);
            g.drawRoundedRectangle(b, kRadius, 2.0f);
        }
    }

    // Drag ghost following the cursor.
    if (dragging && sourceIsDraggable())
    {
        const juce::String label = dragSource == -2 ? currentName : processor.chordPad(dragSource).name;
        auto ghost = juce::Rectangle<float>(0.0f, 0.0f, 84.0f, 26.0f).withCentre(dragPos);
        g.setColour(accent.withAlpha(0.92f));
        g.fillRoundedRectangle(ghost, kRadius);
        g.setColour(juce::Colours::black);
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        g.drawText(label, ghost, juce::Justification::centred);
    }
}

void ChordPads::mouseDown(const juce::MouseEvent& e)
{
    downPos = e.position;
    dragPos = e.position;
    dragging = false;
    playing = -1;
    dragSource = cellAt(e.position);

    // Beat-pad: a filled pad fires the instant you press it (release stops it below).
    if (dragSource >= 0 && ! processor.chordPad(dragSource).notes.empty())
    {
        processor.pressChordPad(dragSource);
        playing = dragSource;
    }
    repaint();
}

void ChordPads::mouseDrag(const juce::MouseEvent& e)
{
    dragPos = e.position;
    if (! dragging && e.position.getDistanceFrom(downPos) > 6.0f)
    {
        if (playing >= 0)
        {
            // A press that turns into a drag becomes a rearrange: stop the note first.
            processor.releaseChordPad(playing);
            playing = -1;
            dragging = true; // dragSource is already this pad
        }
        else if (dragSource == -2 && isChord(currentNotes))
        {
            dragging = true; // dragging the live card to capture it
        }
        else
        {
            dragSource = -1; // nothing grabbable under the press
        }
    }
    if (dragging)
        repaint();
}

void ChordPads::mouseUp(const juce::MouseEvent& e)
{
    if (playing >= 0)
    {
        processor.releaseChordPad(playing); // beat-pad: release stops it (Sustain holds it)
        playing = -1;
    }
    else if (dragging)
    {
        const int target = cellAt(e.position);
        if (dragSource == -2 && target >= 0 && isChord(currentNotes))
            processor.setChordPad(target, currentNotes, currentName); // capture the live chord
        else if (dragSource >= 0)
        {
            if (target == -2)
            {
                // Dropped a filled pad onto the live card: recall its chord for editing.
                // Not a move and not a clear - the pad stays exactly where it was.
                if (onRecall)
                    onRecall(processor.chordPad(dragSource).notes);
            }
            else if (target >= 0 && target != dragSource)
                processor.moveChordPad(dragSource, target); // rearrange
            else if (target == -1)
                processor.clearChordPad(dragSource);         // dragged off the row = clear
        }
    }
    dragging = false;
    dragSource = -1;
    repaint();
}
} // namespace keys
