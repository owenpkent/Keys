#include "PianoKeyboard.h"
#include "../NoteMath.h"
#include "KeysLookAndFeel.h"
#include <okstudio/Scales.h>

namespace keys
{
namespace
{
    bool isBlackNote(int n)
    {
        const int pc = ((n % 12) + 12) % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }
} // namespace

PianoKeyboard::PianoKeyboard(KeysProcessor& p) : NoteSurface(p)
{
    // paint() fills the whole component with the instrument-body gradient before it draws a
    // single key, so nothing behind this is ever visible. Saying so stops JUCE repainting the
    // editor's own full-window gradient underneath every time a key lights.
    setOpaque(true);
}

// The keys are in no particular order and there are at most 88 of them, so a scan is cheaper
// than a map that would have to be rebuilt by layoutKeys on every resize.
juce::Rectangle<int> PianoKeyboard::drawnBounds(int drawnNote) const
{
    for (const auto& k : keys)
        if (k.note == drawnNote)
            return k.bounds.getSmallestIntegerContainer();
    return {};
}

void PianoKeyboard::setRange(int newLow, int newCount)
{
    if (newLow == lowNote && newCount == numKeys)
        return;
    lowNote = newLow;
    numKeys = newCount;
    layoutKeys();
    repaint();
}

void PianoKeyboard::setKeyHeightCap(float px)
{
    px = juce::jmax(40.0f, px);
    if (juce::approximatelyEqual(px, keyHeightCap))
        return;
    keyHeightCap = px;
    layoutKeys();
    repaint();
}

void PianoKeyboard::resized()
{
    layoutKeys();
}

void PianoKeyboard::layoutKeys()
{
    keys.clear();
    const auto area = getLocalBounds().toFloat();
    if (area.isEmpty())
        return;

    int whiteCount = 0;
    for (int i = 0; i < numKeys; ++i)
        if (! isBlackNote(lowNote + i))
            ++whiteCount;
    if (whiteCount == 0)
        return;

    const float ww = area.getWidth() / (float) whiteCount;
    // Cap the key height so keys stay piano-proportioned instead of stretching to fill
    // a tall window; any extra height becomes instrument body above, keys anchored low.
    // (185, up from 150: at the default window size the old cap left a wide dead band
    // between the pad strip and the keybed.) setKeyHeightCap lifts it when the keybed
    // is detached, where resizing the window is meant to resize the keys.
    const float wh = juce::jmin(area.getHeight(), keyHeightCap);
    const float yTop = area.getBottom() - wh;
    const float bw = ww * 0.64f; // Octavium proportions: black ~64% of white width.
    const float bh = wh * 0.56f; // Shorter than Octavium's 62%: more of each white key
                                 // is full-width, the accurate zone for mouse-only play.
    keysTop = yTop;

    int whiteIndex = 0;
    for (int i = 0; i < numKeys; ++i)
    {
        const int n = lowNote + i;
        if (isBlackNote(n))
            continue;
        keys.push_back({ n, false, { area.getX() + (float) whiteIndex * ww, yTop, ww, wh } });
        ++whiteIndex;
    }

    // Black keys drawn on top, centred on the boundary with the preceding white key.
    whiteIndex = 0;
    for (int i = 0; i < numKeys; ++i)
    {
        const int n = lowNote + i;
        if (! isBlackNote(n))
        {
            ++whiteIndex;
            continue;
        }
        const float x = area.getX() + (float) whiteIndex * ww - bw * 0.5f;
        keys.push_back({ n, true, { x, yTop, bw, bh } });
    }
}

int PianoKeyboard::drawnAt(juce::Point<float> pos) const
{
    // Black keys are appended last, so a reverse scan checks them first.
    for (auto it = keys.rbegin(); it != keys.rend(); ++it)
        if (it->bounds.contains(pos))
            return it->note;
    return -1;
}

int PianoKeyboard::outputNote(int drawnNote) const
{
    return resolveOutputNote(drawnNote, scaleLock, rootPc, scaleIndex, processor.octaveShift());
}

int PianoKeyboard::drawnForOutputNote(int note) const
{
    // Undo the octave shift; the key must be on the keybed. With Scale Lock on, an
    // out-of-scale recalled tone re-snaps on press, which is what the lock promises.
    const int drawn = note - processor.octaveShift() * 12;
    return (drawn >= lowNote && drawn < lowNote + numKeys) ? drawn : -1;
}

void PianoKeyboard::paint(juce::Graphics& g)
{
    using namespace okstudio;
    const auto full = getLocalBounds().toFloat();
    const auto ac = skin::accentOf(*this); // once per paint, not once per key

    // Instrument body above the keybed.
    g.setGradientFill({ skin::keybedBodyTop, 0.0f, 0.0f,
                        skin::keybedBodyBot, 0.0f, full.getBottom(), false });
    g.fillRect(full);

    const auto inScale = [this](int note)
    { return ! scaleLock || scales::isInScale(note, rootPc, scaleIndex); };

    // Three visual states, matching Octavium: momentary press (hot accent), held
    // via latch/sustain (deeper accent), and resting.
    //
    // A note sounding from somewhere other than this surface (an MCP tool, a chord pad)
    // paints as `held` as well: it is ringing with no finger on it, which is what held
    // already means. Checked last, so a key the user is genuinely pressing still reads
    // as their own gesture.
    enum class State { normal, active, held };
    // Which arp line lit a key travels with the state, because it decides the *colour*: a key
    // the arp is playing wears `skin::lineAccent(line)` rather than the theme accent
    // (2026-08-22, Owen: "each arp to play different colors on the keyboard"). `line` is -1 for
    // every other source - a press, a latch, a chord pad, the MIDI input - all of which keep
    // the theme's own accent exactly as before, which is what stops four colours becoming five.
    struct Lit { State state; int line; };
    const auto external = externallySounding();
    // Hold Visuals During Sustain (2026-08-17, the settings menu; Octavium's own item,
    // wired for the first time - it was never connected to anything there). On, the
    // default and today's behaviour unconditionally before this flag existed, a pedal-held
    // key paints exactly like a latched one. Off, a key caught *only* by the pedal - not
    // pressed, not latched - rests visually while it keeps sounding, so the eye can tell
    // "held by the pedal" from "down right now" even though both are true. This is paint
    // only: `sustained` itself, and therefore what is actually sounding, is untouched.
    const bool holdSustainVisual = processor.layout.holdVisualsOnSustain;
    const auto stateOf = [this, &external, holdSustainVisual](int drawn) -> Lit
    {
        if (pressed.count(drawn))  return { State::active, -1 };
        if (latched.count(drawn)) return { State::held, -1 };
        if (sustained.count(drawn))
            return { holdSustainVisual ? State::held : State::normal, -1 };
        if (const auto it = external.find(drawn); it != external.end())
            return { State::held, it->second };
        return { State::normal, -1 };
    };
    // The **glow strokes** follow the theme accent for a non-arp key, exactly as they always
    // have, and an arp line's own colour otherwise.
    const auto accentFor = [&ac](int line)
    { return line >= 0 ? skin::lineAccent(line) : ac; };
    // The **body gradients** are a different question and the answer is not the same: before
    // the per-line colours these were hard-coded cyan whatever the theme said, so a non-arp key
    // stays cyan here rather than following the swatch. See skin::keyLitFor.
    const auto vGrad = [](juce::Colour a, juce::Colour b, float top, float bot)
    { return juce::ColourGradient(a, 0.0f, top, b, 0.0f, bot, false); };

    // ---- White keys (drawn first; blacks sit on top) ----
    for (const auto& k : keys)
    {
        if (k.black)
            continue;
        const auto b = k.bounds;
        const float top = b.getY(), bot = b.getBottom();
        const auto st = stateOf(k.note);
        const State s = st.state;
        const bool lit = s != State::normal;
        const bool dim = ! lit && ! inScale(k.note);
        juce::Path key;
        key.addRoundedRectangle(b.getX(), top, b.getWidth(), b.getHeight(),
                                2.5f, 2.5f, false, false, true, true);

        // Ivory body, top-lit; lit keys go full accent.
        // Derived only when the key is actually lit: this is the most expensive paint in the
        // window, most keys are not lit, and under a non-cyan accent each set costs sixteen
        // HSB round-trips that would be thrown away.
        const auto ka = lit ? accentFor(st.line) : ac;
        const auto kls = lit ? skin::keyLitFor(st.line) : skin::KeyLitSet {};

        juce::ColourGradient grad = vGrad(skin::keyIvoryTop, skin::keyIvoryBot, top, bot);
        if (s == State::active)      grad = vGrad(kls.body.activeTop, kls.body.activeBot, top, bot);
        else if (s == State::held)   grad = vGrad(kls.body.heldTop, kls.body.heldBot, top, bot);
        else if (dim)                grad = vGrad(skin::keyIvoryDimTop, skin::keyIvoryDimBot, top, bot);
        else                         grad.addColour(0.55, skin::keyIvoryHighlight);
        g.setGradientFill(grad);
        g.fillPath(key);

        // Front lip: the vertical face under the playing surface. It used to be a 10 px
        // band two steps darker than the key with a 30% black line above it, which read
        // as a shadow smeared across the bottom of the keybed rather than as an edge.
        // Now it is a thin bevel — barely darker than the key body, hairline separator —
        // so it still gives the keys a front face without dirtying them.
        const float lipH = juce::jmin(5.0f, b.getHeight() * 0.04f);
        {
            juce::Graphics::ScopedSaveState clip(g);
            g.reduceClipRegion(key);
            juce::Colour lipTop = skin::keyLipTop, lipBot = skin::keyLipBot;
            if (s == State::active)      { lipTop = kls.lip.activeTop; lipBot = kls.lip.activeBot; }
            else if (s == State::held)   { lipTop = kls.lip.heldTop;   lipBot = kls.lip.heldBot; }
            else if (dim)                { lipTop = skin::keyLipDimTop; lipBot = skin::keyLipDimBot; }
            g.setGradientFill(vGrad(lipTop, lipBot, bot - lipH, bot));
            g.fillRect(b.getX(), bot - lipH, b.getWidth(), lipH);
            g.setColour(juce::Colours::black.withAlpha(0.10f));
            g.fillRect(b.getX(), bot - lipH, b.getWidth(), 1.0f);
        }

        // Seams: shadowed gap on the right, a hair of light on the left edge.
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillRect(b.getRight() - 1.0f, top, 1.0f, b.getHeight());
        g.setColour(juce::Colours::white.withAlpha(dim ? 0.10f : 0.25f));
        g.fillRect(b.getX(), top, 1.0f, b.getHeight() - lipH);

        if (lit)
        {
            g.setColour(ka.base.withAlpha(0.45f));
            g.strokePath(key, juce::PathStrokeType(2.0f));
            g.setColour(ka.base.withAlpha(0.15f));
            g.strokePath(key, juce::PathStrokeType(5.0f));
        }

        if ((((k.note % 12) + 12) % 12) == 0) // subtle C marker for orientation
        {
            g.setColour(lit ? skin::keyMarkerInkLit : skin::keyMarkerInk);
            g.setFont(skin::micro(9.5f));
            g.drawText("C" + juce::String(k.note / 12 - 1),
                       b.withTrimmedBottom(lipH + 3.0f).removeFromBottom(12.0f),
                       juce::Justification::centred);
        }
    }

    // ---- Black keys (stepped and glossy, on top) ----
    for (const auto& k : keys)
    {
        if (! k.black)
            continue;
        const auto b = k.bounds;
        const float top = b.getY(), bot = b.getBottom();
        const auto st = stateOf(k.note);
        const State s = st.state;
        const bool lit = s != State::normal;
        const bool dim = ! lit && ! inScale(k.note);
        const auto ka = lit ? accentFor(st.line) : ac;
        const auto kls = lit ? skin::keyLitFor(st.line) : skin::KeyLitSet {};

        // Two-pass drop shadow onto the whites.
        g.setColour(juce::Colours::black.withAlpha(0.30f));
        g.fillRoundedRectangle(b.translated(1.2f, 2.2f).expanded(1.0f, 0.0f), 3.0f);
        g.setColour(juce::Colours::black.withAlpha(0.18f));
        g.fillRoundedRectangle(b.translated(0.5f, 1.0f).expanded(0.5f, 0.0f), 3.0f);

        juce::ColourGradient grad = vGrad(skin::keyBlackTop, skin::keyBlackBot, top, bot);
        if (s == State::active)      grad = vGrad(kls.black.activeTop, kls.black.activeBot, top, bot);
        else if (s == State::held)   grad = vGrad(kls.black.heldTop, kls.black.heldBot, top, bot);
        else if (dim)                grad = vGrad(skin::keyBlackDimTop, skin::keyBlackDimBot, top, bot);
        else                         grad.addColour(0.5, skin::keyBlackHighlight);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(b, 3.0f);

        // The playing surface sits on top and ends in a catch-light edge — the
        // step that makes the key read as a solid block instead of a flat shape.
        const float faceBot = top + b.getHeight() * 0.60f;
        const auto face = juce::Rectangle<float>(b.getX() + 1.6f, top + 1.0f,
                                                 b.getWidth() - 3.2f, faceBot - top - 1.0f);
        juce::Colour faceTop = skin::keyBlackFaceTop, faceLow = skin::keyBlackFaceBot;
        if (s == State::active)      { faceTop = kls.blackFace.activeTop; faceLow = kls.blackFace.activeBot; }
        else if (s == State::held)   { faceTop = kls.blackFace.heldTop;   faceLow = kls.blackFace.heldBot; }
        else if (dim)                { faceTop = skin::keyBlackFaceDimTop; faceLow = skin::keyBlackFaceDimBot; }
        g.setGradientFill(vGrad(faceTop, faceLow, face.getY(), face.getBottom()));
        g.fillRoundedRectangle(face, 2.0f);
        g.setColour(lit ? ka.hot.withAlpha(0.55f) : juce::Colours::white.withAlpha(0.10f));
        g.fillRect(face.getX() + 1.0f, face.getBottom(), face.getWidth() - 2.0f, 1.5f);

        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawRoundedRectangle(b, 3.0f, 1.0f);

        if (lit)
        {
            g.setColour(ka.base.withAlpha(0.35f));
            g.drawRoundedRectangle(b.expanded(1.0f), 4.0f, 2.0f);
            g.setColour(ka.base.withAlpha(0.12f));
            g.drawRoundedRectangle(b.expanded(2.5f), 5.0f, 4.0f);
        }
    }

    // Fallboard rail, the cyan felt strip (the skin's signature detail), and the
    // ambient shadow the board casts down the keys.
    g.setColour(skin::fallboardRail);
    g.fillRect(full.getX(), keysTop, full.getWidth(), 2.0f);
    g.setGradientFill({ ac.base, full.getX(), 0.0f, ac.deep, full.getRight(), 0.0f, false });
    g.fillRect(full.getX(), keysTop + 2.0f, full.getWidth(), 2.5f);
    g.setGradientFill(vGrad(ac.base.withAlpha(0.16f), juce::Colours::transparentBlack,
                            keysTop + 4.5f, keysTop + 13.0f));
    g.fillRect(full.getX(), keysTop + 4.5f, full.getWidth(), 8.5f);
    g.setGradientFill(vGrad(juce::Colours::black.withAlpha(0.45f), juce::Colours::transparentBlack,
                            keysTop + 4.5f, keysTop + 22.0f));
    g.fillRect(full.getX(), keysTop + 4.5f, full.getWidth(), 17.5f);
}
} // namespace keys
