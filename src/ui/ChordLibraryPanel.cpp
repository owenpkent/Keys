#include "ChordLibraryPanel.h"
#include "../ChordNumerals.h"
#include "../Chords.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>
#include <okstudio/Theme.h>

namespace keys
{
namespace
{
    constexpr int kInset = 12;
    constexpr int kGap = 8;
    // 34, matching ChordGenPanel's own header, which this window otherwise copies line for
    // line. It was 28, and the Close button centred in it was therefore a 26 px target - under
    // the 34 px floor CLAUDE.md states as an invariant, on the only on-screen way out of the
    // window. The rule is to give the cell the height the target needs rather than shrink the
    // target into the cell, and `contentSize` below reads this, so the window grew to suit.
    constexpr int kHeaderH = 34;
    constexpr int kAfterHeader = 10;
    constexpr int kFilterRowH = 44; // 14 px caption over a 30 px control, as everywhere else
    constexpr int kAfterFilters = 10;

    // 46, not the 34 px mouse-only floor. A row carries a name over a numeral strip, which is two
    // lines, and 34 fits one of them - the same arithmetic that makes a tray card 54. It is the
    // smaller of the two because a library row's second line is a numeral strip rather than a full
    // note list, and because twelve of these have to fit in a window somebody can see all of.
    constexpr int kRowH = 46;
    constexpr int kRowGap = 4;
    constexpr int kRadius = 6.0f;

    constexpr int kFooterH = 34;
    constexpr int kAfterRows = 8;

    constexpr int kStarW = 34; // the mouse-only floor: a star is a target exactly as a button is
    constexpr int kFavW = 130;     // the "Starred only" toggle beside the three pickers
    constexpr int kFollowsW = 100; // and "Follows" beside it
    constexpr int kMoodW = 160, kGenreW = 170, kFuncW = 150, kCountW = 240;
    constexpr int kTitleW = 150, kCloseW = 90;
    // "Tray" / "Pads", side by side at a row's right end. 66, not the 86 they had while they read
    // "To tray" and "To pads": twenty-four buttons repeating the same two phrases down the window
    // is a lot of text saying one thing, and in a column the preposition carries nothing the
    // position does not. The accessible name keeps the whole phrase - a screen reader has no
    // column to read them in. Well clear of the 34 px floor either way.
    constexpr int kRowBtnW = 66;
    constexpr int kNameW = 250;    // the name column; the numeral strip takes what is left
    constexpr int kTagW = 190;     // mood and genre, micro caps under the buttons' left edge
    constexpr int kPageBtnW = 44;
    constexpr int kPageLabelW = 130;

    void styleLabel(juce::Label& l, const juce::String& text)
    {
        l.setText(text.toUpperCase(), juce::dontSendNotification);
        l.setFont(skin::micro(10.0f));
        l.setColour(juce::Label::textColourId, skin::textDim);
    }

    // How wide a string actually draws, measured through a GlyphArrangement.
    //
    // **Not `Font::getStringWidthFloat`**, which is what this used for one build and which
    // under-measured every numeral: the columns came out sized to the *chord* underneath, so
    // "iim7" drew as "iim", "V7" as "V" and a bare "V" as nothing at all - a table quietly
    // deleting the last character of half its content. It is also deprecated in JUCE 8, and the
    // deprecation names this as the replacement.
    float textWidth(const juce::Font& font, const juce::String& text)
    {
        if (text.isEmpty())
            return 0.0f;
        juce::GlyphArrangement ga;
        ga.addLineOfText(font, text, 0.0f, 0.0f);
        return ga.getBoundingBox(0, -1, true).getWidth();
    }

