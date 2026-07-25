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

    // The accent: a base, a hot core and a deep shade for gradient ends. Every lit state
    // on every surface uses this family.
    //
    // Keys used to have exactly one, the OK Studio cyan. It is now per instance, because
    // a session has a Keys on the pad track and a Keys on the bass track and they were
    // indistinguishable at a glance. Cyan is still the default and still the line's
    // colour; the rest are there to tell one instance from another.
    //
    // Crucially this is *not* a global. A DAW loads every instance into one process, so a
    // global would repaint every track's Keys at once. The live values hang off each
    // editor's KeysLookAndFeel, and components read them through accentOf() below.
    struct Accent
    {
        juce::Colour base, hot, deep;
    };

    // The default: the OK Studio cyan, with its shipped hot/deep pair kept exact rather
    // than derived, since the whole skin was tuned against these three.
    const Accent cyanAccent { okstudio::theme::accent,      // 0xff35c4d7
                              okstudio::theme::accentSoft,  // 0xff8fe8f2
                              juce::Colour(0xff1b8496) };

    // Everything else is derived from one base, so adding a colour is one line.
    inline Accent derive(juce::Colour base)
    {
        return { base, base.brighter(0.75f), base.darker(0.45f) };
    }

    struct AccentChoice
    {
        const char* name;
        juce::uint32 argb; // 0 = use cyanAccent, which is not derived
    };
    inline const AccentChoice* accentChoices()
    {
        static const AccentChoice table[] = {
            { "Cyan",    0 },
            { "Amber",   0xffd7a635 },
            { "Lime",    0xff8fd735 },
            { "Violet",  0xff9a6cf5 },
            { "Magenta", 0xffd7459f },
            { "Orange",  0xffe0703a },
            { "Rose",    0xffe04a6b },
            { "Ice",     0xff8fb4de },
        };
        return table;
    }
    constexpr int numAccents = 8;

    inline Accent accentAt(int index)
    {
        index = juce::jlimit(0, numAccents - 1, index);
        const auto& choice = accentChoices()[index];
        return choice.argb == 0 ? cyanAccent : derive(juce::Colour(choice.argb));
    }

    // The accent of whichever editor this component belongs to. Resolved through the
    // LookAndFeel chain, which JUCE already walks up to the editor, so a component does
    // not need to know who owns it. Falls back to cyan outside a Keys editor.
    Accent accentOf(const juce::Component&);

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

    // One per editor, so each instance of the plugin wears its own colour. Re-applies the
    // JUCE ColourIds the kit derives from the accent (tick marks, slider tracks, the
    // popup highlight), which are baked at construction and would otherwise stay cyan.
    void setAccent(int index);
    skin::Accent accent() const { return accentColours; }
    int accentIndex() const { return index; }

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

    // Tooltips: JUCE's default is a 13 px font in a box up to 400 px wide, which next to
    // this skin's 10 px micro-caps reads like a different application shouting. Smaller
    // type, tighter padding, narrower wrap.
    juce::Rectangle<int> getTooltipBounds(const juce::String& tip, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;

    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator,
                           bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;

private:
    skin::Accent accentColours = skin::cyanAccent;
    int index = 0;
};
} // namespace keys
