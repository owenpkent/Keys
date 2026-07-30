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
    constexpr int knobRowH = 110;
    constexpr int padRowH = 96;      // two rows of eight, names only
    constexpr int padBigRowH = 320;  // four rows of four, with room for the full chord card
    constexpr int dockedKeybedH = 212;                // 185 px of key plus a little body
    constexpr int detachWidth = 104;                  // the Detach / Re-dock button

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

    // The raised panel a performance module floats on. The generator, the arp and the
    // Transcribe panel draw their own cards; the knob bank and the pad strip do not, so
    // their holders draw it behind them.
    void paintModule(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        const auto perf = bounds.toFloat();
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawRoundedRectangle(perf.reduced(0.5f), skin::panelRadius + 0.5f, 1.0f);
        g.setColour(skin::panel);
        g.fillRoundedRectangle(perf.reduced(1.0f), skin::panelRadius);
        g.setColour(juce::Colours::white.withAlpha(0.045f));
        g.fillRoundedRectangle(perf.reduced(1.0f).withHeight(1.5f).reduced(skin::panelRadius, 0.0f), 0.75f);
    }
} // namespace

KeysEditor::KeysEditor(KeysProcessor& p)
    : juce::AudioProcessorEditor(p), processor(p),
      controlsHolder(section(secControls).holder),
      centreHolder(section(secCentre).holder),
      arpHolder(section(secArp).holder),
      padsHolder(section(secPads).holder),
      transcribeHolder(section(secTranscribe).holder),
      keybedHolder(section(secKeyboard).holder),
      keyboard(p), knobBank(p), chordPads(p)
{
    setLookAndFeel(&lnf); // the Keys "Obsidian" skin; palette lives in KeysLookAndFeel.h
    okstudio::ui::makeMouseOnly(*this);

    // --- The section table ---------------------------------------------------------
    // Every section is the same three things: a fold flag, a window frame it remembers, and
    // a floor below which its controls stop being clickable (which for a one-mouse player is
    // the same as broken). Wired before anything is parented, so the holders exist first.
    auto& lay = processor.layout;
    const auto wire = [this](SectionId id, SectionBar& bar, bool& open, bool& detached,
                             juce::Rectangle<int>& bounds, const juce::String& name,
                             const juce::String& windowTitle,
                             juce::Point<int> minSize, juce::Point<int> defaultSize)
    {
        auto& s = section(id);
        s.bar = &bar;
        s.open = &open;
        s.detached = &detached;
        s.bounds = &bounds;
        s.name = name;
        s.windowTitle = windowTitle;
        s.minSize = minSize;
        s.defaultSize = defaultSize;

        bar.onClick = [this, id]
        {
            *section(id).open = section(id).bar->getToggleState();
            refreshSectionPanels(); // folded away, the panel it was showing is destroyed
            applyLayout();
        };
        addAndMakeVisible(bar);

        s.detachButton.setClickingTogglesState(true);
        s.detachButton.setTooltip("Put the " + name.toLowerCase() + " section in a window of its own, "
                                  "so it can be sized without stretching the rest of the plugin.");
        s.detachButton.onClick = [this, id] { setSectionDetached(id, section(id).detachButton.getToggleState()); };
        addAndMakeVisible(s.detachButton);

        s.holder.layout = [this, id]
        {
            switch (id)
            {
                case secControls:   layoutControlsHolder(); break;
                case secCentre:     layoutCentreHolder(); break;
                case secArp:        layoutArpHolder(); break;
                case secPads:       layoutPadsHolder(); break;
                case secTranscribe: layoutTranscribeHolder(); break;
                case secKeyboard:   layoutKeybed(); break;
                default: break;
            }
        };
        addAndMakeVisible(s.holder);
    };

    wire(secControls, controlsBar, lay.controls, lay.controlsDetached, lay.controlsDetachedBounds,
         "Controls", "Keys Controls", { 900, 190 }, { 980, 200 });
    wire(secCentre, centreBar, lay.centre, lay.centreDetached, lay.centreDetachedBounds,
         "Centre", "Keys Centre", { 720, 220 }, { 1010, 420 });
    wire(secArp, arpBar, lay.arp, lay.arpDetached, lay.arpDetachedBounds,
         "Arp", "Keys Arpeggiator", { 900, 300 }, { 1100, 520 });
    wire(secPads, padsBar, lay.pads, lay.padsDetached, lay.padsDetachedBounds,
         "Pads", "Keys Chord Pads", { 620, 180 }, { 940, 300 });
    wire(secTranscribe, transcribeBar, lay.transcribe, lay.transcribeDetached, lay.transcribeDetachedBounds,
         "Transcribe", "Keys Transcribe", { 620, 300 }, { 940, 420 });
    wire(secKeyboard, keyboardBar, lay.keyboard, lay.detached, lay.detachedBounds,
         "Keyboard", "Keys Keyboard", { 420, 190 }, { 1000, 300 });

    // Wheels and the second Size selector belong to the keybed, not to the window it happens
    // to be in, so they follow it out. Owen asked for this: with them left behind, the
    // keyboard window had nothing on it but a close box.
    section(secKeyboard).travellers = { { &wheelsButton, 84, false }, { &detachedSizeBox, 104, true } };

    // The two holders whose content paints no card of its own get one behind it.
    centreHolder.painter = [this](juce::Graphics& g)
    {
        if (knobBank.isVisible())
            paintModule(g, knobBank.getBounds().expanded(4, 4));
    };
    padsHolder.painter = [this](juce::Graphics& g)
    {
        if (chordPads.isVisible())
            paintModule(g, chordPads.getBounds().expanded(4, 4));
    };
    // The header band, and the wordmark under the title. Painted by the holder rather than
    // by the editor so it travels with the section instead of being left behind as a
    // gradient over nothing.
    controlsHolder.painter = [this](juce::Graphics& g)
    {
        const auto band = controlsHolder.getLocalBounds().toFloat();
        g.setGradientFill({ skin::headerTop, 0.0f, band.getY(), skin::headerBot, 0.0f, band.getBottom(), false });
        g.fillRect(band);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRect(0.0f, band.getBottom() - 2.0f, band.getWidth(), 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.04f));
        g.fillRect(0.0f, band.getBottom() - 1.0f, band.getWidth(), 1.0f);

        g.setColour(skin::accentOf(controlsHolder).base.withAlpha(0.85f));
        g.setFont(skin::micro(9.0f).withExtraKerningFactor(0.32f));
        g.drawText("OK STUDIO", titleCaption, juce::Justification::centredLeft);
    };

    // --- Controls section ------------------------------------------------------------
    title.setText("KEYS", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions("Segoe UI", 24.0f, juce::Font::bold))
                      .withExtraKerningFactor(0.10f));
    title.setColour(juce::Label::textColourId, skin::text);
    controlsHolder.addAndMakeVisible(title);

    addCombo(controlsHolder, sizeBox, sizeLabel, "Size", sizeItems(), "size", sizeAtt);
    addCombo(controlsHolder, rootBox, rootLabel, "Root", okstudio::scales::noteNames(), "root", rootAtt);
    addCombo(controlsHolder, scaleBox, scaleLabel, "Scale", okstudio::scales::names(), "scale", scaleAtt);
    addCombo(controlsHolder, channelBox, channelLabel, "MIDI Ch", channelItems(), "channel", channelAtt);
    addCombo(controlsHolder, polyphonyBox, polyphonyLabel, "Voices",
             { "Off", "1", "2", "3", "4", "5", "6", "7", "8" }, "polyphony", polyphonyAtt);

    styleLabel(octaveLabel, "Octave");
    controlsHolder.addAndMakeVisible(octaveLabel);
    octaveSlider.setSliderStyle(juce::Slider::IncDecButtons);
    octaveSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 46, 26);
    octaveSlider.setRange(-5, 5, 1);
    controlsHolder.addAndMakeVisible(octaveSlider);
    octaveAtt = std::make_unique<SliderAtt>(processor.apvts, "octave", octaveSlider);

    // Scale Lock and Humanize belong to the Controls section; Sustain and Exclusive ride on
    // the Keyboard bar, outside every fold, because they are what you reach for while playing.
    controlsHolder.addAndMakeVisible(scaleLockButton);
    controlsHolder.addAndMakeVisible(humanizeButton);
    addAndMakeVisible(sustainButton);
    addAndMakeVisible(latchButton);
    addAndMakeVisible(chordExclusiveButton);
    sustainButton.setTooltip("Pedal. Notes keep sounding after you let go, and clicking a key "
                             "that is already ringing strikes it again. All Off stops them.");
    latchButton.setTooltip("Click a key to hold it, click it again to release it. Use this to "
                           "build a chord a note at a time, or to take one apart.");
    scaleLockAtt = std::make_unique<ButtonAtt>(processor.apvts, "scaleLock", scaleLockButton);
    sustainAtt = std::make_unique<ButtonAtt>(processor.apvts, "sustain", sustainButton);
    latchAtt = std::make_unique<ButtonAtt>(processor.apvts, "latch", latchButton);
    humanizeAtt = std::make_unique<ButtonAtt>(processor.apvts, "humanize", humanizeButton);
    chordExclusiveAtt = std::make_unique<ButtonAtt>(processor.apvts, "chordExclusive", chordExclusiveButton);

    // Humanize amounts: each note picks a random velocity in the [min, max] range (a
    // two-handle slider) and a micro-timing offset up to Timing ms. The range slider has
    // no APVTS attachment (two values), so it is synced to the params by hand.
    styleLabel(humanizeVelLabel, "Velocity");
    controlsHolder.addAndMakeVisible(humanizeVelLabel);
    humanizeVelSlider.setRange(1, 127, 1); // style/textbox are RangeSlider's own
    humanizeVelSlider.setTooltip("Each note takes a random velocity in this range. "
                                 "Drag an end to resize it, or the middle to move it.");
    humanizeVelSlider.setMinAndMaxValues(processor.apvts.getRawParameterValue("humanizeVelMin")->load(),
                                         processor.apvts.getRawParameterValue("humanizeVelMax")->load(),
                                         juce::dontSendNotification);
    humanizeVelSlider.onValueChange = [this]
    {
        writeParam("humanizeVelMin", humanizeVelSlider.getMinValue());
        writeParam("humanizeVelMax", humanizeVelSlider.getMaxValue());
    };
    controlsHolder.addAndMakeVisible(humanizeVelSlider);

    // Chord-pad strum (Octavium "Drift"): spread a pad's note-ons over N ms, in a direction.
    // A range rather than one number, like Velocity above it - each chord rakes at a speed
    // drawn from the band, so a part played on one pad stops sounding stamped out. Two
    // values means no APVTS attachment; synced to the pair of params by hand.
    styleLabel(chordStrumLabel, "Strum");
    controlsHolder.addAndMakeVisible(chordStrumLabel);
    chordStrumSlider.setRange(0, 200, 1);
    chordStrumSlider.setTooltip("Each chord spreads its notes over a time drawn from this "
                                "range. Drag an end to resize it, or the middle to move it; "
                                "both ends together is a fixed strum.");
    chordStrumSlider.setMinAndMaxValues(processor.apvts.getRawParameterValue("chordStrum")->load(),
                                        processor.apvts.getRawParameterValue("chordStrumMax")->load(),
                                        juce::dontSendNotification);
    chordStrumSlider.onValueChange = [this]
    {
        writeParam("chordStrum", chordStrumSlider.getMinValue());
        writeParam("chordStrumMax", chordStrumSlider.getMaxValue());
    };
    controlsHolder.addAndMakeVisible(chordStrumSlider);

    addCombo(controlsHolder, chordStrumDirBox, chordStrumDirLabel, "Dir",
             { "Up", "Down", "Random" }, "chordStrumDir", chordStrumDirAtt);

    // Tempo. The arpeggiator is the only thing in Keys timed in beats, and until now it had
    // no tempo of its own: it followed the host and fell back to whatever the host last
    // said, which in the standalone was 120 forever with no way to change it. A plain drag
    // slider rather than +/- steppers, because getting from 120 to 90 should be one gesture.
    styleLabel(bpmLabel, "BPM");
    controlsHolder.addAndMakeVisible(bpmLabel);
    bpmSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 26);
    bpmSlider.setRange(40, 240, 1);
    bpmSlider.setTooltip("Tempo the arpeggiator runs at when there is no transport to follow "
                         "- always in the standalone, and whenever the host is stopped. While "
                         "the host is playing, Keys follows the host.");
    controlsHolder.addAndMakeVisible(bpmSlider);
    bpmAtt = std::make_unique<SliderAtt>(processor.apvts, "bpm", bpmSlider);

    // --- Keyboard section ------------------------------------------------------------
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
    centreHolder.addAndMakeVisible(knobBank);

    // Centre-view tabs. Each picks a view; the section's own chevron folds it away, the
    // same as every other section. The tabs stay visible while it is folded, so picking
    // one both unfolds and switches - otherwise a folded centre would be a dead end.
    const auto tab = [this](juce::TextButton& b, int view, const juce::String& tip)
    {
        b.setClickingTogglesState(false); // setCentreView owns the lit state
        b.setTooltip(tip);
        b.onClick = [this, view] { setCentreView(view); };
        addAndMakeVisible(b);
    };
    tab(performButton, viewPerform, "The eight CC knobs.");
    tab(chordsButton, viewChords, "Generate chords for this page.");

    // Chord-pad pages: four pages of sixteen (Octavium's 4x4 per page), so a session can
    // hold several keys' worth of chords without the strip shrinking below a comfortable
    // target. Four explicit page buttons, sat with the pads rather than a "1/4" transport
    // parked up on the view bar: one click reaches any page, and what it pages is obvious
    // from where it is.
    for (int pg = 0; pg < KeysProcessor::numPadPages; ++pg)
    {
        auto& b = pageButtons[(size_t) pg];
        b.setButtonText(juce::String(pg + 1));
        b.setTooltip("Chord-pad page " + juce::String(pg + 1) + " of " + juce::String(KeysProcessor::numPadPages) + ".");
        b.setClickingTogglesState(false); // the timer owns the lit state
        b.onClick = [this, pg]
        {
            if (auto* param = processor.apvts.getParameter("padPage"))
                param->setValueNotifyingHost(param->convertTo0to1((float) pg));
        };
        addAndMakeVisible(b);
    }

    // Two rows of eight, or four rows of four with the full chord card on each - the tall
    // arrangement the chord generator used to draw over the top of these same pads. It sits
    // on the Pads bar because it is a question about the pads, not about the generator, and
    // it is worth reaching from any view.
    padsBigButton.setClickingTogglesState(false); // syncSectionControls owns the lit state
    padsBigButton.setTooltip("Bigger cards: four rows of four, each showing the chord's notes "
                             "and a mini keyboard.");
    padsBigButton.onClick = [this]
    {
        processor.layout.padsBig = ! processor.layout.padsBig;
        chordPads.setBigCards(processor.layout.padsBig);
        syncSectionControls();
        if (onIdealHeightChanged)
            onIdealHeightChanged(idealHeight());
        resized();
    };
    addAndMakeVisible(padsBigButton);
    chordPads.setBigCards(processor.layout.padsBig);

    // On rides on the Arp *bar*, not inside the section, so folding the editor away does not
    // take the arp's power switch with it. Same reasoning as Sustain and All Off living on
    // the Keyboard bar.
    arpOnButton.setTooltip("Arpeggiate whatever is sounding. Lit, clicking a chord card also "
                           "hands that chord to the arp and leaves it there until you click "
                           "the card again.");
    addAndMakeVisible(arpOnButton);
    arpOnAtt = std::make_unique<ButtonAtt>(processor.apvts, "arpOn", arpOnButton);

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
    chip(wheelsButton, lay.wheels, "Show or hide the mod and pitch wheels.");

    updateButton.setColour(juce::TextButton::buttonColourId, okstudio::theme::good.withAlpha(0.85f));
    addChildComponent(updateButton); // hidden until the updater finds a newer release

    const auto velocity = [this] { return processor.baseVelocity01(); };
    keyboard.getVelocity = velocity;
    padsHolder.addAndMakeVisible(chordPads);

    // Dropping a pad on the live card latches its notes back onto the keyboard for
    // editing (Octavium's drag-to-edit).
    chordPads.onRecall = [this](const std::vector<int>& notes) { keyboard.recallOutputNotes(notes); };

    // Right-click "Edit on keyboard": the pad's notes latch onto the piano and every
    // latch change writes straight back to the pad, name re-detected live.
    chordPads.onEditToggle = [this](int slot) { toggleEditPad(slot); };

    // The generator's half of a pad's card menu - New chord, and what could follow it. Asked
    // for on every menu rather than installed once, because the generator only exists while
    // the Chords view is open: with it closed the items are simply not offered, which is
    // exactly what happened before, when they lived on a card only that view drew.
    chordPads.onExtraMenuItems = [this](int slot, juce::PopupMenu& m)
    {
        if (genPanel != nullptr)
            genPanel->addPadMenuItems(slot, m);
    };
    chordPads.onExtraMenuChoice = [this](int slot, int id)
    {
        if (genPanel != nullptr)
            genPanel->handlePadMenuChoice(slot, id);
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

    // The bars are full-width and translucent, and the controls that ride on them are
    // siblings, not children: a SectionBar is a Button, and it stays full-width so it can
    // paint the caption and the fold state across the whole strip, even though only the
    // chevron end answers a click (SectionBar::hitTest). Whichever was added last paints
    // last, so without this the bars wash out the buttons and captions sitting on them.
    for (int i = 0; i < numSections; ++i)
        sections[(size_t) i].bar->toBack();

    setResizable(true, true);
    // The floor is everything folded away: six bars and the margins. What used to be
    // the minimum (560) is now roughly the *default*, and Owen can go far below it.
    setResizeLimits(820, 150, 2600, 1400);
    setSize(980, 724);

    // Children configured before they were parented (slider textboxes especially)
    // baked colours from the default LookAndFeel; re-resolve everything under ours.
    sendLookAndFeelChange();

    // Restore the folds this session was saved with, building whichever centre view
    // was up, then pop back out whichever sections were left in windows of their own.
    applyAccent(lay.accent); // before the first layout, so nothing paints cyan then repaints
    syncSectionControls();
    refreshSectionPanels();
    applyLayout();
    for (int i = 0; i < numSections; ++i)
        if (*sections[(size_t) i].detached)
            setSectionDetached((SectionId) i, true);

    startTimerHz(30);
}

