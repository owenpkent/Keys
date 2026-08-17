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

    // Left-aligned title plus an optional right-aligned legend, both in the caption row's own
    // micro-caps. The legend is the single biggest readability fix in this file (2026-08-17,
    // Owen: "they don't make any sense" of the old chip rows) - it says what the picture actually
    // shows, in words, so nobody has to reverse-engineer a wheel or a chain from scratch. It reads
    // brighter than the title (`textDim`, not `textFaint`) because it is new information the eye
    // has to take in, not a label already known from the source buttons above.
    void caption(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
                const juce::String& legend = {})
    {
        g.setColour(skin::textFaint);
        g.setFont(skin::micro(9.0f));
        g.drawText(text, area, juce::Justification::topLeft, false);
        if (legend.isNotEmpty())
        {
            g.setColour(skin::textDim);
            g.drawText(legend, area, juce::Justification::topRight, false);
        }
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

    // The transform between two consecutive tray chords, classified per the rule in
    // ChordSources.h's applyP/L/R: P keeps the root and swaps major/minor, L and R move the root
    // by an amount that depends on whether the chord it started from was major or minor. "?"
    // covers anything that isn't a clean P/L/R step - a chord with no known root, or a jump none
    // of the three transforms reaches - rather than guessing at one.
    juce::String classifyPLR(const KeysProcessor::ChordPad& prev, const KeysProcessor::ChordPad& cur)
    {
        if (prev.rootPc < 0 || cur.rootPc < 0)
            return "?";
        const bool pMajor = isMajorish(prev);
        const int r = prev.rootPc;
        if (cur.rootPc == r)
            return "P";
        if (cur.rootPc == ((r + (pMajor ? 4 : 8)) % 12 + 12) % 12)
            return "L";
        if (cur.rootPc == ((r + (pMajor ? 9 : 3)) % 12 + 12) % 12)
            return "R";
        return "?";
    }

    // A chord's own detected name when it has one (every generated chord does); a plain
    // root+quality spelling for the rare pad that reached this diagram with a known root but no
    // stored name; "?" only when neither is available.
    juce::String chordDisplayName(const KeysProcessor::ChordPad& c)
    {
        if (c.name.isNotEmpty())
            return c.name;
        if (c.rootPc >= 0)
            return pcName(c.rootPc) + juce::String(isMajorish(c) ? "" : "m");
        return "?";
    }

    // The roman numeral for one scale degree, cased by the quality the current mode gives it -
    // upper for major/augmented, lower for minor/diminished, with a small degree sign appended on
    // diminished. Shared by Progressions' pre-generation fallback (every degree of the key) and
    // by a real chord that only carries `degree`, never `numeral` - see progressionNumeral below.
    // The degree sign is built from its code point rather than typed as a literal, so the file
    // stays plain ASCII regardless of the compiler's source encoding.
    juce::String romanForDegree(int degree, int modeIdx)
    {
        static const char* upper[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
        static const char* lower[] = { "i", "ii", "iii", "iv", "v", "vi", "vii" };
        const auto& qualities = modes::get(modeIdx).qualities;
        if (degree < 0 || degree >= (int) qualities.size())
            return {};
        const auto q = qualities[(size_t) degree];
        const bool major = q == modes::Quality::major || q == modes::Quality::augmented;
        juce::String s = major ? upper[degree % 7] : lower[degree % 7];
        if (q == modes::Quality::diminished)
            s += juce::String::charToString((juce::juce_wchar) 0x00B0);
        return s;
    }

    // Progressions never sets `ChordPad::numeral` - that field is Markov-only (see the struct
    // comment in PluginProcessor.h; ChordGenMenu::generateCandidates only ever writes it inside
    // the Markov branch). Every other source, Progressions included, writes `degree` instead
    // (ChordSources.h's progressions() sets it via detail::degreeOf), so that is where the roman
    // numeral has to come from. Order: the numeral itself, if a chord somehow carries one; else
    // the degree it was generated from; else a degree worked out from its root against the
    // current key, for a chord that reached this diagram with neither (a hand-edited pad); "?"
    // only when none of the three resolve - which also covers a genuinely non-diatonic root (a
    // secondary dominant, a borrowed chord) that no degree lookup will ever answer.
    juce::String progressionNumeral(const KeysProcessor::ChordPad& c, int keyRootPc, int modeIdx)
    {
        if (c.numeral.isNotEmpty())
            return c.numeral;

        int degree = c.degree;
        if (degree < 0 && c.rootPc >= 0)
        {
            const auto& intervals = modes::get(modeIdx).intervals;
            const int iv = ((c.rootPc - keyRootPc) % 12 + 12) % 12;
            for (int i = 0; i < (int) intervals.size(); ++i)
                if (intervals[(size_t) i] == iv)
                {
                    degree = i;
                    break;
                }
        }

        const auto s = romanForDegree(degree, modeIdx);
        return s.isNotEmpty() ? s : juce::String("?");
    }
} // namespace

SourceViz::SourceViz()
{
    setInterceptsMouseClicks(false, false);
    setTitle("Chord source visualization");
}

// 160 px, up from 112 (2026-08-17, Owen: the old chip rows "don't make any sense"). The old
// height gave every diagram a fixed 80-ish px square and spent everything else on a row of chips
// that just re-typed the chord names the sixteen tray cards a few hundred pixels below already
// show. Every chip row is gone now - the diagram gets the whole band - and 160 is what a
// circle-of-fifths wheel needs to hold a legible 12-position ring plus its labels without the
// clipping the old fixed box caused.
int SourceViz::preferredHeight() { return 160; }

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
// The wheel anchored left at the diagram's own height, with the walk drawn as a pill chain in
// the freed width to the right rather than as a path across the wheel itself (2026-08-17, second
// pass: a circle can only ever be as wide as the strip is tall, and centring it in the full band
// - the first pass's answer - left roughly 85% of a wide window blank, which read as a mistake
// rather than a diagram). The wheel keeps the labels-at-radius+14 fix from the first pass (that
// part reads cleanly and nothing here touches it); what moved is the walk, now the same node-and-
// arrow language Markov and Neo-Riemannian already use, since that is what reads best in this
// component. Each arrow carries the *signed* step distance round the wheel - +1 a fifth
// sharp-ward, -1 flat-ward, a bigger number a leap - which is the one thing neither the wheel nor
// the tray can say on their own: where the generator moved smoothly and where it jumped. This is
// deliberately not the chip row the first pass deleted: that one was bare root names separated by
// `>` with no relationship between them at all.
void SourceViz::paintCircleOfFifths(juce::Graphics& g, juce::Rectangle<float> area) const
{
    caption(g, area, "CIRCLE OF FIFTHS", "THE WALK ROUND THE WHEEL");
    auto diagram = area.withTrimmedTop(kCaptionH);
    const auto accent = skin::accentOf(*this);
    const auto& inScale = modes::get(mode).intervals;

    const float side = juce::jmin(diagram.getHeight(), diagram.getWidth());
    const auto wheelBox = diagram.removeFromLeft(side);
    const float radius = juce::jmax(30.0f, wheelBox.getHeight() * 0.5f - 20.0f);
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

        const auto labelPos = onWheel(centre, radius + 14.0f, wheelAngle(i));
        g.setColour(isKey ? skin::text : skin::textDim);
        g.setFont(skin::ui(11.0f));
        g.drawText(pcName(pc), juce::Rectangle<float>(28.0f, 14.0f).withCentre(labelPos),
                   juce::Justification::centred, false);
    }

    diagram.removeFromLeft(14.0f); // breathing room between the wheel and the chain
    if (diagram.getWidth() <= 8.0f)
        return;

    const auto steps = filledOf(chords);
    std::vector<int> valid; // indices into `steps` whose root is known - the only ones the wheel
                            // (or a fifths-step distance) can place at all
    for (int i = 0; i < (int) steps.size(); ++i)
        if (steps[(size_t) i]->rootPc >= 0)
            valid.push_back(i);
    if (valid.empty())
        return;

    const float nodeH = juce::jlimit(26.0f, 36.0f, diagram.getHeight());
    const int total = (int) valid.size();
    const float minSlotW = 56.0f; // a root pill plus its own signed-step connector
    const auto vis = visibleWindow(total, diagram.getWidth(), minSlotW);
    const int slots = (total - vis.first) + (vis.truncated ? 1 : 0);
    const float slotW = diagram.getWidth() / (float) slots;
    const float y = diagram.getCentreY();
    float x = diagram.getX() + slotW * 0.5f;
    float prevRight = 0.0f;
    int prevPos = -1;
    bool havePrev = false;

    if (vis.truncated)
    {
        const auto node = juce::Rectangle<float>(juce::jmin(slotW - 14.0f, 24.0f), nodeH).withCentre({ x, y });
        g.setColour(skin::textFaint.withAlpha(0.3f));
        g.fillRoundedRectangle(node, 4.0f);
        g.setColour(skin::textFaint);
        g.setFont(skin::uiSemi(11.5f));
        g.drawText("...", node, juce::Justification::centred, false);
        prevRight = node.getRight();
        prevPos = fifthsPosition(steps[(size_t) valid[(size_t) (vis.first - 1)]]->rootPc);
        havePrev = true;
        x += slotW;
    }

    for (int k = vis.first; k < total; ++k)
    {
        const int stepIdx = valid[(size_t) k];
        const float a = recencyAlpha(stepIdx, (int) steps.size());
        const int pc = steps[(size_t) stepIdx]->rootPc;
        const int pos = fifthsPosition(pc);
        const auto text = pcName(pc);
        const float w = juce::jmin(slotW - 16.0f, juce::jmax(26.0f, skin::uiSemi(12.5f).getStringWidthFloat(text) + 12.0f));
        const auto chip = juce::Rectangle<float>(w, nodeH).withCentre({ x, y });

        if (havePrev && prevPos >= 0)
        {
            int step = ((pos - prevPos) % 12 + 12) % 12;
            if (step > 6)
                step -= 12; // shortest signed distance round a 12-position wheel: -5..+6
            const juce::String label = (step >= 0 ? juce::String("+") : juce::String()) + juce::String(step);
            const juce::Line<float> connector({ prevRight + 2.0f, y }, { chip.getX() - 2.0f, y });
            g.setColour(accent.hot.withAlpha(0.3f + 0.5f * a));
            g.drawArrow(connector, 1.4f, 7.0f, 7.0f);

            const auto labelBox = juce::Rectangle<float>(26.0f, 15.0f).withCentre(connector.getPointAlongLineProportionally(0.5f));
            g.setColour(skin::well.withAlpha(0.85f));
            g.fillRoundedRectangle(labelBox, 3.0f);
            g.setColour(accent.hot);
            g.setFont(skin::uiSemi(10.5f));
            g.drawText(label, labelBox, juce::Justification::centred, false);
        }

        g.setColour(accent.base.withAlpha(0.18f + 0.5f * a));
        g.fillRoundedRectangle(chip, 5.0f);
        g.setColour(skin::text.withAlpha(0.6f + 0.4f * a));
        g.setFont(skin::uiSemi(12.5f));
        g.drawText(text, chip, juce::Justification::centred, false);

        prevRight = chip.getRight();
        prevPos = pos;
        havePrev = true;
        x += slotW;
    }
}

