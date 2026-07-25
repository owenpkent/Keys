#include "ChordGenPanel.h"
#include "../ChordMarkov.h"
#include "../ChordSuggest.h"
#include "../Chords.h"
#include "../ScaleModes.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>
#include <okstudio/Scales.h>
#include <okstudio/Theme.h>

namespace keys
{
namespace
{
    const juce::Colour panelBg { 0xff1c1f24 };
    const juce::Colour scrim { 0xcc0d0f12 };

    void styleLabel(juce::Label& l, const juce::String& text)
    {
        l.setText(text.toUpperCase(), juce::dontSendNotification);
        l.setFont(skin::micro(10.0f));
        l.setColour(juce::Label::textColourId, skin::textDim);
    }

    // A chord's own register, so a suggestion lands where the chord it follows sits.
    int octaveOf(const std::vector<int>& notes, int fallback)
    {
        if (notes.empty())
            return fallback;
        return *std::min_element(notes.begin(), notes.end()) / 12;
    }

    // "C4  E4  G4" — the note list a card carries under its chord name.
    juce::String noteListText(const std::vector<int>& notes)
    {
        const auto names = okstudio::scales::noteNames();
        juce::String out;
        for (const int n : notes)
            out << (out.isEmpty() ? "" : "  ") << names[((n % 12) + 12) % 12] << juce::String(n / 12 - 1);
        return out;
    }

    // A card's mini keyboard: two octaves (three when the chord spills over) from the
    // low note's C, held keys lit in accent. Purely informative, not a target.
    //
    // The accent is passed in rather than looked up: this is a free function with no
    // component to resolve it from, and the accent is per instance now.
    void drawMiniKeyboard(juce::Graphics& g, juce::Rectangle<float> r, const std::vector<int>& notes,
                          skin::Accent ac)
    {
        if (notes.empty())
            return;
        const int lo = *std::min_element(notes.begin(), notes.end());
        const int hi = *std::max_element(notes.begin(), notes.end());
        const int base = (lo / 12) * 12;
        const int octaves = hi < base + 24 ? 2 : 3;
        const auto held = [&notes](int n)
        { return std::find(notes.begin(), notes.end(), n) != notes.end(); };

        constexpr int whitePc[7] = { 0, 2, 4, 5, 7, 9, 11 };
        const int whites = octaves * 7;
        const float ww = r.getWidth() / (float) whites;
        for (int i = 0; i < whites; ++i)
        {
            const int note = base + (i / 7) * 12 + whitePc[i % 7];
            const auto key = juce::Rectangle<float>(r.getX() + ww * (float) i, r.getY(),
                                                    ww, r.getHeight()).reduced(0.5f, 0.0f);
            g.setColour(held(note) ? ac.base : juce::Colours::white.withAlpha(0.30f));
            g.fillRoundedRectangle(key, 1.0f);
        }

        constexpr int blackAfterWhite[5] = { 0, 1, 3, 4, 5 }; // C# D# F# G# A#
        constexpr int blackPc[5] = { 1, 3, 6, 8, 10 };
        const float bw = ww * 0.62f, bh = r.getHeight() * 0.62f;
        for (int o = 0; o < octaves; ++o)
            for (int b = 0; b < 5; ++b)
            {
                const int note = base + o * 12 + blackPc[b];
                const float x = r.getX() + ww * (float) (o * 7 + blackAfterWhite[b] + 1) - bw * 0.5f;
                g.setColour(held(note) ? ac.hot : juce::Colour(0xff101216));
                g.fillRoundedRectangle(x, r.getY(), bw, bh, 1.0f);
            }
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

void ChordGenPanel::PadButton::paintButton(juce::Graphics& g, bool over, bool down)
{
    // Chip background through the skin, then the card's own layout instead of the
    // base class's single text line.
    getLookAndFeel().drawButtonBackground(g, *this,
                                          findColour(juce::TextButton::buttonColourId), over, down);

    if (notes.empty())
    {
        g.setColour(skin::textFaint);
        g.setFont(skin::ui(13.0f));
        g.drawText("-", getLocalBounds(), juce::Justification::centred);
        return;
    }

    auto r = getLocalBounds().toFloat().reduced(10.0f, 8.0f);
    const float kbH = juce::jmin(26.0f, r.getHeight() * 0.34f);
    const auto kb = r.removeFromBottom(kbH)
                        .withSizeKeepingCentre(juce::jmin(170.0f, r.getWidth()), kbH);
    r.removeFromBottom(3.0f);
    const auto noteLine = r.removeFromBottom(14.0f);

    g.setColour(skin::text);
    g.setFont(skin::uiSemi(17.0f));
    g.drawText(getButtonText(), r, juce::Justification::centred, true);

    g.setColour(skin::textDim);
    g.setFont(skin::micro(10.0f));
    g.drawText(noteListText(notes), noteLine.toNearestInt(), juce::Justification::centred, true);

    drawMiniKeyboard(g, kb, notes, skin::accentOf(*this));

    if (locked)
    {
        const auto dot = juce::Rectangle<float>(5.0f, 5.0f)
                             .withCentre({ (float) getWidth() - 10.0f, 10.0f });
        g.setColour(okstudio::theme::good.withAlpha(0.4f));
        g.fillEllipse(dot.expanded(2.0f));
        g.setColour(okstudio::theme::good);
        g.fillEllipse(dot);
    }
}

ChordGenPanel::ChordGenPanel(KeysProcessor& p) : processor(p)
{
    okstudio::ui::makeMouseOnly(*this);
    buildControls();
    startTimerHz(15); // keep the pad grid and the mode's emotion line honest
}

ChordGenPanel::~ChordGenPanel()
{
    stopTimer();
    stopPreview(); // a suggestion left auditioning must not outlive the panel
}

bool ChordGenPanel::markovActive() const
{
    return (int) processor.apvts.getRawParameterValue("genSource")->load() == 1;
}

juce::String ChordGenPanel::moodArg() const
{
    const auto text = moodBox.getText();
    return text == "Any" ? juce::String() : text;
}

juce::String ChordGenPanel::startArg() const
{
    const auto text = startBox.getText();
    return text == "Any" ? juce::String() : text;
}

int ChordGenPanel::genRoot() const
{
    return (int) processor.apvts.getRawParameterValue("genRoot")->load();
}

int ChordGenPanel::genMode() const
{
    return juce::jlimit(0, modes::count() - 1, (int) processor.apvts.getRawParameterValue("genMode")->load());
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

    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeButton);

    styleLabel(rootLabel, "Key");
    addAndMakeVisible(rootLabel);
    rootBox.addItemList(okstudio::scales::noteNames(), 1);
    addAndMakeVisible(rootBox);
    rootAtt = std::make_unique<ComboAtt>(processor.apvts, "genRoot", rootBox);

    styleLabel(modeLabel, "Mode");
    addAndMakeVisible(modeLabel);
    modeBox.addItemList(modes::names(), 1);
    addAndMakeVisible(modeBox);
    modeAtt = std::make_unique<ComboAtt>(processor.apvts, "genMode", modeBox);

    styleLabel(octaveLabel, "Octave");
    addAndMakeVisible(octaveLabel);
    octaveSlider.setSliderStyle(juce::Slider::IncDecButtons);
    octaveSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 26);
    octaveSlider.setRange(2, 6, 1);
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
    lockInfluenceSlider.setTooltip("How much new chords copy the character of the ones you locked.");
    addAndMakeVisible(lockInfluenceSlider);
    lockInfluenceAtt = std::make_unique<SliderAtt>(processor.apvts, "genLockInfluence", lockInfluenceSlider);

