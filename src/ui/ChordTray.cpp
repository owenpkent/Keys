#include "ChordTray.h"
#include "../ChordGen.h"
#include "../Chords.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>
#include <okstudio/Scales.h>
#include <okstudio/Theme.h>
#include <algorithm>

namespace keys
{
namespace
{
    constexpr float kGap = 6.0f;
    constexpr float kRadius = 6.0f;
    constexpr float kNoteLineH = 11.0f; // the note list under the name, as on a pad card

    // The same line a pad card carries, so a candidate and the pad it would become read
    // identically. Deliberately a copy of ChordPads' local helper rather than a shared one:
    // three lines, and hoisting it would put a formatting detail of the card into a header
    // that neither class owns.
    juce::String noteListText(const std::vector<int>& notes)
    {
        const auto names = okstudio::scales::noteNames();
        juce::String out;
        for (const int n : notes)
            out << (out.isEmpty() ? "" : "  ") << names[((n % 12) + 12) % 12] << juce::String(n / 12 - 1);
        return out;
    }

    // The whole chord an octave down or up, or empty when that would run it off the ends of
    // MIDI. Same rule and the same refusal as the pad card's own octave items: a chord that
    // cannot move in one piece does not move at all, and the menu item greys rather than folding
    // one note round to the other end of the keyboard.
    std::vector<int> shiftedOctave(const std::vector<int>& notes, int semitones)
    {
        std::vector<int> out;
        out.reserve(notes.size());
        for (const int n : notes)
        {
            if (n + semitones < 0 || n + semitones > 127)
                return {};
            out.push_back(n + semitones);
        }
        return out;
    }

