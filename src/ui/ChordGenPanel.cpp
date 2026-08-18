#include "ChordGenPanel.h"
#include "../ChordLibrary.h"
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

    // Source is a row of its own now: a 14 px caption over seven 34 px buttons, which is the
    // mouse-only floor and the reason this is not a combo box. The two fixed rows under it carry
    // what every source can see whether or not it reads it: Notes, Inversions and the Octave
    // range on one, Scale Compliance, Lock Influence and Smooth Voicing on the other. They grey
    // where they are dead rather than leaving the row.
    constexpr int kSourceRowH = 48;
    constexpr int kAfterSourceRow = 6;
    constexpr int kAfterViz = 8;
    constexpr int kFixedRowH = 44;
    constexpr int kAfterFixedRow = 6;
    // A tick box is 34 px wide and the full height of its cell. The mouse-only floor applies to a
    // check box exactly as it does to a button, and a tick in the 14 px caption strip would be a
    // target you cannot hit.
    constexpr int kCheckW = 34;
    constexpr int kSourceBtnW = 128; // eight of these plus gaps sets the window's floor width

    // The audition tray. Its rows are 54 px rather than the 34 px mouse-only floor because a
    // card carries a chord name over a note list, exactly as a pad card does, and 34 px fits one
    // of those two. Four rows plus its own header line is what the window grows by.
    // 38, not the 20 a caption line needs, because Fill / Regen / Clear ride this row and a
    // button is a target: the mouse-only floor is ~34 px and a caption-height strip put the
    // button that used to be here at 18.
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
    // 230, not the 120 the three tick boxes needed: two IncDec steppers each want a readout plus
    // a pair of 34 px targets, and at 120 the buttons collapsed to a stacked 22 px pair that is
    // under the mouse-only floor. A stepper is the click-only path to a value, so it is the one
    // control that must never be squeezed.
    constexpr int kNotesW = 230, kInvW = 200, kComplianceW = 230, kLockInfW = 230;
    constexpr int kChainW = 110, kMoodW = 150, kStartW = 100, kTempW = 200, kLengthW = 150;
    constexpr int kSmoothW = 210;    // row A, under every source
    constexpr int kBrightW = 300, kMajMinW = 220;
    constexpr int kCircleDirW = 220; // and one per band added 2026-08-01
    constexpr int kPlrW = 200;
    constexpr int kProgW = 260;
    constexpr int kPlaningW = 140;
    constexpr int kLibMoodW = 150, kLibGenreW = 160, kLibFuncW = 140, kLibResultW = 320;
    constexpr int kFillW = 120, kRegenW = 150, kClearW = 120;
    constexpr int kTitleW = 160, kPageW = 120, kCloseW = 90;

    int rowWidth(std::initializer_list<int> cells)
    {
        int w = -kGap; // n cells have n-1 gaps between them
        for (const int c : cells)
            w += c + kGap;
        return w;
    }

    // The seven diatonic modes ordered bright to dark, as indices into modes::all(). This is the
    // circle-of-fifths ordering of the modes: each step flattens exactly one more degree, which is
    // what "brighter" and "darker" actually mean and why the axis is a line rather than a taste.
    // Lydian raises the 4th; Locrian has flattened everything it can.
    constexpr int kBrightnessOrder[] = { 1, 0, 2, 4, 3, 5, 6 };
    constexpr int kBrightnessCount = 7;
    const char* const kBrightnessNames[] = { "Lydian", "Major", "Mixolydian", "Dorian",
                                             "Minor", "Phrygian", "Locrian" };

    void styleLabel(juce::Label& l, const juce::String& text)
    {
        l.setText(text.toUpperCase(), juce::dontSendNotification);
        l.setFont(skin::micro(10.0f));
        l.setColour(juce::Label::textColourId, skin::textDim);
    }

    // The tray caption's two readings, written once. Both are set from two places - the
    // constructor and the 10 Hz tick - so as literals they were a wording change waiting to be
    // made in one of them and missed in the other.
    const char* const kTrayHint = "Audition - click to hear, drag onto a pad to keep, "
                                  "click a gap for a new chord";
    const char* const kTrayStale = "Audition - settings changed since these were generated. "
                                   "Regen for new ones.";
} // namespace

