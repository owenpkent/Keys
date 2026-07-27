#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace keys
{
// A two-value slider whose *band* can be dragged, moving both ends together and keeping
// the width between them.
//
// JUCE's TwoValueHorizontal only gives you the two thumbs: the span between them is inert,
// so shifting a range meant dragging one end, then the other, and getting the width back
// by eye. For the humanize velocity range that is three careful gestures to do something
// that is conceptually one, which is the wrong trade for a one-mouse player.
//
// Clicks on or near a thumb still go to the base class, so the ends stay independently
// adjustable; only a grab in the middle moves the whole band. Nothing here needs a
// modifier key, per the mouse-only contract.
class RangeSlider : public juce::Slider
{
public:
    RangeSlider()
    {
        setSliderStyle(juce::Slider::TwoValueHorizontal);
        setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (! startBandDrag(e))
            juce::Slider::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (! draggingBand)
        {
            juce::Slider::mouseDrag(e);
            return;
        }

        const double perPixel = valuePerPixel();
        const double width = startMax - startMin;
        // Clamp the *band*, not each end, so a drag to either extreme slides it flush
        // instead of squashing the width the user set.
        const double wanted = startMin + (double) (e.position.x - grabX) * perPixel;
        const double lo = juce::jlimit(getMinimum(), getMaximum() - width, wanted);
        setMinAndMaxValues(lo, lo + width, juce::sendNotificationSync);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (draggingBand)
            draggingBand = false;
        else
            juce::Slider::mouseUp(e);
    }

private:
    // Pixels-to-value from the slider's own geometry, so it stays correct whatever the
    // LookAndFeel does with the track inset.
    double valuePerPixel() const
    {
        const double span = getPositionOfValue(getMaximum()) - getPositionOfValue(getMinimum());
        if (std::abs(span) < 1.0)
            return 0.0;
        return (getMaximum() - getMinimum()) / span;
    }

    bool startBandDrag(const juce::MouseEvent& e)
    {
        const double loX = getPositionOfValue(getMinValue());
        const double hiX = getPositionOfValue(getMaxValue());
        // Leave a thumb's width at each end to the base class, so grabbing an end to
        // resize the range still works and stays the easier gesture of the two.
        constexpr double thumbGuard = 11.0;
        if (hiX - loX < thumbGuard * 2.0 + 6.0)
            return false; // too narrow to have a middle worth grabbing

        const double x = e.position.x;
        if (x <= loX + thumbGuard || x >= hiX - thumbGuard)
            return false;

        draggingBand = true;
        grabX = e.position.x;
        startMin = getMinValue();
        startMax = getMaxValue();
        return true;
    }

    bool draggingBand = false;
    float grabX = 0.0f;
    double startMin = 0.0, startMax = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RangeSlider)
};
} // namespace keys