// ---- 3. Neo-Riemannian --------------------------------------------------------------------
//
// A horizontal chain: chord name, a connector carrying the P/L/R transform that produced the
// next chord, chord name again - `C -P-> Cm -L-> G#m ...` (2026-08-17 rewrite, replacing a tonic
// triad that the fixed 112 px box clipped its own R and L labels off of, plus a chip row of bare
// letters underneath it with no chord names to say what turned into what). The classification is
// unchanged (`classifyPLR`, the same rule the old triangle's edges encoded); what changed is
// where it is drawn - on the connector between the two chords it actually relates, rather than in
// a legend beside a triangle nothing else on the row refers to.
void SourceViz::paintNeoRiemannian(juce::Graphics& g, juce::Rectangle<float> area) const
{
    caption(g, area, "NEO-RIEMANNIAN", "P PARALLEL | L LEADING-TONE | R RELATIVE");
    const auto diagram = area.withTrimmedTop(kCaptionH);
    const auto accent = skin::accentOf(*this);
    const auto steps = filledOf(chords);

    if (steps.size() < 2)
    {
        g.setColour(skin::textFaint);
        g.setFont(skin::ui(11.0f));
        g.drawText("no walk yet", diagram, juce::Justification::centred, false);
        return;
    }

    const int n = (int) steps.size();
    const float nodeH = juce::jlimit(30.0f, 44.0f, diagram.getHeight());
    const float minSlotW = 78.0f; // a chord name plus its own connector letter needs more room
                                  // than a bare chip
    const auto vis = visibleWindow(n, diagram.getWidth(), minSlotW);
    const int slots = (n - vis.first) + (vis.truncated ? 1 : 0);
    const float slotW = diagram.getWidth() / (float) slots;
    const float y = diagram.getCentreY();
    float x = diagram.getX() + slotW * 0.5f;
    float prevRight = 0.0f;
    int prevStepIdx = -1;
    bool havePrev = false;

    if (vis.truncated)
    {
        const auto node = juce::Rectangle<float>(juce::jmin(slotW - 16.0f, 26.0f), nodeH).withCentre({ x, y });
        g.setColour(skin::textFaint.withAlpha(0.3f));
        g.fillRoundedRectangle(node, 5.0f);
        g.setColour(skin::textFaint);
        g.setFont(skin::uiSemi(12.0f));
        g.drawText("...", node, juce::Justification::centred, false);
        prevRight = node.getRight();
        prevStepIdx = vis.first - 1; // the real, unshown chord the walk continues from
        havePrev = true;
        x += slotW;
    }

    for (int i = vis.first; i < n; ++i)
    {
        const float a = recencyAlpha(i, n);
        const auto& cur = *steps[(size_t) i];
        const auto text = chordDisplayName(cur);
        const float w = juce::jmin(slotW - 22.0f, juce::jmax(32.0f, skin::uiSemi(12.5f).getStringWidthFloat(text) + 14.0f));
        const auto chip = juce::Rectangle<float>(w, nodeH).withCentre({ x, y });

        if (havePrev && prevStepIdx >= 0)
        {
            const auto label = classifyPLR(*steps[(size_t) prevStepIdx], cur);
            const bool unknown = label == "?";
            const juce::Line<float> connector({ prevRight + 2.0f, y }, { chip.getX() - 2.0f, y });
            g.setColour((unknown ? skin::textFaint : accent.hot).withAlpha(unknown ? 0.35f : (0.35f + 0.5f * a)));
            g.drawArrow(connector, 1.4f, 7.0f, 7.0f);

            const auto letterBox = juce::Rectangle<float>(20.0f, 15.0f).withCentre(connector.getPointAlongLineProportionally(0.5f));
            g.setColour(skin::well.withAlpha(0.85f));
            g.fillRoundedRectangle(letterBox, 3.0f);
            g.setColour(unknown ? skin::textFaint : accent.hot);
            g.setFont(skin::uiSemi(11.0f));
            g.drawText(label, letterBox, juce::Justification::centred, false);
        }

        g.setColour(accent.base.withAlpha(0.18f + 0.5f * a));
        g.fillRoundedRectangle(chip, 5.0f);
        g.setColour(skin::text.withAlpha(0.6f + 0.4f * a));
        g.setFont(skin::uiSemi(12.5f));
        g.drawText(text, chip, juce::Justification::centred, false);

        prevRight = chip.getRight();
        prevStepIdx = i;
        havePrev = true;
        x += slotW;
    }
}

