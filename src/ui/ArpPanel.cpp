#include "ArpPanel.h"
#include "KeysLookAndFeel.h"
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

ArpPanel::LaneGrid::LaneGrid(KeysProcessor& p, const ArpPanel& o, ArpEngine::Lane l, int lo, int hi)
    : processor(p), owner(o), lane(l), loVal(lo), hiVal(hi)
{
    okstudio::ui::makeMouseOnly(*this);
}

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

void ArpPanel::LaneGrid::paintStepFromMouse(const juce::MouseEvent& e)
{
    const int step = stepAtX(e.position.x);
    const int value = valueAtY(e.position.y);
    processor.arpLine(owner.editLine()).lanes.value[(size_t) lane][(size_t) step].store(value, std::memory_order_relaxed);
    cursorPos = e.position;
    cursorValue = value;
    repaint();
}

void ArpPanel::LaneGrid::mouseDown(const juce::MouseEvent& e)
{
    dragging = true;
    paintStepFromMouse(e);
}

void ArpPanel::LaneGrid::mouseDrag(const juce::MouseEvent& e)
{
    paintStepFromMouse(e); // continuous paint: every move updates the step under it
}

void ArpPanel::LaneGrid::mouseUp(const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

juce::String ArpPanel::LaneGrid::cellText(int value) const
{
    if (lane == ArpEngine::laneNote)
    {
        if (value <= -1)
            return "X";
        if (value == 0)
            return {}; // drawn as a dot instead, see paint()
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

    for (int i = 0; i < length; ++i)
    {
        const int value = juce::jlimit(loVal, hiVal,
                                       processor.arpLine(owner.editLine()).lanes.value[(size_t) lane][(size_t) i].load(std::memory_order_relaxed));
        auto cell = juce::Rectangle<float>(b.getX() + cellW * (float) i, b.getY(), cellW, b.getHeight());

        g.setColour(juce::Colours::white.withAlpha(0.045f));
        g.drawVerticalLine((int) cell.getX(), b.getY(), b.getBottom());

        const auto bar = cell.reduced(1.5f);
        const float frac = hiVal > loVal ? (float) (value - loVal) / (float) (hiVal - loVal) : 0.0f;
        const auto filled = bar.withTop(bar.getBottom() - bar.getHeight() * frac);
        if (filled.getHeight() > 0.5f)
        {
            g.setGradientFill({ skin::accentOf(*this).base.withAlpha(0.55f), 0.0f, filled.getY(),
                                skin::accentOf(*this).deep.withAlpha(0.4f), 0.0f, bar.getBottom(), false });
            g.fillRect(filled);
            g.setColour(skin::accentOf(*this).hot.withAlpha(0.9f));
            g.fillRect(filled.getX(), filled.getY(), filled.getWidth(), 1.5f);
        }

        const bool asDot = (lane == ArpEngine::laneNote && value == 0);
        if (asDot)
        {
            const float r = juce::jmin(6.0f, cell.getWidth() * 0.25f);
            g.setColour(skin::text);
            g.fillEllipse(cell.getCentreX() - r, cell.getCentreY() - r, r * 2.0f, r * 2.0f);
        }
        else if (cell.getWidth() > 16.0f)
        {
            const auto txt = cellText(value);
            if (txt.isNotEmpty())
            {
                g.setColour(lane == ArpEngine::laneNote && value == -1 ? skin::textFaint : skin::text);
                g.setFont(skin::ui(11.0f));
                g.drawText(txt, cell.toNearestInt(), juce::Justification::centred);
            }
        }
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
}

// ---------------------------------------------------------------------------
// MuteRow

ArpPanel::MuteRow::MuteRow(KeysProcessor& p, const ArpPanel& o) : processor(p), owner(o)
{
    okstudio::ui::makeMouseOnly(*this);
}

int ArpPanel::MuteRow::currentLength() const
{
    return juce::jlimit(1, ArpEngine::maxSteps,
                        processor.arpLine(owner.editLine()).lanes.length[(size_t) ArpEngine::laneNote].load(std::memory_order_relaxed));
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
    processor.arpLine(owner.editLine()).lanes.value[(size_t) ArpEngine::laneNote][(size_t) step].store(paintValue, std::memory_order_relaxed);
    repaint();
}

void ArpPanel::MuteRow::mouseDown(const juce::MouseEvent& e)
{
    const int step = stepAtX(e.position.x);
    const int current = processor.arpLine(owner.editLine()).lanes.value[(size_t) ArpEngine::laneNote][(size_t) step].load(std::memory_order_relaxed);
    paintValue = (current == -1) ? 0 : -1; // toggle, then paint every step the drag crosses to match
    dragging = true;
    applyAtX(e.position.x);
}

void ArpPanel::MuteRow::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging)
        applyAtX(e.position.x);
}

void ArpPanel::MuteRow::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    const int length = currentLength();
    const float cellW = length > 0 ? b.getWidth() / (float) length : b.getWidth();

    for (int i = 0; i < length; ++i)
    {
        const int value = processor.arpLine(owner.editLine()).lanes.value[(size_t) ArpEngine::laneNote][(size_t) i].load(std::memory_order_relaxed);
        const bool muted = value == -1;
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
    return juce::jlimit(0, KeysProcessor::numArpLines - 1, editedLine);
}

// A chord card dragged out of the pad strip, in screen coordinates. Mouse capture keeps the
// whole gesture on the strip - JUCE never tells these cards a drag is over them - and the two
// surfaces can be in different top-level windows, so the editor that owns both hit-tests here
// by screen position. Desktop::findComponentAt is what makes another window sitting over the
// row read as "not over a slot", which is the answer you want.
// Walking up from whatever the desktop says is under the point, rather than testing bounds,
// is what makes another window over the row read as "not over a slot" - and it answers the
// folded and detached cases for free, since a panel that is not on screen is never hit. Same
// shape as ChordPads::externalDropSlotAt, deliberately.
int ArpPanel::externalDropSlotAt(juce::Point<int> screenPos) const
{
    auto* hit = juce::Desktop::getInstance().findComponentAt(screenPos);
    for (auto* c = hit; c != nullptr; c = c->getParentComponent())
        for (int i = 0; i < (int) slotCards.size(); ++i)
            if (c == slotCards[(size_t) i].get())
                return i;
    return -1;
}

int ArpPanel::externalDropLineAt(juce::Point<int> screenPos) const
{
    auto* hit = juce::Desktop::getInstance().findComponentAt(screenPos);
    for (auto* c = hit; c != nullptr; c = c->getParentComponent())
        for (int n = 0; n < (int) lineTabs.size(); ++n)
            if (c == lineTabs[(size_t) n].get())
                return n;
    return -1;
}

void ArpPanel::setExternalDropTarget(int slot, int lineTab)
{
    for (int i = 0; i < (int) slotCards.size(); ++i)
        if (slotCards[(size_t) i] != nullptr)
            slotCards[(size_t) i]->setDropTarget(i == slot);
    for (int n = 0; n < (int) lineTabs.size(); ++n)
        if (lineTabs[(size_t) n] != nullptr)
            lineTabs[(size_t) n]->setDropTarget(n == lineTab);
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
void ArpPanel::setMacroView(bool on)
{
    if (on == macroView)
        return;
    macroView = on;
    processor.layout.arpMacro = on;
    for (auto& row : macroRows)
        if (row != nullptr)
            row->setVisible(on);
    setAllVisible({ &bpmKnob, &bpmPrev, &bpmNext, &bpmLabel, &quantizeLabel, &quantizeBox }, on);
    refreshShape();   // hides or restores the band, the lane tabs and the step editor
    refreshMacro();
    for (auto& tab : lineTabs)
        if (tab != nullptr)
            tab->repaint();
    if (macroTab != nullptr)
        macroTab->repaint();
    if (onPreferredHeightChanged)
        onPreferredHeightChanged();  // the panel is a different height in this view
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

void ArpPanel::setEditLine(int line)
{
    line = juce::jlimit(0, KeysProcessor::numArpLines - 1, line);
    // A line tab always leaves the macro view, even when it names the line already current:
    // clicking "B" while All is up plainly means "show me B".
    const bool leavingMacro = macroView;
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
    refreshRateMode();  // the dial's own two, and which unit is live on the new line
    refreshShape();     // Shape and the step editor, which are not attachments
    refreshRetrig();
    refreshLaneReadouts();
    refreshPatternButtons();
    for (auto& tab : lineTabs)
        if (tab != nullptr)
            tab->repaint();
    if (onEditLineChanged)
        onEditLineChanged();
    resized();  // Shape may have changed the panel's height with the line
    repaint();
}

void ArpPanel::buildLaneRow(LaneRow& row, ArpEngine::Lane lane, const juce::String& name, int loVal, int hiVal)
{
    row.tab.setButtonText(name);
    row.tab.onClick = [this, lane] { selectLane((int) lane); };
    addAndMakeVisible(row.tab);

    row.grid = std::make_unique<LaneGrid>(processor, *this, lane, loVal, hiVal);
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

void ArpPanel::refreshLaneReadouts()
{
    const auto li = (size_t) selectedLane;
    const int len = processor.arpLine(editedLine).lanes.length[li].load(std::memory_order_relaxed);
    stepsReadout.setText(juce::String(len), juce::dontSendNotification);
    const int div = juce::jlimit(0, 2, processor.arpLine(editedLine).lanes.clockDiv[li].load(std::memory_order_relaxed));
    speedButton.setButtonText(clockDivNames[div]);
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
        laneRows[(size_t) i].tab.setVisible(pattern);
        if (laneRows[(size_t) i].grid != nullptr)
            laneRows[(size_t) i].grid->setVisible(pattern && i == selectedLane);
    }
    muteRowLabel.setVisible(pattern);
    if (muteRow != nullptr)
        muteRow->setVisible(pattern);

    // Everything that lives in the band, hidden wholesale while the macro rows have its space.
    setAllVisible({ &shapeBox, &distanceBox, &retrigBox, &rateLabel, &shapeLabel, &distanceLabel,
                    &retrigLabel, &rateKnob, &rateModeButton, &shapePrev, &shapeNext, &ratePrev,
                    &rateNext, &dotButton, &tripButton, &anchorButton, &octavesSlider,
                    &swingSlider, &gateSlider, &chanceSlider, &octavesLabel, &swingLabel,
                    &gateLabel, &chanceLabel, &latchButton, &offsetSlider, &rampSlider,
                    &rampTimeSlider, &humanSlider, &offsetLabel, &rampLabel, &rampTimeLabel,
                    &humanLabel }, ! macroView);
    // The STEPS group is the only part of the band that belongs to the step editor, so it
    // is the only part that goes with it.
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &stepsLabel, &speedLabel, &stepsReadout, &stepsMinus, &stepsPlus, &speedButton, &linkButton })
        c->setVisible(pattern);
    groups[2].visible = pattern;

    // The slot row stays on both shapes. Launching a chord through "Up" is as much a thing
    // you do as launching one through an edited pattern, and hiding the row was what made
    // the old A-H buttons feel like an appendix to the step editor rather than the way you
    // drive the arp. Randomize is the exception: there is nothing to randomize but lanes.
    randomizeButton.setVisible(pattern);

    // The card changes height with the mode, so relayout and repaint - but only on an
    // actual change, since refreshShape() runs on the 10 Hz timer.
    if (lastPatternMode != (int) pattern)
    {
        lastPatternMode = (int) pattern;
        // Inline, the card does not size itself: the editor gives it preferredHeight(),
        // so the editor has to re-lay-out first or resized() would carve up stale bounds.
        if (onPreferredHeightChanged)
            onPreferredHeightChanged();
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

// The mode is a change of *unit*, so it changes what the dial is attached to, what its
// readout says, what a stepper click means, and whether Dot and Trip mean anything at all.
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

    // Dot and Trip subdivide a *beat*, and in Hz there is no beat: the engine ignores them
    // there (see ArpEngine::stepLengthBeats), so they grey out rather than sitting lit and
    // doing nothing.
    dotButton.setEnabled(! free);
    tripButton.setEnabled(! free);
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
}

// ---------------------------------------------------------------------------
// SlotCard

ArpPanel::SlotCard::SlotCard(KeysProcessor& p, const ArpPanel& o, int i)
    : juce::Button("Arp slot " + juce::String(i + 1)), processor(p), owner(o), index(i)
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

ArpPanel::MacroRow::MacroRow(KeysProcessor& p, int n) : processor(p), line(n)
{
    okstudio::ui::makeMouseOnly(*this);
    const auto letter = juce::String::charToString((juce::juce_wchar) ('A' + n));
    const auto id = [n](KeysProcessor::ArpParam w) { return KeysProcessor::arpParamId(n, w); };

    onButton.setButtonText(letter);
    onButton.setTitle("Macro line " + letter); // the bar chips already answer to "Arp line A"
    onButton.setTooltip("Arpeggiator line " + letter + " on or off.");
    addAndMakeVisible(onButton);
    onAtt = std::make_unique<ButtonAtt>(processor.apvts, id(KeysProcessor::apOn), onButton);

    // Rate as a readout between two steppers, not as a dial. A dial is a drag target, and this
    // row exists to be *read* three at a time and nudged with single clicks; the deep control
    // with its detented dial is still one tab away.
    rateReadout.setJustificationType(juce::Justification::centred);
    rateReadout.setFont(skin::uiSemi(14.0f));
    addAndMakeVisible(rateReadout);
    ratePrev.onClick = [this] { stepRate(-1); };
    rateNext.onClick = [this] { stepRate(1); };
    for (auto* b : { &ratePrev, &rateNext })
    {
        b->setTooltip("Step this line's rate. Polyrhythm lives here: put one line on 1/8 and "
                      "another on a 1/8 triplet.");
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
                           "Chord" }, 1);
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

    // Gate, Chance and Swing: the three that decide whether two lines interlock or fight.
    // The skin's machined rotary, exactly as the band above draws the same three - a row of
    // linear sliders here read as a different panel bolted on (Owen, 2026-08-01: "I was
    // thinking more knobs rather than slider"). The column heading is drawn once, on the top
    // row only, but every row reserves the same strip so the three knobs stay aligned.
    struct SliderSpec { juce::Slider& s; juce::Label& l; KeysProcessor::ArpParam p; const char* name; const char* tip; };
    const SliderSpec specs[] = {
        { gateSlider, gateLabel, KeysProcessor::apGate, "GATE",
          "How much of each step this line's notes fill. Short gates let another line through." },
        { chanceSlider, chanceLabel, KeysProcessor::apChance, "CHANCE",
          "How often a step fires at all. Thin one line out and the other two show through." },
        { swingSlider, swingLabel, KeysProcessor::apSwing, "SWING",
          "Shifts this line's offbeats late (positive) or early (negative). The quickest way to "
          "stop two lines landing on top of each other." },
    };
    for (const auto& spec : specs)
    {
        spec.s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        // Read-only text box, like every other knob in this panel: the value belongs under the
        // knob, and an editable box is a keyboard target on a surface that has none.
        spec.s.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 46, 14);
        spec.s.setTooltip(spec.tip);
        addAndMakeVisible(spec.s);
        spec.l.setText(spec.name, juce::dontSendNotification);
        spec.l.setJustificationType(juce::Justification::centred);
        spec.l.setFont(skin::micro(9.0f));
        spec.l.setColour(juce::Label::textColourId, skin::textFaint);
        spec.l.setVisible(line == 0); // one heading for the column, not one per row
        addChildComponent(spec.l);
    }
    gateAtt = std::make_unique<SliderAtt>(processor.apvts, id(KeysProcessor::apGate), gateSlider);
    chanceAtt = std::make_unique<SliderAtt>(processor.apvts, id(KeysProcessor::apChance), chanceSlider);
    swingAtt = std::make_unique<SliderAtt>(processor.apvts, id(KeysProcessor::apSwing), swingSlider);

    chordLabel.setJustificationType(juce::Justification::centred);
    chordLabel.setFont(skin::uiSemi(13.0f));
    addAndMakeVisible(chordLabel);

    chainButton.setTitle("Macro chain " + letter);
    chainButton.setTooltip("Play this line's slots that hold a chord, one after another. Three "
                           "chains at three rates is the whole point of the view.");
    chainButton.onClick = [this]
    {
        if (processor.chainRunning(line))
            processor.stopChain(line);
        else
            processor.startChain(line);
        refresh();
    };
    addAndMakeVisible(chainButton);

    refresh();
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
        refresh();
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
    refresh();
}

