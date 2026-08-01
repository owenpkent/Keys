#include "ChordGenPanel.h"
#include "../ChordMarkov.h"
#include "../ChordSources.h" // the Progression picker's items are that table's own names
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
    constexpr int kGap = 8;      // between cells in a row
    constexpr int kAfterHeader = 10;
    constexpr int kAfterRowA = 6;
    constexpr int kAfterRowB = 8;

    // The audition tray. Its rows are 54 px rather than the 34 px mouse-only floor because a
    // card carries a chord name over a note list, exactly as a pad card does, and 34 px fits one
    // of those two. Four rows plus its own header line is what the window grows by.
    // 38, not the 20 a caption line needs, because Reroll rides this row and a button is a
    // target: the mouse-only floor is ~34 px and a caption-height strip put it at 18.
    constexpr int kTrayHeaderH = 38;
    constexpr int kTrayRowH = 54;
    constexpr int kTrayGap = 6;
    constexpr int kTrayH = kTrayHeaderH + 4 * kTrayRowH + 3 * kTrayGap;
    constexpr int kTrayMinW = 4 * 150 + 3 * kTrayGap; // four readable cards side by side

    // The reference row: a caption, one chord card, and the three buttons that act on it. 56 px
    // so the card can carry a name over a note list the way every other card in Keys does, and
    // still leave the buttons their 34 px floor.
    constexpr int kRefH = 56;
    constexpr int kAfterRef = 10;
    constexpr int kRefLabelW = 84;
    constexpr int kRefCardW = 190;
    constexpr int kSimilarW = 110, kFollowW = 140, kClearRefW = 90;

    // Cell widths, per row. Named because contentSize() adds the same numbers up.
    constexpr int kKeyW = 70, kModeW = 190, kOctaveW = 110, kSourceW = 130;
    constexpr int kNotesW = 120, kInvW = 200, kComplianceW = 230, kLockInfW = 230;
    constexpr int kChainW = 110, kMoodW = 150, kStartW = 100, kTempW = 200, kLengthW = 150;
    constexpr int kSmoothW = 210;    // row A, under every source
    constexpr int kCircleDirW = 220; // and one per band added 2026-08-01
    constexpr int kPlrW = 200;
    constexpr int kProgW = 260;
    constexpr int kPlaningW = 140;
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
                                           rowWidth({ kKeyW, kModeW, kOctaveW, kSourceW, kSmoothW }),
                                           rowWidth({ kNotesW, kInvW, kComplianceW, kLockInfW })),
                                juce::jmax(rowWidth({ kChainW, kMoodW, kStartW, kTempW, kLengthW }),
                                           rowWidth({ kPlrW, kPlrW, kPlrW }),
                                           kTrayMinW + kGap + rowWidth({ kFillW, kRegenW, kClearW })));
    // No action row of its own since 2026-08-01: the three buttons ride the tray's header, which
    // is the row that says they belong to it. The reference row is its own though, because what
    // is on it is a card rather than controls and a card wants the height.
    const int h = kHeaderH + kAfterHeader + kRowH + kAfterRowA + kRowH + kAfterRowB
                  + kRefH + kAfterRef + kTrayH;
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

