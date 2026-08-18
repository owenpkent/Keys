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
    constexpr int kHeaderH = 28;
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

    constexpr int kMoodW = 160, kGenreW = 170, kFuncW = 150, kCountW = 240;
    constexpr int kTitleW = 150, kCloseW = 90;
    constexpr int kRowBtnW = 86;   // "To tray" / "To pads", side by side at a row's right end
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

    // The row's second line: every numeral of the progression, spaced. This is the *stored* form,
    // printed as written rather than resolved into a key - which is the point of a library view.
    // "i bVII bVI V" says what the Andalusian cadence is in a way "Cm Bb Ab G" only says if you
    // already knew the key.
    juce::String numeralStrip(const chordlib::Entry& e)
    {
        auto s = juce::String(e.numerals);
        return s.replace(" ", "   ");
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
    const int w = juce::jmax(kMoodW + kGenreW + kFuncW + kCountW + 3 * kGap,
                             kNameW + kTagW + 2 * kRowBtnW + 4 * kGap + 220, // 220: the numeral strip
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
    gen.stopAudition();
}

void ChordLibraryPanel::buildControls()
{
    title.setText("Chord Library", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
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
        t.setButtonText("To tray");
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
        d.setButtonText("To pads");
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
        out.push_back(pad);
    }
    return out;
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
    matches = chordlib::find(gen.libraryMood(), gen.libraryGenre(), gen.libraryFunction());
    page = juce::jlimit(0, numPages() - 1, page);

    const int n = (int) matches.size();
    countLabel.setText(n == 0 ? juce::String("nothing matches - widen a filter")
                              : juce::String(n) + (n == 1 ? " progression" : " progressions")
                                    + " of " + juce::String(chordlib::table().size()),
                       juce::dontSendNotification);
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
            skin::raisedFill(g, b, kRadius, juce::Colour(0xff272b32), juce::Colour(0xff1e2126));
            if (i == hovered)
            {
                g.setColour(accent.base.withAlpha(0.10f));
                g.fillRoundedRectangle(b, kRadius);
            }
        }

        const auto ink = lit ? inkOnAccent : skin::text;
        const auto dim = lit ? inkOnAccent.withAlpha(0.75f) : skin::textDim;

        auto r = b.reduced(8.0f, 4.0f);
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
        g.drawText(e->name, nameTop.toNearestInt(), juce::Justification::bottomLeft, true);
        g.setColour(lit ? inkOnAccent.withAlpha(0.8f) : accent.base);
        g.setFont(skin::micro(9.0f));
        g.drawText(juce::String(chordlib::functionName(e->function)).toUpperCase(),
                   funcLine.toNearestInt(), juce::Justification::topLeft, false);

        g.setColour(ink.withAlpha(lit ? 0.9f : 0.85f));
        g.setFont(skin::ui(13.0f));
        g.drawText(numeralStrip(*e), r.toNearestInt(), juce::Justification::centredLeft, true);

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
        g.drawText("No progression carries all three of those. Widen a filter.", area.toNearestInt(),
                   juce::Justification::centred, true);
    }
}

void ChordLibraryPanel::resized()
{
    auto area = getLocalBounds().reduced(kInset);

    {
        auto header = area.removeFromTop(kHeaderH);
        title.setBounds(header.removeFromLeft(kTitleW));
        closeButton.setBounds(header.removeFromRight(kCloseW).withSizeKeepingCentre(kCloseW, 26));
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
