#include "ArpPanel.h"
#include "KeysLookAndFeel.h"
#include "ComboMenu.h"
#include <okstudio/MouseOnly.h>
#include <cmath>

namespace keys
{
namespace
{
    // Micro-caps captions in the skin's voice. Colours here are the compile-time
    // skin tokens, deliberately not findColour(): ArpPanel is constructed before it
    // is parented into KeysEditor, so a colour snapshotted via findColour() would
    // read the JUCE default LookAndFeel, not the editor's skin.
    void styleLabel(juce::Label& l, const juce::String& text)
    {
        l.setText(text.toUpperCase(), juce::dontSendNotification);
        l.setFont(skin::micro(10.0f));
        l.setColour(juce::Label::textColourId, skin::textDim);
    }

    constexpr const char* clockDivNames[3] = { "1x", "1/2", "1/4" };

    // One control line inside a macro card: 40 px of knob plus its 15 px readout, the same
    // spend the old single-line row made. Declared up here rather than with the other
    // macro-view heights further down, because MacroRow's own resized() is defined above that
    // block and needs it; the block down there builds arpMacroCard out of it, and an
    // anonymous namespace is one namespace however many times it is opened.
    constexpr int arpMacroLine = 55;
    // ...and the card's other strips: the LINE A / LINE B caption rule at the top (the same
    // punched-through-the-frame caption the band's groups draw - each card wears its own name
    // since 2026-08-02, Owen: "we need a bit more clear delineation between the two
    // arpeggiators"), 11 px heading strips for the RATE / SHAPE and knob columns, and the
    // rate's Dot / Tuplet / Anchor at the full 34 px hit height.
    constexpr int arpMacroCap = 18;
    constexpr int arpMacroMods = 34;
    constexpr int arpMacroHeads = 11;
    // The widest the Shape combo is allowed to get (2026-08-21). "Fingered Bottom" is the
    // longest of the fifteen entries and measures about 105 px in the popup font, so this is
    // that plus its chevron, its own insets and room to spare - and it is a *cap*, so above
    // the floor the row simply stops giving the combo more.
    //
    // It is **not** true that a narrow window shrinks it exactly as it used to, and the
    // comment said so for an afternoon: the dice's 34 px cell and its 14 px gap come out of
    // the same row, so at any width where the cap is not biting, Shape is 48 px narrower than
    // before. That is affordable only because `minMacroWidth()` guarantees the row enough
    // width to seat the knob strip, which is wider than this line needs.
    //
    // **The "~166 px at the floor" figure that stood here was carried over from before the
    // dice and never re-derived** - it is nearer 151 at `minPanelWidth()`, and less again at
    // `minMacroWidth()`. It still clears the longest name, so nothing was broken, but a number
    // asserted in a comment is a number that goes stale silently. `LayoutTests` measures the
    // combo against its own longest entry at both floors now, so appending a shape name or
    // adding a control to this row fails a test instead of quietly ellipsising a label.
    // Narrow the floor and this is still the second thing that breaks, after the knobs.
    constexpr int arpMacroShapeMaxW = 170;
    // The ring a RangeKnob draws around its face, and therefore what the knob row is taller
    // than the row above it by (2026-08-03). The row grows rather than the faces shrinking:
    // squeezing a ring out of the space the knob already had would have taken those two under
    // the kit's 48 px advice and every other knob with them, which is the trap this file has
    // logged twice already. Height is the cheap axis in this view.
    constexpr int arpRingPx = 8;
    constexpr int arpMacroKnobLine = arpMacroLine + 2 * arpRingPx;
    // The narrowest a macro knob may be drawn, and therefore what sets the whole view's
    // minimum width. It is over the 34 px mouse-only floor with a little to spare, which is
    // the point: a knob at exactly 34 has no room for the ring inset a range knob adds.
    // `ArpPanel::minMacroWidth()` below turns this into the panel width the view needs, so
    // the floor and the layout are one number rather than two that can drift - which is
    // exactly what happened when STRAY made the strip nine knobs and only the *docked* case
    // was re-measured (2026-08-21).
    constexpr int arpMacroKnobMinW = 38;
    // The gap between the two cards in a row, and the insets the panel puts around them.
    // Named because minMacroWidth() and the layout both need them and must agree - and
    // **substituted at every layout site**, not merely written down beside one. Naming a
    // literal that the layout still spells out by hand buys nothing: the derivation then
    // agrees with a comment rather than with the code, which is the `buildLaneRow` versus
    // `laneRange` trap this file already pays for once. Change one of these and the floor,
    // the card and the row all move together.
    constexpr int arpMacroCardGap = 12;
    constexpr int arpMacroAreaInset = 12; // the panel's inset inside cardBounds()
    constexpr int arpMacroCardInset = 8;  // the card's own inset inside the panel
    constexpr int arpMacroRowInset = 10;  // MacroRow's side inset inside the card

    // The macro card's bottom strip: five fixed cells left to right, a gap after each, and the
    // chord readout at the right end. Named rather than written into the setBounds calls so
    // minMacroWidth() can ask what the *row* needs and not only what the knob strip needs
    // (2026-09-01, when Legato made five). Before that the row's width was asserted in a
    // comment that was two floors out of date - measured against the docked window's card and
    // never the detached Arp window's, where the card is exactly the knob strip wide and
    // Details had been ten pixels short since the dice arrived, with nothing on screen to say
    // so. A fifth chip there would have laid Details out at zero.
    constexpr int arpMacroModDot = 62;
    // 92 where the tick box had 66: a combo showing "5-tuplet" needs the word plus a chevron.
    constexpr int arpMacroModTuplet = 92;
    constexpr int arpMacroModAnchor = 84;
    constexpr int arpMacroModLegato = 84; // "Legato" is "Anchor"'s length: the same cell
    constexpr int arpMacroModDetails = 76;
    constexpr int arpMacroModGap = 8;
    constexpr int arpMacroChordW = 64;    // the held-chord readout; ellipsises gracefully
    constexpr int arpMacroChordGap = 6;
    constexpr int arpMacroModsW = arpMacroModDot + arpMacroModTuplet + arpMacroModAnchor
                                + arpMacroModLegato + arpMacroModDetails + 5 * arpMacroModGap
                                + arpMacroChordGap + arpMacroChordW;
    // What the *deep* pages need, which is more than the macro view (2026-08-21). Measured
    // rather than derived, and honestly so: the band groups share the width by weight and the
    // lane tabs divide what is left by their count, so there is no clean sum to write here the
    // way the knob strip has one - the number is where a Play-page band slider and a Draw-page
    // lane tab stop being starved. `LayoutTests` sweeps every view at `minPanelWidth()` and is
    // what keeps it true: lower this and those two starve again, which is how it was found.
    constexpr int arpDeepPageMinW = 970;
    // The harmony area's dropdown row (2026-08-19, second pass): a 34 px combo centred in it.
    // Its chance knob sits below it at arpMacroLine, one column per voice.
    //
    // 34, not the 26 it shipped at for a few hours. CLAUDE.md's floor is an invariant and names
    // no exceptions ("a check box, a stepper's -/+ pair and a caption-row button are targets
    // exactly as a TextButton is"), and this is a brand-new target on a card whose height budget
    // was being rewritten in the same stroke, so the eight pixels were there for the asking. It
    // is also the only route to the two-column interval popup, so a missed click here is a
    // missed feature rather than a cosmetic annoyance.
    constexpr int arpMacroHarmCombo = 38;

    // Show or hide a whole group of controls in one line. The parameter type is what makes it
    // work: a braced list of mixed component types cannot deduce its own element type, but it
    // converts to this one happily, so the call sites stay readable.
    void setAllVisible(std::initializer_list<juce::Component*> cs, bool visible)
    {
        for (auto* c : cs)
            c->setVisible(visible);
    }
} // namespace

// ---------------------------------------------------------------------------
// LaneGrid

ArpPanel::LaneGrid::LaneGrid(KeysProcessor& p, ArpPanel& o, ArpEngine::Lane l, int lo, int hi)
    : processor(p), owner(o), lane(l), loVal(lo), hiVal(hi)
{
    okstudio::ui::makeMouseOnly(*this);
}

namespace
{
    // The cell a lane is reading right now, or -1 when the line is not running. The engine
    // publishes the step index it last read a lane by; the arithmetic that turns that into a
    // cell is the engine's own (ArpEngine::laneStepIndex), called here rather than copied, so
    // a playhead cannot light a cell the engine did not look at.
    // Cthulhu draws each per-step shape as a little contour of dashes rather than a word (its
    // manual p24), and that is most of why its Note graph reads at a glance: the picture of an
    // arpeggio going up *is* the instruction. Six dashes at these heights, 0 at the bottom.
    // The pairs that differ only in whether the extremes repeat are drawn so you can see which
    // is which - Up-Down turns at a single apex, Up & Down sits on its apex for two dashes.
    const float shapeGlyphs[8][6] = {
        { 0.00f, 0.20f, 0.40f, 0.60f, 0.80f, 1.00f }, // up
        { 1.00f, 0.80f, 0.60f, 0.40f, 0.20f, 0.00f }, // down
        { 0.00f, 0.50f, 1.00f, 0.50f, 0.00f, 0.50f }, // up/down, extremes once
        { 1.00f, 0.50f, 0.00f, 0.50f, 1.00f, 0.50f }, // down/up, extremes once
        { 0.00f, 0.50f, 1.00f, 1.00f, 0.50f, 0.00f }, // up and down, extremes twice
        { 1.00f, 0.50f, 0.00f, 0.00f, 0.50f, 1.00f }, // down and up, extremes twice
        { 1.00f, 0.00f, 0.50f, 0.00f, 1.00f, 0.00f }, // fingered bottom
        { 0.00f, 1.00f, 0.50f, 1.00f, 0.00f, 1.00f }, // fingered top
    };

    void drawShapeGlyph(juce::Graphics& g, juce::Rectangle<float> box, int shapeIndex)
    {
        const auto* h = shapeGlyphs[(size_t) juce::jlimit(0, 7, shapeIndex)];
        const float dashW = juce::jmax(2.0f, box.getWidth() / 7.5f);
        const float dashH = juce::jmax(1.5f, box.getHeight() * 0.16f);
        const float travel = juce::jmax(0.0f, box.getHeight() - dashH);
        for (int i = 0; i < 6; ++i)
        {
            const float x = box.getX() + (box.getWidth() - dashW) * ((float) i / 5.0f);
            const float y = box.getBottom() - dashH - travel * h[i];
            g.fillRect(x, y, dashW, dashH);
        }
    }

    int lanePlayhead(KeysProcessor& processor, int line, int lane)
    {
        const auto& eng = processor.arpLine(line);
        const long long rel = eng.uiRelStep.load(std::memory_order_relaxed);
        if (rel < 0)
            return -1;
        // A lane that is switched off has no playhead, for the same reason a stopped line has
        // none: laneValue returns the lane's default and never reads a cell, so lighting a
        // column would show the engine reading something it did not look at - the one thing
        // this function exists to prevent.
        if (eng.lanes.on[(size_t) lane].load(std::memory_order_relaxed) == 0)
            return -1;
        // Mute walks the Note lane's shape (see ArpEngine::laneValue): it is that lane's
        // companion, and the MUTE strip is drawn against the Note lane's cells. Reading its
        // own shape here would light a different column than the engine reads.
        const int shapeLane = lane == ArpEngine::laneMute ? (int) ArpEngine::laneNote : lane;
        return ArpEngine::laneStepIndex(rel, eng.uiOffset.load(std::memory_order_relaxed),
                                        eng.lanes.shapeOf(shapeLane));
    }
} // namespace

int ArpPanel::LaneGrid::currentLength() const
{
    return juce::jlimit(1, ArpEngine::maxSteps,
                        processor.arpLine(owner.editLine()).lanes.length[(size_t) lane].load(std::memory_order_relaxed));
}

int ArpPanel::LaneGrid::stepAtX(float x) const
{
    const int length = currentLength();
    const float w = (float) getWidth();
    if (w <= 0.0f)
        return 0;
    return juce::jlimit(0, length - 1, (int) (x / (w / (float) length)));
}

int ArpPanel::LaneGrid::valueAtY(float y) const
{
    const float h = (float) getHeight();
    const float frac = h > 0.0f ? 1.0f - juce::jlimit(0.0f, 1.0f, y / h) : 0.0f;
    return juce::jlimit(loVal, hiVal, loVal + juce::roundToInt(frac * (float) (hiVal - loVal)));
}

// `step` < 0 means "work it out from x" - which only the initial press does. Every drag after
// that passes the step the press landed on, so a drag edits **one** step and its horizontal
// travel is ignored (2026-08-14, Owen: "when you're drawing, I don't want you to be able to
// jump from step to step. I just want it to be for that one when you're moving up and down").
//
// Painting across steps is the right gesture for a mute row, where the value is a toggle and a
// swipe means "all of these" - and MuteRow still does exactly that. It is the wrong one here,
// where the value is a height: the pointer has to travel vertically to set it, and any drift
// sideways on the way rewrote a neighbour you had already placed.
void ArpPanel::LaneGrid::paintStepFromMouse(const juce::MouseEvent& e, int step)
{
    if (step < 0)
        step = stepAtX(e.position.x);
    const int value = valueAtY(e.position.y);
    processor.arpLine(owner.editLine()).lanes.value[(size_t) lane][(size_t) step].store(value, std::memory_order_relaxed);
    cursorPos = e.position;
    cursorValue = value;
    repaint();
}

void ArpPanel::LaneGrid::mouseDown(const juce::MouseEvent& e)
{
    // In Select mode a drag marks a span rather than painting one. The anchor is where the
    // press landed and the far end follows the mouse, so a span can be drawn either way round;
    // owner.selFrom/selTo are normalised on every update rather than at the end, since Roll can
    // be clicked mid-gesture.
    if (owner.selectMode)
    {
        dragging = true;
        selAnchor = stepAtX(e.position.x);
        owner.selFrom = owner.selTo = selAnchor;
        owner.repaint();
        return;
    }
    dragging = true;
    paintStep = stepAtX(e.position.x); // locked for the rest of this gesture
    // The press is the whole gesture's undo entry - mouseDrag below deliberately does not push,
    // or one stroke across a lane would be thirty entries and bury everything under it.
    processor.pushUndo("Draw lane", KeysProcessor::UndoScope::arp);
    paintStepFromMouse(e, paintStep);
}

void ArpPanel::LaneGrid::mouseDrag(const juce::MouseEvent& e)
{
    if (owner.selectMode)
    {
        const int s = stepAtX(e.position.x);
        owner.selFrom = juce::jmin(selAnchor, s);
        owner.selTo = juce::jmax(selAnchor, s);
        owner.repaint();
        return;
    }
    paintStepFromMouse(e, paintStep); // the step the press landed on, whatever x does now
}

void ArpPanel::LaneGrid::mouseUp(const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

juce::String ArpPanel::LaneGrid::noteNameFor(int value) const
{
    if (lane != ArpEngine::laneNote || value < 1 || value > 8)
        return {}; // Prev / Hi / Low / Rnd name no fixed pitch, and neither does a rest

    // Through the engine's own snapshot rather than a load-count-then-index loop of our own:
    // the acquire/release handshake with buildSequence is stated once, there, and the MCP
    // get_state tool reads the same sequence the same way.
    std::array<int, ArpEngine::maxHeld * 4> pitches {};
    const int n = processor.arpLine(owner.editLine()).uiSequence(pitches);
    if (n <= 0)
        return {}; // nothing held: the index names nothing, so it stays a number

    // The same wrap the engine applies to a fixed index (fireStep: `(noteVal - 1) % seqCount`),
    // so a lane drawn past the end of a small chord says the note it will actually play.
    return juce::MidiMessage::getMidiNoteName(pitches[(size_t) ((value - 1) % n)], true, true, 3);
}

juce::String ArpPanel::LaneGrid::cellText(int value) const
{
    if (lane == ArpEngine::laneNote)
    {
        if (value <= ArpEngine::noteRest)
            return "X";
        if (value == ArpEngine::noteFollow)
            return {}; // drawn as a dot instead, see paint()
        // The four modes above the fixed indices (Kirnu's ORDER lane). One letter each: a cell
        // is ~40 px at 32 steps, so "Prev" does not fit and an ellipsised word says less than
        // an initial does. The tooltip on the tab carries the words.
        switch (value)
        {
            case ArpEngine::notePrev: return "P";
            case ArpEngine::noteHi:   return "H";
            case ArpEngine::noteLow:  return "L";
            case ArpEngine::noteRnd:  return "R";
            default: break;
        }
        if (value >= ArpEngine::noteShapeFirst && value <= ArpEngine::noteShapeLast)
            return {}; // drawn as its contour instead; see drawShapeGlyph
    }
    // Harmony and Chord are off at zero rather than centred on it, so a row of noughts would
    // read as data where it means "nothing here". The dot the note lane already uses says it
    // better. Late and Transpose keep their zeroes: those two are positions on a range.
    if (value == 0 && (lane == ArpEngine::laneHarmony || lane == ArpEngine::laneChord))
        return {};
    return juce::String(value);
}

void ArpPanel::LaneGrid::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    // Inset well with step bars: accent gradient bodies capped by a hot top line.
    g.setColour(skin::well);
    g.fillRect(b);
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRect(b.getX(), b.getY(), b.getWidth(), 1.5f);

    const int length = currentLength();
    const float cellW = length > 0 ? b.getWidth() / (float) length : b.getWidth();

    auto& lanes = processor.arpLine(owner.editLine()).lanes;
    const auto shape = lanes.shapeOf((int) lane);
    const int playhead = lanePlayhead(processor, owner.editLine(), (int) lane);
    // The window the lane actually walks. Clamped here the same way the engine clamps it, and
    // read back off the shape rather than the raw properties so the two cannot disagree about
    // what a loopTo past the end means.
    int loopFrom = juce::jlimit(0, length - 1, shape.loopFrom);
    int loopTo = juce::jlimit(0, length - 1, shape.loopTo);
    if (loopTo < loopFrom)
        std::swap(loopFrom, loopTo);

    for (int i = 0; i < length; ++i)
    {
        const int value = juce::jlimit(loVal, hiVal,
                                       processor.arpLine(owner.editLine()).lanes.value[(size_t) lane][(size_t) i].load(std::memory_order_relaxed));
        auto cell = juce::Rectangle<float>(b.getX() + cellW * (float) i, b.getY(), cellW, b.getHeight());

        g.setColour(juce::Colours::white.withAlpha(0.045f));
        g.drawVerticalLine((int) cell.getX(), b.getY(), b.getBottom());

        const auto bar = cell.reduced(1.5f);
        const float frac = hiVal > loVal ? (float) (value - loVal) / (float) (hiVal - loVal) : 0.0f;

        // **The Note lane draws a marker at a height; every other lane draws a bar up to it**
        // (2026-08-18, from Cthulhu's Note graph). The difference is what the value *means*: a
        // Velocity of 120 is a magnitude and a column filled to 120 says so, but a Note of 5 is
        // a name, and a column filled to 5 reads as "more than 4" - which is not a thing a
        // chord entry can be. It is also what makes room for the shape glyphs: eight of the
        // values up here are pictures, and a picture on top of a full-height fill is neither.
        const bool markerLane = (lane == ArpEngine::laneNote);
        juce::Rectangle<float> marker;
        const bool isShape = value >= ArpEngine::noteShapeFirst && value <= ArpEngine::noteShapeLast;
        if (markerLane)
        {
            // A shape marker is taller than a note marker, because it has to hold a picture
            // rather than a digit - Cthulhu draws the contour *inside* the block, and dashes
            // spilling out of the top and bottom of a slab read as noise rather than as a shape.
            const float markerH = isShape ? juce::jmax(22.0f, bar.getHeight() * 0.26f)
                                          : juce::jmax(9.0f, bar.getHeight() * 0.12f);
            const float travel = juce::jmax(0.0f, bar.getHeight() - markerH);
            marker = { bar.getX(), bar.getBottom() - markerH - travel * frac, bar.getWidth(), markerH };
            if (value > ArpEngine::noteRest)
            {
                g.setGradientFill({ skin::accentOf(*this).base.withAlpha(0.7f), 0.0f, marker.getY(),
                                    skin::accentOf(*this).deep.withAlpha(0.55f), 0.0f, marker.getBottom(), false });
                g.fillRoundedRectangle(marker, 2.0f);
                g.setColour(skin::accentOf(*this).hot.withAlpha(0.9f));
                g.fillRect(marker.getX() + 1.0f, marker.getY(), marker.getWidth() - 2.0f, 1.5f);
            }
        }
        else
        {
            const auto filled = bar.withTop(bar.getBottom() - bar.getHeight() * frac);
            if (filled.getHeight() > 0.5f)
            {
                g.setGradientFill({ skin::accentOf(*this).base.withAlpha(0.55f), 0.0f, filled.getY(),
                                    skin::accentOf(*this).deep.withAlpha(0.4f), 0.0f, bar.getBottom(), false });
                g.fillRect(filled);
                g.setColour(skin::accentOf(*this).hot.withAlpha(0.9f));
                g.fillRect(filled.getX(), filled.getY(), filled.getWidth(), 1.5f);
            }
        }

        const bool inLoop = i >= loopFrom && i <= loopTo;

        // A shape value is drawn as its contour, inside the marker's own column rather than in
        // the marker: the glyph needs vertical room to be a picture at all, and the marker is
        // nine pixels tall.
        if (markerLane && isShape)
        {
            g.setColour(skin::text.withAlpha(0.95f));
            drawShapeGlyph(g, marker.reduced(3.0f, 4.0f), value - ArpEngine::noteShapeFirst);
        }
        else if (lane == ArpEngine::laneNote && value == 0)
        {
            const float r = juce::jmin(5.0f, cell.getWidth() * 0.22f);
            g.setColour(skin::text);
            g.fillEllipse(marker.getCentreX() - r, marker.getCentreY() - r, r * 2.0f, r * 2.0f);
        }
        else if (cell.getWidth() > 16.0f)
        {
            // The note name where there is room for one, the drawn number where there is not.
            // A Note lane cell saying "3" is an index into a sorted chord nobody can see; the
            // engine publishes what that index currently names, so the lane can say "E3".
            const auto named = cell.getWidth() > 28.0f ? noteNameFor(value) : juce::String();
            const auto txt = named.isNotEmpty() ? named : cellText(value);
            if (txt.isNotEmpty())
            {
                g.setColour(lane == ArpEngine::laneNote && value == -1 ? skin::textFaint : skin::text);
                g.setFont(skin::ui(named.isNotEmpty() ? 10.0f : 11.0f));
                // In the Note lane the text belongs *in* the marker, which is where the value
                // is; everywhere else the bar is the value and the cell is where it reads.
                g.drawText(txt, (markerLane ? marker.expanded(0.0f, 2.0f) : cell).toNearestInt(),
                           juce::Justification::centred);
            }
        }

        // Outside the loop window the cell is still drawn - it is still yours, and you can
        // still edit it - but it is dimmed, because the difference between "a step you drew"
        // and "a step that plays" is the whole point of having a window at all.
        //
        // **Last, over everything the cell drew.** It ran before the marker's contour and the
        // note name, which are painted at full alpha, so an excluded step came out with bright
        // text and a bright glyph sitting on a dimmed background - reading as brighter than the
        // steps that actually play, which is the opposite of what the dim is for, and looking
        // like a rendering fault rather than a state.
        if (! inLoop)
        {
            g.setColour(skin::bgBot.withAlpha(0.55f));
            g.fillRect(cell);
        }
    }

    // The playhead, over the bars and under the selection. Its own column rather than a line:
    // a hairline between two cells belongs to neither of them, and with per-lane lengths the
    // question this answers is "which cell", not "how far along".
    if (playhead >= 0 && playhead < length)
    {
        // In the edited line's own colour (2026-08-19): the playhead is the mark that says
        // which machine is walking, so it wears the same tint as that line's card and its
        // letter on the bar.
        const auto ph = skin::lineAccent(owner.editLine()).base;
        const auto col = juce::Rectangle<float>(b.getX() + cellW * (float) playhead, b.getY(),
                                                cellW, b.getHeight());
        g.setColour(ph.withAlpha(0.16f));
        g.fillRect(col);
        g.setColour(ph.withAlpha(0.85f));
        g.fillRect(col.getX(), b.getY(), 1.5f, b.getHeight());
        g.fillRect(col.getRight() - 1.5f, b.getY(), 1.5f, b.getHeight());
    }

    // A lane that is switched off keeps its drawing on screen and stops claiming to be heard.
    // Scrimmed rather than emptied, and never disabled: you draw on a lane before switching it
    // on at least as often as after, and a disabled component takes no mouse events at all.
    if (lanes.on[(size_t) lane].load(std::memory_order_relaxed) == 0)
    {
        g.setColour(skin::bgBot.withAlpha(0.62f));
        g.fillRect(b);
    }

    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRect(b, 1.0f);

    if (dragging)
    {
        const auto txt = juce::String(cursorValue);
        const juce::Font f = skin::uiSemi(12.0f);
        const int tw = f.getStringWidth(txt) + 14;
        auto box = juce::Rectangle<int>(juce::roundToInt(cursorPos.x) - tw / 2,
                                        juce::roundToInt(cursorPos.y) - 28, tw, 20)
                      .constrainedWithin(getLocalBounds());
        g.setColour(juce::Colour(0xff1e2127));
        g.fillRoundedRectangle(box.toFloat(), 4.0f);
        g.setColour(skin::accentOf(*this).base);
        g.drawRoundedRectangle(box.toFloat(), 4.0f, 1.0f);
        g.setColour(skin::text);
        g.setFont(f);
        g.drawText(txt, box, juce::Justification::centred);
    }

    // The Select span, drawn last so it sits over the bars. A tinted wash plus a bright edge at
    // each end: the wash says "these steps", and the edges say where it starts and stops, which
    // the wash alone does not once the span reaches the grid's own edge.
    if (owner.selectMode && owner.selFrom >= 0)
    {
        const int len = currentLength();
        const float cellW = len > 0 ? (float) getWidth() / (float) len : (float) getWidth();
        const int lo = juce::jlimit(0, len - 1, owner.selFrom);
        const int hi = juce::jlimit(lo, len - 1, owner.selTo);
        const auto span = juce::Rectangle<float>(cellW * (float) lo, 0.0f,
                                                 cellW * (float) (hi - lo + 1), (float) getHeight());
        const auto a = skin::accentOf(*this);
        g.setColour(a.base.withAlpha(0.16f));
        g.fillRect(span);
        g.setColour(a.base.withAlpha(0.75f));
        g.fillRect(span.getX(), 0.0f, 1.5f, (float) getHeight());
        g.fillRect(span.getRight() - 1.5f, 0.0f, 1.5f, (float) getHeight());
    }
}

// ---------------------------------------------------------------------------
// MuteRow

ArpPanel::MuteRow::MuteRow(KeysProcessor& p, const ArpPanel& o) : processor(p), owner(o)
{
    okstudio::ui::makeMouseOnly(*this);
}

// **The mute lane is the Note lane's companion, not a polymetric lane of its own.** It reads
// and writes at the Note lane's length, and syncs its own to match (2026-08-14) - the engine
// wraps every lane read by that lane's own length, so if the two ever disagreed a mute drawn
// at step 20 of a 32-step pattern would be read back modulo 8 and silence the wrong step. It
// has no tab and no STEPS control, so there is nowhere for a user to set it and nothing to
// gain from letting it differ.
void ArpPanel::LaneTab::paintButton(juce::Graphics& g, bool over, bool down)
{
    juce::TextButton::paintButton(g, over, down);

    const auto b = getLocalBounds().toFloat();
    // A dot in the top-right corner when the lane holds anything but its default. Kirnu's own
    // "this control has input values" mark, and it is what makes eleven hidden lanes readable
    // without opening them: the tabs with a dot are the ones doing something.
    if (laneHasData)
    {
        const float r = 2.5f;
        g.setColour(skin::accentOf(*this).hot.withAlpha(laneOn ? 0.95f : 0.4f));
        g.fillEllipse(b.getRight() - 7.0f - r, b.getY() + 6.0f, r * 2.0f, r * 2.0f);
    }

    // Struck through when the lane is switched off, rather than greyed: the tab still has to
    // be readable and clickable - switching a lane back on means selecting it first.
    if (! laneOn)
    {
        g.setColour(skin::textFaint);
        g.fillRect(b.getX() + 8.0f, b.getCentreY() - 0.5f, b.getWidth() - 16.0f, 1.0f);
    }
}

int ArpPanel::MuteRow::currentLength() const
{
    auto& lanes = processor.arpLine(owner.editLine()).lanes;
    const int len = juce::jlimit(1, ArpEngine::maxSteps,
                                 lanes.length[(size_t) ArpEngine::laneNote].load(std::memory_order_relaxed));
    // Cheap and idempotent, and this is the one place that knows both numbers. It also repairs
    // a session saved before the lane existed, which comes back at the default 8 while the
    // Note lane may be anything.
    if (lanes.length[(size_t) ArpEngine::laneMute].load(std::memory_order_relaxed) != len)
        lanes.length[(size_t) ArpEngine::laneMute].store(len, std::memory_order_relaxed);
    return len;
}

int ArpPanel::MuteRow::stepAtX(float x) const
{
    const int length = currentLength();
    const float w = (float) getWidth();
    if (w <= 0.0f)
        return 0;
    return juce::jlimit(0, length - 1, (int) (x / (w / (float) length)));
}

void ArpPanel::MuteRow::applyAtX(float x)
{
    const int step = stepAtX(x);
    processor.arpLine(owner.editLine()).lanes.value[(size_t) ArpEngine::laneMute][(size_t) step].store(paintValue, std::memory_order_relaxed);
    repaint();
}

void ArpPanel::MuteRow::mouseDown(const juce::MouseEvent& e)
{
    const int step = stepAtX(e.position.x);
    // Its own lane since 2026-08-14, where it used to toggle the Note lane between -1 and 0
    // and so threw away whatever that step held. 1 is muted, 0 is heard.
    const int current = processor.arpLine(owner.editLine()).lanes.value[(size_t) ArpEngine::laneMute][(size_t) step].load(std::memory_order_relaxed);
    paintValue = (current > 0) ? 0 : 1; // toggle, then paint every step the drag crosses to match
    processor.pushUndo("Mute steps", KeysProcessor::UndoScope::arp); // once for the whole swipe
    dragging = true;
    applyAtX(e.position.x);
}

void ArpPanel::MuteRow::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging)
        applyAtX(e.position.x);
}

