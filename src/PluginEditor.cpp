#include "PluginEditor.h"
#include "Chords.h"
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

    // Micro-caps section labels, the skin's typographic voice (see KeysLookAndFeel.h).
    void styleLabel(juce::Label& l, const juce::String& text)
    {
        l.setText(text.toUpperCase(), juce::dontSendNotification);
        l.setFont(skin::micro(10.0f));
        l.setColour(juce::Label::textColourId, skin::textDim);
    }
} // namespace

KeysEditor::KeysEditor(KeysProcessor& p)
    : juce::AudioProcessorEditor(p), processor(p),
      keyboard(p), knobBank(p), chordPads(p)
{
    setLookAndFeel(&lnf); // the Keys "Obsidian" skin; palette lives in KeysLookAndFeel.h
    okstudio::ui::makeMouseOnly(*this);

    title.setText("KEYS", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions("Segoe UI", 24.0f, juce::Font::bold))
                      .withExtraKerningFactor(0.10f));
    title.setColour(juce::Label::textColourId, skin::text);
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
        panicFlash = 1.0f; // blue flash, only on an explicit click (Octavium behaviour)
    };
    addAndMakeVisible(panicButton);

    addAndMakeVisible(keyboard);
    addAndMakeVisible(knobBank);

    // Chord-pad pages: four pages of sixteen (Octavium's 4x4 per page), so a session can
    // hold several keys' worth of chords without the strip shrinking below a comfortable
    // target.
    pagePrevButton.onClick = [this] { stepPadPage(-1); };
    pageNextButton.onClick = [this] { stepPadPage(1); };
    chordsButton.onClick = [this] { toggleGenPanel(); };
    chordsButton.setTooltip("Generate chords for this page, and find what could follow them.");
    arpButton.onClick = [this] { toggleArpPanel(); };
    arpButton.setTooltip("Arpeggiator: per-step lanes for note, octave, velocity, gate, ratchet, probability.");
    for (auto* b : { &pagePrevButton, &pageNextButton, &chordsButton, &arpButton })
        addAndMakeVisible(*b);
    styleLabel(pageLabel, "1/4");
    pageLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(pageLabel);

    updateButton.setColour(juce::TextButton::buttonColourId, okstudio::theme::good.withAlpha(0.85f));
    addChildComponent(updateButton); // hidden until the updater finds a newer release

    const auto velocity = [this] { return processor.baseVelocity01(); };
    keyboard.getVelocity = velocity;
    addAndMakeVisible(chordPads);

    // Dropping a pad on the live card latches its notes back onto the keyboard for
    // editing (Octavium's drag-to-edit).
    chordPads.onRecall = [this](const std::vector<int>& notes) { keyboard.recallOutputNotes(notes); };

    // Right-click "Edit on keyboard": the pad's notes latch onto the piano and every
    // latch change writes straight back to the pad, name re-detected live.
    chordPads.onEditToggle = [this](int slot) { toggleEditPad(slot); };

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
    setResizeLimits(820, 560, 2200, 1200);
    setSize(960, 660);

    // Children configured before they were parented (slider textboxes especially)
    // baked colours from the default LookAndFeel; re-resolve everything under ours.
    sendLookAndFeelChange();
    startTimerHz(30);
}

