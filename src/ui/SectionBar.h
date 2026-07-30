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
// **Only the left end folds it** (2026-07-30, Owen's ask). For three days the whole strip
// was the button, on the reasoning that a 34 px-tall full-width band reads as one target,
// that the mouse-only contract is about making targets *bigger*, and that z-order already
// stopped the bar stealing any control's click. That last part was true and still is: the
// controls riding the bar are its siblings sitting in front of it (KeysEditor toBack()s
// every bar), so a click that lands *on* Detach has always reached Detach.
//
// What it missed is that z-order only defends each control's own rectangle. It says nothing
// about the gaps around them, and on a bar whose right end is mostly gap, a click aimed at
// Detach that lands a few pixels off it hits bar, and the bar folded the section away. The
// cost of that miss is asymmetric: hitting Detach does what you wanted, missing it hides
// the thing you were reaching into. Bigger is only kinder when the extra area does what the
// target does, and out there it did the opposite.
//
// So the fold target is the chevron and its caption again, `foldZone()`, still 92 px wide at
// the narrowest caption, and the only part of the bar that lights under the mouse or answers
// to one. Note this reverses the 2026-07-27 removal of this same override; docs that still
// say "the whole bar is the target" are describing that three-day window.
//
// The bar is not just chrome: callers hang the section's own small controls in the space
// to the right of the caption (see KeysEditor::resized), which is why it exposes
// `contentArea()` rather than laying anything out itself. That rectangle is measured off
// `foldZone()`, so the clickable end and the free end cannot drift apart.
class SectionBar : public juce::Button
{
public:
    static constexpr int height = 34;

    explicit SectionBar(const juce::String& caption) : juce::Button(caption), title(caption)
    {
        // Accessible name (a Button's own is its empty button text). "... section" and not
        // the bare caption, so a bar never shares a name with a control riding on it: the
        // screenshot script drives this plugin through UI Automation, which takes the first
        // match, and a bar that answered to the same name as a button would fold the section
        // it was asked to click into.
        setTitle(caption + " section");
        setTooltip("Show or hide " + caption.toLowerCase() + ".");
        setClickingTogglesState(true);
        setToggleState(true, juce::dontSendNotification); // open
    }

    // The part of the bar that folds it: the chevron (28 px) and the caption, plus 4 px of
    // slack so the last letter is not on the edge of the target. In the bar's *own*
    // coordinates, because that is what hitTest and paintButton are given.
    juce::Rectangle<int> foldZone() const { return getLocalBounds().withWidth(28 + captionWidth() + 4); }

    // Everything right of the fold zone belongs to the controls sitting on the bar, so the
    // bar declines those clicks and they land on whatever is there (or on nothing).
    bool hitTest(int x, int y) override { return foldZone().contains(x, y); }

    // Where a caller may put section controls: right of the fold zone, inset from the ends.
    // In the *parent's* coordinates, because those controls are the bar's siblings, not its
    // children. Measured off foldZone() and not off captionWidth() a second time, so the
    // clickable end and the free end are one number and can never disagree.
    juce::Rectangle<int> contentArea() const
    {
        return getBoundsInParent().withTrimmedLeft(foldZone().getWidth() + 8).withTrimmedRight(8).reduced(0, 4);
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto b = getLocalBounds().toFloat();
        const bool open = getToggleState();
        const auto accent = skin::accentOf(*this).base;

        // Open and folded are deliberately different weights, not the same bar with a
        // different arrow (Owen's ask, 2026-07-27). An open section's header is a solid
        // ruled band that reads as the lid of the thing under it; a folded one is a flat
        // dim strip that reads as a closed drawer. With four of these stacked, the shape of
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

        // Where the fold target ends. The strip paints as one object across the full width,
        // but only its left end answers a click, so without a mark the rest reads as a
        // button that ignores you. It cannot be taught by hover: hitTest means `highlighted`
        // never comes up out there, so the one cue that would show the boundary is exactly
        // the cue the boundary suppresses. Hence a hairline, drawn whether or not the mouse
        // is anywhere near the bar.
        g.setColour(juce::Colours::white.withAlpha(open ? 0.06f : 0.04f));
        g.fillRect(juce::Rectangle<float>((float) foldZone().getRight(), 8.0f, 1.0f, b.getHeight() - 16.0f));

        // Only the fold zone lights, because only the fold zone folds. hitTest already means
        // `highlighted` cannot come up out there, but the fill has to be narrowed with it:
        // a band that lit end to end promised a fold to a click that was about to land on
        // Detach, which is the accident this whole arrangement exists to stop.
        if (highlighted || down)
        {
            g.setColour(juce::Colours::white.withAlpha(down ? 0.09f : 0.05f));
            g.fillRoundedRectangle(foldZone().toFloat().reduced(1.0f), skin::radius);
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
    // little narrower than the text in it, and the longest caption ellipsised.
    // The kerning is also deliberately the same open and folded: it feeds foldZone() and so
    // contentArea() too, and a caption box that changed width with the fold would move the
    // clickable end of the bar and shift every control on it as the section opened.
    static juce::Font captionFont() { return skin::micro(10.0f).withExtraKerningFactor(0.2f); }

    int captionWidth() const
    {
        return juce::jmax(60, (int) std::ceil(captionFont().getStringWidthFloat(title.toUpperCase())) + 8);
    }

    juce::String title;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SectionBar)
};
} // namespace keys
