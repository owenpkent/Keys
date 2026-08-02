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
    // rate's Dot / Trip / Anchor at the full 34 px hit height.
    constexpr int arpMacroCap = 18;
    constexpr int arpMacroMods = 34;
    constexpr int arpMacroHeads = 11;

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
    return juce::jlimit(0, KeysProcessor::uiArpLines - 1, editedLine);
}

// A chord card dropped on one of the twelve slots binds that chord there. The slot keeps its
// pattern; what it gains is the chord a launch will hold into the line.
void ArpPanel::takeChordOnSlot(int slot, const chorddrag::Payload& p)
{
    processor.setArpSlotChord(slot, p.chord.notes, p.chord.name, editLine());
    repaint();
}

// A chord card dropped on a line - its tab, or its whole row in the macro view - goes straight
// into that line, and the line becomes the current one: you aimed at it, so the next card click
// should follow the same aim. The view does not move with it (`leaveMacroView` false); in the
// macro view you dropped onto the line itself, and being thrown into that line's deep controls
// is not what the gesture asked for.
void ArpPanel::takeChordOnLine(int line, const chorddrag::Payload& p)
{
    processor.holdArpChordFromPad(p.index, line);
    setEditLine(line, /*leaveMacroView*/ false);
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
    // An armed Copy or Clear waits for a slot click, and the macro view has no slots to
    // click: carrying the armed state across would leave Cancel hidden with nothing to
    // cancel and the next slot click, tabs later, doing something the user armed minutes ago.
    if (on)
        setArmed(armNone);
    for (auto& row : macroRows)
        if (row != nullptr)
            row->setVisible(on);
    refreshShape();   // hides or restores the band, the lane tabs and the step editor
    refreshMacro();
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
    refreshRateMode();  // the dial's own two, and which unit is live on the new line
    refreshShape();     // Shape and the step editor, which are not attachments
    refreshRetrig();
    refreshLaneReadouts();
    refreshPatternButtons();
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
                    &gateLabel, &chanceLabel, &latchButton, &keysBandButton, &offsetSlider,
                    &rampSlider, &rampTimeSlider, &humanSlider, &humanVelSlider, &offsetLabel,
                    &rampLabel, &rampTimeLabel, &humanLabel, &humanVelLabel }, ! macroView);
    // The STEPS group is the only part of the band that belongs to the step editor, so it
    // is the only part that goes with it.
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &stepsLabel, &speedLabel, &stepsReadout, &stepsMinus, &stepsPlus, &speedButton, &linkButton })
        c->setVisible(pattern);
    groups[2].visible = pattern;

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
             &barsMinus, &barsPlus, &barsReadout })
        c->setVisible(slotsOn);
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

