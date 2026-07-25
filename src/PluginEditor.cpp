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

    // Fixed heights of the editor's bands, shared by idealHeight() and resized() so the
    // window the folds ask for and the layout they get can never drift apart.
    constexpr int rowH = 46;                          // one row of header controls
    constexpr int headerH = 14 + rowH * 2 + 6;        // both of them, plus label lead-in
    constexpr int viewBarH = 34;                      // centre-view tabs + pad transport
    constexpr int knobRowH = 110;
    constexpr int padRowH = 96;
    constexpr int dockedKeybedH = 212;                // 185 px of key plus a little body

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
    addCombo(polyphonyBox, polyphonyLabel, "Voices",
             { "Off", "1", "2", "3", "4", "5", "6", "7", "8" }, "polyphony", polyphonyAtt);

    styleLabel(octaveLabel, "Octave");
    addAndMakeVisible(octaveLabel);
    octaveSlider.setSliderStyle(juce::Slider::IncDecButtons);
    octaveSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 46, 26);
    octaveSlider.setRange(-5, 5, 1);
    addAndMakeVisible(octaveSlider);
    octaveAtt = std::make_unique<SliderAtt>(processor.apvts, "octave", octaveSlider);
    for (auto* b : { &scaleLockButton, &sustainButton, &humanizeButton, &chordExclusiveButton })
        addAndMakeVisible(*b);
    scaleLockAtt = std::make_unique<ButtonAtt>(processor.apvts, "scaleLock", scaleLockButton);
    sustainAtt = std::make_unique<ButtonAtt>(processor.apvts, "sustain", sustainButton);
    humanizeAtt = std::make_unique<ButtonAtt>(processor.apvts, "humanize", humanizeButton);
    chordExclusiveAtt = std::make_unique<ButtonAtt>(processor.apvts, "chordExclusive", chordExclusiveButton);

    // Humanize amounts: each note picks a random velocity in the [min, max] range (a
    // two-handle slider) and a micro-timing offset up to Timing ms. The range slider has
    // no APVTS attachment (two values), so it is synced to the params by hand.
    styleLabel(humanizeVelLabel, "Velocity");
    addAndMakeVisible(humanizeVelLabel);
    humanizeVelSlider.setRange(1, 127, 1); // style/textbox are RangeSlider's own
    humanizeVelSlider.setTooltip("Each note takes a random velocity in this range. "
                                 "Drag an end to resize it, or the middle to move it.");
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
    keybedHolder.addAndMakeVisible(modWheel);
    styleLabel(modLabel, "Mod");
    modLabel.setJustificationType(juce::Justification::centred);
    keybedHolder.addAndMakeVisible(modLabel);

    pitchWheel.setSliderStyle(juce::Slider::LinearVertical);
    pitchWheel.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pitchWheel.setRange(0, 16383, 1);
    pitchWheel.setValue(8192, juce::dontSendNotification); // centre
    pitchWheel.setSliderSnapsToMousePosition(false); // relative drag, like a real wheel
    pitchWheel.onValueChange = [this] { processor.sendPitchBend((int) pitchWheel.getValue()); };
    pitchWheel.onDragStart = [this] { pitchReturning = false; };
    pitchWheel.onDragEnd = [this] { pitchReturning = true; }; // eased return, in timerCallback
    pitchWheel.setLookAndFeel(&wheelLnf);
    keybedHolder.addAndMakeVisible(pitchWheel);
    styleLabel(pitchLabel, "Pitch");
    pitchLabel.setJustificationType(juce::Justification::centred);
    keybedHolder.addAndMakeVisible(pitchLabel);

    panicButton.onClick = [this]
    {
        processor.stopAllChordPads(); // else pads keep rendering active after the silence
        keyboard.panic();
        panicFlash = 1.0f; // blue flash, only on an explicit click (Octavium behaviour)
    };
    addAndMakeVisible(panicButton);

    keybedHolder.addAndMakeVisible(keyboard);
    keybedHolder.layout = [this] { layoutKeybed(); };
    addAndMakeVisible(keybedHolder);
    addAndMakeVisible(knobBank);

    // Chord-pad pages: four pages of sixteen (Octavium's 4x4 per page), so a session can
    // hold several keys' worth of chords without the strip shrinking below a comfortable
    // target.
    pagePrevButton.onClick = [this] { stepPadPage(-1); };
    pageNextButton.onClick = [this] { stepPadPage(1); };

    // Centre-view tabs. Each picks a view; the section's own chevron folds it away, the
    // same as Controls and Keyboard. The tabs stay visible while it is folded, so picking
    // one both unfolds and switches - otherwise a folded centre would be a dead end.
    const auto tab = [this](juce::TextButton& b, int view, const juce::String& tip)
    {
        b.setClickingTogglesState(false); // setCentreView owns the lit state
        b.setTooltip(tip);
        b.onClick = [this, view] { setCentreView(view); };
        addAndMakeVisible(b);
    };
    tab(performButton, viewPerform, "Knobs and chord pads.");
    tab(chordsButton, viewChords, "Generate chords for this page.");
    tab(arpButton, viewArp, "Arpeggiator: per-step lanes.");

    for (auto* b : { &pagePrevButton, &pageNextButton })
        addAndMakeVisible(*b);
    styleLabel(pageLabel, "1/4");
    pageLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(pageLabel);

    // Section folds. The bars carry their own toggle state; the sub-section chips
    // (Knobs / Pads / Wheels) are plain toggles on the bar they belong to.
    auto& lay = processor.layout;
    controlsBar.onClick = [this] { processor.layout.controls = controlsBar.getToggleState(); applyLayout(); };
    keyboardBar.onClick = [this] { processor.layout.keyboard = keyboardBar.getToggleState(); applyLayout(); };
    centreBar.onClick = [this]
    {
        processor.layout.centre = centreBar.getToggleState();
        refreshCentrePanels(); // folded away, the panel it was showing is destroyed
        applyLayout();
    };
    addAndMakeVisible(controlsBar);
    addAndMakeVisible(centreBar);
    addAndMakeVisible(keyboardBar);

    themeButton.setTooltip("Colour this instance, to tell it from Keys on other tracks.");
    themeButton.setTitle("Theme");
    themeButton.onClick = [this] { showThemeMenu(); };
    addAndMakeVisible(themeButton);

    // The detached keyboard's own Size selector (see the member declaration for why).
    detachedSizeBox.addItemList(sizeItems(), 1);
    detachedSizeBox.setTitle("Size");
    detachedSizeBox.setTooltip("How many keys the keybed shows.");
    detachedSizeAtt = std::make_unique<ComboAtt>(processor.apvts, "size", detachedSizeBox);

    const auto chip = [this](juce::TextButton& b, bool& flag, const juce::String& tip)
    {
        b.setClickingTogglesState(true);
        b.setTooltip(tip);
        b.onClick = [this, &b, &flag] { flag = b.getToggleState(); applyLayout(); };
        addAndMakeVisible(b);
    };
    chip(knobsButton, lay.knobs, "Show or hide the eight CC knobs.");
    chip(padsButton, lay.pads, "Show or hide the chord-pad strip.");
    chip(wheelsButton, lay.wheels, "Show or hide the mod and pitch wheels.");

    detachButton.setClickingTogglesState(true);
    detachButton.setTooltip("Put the keyboard in its own window, so you can resize it "
                            "without stretching the rest of the plugin.");
    detachButton.onClick = [this] { setKeyboardDetached(detachButton.getToggleState()); };
    addAndMakeVisible(detachButton);

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

    // The bars are full-width and translucent, and the controls that ride on them are
    // siblings, not children (a SectionBar is a Button and must stay clickable end to
    // end). Whichever was added last paints last, so without this the bars wash out the
    // tabs and chips sitting on them.
    controlsBar.toBack();
    centreBar.toBack();
    keyboardBar.toBack();

    setResizable(true, true);
    // The floor is everything folded away: three bars and the margins. What used to be
    // the minimum (560) is now roughly the *default*, and Owen can go far below it.
    setResizeLimits(820, 150, 2600, 1400);
    setSize(980, 724);

    // Children configured before they were parented (slider textboxes especially)
    // baked colours from the default LookAndFeel; re-resolve everything under ours.
    sendLookAndFeelChange();

    // Restore the folds this session was saved with, building whichever centre view
    // was up. setCentreView calls applyLayout(), which sizes the editor to match.
    applyAccent(lay.accent); // before the first layout, so nothing paints cyan then repaints
    syncSectionControls();
    refreshCentrePanels();
    applyLayout();
    if (lay.detached)
        setKeyboardDetached(true);

    startTimerHz(30);
}

