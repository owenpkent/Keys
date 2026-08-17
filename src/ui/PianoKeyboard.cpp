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
    g.setGradientFill({ juce::Colour(0xff121317), 0.0f, 0.0f,
                        juce::Colour(0xff0c0d10), 0.0f, full.getBottom(), false });
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
    const auto external = externallySounding();
    const auto stateOf = [this, &external](int drawn) -> State
    {
        if (pressed.count(drawn))                           return State::active;
        if (latched.count(drawn) || sustained.count(drawn)) return State::held;
        if (external.count(drawn) > 0)                      return State::held;
        return State::normal;
    };
    const auto vGrad = [](juce::Colour a, juce::Colour b, float top, float bot)
    { return juce::ColourGradient(a, 0.0f, top, b, 0.0f, bot, false); };

    // ---- White keys (drawn first; blacks sit on top) ----
    for (const auto& k : keys)
    {
        if (k.black)
            continue;
        const auto b = k.bounds;
        const float top = b.getY(), bot = b.getBottom();
        const State s = stateOf(k.note);
        const bool lit = s != State::normal;
        const bool dim = ! lit && ! inScale(k.note);

        juce::Path key;
        key.addRoundedRectangle(b.getX(), top, b.getWidth(), b.getHeight(),
                                2.5f, 2.5f, false, false, true, true);

        // Ivory body, top-lit; lit keys go full accent.
        juce::ColourGradient grad = vGrad(juce::Colour(0xfff4f6f8), juce::Colour(0xffd2d6db), top, bot);
        if (s == State::active)      grad = vGrad(juce::Colour(0xff8cebf7), juce::Colour(0xff1fa5ba), top, bot);
        else if (s == State::held)   grad = vGrad(juce::Colour(0xff59c9da), juce::Colour(0xff16808f), top, bot);
        else if (dim)                grad = vGrad(juce::Colour(0xffc9cdd4), juce::Colour(0xff9ea4ad), top, bot);
        else                         grad.addColour(0.55, juce::Colour(0xffe9ecef));
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
            juce::Colour lipTop { 0xffdcdfe4 }, lipBot { 0xffc4c8cf };
            if (s == State::active)      { lipTop = juce::Colour(0xff2ab6cb); lipBot = juce::Colour(0xff1a90a2); }
            else if (s == State::held)   { lipTop = juce::Colour(0xff1f9dae); lipBot = juce::Colour(0xff137886); }
            else if (dim)                { lipTop = juce::Colour(0xffb2b7bf); lipBot = juce::Colour(0xff9aa0a9); }
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
            g.setColour(ac.base.withAlpha(0.45f));
            g.strokePath(key, juce::PathStrokeType(2.0f));
            g.setColour(ac.base.withAlpha(0.15f));
            g.strokePath(key, juce::PathStrokeType(5.0f));
        }

        if ((((k.note % 12) + 12) % 12) == 0) // subtle C marker for orientation
        {
            g.setColour(lit ? juce::Colour(0xff07272c) : juce::Colour(0xff6a7078));
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
        const State s = stateOf(k.note);
        const bool lit = s != State::normal;
        const bool dim = ! lit && ! inScale(k.note);

        // Two-pass drop shadow onto the whites.
        g.setColour(juce::Colours::black.withAlpha(0.30f));
        g.fillRoundedRectangle(b.translated(1.2f, 2.2f).expanded(1.0f, 0.0f), 3.0f);
        g.setColour(juce::Colours::black.withAlpha(0.18f));
        g.fillRoundedRectangle(b.translated(0.5f, 1.0f).expanded(0.5f, 0.0f), 3.0f);

        juce::ColourGradient grad = vGrad(juce::Colour(0xff33373e), juce::Colour(0xff0b0d0f), top, bot);
        if (s == State::active)      grad = vGrad(juce::Colour(0xff20b0c6), juce::Colour(0xff0c4c57), top, bot);
        else if (s == State::held)   grad = vGrad(juce::Colour(0xff189aad), juce::Colour(0xff0a3d46), top, bot);
        else if (dim)                grad = vGrad(juce::Colour(0xff3c434c), juce::Colour(0xff151920), top, bot);
        else                         grad.addColour(0.5, juce::Colour(0xff191c20));
        g.setGradientFill(grad);
        g.fillRoundedRectangle(b, 3.0f);

        // The playing surface sits on top and ends in a catch-light edge — the
        // step that makes the key read as a solid block instead of a flat shape.
        const float faceBot = top + b.getHeight() * 0.60f;
        const auto face = juce::Rectangle<float>(b.getX() + 1.6f, top + 1.0f,
                                                 b.getWidth() - 3.2f, faceBot - top - 1.0f);
        juce::Colour faceTop { 0xff3f444c }, faceLow { 0xff23262b };
        if (s == State::active)      { faceTop = juce::Colour(0xff4fd4e6); faceLow = juce::Colour(0xff1793a6); }
        else if (s == State::held)   { faceTop = juce::Colour(0xff2fb4c7); faceLow = juce::Colour(0xff0f7280); }
        else if (dim)                { faceTop = juce::Colour(0xff4a515b); faceLow = juce::Colour(0xff2a3037); }
        g.setGradientFill(vGrad(faceTop, faceLow, face.getY(), face.getBottom()));
        g.fillRoundedRectangle(face, 2.0f);
        g.setColour(lit ? ac.hot.withAlpha(0.55f) : juce::Colours::white.withAlpha(0.10f));
        g.fillRect(face.getX() + 1.0f, face.getBottom(), face.getWidth() - 2.0f, 1.5f);

        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawRoundedRectangle(b, 3.0f, 1.0f);

        if (lit)
        {
            g.setColour(ac.base.withAlpha(0.35f));
            g.drawRoundedRectangle(b.expanded(1.0f), 4.0f, 2.0f);
            g.setColour(ac.base.withAlpha(0.12f));
            g.drawRoundedRectangle(b.expanded(2.5f), 5.0f, 4.0f);
        }
    }

    // Fallboard rail, the cyan felt strip (the skin's signature detail), and the
    // ambient shadow the board casts down the keys.
    g.setColour(juce::Colour(0xff0a0b0d));
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
