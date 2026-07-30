#include "ChancePanel.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>

namespace keys
{
namespace
{
    // Micro-caps captions in the skin's voice, ArpPanel's own helper duplicated here rather
    // than shared: each panel file keeps its own copy (see ArpPanel.cpp and PluginEditor.cpp).
    void styleLabel(juce::Label& l, const juce::String& text)
    {
        l.setText(text.toUpperCase(), juce::dontSendNotification);
        l.setFont(skin::micro(10.0f));
        l.setColour(juce::Label::textColourId, skin::textDim);
    }

    constexpr int tModeGroupId = 9001;
    constexpr int xModeGroupId = 9002;

    // Heights of the two rows of groups, kept next to the resized() that spends them
    // (ArpPanel's own comment applies here too). Row A (Rhythm/Pitch/Key) is one knob-height
    // row; Row B (Mode/Phrase) is two 34 px button rows plus the gap between them. Chance has
    // no shape that changes its control count, so unlike ArpPanel there is only ever this one
    // layout and preferredHeight() is a constant.
    constexpr int chMargin = 12;
    constexpr int chBandTop = 18;   // caption rule + its clearance
    constexpr int chRowAInner = 80; // a 12 px label + a 68 px rotary/textbox or linear slider
    constexpr int chRowBInner = 76; // two 34 px button rows + an 8 px gap
    constexpr int chBandGap = 12;
    constexpr int chBandAH = chBandTop + chRowAInner + 6;
    constexpr int chBandBH = chBandTop + chRowBInner + 6;
    constexpr int chPanelH = chMargin * 2 + chBandAH + chBandGap + chBandBH;

    constexpr int rowAWeights[3] = { 38, 34, 28 }; // Rhythm, Pitch, Key
    constexpr int rowBWeights[2] = { 55, 45 };     // Mode, Phrase
} // namespace

ChancePanel::ChancePanel(KeysProcessor& p) : processor(p)
{
    okstudio::ui::makeMouseOnly(*this);
    buildControls();
    refreshModeButtons();
    refreshPhraseButtons();
    startTimerHz(10); // matches ArpPanel's refresh rate
}

ChancePanel::~ChancePanel()
{
    stopTimer();
}

void ChancePanel::buildControls()
{
    // No title, On or Close here: the Chance section bar above the panel carries all three
    // (see KeysEditor), the same arrangement ArpPanel uses.

    // --- Rhythm: the t generator -----------------------------------------------------
    const auto knob = [this](juce::Slider& s, juce::Label& lab, const juce::String& text,
                             double lo, double hi, double step, const juce::String& tip)
    {
        styleLabel(lab, text);
        addAndMakeVisible(lab);
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 16);
        s.setRange(lo, hi, step);
        s.setTooltip(tip);
        addAndMakeVisible(s);
    };

    knob(densitySlider, densityLabel, "Density", 0.0, 100.0, 1.0, "How many steps fire. Higher is denser.");
    densityAtt = std::make_unique<SliderAtt>(processor.apvts, "chanceDensity", densitySlider);

    // Deja Vu: the loop lock. 50 is a frozen loop that repeats exactly; either direction
    // disturbs it. There is no double-click in this plugin, so there is no
    // setDoubleClickReturnValue reset gesture to find that centre - the value readout is the
    // only way, which is why TextBoxBelow stays on rather than being hidden.
    knob(dejaVuSlider, dejaVuLabel, "Deja Vu", 0.0, 100.0, 1.0,
         "50 is a frozen loop that repeats exactly. Down writes new material in; up reorders "
         "what is already playing.");
    dejaVuAtt = std::make_unique<SliderAtt>(processor.apvts, "chanceDejaVu", dejaVuSlider);

    knob(jitterSlider, jitterLabel, "Jitter", 0.0, 100.0, 1.0,
         "Timing feel: nudges each hit off the grid and pulls it back, like a player lagging "
         "and catching up.");
    jitterAtt = std::make_unique<SliderAtt>(processor.apvts, "chanceJitter", jitterSlider);

