#include "ChordGenPanel.h"
#include "../ChordMarkov.h"
#include "../ScaleModes.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>
#include <okstudio/Scales.h>
#include <okstudio/Theme.h>

namespace keys
{
namespace
{
    // The panel's geometry, written once and read by both resized() and contentSize(), so the
    // window's minimum size is the layout's own arithmetic and not a number somebody chose.
    constexpr int kInset = 12;   // margin round everything
    constexpr int kHeaderH = 28; // title, mode character, page, Close
    constexpr int kRowH = 44;    // a labelled control row: 14 px label over a 30 px control
    constexpr int kActionH = 36; // the page-wide buttons
    constexpr int kGap = 8;      // between cells in a row
    constexpr int kAfterHeader = 10;
    constexpr int kAfterRowA = 6;
    constexpr int kAfterRowB = 8;

    // Cell widths, per row. Named because contentSize() adds the same numbers up.
    constexpr int kKeyW = 70, kModeW = 190, kOctaveW = 110, kSourceW = 130;
    constexpr int kNotesW = 120, kInvW = 200, kComplianceW = 230, kLockInfW = 230;
    constexpr int kChainW = 110, kMoodW = 150, kStartW = 100, kTempW = 200, kLengthW = 150;
    constexpr int kFillW = 120, kRegenW = 150, kClearW = 120;
    constexpr int kTitleW = 160, kPageW = 120, kCloseW = 90;

    int rowWidth(std::initializer_list<int> cells)
    {
        int w = -kGap; // n cells have n-1 gaps between them
        for (const int c : cells)
            w += c + kGap;
        return w;
    }

    void styleLabel(juce::Label& l, const juce::String& text)
    {
        l.setText(text.toUpperCase(), juce::dontSendNotification);
        l.setFont(skin::micro(10.0f));
        l.setColour(juce::Label::textColourId, skin::textDim);
    }
} // namespace

juce::Point<int> ChordGenPanel::contentSize()
{
    // The widest row wins the width, and the rows plus the gaps between them make the height.
    // Both then take the panel's margin on each side.
    const int rows = juce::jmax(juce::jmax(rowWidth({ kTitleW, kPageW, kCloseW }),
                                           rowWidth({ kKeyW, kModeW, kOctaveW, kSourceW }),
                                           rowWidth({ kNotesW, kInvW, kComplianceW, kLockInfW })),
                                juce::jmax(rowWidth({ kChainW, kMoodW, kStartW, kTempW, kLengthW }),
                                           rowWidth({ kFillW, kRegenW, kClearW })));
    const int h = kHeaderH + kAfterHeader + kRowH + kAfterRowA + kRowH + kAfterRowB + kActionH;
    return { rows + kInset * 2, h + kInset * 2 };
}

juce::Point<int> ChordGenPanel::minWindowSize()
{
    // The content, plus what DetachedWindow puts round it: a 38 px title bar (mouse-only, so
    // the window buttons clear the 34 px floor) and the window's border. That border is 1 px a
    // side, not 4: DetachedWindow calls setResizable(true, true), so it gets a corner grip and
    // ResizableWindow::getBorderThickness answers 1 wherever there is no draggable frame. The
    // 8 px below is therefore generous by 6, deliberately - a floor that leaves a few px of
    // slack costs nothing, and a floor that is 6 px short clips the bottom row of controls.
    // Same sum the sections' own minimums are written from.
    const auto c = contentSize();
    return { c.x + 8, c.y + 38 + 8 };
}

juce::Point<int> ChordGenPanel::defaultWindowSize()
{
    // A little over the floor, so the first opening does not look like it is already jammed
    // against its own minimum.
    const auto m = minWindowSize();
    return { m.x + 60, m.y + 40 };
}

ChordGenPanel::ChordGenPanel(KeysProcessor& p, ChordGenMenu& g) : processor(p), gen(g)
{
    okstudio::ui::makeMouseOnly(*this);
    buildControls();
    // Pick the band that matches the source *now*, not on the first timer tick 66 ms from now.
    // Open this window with Source already on Markov and that tick is the difference between a
    // clean row and both sets painted over each other for a frame.
    applySource(! gen.readsScaleSettings());
    startTimerHz(15); // the mode's character line, the page number, and what the buttons can do
}

ChordGenPanel::~ChordGenPanel()
{
    stopTimer();
    // Nothing else to unwind. This class never calls noteOn: the one path that does, the
    // suggestion audition, is ChordGenMenu's and is released by that object's own 800 ms timer
    // and destructor. Closing this window can therefore never leave a note ringing.
}

void ChordGenPanel::buildControls()
{
    title.setText("Chord Generator", juce::dontSendNotification);
    title.setFont(skin::uiSemi(16.0f).withExtraKerningFactor(0.04f));
    title.setColour(juce::Label::textColourId, okstudio::theme::text);
    addAndMakeVisible(title);

    modeEmotion.setFont(juce::Font(juce::FontOptions(11.0f)));
    modeEmotion.setColour(juce::Label::textColourId, okstudio::theme::accentSoft);
    addAndMakeVisible(modeEmotion);

    // The on-screen way out. The title bar's X is the other, and the editor points both at one
    // call, so the two can never tear down differently.
    closeButton.setTitle("Close chord generator");
    closeButton.setTooltip("Shut this window. The generator keeps its settings, and Fill and "
                           "Regen stay on the Pads bar.");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeButton);