    // Each chord of the progression as **one column**: the numeral it is written as over the chord
    // it comes out as in the current key. `i` over `Cm`, `bVII` over `A#`.
    //
    // Drawn as columns rather than as two independent strings, which is what they were for one
    // build. The two lines use different fonts at different sizes, so "I  V  vi  IV" over
    // "C  G  Am  F" drifted apart along the row and by the fourth chord the pairing was something
    // you had to work out rather than see. A column is the whole point of showing both: the
    // numeral says what the progression *is* and the chord says what you will hear, and they only
    // answer each other while they are stacked.
    //
    // Truncates with an ellipsis column when the row runs out of width - a twelve-bar blues has
    // twelve of these - rather than shrinking the type or spilling into the tags.
    void paintChordColumns(juce::Graphics& g, juce::Rectangle<float> area, const chordlib::Entry& e,
                           int rootPc, juce::Colour numeralInk, juce::Colour chordInk)
    {
        const auto tokens = juce::StringArray::fromTokens(juce::String(e.numerals), " ", "");
        const auto realised = chordlib::chordsFor(e, rootPc, e.mode, 4);

        const auto numeralFont = skin::ui(13.0f);
        const auto chordFont = skin::micro(9.0f);
        constexpr float gutter = 10.0f;

        auto top = area;
        const auto bottom = top.removeFromBottom(13.0f);
        float x = area.getX();

        for (int i = 0; i < tokens.size(); ++i)
        {
            const auto numeral = tokens[i];
            const auto chord = i < (int) realised.size() ? chords::detect(realised[(size_t) i].notes)
                                                         : juce::String();
            // Two px of slack on top of the measurement: a glyph box is the ink, and a cell sized
            // to the ink exactly will clip the last stroke of an italic or a descender.
            const float w = juce::jmax(textWidth(numeralFont, numeral),
                                       textWidth(chordFont, chord)) + 2.0f;

            // No room for this column: stop, and say so rather than spilling into the tags.
            if (x + w > area.getRight())
            {
                g.setColour(numeralInk.withAlpha(0.5f));
                g.setFont(numeralFont);
                g.drawText(juce::String::charToString((juce::juce_wchar) 0x2026),
                           juce::Rectangle<float>(x, top.getY(), 14.0f, top.getHeight()).toNearestInt(),
                           juce::Justification::bottomLeft, false);
                return;
            }

            g.setColour(numeralInk);
            g.setFont(numeralFont);
            g.drawText(numeral, juce::Rectangle<float>(x, top.getY(), w, top.getHeight()).toNearestInt(),
                       juce::Justification::bottomLeft, false);

            g.setColour(chordInk);
            g.setFont(chordFont);
            g.drawText(chord, juce::Rectangle<float>(x, bottom.getY(), w, bottom.getHeight()).toNearestInt(),
                       juce::Justification::topLeft, false);

            x += w + gutter;
        }
    }

    // The name with its trailing numerals dropped, **and only when the parenthetical is exactly
    // the numerals** the row already prints in its own column.
    //
    // Most names carry them - "Axis (I-V-vi-IV)", "Andalusian cadence (i-bVII-bVI-V)" - because
    // they were written for a *combo box*, where the name is the only thing on screen and the
    // numerals had nowhere else to be. In a table with a numeral column and a realised-chord
    // column under it, a third copy in the title is noise, and it is what pushed the longest names
    // past the column and into an ellipsis.
    //
    // The test is exact rather than "drop any trailing bracket", because plenty of parentheticals
    // are not numerals and carry the only qualifier the name has: "i-iv-v (natural minor)",
    // "Autumn-leaves turn (major to relative minor)", "Truck driver's gear change (I-V-I up a
    // tone)". Dropping those would leave two rows reading the same thing.
    //
    // Display only. `Entry::name` is the identity - what a favourite is kept under and what a pad
    // stores in `progression` - and none of that moves.
    juce::String displayName(const chordlib::Entry& e)
    {
        const juce::String name(e.name);
        if (! name.endsWithChar(')'))
            return name;
        const int open = name.lastIndexOfChar('(');
        if (open <= 0)
            return name;
        const auto inside = name.substring(open + 1, name.length() - 1).replaceCharacter('-', ' ');
        return inside == juce::String(e.numerals) ? name.substring(0, open).trim() : name;
    }

