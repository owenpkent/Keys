#include "KeysLookAndFeel.h"

namespace keys
{
namespace skin
{
    void raisedFill(juce::Graphics& g, juce::Rectangle<float> r, float corner,
                    juce::Colour top, juce::Colour bottom, bool topHighlight)
    {
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawRoundedRectangle(r.expanded(0.5f), corner + 0.5f, 1.0f);
        g.setGradientFill({ top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false });
        g.fillRoundedRectangle(r, corner);
        if (topHighlight)
        {
            g.setColour(juce::Colours::white.withAlpha(0.055f));
            g.fillRoundedRectangle(r.withHeight(1.5f).reduced(corner * 0.5f, 0.0f), 0.75f);
        }
    }

    void glowRect(juce::Graphics& g, juce::Rectangle<float> r, float corner,
                  juce::Colour colour, float strength)
    {
        g.setColour(colour.withAlpha(0.14f * strength));
        g.drawRoundedRectangle(r.expanded(2.0f), corner + 2.0f, 3.5f);
        g.setColour(colour.withAlpha(0.45f * strength));
        g.drawRoundedRectangle(r.expanded(0.5f), corner + 0.5f, 1.2f);
    }

    void ballThumb(juce::Graphics& g, juce::Point<float> c, float r)
    {
        g.setColour(juce::Colours::black.withAlpha(0.30f));
        g.fillEllipse(juce::Rectangle<float>(r * 2.1f, r * 2.1f).withCentre(c.translated(0.0f, 1.5f)));

        juce::ColourGradient body(juce::Colour(0xff474c55), c.x - r * 0.35f, c.y - r * 0.45f,
                                  juce::Colour(0xff1e2126), c.x + r * 0.6f, c.y + r, true);
        g.setGradientFill(body);
        g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c));

        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c), 1.0f);

        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.fillEllipse(juce::Rectangle<float>(r * 0.9f, r * 0.55f)
                          .withCentre({ c.x - r * 0.25f, c.y - r * 0.45f }));
    }
} // namespace skin

KeysLookAndFeel::KeysLookAndFeel()
{
    using namespace juce;
    setColour(ResizableWindow::backgroundColourId, skin::bgBot);
    setColour(Label::textColourId, skin::text);
    setColour(Label::outlineColourId, Colours::transparentBlack); // V4 default draws a light box

    setColour(ComboBox::backgroundColourId, skin::control);
    setColour(ComboBox::textColourId, skin::text);
    setColour(ComboBox::outlineColourId, Colours::transparentBlack);
    setColour(ComboBox::arrowColourId, skin::textDim);

    setColour(PopupMenu::backgroundColourId, Colour(0xff1e2127));
    setColour(PopupMenu::textColourId, skin::text);
    setColour(PopupMenu::highlightedBackgroundColourId, skin::accent.withAlpha(0.15f));
    setColour(PopupMenu::highlightedTextColourId, Colour(0xffeafcff));

    setColour(TextButton::buttonColourId, skin::control);
    setColour(TextButton::buttonOnColourId, skin::control.interpolatedWith(skin::accent, 0.32f));
    setColour(TextButton::textColourOffId, skin::text);
    setColour(TextButton::textColourOnId, Colour(0xffeafcff));

    setColour(Slider::backgroundColourId, skin::well);
    setColour(Slider::trackColourId, skin::accent);
    setColour(Slider::thumbColourId, skin::accentHot);
    setColour(Slider::textBoxTextColourId, skin::text);
    setColour(Slider::textBoxBackgroundColourId, Colours::transparentBlack); // values float
    setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);

    setColour(TextEditor::backgroundColourId, skin::well);
    setColour(TextEditor::textColourId, skin::text);
    setColour(TextEditor::highlightColourId, skin::accent.withAlpha(0.35f));
    setColour(TextEditor::focusedOutlineColourId, skin::accent.withAlpha(0.6f));
    setColour(TextEditor::outlineColourId, Colours::transparentBlack);

    setColour(ToggleButton::textColourId, skin::text);
    setColour(ToggleButton::tickColourId, skin::accent);
    setColour(ToggleButton::tickDisabledColourId, skin::textDim);
}

