#include "ChordGenMenu.h"
#include "../ChordMarkov.h"
#include "../ChordSuggest.h"
#include "../Chords.h"
#include "../ScaleModes.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>
#include <okstudio/Scales.h>

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

int ChordGenMenu::genRoot() const
{
    return (int) processor.apvts.getRawParameterValue("genRoot")->load();
}

int ChordGenMenu::genMode() const
{
    return juce::jlimit(0, modes::count() - 1, (int) processor.apvts.getRawParameterValue("genMode")->load());
}

// ---------------------------------------------------------------------------------------
// The settings menu. Every control the generator's panel used to carry lives here as a
// submenu of discrete values: a PopupMenu cannot hold a slider, and a mouse-only plugin has
// no business asking for a drag inside a menu anyway.
// ---------------------------------------------------------------------------------------

ChordGenMenu::Ladder ChordGenMenu::ladder(std::initializer_list<float> values,
                                          const juce::String& suffix, int decimals)
{
    Ladder l;
    for (const float v : values)
    {
        l.values.push_back(v);
        l.labels.add(juce::String(v, decimals) + suffix);
    }
    return l;
}

ChordGenMenu::Ladder ChordGenMenu::indexed(const juce::StringArray& names)
{
    Ladder l;
    l.labels = names;
    for (int i = 0; i < names.size(); ++i)
        l.values.push_back((float) i);
    return l;
}

ChordGenMenu::Ladder ChordGenMenu::modeLadder()
{
    // Each mode carries the character it plays in, which the panel showed for the current
    // mode only. In a list it does better work: it is there while you are choosing.
    Ladder l;
    for (int i = 0; i < modes::count(); ++i)
    {
        l.values.push_back((float) i);
        l.labels.add(juce::String(modes::get(i).name) + "   " + modes::get(i).emotion);
    }
    return l;
}

int ChordGenMenu::addSetting(Setting s)
{
    lastSettings.push_back(std::move(s));
    return idSettingsBase + (int) lastSettings.size() - 1;
}

void ChordGenMenu::setParam(const char* param, float value)
{
    if (auto* p = processor.apvts.getParameter(param))
        p->setValueNotifyingHost(p->convertTo0to1(value));
}

void ChordGenMenu::applySetting(const Setting& s)
{
    if (s.text != nullptr)
    {
        *s.text = s.textValue;
        return;
    }
    if (s.param == nullptr)
        return;
    setParam(s.param, s.toggle ? (processor.apvts.getRawParameterValue(s.param)->load() > 0.5f ? 0.0f : 1.0f)
                               : s.value);
}

void ChordGenMenu::addChoice(juce::PopupMenu& parent, const juce::String& name, const char* param,
                             const Ladder& l, bool enabled, const juce::StringArray& shortLabels)
{
    // The nearest value, not the equal one: these are a handful of steps out of a continuous
    // parameter, and host automation can leave it anywhere between two of them. Nearest means
    // exactly one item is always ticked, so the menu can be read as a display.
    const float now = processor.apvts.getRawParameterValue(param)->load();
    int live = 0;
    for (int i = 1; i < (int) l.values.size(); ++i)
        if (std::abs(l.values[(size_t) i] - now) < std::abs(l.values[(size_t) live] - now))
            live = i;

    juce::PopupMenu sub;
    for (int i = 0; i < l.labels.size(); ++i)
    {
        Setting s;
        s.param = param;
        s.value = l.values[(size_t) i];
        sub.addItem(addSetting(s), l.labels[i], enabled, i == live);
    }

    const auto& shown = shortLabels.isEmpty() ? l.labels : shortLabels;
    parent.addSubMenu(name + ":  " + shown[live], sub, enabled);
}

void ChordGenMenu::addToggles(juce::PopupMenu& parent, const juce::String& name,
                              const std::vector<const char*>& params, const juce::StringArray& labels,
                              const juce::String& fallback, bool enabled)
{
    juce::PopupMenu sub;
    juce::StringArray on;
    for (int i = 0; i < labels.size(); ++i)
    {
        const bool lit = processor.apvts.getRawParameterValue(params[(size_t) i])->load() > 0.5f;
        if (lit)
            on.add(labels[i]);
        Setting s;
        s.param = params[(size_t) i];
        s.toggle = true;
        sub.addItem(addSetting(s), labels[i], enabled, lit);
    }
    // Untick everything and generation still has to make something; currentOptions() falls
    // back, and the parent item says what it falls back to rather than reading "none".
    parent.addSubMenu(name + ":  " + (on.isEmpty() ? fallback : on.joinIntoString(", ")), sub, enabled);
}

void ChordGenMenu::addTextChoice(juce::PopupMenu& parent, const juce::String& name, juce::String& target,
                                 const juce::StringArray& choices, bool enabled)
{
    juce::StringArray items { "Any" };
    items.addArray(choices);

    juce::PopupMenu sub;
    for (const auto& c : items)
    {
        Setting s;
        s.text = &target;
        s.textValue = c == "Any" ? juce::String() : c; // empty is the "Any" sentinel everywhere
        const bool ticked = s.textValue == target;
        sub.addItem(addSetting(s), c, enabled, ticked);
    }
    parent.addSubMenu(name + ":  " + (target.isEmpty() ? "Any" : target), sub, enabled);
}