    fillButton.onClick = [this] { fillPage(false); };
    regenButton.onClick = [this] { fillPage(true); };
    clearButton.onClick = [this] { clearPage(); };
    fillButton.setTooltip("Fill every pad on this page (locked pads are kept).");
    regenButton.setTooltip("New chords for the unlocked pads only.");
    for (auto* b : { &fillButton, &regenButton, &clearButton })
        addAndMakeVisible(*b);

    // The generator's brain: the weighted pool above, or Octavium's Markov chains.
    styleLabel(sourceLabel, "Source");
    addAndMakeVisible(sourceLabel);
    sourceBox.addItemList({ "Algorithmic", "Markov" }, 1);
    addAndMakeVisible(sourceBox);
    sourceAtt = std::make_unique<ComboAtt>(processor.apvts, "genSource", sourceBox);

    styleLabel(chainLabel, "Chain");
    chainBox.addItemList({ "Major", "Minor", "Modal" }, 1);
    chainAtt = std::make_unique<ComboAtt>(processor.apvts, "markovMode", chainBox);

    styleLabel(moodLabel, "Mood");
    styleLabel(startLabel, "Start");
    startBox.addItem("Any", 1);
    {
        int id = 2;
        for (const char* token : markov::startTokens())
            startBox.addItem(token, id++);
    }
    startBox.setSelectedId(1, juce::dontSendNotification);
    startBox.setTooltip("Force the first chord of the progression, or let the chain pick.");

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