// ---- 4. Progressions ----------------------------------------------------------------------
//
// A strip of roman numerals - never chord names, the tray below already has those - with the
// template's own repeat period marked by a bracket under each full lap of it (2026-08-17
// rewrite: the old marker was a single tick at each period boundary with nothing to say how far
// the bracket it started actually reached). Before anything has been generated the strip shows
// the plain diatonic degrees of the current key instead - the pool this source draws its named
// templates' steps from - rather than an empty box with nothing to say.
//
// 2026-08-17, second pass: every card was reading "?". Progressions never sets `numeral` - only
// Markov does (see the struct comment in PluginProcessor.h and progressionNumeral's own comment
// above) - so reading `c.numeral` here was reading a field this source never writes. It carries
// `degree` instead, which `progressionNumeral` reads first, and that fixed the second symptom
// along with it: with every numeral reading "?", every card compared equal, so the period search
// below found period 1 immediately and drew one degenerate bracket per card. Real numerals give
// the search real data to find the actual template length with.
void SourceViz::paintProgressions(juce::Graphics& g, juce::Rectangle<float> area) const
{
    caption(g, area, "PROGRESSIONS", "THE TEMPLATE, REPEATED");
    auto diagram = area.withTrimmedTop(kCaptionH);
    const auto accent = skin::accentOf(*this);
    const auto steps = filledOf(chords);

    if (steps.empty())
    {
        const int n = (int) modes::get(mode).intervals.size();
        const float chipW = diagram.getWidth() / (float) juce::jmax(1, n);
        for (int i = 0; i < n; ++i)
        {
            const auto chip = diagram.withWidth(chipW).withX(diagram.getX() + (float) i * chipW).reduced(3.0f, 8.0f);
            g.setColour(skin::textFaint);
            g.setFont(skin::uiSemi(14.0f));
            g.drawText(romanForDegree(i, mode), chip, juce::Justification::centred, false);
        }
        return;
    }

    const int n = (int) steps.size();

    // The loop point: the smallest period whose numerals repeat exactly across the whole tray, so
    // a short template (a four-chord loop filling sixteen cells) shows where it starts again.
    // Computed on the *full* sequence even when the strip below only has room to show part of
    // it, so a bracket that is still in view means the same thing it would at full width. A
    // period of 1 - every card the same numeral - is excluded on purpose: a bracket around each
    // individual card in turn carries no more information than no bracket at all, which is the
    // exact symptom the numeral bug above produced.
    int period = n;
    for (int p = 1; p <= n / 2; ++p)
    {
        bool ok = true;
        for (int i = 0; i < n && ok; ++i)
            if (progressionNumeral(*steps[(size_t) i], rootPc, mode) != progressionNumeral(*steps[(size_t) (i % p)], rootPc, mode))
                ok = false;
        if (ok)
        {
            period = p;
            break;
        }
    }
    const bool showBrackets = period > 1 && period < n;

    juce::Rectangle<float> bracketStrip;
    if (showBrackets)
    {
        diagram.removeFromBottom(2.0f); // breathing room before the bracket strip
        bracketStrip = diagram.removeFromBottom(14.0f);
    }

    // Chips fill the full width; when more chords exist than fit at a legible size the OLDEST
    // are dropped for a single leading ellipsis chip, same rule as every other readout here.
    const float minChipW = 34.0f;
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
        g.setFont(skin::uiSemi(13.0f));
        g.drawText("...", chip, juce::Justification::centred, false);
        ++slot;
    }

    // Slot-space x-range of each period group that is actually on screen, keyed by group index
    // (`i / period`), gathered while the chips themselves are drawn so the brackets below cost a
    // second pass over slots rather than a second pass over the tray.
    std::vector<std::pair<float, float>> groupRange;
    if (showBrackets)
        groupRange.assign((size_t) ((n + period - 1) / period), { -1.0f, -1.0f });

    for (int i = vis.first; i < n; ++i, ++slot)
    {
        const auto chip = diagram.withWidth(chipW).withX(diagram.getX() + (float) slot * chipW).reduced(3.0f, 6.0f);
        const float a = recencyAlpha(i, n);
        g.setColour(accent.base.withAlpha(0.18f + 0.5f * a));
        g.fillRoundedRectangle(chip, 4.0f);
        g.setColour(skin::text.withAlpha(0.6f + 0.4f * a));
        g.setFont(skin::uiSemi(13.0f));
        g.drawText(progressionNumeral(*steps[(size_t) i], rootPc, mode), chip, juce::Justification::centred, false);

        if (showBrackets)
        {
            auto& range = groupRange[(size_t) (i / period)];
            const float x0 = diagram.getX() + (float) slot * chipW;
            const float x1 = x0 + chipW;
            range.first = range.first < 0.0f ? x0 : juce::jmin(range.first, x0);
            range.second = juce::jmax(range.second, x1);
        }
    }

    if (showBrackets)
    {
        g.setColour(accent.base.withAlpha(0.55f));
        for (auto& range : groupRange)
        {
            if (range.first < 0.0f)
                continue;
            const float x0 = range.first + 2.0f, x1 = range.second - 2.0f;
            const float top = bracketStrip.getY();
            const float tickBottom = top + 5.0f;
            g.drawLine(x0, top, x0, tickBottom, 1.2f);
            g.drawLine(x1, top, x1, tickBottom, 1.2f);
            g.drawLine(x0, tickBottom, x1, tickBottom, 1.2f);
        }
    }
}