KeysEditor::~KeysEditor()
{
    stopTimer();
    // Before anything else: the detached window's content is `keybedHolder`, a member of
    // this editor. Tear the window down while that is still a live object.
    if (keyboardWindow != nullptr)
    {
        rememberDetachedBounds();
        keyboardWindow.reset();
    }
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
    g.setColour(accent().base.withAlpha(0.35f));
    g.fillRoundedRectangle(led.expanded(2.0f, 2.0f), 3.0f);
    g.setColour(accent().hot);
    g.fillRoundedRectangle(led, 1.25f);
}

void KeysEditor::addCombo(juce::ComboBox& box, juce::Label& label, const juce::String& text,
                          const juce::StringArray& items, const juce::String& paramID,
                          std::unique_ptr<ComboAtt>& att)
{
    styleLabel(label, text);
    addAndMakeVisible(label);
    box.addItemList(items, 1);
    box.setTitle(text); // accessible name (screen readers / UI Automation); combos otherwise expose none
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

void KeysEditor::setCentreView(int view)
{
    processor.layout.view = juce::jlimit(0, 2, view);
    processor.layout.centre = true; // picking a view unfolds the section it lives in
    refreshCentrePanels();
    applyLayout();
}

void KeysEditor::refreshCentrePanels()
{
    // Only the view on show exists, and nothing exists while the section is folded.
    // Building the generator or the arp is cheap enough to do on a click and expensive
    // enough not to keep two of them warm behind each other.
    const auto& lay = processor.layout;
    const int now = lay.centre ? lay.view : -1;

    if (now != viewChords)
        genPanel.reset();
    if (now != viewArp)
        arpPanel.reset();

    if (now == viewChords && genPanel == nullptr)
    {
        genPanel = std::make_unique<ChordGenPanel>(processor);
        genPanel->setInlineMode(true);
        genPanel->onClose = [this] { setCentreView(viewPerform); };
        addAndMakeVisible(*genPanel);
        genPanel->sendLookAndFeelChange(); // its controls were configured pre-parenting
    }
    if (now == viewArp && arpPanel == nullptr)
    {
        arpPanel = std::make_unique<ArpPanel>(processor);
        arpPanel->setInlineMode(true);
        arpPanel->onClose = [this] { setCentreView(viewPerform); };
        // Shape decides whether the step editor exists, so the panel's height changes
        // under us; re-fit the editor when it does rather than clipping the lanes.
        arpPanel->onPreferredHeightChanged = [this] { applyLayout(); };
        addAndMakeVisible(*arpPanel);
        arpPanel->sendLookAndFeelChange();
    }
}

void KeysEditor::showThemeMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&lnf);
    for (int i = 0; i < skin::numAccents; ++i)
        menu.addItem(i + 1, skin::accentChoices()[i].name, true, i == processor.layout.accent);

    juce::Component::SafePointer<KeysEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&themeButton),
                       [safe](int result)
    {
        if (auto* e = safe.getComponent())
            if (result > 0)
                e->applyAccent(result - 1);
    });
}

