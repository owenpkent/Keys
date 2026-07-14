#include "PianoKeyboard.h"
#include "../NoteMath.h"
#include <okstudio/MouseOnly.h>
#include <okstudio/Scales.h>
#include <okstudio/Theme.h>

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

PianoKeyboard::PianoKeyboard(KeysProcessor& p) : processor(p)
{
    okstudio::ui::makeMouseOnly(*this);
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

void PianoKeyboard::setScaleLock(bool on, int rootPitchClass, int scale)
{
    if (on == scaleLock && rootPitchClass == rootPc && scale == scaleIndex)
        return;
    scaleLock = on;
    rootPc = rootPitchClass;
    scaleIndex = scale;
    repaint();
}

void PianoKeyboard::setSustain(bool on)
{
    if (on == sustain)
        return;
    sustain = on;
    if (! sustain)
    {
        sustained.clear();
        refresh();
    }
}

void PianoKeyboard::setLatch(bool on)
{
    if (on == latch)
        return;
    if (latch && ! on) // leaving latch mode clears held toggles
    {
        latched.clear();
        refresh();
    }
    latch = on;
}

void PianoKeyboard::panic()
{
    pressed.clear();
    latched.clear();
    sustained.clear();
    dragDrawn = -1;
    refresh();
    processor.allNotesOff(); // belt-and-braces across all channels
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
    const float wh = juce::jmin(area.getHeight(), 150.0f);
    const float yTop = area.getBottom() - wh;
    const float bw = ww * 0.64f; // Octavium proportions: black ~64% of white width,
    const float bh = wh * 0.62f; // ~62% of its height.
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

int PianoKeyboard::keyAt(juce::Point<float> pos) const
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

void PianoKeyboard::refresh()
{
    std::set<int> want;
    want.insert(pressed.begin(), pressed.end());
    want.insert(latched.begin(), latched.end());
    want.insert(sustained.begin(), sustained.end());

    for (auto it = sounding.begin(); it != sounding.end();)
    {
        if (want.find(it->first) == want.end())
        {
            processor.noteOff(it->second);
            it = sounding.erase(it);
        }
        else
        {
            ++it;
        }
    }

    const float vel = getVelocity ? getVelocity() : 0.79f;
    for (const int drawn : want)
    {
        if (sounding.find(drawn) == sounding.end())
        {
            const int out = outputNote(drawn);
            processor.noteOn(out, vel);
            sounding[drawn] = out;
        }
    }

    repaint();
}

void PianoKeyboard::mouseDown(const juce::MouseEvent& e)
{
    const int d = keyAt(e.position);
    if (d < 0)
        return;

    if (latch)
    {
        if (latched.count(d))
            latched.erase(d);
        else
            latched.insert(d);
        dragDrawn = d;
        refresh();
    }
    else
    {
        pressed.insert(d);
        dragDrawn = d;
        refresh();
    }
}

void PianoKeyboard::mouseDrag(const juce::MouseEvent& e)
{
    const int d = keyAt(e.position);
    if (d < 0 || d == dragDrawn)
        return;

    if (latch)
    {
        // Paint latches on: every new key the drag enters turns on.
        if (! latched.count(d))
            latched.insert(d);
        dragDrawn = d;
        refresh();
    }
    else
    {
        // Glide: monophonic, the previous key releases as the next sounds. With the
        // pedal down the note you glide off is caught and keeps ringing, so a sustained
        // run leaves a trail (matching Octavium, where every note sounds until sustain off).
        if (sustain)
            sustained.insert(dragDrawn);
        pressed.erase(dragDrawn);
        pressed.insert(d);
        dragDrawn = d;
        refresh();
    }
}

void PianoKeyboard::mouseUp(const juce::MouseEvent&)
{
    if (! latch && dragDrawn >= 0)
    {
        pressed.erase(dragDrawn);
        if (sustain)
            sustained.insert(dragDrawn); // pedal catches the released note
        refresh();
    }
    dragDrawn = -1;
}

void PianoKeyboard::paint(juce::Graphics& g)
{
    using namespace okstudio;
    const auto full = getLocalBounds().toFloat();

    // Instrument bar behind the keys: subtle horizontal vignette, like Octavium.
    juce::ColourGradient bar(juce::Colour(0xff1b1b1b), full.getX(), 0.0f,
                             juce::Colour(0xff1b1b1b), full.getRight(), 0.0f, false);
    bar.addColour(0.5, juce::Colour(0xff202020));
    g.setGradientFill(bar);
    g.fillRect(full);

    const auto inScale = [this](int note)
    { return ! scaleLock || scales::isInScale(note, rootPc, scaleIndex); };

    // Three visual states, matching Octavium: momentary press (bright blue), held
    // via latch/sustain (deeper blue), and resting.
    enum class State { normal, active, held };
    const auto stateOf = [this](int drawn) -> State
    {
        if (pressed.count(drawn))                       return State::active;
        if (latched.count(drawn) || sustained.count(drawn)) return State::held;
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
        const bool dim = s == State::normal && ! inScale(k.note);

        juce::ColourGradient grad = vGrad(juce::Colours::white, juce::Colour(0xffe7e7e7), top, bot);
        if (s == State::active)      grad = vGrad(juce::Colour(0xff6bb8ff), juce::Colour(0xff2f82e6), top, bot);
        else if (s == State::held)   grad = vGrad(juce::Colour(0xff5fb1ff), juce::Colour(0xff2b7ade), top, bot);
        else if (dim)                grad = vGrad(juce::Colour(0xffc4cad2), juce::Colour(0xffa9b1bd), top, bot);
        else { grad.addColour(0.25, juce::Colour(0xfffbfbfb)); grad.addColour(0.55, juce::Colour(0xfff3f3f3)); }
        g.setGradientFill(grad);
        g.fillRect(b);

        // Bottom lip for depth (blue when active/held).
        g.setColour(s == State::normal ? (dim ? juce::Colour(0xff8f97a2) : juce::Colour(0xffbbbbbb))
                                       : juce::Colour(0xff1b64c7));
        g.fillRect(b.getX(), bot - 2.0f, b.getWidth(), 2.0f);

        // Thin seam on the right edge reads as the gap to the next key.
        g.setColour(juce::Colour(0xff0e0e0e).withAlpha(0.55f));
        g.fillRect(b.getRight() - 1.0f, top, 1.0f, b.getHeight());

        if ((((k.note % 12) + 12) % 12) == 0) // subtle C marker for orientation
        {
            g.setColour(juce::Colour(0xff70767d));
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText("C" + juce::String(k.note / 12 - 1),
                       b.withTrimmedBottom(4.0f).removeFromBottom(14.0f), juce::Justification::centred);
        }
    }

    // ---- Black keys (glossy, rounded, on top) ----
    for (const auto& k : keys)
    {
        if (! k.black)
            continue;
        const auto b = k.bounds;
        const float top = b.getY(), bot = b.getBottom();
        const State s = stateOf(k.note);
        const bool dim = s == State::normal && ! inScale(k.note);

        juce::ColourGradient grad = vGrad(juce::Colour(0xff3a3a3a), juce::Colour(0xff050505), top, bot);
        if (s == State::active)      grad = vGrad(juce::Colour(0xff4aa3ff), juce::Colour(0xff2f82e6), top, bot);
        else if (s == State::held)   grad = vGrad(juce::Colour(0xff3f9cff), juce::Colour(0xff2b7ade), top, bot);
        else if (dim)                grad = vGrad(juce::Colour(0xff4a5460), juce::Colour(0xff20262e), top, bot);
        else { grad.addColour(0.12, juce::Colour(0xff2a2a2a)); grad.addColour(0.5, juce::Colour(0xff121212)); }

        // Soft drop shadow onto the white keys below.
        g.setColour(juce::Colours::black.withAlpha(0.28f));
        g.fillRoundedRectangle(b.translated(0.0f, 1.5f).expanded(0.6f, 0.0f), 3.0f);

        g.setGradientFill(grad);
        g.fillRoundedRectangle(b, 3.0f);

        // Glossy top reflection.
        if (s == State::normal && ! dim)
        {
            g.setColour(juce::Colours::white.withAlpha(0.07f));
            g.fillRoundedRectangle(b.withHeight(b.getHeight() * 0.30f).reduced(2.0f, 2.0f), 2.0f);
        }

        g.setColour(juce::Colour(0xff222222));
        g.drawRoundedRectangle(b, 3.0f, 1.0f);
    }

    // Fallboard rail at the top of the keybed, with a soft shadow onto the keys.
    g.setColour(juce::Colour(0xff141414));
    g.fillRect(full.getX(), keysTop, full.getWidth(), 3.0f);
    g.setGradientFill(vGrad(juce::Colours::black.withAlpha(0.35f), juce::Colours::transparentBlack,
                            keysTop + 3.0f, keysTop + 12.0f));
    g.fillRect(full.getX(), keysTop + 3.0f, full.getWidth(), 9.0f);
}
} // namespace keys
