#include "PluginEditor.h"
#include <okstudio/MouseOnly.h>
#include <okstudio/Scales.h>

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

    const char* surfaceNames[] = { "Keys", "Hex", "Pads", "Faders", "XY" };

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
    : juce::AudioProcessorEditor(p), processor(p),
      keyboard(p), hexTable(p), padGrid(p), faderBank(p), xyPad(p), chordPads(p)
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
    addCombo(polyphonyBox, polyphonyLabel, "Voices",
             { "Off", "1", "2", "3", "4", "5", "6", "7", "8" }, "polyphony", polyphonyAtt);

    styleLabel(octaveLabel, "Octave");
    addAndMakeVisible(octaveLabel);
    octaveSlider.setSliderStyle(juce::Slider::IncDecButtons);
    octaveSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 46, 26);
    octaveSlider.setRange(-5, 5, 1);
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

    // Humanize amounts: each note picks a random velocity in the [min, max] range (a
    // two-handle slider) and a micro-timing offset up to Timing ms. The range slider has
    // no APVTS attachment (two values), so it is synced to the params by hand.
    styleLabel(humanizeVelLabel, "Velocity");
    addAndMakeVisible(humanizeVelLabel);
    humanizeVelSlider.setSliderStyle(juce::Slider::TwoValueHorizontal);
    humanizeVelSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    humanizeVelSlider.setRange(1, 127, 1);
    humanizeVelSlider.setMinAndMaxValues(processor.apvts.getRawParameterValue("humanizeVelMin")->load(),
                                         processor.apvts.getRawParameterValue("humanizeVelMax")->load(),
                                         juce::dontSendNotification);
    humanizeVelSlider.onValueChange = [this]
    {
        const auto write = [this](const char* id, double value)
        {
            if (auto* param = processor.apvts.getParameter(id))
            {
                const float norm = param->convertTo0to1((float) value);
                if (std::abs(param->getValue() - norm) > 1.0e-6f)
                    param->setValueNotifyingHost(norm);
            }
        };
        write("humanizeVelMin", humanizeVelSlider.getMinValue());
        write("humanizeVelMax", humanizeVelSlider.getMaxValue());
    };
    addAndMakeVisible(humanizeVelSlider);

    styleLabel(humanizeTimeLabel, "Timing");
    addAndMakeVisible(humanizeTimeLabel);
    humanizeTimeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    humanizeTimeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 26);
    humanizeTimeSlider.setRange(0, 30, 1);
    humanizeTimeSlider.setTextValueSuffix(" ms");
    addAndMakeVisible(humanizeTimeSlider);
    humanizeTimeAtt = std::make_unique<SliderAtt>(processor.apvts, "humanizeTime", humanizeTimeSlider);

    // Chord-pad strum (Octavium "Drift"): spread a pad's note-ons over N ms, in a direction.
    styleLabel(chordStrumLabel, "Strum");
    addAndMakeVisible(chordStrumLabel);
    chordStrumSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    chordStrumSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 26);
    chordStrumSlider.setRange(0, 200, 1);
    chordStrumSlider.setTextValueSuffix(" ms");
    addAndMakeVisible(chordStrumSlider);
    chordStrumAtt = std::make_unique<SliderAtt>(processor.apvts, "chordStrum", chordStrumSlider);

    addCombo(chordStrumDirBox, chordStrumDirLabel, "Dir", { "Up", "Down", "Random" }, "chordStrumDir", chordStrumDirAtt);
    addCombo(layoutBox, layoutLabel, "Layout", { "Classic", "Performer" }, "uiLayout", layoutAtt);

    // Performance wheels, left of the keyboard. Transient (no params/persistence): Mod
    // holds its value (CC1); Pitch bend glides back to centre when you let go.
    modWheel.setSliderStyle(juce::Slider::LinearVertical);
    modWheel.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    modWheel.setRange(0, 127, 1);
    modWheel.setValue(0, juce::dontSendNotification);
    modWheel.setSliderSnapsToMousePosition(false); // relative drag; a stray click can't slam it
    modWheel.onValueChange = [this] { processor.sendCC(1, (int) modWheel.getValue()); };
    modWheel.setLookAndFeel(&wheelLnf);
    addAndMakeVisible(modWheel);
    styleLabel(modLabel, "Mod");
    modLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(modLabel);

    pitchWheel.setSliderStyle(juce::Slider::LinearVertical);
    pitchWheel.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pitchWheel.setRange(0, 16383, 1);
    pitchWheel.setValue(8192, juce::dontSendNotification); // centre
    pitchWheel.setSliderSnapsToMousePosition(false); // relative drag, like a real wheel
    pitchWheel.onValueChange = [this] { processor.sendPitchBend((int) pitchWheel.getValue()); };
    pitchWheel.onDragStart = [this] { pitchReturning = false; };
    pitchWheel.onDragEnd = [this] { pitchReturning = true; }; // eased return, in timerCallback
    pitchWheel.setLookAndFeel(&wheelLnf);
    addAndMakeVisible(pitchWheel);
    styleLabel(pitchLabel, "Pitch");
    pitchLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(pitchLabel);

    panicButton.onClick = [this]
    {
        processor.stopAllChordPads(); // else pads keep rendering active after the silence
        keyboard.panic();
        hexTable.panic();
        padGrid.panic();
        panicFlash = 1.0f; // blue flash, only on an explicit click (Octavium behaviour)
    };
    addAndMakeVisible(panicButton);

    // The playing surfaces and their tabs. Every surface stays constructed (a chord
    // latched on the hex grid keeps ringing while you ride the faders); the tabs only
    // choose what is visible.
    for (int i = 0; i < (int) surfaceButtons.size(); ++i)
    {
        auto& b = surfaceButtons[(size_t) i];
        b.setButtonText(surfaceNames[i]);
        b.setColour(juce::TextButton::buttonOnColourId, okstudio::theme::accent.withAlpha(0.85f));
        b.onClick = [this, i]
        {
            if (auto* param = processor.apvts.getParameter("surface"))
                param->setValueNotifyingHost(param->convertTo0to1((float) i));
        };
        addAndMakeVisible(b);
    }
    addAndMakeVisible(keyboard);
    addChildComponent(hexTable);
    addChildComponent(padGrid);
    addChildComponent(faderBank);
    addChildComponent(xyPad);

    // Chord-pad pages: four pages of sixteen (Octavium's 4x4 per page), so a session can
    // hold several keys' worth of chords without the strip shrinking below a comfortable
    // target.
    pagePrevButton.onClick = [this] { stepPadPage(-1); };
    pageNextButton.onClick = [this] { stepPadPage(1); };
    chordsButton.onClick = [this] { toggleGenPanel(); };
    chordsButton.setTooltip("Generate chords for this page, and find what could follow them.");
    for (auto* b : { &pagePrevButton, &pageNextButton, &chordsButton })
        addAndMakeVisible(*b);
    styleLabel(pageLabel, "1/4");
    pageLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(pageLabel);

    updateButton.setColour(juce::TextButton::buttonColourId, okstudio::theme::good.withAlpha(0.85f));
    addChildComponent(updateButton); // hidden until the updater finds a newer release

    const auto velocity = [this] { return processor.baseVelocity01(); };
    keyboard.getVelocity = velocity;
    hexTable.getVelocity = velocity;
    padGrid.getVelocity = velocity;
    addAndMakeVisible(chordPads);

    // Dropping a pad on the live card latches its notes back onto a note surface for
    // editing (Octavium's drag-to-edit). If a non-note surface is up, hop to the piano
    // first so the recalled chord is actually visible.
    chordPads.onRecall = [this](const std::vector<int>& notes)
    {
        int surf = surfaceIndex();
        if (surf != 0 && surf != 1)
        {
            if (auto* param = processor.apvts.getParameter("surface"))
                param->setValueNotifyingHost(param->convertTo0to1(0.0f));
            surf = 0;
        }
        if (surf == 1)
            hexTable.recallOutputNotes(notes);
        else
            keyboard.recallOutputNotes(notes);
    };

   #if ! (defined(KEYS_HOST) && KEYS_HOST)
    // Auto-update: check the pinned releases repo once, surface a button if newer.
    // Skipped when this editor is embedded inside Keys Host, which is a different
    // product and will get its own release channel.
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
   #endif

    setResizable(true, true);
    setResizeLimits(820, 480, 2200, 1200);
    setSize(960, 600);
    applySurfaceVisibility();
    startTimerHz(30);
}