void KeysEditor::applyAccent(int index)
{
    processor.layout.accent = juce::jlimit(0, skin::numAccents - 1, index);
    lnf.setAccent(processor.layout.accent);
    wheelLnf.setAccent(processor.layout.accent); // a second instance; it has its own copy

    // The ColourIds the LookAndFeel hands out are cached per component, so every one of
    // them has to be told to re-resolve. sendLookAndFeelChange walks the whole tree, and
    // the detached keyboard window is a separate tree that shares this same lnf.
    sendLookAndFeelChange();
    if (keyboardWindow != nullptr)
        keyboardWindow->sendLookAndFeelChange();

    syncSectionControls();
    repaint();
}

void KeysEditor::syncSectionControls()
{
    const auto& lay = processor.layout;
    static const char* viewNames[] = { "Perform", "Chords", "Arp" };
    controlsBar.setToggleState(lay.controls, juce::dontSendNotification);
    centreBar.setToggleState(lay.centre, juce::dontSendNotification);
    centreBar.setCaption(viewNames[juce::jlimit(0, 2, lay.view)]);
    keyboardBar.setToggleState(lay.keyboard, juce::dontSendNotification);
    knobsButton.setToggleState(lay.knobs, juce::dontSendNotification);
    padsButton.setToggleState(lay.pads, juce::dontSendNotification);
    wheelsButton.setToggleState(lay.wheels, juce::dontSendNotification);
    detachButton.setToggleState(lay.detached, juce::dontSendNotification);
    detachButton.setButtonText(lay.detached ? "Re-dock" : "Detach");

    // The theme button is its own swatch: it wears the colour it sets, so the control and
    // the thing it controls are the same object.
    const auto ac = skin::accentAt(lay.accent);
    themeButton.setButtonText(skin::accentChoices()[juce::jlimit(0, skin::numAccents - 1, lay.accent)].name);
    themeButton.setColour(juce::TextButton::buttonColourId, ac.deep);
    themeButton.setColour(juce::TextButton::textColourOffId, ac.hot);

    performButton.setToggleState(lay.view == viewPerform, juce::dontSendNotification);
    chordsButton.setToggleState(lay.view == viewChords, juce::dontSendNotification);
    arpButton.setToggleState(lay.view == viewArp, juce::dontSendNotification);

    // Knobs/Pads belong to the Perform view; the page transport and Exclusive only act on
    // the pad strip. None of them mean anything under another view, or under a folded one.
    // The three tabs stay visible either way: they are how a folded centre comes back.
    const bool perform = lay.view == viewPerform && lay.centre;
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &knobsButton, &padsButton, &chordExclusiveButton,
             &pagePrevButton, &pageNextButton, &pageLabel })
        c->setVisible(perform);
    knobBank.setVisible(perform && lay.knobs);
    chordPads.setVisible(perform && lay.pads);

    // The header rows and the keybed follow their bars; the wheels sit inside the keybed
    // holder, so they can only show when it does.
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &title, &sizeBox, &sizeLabel, &rootBox, &rootLabel, &scaleBox, &scaleLabel,
             &octaveSlider, &octaveLabel, &scaleLockButton, &polyphonyBox, &polyphonyLabel,
             &channelBox, &channelLabel, &humanizeButton,
             &humanizeVelSlider, &humanizeVelLabel, &humanizeTimeSlider, &humanizeTimeLabel,
             &chordStrumSlider, &chordStrumLabel, &chordStrumDirBox, &chordStrumDirLabel })
        c->setVisible(lay.controls);

    wheelsButton.setVisible(lay.keyboard);
    detachButton.setVisible(lay.keyboard);
    // The holder is visible whenever the section is open, wherever it is parented; being
    // detached is a change of parent, not of visibility. Folding the section while it is
    // detached hides the window instead of the slot, so one control means one thing.
    keybedHolder.setVisible(lay.keyboard);
    if (keyboardWindow != nullptr)
        keyboardWindow->setVisible(lay.keyboard);
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &modWheel, &modLabel, &pitchWheel, &pitchLabel })
        c->setVisible(lay.wheels);

    // Detached, the keybed owns its whole window, so the piano proportions cap comes off
    // and dragging the window taller genuinely makes the keys taller.
    keyboard.setKeyHeightCap(processor.layout.detached ? 4000.0f : 185.0f);
}