// --- LoopBar -------------------------------------------------------------------------------

ArpPanel::LoopBar::LoopBar(KeysProcessor& p, const ArpPanel& o) : processor(p), owner(o)
{
    okstudio::ui::makeMouseOnly(*this);
    setTitle("Lane loop");
    setTooltip("The steps this lane walks. Click to move the nearer end, or drag it.");
}

int ArpPanel::LoopBar::stepAtX(float x) const
{
    auto& lanes = processor.arpLine(owner.editLine()).lanes;
    const int len = juce::jlimit(1, ArpEngine::maxSteps,
                                 lanes.length[(size_t) owner.selectedLaneIndex()].load(std::memory_order_relaxed));
    const float cellW = (float) getWidth() / (float) len;
    return juce::jlimit(0, len - 1, (int) (x / juce::jmax(1.0f, cellW)));
}

void ArpPanel::LoopBar::moveNearestHandle(float x)
{
    auto& lanes = processor.arpLine(owner.editLine()).lanes;
    const auto li = (size_t) owner.selectedLaneIndex();
    const int len = juce::jlimit(1, ArpEngine::maxSteps, lanes.length[li].load(std::memory_order_relaxed));
    const int step = stepAtX(x);
    const int from = juce::jlimit(0, len - 1, lanes.loopFrom[li].load(std::memory_order_relaxed));
    const int to = juce::jlimit(0, len - 1, lanes.loopTo[li].load(std::memory_order_relaxed));

    // Kirnu moves the far handle with the right button when the click lands inside the window
    // (its manual p11). Keys cannot: right-click-only paths are a closed list. The nearer
    // handle, always, is the whole rule here - inside the window it shrinks, outside it grows,
    // and either way one left click is the entire gesture.
    if (grabbed < 0)
        grabbed = std::abs(step - from) <= std::abs(step - to) ? 0 : 1;

    const int newFrom = grabbed == 0 ? juce::jmin(step, to) : from;
    const int newTo   = grabbed == 0 ? to : juce::jmax(step, from);

    // **Written to every lane when Link is on**, the way nudgeLength and cycleClockDiv already
    // are. Link on means the lanes share one grid, and a window is part of which grid that is -
    // enforceLinkedLengths copies the Note lane's window over all of them on the 10 Hz tick, so
    // a drag on any other lane's bar was undone within 100 ms and 12 of the 13 tabbed lanes had
    // a control that visibly did nothing. Link off is polymeter and is left alone, as ever.
    const bool linked = processor.apvts.getRawParameterValue(
                            owner.paramId(KeysProcessor::apLinkLanes))->load() > 0.5f;
    const int lo = linked ? 0 : (int) li;
    const int hi = linked ? ArpEngine::numLanes - 1 : (int) li;
    for (int i = lo; i <= hi; ++i)
    {
        lanes.loopFrom[(size_t) i].store(newFrom, std::memory_order_relaxed);
        lanes.loopTo[(size_t) i].store(newTo, std::memory_order_relaxed);
    }
    repaint();
    if (auto* g = getParentComponent())
        g->repaint(); // the grid dims what the window leaves out
}

void ArpPanel::LoopBar::mouseDown(const juce::MouseEvent& e)
{
    grabbed = -1;
    // The press is the whole gesture's undo entry, the same rule LaneGrid and MuteRow follow -
    // a drag along the bar would otherwise be one entry per step crossed. It has to push at
    // all because loopFrom/loopTo ride the arp tree, so arpToTree() snapshots them: without an
    // entry of its own, a window drag was silently reverted by the *next* Undo, which the user
    // aimed at whatever they had drawn before it.
    processor.pushUndo("Loop window", KeysProcessor::UndoScope::arp);
    moveNearestHandle(e.position.x);
}

void ArpPanel::LoopBar::mouseDrag(const juce::MouseEvent& e) { moveNearestHandle(e.position.x); }

void ArpPanel::LoopBar::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    auto& lanes = processor.arpLine(owner.editLine()).lanes;
    const auto li = (size_t) owner.selectedLaneIndex();
    const int len = juce::jlimit(1, ArpEngine::maxSteps, lanes.length[li].load(std::memory_order_relaxed));
    const float cellW = b.getWidth() / (float) len;
    int from = juce::jlimit(0, len - 1, lanes.loopFrom[li].load(std::memory_order_relaxed));
    int to = juce::jlimit(0, len - 1, lanes.loopTo[li].load(std::memory_order_relaxed));
    if (to < from)
        std::swap(from, to);

    g.setColour(skin::well);
    g.fillRect(b);

    const auto a = skin::accentOf(*this);
    const auto span = juce::Rectangle<float>(b.getX() + cellW * (float) from, b.getY(),
                                             cellW * (float) (to - from + 1), b.getHeight());
    g.setColour(a.base.withAlpha(0.55f));
    g.fillRect(span.reduced(0.0f, 4.0f));
    // The ends, drawn full height so the window has two grabbable-looking edges rather than one
    // bar that happens to stop somewhere.
    g.setColour(a.hot.withAlpha(0.95f));
    g.fillRect(span.getX(), b.getY(), 2.0f, b.getHeight());
    g.fillRect(span.getRight() - 2.0f, b.getY(), 2.0f, b.getHeight());

    // Cell divisions, so the bar reads against the grid above it rather than as a free slider.
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    for (int i = 1; i < len; ++i)
        g.drawVerticalLine((int) (b.getX() + cellW * (float) i), b.getY(), b.getBottom());
}

void ArpPanel::MuteRow::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    const int length = currentLength();
    const float cellW = length > 0 ? b.getWidth() / (float) length : b.getWidth();

    for (int i = 0; i < length; ++i)
    {
        const int value = processor.arpLine(owner.editLine()).lanes.value[(size_t) ArpEngine::laneMute][(size_t) i].load(std::memory_order_relaxed);
        const bool muted = value > 0;
        auto cell = juce::Rectangle<float>(b.getX() + cellW * (float) i, b.getY(), cellW, b.getHeight()).reduced(2.0f);

        if (muted)
        {
            g.setColour(skin::well);
            g.fillRoundedRectangle(cell, 3.0f);
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.drawRoundedRectangle(cell, 3.0f, 1.0f);
        }
        else
        {
            g.setGradientFill({ skin::accentOf(*this).base.withAlpha(0.5f), 0.0f, cell.getY(),
                                skin::accentOf(*this).deep.withAlpha(0.45f), 0.0f, cell.getBottom(), false });
            g.fillRoundedRectangle(cell, 3.0f);
            g.setColour(skin::accentOf(*this).hot.withAlpha(0.5f));
            g.fillRect(cell.getX() + 2.0f, cell.getY() + 1.0f, cell.getWidth() - 4.0f, 1.5f);
        }

        if (muted && cell.getWidth() > 14.0f)
        {
            g.setColour(skin::textFaint);
            g.setFont(skin::ui(11.0f));
            g.drawText("X", cell.toNearestInt(), juce::Justification::centred);
        }
    }

    // The Note lane's playhead, since this strip is the Note lane's companion and reads at the
    // Note lane's length. Drawn as an underline rather than the grid's column: these cells are
    // rounded pills with gaps between them, and a filled column would land on the gaps too.
    const int playhead = lanePlayhead(processor, owner.editLine(), (int) ArpEngine::laneNote);
    if (playhead >= 0 && playhead < length)
    {
        g.setColour(skin::lineAccent(owner.editLine()).base.withAlpha(0.85f));
        g.fillRect(b.getX() + cellW * (float) playhead + 2.0f, b.getBottom() - 2.0f,
                   cellW - 4.0f, 2.0f);
    }
}

// ---------------------------------------------------------------------------
// ArpPanel

ArpPanel::ArpPanel(KeysProcessor& p) : processor(p), editedLine(p.arpCurrentLine())
{
    okstudio::ui::makeMouseOnly(*this);
    buildControls();
    // The view Owen left, restored before anything measures the panel: preferredHeight differs
    // between the macro rows and the band, and the editor asks for it as soon as we are built.
    if (p.layout.arpMacro)
        setMacroView(true);
    selectLane(selectedLane); // tab toggles + which grid is visible
    refreshRateMode();        // installs the dial's first attachment; lastRateFree is -1 here
    refreshShape();           // and whether the step editor is showing at all
    refreshRetrig();
    refreshLaneReadouts();
    refreshPatternButtons();
    startTimerHz(10); // repaints the lane grid so edits made elsewhere stay current
}

ArpPanel::~ArpPanel()
{
    stopTimer();
}

int ArpPanel::editLine() const
{
    return juce::jlimit(0, KeysProcessor::uiArpLines - 1, editedLine);
}

// A chord card dropped on one of the twelve slots binds that chord there. The slot keeps its
// pattern; what it gains is the chord a launch will hold into the line.
void ArpPanel::takeChordOnSlot(int slot, const chorddrag::Payload& p)
{
    // Undoable, like Copy slot and Randomize pattern beside it: this replaces the slot's chord,
    // name, shape and rate in place, and an arp slot is one of the two trees undo covers.
    processor.pushUndo("Chord to arp slot", KeysProcessor::UndoScope::arp);
    processor.setArpSlotChord(slot, p.chord.notes, p.chord.name, editLine());
    repaint();
}

// A chord card dropped on a line - its tab, or its whole row in the macro view - goes straight
// into that line, and **that is all it does** (2026-08-18, Owen: "the number of steps changes,
// straight vs triplet etc, when you drag a chord onto a new arpeggiator").
//
// It used to call setEditLine as well, so the panel followed the drop. Nothing was written by
// that - no drop path touches a pattern, a rate or a tuplet - but in a line's deep view every
// readout is per line, so STEPS, Tuplet, Shape and the rate all jumped to the dropped-on line's
// own settings the instant the card landed. A view change and a data change look identical when
// you are watching the numbers, and this one arrived under the hand that was routing a chord.
//
// The reason it moved the aim is worth recording, because it had one and it expired: you aimed
// at that line, so "the next card click should follow the same aim". A card click stopped feeding
// a line on 2026-08-02, so there is no next click left for the aim to serve, and the pad menu's
// Send to arp rows had already been given the opposite behaviour for exactly the complaint above.
// One rule now, for every route a chord takes into a line: routing is not navigating. Looking at
// a line is its tab, or its macro card's Details button.
void ArpPanel::takeChordOnLine(int line, int padSlot)
{
    processor.holdArpChordFromPad(padSlot, line);
}

void ArpPanel::takeChordOnLine(int line, const chorddrag::Payload& dropped)
{
    // A pad still goes the pad way, so the line remembers which card it is holding. Everything
    // else - the live card, a tray candidate, the reference box - is a chord and nothing more.
    if (dropped.from == chorddrag::Payload::From::padSlot)
        processor.holdArpChordFromPad(dropped.index, line);
    else
        processor.holdArpChord(dropped.chord.notes, dropped.chord.name, line);
}

// The panel as a whole is a drop target for a chord card. It hands the chord to the line the
// panel is *editing*, which is the only line it could mean: in the All view a macro card is
// under the pointer and wins, and on the Cards page a slot card does.
bool ArpPanel::isInterestedInDragSource(const SourceDetails& details)
{
    // **Any chord, from anywhere** (2026-08-26). This read `from == padSlot` on all four arp
    // targets, which is what made the live card undraggable into a line: the payload arrived
    // with the chord already in it and was turned away for not carrying an index as well.
    // `chordBeingDragged` is the whole test now - a drag of ours, with notes on it.
    return chorddrag::chordBeingDragged(details) != nullptr;
}

void ArpPanel::itemDragEnter(const SourceDetails&) { panelDropTarget = true; repaint(); }
void ArpPanel::itemDragExit(const SourceDetails&) { panelDropTarget = false; repaint(); }

void ArpPanel::itemDropped(const SourceDetails& details)
{
    panelDropTarget = false;
    repaint();
    if (auto* p = isInterestedInDragSource(details) ? chorddrag::of(details) : nullptr)
    {
        takeChordOnLine(editLine(), *p);
        p->taken = true;
    }
}

// The whole card outlined while a chord is over it, so "anywhere on here" is visible rather
// than something you have to be told. Drawn over the children for the reason MacroRow's scrim
// is: the controls sit on top of the card and would otherwise cover the edge of it.
void ArpPanel::paintOverChildren(juce::Graphics& g)
{
    if (! panelDropTarget)
        return;
    g.setColour(skin::accentOf(*this).base);
    g.drawRoundedRectangle(cardBounds().toFloat().reduced(1.0f), skin::panelRadius, 2.0f);
}

juce::String ArpPanel::paramId(KeysProcessor::ArpParam which) const
{
    return KeysProcessor::arpParamId(editLine(), which);
}

// A tab click. Everything on the panel is bound to one line's parameters, so changing it means
// tearing every attachment down and building it again against the new ids - which is what an
// attachment is: a binding to one named parameter for its whole life. The rate dial has done
// this dance for its two units since the Hz mode landed; this is the same move over all of
// them, and it reuses refreshRateMode's guard so a swap never happens under an open drag.
// The fourth tab. Everything the per-line band and the step editor draw goes away, and three
// rows take their place - one per line, each bound to its own line for good. The *current*
// line is untouched by this: a chord card still has one target, and switching back to A, B or
// C shows the same deep controls you left.
bool ArpPanel::bottomRowFolded() const { return processor.layout.arpMacroBottomFolded; }

// Collapsing changes the panel's height, so the editor has to re-fit - the same call every
// other view and page change here makes. Nothing about the lines is touched: C and D keep
// their chords, their patterns and their output, and their On switches stay on the arp bar.
void ArpPanel::setBottomRowFolded(bool folded)
{
    if (folded == processor.layout.arpMacroBottomFolded)
        return;
    processor.layout.arpMacroBottomFolded = folded;
    resized();
    repaint();
    if (onPreferredHeightChanged)
        onPreferredHeightChanged();
}

// The collapsed row: a chevron, the word LINES, and the letters of the lines that are hidden,
// each in its own accent and dimmed when that line is switched off. Reading which of them are
// running is the whole job here - the switching itself is the arp bar's.
void ArpPanel::FoldedRowStrip::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(skin::bgBot.withAlpha(0.55f));
    g.fillRoundedRectangle(b, 5.0f);
    g.setColour(skin::control.withAlpha(0.75f));
    g.drawRoundedRectangle(b, 5.0f, 1.0f);

    auto area = getLocalBounds().reduced(10, 0);

    // A chevron pointing down: this opens downward, which is where the cards come back.
    const auto chev = area.removeFromLeft(16).toFloat();
    juce::Path p;
    const float cx = chev.getCentreX(), cy = chev.getCentreY();
    p.startNewSubPath(cx - 5.0f, cy - 2.5f);
    p.lineTo(cx, cy + 3.0f);
    p.lineTo(cx + 5.0f, cy - 2.5f);
    g.setColour(skin::textDim);
    g.strokePath(p, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                         juce::PathStrokeType::rounded));
    area.removeFromLeft(10);

    g.setFont(skin::ui(10.0f).withExtraKerningFactor(0.14f));
    g.setColour(skin::textFaint);
    const auto capW = 46;
    g.drawText("LINES", area.removeFromLeft(capW), juce::Justification::centredLeft);
    area.removeFromLeft(4);

    // One letter per hidden line, in its own colour. The bottom row is the last row of the
    // 2x2 grid, so it is whichever lines follow the first two.
    g.setFont(skin::ui(13.0f).boldened());
    for (int n = 2; n < KeysProcessor::uiArpLines; ++n)
    {
        const bool on = processor.apvts
                            .getRawParameterValue(KeysProcessor::arpParamId(n, KeysProcessor::apOn))
                            ->load() > 0.5f;
        const auto accent = skin::lineAccent(n);
        g.setColour(on ? accent.base : accent.base.withAlpha(0.32f));
        g.drawText(juce::String::charToString((juce::juce_wchar) ('A' + n)),
                   area.removeFromLeft(20), juce::Justification::centredLeft);
        area.removeFromLeft(6);
    }

    g.setFont(skin::ui(10.0f));
    g.setColour(skin::textFaint);
    g.drawText("click to show", getLocalBounds().reduced(12, 0), juce::Justification::centredRight);
}

void ArpPanel::setMacroView(bool on)
{
    if (on == macroView)
        return;
    macroView = on;
    processor.layout.arpMacro = on;
    // An armed Copy or Clear waits for a slot click, and the macro view has no slots to
    // click: carrying the armed state across would leave Cancel hidden with nothing to
    // cancel and the next slot click, tabs later, doing something the user armed minutes ago.
    if (on)
        setArmed(armNone);
    for (auto& row : macroRows)
        if (row != nullptr)
            row->setVisible(on);
    // The collapsed-row strip belongs to this view exactly as the cards do. resized() picks
    // cards-or-strip *within* the macro view, but its whole block sits behind `if (macroView)`,
    // so leaving the view never reaches it - and a folded bottom row would go on drawing its
    // strip over the deep view's band.
    if (foldedRowStrip != nullptr)
        foldedRowStrip->setVisible(on && processor.layout.arpMacroBottomFolded);
    refreshShape();   // hides or restores the band, the lane tabs and the step editor
    refreshMacro();
    // The bar's page tabs come and go with this view (they pick a page of a line's deep view,
    // and the macro view has no page), so it has to hear about the change now rather than on
    // the next tick.
    if (onPageChanged)
        onPageChanged();
    // ...and the two views are different heights again since 2026-08-17, so the window has to
    // re-fit. This is the *only* gesture that resizes it: paging inside a deep view shares one
    // height, and a fold is the other thing allowed to move the window. See contentHeight().
    if (onPreferredHeightChanged)
        onPreferredHeightChanged();
    resized();
    repaint();
}

void ArpPanel::refreshMacro()
{
    if (! macroView)
        return;
    for (auto& row : macroRows)
        if (row != nullptr)
            row->refresh();
}

void ArpPanel::setEditLine(int line, bool leaveMacroView)
{
    line = juce::jlimit(0, KeysProcessor::uiArpLines - 1, line);
    // A line tab always leaves the macro view, even when it names the line already current:
    // clicking "B" while All is up plainly means "show me B". A *drop* passes false, because
    // it is routing a chord rather than navigating, and in the macro view the line it landed
    // on is already in front of you.
    const bool leavingMacro = macroView && leaveMacroView;
    if (leavingMacro)
        setMacroView(false);
    if (line == editedLine)
    {
        if (leavingMacro && onEditLineChanged)
            onEditLineChanged();
        return;
    }
    editedLine = line;
    processor.setArpCurrentLine(line);

    buildAttachments();
    // Force the dial's swap. refreshRateMode() early-outs when the *mode* has not changed, and
    // the mode is per line - so switching from a Sync line to another Sync line left the dial
    // attached to the line we had just left, quietly editing A's rate from B's panel. Every
    // other control rebinds in buildAttachments(); this is the one whose attachment lives
    // somewhere else, so it is the one that needed telling.
    lastRateFree = -1;
    refreshRateMode();  // the dial's own two, and which unit is live on the new line
    refreshShape();     // Shape and the step editor, which are not attachments
    refreshRetrig();
    refreshLaneReadouts();
    refreshPatternButtons();
    refreshVoiceButton();       // reads the new line's harmonyMode
    refreshClockDivReadouts();  // reads the new line's rhythmDiv - the documented rate-dial
                                // bug (a control that kept showing the old line) is exactly
                                // what skipping this would repeat
    if (onEditLineChanged)
        onEditLineChanged();
    resized();  // Shape may have changed the panel's height with the line
    repaint();
}

void ArpPanel::buildLaneRow(LaneRow& row, ArpEngine::Lane lane, const juce::String& name)
{
    const auto r = ArpEngine::laneRange((int) lane);
    row.tab.setButtonText(name);
    row.tab.onClick = [this, lane] { selectLane((int) lane); };
    row.hasTab = true;
    addAndMakeVisible(row.tab);

    row.grid = std::make_unique<LaneGrid>(processor, *this, lane, r.lo, r.hi);
    addChildComponent(*row.grid); // only the selected lane's grid is ever visible
}

void ArpPanel::selectLane(int lane)
{
    selectedLane = juce::jlimit(0, ArpEngine::numLanes - 1, lane);
    for (int i = 0; i < ArpEngine::numLanes; ++i)
    {
        auto& row = laneRows[(size_t) i];
        row.tab.setToggleState(i == selectedLane, juce::dontSendNotification);
        if (row.grid != nullptr)
            row.grid->setVisible(patternMode() && i == selectedLane);
    }
    refreshLaneReadouts();
    // Voice is lane-contextual: only the Harmony lane means anything to harmonyMode. Set here
    // as well as in refreshShape(), which gates it on Pattern shape - the two conditions are
    // independent (a lane click cannot change Shape, a Shape change cannot change the lane).
    const bool voiceOn = patternMode() && selectedLane == (int) ArpEngine::laneHarmony;
    voiceButton.setVisible(voiceOn);
    refreshVoiceButton();
    if (loopBar != nullptr)
        loopBar->repaint(); // it reads the selected lane, and its bounds do not move
    applyPageVisibility(); // this only ever turns things on; the page has the last word
    resized();
}

// Link on is the simple case and the default: every lane keeps the same length and
// speed, so the pattern is just "eight steps". Off is polymeter, where lanes of
// different lengths drift against each other. Cthulhu ships the same switch.
void ArpPanel::nudgeLength(int delta)
{
    const bool linked = processor.apvts.getRawParameterValue(paramId(KeysProcessor::apLinkLanes))->load() > 0.5f;
    const int from = linked ? 0 : selectedLane;
    const int to = linked ? ArpEngine::numLanes - 1 : selectedLane;
    const int target = juce::jlimit(1, ArpEngine::maxSteps,
                                    processor.arpLine(editedLine).lanes.length[(size_t) selectedLane]
                                            .load(std::memory_order_relaxed) + delta);
    for (int i = from; i <= to; ++i)
        processor.arpLine(editedLine).lanes.length[(size_t) i].store(target, std::memory_order_relaxed);
    refreshLaneReadouts();
}

void ArpPanel::cycleClockDiv()
{
    const bool linked = processor.apvts.getRawParameterValue(paramId(KeysProcessor::apLinkLanes))->load() > 0.5f;
    const int next = (processor.arpLine(editedLine).lanes.clockDiv[(size_t) selectedLane].load(std::memory_order_relaxed) + 1) % 3;
    const int from = linked ? 0 : selectedLane;
    const int to = linked ? ArpEngine::numLanes - 1 : selectedLane;
    for (int i = from; i <= to; ++i)
        processor.arpLine(editedLine).lanes.clockDiv[(size_t) i].store(next, std::memory_order_relaxed);
    refreshLaneReadouts();
}

// With Link on, every lane shares the Note lane's length and speed - and this **enforces** it
// rather than trusting nudgeLength to have done so (2026-08-14, Owen: "Sometimes the steps do
// not match each other").
//
// nudgeLength writes all twelve when Link is on, which is correct and was never the problem.
// The problem is lanes that were not there when it last ran: Rand, Mute and Chain were appended
// on 2026-08-14 and arrive at ArpPattern's default 8, so a session whose other lanes are at 32
// had three lanes a quarter the length of the rest and no way to tell - the grid draws each
// lane at its own length, so they simply showed a different number of steps. A session saved
// before any of them has the same hole.
//
// The Note lane is the authority because it is the one that has always existed and the one the
// MUTE row is pinned to. Link off is polymeter and is left alone entirely, which is the whole
// point of the switch.
void ArpPanel::enforceLinkedLengths()
{
    if (processor.apvts.getRawParameterValue(paramId(KeysProcessor::apLinkLanes))->load() <= 0.5f)
        return;
    auto& lanes = processor.arpLine(editedLine).lanes;
    const int len = juce::jlimit(1, ArpEngine::maxSteps,
                                 lanes.length[(size_t) ArpEngine::laneNote].load(std::memory_order_relaxed));
    const int div = juce::jlimit(0, 2,
                                 lanes.clockDiv[(size_t) ArpEngine::laneNote].load(std::memory_order_relaxed));
    // The loop window travels with the length (2026-08-18), and the direction deliberately does
    // not. Link on means the lanes share one grid, and a window is part of which grid that is;
    // a *direction* is how a lane walks the grid it shares, and two lanes crossing the same
    // eight steps in opposite directions is the point of having a direction at all.
    const int from = lanes.loopFrom[(size_t) ArpEngine::laneNote].load(std::memory_order_relaxed);
    const int to = lanes.loopTo[(size_t) ArpEngine::laneNote].load(std::memory_order_relaxed);
    for (int l = 0; l < ArpEngine::numLanes; ++l)
    {
        if (lanes.length[(size_t) l].load(std::memory_order_relaxed) != len)
            lanes.length[(size_t) l].store(len, std::memory_order_relaxed);
        if (lanes.clockDiv[(size_t) l].load(std::memory_order_relaxed) != div)
            lanes.clockDiv[(size_t) l].store(div, std::memory_order_relaxed);
        if (lanes.loopFrom[(size_t) l].load(std::memory_order_relaxed) != from)
            lanes.loopFrom[(size_t) l].store(from, std::memory_order_relaxed);
        if (lanes.loopTo[(size_t) l].load(std::memory_order_relaxed) != to)
            lanes.loopTo[(size_t) l].store(to, std::memory_order_relaxed);
    }
}

void ArpPanel::refreshLaneReadouts()
{
    enforceLinkedLengths(); // before the readout, so it reports what the lanes now agree on
    const auto li = (size_t) selectedLane;
    const int len = processor.arpLine(editedLine).lanes.length[li].load(std::memory_order_relaxed);
    stepsReadout.setText(juce::String(len), juce::dontSendNotification);
    const int div = juce::jlimit(0, 2, processor.arpLine(editedLine).lanes.clockDiv[li].load(std::memory_order_relaxed));
    speedButton.setButtonText(clockDivNames[div]);
    refreshLaneStrip();
}

// The lane strip, and the marks on all twelve tabs. Both are derived from the lanes rather
// than cached against a click, so a session load, an undo and a slot launch all land here
// without any of them having to know the strip exists.
void ArpPanel::refreshLaneStrip()
{
    auto& lanes = processor.arpLine(editedLine).lanes;
    const auto li = (size_t) selectedLane;

    const bool on = lanes.on[li].load(std::memory_order_relaxed) != 0;
    laneOnButton.setToggleState(on, juce::dontSendNotification);
    laneOnButton.setButtonText(on ? "On" : "Off");

    // One entry per LaneDir, pinned to the enum. Every other name table in this file that
    // shadows an enum carries the same assert (SlotCard::shapeNames, ArpEngine's shape names):
    // the index below is clamped to numLaneDirs - 1, so appending a direction without adding a
    // name here reads one past the end of this array and hands the result to setText.
    static const char* dirNames[] = { "Up", "Down", "Up alt", "Down alt" };
    static_assert(std::size(dirNames) == (size_t) ArpEngine::numLaneDirs,
                  "every LaneDir needs a name in dirNames");
    const int d = juce::jlimit(0, (int) ArpEngine::numLaneDirs - 1, lanes.dir[li].load(std::memory_order_relaxed));
    dirReadout.setText(dirNames[d], juce::dontSendNotification);

    for (int l = 0; l < ArpEngine::numLanes; ++l)
    {
        auto& tab = laneRows[(size_t) l].tab;
        if (! laneRows[(size_t) l].hasTab)
            continue;
        // "Has data" is measured over the lane's own length, not over all 32 cells: steps past
        // the end are not played and a tab that lit for them would mark every lane that had
        // ever been longer than it is now.
        const int len2 = juce::jlimit(1, ArpEngine::maxSteps, lanes.length[(size_t) l].load(std::memory_order_relaxed));
        bool has = false;
        for (int st = 0; st < len2 && ! has; ++st)
            has = lanes.value[(size_t) l][(size_t) st].load(std::memory_order_relaxed) != ArpEngine::laneDefaults[l];
        const bool lOn = lanes.on[(size_t) l].load(std::memory_order_relaxed) != 0;
        if (tab.laneHasData != has || tab.laneOn != lOn)
        {
            tab.laneHasData = has;
            tab.laneOn = lOn;
            tab.repaint();
        }
    }

    refreshStepTools();
}

void ArpPanel::refreshStepTools()
{
    // Paste is the only one of the three that can be dead: nothing copied yet, or copied from
    // a lane that means something else. Kirnu's rule, and it is not pedantry - a Velocity lane
    // pasted into Note would read as chord indices and play a melody nobody wrote.
    pasteStepsButton.setEnabled(stepClipboardLane == selectedLane && ! stepClipboard.empty());
}

void ArpPanel::toggleLaneOn()
{
    KeysProcessor::UndoGesture gesture { processor, "Lane on/off", KeysProcessor::UndoScope::arp };
    auto& lanes = processor.arpLine(editedLine).lanes;
    const auto li = (size_t) selectedLane;
    lanes.on[li].store(lanes.on[li].load(std::memory_order_relaxed) != 0 ? 0 : 1, std::memory_order_relaxed);
    refreshLaneStrip();
    if (auto& grid = laneRows[li].grid)
        grid->repaint();
}