KeysEditor::~KeysEditor()
{
    stopTimer();
    modWheel.setLookAndFeel(nullptr);
    pitchWheel.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void KeysEditor::WheelLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                                                    float sliderPos, float, float,
                                                    juce::Slider::SliderStyle, juce::Slider& s)
{
    // Groove the full component width, thumb as a chunky grab bar. No value fill —
    // hardware wheels don't show one, and it would read oddly on the centred pitch wheel.
    const auto groove = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h)
                            .reduced((float) w * 0.15f, 6.0f);
    const float corner = groove.getWidth() * 0.35f;
    g.setColour(s.findColour(juce::Slider::backgroundColourId));
    g.fillRoundedRectangle(groove, corner);
    g.setColour(s.findColour(juce::Slider::trackColourId));
    g.drawRoundedRectangle(groove, corner, 1.0f);

    const float thumbH = 18.0f;
    const float thumbY = juce::jlimit(groove.getY(), groove.getBottom() - thumbH, sliderPos - thumbH * 0.5f);
    g.setColour(s.findColour(juce::Slider::thumbColourId));
    g.fillRoundedRectangle(groove.expanded(3.0f, 0.0f).getX(), thumbY,
                           groove.getWidth() + 6.0f, thumbH, 4.0f);
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

int KeysEditor::surfaceIndex() const
{
    return juce::jlimit(0, 4, (int) processor.apvts.getRawParameterValue("surface")->load());
}