int KeysEditor::centreHeight() const
{
    const auto& lay = processor.layout;
    if (! lay.centre)
        return 0;
    switch (lay.view)
    {
        case viewChords: return ChordGenPanel::preferredHeight;
        case viewArp:    return arpPanel != nullptr ? arpPanel->preferredHeight() : 0;
        default: break;
    }
    int h = 0;
    if (lay.knobs)
        h += knobRowH;
    if (lay.pads)
        h += (h > 0 ? 6 : 0) + padRowH;
    return h;
}

int KeysEditor::idealHeight() const
{
    const auto& lay = processor.layout;
    int h = 10 + SectionBar::height; // top margin + the Controls bar
    if (lay.controls)
        h += 4 + headerH;
    h += 4 + viewBarH;
    const int centre = centreHeight();
    if (centre > 0)
        h += 6 + centre;
    h += 6 + SectionBar::height; // the Keyboard bar
    if (lay.keyboard && ! lay.detached)
        h += 4 + dockedKeybedH;
    return h + 10; // bottom margin
}

int KeysEditor::minWidthForView() const
{
    // The generator and the arp carry far more controls than the player, and every one of
    // them has to stay at a full-size target. Grow rather than shrink the targets.
    // 960, not the old 820: the centre bar now also carries Sustain and All Off, and
    // below this the tabs and the pad transport start colliding.
    const auto& lay = processor.layout;
    return (! lay.centre || lay.view == viewPerform) ? 960 : 1010;
}

