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
    constexpr float kNoteLineH = 11.0f; // the note list under the chord name, on every card
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

    // What a card is made of, under what it is called. This came from the chord generator's
    // own copy of this same page of pads; that grid went on 2026-07-30 and this stayed, at
    // first only on the tall (Big) arrangement, and now on every card - a line of it costs
    // 11 px, which the short card has, so Big had nothing left to offer and went too.
    juce::String noteListText(const std::vector<int>& notes)
    {
        const auto names = okstudio::scales::noteNames();
        juce::String out;
        for (const int n : notes)
            out << (out.isEmpty() ? "" : "  ") << names[((n % 12) + 12) % 12] << juce::String(n / 12 - 1);
        return out;
    }

} // namespace

ChordPads::ChordPads(KeysProcessor& p) : processor(p)
{
    okstudio::ui::makeMouseOnly(*this);
}

// See the header. ~Timer stops the timer, but stopping the timer is not releasing the chord -
// the note-ons are the processor's and would simply stay on, reachable only by All Off.
ChordPads::~ChordPads()
{
    endAudition();
}

juce::Rectangle<float> ChordPads::cardBounds() const
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    return r.removeFromLeft(kCardW);
}

juce::Rectangle<float> ChordPads::padBounds(int visibleIndex) const
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    constexpr int rows = 2;
    constexpr int cols = KeysProcessor::padsPerPage / rows; // sixteen as two rows of eight
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

// ---------------------------------------------------------------------------------------
// Taking a drop. Four gestures arrive here and JUCE tells this component about all of them,
// including the one that starts in another window - see ChordDrag.h for why that is not the
// impossibility the code here used to assert it was.
// ---------------------------------------------------------------------------------------

bool ChordPads::isInterestedInDragSource(const SourceDetails& details)
{
    return chorddrag::chordBeingDragged(details) != nullptr;
}

// Which cell a drop would land on, refusals applied. They are not the same for every source,
// which is why this is one function with the provenance in hand rather than a rule per caller.
int ChordPads::dropCellFor(const chorddrag::Payload& p, juce::Point<int> local) const
{
    using From = chorddrag::Payload::From;
    const int cell = cellAt(local.toFloat());

    if (cell == -2)
    {
        // The live card takes a pad being dragged back for editing (onRecall) and nothing else.
        // It is what is under your hand on the keyboard, not a place to put things, so a
        // candidate from the tray has nothing to mean here.
        return p.from == From::padSlot ? -2 : -1;
    }
    if (cell < 0)
        return -1;
    if (p.from == From::padSlot && cell == p.index)
        return -1; // dropped back where it was picked up: the gesture cancelled

    // A locked pad refuses a chord arriving from *outside* this strip, because that drop replaces
    // what the pad holds outright and the lock is the thing that stops a chord being destroyed
    // (Owen, 2026-07-30). A move inside the strip is allowed onto a lock, deliberately:
    // moveChordPad only swaps two slots and destroys nothing, so rearranging a page is not what
    // a lock is protecting against. Capturing the live card onto a locked pad has always been
    // allowed too, and stays that way here rather than being quietly tightened in a refactor.
    if (p.from == From::trayCell && processor.chordPad(cell).locked)
        return -1;
    return cell;
}

void ChordPads::itemDragEnter(const SourceDetails& details) { itemDragMove(details); }

void ChordPads::itemDragMove(const SourceDetails& details)
{
    auto* p = chorddrag::chordBeingDragged(details);
    const int cell = p != nullptr ? dropCellFor(*p, details.localPosition) : -1;
    if (cell == dropCell)
        return;
    dropCell = cell;
    repaint();
}

void ChordPads::itemDragExit(const SourceDetails&)
{
    if (dropCell == -1)
        return;
    dropCell = -1;
    repaint();
}