    styleLabel(rootLabel, "Key");
    addAndMakeVisible(rootLabel);
    rootBox.addItemList(okstudio::scales::noteNames(), 1);
    rootBox.setTitle("Generator key (window)"); // the Pads bar carries one on the same parameter
    addAndMakeVisible(rootBox);
    rootAtt = std::make_unique<ComboAtt>(processor.apvts, "genRoot", rootBox);

    styleLabel(modeLabel, "Mode");
    addAndMakeVisible(modeLabel);
    // The full names here, aliases and all: this window has the width the bar's combo does not,
    // and "Natural Minor (Aeolian)" is worth spelling out where there is room for it.
    modeBox.addItemList(modes::names(), 1);
    modeBox.setTitle("Generator mode (window)");
    addAndMakeVisible(modeBox);
    modeAtt = std::make_unique<ComboAtt>(processor.apvts, "genMode", modeBox);

    styleLabel(octaveLabel, "Octave");
    addAndMakeVisible(octaveLabel);
    octaveSlider.setSliderStyle(juce::Slider::IncDecButtons);
    octaveSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 26);
    octaveSlider.setRange(2, 6, 1);
    octaveSlider.setTooltip("Which register generated chords land in.");
    addAndMakeVisible(octaveSlider);
    octaveAtt = std::make_unique<SliderAtt>(processor.apvts, "genOctave", octaveSlider);

    styleLabel(notesLabel, "Notes");
    addAndMakeVisible(notesLabel);
    for (auto* b : { &triadsButton, &seventhsButton, &ninthsButton })
        addAndMakeVisible(*b);
    triadsButton.setTooltip("Triads (3 notes)");
    seventhsButton.setTooltip("7ths and 6ths (4 notes)");
    ninthsButton.setTooltip("9ths and extensions (5 notes)");
    triadsAtt = std::make_unique<ButtonAtt>(processor.apvts, "genTriads", triadsButton);
    seventhsAtt = std::make_unique<ButtonAtt>(processor.apvts, "genSevenths", seventhsButton);
    ninthsAtt = std::make_unique<ButtonAtt>(processor.apvts, "genNinths", ninthsButton);

    styleLabel(invLabel, "Inversions");
    addAndMakeVisible(invLabel);
    for (auto* b : { &inv0Button, &inv1Button, &inv2Button, &inv3Button })
        addAndMakeVisible(*b);
    inv0Button.setTooltip("Root position");
    inv0Att = std::make_unique<ButtonAtt>(processor.apvts, "genInv0", inv0Button);
    inv1Att = std::make_unique<ButtonAtt>(processor.apvts, "genInv1", inv1Button);
    inv2Att = std::make_unique<ButtonAtt>(processor.apvts, "genInv2", inv2Button);
    inv3Att = std::make_unique<ButtonAtt>(processor.apvts, "genInv3", inv3Button);

    styleLabel(complianceLabel, "Scale Compliance");
    addAndMakeVisible(complianceLabel);
    complianceSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    complianceSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 26);
    complianceSlider.setRange(0, 100, 1);
    complianceSlider.setTextValueSuffix(" %");
    complianceSlider.setTitle("Scale compliance (window)");
    complianceSlider.setTooltip("100% stays in the key. Lower borrows from parallel modes, "
                                "then secondary dominants, then anything.");
    addAndMakeVisible(complianceSlider);
    complianceAtt = std::make_unique<SliderAtt>(processor.apvts, "genCompliance", complianceSlider);

    styleLabel(lockInfluenceLabel, "Lock Influence");
    addAndMakeVisible(lockInfluenceLabel);
    lockInfluenceSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    lockInfluenceSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 26);
    lockInfluenceSlider.setRange(0, 100, 1);
    lockInfluenceSlider.setTextValueSuffix(" %");
    lockInfluenceSlider.setTooltip("How much new chords copy the character of the ones you "
                                   "locked. Lock a card from its right-click menu.");
    addAndMakeVisible(lockInfluenceSlider);
    lockInfluenceAtt = std::make_unique<SliderAtt>(processor.apvts, "genLockInfluence", lockInfluenceSlider);

    // The page-wide actions, all three straight through to the brain. Fill and Regen are also
    // chips on the Pads bar; this is the same call, not a second implementation.
    fillButton.setTitle("Fill chord page (window)");
    fillButton.setTooltip("Fill the empty pads on this page. Nothing already on the page is "
                          "touched.");
    fillButton.onClick = [this] { gen.fillPage(); };
    regenButton.setTitle("Regenerate unlocked chords (window)");
    regenButton.setTooltip("Replace the chords on this page with new ones, except on locked "
                           "pads. Lock a card from its right-click menu to keep it.");
    regenButton.onClick = [this] { gen.regeneratePage(); };
    // Clear page lives here and nowhere else. It empties every unlocked pad on the page and
    // Keys has no undo, so it belongs behind a window you opened on purpose rather than 4 px
    // from Regen on a bar (which is where it was until 2026-07-30).
    clearButton.setTitle("Clear chord page");
    clearButton.setTooltip("Empty every unlocked pad on this page. There is no undo.");
    clearButton.onClick = [this] { gen.clearPage(); };
    for (auto* b : { &fillButton, &regenButton, &clearButton })
        addAndMakeVisible(*b);

    // The generator's brain: the weighted pool above, or Octavium's Markov chains.
    styleLabel(sourceLabel, "Source");
    addAndMakeVisible(sourceLabel);
    sourceBox.addItemList({ "Algorithmic", "Markov" }, 1);
    sourceBox.setTitle("Generator source");
    sourceBox.setTooltip("Algorithmic weighs chords by degree and scale compliance; Markov "
                         "walks a table of moves taken from real progressions.");
    addAndMakeVisible(sourceBox);
    sourceAtt = std::make_unique<ComboAtt>(processor.apvts, "genSource", sourceBox);

    styleLabel(chainLabel, "Chain");
    chainBox.addItemList({ "Major", "Minor", "Modal" }, 1);
    chainBox.setTitle("Markov chain");
    chainAtt = std::make_unique<ComboAtt>(processor.apvts, "markovMode", chainBox);

    styleLabel(moodLabel, "Mood");
    styleLabel(startLabel, "Start");
    // Mood and Start are the two picks that are not parameters: they belong to the progression
    // being generated right now, so they live on ChordGenMenu and outlive this window. The
    // combo boxes are a view of that state in both directions.
    moodBox.setTitle("Markov mood");
    moodBox.onChange = [this] { gen.setMoodChoice(moodBox.getSelectedId() <= 1 ? juce::String()
                                                                              : moodBox.getText()); };
    startBox.addItem("Any", 1);
    {
        int id = 2;
        for (const char* token : markov::startTokens())
            startBox.addItem(token, id++);
    }
    startBox.setTitle("Markov start");
    startBox.setTooltip("Force the first chord of the progression, or let the chain pick.");
    startBox.setSelectedId(1, juce::dontSendNotification);
    startBox.onChange = [this] { gen.setStartChoice(startBox.getSelectedId() <= 1 ? juce::String()
                                                                                 : startBox.getText()); };
    for (int i = 0; i < startBox.getNumItems(); ++i)
        if (startBox.getItemText(i) == gen.startChoice())
            startBox.setSelectedId(startBox.getItemId(i), juce::dontSendNotification);

    styleLabel(tempLabel, "Temperature");
    tempSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    tempSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 26);
    tempSlider.setRange(0.3, 2.0, 0.01);
    tempSlider.setTooltip("Low sticks to the corpus's most common moves; high wanders.");
    tempAtt = std::make_unique<SliderAtt>(processor.apvts, "markovTemp", tempSlider);

    styleLabel(lengthLabel, "Length");
    lengthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    lengthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 26);
    lengthSlider.setRange(4, 16, 1);
    lengthSlider.setTooltip("Unique chords generated before the page loops them.");
    lengthAtt = std::make_unique<SliderAtt>(processor.apvts, "markovLength", lengthSlider);

    // Parented but not shown: applySource() decides which of the two row-B sets is on screen,
    // and the constructor runs it once before this panel is ever painted.
    for (auto* c : markovBand())
        addChildComponent(*c);
    refreshMoodItems();

    pageLabel.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    pageLabel.setColour(juce::Label::textColourId, okstudio::theme::textDim);
    pageLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(pageLabel);
}