void ArpPanel::MacroRow::refresh()
{
    auto& apvts = processor.apvts;
    const bool free = apvts.getRawParameterValue(
                          KeysProcessor::arpParamId(line, KeysProcessor::apRateFree))->load() > 0.5f;
    rateModeButton.setButtonText(free ? "Hz" : "Sync");
    if (free)
    {
        const double hz = (double) apvts.getRawParameterValue(
                              KeysProcessor::arpParamId(line, KeysProcessor::apRateHz))->load();
        rateReadout.setText(ArpEngine::rateHzText(hz) + " Hz", juce::dontSendNotification);
    }
    else if (auto* rate = dynamic_cast<juce::AudioParameterChoice*>(
                 apvts.getParameter(KeysProcessor::arpParamId(line, KeysProcessor::apRate))))
    {
        rateReadout.setText(rate->getCurrentChoiceName(), juce::dontSendNotification);
    }

    const bool pattern = apvts.getRawParameterValue(
                             KeysProcessor::arpParamId(line, KeysProcessor::apPattern))->load() > 0.5f;
    const int dir = (int) apvts.getRawParameterValue(
                        KeysProcessor::arpParamId(line, KeysProcessor::apDirection))->load();
    const int wanted = pattern ? ArpEngine::numDirections
                               : juce::jlimit(0, ArpEngine::numDirections - 1, dir);
    if (shapeBox.getSelectedItemIndex() != wanted)
        shapeBox.setSelectedItemIndex(wanted, juce::dontSendNotification);

    // What this line is holding, and whether something is on its way. A launch waiting on a
    // quantize boundary says so, because otherwise the click looks like it did nothing.
    const auto& held = processor.arpHeldName(line);
    if (processor.arpLaunchPending(line))
        chordLabel.setText("...", juce::dontSendNotification);
    else
        chordLabel.setText(held.isNotEmpty() ? held : juce::String("--"), juce::dontSendNotification);
    chordLabel.setColour(juce::Label::textColourId,
                         held.isNotEmpty() ? skin::text : skin::textFaint);

    chainButton.setToggleState(processor.chainRunning(line), juce::dontSendNotification);
}