void ChordPads::itemDropped(const SourceDetails& details)
{
    using From = chorddrag::Payload::From;
    dropCell = -1;
    auto* p = chorddrag::chordBeingDragged(details);
    if (p == nullptr)
        return;

    // The release landed on this strip, so this drag did not go off the row - and that is true
    // whether or not the strip goes on to do anything with it. A lock refusing, a candidate over
    // the live card, a card dropped back where it started: all of those are gestures that end
    // here, and none of them may read as the drag-off-to-clear. Set before the branches below
    // for exactly that reason.
    if (cellAt(details.localPosition.toFloat()) != -1)
        p->taken = true;

    const int cell = dropCellFor(*p, details.localPosition);
    if (cell == -1)
    {
        repaint();
        return;
    }

    if (cell == -2)
    {
        // A filled pad dropped onto the live card: recall its chord for editing. Not a move and
        // not a clear - the pad stays exactly where it was.
        if (onRecall)
            onRecall(p->chord.notes);
    }
    else if (p->from == From::padSlot)
    {
        processor.moveChordPad(p->index, cell); // rearrange, locked or not
    }
    else if (p->from == From::liveCard)
    {
        processor.setChordPad(cell, p->chord.notes, p->chord.name); // capture the live chord
    }
    else
    {
        // clearChordPad first, and it is not tidiness. A dropped candidate is a *different*
        // chord from whatever the slot held, unlike the octave and voicing edits that go through
        // rewritePadChord and want the sounding notes to follow the card. setChordPad on its own
        // does not stop anything, so a pad left ringing by Sustain - or one feeding the arp -
        // would have had its old notes stranded on with nothing left owning them. clearChordPad
        // is the one public call that stops the pad *and* releases the arp hold if this card is
        // the one holding it, so the old chord is properly given up before the new one lands.
        processor.clearChordPad(cell);
        processor.setChordPad(cell, p->chord);
        // Committed, so the tray's cell goes empty. `taken` alone would not say this: the
        // reference box sets that and keeps the candidate.
        p->consumed = true;
    }
    repaint();
}

int ChordPads::firstEmptyPadOnPage() const
{
    const int offset = processor.padPageOffset();
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
        if (processor.chordPad(offset + v).notes.empty())
            return offset + v;
    return -1;
}

