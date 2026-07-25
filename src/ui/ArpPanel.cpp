#include "ArpPanel.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>

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
} // namespace

// ---------------------------------------------------------------------------
// LaneGrid

ArpPanel::LaneGrid::LaneGrid(KeysProcessor& p, ArpEngine::Lane l, int lo, int hi)
    : processor(p), lane(l), loVal(lo), hiVal(hi)
{
    okstudio::ui::makeMouseOnly(*this);
}

int ArpPanel::LaneGrid::currentLength() const
{
    return juce::jlimit(1, ArpEngine::maxSteps,
                        processor.arp.lanes.length[(size_t) lane].load(std::memory_order_relaxed));
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
    processor.arp.lanes.value[(size_t) lane][(size_t) step].store(value, std::memory_order_relaxed);
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
                                       processor.arp.lanes.value[(size_t) lane][(size_t) i].load(std::memory_order_relaxed));
        auto cell = juce::Rectangle<float>(b.getX() + cellW * (float) i, b.getY(), cellW, b.getHeight());

        g.setColour(juce::Colours::white.withAlpha(0.045f));
        g.drawVerticalLine((int) cell.getX(), b.getY(), b.getBottom());

        const auto bar = cell.reduced(1.5f);
        const float frac = hiVal > loVal ? (float) (value - loVal) / (float) (hiVal - loVal) : 0.0f;
        const auto filled = bar.withTop(bar.getBottom() - bar.getHeight() * frac);
        if (filled.getHeight() > 0.5f)
        {
            g.setGradientFill({ skin::accent.withAlpha(0.55f), 0.0f, filled.getY(),
                                skin::accentDeep.withAlpha(0.4f), 0.0f, bar.getBottom(), false });
            g.fillRect(filled);
            g.setColour(skin::accentHot.withAlpha(0.9f));
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
        g.setColour(skin::accent);
        g.drawRoundedRectangle(box.toFloat(), 4.0f, 1.0f);
        g.setColour(skin::text);
        g.setFont(f);
        g.drawText(txt, box, juce::Justification::centred);
    }
}

// ---------------------------------------------------------------------------
// MuteRow

ArpPanel::MuteRow::MuteRow(KeysProcessor& p) : processor(p)
{
    okstudio::ui::makeMouseOnly(*this);
}

int ArpPanel::MuteRow::currentLength() const
{
    return juce::jlimit(1, ArpEngine::maxSteps,
                        processor.arp.lanes.length[(size_t) ArpEngine::laneNote].load(std::memory_order_relaxed));
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
    processor.arp.lanes.value[(size_t) ArpEngine::laneNote][(size_t) step].store(paintValue, std::memory_order_relaxed);
    repaint();
}