// ---- 5. Negative Harmony ------------------------------------------------------------------
//
// A twelve-position chromatic clock (not the fifths wheel above - the mirror this source uses is
// a straight pitch-class reflection, and a clock face is the layout that makes a straight axis
// read as a straight line), anchored left at the diagram's own height like the fifths wheel, with
// the axis drawn and captioned well inside the ring rather than sharing the note labels' own
// radius. 2026-08-17, second pass: the first pass centred the wheel (leaving most of a wide
// window blank, the same defect the fifths wheel had) and drew a highlight ring on every tray
// chord's root and mirror position directly on the wheel - which, at this wheel's necessarily
// small radius, is what pushed those rings out far enough to collide with the very note labels
// they sat beside (reported worst on A#, A, D#, E; the AXIS caption, sharing that same radius,
// collided with whichever note it happened to fall nearest). The wheel now stays the fixed *map*
// - the twelve positions and the axis, nothing else - and every chord's mapping moves to a pill
// chain in the freed width: root, a tick where the line crosses the axis, mirror. The tick is the
// whole point of drawing it this way rather than as a labelled arrow - the crossing is what makes
// a reflection read as a reflection instead of a stated fact.
void SourceViz::paintNegativeHarmony(juce::Graphics& g, juce::Rectangle<float> area) const
{
    caption(g, area, "NEGATIVE HARMONY", "EACH ROOT MIRRORED THROUGH THE AXIS");
    auto diagram = area.withTrimmedTop(kCaptionH);
    const auto accent = skin::accentOf(*this);

    const float side = juce::jmin(diagram.getHeight(), diagram.getWidth());
    const auto wheelBox = diagram.removeFromLeft(side);
    const float radius = juce::jmax(30.0f, wheelBox.getHeight() * 0.5f - 20.0f);
    const auto centre = wheelBox.getCentre();

    for (int pc = 0; pc < 12; ++pc)
    {
        const auto pos = onWheel(centre, radius, wheelAngle(pc));
        g.setColour(pc == rootPc ? accent.hot : skin::textFaint.withAlpha(0.6f));
        g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(pos));
        const auto labelPos = onWheel(centre, radius + 14.0f, wheelAngle(pc));
        g.setColour(skin::textDim);
        g.setFont(skin::ui(10.5f));
        g.drawText(pcName(pc), juce::Rectangle<float>(24.0f, 13.0f).withCentre(labelPos),
                   juce::Justification::centred, false);
    }

    // mirrorPc(pc, rootPc) = (7 + rootPc*2 - pc) mod 12 has no fixed pitch class (7 is odd, 2*pc
    // is always even, so the two can never be equal mod 12); its axis sits at the half-integer
    // position (7 + 2*rootPc)/2, strictly between two notes rather than through one, exactly how
    // a real negative-harmony axis is always drawn. The line stops well short of the note-label
    // ring (radius+8, not radius+14), and its caption sits inside the wheel altogether, at half
    // the radius along the same line - clear of every note label at any rootPc, rather than
    // sharing their ring and colliding with whichever one the axis happens to fall nearest.
    const float axisPos = std::fmod(7.0f + 2.0f * (float) rootPc, 24.0f) / 2.0f;
    const float axisAngle = wheelAngle(0) + axisPos * (juce::MathConstants<float>::twoPi / 12.0f);
    const auto a1 = onWheel(centre, radius + 8.0f, axisAngle);
    const auto a2 = onWheel(centre, radius + 8.0f, axisAngle + juce::MathConstants<float>::pi);
    g.setColour(accent.base.withAlpha(0.4f));
    g.drawLine(a1.x, a1.y, a2.x, a2.y, 1.3f);

    const auto axisLabelPos = onWheel(centre, radius * 0.5f, axisAngle);
    const auto axisLabelBox = juce::Rectangle<float>(34.0f, 13.0f).withCentre(axisLabelPos);
    g.setColour(skin::well.withAlpha(0.85f));
    g.fillRoundedRectangle(axisLabelBox, 3.0f);
    g.setColour(accent.base);
    g.setFont(skin::micro(8.0f));
    g.drawText("AXIS", axisLabelBox, juce::Justification::centred, false);

    diagram.removeFromLeft(14.0f); // breathing room between the wheel and the chain
    if (diagram.getWidth() <= 8.0f)
        return;

    const auto steps = filledOf(chords);
    std::vector<int> valid; // indices into `steps` whose root is known - a mirror needs one
    for (int i = 0; i < (int) steps.size(); ++i)
        if (steps[(size_t) i]->rootPc >= 0)
            valid.push_back(i);
    if (valid.empty())
        return;

    const float nodeH = juce::jlimit(24.0f, 30.0f, diagram.getHeight());
    const float pillW = 28.0f;
    const float tickW = 20.0f;
    const float pairFootprint = pillW * 2.0f + tickW + 10.0f;
    const int total = (int) valid.size();
    const auto vis = visibleWindow(total, diagram.getWidth(), pairFootprint);
    const int slots = (total - vis.first) + (vis.truncated ? 1 : 0);
    const float slotW = diagram.getWidth() / (float) slots;
    const float y = diagram.getCentreY();
    float x = diagram.getX() + slotW * 0.5f;

    if (vis.truncated)
    {
        const auto node = juce::Rectangle<float>(juce::jmin(slotW - 12.0f, 22.0f), nodeH).withCentre({ x, y });
        g.setColour(skin::textFaint.withAlpha(0.3f));
        g.fillRoundedRectangle(node, 4.0f);
        g.setColour(skin::textFaint);
        g.setFont(skin::uiSemi(11.0f));
        g.drawText("...", node, juce::Justification::centred, false);
        x += slotW;
    }

    for (int k = vis.first; k < total; ++k)
    {
        const int stepIdx = valid[(size_t) k];
        const float a = recencyAlpha(stepIdx, (int) steps.size());
        const int root = steps[(size_t) stepIdx]->rootPc;
        const int mpc = ((7 + rootPc * 2 - root) % 12 + 12) % 12;

        const auto rootPill = juce::Rectangle<float>(pillW, nodeH).withCentre({ x - (tickW * 0.5f + pillW * 0.5f), y });
        const auto mirrorPill = juce::Rectangle<float>(pillW, nodeH).withCentre({ x + (tickW * 0.5f + pillW * 0.5f), y });

        g.setColour(accent.hot.withAlpha(0.3f + 0.5f * a));
        g.drawArrow(juce::Line<float>({ rootPill.getRight() + 1.0f, y }, { mirrorPill.getX() - 1.0f, y }), 1.3f, 6.0f, 6.0f);
        // The tick: a short cross-stroke at the midpoint, marking where this line crosses the
        // mirror axis - the one visual idea this whole readout exists to land.
        g.setColour(accent.base.withAlpha(0.5f + 0.4f * a));
        g.drawLine(x, y - 5.0f, x, y + 5.0f, 1.4f);

        g.setColour(accent.hot.withAlpha(0.2f + 0.5f * a));
        g.fillRoundedRectangle(rootPill, 4.0f);
        g.setColour(skin::text.withAlpha(0.65f + 0.35f * a));
        g.setFont(skin::uiSemi(11.5f));
        g.drawText(pcName(root), rootPill, juce::Justification::centred, false);

        g.setColour(accent.base.withAlpha(0.16f + 0.4f * a));
        g.fillRoundedRectangle(mirrorPill, 4.0f);
        g.setColour(skin::textDim.withAlpha(0.6f + 0.4f * a));
        g.setFont(skin::uiSemi(11.5f));
        g.drawText(pcName(mpc), mirrorPill, juce::Justification::centred, false);

        x += slotW;
    }
}