    // The tags, mood first. Truncated by the drawText rather than by hand: the column is fixed and
    // an ellipsis is the honest answer for a row with four of each.
    juce::String tagLine(const chordlib::Entry& e)
    {
        juce::String out;
        for (const auto* m : e.moods)
            out << (out.isEmpty() ? "" : ", ") << m;
        out << "  -  ";
        bool first = true;
        for (const auto* g : e.genres)
        {
            out << (first ? "" : ", ") << g;
            first = false;
        }
        return out;
    }
} // namespace

juce::Point<int> ChordLibraryPanel::contentSize()
{
    const int w = juce::jmax(kMoodW + kGenreW + kFuncW + kFavW + kFollowsW + kCountW + 5 * kGap,
                             kStarW + kNameW + kTagW + 2 * kRowBtnW + 4 * kGap + 220, // 220: strip
                             kTitleW + kCloseW + kGap);
    const int h = kHeaderH + kAfterHeader + kFilterRowH + kAfterFilters
                  + rowsPerPage * kRowH + (rowsPerPage - 1) * kRowGap + kAfterRows + kFooterH;
    return { w + kInset * 2, h + kInset * 2 };
}

juce::Point<int> ChordLibraryPanel::minWindowSize()
{
    // The content plus what DetachedWindow puts round it: a 38 px title bar (mouse-only, so its
    // buttons clear the 34 px floor) and a 1 px border a side. Same arithmetic as ChordGenPanel's.
    const auto c = contentSize();
    return { c.x + 2, c.y + 38 + 2 };
}

juce::Point<int> ChordLibraryPanel::defaultWindowSize()
{
    const auto m = minWindowSize();
    return { m.x + 80, m.y }; // a little slack on the width, where the numeral strip wants it
}

ChordLibraryPanel::ChordLibraryPanel(KeysProcessor& p, ChordGenMenu& g) : processor(p), gen(g)
{
    okstudio::ui::makeMouseOnly(*this);
    setTitle("Chord library");
    buildControls();
    refreshMatches();
    startTimerHz(10);
}

ChordLibraryPanel::~ChordLibraryPanel()
{
    // A progression left walking must not outlive the window that started it. The audition itself
    // lives on ChordGenMenu, which outlives this - that is why it lives there - but nothing else
    // would stop it, and a window closing mid-walk would leave chords arriving from nowhere.
    //
    // **Only a walk, though.** ChordGenMenu owns one preview path for both this and the tray's
    // 800 ms single-chord audition, so an unconditional stop here cut off a chord the *generator*
    // window was auditioning whenever this window happened to close inside those 800 ms - killing
    // a sound this window did not start and has nothing to do with.
    if (gen.auditioningProgression())
        gen.stopAudition();
}

void ChordLibraryPanel::buildControls()
{
    title.setText("Chord Library", juce::dontSendNotification);
    title.setFont(skin::uiSemi(16.0f).withExtraKerningFactor(0.04f));
    title.setColour(juce::Label::textColourId, skin::text);
    addAndMakeVisible(title);

    closeButton.setTitle("Close chord library");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeButton);

    // The three filters are ChordGenMenu's own state, so this window and the generator's Library
    // band read and write one thing. Offering `moodsInUse` rather than the whole vocabulary is the
    // same call the band makes and for the same reason: a word with no rows behind it is a pick
    // that can only disappoint, and here it would show an empty page with no explanation.
    styleLabel(moodLabel, "Mood");
    moodBox.addItem("Any", 1);
    {
        int id = 2;
        for (const auto& m : chordlib::moodsInUse())
            moodBox.addItem(m, id++);
    }
    moodBox.setSelectedId(1, juce::dontSendNotification);
    moodBox.setTitle("Library mood");
    moodBox.onChange = [this]
    {
        gen.setLibraryMood(moodBox.getSelectedId() <= 1 ? juce::String() : moodBox.getText());
        page = 0; // a new filter is a new list, and page 4 of the old one means nothing in it
        refreshMatches();
    };

    styleLabel(genreLabel, "Genre");
    genreBox.addItem("Any", 1);
    {
        int id = 2;
        for (const auto& g : chordlib::genresInUse())
            genreBox.addItem(g, id++);
    }
    genreBox.setSelectedId(1, juce::dontSendNotification);
    genreBox.setTitle("Library genre");
    genreBox.onChange = [this]
    {
        gen.setLibraryGenre(genreBox.getSelectedId() <= 1 ? juce::String() : genreBox.getText());
        page = 0;
        refreshMatches();
    };

    styleLabel(functionLabel, "Does what");
    functionBox.addItem("Any", 1);
    for (int f = 0; f < (int) chordlib::Function::count; ++f)
        functionBox.addItem(chordlib::functionName((chordlib::Function) f), f + 2);
    functionBox.setSelectedId(1, juce::dontSendNotification);
    functionBox.setTitle("Library function");
    {
        juce::String tip = "What the progression does.";
        for (int f = 0; f < (int) chordlib::Function::count; ++f)
            tip << "\n" << chordlib::functionName((chordlib::Function) f) << ": "
                << chordlib::functionBlurb((chordlib::Function) f);
        functionBox.setTooltip(tip);
    }
    functionBox.onChange = [this]
    {
        gen.setLibraryFunction(functionBox.getSelectedId() <= 1 ? -1
                                                                : functionBox.getSelectedId() - 2);
        page = 0;
        refreshMatches();
    };

    // Adopt whatever the brain already holds. The window is built fresh every time it opens and
    // the picks outlive it, so without this the boxes would come back reading "Any" while the
    // generator still filtered on your last choice.
    for (int i = 0; i < moodBox.getNumItems(); ++i)
        if (moodBox.getItemText(i) == gen.libraryMood())
            moodBox.setSelectedId(moodBox.getItemId(i), juce::dontSendNotification);
    for (int i = 0; i < genreBox.getNumItems(); ++i)
        if (genreBox.getItemText(i) == gen.libraryGenre())
            genreBox.setSelectedId(genreBox.getItemId(i), juce::dontSendNotification);
    if (gen.libraryFunction() >= 0)
        functionBox.setSelectedId(gen.libraryFunction() + 2, juce::dontSendNotification);