int KeysEditor::layoutIndex() const
{
    return juce::jlimit(0, 1, (int) processor.apvts.getRawParameterValue("uiLayout")->load());
}

void KeysEditor::applySurfaceVisibility()
{
    const bool performer = layoutIndex() == 1;
    int surf = surfaceIndex();
    // In Performer the faders and XY are always up in the strip, so those tabs are
    // gone; a session saved on one of them falls back to the piano.
    if (performer && (surf == 3 || surf == 4))
        surf = 0;
    keyboard.setVisible(surf == 0);
    hexTable.setVisible(surf == 1);
    padGrid.setVisible(surf == 2);
    faderBank.setVisible(performer || surf == 3);
    xyPad.setVisible(performer || surf == 4);
    // Wheels ride with the note surfaces only, as in Octavium (piano + harmonic table).
    const bool wheels = surf == 0 || surf == 1;
    modWheel.setVisible(wheels);
    modLabel.setVisible(wheels);
    pitchWheel.setVisible(wheels);
    pitchLabel.setVisible(wheels);
    for (int i = 0; i < (int) surfaceButtons.size(); ++i)
    {
        surfaceButtons[(size_t) i].setVisible(! (performer && i >= 3));
        surfaceButtons[(size_t) i].setToggleState(i == surf, juce::dontSendNotification);
    }
}

void KeysEditor::stepPadPage(int delta)
{
    if (auto* param = processor.apvts.getParameter("padPage"))
    {
        const int next = juce::jlimit(0, KeysProcessor::numPadPages - 1, processor.padPage() + delta);
        param->setValueNotifyingHost(param->convertTo0to1((float) next));
    }
}

