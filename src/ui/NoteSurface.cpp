#include "NoteSurface.h"
#include <algorithm>
#include <iterator>
#include <okstudio/MouseOnly.h>

namespace keys
{
NoteSurface::NoteSurface(KeysProcessor& p) : processor(p)
{
    okstudio::ui::makeMouseOnly(*this);
    lastSoundingGen = processor.soundingGeneration();
    // 30ms is a repaint poll, not a note clock: it only decides how soon a key lights
    // for a note this surface did not play. Nothing musical depends on it.
    startTimer(30);
}

NoteSurface::~NoteSurface()
{
    stopTimer();
}

void NoteSurface::timerCallback()
{
    const auto gen = processor.soundingGeneration();
    if (gen == lastSoundingGen)
        return;
    lastSoundingGen = gen;
    repaintLitChanges();
}

// The keybed is the most expensive paint in the window: every key is a rounded Path, a
// vertical gradient, a clipped bevel and two seam strokes, and paint() rasterises all of them.
// This used to repaint the whole surface on any note change - up to 33 times a second while an
// arpeggio runs, and once per key crossed during a drag glide - which is fine standalone and is
// not fine in a DAW that scales the plugin window, where every one of those paths goes through
// a transform on the way to the screen.
//
// So repaint the keys that changed and nothing else. paint() itself is untouched: JUCE hands it
// the clip region either way, and the keys outside that region cost a bounds test instead of a
// rasterisation. Correctness does not depend on the geometry, only on which ids are lit, so a
// surface that cannot map an id to a rectangle (drawnBounds returns empty) still gets the whole
// surface back, which is exactly what every surface did before.
void NoteSurface::repaintLitChanges()
{
    // **A map of state, not a set of lit keys**, and that distinction is the whole correctness of
    // this. PianoKeyboard::paint draws three states: `pressed` is the hot accent, latched /
    // sustained / externally sounding are the deeper held accent, and the rest are resting. A set
    // of "which keys are lit" cannot tell the first two apart, so every move *between* them
    // compared equal and repainted nothing - release a key under Sustain and it stayed in the
    // bright press colour for good, because it left `pressed` and entered `sustained` in the same
    // breath and the union never moved. Same for a latch toggle, a sustained drag glide, and a key
    // going from your own press to the arp sounding it.
    std::map<int, int> lit;
    for (const int drawn : latched)
        lit[drawn] = stateHeld;
    for (const int drawn : sustained)
        lit[drawn] = stateHeld;
    for (const int drawn : externallySounding())
        lit[drawn] = stateHeld;
    for (const int drawn : pressed)
        lit[drawn] = stateActive; // last: paint checks `pressed` first, so this wins the same way

    if (lit == lastLit)
        return; // the generation moved for a note this surface does not draw

    std::vector<int> changed;
    auto a = lit.begin();
    auto b = lastLit.begin();
    while (a != lit.end() || b != lastLit.end())
    {
        if (b == lastLit.end() || (a != lit.end() && a->first < b->first))
            changed.push_back(a++->first); // newly lit
        else if (a == lit.end() || b->first < a->first)
            changed.push_back(b++->first); // no longer lit
        else
        {
            if (a->second != b->second)
                changed.push_back(a->first); // lit before and lit now, in a different colour
            ++a;
            ++b;
        }
    }
    lastLit.swap(lit);

    // One union rather than a repaint per key: a chord is a handful of neighbours, and JUCE
    // coalesces overlapping dirty rectangles anyway.
    //
    // Expanded by `litOverdrawPx`, which is measured off the widest thing paint() draws outside a
    // key's own rectangle rather than guessed. A lit black key strokes `b.expanded(2.5f)` at a
    // width of 4, so the outer half of that stroke reaches 4.5 px past the key; a lit white key's
    // path stroke reaches 2.5, its seams a little over 1, and a black key's drop shadow 2.2. This
    // was 2 for one build, which covered the seams and not the glow, so turning a note off left a
    // ring of accent hanging in the air until something else forced a full repaint.
    juce::Rectangle<int> dirty;
    for (const int drawn : changed)
    {
        const auto keyBounds = drawnBounds(drawn);
        if (keyBounds.isEmpty())
        {
            repaint();
            return;
        }
        dirty = dirty.getUnion(keyBounds);
    }
    if (! dirty.isEmpty())
        repaint(dirty.expanded(litOverdrawPx));
}

std::set<int> NoteSurface::externallySounding() const
{
    // Skip what this surface is already playing. Its own notes are drawn from
    // `pressed`/`latched`/`sustained`, in the key coordinates they were pressed in, and
    // drawnForOutputNote is only the inverse of outputNote while nothing has moved
    // between the two. Two things do: Scale Lock snaps an out-of-scale key onto a
    // neighbour (out-of-scale keys are dimmed, not disabled, so this is an ordinary
    // click), and the octave can change while a note is latched or sustained and still
    // ringing at its press-time pitch. Inverse-mapping those lit a second, wrong key
    // beside the one the user actually touched.
    std::set<int> own;
    for (const auto& kv : sounding)
        own.insert(kv.second);

    std::set<int> out;
    for (int note = 0; note < 128; ++note)
    {
        // keybedLit, not isNoteSounding: this surface is the one place that wants the arp's
        // *output* rather than everything that is sounding, and with Light keys on it wants the
        // chord handed to a running line left out. The live chord card still asks
        // isNoteSounding and still gets the chord. See KeysProcessor::keybedLit.
        if (own.count(note) > 0 || ! processor.keybedLit(note))
            continue;
        const int drawn = drawnForOutputNote(note);
        if (drawn >= 0)
            out.insert(drawn);
    }
    return out;
}

void NoteSurface::setScaleLock(bool on, int rootPitchClass, int scale)
{
    if (on == scaleLock && rootPitchClass == rootPc && scale == scaleIndex)
        return;
    scaleLock = on;
    rootPc = rootPitchClass;
    scaleIndex = scale;
    repaint();
}

void NoteSurface::setSustain(bool on)
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

void NoteSurface::setLatch(bool on)
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

void NoteSurface::setPolyphony(int cap)
{
    if (cap == polyphonyCap)
        return;
    polyphonyCap = cap;
    refresh(); // lowering the cap steals oldest voices immediately
}

void NoteSurface::panic()
{
    pressed.clear();
    latched.clear();
    sustained.clear();
    voiceOrder.clear();
    dragDrawn = -1;
    rightGesture = false;
    releaseGesture = false; // both gesture flags, or a panic mid-gesture strands the other one
    refresh();
    processor.allNotesOff(); // belt-and-braces across all channels
}

void NoteSurface::recallOutputNotes(const std::vector<int>& notes)
{
    bool changed = false;
    for (const int note : notes)
    {
        const int drawn = drawnForOutputNote(note);
        if (drawn >= 0 && latched.count(drawn) == 0)
        {
            latched.insert(drawn);
            changed = true;
        }
    }
    if (changed)
        refresh();
}

std::vector<int> NoteSurface::soundingOutputNotes() const
{
    std::vector<int> out;
    for (const auto& kv : sounding)
        out.push_back(kv.second);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void NoteSurface::refresh()
{
    std::set<int> want;
    want.insert(pressed.begin(), pressed.end());
    want.insert(latched.begin(), latched.end());
    want.insert(sustained.begin(), sustained.end());

    // Polyphony cap: steal the oldest sounding voices (FIFO) so the total fits. Stealing
    // removes them from the source sets so they don't come straight back next refresh.
    if (polyphonyCap > 0 && (int) want.size() > polyphonyCap)
    {
        for (const int d : voiceOrder) // oldest first
        {
            if ((int) want.size() <= polyphonyCap)
                break;
            if (want.erase(d) > 0)
            {
                pressed.erase(d);
                latched.erase(d);
                sustained.erase(d);
            }
        }
        // Any still over are brand-new notes not yet in voiceOrder (a big chord added at
        // once); drop the highest drawn ids until it fits, keeping the lowest.
        while ((int) want.size() > polyphonyCap)
            want.erase(std::prev(want.end()));
    }

    for (auto it = sounding.begin(); it != sounding.end();)
    {
        if (want.find(it->first) == want.end())
        {
            processor.noteOff(it->second, noteChannel());
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
            processor.noteOn(out, vel, 0.0, noteChannel());
            sounding[drawn] = out;
        }
    }

    // Rebuild the FIFO voice order: keep still-sounding notes in their existing order,
    // then append any newly-sounding ones.
    std::vector<int> next;
    next.reserve(sounding.size());
    for (const int d : voiceOrder)
        if (sounding.count(d) > 0)
            next.push_back(d);
    for (const auto& kv : sounding)
        if (std::find(next.begin(), next.end(), kv.first) == next.end())
            next.push_back(kv.first);
    voiceOrder.swap(next);

    // Same narrowed repaint the timer takes. A drag glide calls this once per key crossed, so
    // it is the other place a full-surface repaint used to land in a run.
    repaintLitChanges();
}

void NoteSurface::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        // Optional accelerator: toggle this note held, independent of the drag gesture.
        // Ignored while a left gesture is in flight so it can never disturb one.
        if (dragDrawn < 0)
        {
            const int d = drawnAt(e.position);
            if (d >= 0)
            {
                // Release beats latch, and it releases whichever hold has the note: a
                // ringing key stops whether Latch toggled it on or the pedal caught it, so
                // a sustained chord comes apart a note at a time without lifting Sustain
                // itself. Only a silent key latches. Erasing both sets unconditionally is
                // also what fixes a note caught by *both*, which used to leave one set and
                // keep sounding from the other. Nothing here can release a key lit by a
                // chord pad, the arp or MCP: those never enter these sets, so refresh()
                // finds no `sounding` entry and their refcounts are left alone.
                const bool wasLatched = latched.erase(d) > 0;
                const bool wasPedal = sustained.erase(d) > 0;
                if (! wasLatched && ! wasPedal)
                    latched.insert(d);
                rightGesture = true;
                refresh();
            }
        }
        return;
    }