// The span both of these act on: the Select span, or the whole lane when nothing is marked.
// The same rule Roll and Reset follow, which is what keeps Select one idea aimed by four
// buttons rather than a mode each of them has to be explained against.
static void laneSpan(const ArpEngine::Lanes& lanes, int lane, int selFrom, int selTo,
                     int& lo, int& hi)
{
    const int len = juce::jlimit(1, ArpEngine::maxSteps,
                                 lanes.length[(size_t) lane].load(std::memory_order_relaxed));
    lo = selFrom < 0 ? 0 : juce::jlimit(0, len - 1, selFrom);
    hi = selTo < 0 ? len - 1 : juce::jlimit(lo, len - 1, selTo);
}

void ArpPanel::copySteps()
{
    auto& lanes = processor.arpLine(editedLine).lanes;
    int lo = 0, hi = 0;
    laneSpan(lanes, selectedLane, selFrom, selTo, lo, hi);
    stepClipboard.clear();
    for (int s = lo; s <= hi; ++s)
        stepClipboard.push_back(lanes.value[(size_t) selectedLane][(size_t) s].load(std::memory_order_relaxed));
    stepClipboardLane = selectedLane;
    refreshStepTools();
}

void ArpPanel::pasteSteps()
{
    if (stepClipboardLane != selectedLane || stepClipboard.empty())
        return;

    processor.pushUndo("Paste steps", KeysProcessor::UndoScope::arp);
    auto& lanes = processor.arpLine(editedLine).lanes;
    int lo = 0, hi = 0;
    laneSpan(lanes, selectedLane, selFrom, selTo, lo, hi);
    // Tiled rather than truncated: copying two steps and pasting them over eight is how a
    // figure gets repeated, and it is the only reading under which a clipboard shorter than
    // its target does something useful instead of leaving a hole.
    for (int s = lo; s <= hi; ++s)
        lanes.value[(size_t) selectedLane][(size_t) s]
            .store(stepClipboard[(size_t) ((s - lo) % (int) stepClipboard.size())], std::memory_order_relaxed);

    refreshLaneReadouts();
    if (auto& g = laneRows[(size_t) selectedLane].grid)
        g->repaint();
}

void ArpPanel::nudgeLaneDir(int delta)
{
    KeysProcessor::UndoGesture gesture { processor, "Lane direction", KeysProcessor::UndoScope::arp };
    auto& lanes = processor.arpLine(editedLine).lanes;
    const auto li = (size_t) selectedLane;
    const int n = (int) ArpEngine::numLaneDirs;
    // Wraps, like the strum direction and unlike the rate steppers: four values with no scale
    // to them are a ring, and stopping at the ends leaves two of them one-sided.
    const int d = ((lanes.dir[li].load(std::memory_order_relaxed) + delta) % n + n) % n;
    lanes.dir[li].store(d, std::memory_order_relaxed);
    refreshLaneStrip();
}

bool ArpPanel::patternMode() const
{
    return processor.apvts.getRawParameterValue(paramId(KeysProcessor::apPattern))->load() > 0.5f;
}

void ArpPanel::applyShapeChoice()
{
    const int chosen = shapeBox.getSelectedItemIndex(); // 0..7 = a direction, 8 = Pattern
    auto& apvts = processor.apvts;
    // Gestures by hand. Every other control here is an APVTS attachment and gets its
    // begin/end for free; Shape spans two parameters so it cannot be one, and without
    // the brackets a host in touch or latch never arms on a Shape change.
    if (auto* pat = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramId(KeysProcessor::apPattern))))
    {
        pat->beginChangeGesture();
        *pat = chosen >= ArpEngine::numDirections;
        pat->endChangeGesture();
    }
    if (chosen >= 0 && chosen < ArpEngine::numDirections)
        if (auto* dir = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramId(KeysProcessor::apDirection))))
        {
            dir->beginChangeGesture();
            *dir = chosen; // "Pattern" leaves the direction alone; lanes can still follow it
            dir->endChangeGesture();
        }
    refreshShape();
}

// Retrigger, the same two-parameter dance as Shape. Item 0 is off, item 1 is the old
// toggle, and 2.. are the clock windows; picking a clock window turns the note retrigger
// off, because "restart every bar AND on every chord" is not a thing anyone means by one
// control (Ableton's Retrigger is one choice for the same reason).
void ArpPanel::applyRetrigChoice()
{
    const int chosen = retrigBox.getSelectedItemIndex();
    auto& apvts = processor.apvts;
    if (auto* on = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramId(KeysProcessor::apRetrigger))))
    {
        on->beginChangeGesture();
        *on = chosen == 1;
        on->endChangeGesture();
    }
    if (auto* bars = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramId(KeysProcessor::apRetrigBars))))
    {
        bars->beginChangeGesture();
        *bars = chosen >= 2 ? chosen - 1 : 0;
        bars->endChangeGesture();
    }
}

void ArpPanel::refreshRetrig()
{
    const int bars = (int) processor.apvts.getRawParameterValue(paramId(KeysProcessor::apRetrigBars))->load();
    const bool onNote = processor.apvts.getRawParameterValue(paramId(KeysProcessor::apRetrigger))->load() > 0.5f;
    const int wanted = bars > 0 ? bars + 1 : (onNote ? 1 : 0);
    if (retrigBox.getSelectedItemIndex() != wanted)
        retrigBox.setSelectedItemIndex(wanted, juce::dontSendNotification);
}

// Parameters are the truth (a host can automate them), so the combo and the step
// editor's visibility are both derived, never assumed.
void ArpPanel::refreshShape()
{
    // In the macro view none of the band exists, so every visibility decision below collapses
    // to "no". The step editor in particular: it belongs to one line, and the macro view is
    // deliberately the one place where no single line is the subject.
    const bool pattern = patternMode() && ! macroView;
    const int dir = (int) processor.apvts.getRawParameterValue(paramId(KeysProcessor::apDirection))->load();
    const int wanted = pattern ? ArpEngine::numDirections : juce::jlimit(0, ArpEngine::numDirections - 1, dir);
    if (shapeBox.getSelectedItemIndex() != wanted)
        shapeBox.setSelectedItemIndex(wanted, juce::dontSendNotification);

    for (int i = 0; i < ArpEngine::numLanes; ++i)
    {
        laneRows[(size_t) i].tab.setVisible(pattern && laneRows[(size_t) i].hasTab);
        if (laneRows[(size_t) i].grid != nullptr)
            laneRows[(size_t) i].grid->setVisible(pattern && i == selectedLane);
    }
    muteRowLabel.setVisible(pattern);
    if (muteRow != nullptr)
        muteRow->setVisible(pattern);

    // Everything that lives in the band, hidden wholesale while the macro rows have its space.
    setAllVisible({ &shapeBox, &distanceBox, &retrigBox, &rateLabel, &shapeLabel, &distanceLabel,
                    &retrigLabel, &rateKnob, &rateModeButton, &shapePrev, &shapeNext, &ratePrev,
                    &rateNext, &dotButton, &tupletBox, &tupletLabel, &anchorButton, &octavesSlider,
                    &swingSlider, &gateSlider, &densitySlider, &octavesLabel, &swingLabel,
                    &gateLabel, &densityLabel, &latchButton, &keysBandButton, &offsetSlider,
                    &rampSlider, &rampTimeSlider, &humanSlider, &humanVelSlider, &offsetLabel,
                    &rampLabel, &rampTimeLabel, &humanLabel, &humanVelLabel,
                    &driftSlider, &driftLabel }, ! macroView);

    // Voice rides the STEPS group's own gate plus the Harmony lane; see selectLane() for the
    // other half of this condition. Roll is not lane-contextual - it acts on whichever lane is
    // showing - so it follows the step editor alone.
    const bool voiceOn = pattern && selectedLane == (int) ArpEngine::laneHarmony;
    voiceButton.setVisible(voiceOn);
    // The tools, and the lane strip beside them - Steps, Speed and Link included, since they
    // moved off the band on 2026-08-18. All of it belongs to the step editor and so all of it
    // follows Pattern shape, which is the one gate they have always shared.
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &rollButton, &resetButton, &selectButton, &rollMinus, &rollReadout, &rollPlus,
             &copyStepsButton, &pasteStepsButton,
             &laneOnButton, &dirLabel, &dirPrev, &dirReadout, &dirNext,
             &stepsLabel, &speedLabel, &stepsReadout, &stepsMinus, &stepsPlus, &speedButton,
             &linkButton })
        c->setVisible(pattern);
    if (loopBar != nullptr)
        loopBar->setVisible(pattern);

    // The slot row stays on both per-line shapes. Launching a chord through "Up" is as much
    // a thing you do as launching one through an edited pattern, and hiding the row was what
    // made the old A-H buttons feel like an appendix to the step editor rather than the way
    // you drive the arp. Randomize is the exception: there is nothing to randomize but lanes.
    // The macro view carries none of it since 2026-08-02 (Owen: "remove everything on the
    // bottom. Copy, clear, stop, chain"): that view is the two lines and what they share,
    // and the slots belong to the tabs where the buttons that act on them still are. Only
    // the A/B/All tabs survive there, up on the view's own header.
    const bool slotsOn = ! macroView;
    for (auto& c : slotCards)
        if (c != nullptr)
            c->setVisible(slotsOn);
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &copyButton, &clearButton, &stopButton, &chainButton,
             &barsMinus, &barsPlus, &barsReadout, &clocksButton })
        c->setVisible(slotsOn);
    if (! slotsOn && clocksStripOpen)
        openClocksStrip(false); // dividers act in every shape, but not in the macro view

    randomizeButton.setVisible(pattern);
    euclidButton.setVisible(pattern);
    if (! pattern && euclidStripOpen)
        openEuclidStrip(false); // Euclid only means anything with the probability lane on screen

    // Last, always: everything above decided visibility on Shape and lane grounds without
    // knowing which page is showing, and this takes back what is off it (2026-08-14). It
    // only ever hides, so the order is what makes it correct - see applyPageVisibility().
    applyPageVisibility();

    // Shape no longer changes the panel's height - every page fits inside arpDeepH - but it
    // still changes what is *in* the Setup page and whether Steps is reachable at all, so the
    // relayout stays. Only on an actual change, since refreshShape() runs on the 10 Hz timer.
    if (lastPatternMode != (int) pattern)
    {
        lastPatternMode = (int) pattern;
        // Leaving Pattern with the Steps page up strands you on a page with nothing on it.
        // setPage sends it to Setup, and calls back into here - which is safe, because
        // lastPatternMode is already written and the recursion stops on this branch.
        if (! pattern && currentPage() == Page::steps)
            setPage(Page::setup);
        // The **Draw** tab greys outside Pattern shape and it lives on the section bar, which is
        // the editor's and not this panel's - so the crossing has to be carried across
        // (2026-08-18, Owen: "how do we get to the part where we add harmony and stuff like
        // that?", with Shape already reading Pattern and Draw greyed). refreshShape() knew this
        // on every 10 Hz tick; the tab's enabled state was written only when the *line* or the
        // *page* changed, so setting Shape to Pattern left Draw greyed until you visited another
        // page and came back, and the one route to the lane editor looked broken.
        //
        // Here rather than beside the notify above, because this branch is already exactly once
        // per crossing, and `lastPatternMode` starting at -1 makes the first call report - which
        // is what opens a session saved in Pattern shape with Draw already live.
        if (onShapeChanged)
            onShapeChanged();
        resized();
        repaint();
    }
}

void ArpPanel::stepCombo(juce::ComboBox& box, int delta)
{
    const int n = box.getNumItems();
    if (n <= 0)
        return;
    // Clamp rather than wrap. Wrapping means one click too many on "Up" drops you at
    // "Pattern" and the whole step editor appears, which is a big surprise for a small
    // button; at the ends the button simply does nothing, which is easy to feel.
    const int next = juce::jlimit(0, n - 1, box.getSelectedItemIndex() + delta);
    if (next != box.getSelectedItemIndex())
        box.setSelectedItemIndex(next); // notifies, so the attachment/onChange runs
}

// The rate steppers, in whichever unit is live. Both branches clamp at the ends rather than
// wrapping, for the reason stepCombo gives: a button that does nothing at the end of its
// travel is easy to feel, where one that jumps to the other end is a nasty surprise.
void ArpPanel::stepRate(int delta)
{
    auto& apvts = processor.apvts;

    if (apvts.getRawParameterValue(paramId(KeysProcessor::apRateFree))->load() > 0.5f)
    {
        auto* hz = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramId(KeysProcessor::apRateHz)));
        if (hz == nullptr)
            return;
        // A click is a quarter of an octave, so four of them halve or double the rate - which
        // is exactly the jump one entry of the Sync list makes, so the button means the same
        // amount of change in both modes, four times finer here. The ladder is anchored on
        // 1 Hz, which puts both ends of the range (2^-5 and 2^5) and every power of two on it,
        // so repeated clicks always land on the same forty values. floor/ceil rather than
        // round, so a value a host left off the ladder still moves a full step the way it was
        // asked to instead of snapping backwards.
        const double rungs = std::log2((double) hz->get()) * 4.0;
        const int wanted = delta > 0 ? (int) std::floor(rungs + 1.0e-4) + 1
                                     : (int) std::ceil(rungs - 1.0e-4) - 1;
        const auto next = (float) juce::jlimit(ArpEngine::minRateHz, ArpEngine::maxRateHz,
                                               std::pow(2.0, (double) wanted * 0.25));
        if (next != hz->get())
        {
            // By hand, like Shape: these are buttons, not an attachment, and without the
            // brackets a host in touch or latch never arms on a rate change.
            hz->beginChangeGesture();
            *hz = next;
            hz->endChangeGesture();
        }
        return;
    }

    if (auto* rate = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramId(KeysProcessor::apRate))))
    {
        const int next = juce::jlimit(0, rate->choices.size() - 1, rate->getIndex() + delta);
        if (next != rate->getIndex())
        {
            rate->beginChangeGesture();
            *rate = next;
            rate->endChangeGesture();
        }
    }
}

// The combo drives itself - it has an ordinary ComboBoxAttachment - so all this does is keep
// the *readout* honest: the dial's text is a function of the division, Dot and Tuplet, and an
// attachment binds it to the first of those alone.
void ArpPanel::refreshTuplet()
{
    const int n = KeysProcessor::tupletFor(
        (int) processor.apvts.getRawParameterValue(paramId(KeysProcessor::apTuplet))->load());
    const int dotted = processor.apvts.getRawParameterValue(paramId(KeysProcessor::apDot))->load() > 0.5f;
    if (n == lastTuplet && dotted == lastDotted)
        return;
    lastTuplet = n;
    lastDotted = dotted;
    rateKnob.updateText();
}

// The dial read "1/8" while the engine played a dotted quintuplet, and until 2026-08-03 there
// was nothing on screen to say so (Owen: "when triplet mode is enabled the division text should
// reflect"). The attachment's own text function is the bare division - it comes from the choice
// parameter, which knows nothing about Dot or Tuplet - so it is replaced here, after every swap,
// because SliderParameterAttachment writes it in its constructor.
//
// Sync only: in Hz the attachment's "4.00 Hz" is already the whole truth, since the engine
// ignores both modifiers there.
void ArpPanel::installRateText()
{
    if (rateSyncAtt == nullptr)
        return;
    // `this` is the panel the knob is a member of, so the capture cannot dangle. No parameter
    // pointer is needed: rateSyncText works from the index, which is what the dial holds.
    rateKnob.textFromValueFunction = [this](double v)
    {
        const int n = KeysProcessor::tupletFor(
            (int) processor.apvts.getRawParameterValue(paramId(KeysProcessor::apTuplet))->load());
        const bool dot = processor.apvts.getRawParameterValue(paramId(KeysProcessor::apDot))->load() > 0.5f;
        return ArpEngine::rateSyncText((int) std::lround(v), dot, n);
    };
    rateKnob.updateText();
}

// The mode is a change of *unit*, so it changes what the dial is attached to, what its
// readout says, what a stepper click means, and whether Dot and Tuplet mean anything at all.
// Parameters are the truth - a host can automate arpRateFree - so this is derived and runs
// off the timer as well as off the button.
void ArpPanel::refreshRateMode()
{
    const bool free = processor.apvts.getRawParameterValue(paramId(KeysProcessor::apRateFree))->load() > 0.5f;
    if (lastRateFree == (int) free)
        return;

    // Never swap the dial's attachment out from under a live drag. A drag is a parameter
    // *gesture*: SliderParameterAttachment turns sliderDragStarted into beginChangeGesture and
    // sliderDragEnded into endChangeGesture, and its destructor only removes the listener - it
    // never closes one in flight. This function runs off the 10 Hz timer, so anything writing
    // arpRateFree from outside the panel (a Chain launching a slot on the bar line, host
    // automation, an MCP client) could otherwise destroy the live attachment mid-drag, leaving
    // a begin with no end on one parameter and an end with no begin on the other: a debug
    // assertion in JUCE, and a host latched in automation-write in a release build. So the
    // mode change waits. It is not deferred by a whole timer tick either: rateKnob.onDragEnd
    // calls back here the moment the button comes up, after the attachment has closed its own
    // gesture (Slider invokes onDragEnd only once every listener's sliderDragEnded has run).
    if (rateDragging)
        return;

    lastRateFree = (int) free;

    // Swap, don't hand-sync. Destroy first so only one attachment is ever listening to the
    // dial, then build the other: it brings the parameter's range, its interval (eleven
    // detents in Sync, continuous in Hz), its skew and its text formatting with it, which is
    // why the readout reads "1/8" in one mode and "4.00 Hz" in the other with no code here.
    rateHzAtt.reset();
    rateSyncAtt.reset();
    if (free)
        rateHzAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apRateHz), rateKnob);
    else
        rateSyncAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apRate), rateKnob);
    installRateText(); // the new attachment has just overwritten the readout's text function

    rateModeButton.setButtonText(free ? "Hz" : "Sync"); // the live unit, not the one a click would pick
    rateModeButton.setTooltip(free ? "Rate is free-running in Hz: no tempo, no bar grid, and it "
                                     "runs whether the transport rolls or not. Click for tempo-synced "
                                     "divisions."
                                   : "Rate is tempo-synced, in divisions of the host bar. Click to "
                                     "free-run it in Hz instead.");
    rateKnob.setTooltip(free ? "Step rate as a frequency, 0.031 to 32 Hz - the same span the "
                               "divisions cover at 120 bpm. The dial is near enough logarithmic, "
                               "with 1 Hz at its centre."
                             : "Step length, from 16 bars down to 1/64. The dial detents onto each "
                               "of the eleven divisions, so it cannot land between two.");
    ratePrev.setTooltip(free ? "Slower: down a quarter of an octave. Four clicks halve the rate."
                             : "Slower: the next division up the list.");
    rateNext.setTooltip(free ? "Faster: up a quarter of an octave. Four clicks double the rate."
                             : "Faster: the next division down the list.");

    // Dot and Tuplet subdivide a *beat*, and in Hz there is no beat: the engine ignores them
    // there (see ArpEngine::stepLengthBeats), so they grey out rather than sitting lit and
    // doing nothing.
    dotButton.setEnabled(! free);
    tupletBox.setEnabled(! free);
    tupletLabel.setEnabled(! free);
    // Anchor goes with them, for exactly the same reason. ArpEngine::process() takes the
    // bar-affixed branch on `clock.playing && clock.hasPpq && p.anchored && ! p.rateFree`, so
    // in Hz the toggle is inert - a free-running rate has no bar grid to lock to. It used to
    // sit lit and enabled while it did nothing at all.
    anchorButton.setEnabled(! free);
    anchorButton.setTooltip(free ? "Nothing to anchor to in Hz: a free-running rate follows no "
                                   "bar grid. Switch the rate to Sync to lock the steps to one."
                                 : "Anchored: locked to the host bar grid. Free: never jumps, "
                                   "may drift.");
}

void ArpPanel::setArmed(Armed a, int fromIndex)
{
    if (armed == a && copyFromIndex == fromIndex)
        return;
    armed = a;
    copyFromIndex = fromIndex;
    refreshPatternButtons();
    resized(); // Copy, Clear and Cancel all change width with the armed state
    repaint();
}

void ArpPanel::recallOrCopy(int index)
{
    switch (armed)
    {
        case armCopy:
            if (index != copyFromIndex)
                processor.pushUndo("Copy slot", KeysProcessor::UndoScope::arp);
                processor.copyArpPattern(copyFromIndex, index, editLine());
            break;
        case armClear:
            processor.clearArpSlotChord(index, editLine());
            break;
        case armNone:
            launchSlot(index);
            refreshPatternButtons();
            return; // launching does not disarm anything; there is nothing armed
    }
    setArmed(armNone);
}

void ArpPanel::launchSlot(int index)
{
    // Clicking the slot that is already holding its chord releases it, so one control both
    // starts and stops a slot; anything else needs the Stop button to undo a launch.
    if (processor.arpLaunchedSlot(editLine()) == index)
    {
        processor.stopArpSlot();
        return;
    }
    processor.launchArpSlot(index, editLine());
    refreshShape(); // the slot may have moved Shape, and that decides what is on screen
}

void ArpPanel::showSlotMenu(int index)
{
    juce::PopupMenu m;
    const auto& slot = processor.arpPatternSlot(index, editLine());
    const bool hasChord = ! slot.chordNotes.empty();

    m.addSectionHeader("Slot " + juce::String(index + 1));
    m.addItem(1, "Launch", true, processor.arpLaunchedSlot(editLine()) == index);
    m.addItem(2, "Clear chord", hasChord);
    m.addSeparator();
    m.addItem(3, "Copy this pattern to" + juce::String::fromUTF8("\xe2\x80\xa6"));
    // Only in Pattern shape, which is where the Randomize *button* lives. Enabled on a plain
    // shape it would be the one thing in the arp reachable by right-click alone, and there
    // would be nothing for it to randomize anyway.
    m.addItem(4, "Randomize this pattern", patternMode());

    juce::Component::SafePointer<ArpPanel> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options {}.withParentComponent(getTopLevelComponent()),
                    [safe, index](int r)
    {
        auto* self = safe.getComponent();
        if (self == nullptr || r == 0)
            return;
        if (r == 1)
            self->launchSlot(index);
        else if (r == 2)
            self->processor.clearArpSlotChord(index, self->editLine());
        else if (r == 3)
        {
            // Same arm-then-pick gesture the Copy button uses, seeded from this slot: the
            // menu cannot show a target picker without nesting a second async menu.
            self->setArmed(armCopy, index);
        }
        else if (r == 4)
        {
            self->processor.recallArpPattern(index, self->editLine());
            self->processor.pushUndo("Randomize pattern", KeysProcessor::UndoScope::arp);
            self->processor.randomizeActiveArpPattern(self->editLine());
        }
        self->refreshPatternButtons();
    });
}

void ArpPanel::refreshPatternButtons()
{
    for (auto& c : slotCards)
        if (c != nullptr)
            c->repaint(); // the card paints active/launched itself, from the processor

    // Armed, the two buttons say what they are waiting for. "Pick a slot" is the whole
    // instruction, and Cancel appears next to it so the gesture is never a trap.
    copyButton.setButtonText(armed == armCopy ? "Copy from " + juce::String(copyFromIndex + 1)
                                              : juce::String("Copy"));
    clearButton.setButtonText(armed == armClear ? juce::String("Clear: pick a slot")
                                                : juce::String("Clear"));
    copyButton.setToggleState(armed == armCopy, juce::dontSendNotification);
    clearButton.setToggleState(armed == armClear, juce::dontSendNotification);
    if (cancelButton.isVisible() != (armed != armNone))
    {
        cancelButton.setVisible(armed != armNone);
        resized(); // Cancel's width appears and disappears with it; see the action row
    }
    // The same three-way test the Hold off chip on the arp bar uses, and it has to be: the
    // tooltip below says the two are one button. chainRunning() is not implied by the other
    // two - an Exclusive pad press clears launchedSlot and the held chord while chainOn stays
    // set - so without it Stop greyed out at the exact moment the bar chip lit, leaving the
    // panel's own way out of a running chain disabled while the chain ran.
    stopButton.setEnabled(processor.arpLaunchedSlot(editLine()) >= 0 || ! processor.arpHeldNotes(editLine()).empty()
                          || processor.chainRunning(editLine()));

    // Chain lights while it runs, and says so: the row is playing itself, which is a state
    // worth being able to read from across the room.
    const bool chaining = processor.chainRunning(editLine());
    chainButton.setToggleState(chaining, juce::dontSendNotification);
    chainButton.setButtonText(chaining ? "Chaining" : "Chain");
    const int activeBars = processor.arpSlotBars(processor.arpActivePattern(editLine()), editLine());
    barsReadout.setText(juce::String(activeBars) + (activeBars == 1 ? " bar" : " bars"),
                        juce::dontSendNotification);

    // Clocks retitles the same way Chain does, but on whether any divider is running rather
    // than on the strip being open - the toggle glow already says that (openClocksStrip). Not
    // setToggleState here: that would fight the strip-open state the same button also shows.
    const auto& engine = processor.arpLine(editLine());
    bool anyDividerOn = false;
    for (int i = 0; i < 4; ++i)
        anyDividerOn |= engine.rhythmDiv[(size_t) i].load(std::memory_order_relaxed) != 0;
    clocksButton.setButtonText(anyDividerOn ? "Clocked" : "Clocks");
}

// ---------------------------------------------------------------------------
// SlotCard

ArpPanel::SlotCard::SlotCard(ArpPanel& o, KeysProcessor& p, int i)
    : juce::Button("Arp slot " + juce::String(i + 1)), owner(o), processor(p), index(i)
{
    okstudio::ui::makeMouseOnly(*this);
    setTitle("Arp slot " + juce::String(i + 1)); // accessible name for the capture script
}

void ArpPanel::SlotCard::setDropTarget(bool b)
{
    if (dropTarget == b)
        return;
    dropTarget = b;
    repaint();
}

// A chord from anywhere, as of 2026-08-26 - the pad strip, the live card, the generator's tray
// or its reference box. This was pad-strip-only, deliberately, on the reading that widening it
// was a feature rather than something to let in sideways; Owen asked for the feature. Nothing
// downstream had to change to take it: `takeChordOnSlot` has always read `p.chord` and has never
// cared where it came from.
bool ArpPanel::SlotCard::isInterestedInDragSource(const SourceDetails& details)
{
    return chorddrag::chordBeingDragged(details) != nullptr;
}

void ArpPanel::SlotCard::itemDragEnter(const SourceDetails&) { setDropTarget(true); }
void ArpPanel::SlotCard::itemDragExit(const SourceDetails&) { setDropTarget(false); }

void ArpPanel::SlotCard::itemDropped(const SourceDetails& details)
{
    setDropTarget(false);
    if (auto* p = isInterestedInDragSource(details) ? chorddrag::of(details) : nullptr)
    {
        owner.takeChordOnSlot(index, *p);
        p->taken = true; // and the strip leaves the card it came from alone
    }
}

void ArpPanel::SlotCard::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onRightClick)
            onRightClick();
        return; // never a launch, never shows the down state
    }
    juce::Button::mouseDown(e);
}

// ---------------------------------------------------------------------------
// MacroRow: one arpeggiator line, in one row. Three of these are the macro view.