    for (auto* c : { &moodLabel, &genreLabel, &functionLabel })
        addAndMakeVisible(*c);
    for (auto* c : { &moodBox, &genreBox, &functionBox })
        addAndMakeVisible(*c);

    // A toggle rather than a fourth combo. "Starred only" is a yes/no about the list you already
    // have, where the three beside it are picks *within* it - and a combo reading "All / Starred"
    // would be one more thing to open and read for a question with two answers.
    favouritesButton.setButtonText("Starred only");
    favouritesButton.setTitle("Show starred progressions only");
    favouritesButton.setTooltip("Show only the progressions you have starred. The star is at the "
                                "left of each row.");
    favouritesButton.setClickingTogglesState(true);
    favouritesButton.onClick = [this] { page = 0; refreshMatches(); };
    addAndMakeVisible(favouritesButton);

    // **Follows** is the relational layer, and it is a *mode* rather than a fourth filter because
    // it answers a different question: the three pickers ask "what is there", this asks "what
    // comes after what I already have". While it is on, the three are ignored and the list is
    // `chordlib::couldFollow` on the last progression the pads hold, best first.
    //
    // It points at the pads rather than at a row you select here, because that is where the
    // question actually comes from: you have just laid a progression down and want the next one.
    // Nothing to aim, nothing to remember - if the strip ends in a cadence, this offers what goes
    // after a cadence. It greys when no pad carries a progression, which is honest: there is
    // nothing for it to follow.
    followsButton.setButtonText("Follows");
    followsButton.setTitle("Show progressions that could follow the pads");
    followsButton.setTooltip("Show the progressions that could come after the last one on your "
                             "pads, best first. Structure decides which are eligible - what "
                             "follows a cadence is not what follows a turnaround - and how well "
                             "the last chord joins onto the first orders them.");
    followsButton.setClickingTogglesState(true);
    followsButton.onClick = [this] { page = 0; refreshMatches(); };
    addAndMakeVisible(followsButton);

    countLabel.setFont(skin::ui(12.0f));
    countLabel.setColour(juce::Label::textColourId, skin::textDim);
    countLabel.setJustificationType(juce::Justification::centredLeft);
    countLabel.setTitle("Library match count");
    addAndMakeVisible(countLabel);

    prevPage.setTitle("Previous library page");
    prevPage.onClick = [this] { page = juce::jmax(0, page - 1); refreshRowButtons(); repaint(); };
    nextPage.setTitle("Next library page");
    nextPage.onClick = [this] { page = juce::jmin(numPages() - 1, page + 1); refreshRowButtons(); repaint(); };
    addAndMakeVisible(prevPage);
    addAndMakeVisible(nextPage);

    pageLabel.setFont(skin::ui(12.0f));
    pageLabel.setColour(juce::Label::textColourId, skin::textDim);
    pageLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(pageLabel);

    for (int i = 0; i < rowsPerPage; ++i)
    {
        auto& t = trayButtons[(size_t) i];
        t.setButtonText("Tray");
        t.setTitle("Send progression " + juce::String(i + 1) + " to the tray");
        t.setTooltip("Fill the generator's audition tray with this progression, so you can hear "
                     "each chord and drag the ones you want onto a pad.");
        t.onClick = [this, i]
        {
            if (const auto* e = entryAt(i); e != nullptr && onSendToTray)
            {
                // Tell the brain which row the tray now holds, before the push. The generator's
                // diagram and readout both name that field, and without this they would keep
                // naming whatever the last *generation* rolled while the tray plainly held this.
                gen.setLastLibraryEntry(e->name);
                onSendToTray(padsFor(*e));
            }
        };
        addAndMakeVisible(t);

        auto& d = padsButtons[(size_t) i];
        d.setButtonText("Pads");
        d.setTitle("Send progression " + juce::String(i + 1) + " to the pads");
        d.setTooltip("Write this progression into the empty pads on the current page. Pads that "
                     "already carry a chord are never overwritten.");
        d.onClick = [this, i]
        {
            if (const auto* e = entryAt(i); e != nullptr && onSendToPads)
                onSendToPads(padsFor(*e));
        };
        addAndMakeVisible(d);
    }
}