// ---- 6. Planing ----------------------------------------------------------------------------
//
// Each chord as a vertical spine - one stroke from its lowest tone to its highest, a dot at every
// tone on it - repeated left to right against a left-hand pitch axis (2026-08-17 rewrite: dots
// alone, with no axis to read them against and no line joining a chord's own tones, made "the
// same shape sliding" something you had to take on faith rather than see). A constant shape
// sliding shows up here as a constant silhouette moving against fixed octave lines, which is the
// entire idea "planing" names. Before anything has been generated a generic triad, climbing a
// third each step, stands in so the picture still reads.
void SourceViz::paintPlaning(juce::Graphics& g, juce::Rectangle<float> area) const
{
    caption(g, area, "PLANING", "THE SAME SHAPE SLID UP AND DOWN");
    auto diagram = area.withTrimmedTop(kCaptionH);
    const auto accent = skin::accentOf(*this);
    const auto steps = filledOf(chords);

    const bool haveData = ! steps.empty();
    const int count = haveData ? (int) steps.size() : 6;

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
    // A couple of semitones of headroom top and bottom so the outermost dots never sit flush
    // against the diagram's own edge.
    lo -= 2;
    hi += 2;
    const int span = juce::jmax(1, hi - lo);

    // The left gutter: a pitch axis with a tick and a note name at every C in range, the same
    // idea as a piano roll's own ruler. "The same shape slid up and down" only reads as *moving*
    // against a fixed reference; without one, a transposed copy of a shape and the shape itself
    // look identical.
    const float gutterW = 30.0f;
    const auto gutter = diagram.removeFromLeft(gutterW);
    diagram.removeFromLeft(4.0f);

    const auto yOf = [&](int pitch)
    {
        const float t = 1.0f - (float) (pitch - lo) / (float) span;
        return diagram.getY() + juce::jlimit(0.0f, 1.0f, t) * diagram.getHeight();
    };

    for (int p = lo; p <= hi; ++p)
    {
        if (p % 12 != 0)
            continue; // a line at every C, not every semitone
        const float y = yOf(p);
        g.setColour(skin::textFaint.withAlpha(0.22f));
        g.drawLine(gutter.getRight(), y, diagram.getRight(), y, 1.0f);
        g.setColour(skin::textDim);
        g.setFont(skin::micro(8.5f));
        g.drawText(pcName(0) + juce::String(p / 12 - 1),
                   juce::Rectangle<float>(gutter.getWidth(), 12.0f).withCentre({ gutter.getCentreX(), y }),
                   juce::Justification::centredRight, false);
    }

    const float colW = diagram.getWidth() / (float) juce::jmax(1, count);
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
        std::sort(notes.begin(), notes.end());

        const float a = haveData ? recencyAlpha(i, count) : 0.35f;

        // The spine: one vertical stroke from the chord's lowest tone to its highest. This is the
        // "constant shape" itself, drawn once per chord rather than left for the eye to
        // reconstruct from loose dots.
        g.setColour(accent.base.withAlpha(haveData ? (0.2f + 0.35f * a) : 0.2f));
        g.drawLine(cx, yOf(notes.front()), cx, yOf(notes.back()), 2.0f);

        float sumY = 0.0f;
        for (int nte : notes)
        {
            const float cy = yOf(nte);
            // Same alpha ceiling as every accent fill elsewhere in this window (~0.8 at
            // brightest) rather than a near-solid one, so a busy sixteen-chord tray of dots
            // doesn't read any heavier than the rest of the picture.
            g.setColour(accent.base.withAlpha(haveData ? (0.3f + 0.5f * a) : 0.3f));
            g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({ cx, cy }));
            sumY += cy;
        }
        const juce::Point<float> centreOfChord { cx, sumY / (float) notes.size() };
        if (havePrev)
        {
            g.setColour(skin::textFaint.withAlpha(0.35f));
            g.drawLine(prevCentre.x, prevCentre.y, centreOfChord.x, centreOfChord.y, 1.0f);
        }
        prevCentre = centreOfChord;
        havePrev = true;
    }
}