void ChordGenMenu::addSettingsItems(juce::PopupMenu& m)
{
    const bool markov = markovActive();

    // The Mood tags belong to the chain that is up: one picked under another chain would
    // filter the corpus down to nothing. The combo box used to rebuild its list on every
    // chain change; there is no list to keep now, only this.
    const auto moods = markov::moodsFor(chainMode());
    if (mood.isNotEmpty() && ! moods.contains(mood))
        mood.clear();

    juce::StringArray starts;
    for (const char* token : markov::startTokens())
        starts.add(token);

    addChoice(m, "Source", "genSource", indexed({ "Algorithmic", "Markov" }), true);

    // Key and Octave feed both brains. Everything under them is the weighted pool's, so it is
    // greyed while the chains are up rather than left clickable and silently ignored (which is
    // what Octavium did).
    //
    // Key, Mode and Scale Compliance are also combo boxes on the Pads bar. They are here as
    // well and not only there: the bar is the fast path, this is the complete one, and both
    // are attachments on the same parameter so a change in either shows in the other.
    addChoice(m, "Key", "genRoot", indexed(okstudio::scales::noteNames()), true);
    addChoice(m, "Octave", "genOctave", ladder({ 2, 3, 4, 5, 6 }), true);
    addChoice(m, "Mode", "genMode", modeLadder(), ! markov, modes::names());
    addToggles(m, "Notes", { "genTriads", "genSevenths", "genNinths" }, { "3", "4", "5" }, "3", ! markov);
    addToggles(m, "Inversions", { "genInv0", "genInv1", "genInv2", "genInv3" },
               { "Root", "1st", "2nd", "3rd" }, "Root", ! markov);
    addChoice(m, "Scale Compliance", "genCompliance", ladder({ 0, 25, 50, 75, 100 }, " %"), ! markov);
    addChoice(m, "Lock Influence", "genLockInfluence", ladder({ 0, 25, 50, 75, 100 }, " %"), ! markov);

    // The one group that keeps a level of its own. Flattening these five onto the pad menu too
    // would push it past the bottom of a 1080p screen, and they are the least-reached settings
    // in the plugin: every one of them is inert until Source is Markov, which is not the
    // default. The parent stays clickable whatever Source says, so the values can still be read
    // and set up before switching over - it is the items inside that grey, exactly as before.
    juce::PopupMenu chains;
    addChoice(chains, "Chain", "markovMode", indexed({ "Major", "Minor", "Modal" }), markov);
    addTextChoice(chains, "Mood", mood, moods, markov);
    addTextChoice(chains, "Start", start, starts, markov);
    addChoice(chains, "Temperature", "markovTemp", ladder({ 0.4f, 0.7f, 1.0f, 1.4f, 2.0f }, {}, 1), markov);
    addChoice(chains, "Length", "markovLength", ladder({ 4, 6, 8, 12, 16 }), markov);
    m.addSubMenu("Markov chains", chains);
}

// ---------------------------------------------------------------------------------------
// The brain. Unchanged by the panel going away: it always read the parameters rather than
// the controls, which is why the controls could become a menu.
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

void ChordGenMenu::fillPageMarkov(bool onlyUnlocked)
{
    const int offset = processor.padPageOffset();

    if (onlyUnlocked)
    {
        // Octavium regenerates unlocked cards left to right, each stepping the chain
        // from its (possibly just-updated) left neighbour, so changes cascade.
        for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
        {
            const int slot = offset + v;
            const auto& pad = processor.chordPad(slot);
            if (! pad.locked && ! pad.notes.empty())
                regeneratePadMarkov(slot);
        }
        return;
    }

    std::vector<int> targets;
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
        if (! processor.chordPad(offset + v).locked)
            targets.push_back(offset + v);
    if (targets.empty())
        return;

    const auto generated = markov::generate(chainMode(), genRoot(), currentOptions().octave,
                                            (int) processor.apvts.getRawParameterValue("markovLength")->load(),
                                            processor.apvts.getRawParameterValue("markovTemp")->load(),
                                            mood, start, (int) targets.size(), rng);
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
                                            mood, rng);
    KeysProcessor::ChordPad pad;
    pad.notes = c.notes;
    pad.name = chords::detect(c.notes);
    pad.rootPc = c.rootPc;
    pad.type = c.type;
    pad.numeral = c.numeral;
    pad.locked = processor.chordPad(slot).locked;
    processor.setChordPad(slot, pad);
}

