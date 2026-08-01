#include "SourceViz.h"
#include "KeysLookAndFeel.h"
#include <okstudio/Scales.h>
#include <algorithm>
#include <cmath>

namespace keys
{
namespace
{
    constexpr float kCaptionH = 14.0f;
    constexpr float kPad = 8.0f;

    // The wheel Circle of Fifths itself walks: C G D A E B F# C# G# D# A# F, clockwise from
    // the top (see keys::sources::circleOfFifths in ChordSources.h, step = +7 semitones). A
    // position on this wheel and a step that source actually takes always agree, because both
    // read the same twelve-entry order.
    const int kFifthsOrder[12] = { 0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5 };

    int fifthsPosition(int pc)
    {
        for (int i = 0; i < 12; ++i)
            if (kFifthsOrder[i] == pc)
                return i;
        return 0;
    }

    // Angle for position i of 12 laid out around a wheel: 0 is straight up, and angle grows
    // clockwise (screen y grows downward, so an ordinary increasing sin/cos walk already reads
    // clockwise without any sign flip).
    float wheelAngle(int position)
    {
        return -juce::MathConstants<float>::halfPi
             + (float) position * (juce::MathConstants<float>::twoPi / 12.0f);
    }

    juce::Point<float> onWheel(juce::Point<float> centre, float radius, float angle)
    {
        return { centre.x + radius * std::cos(angle), centre.y + radius * std::sin(angle) };
    }

    juce::String pcName(int pc)
    {
        return okstudio::scales::noteNames()[(size_t) (((pc % 12) + 12) % 12)];
    }

    void caption(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text)
    {
        g.setColour(skin::textFaint);
        g.setFont(skin::micro(9.0f));
        g.drawText(text, area, juce::Justification::topLeft, false);
    }

    // Filled cells only, in the tray's own left-to-right order. An empty cell is a hole waiting
    // to be written, not a step anything walked.
    std::vector<const KeysProcessor::ChordPad*> filledOf(const std::vector<KeysProcessor::ChordPad>& chords)
    {
        std::vector<const KeysProcessor::ChordPad*> out;
        for (auto& c : chords)
            if (! c.notes.empty())
                out.push_back(&c);
        return out;
    }

    // Recency emphasis shared by every walk-style diagram here: the step that produced the most
    // recently filled cell reads brightest, earlier ones fade toward the background. `i` is
    // 0-based from the oldest of `n`.
    float recencyAlpha(int i, int n)
    {
        if (n <= 1)
            return 1.0f;
        return 0.3f + 0.7f * ((float) i / (float) (n - 1));
    }

    juce::String labelFor(const KeysProcessor::ChordPad& c)
    {
        return c.numeral.isNotEmpty() ? c.numeral : c.name;
    }

    // Which slice of `total` items (each needing `itemFootprint` px) is visible across
    // `availWidth`, dropping from the OLDEST end when it doesn't all fit. A walk's most recent
    // steps are the ones worth seeing without having to widen the window, so truncation always
    // eats from `first`, never from the end.
    struct VisibleWindow
    {
        int first;
        bool truncated;
    };

    VisibleWindow visibleWindow(int total, float availWidth, float itemFootprint)
    {
        if (total <= 0)
            return { 0, false };
        const int maxFit = juce::jmax(1, (int) (availWidth / juce::jmax(1.0f, itemFootprint)));
        if (total <= maxFit)
            return { 0, false };
        const int keep = juce::jmax(1, maxFit - 1); // one slot reserved for the ellipsis chip
        return { total - keep, true };
    }