// The pads a row would become, built the same way the Library source builds them: through
// `chordlib::chordsFor` against **the entry's own mode**, so a minor row lands with degrees that
// resolve rather than a strip of question marks. See ChordGenMenu's own note on that choice.
//
// Deliberately *not* through the generator's voicing passes. Notes, Inversions and Lean are
// settings about a chord you are generating; a library row is a written-down progression and the
// point of taking one from here is to get it as written. The tray route is where you can then
// reshape it, because that is what the tray's own controls are for.
std::vector<KeysProcessor::ChordPad> ChordLibraryPanel::padsFor(const chordlib::Entry& e) const
{
    const int root = (int) processor.apvts.getRawParameterValue("genRoot")->load();
    const int oct = juce::jlimit(0, 8, (int) processor.apvts.getRawParameterValue("genOctave")->load());

    std::vector<KeysProcessor::ChordPad> out;
    for (const auto& c : chordlib::chordsFor(e, root, e.mode, oct))
    {
        KeysProcessor::ChordPad pad;
        pad.notes = c.notes;
        pad.name = chords::detect(c.notes);
        pad.rootPc = c.rootPc;
        pad.type = c.type;
        pad.degree = c.degree;
        // The row and the position within it come off the chord now: `chordsFor` stamps them,
        // because it is the one function that knows both, and stamping there is what gives the
        // generator's own Library source the same pair rather than leaving it to each placing
        // end to remember. A pad carries it from here on - the strip draws a bracket under
        // consecutive steps, and it is what lets "could follow" ask about a progression.
        pad.progression = c.progression;
        pad.progressionStep = c.progressionStep;
        out.push_back(pad);
    }
    return out;
}

// The library row the pads end on, scanning the current page backwards. Backwards because the
// question is "what comes next", so the *last* progression laid down is the one being followed -
// and a page holding three of them in a row wants the third, not the first.
//
// The current page only. A progression on page 2 is not what page 1 ends on, and reaching across
// pages would make the answer depend on something you cannot see.
juce::String ChordLibraryPanel::lastProgressionOnPads() const
{
    const int offset = processor.padPageOffset();
    for (int v = KeysProcessor::padsPerPage - 1; v >= 0; --v)
    {
        const auto& pad = processor.chordPad(offset + v);
        if (! pad.notes.empty() && pad.progression.isNotEmpty())
            return pad.progression;
    }
    return {};
}

bool ChordLibraryPanel::isFavourite(const chordlib::Entry& e) const
{
    return processor.layout.libraryFavourites.contains(e.name);
}

void ChordLibraryPanel::toggleFavourite(const chordlib::Entry& e)
{
    auto& favs = processor.layout.libraryFavourites;
    if (const int i = favs.indexOf(e.name); i >= 0)
        favs.remove(i);
    else
        favs.add(e.name);

    // Un-starring the last row while the filter is showing favourites only would otherwise leave
    // you on an empty page with the way back three clicks away, so the list is re-run either way.
    refreshMatches();
}

int ChordLibraryPanel::numPages() const
{
    return juce::jmax(1, ((int) matches.size() + rowsPerPage - 1) / rowsPerPage);
}

const chordlib::Entry* ChordLibraryPanel::entryAt(int indexOnPage) const
{
    const int i = page * rowsPerPage + indexOnPage;
    return i >= 0 && i < (int) matches.size() ? matches[(size_t) i] : nullptr;
}

void ChordLibraryPanel::refreshMatches()
{
    lastSignature = gen.libraryMood() + "|" + gen.libraryGenre() + "|"
                    + juce::String(gen.libraryFunction());
    // What the pads end on, if anything. Scanned every refresh rather than cached: the pads change
    // from four other surfaces and none of them knows this window exists.
    const auto* seed = chordlib::byName(lastProgressionOnPads());
    followsButton.setEnabled(seed != nullptr);
    const bool following = followsButton.getToggleState() && seed != nullptr;

    if (following)
        matches = chordlib::couldFollow(*seed, false, (int) chordlib::table().size());
    else
        matches = chordlib::find(gen.libraryMood(), gen.libraryGenre(), gen.libraryFunction());

    // Starred-only narrows what the three pickers matched rather than replacing it, so a star and
    // a mood together mean "the sad ones I kept" rather than one silently cancelling the other.
    if (favouritesButton.getToggleState())
    {
        const auto& favs = processor.layout.libraryFavourites;
        std::vector<const chordlib::Entry*> kept;
        for (const auto* e : matches)
            if (favs.contains(e->name))
                kept.push_back(e);
        matches = std::move(kept);
    }
    page = juce::jlimit(0, numPages() - 1, page);

    const int n = (int) matches.size();
    const int stars = processor.layout.libraryFavourites.size();
    juce::String count;
    if (n == 0)
        count = favouritesButton.getToggleState() && stars == 0
                    ? juce::String("nothing starred yet - the star is at the left of a row")
                    : juce::String("nothing matches - widen a filter");
    else if (following)
        // Naming what it is following is the whole of what makes this mode readable. Without it
        // the list is a reordering with no stated cause, which is indistinguishable from a bug.
        count = juce::String(n) + " could follow " + seed->name;
    else if (n == (int) chordlib::table().size())
        count = juce::String(n) + " progressions"; // "355 of 355" is a sentence saying nothing
    else
        count = juce::String(n) + (n == 1 ? " progression" : " progressions") + " of "
                + juce::String(chordlib::table().size());
    if (stars > 0)
        count << "   |   " << juce::String(stars) << " starred";
    countLabel.setText(count, juce::dontSendNotification);
    refreshRowButtons();
    repaint();
}