void ArpPanel::MuteRow::mouseDown(const juce::MouseEvent& e)
{
    const int step = stepAtX(e.position.x);
    const int current = processor.arp.lanes.value[(size_t) ArpEngine::laneNote][(size_t) step].load(std::memory_order_relaxed);
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
        const int value = processor.arp.lanes.value[(size_t) ArpEngine::laneNote][(size_t) i].load(std::memory_order_relaxed);
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
            g.setGradientFill({ skin::accent.withAlpha(0.5f), 0.0f, cell.getY(),
                                skin::accentDeep.withAlpha(0.45f), 0.0f, cell.getBottom(), false });
            g.fillRoundedRectangle(cell, 3.0f);
            g.setColour(skin::accentHot.withAlpha(0.5f));
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

ArpPanel::ArpPanel(KeysProcessor& p) : processor(p)
{
    okstudio::ui::makeMouseOnly(*this);
    buildControls();
    selectLane(selectedLane); // tab toggles + which grid is visible
    refreshShape();           // and whether the step editor is showing at all
    refreshLaneReadouts();
    refreshPatternButtons();
    startTimerHz(10); // repaints the lane grid so edits made elsewhere stay current
}

ArpPanel::~ArpPanel()
{
    stopTimer();
}

void ArpPanel::buildLaneRow(LaneRow& row, ArpEngine::Lane lane, const juce::String& name, int loVal, int hiVal)
{
    row.tab.setButtonText(name);
    row.tab.onClick = [this, lane] { selectLane((int) lane); };
    addAndMakeVisible(row.tab);

    row.grid = std::make_unique<LaneGrid>(processor, lane, loVal, hiVal);
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
    const bool linked = processor.apvts.getRawParameterValue("arpLinkLanes")->load() > 0.5f;
    const int from = linked ? 0 : selectedLane;
    const int to = linked ? ArpEngine::numLanes - 1 : selectedLane;
    const int target = juce::jlimit(1, ArpEngine::maxSteps,
                                    processor.arp.lanes.length[(size_t) selectedLane]
                                            .load(std::memory_order_relaxed) + delta);
    for (int i = from; i <= to; ++i)
        processor.arp.lanes.length[(size_t) i].store(target, std::memory_order_relaxed);
    refreshLaneReadouts();
}

void ArpPanel::cycleClockDiv()
{
    const bool linked = processor.apvts.getRawParameterValue("arpLinkLanes")->load() > 0.5f;
    const int next = (processor.arp.lanes.clockDiv[(size_t) selectedLane].load(std::memory_order_relaxed) + 1) % 3;
    const int from = linked ? 0 : selectedLane;
    const int to = linked ? ArpEngine::numLanes - 1 : selectedLane;
    for (int i = from; i <= to; ++i)
        processor.arp.lanes.clockDiv[(size_t) i].store(next, std::memory_order_relaxed);
    refreshLaneReadouts();
}

void ArpPanel::refreshLaneReadouts()
{
    const auto li = (size_t) selectedLane;
    const int len = processor.arp.lanes.length[li].load(std::memory_order_relaxed);
    stepsReadout.setText(juce::String(len), juce::dontSendNotification);
    const int div = juce::jlimit(0, 2, processor.arp.lanes.clockDiv[li].load(std::memory_order_relaxed));
    speedButton.setButtonText(clockDivNames[div]);
}

bool ArpPanel::patternMode() const
{
    return processor.apvts.getRawParameterValue("arpPattern")->load() > 0.5f;
}

void ArpPanel::applyShapeChoice()
{
    const int chosen = shapeBox.getSelectedItemIndex(); // 0..7 = a direction, 8 = Pattern
    auto& apvts = processor.apvts;
    // Gestures by hand. Every other control here is an APVTS attachment and gets its
    // begin/end for free; Shape spans two parameters so it cannot be one, and without
    // the brackets a host in touch or latch never arms on a Shape change.
    if (auto* pat = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("arpPattern")))
    {
        pat->beginChangeGesture();
        *pat = chosen >= ArpEngine::numDirections;
        pat->endChangeGesture();
    }
    if (chosen >= 0 && chosen < ArpEngine::numDirections)
        if (auto* dir = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("arpDirection")))
        {
            dir->beginChangeGesture();
            *dir = chosen; // "Pattern" leaves the direction alone; lanes can still follow it
            dir->endChangeGesture();
        }
    refreshShape();
}

// Parameters are the truth (a host can automate them), so the combo and the step
// editor's visibility are both derived, never assumed.
void ArpPanel::refreshShape()
{
    const bool pattern = patternMode();
    const int dir = (int) processor.apvts.getRawParameterValue("arpDirection")->load();
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
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &stepsLabel, &speedLabel, &stepsReadout, &stepsMinus, &stepsPlus, &speedButton, &linkButton })
        c->setVisible(pattern);
    for (auto& b : patternButtons)
        b.setVisible(pattern);
    copyButton.setVisible(pattern);
    cancelButton.setVisible(pattern && copyArmed);
    randomizeButton.setVisible(pattern);

    // The card changes height with the mode, so relayout and repaint - but only on an
    // actual change, since refreshShape() runs on the 10 Hz timer.
    if (lastPatternMode != (int) pattern)
    {
        lastPatternMode = (int) pattern;
        resized();
        repaint();
    }
}