ChordGenPanel::ChordGenPanel(KeysProcessor& p, ChordGenMenu& g)
    : processor(p), gen(g), tray(p, g), refCard(p, g)
{
    okstudio::ui::makeMouseOnly(*this);
    buildControls();
    // Pick the band that matches the source *now*, not on the first timer tick 66 ms from now.
    // Open this window with Source already on Markov and that tick is the difference between a
    // clean row and both sets painted over each other for a frame.
    applySource(gen.sourceIndex());
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

    // The three actions, and every one of them acts on the **tray** rather than on the page
    // (Owen, 2026-08-01: "when you click on regenerate unlocked, I don't want it to regenerate
    // the ones in the host window, only in the card generator window"). Nothing in this window
    // writes a pad now; the only way a chord in here reaches the strip is a drag you made.
    //
    // They keep the shape they had - the safe one, the destructive one, the empty one - because
    // that split is worth keeping, not because the names had to survive. What changed is what
    // they are destructive *to*, and a tray card is not in the session and is one drag from a
    // pad if you want it, so none of the three can lose work and Clear needs no lock to respect.
    //
    // The Pads bar still carries Fill and Regen for the page itself, next to the pads they
    // write, which is where a page-wide action belongs. Clear page is gone with this change:
    // emptying sixteen pads at once with no undo had one home and this window was it.
    fillButton.setButtonText("Fill");
    fillButton.setTitle("Fill empty tray cells");
    fillButton.setTooltip("Generate a candidate into every empty cell of the tray. Nothing "
                          "already in the tray is touched, and no pad is written.");
    fillButton.onClick = [this] { tray.fill(); };
    regenButton.setButtonText("Regen");
    regenButton.setTitle("Regenerate tray candidates");
    regenButton.setTooltip("Replace the candidates in the tray with new ones. Your pads are not "
                           "touched - only a drag onto a pad writes one.");
    regenButton.onClick = [this] { tray.regen(); };
    clearButton.setButtonText("Clear");
    clearButton.setTitle("Clear the tray");
    clearButton.setTooltip("Empty the tray. Nothing is lost: a candidate is not on a pad until "
                           "you drag it onto one.");
    clearButton.onClick = [this] { tray.clear(); };
    for (auto* b : { &fillButton, &regenButton, &clearButton })
        addAndMakeVisible(*b);

    // The generator's brain: the weighted pool above, or Octavium's Markov chains.
    styleLabel(sourceLabel, "Source");
    addAndMakeVisible(sourceLabel);
    // Seven, in the parameter's own order. Never reorder: the choice index is what a session
    // stores, so moving an entry moves every saved session's source with it.
    sourceBox.addItemList({ "Algorithmic", "Markov", "Circle of Fifths", "Neo-Riemannian",
                            "Progressions", "Negative Harmony", "Planing" },
                          1);
    sourceBox.setTitle("Generator source");
    sourceBox.setTooltip("Which brain writes the chords. Each one brings its own settings to the "
                         "row below.");
    addAndMakeVisible(sourceBox);
    sourceAtt = std::make_unique<ComboAtt>(processor.apvts, "genSource", sourceBox);

    // Voice leading, in row A because it belongs to all seven sources rather than to any of them.
    styleLabel(smoothLabel, "Voice Leading");
    addAndMakeVisible(smoothLabel);
    smoothSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    smoothSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 26);
    smoothSlider.setRange(0, 100, 1);
    smoothSlider.setTextValueSuffix(" %");
    smoothSlider.setTooltip("How much each chord is revoiced to move the least from the one "
                            "before it. 0 leaves the voicings alone. Never changes which notes a "
                            "chord contains, only which octave they sit in.");
    addAndMakeVisible(smoothSlider);
    smoothAtt = std::make_unique<SliderAtt>(processor.apvts, "genSmooth", smoothSlider);

    // --- the five bands added on 2026-08-01, one per new source -------------------------------
    styleLabel(circleDirLabel, "Direction");
    circleDirBox.addItemList({ "Flat-ward (down a 5th)", "Sharp-ward (up a 5th)" }, 1);
    circleDirBox.setTitle("Circle direction");
    circleDirBox.setTooltip("Which way round the circle the walk goes. Flat-ward is the falling "
                            "fifth that most progressions are built on.");
    circleDirAtt = std::make_unique<ComboAtt>(processor.apvts, "genCircleDir", circleDirBox);

    // Relative weights, not percentages, which is why they do not have to add up and why all
    // three at zero is read as equal thirds rather than as "generate nothing".
    styleLabel(plrPLabel, "P (Parallel)");
    styleLabel(plrLLabel, "L (Leading-tone)");
    styleLabel(plrRLabel, "R (Relative)");
    for (auto* s : { &plrPSlider, &plrLSlider, &plrRSlider })
    {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 26);
        s->setRange(0, 100, 1);
    }
    plrPSlider.setTooltip("Major to minor on the same root, and back. C to Cm.");
    plrLSlider.setTooltip("The root falls a semitone: C E G to B E G, which is E minor.");
    plrRSlider.setTooltip("To the relative: C major to A minor, A minor to C major.");
    plrPAtt = std::make_unique<SliderAtt>(processor.apvts, "genPlrP", plrPSlider);
    plrLAtt = std::make_unique<SliderAtt>(processor.apvts, "genPlrL", plrLSlider);
    plrRAtt = std::make_unique<SliderAtt>(processor.apvts, "genPlrR", plrRSlider);

    styleLabel(progressionLabel, "Progression");
    {
        // The picker's items are the table's own names, so the two can never drift apart.
        int id = 1;
        for (const auto& n : sources::progressionNames())
            progressionBox.addItem(n, id++);
    }
    progressionBox.setTitle("Progression template");
    progressionBox.setTooltip("A real progression, transposed to your key. Random picks a "
                              "different one each time you generate.");
    progressionAtt = std::make_unique<ComboAtt>(processor.apvts, "genProgression", progressionBox);

    styleLabel(planingLabel, "Planing");
    planingDiatonicButton.setTooltip("On: the shape slides through the scale, so its quality "
                                     "bends to fit the key. Off: it slides chromatically and the "
                                     "shape is preserved exactly, which is the Debussy sound.");
    planingDiatonicAtt = std::make_unique<ButtonAtt>(processor.apvts, "genPlaningDiatonic",
                                                     planingDiatonicButton);

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

    // Parented but not shown: applySource() decides which row-B band is on screen, and the
    // constructor runs it once before this panel is ever painted. Every band except the
    // algorithmic one goes in as a child component, so nothing flashes on the first paint.
    for (int src = 1; src <= 6; ++src)
        for (auto* c : bandFor(src))
            addChildComponent(*c);
    refreshMoodItems();

    // The audition tray, and the one line that says what a card in it does. The label is worth
    // its 20 px: a drag is the only way a candidate reaches a pad, and a gesture nothing on
    // screen mentions is a gesture nobody finds.
    styleLabel(trayLabel, "Audition - click a chord to hear it, drag it onto a pad to keep it");
    addAndMakeVisible(trayLabel);

    // The reference chord: one slot the tray's own actions cannot reach. Fill, Regen and Clear
    // all stop at the tray, so the chord you are working *from* survives every answer you ask
    // for. Both fills seed from it, which is what makes keeping it worth anything.
    styleLabel(refLabel, "Reference");
    addAndMakeVisible(refLabel);
    addAndMakeVisible(refCard);

    similarButton.setTitle("Fill tray with chords similar to the reference");
    similarButton.setTooltip("Fill the tray with the reference chord in other colours: sevenths, "
                             "ninths, sus, the parallel major or minor. Same root throughout.");
    similarButton.onClick = [this]
    { tray.setAll(gen.similarTo(refCard.chord().notes, ChordTray::numCells)); };
    followButton.setTitle("Fill tray with chords that could follow the reference");
    followButton.setTooltip("Fill the tray with chords that could come after the reference: the "
                            "same eighteen moves a pad's card menu offers.");
    followButton.onClick = [this]
    { tray.setAll(gen.couldFollow(refCard.chord().notes, ChordTray::numCells)); };
    clearRefButton.setTitle("Clear the reference chord");
    clearRefButton.setTooltip("Empty the reference slot. The tray is not touched.");
    clearRefButton.onClick = [this] { refCard.clearChord(); };
    for (auto* b : { &similarButton, &followButton, &clearRefButton })
        addAndMakeVisible(*b);

    // Straight through to whoever holds the pad strip. Unwired, the tray still auditions.
    // A tray drag is offered to the reference card first, because that target is inside this
    // window and the editor's is not: asking the far end about a point that never left this
    // window would light a pad under a drag that was always going to land here.
    tray.onDragOver = [this](juce::Point<int> p)
    {
        const bool overRef = refCard.getScreenBounds().contains(p);
        refCard.setDropHighlight(overRef);
        if (onCandidateDragOver)
            onCandidateDragOver(overRef ? juce::Point<int> { -1, -1 } : p);
    };
    tray.onDrop = [this](juce::Point<int> p, const KeysProcessor::ChordPad& pad)
    {
        if (refCard.getScreenBounds().contains(p))
        {
            refCard.setChord(pad);
            // False, so the tray keeps the card: a reference is a *copy* of a chord you like,
            // and taking the candidate away as payment for keeping it would be backwards.
            return false;
        }
        return onCandidateDropped ? onCandidateDropped(p, pad) : false;
    };
    tray.onDragEnd = [this] { if (onCandidateDragEnd) onCandidateDragEnd(); };
    tray.onSendToFirstEmpty = [this](const KeysProcessor::ChordPad& pad)
    { return onCandidateToFirstEmptyPad ? onCandidateToFirstEmptyPad(pad) : false; };
    tray.onPageHasEmptyPad = [this] { return onPageHasEmptyPad ? onPageHasEmptyPad() : false; };
    addAndMakeVisible(tray);

    pageLabel.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    pageLabel.setColour(juce::Label::textColourId, okstudio::theme::textDim);
    pageLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(pageLabel);
}