void KeysEditor::applyLayout()
{
    // Every path into here has just changed a fold, so re-derive what is on screen before
    // spending any geometry on it. Doing it here rather than in each onClick is what keeps
    // a folded section from leaving its controls painted over the section below.
    syncSectionControls();

    bool laidOut = false;
    if (embedded)
    {
        // Keys Host owns geometry: fold, but never resize ourselves. Tell it what we need
        // (both ways - folding a section there should shrink its window too).
        if (onIdealHeightChanged)
            onIdealHeightChanged(idealHeight());
    }
    else
    {
        const int w = juce::jmax(getWidth(), minWidthForView());
        const int h = idealHeight();

        // The floor moves with the folds. Owen could drag the window down to the old
        // fixed minimum with every section open, and the layout does not degrade
        // gracefully at that point - rows just get carved off the bottom and controls
        // vanish. The content's own size *is* the minimum; anything above it is slack the
        // keybed absorbs as instrument body.
        setResizeLimits(minWidthForView(), h, 2600, 1400);

        if (w != getWidth() || h != getHeight())
        {
            setSize(w, h); // triggers resized()
            laidOut = true;
        }
    }
    if (! laidOut)
        resized();

    // Always, and after the above: the wheels fold changes what is *inside* the keybed
    // holder without changing the holder's own bounds, and JUCE only calls resized() on a
    // component whose bounds actually moved. Detached, the holder is not even our child.
    // Without this the wheels vanish and the keys keep their old width.
    layoutKeybed();
    repaint();
}

void KeysEditor::setKeyboardDetached(bool detach)
{
    auto& lay = processor.layout;
    if (detach == (keyboardWindow != nullptr))
    {
        lay.detached = detach; // already in the asked-for state; just keep the flag honest
        syncSectionControls();
        return;
    }
    lay.detached = detach;

    // Wheels and Detach belong to the keybed, not to the window it happens to be in, so
    // they move with it. Owen asked for this: with the button left behind on the main
    // editor, the keyboard window had nothing on it but a close box, and the control that
    // undoes the detach was in the window you were not looking at.
    if (detach)
    {
        keybedHolder.addAndMakeVisible(wheelsButton);
        keybedHolder.addAndMakeVisible(detachButton);
        keybedHolder.addAndMakeVisible(detachedSizeBox);
        detachedSizeBox.sendLookAndFeelChange(); // configured before it was ever parented
        removeChildComponent(&keybedHolder);
        keyboardWindow = std::make_unique<KeyboardWindow>(
            lnf, keybedHolder, lay.detachedBounds,
            [this] { setKeyboardDetached(false); },   // its close button re-docks
            [this] { rememberDetachedBounds(); });
    }
    else
    {
        rememberDetachedBounds();
        keyboardWindow.reset(); // clears its content, handing keybedHolder back
        addAndMakeVisible(keybedHolder);
        addAndMakeVisible(wheelsButton);   // back onto the Keyboard bar
        addAndMakeVisible(detachButton);
        keybedHolder.removeChildComponent(&detachedSizeBox); // Controls has the real one
    }

    syncSectionControls();
    applyLayout();
}