void ArpPanel::recallOrCopy(int index)
{
    if (copyArmed)
    {
        if (index != copyFromIndex)
            processor.copyArpPattern(copyFromIndex, index);
        copyArmed = false;
        copyFromIndex = -1;
    }
    else
    {
        processor.recallArpPattern(index);
    }
    refreshPatternButtons();
}

void ArpPanel::refreshPatternButtons()
{
    const int active = processor.arpActivePattern();
    for (int i = 0; i < (int) patternButtons.size(); ++i)
        patternButtons[(size_t) i].setToggleState(! copyArmed && i == active, juce::dontSendNotification);

    copyButton.setButtonText(copyArmed
                                 ? "Copy from " + juce::String::charToString((juce::juce_wchar) ('A' + copyFromIndex))
                                 : juce::String("Copy"));
    copyButton.setToggleState(copyArmed, juce::dontSendNotification);
    cancelButton.setVisible(copyArmed && patternMode());
}

void ArpPanel::buildControls()
{
    title.setText("Arpeggiator", juce::dontSendNotification);
    title.setFont(skin::uiSemi(16.0f).withExtraKerningFactor(0.04f));
    addAndMakeVisible(title);

    addAndMakeVisible(onButton);
    onAtt = std::make_unique<ButtonAtt>(processor.apvts, "arpOn", onButton);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeButton);

    // Globals: rate + feel.
    styleLabel(rateLabel, "Rate");
    addAndMakeVisible(rateLabel);
    rateBox.addItemList({ "16 bars", "8 bars", "4 bars", "2 bars", "1 bar",
                          "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" }, 1);
    addAndMakeVisible(rateBox);
    rateAtt = std::make_unique<ComboAtt>(processor.apvts, "arpRate", rateBox);

    for (auto* b : { &dotButton, &tripButton, &anchorButton })
        addAndMakeVisible(*b);
    dotAtt = std::make_unique<ButtonAtt>(processor.apvts, "arpDot", dotButton);
    tripAtt = std::make_unique<ButtonAtt>(processor.apvts, "arpTrip", tripButton);
    anchorAtt = std::make_unique<ButtonAtt>(processor.apvts, "arpAnchor", anchorButton);
    anchorButton.setTooltip("Anchored: locked to the host bar grid. Free: never jumps, may drift.");

    styleLabel(shapeLabel, "Shape");
    addAndMakeVisible(shapeLabel);
    shapeBox.addItemList({ "Up", "Down", "Up-Down", "Down-Up",
                           "Up & Down", "Down & Up", "As Played", "Reversed" }, 1);
    shapeBox.addItem("Pattern", ArpEngine::numDirections + 1);
    shapeBox.onChange = [this] { applyShapeChoice(); };
    shapeBox.setTooltip("A shape arpeggiates the held chord. \"Pattern\" opens the step editor.");
    addAndMakeVisible(shapeBox);

    styleLabel(octavesLabel, "Octaves");
    addAndMakeVisible(octavesLabel);
    octavesSlider.setSliderStyle(juce::Slider::IncDecButtons);
    octavesSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 26);
    octavesSlider.setRange(1, 4, 1);
    addAndMakeVisible(octavesSlider);
    octavesAtt = std::make_unique<SliderAtt>(processor.apvts, "arpOctaves", octavesSlider);

    styleLabel(swingLabel, "Swing");
    addAndMakeVisible(swingLabel);
    swingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    swingSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 26);
    swingSlider.setRange(0.0, 0.75, 0.01);
    addAndMakeVisible(swingSlider);
    swingAtt = std::make_unique<SliderAtt>(processor.apvts, "arpSwing", swingSlider);

    for (auto* b : { &latchButton, &retriggerButton })
        addAndMakeVisible(*b);
    latchAtt = std::make_unique<ButtonAtt>(processor.apvts, "arpLatch", latchButton);
    retriggerAtt = std::make_unique<ButtonAtt>(processor.apvts, "arpRetrigger", retriggerButton);
    latchButton.setTooltip("Ignore note-offs until a new chord arrives.");
    retriggerButton.setTooltip("Restart at step 1 when a note arrives on an empty set.");

    // The six lanes, in ArpEngine::Lane order.
    buildLaneRow(laneRows[(size_t) ArpEngine::laneNote], ArpEngine::laneNote, "Note", -1, 8);
    buildLaneRow(laneRows[(size_t) ArpEngine::laneOctave], ArpEngine::laneOctave, "Octave", -3, 3);
    buildLaneRow(laneRows[(size_t) ArpEngine::laneVelocity], ArpEngine::laneVelocity, "Velocity", 10, 200);
    buildLaneRow(laneRows[(size_t) ArpEngine::laneGate], ArpEngine::laneGate, "Gate", 5, 200);
    buildLaneRow(laneRows[(size_t) ArpEngine::laneRatchet], ArpEngine::laneRatchet, "Ratchet", 1, 4);
    buildLaneRow(laneRows[(size_t) ArpEngine::laneProbability], ArpEngine::laneProbability, "Probability", 0, 100);

    styleLabel(muteRowLabel, "Mute");
    addAndMakeVisible(muteRowLabel);
    muteRow = std::make_unique<MuteRow>(processor);
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
    linkAtt = std::make_unique<ButtonAtt>(processor.apvts, "arpLinkLanes", linkButton);

    // Patterns: A-H recall, Copy (arm, then click a target letter), Randomize.
    for (int i = 0; i < (int) patternButtons.size(); ++i)
    {
        auto& b = patternButtons[(size_t) i];
        b.setButtonText(juce::String::charToString((juce::juce_wchar) ('A' + i)));
        b.onClick = [this, i] { recallOrCopy(i); };
        addAndMakeVisible(b);
    }
    copyButton.onClick = [this]
    {
        if (copyArmed)
        {
            copyArmed = false;
            copyFromIndex = -1;
        }
        else
        {
            copyArmed = true;
            copyFromIndex = processor.arpActivePattern();
        }
        refreshPatternButtons();
    };
    addAndMakeVisible(copyButton);

    cancelButton.onClick = [this]
    {
        copyArmed = false;
        copyFromIndex = -1;
        refreshPatternButtons();
    };
    addAndMakeVisible(cancelButton);
    cancelButton.setVisible(false);

    randomizeButton.onClick = [this] { processor.randomizeActiveArpPattern(); };
    addAndMakeVisible(randomizeButton);
}

