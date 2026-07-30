#include "ChordPads.h"
#include "../Chords.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>
#include <okstudio/Scales.h>
#include <algorithm>

namespace keys
{
namespace
{
    constexpr float kCardW = 108.0f;
    constexpr float kGap = 6.0f;
    constexpr float kRadius = 6.0f;
    constexpr float kSaveW = 38.0f; // the edit tick's strip at the right end of a pad
    // Below this a pad is a name and nothing else; above it there is room for the note list
    // and the mini keyboard as well. A height, not a mode flag, so the card draws whatever
    // its space affords and Big/Small stays purely a question of layout.
    constexpr float kRichCardH = 58.0f;
    bool isChord(const std::vector<int>& n) { return n.size() >= 2; }

    // The two halves of the full chord card. They were the chord generator's, drawn onto its
    // own copy of this same page of pads; the generator's grid is gone and they live here.
    juce::String noteListText(const std::vector<int>& notes)
    {
        const auto names = okstudio::scales::noteNames();
        juce::String out;
        for (const int n : notes)
            out << (out.isEmpty() ? "" : "  ") << names[((n % 12) + 12) % 12] << juce::String(n / 12 - 1);
        return out;
    }

    // Two octaves (three when the chord spills over) from the low note's C, held keys lit.
    // Purely informative, never a target.
    void drawMiniKeyboard(juce::Graphics& g, juce::Rectangle<float> r, const std::vector<int>& notes,
                          skin::Accent ac)
    {
        if (notes.empty())
            return;
        const int lo = *std::min_element(notes.begin(), notes.end());
        const int hi = *std::max_element(notes.begin(), notes.end());
        const int base = (lo / 12) * 12;
        const int octaves = hi < base + 24 ? 2 : 3;
        const auto held = [&notes](int n)
        { return std::find(notes.begin(), notes.end(), n) != notes.end(); };

        constexpr int whitePc[7] = { 0, 2, 4, 5, 7, 9, 11 };
        const int whites = octaves * 7;
        const float ww = r.getWidth() / (float) whites;
        for (int i = 0; i < whites; ++i)
        {
            const int note = base + (i / 7) * 12 + whitePc[i % 7];
            const auto key = juce::Rectangle<float>(r.getX() + ww * (float) i, r.getY(),
                                                    ww, r.getHeight()).reduced(0.5f, 0.0f);
            g.setColour(held(note) ? ac.base : juce::Colours::white.withAlpha(0.30f));
            g.fillRoundedRectangle(key, 1.0f);
        }

        constexpr int blackAfterWhite[5] = { 0, 1, 3, 4, 5 }; // C# D# F# G# A#
        constexpr int blackPc[5] = { 1, 3, 6, 8, 10 };
        const float bw = ww * 0.62f, bh = r.getHeight() * 0.62f;
        for (int o = 0; o < octaves; ++o)
            for (int b = 0; b < 5; ++b)
            {
                const int note = base + o * 12 + blackPc[b];
                const float x = r.getX() + ww * (float) (o * 7 + blackAfterWhite[b] + 1) - bw * 0.5f;
                g.setColour(held(note) ? ac.hot : juce::Colour(0xff101216));
                g.fillRoundedRectangle({ x, r.getY(), bw, bh }, 1.0f);
            }
    }
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

void ChordPads::setBigCards(bool big)
{
    if (bigCards == big)
        return;
    bigCards = big;
    repaint(); // geometry is computed per paint, so there is nothing else to move
}

juce::Rectangle<float> ChordPads::padBounds(int visibleIndex) const
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    const int rows = rowsFor(bigCards);
    const int cols = KeysProcessor::padsPerPage / rows; // 16 as 2x8 or 4x4
    r.removeFromLeft(kCardW + 10.0f); // card + separation
    const int row = visibleIndex / cols;
    const int col = visibleIndex % cols;
    const float w = (r.getWidth() - kGap * (float) (cols - 1)) / (float) cols;
    const float h = (r.getHeight() - kGap * (float) (rows - 1)) / (float) rows;
    return { r.getX() + (float) col * (w + kGap), r.getY() + (float) row * (h + kGap), w, h };
}

juce::Rectangle<float> ChordPads::saveBadgeBounds(juce::Rectangle<float> pad)
{
    return pad.removeFromRight(kSaveW).reduced(3.0f); // `pad` is a copy; this trims that copy
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
            g.setColour(skin::accentOf(*this).base.withAlpha(0.10f));
            g.fillRoundedRectangle(b, kRadius);
            skin::glowRect(g, b, kRadius, skin::accentOf(*this).base, 0.9f);
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
            skin::glowRect(g, b, kRadius, skin::accentOf(*this).hot);
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

        // The pad being edited gives up its right end to the tick that ends the edit, so
        // the chord name moves over rather than running underneath it.
        auto nameArea = b;
        if (i == editingSlot)
            nameArea.removeFromRight(kSaveW);

        if (! filled)
        {
            g.setColour(skin::well.withAlpha(0.55f));
            g.fillRoundedRectangle(b, kRadius);
            g.setColour(juce::Colours::white.withAlpha(0.035f));
            g.drawRoundedRectangle(b, kRadius, 1.0f);
        }
        else
        {
            if (active)
            {
                g.setGradientFill({ skin::accentOf(*this).hot, 0.0f, b.getY(),
                                    skin::accentOf(*this).base, 0.0f, b.getBottom(), false });
                g.fillRoundedRectangle(b, kRadius);
                skin::glowRect(g, b, kRadius, skin::accentOf(*this).base);
            }
            else
            {
                skin::raisedFill(g, b, kRadius, juce::Colour(0xff272b32), juce::Colour(0xff1e2126));
            }

            // Tall enough, and the card says what the chord *is* as well as what it is
            // called: the note list with octave numbers, and a mini keyboard of the shape
            // under your hand. Short, and there is only room for the name, which is the
            // strip this section has always been.
            const auto ink = active ? inkOnAccent : skin::text;
            auto text = nameArea.reduced(4.0f);
            if (text.getHeight() >= kRichCardH)
            {
                text = text.reduced(6.0f, 4.0f);
                const float kbH = juce::jmin(24.0f, text.getHeight() * 0.34f);
                const auto kb = text.removeFromBottom(kbH)
                                    .withSizeKeepingCentre(juce::jmin(170.0f, text.getWidth()), kbH);
                text.removeFromBottom(3.0f);
                const auto noteLine = text.removeFromBottom(13.0f);

                g.setColour(ink);
                g.setFont(skin::uiSemi(16.0f));
                g.drawText(pad.name, text, juce::Justification::centred, true);

                g.setColour(active ? inkOnAccent.withAlpha(0.75f) : skin::textDim);
                g.setFont(skin::micro(10.0f));
                g.drawText(noteListText(pad.notes), noteLine.toNearestInt(),
                           juce::Justification::centred, true);

                drawMiniKeyboard(g, kb, pad.notes, skin::accentOf(*this));
            }
            else
            {
                g.setColour(ink);
                g.setFont(skin::uiSemi(13.5f));
                g.drawText(pad.name, text, juce::Justification::centred);
            }
        }

        // The pad currently feeding the arp. A ring rather than the "active" fill, because
        // it is a different state: the chord is held for the arp to chew on, not simply
        // sounding, and both can be true at once.
        // Not gated on `filled`: a card cleared while it was feeding the arp still owns the
        // sounding chord, and an invisible ring is a chord you cannot find or stop.
        if (processor.arpHeldPad() == i)
        {
            g.setColour(skin::accentOf(*this).hot);
            g.drawRoundedRectangle(b.reduced(1.0f), kRadius, 2.0f);
            skin::glowRect(g, b, kRadius, skin::accentOf(*this).hot, 0.8f);
        }

        // Locked pads carry a corner dot. It is an indicator, not a target: the toggle is
        // Lock on this pad's own card menu, where it can be a full-size item.
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
            skin::glowRect(g, b, kRadius, skin::accentOf(*this).hot);

        // The pad currently linked to the keyboard for editing, and the tick that ends the
        // link. The tick lives on the pad itself (Owen's call, 2026-07-27): the edit is a
        // thing happening *to this card*, and a button parked on the section bar meant
        // looking away from the card to finish. It is drawn as a path rather than a glyph,
        // like every other mark in the skin, so it scales with the pad and never depends on
        // a font having the character.
        if (i == editingSlot)
        {
            skin::glowRect(g, b, kRadius, skin::accentOf(*this).hot);
            g.setColour(skin::accentOf(*this).hot);
            g.setFont(skin::micro(8.0f));
            g.drawText("EDIT", nameArea.reduced(6.0f, 3.0f).toNearestInt(), juce::Justification::topLeft);

            const auto badge = saveBadgeBounds(b);
            g.setColour(theme::good.withAlpha(0.22f));
            g.fillRoundedRectangle(badge, 5.0f);
            g.setColour(theme::good.withAlpha(0.75f));
            g.drawRoundedRectangle(badge.reduced(0.5f), 5.0f, 1.0f);

            const auto c = badge.getCentre();
            const float s = juce::jmin(badge.getWidth(), badge.getHeight()) * 0.28f;
            juce::Path tick;
            tick.startNewSubPath(c.x - s, c.y + s * 0.05f);
            tick.lineTo(c.x - s * 0.25f, c.y + s * 0.75f);
            tick.lineTo(c.x + s, c.y - s * 0.7f);
            g.setColour(theme::good);
            g.strokePath(tick, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }
    }

    // Drag ghost following the cursor.
    if (dragging && sourceIsDraggable())
    {
        const juce::String label = dragSource == -2 ? currentName : processor.chordPad(dragSource).name;
        auto ghost = juce::Rectangle<float>(0.0f, 0.0f, 84.0f, 26.0f).withCentre(dragPos);
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(ghost.translated(0.0f, 2.0f), kRadius);
        g.setGradientFill({ skin::accentOf(*this).hot, 0.0f, ghost.getY(), skin::accentOf(*this).base, 0.0f, ghost.getBottom(), false });
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

bool ChordPads::toArp() const
{
    return processor.cardsFeedArp();
}

void ChordPads::showPadMenu(int slot)
{
    const auto& pad = processor.chordPad(slot);
    const bool editing = slot == editingSlot;

    // Three groups down this menu, each behind a section header: what acts on this pad, what
    // acts on the whole page, and the generator's settings. The generator adds the last two
    // and the tail of the first, so the order the items are built in is the order they read
    // in - Send to arp slot moved up here (2026-07-30) to close the pad group before it.
    juce::PopupMenu menu;
    menu.addSectionHeader("This pad");
    menu.addItem(1, editing ? "Done editing" : "Edit on keyboard");
    menu.addItem(2, "Clear pad", ! pad.notes.empty() && ! pad.locked);
    // Lock lives here now. The strip has always painted the lock dot and never been able to
    // set it - the toggle was on the chord generator's own copy of these same pads, so a
    // state you could see from the keyboard could only be changed from another view.
    menu.addItem(3, pad.locked ? "Unlock" : "Lock", ! pad.notes.empty());

    // Bind this card to an arp slot, so launching that slot plays this chord through that
    // slot's pattern. The other half of the "cards into the arp" pair: a click with the arp
    // On holds a card right now, this parks one in a slot for later.
    juce::PopupMenu slots;
    for (int s = 0; s < KeysProcessor::numArpPatterns; ++s)
    {
        const auto& target = processor.arpPatternSlot(s);
        auto label = juce::String(s + 1);
        if (target.chordName.isNotEmpty())
            label += "  (" + target.chordName + ")";
        slots.addItem(100 + s, label, ! pad.notes.empty());
    }
    menu.addSubMenu("Send to arp slot", slots, ! pad.notes.empty());

    // Whatever else can act on this pad right now: the generator adds New chord, its
    // suggestion families and every setting it has. It has no panel and no view of its own
    // since 2026-07-30, so this is where all of it is, on every pad and every page.
    if (onExtraMenuItems)
        onExtraMenuItems(slot, menu);

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
        else if (choice == 3)
        {
            safe->processor.setChordPadLocked(slot, ! safe->processor.chordPad(slot).locked);
        }
        else if (choice >= 100 && choice < 100 + KeysProcessor::numArpPatterns)
        {
            const auto& pad = safe->processor.chordPad(slot);
            safe->processor.setArpSlotChord(choice - 100, pad.notes, pad.name);
        }
        else if (choice >= extraMenuIdBase && safe->onExtraMenuChoice)
        {
            safe->onExtraMenuChoice(slot, choice);
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

    // The tick on the pad being edited ends the edit, and is checked before anything else
    // a click on that pad could mean: on this one card, in this one state, the right-hand
    // end is a button and not the pad. Nothing is set up for a drag or a press, so the
    // mouseUp that follows has nothing to undo.
    if (editingSlot >= 0)
    {
        const int visible = editingSlot - processor.padPageOffset();
        if (visible >= 0 && visible < KeysProcessor::padsPerPage
            && saveBadgeBounds(padBounds(visible)).contains(e.position))
        {
            dragging = false;
            dragSource = -1;
            playing = -1;
            if (onEditToggle)
                onEditToggle(editingSlot); // toggling the slot that is already editing ends it
            repaint();
            return;
        }
    }

    downPos = e.position;
    dragPos = e.position;
    dragging = false;
    playing = -1;
    dragSource = cellAt(e.position);

    // Arp On: a filled pad hands its chord to the arpeggiator and it stays there. Not a
    // beat-pad press, so `playing` stays -1 and mouseUp has nothing to release; a second
    // click on the pad already feeding the arp re-plays it, the way a second press on a
    // beat pad re-fires it. Checked before the beat-pad branch because in this mode the
    // click means something else entirely.
    //
    // The `arpHeldPad()` half of the test is not redundant with the notes test: a card can
    // be cleared while it is still the one feeding the arp, and then it wears the ring with
    // no notes behind it. Without that clause the click falls past every branch below and
    // does nothing at all, which is a dead click on a lit target.
    if (toArp() && dragSource >= 0
        && (! processor.chordPad(dragSource).notes.empty() || processor.arpHeldPad() == dragSource))
    {
        if (processor.chordPad(dragSource).notes.empty())
        {
            // Ringed but empty: there is nothing to re-play, so the click means the only
            // other thing it can mean. This is the ring's own way out, and the reason it is
            // drawn on a cleared card at all.
            processor.releaseArpChord();
        }
        else
        {
            // Re-playing the holder is a retrigger, never a second owner on the same
            // pitches: holdArpChordFromPad goes through holdArpChord, which releases the
            // previous hold (releaseNotes on arpChordTag, so the refs and the arp's held set
            // both unwind) before it fires, and applies Exclusive to the new one. Stopping a
            // filled card's hold outright is the Hold off button on the arp bar.
            processor.holdArpChordFromPad(dragSource);
        }

        dragSource = -1; // and it is not a drag handle in this mode either
        repaint();
        return;
    }

    // The live "current chord" card is a chord card too, and the mode's tooltip says so.
    if (toArp() && dragSource == -2 && isChord(currentNotes))
    {
        processor.holdArpChord(currentNotes, currentName);
        dragSource = -1;
        repaint();
        return;
    }

    // Beat-pad: a filled pad fires the instant you press it (release stops it below).
    if (dragSource >= 0 && ! processor.chordPad(dragSource).notes.empty())
    {
        processor.pressChordPad(dragSource);
        playing = dragSource;
    }
    else if (dragSource == -2 && isChord(currentNotes))
    {
        // The live card plays too. Holding a chord on the keyboard sounds the keys you
        // are holding; clicking the card fires the same notes as one chord, so you hear
        // it strummed and humanized the way a pad would play it. A drag still captures
        // (mouseDrag stops this the moment the mouse actually moves).
        processor.pressLiveChord(currentNotes);
        playingLive = true;
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
            if (playingLive)
            {
                processor.releaseLiveChord(true); // a press that became a drag is a capture
                playingLive = false;
            }
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
    if (playingLive)
    {
        processor.releaseLiveChord(); // Sustain holds it, same as a pad
        playingLive = false;
    }
    else if (playing >= 0)
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
