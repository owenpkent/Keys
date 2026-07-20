#include "ArpPanel.h"
#include <okstudio/MouseOnly.h>

namespace keys
{
namespace
{
    // Caption styling only (font, no colour). ArpPanel is constructed before it is
    // parented into KeysEditor (same as ChordGenPanel), so a colour snapshotted here
    // via findColour() would read the JUCE default LookAndFeel, not the editor's
    // okstudio theme. Leaving Label/ToggleButton colours unset lets every control
    // resolve its colour through the parent chain at paint time instead, which is
    // when the theme is actually in effect.
    void styleLabel(juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
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
    const auto outline = findColour(juce::ComboBox::outlineColourId);
    const auto accent = findColour(juce::Slider::trackColourId);
    const auto text = findColour(juce::Label::textColourId);
    const auto dim = findColour(juce::ToggleButton::tickDisabledColourId);

    g.setColour(findColour(juce::ComboBox::backgroundColourId));
    g.fillRect(b);

    const int length = currentLength();
    const float cellW = length > 0 ? b.getWidth() / (float) length : b.getWidth();

    for (int i = 0; i < length; ++i)
    {
        const int value = juce::jlimit(loVal, hiVal,
                                       processor.arp.lanes.value[(size_t) lane][(size_t) i].load(std::memory_order_relaxed));
        auto cell = juce::Rectangle<float>(b.getX() + cellW * (float) i, b.getY(), cellW, b.getHeight());

        g.setColour(outline.withAlpha(0.35f));
        g.drawVerticalLine((int) cell.getX(), b.getY(), b.getBottom());

        const auto bar = cell.reduced(1.5f);
        const float frac = hiVal > loVal ? (float) (value - loVal) / (float) (hiVal - loVal) : 0.0f;
        const auto filled = bar.withTop(bar.getBottom() - bar.getHeight() * frac);
        g.setColour(accent.withAlpha(0.6f));
        g.fillRect(filled);

        const bool asDot = (lane == ArpEngine::laneNote && value == 0);
        if (asDot)
        {
            const float r = juce::jmin(6.0f, cell.getWidth() * 0.25f);
            g.setColour(text);
            g.fillEllipse(cell.getCentreX() - r, cell.getCentreY() - r, r * 2.0f, r * 2.0f);
        }
        else if (cell.getWidth() > 16.0f)
        {
            const auto txt = cellText(value);
            if (txt.isNotEmpty())
            {
                g.setColour(lane == ArpEngine::laneNote && value == -1 ? dim : text);
                g.setFont(juce::Font(juce::FontOptions(11.0f)));
                g.drawText(txt, cell.toNearestInt(), juce::Justification::centred);
            }
        }
    }

    g.setColour(outline);
    g.drawRect(b, 1.0f);

    if (dragging)
    {
        const auto txt = juce::String(cursorValue);
        const juce::Font f(juce::FontOptions(12.0f, juce::Font::bold));
        const int tw = f.getStringWidth(txt) + 14;
        auto box = juce::Rectangle<int>(juce::roundToInt(cursorPos.x) - tw / 2,
                                        juce::roundToInt(cursorPos.y) - 28, tw, 20)
                      .constrainedWithin(getLocalBounds());
        g.setColour(findColour(juce::PopupMenu::backgroundColourId));
        g.fillRoundedRectangle(box.toFloat(), 4.0f);
        g.setColour(accent);
        g.drawRoundedRectangle(box.toFloat(), 4.0f, 1.0f);
        g.setColour(text);
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
    const auto outline = findColour(juce::ComboBox::outlineColourId);
    const auto accent = findColour(juce::Slider::trackColourId);
    const auto dim = findColour(juce::ToggleButton::tickDisabledColourId);

    const int length = currentLength();
    const float cellW = length > 0 ? b.getWidth() / (float) length : b.getWidth();

    for (int i = 0; i < length; ++i)
    {
        const int value = processor.arp.lanes.value[(size_t) ArpEngine::laneNote][(size_t) i].load(std::memory_order_relaxed);
        const bool muted = value == -1;
        auto cell = juce::Rectangle<float>(b.getX() + cellW * (float) i, b.getY(), cellW, b.getHeight()).reduced(2.0f);

        g.setColour(muted ? dim.withAlpha(0.3f) : accent.withAlpha(0.6f));
        g.fillRoundedRectangle(cell, 3.0f);
        g.setColour(outline);
        g.drawRoundedRectangle(cell, 3.0f, 1.0f);

        if (muted && cell.getWidth() > 14.0f)
        {
            g.setColour(dim);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
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
    refreshLaneReadouts();
    refreshPatternButtons();
    startTimerHz(10); // repaints the lane grids so edits made elsewhere stay current
}

ArpPanel::~ArpPanel()
{
    stopTimer();
}

void ArpPanel::buildLaneRow(LaneRow& row, ArpEngine::Lane lane, const juce::String& name, int loVal, int hiVal)
{
    styleLabel(row.name, name);
    addAndMakeVisible(row.name);

    row.grid = std::make_unique<LaneGrid>(processor, lane, loVal, hiVal);
    addAndMakeVisible(*row.grid);

    row.lengthReadout.setJustificationType(juce::Justification::centred);
    row.lengthReadout.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(row.lengthReadout);

    row.lenMinus.onClick = [this, lane]
    {
        auto& len = processor.arp.lanes.length[(size_t) lane];
        len.store(juce::jlimit(1, ArpEngine::maxSteps, len.load(std::memory_order_relaxed) - 1), std::memory_order_relaxed);
        refreshLaneReadouts();
    };
    row.lenPlus.onClick = [this, lane]
    {
        auto& len = processor.arp.lanes.length[(size_t) lane];
        len.store(juce::jlimit(1, ArpEngine::maxSteps, len.load(std::memory_order_relaxed) + 1), std::memory_order_relaxed);
        refreshLaneReadouts();
    };
    addAndMakeVisible(row.lenMinus);
    addAndMakeVisible(row.lenPlus);

    row.clockDiv.onClick = [this, lane] { cycleClockDiv(lane); };
    row.clockDiv.setTooltip("Step speed for this lane: full speed, half, or quarter (polymeter).");
    addAndMakeVisible(row.clockDiv);
}

void ArpPanel::cycleClockDiv(ArpEngine::Lane lane)
{
    auto& div = processor.arp.lanes.clockDiv[(size_t) lane];
    div.store((div.load(std::memory_order_relaxed) + 1) % 3, std::memory_order_relaxed);
    refreshLaneReadouts();
}

void ArpPanel::refreshLaneReadouts()
{
    for (int i = 0; i < ArpEngine::numLanes; ++i)
    {
        auto& row = laneRows[(size_t) i];
        const int len = processor.arp.lanes.length[(size_t) i].load(std::memory_order_relaxed);
        row.lengthReadout.setText(juce::String(len), juce::dontSendNotification);
        const int div = juce::jlimit(0, 2, processor.arp.lanes.clockDiv[(size_t) i].load(std::memory_order_relaxed));
        row.clockDiv.setButtonText(clockDivNames[div]);
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
    cancelButton.setVisible(copyArmed);
}

void ArpPanel::buildControls()
{
    title.setText("Arpeggiator", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
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

    styleLabel(directionLabel, "Direction");
    addAndMakeVisible(directionLabel);
    directionBox.addItemList({ "Up", "Down", "Up-Down", "Down-Up",
                               "Up & Down", "Down & Up", "As Played", "Reversed" }, 1);
    addAndMakeVisible(directionBox);
    directionAtt = std::make_unique<ComboAtt>(processor.apvts, "arpDirection", directionBox);

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
    refreshLaneReadouts();
    refreshPatternButtons();
    for (auto& row : laneRows)
        row.grid->repaint();
    if (muteRow != nullptr)
        muteRow->repaint();
}

void ArpPanel::mouseDown(const juce::MouseEvent&)
{
    // The overlay is opaque to clicks: nothing behind it should react while it is up.
}

void ArpPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.75f)); // dim whatever is behind the overlay
    auto b = getLocalBounds().reduced(8).toFloat();
    g.setColour(findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(b, 8.0f);
    g.setColour(findColour(juce::Slider::trackColourId));
    g.drawRoundedRectangle(b, 8.0f, 1.5f);
}

void ArpPanel::resized()
{
    auto area = getLocalBounds().reduced(8).reduced(12);

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
    cell(globalsA, 110, rateLabel, rateBox);
    toggleCell(globalsA, 60, dotButton);
    toggleCell(globalsA, 60, tripButton);
    toggleCell(globalsA, 80, anchorButton);
    cell(globalsA, 160, directionLabel, directionBox);

    auto globalsB = area.removeFromTop(40);
    area.removeFromTop(8);
    cell(globalsB, 120, octavesLabel, octavesSlider);
    cell(globalsB, 180, swingLabel, swingSlider);
    toggleCell(globalsB, 80, latchButton);
    toggleCell(globalsB, 100, retriggerButton);

    // Six lane rows: name | grid | length -/+ and clock-div on the right. The note
    // lane's mute row goes immediately under it, aligned to the same grid columns.
    constexpr int laneRowH = 76;
    for (int i = 0; i < ArpEngine::numLanes; ++i)
    {
        auto row = area.removeFromTop(laneRowH);
        area.removeFromTop(6);

        auto& lr = laneRows[(size_t) i];
        lr.name.setBounds(row.removeFromLeft(90));
        row.removeFromLeft(8);
        auto rightCol = row.removeFromRight(120);
        row.removeFromRight(8);
        lr.grid->setBounds(row);

        auto lenRow = rightCol.removeFromTop(34);
        lr.lenMinus.setBounds(lenRow.removeFromLeft(34));
        lr.lenPlus.setBounds(lenRow.removeFromRight(34));
        lr.lengthReadout.setBounds(lenRow);
        rightCol.removeFromTop(6);
        lr.clockDiv.setBounds(rightCol.removeFromTop(34));

        if (i == (int) ArpEngine::laneNote)
        {
            auto muteArea = area.removeFromTop(34);
            area.removeFromTop(6);
            muteRowLabel.setBounds(muteArea.removeFromLeft(90));
            muteArea.removeFromLeft(8);
            muteArea.removeFromRight(120);
            muteArea.removeFromRight(8);
            muteRow->setBounds(muteArea);
        }
    }

    area.removeFromTop(6);
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