void ArpPanel::MacroRow::paint(juce::Graphics& g)
{
    // A hairline under every row but the last, so three rows read as three lines rather than
    // as one block of controls.
    g.setColour(skin::text.withAlpha(0.06f));
    g.fillRect(0.0f, (float) getHeight() - 1.0f, (float) getWidth(), 1.0f);
}

void ArpPanel::MacroRow::resized()
{
    auto full = getLocalBounds().withTrimmedBottom(2);
    // Every row gives up the same strip to the column headings, whether or not it draws them,
    // so the three knob columns line up down the view.
    auto heads = full.removeFromTop(11);
    auto r = full;
    const auto take = [&r](int w) { auto c = r.removeFromLeft(w); r.removeFromLeft(6); return c; };
    // The single-height controls sit centred against the knobs beside them.
    const auto centred = [](juce::Rectangle<int> c) { return c.withSizeKeepingCentre(c.getWidth(), 26); };

    onButton.setBounds(centred(take(44)));
    ratePrev.setBounds(centred(take(30)));
    rateReadout.setBounds(centred(take(58)));
    rateNext.setBounds(centred(take(30)));
    rateModeButton.setBounds(centred(take(46)));
    shapePrev.setBounds(centred(take(28)));
    shapeBox.setBounds(centred(take(juce::jlimit(90, 128, r.getWidth() / 5))));
    shapeNext.setBounds(centred(take(28)));
    heads.removeFromLeft(full.getWidth() - r.getWidth()); // the headings start where the knobs do

    // Chain and the chord come off the right, so the three knobs share whatever is left and
    // the row degrades by squeezing them rather than by pushing controls off the end.
    chainButton.setBounds(centred(r.removeFromRight(64)));
    heads.removeFromRight(64 + 6);
    r.removeFromRight(6);
    chordLabel.setBounds(centred(r.removeFromRight(72)));
    heads.removeFromRight(72 + 6);
    r.removeFromRight(6);

    const int each = juce::jmax(56, (r.getWidth() - 12) / 3);
    juce::Slider* knobs[] = { &gateSlider, &chanceSlider, &swingSlider };
    juce::Label* labels[] = { &gateLabel, &chanceLabel, &swingLabel };
    for (int i = 0; i < 3; ++i)
    {
        const int w = i == 2 ? r.getWidth() : each;
        auto c = r.removeFromLeft(w);
        r.removeFromLeft(6);
        labels[i]->setBounds(heads.removeFromLeft(w));
        heads.removeFromLeft(6);
        knobs[i]->setBounds(c);
    }
}