    // A left-anchored row of chips filling `row`'s width exactly, most-recent (the end of
    // `labels`) brightest, that truncates from the OLDEST end - an ellipsis chip stands in for
    // whatever was dropped - rather than shrinking chips below `minChipW` or spilling past the
    // component's bounds. `separator`, when given, is drawn between adjacent chips (Circle of
    // Fifths' "C > G"). `dimFlags`, when given, marks per-label entries (same length as
    // `labels`) that should paint as an unresolved "?" step rather than a confident one.
    void drawChipRow(juce::Graphics& g, juce::Rectangle<float> row, const std::vector<juce::String>& labels,
                     float minChipW, const skin::Accent& accent, juce::Font font,
                     const juce::String& separator = {}, const std::vector<bool>* dimFlags = nullptr)
    {
        if (labels.empty() || row.getWidth() <= 1.0f)
            return;

        const int total = (int) labels.size();
        const float gap = 4.0f;
        const float sepW = separator.isEmpty() ? 0.0f : 12.0f;
        const auto vis = visibleWindow(total, row.getWidth(), minChipW + gap + sepW);
        const int keep = total - vis.first;
        const int slots = keep + (vis.truncated ? 1 : 0);
        const float chipW = juce::jmax(14.0f,
            (row.getWidth() - gap * (float) (slots - 1) - sepW * (float) (slots - 1)) / (float) slots);

        float x = row.getX();
        int slotsLeft = slots;

        const auto drawOne = [&](const juce::String& text, float alpha, bool dim)
        {
            const auto chip = juce::Rectangle<float>(chipW, row.getHeight()).withPosition(x, row.getY());
            g.setColour((dim ? skin::textFaint : accent.base).withAlpha(dim ? 0.3f : (0.18f + 0.5f * alpha)));
            g.fillRoundedRectangle(chip, 4.0f);
            g.setColour(dim ? skin::textFaint : skin::text.withAlpha(0.6f + 0.4f * alpha));
            g.setFont(font);
            g.drawText(text, chip, juce::Justification::centred, false);
            x += chipW;
            --slotsLeft;
            if (slotsLeft > 0 && sepW > 0.0f)
            {
                g.setColour(skin::textFaint);
                g.drawText(separator, juce::Rectangle<float>(sepW, row.getHeight()).withPosition(x, row.getY()),
                           juce::Justification::centred, false);
                x += sepW;
            }
        };

        if (vis.truncated)
            drawOne("...", 0.0f, true);

        for (int i = vis.first; i < total; ++i)
        {
            const bool dim = dimFlags != nullptr && (*dimFlags)[(size_t) i];
            drawOne(labels[(size_t) i], recencyAlpha(i, total), dim);
        }
    }

    // Best-effort major/minor read directly off a chord's own notes rather than its stored
    // `type` index, so this file has no need to include ChordGen.h just to decode one bit of
    // quality - Neo-Riemannian's P/L/R only ever ask "does the third sit a major or minor third
    // above the root", and the notes already answer that without any lookup table.
    bool isMajorish(const KeysProcessor::ChordPad& c)
    {
        if (c.rootPc < 0)
            return true;
        bool hasMajor3 = false, hasMinor3 = false;
        for (int n : c.notes)
        {
            const int iv = (((n - c.rootPc) % 12) + 12) % 12;
            if (iv == 4) hasMajor3 = true;
            if (iv == 3) hasMinor3 = true;
        }
        if (hasMajor3) return true;
        if (hasMinor3) return false;
        return true; // ambiguous (sus, no third at all) - major is the more common default
    }
} // namespace

SourceViz::SourceViz()
{
    setInterceptsMouseClicks(false, false);
    setTitle("Chord source visualization");
}

int SourceViz::preferredHeight() { return 112; }

void SourceViz::setSource(int sourceIndex)
{
    sourceIndex = juce::jlimit(0, 6, sourceIndex);
    if (sourceIndex == source)
        return;
    source = sourceIndex;
    repaint();
}

void SourceViz::setKey(int newRootPc, int newMode)
{
    newRootPc = ((newRootPc % 12) + 12) % 12;
    newMode = juce::jlimit(0, modes::count() - 1, newMode);
    if (newRootPc == rootPc && newMode == mode)
        return;
    rootPc = newRootPc;
    mode = newMode;
    repaint();
}

void SourceViz::setChords(const std::vector<KeysProcessor::ChordPad>& newChords)
{
    if (newChords.size() == chords.size())
    {
        bool same = true;
        for (size_t i = 0; i < newChords.size() && same; ++i)
        {
            const auto& a = newChords[i];
            const auto& b = chords[i];
            same = a.notes == b.notes && a.rootPc == b.rootPc && a.degree == b.degree
                 && a.numeral == b.numeral && a.name == b.name;
        }
        if (same)
            return;
    }
    chords = newChords;
    repaint();
}

void SourceViz::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour(skin::well.withAlpha(0.45f));
    g.fillRoundedRectangle(b, 6.0f);
    g.setColour(juce::Colours::white.withAlpha(0.04f));
    g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.0f);

    const auto area = b.reduced(kPad);

    switch (source)
    {
        case 0: paintAlgorithmic(g, area); break;
        case 1: paintMarkov(g, area); break;
        case 2: paintCircleOfFifths(g, area); break;
        case 3: paintNeoRiemannian(g, area); break;
        case 4: paintProgressions(g, area); break;
        case 5: paintNegativeHarmony(g, area); break;
        case 6: paintPlaning(g, area); break;
        default: break;
    }
}