bool ChordPads::sendChordToFirstEmptyPad(const KeysProcessor::ChordPad& pad)
{
    const int slot = firstEmptyPadOnPage();
    if (slot < 0)
        return false;
    // No clearChordPad first, unlike the drop: the slot is empty by definition, so there is no
    // chord to give up and nothing sounding to release.
    processor.setChordPad(slot, pad);
    repaint();
    return true;
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

    // Live chord card: an inset well that lights up while a chord is sounding.
    // While a filled pad is being dragged over it, it highlights to offer the
    // recall gesture (drop to pull that pad's chord back for editing).
    {
        const auto b = cardBounds();
        const bool has = isChord(currentNotes);
        const bool recallHover = dropCell == -2;

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
        // Named, and under it the notes themselves, the same way a pad card reads. The live
        // card is what is under your hand rather than what is stored, which is the one place
        // "what notes are these" is a question you ask mid-chord.
        auto cardText = b.reduced(6.0f);
        const auto liveNotes = has ? cardText.removeFromBottom(kNoteLineH)
                                   : juce::Rectangle<float>();
        g.setColour(has ? skin::text : skin::textFaint);
        g.setFont(has ? skin::uiSemi(15.0f) : skin::ui(11.0f));
        g.drawText(has ? currentName : juce::String("hold a chord"), cardText,
                   juce::Justification::centred, true);
        if (has)
        {
            g.setColour(skin::textDim);
            g.setFont(skin::micro(9.0f));
            g.drawText(noteListText(currentNotes), liveNotes.toNearestInt(),
                       juce::Justification::centred, true);
        }
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
        // Any drag can be offering this pad: one inside the strip, or a candidate dragged in
        // from the generator's tray in another window. They light the same, because to the pad
        // they mean the same thing - let go here and this card takes that chord. One field for
        // all of them now, and it already has the refusals applied, so a locked pad no longer
        // lights for a drop it is going to turn away.
        const bool dropHere = dropCell == i;

        // The card in the air fades where it sits. The ghost is a desktop window of its own and
        // follows the cursor, so this is the hole it left rather than a stand-in for it.
        const bool airborne = dragging && dragSource == i;

        // The pad being edited gives up its right end to the tick that ends the edit, so
        // the chord name moves over rather than running underneath it.
        auto nameArea = b;
        if (i == editingSlot)
            nameArea.removeFromRight(kSaveW);

        // A transparency layer rather than g.setOpacity: setColour resets the fill alpha, so
        // dimming that way reaches the card's fill and leaves its lettering at full strength.
        if (airborne)
            g.beginTransparencyLayer(0.4f);

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

            // The card says what the chord *is* as well as what it is called: the name, and
            // under it the notes a press of this pad will play, with octave numbers. Both,
            // always - the note list used to be the tall arrangement's alone, and a card is
            // no use as a reference if reading it means a mode switch first.
            const auto ink = active ? inkOnAccent : skin::text;
            auto text = nameArea.reduced(4.0f, 3.0f);
            const auto noteLine = text.removeFromBottom(kNoteLineH);

            g.setColour(ink);
            g.setFont(skin::uiSemi(13.5f));
            g.drawText(pad.name, text, juce::Justification::centred, true);

            g.setColour(active ? inkOnAccent.withAlpha(0.75f) : skin::textDim);
            g.setFont(skin::micro(9.0f));
            g.drawText(noteListText(pad.notes), noteLine.toNearestInt(),
                       juce::Justification::centred, true);
        }

        // The pad currently feeding the arp. A ring rather than the "active" fill, because
        // it is a different state: the chord is held for the arp to chew on, not simply
        // sounding, and both can be true at once.
        // Not gated on `filled`: a card cleared while it was feeding the arp still owns the
        // sounding chord, and an invisible ring is a chord you cannot find or stop.
        if (const int heldBy = processor.arpLineHoldingPad(i); heldBy >= 0)
        {
            g.setColour(skin::accentOf(*this).hot);
            g.drawRoundedRectangle(b.reduced(1.0f), kRadius, 2.0f);
            skin::glowRect(g, b, kRadius, skin::accentOf(*this).hot, 0.8f);
            // Which of the three lines has it. A letter in the ring rather than a colour per
            // line: the skin has one accent by design (see the theme swatch), so three rings
            // in three colours would be three colours this plugin does not own. Bottom-right,
            // opposite the lock dot, so a locked card feeding line B says both.
            g.setFont(skin::micro(9.0f));
            g.drawText(juce::String::charToString((juce::juce_wchar) ('A' + heldBy)),
                       b.reduced(5.0f).toNearestInt(), juce::Justification::bottomRight, false);
        }

        // Locked pads carry a corner dot, and only locked ones. It is an indicator and nothing
        // else: the toggle is Lock on this pad's own card menu, where it can be a full-size
        // item (Owen, 2026-07-30 - "I don't want the lock button to be visible. I only want it
        // to be in right click"). A clickable chip lived here for a few hours earlier the same
        // day; it took a quarter of the card away from playing, dragging and feeding the arp,
        // which is the whole point of the card. A dot costs the surface nothing.
        //
        // Not on the card being edited: the tick that ends the edit is a full-height strip at
        // that same right-hand end, and the dot would sit inside it.
        if (filled && pad.locked && i != editingSlot)
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

        if (airborne)
            g.endTransparencyLayer();
    }

    // There is no ghost drawn here any more (2026-08-02). It used to be an 84x26 chip painted at
    // the cursor because the drag could not leave this component; the real card now travels as a
    // desktop window of its own and follows the mouse across the whole screen, which is what
    // makes dropping onto the generator's reference box in another window a thing you can aim.
    // The dimmed hole where the card sits is the half of that feedback this component still owns.
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