juce::Point<int> ChordGenPanel::contentSize()
{
    // The widest row wins the width, and the rows plus the gaps between them make the height.
    // Both then take the panel's margin on each side.
    const int rows =
        juce::jmax(juce::jmax(rowWidth({ kTitleW, kPageW, kCloseW }),
                              rowWidth({ kKeyW, kModeW, kBrightW, kMajMinW }) + 2 * kCheckW,
                              rowWidth({ kNotesW, kInvW, kOctaveW, kOctaveW }) + 3 * kCheckW,
                              rowWidth({ kComplianceW, kLockInfW, kSmoothW }) + kCheckW),
                   // Nested rather than one call: juce::jmax takes at most four arguments, and
                   // the Library band made this list five long.
                   juce::jmax(juce::jmax(8 * kSourceBtnW + 7 * kGap,
                                         rowWidth({ kChainW, kMoodW, kStartW, kTempW, kLengthW }),
                                         rowWidth({ kLibMoodW, kLibGenreW, kLibFuncW, kLibResultW })),
                              rowWidth({ kPlrW, kPlrW, kPlrW }),
                              kTrayMinW + kGap + rowWidth({ kFillW, kRegenW, kClearW })));
    // No action row of its own since 2026-08-01: the three buttons ride the tray's header, which
    // is the row that says they belong to it. The reference row is its own though, because what
    // is on it is a card rather than controls and a card wants the height.
    const int h = kHeaderH + kAfterHeader + kRowH + kAfterRowA
                  + kSourceRowH + kAfterSourceRow + SourceViz::preferredHeight() + kAfterViz
                  + 2 * (kFixedRowH + kAfterFixedRow)
                  + kRowH + kAfterRowB + kRefH + kAfterRef + kTrayH;
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

    // Two steppers rather than a drag target: a slider is a *drag*, and these are the click-only
    // path to every value between 2 and 11, the same reasoning the arp's rate steppers are built
    // on. IncDecButtons gives a pair of 34 px targets plus a readout for free.
    styleLabel(notesLabel, "Notes (min / max)");
    addAndMakeVisible(notesLabel);
    for (auto* sl : { &notesMinSlider, &notesMaxSlider })
    {
        sl->setSliderStyle(juce::Slider::IncDecButtons);
        sl->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 34, 26);
        sl->setRange(2, 11, 1);
        addAndMakeVisible(*sl);
    }
    notesMinSlider.setTitle("Fewest notes per chord");
    notesMinSlider.setTooltip("The fewest notes a generated chord may have. 2 gives dyads.");
    notesMaxSlider.setTitle("Most notes per chord");
    notesMaxSlider.setTooltip("The most a generated chord may have. Above 5 the stack keeps "
                              "climbing in thirds through the mode, so 11 covers every degree.");
    notesMinAtt = std::make_unique<SliderAtt>(processor.apvts, "genNotesMin", notesMinSlider);
    notesMaxAtt = std::make_unique<SliderAtt>(processor.apvts, "genNotesMax", notesMaxSlider);

    styleLabel(octaveMaxLabel, "to");
    addAndMakeVisible(octaveMaxLabel);
    octaveMaxSlider.setSliderStyle(juce::Slider::IncDecButtons);
    octaveMaxSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 34, 26);
    octaveMaxSlider.setRange(2, 6, 1);
    octaveMaxSlider.setTitle("Highest octave");
    octaveMaxSlider.setTooltip("The top of the register generated chords land in. Set it above "
                               "Octave and a page spreads across octaves instead of stacking up "
                               "in one.");
    addAndMakeVisible(octaveMaxSlider);
    octaveMaxAtt = std::make_unique<SliderAtt>(processor.apvts, "genOctaveMax", octaveMaxSlider);

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
    addAndMakeVisible(viz);
    // Seven buttons, in the parameter's own order. Never reorder: the choice index is what a
    // session stores, so moving an entry moves every saved session's source with it.
    {
        static const char* names[] = { "Algorithmic", "Markov", "Circle of 5ths", "Neo-Riemannian",
                                       "Progressions", "Negative", "Planing", "Library" };
        static const char* tips[] = {
            "Weighs a pool of candidate chords by degree and how far you let it stray from the key.",
            "Walks a table of moves taken from real progressions.",
            "Walks the circle of fifths from the key, taking each degree's quality from the mode.",
            "Moves one note at a time, keeping the common tones. Smooth and key-ambiguous.",
            "Transposes a real progression to your key: ii-V-I, the axis, 12-bar blues and more.",
            "Mirrors the key about the axis between tonic and dominant. C major becomes C minor.",
            "Takes one chord shape and slides it, through the scale or chromatically.",
            "Looks a named progression up by mood, genre and what it does. 355 of them."
        };
        for (int i = 0; i < (int) sourceButtons.size(); ++i)
        {
            auto& b = sourceButtons[(size_t) i];
            b.setButtonText(names[i]);
            b.setTitle(juce::String("Source: ") + names[i]);
            b.setTooltip(tips[i]);
            b.setClickingTogglesState(false); // refreshRadioStates owns the tick, not the click
            b.onClick = [this, i] { setSourceParam(i); };
            addAndMakeVisible(b);
        }
    }

    // Voice leading, in row A because it belongs to all seven sources rather than to any of them.
    // Brightness: a view onto genMode through kBrightnessOrder, not a parameter of its own.
    styleLabel(brightnessLabel, "Brightness (major / minor)");
    addAndMakeVisible(brightnessLabel);
    brightnessSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    brightnessSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 84, 26);
    brightnessSlider.setRange(0, kBrightnessCount - 1, 1);
    brightnessSlider.setTitle("Mode brightness");
    brightnessSlider.setTooltip("Slides through the seven modes from brightest to darkest: "
                               "Lydian, Major, Mixolydian, Dorian, Minor, Phrygian, Locrian. "
                               "Major and minor are two points on it, so you can slide past "
                               "either into the modes between. It sets Mode.");
    brightnessSlider.textFromValueFunction = [](double v)
    {
        const int i = juce::jlimit(0, kBrightnessCount - 1, (int) v);
        return juce::String(kBrightnessNames[i]);
    };
    brightnessSlider.onValueChange = [this] { setModeFromBrightness((int) brightnessSlider.getValue()); };
    addAndMakeVisible(brightnessSlider);

    // Major / Minor: leans the thirds without touching the mode.
    styleLabel(majMinLabel, "Lean");
    addAndMakeVisible(majMinLabel);
    majMinSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    majMinSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 26);
    majMinSlider.setRange(-100, 100, 1);
    majMinSlider.setTitle("Major / minor lean");
    majMinSlider.setTooltip("Leans generated chords major (right) or minor (left) by moving their "
                            "thirds, whatever mode you are in. The size of the lean is how often "
                            "a chord gets pushed, so it colours a page without flattening it. "
                            "Centre leaves every chord as its source made it.");
    addAndMakeVisible(majMinSlider);
    majMinAtt = std::make_unique<SliderAtt>(processor.apvts, "genMajMin", majMinSlider);

    // The six tick boxes, in the order their settings appear.
    {
        static const char* ids[] = { "genUseKey", "genUseMode", "genUseOctave",
                                     "genUseNotes", "genUseInversions", "genUseCompliance" };
        static const char* what[] = { "Key", "Mode", "Octave", "note count", "inversions",
                                      "scale compliance" };
        for (int i = 0; i < (int) useBoxes.size(); ++i)
        {
            useBoxes[(size_t) i].setTitle(juce::String("Constrain ") + what[i]);
            useBoxes[(size_t) i].setTooltip(juce::String("Ticked, generation obeys the ") + what[i]
                                            + " beside this box. Unticked, the generator picks it "
                                              "freely each time it generates.");
            addAndMakeVisible(useBoxes[(size_t) i]);
            useAtts[(size_t) i] = std::make_unique<ButtonAtt>(processor.apvts, ids[i],
                                                              useBoxes[(size_t) i]);
        }
    }

    // "Voice Leading" until 2026-08-01, when Owen said "I don't understand what the voice
    // reading does". The name was the problem: it is jargon for a thing with a plain description,
    // and the tooltip below is now that description rather than a restatement of the label.
    styleLabel(smoothLabel, "Smooth Voicing");
    addAndMakeVisible(smoothLabel);
    smoothSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    smoothSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 26);
    smoothSlider.setRange(0, 100, 1);
    smoothSlider.setTextValueSuffix(" %");
    smoothSlider.setTooltip("Keeps consecutive chords close together on the keyboard. C-E-G then "
                            "F-A-C becomes C-E-G then C-F-A: the same two chords, with less "
                            "jumping between them. It never changes which chords you get or which "
                            "notes they contain, only which octave each note sits in. 0 leaves "
                            "every chord in root position.");
    addAndMakeVisible(smoothSlider);
    smoothAtt = std::make_unique<SliderAtt>(processor.apvts, "genSmooth", smoothSlider);

    // --- the five bands added on 2026-08-01, one per new source -------------------------------
    styleLabel(circleDirLabel, "Direction");
    {
        static const char* names[] = { "Flat-ward", "Sharp-ward" };
        static const char* tips[] = { "Down a fifth each step. The falling fifth most progressions "
                                      "are built on.",
                                      "Up a fifth each step." };
        for (int i = 0; i < (int) circleDirButtons.size(); ++i)
        {
            auto& b = circleDirButtons[(size_t) i];
            b.setButtonText(names[i]);
            b.setTitle(juce::String("Circle direction: ") + names[i]);
            b.setTooltip(tips[i]);
            b.setClickingTogglesState(false);
            b.onClick = [this, i] { setCircleDirParam(i); };
        }
    }

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

    // ---- The Library band -------------------------------------------------------------------
    //
    // Three filters, each with "Any" as item 1 so the picker's first entry always means "do not
    // narrow on this axis" - the same convention Markov's Mood and Start already use, and the
    // convention `chordlib::find` is written against.
    //
    // The two word axes offer **`moodsInUse` / `genresInUse` rather than the full vocabulary**,
    // which is the difference between a picker that works and one that lies: the table grows
    // unevenly by design, so a mood with no rows behind it is an item that can only ever
    // disappoint, and there is nothing on screen that could explain the empty result.
    styleLabel(libMoodLabel, "Mood");
    libMoodBox.addItem("Any", 1);
    {
        int id = 2;
        for (const auto& m : chordlib::moodsInUse())
            libMoodBox.addItem(m, id++);
    }
    libMoodBox.setSelectedId(1, juce::dontSendNotification);
    libMoodBox.setTitle("Library mood");
    libMoodBox.setTooltip("How it should feel. Any leaves the mood open.");
    libMoodBox.onChange = [this] {
        gen.setLibraryMood(libMoodBox.getSelectedId() <= 1 ? juce::String() : libMoodBox.getText());
        refreshLibraryResult();
    };

    styleLabel(libGenreLabel, "Genre");
    libGenreBox.addItem("Any", 1);
    {
        int id = 2;
        for (const auto& g : chordlib::genresInUse())
            libGenreBox.addItem(g, id++);
    }
    libGenreBox.setSelectedId(1, juce::dontSendNotification);
    libGenreBox.setTitle("Library genre");
    libGenreBox.setTooltip("What it should sound like. Any leaves the genre open.");
    libGenreBox.onChange = [this] {
        gen.setLibraryGenre(libGenreBox.getSelectedId() <= 1 ? juce::String() : libGenreBox.getText());
        refreshLibraryResult();
    };

    // Function is the axis Scaler does not have and the one that turns browsing into composing:
    // "sad" is forty candidates, where "sad and it loops" and "sad and it ends" are two different
    // requests. The tooltip carries each entry's own blurb, since eight one-word names are not
    // self-explanatory and there is no room for a caption per item.
    styleLabel(libFunctionLabel, "Does what");
    libFunctionBox.addItem("Any", 1);
    for (int f = 0; f < (int) chordlib::Function::count; ++f)
        libFunctionBox.addItem(chordlib::functionName((chordlib::Function) f), f + 2);
    libFunctionBox.setSelectedId(1, juce::dontSendNotification);
    libFunctionBox.setTitle("Library function");
    {
        juce::String tip = "What the progression does.";
        for (int f = 0; f < (int) chordlib::Function::count; ++f)
            tip << "\n" << chordlib::functionName((chordlib::Function) f) << ": "
                << chordlib::functionBlurb((chordlib::Function) f);
        libFunctionBox.setTooltip(tip);
    }
    libFunctionBox.onChange = [this] {
        gen.setLibraryFunction(libFunctionBox.getSelectedId() <= 1 ? -1
                                                                   : libFunctionBox.getSelectedId() - 2);
        refreshLibraryResult();
    };

    styleLabel(libResultLabel, "");
    libResultLabel.setTitle("Library match");

    // Adopt whatever the brain already holds: the window is built and destroyed every time it
    // opens, and the three picks outlive it on purpose. Without this the boxes would come back
    // reading "Any" while generation still filtered on your last pick, which is the exact failure
    // Markov's Mood and Start were given this shape to avoid.
    for (int i = 0; i < libMoodBox.getNumItems(); ++i)
        if (libMoodBox.getItemText(i) == gen.libraryMood())
            libMoodBox.setSelectedId(libMoodBox.getItemId(i), juce::dontSendNotification);
    for (int i = 0; i < libGenreBox.getNumItems(); ++i)
        if (libGenreBox.getItemText(i) == gen.libraryGenre())
            libGenreBox.setSelectedId(libGenreBox.getItemId(i), juce::dontSendNotification);
    if (gen.libraryFunction() >= 0)
        libFunctionBox.setSelectedId(gen.libraryFunction() + 2, juce::dontSendNotification);
    refreshLibraryResult();


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
    for (int src = 1; src < (int) sourceButtons.size(); ++src)
        for (auto* c : bandFor(src))
            addChildComponent(*c);
    refreshMoodItems();

    // The audition tray, and the one line that says what a card in it does. The label is worth
    // its 20 px: a drag is the only way a candidate reaches a pad, and a gesture nothing on
    // screen mentions is a gesture nobody finds.
    styleLabel(trayLabel, kTrayHint);
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

    // The tray's drag needs no wiring at all any more (2026-08-02). It goes out through this
    // class as a `DragAndDropContainer` and lands on whichever target is under the cursor - the
    // reference card beside it, or a pad in the other window - so the ordering this used to have
    // to arrange by hand ("offer the reference first, because that target is inside this window")
    // is now just which component the point is over, and it is right about a window sitting on
    // top of another where a bounds test was not. Only the *menu* item below still needs a
    // pass-through, because a menu item has no target to hit.
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
// The three functions that stand in for the attachment a row of buttons on one choice parameter
// cannot have. Writing goes through the parameter object rather than the raw atomic, so the host
// and every other view of `genSource` (the Pads bar has none today, but the rule is the rule)
// hear about it exactly as they would from a combo box.
void ChordGenPanel::setSourceParam(int index)
{
    if (auto* p = processor.apvts.getParameter("genSource"))
        p->setValueNotifyingHost(p->convertTo0to1((float) index));
}