// ---- 2. Circle of Fifths -----------------------------------------------------------------
//
// The clearest one to draw because the source itself is a walk around a wheel. The wheel
// fills the band's height and sits at the left margin - Neo-Riemannian's shape below, so at
// full panel width every source reads as one family (a fixed figure, then a readout filling
// whatever room is left) rather than one wheel marooned in the middle of a wide window. The
// walk itself is written out as a row of root chips to the right, most recent brightest.
void SourceViz::paintCircleOfFifths(juce::Graphics& g, juce::Rectangle<float> area) const
{
    caption(g, area, "CIRCLE OF FIFTHS");
    auto diagram = area.withTrimmedTop(kCaptionH);
    const auto accent = skin::accentOf(*this);
    const auto& inScale = modes::get(mode).intervals;

    const float side = juce::jmin(diagram.getHeight(), diagram.getWidth());
    const auto wheelBox = diagram.removeFromLeft(side);
    const float radius = juce::jmax(20.0f, wheelBox.getHeight() * 0.5f - 16.0f);
    const auto centre = wheelBox.getCentre();

    for (int i = 0; i < 12; ++i)
    {
        const int pc = kFifthsOrder[i];
        const auto pos = onWheel(centre, radius, wheelAngle(i));
        const bool isKey = pc == rootPc;
        const bool diatonic = std::find(inScale.begin(), inScale.end(), ((pc - rootPc) % 12 + 12) % 12) != inScale.end();
        const float dotR = isKey ? 6.0f : 4.0f;
        const auto dotRect = juce::Rectangle<float>(dotR * 2.0f, dotR * 2.0f).withCentre(pos);

        if (isKey)
        {
            skin::glowRect(g, dotRect, dotR, accent.base, 0.7f);
            g.setColour(accent.hot);
        }
        else
        {
            g.setColour(diatonic ? accent.base.withAlpha(0.55f) : skin::textFaint.withAlpha(0.5f));
        }
        g.fillEllipse(dotRect);

        const auto labelPos = onWheel(centre, radius + 10.0f, wheelAngle(i));
        g.setColour(isKey ? skin::text : skin::textDim);
        g.setFont(skin::ui(9.5f));
        g.drawText(pcName(pc), juce::Rectangle<float>(22.0f, 12.0f).withCentre(labelPos),
                   juce::Justification::centred, false);
    }

    const auto steps = filledOf(chords);
    const auto readout = diagram.withTrimmedLeft(10.0f);
    if (! steps.empty() && readout.getWidth() > 8.0f)
    {
        std::vector<juce::String> labels;
        labels.reserve(steps.size());
        for (auto* s : steps)
            labels.push_back(s->rootPc >= 0 ? pcName(s->rootPc) : juce::String("?"));
        drawChipRow(g, readout.withSizeKeepingCentre(readout.getWidth(), 22.0f), labels, 26.0f, accent,
                   skin::uiSemi(11.0f), ">");
    }
}