// A pad's card menu. Nine rows and two separators, which at the 34 px mouse-only item height
// (a separator is half that, KeysLookAndFeel::getIdealPopupMenuItemSize) is 9 * 34 + 2 * 17 =
// 340 px. That budget is the reason this list is short: the menu hangs off a pad near the
// bottom of a 699 px window and grows *upwards*, and JUCE answers one taller than the space it
// has by splitting it into columns or making it hover-scroll. A scrolling popup cannot be used
// with one mouse at all - hovering the arrow scrolls, and moving to click scrolls the item
// away. It ran to 23 rows and roughly 820 px earlier on 2026-07-30. Three groups, no headers,
// nothing three levels deep except the suggestion families:
//
//   Edit on keyboard / Clear pad / Lock
//   Octave down / Octave up / Next voicing
//   New chord / Next: could follow > / Send to arp slot >
//
// The fourth group went with the generator's settings, into the window that now holds them
// (ChordGenPanel): its Fill, Regen and Clear act on the audition tray, not on this page, where
// actions are. The separators do the work section headers used to, at half the height and
// without naming what is already obvious from the items under them.
void ChordPads::showPadMenu(int slot)
{
    const auto& pad = processor.chordPad(slot);
    const bool editing = slot == editingSlot;
    const bool filled = ! pad.notes.empty();

    juce::PopupMenu menu;
    menu.addItem(1, editing ? "Done editing" : "Edit on keyboard");
    menu.addItem(2, "Clear pad", filled && ! pad.locked);
    // The only way to set a lock (Owen, 2026-07-30). It is the one item in the plugin whose
    // left-click twin was deliberately taken away rather than never built: a chip in the card's
    // corner did the job for a few hours and cost the card a quarter of its surface. The card
    // still *shows* a lock as a corner dot, so the state is readable without opening this menu.
    // Recorded as an owner-directed right-click-only path in CLAUDE.md.
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
    // suggestion families. Both are per-card actions, so they belong on a card's own menu
    // however the generator's settings are reached; they are offered on every pad and every
    // page whether or not the generator's window is open, which is what ChordGenMenu outliving
    // that window is for.
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
    const int heldByLine = processor.arpLineHoldingPad(slot);
    const bool wasHeld = heldByLine >= 0;

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
        processor.holdArpChordFromPad(slot, heldByLine);
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

    // Any left click on this strip ends an audition still ringing from the last one, before it
    // is even known what this click means. First, so that every early return below inherits it:
    // the edit tick used to clear `playing` by hand, which now would strand the note it stands
    // for rather than release it.
    endAudition();

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
            if (onEditToggle)
                onEditToggle(editingSlot); // toggling the slot that is already editing ends it
            repaint();
            return;
        }
    }

    // Nothing else on a card is a target. The lock had a clickable chip in the top-right
    // corner for a few hours on 2026-07-30 and Owen asked for it back off: it stole a quarter
    // of the card from playing, dragging and feeding the arp, and every one of those is a
    // gesture the whole surface is supposed to answer. Lock is a right-click item now, and only
    // that; the card paints a dot when it is set, which is a mark and not a target.
    // Press does nothing but remember where it landed. Every meaning this gesture can have -
    // play the chord, hand it to an arp line, drag it onto a pad, a slot or a line's row - is
    // decided in mouseUp, because the two families are told apart by whether the mouse moved,
    // and that is not knowable yet (2026-08-02; see the note on the class).
    //
    downPos = e.position;
    dragging = false;
    dragSource = cellAt(e.position);
    repaint();
}

// Stop an audition still sounding from an earlier click, if there is one. Safe to call at any
// time and on any path - it is how a click, a drag and the timer all end the same state.
void ChordPads::endAudition()
{
    stopTimer();
    if (playingLive)
    {
        processor.releaseLiveChord(); // Sustain holds it, exactly as the old mouse-up did
        playingLive = false;
    }
    if (playing >= 0)
    {
        processor.releaseChordPad(playing);
        playing = -1;
    }
}

void ChordPads::timerCallback()
{
    endAudition(); // which stops this timer
    repaint();
}

void ChordPads::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging || e.position.getDistanceFrom(downPos) <= 6.0f)
        return;

    // Nothing to silence here: the press never sounded. What a drag has to decide is only
    // whether there was something under it worth carrying, and a *filled* pad is the test -
    // dragging an empty cell has never meant anything. The arp branch used to clear dragSource
    // before this ran, which is what made a card undraggable with a line on.
    if (! sourceIsDraggable())
    {
        dragSource = -1; // nothing grabbable under the press
        return;
    }

    beginChordDrag(e);
}

// Everything after this call belongs to JUCE: the ghost that follows the cursor out of this
// window, the target lighting up, and the drop itself. What stays here is which card was picked
// up and what happens if nobody wants it.
void ChordPads::beginChordDrag(const juce::MouseEvent& e)
{
    using From = chorddrag::Payload::From;
    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (container == nullptr)
    {
        jassertfalse; // the section holder is meant to be one, docked or in a window of its own
        dragSource = -1;
        return;
    }

    const bool live = dragSource == -2;
    auto chord = live ? KeysProcessor::ChordPad {} : processor.chordPad(dragSource);
    if (live)
    {
        chord.notes = currentNotes;
        chord.name = currentName;
    }

    // Snapped before `dragging` goes true, so the ghost is a picture of the card as it reads at
    // rest rather than of the dimmed hole it is about to leave behind.
    const auto b = (live ? cardBounds() : padBounds(dragSource - processor.padPageOffset()))
                       .toNearestInt();
    const auto ghost = createComponentSnapshot(b, true, 2.0f).convertedToFormat(juce::Image::ARGB);
    const auto grab = b.getTopLeft() - downPos.roundToInt(); // JUCE negates this into the image

    inFlight = new chorddrag::Payload(live ? From::liveCard : From::padSlot,
                                      live ? -1 : dragSource, std::move(chord));
    dragging = true;
    repaint();

    container->startDragging(juce::var(inFlight.get()), this, juce::ScaledImage(ghost, 2.0),
                             /*allowDraggingToExternalWindows*/ true, &grab, &e.source);
}