KeysEditor::~KeysEditor()
{
    stopTimer();
    // Before anything else: each detached window's content is a holder that is a member of
    // this editor. Tear the windows down while those are still live objects.
    for (int i = 0; i < numSections; ++i)
    {
        auto& s = sections[(size_t) i];
        if (s.window != nullptr)
        {
            rememberSectionBounds((SectionId) i);
            s.window.reset();
        }
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

void KeysEditor::addCombo(juce::Component& parent, juce::ComboBox& box, juce::Label& label,
                          const juce::String& text, const juce::StringArray& items,
                          const juce::String& paramID, std::unique_ptr<ComboAtt>& att)
{
    styleLabel(label, text);
    parent.addAndMakeVisible(label);
    box.addItemList(items, 1);
    box.setTitle(text); // accessible name (screen readers / UI Automation); combos otherwise expose none
    parent.addAndMakeVisible(box);
    att = std::make_unique<ComboAtt>(processor.apvts, paramID, box);
}

void KeysEditor::writeParam(const char* paramID, double value)
{
    if (auto* param = processor.apvts.getParameter(paramID))
    {
        const float norm = param->convertTo0to1((float) value);
        if (std::abs(param->getValue() - norm) > 1.0e-6f)
            param->setValueNotifyingHost(norm);
    }
}

void KeysEditor::setCentreView(int view)
{
    processor.layout.view = juce::jlimit(0, 1, view);
    processor.layout.centre = true; // picking a view unfolds the section it lives in
    refreshCentrePanels();
    applyLayout();
}

void KeysEditor::refreshSectionPanels()
{
    // Called on every fold and every detach: each of these builds or destroys nothing
    // unless its own section changed state.
    refreshCentrePanels();
    refreshArpPanel();
    refreshTranscribePanel();
}

void KeysEditor::refreshCentrePanels()
{
    // Only the view on show exists, and nothing exists while the section is folded. The
    // generator is a band of controls now - its own copy of the pad grid was removed on
    // 2026-07-30, being the same sixteen pads the Pads section already shows - so what is
    // built and thrown away here is cheap either way.
    const auto& lay = processor.layout;
    const int now = lay.centre ? lay.view : -1;

    if (now != viewChords)
        genPanel.reset();

    if (now == viewChords && genPanel == nullptr)
    {
        genPanel = std::make_unique<ChordGenPanel>(processor);
        genPanel->setInlineMode(true);
        genPanel->onClose = [this] { setCentreView(viewPerform); };
        centreHolder.addAndMakeVisible(*genPanel);
        genPanel->sendLookAndFeelChange(); // its controls were configured pre-parenting
        layoutCentreHolder();
    }
}

void KeysEditor::refreshArpPanel()
{
    // The panel is the *view* of the arp, not the arp: destroying it folds the editor away
    // and leaves the arpeggiator running, which is the whole point of the On toggle staying
    // out on the bar.
    if (! processor.layout.arp)
    {
        arpPanel.reset();
        return;
    }
    if (arpPanel != nullptr)
        return;

    arpPanel = std::make_unique<ArpPanel>(processor);
    arpPanel->setInlineMode(true);
    arpPanel->onClose = [this]
    {
        processor.layout.arp = false;
        refreshArpPanel();
        applyLayout();
    };
    // Shape decides whether the step editor exists, so the panel's height changes under us;
    // re-fit the editor when it does rather than clipping the lanes.
    arpPanel->onPreferredHeightChanged = [this] { applyLayout(); };
    arpHolder.addAndMakeVisible(*arpPanel);
    arpPanel->sendLookAndFeelChange();
    layoutArpHolder();
}

void KeysEditor::refreshTranscribePanel()
{
#if KEYS_TRANSCRIBE
    const bool wanted = processor.layout.transcribe;

    if (wanted && transcribePanel == nullptr)
    {
        transcribePanel = std::make_unique<TranscribePanel>();
        transcribeHolder.addAndMakeVisible(*transcribePanel);
        layoutTranscribeHolder();
    }
    else if (! wanted && transcribePanel != nullptr)
    {
        // Destroyed rather than hidden: it owns an open audio device and the model's weights.
        transcribePanel.reset();
    }
#endif
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
    // each detached section is a separate tree that shares this same lnf.
    sendLookAndFeelChange();
    for (auto& s : sections)
        if (s.window != nullptr)
            s.window->sendLookAndFeelChange();

    syncSectionControls();
    repaint();
}

void KeysEditor::syncSectionControls()
{
    const auto& lay = processor.layout;

    // Every section, the same three questions: is it open, is it out in a window of its
    // own, and does the button that put it there still say the right thing.
    for (auto& s : sections)
    {
        s.bar->setToggleState(*s.open, juce::dontSendNotification);
        s.detachButton.setToggleState(*s.detached, juce::dontSendNotification);
        s.detachButton.setButtonText(*s.detached ? "Re-dock" : "Detach");
        // Six buttons reading "Detach" are six identical accessible names, which is no use
        // to a screen reader and made UI Automation pick whichever it found first. Say which
        // section each one moves.
        s.detachButton.setTitle(s.detachButton.getButtonText() + " " + s.name);
        // Detach goes away with the section it detaches (2026-07-27, Owen's ask). Folded
        // away, it was the loudest thing left on a bar whose whole job is to be quiet, and
        // it offered a gesture with nothing behind it: detaching a folded section built a
        // window that opened hidden. Every other control on a bar already hides with its
        // section - the pad pages, the Knobs chip, Wheels - so this was the odd one out
        // rather than a rule being broken. The deliberate exceptions stay: the arp's On
        // (folding the panel must not stop the arpeggiator), the centre's two tabs (they
        // are how a folded centre comes back) and the theme swatch (it belongs to the
        // plugin, not the section).
        s.detachButton.setVisible(*s.open);
        // A holder is visible whenever its section is open, wherever it is parented: being
        // detached is a change of parent, not of visibility. Folding a detached section
        // hides its window instead of its slot, so one control means one thing.
        s.holder.setVisible(*s.open);
        if (s.window != nullptr)
            s.window->setVisible(*s.open);
    }

    static const char* viewNames[] = { "Perform", "Chords" };
    centreBar.setCaption(viewNames[juce::jlimit(0, 1, lay.view)]);
    knobsButton.setToggleState(lay.knobs, juce::dontSendNotification);
    wheelsButton.setToggleState(lay.wheels, juce::dontSendNotification);

    // The theme button is its own swatch: it wears the colour it sets, so the control and
    // the thing it controls are the same object.
    const auto ac = skin::accentAt(lay.accent);
    themeButton.setButtonText(skin::accentChoices()[juce::jlimit(0, skin::numAccents - 1, lay.accent)].name);
    themeButton.setColour(juce::TextButton::buttonColourId, ac.deep);
    themeButton.setColour(juce::TextButton::textColourOffId, ac.hot);

    performButton.setToggleState(lay.view == viewPerform, juce::dontSendNotification);
    chordsButton.setToggleState(lay.view == viewChords, juce::dontSendNotification);

    // Knobs is all that is left of the Perform view, so its chip is the only one that
    // hides with another view. The two tabs stay visible either way: they are how a
    // folded centre comes back.
    const bool perform = lay.view == viewPerform && lay.centre;
    knobsButton.setVisible(perform);
    knobBank.setVisible(perform && lay.knobs);

    // The pads' page buttons stay on their bar in the main window even when the strip is
    // off in one of its own: they page the cards, and paging from the window you are
    // already looking at is one click either way.
    for (auto& b : pageButtons)
        b.setVisible(lay.pads); // pointless without pads
    padsBigButton.setVisible(lay.pads);
    padsBigButton.setToggleState(lay.padsBig, juce::dontSendNotification);

    wheelsButton.setVisible(lay.keyboard);
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &modWheel, &modLabel, &pitchWheel, &pitchLabel })
        c->setVisible(lay.wheels);

    // Detached, the keybed owns its whole window, so the piano proportions cap comes off
    // and dragging the window taller genuinely makes the keys taller.
    keyboard.setKeyHeightCap(lay.detached ? 4000.0f : 185.0f);
}