// ---- 3. Neo-Riemannian --------------------------------------------------------------------
//
// The tonic triad in the middle with P/L/R pointing out to what each transform lands on (the
// map), and a row of chips reading the actual sequence of transforms the tray's chords took
// (the trip), classified per the rule in ChordSources.h's applyP/L/R: P keeps the root and
// swaps major/minor, L and R move the root by an amount that depends on whether the chord it
// started from was major or minor.
void SourceViz::paintNeoRiemannian(juce::Graphics& g, juce::Rectangle<float> area) const
{
    caption(g, area, "NEO-RIEMANNIAN (PLR)");
    auto diagram = area.withTrimmedTop(kCaptionH);
    const auto accent = skin::accentOf(*this);

    // Same footprint as the Circle of Fifths wheel: a square using the full band height,
    // anchored left, so the triangle grows with the height it is given rather than sitting
    // undersized in a fixed 120 px box.
    const float side = juce::jmin(diagram.getHeight(), diagram.getWidth());
    const auto triBox = diagram.removeFromLeft(side);
    const auto tCentre = triBox.getCentre();
    const bool major = modes::get(mode).qualities[0] == modes::Quality::major
                     || modes::get(mode).qualities[0] == modes::Quality::augmented;
    const juce::String tonicName = pcName(rootPc) + juce::String(major ? "" : "m");

    struct Edge { const char* label; float angle; };
    const Edge edges[3] = {
        { "P", -juce::MathConstants<float>::halfPi },
        { "L", -juce::MathConstants<float>::halfPi + juce::MathConstants<float>::twoPi / 3.0f },
        { "R", -juce::MathConstants<float>::halfPi + juce::MathConstants<float>::twoPi * 2.0f / 3.0f },
    };
    const float er = juce::jmax(20.0f, side * 0.5f - 18.0f);

    for (const auto& e : edges)
    {
        const auto p = onWheel(tCentre, er, e.angle);
        g.setColour(skin::textFaint);
        g.drawLine(tCentre.x, tCentre.y, p.x, p.y, 1.0f);
        g.setColour(accent.base);
        g.setFont(skin::uiSemi(12.0f));
        g.drawText(e.label, juce::Rectangle<float>(20.0f, 18.0f).withCentre(p), juce::Justification::centred, false);
    }

    const auto centreBox = juce::Rectangle<float>(52.0f, 22.0f).withCentre(tCentre);
    g.setColour(skin::well);
    g.fillRoundedRectangle(centreBox, 4.0f);
    g.setColour(skin::text);
    g.setFont(skin::uiSemi(12.5f));
    g.drawText(tonicName, centreBox, juce::Justification::centred, false);

    const auto steps = filledOf(chords);
    const auto readout = diagram.withTrimmedLeft(10.0f);
    if (steps.size() < 2 || readout.getWidth() <= 8.0f)
    {
        g.setColour(skin::textFaint);
        g.setFont(skin::ui(10.0f));
        g.drawText("no walk yet", readout, juce::Justification::centredLeft, false);
        return;
    }

    // The actual sequence of transforms the tray's chords took, classified per the rule in
    // ChordSources.h's applyP/L/R: P keeps the root and swaps major/minor, L and R move the
    // root by an amount that depends on whether the chord it started from was major or minor.
    std::vector<juce::String> labels;
    std::vector<bool> unknown;
    labels.reserve(steps.size() - 1);
    unknown.reserve(steps.size() - 1);
    for (size_t i = 0; i + 1 < steps.size(); ++i)
    {
        const auto& prev = *steps[i];
        const auto& cur = *steps[i + 1];
        juce::String label = "?";
        if (prev.rootPc >= 0 && cur.rootPc >= 0)
        {
            const bool pMajor = isMajorish(prev);
            const int r = prev.rootPc;
            if (cur.rootPc == r)
                label = "P";
            else if (cur.rootPc == ((r + (pMajor ? 4 : 8)) % 12 + 12) % 12)
                label = "L";
            else if (cur.rootPc == ((r + (pMajor ? 9 : 3)) % 12 + 12) % 12)
                label = "R";
        }
        labels.push_back(label);
        unknown.push_back(label == "?");
    }
    drawChipRow(g, readout.withSizeKeepingCentre(readout.getWidth(), 22.0f), labels, 24.0f, accent,
               skin::uiSemi(11.0f), {}, &unknown);
}