    styleLabel(loopLenLabel, "Length");
    addAndMakeVisible(loopLenLabel);
    loopLenBox.addItemList({ "1", "2", "3", "4", "6", "8", "12", "16" }, 1);
    loopLenBox.setTitle("Length"); // accessible name; combos otherwise expose none
    loopLenBox.setTooltip("How many steps back the Deja Vu loop reaches.");
    addAndMakeVisible(loopLenBox);
    loopLenAtt = std::make_unique<ComboAtt>(processor.apvts, "chanceLoopLen", loopLenBox);

    // Steppers beside the combo, ArpPanel's Rate idiom exactly: one click each, no travel
    // off the panel to walk through the lengths.
    for (auto* b : { &loopLenPrev, &loopLenNext })
        addAndMakeVisible(*b);
    loopLenPrev.onClick = [this] { stepCombo(loopLenBox, -1); };
    loopLenNext.onClick = [this] { stepCombo(loopLenBox, 1); };
    loopLenPrev.setTitle("Shorter loop");
    loopLenNext.setTitle("Longer loop");

    // --- Pitch: the X generator -------------------------------------------------------
    knob(spreadSlider, spreadLabel, "Spread", 0.0, 100.0, 1.0, "How wide a register the draw covers.");
    spreadAtt = std::make_unique<SliderAtt>(processor.apvts, "chanceSpread", spreadSlider);

    knob(biasSlider, biasLabel, "Bias", -100.0, 100.0, 1.0, "Where the draw centres: low register to high.");
    biasAtt = std::make_unique<SliderAtt>(processor.apvts, "chanceBias", biasSlider);

    knob(tempSlider, tempLabel, "Temp", 0.0, 100.0, 1.0,
         "0 always picks the strongest candidate; 100 is uniform and every weighting is erased.");
    tempAtt = std::make_unique<SliderAtt>(processor.apvts, "chanceTemp", tempSlider);

    knob(wanderSlider, wanderLabel, "Wander", 0.0, 100.0, 1.0,
         "A 1/f melodic walk: correlated over a short span without drifting off.");
    wanderAtt = std::make_unique<SliderAtt>(processor.apvts, "chanceWander", wanderSlider);

    // --- Key: the two adherence axes, as horizontal sliders rather than knobs ---------
    const auto linSlider = [this](juce::Slider& s, juce::Label& lab, const juce::String& text,
                                  double lo, double hi, double step, const juce::String& tip)
    {
        styleLabel(lab, text);
        addAndMakeVisible(lab);
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 20);
        s.setRange(lo, hi, step);
        s.setTooltip(tip);
        addAndMakeVisible(s);
    };
    linSlider(keySlider, keyLabel, "Key", 0.0, 100.0, 1.0,
             "Collapses the pool toward the key as you turn it up: root and fifth at the top.");
    keyAtt = std::make_unique<SliderAtt>(processor.apvts, "chanceKey", keySlider);

    linSlider(chordPullSlider, chordPullLabel, "Chord Pull", 0.0, 100.0, 1.0,
             "How strongly the draw favours the chord you are holding over other in-key tones.");
    chordPullAtt = std::make_unique<SliderAtt>(processor.apvts, "chanceChord", chordPullSlider);

    // --- Mode: two independent tri-state radio rows -----------------------------------
    // Plain juce::TextButtons in a radio group, not combo boxes: a visible set beats a
    // dropdown for three states under a mouse-only contract. Each row writes its choice
    // parameter directly (setTMode/setXMode) rather than through an attachment.
    styleLabel(rhythmModeLabel, "Rhythm");
    styleLabel(voiceModeLabel, "Voice");
    addAndMakeVisible(rhythmModeLabel);
    addAndMakeVisible(voiceModeLabel);

    for (auto* b : { &tCoinButton, &tEuclidButton, &tBurstsButton })
    {
        b->setClickingTogglesState(true);
        b->setRadioGroupId(tModeGroupId);
        addAndMakeVisible(*b);
    }
    tCoinButton.setTooltip("Bernoulli per step: scattered, deliberately ungridded.");
    tEuclidButton.setTooltip("Euclidean rhythm: even and repeatable at every density.");
    tBurstsButton.setTooltip("Euclidean onsets that may ratchet into 2-4 sub-hits.");
    tCoinButton.onClick = [this] { setTMode(0); };
    tEuclidButton.onClick = [this] { setTMode(1); };
    tBurstsButton.onClick = [this] { setTMode(2); };

    for (auto* b : { &xLineButton, &xDuetButton, &xClusterButton })
    {
        b->setClickingTogglesState(true);
        b->setRadioGroupId(xModeGroupId);
        addAndMakeVisible(*b);
    }
    xLineButton.setTooltip("One note at a time.");
    xDuetButton.setTooltip("Two notes, mirrored about Bias.");
    xClusterButton.setTooltip("A chord burst of up to three notes.");
    xLineButton.onClick = [this] { setXMode(0); };
    xDuetButton.onClick = [this] { setXMode(1); };
    xClusterButton.onClick = [this] { setXMode(2); };

    // --- Phrase: the three actions -----------------------------------------------------
    generateButton.onClick = [this] { processor.regenerateChance(); };
    generateButton.setTooltip("A new phrase: everything Chance does is a pure function of its "
                              "seed, so this picks a new one.");
    addAndMakeVisible(generateButton);

    addAndMakeVisible(learnButton);
    learnAtt = std::make_unique<ButtonAtt>(processor.apvts, "chanceLearn", learnButton);
    learnButton.setTooltip("Arm the histogram: play a few bars and it becomes the weights Key "
                           "quantizes against and Temperature samples from.");

    // Freeze's label names its destination and it disables itself rather than lying about
    // what a click would do; refreshPhraseButtons() (construction + the timer) keeps both
    // current.
    freezeButton.onClick = [this] { processor.freezeChanceToSlot(processor.arpActivePattern()); };
    freezeButton.setTooltip("Capture what Chance just played into the active arp slot, as an "
                            "ordinary editable pattern.");
    addAndMakeVisible(freezeButton);
}