// ---- 0. Algorithmic ------------------------------------------------------------------------
//
// Seven degree columns (however many the current mode actually has), a bar per degree showing
// how many of the tray's current chords landed there, with the count itself written just above
// the bar (2026-08-17: "how tall" and "how many" are the same number said two ways, so a reader
// no longer has to eyeball bar heights against each other to tell three chords on IV from one).
// This is the tiered weighted pool (ChordGen.h) rather than a walk, so there is no directed line
// to draw - the bar chart is the honest picture of "which degrees this source has actually been
// reaching for."
void SourceViz::paintAlgorithmic(juce::Graphics& g, juce::Rectangle<float> area) const
{
    caption(g, area, "ALGORITHMIC", "HOW MANY CHORDS LANDED ON EACH DEGREE");
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

    // The bars now spend the whole band the caption leaves them, top to bottom, with room
    // reserved above the tallest possible bar for its own count and below the shortest for its
    // roman numeral. The baseline rule is what lets an empty degree still read as "zero here"
    // rather than as nothing at all.
    const float labelH = 14.0f;
    const float countH = 15.0f;
    const float baselineY = diagram.getBottom() - labelH - 2.0f;
    const float barsTop = diagram.getY() + countH + 2.0f;

    g.setColour(skin::textFaint.withAlpha(0.4f));
    g.drawLine(diagram.getX(), baselineY, diagram.getRight(), baselineY, 1.0f);

    for (int d = 0; d < n; ++d)
    {
        const float slotX = diagram.getX() + (float) d * colW;
        const float barW = juce::jmax(3.0f, colW / 3.0f);

        if (counts[(size_t) d] > 0)
        {
            const float frac = (float) counts[(size_t) d] / (float) maxCount;
            const float barH = juce::jmax(3.0f, (baselineY - barsTop) * frac);
            const auto bar = juce::Rectangle<float>(barW, barH).withPosition(slotX + (colW - barW) * 0.5f, baselineY - barH);
            g.setColour(accent.base.withAlpha(0.55f));
            g.fillRoundedRectangle(bar, 2.5f);

            g.setColour(accent.hot);
            g.setFont(skin::uiSemi(11.5f));
            g.drawText(juce::String(counts[(size_t) d]),
                       juce::Rectangle<float>(colW, countH).withPosition(slotX, bar.getY() - countH),
                       juce::Justification::centred, false);
        }

        const bool major = qualities[(size_t) d] == modes::Quality::major
                          || qualities[(size_t) d] == modes::Quality::augmented;
        g.setColour(skin::textDim);
        g.setFont(skin::micro(9.5f));
        g.drawText(major ? upper[d % 7] : lower[d % 7],
                   juce::Rectangle<float>(colW, labelH).withPosition(slotX, diagram.getBottom() - labelH),
                   juce::Justification::centred, false);
    }
}

