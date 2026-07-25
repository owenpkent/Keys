#pragma once

#include "KeysLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace keys
{
// The fold/unfold header above a section of the editor. Owen asked for every section to
// be minimizable so the plugin can be squeezed into a small window when the screen is
// busy; this is the affordance that does it.
//
// It is a juce::Button, not a hand-rolled Component, so it inherits the mouse-only
// contract for free: single left-click, no modifiers, and a real accessible name for the
// UI Automation path the screenshot script uses. The whole bar is the target — 34 px tall
// and full width, well past the 34 px floor.
//
// The bar is not just chrome: callers hang the section's own small controls in the space
// to the right of the caption (see KeysEditor::resized), which is why it exposes
// `contentArea()` rather than laying anything out itself.
class SectionBar : public juce::Button
{
public:
    static constexpr int height = 34;

    explicit SectionBar(const juce::String& caption) : juce::Button(caption), title(caption)
    {
        setTitle(caption); // accessible name; a Button's own is its (empty) button text
        setTooltip("Show or hide " + caption.toLowerCase() + ".");
        setClickingTogglesState(true);
        setToggleState(true, juce::dontSendNotification); // open
    }

    // Where a caller may put section controls: right of the caption, inset from the ends.
    // In the *parent's* coordinates, because those controls are the bar's siblings, not
    // its children — the bar is a Button and must keep the whole of itself clickable.
    juce::Rectangle<int> contentArea() const
    {
        return getBoundsInParent().withTrimmedLeft(26 + captionWidth() + 12).withTrimmedRight(8).reduced(0, 4);
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto b = getLocalBounds().toFloat();
        const bool open = getToggleState();

        g.setColour(skin::headerTop.withAlpha(highlighted || down ? 1.0f : 0.75f));
        g.fillRoundedRectangle(b, skin::radius);
        g.setColour(juce::Colours::white.withAlpha(0.04f));
        g.fillRoundedRectangle(b.withHeight(1.0f).reduced(skin::radius, 0.0f), 0.5f);

        // Disclosure chevron: down when open, right when folded away.
        const auto centre = juce::Point<float>(15.0f, b.getCentreY());
        juce::Path chevron;
        if (open)
        {
            chevron.startNewSubPath(centre.x - 4.5f, centre.y - 2.0f);
            chevron.lineTo(centre.x, centre.y + 2.5f);
            chevron.lineTo(centre.x + 4.5f, centre.y - 2.0f);
        }
        else
        {
            chevron.startNewSubPath(centre.x - 2.0f, centre.y - 4.5f);
            chevron.lineTo(centre.x + 2.5f, centre.y);
            chevron.lineTo(centre.x - 2.0f, centre.y + 4.5f);
        }
        g.setColour(open ? skin::accent : skin::textDim);
        g.strokePath(chevron, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        g.setColour(open ? skin::text : skin::textDim);
        g.setFont(skin::micro(10.0f).withExtraKerningFactor(0.16f));
        g.drawText(title.toUpperCase(), getLocalBounds().withTrimmedLeft(26).withWidth(captionWidth()),
                   juce::Justification::centredLeft);
    }

private:
    int captionWidth() const { return juce::jmax(60, (int) std::ceil(skin::micro(10.0f).getStringWidthFloat(title.toUpperCase())) + 8); }

    juce::String title;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SectionBar)
};
} // namespace keys