void ChancePanel::stepCombo(juce::ComboBox& box, int delta)
{
    // Clamp rather than wrap, ArpPanel's stepCombo exactly: wrapping at the ends would mean
    // one click too many jumps from one end of the list to the other.
    const int n = box.getNumItems();
    if (n <= 0)
        return;
    const int next = juce::jlimit(0, n - 1, box.getSelectedItemIndex() + delta);
    if (next != box.getSelectedItemIndex())
        box.setSelectedItemIndex(next); // notifies, so the attachment runs
}

void ChancePanel::setTMode(int index)
{
    // Gestures by hand, ArpPanel's applyShapeChoice pattern: the buttons are not an APVTS
    // attachment (a radio row of TextButtons is not one of the attachment types), so the
    // begin/end bracket has to be written explicitly or a host in touch or latch never arms.
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*>(processor.apvts.getParameter("chanceTMode")))
    {
        param->beginChangeGesture();
        *param = index;
        param->endChangeGesture();
    }
}

void ChancePanel::setXMode(int index)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*>(processor.apvts.getParameter("chanceXMode")))
    {
        param->beginChangeGesture();
        *param = index;
        param->endChangeGesture();
    }
}

void ChancePanel::refreshModeButtons()
{
    // Parameters are the truth (a host can automate either mode out from under the buttons),
    // so which button is lit is always derived, never assumed.
    const int t = (int) processor.apvts.getRawParameterValue("chanceTMode")->load();
    tCoinButton.setToggleState(t == 0, juce::dontSendNotification);
    tEuclidButton.setToggleState(t == 1, juce::dontSendNotification);
    tBurstsButton.setToggleState(t == 2, juce::dontSendNotification);

    const int x = (int) processor.apvts.getRawParameterValue("chanceXMode")->load();
    xLineButton.setToggleState(x == 0, juce::dontSendNotification);
    xDuetButton.setToggleState(x == 1, juce::dontSendNotification);
    xClusterButton.setToggleState(x == 2, juce::dontSendNotification);
}

void ChancePanel::refreshPhraseButtons()
{
    freezeButton.setButtonText("Freeze to " + juce::String(processor.arpActivePattern() + 1));
    freezeButton.setEnabled(processor.chanceHasPhrase());
}