int KeysEditor::centreHeight() const
{
    // Perform is the knob bank alone since the pads moved out into their own section.
    if (processor.layout.view == viewChords)
        return ChordGenPanel::preferredHeight;
    return processor.layout.knobs ? knobRowH : 0;
}

int KeysEditor::arpHeight() const
{
    return arpPanel != nullptr ? arpPanel->preferredHeight() : 0;
}

int KeysEditor::sectionHeight(SectionId id) const
{
    // Folded, or off in a window of its own: either way the section occupies no height here.
    const auto& s = section(id);
    if (! *s.open || *s.detached)
        return 0;

    switch (id)
    {
        case secControls:   return headerH;
        case secCentre:     return centreHeight();
        case secArp:        return arpHeight();
        case secPads:       return processor.layout.padsBig ? padBigRowH : padRowH;
        case secTranscribe:
           #if KEYS_TRANSCRIBE
            return TranscribePanel::idealHeight;
           #else
            return 0;
           #endif
        case secKeyboard:   return dockedKeybedH;
        default:            return 0;
    }
}

int KeysEditor::idealHeight() const
{
    int h = 10; // top margin
    for (int i = 0; i < numSections; ++i)
    {
        if (i > 0)
            h += 6; // gap above the bar
        h += SectionBar::height;
        const int band = sectionHeight((SectionId) i);
        if (band > 0)
            h += 4 + band;
    }
    return h + 10; // bottom margin
}

