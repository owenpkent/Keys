#include "XYPad.h"
#include "CCMenu.h"
#include <okstudio/MouseOnly.h>

namespace keys
{
XYPad::XYPad(KeysProcessor& p) : processor(p)
{
    okstudio::ui::makeMouseOnly(*this);

    const auto assign = [this](juce::TextButton& button, const char* paramID, bool yAxis)
    {
        button.onClick = [this, &button, paramID, yAxis]
        {
            cc::showMenu(button, assignedCC(yAxis), [this, paramID](int cc)
            {
                if (auto* param = processor.apvts.getParameter(paramID))
                    param->setValueNotifyingHost(param->convertTo0to1((float) cc));
            });
        };
        addAndMakeVisible(button);
    };
    assign(xButton, "xyCCX", false);
    assign(yButton, "xyCCY", true);

    lockXButton.setClickingTogglesState(true);
    lockYButton.setClickingTogglesState(true);
    addAndMakeVisible(lockXButton);
    addAndMakeVisible(lockYButton);

    resetButton.onClick = [this]
    {
        posX = posY = 0.5f;
        // Octavium's Reset announces the recentre on both CCs, even if unchanged.
        processor.sendCC(assignedCC(false), lastSentX = 64);
        processor.sendCC(assignedCC(true), lastSentY = 64);
        repaint();
    };
    addAndMakeVisible(resetButton);
    refreshAssignments();
}

int XYPad::assignedCC(bool yAxis) const
{
    return (int) processor.apvts.getRawParameterValue(yAxis ? "xyCCY" : "xyCCX")->load();
}

void XYPad::refreshAssignments()
{
    const auto x = "X: " + cc::label(assignedCC(false));
    const auto y = "Y: " + cc::label(assignedCC(true));
    if (xButton.getButtonText() != x)
        xButton.setButtonText(x);
    if (yButton.getButtonText() != y)
        yButton.setButtonText(y);
}

juce::Rectangle<float> XYPad::padArea() const
{
    auto area = getLocalBounds().toFloat().reduced(10.0f, 8.0f);
    area.removeFromBottom(38.0f); // control buttons live below the surface
    return area;
}

void XYPad::resized()
{
    // Assignments get double the width of the toggles: they carry CC names.
    auto row = getLocalBounds().reduced(10, 8).removeFromBottom(34);
    const int unit = row.getWidth() / 7;
    xButton.setBounds(row.removeFromLeft(unit * 2).reduced(2, 0));
    yButton.setBounds(row.removeFromLeft(unit * 2).reduced(2, 0));
    lockXButton.setBounds(row.removeFromLeft(unit).reduced(2, 0));
    lockYButton.setBounds(row.removeFromLeft(unit).reduced(2, 0));
    resetButton.setBounds(row.reduced(2, 0));
}

void XYPad::sendChanged()
{
    // Send only on change, so a slow drag doesn't spam identical values.
    const int x = juce::roundToInt(posX * 127.0f);
    const int y = juce::roundToInt(posY * 127.0f);
    if (x != lastSentX && ! lockXButton.getToggleState())
        processor.sendCC(assignedCC(false), lastSentX = x);
    if (y != lastSentY && ! lockYButton.getToggleState())
        processor.sendCC(assignedCC(true), lastSentY = y);
    repaint();
}

void XYPad::mouseDown(const juce::MouseEvent& e)
{
    // Relative drag like Octavium: the press only takes a reference; nothing moves or
    // sends until the mouse actually travels.
    refPos = e.position;
    refX = posX;
    refY = posY;
}

void XYPad::mouseDrag(const juce::MouseEvent& e)
{
    const auto pad = padArea();
    if (pad.isEmpty())
        return;
    if (! lockXButton.getToggleState())
        posX = juce::jlimit(0.0f, 1.0f, refX + (e.position.x - refPos.x) / pad.getWidth());
    if (! lockYButton.getToggleState())
        posY = juce::jlimit(0.0f, 1.0f, refY - (e.position.y - refPos.y) / pad.getHeight());
    sendChanged();
}

void XYPad::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1b1b1b));
    const auto pad = padArea();

    g.setColour(juce::Colour(0xff181a1f));
    g.fillRoundedRectangle(pad, 8.0f);

    // Light 10x10 grid, like Octavium's guide lines.
    g.setColour(juce::Colour(0xff2a2f35));
    for (int i = 1; i < 10; ++i)
    {
        const float fx = pad.getX() + pad.getWidth() * (float) i / 10.0f;
        const float fy = pad.getY() + pad.getHeight() * (float) i / 10.0f;
        g.drawLine(fx, pad.getY(), fx, pad.getBottom(), 1.0f);
        g.drawLine(pad.getX(), fy, pad.getRight(), fy, 1.0f);
    }
    g.drawRoundedRectangle(pad, 8.0f, 1.2f);

    // The knob: Octavium's rounded square, but scaled up to a real hit-size cue.
    const float cx = pad.getX() + posX * pad.getWidth();
    const float cy = pad.getY() + (1.0f - posY) * pad.getHeight();
    const juce::Rectangle<float> knob(cx - 10.0f, cy - 10.0f, 20.0f, 20.0f);
    g.setColour(juce::Colour(0xff61b3ff));
    g.fillRoundedRectangle(knob, 4.0f);
    g.setColour(juce::Colour(0xff2f82e6));
    g.drawRoundedRectangle(knob, 4.0f, 2.0f);
}
} // namespace keys