void ChancePanel::timerCallback()
{
    refreshModeButtons();
    refreshPhraseButtons();
}

int ChancePanel::preferredHeight() const
{
    return chPanelH;
}

void ChancePanel::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff1c1f24));
    g.fillRoundedRectangle(b, skin::panelRadius);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.fillRoundedRectangle(b.withHeight(1.5f).reduced(skin::panelRadius, 0.0f), 0.75f);
    skin::glowRect(g, b, skin::panelRadius, skin::accentOf(*this).base, 0.30f);

    // The group boxes: a hairline frame with a gap punched through the top rule for the
    // caption. ArpPanel's idiom exactly (see its paint() for the longer version of this
    // comment) - drawn rather than built from components, since a caption and a rule are
    // four lines of Graphics per group.
    g.setFont(skin::micro(9.5f).withExtraKerningFactor(0.16f));
    for (const auto& grp : groups)
    {
        if (grp.bounds.isEmpty())
            continue;
        const auto r = grp.bounds.toFloat();
        const auto caption = grp.caption.toUpperCase();
        const auto textW = juce::GlyphArrangement::getStringWidth(
            skin::micro(9.5f).withExtraKerningFactor(0.16f), caption);
        const float capY = r.getY() + 5.0f;

        g.setColour(juce::Colours::white.withAlpha(0.07f));
        juce::Path frame;
        frame.startNewSubPath(r.getCentreX() - textW * 0.5f - 8.0f, capY);
        frame.lineTo(r.getX() + skin::radius, capY);
        frame.quadraticTo(r.getX(), capY, r.getX(), capY + skin::radius);
        frame.lineTo(r.getX(), r.getBottom() - skin::radius);
        frame.quadraticTo(r.getX(), r.getBottom(), r.getX() + skin::radius, r.getBottom());
        frame.lineTo(r.getRight() - skin::radius, r.getBottom());
        frame.quadraticTo(r.getRight(), r.getBottom(), r.getRight(), r.getBottom() - skin::radius);
        frame.lineTo(r.getRight(), capY + skin::radius);
        frame.quadraticTo(r.getRight(), capY, r.getRight() - skin::radius, capY);
        frame.lineTo(r.getCentreX() + textW * 0.5f + 8.0f, capY);
        g.strokePath(frame, juce::PathStrokeType(1.0f));

        g.setColour(skin::textDim);
        g.drawText(caption, grp.bounds.withHeight(12).withY((int) capY - 6),
                   juce::Justification::centred);
    }
}