int KeysEditor::minWidthForView() const
{
    // The generator and the arp carry far more controls than the player, and every one of
    // them has to stay at a full-size target. Grow rather than shrink the targets.
    // 960, not the old 820: the centre bar now also carries Sustain and All Off, and
    // below this the tabs and the pad transport start colliding.
    //
    // Either section asks for the wider floor only while it is docked: in a window of its
    // own it is free to be any width it likes and has no business setting this one.
    const auto& lay = processor.layout;
    const bool wideCentre = lay.centre && ! lay.centreDetached && lay.view == viewChords;
    const bool wideArp = lay.arp && ! lay.arpDetached;
    return (wideCentre || wideArp) ? 1010 : 960;
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

    // Always, and after the above: a fold inside a section (the wheels, the knobs) changes
    // what is *inside* a holder without changing the holder's own bounds, and JUCE only
    // calls resized() on a component whose bounds actually moved. Detached, the holder is
    // not even our child. Without this the wheels vanish and the keys keep their old width.
    for (auto& s : sections)
        if (s.holder.layout)
            s.holder.layout();
    repaint();
}

// ---------------------------------------------------------------------------------------
// Detaching. Every section can be popped into a window of its own: the editor keeps owning
// the components either way and simply re-parents one holder, so nothing below this cares
// where a section currently lives.
// ---------------------------------------------------------------------------------------