namespace
{
    // The knobs a macro row carries, in the order they are laid out. Heading, parameter and
    // tooltip in one place so the columns cannot drift out of step with what they set.
    struct MacroKnobSpec { KeysProcessor::ArpParam param; const char* heading; const char* tip; };
    const MacroKnobSpec macroKnobSpecs[] = {
        { KeysProcessor::apOctShift, "OCT",
          "Moves this whole line up or down whole octaves. Centred: nothing at 12 o'clock, down "
          "to the left, up to the right. Put one line an octave under the other and they stop "
          "fighting for the same register." },
        { KeysProcessor::apGate, "GATE",
          "How much of each step this line's notes fill. Short gates let another line through." },
        // DENSITY (2026-09-01, Owen: "a density knob where it controls, like, how much notes
        // there are"). It is the Chance parameter - the Play page's slider, given a face here -
        // and it reads as Density on both, because "chance" is what the *lane* is called and
        // this is the one knob that thins a whole line. The lower half of Owen's ask; the half
        // that would add notes is the Ratchet lane and the harmony voices, which already exist.
        { KeysProcessor::apChance, "DENSITY",
          "How many of this line's steps actually play. At 100 every step fires; turn it down "
          "and steps drop out at random, thinning the run. Multiplies the Chance lane. With "
          "LEGATO on, the note before a dropped step holds through it instead of leaving a gap." },
        // MUTATE and LOCK replaced CHANCE here on 2026-08-18 (Owen: "explore the chance knob
        // being a drift instead where it explores other patterns and notes... could be multiple
        // knobs. want notes. mutations"). Chance lost nothing by it: it is still a step lane and
        // still has its own slider in the Play page's PLAYBACK group, which is where a control
        // you set once and leave belongs. These two are the ones you sit and turn.
        { KeysProcessor::apMutate, "MUTATE",
          "How often the run swaps a step for a different note of the chord you are holding, "
          "and how far along the chord it reaches to find one. It never plays a note that is "
          "not in your chord, at any setting - that is what STRAY beside it is for." },
        // STRAY (2026-08-21) is Mutate's own upper half, moved out to a control of its own.
        // Owen: "it's adding additional notes in the arpeggiator ... it should just change the
        // existing ones" - the additional notes were this stage, reachable only by turning
        // Mutate past halfway, so there was no way to explore the chord hard without them.
        // Zero is off and is the default, which is what makes turning MUTATE up safe again.
        { KeysProcessor::apStray, "STRAY",
          "How often a step is allowed to land on a note that is not in your chord. At 0 - "
          "where it starts - this line plays your chord and nothing else. Turned up, strays "
          "are in-scale neighbours at first and go chromatic towards the top. LOCK holds them "
          "like any other variation, so a wrong note worth keeping stays." },
        { KeysProcessor::apMutateLock, "LOCK",
          "How long it keeps what it finds. Left, a new variation every time round; right, the "
          "first one it finds repeats for good. In between it holds an idea, then moves on." },
        { KeysProcessor::apSwing, "SWING",
          "Shifts this line's offbeats late (right) or early (left). The quickest way to stop "
          "two lines landing on top of each other." },
        { KeysProcessor::apOffset, "OFFSET",
          "Starts this line's pattern from a different foot. Two lines on the same rate and "
          "different offsets are out of phase rather than in unison." },
        // VEL replaced VOL on 2026-08-02 (Owen: "it should start in the middle so you can
        // turn it up or down. But really, the volume is controlling velocity"): bipolar,
        // centred at "as played", and named for what it actually touches. The old arpVolume
        // parameter still exists for saved sessions; migrateVelTrim folds it into this.
        // Humanize Velocity folded into its ring on 2026-08-17 (Owen: "I didn't realize there
        // was a separate velocity knob. I only want one velocity knob, and I want this
        // humanize section to be the outer ring") - see the RangeKnob wiring in the
        // constructor for how the ring reaches arpHumanVel instead of a span of VEL itself.
        // Absolute MIDI velocity since 2026-08-18 (Owen, on the readout reading "-31 ~20":
        // "still wrong"). It was apVelTrim, a bipolar *percentage* trim on the velocity that
        // arrived - percentages on a control called VEL, beside a pads knob that had just become
        // an absolute 0-127 band. One quantity, one unit, one number you can read.
        { KeysProcessor::apVelLevel, "VEL",
          "This line's velocity, 0-127. The knob is the middle of the band and the ring is how "
          "far either side of it a hit can land, so the two read as one band - the same as the "
          "pads' own Humanize knob. At 0 the line is silent. The way to balance two lines "
          "against each other without playing one of them softer." },
        // HUMAN split into its halves on 2026-08-02 (Owen: "maybe we could split it up into
        // two knobs"), so timing and dynamics randomize independently. H.VEL, the velocity
        // half, moved onto VEL's own ring above; only the timing half is still its own knob.
        { KeysProcessor::apHumanize, "H.TIME",
          "Nudges each hit a little late, by a different amount every time. At 0 the line is "
          "dead on the grid." },
    };
    static_assert(sizeof(macroKnobSpecs) / sizeof(macroKnobSpecs[0])
                      == (size_t) ArpPanel::MacroRow::numKnobs,
                  "every macro knob needs a heading and a parameter");
} // namespace

// The harmony dropdown's own two-column menu (2026-08-19, Owen: "make harmony 2 columns" -
// and, shown a single tall column, "still one column"). Off and the descending intervals fill
// the left column, the ascending ones the right, the split BigSky's panel draws for the same
// list. The break is found by text rather than by a hard-coded index, so the choice list and
// this popup cannot drift apart; everything else - ids, tick, attachment - is exactly what the
// ComboBox's own menu would have carried.
juce::PopupMenu ArpPanel::buildHarmonyMenu(const juce::ComboBox& box)
{
    // Two columns, descending intervals left and ascending right, the split BigSky's own panel
    // draws (2026-08-19, Owen: "make harmony 2 columns", and shown a single tall column, "still
    // one column").
    //
    // **The break is found from the semitones, not from the label text.** It matched
    // `startsWith("+")` until 2026-08-22, which reads as data-driven and is not: the harmony
    // table's own rule is that appending is the only safe edit, and an appended *descending*
    // row lands after all the "+" rows, so no break fires for it and it is drawn at the foot of
    // the ascending column. Asking `harmonySemisFor` puts the question to the same table the
    // engine plays, and `ArpTests` pins that the table stays grouped - non-positive rows first -
    // so an append that breaks the grouping fails a test instead of quietly mis-columning.
    //
    // **`harmonySemisFor` takes the 0-based choice index, which is the ComboBox's *index* and
    // not its id.** This was written as `harmonySemisFor(box.getItemId(n))` first and read one
    // row late, putting the break a row early - the same index-versus-id slip as the bug this
    // whole function exists to fix, made while fixing it. The test caught it; that is what the
    // test is for.
    return combomenu::build(box,
                            [](int i)
                            {
                                return KeysProcessor::harmonySemisFor(i) > 0
                                       && KeysProcessor::harmonySemisFor(i - 1) <= 0;
                            });
}

void ArpPanel::MacroRow::HarmonyBox::showPopup()
{
    auto menu = buildHarmonyMenu(*this);
    // The box's own LookAndFeel, exactly as ComboBox::showPopup hands its internal menu:
    // a PopupMenu does not inherit the target component's skin on its own, and without this
    // the two columns came up in JUCE's stock grey. Set here rather than in the builder so the
    // builder needs no live component and a test can call it against a box alone.
    // The standard item height JUCE's own ComboBox::showPopup passes, so the popup's geometry
    // tracks the box it belongs to rather than falling through to whatever the LookAndFeel
    // happens to answer. Those agree today at the 34 px mouse-only height; this keeps them
    // agreeing if either moves.
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withStandardItemHeight(juce::jmax(34, getHeight()))
                           .withTargetComponent(this)
                           .withItemThatMustBeVisible(getSelectedId())
                           // What LookAndFeel_V2::getOptionsForComboBoxPopupMenu passes for
                           // every stock combo, and `StepComboBox` passes too: it highlights the
                           // current row on open, which on a 27-row two-column menu is the
                           // difference between seeing where you are and hunting for a tick.
                           .withInitiallySelectedItem(getSelectedId())
                           .withMinimumWidth(getWidth()),
                       [safe = juce::Component::SafePointer<HarmonyBox>(this)](int result)
                       {
                           if (safe == nullptr)
                               return;
                           // hidePopup first: it is what resets the box's own menu-active
                           // state, which showPopupIfNotActive set before calling us.
                           safe->hidePopup();
                           if (result != 0)
                               safe->setSelectedId(result);
                       });
}

// A die showing five, which is the face that reads at this size: four corners and a centre stay
// distinct where six pips smear into two columns and three reads as a diagonal smudge. Drawn
// from the button's own bounds rather than carrying a magic size, so it follows whatever cell
// the row gives it.
//
// That cell is **34 x 26**, not 34 square: `centred` in MacroRow::resized fixes every control on
// this line at 26 px tall, and the dice matching its neighbours is what keeps the row reading as
// one row. So it is under CLAUDE.md's 34 px floor in one axis, exactly as the four steppers and
// the two combos beside it have always been - a standing property of this line rather than
// anything the dice introduced. Worth fixing, and worth fixing for the whole row at once.
//
// **What that costs, so the decision is one somebody can actually take** (measured 2026-08-21,
// in review): `centred` would have to go and `arpMacroLine` grow by 8 px, which is 8 px on every
// card, two card rows, so 16 px on the All view - and the All view is what sets the window's
// minimum height. That lands on a figure the fold bullet in CLAUDE.md already shows is tight
// against Owen's own 1392 px work area. It is affordable; it is not free, and it is a product
// call rather than a tidy-up, which is why this note says what it would take instead of
// quietly taking it.
void ArpPanel::MacroRow::DiceButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced(5.0f);
    const bool live = isEnabled();
    const auto ink = live ? (highlighted ? skin::text : skin::textDim) : skin::textFaint;

    if (live && (highlighted || down))
    {
        g.setColour(skin::accentOf(*this).base.withAlpha(down ? 0.22f : 0.12f));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 4.0f);
    }

    g.setColour(ink);
    g.drawRoundedRectangle(r, 3.0f, 1.4f);

    const float pip = juce::jmax(1.6f, r.getWidth() * 0.11f);
    const float ox = r.getWidth() * 0.26f, oy = r.getHeight() * 0.26f;
    const auto c = r.getCentre();
    for (auto p : { juce::Point<float>(c.x - ox, c.y - oy), juce::Point<float>(c.x + ox, c.y - oy),
                    c,
                    juce::Point<float>(c.x - ox, c.y + oy), juce::Point<float>(c.x + ox, c.y + oy) })
        g.fillEllipse(juce::Rectangle<float>(pip * 2.0f, pip * 2.0f).withCentre(p));
}

ArpPanel::MacroRow::MacroRow(ArpPanel& o, KeysProcessor& p, int n) : owner(o), processor(p), line(n)
{
    okstudio::ui::makeMouseOnly(*this);
    const auto letter = juce::String::charToString((juce::juce_wchar) ('A' + n));
    const auto id = [n](KeysProcessor::ArpParam w) { return KeysProcessor::arpParamId(n, w); };
    // Every card carries its own headings since the cards went side by side (2026-08-02):
    // "written once on the top row" only worked while the rows stacked and B's columns sat
    // exactly under A's.
    const auto heading = [this](juce::Label& l, const char* text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(skin::micro(9.0f));
        l.setColour(juce::Label::textColourId, skin::textFaint);
        addAndMakeVisible(l);
    };

    // No Latch, PLAY or Chain on the row since the second 2026-08-02 pass (Owen: "I think we
    // can remove the chain button, maybe the play and the [latch] button"): the rows keep
    // what you reach for while both lines run, and the set-and-forget switches live on the
    // line's own tab - Latch and Play on the band, Chain on the action row. PLAY's history
    // (it was KEYS, and collided with the bar's Light keys) moved to keysBandButton with it.
    // The line switch itself (onButton) left the same way on 2026-08-02, the day the A/B
    // chips on the ARP section bar became the per-line On toggles: the card's own On/Off is
    // shown instead as a scrim over the whole body (paintOverChildren), never as a second
    // control bound to the same parameter.

    // The rate's three modifiers, on the sub-row under it. Same three the band carries, same
    // parameters, and greyed by the same question - see refreshRateMode.
    dotButton.setTitle("Macro dot " + letter);
    dotButton.setTooltip("Dotted: each step lasts half again as long, so 1/8 becomes a dotted "
                         "1/8. Stacks with Tuplet, which is a different question. Greyed with "
                         "the rate in Hz, where there is no beat to dot.");
    // Tuplet is a combo here too, and uncaptioned: the sub-row is one 34 px strip with no
    // caption anywhere on it, so the entries have to name themselves - which is why the list
    // reads "Triplet" and "5-tuplet" rather than "3" and "5".
    tupletBox.addItemList(KeysProcessor::tupletChoices(), 1);
    tupletBox.setTitle("Macro tuplet " + letter);
    tupletBox.setTooltip("Fits an odd number of steps into the space a power of two would take: "
                         "Triplet is three where two go, 5-tuplet five where four go. The rate "
                         "readout shows the result, so 1/4 in fives reads \"1/5\". Greyed with "
                         "the rate in Hz, where there is no beat to divide. Run one line straight "
                         "and the other in fives and you have the polyrhythm this view is for.");
    addAndMakeVisible(tupletBox);
    // The dot changes the rate readout as well as the timing, and its attachment does not know
    // that: the dial's text is a function of three parameters and bound to one.
    dotButton.onClick = [this] { refreshTuplet(); };
    anchorButton.setTitle("Macro anchor " + letter);
    // Anchor's tooltip is written by refreshRateMode, beside its enablement: it says something
    // different in Hz, where there is no bar grid to anchor to. Same split as the band's.
    for (auto* b : { &dotButton, &anchorButton })
        addAndMakeVisible(*b);
    dotAtt = std::make_unique<ButtonAtt>(processor.apvts, id(KeysProcessor::apDot), dotButton);
    tupletAtt = std::make_unique<ComboAtt>(processor.apvts, id(KeysProcessor::apTuplet), tupletBox);
    anchorAtt = std::make_unique<ButtonAtt>(processor.apvts, id(KeysProcessor::apAnchor), anchorButton);

    // Legato (2026-09-01): the other half of Density's ask. A step that does not fire - Density
    // turned down, a Chance cell, a mute, a rest, a Chain condition - is a silence with this
    // off; on, the note before it holds through and is released just after the next fired
    // step's note-on, the overlap a synth's legato or glide mode needs. Not greyed in Hz or on
    // any shape: skips happen on every clock and every shape alike.
    legatoButton.setTitle("Macro legato " + letter);
    legatoButton.setTooltip("Hold a note through the steps that do not fire - Density turned "
                            "down, a Chance cell, a mute, a rest - and let it go just after the "
                            "next note that does, so a synth in legato or glide mode slides "
                            "instead of restarting. Gate still ends a note when the next step "
                            "fires; this only fills the gaps.");
    addAndMakeVisible(legatoButton);
    legatoAtt = std::make_unique<ButtonAtt>(processor.apvts, id(KeysProcessor::apLegato), legatoButton);

    // Opens this line's own detailed view - the band, and the step editor once Shape is
    // Pattern. With the A/B tabs gone as navigation (they are the section bar's On switches
    // now), this is the only way from a macro card back to the deep controls.
    detailsButton.setTitle("Macro details " + letter);
    detailsButton.setTooltip("Open line " + letter + "'s detailed view.");
    detailsButton.onClick = [this] { owner.setEditLine(line); };
    addAndMakeVisible(detailsButton);

    // Rate: the same detented dial the band uses, so a division is a detent and the readout
    // under it says which one. The steppers beside it are the click-only path to every value,
    // in both units - a dial is a drag target, and this panel may not require a drag.
    rateKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    rateKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 58, 15);
    rateKnob.setTitle("Macro rate " + letter);
    rateKnob.setTooltip("This line's rate: a time division in Sync, a frequency in Hz. Polyrhythm "
                        "lives here - put one line on 1/8 and another on a 1/8 triplet.");
    rateKnob.onDragStart = [this] { rateDragging = true; };
    rateKnob.onDragEnd = [this] { rateDragging = false; refreshRateMode(); };
    addAndMakeVisible(rateKnob);
    heading(knobLabels[kOctShift], macroKnobSpecs[kOctShift].heading); // laid out with the rest
    // Names over the top line's two stepper groups (2026-08-02, Owen: "the arrows to adjust
    // certain parameters are not clear as to what they're adjusting"): `< dial > Sync < combo >`
    // is two flanked pairs touching, and without words above them the middle two arrows read
    // as one orphaned pair.
    heading(rateHeadLabel, "RATE");
    heading(shapeHeadLabel, "SHAPE");

    ratePrev.onClick = [this] { stepRate(-1); };
    rateNext.onClick = [this] { stepRate(1); };
    for (auto* b : { &ratePrev, &rateNext })
    {
        b->setTooltip("Step this line's rate: one division in Sync, a quarter of an octave in Hz.");
        addAndMakeVisible(*b);
    }
    rateModeButton.setClickingTogglesState(true);
    rateModeButton.setTitle("Macro rate mode " + letter);
    rateModeButton.setTooltip("Sync follows the tempo and its bar grid; Hz free-runs whatever "
                              "the transport is doing.");
    addAndMakeVisible(rateModeButton);
    rateModeAtt = std::make_unique<ButtonAtt>(processor.apvts, id(KeysProcessor::apRateFree), rateModeButton);

    shapeBox.addItemList({ "Up", "Down", "Up-Down", "Down-Up", "Up & Down", "Down & Up",
                           "As Played", "Reversed", "Random", "Random Other", "Random Once",
                           "Chord", "Fingered Bottom", "Fingered Top" }, 1);
    shapeBox.addItem("Pattern", ArpEngine::numDirections + 1);
    shapeBox.onChange = [this] { applyShape(); };
    shapeBox.setTitle("Macro shape " + letter);
    shapeBox.setTooltip("What this line plays. \"Pattern\" hands it to the step editor on that "
                        "line's own tab.");
    addAndMakeVisible(shapeBox);
    shapePrev.onClick = [this] { stepShape(-1); };
    shapeNext.onClick = [this] { stepShape(1); };
    for (auto* b : { &shapePrev, &shapeNext })
        addAndMakeVisible(*b);

    // The dice deals this line's Random Once shape a new order. It writes no parameter - the
    // order is engine state, not something a session stores - so there is no attachment and no
    // gesture to bracket; it is one call, and the audio thread picks it up on its next block.
    diceButton.setTitle("Macro reroll " + letter);
    diceButton.setTooltip("Deal this line a new random order. Only Random Once has an order to "
                          "deal again - Random and Random Other draw a fresh note every step, "
                          "so there is nothing stored for this to change.");
    diceButton.onClick = [this] { processor.rerollArpRandom(line); };
    addAndMakeVisible(diceButton);
    // Seed the combo *before* asking the dice about it. `addItemList` selects nothing and the
    // shape has no attachment to seed it either (applyShape writes two parameters by hand), so
    // the selection is -1 until something sets it - and refreshDice reading -1 opened the
    // button greyed on a Random Once session, live only after the panel's first 10 Hz tick.
    // The comment here claimed the opposite for an afternoon, which is the tell: a card built
    // from a saved session must be *right* when it is built, not a hundred milliseconds later.
    syncShapeBox();
    refreshDice(); // so a card built on a Random Once session opens with it live

    // The settings a regular arpeggiator has, as the skin's machined rotary - the same
    // knob the band above draws the same parameters with (Owen, 2026-08-01: "what other knobs
    // can we have? should be like regular arp settings").
    for (int k = 0; k < numKnobs; ++k)
    {
        const auto name = juce::String(macroKnobSpecs[(size_t) k].heading);
        // Two of the knobs are ranges. Everything below is written against knobFace(), so a
        // range knob is attached, titled and tooltipped exactly as a plain one - the ring is
        // the only extra, and it is wired once, here. The two rings are not the same shape:
        // H.TIME's is a genuine span of its own face (the parameter is the draw's ceiling,
        // the ring is its floor); VEL's ring is a *different* parameter's own value -
        // Humanize Velocity, folded in here on 2026-08-17 (Owen: "I only want one velocity
        // knob, and I want this humanize section to be the outer ring") so loudness has one
        // knob per line instead of two two cells apart.
        if (isRangeKnob(k))
        {
            auto& rk = *(ranges[(size_t) k] = std::make_unique<RangeKnob>());
            rk.setFaceInset(arpRingPx);
            rk.setReadoutHeight(15); // the plain knobs' text box, so the row's numbers line up
            rk.setTitle("Macro " + name + " range " + letter);
            rk.spanHandle().setTitle("Macro " + name + " range handle " + letter);
            addAndMakeVisible(rk);

            if (k == kVel)
            {
                // arpHumanVel (0-100, "Human Velocity") *is* the ring here, not a span of
                // VEL's own value - VEL can sit anywhere from -100 to 100 and the ring still
                // reaches straight down from wherever that is. Its own former ring,
                // arpHumanVelSpan, is pinned to 100 by every write below rather than removed:
                // the draw was already uniform between the floor and the knob before this
                // merge (the parameter's own default), and pinning it is what keeps that true
                // with one fewer number on screen. The engine reads both parameters exactly
                // as it always did; only the card stops exposing the span as its own control.
                rk.setTooltip("This line's velocity band, 0-127: the knob is the middle of the "
                              "band and the ring is how far either side of it a hit can land, "
                              "by a different amount every time. Click the little dial at the "
                              "top left to switch Humanize Velocity on or off; drag it, or "
                              "anywhere on the ring, to set how far it reaches.");
                rk.setSpanTooltip("Drag up for a wider velocity band, down for a tighter one - "
                                  "it opens equally both ways around the knob, which stays "
                                  "put. The wheel works too. Click to switch Humanize Velocity "
                                  "on or off.");
                // **The plain lo-hi readout, same as every other range knob** (2026-08-18). It
                // read "level ~reach" while the face was a bipolar trim and the ring a percentage
                // of shave: two different quantities in two different units, which no two-number
                // format can honestly join, and which produced things like "-31 ~20" - numbers
                // that look like velocities and are not. Both ends are MIDI velocity now, so the
                // band names itself and the default format is the right one.

                const auto velId = id(KeysProcessor::apHumanVel);
                const auto spanId = id(KeysProcessor::apHumanVelSpan);
                // **The ring's own range, not the face's** (see RangeKnob::setSpanMax). VEL's
                // face is a bipolar trim, -100..100, so the default would calibrate the drag
                // to 200 units while arpHumanVel stops at 100: the top half of the satellite's
                // travel wrote nothing, and refresh() below read the clamped value back at
                // 10 Hz and yanked the arc down under a hand still moving up. Taken from the
                // parameter rather than written as 100 so the two cannot drift apart.
                // Both are 0..127 now, so this lands on the face's own travel anyway - it stays
                // read from the ring's parameter because that is the rule (see setSpanMax), not
                // because the two happen to differ today.
                // spanMax() takes half the face's travel off this again, which is what makes
                // the top of the sweep reachable: the band is centred on the face, so it can
                // never open wider than room() allows, and room() tops out at half. Before
                // that landed (2026-08-23) about four fifths of this drag wrote a parameter
                // the ring could not draw.
                const auto velRange = processor.apvts.getParameterRange(velId);
                rk.setSpanMax((double) (velRange.end - velRange.start));
                // A lamp, unlike H.TIME's ring: VEL keeps a level even with Humanize off, and
                // its knob at zero means *silent* rather than "no wander" - so no position of
                // the face can double as Humanize Velocity's own off switch the way a plain
                // Humanize knob's zero does. isOn/setOn give the lamp that job instead (2026-08-17, Owen:
                // "whether humanize velocity is on at all"); off parks the amount at zero and
                // remembers what it was, the same shape ChordPads' Strum lamp uses.
                rk.isOn = [this, velId]
                { return processor.apvts.getRawParameterValue(velId)->load() > 0.0f; };
                rk.setOn = [this, velId, spanId](bool on)
                {
                    auto* vp = processor.apvts.getParameter(velId);
                    if (vp == nullptr)
                        return;
                    if (! on)
                        lastHumanVelAmount = juce::jmax(1.0, (double) processor.apvts
                                                 .getRawParameterValue(velId)->load());
                    vp->beginChangeGesture();
                    vp->setValueNotifyingHost(vp->convertTo0to1((float) (on ? lastHumanVelAmount : 0.0)));
                    vp->endChangeGesture();
                    if (auto* sp = processor.apvts.getParameter(spanId))
                    {
                        sp->beginChangeGesture();
                        sp->setValueNotifyingHost(sp->convertTo0to1(100.0f));
                        sp->endChangeGesture();
                    }
                };
                // By hand, with the gesture brackets an attachment would have given it: the
                // ring is not a Slider, so there is nothing for a SliderAttachment to bind to.
                // The same shape Shape and the rate steppers use a few hundred lines up - and
                // now two parameters move together instead of one, since every drag also pins
                // the span.
                rk.onSpanDragStart = [this, velId, spanId]
                {
                    if (auto* p = processor.apvts.getParameter(velId))
                        p->beginChangeGesture();
                    if (auto* p = processor.apvts.getParameter(spanId))
                        p->beginChangeGesture();
                };
                rk.onSpanChanged = [this, velId, spanId](double v)
                {
                    if (auto* p = processor.apvts.getParameter(velId))
                        p->setValueNotifyingHost(p->convertTo0to1((float) v));
                    if (auto* p = processor.apvts.getParameter(spanId))
                        p->setValueNotifyingHost(p->convertTo0to1(100.0f));
                };
                rk.onSpanDragEnd = [this, velId, spanId]
                {
                    if (auto* p = processor.apvts.getParameter(velId))
                        p->endChangeGesture();
                    if (auto* p = processor.apvts.getParameter(spanId))
                        p->endChangeGesture();
                };
            }
            else // kHTime
            {
                rk.setTooltip("The knob is the typical lateness; the ring around it is how far "
                              "either side of that a hit can land. Drag the little dial at the "
                              "top left - or anywhere on the ring - to open and close it. Wide "
                              "open, hits wander from dead on the grid to twice the knob; "
                              "closed, every hit is exactly that late. Turn the knob and the "
                              "whole range moves with it.");
                rk.setSpanTooltip("Drag up to open this knob's range, down to close it - it "
                                  "opens equally both ways around the knob, which stays put. "
                                  "The wheel works too.");
                // Both ends in one readout, in the knob's own units - a range that only shows one
                // of its ends is the readout problem the arp rate had this morning.
                rk.textFromRange = [](double lo, double hi)
                { return juce::String((int) lo) + "-" + juce::String((int) hi); };

                const auto spanId = id(KeysProcessor::apHumanizeSpan);
                // Set for the same reason as VEL's above, and here it names exactly the
                // number the default already produced: face and ring are both 0..100. It is
                // kept so the rule reads the same on both knobs rather than as a special case
                // living on one of them - spanMax() halves it either way, which is what stopped
                // 228 px of this 300 px drag being inert at the shipping default of 24.
                const auto humanRange = processor.apvts.getParameterRange(spanId);
                rk.setSpanMax((double) (humanRange.end - humanRange.start));
                // By hand, with the gesture brackets an attachment would have given it: the ring
                // is not a Slider, so there is nothing for a SliderAttachment to bind to. The
                // same shape Shape and the rate steppers use a few hundred lines up.
                rk.onSpanDragStart = [this, spanId]
                {
                    if (auto* p = processor.apvts.getParameter(spanId))
                        p->beginChangeGesture();
                };
                rk.onSpanChanged = [this, spanId](double v)
                {
                    if (auto* p = processor.apvts.getParameter(spanId))
                        p->setValueNotifyingHost(p->convertTo0to1((float) v));
                };
                rk.onSpanDragEnd = [this, spanId]
                {
                    if (auto* p = processor.apvts.getParameter(spanId))
                        p->endChangeGesture();
                };
            }
        }

        auto& knob = knobFace(k);
        knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        // Read-only text box, like every other knob here: the value belongs under the knob, and
        // an editable box is a keyboard target on a surface that has none. A range knob draws
        // its own readout instead, since it has two numbers to show.
        if (! isRangeKnob(k))
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 52, 15);
        knob.setTooltip(macroKnobSpecs[(size_t) k].tip);
        knob.setTitle("Macro " + name + " " + letter);
        if (! isRangeKnob(k))
            addAndMakeVisible(knob); // a range knob's face is already its own child
        if (k != kOctShift)          // its heading is already made, above
            heading(knobLabels[(size_t) k], macroKnobSpecs[(size_t) k].heading);
        knobAtts[(size_t) k] = std::make_unique<SliderAtt>(
            processor.apvts, id(macroKnobSpecs[(size_t) k].param), knob);
        // **The attachment is what gives the face its range, and spanMax() reads that range.**
        // So the setSpanMax() calls above - a hundred lines up, inside the range-knob branch -
        // ran while the face was still on JUCE's default 0..10 and re-clamped at a ceiling of
        // five. It is inert today only because the span is still zero at that point, which is
        // luck rather than design: the whole point of that re-clamp is to bite when a ceiling
        // narrows under a live span. Re-running it here, once the face knows its own range, is
        // what makes the ordering stop mattering.
        if (auto& rk = ranges[(size_t) k])
            rk->reclampSpan();
    }

    // The two fixed harmony voices (2026-08-19, BigSky's shimmer list). The combo picks the
    // interval, the knob beside it says how often that voice fires; both are ordinary
    // attachments to this line's own appended parameters, bound for good like everything else
    // on a card. The knob matches the strip above it - same rotary, same read-only readout -
    // so the row reads as more of the card rather than a different instrument.
    for (int s = 0; s < 2; ++s)
    {
        const auto v = juce::String(s + 1);
        auto& box = harmBoxes[(size_t) s];
        box.addItemList(KeysProcessor::harmonyChoices(), 1);
        box.setTitle("Macro harmony " + v + " " + letter);
        box.setTooltip("A fixed interval this line adds " + juce::String(s == 0 ? "as its first"
                       : "as its second") + " harmony voice, above or below every note it "
                       "plays. Off is silence; the list is chromatic on purpose, so a Major "
                       "3rd is a Major 3rd whatever the scale says.");
        addAndMakeVisible(box);
        harmAtts[(size_t) s] = std::make_unique<ComboAtt>(
            processor.apvts, id(s == 0 ? KeysProcessor::apHarm1 : KeysProcessor::apHarm2), box);

        auto& knob = harmChanceKnobs[(size_t) s];
        knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 52, 15);
        knob.setTitle("Macro harmony " + v + " chance " + letter);
        knob.setTooltip("How often harmony voice " + v + " actually fires, per step. 100 is "
                        "every note, lower thins it into something that glints rather than "
                        "doubles.");
        addAndMakeVisible(knob);
        harmChanceAtts[(size_t) s] = std::make_unique<SliderAtt>(
            processor.apvts,
            id(s == 0 ? KeysProcessor::apHarm1Chance : KeysProcessor::apHarm2Chance), knob);

        heading(harmLabels[(size_t) (s * 2)], s == 0 ? "HARMONY 1" : "HARMONY 2");
        heading(harmLabels[(size_t) (s * 2 + 1)], "CHANCE");
    }

    chordLabel.setJustificationType(juce::Justification::centred);
    chordLabel.setFont(skin::uiSemi(13.0f));
    addAndMakeVisible(chordLabel);

    refreshRateMode();
    refresh();
}