void ArpPanel::timerCallback()
{
    refreshShape(); // the host can automate arpPattern/arpDirection out from under us
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
    // The overlay is opaque to clicks: nothing behind it should react while it is up.
}

// Heights of the two layouts, kept next to the resized() that spends them: title + the
// two globals rows, plus (in Pattern) tabs, grid, mute row, the steps row and the
// pattern row, with the 12 px inner padding at both ends.
juce::Rectangle<int> ArpPanel::cardBounds() const
{
    constexpr int shapeH = 12 + (28 + 8) + (40 + 6) + 40 + 12;
    constexpr int patternH = shapeH + (34 + 6) + (150 + 6) + (14 + 2) + (34 + 10) + (48 + 8) + 40;
    const auto full = getLocalBounds().reduced(8);
    return full.withHeight(juce::jmin(full.getHeight(), patternMode() ? patternH : shapeH));
}

void ArpPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.78f)); // dim whatever is behind the overlay
    const auto b = cardBounds().toFloat();
    g.setColour(juce::Colour(0xff1c1f24));
    g.fillRoundedRectangle(b, skin::panelRadius);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.fillRoundedRectangle(b.withHeight(1.5f).reduced(skin::panelRadius, 0.0f), 0.75f);
    skin::glowRect(g, b, skin::panelRadius, skin::accent, 0.55f);
}