void KeysEditor::setSectionDetached(SectionId id, bool detach)
{
    auto& s = section(id);
    if (detach == (s.window != nullptr))
    {
        *s.detached = detach; // already in the asked-for state; just keep the flag honest
        syncSectionControls();
        return;
    }
    *s.detached = detach;

    if (detach)
    {
        // Detaching implies showing it: popping a window that then paints nothing would be a
        // dead end, since the chevron that unfolds it is on a bar in the other window.
        *s.open = true;
        refreshSectionPanels();

        // The Detach button travels with the section, so the window carries the control that
        // undoes it - along with anything else that belongs to the content rather than to
        // the editor around it (the keybed's wheels and Size).
        s.holder.addAndMakeVisible(s.detachButton);
        for (auto& t : s.travellers)
        {
            s.holder.addAndMakeVisible(*t.c);
            t.c->sendLookAndFeelChange(); // some were configured before they were ever parented
        }

        removeChildComponent(&s.holder);
        s.window = std::make_unique<DetachedWindow>(
            s.windowTitle, lnf, s.holder, *s.bounds, s.minSize, s.defaultSize,
            [this, id] { setSectionDetached(id, false); },   // its close button re-docks
            [this, id] { rememberSectionBounds(id); });
    }
    else
    {
        rememberSectionBounds(id);
        s.window.reset(); // clears its content, handing the holder back
        addAndMakeVisible(s.holder);
        addAndMakeVisible(s.detachButton); // back onto the section bar
        for (auto& t : s.travellers)
        {
            if (t.detachedOnly)
                s.holder.removeChildComponent(t.c); // the window's own; nothing docked shows it
            else
                addAndMakeVisible(*t.c);
        }
    }

    syncSectionControls();
    applyLayout();
}