// The two sets that share row B. They are laid into the *same* rect, so exactly one of them
// may be visible: at the layout's own minimum width the algorithmic set is 804 px wide and the
// Markov set 742, and leaving both on screen put 62 px of a greyed Lock Influence slider, its
// percent box and a sliver of its label out to the right of Length.
std::array<juce::Component*, 13> ChordGenPanel::algorithmicBand()
{
    return { &notesLabel, &triadsButton, &seventhsButton, &ninthsButton,
             &invLabel, &inv0Button, &inv1Button, &inv2Button, &inv3Button,
             &complianceLabel, &complianceSlider, &lockInfluenceLabel, &lockInfluenceSlider };
}

std::array<juce::Component*, 10> ChordGenPanel::markovBand()
{
    return { &chainLabel, &chainBox, &moodLabel, &moodBox, &startLabel,
             &startBox, &tempLabel, &tempSlider, &lengthLabel, &lengthSlider };
}

void ChordGenPanel::applySource(bool markov)
{
    markovShown = markov;
    for (auto* c : markovBand())
        c->setVisible(markov);
    for (auto* c : algorithmicBand())
        c->setVisible(! markov);
    // Mode is row A's, so it never overlaps anything and stays on screen either way. It means
    // nothing to a chain walk, so it greys rather than hides (Octavium left it clickable and
    // silently ignored it; greying is honest). Everything else the source turns off is in the
    // band above, where hiding says the same thing more plainly.
    modeBox.setEnabled(! markov);
    modeLabel.setEnabled(! markov);
    resized();
}