void KeysEditor::rememberDetachedBounds()
{
    if (keyboardWindow != nullptr)
        processor.layout.detachedBounds = keyboardWindow->getBounds();
}

void KeysEditor::layoutKeybed()
{
    auto area = keybedHolder.getLocalBounds();

    // Detached, the holder is the whole window, so it carries its own strip for the two
    // controls that came with it. Docked, those live on the Keyboard section bar and this
    // strip does not exist (there is no spare height for it under a 185 px keybed).
    if (processor.layout.detached)
    {
        auto strip = area.removeFromTop(SectionBar::height);
        area.removeFromTop(4);
        detachButton.setBounds(strip.removeFromRight(104).reduced(2, 2));
        strip.removeFromRight(6);
        wheelsButton.setBounds(strip.removeFromRight(84).reduced(2, 2));
        strip.removeFromRight(6);
        detachedSizeBox.setBounds(strip.removeFromRight(104).reduced(2, 2));
    }

    // Wheels sit left of the keyboard; the keyboard fills the rest. Columns are as
    // slim as the mouse-only floor allows (40 px column -> 36 px slider, floor 34).
    if (processor.layout.wheels)
    {
        auto wheels = area.removeFromLeft(84);
        area.removeFromLeft(4);
        auto modCol = wheels.removeFromLeft(40);
        wheels.removeFromLeft(4);
        auto pitchCol = wheels;
        modLabel.setBounds(modCol.removeFromBottom(15));
        modWheel.setBounds(modCol.reduced(2, 2));
        pitchLabel.setBounds(pitchCol.removeFromBottom(15));
        pitchWheel.setBounds(pitchCol.reduced(2, 2));
    }
    keyboard.setBounds(area);
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
    keyboard.setLatch(editingPad >= 0);
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

    // Only the timing spread greys out with Humanize now. The velocity range is the
    // velocity control whether Humanize is on or off (it plays the band's midpoint when
    // off), so disabling it would grey out the only way to set how hard Keys plays.
    const bool hum = apvts.getRawParameterValue("humanize")->load() > 0.5f;
    humanizeTimeSlider.setEnabled(hum);
    humanizeTimeLabel.setEnabled(hum);

    // Keep the two-handle velocity range synced to its params and show the numbers. With
    // Humanize off the two ends are one value as far as playing goes, so read out the
    // midpoint rather than a range that is not being spread over.
    const int vmin = (int) apvts.getRawParameterValue("humanizeVelMin")->load();
    const int vmax = (int) apvts.getRawParameterValue("humanizeVelMax")->load();
    humanizeVelSlider.setMinAndMaxValues(vmin, vmax, juce::dontSendNotification);
    humanizeVelLabel.setText(hum ? "VELOCITY  " + juce::String(juce::jmin(vmin, vmax)) + "-"
                                       + juce::String(juce::jmax(vmin, vmax))
                                 : "VELOCITY  " + juce::String((vmin + vmax) / 2),
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
                              skin::control.interpolatedWith(skin::accentOf(*this).base, panicFlash));
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
    // Folded away, there is no band to draw and no wordmark under a hidden title.
    if (processor.layout.controls && ! headerBand.isEmpty())
    {
        const auto band = headerBand.toFloat();
        g.setGradientFill({ skin::headerTop, 0.0f, band.getY(), skin::headerBot, 0.0f, band.getBottom(), false });
        g.fillRect(band);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRect(0.0f, band.getBottom(), full.getWidth(), 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.04f));
        g.fillRect(0.0f, band.getBottom() + 1.0f, full.getWidth(), 1.0f);

        // Wordmark caption under the title.
        g.setColour(skin::accentOf(*this).base.withAlpha(0.85f));
        g.setFont(skin::micro(9.0f).withExtraKerningFactor(0.32f));
        g.drawText("OK STUDIO", titleCaption, juce::Justification::centredLeft);
    }

    // The performance module floats as one raised panel behind whichever of the knob bank
    // and the pad strip is showing. The other centre views draw their own card.
    if (knobBank.isVisible() || chordPads.isVisible())
    {
        auto module = knobBank.isVisible() ? knobBank.getBounds() : chordPads.getBounds();
        if (knobBank.isVisible() && chordPads.isVisible())
            module = module.getUnion(chordPads.getBounds());
        const auto perf = module.toFloat().expanded(4.0f, 5.0f);
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawRoundedRectangle(perf.expanded(0.5f), skin::panelRadius + 0.5f, 1.0f);
        g.setColour(skin::panel);
        g.fillRoundedRectangle(perf, skin::panelRadius);
        g.setColour(juce::Colours::white.withAlpha(0.045f));
        g.fillRoundedRectangle(perf.withHeight(1.5f).reduced(skin::panelRadius, 0.0f), 0.75f);
    }

    // Say where the keybed went, on the bar itself. Below the bar would be the obvious
    // place, but when the keyboard is away the window shrinks until there is no "below"
    // left - the caption was drawn straight off the bottom edge. The bar's content area
    // is empty in exactly these two states (Wheels and Detach travel with the keybed).
    if (! processor.layout.keyboard || processor.layout.detached)
    {
        g.setColour(skin::textFaint);
        g.setFont(skin::micro(10.0f).withExtraKerningFactor(0.2f));
        g.drawText(processor.layout.detached ? "IN ITS OWN WINDOW" : "HIDDEN",
                   keyboardBar.contentArea().withTrimmedRight(4),
                   juce::Justification::centredRight);
    }
}