void ChordLibraryPanel::refreshRowButtons()
{
    pageLabel.setText("Page " + juce::String(page + 1) + " of " + juce::String(numPages()),
                      juce::dontSendNotification);
    prevPage.setEnabled(page > 0);
    nextPage.setEnabled(page < numPages() - 1);

    // A button on a row with nothing on it is a target that does nothing, so the pair *hides* with
    // its row rather than greying: an empty row draws no card either, and a floating button over
    // background would read as a bug. The two enabled states below are different - there the row
    // exists and the action is simply unavailable, which is what greying is for.
    const bool trayOpen = onTrayIsOpen && onTrayIsOpen();
    const bool haveRoom = onPageHasEmptyPad && onPageHasEmptyPad();
    for (int i = 0; i < rowsPerPage; ++i)
    {
        const bool has = entryAt(i) != nullptr;
        trayButtons[(size_t) i].setVisible(has);
        padsButtons[(size_t) i].setVisible(has);
        trayButtons[(size_t) i].setEnabled(trayOpen);
        padsButtons[(size_t) i].setEnabled(haveRoom);
    }
}

void ChordLibraryPanel::timerCallback()
{
    // The filters can move from the *other* window - they are one piece of state shared with the
    // generator's Library band - so this window has to notice. Polled rather than pushed because
    // the two windows do not know about each other and should not have to.
    const auto sig = gen.libraryMood() + "|" + gen.libraryGenre() + "|"
                     + juce::String(gen.libraryFunction());
    if (sig != lastSignature)
    {
        for (int i = 0; i < moodBox.getNumItems(); ++i)
            if (moodBox.getItemText(i) == gen.libraryMood())
                moodBox.setSelectedId(moodBox.getItemId(i), juce::dontSendNotification);
        if (gen.libraryMood().isEmpty())
            moodBox.setSelectedId(1, juce::dontSendNotification);
        for (int i = 0; i < genreBox.getNumItems(); ++i)
            if (genreBox.getItemText(i) == gen.libraryGenre())
                genreBox.setSelectedId(genreBox.getItemId(i), juce::dontSendNotification);
        if (gen.libraryGenre().isEmpty())
            genreBox.setSelectedId(1, juce::dontSendNotification);
        functionBox.setSelectedId(gen.libraryFunction() < 0 ? 1 : gen.libraryFunction() + 2,
                                  juce::dontSendNotification);
        page = 0;
        refreshMatches();
    }

    refreshRowButtons(); // the two greys track another window's state too

    // What the pads end on changes from four other surfaces - a drop, a Fill, an undo, a page
    // flip - and none of them knows this window exists. Polled for the same reason the filters
    // are, and cached so a tick that changed nothing costs one string compare rather than a
    // re-filter of 355 rows.
    //
    // Without this, **Follows stayed greyed after the pads gained a progression**: the seed is
    // worked out in `refreshMatches`, which only ran when a picker moved. It looked like the
    // feature not working, which is what a control that is right about a stale fact always looks
    // like.
    if (const auto onPads = lastProgressionOnPads(); onPads != lastPadsProgression)
    {
        lastPadsProgression = onPads;
        refreshMatches();
    }

    // The lit row goes out when the walk ends. Nothing tells this window when that happens - the
    // audition is on a timer inside ChordGenMenu - so it asks.
    if (playing >= 0 && ! gen.auditioningProgression())
    {
        playing = -1;
        repaint();
    }
}

juce::Rectangle<float> ChordLibraryPanel::rowBounds(int indexOnPage) const
{
    auto area = getLocalBounds().reduced(kInset);
    area.removeFromTop(kHeaderH + kAfterHeader + kFilterRowH + kAfterFilters);
    area.removeFromBottom(kFooterH + kAfterRows);
    const float h = (float) kRowH;
    return juce::Rectangle<float>((float) area.getX(),
                                  (float) area.getY() + (float) indexOnPage * (h + (float) kRowGap),
                                  (float) area.getWidth(), h);
}

