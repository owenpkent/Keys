#include "PluginEditor.h"
#include <okstudio/MouseOnly.h>
#include <okstudio/Scales.h>
#include <cmath>

namespace keys
{
namespace
{
    // Low note + key count for each of the six sizes, matching the "size" parameter.
    struct SizeSpec
    {
        int low, count;
    };
    const SizeSpec sizeSpecs[] = { { 48, 25 }, { 36, 49 }, { 36, 61 }, { 24, 73 }, { 28, 76 }, { 21, 88 } };

    juce::StringArray sizeItems() { return { "25 keys", "49 keys", "61 keys", "73 keys", "76 keys", "88 keys" }; }

    juce::StringArray channelItems()
    {
        juce::StringArray out;
        for (int i = 1; i <= 16; ++i)
            out.add(juce::String(i));
        return out;
    }

    void styleLabel(juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        l.setColour(juce::Label::textColourId, okstudio::theme::textDim);
    }

    // Octavium-neutral chrome (local to Keys; the shared kit palette is blue-tinted).
    const juce::Colour neutralBg      { 0xff1a1c20 };
    const juce::Colour neutralPanel   { 0xff23262c };
    const juce::Colour neutralControl { 0xff2b2f36 };
    const juce::Colour neutralOutline { 0xff3b4148 };
    const juce::Colour neutralGroove  { 0xff3a3f46 };
} // namespace

KeysEditor::KeysEditor(KeysProcessor& p)
    : juce::AudioProcessorEditor(p), processor(p), keyboard(p), chordPads(p)
{
    setLookAndFeel(&lnf);
    // Retint the kit chrome toward Octavium's neutral grey. Overrides live on this
    // editor's own LookAndFeel instance, so sibling plugins are untouched.
    lnf.setColour(juce::ResizableWindow::backgroundColourId, neutralBg);
    lnf.setColour(juce::ComboBox::backgroundColourId, neutralControl);
    lnf.setColour(juce::ComboBox::outlineColourId, neutralOutline);
    lnf.setColour(juce::PopupMenu::backgroundColourId, neutralPanel);
    lnf.setColour(juce::TextButton::buttonColourId, neutralControl);
    lnf.setColour(juce::Slider::backgroundColourId, neutralGroove);
    okstudio::ui::makeMouseOnly(*this);

    title.setText("Keys", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, okstudio::theme::text);
    addAndMakeVisible(title);

    addCombo(sizeBox, sizeLabel, "Size", sizeItems(), "size", sizeAtt);
    addCombo(rootBox, rootLabel, "Root", okstudio::scales::noteNames(), "root", rootAtt);
    addCombo(scaleBox, scaleLabel, "Scale", okstudio::scales::names(), "scale", scaleAtt);
    addCombo(channelBox, channelLabel, "MIDI Ch", channelItems(), "channel", channelAtt);
    addCombo(curveBox, curveLabel, "Curve", { "Soft", "Linear", "Hard" }, "curve", curveAtt);

    styleLabel(octaveLabel, "Octave");
    addAndMakeVisible(octaveLabel);
    octaveSlider.setSliderStyle(juce::Slider::IncDecButtons);
    octaveSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 46, 26);
    octaveSlider.setRange(-3, 3, 1);
    addAndMakeVisible(octaveSlider);
    octaveAtt = std::make_unique<SliderAtt>(processor.apvts, "octave", octaveSlider);