// One dial, two parameters, two units - the band's `refreshRateMode` for one row. The
// attachment that is alive decides the dial's range, its detents and its readout, so the swap
// has to happen on every mode change; it cannot happen under an open drag, because the
// attachment that started the gesture has to be the one that ends it.
void ArpPanel::MacroRow::refreshRateMode()
{
    const bool free = processor.apvts.getRawParameterValue(
                          KeysProcessor::arpParamId(line, KeysProcessor::apRateFree))->load() > 0.5f;
    rateModeButton.setButtonText(free ? "Hz" : "Sync");

    // Dot, Tuplet and Anchor all subdivide or align against a *beat*, and in Hz there is no beat:
    // the engine ignores all three there, so they grey out rather than sitting lit and doing
    // nothing. Same rule and the same words as the band's - see ArpPanel::refreshRateMode.
    // Outside the early-out below, which only guards the attachment swap: these have to be
    // right on the first call too, when lastRateFree is still -1 and a Hz session has just
    // been restored.
    dotButton.setEnabled(! free);
    tupletBox.setEnabled(! free);
    anchorButton.setEnabled(! free);
    anchorButton.setTooltip(free ? "Nothing to anchor to in Hz: a free-running rate follows no "
                                   "bar grid. Switch the rate to Sync to lock the steps to one."
                                 : "Anchored: locked to the host bar grid. Free: never jumps, "
                                   "may drift.");

    if (lastRateFree == (int) free)
        return;
    if (rateDragging)
        return; // onDragEnd calls back
    lastRateFree = (int) free;
    rateSyncAtt.reset();
    rateHzAtt.reset();
    if (free)
        rateHzAtt = std::make_unique<SliderAtt>(
            processor.apvts, KeysProcessor::arpParamId(line, KeysProcessor::apRateHz), rateKnob);
    else
        rateSyncAtt = std::make_unique<SliderAtt>(
            processor.apvts, KeysProcessor::arpParamId(line, KeysProcessor::apRate), rateKnob);
    installRateText(); // the new attachment has just overwritten the readout's text function
}

// A knob column is either a plain rotary or a RangeKnob wrapping one. These two are the only
// places that care which, so nothing else in the row has to branch.
juce::Component& ArpPanel::MacroRow::knobCell(int k)
{
    if (auto& r = ranges[(size_t) juce::jlimit(0, numKnobs - 1, k)])
        return *r;
    return knobs[(size_t) juce::jlimit(0, numKnobs - 1, k)];
}

juce::Slider& ArpPanel::MacroRow::knobFace(int k)
{
    if (auto& r = ranges[(size_t) juce::jlimit(0, numKnobs - 1, k)])
        return r->face();
    return knobs[(size_t) juce::jlimit(0, numKnobs - 1, k)];
}

// The band's two, for one row. Same rules, same words - see ArpPanel::refreshTuplet and
// ArpPanel::installRateText, which carry the reasoning.
void ArpPanel::MacroRow::refreshTuplet()
{
    auto& apvts = processor.apvts;
    const int n = KeysProcessor::tupletFor((int) apvts.getRawParameterValue(
        KeysProcessor::arpParamId(line, KeysProcessor::apTuplet))->load());
    const int dotted = apvts.getRawParameterValue(
                           KeysProcessor::arpParamId(line, KeysProcessor::apDot))->load() > 0.5f;
    if (n == lastTuplet && dotted == lastDotted)
        return;
    lastTuplet = n;
    lastDotted = dotted;
    rateKnob.updateText();
}

void ArpPanel::MacroRow::installRateText()
{
    if (rateSyncAtt == nullptr)
        return;
    rateKnob.textFromValueFunction = [this](double v)
    {
        auto& apvts = processor.apvts;
        const int n = KeysProcessor::tupletFor((int) apvts.getRawParameterValue(
            KeysProcessor::arpParamId(line, KeysProcessor::apTuplet))->load());
        const bool dot = apvts.getRawParameterValue(
                             KeysProcessor::arpParamId(line, KeysProcessor::apDot))->load() > 0.5f;
        return ArpEngine::rateSyncText((int) std::lround(v), dot, n);
    };
    rateKnob.updateText();
}

// Shape spans two parameters here exactly as it does in the band above (arpDirection plus
// arpPattern), so it cannot be an attachment and the gestures are bracketed by hand.
void ArpPanel::MacroRow::applyShape()
{
    const int chosen = shapeBox.getSelectedItemIndex();
    auto& apvts = processor.apvts;
    if (auto* pat = dynamic_cast<juce::AudioParameterBool*>(
            apvts.getParameter(KeysProcessor::arpParamId(line, KeysProcessor::apPattern))))
    {
        pat->beginChangeGesture();
        *pat = chosen >= ArpEngine::numDirections;
        pat->endChangeGesture();
    }
    if (chosen >= 0 && chosen < ArpEngine::numDirections)
        if (auto* dir = dynamic_cast<juce::AudioParameterChoice*>(
                apvts.getParameter(KeysProcessor::arpParamId(line, KeysProcessor::apDirection))))
        {
            dir->beginChangeGesture();
            *dir = chosen;
            dir->endChangeGesture();
        }
    refreshDice();
}

// Only Random Once has a stored order for the dice to deal again (2026-08-21). Random and
// Random Other draw a fresh note every step, and every other shape is a walk with no randomness
// in it at all - so on those the button greys rather than vanishing, which keeps the row from
// reflowing under the mouse and keeps the dice discoverable when you do land on the shape.
//
// One method rather than the test written twice: applyShape calls it the instant the combo
// moves (the 10 Hz refresh would otherwise leave the button a tick behind the shape it belongs
// to) and refresh calls it for every other route a shape can change - a host lane, a session
// load, an MCP client, the steppers.
void ArpPanel::MacroRow::syncShapeBox()
{
    auto& apvts = processor.apvts;
    const bool pattern = apvts.getRawParameterValue(
                             KeysProcessor::arpParamId(line, KeysProcessor::apPattern))->load() > 0.5f;
    const int dir = (int) apvts.getRawParameterValue(
                        KeysProcessor::arpParamId(line, KeysProcessor::apDirection))->load();
    // Pattern is the entry past the directions, which is why it is added separately above.
    const int wanted = pattern ? ArpEngine::numDirections
                               : juce::jlimit(0, ArpEngine::numDirections - 1, dir);
    if (shapeBox.getSelectedItemIndex() != wanted)
        shapeBox.setSelectedItemIndex(wanted, juce::dontSendNotification); // never re-enters applyShape
}

void ArpPanel::MacroRow::refreshDice()
{
    const bool canDeal = shapeBox.getSelectedItemIndex() == (int) ArpEngine::Direction::randomOnce;
    // No guard needed: Component::setEnabled opens with `if (flags.isEnabledFlag != shouldBe)`
    // and returns without repainting otherwise, so a 10 Hz call on an unchanged value already
    // costs nothing. A wrapper here would only teach the next timer-driven control to add one.
    diceButton.setEnabled(canDeal);
}

void ArpPanel::MacroRow::stepShape(int delta)
{
    const int n = shapeBox.getNumItems();
    if (n <= 0)
        return;
    // Stops at the ends rather than wrapping, like the band's own pair: a stepper that wraps
    // turns "one past the last" into "the first", which is never what the click meant.
    const int next = juce::jlimit(0, n - 1, shapeBox.getSelectedItemIndex() + delta);
    if (next != shapeBox.getSelectedItemIndex())
        shapeBox.setSelectedItemIndex(next); // fires onChange -> applyShape
}

void ArpPanel::MacroRow::stepRate(int delta)
{
    auto& apvts = processor.apvts;
    const auto rateId = KeysProcessor::arpParamId(line, KeysProcessor::apRate);
    const auto hzId = KeysProcessor::arpParamId(line, KeysProcessor::apRateHz);

    if (apvts.getRawParameterValue(KeysProcessor::arpParamId(line, KeysProcessor::apRateFree))->load() > 0.5f)
    {
        // A click is a quarter of an octave, so four halve or double the rate - the same jump
        // one entry of the Sync list makes. Identical rule to ArpPanel::stepRate; see there.
        if (auto* hz = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(hzId)))
        {
            const double cur = juce::jlimit((double) ArpEngine::minRateHz, (double) ArpEngine::maxRateHz,
                                            (double) hz->get());
            const double rungs = std::log2(cur) * 4.0;
            const double wanted = std::pow(2.0, (std::floor(rungs + 1.0e-6) + delta) / 4.0);
            hz->beginChangeGesture();
            *hz = (float) juce::jlimit((double) ArpEngine::minRateHz, (double) ArpEngine::maxRateHz, wanted);
            hz->endChangeGesture();
        }
        return;
    }

    if (auto* rate = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(rateId)))
    {
        const int next = juce::jlimit(0, rate->choices.size() - 1, rate->getIndex() + delta);
        if (next != rate->getIndex())
        {
            rate->beginChangeGesture();
            *rate = next;
            rate->endChangeGesture();
        }
    }
}

void ArpPanel::MacroRow::refresh()
{
    refreshRateMode(); // a host can automate the unit out from under us
    refreshTuplet();   // ... and the tuplet, which has no attachment to hear it change

    auto& apvts = processor.apvts;
    // ... and the two ring values, for the same reason: the ring writes its parameter but
    // nothing reads it back, so a session load, a host lane or an MCP client would otherwise
    // move the value and leave the arc where it was. setSpan no-ops when nothing changed, so
    // this costs a comparison per tick and never fights an open drag. H.TIME's ring reads its
    // own span parameter, same as ever; VEL's reads arpHumanVel directly, since that is what
    // its ring now *is* rather than a span of VEL's own value - and the same read is what the
    // lamp's isOn() already uses, so the two can never disagree about whether Humanize
    // Velocity is on.
    for (int k = 0; k < numKnobs; ++k)
        if (auto& rk = ranges[(size_t) k])
            rk->setSpan(apvts.getRawParameterValue(
                                 KeysProcessor::arpParamId(line, k == kHTime
                                                                     ? KeysProcessor::apHumanizeSpan
                                                                     : KeysProcessor::apHumanVel))
                            ->load());
    syncShapeBox();
    refreshDice();

    // What this line is holding, and whether something is on its way. A launch waiting on a
    // quantize boundary says so, because otherwise the click looks like it did nothing.
    const auto& held = processor.arpHeldName(line);
    if (processor.arpLaunchPending(line))
        chordLabel.setText("...", juce::dontSendNotification);
    else
        chordLabel.setText(held.isNotEmpty() ? held : juce::String("--"), juce::dontSendNotification);
    chordLabel.setColour(juce::Label::textColourId,
                         held.isNotEmpty() ? skin::text : skin::textFaint);

    // The scrim in paintOverChildren reads processor.arpLineOn() live, so this cache exists
    // only to decide when to ask for a repaint - driven by the panel's 10 Hz timer while the
    // macro view is up, which is the same tick that already moves the chord readout above.
    const bool lineOn = processor.arpLineOn(line);
    if (lineOn != lastLineOn)
    {
        lastLineOn = lineOn;
        repaint();
    }
}

void ArpPanel::MacroRow::setDropTarget(bool b)
{
    if (dropTarget == b)
        return;
    dropTarget = b;
    repaint();
}

// Same source rule as a slot card, and one thing more that used to need saying by hand: a row
// that is not on screen is never hit, so the `macroView` test the old screen hit-test carried
// is now the row's own visibility.
bool ArpPanel::MacroRow::isInterestedInDragSource(const SourceDetails& details)
{
    return chorddrag::chordBeingDragged(details) != nullptr;
}

void ArpPanel::MacroRow::itemDragEnter(const SourceDetails&) { setDropTarget(true); }
void ArpPanel::MacroRow::itemDragExit(const SourceDetails&) { setDropTarget(false); }

void ArpPanel::MacroRow::itemDropped(const SourceDetails& details)
{
    setDropTarget(false);
    if (auto* p = isInterestedInDragSource(details) ? chorddrag::of(details) : nullptr)
    {
        owner.takeChordOnLine(line, *p);
        p->taken = true;
    }
}

void ArpPanel::MacroRow::paint(juce::Graphics& g)
{
    // A chord card is over this card: the whole thing lights, because the card *is* the line
    // here and a target you can only hit by aiming at a 40 px tab is a target a single mouse
    // fights.
    if (dropTarget)
    {
        const auto accent = skin::accentOf(*this).base;
        const auto b = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(accent.withAlpha(0.10f));
        g.fillRoundedRectangle(b, skin::radius);
        g.setColour(accent);
        g.drawRoundedRectangle(b, skin::radius, 2.0f);
        return; // the frame would only fight the outline
    }

    // Each card is its own captioned, ruled box - the band's group-frame look, with the
    // line's name punched through the top rule and a fill behind it (2026-08-02, Owen: "we
    // need a bit more clear delineation between the two arpeggiators. They kinda look like
    // one right now"). The fill and a rule brighter than the groups' 0.07 are what make two
    // side-by-side cards read as two machines rather than one field of knobs.
    const auto r = getLocalBounds().toFloat().reduced(1.0f);
    const float capY = r.getY() + 5.0f;
    const auto caption = "LINE " + juce::String::charToString((juce::juce_wchar) ('A' + line));
    const auto font = skin::micro(9.5f).withExtraKerningFactor(0.16f);
    const auto textW = juce::GlyphArrangement::getStringWidth(font, caption);

    // The line's own colour (2026-08-19, Owen: "each one should have a color"), worn by the
    // fill, the frame and the caption - the marks that say *which line* - and by nothing on
    // the card that is a control, so the skin's one-accent law bends here rather than breaks.
    const auto tint = skin::lineAccent(line).base;
    g.setColour(tint.withAlpha(0.045f));
    g.fillRoundedRectangle(r.withTrimmedTop(capY - r.getY()), skin::radius);

    g.setColour(tint.withAlpha(0.38f));
    juce::Path frame;
    frame.startNewSubPath(r.getCentreX() - textW * 0.5f - 8.0f, capY);
    frame.lineTo(r.getX() + skin::radius, capY);
    frame.quadraticTo(r.getX(), capY, r.getX(), capY + skin::radius);
    frame.lineTo(r.getX(), r.getBottom() - skin::radius);
    frame.quadraticTo(r.getX(), r.getBottom(), r.getX() + skin::radius, r.getBottom());
    frame.lineTo(r.getRight() - skin::radius, r.getBottom());
    frame.quadraticTo(r.getRight(), r.getBottom(), r.getRight(), r.getBottom() - skin::radius);
    frame.lineTo(r.getRight(), capY + skin::radius);
    frame.quadraticTo(r.getRight(), capY, r.getRight() - skin::radius, capY);
    frame.lineTo(r.getCentreX() + textW * 0.5f + 8.0f, capY);
    g.strokePath(frame, juce::PathStrokeType(1.0f));

    g.setFont(font);
    g.setColour(tint);
    g.drawText(caption, getLocalBounds().withHeight(12).withY((int) capY - 6),
               juce::Justification::centred);
}

// A line that is off still takes chords in (CLAUDE.md), so this greys the card without
// touching setEnabled anywhere on it: every knob, the rate dial and the card itself as a drop
// target all have to keep receiving mouse events. Painted over the children rather than under
// them, and skipped while dropTarget is true so the drop highlight in paint() above is never
// muddied by it.
void ArpPanel::MacroRow::paintOverChildren(juce::Graphics& g)
{
    if (dropTarget || processor.arpLineOn(line))
        return;

    // Same inset and the same rounded body the fill in paint() uses, trimmed below the LINE A
    // / LINE B caption strip so the name stays legible while everything under it dims.
    const auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(skin::bgBot.withAlpha(0.38f));
    g.fillRoundedRectangle(r.withTrimmedTop((float) arpMacroCap), skin::radius);
}

void ArpPanel::MacroRow::resized()
{
    // Three lines inside one card (2026-08-02, Owen: "having the arpeggiators parallel to
    // each other instead of one on top of the other"): what the line plays (On, rate,
    // shape), the nine knobs under their own heading strip (seven when VEL absorbed H.VEL
    // as its own ring, 2026-08-17), and the rate's modifiers with the held chord. Stacked
    // *inside* the card because two cards now share the panel's width, and the old single
    // line needed more width than half the panel has. The heights here are
    // arpMacroCap / arpMacroLine / arpMacroHeads / arpMacroMods; arpMacroCard is their sum
    // and has to agree with this function exactly - the knob *count* changed with the merge,
    // but every one of those heights is independent of it, so arpMacroCard did not move.
    auto full = getLocalBounds().reduced(arpMacroRowInset, 0);
    full.removeFromTop(arpMacroCap); // the LINE A / LINE B caption rule, drawn by paint()
    // The single-height controls sit centred against the knobs beside them.
    const auto centred = [](juce::Rectangle<int> c) { return c.withSizeKeepingCentre(c.getWidth(), 26); };

    const auto heads1 = full.removeFromTop(arpMacroHeads);
    auto line1 = full.removeFromTop(arpMacroLine);
    {
        auto r = line1;
        const auto take = [&r](int w) { auto c = r.removeFromLeft(w); r.removeFromLeft(6); return c; };
        // onButton's 40 px (plus its 6 px gap) is gone from this line since 2026-08-02, and
        // deliberately not replaced with a spacer: everything below simply shifts left, and
        // that freed width lands on shapeBox, the one elastic control on this line and the
        // tightest thing in the card before this.
        ratePrev.setBounds(centred(take(26)));
        rateKnob.setBounds(take(58));
        rateNext.setBounds(centred(take(26)));
        rateModeButton.setBounds(centred(take(42)));
        // Shape's steppers are reserved before its combo takes the rest: they are targets
        // and it is a readout, the same reserve-the-fixed-thing-first ordering the old
        // single-line layout paid for twice (see the 2026-08-02 entries in CLAUDE.md).
        //
        // **The combo is capped now** (2026-08-21, Owen: "the shape of the arpeggiator drop
        // down doesn't need to be so big. Make it smaller"). It was the one elastic control on
        // this line, so on any window wider than the floor it swallowed everything the row had
        // left - about 540 px on Owen's screen to hold "Fingered Bottom", the longest of the
        // fifteen names. `arpMacroShapeMaxW` is that name with room around it; past that the
        // row simply ends, and the slack sits at the right where the dice is rather than
        // stretching a readout that has nothing more to say.
        //
        // The dice's own cell comes out of the row first and on every shape, greyed or not:
        // reserve the fixed thing before the elastic one takes the rest, and never let a
        // control appear or vanish under the mouse. It is *placed* after the shape group
        // rather than at the far end of the row - Owen asked for it "nearby", and pinned right
        // it sat a clear 250 px from the thing it acts on, reading as unrelated to it. The 14
        // px gap is wider than the 6 px between the shape's own parts, which is what keeps it
        // from being mistaken for a third stepper. Whatever the row has left over then falls
        // at the end, where empty space costs nothing.
        const int diceW = 34;
        shapePrev.setBounds(centred(r.removeFromLeft(26)));
        r.removeFromLeft(6);
        const int shapeW = juce::jmin(arpMacroShapeMaxW,
                                      juce::jmax(0, r.getWidth() - 26 - 6 - 14 - diceW));
        shapeBox.setBounds(centred(r.removeFromLeft(shapeW)));
        r.removeFromLeft(6);
        shapeNext.setBounds(centred(r.removeFromLeft(26)));
        r.removeFromLeft(14);
        diceButton.setBounds(centred(r.removeFromLeft(diceW)));
    }
    // The RATE / SHAPE names span their whole group, steppers included, so the arrows can
    // only be read as belonging to the word above them. Placed from the controls, as ever.
    rateHeadLabel.setBounds(ratePrev.getX(), heads1.getY(),
                            rateModeButton.getRight() - ratePrev.getX(), heads1.getHeight());
    shapeHeadLabel.setBounds(shapePrev.getX(), heads1.getY(),
                             shapeNext.getRight() - shapePrev.getX(), heads1.getHeight());

    const auto headStrip = full.removeFromTop(arpMacroHeads);
    auto knobLine = full.removeFromTop(arpMacroKnobLine);
    // `arpMacroKnobMinW` keeps the card solvable at the *narrowest place this view is ever
    // drawn*, and knobs stop growing at 96 as before. **Nine since 2026-08-21**, when STRAY
    // joined the row: 9*38 + the two rings + eight gaps is 422 px, which is what
    // `minMacroWidth()` is derived from, so the floor tracks the count instead of being a
    // number somebody has to remember to re-measure.
    //
    // That is the lesson of the ninth knob rather than a note about it. The docked editor was
    // re-measured and fitted fine (a column there is ~614 px); the **detached** Arp window was
    // not, and its own 900 px minimum left a column of 420 - two pixels of clamp away from
    // starving H.TIME's range knob to a 16 px face. A view that can be drawn in two windows has
    // two floors, and the smaller one is the one that binds. A tenth knob must buy the width -
    // raise the floors, never starve the row.
    //
    // The two range knobs are `each` wide *plus their ring on both sides*, reserved out of the
    // row here rather than taken off a neighbour later: the face inside a range knob is then
    // exactly as wide as every plain one, so the row reads as nine knobs of one size with a
    // ring round two of them, which is what it is.
    const int rings = 2 * 2 * arpRingPx; // two range knobs, a ring either side of each
    const int each = juce::jlimit(arpMacroKnobMinW, 96,
                                  (knobLine.getWidth() - rings - 6 * (numKnobs - 1)) / numKnobs);
    auto knobStrip = knobLine.removeFromLeft(each * numKnobs + rings + 6 * (numKnobs - 1));
    for (int k = 0; k < numKnobs; ++k)
    {
        const bool ranged = isRangeKnob(k);
        auto cell = knobStrip.removeFromLeft(each + (ranged ? 2 * arpRingPx : 0));
        // A plain knob drops its top by the full ring so its *readout* lines up with a range
        // knob's, which is the alignment the eye actually checks along a row of numbers. Its
        // face then sits a few pixels lower inside its cell than a ringed one does, and the
        // ring fills exactly that space, so the two still read as the same size.
        knobCell(k).setBounds(ranged ? cell : cell.withTrimmedTop(2 * arpRingPx));
        knobStrip.removeFromLeft(6);
    }
    // Headings are placed from the knob they name, not by walking a second copy of the
    // layout: one source of truth for where a column is, so they cannot drift apart.
    const auto headFor = [&headStrip](juce::Label& l, const juce::Component& c)
    { l.setBounds(c.getX(), headStrip.getY(), c.getWidth(), headStrip.getHeight()); };
    for (int k = 0; k < numKnobs; ++k)
        headFor(knobLabels[(size_t) k], knobCell(k));

    // The harmony area, two columns (2026-08-19, second pass - Owen: "make harmony 2
    // columns"): one column per voice, its dropdown stacked over its chance knob, the way the
    // BigSky panel pairs a shift with its amount. The first cut ran all four controls in one
    // row, which read as a strip of parts rather than as two voices; a column is the pairing
    // drawn as geometry. Headings are placed from the controls, as ever.
    const auto harmHeads = full.removeFromTop(arpMacroHeads);
    auto harmCombos = full.removeFromTop(arpMacroHarmCombo);
    const auto chanceHeads = full.removeFromTop(arpMacroHeads);
    auto harmKnobs = full.removeFromTop(arpMacroLine);
    {
        const int gap = 12;
        const int half = (harmCombos.getWidth() - gap) / 2;
        for (int s = 0; s < 2; ++s)
        {
            auto comboCell = s == 0 ? harmCombos.removeFromLeft(half) : harmCombos;
            auto knobCol = s == 0 ? harmKnobs.removeFromLeft(half) : harmKnobs;
            if (s == 0)
            {
                harmCombos.removeFromLeft(gap);
                harmKnobs.removeFromLeft(gap);
            }
            auto& box = harmBoxes[(size_t) s];
            box.setBounds(comboCell.withSizeKeepingCentre(comboCell.getWidth(), 34));
            auto& knob = harmChanceKnobs[(size_t) s];
            knob.setBounds(knobCol.withSizeKeepingCentre(52, knobCol.getHeight()));
            harmLabels[(size_t) (s * 2)].setBounds(box.getX(), harmHeads.getY(),
                                                   box.getWidth(), harmHeads.getHeight());
            harmLabels[(size_t) (s * 2 + 1)].setBounds(box.getX(), chanceHeads.getY(),
                                                       box.getWidth(), chanceHeads.getHeight());
        }
    }

    // The rate's modifiers keep their full 34 px hit height - they are targets, and wide
    // enough for the word plus its tick, since a bare tick box beside "Dot" would be two
    // controls' worth of ambiguity in a card that already has nine unlabelled knobs. The
    // held chord sits at the card's bottom-right corner, where a dropped card lands.
    full.removeFromTop(2);
    auto subRow = full.removeFromTop(arpMacroMods);
    chordLabel.setBounds(centred(subRow.removeFromRight(arpMacroChordW)));
    subRow.removeFromRight(arpMacroChordGap);
    const auto takeMod = [&subRow](int w)
    { auto c = subRow.removeFromLeft(w); subRow.removeFromLeft(arpMacroModGap); return c; };
    // Every cell is a named constant and their sum is arpMacroModsW, which minMacroWidth()
    // takes against the knob strip: the row is never asked to fit in less than it needs, so
    // takeMod's clamp on the last cell - which is what a starved row looks like, Details
    // drawn as a sliver with nothing to say why - cannot bite at either window's floor.
    // LayoutTests measures the five chips and the readout at both. Full 34 px height, unlike
    // the band's 28 - the strip is 34 already.
    dotButton.setBounds(takeMod(arpMacroModDot));
    tupletBox.setBounds(takeMod(arpMacroModTuplet));
    anchorButton.setBounds(takeMod(arpMacroModAnchor));
    legatoButton.setBounds(takeMod(arpMacroModLegato));
    detailsButton.setBounds(takeMod(arpMacroModDetails));
}

// The LineTab class lived here until 2026-08-02, when the A/B/All tabs moved to the ARP
// section bar (Owen: "move the bpm and the a b and all into the header also"); the bar
// belongs to the editor, so the tabs do too now - see KeysEditor::ArpBarTab.

void ArpPanel::SlotCard::paintButton(juce::Graphics& g, bool over, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced(1.0f);
    const auto accent = skin::accentOf(*this).base;
    const auto& slot = processor.arpPatternSlot(index, owner.editLine());
    const bool active = processor.arpActivePattern(owner.editLine()) == index;   // its lanes are the live ones
    const bool launched = processor.arpLaunchedSlot(owner.editLine()) == index;  // its chord is sounding

    skin::raisedFill(g, b, skin::radius,
                     launched ? accent.withAlpha(0.34f) : skin::control.withAlpha(down ? 0.7f : 1.0f),
                     launched ? accent.withAlpha(0.20f) : skin::controlBot);
    if (over || dropTarget)
    {
        g.setColour(juce::Colours::white.withAlpha(dropTarget ? 0.14f : 0.06f));
        g.fillRoundedRectangle(b, skin::radius);
    }
    // Active = these are the lanes the step editor is editing. Launched = its chord is
    // what the arp is chewing on. They are different things and both need to be visible.
    if (active || launched || dropTarget)
    {
        g.setColour(dropTarget ? accent : (launched ? accent : accent.withAlpha(0.55f)));
        g.drawRoundedRectangle(b.reduced(0.75f), skin::radius,
                               dropTarget ? 2.0f : (launched ? 1.6f : 1.0f));
    }

    auto area = b.reduced(5.0f, 4.0f);

    g.setColour(launched ? accent : skin::textFaint);
    g.setFont(skin::micro(9.0f));
    const auto top = area.removeFromTop(11.0f);
    g.drawText(juce::String(index + 1), top, juce::Justification::topLeft);
    // How many bars the chain holds this slot for, opposite the number. Only when it is not
    // the default: twelve cards each saying "x1" would be twelve pieces of noise for the one
    // case where the answer does not matter.
    if (const int bars = processor.arpSlotBars(index, owner.editLine()); bars > 1)
        g.drawText("x" + juce::String(bars), top, juce::Justification::topRight);

    // The chord it will play, which is the whole reason a slot is a card and not a letter.
    g.setColour(slot.chordNotes.empty() ? skin::textFaint : skin::text);
    g.setFont(skin::uiSemi(13.0f));
    g.drawText(slot.chordName.isNotEmpty() ? slot.chordName
                                           : (slot.chordNotes.empty() ? juce::String("--")
                                                                      : juce::String("Chord")),
               area.removeFromTop(17.0f), juce::Justification::centred, false);

    // What it will install: the shape and the rate, or "--" where the slot leaves the
    // current setting alone.
    // One name per shape, then Pattern - and it has to stay one per shape: the guard below
    // admits everything up to numDirections, so a shape added without a name here reads off
    // the end of this array.
    static const char* shapeNames[] = { "Up", "Down", "Up-Dn", "Dn-Up",
                                        "Up&Dn", "Dn&Up", "Played", "Rev",
                                        "Rnd", "Rnd-O", "Rnd-1", "Chord",
                                        "Fing-B", "Fing-T", "Pattern" };
    static_assert(sizeof(shapeNames) / sizeof(shapeNames[0]) == ArpEngine::numDirections + 1,
                  "every shape needs a card label, plus Pattern");
    static const char* rateNames[] = { "16 bar", "8 bar", "4 bar", "2 bar", "1 bar",
                                       "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
    juce::String sub;
    if (slot.shape >= 0 && slot.shape <= ArpEngine::numDirections)
        sub = shapeNames[slot.shape];
    if (slot.rate >= 0 && slot.rate < (int) (sizeof(rateNames) / sizeof(rateNames[0])))
        // A slot captured in Hz says Hz. `rate` still holds a division (it is captured
        // whatever the mode is), so printing it regardless would name a speed the launch
        // will not play.
        //
        // The parameter's own decimals-by-decade rule, not a decimal place of the card's
        // choosing: at one place 0.031 Hz and 0.062 Hz both painted as "0.0Hz" and 0.125 as
        // "0.1Hz", so a card naming a rate could name a stopped arp - which is the one thing
        // printing the rate at all is here to prevent.
        sub += (sub.isEmpty() ? "" : " ")
             + (slot.rateFree ? ArpEngine::rateHzText(slot.rateHz) + "Hz"
                              : juce::String(rateNames[slot.rate]));
    g.setColour(skin::textDim);
    g.setFont(skin::ui(10.0f));
    g.drawText(sub.isEmpty() ? juce::String("--") : sub, area.removeFromTop(13.0f),
               juce::Justification::centred, false);

    // The launch triangle, bottom-right: the eye's target, though the whole card is live.
    const auto tri = area.removeFromBottom(12.0f).removeFromRight(16.0f);
    juce::Path p;
    p.addTriangle(tri.getX(), tri.getY(), tri.getX(), tri.getBottom(), tri.getRight(), tri.getCentreY());
    g.setColour(launched ? accent : skin::textFaint);
    g.fillPath(p);
}

// Every attachment on the panel, bound to the current line. Split out of buildControls so
// setEditLine can tear them down and build them again against another line's ids; the rate
// dial's two are not here because refreshRateMode owns which of them exists (see there).
void ArpPanel::buildAttachments()
{
    rateModeAtt = std::make_unique<ButtonAtt>(processor.apvts, paramId(KeysProcessor::apRateFree), rateModeButton);
    dotAtt = std::make_unique<ButtonAtt>(processor.apvts, paramId(KeysProcessor::apDot), dotButton);
    tupletAtt = std::make_unique<ComboAtt>(processor.apvts, paramId(KeysProcessor::apTuplet), tupletBox);
    // The readout is not an attachment's business: force a miss so it redraws against whatever
    // line we have just moved to.
    lastTuplet = -1;
    refreshTuplet();
    anchorAtt = std::make_unique<ButtonAtt>(processor.apvts, paramId(KeysProcessor::apAnchor), anchorButton);
    octavesAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apOctaves), octavesSlider);
    distanceAtt = std::make_unique<ComboAtt>(processor.apvts, paramId(KeysProcessor::apDistance), distanceBox);
    offsetAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apOffset), offsetSlider);
    rampAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apVelRamp), rampSlider);
    rampTimeAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apRampBeats), rampTimeSlider);
    humanAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apHumanize), humanSlider);
    humanVelAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apHumanVel), humanVelSlider);
    driftAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apDrift), driftSlider);
    keysBandAtt = std::make_unique<ButtonAtt>(processor.apvts, paramId(KeysProcessor::apKeys), keysBandButton);
    swingAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apSwing), swingSlider);
    gateAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apGate), gateSlider);
    densityAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apChance), densitySlider);
    latchAtt = std::make_unique<ButtonAtt>(processor.apvts, paramId(KeysProcessor::apLatch), latchButton);
    linkAtt = std::make_unique<ButtonAtt>(processor.apvts, paramId(KeysProcessor::apLinkLanes), linkButton);
}