void ChordGenPanel::setCircleDirParam(int index)
{
    if (auto* p = processor.apvts.getParameter("genCircleDir"))
        p->setValueNotifyingHost(p->convertTo0to1((float) index));
}

void ChordGenPanel::refreshRadioStates()
{
    // Polled, not set on click, so a source arriving from the host or a session load lights the
    // right button too. `setClickingTogglesState(false)` on every one of these is what makes the
    // toggle state purely a readout.
    const int src = gen.sourceIndex();
    for (int i = 0; i < (int) sourceButtons.size(); ++i)
        sourceButtons[(size_t) i].setToggleState(i == src, juce::dontSendNotification);

    const int dir = processor.apvts.getRawParameterValue("genCircleDir")->load() > 0.5f ? 1 : 0;
    for (int i = 0; i < (int) circleDirButtons.size(); ++i)
        circleDirButtons[(size_t) i].setToggleState(i == dir, juce::dontSendNotification);
}

// Brightness writes genMode and reads it back, the same shape the source buttons use and for the
// same reason: JUCE has no attachment for "a control whose value is a permutation of a
// parameter's". Both halves live here so the mapping exists in exactly one place.
void ChordGenPanel::setModeFromBrightness(int position)
{
    const int p = juce::jlimit(0, kBrightnessCount - 1, position);
    if (auto* param = processor.apvts.getParameter("genMode"))
        param->setValueNotifyingHost(param->convertTo0to1((float) kBrightnessOrder[p]));
}

