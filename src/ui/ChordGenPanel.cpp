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
                g.setColour(skin::accent.withAlpha(0.15f));
                g.fillRoundedRectangle(r, 4.0f);
                g.setColour(skin::accent.withAlpha(0.4f));
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

    // Emotion presets: one click sets the generator's key and mode, and moves Root/Scale
    // to match so Scale Lock agrees with what the pads are about to be built from.
    styleLabel(emotionLabel, "Feel");
    addAndMakeVisible(emotionLabel);
    for (int i = 0; i < (int) modes::emotions().size(); ++i)
    {
        auto* b = emotionButtons.add(new juce::TextButton(modes::emotions()[(size_t) i].label));
        b->onClick = [this, i] { applyEmotion(i); };
        addAndMakeVisible(*b);
    }

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
        row->lock.onClick = [this, v]
        {
            const int slot = processor.padPageOffset() + v;
            processor.setChordPadLocked(slot, ! processor.chordPad(slot).locked);
        };
        row->regen.onClick = [this, v] { regeneratePad(processor.padPageOffset() + v); };
        row->next.onClick = [this, v] { showSuggestions(processor.padPageOffset() + v); };

        row->regen.setButtonText("New");
        row->next.setButtonText("Next");
        row->lock.setTooltip("Keep this chord when regenerating, and steer new chords toward its character.");
        row->regen.setTooltip("A different chord for this pad's scale degree.");
        row->next.setTooltip("Chords that could follow this one.");

        for (auto* b : { &row->play, &row->lock, &row->regen, &row->next })
            addAndMakeVisible(*b);
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