void ArpPanel::buildControls()
{
    // No title, On or Close here: the Arp section bar above the panel carries all three
    // (see KeysEditor). They are still members so the overlay mode this class kept for
    // Keys Host has something to fall back on, but nothing parents them any more.

    // Globals: rate + feel.
    //
    // Rate is the band's fourth knob (2026-07-30), where it was a list of eleven divisions.
    // The dial is the kit's rotary, the same one KnobBank uses, because a rate that can be a
    // *frequency* has no list to be: 0.031 to 32 Hz is a continuum, and a combo of it would be
    // either a menu of guesses or a number to type. In Sync the attachment gives it eleven
    // detents, one per division, so it still cannot land between two.
    styleLabel(rateLabel, "Rate");
    addAndMakeVisible(rateLabel);
    // 60 px of text box, not the 52 the PLAYBACK knobs use: this one prints "16 bars" and
    // "0.031 Hz", where those three print a number.
    //
    // Read-only, and every text box in this panel is: the second argument is isReadOnly, and
    // passing false makes JUCE call valueBox->setEditable(true), which is edit-on-*single*
    // click. One left click on this 60x16 strip opened a TextEditor and took keyboard focus,
    // in a plugin whose owner has no keyboard - the only way out was clicking somewhere else.
    // Worse here than anywhere: in Sync the dial is on an AudioParameterChoice, whose
    // getValueForText is choices.indexOf(text), so anything that is not an exact division
    // name returns -1, clamps to 0 and drops the rate to "16 bars". The `<` `>` pair reaches
    // every value the dial holds, so nothing is lost by making the readout a readout.
    rateKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 60, 16);
    rateKnob.setTitle("Arp rate");
    addAndMakeVisible(rateKnob);
    // No attachment here: refreshRateMode() installs whichever of the two the mode calls for,
    // and it is the only place that ever touches those two pointers.
    //
    // These two exist only so that place can tell whether a drag is in flight. Destroying an
    // attachment mid-drag strands the parameter gesture it opened; see refreshRateMode(). The
    // mouse-up call is what keeps a deferred mode change from waiting on the timer.
    rateKnob.onDragStart = [this] { rateDragging = true; };
    rateKnob.onDragEnd = [this] { rateDragging = false; refreshRateMode(); };

    // The mode switch. A TextButton rather than a ToggleButton: a tick box beside the word
    // "Hz" would read as "add Hz to something", where a chip that names the live unit and
    // lights while the arp is off the tempo grid reads as the switch it is.
    rateModeButton.setClickingTogglesState(true);
    rateModeButton.setTitle("Arp rate mode");
    rateModeButton.onClick = [this] { refreshRateMode(); }; // attached, so the parameter is already set
    addAndMakeVisible(rateModeButton);

    for (auto* b : { &dotButton, &anchorButton })
        addAndMakeVisible(*b);
    // Anchor's tooltip is written by refreshRateMode(), beside its enablement: it says
    // something different in Hz, where there is no bar grid to anchor to.

    // Tuplet: a combo, the same idiom as Shape and Distance, because it picks one of five.
    // The list names the family and the dial's readout names the length it produces.
    styleLabel(tupletLabel, "Tuplet");
    addAndMakeVisible(tupletLabel);
    tupletBox.addItemList(KeysProcessor::tupletChoices(), 1);
    tupletBox.setTitle("Arp tuplet");
    tupletBox.setTooltip("Fits an odd number of steps into the space a power of two would "
                         "take: Triplet is three where two go, 5-tuplet five where four go. "
                         "The rate readout shows the result, so 1/4 in fives reads \"1/5\". "
                         "Greyed with the rate in Hz, where there is no beat to divide. Run "
                         "one line straight and another in fives and you have a polyrhythm.");
    addAndMakeVisible(tupletBox);
    dotButton.setTitle("Arp dot");
    dotButton.setTooltip("Dotted: each step lasts half again as long, so 1/8 becomes a dotted "
                         "1/8. Stacks with Tuplet, which is a different question. Greyed with "
                         "the rate in Hz, where there is no beat to dot.");
    // The dot changes the readout too, and its attachment does not know that.
    dotButton.onClick = [this] { refreshTuplet(); };

    styleLabel(shapeLabel, "Shape");
    addAndMakeVisible(shapeLabel);
    // Twelve shapes then "Pattern", which stays last however many shapes are added: it is
    // the one entry that changes what is on screen, and the < > steppers clamp at the ends,
    // so a click too many on the shape before it cannot throw the step editor open.
    shapeBox.addItemList({ "Up", "Down", "Up-Down", "Down-Up",
                           "Up & Down", "Down & Up", "As Played", "Reversed",
                           "Random", "Random Other", "Random Once", "Chord",
                           "Fingered Bottom", "Fingered Top" }, 1);
    shapeBox.addItem("Pattern", ArpEngine::numDirections + 1);
    shapeBox.onChange = [this] { applyShapeChoice(); };
    shapeBox.setTooltip("A shape arpeggiates the held chord. \"Pattern\" opens the step editor.");
    addAndMakeVisible(shapeBox);

    // Step buttons beside both lists. Walking the shapes is what you actually do to an arp
    // while it plays, and a combo costs a click, a travel down the menu and a second click
    // to do it; these cost one click each and never move the pointer off the panel.
    for (auto* b : { &shapePrev, &shapeNext, &ratePrev, &rateNext })
        addAndMakeVisible(*b);
    shapePrev.onClick = [this] { stepCombo(shapeBox, -1); };
    shapeNext.onClick = [this] { stepCombo(shapeBox, 1); };
    // The rate pair is not decoration and never becomes it: the dial beside them is a drag
    // target, and these two are how every rate it can hold is reached with clicks alone.
    // refreshRateMode() writes their tooltips, which say what a click does in the live mode.
    ratePrev.onClick = [this] { stepRate(-1); };
    rateNext.onClick = [this] { stepRate(1); };
    shapePrev.setTooltip("Previous shape.");
    shapeNext.setTooltip("Next shape.");
    // A button's accessible name is its text, and all four of these say "<" or ">". Name
    // them properly: a screen reader gets something meaningful, and the screenshot script
    // can drive one particular stepper through UI Automation instead of the first match.
    shapePrev.setTitle("Previous shape");
    shapeNext.setTitle("Next shape");
    ratePrev.setTitle("Slower rate");
    rateNext.setTitle("Faster rate");

    // "Repeats", not "Octaves", since 2026-07-30: it says how many times the chord is
    // stacked, and Distance beside it says how far each stack goes. An octave is only the
    // default now, not the only answer.
    styleLabel(octavesLabel, "Repeats");
    addAndMakeVisible(octavesLabel);
    octavesSlider.setSliderStyle(juce::Slider::IncDecButtons);
    octavesSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, true, 34, 26); // read-only; see rateKnob
    octavesSlider.setRange(1, 4, 1);
    octavesSlider.setTooltip("How many times the chord repeats, each one Distance further up.");
    addAndMakeVisible(octavesSlider);

    styleLabel(distanceLabel, "Distance");
    addAndMakeVisible(distanceLabel);
    distanceBox.addItemList({ "Octave", "5th", "4th", "Maj 3rd", "min 3rd",
                              "Scale 2nd", "Scale 3rd", "Scale 5th", "Scale 7th" }, 1);
    distanceBox.setTooltip("How far each repeat of the chord goes up. The Scale entries count "
                           "degrees of Root and Scale, so a third stays a third of this key.");
    addAndMakeVisible(distanceBox);

    styleLabel(offsetLabel, "Offset");
    addAndMakeVisible(offsetLabel);
    offsetSlider.setSliderStyle(juce::Slider::IncDecButtons);
    offsetSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, true, 34, 26); // read-only; see rateKnob
    offsetSlider.setRange(0, 31, 1);
    offsetSlider.setTooltip("Start the run further in: rotates the step lanes and the walk together.");
    addAndMakeVisible(offsetSlider);

    // FEEL's three. Horizontal, because this row is one row tall (see resized), and with the
    // value on the right where a knob would have put it underneath.
    const auto bar = [this](juce::Slider& s, juce::Label& lab, const juce::String& text,
                            double lo, double hi, const juce::String& suffix, const juce::String& tip)
    {
        styleLabel(lab, text);
        addAndMakeVisible(lab);
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, true, 46, 22); // read-only; see rateKnob
        s.setRange(lo, hi, 1.0);
        s.setTextValueSuffix(suffix);
        s.setTooltip(tip);
        addAndMakeVisible(s);
    };
    bar(rampSlider, rampLabel, "Ramp", -100.0, 100.0, "%",
        "Velocity change over Time, from the moment a chord starts. Left fades a held chord "
        "out, right swells it, centre is flat.");
    bar(rampTimeSlider, rampTimeLabel, "Time", 1.0, 32.0, " beats", "How long the Ramp takes.");
    // One Humanize control until 2026-08-02, split at Owen's ask so timing and dynamics
    // randomize independently. The band keeps both halves as sliders even after the macro
    // card folded its own H.VEL into VEL's ring (2026-08-17): this view is the one place
    // Human Vel is still its own control, per the task that asked for the merge.
    bar(humanSlider, humanLabel, "Human Time", 0.0, 100.0, "%",
        "Nudges each hit a little late, by a different amount every time. "
        "The arp is dead on the grid at 0.");
    bar(humanVelSlider, humanVelLabel, "Human Vel", 0.0, 100.0, "%",
        "Takes a little off each hit's velocity, by a different amount every time. Every hit "
        "lands at full strength at 0.");
    bar(driftSlider, driftLabel, "Drift", 0.0, 100.0, "%",
        "Strays from what the lanes hold while it plays, so the part never repeats exactly. "
        "Octave, velocity, gate, lateness and chance wander; the notes never do. "
        "The lanes on screen are not changed - use Roll on the Draw page for that.");

    // Swing, Gate and Chance as knobs with the value in the middle: three continuous
    // controls side by side, where three labelled horizontal sliders would have eaten the
    // whole width of the group. The knob is the skin's machined rotary (KnobBank uses the
    // same one), and 46 px clears the mouse-only floor comfortably.
    const auto knob = [this](juce::Slider& s, juce::Label& lab, const juce::String& text,
                             double lo, double hi, double step, const juce::String& tip)
    {
        styleLabel(lab, text);
        addAndMakeVisible(lab);
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 52, 16); // read-only; see rateKnob
        s.setRange(lo, hi, step);
        s.setTooltip(tip);
        addAndMakeVisible(s);
    };
    // Swing starts centred and goes both ways: right delays the offbeats (the shuffle),
    // left pulls them early (rushed, on top of the beat). Zero is straight.
    knob(swingSlider, swingLabel, "Swing", -0.75, 0.75, 0.01,
         "Shift the offbeat steps, as a fraction of a step. Right delays them for a shuffle, "
         "left pulls them early to rush the beat, centre is straight.");

    knob(gateSlider, gateLabel, "Gate", 5.0, 200.0, 1.0,
         "How much of each step the note sounds for. Over 100% ties into the next step. "
         "Multiplies the Gate lane, so it works on any shape.");

    // "Density" since 2026-09-01, when this parameter grew a face on the macro card under that
    // name: one parameter, one word on both surfaces. "Chance" is what the *lane* is still
    // called - a per-step odds - where this is the one knob that thins a whole line.
    knob(densitySlider, densityLabel, "Density", 0.0, 100.0, 1.0,
         "How many of this line's steps actually play. At 100 every step fires; lower, and "
         "steps drop out at random. Multiplies the Chance lane, so it thins a run out on any "
         "shape. The same knob as DENSITY on the line's card.");

    addAndMakeVisible(latchButton);
    latchButton.setTooltip("Ignore note-offs until a new chord arrives.");
    // Accessible name only; the button still reads "Latch". The keybed's Latch is on the
    // Keyboard bar in the same window, and UI Automation takes the first name that matches.
    latchButton.setTitle("Arp latch");

    // PLAY moved here from the macro rows when they slimmed down (2026-08-02). The word is
    // Play rather than Keys for the reason logged the same day: "KEYS" collided head-on with
    // the bar's Light keys, two controls one word apart with nothing to say which was which.
    addAndMakeVisible(keysBandButton);
    keysBandButton.setTooltip("Does this line arpeggiate what you play on the keyboard at the "
                              "bottom? On, the keys you hold feed it. Off, it ignores the keybed "
                              "entirely and plays only the chord cards you hand it - which is "
                              "what lets one line follow your hands while the other runs a card. "
                              "MIDI arriving on the track is a separate question with a switch "
                              "of its own: Track MIDI, on the arp bar, off by default.");
    keysBandButton.setTitle("Arp play");

    // Retrigger was a toggle that only answered "on a new chord". The list adds the clock
    // half, so a five-step lane can still be made to land on the bar, and the two are
    // alternatives on one control rather than two switches that can disagree.
    styleLabel(retrigLabel, "Retrigger");
    addAndMakeVisible(retrigLabel);
    retrigBox.addItemList({ "Off", "Note", "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars" }, 1);
    retrigBox.setTooltip("When the pattern starts over: never, on a new chord, or on the clock.");
    retrigBox.onChange = [this] { applyRetrigChoice(); };
    addAndMakeVisible(retrigBox);

    // The ten lanes, in ArpEngine::Lane order. The original six first:
    buildLaneRow(laneRows[(size_t) ArpEngine::laneNote], ArpEngine::laneNote, "Note");
    buildLaneRow(laneRows[(size_t) ArpEngine::laneOctave], ArpEngine::laneOctave, "Octave");
    buildLaneRow(laneRows[(size_t) ArpEngine::laneVelocity], ArpEngine::laneVelocity, "Velocity");
    buildLaneRow(laneRows[(size_t) ArpEngine::laneGate], ArpEngine::laneGate, "Gate");
    buildLaneRow(laneRows[(size_t) ArpEngine::laneRatchet], ArpEngine::laneRatchet, "Ratchet");
    // "Chance", not "Prob" (2026-08-14). The knob on the Play page was called CHANCE and the two
    // multiply together, so one word for one idea: a lane at 60 under a knob at 100 fires six
    // times in ten. Owen asked for per-step odds to be findable, and two names for the same
    // thing in two places is most of why they were not. **The knob is DENSITY since
    // 2026-09-01**, when it grew a face on the macro card - and that is a different word on
    // purpose rather than the old mistake back: the lane is per-step odds, the knob thins the
    // whole line, and its tooltip on both surfaces says it multiplies this lane.
    buildLaneRow(laneRows[(size_t) ArpEngine::laneProbability], ArpEngine::laneProbability, "Chance");
    // The 2026-07-30 four. "Prob" above shortened with them: ten tabs share the width six
    // used to, and "Probability" is the only old label that will not fit at that size.
    buildLaneRow(laneRows[(size_t) ArpEngine::laneTranspose], ArpEngine::laneTranspose, "Transpose");
    buildLaneRow(laneRows[(size_t) ArpEngine::laneLate], ArpEngine::laneLate, "Late");
    buildLaneRow(laneRows[(size_t) ArpEngine::laneHarmony], ArpEngine::laneHarmony, "Harmony");
    buildLaneRow(laneRows[(size_t) ArpEngine::laneChord], ArpEngine::laneChord, "Chord");
    // Rand gets a tab; Mute deliberately does not - the MUTE row under the grid has always been
    // its editor, and a tab as well would be two ways to draw one lane.
    buildLaneRow(laneRows[(size_t) ArpEngine::laneRand], ArpEngine::laneRand, "Rand");
    buildLaneRow(laneRows[(size_t) ArpEngine::laneChain], ArpEngine::laneChain, "Chain");
    buildLaneRow(laneRows[(size_t) ArpEngine::laneReset], ArpEngine::laneReset, "Reset");
    laneRows[(size_t) ArpEngine::laneNote].tab.setTooltip(
        "Which note of the held chord this step plays. Drag up through the whole vocabulary: "
        "X rests, a dot follows the line's Shape, 1-8 pick a fixed note, P/H/L/R are Prev, "
        "Highest, Lowest and Random, and the eight little contours above those are shapes this "
        "step runs on its own - up, down, up/down, down/up, up & down, down & up, and fingered "
        "low and high. Shapes share one walk, so four steps of Up then four of Down comes back "
        "down the line it went up rather than starting over.");
    laneRows[(size_t) ArpEngine::laneChain].tab.setTooltip(
        "Play this step only on a condition: 0 always, 1 only if the step before it sounded, "
        "2 only if it did not. Chance says maybe; this says only if.");
    laneRows[(size_t) ArpEngine::laneReset].tab.setTooltip(
        "Restart the shape's walk on this step, so it plays the note the walk starts on again. "
        "Cthulhu's Position Reset. It restarts the walk, not the lanes - everything else keeps "
        "running, so a reset every other step keeps an Up shape on its first two notes.");

    styleLabel(muteRowLabel, "Mute");
    addAndMakeVisible(muteRowLabel);
    muteRow = std::make_unique<MuteRow>(processor, *this);
    addAndMakeVisible(*muteRow);
    loopBar = std::make_unique<LoopBar>(processor, *this);
    addAndMakeVisible(*loopBar);

    // The lane strip: what this lane *is*, on the page you draw it on.
    laneOnButton.setClickingTogglesState(false); // the lane's own flag drives the lit state
    laneOnButton.onClick = [this] { toggleLaneOn(); };
    laneOnButton.setTooltip("Switch this lane off without losing what you drew. Off, the lane "
                            "keeps its drawing and the engine reads its default instead.");
    laneOnButton.setTitle("Lane on");
    addAndMakeVisible(laneOnButton);

    styleLabel(dirLabel, "Dir");
    addAndMakeVisible(dirLabel);
    dirReadout.setJustificationType(juce::Justification::centred);
    dirReadout.setFont(juce::Font(juce::FontOptions(13.0f)));
    addAndMakeVisible(dirReadout);
    dirPrev.onClick = [this] { nudgeLaneDir(-1); };
    dirNext.onClick = [this] { nudgeLaneDir(1); };
    dirPrev.setTooltip("Which way this lane walks its loop: Up, Down, or either of those and "
                       "back again. Two lanes crossing the same steps in opposite directions "
                       "is what stops a pattern repeating itself.");
    dirNext.setTooltip(dirPrev.getTooltip());
    dirPrev.setTitle("Lane direction back");
    dirNext.setTitle("Lane direction forward");
    addAndMakeVisible(dirPrev);
    addAndMakeVisible(dirNext);

    // Kirnu's remaining palette tools. Each acts on the Select span, or on the whole lane when
    // nothing is selected - the same rule Roll and Reset already follow, so Select stays one
    // idea aimed by five buttons rather than a mode three of them need explaining against.
    copyStepsButton.onClick = [this] { copySteps(); };
    pasteStepsButton.onClick = [this] { pasteSteps(); };
    copyStepsButton.setTooltip("Copy the selected steps of this lane, or all of them.");
    pasteStepsButton.setTooltip("Paste them back, tiled across the selected steps - two copied "
                                "steps fill eight. Only into the same lane, as in Kirnu.");
    copyStepsButton.setTitle("Copy steps");
    pasteStepsButton.setTitle("Paste steps");
    addAndMakeVisible(copyStepsButton);
    addAndMakeVisible(pasteStepsButton);

    // One length and one speed control, for whichever lane is showing, finally with
    // room to say what they are. Both were previously repeated once per lane, unlabelled.
    styleLabel(stepsLabel, "Steps");
    styleLabel(speedLabel, "Speed");
    addAndMakeVisible(stepsLabel);
    addAndMakeVisible(speedLabel);

    stepsReadout.setJustificationType(juce::Justification::centred);
    stepsReadout.setFont(juce::Font(juce::FontOptions(14.0f)));
    addAndMakeVisible(stepsReadout);

    stepsMinus.onClick = [this] { nudgeLength(-1); };
    stepsPlus.onClick = [this] { nudgeLength(1); };
    stepsMinus.setTooltip("How many steps this pattern runs before repeating (1-32).");
    stepsPlus.setTooltip(stepsMinus.getTooltip());
    addAndMakeVisible(stepsMinus);
    addAndMakeVisible(stepsPlus);

    speedButton.onClick = [this] { cycleClockDiv(); };
    speedButton.setTooltip("Step speed: full, half or quarter. Different speeds per lane give polymeter.");
    addAndMakeVisible(speedButton);

    linkButton.setTooltip("On: every lane shares one length and speed. Off: each lane keeps its own (polymeter).");
    addAndMakeVisible(linkButton);

    // Twelve launchable slots. Left-click launches (or, while Copy is armed, is the copy
    // target); right-click opens the slot menu, which is an accelerator for the buttons
    // beside the row.
    for (int i = 0; i < (int) slotCards.size(); ++i)
    {
        auto card = std::make_unique<SlotCard>(*this, processor, i);
        card->onClick = [this, i] { recallOrCopy(i); };
        card->onRightClick = [this, i] { showSlotMenu(i); };
        card->setTooltip("Launch slot " + juce::String(i + 1) + ": its pattern, its shape and "
                         "rate, and the chord it holds.");
        addAndMakeVisible(*card);
        slotCards[(size_t) i] = std::move(card);
    }

    // releaseArpHold, not stopArpSlot: the tooltip below says this is the same button as Hold
    // off on the section bar, and it has to be. Releasing the chord while the chain runs only
    // holds until the next bar line, when the following slot launches and hands over another.
    stopButton.onClick = [this] { processor.releaseArpHold(); refreshPatternButtons(); };
    stopButton.setTooltip("Let go of the chord being held into the arp, whichever card or "
                          "slot put it there, and stop the Chain if it is running. The pattern "
                          "stays put. Same button as Hold off on the section bar, which "
                          "survives folding this panel away.");
    addAndMakeVisible(stopButton);

    copyButton.onClick = [this]
    {
        setArmed(armed == armCopy ? armNone : armCopy, processor.arpActivePattern(editLine()));
    };
    copyButton.setTooltip("Copy the live pattern, then click the slot to copy it into.");
    addAndMakeVisible(copyButton);

    // Clearing a slot's chord needs a left-click path of its own: it is in the slot's
    // right-click menu too, but that menu is only ever an accelerator (see CLAUDE.md).
    clearButton.onClick = [this] { setArmed(armed == armClear ? armNone : armClear); };
    clearButton.setTooltip("Click, then click a slot to take its chord away. The pattern stays.");
    addAndMakeVisible(clearButton);

    cancelButton.onClick = [this] { setArmed(armNone); };
    addAndMakeVisible(cancelButton);
    cancelButton.setVisible(false);

    randomizeButton.onClick = [this]
    {
        processor.pushUndo("Randomize pattern", KeysProcessor::UndoScope::arp);
        processor.randomizeActiveArpPattern(editLine());
    };
    addAndMakeVisible(randomizeButton);

    // Chain: one click plays the row as a progression. It starts at the lowest slot holding
    // a chord and walks the filled ones, giving each the bars its card shows, so a page of
    // twelve cards is a twelve-chord song and not just twelve things to click.
    chainButton.onClick = [this]
    {
        if (processor.chainRunning(editLine()))
            processor.stopChain(editLine());
        else
            processor.startChain(editLine());
        refreshPatternButtons();
    };
    chainButton.setTooltip("Play the slots that hold a chord, one after another, each for the "
                           "bars on its card. Click again to stop.");
    addAndMakeVisible(chainButton);

    barsReadout.setJustificationType(juce::Justification::centred);
    barsReadout.setFont(juce::Font(juce::FontOptions(14.0f)));
    addAndMakeVisible(barsReadout);
    barsMinus.onClick = [this] { nudgeBars(-1); };
    barsPlus.onClick = [this] { nudgeBars(1); };
    barsMinus.setTitle("Fewer bars");
    barsPlus.setTitle("More bars");
    for (auto* b : { &barsMinus, &barsPlus })
    {
        b->setTooltip("How many bars the chain holds the selected slot. Click a slot card to "
                      "select it.");
        addAndMakeVisible(*b);
    }

    // Euclid: opens a non-destructive preview strip of three steppers (HITS/STEPS/ROTATE).
    // A click on any stepper writes straight into the probability lane; opening the strip on
    // its own writes nothing. setClickingTogglesState mirrors rateModeButton's own toggle
    // chip above: the button flips its own state on click, and the handler reads it back
    // rather than tracking a second boolean.
    euclidButton.setClickingTogglesState(true);
    euclidButton.setTitle("Euclid pattern");
    euclidButton.setTooltip("A Euclidean rhythm, spread into the probability lane: HITS beats "
                            "spaced as evenly as possible across STEPS, shifted by ROTATE.");
    euclidButton.onClick = [this] { openEuclidStrip(euclidButton.getToggleState()); };
    addAndMakeVisible(euclidButton);

    const auto buildStepper = [this](juce::Label& cap, const juce::String& capText,
                                     juce::TextButton& minus, const juce::String& minusName,
                                     juce::TextButton& plus, const juce::String& plusName,
                                     juce::Label& readout)
    {
        styleLabel(cap, capText);
        addChildComponent(cap); // strips lay out regardless; visibility follows the strip
        minus.setButtonText("-");
        plus.setButtonText("+");
        minus.setTitle(minusName);
        plus.setTitle(plusName);
        addChildComponent(minus);
        addChildComponent(plus);
        readout.setJustificationType(juce::Justification::centred);
        readout.setFont(juce::Font(juce::FontOptions(13.0f)));
        addChildComponent(readout);
    };
    buildStepper(euclidHitsLabel, "Hits", euclidHitsMinus, "Fewer hits", euclidHitsPlus, "More hits", euclidHitsReadout);
    buildStepper(euclidStepsLabel, "Steps", euclidStepsMinus, "Fewer steps", euclidStepsPlus, "More steps", euclidStepsReadout);
    buildStepper(euclidRotateLabel, "Rotate", euclidRotateMinus, "Rotate left", euclidRotatePlus, "Rotate right", euclidRotateReadout);
    euclidHitsMinus.onClick = [this] { nudgeEuclid(0, -1); };
    euclidHitsPlus.onClick = [this] { nudgeEuclid(0, 1); };
    euclidStepsMinus.onClick = [this] { nudgeEuclid(1, -1); };
    euclidStepsPlus.onClick = [this] { nudgeEuclid(1, 1); };
    euclidRotateMinus.onClick = [this] { nudgeEuclid(2, -1); };
    euclidRotatePlus.onClick = [this] { nudgeEuclid(2, 1); };
    refreshEuclidReadouts();

    // Clocks: the four rhythm dividers, same strip mechanism, mutually exclusive with Euclid.
    clocksButton.setClickingTogglesState(true);
    clocksButton.setTitle("Rhythm dividers");
    clocksButton.setTooltip("Four independent clock dividers layered under the pattern - "
                            "0 is off, higher numbers slow that voice down further.");
    clocksButton.onClick = [this] { openClocksStrip(clocksButton.getToggleState()); };
    addAndMakeVisible(clocksButton);

    for (int i = 0; i < 4; ++i)
    {
        buildStepper(clockDivLabels[(size_t) i], "Div " + juce::String(i + 1),
                    clockDivMinus[(size_t) i], "Divider " + juce::String(i + 1) + " slower",
                    clockDivPlus[(size_t) i], "Divider " + juce::String(i + 1) + " faster",
                    clockDivReadouts[(size_t) i]);
        const int idx = i;
        clockDivMinus[(size_t) i].onClick = [this, idx] { nudgeClockDiv(idx, -1); };
        clockDivPlus[(size_t) i].onClick = [this, idx] { nudgeClockDiv(idx, 1); };
    }
    refreshClockDivReadouts();

    // Voice: harmony mode, the panel's first lane-contextual control (visible only with the
    // Harmony lane selected). "Chord" plays the held chord's own tones in the second voice;
    // "Sub" plays the undertone series instead.
    voiceButton.setTitle("Harmony voice");
    voiceButton.setTooltip("The Harmony lane's second voice: chord tones, or the subharmonic "
                           "series below the root.");
    voiceButton.onClick = [this]
    {
        auto& engine = processor.arpLine(editLine());
        const int next = engine.harmonyMode.load(std::memory_order_relaxed) == 0 ? 1 : 0;
        engine.harmonyMode.store(next, std::memory_order_relaxed);
        refreshVoiceButton();
    };
    addChildComponent(voiceButton);
    refreshVoiceButton();

    // Roll, and how far it may stray. Its own steppers rather than a slider, for the reason the
    // rate's and the note count's have: a slider is a drag, and the mouse-only contract wants a
    // click-only path to every value.
    rollButton.setTitle("Roll lane");
    rollButton.setTooltip("Reroll the lane you are looking at, straying from what is drawn by "
                          "the amount beside this. 100 is a full scramble.");
    rollButton.onClick = [this] { rollSelectedLane(); };
    addChildComponent(rollButton);
    resetButton.setTitle("Reset lane");
    resetButton.setTooltip("Put this lane back to its default across its whole length - the "
                           "state it is in before you touch it. The way back from a Roll you "
                           "did not want.");
    resetButton.onClick = [this] { resetSelectedLane(); };
    addChildComponent(resetButton);
    selectButton.setClickingTogglesState(true);
    selectButton.setTitle("Select steps");
    selectButton.setTooltip("Drag on the grid to mark a span of steps instead of drawing on it. "
                            "Roll and Reset then act on that span alone. Click again to go back "
                            "to drawing, which also clears the span.");
    selectButton.onClick = [this]
    {
        selectMode = selectButton.getToggleState();
        if (! selectMode)
            clearSelection(); // leaving the mode drops the span: it has nothing left to show it
        repaint();
    };
    addChildComponent(selectButton);
    rollMinus.setTitle("Less roll");
    rollPlus.setTitle("More roll");
    rollMinus.onClick = [this] { nudgeRollAmount(-5); };
    rollPlus.onClick = [this] { nudgeRollAmount(5); };
    addChildComponent(rollMinus);
    addChildComponent(rollPlus);
    rollReadout.setJustificationType(juce::Justification::centred);
    rollReadout.setFont(juce::Font(juce::FontOptions(13.0f)));
    addChildComponent(rollReadout);
    rollReadout.setText(juce::String(rollAmount) + "%", juce::dontSendNotification);

    // One macro card per line, hidden until the All view asks for them. Built once and kept,
    // so their attachments never churn: each one is bound to its own line for good.
    //
    // uiArpLines, not numArpLines: the arrays stay numArpLines long and their tail stays null,
    // which is why every loop over them already null-checks. Nothing else has to know.
    //
    // The A/B/All tabs, the BPM cell and Launch Quantize all left this panel for the ARP
    // section bar on 2026-08-02 (Owen: "move the bpm and the a b and all into the header
    // also"), so the All view is nothing but the cards. The editor owns them now; the bar
    // outlives the panel, which is the point.
    for (int n = 0; n < KeysProcessor::uiArpLines; ++n)
    {
        auto row = std::make_unique<MacroRow>(*this, processor, n);
        addChildComponent(*row);
        macroRows[(size_t) n] = std::move(row);
    }

    // The stand-in for the bottom row while it is collapsed. addChildComponent, not
    // addAndMakeVisible: resized() is the one place that decides which of the two is on screen.
    foldedRowStrip = std::make_unique<FoldedRowStrip>(*this, processor);
    addChildComponent(*foldedRowStrip);

    // After every control this panel owns exists, and after the slot cards and lane grids in
    // particular: the lists hold raw pointers into members built above, so this cannot move
    // any earlier.
    buildPageLists();

    buildAttachments();
}