// The star's own cell, reserved out of the row's left end before anything else takes a cut - the
// standing rule here, and it matters more than usual because the elastic thing beside it is the
// numeral strip, which will happily eat a fixed cell if asked to leave room for one.
juce::Rectangle<float> ChordLibraryPanel::starBounds(int indexOnPage) const
{
    return rowBounds(indexOnPage).withWidth((float) kStarW + 8.0f).reduced(4.0f, 6.0f);
}

int ChordLibraryPanel::rowAt(juce::Point<float> pos) const
{
    for (int i = 0; i < rowsPerPage; ++i)
        if (rowBounds(i).contains(pos))
            return i;
    return -1;
}

void ChordLibraryPanel::mouseMove(const juce::MouseEvent& e)
{
    const int h = rowAt(e.position);
    if (h != hovered)
    {
        hovered = h;
        repaint();
    }
}

void ChordLibraryPanel::mouseExit(const juce::MouseEvent&)
{
    if (hovered != -1)
    {
        hovered = -1;
        repaint();
    }
}

void ChordLibraryPanel::mouseDown(const juce::MouseEvent& e)
{
    // The whole row is the Hear button. The two placement buttons sit on top of it and eat their
    // own clicks, so there is nothing to test for here: a press that reaches this component is a
    // press on the row itself, which is the same way a chord card works.
    const int i = rowAt(e.position);
    const auto* entry = i >= 0 ? entryAt(i) : nullptr;
    if (entry == nullptr)
        return;

    // The star is tested before the row, because it sits *inside* the row's rectangle rather than
    // on top of it as a child Component. It is painted rather than built as a button for the same
    // reason the lock dot on a chord card is: twelve more Components to lay out, hide and re-title
    // on every page turn, for a two-state mark. Its cell is the mouse-only 34 px all the same.
    if (starBounds(i).contains(e.position))
    {
        toggleFavourite(*entry);
        return;
    }

    // A second click on the row that is already walking stops it, so the way out of a
    // twelve-chord blues is the same target that started it rather than a hunt for a Stop button.
    if (i == playing)
    {
        gen.stopAudition();
        playing = -1;
        repaint();
        return;
    }

    std::vector<std::vector<int>> chords;
    for (const auto& pad : padsFor(*entry))
        chords.push_back(pad.notes);
    gen.auditionProgression(chords);
    playing = i;
    repaint();
}

void ChordLibraryPanel::paint(juce::Graphics& g)
{
    g.fillAll(skin::bgBot);

    const juce::Colour inkOnAccent { 0xff07272c };
    const auto accent = skin::accentOf(*this);

    for (int i = 0; i < rowsPerPage; ++i)
    {
        const auto* e = entryAt(i);
        const auto b = rowBounds(i);
        if (e == nullptr)
            continue;

        const bool lit = (i == playing);
        if (lit)
        {
            g.setGradientFill({ accent.hot, 0.0f, b.getY(), accent.base, 0.0f, b.getBottom(), false });
            g.fillRoundedRectangle(b, kRadius);
            skin::glowRect(g, b, kRadius, accent.base);
        }
        else
        {
            skin::raisedFill(g, b, kRadius, skin::cardFace, skin::cardFaceBot);
            if (i == hovered)
            {
                g.setColour(accent.base.withAlpha(0.10f));
                g.fillRoundedRectangle(b, kRadius);
            }
        }

        const auto ink = lit ? inkOnAccent : skin::text;
        const auto dim = lit ? inkOnAccent.withAlpha(0.75f) : skin::textDim;

        // The star, and its cell out of the row's left end before anything else takes a cut.
        {
            const auto star = starBounds(i);
            const bool on = isFavourite(*e);
            const bool hot = (i == hovered) && star.contains(getMouseXYRelative().toFloat());
            g.setColour(on ? (lit ? inkOnAccent : accent.base)
                           : (lit ? inkOnAccent.withAlpha(hot ? 0.6f : 0.25f)
                                  : skin::textFaint.withAlpha(hot ? 1.0f : 0.5f)));
            g.setFont(skin::uiSemi(15.0f));
            // A filled star for kept, a hollow one for not. Two glyphs rather than one glyph at two
            // alphas: an unstarred row still has to say "you can star this", and a dim star says it
            // where a very dim one just looks like a rendering fault.
            g.drawText(juce::String::charToString((juce::juce_wchar) (on ? 0x2605 : 0x2606)),
                       star.toNearestInt(), juce::Justification::centred, false);
        }

        auto r = b.reduced(8.0f, 4.0f);
        r = r.withTrimmedLeft((float) kStarW);
        r.removeFromRight((float) (2 * kRowBtnW + kGap + kGap)); // the two buttons' cell
        const auto tags = r.removeFromRight((float) kTagW);
        const auto name = r.removeFromLeft((float) kNameW);

        // Name over nothing, numerals over nothing, tags to the right: three columns rather than
        // stacked lines, because a row is 46 px and two 13 px lines with a gap is all of it. What
        // stacks is inside the name column, where the function label sits under the name - the one
        // pairing where the second line is *about* the first.
        auto nameTop = name;
        const auto funcLine = nameTop.removeFromBottom(13.0f);
        g.setColour(ink);
        g.setFont(skin::uiSemi(13.5f));
        g.drawText(displayName(*e), nameTop.toNearestInt(), juce::Justification::bottomLeft, true);
        g.setColour(lit ? inkOnAccent.withAlpha(0.8f) : accent.base);
        g.setFont(skin::micro(9.0f));
        g.drawText(juce::String(chordlib::functionName(e->function)).toUpperCase(),
                   funcLine.toNearestInt(), juce::Justification::topLeft, false);

        // Numerals over the chords they come out as, on the same two lines the name and its
        // function use - so the eye tracks straight across the row instead of stepping over a
        // strip that was vertically centred against two that were not.
        paintChordColumns(g, r, *e,
                          (int) processor.apvts.getRawParameterValue("genRoot")->load(),
                          ink.withAlpha(lit ? 0.9f : 0.85f), dim);

        g.setColour(dim);
        g.setFont(skin::micro(9.0f));
        g.drawText(tagLine(*e), tags.toNearestInt(), juce::Justification::centredRight, true);
    }

    // Nothing matched at all: say so where the rows would have been rather than leaving a tall
    // empty panel that reads as a window still loading.
    if (matches.empty())
    {
        auto area = rowBounds(0).withBottom(rowBounds(rowsPerPage - 1).getBottom());
        g.setColour(skin::textFaint);
        g.setFont(skin::ui(14.0f));
        g.drawText(favouritesButton.getToggleState()
                       ? "Nothing starred here. The star sits at the left of every row."
                       : "No progression carries all three of those. Widen a filter.",
                   area.toNearestInt(), juce::Justification::centred, true);
    }
}

