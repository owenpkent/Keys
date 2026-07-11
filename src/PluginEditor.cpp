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
} // namespace

KeysEditor::KeysEditor(KeysProcessor& p) : juce::AudioProcessorEditor(p), processor(p), keyboard(p)
{
    setLookAndFeel(&lnf);
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

    for (auto* b : { &scaleLockButton, &sustainButton, &latchButton })
        addAndMakeVisible(*b);
    scaleLockAtt = std::make_unique<ButtonAtt>(processor.apvts, "scaleLock", scaleLockButton);
    sustainAtt = std::make_unique<ButtonAtt>(processor.apvts, "sustain", sustainButton);
    latchAtt = std::make_unique<ButtonAtt>(processor.apvts, "latch", latchButton);

    panicButton.onClick = [this] { keyboard.panic(); };
    addAndMakeVisible(panicButton);

    updateButton.setVisible(false);
    updateButton.setColour(juce::TextButton::buttonColourId, okstudio::theme::good.withAlpha(0.85f));
    addAndMakeVisible(updateButton);

    keyboard.getVelocity = [this] { return currentVelocity01(); };
    addAndMakeVisible(keyboard);

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
    setResizeLimits(680, 300, 2200, 1000);
    setSize(920, 380);
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

float KeysEditor::currentVelocity01() const
{
    const float v = processor.apvts.getRawParameterValue("velocity")->load();
    const float pos = juce::jlimit(0.0f, 1.0f, (v - 1.0f) / 126.0f);
    switch ((int) processor.apvts.getRawParameterValue("curve")->load())
    {
        case 0: return std::pow(pos, 0.6f);  // Soft: easier to reach high velocities
        case 2: return std::pow(pos, 1.7f);  // Hard: leans quiet until you push
        default: return pos;                 // Linear
    }
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
}

void KeysEditor::paint(juce::Graphics& g)
{
    g.fillAll(okstudio::theme::background);
    g.setColour(okstudio::theme::panel);
    g.fillRect(getLocalBounds().removeFromTop(124));
}

void KeysEditor::resized()
{
    auto area = getLocalBounds().reduced(10);

    auto header = area.removeFromTop(104);
    title.setBounds(header.removeFromLeft(80).withTrimmedTop(30).withHeight(34));

    // Two rows of labelled controls. Each cell = a short label above its control.
    auto rowA = header.removeFromTop(48);
    auto rowB = header.removeFromTop(4).withHeight(0); // spacer only
    juce::ignoreUnused(rowB);
    auto rowTwo = header; // remaining ~48

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

    cell(rowTwo, 210, velocityLabel, velocitySlider);
    cell(rowTwo, 110, curveLabel, curveBox);
    cell(rowTwo, 70, channelLabel, channelBox);
    toggleCell(rowTwo, 90, sustainButton);
    toggleCell(rowTwo, 80, latchButton);
    toggleCell(rowTwo, 80, panicButton);
    if (updateButton.isVisible())
        updateButton.setBounds(rowTwo.removeFromLeft(150).withTrimmedTop(14));

    area.removeFromTop(8);
    keyboard.setBounds(area);
}
} // namespace keys