    const int d = drawnAt(e.position);
    if (d < 0)
        return;

    if (latch)
    {
        if (latched.count(d))
        {
            latched.erase(d);
            sustained.erase(d); // a note the pedal also caught has to stop, not linger
        }
        else
        {
            latched.insert(d);
        }
        dragDrawn = d;
        refresh();
        return;
    }

    // A note held by a right-click releases when you left-click it again. Right-click is
    // only ever an accelerator, so the way out of it has to be an ordinary click; a chord
    // built that way can be taken apart a note at a time. Checked before `pressed` so the
    // click reads as "stop that note", and the gesture is marked so the drag and mouse-up
    // below cannot put it straight back (mouseUp's pedal-catch would otherwise re-sustain
    // what we released).
    if (latched.count(d))
    {
        latched.erase(d);
        sustained.erase(d);
        releaseGesture = true;
        dragDrawn = -1;
        refresh();
        return;
    }

    // Sustain is a pedal, not a per-note toggle: a key the pedal is already holding sounds
    // *again* when you click it, the way a real keyboard does with the pedal down. It used
    // to release instead, which is Latch's job and now Latch's alone (restored as its own
    // toggle 2026-07-30, Owen's call). refresh() emits only the delta, so the note has to
    // come off before the press goes back in or nothing would be sent at all.
    if (sustained.erase(d) > 0)
        refresh();

    pressed.insert(d);
    dragDrawn = d;
    refresh();
}

void NoteSurface::mouseDrag(const juce::MouseEvent& e)
{
    if (rightGesture || releaseGesture || e.mods.isRightButtonDown())
        return;

    const int d = drawnAt(e.position);
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
        // Gliding back onto a key the pedal is holding strikes it again, exactly as
        // clicking it does. Same delta problem as mouseDown: off first, then on.
        if (sustained.erase(d) > 0)
            refresh();
        pressed.insert(d);
        dragDrawn = d;
        refresh();
    }
}

void NoteSurface::mouseUp(const juce::MouseEvent&)
{
    if (rightGesture)
    {
        rightGesture = false;
        return;
    }
    if (releaseGesture)
    {
        releaseGesture = false;
        return;
    }
    if (! latch && dragDrawn >= 0)
    {
        pressed.erase(dragDrawn);
        if (sustain)
            sustained.insert(dragDrawn); // pedal catches the released note
        refresh();
    }
    dragDrawn = -1;
}
} // namespace keys