// Chords from the pad strip only. A tray candidate is not offered a slot today - the tray's drag
// went to the pads and the reference box and nowhere else - and widening that is a feature, not
// something to let in sideways because the framework now makes it free.
bool ArpPanel::SlotCard::isInterestedInDragSource(const SourceDetails& details)
{
    auto* p = chorddrag::chordBeingDragged(details);
    return p != nullptr && p->from == chorddrag::Payload::From::padSlot;
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
        { KeysProcessor::apChance, "CHANCE",
          "How often a step fires at all. Thin one line out and the other two show through." },
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
        { KeysProcessor::apVelTrim, "VEL",
          "This line's level, as velocity: centre plays the notes as they came, right pushes "
          "them louder, left quieter, and full left is silence. The way to balance two lines "
          "without playing one of them softer." },
        // ...and HUMAN split into its halves the same day (Owen: "maybe we could split it up
        // into two knobs"), so timing and dynamics randomize independently.
        { KeysProcessor::apHumanize, "H.TIME",
          "Nudges each hit a little late, by a different amount every time. At 0 the line is "
          "dead on the grid." },
        { KeysProcessor::apHumanVel, "H.VEL",
          "Takes a little off each hit's velocity, by a different amount every time. At 0 "
          "every hit lands at full strength." },
    };
    static_assert(sizeof(macroKnobSpecs) / sizeof(macroKnobSpecs[0])
                      == (size_t) ArpPanel::MacroRow::numKnobs,
                  "every macro knob needs a heading and a parameter");
} // namespace

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

    onButton.setButtonText(letter);
    onButton.setTitle("Macro line " + letter); // the bar chips already answer to "Arp line A"
    onButton.setTooltip("Arpeggiator line " + letter + " on or off.");
    addAndMakeVisible(onButton);
    onAtt = std::make_unique<ButtonAtt>(processor.apvts, id(KeysProcessor::apOn), onButton);

    // No Latch, PLAY or Chain on the row since the second 2026-08-02 pass (Owen: "I think we
    // can remove the chain button, maybe the play and the [latch] button"): the rows keep
    // what you reach for while both lines run, and the set-and-forget switches live on the
    // line's own tab - Latch and Play on the band, Chain on the action row. PLAY's history
    // (it was KEYS, and collided with the bar's Light keys) moved to keysBandButton with it.

    // The rate's three modifiers, on the sub-row under it. Same three the band carries, same
    // parameters, and greyed by the same question - see refreshRateMode.
    dotButton.setTitle("Macro dot " + letter);
    dotButton.setTooltip("Dotted: each step lasts half again as long, so 1/8 becomes a dotted "
                         "1/8. Greyed with the rate in Hz, where there is no beat to dot.");
    tripButton.setTitle("Macro trip " + letter);
    tripButton.setTooltip("Triplets: three steps in the space of two, so 1/8 becomes an 1/8 "
                          "triplet. Greyed with the rate in Hz, where there is no beat to "
                          "divide. Run one line straight and the other in triplets and you have "
                          "the polyrhythm this view is for.");
    anchorButton.setTitle("Macro anchor " + letter);
    // Anchor's tooltip is written by refreshRateMode, beside its enablement: it says something
    // different in Hz, where there is no bar grid to anchor to. Same split as the band's.
    for (auto* b : { &dotButton, &tripButton, &anchorButton })
        addAndMakeVisible(*b);
    dotAtt = std::make_unique<ButtonAtt>(processor.apvts, id(KeysProcessor::apDot), dotButton);
    tripAtt = std::make_unique<ButtonAtt>(processor.apvts, id(KeysProcessor::apTrip), tripButton);
    anchorAtt = std::make_unique<ButtonAtt>(processor.apvts, id(KeysProcessor::apAnchor), anchorButton);

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

    // The eight settings a regular arpeggiator has, as the skin's machined rotary - the same
    // knob the band above draws the same parameters with (Owen, 2026-08-01: "what other knobs
    // can we have? should be like regular arp settings").
    for (int k = 0; k < numKnobs; ++k)
    {
        auto& knob = knobs[(size_t) k];
        knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        // Read-only text box, like every other knob here: the value belongs under the knob, and
        // an editable box is a keyboard target on a surface that has none.
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 52, 15);
        knob.setTooltip(macroKnobSpecs[(size_t) k].tip);
        knob.setTitle("Macro " + juce::String(macroKnobSpecs[(size_t) k].heading) + " " + letter);
        addAndMakeVisible(knob);
        if (k != kOctShift) // its heading is already made, above
            heading(knobLabels[(size_t) k], macroKnobSpecs[(size_t) k].heading);
        knobAtts[(size_t) k] = std::make_unique<SliderAtt>(
            processor.apvts, id(macroKnobSpecs[(size_t) k].param), knob);
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

    // Dot, Trip and Anchor all subdivide or align against a *beat*, and in Hz there is no beat:
    // the engine ignores all three there, so they grey out rather than sitting lit and doing
    // nothing. Same rule and the same words as the band's - see ArpPanel::refreshRateMode.
    // Outside the early-out below, which only guards the attachment swap: these have to be
    // right on the first call too, when lastRateFree is still -1 and a Hz session has just
    // been restored.
    dotButton.setEnabled(! free);
    tripButton.setEnabled(! free);
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

    auto& apvts = processor.apvts;
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
    auto* p = chorddrag::chordBeingDragged(details);
    return p != nullptr && p->from == chorddrag::Payload::From::padSlot;
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

    g.setColour(juce::Colours::white.withAlpha(0.03f));
    g.fillRoundedRectangle(r.withTrimmedTop(capY - r.getY()), skin::radius);

    g.setColour(juce::Colours::white.withAlpha(0.10f));
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
    g.setColour(skin::textDim);
    g.drawText(caption, getLocalBounds().withHeight(12).withY((int) capY - 6),
               juce::Justification::centred);
}