void ChordGenPanel::applyEmotion(int emotionIndex)
{
    const auto& e = modes::emotions()[(size_t) emotionIndex];
    const int modeIdx = modes::indexOf(e.mode);

    const auto set = [this](const char* id, int value)
    {
        if (auto* p = processor.apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1((float) value));
    };
    set("genRoot", e.rootPc);
    set("genMode", modeIdx);

    // Move Scale Lock to the same key, so the keyboard and the pads agree.
    set("root", e.rootPc);
    if (const int kit = modes::kitScaleIndex(modeIdx); kit >= 0)
        set("scale", kit);

    if (onKeyChanged)
        onKeyChanged();
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

void ChordGenPanel::showSuggestions(int slot)
{
    const auto& pad = processor.chordPad(slot);
    if (pad.notes.empty())
        return;

    // A generated pad already knows what it is; a hand-captured one gets worked out here.
    auto [rootPc, type] = pad.type >= 0 ? std::pair<int, int> { pad.rootPc, pad.type }
                                        : suggest::analyse(pad.notes);
    const int octave = octaveOf(pad.notes, currentOptions().octave);
    const auto suggestions = suggest::all(rootPc, type, octave);

    // A suggestion answers "what comes next", so it lands in the next free pad on the page
    // rather than replacing the chord you asked about. With the page full it takes the slot
    // right after, which is where a progression would go anyway.
    const int offset = processor.padPageOffset();
    int target = -1;
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
    if (target < 0)
        return; // everything on the page is locked; nowhere to put it

    juce::PopupMenu menu;
    const char* categories[] = { "Neo-Riemannian", "Circle of Fifths", "Diatonic", "Chromatic" };
    int id = 1;
    std::vector<suggest::Suggestion> flat;
    juce::Component::SafePointer<ChordGenPanel> safe(this);
    for (const char* cat : categories)
    {
        juce::PopupMenu sub;
        for (const auto& s : suggestions)
        {
            if (juce::String(s.category) != cat)
                continue;
            // Custom rows so each carries a play button: audition without closing the
            // menu (Octavium's per-row preview), click the row itself to take it.
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

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(&padRows[(size_t) (slot - offset)]->next)
                           .withStandardItemHeight(okstudio::ui::minHitPx), // mouse-only: no small targets
                       [safe, flat, target](int choice)
    {
        if (safe == nullptr)
            return;
        safe->stopPreview(); // don't let the last audition ring past the menu
        if (choice <= 0 || choice > (int) flat.size())
            return;
        const auto& s = flat[(size_t) (choice - 1)];
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
    for (auto* b : emotionButtons)
        b->setEnabled(! markov);

    const int offset = processor.padPageOffset();
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
    {
        const auto& pad = processor.chordPad(offset + v);
        auto& r = *padRows[(size_t) v];
        const bool filled = ! pad.notes.empty();

        r.play.setButtonText(filled ? pad.name : juce::String("-"));
        r.play.setEnabled(filled);
        r.lock.setButtonText(pad.locked ? "Locked" : "Lock");
        r.lock.setColour(juce::TextButton::buttonColourId,
                         pad.locked ? okstudio::theme::good.withAlpha(0.7f) : skin::control);
        r.lock.setEnabled(filled);
        r.regen.setEnabled(filled && ! pad.locked);
        r.next.setEnabled(filled);
    }
    repaint();
}

void ChordGenPanel::mouseDown(const juce::MouseEvent&)
{
    // The overlay is opaque to clicks: nothing behind it should react while it is up.
}

void ChordGenPanel::paint(juce::Graphics& g)
{
    g.fillAll(scrim); // dim whatever is behind, so the panel reads as the active surface
    const auto b = getLocalBounds().reduced(8).toFloat();
    g.setColour(panelBg);
    g.fillRoundedRectangle(b, skin::panelRadius);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.fillRoundedRectangle(b.withHeight(1.5f).reduced(skin::panelRadius, 0.0f), 0.75f);
    skin::glowRect(g, b, skin::panelRadius, skin::accent, 0.55f);
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

    // Row 2: the emotion presets, evenly across the width.
    auto rowB = area.removeFromTop(44);
    emotionLabel.setBounds(rowB.removeFromLeft(40).removeFromTop(14).translated(0, 14));
    {
        const int n = emotionButtons.size();
        const int gap = 4;
        const int w = (rowB.getWidth() - gap * (n - 1)) / juce::jmax(n, 1);
        for (int i = 0; i < n; ++i)
        {
            emotionButtons[i]->setBounds(rowB.removeFromLeft(w).withTrimmedTop(6));
            rowB.removeFromLeft(gap);
        }
    }
    area.removeFromTop(4);

    // Row 3: note counts, inversions, and the two weighting sliders — or, when the
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

    // Row 4: the page-wide actions.
    auto rowD = area.removeFromTop(36);
    fillButton.setBounds(rowD.removeFromLeft(120));
    rowD.removeFromLeft(8);
    regenButton.setBounds(rowD.removeFromLeft(150));
    rowD.removeFromLeft(8);
    clearButton.setBounds(rowD.removeFromLeft(120));
    area.removeFromTop(8);

    // The pad grid: 4 across, 4 down (16 pads, Octavium's 4x4), each with its own actions underneath.
    const int cols = 4, rows = KeysProcessor::padsPerPage / 4;
    const int gap = 8;
    const int cw = (area.getWidth() - gap * (cols - 1)) / cols;
    const int ch = (area.getHeight() - gap * (rows - 1)) / juce::jmax(rows, 1);
    for (int v = 0; v < KeysProcessor::padsPerPage; ++v)
    {
        auto& r = *padRows[(size_t) v];
        const int col = v % cols, rw = v / cols;
        juce::Rectangle<int> cellArea(area.getX() + col * (cw + gap), area.getY() + rw * (ch + gap), cw, ch);

        // The chord button takes the space that's left; the three actions get a fixed strip
        // at the bottom, never thinner than the minimum hit target.
        auto actions = cellArea.removeFromBottom(juce::jmax(okstudio::ui::minHitPx, ch / 3));
        r.play.setBounds(cellArea.reduced(0, 2));
        const int aw = (actions.getWidth() - 8) / 3;
        r.lock.setBounds(actions.removeFromLeft(aw));
        actions.removeFromLeft(4);
        r.regen.setBounds(actions.removeFromLeft(aw));
        actions.removeFromLeft(4);
        r.next.setBounds(actions);
    }
}
} // namespace keys