    // The next inversion up: the lowest note climbs an octave onto the top of the stack. Enough
    // rotations bring the chord back to where it started an octave higher, which is what makes
    // this a single repeatable item rather than a submenu of named inversions.
    std::vector<int> nextVoicing(const std::vector<int>& notes)
    {
        if (notes.size() < 2 || notes.front() + 12 > 127)
            return {};
        std::vector<int> out(notes.begin() + 1, notes.end());
        out.push_back(notes.front() + 12);
        std::sort(out.begin(), out.end());
        return out;
    }
} // namespace

ChordTray::ChordTray(KeysProcessor& p, ChordGenMenu& g) : processor(p), gen(g)
{
    okstudio::ui::makeMouseOnly(*this);
    setTitle("Chord audition tray");
    cells.resize((size_t) numCells); // twelve blanks, then fill() writes every one of them
    fill();
}

juce::Rectangle<float> ChordTray::cellBounds(int index) const
{
    const auto r = getLocalBounds().toFloat();
    const int row = index / cols;
    const int col = index % cols;
    const float w = (r.getWidth() - kGap * (float) (cols - 1)) / (float) cols;
    const float h = (r.getHeight() - kGap * (float) (rows - 1)) / (float) rows;
    return { r.getX() + (float) col * (w + kGap), r.getY() + (float) row * (h + kGap), w, h };
}

int ChordTray::cellAt(juce::Point<float> pos) const
{
    for (int i = 0; i < numCells; ++i)
        if (cellBounds(i).contains(pos))
            return i;
    return -1;
}

juce::String ChordTray::settingsSignature() const
{
    // Every parameter either brain reads, plus the two picks that are not parameters. Not the
    // page's locked chords: they reach generation through Lock Influence, and folding them in
    // here would reroll the tray each time a commit changed the page under it.
    static const char* ids[] = { "genRoot", "genMode", "genOctave", "genSource",
                                 "genNotesMin", "genNotesMax", "genOctaveMax", "genMajMin",
                                 "genInv0", "genInv1", "genInv2", "genInv3",
                                 "genCompliance", "genLockInfluence", "genSmooth",
                                 "genCircleDir", "genPlrP", "genPlrL", "genPlrR",
                                 "genProgression", "genPlaningDiatonic",
                                 "genUseKey", "genUseMode", "genUseOctave", "genUseNotes",
                                 "genUseInversions", "genUseCompliance",
                                 "markovMode", "markovTemp", "markovLength" };
    juce::String sig;
    for (const char* id : ids)
        if (auto* v = processor.apvts.getRawParameterValue(id))
            sig << juce::String(v->load(), 3) << '|';
    sig << gen.moodChoice() << '|' << gen.startChoice();
    return sig;
}

bool ChordTray::hasEmptyCells() const
{
    return std::any_of(cells.begin(), cells.end(), [](const auto& c) { return c.notes.empty(); });
}

bool ChordTray::hasFilledCells() const
{
    return std::any_of(cells.begin(), cells.end(), [](const auto& c) { return ! c.notes.empty(); });
}

int ChordTray::sendAllToPads()
{
    if (! onSendToFirstEmpty)
        return 0;

    int placed = 0;
    for (auto& cell : cells)
    {
        if (cell.notes.empty())
            continue;
        if (! onSendToFirstEmpty(cell))
            break; // the page is full; every later card would get the same answer
        cell = {};  // committed, so it leaves a hole exactly as a drag does
        ++placed;
    }
    if (placed > 0)
        repaint();
    return placed;
}

// The one place candidates are asked for, so Fill and Regen differ only in which cells they
// offer up. `targets` is by index, and short of what was asked for leaves the rest as they were
// rather than half-writing the grid.
void ChordTray::writeInto(const std::vector<int>& targets)
{
    if (targets.empty())
        return;
    const auto fresh = gen.generateCandidates((int) targets.size());
    for (size_t i = 0; i < targets.size() && i < fresh.size(); ++i)
        cells[(size_t) targets[i]] = fresh[i];

    // The caption speaks for the **whole tray** ("these were generated before you moved that
    // setting"), so only a write that covers every chord now on screen may clear it. Stamping it
    // unconditionally meant filling one hole declared the other fifteen fresh: sweep Source to
    // Markov with a full tray, take one card, click the hole it left, and the warning vanished
    // while fifteen candidates from the old Source stayed where they were.
    //
    // Cheap to state exactly rather than by a flag per caller: a cell that carries a chord and
    // was not written by this call is a cell the current settings have never seen. Regen clears
    // it (it targets every filled cell), Fill on an empty tray clears it, and neither a one-cell
    // fill nor a Fill around cards you kept does.
    bool coversEveryChord = true;
    for (int i = 0; i < numCells && coversEveryChord; ++i)
        if (! cells[(size_t) i].notes.empty())
            coversEveryChord = std::find(targets.begin(), targets.end(), i) != targets.end();
    if (coversEveryChord)
        lastSignature = settingsSignature();
    repaint();
}

void ChordTray::fill()
{
    std::vector<int> targets;
    for (int i = 0; i < numCells; ++i)
        if (cells[(size_t) i].notes.empty())
            targets.push_back(i);
    writeInto(targets);
}

void ChordTray::regen()
{
    std::vector<int> targets;
    for (int i = 0; i < numCells; ++i)
        if (! cells[(size_t) i].notes.empty())
            targets.push_back(i);
    writeInto(targets);
}

void ChordTray::setAll(const std::vector<KeysProcessor::ChordPad>& candidates)
{
    for (int i = 0; i < numCells; ++i)
        cells[(size_t) i] = i < (int) candidates.size() ? candidates[(size_t) i]
                                                        : KeysProcessor::ChordPad {};
    // Not a settings change, so `lastSignature` is left alone on purpose: a seeded trayful is an
    // answer to a question about one chord, and the settings poll must not decide it is stale and
    // reroll it out from under you on its next tick.
    repaint();
}

void ChordTray::clear()
{
    // Stop anything this tray is auditioning first: the card that is sounding is about to stop
    // existing, and the 800 ms timer would otherwise release notes belonging to a chord the
    // window no longer shows.
    gen.stopAudition();
    auditioning = -1; // nothing is sounding, so nothing may still be lit as if it were
    for (auto& c : cells)
        c = {};
    repaint();
}

bool ChordTray::settingsMovedSinceFill() const
{
    return hasFilledCells() && settingsSignature() != lastSignature;
}

void ChordTray::paint(juce::Graphics& g)
{
    const juce::Colour inkOnAccent { 0xff07272c };

    for (int i = 0; i < numCells; ++i)
    {
        const auto b = cellBounds(i);
        const auto& c = cells[(size_t) i];
        const bool filled = ! c.notes.empty();
        // The card being dragged dims where it sits. The ghost does follow the cursor out of this
        // window now (2026-08-02), so this is no longer standing in for it - it is the hole the
        // card left, which is the thing that says a commit will empty this cell.
        const bool airborne = dragging && i == pressed;

        if (! filled)
        {
            g.setColour(skin::well.withAlpha(0.55f));
            g.fillRoundedRectangle(b, kRadius);
            // A hole is a target since 2026-08-16 - a click fills it - so it has to look like one.
            // The hover tint is the filled card's own, and the plus says what the click does: an
            // unmarked well reads as scenery, which is why the way back out of a hole was invisible
            // even after Fill on the header could reach it.
            const bool hot = i == hovered && ! dragging;
            if (hot)
            {
                g.setColour(skin::accentOf(*this).base.withAlpha(0.10f));
                g.fillRoundedRectangle(b, kRadius);
            }
            g.setColour(juce::Colours::white.withAlpha(hot ? 0.10f : 0.035f));
            g.drawRoundedRectangle(b, kRadius, 1.0f);
            g.setColour(hot ? skin::accentOf(*this).base : skin::textFaint);
            g.setFont(skin::uiSemi(17.0f));
            g.drawText("+", b, juce::Justification::centred, false);
            continue;
        }

        {
            juce::Graphics::ScopedSaveState ss(g);
            if (airborne)
                g.setOpacity(0.4f);
            skin::raisedFill(g, b, kRadius, juce::Colour(0xff272b32), juce::Colour(0xff1e2126));

            // Sounding from this press and not yet dragged: light it the way a sounding pad is
            // lit. The press *is* the note, and the fill is the only thing that says so.
            //
            // Keyed on `auditioning` rather than on `pressed`, because those two came apart when
            // a hole became clickable: that path clears `pressed` at once so the new card cannot
            // become a drag source mid-gesture, which also left the one card you had just asked
            // for as the only audition in the window that never lit.
            if (i == auditioning && ! dragging)
            {
                g.setGradientFill({ skin::accentOf(*this).hot, 0.0f, b.getY(),
                                    skin::accentOf(*this).base, 0.0f, b.getBottom(), false });
                g.fillRoundedRectangle(b, kRadius);
                skin::glowRect(g, b, kRadius, skin::accentOf(*this).base);
            }
            else if (i == hovered)
            {
                g.setColour(skin::accentOf(*this).base.withAlpha(0.10f));
                g.fillRoundedRectangle(b, kRadius);
            }

            const bool lit = (i == auditioning && ! dragging);
            const auto ink = lit ? inkOnAccent : skin::text;
            auto text = b.reduced(4.0f, 3.0f);
            const auto noteLine = text.removeFromBottom(kNoteLineH);

            g.setColour(ink);
            g.setFont(skin::uiSemi(13.5f));
            g.drawText(c.name, text, juce::Justification::centred, true);

            g.setColour(lit ? inkOnAccent.withAlpha(0.75f) : skin::textDim);
            g.setFont(skin::micro(9.0f));
            g.drawText(noteListText(c.notes), noteLine.toNearestInt(),
                       juce::Justification::centred, true);
        }
    }
}

void ChordTray::mouseMove(const juce::MouseEvent& e)
{
    if (const int h = cellAt(e.position); h != hovered)
    {
        hovered = h;
        repaint();
    }
}

void ChordTray::mouseExit(const juce::MouseEvent&)
{
    if (hovered != -1)
    {
        hovered = -1;
        repaint();
    }
}

// The tray card menu (Owen, 2026-08-01: "when you right click on a chord in there, I want you to
// have a whole bunch of options about trying to find similar ones or what might come next").
//
// This is a **new owner-directed right-click path**, added to the closed list in CLAUDE.md on
// that date rather than drifting in. It earns it the same way the pad card menu did: a tray card
// is all playing surface too, and the two questions worth asking about a chord you just heard -
// "more like this" and "what comes after this" - have nowhere on a card to live as buttons.
//
// Every item that *can* have a left-click twin has one. Send to first empty pad is the drag with
// the aim taken out; the three shaping edits are the same three the pad menu carries, and they
// are menu-only there for the same reason they are here.
// An **empty** cell gets the two rows that need no seed, rather than nothing at all (Owen,
// 2026-08-16: "when you are generating chords and you move one off, there's an empty space, and
// then you can't regenerate it"). The hole a committed card leaves is deliberate - it is the record
// of what you have taken and the thing that gives Fill a job - but it had no per-cell way back:
// Regen acts on the filled cells by design, this menu returned before it was built, and mouseDown
// returned before even reaching it. Left-clicking the hole fills it now; these two are the same
// action aimed and in bulk.
void ChordTray::showCardMenu(int index)
{
    const auto seed = cells[(size_t) index].notes;
    const bool filled = ! seed.empty();

    // **Opening this menu makes no sound** (Owen, 2026-08-01: "when you right click, it plays the
    // chord. We don't want it to play"). It auditioned the card for a few minutes earlier that
    // day, on the theory that you would want to hear what you were deciding about. You do not:
    // the left click is already the way to hear a card, so right-clicking one you have just
    // auditioned replayed it, and right-clicking to reach Clear made a noise on the way to
    // throwing the chord away. Hearing a chord is a left click and nothing else.
    const bool pageHasRoom = onPageHasEmptyPad ? onPageHasEmptyPad() : false;

    juce::PopupMenu m;
    if (filled)
    {
        m.addItem(1, "Send to first empty pad", pageHasRoom);
        m.addSeparator();
        m.addItem(2, "Fill tray with similar chords");
        m.addItem(3, "Fill tray with what could follow");
        m.addSeparator();
        m.addItem(4, "Octave down", seed.front() >= 12);
        m.addItem(5, "Octave up", seed.back() <= 115);
        m.addItem(6, "Next voicing", seed.size() >= 2);
        m.addSeparator();
    }
    m.addItem(7, "New chord here");
    if (filled)
        m.addItem(8, "Clear this card");
    else
        // Unconditionally live, and no `hasEmptyCells()` guard: this row only exists on an empty
        // cell, and that cell is one of the cells the predicate scans, so it could never be false
        // here. A guard that cannot fail reads as a precondition and gets copied as one.
        m.addItem(9, "Fill every empty card");

    juce::PopupMenu::Options opts;
    opts = opts.withTargetComponent(this).withTargetScreenArea(
        localAreaToGlobal(cellBounds(index)).toNearestInt());

    juce::Component::SafePointer<ChordTray> safe(this);
    m.showMenuAsync(opts, [safe, index, seed](int id)
    {
        auto* t = safe.getComponent();
        if (t == nullptr || id == 0)
            return;
        t->gen.stopAudition();

        switch (id)
        {
            case 1:
                if (t->onSendToFirstEmpty && t->onSendToFirstEmpty(t->cells[(size_t) index]))
                    t->cells[(size_t) index] = {}; // committed, so it leaves a hole like a drag
                break;
            case 2:
            case 3:
            {
                // The seeded fills replace the *whole* tray, this card included. Keeping the
                // seed on screen would cost a sixteenth of the answer to show you the chord you
                // are already holding in your head, and the seed is one Regen away if you want
                // it back.
                const auto fresh = (id == 2) ? t->gen.similarTo(seed, numCells)
                                             : t->gen.couldFollow(seed, numCells);
                for (int i = 0; i < numCells; ++i)
                    t->cells[(size_t) i] = i < (int) fresh.size() ? fresh[(size_t) i]
                                                                  : KeysProcessor::ChordPad {};
                break;
            }
            case 4: t->reshapeCell(index, shiftedOctave(seed, -12)); break;
            case 5: t->reshapeCell(index, shiftedOctave(seed, 12)); break;
            case 6: t->reshapeCell(index, nextVoicing(seed)); break;
            case 7: t->writeInto({ index }); break;
            case 8: t->cells[(size_t) index] = {}; break;
            case 9: t->fill(); break;
            default: break;
        }
        t->repaint();
    });
}

// An edit to one candidate's notes. Nothing to unwind the way ChordPads::rewritePadChord has to:
// a tray card is not sounding except for the audition, which owns its own release, and it is not
// feeding the arp because it is not on a pad at all.
void ChordTray::reshapeCell(int index, const std::vector<int>& notes)
{
    if (notes.empty())
        return; // the edit would have run off the keyboard; the menu item was greyed for it
    cells[(size_t) index].notes = notes;
    cells[(size_t) index].name = chords::detect(notes);
    // Silent, like everything else the card menu does. This played the edited chord back for a
    // few minutes on 2026-08-01 so you could hear what an octave shift did; it went with the
    // menu-open audition, because "the right-click path never makes a sound" is a rule worth
    // more than the one confirmation it bought. Left-click the card to hear the result.
    repaint();
}

void ChordTray::mouseDown(const juce::MouseEvent& e)
{
    pressed = cellAt(e.position);
    downPos = e.position;
    dragging = false;

    if (pressed < 0)
        return;

    if (e.mods.isPopupMenu())
    {
        const int card = pressed;
        pressed = -1; // the menu owns the gesture from here; this is not a press-and-drag
        showCardMenu(card);
        return;
    }

    // Everything below generates or sounds something, so it belongs to the left button alone.
    // The guard this replaced was `pressed < 0 || cells[pressed].notes.empty()`, which made a
    // middle or X-button press over the tray inert as a side effect of the empty-cell test; the
    // moment a hole became a live target, that press started rolling a chord and taking the room
    // instead. The contract in CLAUDE.md is "single left-click or drag".
    if (! e.mods.isLeftButtonDown())
    {
        pressed = -1;
        return;
    }

    // A click on the hole a committed card left generates a chord into it, and then hears it, so
    // taking a card and getting another one back is the same gesture twice (Owen, 2026-08-16:
    // "you can't regenerate it"). It is a free gesture: an empty cell has nothing to audition and
    // nothing to drag, so this click did nothing at all before. Regen still means "reroll the
    // cards I kept", which is what makes the pair of them worth having.
    if (cells[(size_t) pressed].notes.empty())
    {
        const int cell = pressed;
        pressed = -1; // filled now, but not by a press that may still become a drag
        writeInto({ cell });
        if (! cells[(size_t) cell].notes.empty())
        {
            gen.auditionChord(cells[(size_t) cell].notes); // a click on a tray card makes a sound
            auditioning = cell; // and a card that is sounding lights, this one included
        }
        repaint();
        return;
    }

    // The audition fires on the press, not the release: it is a beat pad in every way that
    // matters, and a chord you only hear after letting go is a chord you cannot compare with
    // the next one. A drag cancels it below the moment the mouse actually moves.
    gen.auditionChord(cells[(size_t) pressed].notes);
    auditioning = pressed;
    repaint();
}

void ChordTray::mouseDrag(const juce::MouseEvent& e)
{
    if (pressed < 0 || dragging)
        return;

    if (e.position.getDistanceFrom(downPos) > 6.0f)
    {
        // A press that becomes a drag is a commit, not an audition. Stop the note first, the
        // same way the pad strip turns a press into a rearrange.
        gen.stopAudition();
        beginDrag(e);
    }
}

// Hand the candidate to JUCE and let go of it. Everything after this - the ghost, the target
// lighting up, the drop landing in another window - is the framework's, which is the whole point
// of the 2026-08-02 migration: this used to be a screen-coordinate callback per phase.
void ChordTray::beginDrag(const juce::MouseEvent& e)
{
    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (container == nullptr)
    {
        jassertfalse; // ChordGenPanel is meant to be one; a tray with no container cannot commit
        return;
    }

    // The snapshot is taken *before* `dragging` goes true, so the ghost is a picture of the card
    // as it reads at rest rather than of the dimmed hole it is about to become.
    const auto cell = cellBounds(pressed).toNearestInt();
    const auto ghost = createComponentSnapshot(cell, true, 2.0f).convertedToFormat(juce::Image::ARGB);
    const auto grab = cell.getTopLeft() - downPos.roundToInt(); // JUCE negates this into the image

    inFlight = new chorddrag::Payload(chorddrag::Payload::From::trayCell, pressed,
                                      cells[(size_t) pressed]);
    dragging = true;
    repaint();

    container->startDragging(juce::var(inFlight.get()), this, juce::ScaledImage(ghost, 2.0),
                             /*allowDraggingToExternalWindows*/ true, &grab, &e.source);
}

void ChordTray::mouseUp(const juce::MouseEvent&)
{
    // Ahead of the early return below, because the hole-filling press clears `pressed` on the way
    // down and would otherwise leave its card lit until the next press landed somewhere.
    const bool wasLit = auditioning >= 0;
    auditioning = -1;

    if (pressed < 0)
    {
        if (wasLit)
            repaint();
        return;
    }

    if (! dragging)
    {
        // Audition only. The note is already sounding and ChordGenMenu's 800 ms timer owns its
        // release, so there is deliberately nothing to do here: letting go early would make the
        // preview as short as the click and unusable for comparing two chords.
        pressed = -1;
        repaint();
        return;
    }

    // Whether a pad took the candidate is not known yet - `itemDropped` runs after this, later in
    // the same event. Ask a message-loop turn from now, which is after it and before the next
    // frame. See chorddrag::whenDragSettles.
    chorddrag::whenDragSettles(*this, inFlight,
                               [](ChordTray& t, const chorddrag::Payload& p)
                               {
                                   if (p.consumed && p.index >= 0 && p.index < numCells)
                                   {
                                       // It landed, and the cell it came from goes empty rather
                                       // than refilling itself. The hole is the only state this
                                       // tray keeps and it earns its place twice: it is how you
                                       // see which of the sixteen you have already taken, and it
                                       // is what gives Fill something to do. A cell that refilled
                                       // instantly left Fill permanently greyed and Regen
                                       // indistinguishable from "reroll everything".
                                       t.cells[(size_t) p.index] = {};
                                   }
                                   // Anywhere else - the tray itself, the reference box, the
                                   // desktop, a folded pad section - keeps the candidate. There is
                                   // no "drag off to discard" here: a tray card costs nothing to
                                   // leave alone, and the gesture that clears a *pad* is the same
                                   // shape, so making this one destructive would be the one drag
                                   // in Keys that loses work by missing.
                                   t.endDrag();
                               });
}

void ChordTray::endDrag()
{
    dragging = false;
    pressed = -1;
    inFlight = nullptr;
    repaint();
}

// ---------------------------------------------------------------------------------------
// The reference card. See the header for why it exists; this end is deliberately small, because
// the card holds a chord and does nothing to it. Every action *on* the reference is a button
// beside it in ChordGenPanel, not a gesture buried in here.
// ---------------------------------------------------------------------------------------

ChordRefCard::ChordRefCard(KeysProcessor& p, ChordGenMenu& g) : processor(p), gen(g)
{
    okstudio::ui::makeMouseOnly(*this);
    setTitle("Reference chord");
}

void ChordRefCard::setChord(const KeysProcessor::ChordPad& pad)
{
    held = pad;
    held.locked = false; // a lock is a fact about a pad slot, and this is not one
    dropHighlight = false;
    repaint();
}

void ChordRefCard::clearChord()
{
    gen.stopAudition(); // it may be the chord currently sounding, and it is about to not exist
    held = {};
    repaint();
}

void ChordRefCard::setDropHighlight(bool on)
{
    if (on == dropHighlight)
        return;
    dropHighlight = on;
    repaint();
}

// The reference takes chords from either window. It refuses the live card because that one is
// what you are holding on the keyboard rather than a chord you have decided to keep, which is
// the same refusal the strip's own drag made when it offered only filled pads to the outside.
// It refuses its own chord for the duller reason: since 2026-08-17 this card is a drag source
// too, and a card that lit up to accept what it is already holding would be promising a change
// it cannot make.
bool ChordRefCard::isInterestedInDragSource(const SourceDetails& details)
{
    auto* p = chorddrag::chordBeingDragged(details);
    using From = chorddrag::Payload::From;
    return p != nullptr && p->from != From::liveCard && p->from != From::refCard;
}

void ChordRefCard::itemDragEnter(const SourceDetails&) { setDropHighlight(true); }

// The highlight now goes out on every path there is, including the one that used to leak: JUCE
// calls this when the drag leaves, when it is dropped elsewhere, and when the drag image dies
// with the window that started it. The editor used to have to remember to clear it by hand.
void ChordRefCard::itemDragExit(const SourceDetails&) { setDropHighlight(false); }

void ChordRefCard::itemDropped(const SourceDetails& details)
{
    setDropHighlight(false);
    auto* p = chorddrag::chordBeingDragged(details);
    if (p == nullptr || p->from == chorddrag::Payload::From::liveCard)
        return;

    setChord(p->chord);
    // Taken, so a pad that came from the strip is not cleared behind it - but never *consumed*,
    // so a tray candidate stays in its cell. A reference is a copy of a chord you like, and
    // charging the tray a card for keeping one would be backwards.
    p->taken = true;
}

void ChordRefCard::mouseEnter(const juce::MouseEvent&) { hovered = true; repaint(); }
void ChordRefCard::mouseExit(const juce::MouseEvent&) { hovered = false; repaint(); }

void ChordRefCard::mouseDown(const juce::MouseEvent& e)
{
    if (! hasChord())
        return;
    // Left click auditions, exactly as a tray card does, and there is no right-click path here
    // at all: the three things you can do to a reference chord are buttons beside it.
    downPos = e.position; // ...and where it landed, so mouseDrag can tell this from a commit
    dragging = false;
    pressed = true;
    gen.auditionChord(held.notes);
    repaint();
}

void ChordRefCard::mouseDrag(const juce::MouseEvent& e)
{
    if (! hasChord() || dragging)
        return;

    if (e.position.getDistanceFrom(downPos) > 6.0f)
    {
        // A press that becomes a drag is a commit, not an audition - the same handover the tray
        // makes, and the reason the press is allowed to sound at all is that letting go without
        // moving is still the commonest thing you do to this card.
        gen.stopAudition();
        beginDrag(e);
    }
}

// The tray's own beginDrag with one word changed. Kept as a second copy rather than hoisted:
// they share four lines of JUCE boilerplate and disagree about the only thing that matters
// (which cell is being carried, and whether it survives), so a shared helper would take both
// answers as parameters and save nothing.
void ChordRefCard::beginDrag(const juce::MouseEvent& e)
{
    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (container == nullptr)
    {
        jassertfalse; // ChordGenPanel is meant to be one, exactly as it is for the tray
        return;
    }

    const auto box = getLocalBounds();
    const auto ghost = createComponentSnapshot(box, true, 2.0f).convertedToFormat(juce::Image::ARGB);
    const auto grab = box.getTopLeft() - downPos.roundToInt(); // JUCE negates this into the image

    inFlight = new chorddrag::Payload(chorddrag::Payload::From::refCard, -1, held);
    dragging = true;
    repaint();

    container->startDragging(juce::var(inFlight.get()), this, juce::ScaledImage(ghost, 2.0),
                             /*allowDraggingToExternalWindows*/ true, &grab, &e.source);
}

void ChordRefCard::mouseUp(const juce::MouseEvent&)
{
    pressed = false;
    if (dragging)
    {
        // Nothing to undo whichever way it went: this card is a copy source, so a drop that
        // nobody took is simply a drag that did not happen. The payload still has to be let go
        // of a turn later rather than here - see whenDragSettles for why mouseUp is too early
        // to know anything, and ChordPads::mouseUp for the case where the answer matters.
        chorddrag::whenDragSettles(*this, inFlight,
                                   [](ChordRefCard& card, const chorddrag::Payload&)
                                   {
                                       card.inFlight = nullptr;
                                       card.repaint();
                                   });
        dragging = false;
    }
    repaint(); // the 800 ms timer owns the release, so letting go is not a note-off
}

void ChordRefCard::paint(juce::Graphics& g)
{
    const juce::Colour inkOnAccent { 0xff07272c };
    const auto b = getLocalBounds().toFloat().reduced(1.0f);

    if (! hasChord())
    {
        // An empty reference reads as a well and says what it is for. It is the one card in this
        // window that has to explain itself: the tray is obviously chords, this is obviously
        // nothing until something is in it.
        g.setColour(skin::well.withAlpha(0.55f));
        g.fillRoundedRectangle(b, kRadius);
        g.setColour(juce::Colours::white.withAlpha(dropHighlight ? 0.25f : 0.05f));
        g.drawRoundedRectangle(b, kRadius, 1.0f);
        g.setColour(skin::textFaint);
        g.setFont(skin::ui(10.5f));
        g.drawText("drag a chord here", b.reduced(4.0f), juce::Justification::centred, true);
        if (dropHighlight)
            skin::glowRect(g, b, kRadius, skin::accentOf(*this).hot);
        return;
    }

    skin::raisedFill(g, b, kRadius, juce::Colour(0xff272b32), juce::Colour(0xff1e2126));
    if (pressed)
    {
        g.setGradientFill({ skin::accentOf(*this).hot, 0.0f, b.getY(),
                            skin::accentOf(*this).base, 0.0f, b.getBottom(), false });
        g.fillRoundedRectangle(b, kRadius);
        skin::glowRect(g, b, kRadius, skin::accentOf(*this).base);
    }
    else if (hovered)
    {
        g.setColour(skin::accentOf(*this).base.withAlpha(0.10f));
        g.fillRoundedRectangle(b, kRadius);
    }

    auto text = b.reduced(4.0f, 3.0f);
    const auto noteLine = text.removeFromBottom(kNoteLineH);
    g.setColour(pressed ? inkOnAccent : skin::text);
    g.setFont(skin::uiSemi(13.5f));
    g.drawText(held.name, text, juce::Justification::centred, true);
    g.setColour(pressed ? inkOnAccent.withAlpha(0.75f) : skin::textDim);
    g.setFont(skin::micro(9.0f));
    g.drawText(noteListText(held.notes), noteLine.toNearestInt(), juce::Justification::centred, true);

    if (dropHighlight)
        skin::glowRect(g, b, kRadius, skin::accentOf(*this).hot);
}
} // namespace keys