void KeysEditor::toggleGenPanel()
{
    if (genPanel != nullptr)
    {
        genPanel.reset();
        return;
    }
    // The generator carries far more controls than the player, and every one of them has to
    // stay at a full-size target. Grow the editor to fit rather than shrink the targets
    // (unless embedded, where the parent owns geometry and the overlay makes do).
    if (! embedded)
        setSize(juce::jmax(getWidth(), 1010), juce::jmax(getHeight(), 640));

    genPanel = std::make_unique<ChordGenPanel>(processor);
    genPanel->onClose = [this] { genPanel.reset(); };
    // An emotion preset moves Root and Scale as well as the generator's own key; the
    // combos are APVTS-attached and follow on their own, so this is just the repaint.
    genPanel->onKeyChanged = [this] { repaint(); };
    addAndMakeVisible(*genPanel);
    genPanel->setBounds(getLocalBounds());
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

    // Push shared performance config into every note surface; each ignores what it
    // doesn't use (the pad grid reads no scale, but keeping one loop keeps them honest).
    NoteSurface* noteSurfaces[] = { &keyboard, &hexTable, &padGrid };
    const bool sus = apvts.getRawParameterValue("sustain")->load() > 0.5f;
    for (auto* s : noteSurfaces)
    {
        s->setScaleLock(apvts.getRawParameterValue("scaleLock")->load() > 0.5f,
                        (int) apvts.getRawParameterValue("root")->load(),
                        (int) apvts.getRawParameterValue("scale")->load());
        s->setSustain(sus);
        s->setLatch(apvts.getRawParameterValue("latch")->load() > 0.5f);
        s->setPolyphony((int) apvts.getRawParameterValue("polyphony")->load()); // 0 = unlimited
    }

    // Lifting the sustain pedal releases any pad chords left ringing by it.
    if (! sus && lastSustain)
        processor.stopAllChordPads();
    lastSustain = sus;

    // Changing MIDI channel while notes sound would strand them on the old channel
    // (note-off goes to the new one), so panic on any channel change. The Pad Grid has
    // its own channel and its own equivalent below.
    const int ch = (int) apvts.getRawParameterValue("channel")->load();
    if (ch != lastChannel)
    {
        if (lastChannel >= 0)
        {
            processor.stopAllChordPads();
            keyboard.panic();
            hexTable.panic();
        }
        lastChannel = ch;
    }
    const int padCh = (int) apvts.getRawParameterValue("padChannel")->load();
    if (padCh != lastPadChannel)
    {
        if (lastPadChannel >= 0)
            padGrid.panic();
        lastPadChannel = padCh;
    }

    // Switching surfaces silences the one you left, so nothing rings on unseen views;
    // latched chords you *want* to keep belong to the chord pads, which live below the
    // tabs and keep sounding regardless.
    const int surf = surfaceIndex();
    if (surf != lastSurface)
    {
        if (lastSurface == 0)      keyboard.panic();
        else if (lastSurface == 1) hexTable.panic();
        else if (lastSurface == 2) padGrid.panic();
        lastSurface = surf;
        applySurfaceVisibility();
    }

    if (const int lay = layoutIndex(); lay != lastLayout)
    {
        lastLayout = lay;
        applySurfaceVisibility();
        // Performer needs room for the control strip. A top-level editor grows
        // itself; an embedded one leaves geometry to Keys Host.
        if (! embedded && lay == 1)
            setSize(juce::jmax(getWidth(), 1010), juce::jmax(getHeight(), 720));
        resized();
    }

    // Grey the humanize amounts out when Humanize is off, so their state reads at a glance.
    const bool hum = apvts.getRawParameterValue("humanize")->load() > 0.5f;
    humanizeVelSlider.setEnabled(hum);
    humanizeTimeSlider.setEnabled(hum);
    humanizeVelLabel.setEnabled(hum);
    humanizeTimeLabel.setEnabled(hum);

    // Keep the two-handle velocity range synced to its params and show the numbers.
    const int vmin = (int) apvts.getRawParameterValue("humanizeVelMin")->load();
    const int vmax = (int) apvts.getRawParameterValue("humanizeVelMax")->load();
    humanizeVelSlider.setMinAndMaxValues(vmin, vmax, juce::dontSendNotification);
    humanizeVelLabel.setText("Velocity  " + juce::String(vmin) + "-" + juce::String(vmax),
                             juce::dontSendNotification);

    // Pad page: label and the ends of the range.
    const int page = processor.padPage();
    pageLabel.setText(juce::String(page + 1) + "/" + juce::String(KeysProcessor::numPadPages),
                      juce::dontSendNotification);
    pagePrevButton.setEnabled(page > 0);
    pageNextButton.setEnabled(page < KeysProcessor::numPadPages - 1);

    // Pitch wheel gliding home after release: exponential ease, ~160 ms to centre
    // (Octavium animates this; hardware springs, it doesn't snap).
    if (pitchReturning)
    {
        const double v = pitchWheel.getValue();
        const double next = v + (8192.0 - v) * 0.45;
        if (std::abs(next - 8192.0) < 24.0)
        {
            pitchWheel.setValue(8192, juce::sendNotificationSync);
            pitchReturning = false;
        }
        else
        {
            pitchWheel.setValue(next, juce::sendNotificationSync);
        }
    }

    // Panic flash: decay the blue behind All Off (Octavium fires this on click only, never
    // on the internal clears, so it always means "you just did that").
    if (panicFlash > 0.0f)
    {
        panicFlash = juce::jmax(0.0f, panicFlash - 0.08f); // ~400 ms at 30 Hz
        panicButton.setColour(juce::TextButton::buttonColourId,
                              neutralControl.interpolatedWith(okstudio::theme::accent, panicFlash));
    }

    // Keep the CC assignment labels current (they are parameters; automation or another
    // editor instance can move them).
    faderBank.refreshAssignments();
    xyPad.refreshAssignments();

    // Feed the pads the chord sounding on the active note surface (drives the live card
    // and capture). The pad grid is drums on its own channel, so it doesn't feed the card.
    if (surf == 0)
        chordPads.setCurrentChord(keyboard.soundingOutputNotes());
    else if (surf == 1)
        chordPads.setCurrentChord(hexTable.soundingOutputNotes());
    else
        chordPads.setCurrentChord({});
    chordPads.repaint();
}

void KeysEditor::paint(juce::Graphics& g)
{
    g.fillAll(neutralBg);
    g.setColour(neutralPanel);
    g.fillRect(getLocalBounds().removeFromTop(168)); // behind the three control rows
}

