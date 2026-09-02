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

    // The mod/pitch wheel's groove and grab bar (KeysEditor::WheelLookAndFeel::drawLinearSlider
    // in PluginEditor.cpp). Hardware-wheel chrome rather than a panel control - a deeper well
    // than `well` and a differently-lit bar than `control` - so it gets its own pair rather than
    // being bent onto either.
    const juce::Colour wheelGrooveTop { 0xff0c0e11 };
    const juce::Colour wheelGrooveBot { 0xff16191d };
    const juce::Colour wheelThumbTop  { 0xff3f444c };
    const juce::Colour wheelThumbBot  { 0xff22252a };

    // The chord card's raised face: an unfilled/unpressed pad, tray card or library row, all
    // drawn with skin::raisedFill(). Was the same literal pair typed out at four call sites.
    const juce::Colour cardFace    { 0xff272b32 };
    const juce::Colour cardFaceBot { 0xff1e2126 };

    // A small floating surface tracking the pointer: the Draw grid's drag-value readout
    // (LaneGrid). The same byte value KeysLookAndFeel.cpp already uses for the tooltip and
    // popup menu backgrounds, kept as its own token rather than nudged onto cardFaceBot (one
    // bit off in the blue channel) and quietly changing what either paints.
    const juce::Colour floatingBg { 0xff1e2127 };

    // Armed REC. The one red in Keys, and deliberately *not* part of the Accent family below:
    // the accent is per instance and this must not be, because "recording" has to read the same
    // on the pad track and the bass track. A token rather than a literal in PluginEditor.cpp,
    // which is the standing rule for every colour in this product.
    const juce::Colour recordLit  { 0xffb0343c };

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

    // One colour per arp line (2026-08-19, Owen: "I want 4 arps. and each one should have a
    // color"). Fixed, not theme-following: the point is telling the four lines apart, and a
    // palette that moved with the theme swatch could put the theme's own colour on the wrong
    // line. A is the OK Studio cyan so the line Keys has always had keeps the colour it has
    // always worn; B, C and D reuse hexes from accentChoices() so nothing new needs tuning.
    // Worn by the macro card's frame and caption, the bar's letter switch and the Draw grid's
    // playhead - the marks that say *which line*, never the whole control set, so the one-cyan
    // skin law bends here rather than breaks.
    inline Accent lineAccent(int line)
    {
        switch (((line % 4) + 4) % 4)
        {
            case 1:  return derive(juce::Colour(0xffd7459f)); // B: magenta
            case 2:  return derive(juce::Colour(0xffd7a635)); // C: amber
            case 3:  return derive(juce::Colour(0xff8fd735)); // D: lime
            default: return cyanAccent;                       // A: the accent Keys shipped with
        }
    }

    // The four gradient stops a keybed key is painted with when it is lit - one set for the
    // key body, one for its front lip, and two more for a black key's body and raised face.
    // (2026-08-22, Owen: "new branch for each arp to play different colors on the keyboard" -
    // the keys the arp is playing wear the colour of the line playing them.)
    //
    // These were sixteen hard-coded cyan hexes in PianoKeyboard::paint, which is exactly the
    // per-file chrome the skin rule forbids, and they had to leave that file for a second
    // colour to be possible at all.
    //
    // **They hang off Accent rather than being four functions that each re-sniff for cyan.**
    // The first cut was `keyLit(a)`, `keyLitLip(a)` ... each opening with the identical
    // `if (a.base == cyanAccent.base)` branch: four copies of one decision, four chances to get
    // the next one wrong, and a function guessing something its caller already knows. Carrying
    // the sets on the Accent means the branch happens **once**, where an Accent is made -
    // stated outright in `cyanAccent`, filled in by `derive()` for everything else.
    struct KeyLit
    {
        juce::Colour activeTop, activeBot, heldTop, heldBot;
    };

    struct KeyLitSet
    {
        KeyLit body, lip, black, blackFace;
    };

    // Cyan's are the values the keybed was tuned against and are kept **byte for byte** rather
    // than re-derived - the same reasoning `cyanAccent` itself is written down rather than
    // derived from a hue. Line A is the line Keys has always had, and every non-arp key still
    // wears these too, so a wrong digit here is visible on every press in the product.
    inline KeyLitSet cyanKeyLit()
    {
        return { { juce::Colour(0xff8cebf7), juce::Colour(0xff1fa5ba),    // body
                   juce::Colour(0xff59c9da), juce::Colour(0xff16808f) },
                 { juce::Colour(0xff2ab6cb), juce::Colour(0xff1a90a2),    // lip
                   juce::Colour(0xff1f9dae), juce::Colour(0xff137886) },
                 { juce::Colour(0xff20b0c6), juce::Colour(0xff0c4c57),    // black body
                   juce::Colour(0xff189aad), juce::Colour(0xff0a3d46) },
                 { juce::Colour(0xff4fd4e6), juce::Colour(0xff1793a6),    // black face
                   juce::Colour(0xff2fb4c7), juce::Colour(0xff0f7280) } };
    }

    // Every other accent sits in the same relationship to its base that cyan's do to theirs.
    inline KeyLitSet deriveKeyLit(juce::Colour base, juce::Colour deep)
    {
        return { { base.brighter(0.55f), base.darker(0.25f),
                   base.brighter(0.20f), deep },
                 { base.darker(0.10f), base.darker(0.35f),
                   base.darker(0.25f), deep.darker(0.15f) },
                 { base.darker(0.05f), deep.darker(0.45f),
                   base.darker(0.20f), deep.darker(0.55f) },
                 { base.brighter(0.35f), base.darker(0.15f),
                   base.brighter(0.10f), deep.darker(0.10f) } };
    }

    // The gradients a lit key is drawn with: an arp line's own colour, or - for **everything
    // that is not an arp line** - the cyan the keybed has always used.
    //
    // That -1 case is load-bearing and was wrong for a day. The first cut derived these from
    // the *theme* accent, so on a non-cyan swatch your own presses changed colour: a real
    // visual change nobody asked for, and one the changelog flatly denied ("keep the theme's
    // accent exactly as before"). Before the per-line colours these gradients were hard-coded
    // cyan whatever the theme said - only the glow strokes followed the accent, and they still
    // do - so cyan is what "exactly as before" actually means here.
    //
    // The cost, stated rather than hidden: on the default cyan swatch a chord pad's key is the
    // same colour as line A's, because line A *is* that cyan. B, C and D are unambiguous. A
    // colour therefore means "an arp line, or the keybed's own" rather than "an arp line".
    inline KeyLitSet keyLitFor(int line)
    {
        if (line < 0)
            return cyanKeyLit();
        const auto a = lineAccent(line);
        return a.base == cyanAccent.base ? cyanKeyLit() : deriveKeyLit(a.base, a.deep);
    }

    // Keybed. Everything PianoKeyboard::paint draws that is not a lit gradient (those are
    // KeyLitSet above) or the theme accent: the instrument body behind the keys, the ivory and
    // ebony key colours at rest and dimmed (out of Scale Lock), the front lip bevel, the C
    // marker ink, and the fallboard rail. These were twenty-odd hex literals typed straight
    // into that file; moved here byte for byte rather than re-derived, so the paint is
    // unchanged.
    const juce::Colour keybedBodyTop { 0xff121317 };
    const juce::Colour keybedBodyBot { 0xff0c0d10 };

    const juce::Colour keyIvoryTop       { 0xfff4f6f8 };
    const juce::Colour keyIvoryBot       { 0xffd2d6db };
    const juce::Colour keyIvoryHighlight { 0xffe9ecef };  // mid-gradient stop, resting key
    const juce::Colour keyIvoryDimTop    { 0xffc9cdd4 };
    const juce::Colour keyIvoryDimBot    { 0xff9ea4ad };

    const juce::Colour keyLipTop    { 0xffdcdfe4 };
    const juce::Colour keyLipBot    { 0xffc4c8cf };
    const juce::Colour keyLipDimTop { 0xffb2b7bf };
    const juce::Colour keyLipDimBot { 0xff9aa0a9 };

    const juce::Colour keyMarkerInkLit { 0xff07272c };  // C marker text on a lit key
    const juce::Colour keyMarkerInk    { 0xff6a7078 };  // C marker text on a resting key

    const juce::Colour keyBlackTop       { 0xff33373e };
    const juce::Colour keyBlackBot       { 0xff0b0d0f };
    const juce::Colour keyBlackHighlight { 0xff191c20 };  // mid-gradient stop, resting key
    const juce::Colour keyBlackDimTop    { 0xff3c434c };
    const juce::Colour keyBlackDimBot    { 0xff151920 };

    const juce::Colour keyBlackFaceTop    { 0xff3f444c };
    const juce::Colour keyBlackFaceBot    { 0xff23262b };
    const juce::Colour keyBlackFaceDimTop { 0xff4a515b };
    const juce::Colour keyBlackFaceDimBot { 0xff2a3037 };

    const juce::Colour fallboardRail { 0xff0a0b0d };

    // Brightened on 2026-08-01 (Owen: "hard to read some text. too dark"). `textDim` was
    // 0xff8a919c and `textFaint` 0xff5a6068, which are fine as *shades* and were chosen looking
    // at them next to `text`. That is the wrong comparison: almost everything wearing them is
    // 9 to 11 px uppercase with letter spacing - the section captions, the note list under every
    // chord name - and small letterforms need far more contrast than large ones to read at the
    // same effort. Judge these against the background at the size they are actually used, not
    // against each other. `text` is unchanged; it was never the problem.
    const juce::Colour text      { 0xffe9ecf0 };
    const juce::Colour textDim   { 0xffb4bac4 };
    const juce::Colour textFaint { 0xff8a919c };

    constexpr float radius = 6.0f;       // controls

    // A Slider component property: the proportion (0..1) its rotary arc should be lit *from*,
    // instead of from zero or from a bipolar centre. keys::RangeKnob sets it so the lit
    // stretch is its range. Named here rather than spelled out at both ends, since a typo in
    // one of two string literals would fail silently by simply never matching.
    inline const juce::Identifier arcFromProperty { "okArcFrom" };
    // Its twin: where the lit arc *ends*, instead of at the value. RangeKnob sets both since
    // the band became symmetric around the face (2026-08-19, Owen: "should be equal from
    // center") - the arc has to reach past the pointer on the high side, which no end-at-value
    // arc can.
    inline const juce::Identifier arcToProperty { "okArcTo" };
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

    // The roman numeral in a chord card's top-left corner (2026-08-18). Shared by the chord pads
    // and the generator's tray because the two are the same card read at two moments - the chord
    // you kept and the chord you are trying - and a numeral that sits differently on each would
    // say they were different things. Draws nothing at all for an empty string, which is what
    // `numerals::forChord` answers for a chord whose degree it cannot resolve.
    //
    // Top-left is the one corner a card has left: the lock dot owns the top-right and the arp
    // line's letter the bottom-right. `ink` is the card's own text colour, so a lit card gets the
    // dark-on-accent ink the rest of its text does.
    void numeralBadge(juce::Graphics&, juce::Rectangle<float> card, const juce::String& numeral,
                      juce::Colour ink);
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
    // Must agree with drawPopupMenuItem's gutters, or JUCE sizes the menu to the text
    // alone and our own inset then clips it. This is what was truncating "Magenta".
    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardHeight,
                                   int& idealWidth, int& idealHeight) override;
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