void KeysEditor::rememberSectionBounds(SectionId id)
{
    // Remember where Owen put it, so the next session opens it in the same place.
    auto& s = section(id);
    if (s.window != nullptr)
        *s.bounds = s.window->getBounds();
}

juce::Rectangle<int> KeysEditor::layoutDetachRow(SectionId id, juce::Rectangle<int> row, bool onBar)
{
    // The Detach button and its travellers are in exactly one of the two places at a time:
    // on the section bar while docked, on the window's own strip while detached.
    auto& s = section(id);
    if (onBar == *s.detached)
        return row;

    const int vInset = onBar ? 0 : 2;
    s.detachButton.setBounds(row.removeFromRight(detachWidth).reduced(2, vInset));
    for (auto& t : s.travellers)
    {
        row.removeFromRight(6);
        t.c->setBounds(row.removeFromRight(t.width).reduced(2, vInset));
    }
    return row;
}

juce::Rectangle<int> KeysEditor::holderContent(SectionId id)
{
    auto& s = section(id);
    auto area = s.holder.getLocalBounds();
    if (! *s.detached)
        return area;

    // Detached, the holder owns a whole window, so it carries a strip at the top for the
    // controls that came out with it. Docked, those live on the section bar and this strip
    // does not exist (there is no spare height for it under a 185 px keybed).
    auto strip = area.removeFromTop(SectionBar::height);
    area.removeFromTop(4);
    layoutDetachRow(id, strip.reduced(6, 0), false);
    return area;
}

// ---------------------------------------------------------------------------------------
// Holder layouts. Each one is written once and used in both places the section can be: the
// only thing that changes when it detaches is the size of the rectangle it gets.
// ---------------------------------------------------------------------------------------

void KeysEditor::layoutControlsHolder()
{
    // The holder is the painted band, so the rows sit inside it with the same margins the
    // editor uses. Two rows, down from three: dropping the fixed Velocity slider and the
    // Latch toggle emptied the middle one, so the remaining controls close up.
    auto header = holderContent(secControls).reduced(10, 6);
    if (header.isEmpty())
        return;

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

    cell(rowA, 88, sizeLabel, sizeBox);
    cell(rowA, 70, rootLabel, rootBox);
    cell(rowA, 150, scaleLabel, scaleBox);
    cell(rowA, 120, octaveLabel, octaveSlider);
    toggleCell(rowA, 110, scaleLockButton);
    cell(rowA, 90, polyphonyLabel, polyphonyBox);
    cell(rowA, 70, channelLabel, channelBox);

    toggleCell(rowB, 96, humanizeButton);
    cell(rowB, 208, humanizeVelLabel, humanizeVelSlider);
    cell(rowB, 150, chordStrumLabel, chordStrumSlider);
    cell(rowB, 100, chordStrumDirLabel, chordStrumDirBox);
    cell(rowB, 170, bpmLabel, bpmSlider);
}