void ChordGenPanel::refreshMoodItems()
{
    lastChainMode = gen.chainMode();
    const auto keep = gen.moodChoice();
    moodBox.clear(juce::dontSendNotification);
    moodBox.addItem("Any", 1);
    int id = 2;
    for (const auto& m : markov::moodsFor(lastChainMode))
        moodBox.addItem(m, id++);
    moodBox.setSelectedId(1, juce::dontSendNotification);
    for (int i = 0; i < moodBox.getNumItems(); ++i)
        if (moodBox.getItemText(i) == keep)
            moodBox.setSelectedId(moodBox.getItemId(i), juce::dontSendNotification);
    // The chain may have moved under a mood that no longer exists in it; say so out loud
    // rather than leaving the box reading Any while the brain still holds the old tag.
    gen.setMoodChoice(moodBox.getSelectedId() <= 1 ? juce::String() : moodBox.getText());
}

void ChordGenPanel::timerCallback()
{
    const int mode = juce::jlimit(0, modes::count() - 1,
                                  (int) processor.apvts.getRawParameterValue("genMode")->load());
    modeEmotion.setText(modes::get(mode).emotion, juce::dontSendNotification);
    pageLabel.setText("Page " + juce::String(processor.padPage() + 1) + " of "
                          + juce::String(KeysProcessor::numPadPages),
                      juce::dontSendNotification);

    // Each action greys itself out when it would find nothing to do, the same answers the two
    // chips on the Pads bar grey on. Clear page takes exactly what Regen would - every unlocked
    // pad that carries a chord - so it asks the same question.
    fillButton.setEnabled(gen.pageHasEmptyPads());
    regenButton.setEnabled(gen.pageHasRegeneratablePads());
    clearButton.setEnabled(gen.pageHasRegeneratablePads());

    // The Mood list belongs to the chain that is up.
    if (gen.chainMode() != lastChainMode)
        refreshMoodItems();

    // Source switch: swap which of the two row-B bands is on screen. The source is a
    // parameter, so it can move from the window's own combo, from the host or from a session
    // load, and this poll is what catches all three.
    if (const bool markov = ! gen.readsScaleSettings(); markov != markovShown)
        applySource(markov);
}