// The two sets that share row B. They are laid into the *same* rect, so exactly one of them
// may be visible: at the layout's own minimum width the algorithmic set is 804 px wide and the
// Markov set 742, and leaving both on screen put 62 px of a greyed Lock Influence slider, its
// percent box and a sliver of its label out to the right of Length.
std::vector<juce::Component*> ChordGenPanel::bandFor(int source)
{
    switch (source)
    {
        case 1: // Markov
            return { &chainLabel, &chainBox, &moodLabel, &moodBox, &startLabel,
                     &startBox, &tempLabel, &tempSlider, &lengthLabel, &lengthSlider };
        case 2: // Circle of Fifths
            return { &circleDirLabel, &circleDirBox };
        case 3: // Neo-Riemannian
            return { &plrPLabel, &plrPSlider, &plrLLabel, &plrLSlider, &plrRLabel, &plrRSlider };
        case 4: // Progressions
            return { &progressionLabel, &progressionBox };
        case 5: // Negative Harmony: Key, Mode and Octave in row A are the whole of it
            return {};
        case 6: // Planing
            return { &planingLabel, &planingDiatonicButton };
        default: // Algorithmic
            return { &notesLabel, &triadsButton, &seventhsButton, &ninthsButton,
                     &invLabel, &inv0Button, &inv1Button, &inv2Button, &inv3Button,
                     &complianceLabel, &complianceSlider, &lockInfluenceLabel, &lockInfluenceSlider };
    }
}

