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
    const float wh = area.getHeight();
    const float bw = ww * 0.62f;
    const float bh = wh * 0.62f;

    int whiteIndex = 0;
    for (int i = 0; i < numKeys; ++i)
    {
        const int n = lowNote + i;
        if (isBlackNote(n))
            continue;
        keys.push_back({ n, false, { area.getX() + (float) whiteIndex * ww, area.getY(), ww, wh } });
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
        keys.push_back({ n, true, { x, area.getY(), bw, bh } });
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
        // Glide: monophonic, the previous key releases as the next sounds.
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
    g.fillAll(theme::background);

    const juce::Colour whiteCol { 0xffe9eef5 };
    const juce::Colour whiteDim { 0xff9aa7b6 };
    const juce::Colour blackCol { 0xff10151d };
    const juce::Colour blackDim { 0xff2a333f };

    const auto inScale = [this](int note)
    { return ! scaleLock || scales::isInScale(note, rootPc, scaleIndex); };

    // Whites first, then blacks on top (that is the order they sit in the vector).
    for (const auto& k : keys)
    {
        if (k.black)
            continue;
        const bool on = sounding.count(k.note) > 0;
        g.setColour(on ? theme::accent : (inScale(k.note) ? whiteCol : whiteDim));
        g.fillRect(k.bounds.reduced(0.5f));
        g.setColour(theme::background);
        g.drawRect(k.bounds, 1.0f);
        if ((((k.note % 12) + 12) % 12) == 0) // mark every C for orientation
        {
            auto labelArea = k.bounds;
            g.setColour(theme::textDim);
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText("C" + juce::String(k.note / 12 - 1), labelArea.removeFromBottom(14.0f),
                       juce::Justification::centred);
        }
    }

    for (const auto& k : keys)
    {
        if (! k.black)
            continue;
        const bool on = sounding.count(k.note) > 0;
        g.setColour(on ? theme::accent.darker(0.2f) : (inScale(k.note) ? blackCol : blackDim));
        g.fillRoundedRectangle(k.bounds.reduced(0.5f), 2.0f);
        g.setColour(theme::outline);
        g.drawRoundedRectangle(k.bounds.reduced(0.5f), 2.0f, 1.0f);
    }
}
} // namespace keys
