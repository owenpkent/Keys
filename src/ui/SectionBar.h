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
// UI Automation path the screenshot script uses.
//
// **The whole bar folds it** (2026-07-27, Owen's ask). It used to be the chevron end alone,
// on the reasoning that a click missing one of the bar's own controls by a few pixels would
// fold the section by accident. In practice the opposite was the problem: a 40 px target on
// a 34 px-tall full-width strip reads as if the strip is the button, so aiming at the
// chevron was the fiddly part, and the mouse-only contract is about making targets *bigger*.
// The controls that ride on the bar are its siblings and sit in front of it (KeysEditor
// toBack()s every bar), so they still take their own clicks; the bar only ever gets what
// they do not want.
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
        // Accessible name (a Button's own is its empty button text). "... section" and not
        // the bare caption, because the centre bar's caption follows the view and would
        // otherwise collide with the tab of the same name — ambiguous for a screen reader,
        // and it made UI Automation fold the section when asked to click the tab.
        setTitle(caption + " section");
        setTooltip("Show or hide " + caption.toLowerCase() + ".");
        setClickingTogglesState(true);
        setToggleState(true, juce::dontSendNotification); // open
    }

    // The centre bar's caption follows whichever view is showing, so it is not fixed at
    // construction like the other two.
    void setCaption(const juce::String& c)
    {
        if (c == title)
            return;
        title = c;
        setTitle(c + " section"); // keep the accessible name honest, and still distinct
        repaint();
    }

    // Where a caller may put section controls: right of the caption, inset from the ends.
    // In the *parent's* coordinates, because those controls are the bar's siblings, not
    // its children — the bar is a Button and must keep the whole of itself clickable.
    juce::Rectangle<int> contentArea() const
    {
        return getBoundsInParent().withTrimmedLeft(28 + captionWidth() + 12).withTrimmedRight(8).reduced(0, 4);
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto b = getLocalBounds().toFloat();
        const bool open = getToggleState();
        const auto accent = skin::accentOf(*this).base;

        // Open and folded are deliberately different weights, not the same bar with a
        // different arrow (Owen's ask, 2026-07-27). An open section's header is a solid
        // ruled band that reads as the lid of the thing under it; a folded one is a flat
        // dim strip that reads as a closed drawer. With six of these stacked, the shape of
        // the window is legible before you read a single caption.
        if (open)
        {
            g.setGradientFill({ skin::headerTop, b.getX(), b.getY(),
                                skin::headerBot, b.getX(), b.getBottom(), false });
            g.fillRoundedRectangle(b, skin::radius);

            // Top highlight and bottom rule: the two edges that make it a band rather than
            // a smear. The rule is the one that does the work - it is the join between the
            // header and its content.
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.fillRect(b.withHeight(1.0f).reduced(skin::radius, 0.0f));
            g.setColour(accent.withAlpha(0.28f));
            g.fillRect(b.withTop(b.getBottom() - 1.0f).reduced(skin::radius, 0.0f));

            // A tick of accent at the left end, so the eye finds the stack of open sections
            // down the left edge without any of them shouting.
            g.setColour(accent.withAlpha(0.85f));
            g.fillRoundedRectangle(juce::Rectangle<float>(3.0f, 7.0f, 2.5f, b.getHeight() - 14.0f), 1.25f);
        }
        else
        {
            g.setColour(skin::headerBot.withAlpha(0.55f));
            g.fillRoundedRectangle(b, skin::radius);
            g.setColour(juce::Colours::white.withAlpha(0.03f));
            g.drawRoundedRectangle(b.reduced(0.5f), skin::radius, 1.0f);
        }

        // The whole bar is the target now, so the whole bar lights. Anything less would
        // advertise a smaller button than the one that is actually there.
        if (highlighted || down)
        {
            g.setColour(juce::Colours::white.withAlpha(down ? 0.09f : 0.05f));
            g.fillRoundedRectangle(b.reduced(1.0f), skin::radius);
        }

        // Disclosure chevron: down when open, right when folded away.
        const auto centre = juce::Point<float>(17.0f, b.getCentreY());
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
        g.setColour(open ? accent : skin::textDim);
        g.strokePath(chevron, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        g.setColour(open ? skin::text : skin::textDim.withAlpha(0.75f));
        g.setFont(captionFont());
        g.drawText(title.toUpperCase(), getLocalBounds().withTrimmedLeft(28).withWidth(captionWidth()),
                   juce::Justification::centredLeft);
    }

private:
    // One font for measuring and for drawing. They used to differ - captionWidth() measured
    // the plain micro face while paintButton drew it letter-spaced - so the box was always a
    // little narrower than the text in it, and the longest caption ellipsised to "TRANSCRI...".
    // The kerning is also deliberately the same open and folded: it feeds contentArea(), and a
    // caption box that changed width with the fold would shift every control on the bar.
    static juce::Font captionFont() { return skin::micro(10.0f).withExtraKerningFactor(0.2f); }

    int captionWidth() const
    {
        return juce::jmax(60, (int) std::ceil(captionFont().getStringWidthFloat(title.toUpperCase())) + 8);
    }

    juce::String title;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SectionBar)
};
} // namespace keys