// ---------------------------------------------------------------------------
// LineTab

ArpPanel::LineTab::LineTab(KeysProcessor& p, const ArpPanel& o, int n)
    : juce::Button(n < 0 ? juce::String("Arp macro view")
                         : "Arp line " + juce::String::charToString((juce::juce_wchar) ('A' + n))),
      processor(p), owner(o), line(n)
{
    okstudio::ui::makeMouseOnly(*this);
    // Named for the capture script, and distinct from the A/B/C chips on the section bar:
    // those switch a line on, this one selects which line the panel is editing.
    setTitle(n < 0 ? juce::String("Arp all tab")
                   : "Arp line " + juce::String::charToString((juce::juce_wchar) ('A' + n)) + " tab");
}

void ArpPanel::LineTab::setDropTarget(bool b)
{
    if (dropTarget == b)
        return;
    dropTarget = b;
    repaint();
}

void ArpPanel::LineTab::paintButton(juce::Graphics& g, bool over, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced(1.0f);
    const auto accent = skin::accentOf(*this).base;
    const bool macro = line < 0;
    // The macro tab is "selected" when its view is up, and "on" when any line is running -
    // which is the honest reading of a tab that stands for all three at once.
    const bool selected = macro ? owner.isMacroView() : (! owner.isMacroView() && owner.editLine() == line);
    const bool on = macro ? processor.cardsFeedArp() : processor.arpLineOn(line);

    skin::raisedFill(g, b, skin::radius,
                     on ? accent.withAlpha(0.30f) : skin::control.withAlpha(down ? 0.7f : 1.0f),
                     on ? accent.withAlpha(0.18f) : skin::controlBot);
    if (over || dropTarget)
    {
        g.setColour(juce::Colours::white.withAlpha(dropTarget ? 0.14f : 0.06f));
        g.fillRoundedRectangle(b, skin::radius);
    }
    // Selected = the panel is showing this line. On = it is running. Two different things, and
    // both have to be readable at once: you edit a line that is off all the time.
    if (selected || dropTarget)
    {
        g.setColour(dropTarget ? accent : accent.withAlpha(0.75f));
        g.drawRoundedRectangle(b.reduced(0.75f), skin::radius, dropTarget ? 2.0f : 1.4f);
    }

    auto area = b.reduced(5.0f, 4.0f);
    g.setColour(selected ? skin::text : skin::textFaint);
    g.setFont(skin::uiSemi(15.0f));
    g.drawText(macro ? juce::String("All")
                     : juce::String::charToString((juce::juce_wchar) ('A' + line)),
               area.removeFromTop(17.0f), juce::Justification::centred, false);

    // What this line is holding, so three tabs read as three arpeggiators rather than three
    // letters. Faint and small: it is a status line, not the card's own name.
    g.setColour(on ? accent.withAlpha(0.85f) : skin::textFaint);
    g.setFont(skin::micro(9.0f));
    const auto sub = macro ? juce::String("3 lines")
                           : (processor.arpHeldName(line).isNotEmpty() ? processor.arpHeldName(line)
                                                                       : juce::String(on ? "on" : "off"));
    g.drawText(sub, area, juce::Justification::centredTop, false);
}

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
                                        "Rnd", "Rnd-O", "Rnd-1", "Chord", "Pattern" };
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
    tripAtt = std::make_unique<ButtonAtt>(processor.apvts, paramId(KeysProcessor::apTrip), tripButton);
    anchorAtt = std::make_unique<ButtonAtt>(processor.apvts, paramId(KeysProcessor::apAnchor), anchorButton);
    octavesAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apOctaves), octavesSlider);
    distanceAtt = std::make_unique<ComboAtt>(processor.apvts, paramId(KeysProcessor::apDistance), distanceBox);
    offsetAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apOffset), offsetSlider);
    rampAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apVelRamp), rampSlider);
    rampTimeAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apRampBeats), rampTimeSlider);
    humanAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apHumanize), humanSlider);
    swingAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apSwing), swingSlider);
    gateAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apGate), gateSlider);
    chanceAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apChance), chanceSlider);
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

    for (auto* b : { &dotButton, &tripButton, &anchorButton })
        addAndMakeVisible(*b);
    // Anchor's tooltip is written by refreshRateMode(), beside its enablement: it says
    // something different in Hz, where there is no bar grid to anchor to.

    styleLabel(shapeLabel, "Shape");
    addAndMakeVisible(shapeLabel);
    // Twelve shapes then "Pattern", which stays last however many shapes are added: it is
    // the one entry that changes what is on screen, and the < > steppers clamp at the ends,
    // so a click too many on the shape before it cannot throw the step editor open.
    shapeBox.addItemList({ "Up", "Down", "Up-Down", "Down-Up",
                           "Up & Down", "Down & Up", "As Played", "Reversed",
                           "Random", "Random Other", "Random Once", "Chord" }, 1);
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
    bar(humanSlider, humanLabel, "Human", 0.0, 100.0, "%",
        "Nudges each hit a little late and a little quieter, by a different amount every time. "
        "The arp is dead on the grid at 0.");

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

    knob(chanceSlider, chanceLabel, "Chance", 0.0, 100.0, 1.0,
         "How likely each step is to fire. Multiplies the Probability lane, so it thins a "
         "run out on any shape.");

    addAndMakeVisible(latchButton);
    latchButton.setTooltip("Ignore note-offs until a new chord arrives.");
    // Accessible name only; the button still reads "Latch". The keybed's Latch is on the
    // Keyboard bar in the same window, and UI Automation takes the first name that matches.
    latchButton.setTitle("Arp latch");

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
    buildLaneRow(laneRows[(size_t) ArpEngine::laneNote], ArpEngine::laneNote, "Note", -1, 8);
    buildLaneRow(laneRows[(size_t) ArpEngine::laneOctave], ArpEngine::laneOctave, "Octave", -3, 3);
    buildLaneRow(laneRows[(size_t) ArpEngine::laneVelocity], ArpEngine::laneVelocity, "Velocity", 10, 200);
    buildLaneRow(laneRows[(size_t) ArpEngine::laneGate], ArpEngine::laneGate, "Gate", 5, 200);
    buildLaneRow(laneRows[(size_t) ArpEngine::laneRatchet], ArpEngine::laneRatchet, "Ratchet", 1, 4);
    buildLaneRow(laneRows[(size_t) ArpEngine::laneProbability], ArpEngine::laneProbability, "Prob", 0, 100);
    // The 2026-07-30 four. "Prob" above shortened with them: ten tabs share the width six
    // used to, and "Probability" is the only old label that will not fit at that size.
    buildLaneRow(laneRows[(size_t) ArpEngine::laneTranspose], ArpEngine::laneTranspose, "Transpose", -7, 7);
    buildLaneRow(laneRows[(size_t) ArpEngine::laneLate], ArpEngine::laneLate, "Late", 0, 90);
    buildLaneRow(laneRows[(size_t) ArpEngine::laneHarmony], ArpEngine::laneHarmony, "Harmony", 0, 7);
    buildLaneRow(laneRows[(size_t) ArpEngine::laneChord], ArpEngine::laneChord, "Chord", 0, 12);

    styleLabel(muteRowLabel, "Mute");
    addAndMakeVisible(muteRowLabel);
    muteRow = std::make_unique<MuteRow>(processor, *this);
    addAndMakeVisible(*muteRow);

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
        auto card = std::make_unique<SlotCard>(processor, *this, i);
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

    randomizeButton.onClick = [this] { processor.randomizeActiveArpPattern(editLine()); };
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

    // The three line tabs, at the left of the slot row. Clicking one moves the whole panel to
    // that line - band, step lanes, the twelve slots, Bars and Chain - which is what keeps one
    // row of controls in front of three arpeggiators without the panel growing at all.
    // Three macro rows, hidden until the fourth tab asks for them. Built once and kept, so
    // their attachments never churn: each one is bound to its own line for good.
    for (int n = 0; n < KeysProcessor::numArpLines; ++n)
    {
        auto row = std::make_unique<MacroRow>(processor, n);
        addChildComponent(*row);
        macroRows[(size_t) n] = std::move(row);
    }

    // The two things all three lines share, and the reason the macro view is more than three
    // rows side by side. BPM is the tempo they run at when there is no transport to follow -
    // always in the standalone, and whenever the host is stopped; a rolling host still wins.
    bpmLabel.setText("BPM", juce::dontSendNotification);
    bpmLabel.setFont(skin::micro(9.0f));
    bpmLabel.setColour(juce::Label::textColourId, skin::textFaint);
    addChildComponent(bpmLabel);
    bpmKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    bpmKnob.setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 18);
    bpmKnob.setTitle("Arp BPM");
    bpmKnob.setTooltip("The tempo all three lines run at when there is no transport to follow: "
                       "always in the standalone, and whenever the host is stopped. A host that "
                       "is playing always wins, and a line whose rate is in Hz follows neither.");
    addChildComponent(bpmKnob);
    bpmAtt = std::make_unique<SliderAtt>(processor.apvts, "bpm", bpmKnob);
    // Steppers beside it for the same reason the rate dial has them: a knob is a drag target,
    // and these are the click-only path to every value it can hold.
    bpmPrev.onClick = [this] { nudgeBpm(-1); };
    bpmNext.onClick = [this] { nudgeBpm(1); };
    for (auto* b : { &bpmPrev, &bpmNext })
    {
        b->setTooltip("Nudge the tempo by one BPM.");
        addChildComponent(*b);
    }

    quantizeLabel.setText("QUANTIZE", juce::dontSendNotification);
    quantizeLabel.setFont(skin::micro(9.0f));
    quantizeLabel.setColour(juce::Label::textColourId, skin::textFaint);
    addChildComponent(quantizeLabel);
    // The list has to match the parameter's choices exactly, and in order: an attachment binds
    // to items that already exist rather than creating them, so an empty box stays empty.
    quantizeBox.addItemList({ "Off", "1/16", "1/8", "1/4", "1/2", "1 Bar", "2 Bars" }, 1);
    quantizeBox.setTitle("Arp launch quantize");
    quantizeBox.setTooltip("Hold a chord card, a slot launch or a drag onto a line until the "
                           "next boundary, so it can only ever land on the grid - Ableton's "
                           "Quantization, for the arp. Off fires the instant you click. It never "
                           "delays the keys you play.");
    addChildComponent(quantizeBox);
    quantizeAtt = std::make_unique<ComboAtt>(processor.apvts, "arpQuantize", quantizeBox);

    for (int n = 0; n < KeysProcessor::numArpLines; ++n)
    {
        auto tab = std::make_unique<LineTab>(processor, *this, n);
        tab->onClick = [this, n] { setEditLine(n); };
        const auto letter = juce::String::charToString((juce::juce_wchar) ('A' + n));
        tab->setTooltip("Arpeggiator line " + letter + ". Click to edit it here, and to send it "
                        "the next chord card you click. Each line has its own rate, shape, "
                        "pattern and twelve slots, so three of them make a polyrhythm.");
        addAndMakeVisible(*tab);
        lineTabs[(size_t) n] = std::move(tab);
    }

    // The fourth: all three at once. Owen's shape for this - "a fourth option for a simplified
    // version that shows a little bit of all of them" - which is why it is a tab in the row
    // that already selects lines rather than a window or a fifth section.
    macroTab = std::make_unique<LineTab>(processor, *this, -1);
    macroTab->onClick = [this] { setMacroView(true); };
    macroTab->setTooltip("All three lines at once: rate, shape, gate, chance and swing for each, "
                         "with the tempo and Launch Quantize they share. The place to build a "
                         "polyrhythm; the line tabs beside it are where you go deep on one.");
    addAndMakeVisible(*macroTab);

    buildAttachments();
}