// ---- 4. Progressions ----------------------------------------------------------------------
//
// A horizontal strip of roman numerals (or names, when a chord carries no numeral). Before
// anything has been generated the strip shows the plain diatonic degrees of the current key
// instead - the pool this source draws its named templates' steps from - rather than an empty
// box with nothing to say.
void SourceViz::paintProgressions(juce::Graphics& g, juce::Rectangle<float> area) const
{
    caption(g, area, "PROGRESSIONS");
    const auto diagram = area.withTrimmedTop(kCaptionH);
    const auto accent = skin::accentOf(*this);
    const auto steps = filledOf(chords);

    if (steps.empty())
    {
        static const char* romanMajor[] = { "I", "ii", "iii", "IV", "V", "vi", "vii" };
        static const char* romanMinor[] = { "i", "ii", "III", "iv", "v", "VI", "VII" };
        const bool minorish = modes::get(mode).qualities[0] == modes::Quality::minor;
        const auto* roman = minorish ? romanMinor : romanMajor;
        const int n = (int) modes::get(mode).intervals.size();
        const float chipW = diagram.getWidth() / (float) juce::jmax(1, n);
        for (int i = 0; i < n; ++i)
        {
            const auto chip = diagram.withWidth(chipW).withX(diagram.getX() + (float) i * chipW).reduced(3.0f, 8.0f);
            g.setColour(skin::textFaint);
            g.setFont(skin::uiSemi(12.0f));
            g.drawText(roman[i], chip, juce::Justification::centred, false);
        }
        return;
    }

    const int n = (int) steps.size();

    // The loop point: the smallest period whose labels repeat exactly across the whole tray, so
    // a short template (a four-chord loop filling sixteen cells) shows where it starts again.
    // Computed on the *full* sequence even when the strip below only has room to show part of
    // it, so a marker that is still in view means the same thing it would at full width.
    int period = n;
    for (int p = 1; p <= n / 2; ++p)
    {
        bool ok = true;
        for (int i = 0; i < n && ok; ++i)
            if (labelFor(*steps[(size_t) i]) != labelFor(*steps[(size_t) (i % p)]))
                ok = false;
        if (ok)
        {
            period = p;
            break;
        }
    }

    // Chips fill the full width; when more chords exist than fit at a legible size the OLDEST
    // are dropped for a single leading ellipsis chip, same rule as every other readout here.
    const float minChipW = 30.0f;
    const auto vis = visibleWindow(n, diagram.getWidth(), minChipW);
    const int slots = (n - vis.first) + (vis.truncated ? 1 : 0);
    const float chipW = diagram.getWidth() / (float) slots;

    int slot = 0;
    if (vis.truncated)
    {
        const auto chip = diagram.withWidth(chipW).withX(diagram.getX()).reduced(3.0f, 6.0f);
        g.setColour(skin::textFaint.withAlpha(0.3f));
        g.fillRoundedRectangle(chip, 4.0f);
        g.setColour(skin::textFaint);
        g.setFont(skin::uiSemi(12.0f));
        g.drawText("...", chip, juce::Justification::centred, false);
        ++slot;
    }

    for (int i = vis.first; i < n; ++i, ++slot)
    {
        const auto chip = diagram.withWidth(chipW).withX(diagram.getX() + (float) slot * chipW).reduced(3.0f, 6.0f);
        const float a = recencyAlpha(i, n);
        g.setColour(accent.base.withAlpha(0.18f + 0.5f * a));
        g.fillRoundedRectangle(chip, 4.0f);
        g.setColour(skin::text.withAlpha(0.6f + 0.4f * a));
        g.setFont(skin::uiSemi(12.0f));
        g.drawText(labelFor(*steps[(size_t) i]), chip, juce::Justification::centred, false);

        if (period < n && i > vis.first && i % period == 0)
        {
            g.setColour(accent.hot.withAlpha(0.8f));
            g.drawLine(chip.getX() - 2.0f, diagram.getY(), chip.getX() - 2.0f, diagram.getBottom(), 1.5f);
        }
    }
}

