#include "LaneGrid.h"
#include "ArpPanel.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>
#include <cmath>

namespace keys
{
LaneGrid::LaneGrid(KeysProcessor& p, ArpPanel& o, ArpEngine::Lane l, int lo, int hi)
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

int LaneGrid::currentLength() const
{
    return juce::jlimit(1, ArpEngine::maxSteps,
                        processor.arpLine(owner.editLine()).lanes.length[(size_t) lane].load(std::memory_order_relaxed));
}

int LaneGrid::stepAtX(float x) const
{
    const int length = currentLength();
    const float w = (float) getWidth();
    if (w <= 0.0f)
        return 0;
    return juce::jlimit(0, length - 1, (int) (x / (w / (float) length)));
}

int LaneGrid::valueAtY(float y) const
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
void LaneGrid::paintStepFromMouse(const juce::MouseEvent& e, int step)
{
    if (step < 0)
        step = stepAtX(e.position.x);
    const int value = valueAtY(e.position.y);
    processor.arpLine(owner.editLine()).lanes.value[(size_t) lane][(size_t) step].store(value, std::memory_order_relaxed);
    cursorPos = e.position;
    cursorValue = value;
    repaint();
}

void LaneGrid::mouseDown(const juce::MouseEvent& e)
{
    // In Select mode a drag marks a span rather than painting one. The anchor is where the
    // press landed and the far end follows the mouse, so a span can be drawn either way round;
    // the span is normalised on every update rather than at the end, since Roll can be
    // clicked mid-gesture.
    if (owner.selectModeOn())
    {
        dragging = true;
        selAnchor = stepAtX(e.position.x);
        owner.setSelectionSpan(selAnchor, selAnchor);
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

void LaneGrid::mouseDrag(const juce::MouseEvent& e)
{
    if (owner.selectModeOn())
    {
        const int s = stepAtX(e.position.x);
        owner.setSelectionSpan(juce::jmin(selAnchor, s), juce::jmax(selAnchor, s));
        owner.repaint();
        return;
    }
    paintStepFromMouse(e, paintStep); // the step the press landed on, whatever x does now
}

void LaneGrid::mouseUp(const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

juce::String LaneGrid::noteNameFor(int value) const
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

juce::String LaneGrid::cellText(int value) const
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

void LaneGrid::paint(juce::Graphics& g)
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
    if (owner.selectModeOn() && owner.selectionFrom() >= 0)
    {
        const int len = currentLength();
        const float cellW = len > 0 ? (float) getWidth() / (float) len : (float) getWidth();
        const int lo = juce::jlimit(0, len - 1, owner.selectionFrom());
        const int hi = juce::jlimit(lo, len - 1, owner.selectionTo());
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

MuteRow::MuteRow(KeysProcessor& p, const ArpPanel& o) : processor(p), owner(o)
{
    okstudio::ui::makeMouseOnly(*this);
}

// **The mute lane is the Note lane's companion, not a polymetric lane of its own.** It reads
// and writes at the Note lane's length, and syncs its own to match (2026-08-14) - the engine
// wraps every lane read by that lane's own length, so if the two ever disagreed a mute drawn
// at step 20 of a 32-step pattern would be read back modulo 8 and silence the wrong step. It
// has no tab and no STEPS control, so there is nowhere for a user to set it and nothing to
// gain from letting it differ.
int MuteRow::currentLength() const
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

int MuteRow::stepAtX(float x) const
{
    const int length = currentLength();
    const float w = (float) getWidth();
    if (w <= 0.0f)
        return 0;
    return juce::jlimit(0, length - 1, (int) (x / (w / (float) length)));
}

void MuteRow::applyAtX(float x)
{
    const int step = stepAtX(x);
    processor.arpLine(owner.editLine()).lanes.value[(size_t) ArpEngine::laneMute][(size_t) step].store(paintValue, std::memory_order_relaxed);
    repaint();
}

void MuteRow::mouseDown(const juce::MouseEvent& e)
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

void MuteRow::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging)
        applyAtX(e.position.x);
}

// --- LoopBar -------------------------------------------------------------------------------

LoopBar::LoopBar(KeysProcessor& p, const ArpPanel& o) : processor(p), owner(o)
{
    okstudio::ui::makeMouseOnly(*this);
    setTitle("Lane loop");
    setTooltip("The steps this lane walks. Click to move the nearer end, or drag it.");
}

int LoopBar::stepAtX(float x) const
{
    auto& lanes = processor.arpLine(owner.editLine()).lanes;
    const int len = juce::jlimit(1, ArpEngine::maxSteps,
                                 lanes.length[(size_t) owner.selectedLaneIndex()].load(std::memory_order_relaxed));
    const float cellW = (float) getWidth() / (float) len;
    return juce::jlimit(0, len - 1, (int) (x / juce::jmax(1.0f, cellW)));
}

void LoopBar::moveNearestHandle(float x)
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
    const bool linked = owner.lanesLinked();
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

void LoopBar::mouseDown(const juce::MouseEvent& e)
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

void LoopBar::mouseDrag(const juce::MouseEvent& e) { moveNearestHandle(e.position.x); }

void LoopBar::paint(juce::Graphics& g)
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

void MuteRow::paint(juce::Graphics& g)
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
} // namespace keys