void ChordGenPanel::refreshBrightness()
{
    const int mode = (int) processor.apvts.getRawParameterValue("genMode")->load();
    int pos = -1;
    for (int i = 0; i < kBrightnessCount; ++i)
        if (kBrightnessOrder[i] == mode)
            pos = i;

    // Off the axis entirely (harmonic minor, blues, a pentatonic). The slider greys and keeps
    // whatever it last showed rather than snapping to an end, because either end would be a lie
    // about where you are. Moving it is still how you get back onto the axis.
    const bool onAxis = pos >= 0;
    brightnessSlider.setEnabled(onAxis);
    brightnessLabel.setEnabled(onAxis);
    if (onAxis && pos != lastBrightnessShown)
    {
        lastBrightnessShown = pos;
        brightnessSlider.setValue(pos, juce::dontSendNotification);
    }
}

std::vector<juce::Component*> ChordGenPanel::bandFor(int source)
{
    switch (source)
    {
        case 1: // Markov
            return { &chainLabel, &chainBox, &moodLabel, &moodBox, &startLabel,
                     &startBox, &tempLabel, &tempSlider, &lengthLabel, &lengthSlider };
        case 2: // Circle of Fifths
            return { &circleDirLabel, &circleDirButtons[0], &circleDirButtons[1] };
        case 3: // Neo-Riemannian
            return { &plrPLabel, &plrPSlider, &plrLLabel, &plrLSlider, &plrRLabel, &plrRSlider };
        case 4: // Progressions
            return { &progressionLabel, &progressionBox };
        case 7: // Library
            return { &libMoodLabel, &libMoodBox, &libGenreLabel, &libGenreBox,
                     &libFunctionLabel, &libFunctionBox, &libResultLabel };
        case 5: // Negative Harmony: Key, Mode and Octave in row A are the whole of it
            return {};
        case 6: // Planing
            return { &planingLabel, &planingDiatonicButton };
        default: // Algorithmic
            // Scale Compliance is NOT in here any more (Owen, 2026-08-01: "we should have a scale
            // slider like we had before"). It moved to the always-on row above, where every
            // source can see it, and it greys under the ones that do not read it rather than
            // vanishing. Lock Influence went with it: the two are read together and splitting
            // them across a hiding band and a fixed row would have been the confusing half.
            // Nothing left that is the weighted pool's alone: Compliance and Lock Influence
            // went to the fixed row on 2026-08-01, and Notes and Inversions followed them there
            // when they became post-passes every source honours. Algorithmic has no band of its
            // own any more, which is honest rather than a gap.
            return {};
    }
}

