#include "ChordPads.h"
#include "../ChordLibrary.h"
#include "../ChordGen.h"
#include "../ChordNumerals.h"
#include "../ChordSuggest.h"
#include "../Chords.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>
#include <okstudio/Scales.h>
#include <algorithm>
#include <cmath>

namespace keys
{
namespace
{
    constexpr float kCardW = 108.0f;
    constexpr float kGap = 6.0f;
    constexpr float kRadius = 6.0f;
    constexpr float kSaveW = 38.0f; // the edit tick's strip at the right end of a pad
    constexpr float kNoteLineH = 11.0f; // the note list under the chord name, on every card
    // **A single note is a card** (2026-08-21, Owen: "I also like to allow one note to show up
    // in the chord pad and the chord preview"). This was `n.size() >= 2` and was the only thing
    // standing between a held note and a pad: with one key down the live card read "hold a
    // chord", could not be pressed and - because an empty card is not draggable - could not be
    // carried onto a pad at all. Nothing downstream ever needed two. `chords::detect` already
    // names a lone pitch class by its note name, `applyInversion` and `applySpread` return a
    // one-note chord unchanged, and the arp builds a one-entry sequence from it, so this is a
    // gate that was refusing something the rest of Keys could already do.
    //
    // The name is what changed with it: this asks whether there is anything here to show, play
    // or drag, which is a different question from whether the notes form a chord. Bass lines,
    // pedal tones and single-note stabs are all things you want on a pad.
    bool hasNotes(const std::vector<int>& n) { return ! n.empty(); }

    // The half of the old `isChord` that was actually asking about chords. The rule itself is
    // `chordgen::canRevoice`, beside the walk it gates, because the tray asks it too.
    using chordgen::canRevoice;

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

    // "Save chord as MIDI": one bar of every note in the chord, on together at tick zero and
    // off together a bar later, at whatever tempo is playing right now.
    //
    // Not a call into KeysProcessor::buildTakeMidiFile, and that was checked rather than
    // assumed - it is the only MidiFile writer in this codebase, but its shape is a *recorded
    // performance*: it walks `capturedTake`, a message-thread log of arbitrary events over real
    // time, trims to the first captured note-on, and freezes at the tempo recording was armed
    // at. There is no note list in it to hand a chord through, and reusing it would mean
    // stuffing synthetic events into a live take buffer from a menu click - real recording
    // state a card menu has no business touching. A chord is the other shape entirely: every
    // note starts together and the file is exactly one bar long, so this is its own small
    // function rather than a second copy of the take writer's plumbing.
    bool oneBarChordMidiFile(const std::vector<int>& notes, float velocity01, double bpm,
                             juce::MidiFile& out)
    {
        if (notes.empty() || bpm <= 0.0)
            return false;

        constexpr short ticksPerQuarter = 960; // ample resolution; nothing here is quantized finer
        const double barTicks = (double) ticksPerQuarter * 4.0; // one bar of 4/4

        juce::MidiMessageSequence seq;
        seq.addEvent(juce::MidiMessage::tempoMetaEvent((int) std::llround(60000000.0 / bpm)), 0.0);
        seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
        const float vel = juce::jlimit(1.0f / 127.0f, 1.0f, velocity01); // the floor noteOn() uses
        for (const int n : notes)
            seq.addEvent(juce::MidiMessage::noteOn(1, n, vel), 0.0);
        for (const int n : notes)
            seq.addEvent(juce::MidiMessage::noteOff(1, n), barTicks);
        seq.updateMatchedPairs();

        out.setTicksPerQuarterNote(ticksPerQuarter);
        out.addTrack(seq);
        return true;
    }

} // namespace

ChordPads::ChordPads(KeysProcessor& p) : processor(p)
{
    okstudio::ui::makeMouseOnly(*this);
}

// See the header. A section can fold, or its window close, mid-press - and the note-ons are the
// processor's, so without this they would simply stay on, reachable only by All Off.
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
    constexpr int cols = KeysProcessor::padsPerPage / rows; // twelve as two rows of six
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
    if ((p.from == From::trayCell || p.from == From::refCard) && processor.chordPad(cell).locked)
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
        const KeysProcessor::UndoGesture undoable { processor, "Move chord",
                                                   KeysProcessor::UndoScope::pads };
        processor.moveChordPad(p->index, cell); // rearrange, locked or not
    }
    else if (p->from == From::liveCard)
    {
        const KeysProcessor::UndoGesture undoable { processor, "Capture chord",
                                                   KeysProcessor::UndoScope::pads };
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
        const KeysProcessor::UndoGesture undoable { processor, "Drop chord",
                                                   KeysProcessor::UndoScope::pads };
        processor.clearChordPad(cell);
        processor.setChordPad(cell, p->chord);
        // Committed, so the tray's cell goes empty. `taken` alone would not say this: the
        // reference box sets that and keeps the candidate.
        //
        // And the reference box is why this is a test rather than an unconditional write
        // (2026-08-17): a chord dragged *out* of the reference is the same drop as a tray
        // candidate in every respect but this one. The reference is the tray's fixed point, so
        // it keeps its chord however many pads it fills - `consumed` there would empty the box
        // the first time you used it, which is the opposite of what it is for.
        p->consumed = p->from == From::trayCell;
    }
    repaint();
}