// ---- 5. Negative Harmony ------------------------------------------------------------------
//
// A twelve-position clock, chromatic order rather than the fifths wheel above - the mirror this
// source uses is a straight pitch-class reflection, and a clock face is the layout that makes a
// straight axis read as a straight line. The axis is drawn at the same half-integer pitch class
// keys::sources::detail::mirrorPc reflects about. Same left-anchored, fill-the-height shape as
// the other two wheels; each current chord's root and its own reflection are written out as
// "X -> Y" chips to the right rather than drawn on the wheel itself, which is what keeps the
// wheel legible once more than a couple of chords have been generated.
void SourceViz::paintNegativeHarmony(juce::Graphics& g, juce::Rectangle<float> area) const
{
    caption(g, area, "NEGATIVE HARMONY");
    auto diagram = area.withTrimmedTop(kCaptionH);
    const auto accent = skin::accentOf(*this);

    const float side = juce::jmin(diagram.getHeight(), diagram.getWidth());
    const auto wheelBox = diagram.removeFromLeft(side);
    const float radius = juce::jmax(20.0f, wheelBox.getHeight() * 0.5f - 16.0f);
    const auto centre = wheelBox.getCentre();

    for (int pc = 0; pc < 12; ++pc)
    {
        const auto pos = onWheel(centre, radius, wheelAngle(pc));
        g.setColour(pc == rootPc ? accent.hot : skin::textFaint.withAlpha(0.6f));
        g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(pos));
        const auto labelPos = onWheel(centre, radius + 10.0f, wheelAngle(pc));
        g.setColour(skin::textDim);
        g.setFont(skin::ui(9.0f));
        g.drawText(pcName(pc), juce::Rectangle<float>(20.0f, 12.0f).withCentre(labelPos),
                   juce::Justification::centred, false);
    }

    // mirrorPc(pc, rootPc) = (7 + rootPc*2 - pc) mod 12 has no fixed pitch class (7 is odd, 2*pc
    // is always even, so the two can never be equal mod 12); its axis sits at the half-integer
    // position (7 + 2*rootPc)/2, strictly between two notes rather than through one, which is
    // exactly how a real negative-harmony axis is always drawn.
    const float axisPos = std::fmod(7.0f + 2.0f * (float) rootPc, 24.0f) / 2.0f;
    const float axisAngle = wheelAngle(0) + axisPos * (juce::MathConstants<float>::twoPi / 12.0f);
    const auto a1 = onWheel(centre, radius + 8.0f, axisAngle);
    const auto a2 = onWheel(centre, radius + 8.0f, axisAngle + juce::MathConstants<float>::pi);
    g.setColour(accent.base.withAlpha(0.35f));
    g.drawLine(a1.x, a1.y, a2.x, a2.y, 1.2f);

    const auto steps = filledOf(chords);
    const auto readout = diagram.withTrimmedLeft(10.0f);
    if (! steps.empty() && readout.getWidth() > 8.0f)
    {
        std::vector<juce::String> labels;
        labels.reserve(steps.size());
        for (auto* s : steps)
        {
            if (s->rootPc < 0)
            {
                labels.push_back("?");
                continue;
            }
            const int mpc = ((7 + rootPc * 2 - s->rootPc) % 12 + 12) % 12;
            labels.push_back(pcName(s->rootPc) + " -> " + pcName(mpc));
        }
        drawChipRow(g, readout.withSizeKeepingCentre(readout.getWidth(), 22.0f), labels, 56.0f, accent,
                   skin::uiSemi(10.5f));
    }
}