void ArpPanel::MacroRow::resized()
{
    // Three lines inside one card (2026-08-02, Owen: "having the arpeggiators parallel to
    // each other instead of one on top of the other"): what the line plays (On, rate,
    // shape), the eight knobs under their own heading strip, and the rate's modifiers with
    // the held chord. Stacked *inside* the card because two cards now share the panel's
    // width, and the old single line needed more width than half the panel has. The heights
    // here are arpMacroCap / arpMacroLine / arpMacroHeads / arpMacroMods; arpMacroCard is
    // their sum and has to agree with this function exactly.
    auto full = getLocalBounds().reduced(10, 0);
    full.removeFromTop(arpMacroCap); // the LINE A / LINE B caption rule, drawn by paint()
    // The single-height controls sit centred against the knobs beside them.
    const auto centred = [](juce::Rectangle<int> c) { return c.withSizeKeepingCentre(c.getWidth(), 26); };

    const auto heads1 = full.removeFromTop(arpMacroHeads);
    auto line1 = full.removeFromTop(arpMacroLine);
    {
        auto r = line1;
        const auto take = [&r](int w) { auto c = r.removeFromLeft(w); r.removeFromLeft(6); return c; };
        onButton.setBounds(centred(take(40)));
        ratePrev.setBounds(centred(take(26)));
        rateKnob.setBounds(take(58));
        rateNext.setBounds(centred(take(26)));
        rateModeButton.setBounds(centred(take(42)));
        // Shape's steppers are reserved before its combo takes the rest: they are targets
        // and it is a readout, the same reserve-the-fixed-thing-first ordering the old
        // single-line layout paid for twice (see the 2026-08-02 entries in CLAUDE.md).
        shapePrev.setBounds(centred(r.removeFromLeft(26)));
        r.removeFromLeft(6);
        shapeNext.setBounds(centred(r.removeFromRight(26)));
        r.removeFromRight(6);
        shapeBox.setBounds(centred(r));
    }
    // The RATE / SHAPE names span their whole group, steppers included, so the arrows can
    // only be read as belonging to the word above them. Placed from the controls, as ever.
    rateHeadLabel.setBounds(ratePrev.getX(), heads1.getY(),
                            rateModeButton.getRight() - ratePrev.getX(), heads1.getHeight());
    shapeHeadLabel.setBounds(shapePrev.getX(), heads1.getY(),
                             shapeNext.getRight() - shapePrev.getX(), heads1.getHeight());

    const auto headStrip = full.removeFromTop(arpMacroHeads);
    auto knobLine = full.removeFromTop(arpMacroLine);
    // 38 keeps the card solvable at the editor's minimum width, where a column is ~430 px
    // inside and eight knobs land at 48; they stop growing at 96 as before.
    const int each = juce::jlimit(38, 96, (knobLine.getWidth() - 6 * (numKnobs - 1)) / numKnobs);
    auto knobStrip = knobLine.removeFromLeft(each * numKnobs + 6 * (numKnobs - 1));
    for (int k = 0; k < numKnobs; ++k)
    {
        knobs[(size_t) k].setBounds(knobStrip.removeFromLeft(each));
        knobStrip.removeFromLeft(6);
    }
    // Headings are placed from the knob they name, not by walking a second copy of the
    // layout: one source of truth for where a column is, so they cannot drift apart.
    const auto headFor = [&headStrip](juce::Label& l, const juce::Component& c)
    { l.setBounds(c.getX(), headStrip.getY(), c.getWidth(), headStrip.getHeight()); };
    for (int k = 0; k < numKnobs; ++k)
        headFor(knobLabels[(size_t) k], knobs[(size_t) k]);

    // The rate's modifiers keep their full 34 px hit height - they are targets, and wide
    // enough for the word plus its tick, since a bare tick box beside "Dot" would be two
    // controls' worth of ambiguity in a card that already has eight unlabelled knobs. The
    // held chord sits at the card's bottom-right corner, where a dropped card lands.
    full.removeFromTop(2);
    auto subRow = full.removeFromTop(arpMacroMods);
    chordLabel.setBounds(centred(subRow.removeFromRight(64)));
    subRow.removeFromRight(6);
    const auto takeMod = [&subRow](int w)
    { auto c = subRow.removeFromLeft(w); subRow.removeFromLeft(8); return c; };
    dotButton.setBounds(takeMod(62));
    tripButton.setBounds(takeMod(66));
    anchorButton.setBounds(takeMod(84));
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
    humanVelAtt = std::make_unique<SliderAtt>(processor.apvts, paramId(KeysProcessor::apHumanVel), humanVelSlider);
    keysBandAtt = std::make_unique<ButtonAtt>(processor.apvts, paramId(KeysProcessor::apKeys), keysBandButton);
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
    // One Humanize control until 2026-08-02, split at Owen's ask so timing and dynamics
    // randomize independently. Same split as the macro rows' H.TIME / H.VEL pair.
    bar(humanSlider, humanLabel, "Human Time", 0.0, 100.0, "%",
        "Nudges each hit a little late, by a different amount every time. "
        "The arp is dead on the grid at 0.");
    bar(humanVelSlider, humanVelLabel, "Human Vel", 0.0, 100.0, "%",
        "Takes a little off each hit's velocity, by a different amount every time. Every hit "
        "lands at full strength at 0.");

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

    // PLAY moved here from the macro rows when they slimmed down (2026-08-02). The word is
    // Play rather than Keys for the reason logged the same day: "KEYS" collided head-on with
    // the bar's Light keys, two controls one word apart with nothing to say which was which.
    addAndMakeVisible(keysBandButton);
    keysBandButton.setTooltip("Does this line arpeggiate what you play on the keyboard at the "
                              "bottom? On, the keys you hold feed it. Off, it ignores the keybed "
                              "entirely and plays only the chord cards you hand it - which is "
                              "what lets one line follow your hands while the other runs a card.");
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

    buildAttachments();
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
    // The macro view: one *card* per line, side by side (2026-08-02, Owen: "having the
    // arpeggiators parallel to each other instead of one on top of the other"), and nothing
    // else - the tabs, BPM and Quantize ride the ARP section bar. A card is its caption rule
    // and three stacked lines - rate and shape under their RATE / SHAPE headings, the eight
    // knobs under theirs, the rate's modifiers with the held chord - because half the panel's
    // width cannot hold the old single-line row, and side by side is the point: two parallel
    // instruments, each a drop target half the panel wide. This sum has to agree with
    // MacroRow::resized exactly.
    constexpr int arpMacroCard = arpMacroCap + arpMacroHeads + arpMacroLine
                                 + arpMacroHeads + arpMacroLine + 2 + arpMacroMods + 6;
    // The second 2026-08-02 pass (Owen: "we need to make the window shorter ... move the BPM
    // up into the title ... move the A B All into the title and remove everything on the
    // bottom"). The shared row is gone: the A/B/All tabs, the BPM cell and Quantize all sit
    // on one 34 px header strip inside the LINES frame, their captions inline rather than
    // above. The slot row and the action row left this view entirely - the slots belong to
    // the per-line tabs now - so the view is the header and the two rows, full stop.
    constexpr int arpMacroH = arpMacroCard;
    constexpr int arpShapeH = 12 + (arpBandH + 8) + (arpBand2H + 12) + (arpSlotsH + 8) + 34 + 12;
    // The gap under the block. The outer `reduced(12)` is shared with the band and Pattern
    // views, and this height has to agree with resized() exactly or the panel is the wrong
    // size with nothing to say so.
    constexpr int arpMacroBelow = 8;
    constexpr int arpMacroTotalH = 12 + (arpMacroH + arpMacroBelow) + 12;
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
        area.removeFromTop(arpMacroBelow);
        // No LINES frame and no header strip: the view is the two cards and nothing else
        // (2026-08-02, Owen: "remove the 'lines' text" - and a box drawn around both cards
        // was the single strongest cue that they were one thing, the exact complaint). The
        // tabs, BPM and Quantize live on the ARP section bar now, and each card draws its
        // own captioned frame.
        for (auto& grp : groups)
            grp.visible = false;

        // The cards, side by side. Column count follows uiArpLines; at three, a column would
        // be ~300 px and eight 38 px knobs need ~350, so bringing line C back means giving
        // this layout a knob-width rethink, not just raising the constant.
        auto cardsArea = block.removeFromTop(arpMacroCard);
        const int cardGap = 12;
        const int cols = juce::jmax(1, KeysProcessor::uiArpLines);
        const int colW = (cardsArea.getWidth() - cardGap * (cols - 1)) / cols;
        for (auto& row : macroRows)
        {
            if (row == nullptr)
                continue;
            row->setBounds(cardsArea.removeFromLeft(colW));
            cardsArea.removeFromLeft(cardGap);
        }
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
        const int each = juce::jmax(120, (row.getWidth() - 24) / 4);
        cell(row, each, rampLabel, rampSlider);
        cell(row, each, rampTimeLabel, rampTimeSlider);
        cell(row, each, humanLabel, humanSlider);
        cell(row, juce::jmax(120, row.getWidth()), humanVelLabel, humanVelSlider);
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

    // --- The slot row and its buttons, at the bottom of both per-line shapes -------
    // Not the macro view, since 2026-08-02: the slots and the action row belong to the
    // per-line tabs, and the A/B/All tabs themselves were laid out in its header above -
    // running this block there would move them right back down to a row that no longer
    // exists on screen.
    if (! macroView)
    {
        auto actionRow = area.removeFromBottom(34);
        area.removeFromBottom(8);
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