void ChordGenPanel::paint(juce::Graphics& g)
{
    // A window of its own, so this is the whole surface rather than a card floating on the
    // editor: the plain background every detached section's holder paints, and one accent hair
    // line under the header to separate the settings from the title row.
    g.fillAll(skin::bgBot);

    const auto band = getLocalBounds().reduced(kInset).removeFromTop(kHeaderH + kAfterHeader / 2);
    g.setColour(skin::accentOf(*this).base.withAlpha(0.25f));
    g.fillRect((float) band.getX(), (float) band.getBottom(), (float) band.getWidth(), 1.0f);
}

void ChordGenPanel::resized()
{
    auto area = getLocalBounds().reduced(kInset);
    if (area.isEmpty())
        return;

    auto top = area.removeFromTop(kHeaderH);
    title.setBounds(top.removeFromLeft(kTitleW));
    closeButton.setBounds(top.removeFromRight(kCloseW).withSizeKeepingCentre(kCloseW, kHeaderH));
    pageLabel.setBounds(top.removeFromRight(kPageW));
    modeEmotion.setBounds(top.reduced(kGap, 0));
    area.removeFromTop(kAfterHeader);

    const auto cell = [](juce::Rectangle<int>& row, int w, juce::Label& lab, juce::Component& ctl)
    {
        auto c = row.removeFromLeft(w);
        row.removeFromLeft(kGap);
        lab.setBounds(c.removeFromTop(14));
        ctl.setBounds(c);
    };

    // Row A: key, mode, octave, and which brain generates. Every one of these is live under
    // both sources except Mode, which the timer greys.
    auto rowA = area.removeFromTop(kRowH);
    cell(rowA, kKeyW, rootLabel, rootBox);
    cell(rowA, kModeW, modeLabel, modeBox);
    cell(rowA, kOctaveW, octaveLabel, octaveSlider);
    cell(rowA, kSourceW, sourceLabel, sourceBox);
    area.removeFromTop(kAfterRowA);

    // Row B: note counts, inversions and the two weighting sliders - or, when the Markov source
    // is up, its chain controls in the same band. Both are laid out here and applySource() has
    // already hidden one of them, which it must: these two rects overlap and the algorithmic
    // set is the wider, so a hidden-but-visible one paints out past the other's right edge.
    auto rowB = area.removeFromTop(kRowH);
    {
        auto markovRow = rowB;
        cell(markovRow, kChainW, chainLabel, chainBox);
        cell(markovRow, kMoodW, moodLabel, moodBox);
        cell(markovRow, kStartW, startLabel, startBox);
        cell(markovRow, kTempW, tempLabel, tempSlider);
        cell(markovRow, kLengthW, lengthLabel, lengthSlider);
    }
    {
        auto c = rowB.removeFromLeft(kNotesW);
        rowB.removeFromLeft(kGap);
        notesLabel.setBounds(c.removeFromTop(14));
        const int w = c.getWidth() / 3;
        triadsButton.setBounds(c.removeFromLeft(w));
        seventhsButton.setBounds(c.removeFromLeft(w));
        ninthsButton.setBounds(c);
    }
    {
        auto c = rowB.removeFromLeft(kInvW);
        rowB.removeFromLeft(kGap);
        invLabel.setBounds(c.removeFromTop(14));
        const int w = c.getWidth() / 4;
        inv0Button.setBounds(c.removeFromLeft(w));
        inv1Button.setBounds(c.removeFromLeft(w));
        inv2Button.setBounds(c.removeFromLeft(w));
        inv3Button.setBounds(c);
    }
    cell(rowB, kComplianceW, complianceLabel, complianceSlider);
    cell(rowB, kLockInfW, lockInfluenceLabel, lockInfluenceSlider);
    area.removeFromTop(kAfterRowB);

    // Row C: the page-wide actions. Clear is last and furthest from the two constructive ones,
    // because it is the only button in here that can lose work.
    auto rowC = area.removeFromTop(kActionH);
    fillButton.setBounds(rowC.removeFromLeft(kFillW));
    rowC.removeFromLeft(kGap);
    regenButton.setBounds(rowC.removeFromLeft(kRegenW));
    rowC.removeFromLeft(kGap);
    clearButton.setBounds(rowC.removeFromLeft(kClearW));
}
} // namespace keys