KeysEditor::~KeysEditor()
{
    stopTimer();
    if (styledWindow != nullptr)
        styledWindow->setLookAndFeel(nullptr);
    modWheel.setLookAndFeel(nullptr);
    pitchWheel.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void KeysEditor::parentHierarchyChanged()
{
    // Standalone only: the JUCE wrapper window above this editor draws its own
    // title bar with the process-default LookAndFeel, which clashes with the skin.
    // Point it at ours (and restore in the destructor). In a DAW the host owns the
    // window chrome and no DocumentWindow ever appears in the parent chain.
    //
    // Deferred a message-loop turn: this fires while the wrapper is still
    // assembling its hierarchy, and restyling the window mid-construction breaks
    // its content sizing (it opens at the minimum size).
    if (embedded || ! juce::JUCEApplicationBase::isStandaloneApp() || styledWindow != nullptr)
        return;
    juce::Component::SafePointer<KeysEditor> safe(this);
    juce::MessageManager::callAsync([safe]
    {
        auto* e = safe.getComponent();
        if (e == nullptr || e->styledWindow != nullptr)
            return;
        if (auto* window = dynamic_cast<juce::DocumentWindow*>(e->getTopLevelComponent()))
        {
            e->styledWindow = window;
            window->setLookAndFeel(&e->lnf);
            window->setTitleBarHeight(38); // window buttons become 38 px targets (mouse-only floor is 34)
        }
    });
}

void KeysEditor::WheelLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                                                    float sliderPos, float, float,
                                                    juce::Slider::SliderStyle, juce::Slider& s)
{
    // Hardware-wheel look: a deep ridged groove and a machined grab bar with an
    // accent LED stripe. No value fill — hardware wheels don't show one, and it
    // would read oddly on the centred pitch wheel.
    const auto groove = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h)
                            .reduced((float) w * 0.14f, 5.0f);
    const float corner = groove.getWidth() * 0.28f;

    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(groove.expanded(0.5f), corner + 0.5f, 1.0f);
    g.setGradientFill({ juce::Colour(0xff0c0e11), 0.0f, groove.getY(),
                        juce::Colour(0xff16191d), 0.0f, groove.getBottom(), false });
    g.fillRoundedRectangle(groove, corner);

    {
        // Rubber ridges, clipped to the groove.
        juce::Graphics::ScopedSaveState clip(g);
        juce::Path grooveClip;
        grooveClip.addRoundedRectangle(groove, corner);
        g.reduceClipRegion(grooveClip);
        g.setColour(juce::Colours::black.withAlpha(0.22f));
        for (float ry = groove.getY() + 4.0f; ry < groove.getBottom() - 3.0f; ry += 6.0f)
            g.fillRect(groove.getX() + 3.0f, ry, groove.getWidth() - 6.0f, 1.0f);
    }

    // Centre detent marker on the sprung pitch wheel (identified by its bend range).
    if (s.getMaximum() > 10000.0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRect(groove.getX() + 2.0f, groove.getCentreY() - 1.0f, groove.getWidth() - 4.0f, 2.0f);
    }

    const float thumbH = 20.0f;
    const float thumbY = juce::jlimit(groove.getY(), groove.getBottom() - thumbH, sliderPos - thumbH * 0.5f);
    const auto thumb = juce::Rectangle<float>(groove.getX() - 3.0f, thumbY, groove.getWidth() + 6.0f, thumbH);

    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(thumb.translated(0.0f, 2.0f), 4.0f);
    g.setGradientFill({ juce::Colour(0xff3f444c), 0.0f, thumb.getY(),
                        juce::Colour(0xff22252a), 0.0f, thumb.getBottom(), false });
    g.fillRoundedRectangle(thumb, 4.0f);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.fillRoundedRectangle(thumb.withHeight(1.5f).reduced(3.0f, 0.0f), 0.75f);

    const auto led = juce::Rectangle<float>(thumb.getX() + 5.0f, thumb.getCentreY() - 1.25f,
                                            thumb.getWidth() - 10.0f, 2.5f);
    g.setColour(skin::accent.withAlpha(0.35f));
    g.fillRoundedRectangle(led.expanded(2.0f, 2.0f), 3.0f);
    g.setColour(skin::accentHot);
    g.fillRoundedRectangle(led, 1.25f);
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
    // The door buttons light up while their overlay is open (skin toggle look).
    const auto syncDoors = [this]
    {
        chordsButton.setToggleState(genPanel != nullptr, juce::dontSendNotification);
        arpButton.setToggleState(arpPanel != nullptr, juce::dontSendNotification);
    };
    if (genPanel != nullptr)
    {
        genPanel.reset();
        syncDoors();
        return;
    }
    if (arpPanel != nullptr)
        arpPanel.reset(); // only one overlay at a time

    // The generator carries far more controls than the player, and every one of them has to
    // stay at a full-size target. Grow the editor to fit rather than shrink the targets
    // (unless embedded, where the parent owns geometry and the overlay makes do).
    if (! embedded)
        setSize(juce::jmax(getWidth(), 1010), juce::jmax(getHeight(), 640));

    genPanel = std::make_unique<ChordGenPanel>(processor);
    genPanel->onClose = [this, syncDoors] { genPanel.reset(); syncDoors(); };
    addAndMakeVisible(*genPanel);
    genPanel->setBounds(getLocalBounds());
    genPanel->sendLookAndFeelChange(); // its controls were configured pre-parenting
    syncDoors();
}