void KeysEditor::layoutCentreHolder()
{
    // The holder is the module's raised panel; its content sits inside the bevel.
    const auto area = holderContent(secCentre).reduced(4, 4);
    if (genPanel != nullptr)
        genPanel->setBounds(area);
    knobBank.setBounds(area.withHeight(juce::jmin(area.getHeight(), knobRowH)));
}

void KeysEditor::layoutPadsHolder()
{
    chordPads.setBounds(holderContent(secPads).reduced(4, 4));
}

void KeysEditor::layoutTranscribeHolder()
{
#if KEYS_TRANSCRIBE
    if (transcribePanel != nullptr)
        transcribePanel->setBounds(holderContent(secTranscribe));
#endif
}

void KeysEditor::layoutArpHolder()
{
    if (arpPanel != nullptr)
        arpPanel->setBounds(holderContent(secArp));
}

void KeysEditor::layoutKeybed()
{
    auto area = holderContent(secKeyboard);

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
    chordPads.setEditingSlot(slot); // the pad grows a tick that ends the edit
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
    // is forced on regardless of the button.
    keyboard.setLatch(editingPad >= 0 || apvts.getRawParameterValue("latch")->load() > 0.5f);
    keyboard.setPolyphony((int) apvts.getRawParameterValue("polyphony")->load()); // 0 = unlimited

    // Lifting the sustain pedal releases any pad chords left ringing by it.
    if (! sus && lastSustain)
        processor.stopAllChordPads();
    lastSustain = sus;

    // Switching the arp off releases a chord held into it. That check used to live here, and
    // it had two holes an editor could not close: it was gated on the chord having come from
    // a *pad*, so a chord handed over from the live card was never released, and with no
    // window open nothing polled at all. It belongs to the processor's heartbeat now (see
    // KeysProcessor::heartbeatTick), which owns the chord and runs whether or not anyone is
    // looking.

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

    // Strum is the same shape: two params, one band, the numbers in the label. Both ends
    // equal is a fixed strum, and reads as one number rather than a range of nothing.
    // Sorted before it reaches the slider: the pair can arrive the wrong way round from host
    // automation, and a TwoValue slider handed max < min is not a defined thing to look at.
    const int sa = (int) apvts.getRawParameterValue("chordStrum")->load();
    const int sb = (int) apvts.getRawParameterValue("chordStrumMax")->load();
    const int smin = juce::jmin(sa, sb), smax = juce::jmax(sa, sb);
    chordStrumSlider.setMinAndMaxValues(smin, smax, juce::dontSendNotification);
    chordStrumLabel.setText(smin == smax ? "STRUM  " + juce::String(smin) + " MS"
                                         : "STRUM  " + juce::String(juce::jmin(smin, smax)) + "-"
                                               + juce::String(juce::jmax(smin, smax)) + " MS",
                            juce::dontSendNotification);

    // Pad page: label and the ends of the range.
    const int page = processor.padPage();
    for (int p = 0; p < KeysProcessor::numPadPages; ++p)
        pageButtons[(size_t) p].setToggleState(p == page, juce::dontSendNotification);

    // Keyboard-edit link: the edited pad follows the keyboard's sounding set. An
    // all-notes-removed state is not written (Clear is the explicit wipe), and
    // flipping to another page ends the edit.
    if (editingPad >= 0)
    {
        const int offset = processor.padPageOffset();
        // Folding the pads away takes the tick with them, and the edit would have no exit
        // left. Same rule as flipping to another page: the pad keeps what it already got.
        if (! processor.layout.pads
            || editingPad < offset || editingPad >= offset + KeysProcessor::padsPerPage)
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
    // Notes arriving on the MIDI input count: play a chord on a physical keyboard and the
    // live card names it and can be captured to a pad, which is the whole point of watching
    // the input at all. Merged rather than replaced, so a hand on each keyboard still reads
    // as one chord.
    auto chord = keyboard.soundingOutputNotes();
    for (int n : processor.inputNotes())
        if (std::find(chord.begin(), chord.end(), n) == chord.end())
            chord.push_back(n);
    std::sort(chord.begin(), chord.end());
    chordPads.setCurrentChord(chord);
    chordPads.repaint();
}

void KeysEditor::paint(juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    g.setGradientFill({ skin::bgTop, 0.0f, 0.0f, skin::bgBot, 0.0f, full.getBottom(), false });
    g.fillRect(full);

    // Say where a section went, on its own bar. Below the bar would be the obvious place,
    // but when a section is away the window shrinks until there is no "below" left - the
    // caption was drawn straight off the bottom edge. The zone is whatever the bar's own
    // controls did not use, so it can never land under a button.
    g.setFont(skin::micro(10.0f).withExtraKerningFactor(0.2f));
    for (int i = 0; i < numSections; ++i)
    {
        const auto& s = sections[(size_t) i];
        // "HIDDEN" only for the keyboard: a missing keybed is dramatic enough to be worth
        // saying out loud, whereas every other folded bar has its chevron and nothing else
        // to explain.
        const bool away = *s.detached;
        const bool hidden = ! *s.open && (SectionId) i == secKeyboard;
        if ((! away && ! hidden) || s.caption.getWidth() < 90)
            continue;
        g.setColour(skin::textFaint);
        g.drawText(away ? "IN ITS OWN WINDOW" : "HIDDEN", s.caption.withTrimmedRight(4),
                   juce::Justification::centredRight);
    }
}

void KeysEditor::resized()
{
    auto area = getLocalBounds().reduced(10);

    // Top to bottom: every section is a bar, and - unless it is folded or off in a window of
    // its own - a band of content under it. The keybed is last and takes whatever is left, so
    // extra window height reads as instrument body under a bottom-anchored keyboard.

    // --- Controls -------------------------------------------------------------------
    controlsBar.setBounds(area.removeFromTop(SectionBar::height));
    {
        auto bar = layoutDetachRow(secControls, controlsBar.contentArea(), true);
        bar.removeFromRight(6);
        themeButton.setBounds(bar.removeFromRight(112).reduced(2, 0));
        bar.removeFromRight(6);
        if (updateButton.isVisible())
            updateButton.setBounds(bar.removeFromRight(170).reduced(0, 1));
        section(secControls).caption = bar;
    }
    if (const int h = sectionHeight(secControls); h > 0)
    {
        area.removeFromTop(4);
        const auto band = area.removeFromTop(h);
        // Full width and a little past the rows: the holder paints the header band, and the
        // band has always bled into the margins the rest of the editor keeps.
        controlsHolder.setBounds(getLocalBounds().withY(band.getY() - 6).withHeight(h + 12));
    }

    // --- Centre ---------------------------------------------------------------------
    area.removeFromTop(6);
    centreBar.setBounds(area.removeFromTop(SectionBar::height));
    section(secCentre).caption = layoutToolRow(layoutDetachRow(secCentre, centreBar.contentArea(), true));
    if (const int h = sectionHeight(secCentre); h > 0)
    {
        area.removeFromTop(4);
        // Expanded, because the holder *is* the module's raised panel and that has always
        // sat a few pixels proud of the content it carries.
        centreHolder.setBounds(area.removeFromTop(h).expanded(4, 4));
    }

    // --- Arp ------------------------------------------------------------------------
    area.removeFromTop(6);
    arpBar.setBounds(area.removeFromTop(SectionBar::height));
    {
        // On sits on the bar, so it survives folding the section away.
        auto bar = layoutDetachRow(secArp, arpBar.contentArea(), true);
        bar.removeFromRight(6);
        arpOnButton.setBounds(bar.removeFromRight(70).withSizeKeepingCentre(68, 24));
        section(secArp).caption = bar;
    }
    if (const int h = sectionHeight(secArp); h > 0)
    {
        area.removeFromTop(4);
        arpHolder.setBounds(area.removeFromTop(h));
    }

    // --- Pads -----------------------------------------------------------------------
    area.removeFromTop(6);
    padsBar.setBounds(area.removeFromTop(SectionBar::height));
    {
        // The page buttons ride on the Pads bar, where they used to sit in a row of their
        // own under the strip. One click still reaches any page, and the section keeps its
        // height for pads instead of spending 34 px on a transport.
        auto bar = layoutDetachRow(secPads, padsBar.contentArea(), true);
        for (auto& b : pageButtons)
        {
            b.setBounds(bar.removeFromLeft(46).reduced(1, 2));
            bar.removeFromLeft(4);
        }
        bar.removeFromLeft(10);
        padsBigButton.setBounds(bar.removeFromLeft(62).reduced(1, 2));
        section(secPads).caption = bar;
    }
    if (const int h = sectionHeight(secPads); h > 0)
    {
        area.removeFromTop(4);
        padsHolder.setBounds(area.removeFromTop(h).expanded(4, 4));
    }

    // --- Transcribe -----------------------------------------------------------------
    area.removeFromTop(6);
    transcribeBar.setBounds(area.removeFromTop(SectionBar::height));
    section(secTranscribe).caption = layoutDetachRow(secTranscribe, transcribeBar.contentArea(), true);
    if (const int h = sectionHeight(secTranscribe); h > 0)
    {
        area.removeFromTop(4);
        transcribeHolder.setBounds(area.removeFromTop(h));
    }

    // --- Keyboard -------------------------------------------------------------------
    area.removeFromTop(6);
    keyboardBar.setBounds(area.removeFromTop(SectionBar::height));
    {
        // The Keyboard bar carries two kinds of control, and they behave differently when
        // the section folds. Wheels and Detach belong to the keybed, so they go with it
        // (detached, they are not our children at all - the holder places them in that
        // window). Exclusive, Sustain and All Off are what you reach for *while playing*,
        // so they stay put whatever the keybed is doing.
        auto bar = layoutDetachRow(secKeyboard, keyboardBar.contentArea(), true);
        bar.removeFromRight(14);
        // 24 px tall like Sustain and Exclusive beside it, not the old reduced(0, 3) which
        // left it 20. The skin's button font scales with height, so those 4 px were also
        // shrinking the label: All Off was the odd one out on the bar for no reason.
        panicButton.setBounds(bar.removeFromRight(84).withSizeKeepingCentre(84, 24));
        bar.removeFromRight(6);
        latchButton.setBounds(bar.removeFromRight(78).withSizeKeepingCentre(76, 24));
        bar.removeFromRight(6);
        sustainButton.setBounds(bar.removeFromRight(96).withSizeKeepingCentre(94, 24));
        bar.removeFromRight(6);
        chordExclusiveButton.setBounds(bar.removeFromRight(104).withSizeKeepingCentre(102, 24));
        section(secKeyboard).caption = bar;
    }
    if (sectionHeight(secKeyboard) > 0)
    {
        area.removeFromTop(4);
        keybedHolder.setBounds(area); // the slack is instrument body under the keys
    }
}

juce::Rectangle<int> KeysEditor::layoutToolRow(juce::Rectangle<int> row)
{
    // The centre bar's content: which view is showing, then whatever that view folds. Both
    // tabs are always there (they are how a folded centre comes back); Knobs is all the
    // Perform view has left to fold now the pads and the arp have sections of their own.
    performButton.setBounds(row.removeFromLeft(78).reduced(0, 2));
    row.removeFromLeft(4);
    chordsButton.setBounds(row.removeFromLeft(72).reduced(0, 2));
    row.removeFromLeft(14);

    knobsButton.setBounds(row.removeFromLeft(66).reduced(0, 2));
    return row;
}

} // namespace keys