void ChordLibraryPanel::resized()
{
    auto area = getLocalBounds().reduced(kInset);

    {
        auto header = area.removeFromTop(kHeaderH);
        title.setBounds(header.removeFromLeft(kTitleW));
        closeButton.setBounds(header.removeFromRight(kCloseW).withSizeKeepingCentre(kCloseW, kHeaderH));
    }
    area.removeFromTop(kAfterHeader);

    {
        auto row = area.removeFromTop(kFilterRowH);
        const auto cell = [&row](int w, juce::Label& l, juce::Component& c)
        {
            auto cellArea = row.removeFromLeft(w);
            l.setBounds(cellArea.removeFromTop(14));
            c.setBounds(cellArea);
            row.removeFromLeft(kGap);
        };
        cell(kMoodW, moodLabel, moodBox);
        cell(kGenreW, genreLabel, genreBox);
        cell(kFuncW, functionLabel, functionBox);
        {
            // Reserved before the count takes the remainder: the count is the elastic one here.
            auto favCell = row.removeFromLeft(kFavW);
            favCell.removeFromTop(14); // no caption of its own - the button says what it is
            favouritesButton.setBounds(favCell);
            row.removeFromLeft(kGap);
            auto followCell = row.removeFromLeft(kFollowsW);
            followCell.removeFromTop(14);
            followsButton.setBounds(followCell);
            row.removeFromLeft(kGap);
        }
        row.removeFromTop(14); // no caption over the count: it is a sentence, not a value
        countLabel.setBounds(row);
    }
    area.removeFromTop(kAfterFilters);

    {
        auto footer = area.removeFromBottom(kFooterH);
        area.removeFromBottom(kAfterRows);
        prevPage.setBounds(footer.removeFromLeft(kPageBtnW));
        footer.removeFromLeft(kGap);
        pageLabel.setBounds(footer.removeFromLeft(kPageLabelW));
        footer.removeFromLeft(kGap);
        nextPage.setBounds(footer.removeFromLeft(kPageBtnW));
    }

    // The row buttons, laid into each row's own right end. Reserved out of the row before anything
    // else takes a cut, which is the standing rule here: the elastic thing is the numeral strip.
    for (int i = 0; i < rowsPerPage; ++i)
    {
        auto r = rowBounds(i).reduced(8.0f, 6.0f).toNearestInt();
        padsButtons[(size_t) i].setBounds(r.removeFromRight(kRowBtnW));
        r.removeFromRight(kGap);
        trayButtons[(size_t) i].setBounds(r.removeFromRight(kRowBtnW));
    }
}
} // namespace keys