void ChordPads::mouseUp(const juce::MouseEvent&)
{
    if (dragging)
    {
        // Where this drag landed is not known yet. Every drop - onto a pad of this strip, onto
        // the live card, onto the generator's reference box, onto an arp slot or line - is
        // delivered by JUCE *after* this method, later in the same event, so the one question
        // left here has to be asked a message-loop turn from now. See chorddrag::whenDragSettles
        // for why that is the right length of wait and dragOperationEnded is not.
        chorddrag::whenDragSettles(
            *this, inFlight,
            [](ChordPads& pads, const chorddrag::Payload& p)
            {
                // Nobody took it, so the card was let go off the row, and off the row means
                // clear. The veto is what keeps this from being the way to *lose* a chord:
                // reaching for the reference box means dragging a card off the strip, and that
                // box - like every arp target - says so by setting `taken`.
                //
                // A locked card dropped off the strip does nothing at all. The lock is the thing
                // that stops a chord being destroyed (Owen, 2026-07-30), and "Clear pad" on the
                // card menu has always greyed for a locked pad - this path was the hole in that,
                // and a wider gesture than the menu item it was quietly overriding. The drag
                // itself stays allowed, because moveChordPad only swaps two slots and a locked
                // card still has to be arrangeable.
                if (! p.taken && p.from == chorddrag::Payload::From::padSlot
                    && ! pads.processor.chordPad(p.index).locked)
                    pads.processor.clearChordPad(p.index);

                pads.inFlight = nullptr;
                pads.repaint();
            });
    }
    else
    {
        // A click: the mouse went down on a card and came back up without travelling. This is
        // where everything the press used to do now happens, in the same order it used to be
        // tested in, so only the *timing* changed and not which branch a given card takes.

        // Arp On: a filled pad hands its chord to the arpeggiator and it stays there. Not an
        // audition, so `playing` stays -1 and no timer is started - held means held. A second
        // click on the pad already feeding the arp re-plays it. Checked before the audition
        // branch because in this mode the click means something else entirely.
        //
        // The `arpLineHoldingPad()` half of the test is not redundant with the notes test: a
        // card can be cleared while it is still the one feeding the arp, and then it wears the
        // ring with no notes behind it. Without that clause the click falls past every branch
        // below and does nothing at all, which is a dead click on a lit target.
        if (toArp() && dragSource >= 0
            && (! processor.chordPad(dragSource).notes.empty()
                || processor.arpLineHoldingPad(dragSource) >= 0))
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
                processor.holdArpChordFromPad(dragSource, processor.arpCurrentLine());
            }
        }
        // The live "current chord" card is a chord card too, and the mode's tooltip says so.
        else if (toArp() && dragSource == -2 && isChord(currentNotes))
        {
            processor.holdArpChord(currentNotes, currentName);
        }
        // No line listening: the click auditions the chord. It sounds now and the timer lets it
        // go, because the button is already up and nothing else is coming to end it.
        else if (dragSource >= 0 && ! processor.chordPad(dragSource).notes.empty())
        {
            processor.pressChordPad(dragSource);
            playing = dragSource;
            startTimer(auditionMs);
        }
        else if (dragSource == -2 && isChord(currentNotes))
        {
            // The live card plays too. Holding a chord on the keyboard sounds the keys you are
            // holding; clicking the card fires the same notes as one chord, so you hear it
            // strummed and humanized the way a pad would play it.
            processor.pressLiveChord(currentNotes);
            playingLive = true;
            startTimer(auditionMs);
        }
    }
    // Putting the outside taker's highlight back out is nobody's job here any more. Every target
    // gets `itemDragExit` from JUCE on every path a drag can end - dropped elsewhere, dragged
    // away, or the window it lives in closed mid-gesture - which is the whole of a bug class the
    // editor used to carry by hand and get wrong: dragging out over the reference box and then
    // back onto a pad left the box lit with nothing in the air.
    dragging = false;
    dragSource = -1;
    repaint();
}
} // namespace keys