std::vector<juce::Component*> ChordGenPanel::allBandControls()
{
    // Every band's controls, so applySource can hide the lot before showing one and never has to
    // know which band it is hiding. Built from bandFor rather than listed again, because a second
    // list of the same components is a list that goes stale the first time a band gains a control.
    std::vector<juce::Component*> all;
    for (int s = 0; s <= 6; ++s)
        for (auto* c : bandFor(s))
            all.push_back(c);
    return all;
}

void ChordGenPanel::applySource(int source)
{
    shownSource = source;
    for (auto* c : allBandControls())
        c->setVisible(false);
    for (auto* c : bandFor(source))
        c->setVisible(true);
    // Mode is row A's, so it never overlaps anything and stays on screen whatever is below. It
    // means nothing to the sources that do not weigh a scale, so it greys rather than hides
    // (Octavium left it clickable and silently ignored it; greying is honest). Everything else a
    // source turns off is in the band, where hiding says the same thing more plainly.
    const bool scaleAware = gen.readsScaleSettings();
    modeBox.setEnabled(scaleAware);
    modeLabel.setEnabled(scaleAware);
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

// The reverse crossing: a pad being dragged out of the main window, offered to the reference.
// Bounds-checked rather than hit-tested through Desktop::findComponentAt, unlike the drop going
// the other way. That test exists because the *generator window* can cover the pad strip; this
// direction has the opposite problem and no equivalent, since a drag that has reached this
// window is over it by definition.
void ChordGenPanel::showReferenceDropTarget(juce::Point<int> screenPos)
{
    refCard.setDropHighlight(refCard.getScreenBounds().contains(screenPos));
}

bool ChordGenPanel::offerReferenceDrop(juce::Point<int> screenPos, const KeysProcessor::ChordPad& pad)
{
    refCard.setDropHighlight(false);
    if (! refCard.getScreenBounds().contains(screenPos) || pad.notes.empty())
        return false;
    refCard.setChord(pad);
    return true; // and ChordPads reads this as "do not clear the card I just dragged"
}

void ChordGenPanel::clearReferenceDropTarget()
{
    refCard.setDropHighlight(false);
}

void ChordGenPanel::timerCallback()
{
    pageLabel.setText("Page " + juce::String(processor.padPage() + 1) + " of "
                          + juce::String(KeysProcessor::numPadPages),
                      juce::dontSendNotification);

    // Each action greys itself out when it would find nothing to do. These ask the tray now, not
    // the page: Fill wants a hole to write into, and Regen and Clear both want a candidate that
    // is actually there. The two chips on the Pads bar still ask the page the same questions
    // about the pads, which is the pair of answers `gen` still exposes.
    fillButton.setEnabled(tray.hasEmptyCells());
    regenButton.setEnabled(tray.hasFilledCells());
    clearButton.setEnabled(tray.hasFilledCells());

    // All three reference actions need a reference. An empty slot greys them rather than hiding
    // them, so the row still says what the box is for while it is empty.
    const bool haveRef = refCard.hasChord();
    similarButton.setEnabled(haveRef);
    followButton.setEnabled(haveRef);
    clearRefButton.setEnabled(haveRef);

    // The Mood list belongs to the chain that is up.
    if (gen.chainMode() != lastChainMode)
        refreshMoodItems();

    // And the tray belongs to the settings: change the Key and the sixteen candidates on screen
    // are answers to the old question. It rerolls itself rather than greying a button, because
    // unlike every other action in this window a tray card is not state and rerolling can lose
    // nothing - a candidate you wanted is already on a pad. Polled for the same reason the
    // source swap below is: these are parameters, so they also move from the Pads bar, the host
    // and a session load.
    tray.refreshForSettings();

    // Source switch: swap which row-B band is on screen. The source is a parameter, so it can
    // move from the window's own combo, from the host or from a session load, and this poll is
    // what catches all three.
    if (const int src = gen.sourceIndex(); src != shownSource)
        applySource(src);
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
    // Voice leading is here rather than in a band because it belongs to every source: it is a
    // pass over whatever a source produced, so no band may be able to hide it.
    cell(rowA, kSmoothW, smoothLabel, smoothSlider);
    area.removeFromTop(kAfterRowA);

    // Row B: note counts, inversions and the two weighting sliders - or, when the Markov source
    // is up, its chain controls in the same band. Both are laid out here and applySource() has
    // already hidden one of them, which it must: these two rects overlap and the algorithmic
    // set is the wider, so a hidden-but-visible one paints out past the other's right edge.
    auto rowB = area.removeFromTop(kRowH);
    // A pristine copy, because the algorithmic band below consumes `rowB` as it goes and every
    // other band has to start from the same left edge rather than from whatever is left over.
    const auto rowFull = rowB;
    {
        auto markovRow = rowFull;
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

    // The four bands added on 2026-08-01, each laid into that same rect from the left. Every one
    // of them is invisible unless its source is up, which applySource guarantees, so overlapping
    // rects here are correct rather than a bug waiting to happen. Negative Harmony is absent on
    // purpose: it has no band, so there is nothing to place.
    {
        auto row = rowFull;
        cell(row, kCircleDirW, circleDirLabel, circleDirBox);
    }
    {
        auto row = rowFull;
        cell(row, kPlrW, plrPLabel, plrPSlider);
        cell(row, kPlrW, plrLLabel, plrLSlider);
        cell(row, kPlrW, plrRLabel, plrRSlider);
    }
    {
        auto row = rowFull;
        cell(row, kProgW, progressionLabel, progressionBox);
    }
    {
        auto row = rowFull;
        auto c = row.removeFromLeft(kPlaningW);
        planingLabel.setBounds(c.removeFromTop(14));
        planingDiatonicButton.setBounds(c);
    }
    area.removeFromTop(kAfterRowB);

    // The tray takes everything left, so making the window taller makes the cards taller rather
    // than leaving a band of background under them. Its header carries the instruction on the
    // left and the three actions on the right, and they sit *on the tray's own header* rather
    // than in a row of their own above it because that is the whole point of the change: a
    // button in this window acts on the tray it is attached to, and nothing here writes a pad.
    // Clear is last and furthest from the two constructive ones, as it was on the old row.
    // The reference row, above the tray and outside it, which is the point: everything below is
    // disposable and this is not.
    {
        auto row = area.removeFromTop(kRefH);
        refLabel.setBounds(row.removeFromLeft(kRefLabelW).withSizeKeepingCentre(kRefLabelW, 14));
        refCard.setBounds(row.removeFromLeft(kRefCardW));
        row.removeFromLeft(kGap * 2);
        const auto refAction = [&row](juce::TextButton& b, int w)
        {
            b.setBounds(row.removeFromLeft(w).withSizeKeepingCentre(w, 34));
            row.removeFromLeft(kGap);
        };
        refAction(similarButton, kSimilarW);
        refAction(followButton, kFollowW);
        refAction(clearRefButton, kClearRefW);
    }
    area.removeFromTop(kAfterRef);

    auto trayHeader = area.removeFromTop(kTrayHeaderH);
    const auto action = [&trayHeader](juce::TextButton& b, int w)
    {
        b.setBounds(trayHeader.removeFromRight(w).withSizeKeepingCentre(w, 34));
        trayHeader.removeFromRight(kGap);
    };
    action(clearButton, kClearW);
    action(regenButton, kRegenW);
    action(fillButton, kFillW);
    trayLabel.setBounds(trayHeader);
    tray.setBounds(area);
}
} // namespace keys
