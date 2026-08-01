#include "ChordGenMenu.h"
#include "../ChordMarkov.h"
#include "../ChordSuggest.h"
#include "../Chords.h"
#include "../ScaleModes.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>
#include <algorithm>

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

bool ChordGenMenu::markovActive() const
{
    return (int) processor.apvts.getRawParameterValue("genSource")->load() == 1;
}

bool ChordGenMenu::readsScaleSettings() const { return ! markovActive(); }

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
    return (int) processor.apvts.getRawParameterValue("genRoot")->load();
}

int ChordGenMenu::genMode() const
{
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
    o.noteCounts.clear();
    if (on("genTriads"))   o.noteCounts.push_back(3);
    if (on("genSevenths")) o.noteCounts.push_back(4);
    if (on("genNinths"))   o.noteCounts.push_back(5);
    if (o.noteCounts.empty())
        o.noteCounts = { 3 }; // unticking everything would generate nothing; triads are the floor
    o.inversions.clear();
    if (on("genInv0")) o.inversions.push_back(0);
    if (on("genInv1")) o.inversions.push_back(1);
    if (on("genInv2")) o.inversions.push_back(2);
    if (on("genInv3")) o.inversions.push_back(3);
    if (o.inversions.empty())
        o.inversions = { 0 };
    o.scaleCompliance = a.getRawParameterValue("genCompliance")->load() * 0.01f;
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
    if (markovActive())
    {
        fillPageMarkov();
        return;
    }

    const auto targets = emptyPadsOnPage();
    if (targets.empty())
        return;

    const auto chords = chordgen::generate(genRoot(), genMode(), (int) targets.size(),
                                           currentOptions(), lockedTypesOnPage(), rng);
    for (int i = 0; i < (int) targets.size() && i < (int) chords.size(); ++i)
        writeChord(targets[(size_t) i], chords[(size_t) i]);
}

// Regen: the destructive one, and the only one. It rerolls the pads that already carry a
// chord and skips the locked ones, which is the whole point of it - replacing what is there
// is what "regenerate" means, and the lock is what says "not this one". A blank is left blank;
// Fill is what blanks are for.
void ChordGenMenu::regeneratePage()
{
    if (markovActive())
    {
        regeneratePageMarkov();
        return;
    }

    const auto targets = regeneratablePadsOnPage();
    if (targets.empty())
        return;

    const auto chords = chordgen::generate(genRoot(), genMode(), (int) targets.size(),
                                           currentOptions(), lockedTypesOnPage(), rng);
    for (int i = 0; i < (int) targets.size() && i < (int) chords.size(); ++i)
        writeChord(targets[(size_t) i], chords[(size_t) i]);
}

void ChordGenMenu::clearPage()
{
    const int offset = processor.padPageOffset();
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
        if (! processor.chordPad(offset + v).locked) // Clear spares locks too, like Regen
            processor.clearChordPad(offset + v);
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
    const auto generated = chordgen::generate(genRoot(), genMode(), 1, currentOptions(),
                                              lockedTypesOnPage(), rng);
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
