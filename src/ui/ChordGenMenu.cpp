#include "ChordGenMenu.h"
#include "../ChordLibrary.h"
#include "../ChordMarkov.h"
#include "../ChordSources.h"
#include "../ChordSuggest.h"
#include "../Chords.h"
#include "../ScaleModes.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>
#include <algorithm>
#include <cmath>

namespace keys
{
namespace
{
    // A chord's own register, so a suggestion lands where the chord it follows sits.
    int octaveOf(const std::vector<int>& notes, int fallback)
    {
        if (notes.empty())
            return fallback;
        return *std::min_element(notes.begin(), notes.end()) / 12;
    }

    // One row of the Next menu: a play button that auditions without closing the menu
    // (Octavium's per-row preview), then the row itself places the chord. The whole
    // non-button area triggers, so the target stays huge.
    class SuggestionRow : public juce::PopupMenu::CustomComponent
    {
    public:
        SuggestionRow(juce::String text, std::function<void()> preview)
            : juce::PopupMenu::CustomComponent(true), label(std::move(text))
        {
            play.setButtonText(juce::String::fromUTF8("\xe2\x96\xb6"));
            play.setTooltip("Hear it (the menu stays open)");
            play.onClick = std::move(preview);
            addAndMakeVisible(play);
        }

        void getIdealSize(int& w, int& h) override
        {
            w = 320;
            h = okstudio::ui::minHitPx;
        }

        void resized() override
        {
            play.setBounds(getLocalBounds().removeFromLeft(okstudio::ui::minHitPx).reduced(2));
        }

        void paint(juce::Graphics& g) override
        {
            if (isItemHighlighted())
            {
                const auto r = getLocalBounds().toFloat().reduced(2.0f, 1.0f);
                g.setColour(skin::accentOf(*this).base.withAlpha(0.15f));
                g.fillRoundedRectangle(r, 4.0f);
                g.setColour(skin::accentOf(*this).base.withAlpha(0.4f));
                g.drawRoundedRectangle(r, 4.0f, 1.0f);
            }
            g.setColour(isItemHighlighted() ? juce::Colour(0xffeafcff) : skin::text);
            g.setFont(skin::ui(14.0f));
            g.drawText(label, getLocalBounds().withTrimmedLeft(okstudio::ui::minHitPx + 10),
                       juce::Justification::centredLeft);
        }

