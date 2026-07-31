#include "ChordPads.h"
#include "../ChordGen.h"
#include "../ChordSuggest.h"
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

    bool onKeyboard(const std::vector<int>& notes)
    {
        return std::none_of(notes.begin(), notes.end(), [](int n) { return n < 0 || n > 127; });
    }

    // The whole chord an octave down or up, or empty when that would run it off the ends of
    // MIDI. No wrapping: a chord that cannot move in one piece does not move at all, and the
    // menu item greys out rather than folding a note round to the other end of the keyboard.
    std::vector<int> shiftedOctave(const std::vector<int>& notes, int semitones)
    {
        if (notes.empty())
            return {};
        std::vector<int> out;
        out.reserve(notes.size());
        for (const int n : notes)
            out.push_back(n + semitones);
        return onKeyboard(out) ? out : std::vector<int> {};
    }

    std::vector<int> sortedCopy(std::vector<int> v)
    {
        std::sort(v.begin(), v.end());
        return v;
    }

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

    // The lock chip in a filled card's top-right corner: the target `lockBadgeBounds` hands
    // back, painted at whatever size that target came out. Until 2026-07-30 the mark here was a
    // 5 px dot at alpha 0.28 inside a 34 px hit area, which on a docked card is 79% of the
    // card's height: a click half a card *below* the dot silently toggled the lock, and nothing
    // on screen said the corner was a control at all. A chip cannot be hit without being seen.
    //
    // Drawn as paths rather than a font glyph, like the edit tick, so it scales with the card
    // and never depends on a character existing. Unlocked is an inset well with the shackle
    // lifted clear and leaning open; locked lights the whole chip and closes the shackle onto
    // the body.
    void drawLockBadge(juce::Graphics& g, juce::Rectangle<float> r, bool locked, juce::Colour ink)
    {
        const auto chip = r.reduced(3.0f);
        if (chip.getWidth() < 8.0f || chip.getHeight() < 8.0f)
            return;

        g.setColour(locked ? ink.withAlpha(0.30f) : juce::Colours::black.withAlpha(0.22f));
        g.fillRoundedRectangle(chip, 4.0f);
        g.setColour(ink.withAlpha(locked ? 0.85f : 0.38f));
        g.drawRoundedRectangle(chip.reduced(0.5f), 4.0f, 1.0f);

        const float s = juce::jmin(chip.getWidth(), chip.getHeight());
        const auto body = juce::Rectangle<float>(s * 0.50f, s * 0.34f)
                              .withCentre({ chip.getCentreX(), chip.getCentreY() + s * 0.08f });
        const float rad = body.getWidth() * 0.32f;

        juce::Path shackle;
        shackle.addCentredArc(locked ? body.getCentreX() : body.getCentreX() + rad * 0.55f,
                              body.getY() - (locked ? 0.0f : s * 0.08f), rad, rad, 0.0f,
                              -juce::MathConstants<float>::halfPi,
                              juce::MathConstants<float>::halfPi, true);

        g.setColour(ink.withAlpha(locked ? 0.95f : 0.50f));
        g.strokePath(shackle, juce::PathStrokeType(juce::jmax(1.2f, s * 0.09f),
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.fillRoundedRectangle(body, s * 0.06f);
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

juce::Rectangle<float> ChordPads::lockBadgeBounds(juce::Rectangle<float> pad)
{
    // Scaled to the card, not fixed at the mouse-only floor. A flat 34 px square was 27% of a
    // docked card at the default width and 34% at the 820 px minimum - and 79% of its height,
    // across the whole right-hand end - which is a third of the card that no longer plays the
    // chord, no longer arms a drag and (this branch being tested first) no longer feeds the arp.
    //
    // 0.30 of the width rather than 0.28: it puts the chip on exactly 24 px at both the default
    // and the minimum width, which is the size of every control on every section bar. That is
    // the convention for an accelerator beside a full-size primary path, and the primary path
    // here is the card menu's Lock item. Big cards have the room for the full 34.
    // `pad` is a copy; this trims that copy.
    const float s = (float) juce::jmin(okstudio::ui::minHitPx,
                                       juce::roundToInt(pad.getHeight() * 0.55f),
                                       juce::roundToInt(pad.getWidth() * 0.30f));
    return pad.removeFromTop(juce::jmin(s, pad.getHeight()))
        .removeFromRight(juce::jmin(s, pad.getWidth()));
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

        // The lock, on every filled card: an unlit chip while the chord is open to generation,
        // lit once it is protected from it. It used to be painted only when set, and it was an
        // indicator and nothing else - the toggle lived on the generator's own copy of these
        // pads, so a state you could read from the keyboard needed another view to change.
        // Both halves of that are fixed here: the corner is a target (`lockBadgeBounds`), and
        // it is painted at the size of that target rather than as a 5 px dot floating in it.
        //
        // Not on the card being edited: there the tick that ends the edit owns that end of the
        // card and mouseDown gives it the click, so a lock chip drawn over it would be a lie.
        if (filled && i != editingSlot)
            drawLockBadge(g, lockBadgeBounds(b), pad.locked, active ? inkOnAccent : theme::good);

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

// A pad's card menu. Eleven rows and three rules, which is the shape it has to hold: it is
// anchored to a pad near the bottom of the window and shown at the 34 px mouse-only item
// height, so every row costs 34 px of screen upwards from there. It ran to 23 rows plus four
// section headers on 2026-07-30 - about 820 px, taller than the space above the pad it hangs
// off - and JUCE answers that by splitting the menu into columns or turning it into a
// hover-scrolling one, and a scrolling popup is unusable with a single mouse: hovering the
// arrow scrolls, and moving to click scrolls the item away. Four groups, no headers, nothing
// three levels deep except the suggestion families:
//
//   Edit on keyboard / Clear pad / Lock
//   Octave down / Octave up / Next voicing
//   New chord / Next: could follow > / Send to arp slot >
//   Clear page / Generator settings >
//
// The separators do the work the section headers used to, at half the height and without
// naming what is already obvious from the items under them. The last header to go was "This
// pad", and it went for the same reason: a JUCE section header is an item and a half tall
// (51 px here), which is one and a half rows spent saying what the rule below it already says.
void ChordPads::showPadMenu(int slot)
{
    const auto& pad = processor.chordPad(slot);
    const bool editing = slot == editingSlot;
    const bool filled = ! pad.notes.empty();

    juce::PopupMenu menu;
    menu.addItem(1, editing ? "Done editing" : "Edit on keyboard");
    menu.addItem(2, "Clear pad", filled && ! pad.locked);
    // Lock is the full-size path; the chip in the card's top-right corner is the 24 px
    // accelerator. Both routes, because the chip is smaller than the mouse-only floor on a
    // docked card and a named item also says which way it is about to go.
    menu.addItem(3, pad.locked ? "Unlock" : "Lock", filled);

    // Octave and voicing: the two ways to move a chord without changing what it is. Both act
    // on the stored chord, and both are offered on a locked pad on purpose - a lock protects a
    // chord from *generation*, not from its owner asking for this card by name.
    //
    // All three grey out while this card is the one linked to the keyboard, because
    // rewritePadChord cannot reach the keybed and the edit link is the keybed's to write.
    // Left live they were worse than useless: the card moved, the keys stayed where they were,
    // and the next latched note wrote the *un*shifted set straight back over the shift through
    // the capture path, which also drops the generator metadata rewritePadChord exists to keep.
    // Pushing the new notes back the other way is not the cheaper fix it looks: recalling onto
    // the keybed means panic() first (recallOutputNotes only ever adds), and panic() takes
    // every other sounding chord in the plugin with it. Done editing is the row above.
    const auto down = shiftedOctave(pad.notes, -12);
    const auto up = shiftedOctave(pad.notes, 12);
    const int rootPc = padRootPc(slot);
    const auto base = chordgen::rootPosition(pad.notes, rootPc);
    const int voicing = chordgen::voicingOf(pad.notes, rootPc);
    const auto revoiced = chordgen::applyVoicing(base, voicing + 1);

    juce::String voicingItem = "Next voicing";
    if (filled && voicing >= 0)
        voicingItem << "   (now: " << chordgen::voicingName(voicing, (int) base.size()) << ")";

    menu.addSeparator();
    menu.addItem(4, "Octave down", ! editing && ! down.empty());
    menu.addItem(5, "Octave up", ! editing && ! up.empty());
    // isChord on the *result* as well as the source: a card holding nothing but a doubled pitch
    // class collapses to one note in root position, and a one-note pad is not a chord card.
    menu.addItem(6, voicingItem,
                 ! editing && isChord(pad.notes) && isChord(revoiced) && onKeyboard(revoiced)
                     && sortedCopy(revoiced) != sortedCopy(pad.notes));

    // Whatever else can act on this pad right now: the generator adds New chord and the
    // suggestion families. It has no panel and no view of its own since 2026-07-30, so a card
    // menu is where all of it is, on every pad and every page.
    menu.addSeparator();
    if (onExtraMenuItems)
        onExtraMenuItems(slot, menu);

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
        slots.addItem(100 + s, label, filled);
    }
    menu.addSubMenu("Send to arp slot", slots, filled);

    // And the page group at the foot: Clear page and everything the generator can be set to.
    if (onExtraPageItems)
    {
        menu.addSeparator();
        onExtraPageItems(menu);
    }

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
        else if (choice == 4)
        {
            safe->shiftPadOctave(slot, -12);
        }
        else if (choice == 5)
        {
            safe->shiftPadOctave(slot, 12);
        }
        else if (choice == 6)
        {
            safe->nextPadVoicing(slot);
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

int ChordPads::padRootPc(int slot) const
{
    const auto& pad = processor.chordPad(slot);
    // A generated card knows its own root; one built on the keyboard gets worked out, exactly
    // the way the suggestion menu works one out for the same reason.
    if (pad.rootPc >= 0)
        return pad.rootPc;
    return pad.notes.empty() ? 0 : suggest::analyse(pad.notes).first;
}

// New notes on a pad that already has a chord, keeping everything else about the card: the
// lock, and the generator metadata that lets New chord know which degree this was.
//
// The chord may be sounding (Sustain left it ringing) and may be the one held into the
// arpeggiator, and both have to follow the card to the new notes. Neither is released by
// hand: `pressChordPad` is the stop-then-press path, `holdArpChordFromPad` goes through
// `holdArpChord` which releases the previous hold first, so every note-on gives its reference
// back before the new one takes it. A raw noteOff here would take a reference belonging to
// somebody else, leak this pad's, and leave the chord droning.
void ChordPads::rewritePadChord(int slot, const std::vector<int>& notes)
{
    auto pad = processor.chordPad(slot);
    if (pad.notes.empty() || notes.empty())
        return;

    const bool wasSounding = processor.chordPadActive(slot);
    const bool wasHeld = processor.arpHeldPad() == slot;

    pad.notes = notes;
    pad.name = chords::detect(notes);
    processor.setChordPad(slot, pad);

    // Each state restored exactly once, and with Exclusive on only one of them can be.
    //
    // Exclusive makes both restore paths call stopAllChordPads(), and that stops every pad
    // *and* releases the arp hold. Doing both therefore fired the chord, killed it, and fired
    // it again - two Humanize/strum rolls audible in a row - and left the card holding the arp
    // but not sounding, which is neither of the states it started in. Exclusive means one chord
    // source at a time, so when it is on there is one state to put back, and it is the hold:
    // the arp goes on playing off a held chord until something replaces it, where a pad that is
    // still ringing is only what Sustain left behind. Hold first either way, since with
    // Exclusive off pressChordPad only re-triggers this one pad and leaves the hold alone.
    const bool exclusive = processor.apvts.getRawParameterValue("chordExclusive")->load() > 0.5f;
    if (wasHeld)
        processor.holdArpChordFromPad(slot);
    if (wasSounding && ! (exclusive && wasHeld))
        processor.pressChordPad(slot);
    repaint();
}

void ChordPads::shiftPadOctave(int slot, int semitones)
{
    const auto moved = shiftedOctave(processor.chordPad(slot).notes, semitones);
    if (! moved.empty()) // empty means it would have run off the keyboard; the item is greyed
        rewritePadChord(slot, moved);
}

void ChordPads::nextPadVoicing(int slot)
{
    const auto notes = processor.chordPad(slot).notes; // a copy: rewritePadChord writes the slot
    const int rootPc = padRootPc(slot);
    const auto base = chordgen::rootPosition(notes, rootPc);
    const auto next = chordgen::applyVoicing(base, chordgen::voicingOf(notes, rootPc) + 1);
    if (isChord(next) && onKeyboard(next)) // matches the menu item's own enable test
        rewritePadChord(slot, next);
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

    // The lock chip, on a filled card: a click there sets the lock and does nothing else.
    // Checked ahead of every branch below, so it never fires the chord, never hands it to the
    // arp and never arms a drag - nothing is set up for a press or a drag, so the mouseDrag
    // and mouseUp that follow have nothing to act on. It works in both card arrangements
    // because it is derived from padBounds, which already knows which one is up, and it is
    // exactly the rectangle drawLockBadge paints: this branch is the reason the chip has to be
    // painted at the size of the target rather than as a dot inside it.
    //
    // Not offered on the card being edited: the tick that ends the edit is a full-height strip
    // at that same right-hand end, is tested above, and wins. Lock on the card menu still
    // reaches it there, and the chip is not drawn on that card.
    const int lockCell = cellAt(e.position);
    if (lockCell >= 0 && lockCell != editingSlot && ! processor.chordPad(lockCell).notes.empty()
        && lockBadgeBounds(padBounds(lockCell - processor.padPageOffset())).contains(e.position))
    {
        dragging = false;
        dragSource = -1;
        playing = -1;
        processor.setChordPadLocked(lockCell, ! processor.chordPad(lockCell).locked);
        repaint();
        return;
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