std::vector<juce::Component*> ChordGenPanel::allBandControls()
{
    // Every band's controls, so applySource can hide the lot before showing one and never has to
    // know which band it is hiding. Built from bandFor rather than listed again, because a second
    // list of the same components is a list that goes stale the first time a band gains a control.
    std::vector<juce::Component*> all;
    for (int s = 0; s < (int) sourceButtons.size(); ++s)
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
    // greys rather than hides where it is dead (Octavium left it clickable and silently ignored
    // it; greying is honest), and that is **Markov alone**: every other source reads the mode,
    // whether to pick a landing degree's quality, a starting triad, or the scale it slides
    // through. Greying it for all six non-Algorithmic sources was a bug for a few minutes on
    // 2026-08-01, from asking `readsScaleSettings` a question about Mode that it does not answer.
    const bool modeAware = gen.readsMode();
    modeBox.setEnabled(modeAware);
    modeLabel.setEnabled(modeAware);
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

void ChordGenPanel::refreshLibraryResult()
{
    const int n = (int) chordlib::find(gen.libraryMood(), gen.libraryGenre(),
                                       gen.libraryFunction()).size();

    // Zero matches is the one state worth spelling out. The two word pickers only ever offer tags
    // with rows behind them, so a narrow filter can still come back empty on a *combination*
    // nobody has written - "Funky" and "Classical" - and generation quietly falls back to the
    // whole table there. Saying so is the difference between a fallback and a bug: without this
    // line, Fill would hand you something that ignored both your picks and never mention it.
    juce::String text;
    if (n == 0)
        text = "no match - any progression";
    else
        text = juce::String(n) + (n == 1 ? " progression" : " progressions");

    if (gen.lastLibraryEntry().isNotEmpty())
        text << "   |   " << gen.lastLibraryEntry();

    libResultLabel.setText(text, juce::dontSendNotification);
}

// The reverse crossing - a pad dragged out of the main window and offered to the reference - has
// no entry point here at all now. ChordRefCard is a `DragAndDropTarget` and JUCE delivers to it
// directly, highlight and drop and the exit that puts the highlight back out, so the three
// screen-coordinate methods that used to live here went with the editor's plumbing that called
// them (2026-08-02).

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

    // The Library readout carries the name of the row the last generation landed on, and
    // generation happens outside this window (Fill and Regen are on the tray's header, and the
    // Pads bar has its own pair). Polling is what catches that; the three onChange handlers cover
    // the half that starts here.
    if (shownSource == 7 && gen.lastLibraryEntry() != lastLibraryEntry)
    {
        lastLibraryEntry = gen.lastLibraryEntry();
        refreshLibraryResult();
    }

    // Source switch: swap which row-B band is on screen. The source is a parameter, so it can
    // move from this window's own buttons, from the host or from a session load, and this poll is
    // what catches all three.
    if (const int src = gen.sourceIndex(); src != shownSource)
        applySource(src);
    refreshRadioStates();
    refreshBrightness();

    // Keep the diagram pointed at what is actually happening. Each setter no-ops when nothing
    // changed, so this costs a few comparisons per tick rather than a repaint.
    viz.setSource(gen.sourceIndex());
    viz.setKey((int) processor.apvts.getRawParameterValue("genRoot")->load(),
               (int) processor.apvts.getRawParameterValue("genMode")->load());
    viz.setChords(tray.candidates());
    viz.setLibraryEntry(gen.lastLibraryEntry());

    // Say the tray is out of date; never act on it. Changing a setting generates nothing (Owen,
    // 2026-08-01), so this is the whole of what a settings change does to the tray: the caption
    // tells you Regen would now give you something different.
    trayLabel.setText(juce::String(tray.settingsMovedSinceFill() ? kTrayStale : kTrayHint)
                          .toUpperCase(),
                      juce::dontSendNotification);

    // Compliance and Lock Influence are on the fixed row now, so they grey where they are dead
    // rather than leaving the row. Only the weighted pool reads either.
    const bool scaleAware = gen.readsScaleSettings();
    const std::array<juce::Component*, 4> fixedRow { &complianceLabel, &complianceSlider,
                                                     &lockInfluenceLabel, &lockInfluenceSlider };
    for (auto* c : fixedRow)
        c->setEnabled(scaleAware);
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
    // A cell with its tick box: the box takes 34 px of full height at the left, then the caption
    // and control fill what is left. Only the six settings where "free" differs from "zero" have
    // one, which is why `cell` still exists beside this.
    const auto checkCell = [&cell](juce::Rectangle<int>& row, int w, juce::ToggleButton& box,
                                   juce::Label& lab, juce::Component& ctl)
    {
        box.setBounds(row.removeFromLeft(kCheckW));
        cell(row, w, lab, ctl);
    };

    auto rowA = area.removeFromTop(kRowH);
    checkCell(rowA, kKeyW, useBoxes[0], rootLabel, rootBox);
    checkCell(rowA, kModeW, useBoxes[1], modeLabel, modeBox);
    cell(rowA, kBrightW, brightnessLabel, brightnessSlider);
    cell(rowA, kMajMinW, majMinLabel, majMinSlider);
    area.removeFromTop(kAfterRowA);

    // The source row. Eight buttons, equal width, filling whatever the window is: at the layout's
    // floor they are kSourceBtnW each, and a wider window spreads them rather than leaving a gap,
    // because a bigger target costs nothing and this is the row you reach for most.
    {
        auto row = area.removeFromTop(kSourceRowH);
        sourceLabel.setBounds(row.removeFromTop(14));
        const int n = (int) sourceButtons.size();
        const int w = (row.getWidth() - kGap * (n - 1)) / n;
        for (int i = 0; i < n; ++i)
        {
            sourceButtons[(size_t) i].setBounds(row.removeFromLeft(w));
            row.removeFromLeft(kGap);
        }
    }
    area.removeFromTop(kAfterSourceRow);

    // The diagram, directly under the buttons that choose what it draws.
    viz.setBounds(area.removeFromTop(SourceViz::preferredHeight()));
    area.removeFromTop(kAfterViz);

    // The fixed row: what every source can see. These used to be the tail of the algorithmic band
    // and went off screen under the other six, which is what Owen was asking for back on
    // 2026-08-01 ("we should have a scale slider like we had before").
    {
        auto row = area.removeFromTop(kFixedRowH);
        // Notes first: it is the one most often reached for, and it is the one that changed shape.
        useBoxes[3].setBounds(row.removeFromLeft(kCheckW));
        {
            auto c = row.removeFromLeft(kNotesW);
            row.removeFromLeft(kGap);
            notesLabel.setBounds(c.removeFromTop(14));
            const int w = (c.getWidth() - kGap) / 2;
            notesMinSlider.setBounds(c.removeFromLeft(w));
            c.removeFromLeft(kGap);
            notesMaxSlider.setBounds(c.removeFromLeft(w));
        }
        useBoxes[4].setBounds(row.removeFromLeft(kCheckW));
        {
            auto c = row.removeFromLeft(kInvW);
            row.removeFromLeft(kGap);
            invLabel.setBounds(c.removeFromTop(14));
            const int w = c.getWidth() / 4;
            inv0Button.setBounds(c.removeFromLeft(w));
            inv1Button.setBounds(c.removeFromLeft(w));
            inv2Button.setBounds(c.removeFromLeft(w));
            inv3Button.setBounds(c);
        }
        checkCell(row, kOctaveW, useBoxes[2], octaveLabel, octaveSlider);
        cell(row, kOctaveW, octaveMaxLabel, octaveMaxSlider);
    }
    area.removeFromTop(kAfterFixedRow);

    // A second fixed row for the three dials whose own zero already means "off", which is exactly
    // why none of them carries a tick box: a box beside Smooth Voicing would be a second control
    // for what 0 % already says.
    {
        auto row = area.removeFromTop(kFixedRowH);
        checkCell(row, kComplianceW, useBoxes[5], complianceLabel, complianceSlider);
        cell(row, kLockInfW, lockInfluenceLabel, lockInfluenceSlider);
        cell(row, kSmoothW, smoothLabel, smoothSlider);
    }
    area.removeFromTop(kAfterFixedRow);

    // Row B: note counts, inversions and the two weighting sliders - or, when the Markov source
    // is up, its chain controls in the same band. Both are laid out here and applySource() has
    // already hidden one of them, which it must: these two rects overlap and the algorithmic
    // set is the wider, so a hidden-but-visible one paints out past the other's right edge.
    // The band row collapses when the source has no band, which since 2026-08-01 is Algorithmic
    // (everything that was its own moved to the fixed rows) and Negative Harmony (a reflection
    // needs only Key, Mode and Octave). Leaving a 44 px hole under those two read as a missing
    // control rather than as an absent one. The height goes to the tray, which takes whatever is
    // left, so the window does not resize under you when you switch source.
    auto rowB = area.removeFromTop(bandFor(shownSource).empty() ? 0 : kRowH);
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
    // The four bands added on 2026-08-01, each laid into that same rect from the left. Every one
    // of them is invisible unless its source is up, which applySource guarantees, so overlapping
    // rects here are correct rather than a bug waiting to happen. Negative Harmony is absent on
    // purpose: it has no band, so there is nothing to place.
    {
        auto row = rowFull;
        auto c = row.removeFromLeft(kCircleDirW);
        circleDirLabel.setBounds(c.removeFromTop(14));
        const int w = (c.getWidth() - kGap) / 2;
        circleDirButtons[0].setBounds(c.removeFromLeft(w));
        c.removeFromLeft(kGap);
        circleDirButtons[1].setBounds(c.removeFromLeft(w));
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
    {
        // The Library band. The readout takes whatever is left rather than a fixed cell, so a
        // wider window gives it the room - it is the one thing here whose content varies in length
        // ("271 progressions" against "Andalusian cadence (i-bVII-bVI-V)"), and the three pickers
        // beside it are reserved first, which is the standing rule.
        auto row = rowFull;
        cell(row, kLibMoodW, libMoodLabel, libMoodBox);
        cell(row, kLibGenreW, libGenreLabel, libGenreBox);
        cell(row, kLibFuncW, libFunctionLabel, libFunctionBox);
        row.removeFromTop(14); // no caption over the readout: it is a sentence, not a value
        libResultLabel.setBounds(row);
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
