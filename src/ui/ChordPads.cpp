#include "ChordPads.h"
#include "../Chords.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>

namespace keys
{
namespace
{
    constexpr float kCardW = 108.0f;
    constexpr float kGap = 6.0f;
    constexpr float kRadius = 6.0f;
    constexpr int kRows = 2; // two rows of eight, Octavium parity
    bool isChord(const std::vector<int>& n) { return n.size() >= 2; }
} // namespace

ChordPads::ChordPads(KeysProcessor& p) : processor(p)
{
    okstudio::ui::makeMouseOnly(*this);
}

juce::Rectangle<float> ChordPads::cardBounds() const
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    return r.removeFromLeft(kCardW);
}

juce::Rectangle<float> ChordPads::padBounds(int visibleIndex) const
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    const int rows = kRows;
    const int cols = KeysProcessor::padsPerPage / kRows; // 16 / 2 = 8 across
    r.removeFromLeft(kCardW + 10.0f); // card + separation
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
    const juce::Colour inkOnAccent { 0xff07272c }; // dark ink for text on lit surfaces

    const int offset = processor.padPageOffset();
    const int hovered = dragging ? cellAt(dragPos) : -1;

    // Live chord card: an inset well that lights up while a chord is sounding.
    // While a filled pad is being dragged over it, it highlights to offer the
    // recall gesture (drop to pull that pad's chord back for editing).
    {
        const auto b = cardBounds();
        const bool has = isChord(currentNotes);
        const bool recallHover = dragging && dragSource >= 0 && hovered == -2;

        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawRoundedRectangle(b.expanded(0.5f), kRadius + 0.5f, 1.0f);
        g.setColour(skin::well);
        g.fillRoundedRectangle(b, kRadius);
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(b.withHeight(2.0f).reduced(kRadius, 0.0f), 1.0f);

        if (has)
        {
            g.setColour(skin::accent.withAlpha(0.10f));
            g.fillRoundedRectangle(b, kRadius);
            skin::glowRect(g, b, kRadius, skin::accent, 0.9f);
        }
        else
        {
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.drawRoundedRectangle(b, kRadius, 1.0f);
        }
        g.setColour(has ? skin::text : skin::textFaint);
        g.setFont(has ? skin::uiSemi(15.0f) : skin::ui(11.0f));
        g.drawText(has ? currentName : juce::String("hold a chord"), b.reduced(6.0f),
                   juce::Justification::centred);
        if (recallHover)
            skin::glowRect(g, b, kRadius, skin::accentHot);
    }

    // Pads: the current page's slice, drawn two rows of eight. Empty slots are
    // quiet inset wells; filled pads are raised chips; a sounding pad is lit.
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
    {
        const int i = offset + v;
        const auto b = padBounds(v);
        const auto& pad = processor.chordPad(i);
        const bool filled = ! pad.notes.empty();
        const bool active = processor.chordPadActive(i);
        const bool dropHere = dragging && hovered == i
                              && ((dragSource == -2 && isChord(currentNotes)) || (dragSource >= 0 && dragSource != i));

        if (! filled)
        {
            g.setColour(skin::well.withAlpha(0.55f));
            g.fillRoundedRectangle(b, kRadius);
            g.setColour(juce::Colours::white.withAlpha(0.035f));
            g.drawRoundedRectangle(b, kRadius, 1.0f);
        }
        else if (active)
        {
            g.setGradientFill({ skin::accentHot, 0.0f, b.getY(), skin::accent, 0.0f, b.getBottom(), false });
            g.fillRoundedRectangle(b, kRadius);
            skin::glowRect(g, b, kRadius, skin::accent);
            g.setColour(inkOnAccent);
            g.setFont(skin::uiSemi(13.5f));
            g.drawText(pad.name, b.reduced(4.0f), juce::Justification::centred);
        }
        else
        {
            skin::raisedFill(g, b, kRadius, juce::Colour(0xff272b32), juce::Colour(0xff1e2126));
            g.setColour(skin::text);
            g.setFont(skin::uiSemi(13.5f));
            g.drawText(pad.name, b.reduced(4.0f), juce::Justification::centred);
        }

        // Locked pads carry a corner dot. It is an indicator, not a target: the toggle
        // lives in the Chords panel, where it can be a full-size button.
        if (filled && pad.locked)
        {
            const auto dot = juce::Rectangle<float>(5.0f, 5.0f)
                                 .withCentre({ b.getRight() - 8.0f, b.getY() + 8.0f });
            const auto c = active ? inkOnAccent : theme::good;
            g.setColour(c.withAlpha(0.4f));
            g.fillEllipse(dot.expanded(2.0f));
            g.setColour(c);
            g.fillEllipse(dot);
        }

        if (dropHere)
            skin::glowRect(g, b, kRadius, skin::accentHot);

        // The pad currently linked to the keyboard for editing.
        if (i == editingSlot)
        {
            skin::glowRect(g, b, kRadius, skin::accentHot);
            g.setColour(skin::accentHot);
            g.setFont(skin::micro(8.0f));
            g.drawText("EDIT", b.reduced(6.0f, 3.0f).toNearestInt(), juce::Justification::topLeft);
        }
    }

    // Drag ghost following the cursor.
    if (dragging && sourceIsDraggable())
    {
        const juce::String label = dragSource == -2 ? currentName : processor.chordPad(dragSource).name;
        auto ghost = juce::Rectangle<float>(0.0f, 0.0f, 84.0f, 26.0f).withCentre(dragPos);
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(ghost.translated(0.0f, 2.0f), kRadius);
        g.setGradientFill({ skin::accentHot, 0.0f, ghost.getY(), skin::accent, 0.0f, ghost.getBottom(), false });
        g.fillRoundedRectangle(ghost, kRadius);
        g.setColour(inkOnAccent);
        g.setFont(skin::uiSemi(12.0f));
        g.drawText(label, ghost, juce::Justification::centred);
    }
}

void ChordPads::setEditingSlot(int slot)
{
    if (editingSlot == slot)
        return;
    editingSlot = slot;
    repaint();
}

void ChordPads::showPadMenu(int slot)
{
    const auto& pad = processor.chordPad(slot);
    const bool editing = slot == editingSlot;

    juce::PopupMenu menu;
    menu.addItem(1, editing ? "Done editing" : "Edit on keyboard");
    menu.addItem(2, "Clear pad", ! pad.notes.empty() && ! pad.locked);

    const auto area = localAreaToGlobal(padBounds(slot - processor.padPageOffset()).toNearestInt());
    juce::Component::SafePointer<ChordPads> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetScreenArea(area)
                           .withStandardItemHeight(okstudio::ui::minHitPx), // mouse-only: no small targets
                       [safe, slot](int choice)
    {
        if (safe == nullptr)
            return;
        if (choice == 1 && safe->onEditToggle)
        {
            safe->onEditToggle(slot);
        }
        else if (choice == 2)
        {
            if (slot == safe->editingSlot && safe->onEditToggle)
                safe->onEditToggle(slot); // end the edit before wiping its target
            safe->processor.clearChordPad(slot);
        }
    });
}

void ChordPads::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        // Right-click never plays or drags; it opens the pad's card menu.
        const int cell = cellAt(e.position);
        if (cell >= 0)
            showPadMenu(cell);
        return;
    }

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