void ArpPanel::resized()
{
    auto area = cardBounds().reduced(12);

    auto top = area.removeFromTop(28);
    title.setBounds(top.removeFromLeft(160));
    closeButton.setBounds(top.removeFromRight(80).withSizeKeepingCentre(80, 28));
    onButton.setBounds(top.removeFromRight(70).withSizeKeepingCentre(70, 28));
    area.removeFromTop(8);

    const auto cell = [](juce::Rectangle<int>& row, int w, juce::Label& lab, juce::Component& ctl)
    {
        auto c = row.removeFromLeft(w);
        row.removeFromLeft(8);
        lab.setBounds(c.removeFromTop(14));
        ctl.setBounds(c);
    };
    const auto toggleCell = [](juce::Rectangle<int>& row, int w, juce::Component& ctl)
    {
        auto c = row.removeFromLeft(w);
        row.removeFromLeft(8);
        ctl.setBounds(c.withTrimmedTop(14));
    };

    auto globalsA = area.removeFromTop(40);
    area.removeFromTop(6);
    cell(globalsA, 160, shapeLabel, shapeBox); // Shape leads: it decides what else exists
    cell(globalsA, 110, rateLabel, rateBox);
    toggleCell(globalsA, 60, dotButton);
    toggleCell(globalsA, 60, tripButton);
    toggleCell(globalsA, 80, anchorButton);

    auto globalsB = area.removeFromTop(40);
    area.removeFromTop(8);
    cell(globalsB, 120, octavesLabel, octavesSlider);
    cell(globalsB, 180, swingLabel, swingSlider);
    toggleCell(globalsB, 80, latchButton);
    toggleCell(globalsB, 100, retriggerButton);

    // Everything below exists only in Pattern shape. Laying it out regardless is
    // harmless (it is all invisible) and keeps this function free of a second branch.

    // Lane tabs, then the one lane they select, then the mute row beneath it. Six
    // stacked lanes needed ~750 px and the panel gets ~600, so the Probability lane and
    // the whole pattern row used to be cut off the bottom of the window entirely.
    auto tabsRow = area.removeFromTop(34);
    area.removeFromTop(6);
    const int tabW = juce::jmax(70, (tabsRow.getWidth() - 5 * 4) / ArpEngine::numLanes);
    for (auto& lr : laneRows)
    {
        lr.tab.setBounds(tabsRow.removeFromLeft(tabW));
        tabsRow.removeFromLeft(4);
    }

    auto gridArea = area.removeFromTop(150);
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
    auto muteArea = area.removeFromTop(34);
    area.removeFromTop(10);
    muteRow->setBounds(muteArea); // same x and width as gridArea, both carved off `area`

    auto stepsRow = area.removeFromTop(48);
    area.removeFromTop(8);
    auto stepsCell = stepsRow.removeFromLeft(150);
    stepsLabel.setBounds(stepsCell.removeFromTop(14));
    stepsMinus.setBounds(stepsCell.removeFromLeft(34));
    stepsPlus.setBounds(stepsCell.removeFromRight(34));
    stepsReadout.setBounds(stepsCell);
    stepsRow.removeFromLeft(12);

    auto speedCell = stepsRow.removeFromLeft(90);
    speedLabel.setBounds(speedCell.removeFromTop(14));
    speedButton.setBounds(speedCell);
    stepsRow.removeFromLeft(16);
    linkButton.setBounds(stepsRow.removeFromLeft(130).withTrimmedTop(14));

    auto patternRow = area.removeFromTop(40);
    for (auto& b : patternButtons)
    {
        b.setBounds(patternRow.removeFromLeft(38));
        patternRow.removeFromLeft(4);
    }
    patternRow.removeFromLeft(8);
    copyButton.setBounds(patternRow.removeFromLeft(120));
    patternRow.removeFromLeft(4);
    cancelButton.setBounds(patternRow.removeFromLeft(80));
    patternRow.removeFromLeft(12);
    randomizeButton.setBounds(patternRow.removeFromLeft(120));
}
} // namespace keys