void ArpPanel::nudgeBpm(int delta)
{
    if (auto* p = dynamic_cast<juce::AudioParameterInt*>(processor.apvts.getParameter("bpm")))
    {
        p->beginChangeGesture();
        *p = juce::jlimit(p->getRange().getStart(), p->getRange().getEnd(), p->get() + delta);
        p->endChangeGesture();
    }
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

void ArpPanel::timerCallback()
{
    refreshMacro(); // rate, shape and the held chord, none of which an attachment drives

    refreshShape(); // the host can automate arpPattern/arpDirection out from under us
    refreshRateMode(); // ... and arpRateFree, which decides what the dial is even measuring
    refreshRetrig();
    refreshLaneReadouts();
    refreshPatternButtons();
    if (! patternMode())
        return; // nothing of the step editor is on screen to repaint

    auto& grid = laneRows[(size_t) selectedLane].grid;
    if (grid != nullptr)
        grid->repaint();
    if (muteRow != nullptr)
        muteRow->repaint();
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
    // The macro view: three line rows plus one shared row (tempo and quantize). It replaces
    // the two band rows rather than joining them, which is what keeps the panel exactly as
    // tall as it is on a shape - the whole point of a fourth tab rather than a fourth band.
    // 11 for the column headings, 40 for the knob, 15 for its readout - the row is as tall as
    // a knob needs and no taller, which is what keeps three of them under the Pattern view.
    constexpr int arpMacroRow = 66;
    constexpr int arpMacroShared = 56; // the shared row: a knob needs more height than a slider
    constexpr int arpMacroH = arpBandTop + arpMacroRow * 3 + 10 + arpMacroShared + 4;
    constexpr int arpShapeH = 12 + (arpBandH + 8) + (arpBand2H + 12) + (arpSlotsH + 8) + 34 + 12;
    constexpr int arpMacroTotalH = 12 + (arpMacroH + 12) + (arpSlotsH + 8) + 34 + 12;
    constexpr int arpPatternH = arpShapeH + (34 + 6) + (140 + 6) + (14 + 2) + (32 + 10);

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
    constexpr int groupWeights[3] = { 40, 42, 18 };
    constexpr int group2Weights[2] = { 44, 56 };
} // namespace

int ArpPanel::preferredHeight() const
{
    return contentHeight() + 16; // + the 8 px margin at both ends
}

// One answer for the three views: the macro rows, a plain shape, or the step editor.
int ArpPanel::contentHeight() const
{
    if (macroView)
        return arpMacroTotalH;
    return patternMode() ? arpPatternH : arpShapeH;
}

juce::Rectangle<int> ArpPanel::cardBounds() const
{
    const auto full = getLocalBounds().reduced(8);
    if (inlineMode)
        return full; // the editor already gave us exactly preferredHeight()
    return full.withHeight(juce::jmin(full.getHeight(), contentHeight()));
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
    auto area = cardBounds().reduced(12);

    // No title row. The section bar above says ARP and carries the On toggle and Detach, so a
    // second "Arpeggiator" caption with its own On and Close was three duplicated controls and
    // a wasted 38 px of a panel that is already the tallest thing in the editor.

    // --- The macro view takes the band's place, never sits beside it ----------------
    if (macroView)
    {
        auto block = area.removeFromTop(arpMacroH);
        area.removeFromTop(12);
        groups[0].bounds = block;
        groups[0].caption = "Lines";
        for (int i = 1; i < (int) groups.size(); ++i)
            groups[(size_t) i].visible = false;

        auto inner = block.reduced(10, 0).withTrimmedTop(arpBandTop);
        for (auto& row : macroRows)
        {
            if (row != nullptr)
                row->setBounds(inner.removeFromTop(arpMacroRow));
        }
        inner.removeFromTop(10);

        // The shared row, under the three: one tempo and one quantize, laid out from the left
        // so they read as belonging to all of it rather than to the last line above them.
        auto shared = inner.removeFromTop(arpMacroShared);
        auto bpmCell = shared.removeFromLeft(196);
        shared.removeFromLeft(16);
        bpmLabel.setBounds(bpmCell.removeFromTop(11));
        bpmPrev.setBounds(bpmCell.removeFromLeft(30).withSizeKeepingCentre(30, 26));
        bpmNext.setBounds(bpmCell.removeFromRight(30).withSizeKeepingCentre(30, 26));
        bpmKnob.setBounds(bpmCell.reduced(6, 0));

        auto qCell = shared.removeFromLeft(juce::jlimit(120, 200, shared.getWidth() / 3));
        quantizeLabel.setBounds(qCell.removeFromTop(11));
        quantizeBox.setBounds(qCell.withSizeKeepingCentre(qCell.getWidth(),
                                                          juce::jmin(qCell.getHeight(), 28)));
    }

    // --- The control band: three captioned groups sharing the width ---------------
    auto band = macroView ? juce::Rectangle<int>() : area.removeFromTop(arpBandH);
    if (! macroView)
        area.removeFromTop(8);
    auto band2 = macroView ? juce::Rectangle<int>() : area.removeFromTop(arpBand2H);
    if (! macroView)
        area.removeFromTop(12);
    if (! macroView)
    {
        const int gaps = 2 * 10;
        const int usable = band.getWidth() - gaps;
        const int total = groupWeights[0] + groupWeights[1] + groupWeights[2];
        for (int i = 0; i < 3; ++i)
        {
            const int w = i == 2 ? band.getWidth() : usable * groupWeights[i] / total;
            groups[(size_t) i].bounds = band.removeFromLeft(w);
            if (i < 2)
                band.removeFromLeft(10);
        }
        const int usable2 = band2.getWidth() - 10;
        const int total2 = group2Weights[0] + group2Weights[1];
        groups[3].bounds = band2.removeFromLeft(usable2 * group2Weights[0] / total2);
        band2.removeFromLeft(10);
        groups[4].bounds = band2;
        groups[0].caption = "Pattern";
        groups[1].caption = "Playback";
        groups[2].caption = "Steps";
        groups[3].caption = "Spread";
        groups[4].caption = "Feel";
        for (int i = 0; i < (int) groups.size(); ++i)
            groups[(size_t) i].visible = true;
        // ...except STEPS, which belongs to the step editor and follows Shape. Restoring it
        // unconditionally drew an empty ruled box beside the band on every plain shape.
        groups[2].visible = patternMode();
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
        // Trip and Dot down to an ellipsis while Rate sat wider than its longest entry.
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
        // as one line with Trip and Dot.
        auto rateSteps = rowB.removeFromLeft(72).removeFromBottom(34);
        rowB.removeFromLeft(8);
        ratePrev.setBounds(rateSteps.removeFromLeft(34));
        rateNext.setBounds(rateSteps.removeFromRight(34));
        rateModeButton.setBounds(rowB.removeFromLeft(58).removeFromBottom(34));
        rowB.removeFromLeft(6);
        toggleCell(rowB, 56, tripButton);
        toggleCell(rowB, 52, dotButton);
    }

    // PLAYBACK: how the run behaves once it is going. Three knobs down the left, then the
    // two rows of discrete controls. Anchor sits here rather than with Rate: it is about
    // how the clock runs, not about what the pattern is.
    {
        auto inner = groupInner(groups[1].bounds);
        knobColumn(inner, 50, swingLabel, swingSlider);
        knobColumn(inner, 50, gateLabel, gateSlider);
        knobColumn(inner, 50, chanceLabel, chanceSlider);
        inner.removeFromLeft(8);

        // ~195 px left after the knobs, and every one of these is spent. A toggle needs
        // 20 px of tick box plus 9 px of gap before its text starts (see the skin's
        // drawToggleButton), which is why they look wider than their words.
        juce::Rectangle<int> rowA, rowB;
        splitRows(inner, rowA, rowB);
        // Retrigger is a combo where Repeats used to be a stepper, and it needs the whole
        // row: at 128 px beside Anchor's 83 the group ran ~24 px over and ellipsised the
        // *toggle*, which is the one control on the band with no room to lose any. Anchor
        // moves down beside Latch, where there is now a whole row spare.
        cell(rowA, juce::jmax(120, rowA.getWidth()), retrigLabel, retrigBox);
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

    // FEEL: the three that decide whether it sounds played. Sliders share what is left
    // equally, since none of them has a natural width and all three are dragged, not read.
    {
        auto inner = groupInner(groups[4].bounds).withHeight(arpBandRow);
        auto row = inner;
        const int each = juce::jmax(120, (row.getWidth() - 16) / 3);
        cell(row, each, rampLabel, rampSlider);
        cell(row, each, rampTimeLabel, rampTimeSlider);
        cell(row, juce::jmax(120, row.getWidth()), humanLabel, humanSlider);
    }

    // STEPS: the step editor's own length/speed pair, so it sits with the editor it drives
    // rather than floating under the grid where it used to.
    {
        juce::Rectangle<int> rowA, rowB;
        splitRows(groupInner(groups[2].bounds), rowA, rowB);
        auto stepsCell = rowA.removeFromLeft(juce::jlimit(120, 150, rowA.getWidth()));
        stepsLabel.setBounds(stepsCell.removeFromTop(14));
        stepsCell = stepsCell.withSizeKeepingCentre(stepsCell.getWidth(), juce::jmin(stepsCell.getHeight(), 28));
        stepsMinus.setBounds(stepsCell.removeFromLeft(34));
        stepsPlus.setBounds(stepsCell.removeFromRight(34));
        stepsReadout.setBounds(stepsCell);
        auto speedCell = rowB.removeFromLeft(64);
        rowB.removeFromLeft(8);
        speedLabel.setBounds(speedCell.removeFromTop(14));
        speedButton.setBounds(speedCell.withSizeKeepingCentre(speedCell.getWidth(),
                                                              juce::jmin(speedCell.getHeight(), 28)));
        linkButton.setBounds(rowB.withTrimmedTop(14));
    }

    // --- The slot row and its buttons, at the bottom in both shapes ----------------
    auto actionRow = area.removeFromBottom(34);
    area.removeFromBottom(8);
    auto slotRow = area.removeFromBottom(arpSlotsH);
    area.removeFromBottom(12);
    {
        // The three line tabs first, then All, at the left end of the row. They take a cell
        // each out of the same width the slots share, which is what makes them cost no height
        // at all: the row is already arpSlotsH tall and a tab is the mouse-only 34 centred in
        // it. Four tabs and twelve slots is sixteen cells.
        const int n = (int) slotCards.size() + (int) lineTabs.size() + 1;
        const int gap = 4;
        const int w = juce::jmax(46, (slotRow.getWidth() - gap * (n - 1)) / n);
        for (auto& t : lineTabs)
        {
            if (t != nullptr)
                t->setBounds(slotRow.removeFromLeft(w).withSizeKeepingCentre(w, 34));
            slotRow.removeFromLeft(gap);
        }
        if (macroTab != nullptr)
            macroTab->setBounds(slotRow.removeFromLeft(w).withSizeKeepingCentre(w, 34));
        slotRow.removeFromLeft(gap);
        slotRow.removeFromLeft(6); // a breath between the tabs and the slots they select
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

        // Chain and its Bars stepper sit at the far end, away from the four that act on one
        // slot: these two are about the row as a whole.
        auto barsCell = actionRow.removeFromRight(126);
        barsPlus.setBounds(barsCell.removeFromRight(34));
        barsMinus.setBounds(barsCell.removeFromLeft(34));
        barsReadout.setBounds(barsCell); // says "2 bars", so it needs no caption beside it
        actionRow.removeFromRight(8);
        chainButton.setBounds(actionRow.removeFromRight(96));
    }

    // Everything left exists only in Pattern shape. Laying it out regardless is harmless
    // (it is all invisible) and keeps this function free of a second branch.

    // Lane tabs, then the one lane they select, then the mute row beneath it. Six
    // stacked lanes needed ~750 px and the panel gets ~600, so the Probability lane and
    // the whole pattern row used to be cut off the bottom of the window entirely.
    auto tabsRow = area.removeFromTop(34);
    area.removeFromTop(6);
    const int tabW = juce::jmax(70, (tabsRow.getWidth() - (ArpEngine::numLanes - 1) * 4) / ArpEngine::numLanes);
    for (auto& lr : laneRows)
    {
        lr.tab.setBounds(tabsRow.removeFromLeft(tabW));
        tabsRow.removeFromLeft(4);
    }

    auto gridArea = area.removeFromTop(140);
    area.removeFromTop(6);
    for (auto& lr : laneRows)
        if (lr.grid != nullptr)
            lr.grid->setBounds(gridArea); // all share the slot; only one is visible

    // The mute strip divides its own width into the same step count the grid does, so it
    // only reads as "the steps above, muted" while the two share an origin and a width.
    // The caption therefore goes above the strip, not beside it: a left gutter on one and
    // not the other silently slid every mute cell off the step it belongs to.
    muteRowLabel.setBounds(area.removeFromTop(14));
    area.removeFromTop(2);
    muteRow->setBounds(area.removeFromTop(32)); // same x and width as gridArea, both off `area`
}
} // namespace keys