void ChordGenMenu::fillPage(bool onlyUnlocked)
{
    if (markovActive())
    {
        fillPageMarkov(onlyUnlocked);
        return;
    }

    const int offset = processor.padPageOffset();
    const auto opts = currentOptions();
    const auto locked = lockedTypesOnPage();

    // Which slots we're allowed to write. Fill replaces everything except locks; Regen only
    // touches unlocked slots. Either way a locked pad is never overwritten.
    std::vector<int> targets;
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
    {
        const int slot = offset + v;
        if (processor.chordPad(slot).locked)
            continue;
        if (onlyUnlocked && processor.chordPad(slot).notes.empty())
            continue; // Regen refreshes what's there; it doesn't fill blanks
        targets.push_back(slot);
    }
    if (targets.empty())
        return;

    const auto chords = chordgen::generate(genRoot(), genMode(), (int) targets.size(), opts, locked, rng);
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
// building. Lock is not here: it needs nothing from the generator and belongs to the card
// itself, so ChordPads offers it either way. Everything else the generator has is here, and
// it is offered on every pad on every page, because there is no longer a view whose absence
// could take it away.
//
// Three groups, in the order the mouse wants them and each under a section header: what acts
// on **this pad** (ChordPads opened that group, this continues it with New chord and Next),
// what acts on **this page**, then the **settings**. Everything is two levels deep at most.
void ChordGenMenu::addPadMenuItems(int slot, juce::PopupMenu& menu)
{
    const auto& pad = processor.chordPad(slot);
    const bool filled = ! pad.notes.empty();
    const int offset = processor.padPageOffset();
    juce::WeakReference<ChordGenMenu> safe(this);

    lastSuggestions.clear();
    lastSuggestTarget = -1;
    lastSettings.clear();

    // No separator first: New chord and Next act on the card ChordPads has just offered Edit,
    // Clear pad and Lock for, so they belong inside that group rather than after a rule.
    menu.addItem(idNewChord, "New chord", ! pad.locked);

    // "What could follow this?" - the four suggestion families, each row carrying a
    // play button so it can audition without closing the menu (Octavium's per-row
    // preview). A pick lands in the next free pad on the page rather than replacing
    // the chord you asked about; with the page full it takes the slot right after,
    // which is where a progression would go anyway.
    int target = -1;
    if (filled)
    {
        for (int v = 0; v < KeysProcessor::padsPerPage && target < 0; ++v)
        {
            const int s = offset + v;
            if (processor.chordPad(s).notes.empty() && ! processor.chordPad(s).locked)
                target = s;
        }
        if (target < 0)
        {
            const int after = slot + 1 < offset + KeysProcessor::padsPerPage ? slot + 1 : offset;
            target = processor.chordPad(after).locked ? -1 : after;
        }
    }
    if (filled && target >= 0)
    {
        // A generated pad already knows what it is; a hand-captured one gets worked out here.
        auto [rootPc, type] = pad.type >= 0 ? std::pair<int, int> { pad.rootPc, pad.type }
                                            : suggest::analyse(pad.notes);
        const auto suggestions = suggest::all(rootPc, type, octaveOf(pad.notes, currentOptions().octave));

        menu.addSectionHeader("Next: could follow");
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
            menu.addSubMenu(cat, sub);
        }
    }
    lastSuggestTarget = target;

    // Clear page, beside the settings. It was a chip on the Pads bar until 2026-07-30 and had
    // no business being one: it empties every unlocked pad on the page, Keys has no undo of
    // any kind, and it rode 4 px from Regen and a few more from the page buttons - the two
    // things on that bar that get clicked constantly. A menu costs a right-click and a read,
    // which is the right price for sixteen pads. Greyed out when there is nothing to take,
    // so it never looks like it did something it didn't.
    bool anyToClear = false;
    for (int v = 0; v < KeysProcessor::padsPerPage && ! anyToClear; ++v)
    {
        const auto& p = processor.chordPad(offset + v);
        anyToClear = ! p.locked && ! p.notes.empty();
    }

    menu.addSeparator();
    menu.addSectionHeader("This page");
    menu.addItem(idClearPage, "Clear page", anyToClear);

    // And the settings, flat. They used to sit behind a "Generator settings" submenu, which
    // made every one of them a three-level diagonal hover; see the class comment. The headers
    // and rules above are what pays for the length that costs.
    menu.addSeparator();
    menu.addSectionHeader("Generator settings");
    addSettingsItems(menu);
}

// The other half: what to do with a choice from those items. The menu is shown and
// dismissed by ChordPads, which knows nothing about suggestions or settings, so both lists
// built above are held here between the two calls - both happen on the message thread, one
// after the other, and a second menu rebuilds them before either can be read.
void ChordGenMenu::handlePadMenuChoice(int slot, int id)
{
    stopPreview(); // don't let the last audition ring past the menu
    if (id == idNewChord)
    {
        newChordFor(slot);
        return;
    }
    if (id == idClearPage)
    {
        clearPage();
        return;
    }
    if (id >= idSettingsBase)
    {
        const int index = id - idSettingsBase;
        if (index < (int) lastSettings.size())
            applySetting(lastSettings[(size_t) index]);
        return;
    }
    const int index = id - idSuggestBase;
    if (index < 0 || index >= (int) lastSuggestions.size() || lastSuggestTarget < 0)
        return;
    const auto& s = lastSuggestions[(size_t) index];
    writeChord(lastSuggestTarget, { s.rootPc, s.type, s.notes, -1 });
}
} // namespace keys