void KeysEditor::resized()
{
    const auto& lay = processor.layout;
    auto area = getLocalBounds().reduced(10);

    // Top to bottom, each band skipped when its section is folded: the Controls bar, the
    // three control rows, the view bar (centre tabs + chord-pad transport), the centre
    // view, the Keyboard bar, and the keybed. The keybed takes whatever is left, so extra
    // window height reads as instrument body under a bottom-anchored keyboard.
    controlsBar.setBounds(area.removeFromTop(SectionBar::height));
    {
        auto bar = controlsBar.contentArea();
        themeButton.setBounds(bar.removeFromRight(112).reduced(2, 0));
        bar.removeFromRight(6);
        if (updateButton.isVisible())
            updateButton.setBounds(bar.removeFromRight(170).reduced(0, 1));
    }

    auto header = juce::Rectangle<int> {};
    if (lay.controls)
    {
        area.removeFromTop(4);
        header = area.removeFromTop(headerH);
        headerBand = getLocalBounds().withY(header.getY() - 6).withHeight(header.getHeight() + 12);
    }
    else
    {
        headerBand = {};
    }

    area.removeFromTop(4);
    centreBar.setBounds(area.removeFromTop(SectionBar::height));
    auto toolRow = centreBar.contentArea();

    const int centre = centreHeight();
    juce::Rectangle<int> centreArea;
    if (centre > 0)
    {
        area.removeFromTop(6);
        centreArea = area.removeFromTop(centre);
    }

    area.removeFromTop(6);
    keyboardBar.setBounds(area.removeFromTop(SectionBar::height));
    if (! lay.detached)
    {
        // Wheels and Detach ride on the Keyboard bar: they are the section's own controls,
        // and putting them here means folding the section takes them with it. Detached,
        // they are not our children at all - layoutKeybed() places them in that window.
        auto bar = keyboardBar.contentArea();
        detachButton.setBounds(bar.removeFromRight(104).reduced(2, 0));
        bar.removeFromRight(6);
        wheelsButton.setBounds(bar.removeFromRight(84).reduced(2, 0));
    }

    if (lay.keyboard && ! lay.detached)
    {
        area.removeFromTop(4);
        keybedHolder.setBounds(area);
    }

    // The centre views fill the slot the Perform pair would have used.
    if (genPanel != nullptr)
        genPanel->setBounds(centreArea);
    if (arpPanel != nullptr)
        arpPanel->setBounds(centreArea);
    if (lay.view == viewPerform)
    {
        auto perform = centreArea;
        if (lay.knobs)
            knobBank.setBounds(perform.removeFromTop(knobRowH));
        if (lay.knobs && lay.pads)
            perform.removeFromTop(6);
        if (lay.pads)
            chordPads.setBounds(perform.removeFromTop(padRowH));
    }

    if (! lay.controls)
    {
        layoutToolRow(toolRow);
        return; // nothing below needs a header that is not there
    }

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

    // Two rows, down from three: dropping the fixed Velocity slider and the Latch toggle
    // emptied the middle one, so the remaining controls close up and the header (and with
    // it the whole window) gets 49 px shorter.
    cell(rowA, 88, sizeLabel, sizeBox);
    cell(rowA, 70, rootLabel, rootBox);
    cell(rowA, 150, scaleLabel, scaleBox);
    cell(rowA, 120, octaveLabel, octaveSlider);
    toggleCell(rowA, 110, scaleLockButton);
    cell(rowA, 90, polyphonyLabel, polyphonyBox);
    cell(rowA, 70, channelLabel, channelBox);

    toggleCell(rowB, 96, humanizeButton);
    cell(rowB, 208, humanizeVelLabel, humanizeVelSlider);
    cell(rowB, 130, humanizeTimeLabel, humanizeTimeSlider);
    cell(rowB, 150, chordStrumLabel, chordStrumSlider);
    cell(rowB, 100, chordStrumDirLabel, chordStrumDirBox);

    layoutToolRow(toolRow);
}