    markovShown = markovActive();
    for (auto* c : std::initializer_list<juce::Component*> { &chainLabel, &chainBox, &moodLabel, &moodBox,
                                                             &startLabel, &startBox, &tempLabel, &tempSlider,
                                                             &lengthLabel, &lengthSlider })
    {
        addChildComponent(*c);
        c->setVisible(markovShown);
    }
    refreshMoodItems();

    pageLabel.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    pageLabel.setColour(juce::Label::textColourId, okstudio::theme::textDim);
    addAndMakeVisible(pageLabel);

    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
    {
        auto row = std::make_unique<PadRow>();

        // Press-and-hold auditions the chord: same gesture as the pad strip, same code path.
        row->play.onStateChange = [this, v]
        {
            auto& r = *padRows[(size_t) v];
            const int slot = processor.padPageOffset() + v;
            const bool down = r.play.isDown();
            if (down && ! r.playHeld)
                processor.pressChordPad(slot);
            else if (! down && r.playHeld)
                processor.releaseChordPad(slot);
            r.playHeld = down;
        };
        // Octavium's card menu, back on the card itself: Lock / New chord / Next.
        row->play.onRightClick = [this, v] { showPadMenu(processor.padPageOffset() + v); };
        row->play.setTooltip("Hold to hear it. Right-click for Lock, New chord, and what could follow.");

        addAndMakeVisible(row->play);
        padRows[(size_t) v] = std::move(row);
    }
}

chordgen::Options ChordGenPanel::currentOptions() const
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

std::vector<int> ChordGenPanel::lockedTypesOnPage() const
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

void ChordGenPanel::writeChord(int slot, const chordgen::Chord& c)
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

void ChordGenPanel::refreshMoodItems()
{
    lastChainMode = juce::jlimit(0, 2, (int) processor.apvts.getRawParameterValue("markovMode")->load());
    const auto keep = moodBox.getText();
    moodBox.clear(juce::dontSendNotification);
    moodBox.addItem("Any", 1);
    int id = 2;
    for (const auto& mood : markov::moodsFor(lastChainMode))
        moodBox.addItem(mood, id++);
    moodBox.setSelectedId(1, juce::dontSendNotification);
    for (int i = 0; i < moodBox.getNumItems(); ++i)
        if (moodBox.getItemText(i) == keep)
            moodBox.setSelectedId(moodBox.getItemId(i), juce::dontSendNotification);
}

void ChordGenPanel::previewChord(const std::vector<int>& notes)
{
    stopPreview();
    const float vel = processor.baseVelocity01();
    for (const int n : notes)
        processor.noteOn(n, vel); // Humanize colours the audition like everything else
    previewNotes = notes;
    previewEndMs = juce::Time::getMillisecondCounter() + 800; // Octavium's preview length
}

void ChordGenPanel::stopPreview()
{
    for (const int n : previewNotes)
        processor.noteOff(n);
    previewNotes.clear();
    previewEndMs = 0;
}

void ChordGenPanel::fillPageMarkov(bool onlyUnlocked)
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