// Which controls belong to which page (2026-08-14). One block, built once, naming every
// control exactly once - the alternative was a flag per component or three parent Components
// to reparent into, and this is the version you can read against the three page names.
//
// Not in it, on purpose: the macro rows (their own view, gated by macroView), the slot cards'
// own children, and Cancel, whose visibility is the armed state's to decide and which
// applyPageVisibility() therefore only ever hides.
void ArpPanel::buildPageLists()
{
    pageSetup = {
        &rateLabel, &rateKnob, &rateModeButton, &ratePrev, &rateNext,
        &shapeLabel, &shapeBox, &shapePrev, &shapeNext,
        &tupletLabel, &tupletBox, &dotButton,
        &swingLabel, &swingSlider, &gateLabel, &gateSlider, &densityLabel, &densitySlider,
        &retrigLabel, &retrigBox, &keysBandButton, &latchButton, &anchorButton,
        &octavesLabel, &octavesSlider, &distanceLabel, &distanceBox, &offsetLabel, &offsetSlider,
        &rampLabel, &rampSlider, &rampTimeLabel, &rampTimeSlider,
        &humanLabel, &humanSlider, &humanVelLabel, &humanVelSlider,
        &driftLabel, &driftSlider,
    };

    pageSlots = {
        &copyButton, &clearButton, &cancelButton, &stopButton, &chainButton,
        &barsMinus, &barsReadout, &barsPlus, &randomizeButton,
        &euclidButton, &euclidHitsLabel, &euclidStepsLabel, &euclidRotateLabel,
        &euclidHitsReadout, &euclidStepsReadout, &euclidRotateReadout,
        &euclidHitsMinus, &euclidHitsPlus, &euclidStepsMinus, &euclidStepsPlus,
        &euclidRotateMinus, &euclidRotatePlus,
        &clocksButton,
    };
    for (auto& c : slotCards)
        if (c != nullptr)
            pageSlots.push_back(c.get());
    for (int i = 0; i < 4; ++i)
    {
        pageSlots.push_back(&clockDivLabels[(size_t) i]);
        pageSlots.push_back(&clockDivReadouts[(size_t) i]);
        pageSlots.push_back(&clockDivMinus[(size_t) i]);
        pageSlots.push_back(&clockDivPlus[(size_t) i]);
    }

    pageSteps = { &muteRowLabel, &voiceButton,
                  &rollButton, &resetButton, &selectButton,
                  &rollMinus, &rollReadout, &rollPlus,
                  &copyStepsButton, &pasteStepsButton,
                  &laneOnButton, &dirLabel, &dirPrev, &dirReadout, &dirNext,
                  &stepsLabel, &stepsMinus, &stepsReadout, &stepsPlus,
                  &speedLabel, &speedButton, &linkButton };
    if (muteRow != nullptr)
        pageSteps.push_back(muteRow.get());
    if (loopBar != nullptr)
        pageSteps.push_back(loopBar.get());
    for (auto& lr : laneRows)
    {
        pageSteps.push_back(&lr.tab);
        if (lr.grid != nullptr)
            pageSteps.push_back(lr.grid.get());
    }
}

// Hide everything that is not on the current page. **Never shows anything**: refreshShape()
// is the one place a control is turned on, on its own Shape, lane and armed-state gates, and
// this runs at the end of it to take away what those gates could not know about. Turning
// things on here as well would mean two writers for one visibility and a control that comes
// back wherever the two disagree - the mistake the macro card's second On toggle was.
void ArpPanel::applyPageVisibility()
{
    const auto p = currentPage();
    const auto hideList = [](const std::vector<juce::Component*>& list)
    {
        for (auto* c : list)
            if (c != nullptr)
                c->setVisible(false);
    };

    // The macro view is not a page and shows none of them.
    if (macroView || p != Page::setup)
    {
        hideList(pageSetup);
        for (auto& grp : groups)
            grp.visible = false;
    }
    if (macroView || p != Page::slots)
        hideList(pageSlots);
    if (macroView || p != Page::steps)
        hideList(pageSteps);
}

void ArpPanel::nudgeBars(int delta)
{
    const int slot = processor.arpActivePattern(editLine());
    processor.setArpSlotBars(slot, processor.arpSlotBars(slot, editLine()) + delta, editLine());
    refreshPatternButtons();
    for (auto& c : slotCards)
        if (c != nullptr)
            c->repaint();
}

// Euclid and Clocks are mutually exclusive: the panel never grows by more than one strip's
// height at a time. Opening one closes the other; each strip's own components are the only
// place their visibility is set, since resized() lays them out regardless (they read a
// possibly-empty rectangle when closed, which is harmless because nothing sees it).
void ArpPanel::openEuclidStrip(bool open)
{
    euclidStripOpen = open;
    euclidButton.setToggleState(open, juce::dontSendNotification);
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &euclidHitsLabel, &euclidStepsLabel, &euclidRotateLabel,
             &euclidHitsReadout, &euclidStepsReadout, &euclidRotateReadout,
             &euclidHitsMinus, &euclidHitsPlus, &euclidStepsMinus, &euclidStepsPlus,
             &euclidRotateMinus, &euclidRotatePlus })
        c->setVisible(open);
    if (open)
        openClocksStrip(false);
    if (onPreferredHeightChanged)
        onPreferredHeightChanged();
    resized();
    repaint();
}

// which: 0 = hits, 1 = steps, 2 = rotate. Every click is immediately audible - that is the
// point of a preview strip, not a bug - so this both writes through the processor and
// refreshes the probability lane's own readout (applyEuclidToActiveArpPattern changes that
// lane's length).
void ArpPanel::nudgeEuclid(int which, int delta)
{
    switch (which)
    {
        case 0: euclidHits += delta; break;
        case 1: euclidSteps += delta; break;
        default: euclidRotate += delta; break;
    }
    // Re-clamp hits and rotate against the possibly-changed steps value, whichever stepper
    // moved: a STEPS click can leave either of the other two out of range.
    euclidSteps = juce::jlimit(1, ArpEngine::maxSteps, euclidSteps);
    euclidHits = juce::jlimit(0, euclidSteps, euclidHits);
    euclidRotate = juce::jlimit(0, euclidSteps - 1, euclidRotate);

    processor.pushUndo("Euclid", KeysProcessor::UndoScope::arp);
    processor.applyEuclidToActiveArpPattern(editLine(), euclidHits, euclidSteps, euclidRotate,
                                            ArpEngine::laneProbability);
    refreshLaneReadouts();
    refreshEuclidReadouts();
}

void ArpPanel::refreshEuclidReadouts()
{
    euclidHitsReadout.setText(juce::String(euclidHits), juce::dontSendNotification);
    euclidStepsReadout.setText(juce::String(euclidSteps), juce::dontSendNotification);
    euclidRotateReadout.setText(juce::String(euclidRotate), juce::dontSendNotification);
}

void ArpPanel::openClocksStrip(bool open)
{
    clocksStripOpen = open;
    clocksButton.setToggleState(open, juce::dontSendNotification);
    for (int i = 0; i < 4; ++i)
    {
        clockDivLabels[(size_t) i].setVisible(open);
        clockDivReadouts[(size_t) i].setVisible(open);
        clockDivMinus[(size_t) i].setVisible(open);
        clockDivPlus[(size_t) i].setVisible(open);
    }
    if (open)
    {
        openEuclidStrip(false);
        refreshClockDivReadouts();
    }
    if (onPreferredHeightChanged)
        onPreferredHeightChanged();
    resized();
    repaint();
}

// Writes straight to the atomic - no snapshot, no dirty flag, same contract as Randomize.
void ArpPanel::nudgeClockDiv(int index, int delta)
{
    auto& engine = processor.arpLine(editLine());
    const int next = juce::jlimit(0, 16,
                                  engine.rhythmDiv[(size_t) index].load(std::memory_order_relaxed) + delta);
    engine.rhythmDiv[(size_t) index].store(next, std::memory_order_relaxed);
    refreshClockDivReadouts();
    refreshPatternButtons(); // the Clocks/Clocked retitle depends on all four
}

void ArpPanel::refreshClockDivReadouts()
{
    auto& engine = processor.arpLine(editLine());
    for (int i = 0; i < 4; ++i)
    {
        const int v = engine.rhythmDiv[(size_t) i].load(std::memory_order_relaxed);
        clockDivReadouts[(size_t) i].setText(v == 0 ? "Off" : juce::String(v), juce::dontSendNotification);
    }
}

void ArpPanel::nudgeRollAmount(int delta)
{
    rollAmount = juce::jlimit(5, 100, rollAmount + delta);
    rollReadout.setText(juce::String(rollAmount) + "%", juce::dontSendNotification);
}

// Rerolls the lane on screen and repaints its grid. No audition and no undo - the grid shows
// the result immediately, and clicking again is how you reject one, which is the same deal
// Randomize has always offered.
void ArpPanel::rollSelectedLane()
{
    processor.pushUndo("Roll lane", KeysProcessor::UndoScope::arp);
    processor.rerollArpLane(editLine(), selectedLane, rollAmount, selFrom, selTo);
    refreshLaneReadouts();
    if (auto& g = laneRows[(size_t) juce::jlimit(0, ArpEngine::numLanes - 1, selectedLane)].grid)
        g->repaint();
}

void ArpPanel::clearSelection()
{
    selFrom = selTo = -1;
    repaint();
}

void ArpPanel::resetSelectedLane()
{
    processor.pushUndo("Reset lane", KeysProcessor::UndoScope::arp);
    processor.resetArpLane(editLine(), selectedLane, selFrom, selTo);
    refreshLaneReadouts();
    if (auto& g = laneRows[(size_t) juce::jlimit(0, ArpEngine::numLanes - 1, selectedLane)].grid)
        g->repaint();
}

void ArpPanel::refreshVoiceButton()
{
    // The word travels with the state, because the caption above it is gone: the button had a
    // 12 px "VOICE" label over it until 2026-08-14, and dropping that is what let the target
    // have the tab row's whole 34 px. One control, saying what it is and what it is set to.
    const int mode = processor.arpLine(editLine()).harmonyMode.load(std::memory_order_relaxed);
    voiceButton.setButtonText(mode == 1 ? "Voice: Sub" : "Voice: Chord");
}

void ArpPanel::timerCallback()
{
    // The lane-length repair runs whether or not anyone is looking. A session saved before a
    // lane existed loads that lane at ArpPattern's default 8 while its neighbours are at 16 or
    // 32, and the ARP section may well be folded when it lands - so gating this on being on
    // screen would leave the repair waiting for the user to open the panel.
    //
    // **The repair only, not the readout.** This was refreshLaneReadouts(), which also runs
    // refreshLaneStrip() - a walk of all fourteen lanes across up to 32 cells each, plus button
    // text, toggle states, the Dir readout and Paste enablement - for controls that are not on
    // screen. That is exactly the per-tick cost the gate three lines below refuses to pay, and
    // it was being paid above it. The readout runs under the gate now, with the rest of the
    // display work.
    enforceLinkedLengths();

    // Everything below here is display. The panel keeps existing while its section is folded
    // and while it sits in a closed detached window, and refreshing controls nobody can see
    // costs the message thread - which in a plugin is the DAW's UI thread, not ours.
    if (! isShowing())
        return;

    refreshMacro(); // rate, shape and the held chord, none of which an attachment drives

    refreshShape(); // the host can automate arpPattern/arpDirection out from under us
    refreshRateMode(); // ... and arpRateFree, which decides what the dial is even measuring
    refreshTuplet();   // ... and arpTuplet, which has no attachment to hear it change
    refreshRetrig();
    // The readout and the lane strip, on screen only. Above the gate there is just the length
    // repair, which is the one part of this that has to run while the panel is hidden.
    refreshLaneReadouts();
    refreshPatternButtons();
    if (! patternMode())
        return; // nothing of the step editor is on screen to repaint

    auto& grid = laneRows[(size_t) selectedLane].grid;
    if (grid != nullptr)
        grid->repaint();
    if (muteRow != nullptr)
        muteRow->repaint();
    if (loopBar != nullptr)
        loopBar->repaint();
}

void ArpPanel::mouseDown(const juce::MouseEvent&)
{
    // Opaque to clicks: as an overlay nothing behind it should react, and inline the
    // card's own background should not fall through to the editor either.
}

void ArpPanel::setInlineMode(bool b)
{
    if (inlineMode == b)
        return;
    inlineMode = b;
    resized();
    repaint();
}

namespace
{
    // Heights of the two layouts, kept next to the resized() that spends them. Every arp
    // has the title row, the control band and the slot row with its buttons; Pattern adds
    // the lane tabs, the grid and the mute row between the two. 12 px padding at each end.
    // The band: a caption rule, then two 42 px control rows. A knob column spans both.
    constexpr int arpBandTop = 18;               // caption rule + its clearance
    constexpr int arpBandRow = 42;
    constexpr int arpBandInner = arpBandRow * 2 + 6;
    constexpr int arpBandH = arpBandTop + arpBandInner + 4;
    // The second band row (Spread and Feel) is one control row tall, not two: eight more
    // controls for 64 px rather than the 112 a second full band would have cost, in a panel
    // that is already the tallest thing in the editor.
    constexpr int arpBand2H = arpBandTop + arpBandRow + 4;
    constexpr int arpSlotsH = 58;
    // The macro view: one *card* per line, side by side (2026-08-02, Owen: "having the
    // arpeggiators parallel to each other instead of one on top of the other"), and nothing
    // else - the tabs, BPM and Quantize ride the ARP section bar. A card is its caption rule
    // and three stacked lines - rate and shape under their RATE / SHAPE headings, the seven
    // knobs under theirs, the rate's modifiers with the held chord - because half the panel's
    // width cannot hold the old single-line row, and side by side is the point: two parallel
    // instruments, each a drop target half the panel wide. This sum has to agree with
    // MacroRow::resized exactly.
    // The knob line is arpMacroKnobLine, not arpMacroLine: it carries the two range knobs and
    // is a ring taller either side for them (2026-08-03). The top line, which has no rings on
    // it, is unchanged - two constants because the rows genuinely differ now.
    // + the harmony area (2026-08-19, two columns since the second pass that day): heading,
    // dropdown row, CHANCE heading, knob row - between the knobs and the rate's modifiers.
    constexpr int arpMacroCard = arpMacroCap + arpMacroHeads + arpMacroLine
                                 + arpMacroHeads + arpMacroKnobLine
                                 + arpMacroHeads + arpMacroHarmCombo
                                 + arpMacroHeads + arpMacroLine + 2 + arpMacroMods + 6;
    // The second 2026-08-02 pass (Owen: "we need to make the window shorter ... move the BPM
    // up into the title ... move the A B All into the title and remove everything on the
    // bottom"). The shared row is gone: the A/B/All tabs, the BPM cell and Quantize all sit
    // on one 34 px header strip inside the LINES frame, their captions inline rather than
    // above. The slot row and the action row left this view entirely - the slots belong to
    // the per-line tabs now - so the view is the header and the two rows, full stop.
    // Two rows of two cards since 2026-08-19 (Owen: "I want 4 arps"): the view is a 2x2 grid,
    // so its height is two cards and the gap between the rows. Width was the reason - a card
    // needs ~430 px for its knob strip, and four across would have pushed the floor far past
    // 1320 - and height is the cheap axis in this view, as ever.
    constexpr int arpMacroRowGap = 12;
    constexpr int arpMacroH = 2 * arpMacroCard + arpMacroRowGap;
    // The bottom row collapsed to a strip (2026-08-19, Owen: "maybe you should be able to
    // minimize bottom arps"). 34 px, the mouse-only floor, because the whole strip is the
    // target that expands the row again. Two cards at 323 come to 1349 px of minimum window on
    // their own; this brings the All view back to what it cost when there were two lines.
    constexpr int arpMacroStripH = 34;
    constexpr int arpMacroFoldedH = arpMacroCard + arpMacroRowGap + arpMacroStripH;
    constexpr int arpShapeH = 12 + (arpBandH + 8) + (arpBand2H + 12) + (arpSlotsH + 8) + 34 + 12;
    // The gap under the block. The outer `reduced(12)` is shared with the band and Pattern
    // views, and this height has to agree with resized() exactly or the panel is the wrong
    // size with nothing to say so.
    constexpr int arpMacroBelow = 8;
    constexpr int arpMacroTotalH = 12 + (arpMacroH + arpMacroBelow) + 12;
    constexpr int arpMacroFoldedTotalH = 12 + (arpMacroFoldedH + arpMacroBelow) + 12;
    // Euclid and Clocks each open into one 34 px row plus its 8 px gap above the action row,
    // and the two are mutually exclusive (openEuclidStrip/openClocksStrip each close the
    // other), so at most one of them is ever open at once.
    constexpr int arpStripH = 34 + 8;

    // --- The three pages of a line's deep view (2026-08-14) -------------------------------
    //
    // Owen, looking at the un-paged view: "when you click details it shouldn't resize the whole
    // window, just the full arp section. and we need a way to get out the detail view". The
    // deep view was every block at once - band 112, band2 64, lane tabs 34, grid 140, mute 46,
    // slots 58, action row 34 - which came to 612 px against the macro view's 240. So Details
    // grew the window by 372 px and All shrank it back, and on a screen that could not afford
    // the 372 the keybed lost it off the bottom instead (the 2026-08-02 fail-safe entry).
    //
    // Split by what you are doing rather than by what fits, the blocks come apart cleanly and
    // the tallest page is 258 - eighteen more than the macro view, and 354 *less* than the
    // un-paged deep view. So the panel takes one fixed height for every view and page it has,
    // and the window stops resizing between them at all.
    // + the lane-tools strip (34 + 6), added 2026-08-14 when twelve tabs stopped fitting
    // beside the buttons. arpDeepH is a max over these, so the deep view grew once and stopped.
    // 12 top, the lane tabs, the lane strip (Steps/Speed/Link/On/Dir, 2026-08-18), the tools
    // row, the grid, the loop bar hard under it, then the MUTE caption and its strip.
    constexpr int arpPageStepsH = 12 + (34 + 6) + (34 + 6) + (34 + 6)
                                + (140 + 4) + (16 + 6) + (14 + 2) + 32 + 12; // 358
    constexpr int arpPageSlotsH = 12 + (arpSlotsH + 8) + 34 + 12;                  // 124
    constexpr int arpPageSetupH = 12 + (arpBandH + 8) + arpBand2H + 12;            // 208
    // The one height a *deep view* is, whichever of its three pages is up. Written as a max
    // rather than as the 298 it currently works out to: each is a sum of constants above, and
    // the day one grows past the others this picks it up instead of silently clipping that page.
    // Paging between Play, Cards and Draw is the thing that must never move the window, and this
    // is what stops it - the whole 2026-08-14 win, kept.
    //
    // **The macro view is deliberately not in this max any more** (2026-08-17, Owen: "there's
    // some deadspace I want to remove at bottom"). It was, and it cost the *default* view 58 px
    // of nothing: the macro view needs 240, arpPageStepsH grew from 258 to 298 when the
    // lane-tools strip landed the same day this was written, and nobody re-checked the gap it
    // opened underneath. contentHeight() answers per view now, so entering or leaving the macro
    // view is the one gesture that resizes the window, and it moves by 58 rather than by the 372
    // that made the un-paged deep view untenable. Paging still never moves it.
    constexpr int arpMax2(int a, int b) { return a > b ? a : b; }
    constexpr int arpDeepH = arpMax2(arpPageStepsH, arpMax2(arpPageSlotsH, arpPageSetupH));
    // A strip opens on the Slots page (124 + 42 = 166), which is well inside arpDeepH, so
    // neither strip changes the panel's height any more and contentHeight() has no branch.

    // The band's groups. Weights, not pixels: the panel is as wide as the editor and the
    // groups share whatever that is. Row one is Pattern / Playback / Steps, row two is
    // Spread / Feel.
    //
    // PATTERN went from 36 to 40 on 2026-07-30, when Rate became a dial: a knob column spans
    // both rows and takes 72 px off them (62 plus its two gaps) where the combo took none of
    // it, and 4 points of the weight hands ~37 px back. The 4 came out of STEPS, which had
    // ~50 px spare in each of its two rows (its widest control is a 150 px cell with a 120 px
    // floor), and none out of PLAYBACK, whose second row is the one place on the band with
    // nothing left to give - see the note beside Retrigger below.
    // Two groups, not three. STEPS left the band on 2026-08-18 for the Draw page, where the
    // lane it measures is actually drawn - changing how long a lane runs used to mean leaving
    // the page you were drawing it on. Its 18 points go back to the two that stayed rather
    // than being left as a gap, which is what the band's own weights are for.
    constexpr int groupWeights[2] = { 48, 52 };
    // FEEL took 4 points off SPREAD on 2026-08-02, when Humanize split into two sliders and
    // the group went from three to four of them: at the editor's minimum width 56% left the
    // fourth slider ~5 px under its 120 px floor, and SPREAD still fits its three cells
    // exactly at 40.
    constexpr int group2Weights[2] = { 40, 60 };
} // namespace

int ArpPanel::preferredHeight() const
{
    return contentHeight() + 16; // + the 8 px margin at both ends
}

// **The panel is exactly as tall as what is in it** (2026-08-16, Owen: "fix arp"). This and
// pageHeight() were a pair for two days and are now one answer, which is worth explaining
// because the pairing was a deliberate design and undoing it is too.
//
// 2026-08-14 made this a constant so that opening a line's deep view could not resize the
// window - the un-paged deep view was 612 px against the macro view's 240, so Details grew the
// window by 372 and All shrank it back, and on a screen that could not afford the 372 the keybed
// lost it off the bottom. Paging solved the *size*; the constant then solved the *movement*.
//
// What the constant cost was invisible and permanent: every view shorter than the tallest page
// carried the difference as dead panel. The macro view carried 58 px of it (fixed earlier the
// same day), and the Cards page carried **174** - 124 px of slots in a 298 px reservation - on a
// window Owen had already asked twice to make shorter. A constant that is a max over several sums
// grows the gap under every view but the tallest, silently, and nothing on screen says so.
//
// So the window moves again, and the honest accounting is: All <-> Details moves it by 58, and
// paging Play / Cards / Draw moves it by up to 174. That is a real cost and it was Owen's call to
// pay it. It is not the 2026-08-14 problem returning - that was 372 px on a *fold*, this is a
// window that fits its contents - but if paging ever feels unsettled, the fix is to pin the three
// pages back to `arpMax2(arpPageStepsH, arpMax2(arpPageSlotsH, arpPageSetupH))` and leave only
// the macro view answering for itself. That is one line, here.
//
// Still load-bearing that this answers from view and page state alone, never from what is *in* a
// page: preferredHeight() feeds the editor's idealHeight(), and anything finer-grained would be a
// window that moved while you worked inside one page.
int ArpPanel::contentHeight() const
{
    return pageHeight();
}

// What *this* view or page actually needs, as opposed to the fixed height the panel reserves.
// The two are a pair and the split is the point: contentHeight() feeds the editor, so the
// window never moves, while this feeds cardBounds(), so the drawn card is only as tall as
// what is in it. Without it the Slots page - 154 px of content in a 258 px card - drew its
// slots pinned to the bottom of a large empty box, since the block is laid out from the
// bottom up. The leftover is panel background, not a card with nothing in it.
int ArpPanel::pageHeight() const
{
    if (macroView)
        return processor.layout.arpMacroBottomFolded ? arpMacroFoldedTotalH : arpMacroTotalH;
    switch (currentPage())
    {
        case Page::steps:
            return patternMode() ? arpPageStepsH : arpPageSetupH; // Steps falls back to Setup
        case Page::slots:
            return arpPageSlotsH + ((euclidStripOpen || clocksStripOpen) ? arpStripH : 0);
        case Page::setup:
        default:
            return arpPageSetupH;
    }
}

ArpPanel::Page ArpPanel::currentPage() const
{
    return (Page) juce::jlimit(0, 2, processor.layout.arpPage);
}

// Steps is the lane editor, and outside Pattern shape there is no lane editor to show. The
// other two hold controls that act on the line whatever it is playing, so they never gate.
bool ArpPanel::pageAvailable(Page p) const
{
    return p != Page::steps || patternMode();
}

void ArpPanel::setPage(Page p)
{
    if (! pageAvailable(p))
        p = Page::setup; // the Steps tab greys rather than vanishing; a click on it lands here
    processor.layout.arpPage = (int) p;
    // A page with no slots on screen must not leave an armed Copy or Clear waiting: the pick
    // would fire, pages later, on a click the user armed and forgot. Same reasoning as
    // entering the macro view, which disarms for the same reason.
    if (p != Page::slots)
        setArmed(armNone);
    refreshShape();  // shape gates first, then applyPageVisibility() at its end
    if (onPageChanged)
        onPageChanged();
    // The pages are three different heights again since 2026-08-16, so changing one re-fits the
    // window. Cheap and idempotent: refreshShape() above may already have come back through here
    // on the Pattern fallback, and applyLayout() twice in a turn costs one extra layout pass.
    if (onPreferredHeightChanged)
        onPreferredHeightChanged();
    resized();
    repaint();
}