void KeysEditor::toggleArpPanel()
{
    const auto syncDoors = [this]
    {
        chordsButton.setToggleState(genPanel != nullptr, juce::dontSendNotification);
        arpButton.setToggleState(arpPanel != nullptr, juce::dontSendNotification);
    };
    if (arpPanel != nullptr)
    {
        arpPanel.reset();
        syncDoors();
        return;
    }
    if (genPanel != nullptr)
        genPanel.reset(); // only one overlay at a time

    // Six lane grids plus globals and pattern rows need more room than the player;
    // grow to fit, same rule as the chord generator (never shrink when embedded).
    if (! embedded)
        setSize(juce::jmax(getWidth(), 1010), juce::jmax(getHeight(), 780));

    arpPanel = std::make_unique<ArpPanel>(processor);
    arpPanel->onClose = [this, syncDoors] { arpPanel.reset(); syncDoors(); };
    addAndMakeVisible(*arpPanel);
    arpPanel->setBounds(getLocalBounds());
    arpPanel->sendLookAndFeelChange(); // its controls were configured pre-parenting
    syncDoors();
}

void KeysEditor::toggleEditPad(int slot)
{
    if (editingPad == slot)
    {
        endPadEdit();
        return;
    }
    editingPad = slot;
    lastEditNotes = processor.chordPad(slot).notes;
    keyboard.panic(); // clean slate, so the latched set mirrors exactly this pad
    keyboard.recallOutputNotes(lastEditNotes);
    chordPads.setEditingSlot(slot);
}

void KeysEditor::endPadEdit()
{
    if (editingPad < 0)
        return;
    editingPad = -1;
    lastEditNotes.clear();
    chordPads.setEditingSlot(-1);
    keyboard.panic(); // the editing chord stops ringing; the pad keeps what it got
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

    // Push shared performance config into the playing surface.
    const bool sus = apvts.getRawParameterValue("sustain")->load() > 0.5f;
    keyboard.setScaleLock(apvts.getRawParameterValue("scaleLock")->load() > 0.5f,
                          (int) apvts.getRawParameterValue("root")->load(),
                          (int) apvts.getRawParameterValue("scale")->load());
    keyboard.setSustain(sus);
    // While a pad is linked for editing, clicks must toggle notes, so Latch behaviour
    // is forced on regardless of the parameter.
    keyboard.setLatch(apvts.getRawParameterValue("latch")->load() > 0.5f || editingPad >= 0);
    keyboard.setPolyphony((int) apvts.getRawParameterValue("polyphony")->load()); // 0 = unlimited

    // Lifting the sustain pedal releases any pad chords left ringing by it.
    if (! sus && lastSustain)
        processor.stopAllChordPads();
    lastSustain = sus;

    // Changing MIDI channel while notes sound would strand them on the old channel
    // (note-off goes to the new one), so panic on any channel change.
    const int ch = (int) apvts.getRawParameterValue("channel")->load();
    if (ch != lastChannel)
    {
        if (lastChannel >= 0)
        {
            processor.stopAllChordPads();
            keyboard.panic();
        }
        lastChannel = ch;
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
    humanizeVelLabel.setText("VELOCITY  " + juce::String(vmin) + "-" + juce::String(vmax),
                             juce::dontSendNotification);

    // Pad page: label and the ends of the range.
    const int page = processor.padPage();
    pageLabel.setText(juce::String(page + 1) + "/" + juce::String(KeysProcessor::numPadPages),
                      juce::dontSendNotification);
    pagePrevButton.setEnabled(page > 0);
    pageNextButton.setEnabled(page < KeysProcessor::numPadPages - 1);

    // Keyboard-edit link: the edited pad follows the keyboard's sounding set. An
    // all-notes-removed state is not written (Clear is the explicit wipe), and
    // flipping to another page ends the edit.
    if (editingPad >= 0)
    {
        const int offset = processor.padPageOffset();
        if (editingPad < offset || editingPad >= offset + KeysProcessor::padsPerPage)
        {
            endPadEdit();
        }
        else
        {
            const auto now = keyboard.soundingOutputNotes();
            if (now != lastEditNotes)
            {
                lastEditNotes = now;
                if (! now.empty())
                    processor.setChordPad(editingPad, now, chords::detect(now));
            }
        }
    }

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
                              skin::control.interpolatedWith(skin::accent, panicFlash));
    }

    // Keep the CC assignment labels current (they are parameters; automation or another
    // editor instance can move them).
    knobBank.refreshAssignments();

    // Feed the pads the chord sounding on the keyboard (drives the live card and capture).
    chordPads.setCurrentChord(keyboard.soundingOutputNotes());
    chordPads.repaint();
}