    styleLabel(velocityLabel, "Velocity");
    addAndMakeVisible(velocityLabel);
    velocitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    velocitySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 26);
    velocitySlider.setRange(1, 127, 1);
    addAndMakeVisible(velocitySlider);
    velocityAtt = std::make_unique<SliderAtt>(processor.apvts, "velocity", velocitySlider);

    for (auto* b : { &scaleLockButton, &sustainButton, &latchButton, &humanizeButton, &chordExclusiveButton })
        addAndMakeVisible(*b);
    scaleLockAtt = std::make_unique<ButtonAtt>(processor.apvts, "scaleLock", scaleLockButton);
    sustainAtt = std::make_unique<ButtonAtt>(processor.apvts, "sustain", sustainButton);
    latchAtt = std::make_unique<ButtonAtt>(processor.apvts, "latch", latchButton);
    humanizeAtt = std::make_unique<ButtonAtt>(processor.apvts, "humanize", humanizeButton);
    chordExclusiveAtt = std::make_unique<ButtonAtt>(processor.apvts, "chordExclusive", chordExclusiveButton);

    // Humanize amounts: each note picks a random velocity in [Vel Min, Vel Max] and a
    // micro-timing offset up to Timing ms. Active only when Humanize is on.
    styleLabel(humanizeVelMinLabel, "Vel Min");
    addAndMakeVisible(humanizeVelMinLabel);
    humanizeVelMinSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    humanizeVelMinSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 26);
    humanizeVelMinSlider.setRange(1, 127, 1);
    addAndMakeVisible(humanizeVelMinSlider);
    humanizeVelMinAtt = std::make_unique<SliderAtt>(processor.apvts, "humanizeVelMin", humanizeVelMinSlider);

    styleLabel(humanizeVelMaxLabel, "Vel Max");
    addAndMakeVisible(humanizeVelMaxLabel);
    humanizeVelMaxSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    humanizeVelMaxSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 26);
    humanizeVelMaxSlider.setRange(1, 127, 1);
    addAndMakeVisible(humanizeVelMaxSlider);
    humanizeVelMaxAtt = std::make_unique<SliderAtt>(processor.apvts, "humanizeVelMax", humanizeVelMaxSlider);

    styleLabel(humanizeTimeLabel, "Timing");
    addAndMakeVisible(humanizeTimeLabel);
    humanizeTimeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    humanizeTimeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 26);
    humanizeTimeSlider.setRange(0, 30, 1);
    humanizeTimeSlider.setTextValueSuffix(" ms");
    addAndMakeVisible(humanizeTimeSlider);
    humanizeTimeAtt = std::make_unique<SliderAtt>(processor.apvts, "humanizeTime", humanizeTimeSlider);

    panicButton.onClick = [this] { keyboard.panic(); };
    addAndMakeVisible(panicButton);

    updateButton.setColour(juce::TextButton::buttonColourId, okstudio::theme::good.withAlpha(0.85f));
    addChildComponent(updateButton); // hidden until the updater finds a newer release

    keyboard.getVelocity = [this] { return processor.baseVelocity01(); };
    addAndMakeVisible(keyboard);
    addAndMakeVisible(chordPads);

    // Auto-update: check the pinned releases repo once, surface a button if newer.
    updaterConfig.productName = "Keys";
    updaterConfig.releasesRepo = "okstudio1/keys-releases";
    updaterConfig.currentVersion = juce::String(KEYS_VERSION);
    updaterConfig.assetPrefix = "KeysSetup-";
    juce::Component::SafePointer<KeysEditor> safe(this);
    okstudio::updater::checkAsync(updaterConfig, [safe](okstudio::updater::UpdateInfo info)
    {
        if (auto* e = safe.getComponent())
            e->showUpdate(info);
    });

    setResizable(true, true);
    setResizeLimits(820, 376, 2200, 1000);
    setSize(940, 386);
    startTimerHz(30);
}

KeysEditor::~KeysEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void KeysEditor::addCombo(juce::ComboBox& box, juce::Label& label, const juce::String& text,
                          const juce::StringArray& items, const juce::String& paramID,
                          std::unique_ptr<ComboAtt>& att)
{
    styleLabel(label, text);
    addAndMakeVisible(label);
    box.addItemList(items, 1);
    addAndMakeVisible(box);
    att = std::make_unique<ComboAtt>(processor.apvts, paramID, box);
}

void KeysEditor::showUpdate(const okstudio::updater::UpdateInfo& info)
{
    pendingUpdate = info;
    updateButton.setButtonText("Update to v" + info.version);
    updateButton.setVisible(true);
    juce::Component::SafePointer<KeysEditor> safe(this);
    updateButton.onClick = [this, safe]
    {
        updateButton.setEnabled(false);
        updateButton.setButtonText("Starting" + juce::String::fromUTF8("\xe2\x80\xa6"));
        okstudio::updater::downloadAndInstallAsync(updaterConfig, pendingUpdate,
                                                   [safe](juce::String status, bool failed)
        {
            if (auto* e = safe.getComponent())
            {
                e->updateButton.setButtonText(status);
                e->updateButton.setEnabled(failed);
            }
        });
    };
    resized();
}