    const int chain = juce::jlimit(0, 2, (int) processor.apvts.getRawParameterValue("markovMode")->load());
    const auto generated = markov::generate(chain, genRoot(), currentOptions().octave,
                                            (int) processor.apvts.getRawParameterValue("markovLength")->load(),
                                            processor.apvts.getRawParameterValue("markovTemp")->load(),
                                            moodArg(), startArg(), (int) targets.size(), rng);
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

void ChordGenPanel::regeneratePadMarkov(int slot)
{
    const int offset = processor.padPageOffset();
    // The chain steps from the pad to the left on this page; the first pad restarts.
    juce::String predecessor;
    if (slot > offset)
        predecessor = processor.chordPad(slot - 1).numeral;

    const int chain = juce::jlimit(0, 2, (int) processor.apvts.getRawParameterValue("markovMode")->load());
    const auto c = markov::regenerateSingle(chain, genRoot(), currentOptions().octave,
                                            predecessor, processor.chordPad(slot).numeral,
                                            processor.apvts.getRawParameterValue("markovTemp")->load(),
                                            moodArg(), rng);
    KeysProcessor::ChordPad pad;
    pad.notes = c.notes;
    pad.name = chords::detect(c.notes);
    pad.rootPc = c.rootPc;
    pad.type = c.type;
    pad.numeral = c.numeral;
    pad.locked = processor.chordPad(slot).locked;
    processor.setChordPad(slot, pad);
}

void ChordGenPanel::fillPage(bool onlyUnlocked)
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

void ChordGenPanel::clearPage()
{
    const int offset = processor.padPageOffset();
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
        if (! processor.chordPad(offset + v).locked) // Clear spares locks too, like Regen
            processor.clearChordPad(offset + v);
}

void ChordGenPanel::regeneratePad(int slot)
{
    const auto& pad = processor.chordPad(slot);
    if (pad.locked)
        return;
    // A Markov pad regenerates through its chain regardless of the Source combo: the
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

void ChordGenPanel::newChordFor(int slot)
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

void ChordGenPanel::showPadMenu(int slot)
{
    const auto& pad = processor.chordPad(slot);
    const bool filled = ! pad.notes.empty();
    const int offset = processor.padPageOffset();
    juce::Component::SafePointer<ChordGenPanel> safe(this);

    juce::PopupMenu menu;
    menu.addItem(1, pad.locked ? "Unlock" : "Lock", filled);
    menu.addItem(2, "New chord", ! pad.locked);

    // "What could follow this?" — the four suggestion families, each row carrying a
    // play button so it can audition without closing the menu (Octavium's per-row
    // preview). A pick lands in the next free pad on the page rather than replacing
    // the chord you asked about; with the page full it takes the slot right after,
    // which is where a progression would go anyway.
    std::vector<suggest::Suggestion> flat;
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

        menu.addSeparator();
        menu.addSectionHeader("Next: could follow");
        const char* categories[] = { "Neo-Riemannian", "Circle of Fifths", "Diatonic", "Chromatic" };
        int id = 10;
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
                flat.push_back(s);
            }
            menu.addSubMenu(cat, sub);
        }
    }

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(&padRows[(size_t) (slot - offset)]->play)
                           .withStandardItemHeight(okstudio::ui::minHitPx), // mouse-only: no small targets
                       [safe, slot, flat, target](int choice)
    {
        if (safe == nullptr)
            return;
        safe->stopPreview(); // don't let the last audition ring past the menu
        if (choice == 1)
        {
            auto& p = safe->processor;
            p.setChordPadLocked(slot, ! p.chordPad(slot).locked);
            return;
        }
        if (choice == 2)
        {
            safe->newChordFor(slot);
            return;
        }
        if (choice < 10 || choice - 10 >= (int) flat.size() || target < 0)
            return;
        const auto& s = flat[(size_t) (choice - 10)];
        safe->writeChord(target, { s.rootPc, s.type, s.notes, -1 });
    });
}

void ChordGenPanel::timerCallback()
{
    modeEmotion.setText(modes::get(genMode()).emotion, juce::dontSendNotification);
    pageLabel.setText("Page " + juce::String(processor.padPage() + 1) + " of "
                          + juce::String(KeysProcessor::numPadPages),
                      juce::dontSendNotification);

    // A suggestion audition stops itself after Octavium's 800 ms.
    if (! previewNotes.empty() && juce::Time::getMillisecondCounter() >= previewEndMs)
        stopPreview();

    // The Mood list belongs to the chain that's up.
    if ((int) processor.apvts.getRawParameterValue("markovMode")->load() != lastChainMode)
        refreshMoodItems();

    // Source switch: show the Markov controls, grey what doesn't apply to chains
    // (Octavium left these clickable and silently ignored them — greying is honest).
    const bool markov = markovActive();
    if (markov != markovShown)
    {
        markovShown = markov;
        for (auto* c : std::initializer_list<juce::Component*> { &chainLabel, &chainBox, &moodLabel, &moodBox,
                                                                 &startLabel, &startBox, &tempLabel, &tempSlider,
                                                                 &lengthLabel, &lengthSlider })
            c->setVisible(markov);
        resized();
    }
    for (auto* c : std::initializer_list<juce::Component*> { &modeBox, &modeLabel, &notesLabel, &invLabel,
                                                             &triadsButton, &seventhsButton, &ninthsButton,
                                                             &inv0Button, &inv1Button, &inv2Button, &inv3Button,
                                                             &complianceSlider, &complianceLabel,
                                                             &lockInfluenceSlider, &lockInfluenceLabel })
        c->setEnabled(! markov);
    const int offset = processor.padPageOffset();
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
    {
        const auto& pad = processor.chordPad(offset + v);
        auto& r = *padRows[(size_t) v];
        const bool filled = ! pad.notes.empty();

        r.play.setButtonText(filled ? pad.name : juce::String("-"));
        r.play.setEnabled(filled); // the right-click menu still opens on an empty pad
        if (r.play.locked != pad.locked || r.play.notes != pad.notes)
        {
            r.play.locked = pad.locked;
            r.play.notes = pad.notes;
            r.play.repaint();
        }
    }
    repaint();
}

void ChordGenPanel::mouseDown(const juce::MouseEvent&)
{
    // Opaque to clicks: as an overlay nothing behind it should react, and inline the
    // card's own background should not fall through to the editor either.
}