void KeysEditor::paint(juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    g.setGradientFill({ skin::bgTop, 0.0f, 0.0f, skin::bgBot, 0.0f, full.getBottom(), false });
    g.fillRect(full);

    // Header band behind the three control rows, seated on a shadow + catch-light pair.
    const auto band = full.withHeight(168.0f);
    g.setGradientFill({ skin::headerTop, 0.0f, 0.0f, skin::headerBot, 0.0f, band.getBottom(), false });
    g.fillRect(band);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRect(0.0f, band.getBottom(), full.getWidth(), 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.04f));
    g.fillRect(0.0f, band.getBottom() + 1.0f, full.getWidth(), 1.0f);

    // Wordmark caption under the title.
    g.setColour(skin::accent.withAlpha(0.85f));
    g.setFont(skin::micro(9.0f).withExtraKerningFactor(0.32f));
    g.drawText("OK STUDIO", titleCaption, juce::Justification::centredLeft);

    // The performance module (knobs + pads) floats as one raised panel.
    const auto perf = knobBank.getBounds().getUnion(chordPads.getBounds()).toFloat().expanded(4.0f, 5.0f);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.drawRoundedRectangle(perf.expanded(0.5f), skin::panelRadius + 0.5f, 1.0f);
    g.setColour(skin::panel);
    g.fillRoundedRectangle(perf, skin::panelRadius);
    g.setColour(juce::Colours::white.withAlpha(0.045f));
    g.fillRoundedRectangle(perf.withHeight(1.5f).reduced(skin::panelRadius, 0.0f), 0.75f);
}

void KeysEditor::resized()
{
    if (genPanel != nullptr)
        genPanel->setBounds(getLocalBounds()); // the overlay always covers the whole editor
    if (arpPanel != nullptr)
        arpPanel->setBounds(getLocalBounds());

    auto area = getLocalBounds().reduced(10);

    // Top to bottom: three control rows, a tool row (chord-pad transport + the
    // generator/arp doors), the knob row, the chord-pad strip, then the playing
    // surface takes all remaining height (the piano anchors its keys to the bottom
    // of it, so extra height reads as instrument body).
    const int rowH = 46;
    auto header = area.removeFromTop(14 + rowH * 3 + 6);

    auto toolRow = area.removeFromTop(34);
    area.removeFromTop(4);

    auto knobRow = area.removeFromTop(110);
    area.removeFromTop(6);

    auto padRows = area.removeFromTop(96);
    area.removeFromTop(6);

    auto play = area;

    {
        // Title + wordmark caption, centred as a pair in the header band.
        auto titleCol = header.removeFromLeft(84);
        const int pairTop = titleCol.getY() + (titleCol.getHeight() - 48) / 2;
        title.setBounds(titleCol.withY(pairTop).withHeight(34));
        titleCaption = { title.getX() + 2, title.getBottom() - 2, 80, 12 };
    }
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
    toggleCell(rowB, 90, panicButton);

    toggleCell(rowC, 96, humanizeButton);
    cell(rowC, 208, humanizeVelLabel, humanizeVelSlider);
    cell(rowC, 130, humanizeTimeLabel, humanizeTimeSlider);
    cell(rowC, 150, chordStrumLabel, chordStrumSlider);
    cell(rowC, 100, chordStrumDirLabel, chordStrumDirBox);

    // Tool row: chord-pad exclusivity, the generator/arp doors, and page navigation.
    chordExclusiveButton.setBounds(toolRow.removeFromLeft(112).withSizeKeepingCentre(110, 26));
    toolRow.removeFromLeft(4);
    chordsButton.setBounds(toolRow.removeFromLeft(66));
    toolRow.removeFromLeft(4);
    arpButton.setBounds(toolRow.removeFromLeft(64));
    toolRow.removeFromLeft(4);
    pagePrevButton.setBounds(toolRow.removeFromLeft(34));
    pageLabel.setBounds(toolRow.removeFromLeft(28));
    pageNextButton.setBounds(toolRow.removeFromLeft(34));

    knobBank.setBounds(knobRow);
    chordPads.setBounds(padRows);

    // Wheels sit left of the keyboard; the keyboard fills the rest.
    auto playArea = play;
    auto wheels = playArea.removeFromLeft(112);
    playArea.removeFromLeft(6);
    auto modCol = wheels.removeFromLeft(54);
    wheels.removeFromLeft(4);
    auto pitchCol = wheels;
    modLabel.setBounds(modCol.removeFromBottom(15));
    modWheel.setBounds(modCol.reduced(2, 2));
    pitchLabel.setBounds(pitchCol.removeFromBottom(15));
    pitchWheel.setBounds(pitchCol.reduced(2, 2));

    keyboard.setBounds(playArea);
}
} // namespace keys