// ---- 6. Planing ----------------------------------------------------------------------------
//
// The chord shape as a small stack of dots plotted at real pitch height, repeated left to right
// - a constant shape sliding shows up here as a constant silhouette moving across the strip,
// which is the entire idea "planing" names. Before anything has been generated a generic triad,
// climbing a third each step, stands in for a real chord so the picture still reads.
void SourceViz::paintPlaning(juce::Graphics& g, juce::Rectangle<float> area) const
{
    caption(g, area, "PLANING");
    const auto diagram = area.withTrimmedTop(kCaptionH);
    const auto accent = skin::accentOf(*this);
    const auto steps = filledOf(chords);

    const bool haveData = ! steps.empty();
    const int count = haveData ? (int) steps.size() : 6;
    const float colW = diagram.getWidth() / (float) juce::jmax(1, count);

    int lo = 128, hi = -1;
    if (haveData)
        for (auto* c : steps)
            for (int n : c->notes)
            {
                lo = juce::jmin(lo, n);
                hi = juce::jmax(hi, n);
            }
    if (! haveData || hi < lo)
    {
        lo = 60;
        hi = 72;
    }
    const int span = juce::jmax(1, hi - lo);

    juce::Point<float> prevCentre;
    bool havePrev = false;
    for (int i = 0; i < count; ++i)
    {
        const float cx = diagram.getX() + ((float) i + 0.5f) * colW;
        std::vector<int> notes;
        if (haveData)
            notes = steps[(size_t) i]->notes;
        else
        {
            const int root = 60 + i * 4;
            notes = { root, root + 4, root + 7 };
        }
        if (notes.empty())
            continue;

        const float a = haveData ? recencyAlpha(i, count) : 0.35f;
        float sumY = 0.0f;
        for (int nte : notes)
        {
            const float t = 1.0f - (float) (nte - lo) / (float) span;
            const float cy = diagram.getY() + juce::jlimit(0.0f, 1.0f, t) * diagram.getHeight();
            // Same alpha ceiling as every chip elsewhere in this window (~0.7 at brightest)
            // rather than the near-solid 0.9 this used to reach, so a busy sixteen-chord tray
            // of small dots doesn't read any heavier than a row of chips would.
            g.setColour(accent.base.withAlpha(haveData ? (0.25f + 0.45f * a) : 0.25f));
            g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({ cx, cy }));
            sumY += cy;
        }
        const juce::Point<float> centreOfChord { cx, sumY / (float) notes.size() };
        if (havePrev)
        {
            g.setColour(skin::textFaint.withAlpha(0.4f));
            g.drawLine(prevCentre.x, prevCentre.y, centreOfChord.x, centreOfChord.y, 1.0f);
        }
        prevCentre = centreOfChord;
        havePrev = true;
    }
}