void ChordGenPanel::setInlineMode(bool b)
{
    if (inlineMode == b)
        return;
    inlineMode = b;
    repaint();
}

void ChordGenPanel::paint(juce::Graphics& g)
{
    if (! inlineMode)
        g.fillAll(scrim); // dim whatever is behind, so the panel reads as the active surface
    const auto b = getLocalBounds().reduced(8).toFloat();
    g.setColour(panelBg);
    g.fillRoundedRectangle(b, skin::panelRadius);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.fillRoundedRectangle(b.withHeight(1.5f).reduced(skin::panelRadius, 0.0f), 0.75f);
    skin::glowRect(g, b, skin::panelRadius, skin::accentOf(*this).base, inlineMode ? 0.30f : 0.55f);
}

void ChordGenPanel::resized()
{
    auto area = getLocalBounds().reduced(8).reduced(12);

    auto top = area.removeFromTop(28);
    title.setBounds(top.removeFromLeft(150));
    closeButton.setBounds(top.removeFromRight(80).withSizeKeepingCentre(80, 28));
    pageLabel.setBounds(top.removeFromRight(110));
    modeEmotion.setBounds(top.reduced(8, 0));
    area.removeFromTop(8);

    const auto cell = [](juce::Rectangle<int>& row, int w, juce::Label& lab, juce::Component& ctl)
    {
        auto c = row.removeFromLeft(w);
        row.removeFromLeft(8);
        lab.setBounds(c.removeFromTop(14));
        ctl.setBounds(c);
    };

    // Row 1: key, mode, octave, and which brain generates.
    auto rowA = area.removeFromTop(44);
    cell(rowA, 70, rootLabel, rootBox);
    cell(rowA, 170, modeLabel, modeBox);
    cell(rowA, 110, octaveLabel, octaveSlider);
    cell(rowA, 130, sourceLabel, sourceBox);
    area.removeFromTop(4);

    // Row 2: note counts, inversions, and the two weighting sliders — or, when the
    // Markov source is up, its chain controls in the same band (visibility picks one).
    auto rowC = area.removeFromTop(44);
    {
        auto markovRow = rowC;
        cell(markovRow, 110, chainLabel, chainBox);
        cell(markovRow, 150, moodLabel, moodBox);
        cell(markovRow, 100, startLabel, startBox);
        cell(markovRow, 220, tempLabel, tempSlider);
        cell(markovRow, 150, lengthLabel, lengthSlider);
    }
    {
        auto c = rowC.removeFromLeft(120);
        rowC.removeFromLeft(8);
        notesLabel.setBounds(c.removeFromTop(14));
        const int w = c.getWidth() / 3;
        triadsButton.setBounds(c.removeFromLeft(w));
        seventhsButton.setBounds(c.removeFromLeft(w));
        ninthsButton.setBounds(c);
    }
    {
        auto c = rowC.removeFromLeft(200);
        rowC.removeFromLeft(8);
        invLabel.setBounds(c.removeFromTop(14));
        const int w = c.getWidth() / 4;
        inv0Button.setBounds(c.removeFromLeft(w));
        inv1Button.setBounds(c.removeFromLeft(w));
        inv2Button.setBounds(c.removeFromLeft(w));
        inv3Button.setBounds(c);
    }
    cell(rowC, 230, complianceLabel, complianceSlider);
    cell(rowC, 230, lockInfluenceLabel, lockInfluenceSlider);
    area.removeFromTop(6);

    // Row 3: the page-wide actions (the left-click bulk path).
    auto rowD = area.removeFromTop(36);
    fillButton.setBounds(rowD.removeFromLeft(120));
    rowD.removeFromLeft(8);
    regenButton.setBounds(rowD.removeFromLeft(150));
    rowD.removeFromLeft(8);
    clearButton.setBounds(rowD.removeFromLeft(120));
    area.removeFromTop(8);

    // The pad grid: 4 across, 4 down (16 pads, Octavium's 4x4). Just the cards — the
    // per-pad actions live in each card's right-click menu, so every card is a big,
    // clean play target.
    const int cols = 4, rows = KeysProcessor::padsPerPage / 4;
    const int gap = 8;
    const int cw = (area.getWidth() - gap * (cols - 1)) / cols;
    const int ch = (area.getHeight() - gap * (rows - 1)) / juce::jmax(rows, 1);
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
    {
        auto& r = *padRows[(size_t) v];
        const int col = v % cols, rw = v / cols;
        r.play.setBounds(area.getX() + col * (cw + gap), area.getY() + rw * (ch + gap), cw, ch);
    }
}
} // namespace keys