    private:
        juce::String label;
        juce::TextButton play;
    };
} // namespace

ChordGenMenu::ChordGenMenu(KeysProcessor& p) : processor(p)
{
}

ChordGenMenu::~ChordGenMenu()
{
    stopPreview(); // a suggestion left auditioning must not outlive the generator
}

int ChordGenMenu::sourceIndex() const
{
    // Floor only. An upper clamp here has to be a literal count of the sources, and the one that
    // was here read 6, so appending an eighth brain - the one growth the parameter's own comment
    // calls safe - would have arrived silently reading as Planing. A source this build does not
    // know instead falls through generateChords' `default:` to the weighted pool, which is the
    // safe answer that switch was written to give, and shows no per-source band.
    return juce::jmax(0, (int) processor.apvts.getRawParameterValue("genSource")->load());
}

bool ChordGenMenu::markovActive() const { return sourceIndex() == 1; }

// Two different questions, and conflating them greyed Mode under five sources that read it.
//
//   * **Scale Compliance and Lock Influence** are the weighted pool's own dials: how far it may
//     stray from the key, and how much it copies the families of your locked cards. Nothing else
//     has a pool to weight, so they are dead to the other six;
//   * **Mode** is read by everything except Markov. Circle of fifths takes each landing degree's
//     quality from it, Neo-Riemannian picks its starting triad from it, Progressions resolves its
//     degrees through it, and negative harmony and planing use it as the scale they reflect or
//     slide through. A chain walk is the one brain with no scale in it at all.
bool ChordGenMenu::readsScaleSettings() const { return sourceIndex() == 0; }
bool ChordGenMenu::readsMode() const { return sourceIndex() != 1; }

bool ChordGenMenu::constrains(const char* paramId) const
{
    // Absent parameter reads as ticked. A setting whose box was never wired should behave as it
    // always did, not silently free itself.
    if (auto* v = processor.apvts.getRawParameterValue(paramId))
        return v->load() > 0.5f;
    return true;
}

void ChordGenMenu::rollFreeChoices()
{
    rolledRoot = constrains("genUseKey") ? -1 : rng.nextInt(12);
    // Diatonic modes only when Mode is free. The table's back half is harmonic minor, blues and
    // the pentatonics, which are scales rather than modes: wandering into a five-note scale
    // because you unticked a box is a surprise, not a freedom.
    rolledMode = constrains("genUseMode") ? -1 : rng.nextInt(7);
}

// Lean every chord's third. Positive pushes major, negative minor, and the magnitude is the
// chance that any given chord is pushed at all, so 40% leans most of a page without flattening
// it into one colour. Only the third moves: the root, the fifth and every extension stay, so a
// major ninth leaned minor is still a ninth.
void ChordGenMenu::applyMajorMinorBias(std::vector<chordgen::Chord>& chords)
{
    const int bias = (int) processor.apvts.getRawParameterValue("genMajMin")->load();
    if (bias == 0)
        return;
    const bool wantMajor = bias > 0;
    const float chance = (float) std::abs(bias) * 0.01f;

    for (auto& c : chords)
    {
        if (c.notes.empty() || rng.nextFloat() > chance)
            continue;
        for (auto& n : c.notes)
        {
            const int iv = (((n - c.rootPc) % 12) + 12) % 12;
            if (wantMajor && iv == 3 && n + 1 <= 127)
                n += 1;
            else if (! wantMajor && iv == 4 && n - 1 >= 0)
                n -= 1;
        }
        std::sort(c.notes.begin(), c.notes.end());

        // The chord's *label* has to follow its notes. This pass moves a third, which is the one
        // interval a type name is mostly about, so a major triad leaned minor kept reading as
        // "Major" in `Chord::type` - and that is what `generateCandidates` copies onto a pad,
        // where Next voicing and the suggestion table both read it. The name beside it is
        // re-detected from the notes, so leaving type alone made the two disagree on the same
        // card. Only taken when the reading still calls the same note the root: analyse is free
        // to name any sounding pitch class the root, and letting it move one under fitVoicing -
        // which stacks and shrinks a chord *around* its root - would be a worse bug than a stale
        // type. Where it does move, both fields keep the source's answer and stay consistent.
        const auto [readRoot, readType] = suggest::analyse(c.notes);
        if (readRoot == c.rootPc)
            c.type = readType;
    }
}

// Ranges rather than single values since 2026-08-01, and read with the ends swapped if they cross:
// two independent parameters cannot police each other, and the alternative is a generator that
// silently produces nothing whenever a min is dragged past its max.
std::pair<int, int> ChordGenMenu::noteCountRange() const
{
    const int a = (int) processor.apvts.getRawParameterValue("genNotesMin")->load();
    const int b = (int) processor.apvts.getRawParameterValue("genNotesMax")->load();
    return { juce::jmin(a, b), juce::jmax(a, b) };
}

std::pair<int, int> ChordGenMenu::octaveRange() const
{
    const int a = (int) processor.apvts.getRawParameterValue("genOctave")->load();
    const int b = (int) processor.apvts.getRawParameterValue("genOctaveMax")->load();
    return { juce::jmin(a, b), juce::jmax(a, b) };
}

// Note count and register, applied to every chord whatever made it. These are post-passes for the
// same reason voice leading is: they are facts about the *voicing* a chord arrives in, not about
// which chord it is, so asking each of seven brains to honour them separately would be seven
// places to get it wrong. Owen's ask was exactly this shape - "all of their options should have
// the option for how many notes and what inversion".
//
// Growing a chord stacks further thirds **through the mode**, so an eleven-note chord is still in
// the key rather than a chromatic pile. Shrinking drops from the top, which keeps the root and the
// third and therefore keeps the chord recognisable: a dyad off a major seventh should be the root
// and its third, not the seventh and the ninth.
void ChordGenMenu::fitVoicing(std::vector<chordgen::Chord>& chords)
{
    const auto [minN, maxN] = noteCountRange();
    const auto [minOct, maxOct] = octaveRange();
    const auto& scale = modes::get(genMode()).intervals; // semitone offsets from the tonic
    const int root = genRoot();
    // Unticked means every inversion is fair game, which is not the same as every box ticked:
    // the boxes are a *choice* the generator may not exceed, and this is the absence of one.
    const bool freeNotes = ! constrains("genUseNotes");
    const bool freeOctave = ! constrains("genUseOctave");
    const auto inversions = constrains("genUseInversions")
                                ? currentOptions().inversions // defaulted to root if none ticked
                                : std::vector<int> { 0, 1, 2, 3 };

    for (auto& c : chords)
    {
        if (c.notes.empty())
            continue;

        // Unticked means the range is not a constraint at all, so the roll spans the whole 2..11
        // the parameter can express rather than whatever the two steppers happen to read.
        const int want = freeNotes ? 2 + rng.nextInt(10)
                                   : minN + (maxN > minN ? rng.nextInt(maxN - minN + 1) : 0);
        std::sort(c.notes.begin(), c.notes.end());

        // Root position **first**, before the note count is fitted rather than after it.
        //
        // This is the normalisation that makes an inversion *replace* whatever rotation the
        // chord arrived in instead of compounding with it, and it has to happen; the ordering is
        // the part that was wrong. `rootPosition` also collapses repeated pitch classes and
        // restacks what survives inside a single octave, so running it after the grow loop threw
        // the grow loop away: stacking thirds through a seven-note mode comes back round to the
        // root's own pitch class on the eighth note, so every count above seven silently
        // returned seven (five under a pentatonic mode), and the two-octave stack the loop had
        // just built returned as a one-octave cluster. Normalise, then fit, then invert.
        auto voiced = chordgen::rootPosition(c.notes, c.rootPc);
        if (voiced.empty())
            voiced = c.notes; // a root that is not in its own chord; keep what we were given

        // Shrink: from the top, so the root and third survive. On root position, which is the
        // one arrangement where "the top" and "the extensions" are the same notes.
        if ((int) voiced.size() > want && want >= 1)
            voiced.resize((size_t) want);

        // Grow: keep stacking scale thirds above the top note. Stepping two scale degrees at a
        // time is what "a third" means inside a mode, and it is why this stays diatonic where
        // adding a flat 4 semitones would not.
        while ((int) voiced.size() < want)
        {
            const int top = voiced.back();
            int next = top + 3;
            for (int i = 0; i < 12; ++i) // find the next scale tone at least a third above
            {
                const int norm = ((((next + i) - root) % 12) + 12) % 12;
                if (std::find(scale.begin(), scale.end(), norm) != scale.end())
                {
                    next = next + i;
                    break;
                }
            }
            if (next > 127 || next <= top)
                break; // off the keyboard, or the search found nothing: stop rather than loop
            voiced.push_back(next);
        }

        // Inversion, for every source rather than the weighted pool alone: tick R alone and you
        // get root position, even from a pool that had already inverted the chord itself. Last,
        // so it rotates the chord you actually asked for rather than the one the source happened
        // to hand over.
        if (! inversions.empty())
        {
            const int inv = inversions[(size_t) rng.nextInt((int) inversions.size())];
            auto rotated = chordgen::applyInversion(voiced, inv);
            bool fits = true;
            for (const int n : rotated)
                if (n < 0 || n > 127)
                    fits = false;
            if (fits)
                voiced = std::move(rotated);
        }
        c.notes = std::move(voiced);

        // Register: move the whole chord so its lowest note sits in an octave inside the range.
        // The chord moves in one piece, so its shape and its voice leading are untouched.
        const int wantOct = freeOctave ? 2 + rng.nextInt(5)
                                       : minOct + (maxOct > minOct ? rng.nextInt(maxOct - minOct + 1) : 0);
        const int haveOct = c.notes.front() / 12;
        const int shift = (wantOct - haveOct) * 12;
        if (shift != 0)
        {
            bool fits = true;
            for (const int n : c.notes)
                if (n + shift < 0 || n + shift > 127)
                    fits = false;
            if (fits)
                for (auto& n : c.notes)
                    n += shift;
        }
    }
}

// One door to five brains plus the original. Voice leading runs on the way out, which is the
// point of it being a pass and not a source: whichever of these produced the chords, they arrive
// at the caller already smoothed by however much the dial asks for.
std::vector<chordgen::Chord> ChordGenMenu::generateChords(int count)
{
    rollFreeChoices(); // before anything reads genRoot or genMode
    const auto& a = processor.apvts;
    const int oct = currentOptions().octave;
    const int root = genRoot(), mode = genMode();
    std::vector<chordgen::Chord> out;

    switch (sourceIndex())
    {
        case 2:
            out = sources::circleOfFifths(root, mode, oct, count,
                                          a.getRawParameterValue("genCircleDir")->load() > 0.5f ? 1 : -1, rng);
            break;
        case 3:
            out = sources::neoRiemannian(root, mode, oct, count,
                                         (int) a.getRawParameterValue("genPlrP")->load(),
                                         (int) a.getRawParameterValue("genPlrL")->load(),
                                         (int) a.getRawParameterValue("genPlrR")->load(), rng);
            break;
        case 4:
            // The picker's entry 0 is "Random", which the table spells -1.
            out = sources::progressions(root, mode, oct, count,
                                        (int) a.getRawParameterValue("genProgression")->load() - 1, rng);
            break;
        case 5: out = sources::negativeHarmony(root, mode, oct, count, rng); break;
        case 6:
            out = sources::planing(root, mode, oct, count,
                                   a.getRawParameterValue("genPlaningDiatonic")->load() > 0.5f, rng);
            break;
        case 7:
        {
            // Library. The one source that looks a sequence up rather than computing one.
            //
            // A filter that matches nothing falls back to the whole table rather than returning
            // empty. Empty would leave the tray blank with nothing on screen to say why, and the
            // pickers only ever offer words that have rows behind them (`moodsInUse`), so the only
            // way to get here is a *combination* nobody has written yet - "Funky" and "Classical",
            // say - where the honest answer is "not that, but here is something". The band's
            // readout says "no match - any progression" so the fallback is never silent.
            auto rows = chordlib::find(libMood, libGenre, libFunction);
            if (rows.empty())
                rows = chordlib::find({}, {}, -1);

            // **Whole progressions laid end to end, not one looped.** The first cut looped a single
            // row to fill `count`, which `sources::progressions` does with its own templates, and
            // it is wrong here for a reason that only showed up on screen: the library holds vamps,
            // and rolling the two-chord "Minimal one-chord" filled all sixteen tray cards with the
            // same Cm9. Sixteen copies of one chord is not a trayful of candidates, it is one
            // candidate wasting fifteen cells - and the tray exists to let you compare.
            //
            // Laid end to end instead, a "Vamp" filter gives you eight different vamps to audition
            // and a "12-Bar Blues" fills the tray on its own, which is the same rule producing the
            // right answer at both extremes. A row is never cut short: the last one may overrun
            // `count` and is trimmed, but it is the only one that can be, so every progression
            // before it arrives whole.
            //
            // Shuffled rather than taken in table order, because a filtered list is an offer and
            // walking it from the top would make Regen inert under a narrow filter.
            int taken = 0;
            libLastEntry.clear();
            for (int guard = 0; ! rows.empty() && (int) out.size() < count && guard < count + 8; ++guard)
            {
                const size_t pick = (size_t) rng.nextInt((int) rows.size());
                const auto* e = rows[pick];
                // Degrees resolved against **the row's own mode**, not the session's. Every other
                // source passes `mode` here, and it is right for them: they generate *in* that
                // mode, so a chord that falls outside it genuinely is a borrowing. A library row
                // arrives with a mode of its own, and a minor row read against a major session
                // resolves nothing - the tray came back with half its cards labelled "?", which is
                // the numeral saying "outside the key" about a progression that is perfectly
                // in *its* key. `degree` is stored on the pad, so this is also what the strip
                // shows later, and "i bVII bVI" is worth more there than four question marks.
                //
                // Nothing about the pitches moves either way: the numerals are absolute, which is
                // exactly what `ChordLibraryTests.cpp` pins.
                const auto chords = chordlib::chordsFor(*e, root, e->mode, oct);
                if (chords.empty())
                    break;
                out.insert(out.end(), chords.begin(), chords.end());
                if (taken == 0)
                    libLastEntry = e->name;
                ++taken;

                // Drawn without replacement while there is anything left to draw, so a shortlist of
                // six gives six different progressions before any of them comes round again.
                rows.erase(rows.begin() + (long) pick);
                if (rows.empty())
                    rows = chordlib::find(libMood, libGenre, libFunction);
            }
            if (taken > 1)
                libLastEntry << " +" << juce::String(taken - 1);
            out.resize((size_t) juce::jlimit(0, (int) out.size(), count));
            break;
        }
        default:
            // Algorithmic, and the fallback for a source index this build does not know - a
            // session from a later version could carry one, and the weighted pool is the safe
            // thing to answer with.
            out = chordgen::generate(root, mode, count, currentOptions(), lockedTypesOnPage(), rng);
            break;
    }

    // Bias the thirds, then fit the voicing, then smooth it, and the order is load-bearing in
    // both joins. Bias first because it changes *which* notes a chord holds while the other two
    // only move them about. Smoothing last because fitVoicing moves whole chords between
    // octaves, so running it afterwards would undo exactly what the smoothing pass worked out.
    applyMajorMinorBias(out);
    fitVoicing(out);
    sources::applyVoiceLeading(out, a.getRawParameterValue("genSmooth")->load() * 0.01f);
    return out;
}

// The same pass for Markov, which cannot go through generateChords: its chords carry a numeral
// ChordGen has no field for, so they arrive already built as pads. Round-trips the notes through
// a throwaway Chord vector rather than duplicating the algorithm, and copies only the notes back,
// so the numeral and name survive. Voice leading never changes which pitch classes a chord holds,
// only their register, so a name written before this runs is still right after it.
// fitVoicing for the Markov path, which arrives as pads rather than Chords. Round-trips through
// a throwaway Chord vector exactly as smoothPads does, and copies back the notes plus the name,
// because unlike smoothing this pass *does* change which notes a chord holds.
void ChordGenMenu::fitPads(std::vector<KeysProcessor::ChordPad>& pads)
{
    std::vector<chordgen::Chord> tmp;
    tmp.reserve(pads.size());
    for (const auto& p : pads)
    {
        chordgen::Chord c;
        c.rootPc = p.rootPc;
        c.type = p.type;
        c.notes = p.notes;
        tmp.push_back(std::move(c));
    }
    applyMajorMinorBias(tmp); // same order as generateChords: bias, then fit
    fitVoicing(tmp);
    for (size_t i = 0; i < pads.size() && i < tmp.size(); ++i)
    {
        pads[i].notes = tmp[i].notes;
        pads[i].name = chords::detect(tmp[i].notes);
    }
}

void ChordGenMenu::smoothPads(std::vector<KeysProcessor::ChordPad>& pads) const
{
    const float amount = processor.apvts.getRawParameterValue("genSmooth")->load() * 0.01f;
    if (amount <= 0.0f || pads.size() < 2)
        return;

    std::vector<chordgen::Chord> tmp;
    tmp.reserve(pads.size());
    for (const auto& p : pads)
    {
        chordgen::Chord c;
        c.rootPc = p.rootPc;
        c.type = p.type;
        c.notes = p.notes;
        tmp.push_back(std::move(c));
    }
    sources::applyVoiceLeading(tmp, amount);
    for (size_t i = 0; i < pads.size() && i < tmp.size(); ++i)
        pads[i].notes = tmp[i].notes;
}

int ChordGenMenu::chainMode() const
{
    return juce::jlimit(0, 2, (int) processor.apvts.getRawParameterValue("markovMode")->load());
}

// The Mood tags belong to the chain that is up: one picked under another chain would filter the
// corpus down to nothing and the page would come back empty. Checked where the mood is *used*
// rather than where it is set, because the chain can move underneath it - host automation, a
// session load, or simply the window being shut while the combo box was showing a stale tag.
// An empty string is the "Any" sentinel everywhere, and `contains` says false for it, so an
// unset mood falls through this unchanged.
juce::String ChordGenMenu::moodForChain() const
{
    return markov::moodsFor(chainMode()).contains(mood) ? mood : juce::String();
}

int ChordGenMenu::genRoot() const
{
    if (rolledRoot >= 0)
        return rolledRoot; // Key is unticked; rollFreeChoices picked one for this generation
    return (int) processor.apvts.getRawParameterValue("genRoot")->load();
}

int ChordGenMenu::genMode() const
{
    if (rolledMode >= 0)
        return rolledMode; // Mode is unticked
    return juce::jlimit(0, modes::count() - 1, (int) processor.apvts.getRawParameterValue("genMode")->load());
}

// ---------------------------------------------------------------------------------------
// The brain. It reads the parameters rather than any control, which is why its surface has
// been a full-screen overlay, an inline band, a menu and now a window without a line of this
// half changing.
// ---------------------------------------------------------------------------------------

chordgen::Options ChordGenMenu::currentOptions() const
{
    const auto& a = processor.apvts;
    const auto on = [&a](const char* id) { return a.getRawParameterValue(id)->load() > 0.5f; };

    chordgen::Options o;
    o.octave = (int) a.getRawParameterValue("genOctave")->load();
    // Every type the pool knows, always. This used to be filtered by the three note-count tick
    // boxes; since 2026-08-01 the Notes range decides how many notes a chord ends up with, in
    // `fitVoicing`, after the source has chosen it. Filtering here as well would mean a request
    // for five notes could only ever be met by a type that already had five, which is a much
    // narrower pool than "any chord, grown to five".
    o.noteCounts = { 3, 4, 5 };
    o.inversions.clear();
    if (on("genInv0")) o.inversions.push_back(0);
    if (on("genInv1")) o.inversions.push_back(1);
    if (on("genInv2")) o.inversions.push_back(2);
    if (on("genInv3")) o.inversions.push_back(3);
    if (o.inversions.empty())
        o.inversions = { 0 };
    // Free means a different amount of straying every generation, which is what "the generator
    // decides" has to mean for a dial: pinning it to 0 or 100 would just be a third fixed value.
    o.scaleCompliance = constrains("genUseCompliance")
                            ? a.getRawParameterValue("genCompliance")->load() * 0.01f
                            : rng.nextFloat();
    o.lockInfluence = a.getRawParameterValue("genLockInfluence")->load() * 0.01f;
    return o;
}

std::vector<int> ChordGenMenu::lockedTypesOnPage() const
{
    std::vector<int> out;
    const int offset = processor.padPageOffset();
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
    {
        const auto& pad = processor.chordPad(offset + v);
        if (pad.locked && ! pad.notes.empty() && pad.type >= 0)
            out.push_back(pad.type);
    }
    return out;
}

void ChordGenMenu::writeChord(int slot, const chordgen::Chord& c)
{
    KeysProcessor::ChordPad pad;
    pad.notes = c.notes;
    pad.name = chords::detect(c.notes); // name it the way the live card would, not by type name
    pad.rootPc = c.rootPc;
    pad.type = c.type;
    pad.degree = c.degree;
    pad.locked = processor.chordPad(slot).locked;
    processor.setChordPad(slot, pad);
}

void ChordGenMenu::previewChord(const std::vector<int>& notes)
{
    stopPreview();

    // Choke every other chord source before sounding this one, and it is not a nicety (Owen,
    // 2026-08-01: "when I drag a chord from the main window to this window and then click on it,
    // it doesn't play. And some of the generated chords sound like they're only one note even
    // though they're saying there's three").
    //
    // Both of those were one bug, and it was this rule working exactly as designed. Keys emits
    // one note-on per *pitch* and only on the 0->1 transition of noteRefs, so that releasing one
    // owner can never silence another's notes. An audition asking for a pitch some pad is already
    // holding therefore emits nothing at all. With Sustain on, a pad left ringing made the whole
    // audition silent when the chords matched, and made it sound like one note when they merely
    // overlapped: you heard only the pitches the pad did not already own. Nothing was wrong with
    // the chord, which is why the card could truthfully list three notes and play one.
    //
    // An audition is a monitor, not a performance, so it takes the room. This is the same call
    // Exclusive makes and it reaches the same three places - the pads, the live card, and a chord
    // held into the arp - because any of them can own a pitch this needs. Unconditional rather
    // than only-when-they-collide: which pitches overlap is invisible, and a "hear this chord"
    // button that works or does not depending on an overlap you cannot see is the bug again in a
    // quieter form. The cost is that auditioning stops a chord you were deliberately sustaining,
    // which is a click to put back and worth paying for a button that always does what it says.
    processor.stopAllChordPads();

    const float vel = processor.baseVelocity01();
    for (const int n : notes)
        processor.noteOn(n, vel); // Humanize colours the audition like everything else
    previewNotes = notes;
    startTimer(800); // Octavium's preview length; the callback releases it
}

void ChordGenMenu::stopPreview()
{
    stopTimer();
    for (const int n : previewNotes)
        processor.noteOff(n);
    previewNotes.clear();
}

void ChordGenMenu::timerCallback()
{
    stopPreview(); // the only thing on a clock here: an audition that has had its 800 ms
}

void ChordGenMenu::regeneratePageMarkov()
{
    // One entry for the whole page, not one per pad: generation is a single
    // gesture and the pads it writes are what you would want back together.
    const KeysProcessor::UndoGesture undoable { processor, "Regenerate page",
                                               KeysProcessor::UndoScope::pads };

    rollFreeChoices();
    // Octavium regenerates unlocked cards left to right, each stepping the chain
    // from its (possibly just-updated) left neighbour, so changes cascade.
    const int offset = processor.padPageOffset();
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
    {
        const int slot = offset + v;
        const auto& pad = processor.chordPad(slot);
        if (! pad.locked && ! pad.notes.empty())
            regeneratePadMarkov(slot);
    }
}

void ChordGenMenu::fillPageMarkov()
{
    // One entry for the whole page, not one per pad: generation is a single
    // gesture and the pads it writes are what you would want back together.
    const KeysProcessor::UndoGesture undoable { processor, "Fill page",
                                               KeysProcessor::UndoScope::pads };

    rollFreeChoices();
    const auto targets = emptyPadsOnPage();
    if (targets.empty())
        return;

    const auto generated = markov::generate(chainMode(), genRoot(), currentOptions().octave,
                                            (int) processor.apvts.getRawParameterValue("markovLength")->load(),
                                            processor.apvts.getRawParameterValue("markovTemp")->load(),
                                            moodForChain(), start, (int) targets.size(), rng);
    for (int i = 0; i < (int) targets.size() && i < (int) generated.size(); ++i)
    {
        const auto& c = generated[(size_t) i];
        KeysProcessor::ChordPad pad;
        pad.notes = c.notes;
        pad.name = chords::detect(c.notes);
        pad.rootPc = c.rootPc;
        pad.type = c.type;
        pad.numeral = c.numeral;
        pad.locked = processor.chordPad(targets[(size_t) i]).locked;
        processor.setChordPad(targets[(size_t) i], pad);
    }
}

void ChordGenMenu::regeneratePadMarkov(int slot)
{
    rollFreeChoices();
    const int offset = processor.padPageOffset();
    // The chain steps from the pad to the left on this page; the first pad restarts.
    juce::String predecessor;
    if (slot > offset)
        predecessor = processor.chordPad(slot - 1).numeral;

    const auto c = markov::regenerateSingle(chainMode(), genRoot(), currentOptions().octave,
                                            predecessor, processor.chordPad(slot).numeral,
                                            processor.apvts.getRawParameterValue("markovTemp")->load(),
                                            moodForChain(), rng);
    KeysProcessor::ChordPad pad;
    pad.notes = c.notes;
    pad.name = chords::detect(c.notes);
    pad.rootPc = c.rootPc;
    pad.type = c.type;
    pad.numeral = c.numeral;
    pad.locked = processor.chordPad(slot).locked;
    processor.setChordPad(slot, pad);
}

// Candidates for the audition tray: the same two brains, the same settings, and no slot. This
// is the only generator path that builds a ChordPad it does not hand to the processor, which is
// why the pad-building is repeated here rather than shared with writeChord - that one preserves
// the target slot's lock, and a candidate has no target yet.
std::vector<KeysProcessor::ChordPad> ChordGenMenu::generateCandidates(int count)
{
    std::vector<KeysProcessor::ChordPad> out;
    if (count <= 0)
        return out;
    out.reserve((size_t) count);

    rollFreeChoices(); // Markov reads genRoot below, so this must precede it too
    if (markovActive())
    {
        // One walk of the chain rather than `count` independent first chords, so the tray reads
        // as a progression you could take in order - which is what a Markov corpus is for, and
        // what fillPageMarkov already does with the page.
        const auto generated = markov::generate(chainMode(), genRoot(), currentOptions().octave,
                                                (int) processor.apvts.getRawParameterValue("markovLength")->load(),
                                                processor.apvts.getRawParameterValue("markovTemp")->load(),
                                                moodForChain(), start, count, rng);
        for (const auto& c : generated)
        {
            KeysProcessor::ChordPad pad;
            pad.notes = c.notes;
            pad.name = chords::detect(c.notes);
            pad.rootPc = c.rootPc;
            pad.type = c.type;
            pad.numeral = c.numeral;
            out.push_back(std::move(pad));
        }
        fitPads(out);    // note count and register, every source including this one
        smoothPads(out); // and then the smoothing, in that order for the reason fitVoicing gives
        return out;
    }

    for (const auto& c : generateChords(count))
    {
        KeysProcessor::ChordPad pad;
        pad.notes = c.notes;
        pad.name = chords::detect(c.notes);
        pad.rootPc = c.rootPc;
        pad.type = c.type;
        pad.degree = c.degree;
        out.push_back(std::move(pad));
    }
    return out;
}

// The two seeded answers behind the tray's card menu. Both take the chord you are pointing at
// and hand back a trayful built from it, and the split between them is the whole reason there
// are two: "more like this" is a question about *colour* and keeps the root, "what comes next"
// is a question about *motion* and changes it.
std::vector<KeysProcessor::ChordPad> ChordGenMenu::similarTo(const std::vector<int>& seed, int count)
{
    std::vector<KeysProcessor::ChordPad> out;
    if (seed.empty() || count <= 0)
        return out;

    const int rootPc = suggest::analyse(seed).first;
    const int octave = seed.front() / 12; // the seed's own register, so the family sits with it

    // Same root, every colour chordgen knows: the triad, the sevenths, the sus, the ninths, on
    // down the type table. Deliberately not `suggest::all` - that table moves the root, which is
    // exactly what "similar" must not do.
    const auto& types = chordgen::types();
    for (int pass = 0; pass < 3 && (int) out.size() < count; ++pass)
    {
        // Later passes drop an octave and then climb one, so a request for more variations than
        // there are chord types answers with the same colours in a different register rather
        // than running out and leaving the tray half empty.
        const int oct = octave + (pass == 1 ? -1 : pass == 2 ? 1 : 0);
        for (int t = 0; t < (int) types.size() && (int) out.size() < count; ++t)
        {
            auto notes = chordgen::chordNotes(rootPc, t, oct);
            if (notes.empty() || notes == seed || notes.back() > 127 || notes.front() < 0)
                continue; // a chord is not one of its own neighbours, and neither is a silent one

            KeysProcessor::ChordPad pad;
            pad.name = chords::detect(notes);
            pad.notes = std::move(notes);
            pad.rootPc = rootPc;
            pad.type = t;
            out.push_back(std::move(pad));
        }
    }
    return out;
}

std::vector<KeysProcessor::ChordPad> ChordGenMenu::couldFollow(const std::vector<int>& seed, int count)
{
    std::vector<KeysProcessor::ChordPad> out;
    if (seed.empty() || count <= 0)
        return out;

    // Straight onto `suggest::all`, which is the same eighteen moves the pad card menu offers
    // under "Next: could follow". One table, one opinion: a second answer to "what comes after
    // this" that disagreed with the first would be a bug wearing two hats.
    const auto [rootPc, type] = suggest::analyse(seed);
    for (const auto& s : suggest::all(rootPc, type, seed.front() / 12))
    {
        if ((int) out.size() >= count)
            break;
        KeysProcessor::ChordPad pad;
        pad.notes = s.notes;
        pad.name = chords::detect(s.notes);
        pad.rootPc = s.rootPc;
        pad.type = s.type;
        out.push_back(std::move(pad));
    }
    return out;
}

std::vector<int> ChordGenMenu::emptyPadsOnPage() const
{
    std::vector<int> out;
    const int offset = processor.padPageOffset();
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
        if (processor.chordPad(offset + v).notes.empty())
            out.push_back(offset + v);
    return out;
}

std::vector<int> ChordGenMenu::regeneratablePadsOnPage() const
{
    std::vector<int> out;
    const int offset = processor.padPageOffset();
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
    {
        const auto& pad = processor.chordPad(offset + v);
        if (! pad.locked && ! pad.notes.empty())
            out.push_back(offset + v);
    }
    return out;
}

// Fill: the empty pads, and only ever the empty pads (Owen, 2026-07-30 - "new generations
// shouldn't overwrite existing"). It used to write every unlocked slot on the page, which
// made the one constructive button on the bar the fastest way to lose sixteen chords, with no
// undo behind it and only a lock as protection - and locking each of fifteen keepers to
// generate a sixteenth is not a way anyone works. Filling blanks needs no protection at all,
// so this asks for none: locked or not is beside the point when the slot is empty.
void ChordGenMenu::fillPage()
{
    // One entry for the whole page, not one per pad: generation is a single
    // gesture and the pads it writes are what you would want back together.
    const KeysProcessor::UndoGesture undoable { processor, "Fill page",
                                               KeysProcessor::UndoScope::pads };

    if (markovActive())
    {
        fillPageMarkov();
        return;
    }

    const auto targets = emptyPadsOnPage();
    if (targets.empty())
        return;

    const auto chords = generateChords((int) targets.size());
    for (int i = 0; i < (int) targets.size() && i < (int) chords.size(); ++i)
        writeChord(targets[(size_t) i], chords[(size_t) i]);
}

// Regen: the destructive one, and the only one. It rerolls the pads that already carry a
// chord and skips the locked ones, which is the whole point of it - replacing what is there
// is what "regenerate" means, and the lock is what says "not this one". A blank is left blank;
// Fill is what blanks are for.
void ChordGenMenu::regeneratePage()
{
    // One entry for the whole page, not one per pad: generation is a single
    // gesture and the pads it writes are what you would want back together.
    const KeysProcessor::UndoGesture undoable { processor, "Regenerate page",
                                               KeysProcessor::UndoScope::pads };

    if (markovActive())
    {
        regeneratePageMarkov();
        return;
    }

    const auto targets = regeneratablePadsOnPage();
    if (targets.empty())
        return;

    const auto chords = generateChords((int) targets.size());
    for (int i = 0; i < (int) targets.size() && i < (int) chords.size(); ++i)
        writeChord(targets[(size_t) i], chords[(size_t) i]);
}

void ChordGenMenu::regeneratePad(int slot)
{
    const auto& pad = processor.chordPad(slot);
    if (pad.locked)
        return;
    // A Markov pad regenerates through its chain regardless of the Source setting: the
    // numeral is the pad's provenance, the way degree is for an algorithmic pad.
    if (pad.numeral.isNotEmpty())
    {
        regeneratePadMarkov(slot);
        return;
    }
    const auto c = chordgen::generateSingle(genRoot(), genMode(), pad.degree, pad.type,
                                            currentOptions(), lockedTypesOnPage(), rng);
    writeChord(slot, c);
}

void ChordGenMenu::newChordFor(int slot)
{
    const auto& pad = processor.chordPad(slot);
    if (pad.locked)
        return;
    if (! pad.notes.empty())
    {
        regeneratePad(slot);
        return;
    }
    // An empty slot gets a fresh chord from whichever brain is up; a Markov one
    // steps from its left neighbour, exactly like a page fill would.
    if (markovActive())
    {
        regeneratePadMarkov(slot);
        return;
    }
    const auto generated = generateChords(1);
    if (! generated.empty())
        writeChord(slot, generated.front());
}

// The generator's half of a pad's card menu, added to the menu the pad strip is already
// building. It closes the **this pad** group, which ChordPads opened with Edit, Clear pad,
// Lock, and the octave and voicing items.
//
// Only the two per-card actions are here. Everything about the *page* or the settings is in
// the generator's window (ChordGenPanel), and this is the reason these two are not: New chord
// and the suggestions are questions about the card under the mouse, so they belong on the menu
// that card already opens. They are offered on every pad on every page whatever the window is
// doing, because this object outlives it.
//
// Lock is not here either: it needs nothing from the generator and belongs to the card itself,
// so ChordPads offers it.
void ChordGenMenu::addPadMenuItems(int slot, juce::PopupMenu& menu)
{
    const auto& pad = processor.chordPad(slot);
    const bool filled = ! pad.notes.empty();
    juce::WeakReference<ChordGenMenu> safe(this);

    lastSuggestions.clear();
    lastSuggestTarget = -1;

    // Greyed on a locked card, and on the card the keyboard is editing. The second is the
    // same rule Octave down/up and Next voicing follow on this menu: while the link lasts the
    // keybed writes that pad on every latch change, so a chord generated into it survives
    // until the next click on a key and then goes, silently and with no undo.
    menu.addItem(idNewChord, "New chord", ! pad.locked && slot != editingSlot);

    // "What could follow this?" - the four suggestion families, each row carrying a
    // play button so it can audition without closing the menu (Octavium's per-row
    // preview). A pick lands in the first free pad on the page rather than replacing
    // the chord you asked about, and *only* ever in a free one: with the page full it used to
    // fall through to the slot right after this one, and writeChord replaces what is there, so
    // the one path left that could lose a chord you had was this one. A full page greys the
    // row instead - the same answer Fill gives when it has nowhere to write.
    //
    // Free means empty, lock or no lock. That is the one definition in the generator
    // (`emptyPadsOnPage`, which Fill uses): a lock protects a chord, and a blank slot has no
    // chord to protect. The search here used to demand empty *and* unlocked, which disagreed
    // with the helper next to it about the same page.
    // The card being edited is not a landing site either, for the reason New chord is greyed
    // on it: the keybed owns that pad until the link ends.
    const auto blanks = emptyPadsOnPage();
    const auto free = std::find_if(blanks.begin(), blanks.end(),
                                   [this](int s) { return s != editingSlot; });
    const int target = filled && free != blanks.end() ? *free : -1;
    // One row, four families inside it. The families were four rows of their own with a
    // section header over them until 2026-07-30, which is five rows for a path that is
    // explored occasionally and never in a hurry - and rows were what the menu had run out of.
    // The suggestions are the one thing here three levels deep, and they are the right thing
    // to spend that on: the extra hover buys back four rows for the items that are used
    // constantly. Kept as a greyed row rather than dropped when there is nothing to suggest,
    // so the menu is the same shape on every card.
    juce::PopupMenu next;
    if (filled && target >= 0)
    {
        // A generated pad already knows what it is; a hand-captured one gets worked out here.
        auto [rootPc, type] = pad.type >= 0 ? std::pair<int, int> { pad.rootPc, pad.type }
                                            : suggest::analyse(pad.notes);
        const auto suggestions = suggest::all(rootPc, type, octaveOf(pad.notes, currentOptions().octave));

        const char* categories[] = { "Neo-Riemannian", "Circle of Fifths", "Diatonic", "Chromatic" };
        int id = idSuggestBase;
        for (const char* cat : categories)
        {
            juce::PopupMenu sub;
            for (const auto& s : suggestions)
            {
                if (juce::String(s.category) != cat)
                    continue;
                sub.addCustomItem(id++,
                                  std::make_unique<SuggestionRow>(juce::String(s.transform) + "   " + s.name,
                                                                  [safe, notes = s.notes]
                                                                  {
                                                                      if (safe != nullptr)
                                                                          safe->previewChord(notes);
                                                                  }));
                lastSuggestions.push_back(s);
            }
            next.addSubMenu(cat, sub);
        }
    }
    menu.addSubMenu("Next: could follow", next, filled && target >= 0);
    lastSuggestTarget = target;
}

// The other half: what to do with a choice from those items. The menu is shown and dismissed
// by ChordPads, which knows nothing about suggestions, so the list built above is held here
// between the two calls - both happen on the message thread, one after the other, and a second
// menu rebuilds it before it can be read.
void ChordGenMenu::handlePadMenuChoice(int slot, int id)
{
    stopPreview(); // don't let the last audition ring past the menu
    if (id == idNewChord)
    {
        newChordFor(slot);
        return;
    }
    const int index = id - idSuggestBase;
    if (index < 0 || index >= (int) lastSuggestions.size() || lastSuggestTarget < 0)
        return;
    const auto& s = lastSuggestions[(size_t) index];
    writeChord(lastSuggestTarget, { s.rootPc, s.type, s.notes, -1 });
}
} // namespace keys
