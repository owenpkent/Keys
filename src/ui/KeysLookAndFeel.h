#pragma once

#include <okstudio/Theme.h>

// The Keys "Obsidian" skin: a dimensional pass over the kit LookAndFeel in the
// spirit of the great soft-synth panels (Serum's discipline: near-black neutral
// chrome, one glowing accent, machined knobs, micro-caps labels). Everything is
// vector-drawn (gradients + layered strokes, no images, no OpenGL) so it scales
// with the resizable editor and stays cheap on the message thread.
//
// This lives in Keys, not the kit, on purpose: it restyles the shared widgets for
// this product only, so Undertow/Beatform keep their current look until the line
// decides to adopt it. Structure mirrors the kit LookAndFeel so promotion is a
// file move plus a palette parameter. The mouse-only contract is untouched: hit
// targets, interaction, and layout are exactly the kit's; only painting changes.
namespace keys
{
namespace skin
{
    // Chrome (Octavium-neutral charcoal, slightly cool, never blue).
    const juce::Colour bgTop      { 0xff17181c };
    const juce::Colour bgBot      { 0xff0e0f12 };
    const juce::Colour headerTop  { 0xff1c1f24 };
    const juce::Colour headerBot  { 0xff16181d };
    const juce::Colour panel      { 0xff1a1c21 };  // raised module strip
    const juce::Colour well       { 0xff101216 };  // inset grooves + value wells
    const juce::Colour control    { 0xff262a31 };  // raised control top
    const juce::Colour controlBot { 0xff1f2227 };

    // The one accent: the OK Studio cyan, with a hot core and a deep shade for
    // gradient ends. Every lit state on every surface uses this family.
    const juce::Colour accent     = okstudio::theme::accent;      // 0xff35c4d7
    const juce::Colour accentHot  = okstudio::theme::accentSoft;  // 0xff8fe8f2
    const juce::Colour accentDeep { 0xff1b8496 };

    const juce::Colour text      { 0xffe9ecf0 };
    const juce::Colour textDim   { 0xff8a919c };
    const juce::Colour textFaint { 0xff5a6068 };

    constexpr float radius = 6.0f;       // controls
    constexpr float panelRadius = 8.0f;  // panels / modules

    // Segoe UI keeps the panel crisp on Windows (the shipping target) and falls
    // back to the platform sans elsewhere; nothing is embedded.
    inline juce::Font ui(float height)
    {
        return juce::Font(juce::FontOptions("Segoe UI", height, juce::Font::plain));
    }
    inline juce::Font uiSemi(float height)
    {
        return juce::Font(juce::FontOptions("Segoe UI Semibold", height, juce::Font::plain));
    }
    // Micro-caps section labels; callers pass uppercase text.
    inline juce::Font micro(float height = 10.0f)
    {
        return uiSemi(height).withExtraKerningFactor(0.08f);
    }

    // A raised chip: soft vertical gradient, dark seat line, 1 px top catch-light.
    void raisedFill(juce::Graphics&, juce::Rectangle<float>, float cornerRadius,
                    juce::Colour top, juce::Colour bottom, bool topHighlight = true);

    // Two-pass accent halo around a rounded rect (tight bright pass + wide soft pass).
    void glowRect(juce::Graphics&, juce::Rectangle<float>, float cornerRadius,
                  juce::Colour colour, float strength = 1.0f);

    // A lit metal ball thumb: drop shadow, top-lit sphere, specular dot.
    void ballThumb(juce::Graphics&, juce::Point<float> centre, float radius);
} // namespace skin

class KeysLookAndFeel : public okstudio::theme::LookAndFeel
{
public:
    KeysLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                          float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle,
                          juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX,
                      int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    // Standalone window chrome (title bar + window buttons). Only the standalone
    // wrapper ever shows these; in a DAW the host owns the window.
    void drawDocumentWindowTitleBar(juce::DocumentWindow&, juce::Graphics&, int w, int h,
                                    int titleSpaceX, int titleSpaceW, const juce::Image* icon,
                                    bool drawTitleTextOnLeft) override;
    juce::Button* createDocumentWindowButton(int buttonType) override;

    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator,
                           bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
};
} // namespace keys