// A machined knob: shadowed cap with a top-lit face and specular, a dark groove
// arc, and a glowing accent value arc with a hot core. Reads at 48 px and up.
void KeysLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                       juce::Slider& slider)
{
    const bool enabled = slider.isEnabled();
    if (! enabled)
        g.beginTransparencyLayer(0.45f);

    const auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 3.0f;
    const float lw = juce::jmax(2.5f, radius * 0.15f);
    const float arcR = radius - lw * 0.5f;
    const float capR = arcR - lw * 1.6f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Groove.
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(skin::well);
    g.strokePath(track, { lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    // Value arc: halo, body, hot core.
    if (sliderPos > 0.001f)
    {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, angle, true);
        g.setColour(skin::accent.withAlpha(0.16f));
        g.strokePath(value, { lw * 2.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        g.setColour(skin::accent.withAlpha(0.55f));
        g.strokePath(value, { lw * 1.15f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        g.setColour(skin::accentHot);
        g.strokePath(value, { lw * 0.55f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }

    // Cap shadow, rim, face.
    {
        juce::ColourGradient shadow(juce::Colours::black.withAlpha(0.4f),
                                    centre.x, centre.y + capR * 0.18f,
                                    juce::Colours::transparentBlack,
                                    centre.x, centre.y + capR * 1.5f, true);
        g.setGradientFill(shadow);
        g.fillEllipse(juce::Rectangle<float>(capR * 2.9f, capR * 2.9f)
                          .withCentre(centre.translated(0.0f, capR * 0.16f)));

        const auto rim = juce::Rectangle<float>(capR * 2.0f, capR * 2.0f).withCentre(centre);
        g.setGradientFill({ juce::Colour(0xff363b42), 0.0f, rim.getY(),
                            juce::Colour(0xff15171a), 0.0f, rim.getBottom(), false });
        g.fillEllipse(rim);

        const auto face = rim.reduced(1.6f);
        g.setGradientFill({ juce::Colour(0xff3c4149), 0.0f, face.getY(),
                            juce::Colour(0xff1d2025), 0.0f, face.getBottom(), false });
        g.fillEllipse(face);

        // Specular pool near the top of the face.
        juce::Graphics::ScopedSaveState clip(g);
        juce::Path faceClip;
        faceClip.addEllipse(face);
        g.reduceClipRegion(faceClip);
        g.setGradientFill({ juce::Colours::white.withAlpha(0.11f), 0.0f, face.getY(),
                            juce::Colours::transparentWhite, 0.0f, face.getCentreY(), false });
        g.fillEllipse(face.withHeight(face.getHeight() * 0.55f).expanded(1.0f, 0.0f));
    }
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawEllipse(juce::Rectangle<float>(capR * 2.0f, capR * 2.0f).withCentre(centre), 1.1f);

    // Pointer on the cap, glow under a hot core.
    {
        const float t = juce::jmax(2.2f, capR * 0.16f);
        juce::Path pointer;
        pointer.addRoundedRectangle(-t * 0.5f, -capR * 0.86f, t, capR * 0.58f, t * 0.5f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centre));
        g.setColour(skin::accent.withAlpha(0.30f));
        g.strokePath(pointer, juce::PathStrokeType(t * 1.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.setColour(skin::accentHot);
        g.fillPath(pointer);
    }

    if (! enabled)
        g.endTransparencyLayer();
}

// Horizontal sliders (velocity, humanize range, timing, strum): an inset groove,
// an accent fill with a soft halo, and lit ball thumbs. Other styles fall back.
void KeysLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float minSliderPos, float maxSliderPos,
                                       juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::TwoValueHorizontal)
    {
        okstudio::theme::LookAndFeel::drawLinearSlider(g, x, y, width, height, sliderPos,
                                                       minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const bool enabled = slider.isEnabled();
    if (! enabled)
        g.beginTransparencyLayer(0.4f);

    const float cy = (float) y + (float) height * 0.5f;
    const float thumbR = 7.5f;
    const auto track = juce::Rectangle<float>((float) x + thumbR, cy - 3.0f,
                                              (float) width - thumbR * 2.0f, 6.0f);

    // Groove with an inner top shadow.
    g.setColour(skin::well);
    g.fillRoundedRectangle(track, 3.0f);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(track.withHeight(1.5f).reduced(2.0f, 0.0f), 0.75f);
    g.setColour(juce::Colours::white.withAlpha(0.04f));
    g.fillRoundedRectangle(track.withY(track.getBottom() - 1.0f).withHeight(1.0f).reduced(2.0f, 0.0f), 0.5f);

    const bool twoValue = style == juce::Slider::TwoValueHorizontal;
    const float fillL = twoValue ? minSliderPos : track.getX();
    const float fillR = twoValue ? maxSliderPos : sliderPos;
    if (fillR > fillL + 0.5f)
    {
        const auto fill = juce::Rectangle<float>(fillL, track.getY(), fillR - fillL, track.getHeight());
        g.setGradientFill({ skin::accentDeep, fill.getX(), 0.0f, skin::accent, fill.getRight(), 0.0f, false });
        g.fillRoundedRectangle(fill, 3.0f);
        g.setColour(skin::accent.withAlpha(0.18f));
        g.drawRoundedRectangle(fill.expanded(1.5f), 4.0f, 2.5f);
    }

    if (twoValue)
    {
        skin::ballThumb(g, { minSliderPos, cy }, thumbR);
        skin::ballThumb(g, { maxSliderPos, cy }, thumbR);
    }
    else
    {
        skin::ballThumb(g, { sliderPos, cy }, thumbR);
    }

    if (! enabled)
        g.endTransparencyLayer();
}

juce::Label* KeysLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = okstudio::theme::LookAndFeel::createSliderTextBox(slider);
    label->setFont(skin::ui(13.5f));
    // Values float on the panel; the base class may have baked stale colours from
    // whichever LookAndFeel was current when the slider was first configured.
    label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::textColourId, skin::text);
    label->setColour(juce::TextEditor::backgroundColourId, skin::well);
    label->setColour(juce::TextEditor::outlineColourId, skin::accent.withAlpha(0.4f));
    label->setColour(juce::TextEditor::textColourId, skin::text);
    label->setColour(juce::TextEditor::highlightColourId, skin::accent.withAlpha(0.35f));
    return label;
}

void KeysLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                           const juce::Colour& backgroundColour,
                                           bool highlighted, bool down)
{
    const auto r = button.getLocalBounds().toFloat().reduced(0.5f);
    const float dim = button.isEnabled() ? 1.0f : 0.45f;
    if (dim < 1.0f)
        g.beginTransparencyLayer(dim);

    // backgroundColour arrives resolved (buttonColourId / buttonOnColourId, plus any
    // per-button override like the panic flash or the updater green), so shade from it.
    auto base = backgroundColour;
    if (down)
        base = base.darker(0.25f);
    else if (highlighted)
        base = base.brighter(0.12f);

    skin::raisedFill(g, r, skin::radius, base.brighter(down ? 0.0f : 0.05f),
                     base.darker(down ? 0.05f : 0.16f), ! down);

    if (button.getToggleState())
        skin::glowRect(g, r, skin::radius, skin::accent);

    if (dim < 1.0f)
        g.endTransparencyLayer();
}

juce::Font KeysLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return skin::uiSemi(juce::jmin(14.0f, (float) buttonHeight * 0.45f));
}

// Checkbox toggles (Scale Lock, Sustain, Latch, ...): an inset well that fills
// with glowing accent and a white check when on.
void KeysLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                       bool highlighted, bool)
{
    const float dim = button.isEnabled() ? 1.0f : 0.45f;
    if (dim < 1.0f)
        g.beginTransparencyLayer(dim);

    const auto bounds = button.getLocalBounds().toFloat();
    const float boxS = 20.0f;
    const auto box = juce::Rectangle<float>(boxS, boxS)
                         .withCentre({ bounds.getX() + boxS * 0.5f + 1.0f, bounds.getCentreY() });
    const bool on = button.getToggleState();

    if (on)
    {
        g.setGradientFill({ skin::accentHot, 0.0f, box.getY(), skin::accent, 0.0f, box.getBottom(), false });
        g.fillRoundedRectangle(box, 5.0f);
        skin::glowRect(g, box, 5.0f, skin::accent, 0.9f);

        juce::Path check;
        check.startNewSubPath(box.getX() + boxS * 0.26f, box.getY() + boxS * 0.54f);
        check.lineTo(box.getX() + boxS * 0.44f, box.getY() + boxS * 0.72f);
        check.lineTo(box.getX() + boxS * 0.76f, box.getY() + boxS * 0.30f);
        g.setColour(juce::Colour(0xff07272c));
        g.strokePath(check, { 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }
    else
    {
        g.setColour(skin::well);
        g.fillRoundedRectangle(box, 5.0f);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillRoundedRectangle(box.withHeight(1.5f).reduced(3.0f, 0.0f), 0.75f);
        g.setColour(juce::Colours::white.withAlpha(highlighted ? 0.16f : 0.07f));
        g.drawRoundedRectangle(box, 5.0f, 1.0f);
    }

    g.setColour(skin::text.withAlpha(on ? 1.0f : 0.88f));
    g.setFont(skin::uiSemi(13.0f));
    g.drawText(button.getButtonText(),
               button.getLocalBounds().withTrimmedLeft((int) boxS + 9),
               juce::Justification::centredLeft, true);

    if (dim < 1.0f)
        g.endTransparencyLayer();
}

void KeysLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                   int, int, int, int, juce::ComboBox& box)
{
    const float dim = box.isEnabled() ? 1.0f : 0.45f;
    if (dim < 1.0f)
        g.beginTransparencyLayer(dim);

    const auto r = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height).reduced(0.5f);
    const auto base = box.findColour(juce::ComboBox::backgroundColourId);
    skin::raisedFill(g, r, skin::radius, base.brighter(0.05f), base.darker(0.16f));

    if (box.isPopupActive())
        skin::glowRect(g, r, skin::radius, skin::accent, 0.7f);

    // Chevron.
    const float cx = (float) width - 13.0f, cy = (float) height * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath(cx - 4.5f, cy - 2.5f);
    chevron.lineTo(cx, cy + 2.5f);
    chevron.lineTo(cx + 4.5f, cy - 2.5f);
    g.setColour(box.isPopupActive() ? skin::accent : skin::textDim);
    g.strokePath(chevron, { 1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    if (dim < 1.0f)
        g.endTransparencyLayer();
}

juce::Font KeysLookAndFeel::getComboBoxFont(juce::ComboBox&) { return skin::ui(14.0f); }

void KeysLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(10, 1, box.getWidth() - 32, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void KeysLookAndFeel::drawDocumentWindowTitleBar(juce::DocumentWindow& window, juce::Graphics& g,
                                                 int w, int h, int titleSpaceX, int titleSpaceW,
                                                 const juce::Image*, bool drawTitleTextOnLeft)
{
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, (float) w, (float) h);
    g.setGradientFill({ juce::Colour(0xff15171b), 0.0f, 0.0f,
                        juce::Colour(0xff101216), 0.0f, r.getBottom(), false });
    g.fillRect(r);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRect(0.0f, r.getBottom() - 1.0f, r.getWidth(), 1.0f);

    // Window name in the wordmark's voice: tracked caps, dimmed when unfocused.
    g.setColour(skin::text.withAlpha(window.isActiveWindow() ? 0.92f : 0.5f));
    g.setFont(skin::uiSemi(14.0f).withExtraKerningFactor(0.10f));
    g.drawText(window.getName().toUpperCase(),
               juce::Rectangle<int>(titleSpaceX, 0, titleSpaceW, h),
               drawTitleTextOnLeft ? juce::Justification::centredLeft
                                   : juce::Justification::centred,
               true);
}

juce::Button* KeysLookAndFeel::createDocumentWindowButton(int buttonType)
{
    // Thin-line glyphs on an invisible pad; the pad (button bounds) is the full
    // title-bar-height square, so the mouse target stays comfortably large.
    const auto stroked = [](const juce::Path& p)
    {
        juce::Path out;
        juce::PathStrokeType(1.9f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded)
            .createStrokedPath(out, p);
        return out;
    };

    juce::ShapeButton* button = nullptr;
    if (buttonType == juce::DocumentWindow::closeButton)
    {
        juce::Path x;
        x.startNewSubPath(0.0f, 0.0f);
        x.lineTo(10.0f, 10.0f);
        x.startNewSubPath(10.0f, 0.0f);
        x.lineTo(0.0f, 10.0f);
        button = new juce::ShapeButton("close", skin::textDim,
                                       juce::Colour(0xffff8a80), juce::Colour(0xffe25d5d));
        button->setShape(stroked(x), false, true, false);
    }
    else if (buttonType == juce::DocumentWindow::minimiseButton)
    {
        juce::Path dash;
        dash.startNewSubPath(0.0f, 5.0f);
        dash.lineTo(10.0f, 5.0f);
        button = new juce::ShapeButton("minimise", skin::textDim, skin::accentHot, skin::accent);
        button->setShape(stroked(dash), false, true, false);
    }
    else
    {
        juce::Path square;
        square.addRoundedRectangle(0.0f, 0.0f, 10.0f, 10.0f, 2.0f);
        button = new juce::ShapeButton("maximise", skin::textDim, skin::accentHot, skin::accent);
        button->setShape(stroked(square), false, true, false);
    }
    button->setBorderSize(juce::BorderSize<int>(13));
    return button;
}

void KeysLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height);
    g.fillAll(findColour(juce::PopupMenu::backgroundColourId));
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawRect(r, 1.0f);
}

void KeysLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                        bool isSeparator, bool isActive, bool isHighlighted,
                                        bool isTicked, bool hasSubMenu, const juce::String& text,
                                        const juce::String& shortcutKeyText, const juce::Drawable*,
                                        const juce::Colour* textColourToUse)
{
    if (isSeparator)
    {
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.fillRect(area.reduced(8, 0).withHeight(1).withY(area.getCentreY()));
        return;
    }

    const auto r = area.toFloat().reduced(3.0f, 1.0f);
    if (isHighlighted && isActive)
    {
        g.setColour(skin::accent.withAlpha(0.15f));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(skin::accent.withAlpha(0.4f));
        g.drawRoundedRectangle(r, 4.0f, 1.0f);
    }

    if (isTicked)
    {
        g.setColour(skin::accent);
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f)
                          .withCentre({ r.getX() + 10.0f, r.getCentreY() }));
    }

    auto colour = textColourToUse != nullptr ? *textColourToUse
                                             : (isHighlighted ? juce::Colour(0xffeafcff) : skin::text);
    g.setColour(isActive ? colour : skin::textFaint);
    g.setFont(getPopupMenuFont());
    g.drawText(text, area.reduced(26, 0), juce::Justification::centredLeft, true);

    if (hasSubMenu)
    {
        const float cx = (float) area.getRight() - 12.0f, cy = (float) area.getCentreY();
        juce::Path arrow;
        arrow.startNewSubPath(cx - 2.0f, cy - 4.0f);
        arrow.lineTo(cx + 2.5f, cy);
        arrow.lineTo(cx - 2.0f, cy + 4.0f);
        g.setColour(skin::textDim);
        g.strokePath(arrow, { 1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour(skin::textDim);
        g.setFont(skin::ui(12.0f));
        g.drawText(shortcutKeyText, area.reduced(26, 0), juce::Justification::centredRight, true);
    }
}

juce::Font KeysLookAndFeel::getPopupMenuFont() { return skin::ui(13.5f); }
} // namespace keys