void KeysEditor::layoutToolRow(juce::Rectangle<int> row)
{
    // The centre bar's content: which view is showing, then the chord-pad transport. The
    // three tabs are always there (they are how a folded centre comes back); everything to
    // the right of them only acts on the pad strip, so it hides with the Perform view.
    performButton.setBounds(row.removeFromLeft(78).reduced(0, 2));
    row.removeFromLeft(4);
    chordsButton.setBounds(row.removeFromLeft(72).reduced(0, 2));
    row.removeFromLeft(4);
    arpButton.setBounds(row.removeFromLeft(60).reduced(0, 2));
    row.removeFromLeft(14);

    knobsButton.setBounds(row.removeFromLeft(66).reduced(0, 2));
    row.removeFromLeft(4);
    padsButton.setBounds(row.removeFromLeft(60).reduced(0, 2));
    row.removeFromLeft(14);

    pagePrevButton.setBounds(row.removeFromLeft(34).reduced(0, 2));
    pageLabel.setBounds(row.removeFromLeft(28));
    pageNextButton.setBounds(row.removeFromLeft(34).reduced(0, 2));
    row.removeFromLeft(8);
    chordExclusiveButton.setBounds(row.removeFromLeft(104).withSizeKeepingCentre(102, 24));

    // Sustain and All Off ride here rather than in the Controls section, at Owen's
    // request. They are the two controls you reach for *while playing*, and Controls is
    // the section most likely to be folded away, which was taking them with it.
    row.removeFromLeft(12);
    sustainButton.setBounds(row.removeFromLeft(96).withSizeKeepingCentre(94, 24));
    row.removeFromLeft(4);
    panicButton.setBounds(row.removeFromLeft(84).reduced(0, 3));
}
} // namespace keys