// ---- 0. Algorithmic ------------------------------------------------------------------------
//
// Seven degree columns (however many the current mode actually has), a bar per degree showing
// how many of the tray's current chords landed there. This is the tiered weighted pool
// (ChordGen.h) rather than a walk, so there is no directed line to draw - the bar chart is the
// honest picture of "which degrees this source has actually been reaching for."
void SourceViz::paintAlgorithmic(juce::Graphics& g, juce::Rectangle<float> area) const
{
    caption(g, area, "ALGORITHMIC");
    const auto diagram = area.withTrimmedTop(kCaptionH);
    const auto accent = skin::accentOf(*this);
    const auto& qualities = modes::get(mode).qualities;
    const int n = (int) qualities.size();

    std::vector<int> counts((size_t) n, 0);
    for (auto& c : chords)
        if (! c.notes.empty() && c.degree >= 0 && c.degree < n)
            counts[(size_t) c.degree]++;
    const int maxCount = juce::jmax(1, *std::max_element(counts.begin(), counts.end()));

    static const char* upper[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
    static const char* lower[] = { "i", "ii", "iii", "iv", "v", "vi", "vii" };
    const float colW = diagram.getWidth() / (float) juce::jmax(1, n);

    // Capped at roughly half the band and sitting on a baseline rule, with slim columns rather
    // than filled slot-wide blocks - a full-height, full-width bar chart of solid fills read as
    // a different, much louder application next to the fine-lined wheel and triangle either
    // side of it in the Source picker. The baseline rule is what lets an empty degree still
    // read as "zero here" rather than as nothing at all.
    const float labelH = 12.0f;
    const float baselineY = diagram.getBottom() - labelH - 2.0f;
    const float barsTop = juce::jmax(diagram.getY(), baselineY - diagram.getHeight() * 0.5f);

    g.setColour(skin::textFaint.withAlpha(0.4f));
    g.drawLine(diagram.getX(), baselineY, diagram.getRight(), baselineY, 1.0f);

    for (int d = 0; d < n; ++d)
    {
        const float slotX = diagram.getX() + (float) d * colW;
        const float barW = juce::jmax(3.0f, colW / 3.0f);

        if (counts[(size_t) d] > 0)
        {
            const float frac = (float) counts[(size_t) d] / (float) maxCount;
            const float barH = (baselineY - barsTop) * frac;
            const auto bar = juce::Rectangle<float>(barW, barH).withPosition(slotX + (colW - barW) * 0.5f, baselineY - barH);
            g.setColour(accent.base.withAlpha(0.55f));
            g.fillRoundedRectangle(bar, 2.0f);
        }

        const bool major = qualities[(size_t) d] == modes::Quality::major
                          || qualities[(size_t) d] == modes::Quality::augmented;
        g.setColour(skin::textDim);
        g.setFont(skin::micro(9.0f));
        g.drawText(major ? upper[d % 7] : lower[d % 7],
                   juce::Rectangle<float>(colW, labelH).withPosition(slotX, diagram.getBottom() - labelH),
                   juce::Justification::centred, false);
    }
}

// ---- 1. Markov -----------------------------------------------------------------------------
//
// The tray's numerals in a row with arrows between them, which is what a chain walk actually
// is: each link picked from what tends to follow the one before it. Before anything has walked,
// a generic unlabelled chain stands in to say only that much.
void SourceViz::paintMarkov(juce::Graphics& g, juce::Rectangle<float> area) const
{
    caption(g, area, "MARKOV CHAIN");
    const auto diagram = area.withTrimmedTop(kCaptionH);
    const auto accent = skin::accentOf(*this);
    const auto steps = filledOf(chords);

    const float nodeH = juce::jlimit(20.0f, 30.0f, diagram.getHeight());

    if (steps.empty())
    {
        const float y = diagram.getCentreY();
        const float gap = juce::jmin(90.0f, diagram.getWidth() / 4.0f);
        float x = diagram.getX() + nodeH * 0.5f;
        for (int i = 0; i < 4; ++i)
        {
            const auto node = juce::Rectangle<float>(nodeH, nodeH).withCentre({ x, y });
            g.setColour(skin::textFaint);
            g.drawEllipse(node, 1.2f);
            g.setFont(skin::ui(10.0f));
            g.drawText("?", node, juce::Justification::centred, false);
            if (i < 3)
                g.drawLine(x + nodeH * 0.5f, y, x + gap - nodeH * 0.5f, y, 1.0f);
            x += gap;
        }
        return;
    }

    // Chips are laid out edge to edge like every other readout, dropping the OLDEST when more
    // chords exist than fit at a legible width and standing in a leading ellipsis chip for them.
    const int n = (int) steps.size();
    const float minSlotW = 46.0f;
    const auto vis = visibleWindow(n, diagram.getWidth(), minSlotW);
    const int slots = (n - vis.first) + (vis.truncated ? 1 : 0);
    const float gap = diagram.getWidth() / (float) slots;
    const float y = diagram.getCentreY();
    float x = diagram.getX() + gap * 0.5f;
    float prevRight = 0.0f;
    bool havePrev = false;

    if (vis.truncated)
    {
        const auto node = juce::Rectangle<float>(juce::jmin(gap - 12.0f, 26.0f), nodeH).withCentre({ x, y });
        g.setColour(skin::textFaint.withAlpha(0.3f));
        g.fillRoundedRectangle(node, 4.0f);
        g.setColour(skin::textFaint);
        g.setFont(skin::uiSemi(11.5f));
        g.drawText("...", node, juce::Justification::centred, false);
        prevRight = node.getRight();
        havePrev = true;
        x += gap;
    }

    for (int i = vis.first; i < n; ++i)
    {
        const float a = recencyAlpha(i, n);
        const auto label = labelFor(*steps[(size_t) i]);
        const float w = juce::jmin(gap - 14.0f, juce::jmax(24.0f, skin::uiSemi(11.5f).getStringWidthFloat(label) + 10.0f));
        const auto chip = juce::Rectangle<float>(w, nodeH).withCentre({ x, y });

        if (havePrev)
        {
            g.setColour(accent.base.withAlpha(0.25f + 0.5f * a));
            g.drawArrow(juce::Line<float>({ prevRight + 2.0f, y }, { chip.getX() - 2.0f, y }), 1.4f, 6.0f, 6.0f);
        }

        g.setColour(accent.base.withAlpha(0.18f + 0.5f * a));
        g.fillRoundedRectangle(chip, 4.0f);
        g.setColour(skin::text.withAlpha(0.6f + 0.4f * a));
        g.setFont(skin::uiSemi(11.5f));
        g.drawText(label, chip, juce::Justification::centred, false);

        prevRight = chip.getRight();
        havePrev = true;
        x += gap;
    }
}
} // namespace keys