void KeysEditor::timerCallback()
{
    const auto& apvts = processor.apvts;
    const int sizeIdx = juce::jlimit(0, 5, (int) apvts.getRawParameterValue("size")->load());
    keyboard.setRange(sizeSpecs[sizeIdx].low, sizeSpecs[sizeIdx].count);
    keyboard.setScaleLock(apvts.getRawParameterValue("scaleLock")->load() > 0.5f,
                          (int) apvts.getRawParameterValue("root")->load(),
                          (int) apvts.getRawParameterValue("scale")->load());
    keyboard.setSustain(apvts.getRawParameterValue("sustain")->load() > 0.5f);
    keyboard.setLatch(apvts.getRawParameterValue("latch")->load() > 0.5f);

    // Grey the humanize amounts out when Humanize is off, so their state reads at a glance.
    const bool hum = apvts.getRawParameterValue("humanize")->load() > 0.5f;
    humanizeVelMinSlider.setEnabled(hum);
    humanizeVelMaxSlider.setEnabled(hum);
    humanizeTimeSlider.setEnabled(hum);
    humanizeVelMinLabel.setEnabled(hum);
    humanizeVelMaxLabel.setEnabled(hum);
    humanizeTimeLabel.setEnabled(hum);

    // Feed the pads the chord currently sounding on the keyboard (drives the live card
    // and reflects any pad-active changes).
    chordPads.setCurrentChord(keyboard.soundingOutputNotes());
    chordPads.repaint();
}

void KeysEditor::paint(juce::Graphics& g)
{
    g.fillAll(neutralBg);
    g.setColour(neutralPanel);
    g.fillRect(getLocalBounds().removeFromTop(164));
}

void KeysEditor::resized()
{
    auto area = getLocalBounds().reduced(10);

    // Give the keyboard a fixed height off the bottom, then the chord-pad row above it,
    // then let the three control rows take the space that's left on top. Keeping the keys
    // a fixed height stops them stretching when the host makes the editor taller.
    auto kb = area.removeFromBottom(150);
    area.removeFromBottom(8);
    auto padRow = area.removeFromBottom(48);
    area.removeFromBottom(6);
    auto header = area;

    // Chord pads: an Exclusive toggle on the left, then the pad strip.
    chordExclusiveButton.setBounds(padRow.removeFromLeft(74).withSizeKeepingCentre(70, 26));
    padRow.removeFromLeft(4);
    chordPads.setBounds(padRow);

    const int rowH = 46;
    title.setBounds(header.removeFromLeft(84).withTrimmedTop(header.getHeight() / 2 - 17).withHeight(34));
    header.removeFromLeft(6);

    auto rowA = header.removeFromTop(rowH);
    header.removeFromTop(3);
    auto rowB = header.removeFromTop(rowH);
    header.removeFromTop(3);
    auto rowC = header.removeFromTop(rowH);

    const auto cell = [](juce::Rectangle<int>& row, int w, juce::Label& lab, juce::Component& ctl)
    {
        auto c = row.removeFromLeft(w);
        row.removeFromLeft(8);
        lab.setBounds(c.removeFromTop(14));
        ctl.setBounds(c);
    };
    const auto toggleCell = [](juce::Rectangle<int>& row, int w, juce::Component& ctl)
    {
        auto c = row.removeFromLeft(w);
        row.removeFromLeft(8);
        ctl.setBounds(c.withTrimmedTop(14));
    };

    cell(rowA, 88, sizeLabel, sizeBox);
    cell(rowA, 70, rootLabel, rootBox);
    cell(rowA, 150, scaleLabel, scaleBox);
    cell(rowA, 120, octaveLabel, octaveSlider);
    toggleCell(rowA, 110, scaleLockButton);

    cell(rowB, 210, velocityLabel, velocitySlider);
    cell(rowB, 110, curveLabel, curveBox);
    cell(rowB, 70, channelLabel, channelBox);
    toggleCell(rowB, 100, sustainButton);
    toggleCell(rowB, 90, latchButton);
    toggleCell(rowB, 90, panicButton);

    toggleCell(rowC, 110, humanizeButton);
    cell(rowC, 160, humanizeVelMinLabel, humanizeVelMinSlider);
    cell(rowC, 160, humanizeVelMaxLabel, humanizeVelMaxSlider);
    cell(rowC, 170, humanizeTimeLabel, humanizeTimeSlider);
    if (updateButton.isVisible())
        updateButton.setBounds(rowC.removeFromLeft(150).withTrimmedTop(14));

    keyboard.setBounds(kb);
}
} // namespace keys