// ---- 1. Markov -----------------------------------------------------------------------------
//
// The tray's chain drawn as bigger nodes, each one carrying its numeral over the chord name it
// actually produced (2026-08-17: the old node was a single line reading whichever of the two it
// had, which said what the chain landed on but not what that landing sounded like without going
// and reading the tray card underneath). Before anything has walked, a generic unlabelled chain
// stands in to say only that much.
void SourceViz::paintMarkov(juce::Graphics& g, juce::Rectangle<float> area) const
{
    caption(g, area, "MARKOV CHAIN", "WHERE THE CHAIN WENT");
    const auto diagram = area.withTrimmedTop(kCaptionH);
    const auto accent = skin::accentOf(*this);
    const auto steps = filledOf(chords);

    const float nodeH = juce::jlimit(34.0f, 56.0f, diagram.getHeight());

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
            g.setFont(skin::ui(12.0f));
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
    const float minSlotW = 58.0f; // wider than before: two lines (numeral, chord name) need more
                                  // room than one
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
        const auto& c = *steps[(size_t) i];
        const juce::String numeral = c.numeral.isNotEmpty() ? c.numeral : juce::String("-");
        const juce::String name = c.name.isNotEmpty() ? c.name : juce::String("?");
        const float textW = juce::jmax(skin::uiSemi(13.0f).getStringWidthFloat(numeral),
                                       skin::ui(10.5f).getStringWidthFloat(name));
        const float w = juce::jmin(gap - 14.0f, juce::jmax(30.0f, textW + 12.0f));
        const auto chip = juce::Rectangle<float>(w, nodeH).withCentre({ x, y });

        if (havePrev)
        {
            g.setColour(accent.base.withAlpha(0.25f + 0.5f * a));
            g.drawArrow(juce::Line<float>({ prevRight + 2.0f, y }, { chip.getX() - 2.0f, y }), 1.4f, 6.0f, 6.0f);
        }

        g.setColour(accent.base.withAlpha(0.18f + 0.5f * a));
        g.fillRoundedRectangle(chip, 4.0f);
        const auto numeralBox = chip.withTrimmedBottom(chip.getHeight() * 0.45f);
        const auto nameBox = chip.withTrimmedTop(chip.getHeight() * 0.55f);
        g.setColour(skin::text.withAlpha(0.65f + 0.35f * a));
        g.setFont(skin::uiSemi(13.0f));
        g.drawText(numeral, numeralBox, juce::Justification::centred, false);
        g.setColour(skin::textDim.withAlpha(0.6f + 0.4f * a));
        g.setFont(skin::ui(10.5f));
        g.drawText(name, nameBox, juce::Justification::centred, false);

        prevRight = chip.getRight();
        havePrev = true;
        x += gap;
    }
}
} // namespace keys