void KeysEditor::resized()
{
    if (genPanel != nullptr)
        genPanel->setBounds(getLocalBounds()); // the overlay always covers the whole editor

    auto area = getLocalBounds().reduced(10);

    // Top: three control rows. Bottom: surface tabs, then the playing area takes all
    // remaining height (the piano anchors its keys to the bottom of it, so extra height
    // reads as instrument body; the grids and faders use the room).
    const int rowH = 46;
    auto header = area.removeFromTop(14 + rowH * 3 + 6);

    // Performer layout: the faders and XY pad live in a permanent control strip
    // between the header and the tabs, hardware-controller style.
    const bool performer = layoutIndex() == 1;
    chordPads.setGridLayout(performer);
    juce::Rectangle<int> strip;
    if (performer)
    {
        strip = area.removeFromTop(156);
        area.removeFromTop(4);
    }

    auto tabs = area.removeFromTop(34);
    area.removeFromTop(4);

    // Chord pads: two rows of eight between the tabs and the playing surface. In
    // Performer they move to a 4x4 column beside the keyboard instead.
    juce::Rectangle<int> padRows;
    if (! performer)
    {
        padRows = area.removeFromTop(96);
        area.removeFromTop(6);
    }
    auto play = area;

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
    cell(rowA, 90, polyphonyLabel, polyphonyBox);
    if (updateButton.isVisible())
        updateButton.setBounds(rowA.removeFromLeft(150).withTrimmedTop(14));

    cell(rowB, 210, velocityLabel, velocitySlider);
    cell(rowB, 110, curveLabel, curveBox);
    cell(rowB, 70, channelLabel, channelBox);
    toggleCell(rowB, 100, sustainButton);
    toggleCell(rowB, 90, latchButton);
    if (! performer)
        toggleCell(rowB, 90, panicButton); // Performer parks All Off by the pads column

    toggleCell(rowC, 96, humanizeButton);
    cell(rowC, 208, humanizeVelLabel, humanizeVelSlider);
    cell(rowC, 130, humanizeTimeLabel, humanizeTimeSlider);
    cell(rowC, 150, chordStrumLabel, chordStrumSlider);
    cell(rowC, 100, chordStrumDirLabel, chordStrumDirBox);
    cell(rowC, 110, layoutLabel, layoutBox);

    // Surface tabs on the left of their row; the chord-pad page/generator controls share it.
    for (auto& b : surfaceButtons)
    {
        b.setBounds(tabs.removeFromLeft(72));
        tabs.removeFromLeft(4);
    }
    tabs.removeFromLeft(12);
    chordExclusiveButton.setBounds(tabs.removeFromLeft(112).withSizeKeepingCentre(110, 26));
    tabs.removeFromLeft(4);
    chordsButton.setBounds(tabs.removeFromLeft(66));
    tabs.removeFromLeft(4);
    pagePrevButton.setBounds(tabs.removeFromLeft(34));
    pageLabel.setBounds(tabs.removeFromLeft(28));
    pageNextButton.setBounds(tabs.removeFromLeft(34));

    if (performer)
    {
        auto xyBox = strip.removeFromRight(230);
        strip.removeFromRight(8);
        faderBank.setBounds(strip);
        xyPad.setBounds(xyBox);
    }
    else
    {
        chordPads.setBounds(padRows);
    }

    // Wheels sit left of the note surfaces; other surfaces take the full width. In
    // Performer the chord pads (4x4, card on top, All Off underneath) come first.
    const int surf = surfaceIndex();
    auto playArea = play;
    if (performer)
    {
        auto padsCol = playArea.removeFromLeft(250);
        playArea.removeFromLeft(8);
        panicButton.setBounds(padsCol.removeFromBottom(34));
        padsCol.removeFromBottom(6);
        chordPads.setBounds(padsCol);
    }
    if (surf == 0 || surf == 1)
    {
        auto wheels = playArea.removeFromLeft(112);
        playArea.removeFromLeft(6);
        auto modCol = wheels.removeFromLeft(54);
        wheels.removeFromLeft(4);
        auto pitchCol = wheels;
        modLabel.setBounds(modCol.removeFromBottom(15));
        modWheel.setBounds(modCol.reduced(2, 2));
        pitchLabel.setBounds(pitchCol.removeFromBottom(15));
        pitchWheel.setBounds(pitchCol.reduced(2, 2));
    }

    keyboard.setBounds(playArea);
    hexTable.setBounds(playArea);
    padGrid.setBounds(playArea);
    if (! performer)
    {
        faderBank.setBounds(play);
        xyPad.setBounds(play);
    }
}
} // namespace keys