void ChancePanel::resized()
{
    auto area = getLocalBounds().reduced(chMargin);

    // No title row: the Chance section bar carries the name, On and Detach.

    // --- Row A: the continuous controls, three groups sharing the width --------------
    auto rowA = area.removeFromTop(chBandAH);
    area.removeFromTop(chBandGap);
    {
        const int gaps = 2 * 10;
        const int usable = rowA.getWidth() - gaps;
        const int total = rowAWeights[0] + rowAWeights[1] + rowAWeights[2];
        for (int i = 0; i < 3; ++i)
        {
            const int w = i == 2 ? rowA.getWidth() : usable * rowAWeights[i] / total;
            groups[(size_t) i].bounds = rowA.removeFromLeft(w);
            if (i < 2)
                rowA.removeFromLeft(10);
        }
    }
    groups[0].caption = "Rhythm";
    groups[1].caption = "Pitch";
    groups[2].caption = "Key";

    // --- Row B: the two mode selectors and the phrase actions -------------------------
    auto rowB = area.removeFromTop(chBandBH);
    {
        const int usable = rowB.getWidth() - 10;
        const int total = rowBWeights[0] + rowBWeights[1];
        const int w0 = usable * rowBWeights[0] / total;
        groups[3].bounds = rowB.removeFromLeft(w0);
        rowB.removeFromLeft(10);
        groups[4].bounds = rowB;
    }
    groups[3].caption = "Mode";
    groups[4].caption = "Phrase";

    // Inside a group: past the caption rule, then its own row height. A knob spans the whole
    // row (a 12 px label, then the rotary or slider below it).
    const auto groupInner = [](const juce::Rectangle<int>& g, int innerH)
    {
        return g.reduced(10, 0).withTrimmedTop(chBandTop).withHeight(innerH);
    };
    const auto knobColumn = [](juce::Rectangle<int>& a, int w, juce::Label& lab, juce::Slider& s)
    {
        auto c = a.removeFromLeft(w);
        a.removeFromLeft(6);
        lab.setBounds(c.removeFromTop(12));
        s.setBounds(c);
    };

    // RHYTHM: three knobs, then the Length combo with its steppers.
    {
        auto inner = groupInner(groups[0].bounds, chRowAInner);
        knobColumn(inner, 52, densityLabel, densitySlider);
        knobColumn(inner, 52, dejaVuLabel, dejaVuSlider);
        knobColumn(inner, 52, jitterLabel, jitterSlider);
        inner.removeFromLeft(6);

        loopLenLabel.setBounds(inner.removeFromTop(12));
        auto lenRow = inner.withSizeKeepingCentre(inner.getWidth(), juce::jmin(inner.getHeight(), 34));
        loopLenPrev.setBounds(lenRow.removeFromLeft(34));
        lenRow.removeFromLeft(4);
        loopLenNext.setBounds(lenRow.removeFromRight(34));
        lenRow.removeFromRight(4);
        loopLenBox.setBounds(lenRow);
    }

    // PITCH: four knobs.
    {
        auto inner = groupInner(groups[1].bounds, chRowAInner);
        knobColumn(inner, 50, spreadLabel, spreadSlider);
        knobColumn(inner, 50, biasLabel, biasSlider);
        knobColumn(inner, 50, tempLabel, tempSlider);
        knobColumn(inner, 50, wanderLabel, wanderSlider);
    }

    // KEY: two horizontal sliders, sharing the row height a knob column would use so the
    // whole band lines up.
    {
        auto inner = groupInner(groups[2].bounds, chRowAInner);
        const int w = inner.getWidth() / 2;
        auto c1 = inner.removeFromLeft(w);
        auto c2 = inner;
        const auto sliderCell = [](juce::Rectangle<int> c, juce::Label& lab, juce::Slider& s)
        {
            lab.setBounds(c.removeFromTop(14));
            s.setBounds(c.withSizeKeepingCentre(c.getWidth(), juce::jmin(c.getHeight(), 34)));
        };
        sliderCell(c1.withTrimmedRight(6), keyLabel, keySlider);
        sliderCell(c2.withTrimmedLeft(6), chordPullLabel, chordPullSlider);
    }

    // MODE: two rows of three radio buttons, each led by a small label naming which
    // parameter it drives.
    {
        auto inner = groupInner(groups[3].bounds, chRowBInner);
        auto rowT = inner.removeFromTop(34);
        inner.removeFromTop(8);
        auto rowX = inner.removeFromTop(34);

        const auto modeRow = [](juce::Rectangle<int> row, juce::Label& lab,
                                std::initializer_list<juce::Button*> buttons)
        {
            lab.setBounds(row.removeFromLeft(48));
            row.removeFromLeft(6);
            const int n = (int) buttons.size();
            const int gap = 6;
            const int w = (row.getWidth() - gap * (n - 1)) / n;
            for (auto* b : buttons)
            {
                b->setBounds(row.removeFromLeft(w));
                row.removeFromLeft(gap);
            }
        };
        modeRow(rowT, rhythmModeLabel, { &tCoinButton, &tEuclidButton, &tBurstsButton });
        modeRow(rowX, voiceModeLabel, { &xLineButton, &xDuetButton, &xClusterButton });
    }

    // PHRASE: the three actions, one row, centred in the group's height.
    {
        auto inner = groupInner(groups[4].bounds, chRowBInner);
        auto row = inner.withSizeKeepingCentre(inner.getWidth(), 34);
        const int gap = 8;
        const int w = (row.getWidth() - gap * 2) / 3;
        generateButton.setBounds(row.removeFromLeft(w));
        row.removeFromLeft(gap);
        learnButton.setBounds(row.removeFromLeft(w));
        row.removeFromLeft(gap);
        freezeButton.setBounds(row);
    }
}
} // namespace keys