int ChordPads::firstEmptyPadOnPage() const
{
    const int offset = processor.padPageOffset();
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
        if (! hasNotes(processor.chordPad(offset + v).notes))
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
        return hasNotes(currentNotes);
    if (dragSource >= 0)
        return hasNotes(processor.chordPad(dragSource).notes);
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
        const bool has = hasNotes(currentNotes);
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
        const bool filled = hasNotes(pad.notes);
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

            // The degree of the key this pad holds (2026-08-18, Owen's ask). Read against the
            // *parameter* rather than through ChordGenMenu's genRoot/genMode, which answer with
            // whatever an unticked Key or Mode rolled for the last generation: a pad outlives that
            // roll, and the key you are composing in is the one on the Pads bar. So a strip of
            // twelve reads I - V - vi - IV across the page, and a chord borrowed from outside the
            // key simply draws nothing, which is itself the useful answer.
            const int keyRoot = (int) processor.apvts.getRawParameterValue("genRoot")->load();
            const int keyMode = juce::jlimit(0, modes::count() - 1,
                                             (int) processor.apvts.getRawParameterValue("genMode")->load());
            // A pad from the library asks its row directly. `degree` is an index into the mode the
            // chord was *generated* in, and a library row is generated against its own - so an
            // Andalusian cadence in a C major session had its i, bVII, bVI, V read back as
            // I, vii, vi, V: four wrong numerals under a bracket correctly naming the progression
            // they came from, which is worse than showing none. A row and a step name the chord
            // exactly and need no mode at all.
            auto numeral = chordlib::numeralAt(pad.progression, pad.progressionStep);
            if (numeral.isEmpty())
                numeral = numerals::forChord(pad.numeral, pad.degree, pad.rootPc, keyRoot, keyMode);
            skin::numeralBadge(g, b, numeral, ink);

            // The run's name, on its first pad only and beside the numeral. It cannot go under the
            // bracket: the gap between the two rows is 6 px and this line needs eleven, and it
            // cannot go inside the card either, where the name and the note list already are. The
            // top strip is the one piece of a card with room, and the numeral leaves most of it.
            if (! pad.progression.isEmpty() && pad.progressionStep == 0)
            {
                auto strip = b.reduced(5.0f, 4.0f).removeFromTop(10.0f).withTrimmedLeft(16.0f);
                g.setColour((active ? inkOnAccent : skin::accentOf(*this).base).withAlpha(0.7f));
                g.setFont(skin::micro(8.5f));
                g.drawText(pad.progression, strip.toNearestInt(), juce::Justification::topLeft, true);
            }
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

    // Progression brackets (2026-08-18). A run of adjacent pads that came from the same library
    // row, in step order, gets a hairline under it with the row's name over the middle - so a
    // strip holding the Andalusian cadence says so, where before it was four unrelated minor
    // chords in a row.
    //
    // **Drawn after the pad loop, not inside it**, because a bracket belongs to a run rather than
    // to a card: it has to know where the run ends before it can draw, and a per-pad pass would
    // either draw twelve fragments or need the same scan done twelve times.
    //
    // **A run is broken by a row change, a step that does not follow, and a row break.** The last
    // is the one worth stating: pads wrap from the sixth to the seventh in a two-row strip, so
    // pads 5 and 6 are adjacent by index and nowhere near each other on screen, and a bracket
    // spanning them would be a line drawn across the middle of the strip to nothing.
    {
        int v = 0;
        while (v < KeysProcessor::padsPerPage)
        {
            const auto& first = processor.chordPad(offset + v);
            if (first.progression.isEmpty() || first.notes.empty())
            {
                ++v;
                continue;
            }

            const int rowOf = v / 6; // the strip is two rows of six
            int end = v;
            while (end + 1 < KeysProcessor::padsPerPage)
            {
                const auto& next = processor.chordPad(offset + end + 1);
                if (next.notes.empty() || next.progression != first.progression
                    || next.progressionStep != processor.chordPad(offset + end).progressionStep + 1
                    || (end + 1) / 6 != rowOf)
                    break;
                ++end;
            }

            // A run of one is not a progression on the strip; it is a chord that happens to
            // remember where it came from, and a bracket over it would be a label with nothing to
            // label. The pad still carries the field, which is what "could follow" will read.
            if (end > v)
            {
                const auto a = padBounds(v);
                const auto z = padBounds(end);
                // The bracket sits under the run's *own* row. This was
                // `jmin(padBounds(0).getBottom(), a.getBottom())`, which is row 0's bottom for
                // every run: a run on the second row therefore drew its bracket up in the gap
                // under the first row, over the columns of the pads it does not belong to, and
                // two runs one per row drew two identical brackets on the same line. The loop
                // above already refuses to let a run cross a row, so `a` and `z` share one and
                // either end answers for it.
                const float y = a.getBottom() + 3.0f;
                const auto accent = skin::accentOf(*this);

                juce::Path bracket;
                bracket.startNewSubPath(a.getX() + 2.0f, y + 4.0f);
                bracket.lineTo(a.getX() + 2.0f, y);
                bracket.lineTo(z.getRight() - 2.0f, y);
                bracket.lineTo(z.getRight() - 2.0f, y + 4.0f);
                g.setColour(accent.base.withAlpha(0.45f));
                g.strokePath(bracket, juce::PathStrokeType(1.0f));

                // No label here. It lived under the bracket for one build and the 6 px gap between
                // the pad rows cannot hold an 11 px line, so it painted over the row below. The
                // name is on the run's first pad instead, where the card's top strip has room.
            }
            v = end + 1;
        }
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

// A pad's card menu. **Seventeen rows and three separators since 2026-08-23**, when Clear page
// took a group of its own at the foot (it was sixteen rows and two separators from 2026-08-19,
// when uiArpLines went to four and the Send to arp loop started emitting A, B, C and D, and
// fourteen rows from 2026-08-17, when Copy chord, Paste chord and Save chord as MIDI joined the
// first group, Owen: "need to be able to copy paste chords"). At the 34 px mouse-only item
// height (a separator is half that, KeysLookAndFeel::getIdealPopupMenuItemSize) that is
// 17 * 34 + 3 * 17 = 629 px, up from 578 and from 510 before that. **The row count is a
// function of uiArpLines now, so raising that raises this**: a fifth line would make it 663.
// That budget is the
// reason the three new rows landed flat in the existing first group rather than behind a
// submenu: the menu hangs off a pad near the bottom of the window and grows *upwards*, and JUCE
// answers one taller than the space it has by splitting it into columns or making it
// hover-scroll, neither of which can be worked with one mouse.
//
// Checked against the panel's own arithmetic rather than assumed, twice: once before the three
// 2026-08-17 rows, and again for Clear page. The arp panel's height varies by view and the Pads
// section sits under it, so what moves is this menu's *anchor*, never the menu - and a taller
// arp pushes the anchor **down**, which only ever helps. The figures this note used to carry
// (240 px in the macro view, an anchor around y=656) are two rounds of arp work out of date and
// were the *worst* case even then: since 2026-08-19 the macro view is a 2x2 grid, so
// ArpPanel's own arpMacroTotalH is **401 px with the bottom row folded** - the default - and
// **690 px unfolded**, 160 to 450 px lower than the anchor this note was budgeted against. The
// deep-view pages sit between the two. So the 629 px this menu now wants has more room above it
// than the 510 px version did, not less, and the direction of travel is safe. It ran to 23 rows
// and roughly 820 px on 2026-07-30, which is what a menu genuinely too tall for this budget
// looks like. Four groups, no headers, nothing three levels deep except the suggestion families:
//
//   Edit on keyboard / Clear pad / Lock / Copy chord / Paste chord / Save chord as MIDI
//   Octave down / Octave up / Next voicing
//   New chord / Next: could follow > / Send to arp A / Send to arp B / Send to arp slot >
//   Clear page
//
// The fourth group is one row and is about the **page**, not this card (2026-08-23, Owen: "we
// need to be able to clear all the chords on a pad page"). It is the only thing on this menu
// that acts on anything but the pad it was opened from, which is why it sits alone at the
// bottom behind a separator rather than beside Clear pad, whose name it otherwise reads as a
// plural of. Owen chose this over a chip on the Pads bar when asked, and the reason the bar was
// the worse home is on the record from the day the old Clear chip left it: a page wipe 4 px from
// Regen, and a few px from the page buttons, is a destructive action sitting on top of the two
// things you click constantly. Down here it costs a right-click and a travel, which is the
// right price for it. A page-wide wipe used to be the generator brain's `clearPage`, deleted on
// 2026-08-01 for want of a home; what makes this affordable now and did not then is undo, which
// arrived on 2026-08-14 and covers the pad tree. It is one entry (KeysProcessor::
// clearChordPadPage), so Undo on the Controls bar puts all twelve cards back at once.
//
// The generator's own window has a Fill, a Regen and a Clear too, and they act on the audition
// tray rather than on the page - nothing in that window writes a pad. The separators do the work
// section headers used to, at half the height and without naming what is already obvious from
// the items under them.
void ChordPads::showPadMenu(int slot)
{
    const auto& pad = processor.chordPad(slot);
    const bool editing = slot == editingSlot;
    const bool filled = hasNotes(pad.notes);

    juce::PopupMenu menu;
    menu.addItem(1, editing ? "Done editing" : "Edit on keyboard");
    menu.addItem(2, "Clear pad", filled && ! pad.locked);
    // The only way to set a lock (Owen, 2026-07-30). It is the one item in the plugin whose
    // left-click twin was deliberately taken away rather than never built: a chip in the card's
    // corner did the job for a few hours and cost the card a quarter of its surface. The card
    // still *shows* a lock as a corner dot, so the state is readable without opening this menu.
    // Recorded as an owner-directed right-click-only path in CLAUDE.md.
    menu.addItem(3, pad.locked ? "Unlock" : "Lock", filled);

    // Copy chord / Paste chord (2026-08-17, Owen: "need to be able to copy paste chords").
    // Copy reads whatever this card holds whether or not it is locked - a lock protects the
    // *slot* from being overwritten, not the chord underneath from being read - and stripping
    // `locked` happens at copy time (see the field's own comment in ChordPads.h), so Paste never
    // has to ask. Paste goes through the same choke a drop does: clearChordPad first, so a pad
    // left ringing by Sustain or feeding an arp line gives its old notes up before the pasted
    // chord lands rather than stranding them, all inside one undo entry. It greys on an empty
    // clipboard and on a locked target, the same refusal Clear pad makes just above.
    menu.addItem(7, "Copy chord", filled);
    menu.addItem(8, "Paste chord", clipboard.has_value() && ! pad.locked);

    // Save chord as MIDI (2026-08-17). Ableton's own clipboard is internal to Live and will not
    // take a paste from the Windows clipboard, so the only way a chord built here reaches a Live
    // clip is a .mid file dropped onto a track - the same route Keys already offers for a
    // recorded take (TakePanel::dragTakeOut). A menu row cannot start a drag, so this writes one
    // bar of the chord into KeysProcessor::takeFolder() and reveals it in Explorer instead,
    // ready for that same short drag.
    menu.addItem(9, "Save chord as MIDI", filled);

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
    // canRevoice on the *result* as well as the source: a card holding nothing but a doubled
    // pitch class collapses to one note in root position, and one note has no voicings to walk.
    // A one-note pad is perfectly legal since 2026-08-21 - it just has nothing for this row to
    // do, which is why the row greys rather than the pad being refused.
    menu.addItem(6, voicingItem,
                 ! editing && canRevoice(pad.notes) && canRevoice(revoiced) && onKeyboard(revoiced)
                     && sortedCopy(revoiced) != sortedCopy(pad.notes));

    // Whatever else can act on this pad right now: the generator adds New chord and the
    // suggestion families. Both are per-card actions, so they belong on a card's own menu
    // however the generator's settings are reached; they are offered on every pad and every
    // page whether or not the generator's window is open, which is what ChordGenMenu outliving
    // that window is for.
    menu.addSeparator();
    if (onExtraMenuItems)
        onExtraMenuItems(slot, menu);

    // Hand this card to a line right now (Owen, 2026-08-16: "I'd like to be able to right click
    // on a chord pad and say send to ARP a or b"). Dragging a card onto a line's switch on the arp
    // bar or onto its macro card has been the only left-click path into a line since 2026-08-02,
    // when a plain click stopped feeding one; this is that gesture's accelerator, exactly the
    // relationship "Send to arp slot" below has with a drop on a slot card, so it opens no new
    // right-click-only path.
    //
    // One row per **UI** line, not per engine: `uiArpLines` is what the screen counts off, so C
    // comes back here the day it comes back anywhere. Live on a line that is switched off, on
    // purpose - a line that is off still takes chords in, and switching it on then plays what it
    // was handed.
    for (int n = 0; n < KeysProcessor::uiArpLines; ++n)
        menu.addItem(arpLineIdBase + n,
                     "Send to arp " + juce::String::charToString((juce::juce_wchar) ('A' + n)),
                     filled && onSendToArpLine != nullptr);

    // Bind this card to an arp slot, so launching that slot plays this chord through that
    // slot's pattern. The other half of the "cards into the arp" pair: the rows above - one per
    // line, so four of them since 2026-08-19 - hold a card in a line right now, this parks one
    // in a slot for later.
    juce::PopupMenu slots;
    for (int s = 0; s < KeysProcessor::numArpPatterns; ++s)
    {
        const auto& target = processor.arpPatternSlot(s);
        auto label = juce::String(s + 1);
        if (target.chordName.isNotEmpty())
            label += "  (" + target.chordName + ")";
        slots.addItem(arpSlotIdBase + s, label, filled);
    }
    menu.addSubMenu("Send to arp slot", slots, filled);

    // The page, not the card. Greys when there is nothing on this page a clear would take -
    // an empty page, or one where every filled card is locked - so the row says what it would
    // do without a hover, the way Fill and Regen on the bar do. **Locked pads are spared**:
    // that is the whole meaning of a lock against an action that takes a page at a time.
    menu.addSeparator();
    menu.addItem(10, "Clear page", processor.pageHasClearablePads());

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
            safe->processor.pushUndo("Clear pad", KeysProcessor::UndoScope::pads);
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
        else if (choice == 7) // Copy chord
        {
            auto copied = safe->processor.chordPad(slot);
            copied.locked = false; // a lock belongs to the slot, never travels with the chord
            safe->clipboard = copied;
        }
        else if (choice == 8) // Paste chord
        {
            if (safe->clipboard && ! safe->processor.chordPad(slot).locked)
            {
                // Same choke as a drop - see itemDropped's own comment on why the clear has to
                // come first: it is what stops a pad left ringing by Sustain, or one feeding an
                // arp line, stranding its old notes with nothing left owning them.
                const KeysProcessor::UndoGesture undoable { safe->processor, "Paste chord",
                                                           KeysProcessor::UndoScope::pads };
                safe->processor.clearChordPad(slot);
                safe->processor.setChordPad(slot, *safe->clipboard);
            }
        }
        else if (choice == 9) // Save chord as MIDI
        {
            safe->saveChordAsMidi(slot);
        }
        else if (choice == 10) // Clear page
        {
            // End the edit first if its target is one of the cards about to go, the same
            // reason Clear pad does it above: the link is the keybed's to write, and left
            // live it would write the next latched chord straight back into a slot the user
            // has just emptied. The edited slot need not be the one this menu was opened
            // from, so the test is the *page*, and a locked card survives the clear and so
            // keeps its edit.
            const int first = safe->processor.padPageOffset();
            const int last = first + KeysProcessor::padsPerPage - 1;
            const int edited = safe->editingSlot;
            if (edited >= first && edited <= last && safe->onEditToggle
                && ! safe->processor.chordPad(edited).locked)
                safe->onEditToggle(edited);
            safe->processor.clearChordPadPage();
        }
        else if (choice >= arpSlotIdBase && choice < arpSlotIdBase + KeysProcessor::numArpPatterns)
        {
            // Undoable: this replaces the slot's chord, name, shape and rate in place, and a slot
            // is one of the two trees undo covers. Copy slot and Randomize pattern in ArpPanel
            // both push for the same data; this one did not until 2026-08-17, so sending a pad to
            // a slot you had already dressed threw that slot away with no way back.
            safe->processor.pushUndo("Send pad to arp slot", KeysProcessor::UndoScope::arp);
            const auto& pad = safe->processor.chordPad(slot);
            safe->processor.setArpSlotChord(choice - arpSlotIdBase, pad.notes, pad.name);
        }
        else if (choice >= arpLineIdBase && choice < arpLineIdBase + KeysProcessor::uiArpLines)
        {
            if (safe->onSendToArpLine)
                safe->onSendToArpLine(slot, choice - arpLineIdBase);
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
    if (canRevoice(next) && onKeyboard(next)) // matches the menu item's own enable test
        rewritePadChord(slot, next);
}

// "Save chord as MIDI" (2026-08-17). A menu row cannot start a drag, so this is the other way a
// chord leaves the plugin: write it to disk and hand Explorer the file already selected
// (juce::File::revealToUser), ready for the same short drag TakePanel::dragTakeOut offers for a
// recorded take - Ableton's own clipboard is internal to Live and will not accept a paste from
// the Windows clipboard, so a file dropped onto a track is the only route in.
//
// The velocity is `baseVelocity01()`, the same value `pressChordPad` fires this very pad at - a
// pad has no per-note velocities of its own to read, so the base the processor already plays it
// with *is* "the pad's own velocity path", not a guess standing in for one.
void ChordPads::saveChordAsMidi(int slot)
{
    const auto& pad = processor.chordPad(slot);
    if (pad.notes.empty()) // matches the menu item's own enable test
        return;

    juce::MidiFile file;
    if (! oneBarChordMidiFile(pad.notes, processor.baseVelocity01(), processor.currentTempo(), file))
        return;

    auto folder = KeysProcessor::takeFolder();
    if (! folder.createDirectory().wasOk())
        return;

    // createLegalFileName rather than trusting the detected name outright: the generator's own
    // fallback label can carry parentheses, and nothing stops a hand-built session naming a pad
    // something a filesystem would refuse.
    const auto legalName = juce::File::createLegalFileName(pad.name.isNotEmpty() ? pad.name : "Chord");
    auto out = folder.getChildFile("Keys chord " + legalName + ".mid").getNonexistentSibling();

    juce::FileOutputStream stream(out);
    if (! stream.openedOk() || ! file.writeTo(stream))
        return;
    stream.flush();
    out.revealToUser();
}

void ChordPads::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        // Right-click never plays or drags; it opens the pad's card menu. Both fields are put
        // back explicitly rather than left to whatever the last gesture set: since 2026-08-18 it
        // is *mouseUp* that sounds a card, and the mouseUp closing this right-click must not find
        // a stale `dragSource` under it and play the chord you opened the menu to throw away.
        dragging = false;
        dragSource = -1;
        // **Release whatever is sounding first** (2026-08-22). This branch returns before the
        // `endAudition()` guard below, which was harmless while the press was silent and is not
        // now that it fires: left-press a pad to lean on it, right-click to reach the card menu,
        // and the popup takes the mouse - so the pending left mouseUp never arrives here and the
        // chord rings until the next left press on the strip. Ending it here is also what the
        // menu wants: every row on it is about a card you are no longer playing.
        endAudition();
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
    //
    // **The press is where a card starts sounding** (2026-08-22, Owen: "when the play mode is
    // checked on the pads, I want it to trigger as soon as you click on it and stay held until
    // you let go"), and the release is where it stops - so a stab is short and a lean is long,
    // which is most of what a pad is for. The mouse-up owns the note-off; nothing on this strip
    // is on a timer any more.
    //
    // This was the release for a fixed 800 ms from 2026-08-18, with holding hidden behind a
    // settings tick. The reason was that firing here lets a press that turns out to be a *drag*
    // choke the other chord sources on its way out - which is real, and is now the **Play**
    // toggle's job rather than a second switch's: turn Play off and the strip is drag-only. One
    // control, one question. See `LayoutState::padsPlayOnClick`.
    downPos = e.position;
    dragging = false;
    dragSource = cellAt(e.position);

    // The one gate: the Pads bar's Play toggle (2026-08-19). Off, a strip you are only dragging
    // from cannot fire a chord on the way to the arpeggiator.
    if (processor.layout.padsPlayOnClick)
        startAudition();
    repaint();
}

// Sound whatever the gesture is pointing at - a filled pad, or the live card's own chord. The
// mouse-up owns the note-off, always, which is what makes a stab short and a lean long; there
// is no timed variant any more (2026-08-22, see the header).
void ChordPads::startAudition()
{
    if (dragSource >= 0 && ! processor.chordPad(dragSource).notes.empty())
    {
        processor.pressChordPad(dragSource);
        playing = dragSource;
    }
    else if (dragSource == -2 && hasNotes(currentNotes))
    {
        // The live card plays too, and the same way: it fires the chord you are holding as one
        // strummed, humanized gesture the way a pad plays it, so two cards on one strip must not
        // answer the same click differently.
        processor.pressLiveChord(currentNotes);
        playingLive = true;
    }
}

// Let go of whatever this strip is currently sounding, if anything. Safe to call at any time
// and on any path - it is how a release, a drag and every interruption end the same state.
void ChordPads::endAudition()
{
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

void ChordPads::mouseDrag(const juce::MouseEvent& e)
{
    // **14 px, not 6** (2026-08-22). Six was chosen when the press was silent, so an accidental
    // drag cost nothing but a ghost; now the press sounds and a drag routes a chord, so the
    // threshold is what separates "I am leaning on this chord" from "I am moving this card".
    // Wider is the forgiving direction on a surface driven by one mouse - the same reasoning
    // that gives every target 34 px - and a deliberate drag crosses 14 px without noticing.
    if (dragging || e.position.getDistanceFrom(downPos) <= 14.0f)
        return;

    // What a drag has to decide is only whether there was something under it worth carrying,
    // and a *filled* pad is the test - dragging an empty cell has never meant anything. The arp
    // branch used to clear dragSource before this ran, which is what made a card undraggable
    // with a line on.
    if (! sourceIsDraggable())
    {
        dragSource = -1; // nothing grabbable under the press
        return;
    }

    // **The note is not cut here** (2026-08-22). It was, and that made the *length of a chord*
    // depend on the hand staying inside a small circle: with the press owning the note, any
    // tremor past the threshold ended the audition and put a drag ghost under the cursor, so a
    // lean stopped for no visible reason. Keys is played with one mouse by someone with muscular
    // dystrophy - this is precisely the case a distance threshold penalises, and "stay held
    // until you let go" is what was asked for, not "until you let go or twitch".
    //
    // So the chord runs until mouseUp whatever the gesture turned into, and a drag is a drag
    // that happens to be sounding. Releasing over nothing cancels the drag and ends the note;
    // releasing over a target routes the chord and ends the note. Nothing is left ringing:
    // mouseUp calls endAudition on every path, and so does the destructor.
    //
    // What this still cannot undo is the *choke*: firing a chord stops the other chord sources,
    // and with Exclusive on that includes each arp line's held chord, so by the time a drag is
    // recognised a running line has already been stopped. **That is what the Play toggle is
    // for** - off, the strip is drag-only and silent at both ends, which is the setting for
    // dragging cards into the arpeggiator. Turning Exclusive off instead costs the drag nothing.
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

    // **Say out loud that this window is in front** (2026-08-18, Owen: "you couldn't drag things
    // into the arpeggiator when the generator was opened").
    //
    // JUCE resolves a cross-window drop through `Desktop::findComponentAt`, which walks its own
    // `desktopComponents` list from the top and **returns from the first window whose bounds
    // contain the point** - it never falls through to a lower window when the one it picked has
    // no interested target there. That list is maintained by `Desktop::componentBroughtToFront`,
    // which fires from `Component::toFront` and from a peer's `handleBroughtToFront`.
    //
    // The generator window calls `toFront` when it opens, so it goes to the top of that list. The
    // plugin editor never reports the same thing: it is a *child* window inside the host's, so
    // clicking it raises it on screen without any peer activation JUCE hears about. The list
    // therefore keeps the generator on top for good, and since that window is nearly full screen,
    // every drop aimed at the arpeggiator landed inside its bounds and was resolved against it -
    // finding nothing interested, and vanishing.
    //
    // This is not gaming the order, it is correcting it: the press that started this drag was in
    // *this* window, so this window is the front one, and `toFront(false)` (no focus grab) is how
    // a component says so. The reverse direction still works for the same reason it always did -
    // dragging out of the tray means you clicked the generator, and that window is a real
    // top-level one whose activation JUCE does hear.
    if (auto* top = getTopLevelComponent(); top != nullptr && top->isOnDesktop())
        top->toFront(false);

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
        // **A drag that lands on nothing is a cancelled drag** (2026-08-18, Owen: "when you drag
        // your chord out of the pad into the arpeggiator, it disappears"). This used to clear the
        // pad whenever no target claimed the card and the release had left the strip, on the
        // reading that "off the row means bin it". The trouble is how much of the window is
        // neither the strip nor a target: the two section bars above and below the arp panel, the
        // Controls band, the keybed, every gap between them. Dragging *up* out of the strip
        // crosses the Pads bar on the way to the arpeggiator, so a release a few pixels short of
        // the panel destroyed the chord instead of routing it - and the four arp targets set the
        // `taken` veto correctly, which is why this only ever bit on a near miss and so read as
        // random.
        //
        // The gesture it cost is the one that had the least claim to the pixels: Clear pad is on
        // the card menu, which is its documented home, and undo has covered accidents since
        // 2026-08-14. Aiming at a target with one mouse is hard enough without the whole rest of
        // the window being a shredder. `taken` stays as the flag targets set - the reference box
        // and the tray still read it - it simply no longer has a destructive default behind it.
        inFlight = nullptr;
    }
    else
    {
        // The button came up without travelling, so the press was a play and this is the end of
        // it. The note ends here rather than on a timer (2026-08-16): letting go is the release,
        // which is what makes a stab short and a lean long. Sustain and Latch still decide what
        // "release" means - endAudition goes through releaseChordPad / releaseLiveChord, so a
        // pedalled chord keeps ringing exactly as it did before.
        //
        // A click never hands a chord to the arpeggiator (2026-08-02, Owen: "when an
        // arpeggiator's running and you click on a pad, I don't want it to send it to the
        // arpeggiator unless you drag it"). Feeding a line is the *drag* - onto a line's card,
        // its letter tab on the arp bar, or a slot. One arp behaviour survives on the left
        // button, and it is a stop, not a send: a *cleared* card still feeding a line wears the
        // ring with no notes behind it, so a click on it has nothing to play and keeps meaning
        // the only other thing it can - let go. It sits before the release rather than instead
        // of it: the press found no notes on that card, so there is nothing sounding to end, and
        // running both keeps this branch from being the one path that skips the cleanup.
        if (const int holder = dragSource >= 0 ? processor.arpLineHoldingPad(dragSource) : -1;
            holder >= 0 && dragSource >= 0 && processor.chordPad(dragSource).notes.empty())
        {
            processor.releaseArpChord(holder);
        }

        // The press already fired the chord, so letting go is the release - a stab is short and
        // a lean is long (2026-08-22). There is no other branch here any more: the fixed 800 ms
        // blip this used to start when the press was silent went with the settings tick that
        // chose between them, and `startAudition(true)`'s only caller was that branch.
        //
        // **`endAudition` unconditionally, whatever Play says.** It is the release half of a
        // gesture, not a way of making sound, so gating it on the toggle is how a press that
        // was sounding when Play flipped mid-gesture would be left ringing with nothing left
        // to end it. It routes through releaseChordPad / releaseLiveChord, so Sustain and Latch
        // still decide what "release" means and a pedalled chord keeps ringing exactly as
        // before; with Play off nothing was started, and ending nothing costs nothing.
        endAudition();
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