// The narrowest panel the macro view can be laid out in without starving something. Derived,
// never chosen: it walks the same insets `resized()` and `MacroRow::resized()` do, so a knob
// added to the strip moves this and every window that asks it, in one edit.
//
// Bottom up: nine knobs at their floor plus the two rings and eight gaps (422), the card's own
// side insets, two cards and the gap between them, the panel's area inset and the card inset.
// The knob strip is the binding line - the sub-row spends 416 of the same 422 and the top row
// rather less - so this is the one requirement worth deriving.
int ArpPanel::minMacroWidth()
{
    using MR = ArpPanel::MacroRow;
    const int rings = 2 * 2 * arpRingPx;
    const int strip = MR::numKnobs * arpMacroKnobMinW + rings + 6 * (MR::numKnobs - 1);
    // A card has two rows of fixed-size cells, and the floor is whichever is wider. The knob
    // strip alone was the floor until 2026-09-01, and the bottom strip - Dot, Tuplet, Anchor,
    // Details, then Legato - was quietly over it by ten pixels at the detached window's width
    // before the fifth chip made it sixty. The same shape as the 2026-08-21 lesson one level
    // up: a view drawn in two windows has two floors, and only the docked one was ever looked
    // at, because it is the wider one and everything fits there.
    const int card = juce::jmax(strip, arpMacroModsW) + 2 * arpMacroRowInset;
    return 2 * card + arpMacroCardGap + 2 * arpMacroAreaInset + 2 * arpMacroCardInset;
}

// The narrowest the panel may be drawn in *any* view, which is what a window asks for. The two
// halves are different kinds of number and are kept apart on purpose: the macro view's is
// derived from the knob count and moves on its own the moment a knob is added, while the deep
// pages' is measured and needs the test to keep it honest. Taking the larger is the whole of it.
int ArpPanel::minPanelWidth()
{
    return juce::jmax(minMacroWidth(), arpDeepPageMinW);
}

// The shortest the panel may be drawn in *any* view, the same question one axis over - and it
// exists because fixing only the width would have been the very mistake the width fix is about.
// `pageHeight()` answers for the view that happens to be showing; a *window* has one floor and
// has to clear the tallest of them, because a section switched to its tallest view inside an
// already-minimised window has nowhere to grow into and clips from the bottom.
//
// The macro view is the tallest at 690 (two card rows), which is also the default view, so this
// is not a corner case: the detached Arp window's old 300 px literal left the panel ~216 px and
// laid the second card row - lines C and D - out at zero height. Unfolded, so the floor does not
// depend on a persisted UI toggle: folding is something you do inside a window that already fits.
int ArpPanel::minPanelHeight()
{
    return juce::jmax(arpMacroTotalH,
                      juce::jmax(arpPageStepsH,
                                 juce::jmax(arpPageSlotsH + arpStripH, arpPageSetupH)));
}

juce::Rectangle<int> ArpPanel::cardBounds() const
{
    // pageHeight(), not contentHeight(): the editor hands the panel the fixed height so the
    // window never resizes, and the card drawn inside it is sized to the page actually
    // showing. Inline used to return `full` outright for the same reason it now does not -
    // the height it is given stopped being the height its content needs.
    const auto full = getLocalBounds().reduced(arpMacroCardInset);
    return full.withHeight(juce::jmin(full.getHeight(), pageHeight()));
}

void ArpPanel::paint(juce::Graphics& g)
{
    if (! inlineMode)
        g.fillAll(juce::Colours::black.withAlpha(0.78f)); // dim whatever is behind the overlay
    const auto b = cardBounds().toFloat();
    g.setColour(juce::Colour(0xff1c1f24));
    g.fillRoundedRectangle(b, skin::panelRadius);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.fillRoundedRectangle(b.withHeight(1.5f).reduced(skin::panelRadius, 0.0f), 0.75f);
    skin::glowRect(g, b, skin::panelRadius, skin::accentOf(*this).base, inlineMode ? 0.30f : 0.55f);

    // Which line the deep view below is showing. Nothing else says so any more: the A/B tabs
    // that used to name it left for the ARP section bar as the per-line On switches on
    // 2026-08-02, and the panel's own header strip and LineTab class went with an earlier pass
    // the same day. Sits in the card's own top margin - the strip cardBounds().reduced(12)
    // leaves blank above the band - so it costs no extra height, and uses the same micro-caps
    // treatment the macro cards give their own "LINE A" / "LINE B".
    if (! macroView)
    {
        const auto letter = juce::String::charToString((juce::juce_wchar) ('A' + editedLine));
        g.setFont(skin::micro(9.5f).withExtraKerningFactor(0.16f));
        g.setColour(skin::lineAccent(editedLine).base); // the line's own colour, 2026-08-19
        g.drawText("LINE " + letter, b.withHeight(12.0f).withTrimmedRight(10.0f).toNearestInt(),
                   juce::Justification::centredRight);
    }

    // The band's group boxes: a hairline frame and a micro-caps caption sitting in a gap
    // punched through the top rule, after the hardware panels this layout follows. Drawn
    // rather than built from components - it is four lines of Graphics per group.
    g.setFont(skin::micro(9.5f).withExtraKerningFactor(0.16f));
    for (const auto& grp : groups)
    {
        if (! grp.visible || grp.bounds.isEmpty())
            continue;
        const auto r = grp.bounds.toFloat();
        const auto caption = grp.caption.toUpperCase();
        const auto textW = juce::GlyphArrangement::getStringWidth(skin::micro(9.5f).withExtraKerningFactor(0.16f),
                                                                 caption);
        const float capY = r.getY() + 5.0f;

        g.setColour(juce::Colours::white.withAlpha(0.07f));
        juce::Path frame;
        frame.startNewSubPath(r.getCentreX() - textW * 0.5f - 8.0f, capY);
        frame.lineTo(r.getX() + skin::radius, capY);
        frame.quadraticTo(r.getX(), capY, r.getX(), capY + skin::radius);
        frame.lineTo(r.getX(), r.getBottom() - skin::radius);
        frame.quadraticTo(r.getX(), r.getBottom(), r.getX() + skin::radius, r.getBottom());
        frame.lineTo(r.getRight() - skin::radius, r.getBottom());
        frame.quadraticTo(r.getRight(), r.getBottom(), r.getRight(), r.getBottom() - skin::radius);
        frame.lineTo(r.getRight(), capY + skin::radius);
        frame.quadraticTo(r.getRight(), capY, r.getRight() - skin::radius, capY);
        frame.lineTo(r.getCentreX() + textW * 0.5f + 8.0f, capY);
        g.strokePath(frame, juce::PathStrokeType(1.0f));

        g.setColour(skin::textDim);
        g.drawText(caption, grp.bounds.withHeight(12).withY((int) capY - 6),
                   juce::Justification::centred);
    }
}

void ArpPanel::resized()
{
    auto area = cardBounds().reduced(arpMacroAreaInset);

    // No title row. The section bar above says ARP and carries the On toggle and Detach, so a
    // second "Arpeggiator" caption with its own On and Close was three duplicated controls and
    // a wasted 38 px of a panel that is already the tallest thing in the editor.

    // --- The macro view takes the band's place, never sits beside it ----------------
    if (macroView)
    {
        auto block = area.removeFromTop(arpMacroH);
        area.removeFromTop(arpMacroBelow);
        // No LINES frame and no header strip: the view is the two cards and nothing else
        // (2026-08-02, Owen: "remove the 'lines' text" - and a box drawn around both cards
        // was the single strongest cue that they were one thing, the exact complaint). The
        // tabs, BPM and Quantize live on the ARP section bar now, and each card draws its
        // own captioned frame.
        for (auto& grp : groups)
            grp.visible = false;

        // The cards, a 2x2 grid since 2026-08-19 (Owen: "I want 4 arps"). Two columns is
        // load-bearing: a card's knob strip needs ~430 px, so a column is always half the
        // panel and more lines mean more *rows* - height is the cheap axis in this view,
        // width the expensive one, and this is that rule as arithmetic.
        const bool folded = processor.layout.arpMacroBottomFolded;
        auto cardsArea = block.removeFromTop(folded ? arpMacroFoldedH : arpMacroH);
        const int cardGap = arpMacroCardGap;
        const int cols = 2;
        const int rows = (juce::jmax(1, KeysProcessor::uiArpLines) + cols - 1) / cols;
        for (int rowIdx = 0; rowIdx < rows; ++rowIdx)
        {
            // The last row collapses to the strip while folded. Every card in it is *hidden*
            // rather than resized: a card squeezed into 34 px would draw its knobs on top of one
            // another, and its controls would still take the mouse. The lines themselves keep
            // running - this is a view, exactly as the All/Details split is.
            const bool foldedRow = folded && rowIdx == rows - 1;
            if (foldedRow)
            {
                auto stripArea = cardsArea.removeFromTop(arpMacroStripH);
                if (foldedRowStrip != nullptr)
                {
                    foldedRowStrip->setVisible(true);
                    foldedRowStrip->setBounds(stripArea);
                }
                for (int c = 0; c < cols; ++c)
                {
                    const int n = rowIdx * cols + c;
                    if (n < (int) macroRows.size() && macroRows[(size_t) n] != nullptr)
                        macroRows[(size_t) n]->setVisible(false);
                }
                continue;
            }

            auto rowArea = cardsArea.removeFromTop(arpMacroCard);
            cardsArea.removeFromTop(arpMacroRowGap);
            const int colW = (rowArea.getWidth() - cardGap * (cols - 1)) / cols;
            for (int c = 0; c < cols; ++c)
            {
                const int n = rowIdx * cols + c;
                if (n >= (int) macroRows.size() || macroRows[(size_t) n] == nullptr)
                    continue;
                macroRows[(size_t) n]->setVisible(true);
                macroRows[(size_t) n]->setBounds(rowArea.removeFromLeft(colW));
                rowArea.removeFromLeft(cardGap);
            }
        }
        if (! folded && foldedRowStrip != nullptr)
            foldedRowStrip->setVisible(false);
    }

    // Which page's blocks claim space this pass (2026-08-14). Everything below already used
    // the `macroView ? emptyRect : removeFromTop(...)` idiom, so paging it is a change of
    // condition rather than a restructure: an off-page block lays itself out into an empty
    // rectangle, which the comments below already call harmless because those controls are
    // invisible. applyPageVisibility() is what makes that second half true.
    const bool wantSetup = ! macroView && currentPage() == Page::setup;
    const bool wantSlots = ! macroView && currentPage() == Page::slots;
    const bool wantSteps = ! macroView && currentPage() == Page::steps && patternMode();

    // --- SETUP page: the control band, three captioned groups sharing the width ----
    // Voice left the STEPS group on 2026-08-14 for the lane-tab row, where it costs no height
    // at all and sits beside the lane it belongs to, so the band is back to its two rows.
    auto band = wantSetup ? area.removeFromTop(arpBandH) : juce::Rectangle<int>();
    if (wantSetup)
        area.removeFromTop(8);
    auto band2 = wantSetup ? area.removeFromTop(arpBand2H) : juce::Rectangle<int>();
    if (wantSetup)
        area.removeFromTop(12);
    if (wantSetup)
    {
        const int usable = band.getWidth() - 10;
        const int total = groupWeights[0] + groupWeights[1];
        groups[0].bounds = band.removeFromLeft(usable * groupWeights[0] / total);
        band.removeFromLeft(10);
        groups[1].bounds = band;
        groups[2].bounds = {}; // STEPS is on the Draw page now
        const int usable2 = band2.getWidth() - 10;
        const int total2 = group2Weights[0] + group2Weights[1];
        groups[3].bounds = band2.removeFromLeft(usable2 * group2Weights[0] / total2);
        band2.removeFromLeft(10);
        groups[4].bounds = band2;
        groups[0].caption = "Pattern";
        groups[1].caption = "Playback";

        groups[3].caption = "Spread";
        groups[4].caption = "Feel";
        for (int i = 0; i < (int) groups.size(); ++i)
            groups[(size_t) i].visible = true;
        groups[2].visible = false; // STEPS is a Draw-page strip now, not a band group
    }

    // Inside a group: past the caption rule, then two rows of controls, or the full height
    // for a knob column. A control with a label above it gets `cell`; a bare toggle gets
    // `toggleCell`, which drops the same 14 px so the two line up.
    const auto groupInner = [](const juce::Rectangle<int>& g)
    {
        return g.reduced(10, 0).withTrimmedTop(arpBandTop).withHeight(arpBandInner);
    };
    const auto splitRows = [](juce::Rectangle<int> inner, juce::Rectangle<int>& rowA,
                              juce::Rectangle<int>& rowB)
    {
        rowA = inner.removeFromTop(arpBandRow);
        inner.removeFromTop(6);
        rowB = inner.removeFromTop(arpBandRow);
    };
    const auto cell = [](juce::Rectangle<int>& row, int w, juce::Label& lab, juce::Component& ctl)
    {
        auto c = row.removeFromLeft(w);
        row.removeFromLeft(8);
        lab.setBounds(c.removeFromTop(14));
        ctl.setBounds(c.withSizeKeepingCentre(c.getWidth(), juce::jmin(c.getHeight(), 28)));
    };
    const auto toggleCell = [](juce::Rectangle<int>& row, int w, juce::Component& ctl)
    {
        auto c = row.removeFromLeft(w);
        row.removeFromLeft(6);
        ctl.setBounds(c.withTrimmedTop(14));
    };
    // A knob spans the group's whole height: caption, then the rotary, then its value.
    // Squeezed into one 42 px row it came out a 16 px dot, well under the mouse-only floor.
    const auto knobColumn = [](juce::Rectangle<int>& area, int w, juce::Label& lab, juce::Slider& s)
    {
        auto c = area.removeFromLeft(w);
        area.removeFromLeft(6);
        lab.setBounds(c.removeFromTop(12));
        s.setBounds(c);
    };
    const auto stepper = [](juce::Rectangle<int>& row, juce::Component& prev, juce::Component& next)
    {
        auto c = row.removeFromLeft(72).withTrimmedTop(14);
        row.removeFromLeft(8);
        c = c.withSizeKeepingCentre(c.getWidth(), juce::jmin(c.getHeight(), 28));
        prev.setBounds(c.removeFromLeft(34));
        next.setBounds(c.removeFromRight(34));
    };

    // PATTERN: what it plays and how fast. The rate dial takes the left column and spans both
    // rows, the way the PLAYBACK knobs do; Shape leads the rows, since it decides what else
    // exists. Everything on the second row is about the rate the dial shows.
    {
        auto inner = groupInner(groups[0].bounds);
        // 62, where the PLAYBACK knobs get 50: this one's readout has to fit "16 bars" as
        // well as a number. In a single 42 px row it would have come out a ~26 px knob, well
        // under the kit's 48 px floor for a rotary (okstudio/RotaryKnob.h).
        knobColumn(inner, 62, rateLabel, rateKnob);
        inner.removeFromLeft(4);

        juce::Rectangle<int> rowA, rowB;
        splitRows(inner, rowA, rowB);
        // Fixed widths, not "whatever is left": letting a control soak up the slack starved
        // Tuplet and Dot down to an ellipsis while Rate sat wider than its longest entry.
        // These add up to the ~280 px the two rows get at the editor's minimum width (the
        // group's ~352, less the dial column), which is a good deal less than it looks on a
        // 150% display - every number here is logical pixels, and the panel is ~950 of them
        // wide, not ~1450.
        cell(rowA, juce::jlimit(110, 235, rowA.getWidth() - 80), shapeLabel, shapeBox);
        stepper(rowA, shapePrev, shapeNext);

        // The rate steppers and the mode switch are the only band controls laid out at the
        // full 34 px hit height instead of the band's 28. The dial beside them is a drag
        // target and these three are the click-only way to everything it holds, which makes
        // them worth 6 px of a row that had the room. Bottom-aligned, so the row still reads
        // as one line with Tuplet and Dot.
        auto rateSteps = rowB.removeFromLeft(72).removeFromBottom(34);
        rowB.removeFromLeft(8);
        ratePrev.setBounds(rateSteps.removeFromLeft(34));
        rateNext.setBounds(rateSteps.removeFromRight(34));
        // 52, down from 58: the four letters of "Sync" never needed the other six, and Tuplet
        // below does.
        rateModeButton.setBounds(rowB.removeFromLeft(52).removeFromBottom(34));
        rowB.removeFromLeft(6);
        // Tuplet is a captioned combo now, so it takes `cell` like Shape above rather than
        // `toggleCell`, and it needs room for "5-tuplet" plus a chevron. Its 84 comes out of
        // Sync's six and Dot's four (52 -> 48 still holds a tick and three letters), so the
        // row still adds up to less than it has: a combo made to ellipsise its own entries is
        // the Voices trap off the Controls bar all over again.
        cell(rowB, 84, tupletLabel, tupletBox);
        toggleCell(rowB, 48, dotButton);
    }

    // PLAYBACK: how the run behaves once it is going. Three knobs down the left, then the
    // two rows of discrete controls. Anchor sits here rather than with Rate: it is about
    // how the clock runs, not about what the pattern is.
    {
        auto inner = groupInner(groups[1].bounds);
        knobColumn(inner, 50, swingLabel, swingSlider);
        knobColumn(inner, 50, gateLabel, gateSlider);
        knobColumn(inner, 50, densityLabel, densitySlider);
        inner.removeFromLeft(8);

        // ~195 px left after the knobs, and every one of these is spent. A toggle needs
        // 20 px of tick box plus 9 px of gap before its text starts (see the skin's
        // drawToggleButton), which is why they look wider than their words.
        juce::Rectangle<int> rowA, rowB;
        splitRows(inner, rowA, rowB);
        // Retrigger is a combo where Repeats used to be a stepper. It had the whole row to
        // itself until PLAY needed a band home (2026-08-02, when the macro rows slimmed
        // down); Play's cell is reserved off the right *first* - it is the fixed-size one,
        // and an elastic control with a floor must never be asked to leave room for anything
        // (the 2026-08-02 Shape lesson) - and Retrigger keeps its 120 px floor in the rest.
        // Anchor stays down beside Latch, where the 2026-07-30 overflow put it.
        auto playCell = rowA.removeFromRight(64);
        rowA.removeFromRight(6);
        cell(rowA, juce::jmax(120, rowA.getWidth()), retrigLabel, retrigBox);
        keysBandButton.setBounds(playCell.withTrimmedTop(14));
        toggleCell(rowB, 78, latchButton);
        toggleCell(rowB, 83, anchorButton);
    }

    // SPREAD: how wide the chord is stacked, and where the run starts inside it. The three
    // belong together - Repeats without Distance is only ever octaves, and Offset is the
    // other thing you reach for once the run is longer than the chord.
    {
        auto inner = groupInner(groups[3].bounds).withHeight(arpBandRow);
        auto row = inner;
        cell(row, 104, octavesLabel, octavesSlider);
        cell(row, juce::jlimit(96, 128, row.getWidth() - 120), distanceLabel, distanceBox);
        cell(row, 104, offsetLabel, offsetSlider);
    }

    // FEEL: the four that decide whether it sounds played (Humanize is two since
    // 2026-08-02, its timing and velocity halves split at Owen's ask). Sliders share what
    // is left equally, since none of them has a natural width and all four are dragged,
    // not read.
    {
        auto inner = groupInner(groups[4].bounds).withHeight(arpBandRow);
        auto row = inner;
        const int each = juce::jmax(120, (row.getWidth() - 32) / 5); // five since Drift joined
        cell(row, each, rampLabel, rampSlider);
        cell(row, each, rampTimeLabel, rampTimeSlider);
        cell(row, each, humanLabel, humanSlider);
        cell(row, each, humanVelLabel, humanVelSlider);
        cell(row, juce::jmax(120, row.getWidth()), driftLabel, driftSlider);
    }


    // --- SLOTS page: the twelve cards, the action row, and either strip ------------
    // Not the macro view, since 2026-08-02: the slots and the action row belong to the
    // per-line tabs, and the A/B/All tabs themselves were laid out in its header above -
    // running this block there would move them right back down to a row that no longer
    // exists on screen. Its own page since 2026-08-14, which is what let the whole block
    // keep its full-size targets while the deep view stopped being 612 px tall.
    if (wantSlots)
    {
        auto actionRow = area.removeFromBottom(34);
        area.removeFromBottom(8);
        // Euclid and Clocks open a strip directly above the action row, which stays put at
        // the bottom edge - the row that grows is the one above it, not the one the toggle
        // buttons live in. At most one of these is ever non-empty; the other is a zero-height
        // rect, which is harmless since its strip's components are invisible too.
        juce::Rectangle<int> euclidRow, clocksRow;
        if (euclidStripOpen)
        {
            euclidRow = area.removeFromBottom(34);
            area.removeFromBottom(8);
        }
        if (clocksStripOpen)
        {
            clocksRow = area.removeFromBottom(34);
            area.removeFromBottom(8);
        }
        auto slotRow = area.removeFromBottom(arpSlotsH);
        area.removeFromBottom(12);

        // Twelve cells, no tabs: the A/B/All tabs left this row for the ARP section bar on
        // 2026-08-02, so the slots share the whole width between them.
        const int n = (int) slotCards.size();
        const int gap = 4;
        const int w = juce::jmax(46, (slotRow.getWidth() - gap * (n - 1)) / n);
        for (auto& c : slotCards)
        {
            if (c != nullptr)
                c->setBounds(slotRow.removeFromLeft(w));
            slotRow.removeFromLeft(gap);
        }
        copyButton.setBounds(actionRow.removeFromLeft(armed == armCopy ? 130 : 84));
        actionRow.removeFromLeft(4);
        clearButton.setBounds(actionRow.removeFromLeft(armed == armClear ? 136 : 84));
        // Cancel only exists while something is armed, so it only takes width then -
        // otherwise it leaves a hole that reads as a missing button.
        if (armed != armNone)
        {
            actionRow.removeFromLeft(4);
            cancelButton.setBounds(actionRow.removeFromLeft(80));
        }
        actionRow.removeFromLeft(12);
        stopButton.setBounds(actionRow.removeFromLeft(84));
        actionRow.removeFromLeft(12);
        randomizeButton.setBounds(actionRow.removeFromLeft(110));
        actionRow.removeFromLeft(8);
        euclidButton.setBounds(actionRow.removeFromLeft(110));

        // Chain and its Bars stepper sit at the far end, away from the four that act on one
        // slot: these two are about the row as a whole. Clocks joins them, for the same
        // reason - it acts on the line, not on one slot.
        auto barsCell = actionRow.removeFromRight(126);
        barsPlus.setBounds(barsCell.removeFromRight(34));
        barsMinus.setBounds(barsCell.removeFromLeft(34));
        barsReadout.setBounds(barsCell); // says "2 bars", so it needs no caption beside it
        actionRow.removeFromRight(8);
        chainButton.setBounds(actionRow.removeFromRight(96));
        actionRow.removeFromRight(8);
        clocksButton.setBounds(actionRow.removeFromRight(96));

        // Each strip: three or four [caption][-][readout][+] groups left to right, laid out
        // regardless of which one is actually open this frame (the closed one's row is
        // zero-width, so its removeFromLeft calls are simple no-ops).
        const auto stepperGroup = [](juce::Rectangle<int>& row, int w, juce::Label& cap,
                                     juce::TextButton& minus, juce::Label& readout, juce::TextButton& plus)
        {
            auto cell = row.removeFromLeft(w);
            row.removeFromLeft(8);
            cap.setBounds(cell.removeFromLeft(40));
            minus.setBounds(cell.removeFromLeft(34));
            plus.setBounds(cell.removeFromRight(34));
            readout.setBounds(cell);
        };
        stepperGroup(euclidRow, 156, euclidHitsLabel, euclidHitsMinus, euclidHitsReadout, euclidHitsPlus);
        stepperGroup(euclidRow, 156, euclidStepsLabel, euclidStepsMinus, euclidStepsReadout, euclidStepsPlus);
        stepperGroup(euclidRow, 156, euclidRotateLabel, euclidRotateMinus, euclidRotateReadout, euclidRotatePlus);
        for (int i = 0; i < 4; ++i)
            stepperGroup(clocksRow, 140, clockDivLabels[(size_t) i], clockDivMinus[(size_t) i],
                        clockDivReadouts[(size_t) i], clockDivPlus[(size_t) i]);
    }

    // --- STEPS page: the lane tabs, the one lane they select, the mute row ---------
    // Laying this out with an empty `area` on the other pages is harmless (it is all
    // invisible by then - see applyPageVisibility) and keeps this function free of a
    // second branch.
    if (! wantSteps)
        area = juce::Rectangle<int>();

    auto tabsRow = area.removeFromTop(34);
    area.removeFromTop(6);

    // The lane tabs get the whole row, and the buttons that act on a lane get their own strip
    // below it (2026-08-14). They shared this row until the Chain lane made twelve tabs: at the
    // 70 px floor twelve need 884 px and the row had 784 left after the buttons, so Rand was
    // squeezed to 63 and Chain was laid out at zero width - present in the tree, invisible on
    // screen, and absent from the accessibility tree with nothing to say why.
    //
    // **A crowded row grows a strip; it does not squeeze its targets** - the rule already
    // logged twice (2026-08-01, 2026-08-02) and paid for a third time here. Height is the cheap
    // axis on this page and 34 px buys every tab its full width back: twelve now get ~99 px
    // each against a 70 px floor.
    // Counted, not hard-coded (2026-08-18). This was `(width - 11*4) / 12` with the twelve
    // written in, so appending the Reset lane laid its tab out at four pixels wide - the same
    // invisible starvation the Chain lane caused when it made twelve, one row lower down. Any
    // lane appended from here divides the row correctly on the day it arrives.
    int tabCount = 0;
    for (const auto& lr : laneRows)
        if (lr.hasTab)
            ++tabCount;
    const int tabW = juce::jmax(70, (tabsRow.getWidth() - juce::jmax(0, tabCount - 1) * 4) / juce::jmax(1, tabCount));
    for (auto& lr : laneRows)
    {
        if (! lr.hasTab)
            continue; // Mute has a lane but no tab - the MUTE row below is its editor
        lr.tab.setBounds(tabsRow.removeFromLeft(tabW));
        tabsRow.removeFromLeft(4);
    }

    // The lane strip: what the selected lane *is*, on the page it is drawn on. Steps, Speed
    // and Link came off the Setup page's band for this (2026-08-18) - they are per-lane
    // controls, and having them a page away meant changing a lane's length was a trip out of
    // the editor and back. On/Off and Dir are new beside them, and the four belong together:
    // between them they say how long this lane is, how fast it advances, and which way.
    auto laneRow = area.removeFromTop(34);
    area.removeFromTop(6);
    laneOnButton.setBounds(laneRow.removeFromLeft(64));
    laneRow.removeFromLeft(10);
    stepsLabel.setBounds(laneRow.removeFromLeft(46));
    stepsMinus.setBounds(laneRow.removeFromLeft(34));
    stepsReadout.setBounds(laneRow.removeFromLeft(44));
    stepsPlus.setBounds(laneRow.removeFromLeft(34));
    laneRow.removeFromLeft(10);
    speedLabel.setBounds(laneRow.removeFromLeft(52));
    speedButton.setBounds(laneRow.removeFromLeft(58));
    laneRow.removeFromLeft(10);
    dirLabel.setBounds(laneRow.removeFromLeft(34));
    dirPrev.setBounds(laneRow.removeFromLeft(34));
    dirReadout.setBounds(laneRow.removeFromLeft(72));
    dirNext.setBounds(laneRow.removeFromLeft(34));
    linkButton.setBounds(laneRow.removeFromRight(70)); // Link is about all of them, so it sits apart

    // The lane tools, left to right, in the order you reach for them: mark a span, then put it
    // back, roll it, or take a copy of it. Voice sits at the right end, away from the rest,
    // because it is the one control here that edits the *sound* rather than the drawing - and
    // it only appears at all with the Harmony lane up.
    auto toolsRow = area.removeFromTop(34);
    area.removeFromTop(6);
    selectButton.setBounds(toolsRow.removeFromLeft(84));
    toolsRow.removeFromLeft(6);
    resetButton.setBounds(toolsRow.removeFromLeft(72));
    toolsRow.removeFromLeft(6);
    rollButton.setBounds(toolsRow.removeFromLeft(72));
    toolsRow.removeFromLeft(6);
    rollMinus.setBounds(toolsRow.removeFromLeft(34));
    rollReadout.setBounds(toolsRow.removeFromLeft(52));
    rollPlus.setBounds(toolsRow.removeFromLeft(34));
    toolsRow.removeFromLeft(12);
    copyStepsButton.setBounds(toolsRow.removeFromLeft(72));
    toolsRow.removeFromLeft(6);
    pasteStepsButton.setBounds(toolsRow.removeFromLeft(72));
    voiceButton.setBounds(toolsRow.removeFromRight(112));

    auto gridArea = area.removeFromTop(140);
    area.removeFromTop(4);
    for (auto& lr : laneRows)
        if (lr.grid != nullptr)
            lr.grid->setBounds(gridArea); // all share the slot; only one is visible

    // The loop bar, directly under the grid and off the same rectangle, so a window lands on
    // the steps it is drawn over. Same rule the MUTE strip already follows and for the same
    // reason: a gutter on one and not the other silently slides every cell off its step.
    if (loopBar != nullptr)
        loopBar->setBounds(area.removeFromTop(16));
    area.removeFromTop(6);

    // The mute strip divides its own width into the same step count the grid does, so it
    // only reads as "the steps above, muted" while the two share an origin and a width.
    // The caption therefore goes above the strip, not beside it: a left gutter on one and
    // not the other silently slid every mute cell off the step it belongs to.
    muteRowLabel.setBounds(area.removeFromTop(14));
    area.removeFromTop(2);
    muteRow->setBounds(area.removeFromTop(32)); // same x and width as gridArea, both off `area`
}

} // namespace keys
