#include "PluginEditor.h"
#include "Chords.h"
#include "ScaleModes.h"
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

    // The generator's mode names as a bar-width combo box can show them: the parenthetical
    // alias is dropped, so "Natural Minor (Aeolian)" reads "Natural Minor" and the widest item
    // becomes "Pentatonic Major". Same list, same order, same indices as modes::names() and so
    // as the "genMode" parameter's own choices - only the alias goes, which is why this is not
    // a second vocabulary to keep in step with the first. Spelling them out in full would want
    // a 186 px combo for one mode's alias, on a bar with 300 px for three controls.
    juce::StringArray barModeNames()
    {
        juce::StringArray out;
        for (const auto& n : modes::names())
            out.add(n.upToFirstOccurrenceOf(" (", false, false).trim());
        return out;
    }

    // Scale Compliance is a continuous 0-100 parameter; on the bar it is these five steps. The
    // card menu used to offer the same ladder, out of a ChordGenMenu::ladder helper; both went
    // when the settings moved into the generator's own window on 2026-07-30, and the window has
    // the whole range on a slider. So this is the only ladder left, and the bar shows the step
    // nearest whatever the slider was last set to - see genComplianceBox for why that rounding
    // costs the bar its ComboBoxAttachment.
    juce::StringArray complianceItems() { return { "0 %", "25 %", "50 %", "75 %", "100 %" }; }

    // Fixed heights of the editor's bands, shared by idealHeight() and resized() so the
    // window the folds ask for and the layout they get can never drift apart.
    constexpr int rowH = 46;                          // one row of header controls
    constexpr int headerH = 14 + rowH * 2 + 6;        // both of them, plus label lead-in
    // The knob row, the bottom band of the Controls section. KnobBank::resized() spends 6 + 6
    // on the outer inset, 34 on the CC label button and 4 on the gap above it, so the knob
    // gets knobRowH - 50: 110 makes it 60 px square, and 98 makes it exactly 48.
    //
    // It was 98 for a while, on the reading that 48 is KnobBank's floor so anything above it
    // is slack. It is not slack. 48x48 is the kit's *recommended minimum* for a rotary
    // (okstudio/RotaryKnob.h), deliberately above okstudio::ui::minHitPx, because a knob's
    // usable drag arc shrinks faster than a linear slider's track does as the control gets
    // smaller - and Keys is played by one mouse, so a drag target is worth more here than a
    // click target. Eight rotaries at 48 have 36% less area than eight at 60, which is a bad
    // trade for 12 px of window height. 110 it is, and the floor stays a floor.
    constexpr int knobRowH = 110;
    constexpr int knobGap = 6;       // between the header rows and the knob row
    // Two rows of eight, each card carrying its chord's name and the notes under it. One
    // arrangement now: a Big switch here gave four rows of four at 286 px, and it went on
    // 2026-07-31 when the note list moved onto the short card, which is what anyone was
    // turning Big on to read.
    constexpr int padRowH = 96;
    // The keybed. PianoKeyboard caps a white key at 185 px docked and anchors the keys to the
    // bottom, so 185 is the floor: below it the keys themselves shrink. Nothing needs room
    // above them - the fallboard rail and its shadow are painted *downwards* from the top of
    // the keys, over them - so the remaining 4 px is a sliver of instrument body and not a
    // clearance. It was 212. Any window taller than idealHeight() hands the slack to this
    // section, so the body grows from here rather than being reserved up front.
    constexpr int dockedKeybedH = 189;
    constexpr int detachWidth = 104;                  // the Detach / Re-dock button

    // The tallest the editor may be dragged. It has to clear idealHeight()'s worst case,
    // because applyLayout() passes that same worst case in as the *minimum*: at 1400 the two
    // crossed over and every fully-open layout asked JUCE for a minimum above its maximum.
    //
    // Worst case, everything open and docked, knobs on, the arp in Pattern shape (the one
    // that opens the step editor):
    //     margins            10 + 10                        =   20
    //     four bars          4 * SectionBar::height (34)     =  136
    //     three gaps         3 * 6                           =   18
    //     Controls           4 + headerH 112 + 6 + 110       =  232
    //     Arp                4 + ArpPanel::preferredHeight() =  584   (arpPatternH 564 + 16)
    //     Pads               4 + padRowH                     =  100
    //     Keyboard           4 + dockedKeybedH               =  193
    //                                                          ----
    //                                                          1283
    // It was 1473 while Big cards existed and the Pads line could read 290. 1800 leaves room
    // for the arp to grow a lane row or two without this becoming a bug again, and the slack
    // above idealHeight() is all instrument body under the keys.
    constexpr int maxEditorHeight = 1800;

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

    // The raised panel a performance module floats on. The arp draws its own card; the knob
    // bank and the pad strip do not, so their holders draw it behind them.
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
      arpHolder(section(secArp).holder),
      padsHolder(section(secPads).holder),
      keybedHolder(section(secKeyboard).holder),
      keyboard(p), knobBank(p), chordPads(p), chordGen(p)
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
                case secArp:        layoutArpHolder(); break;
                case secPads:       layoutPadsHolder(); break;
                case secKeyboard:   layoutKeybed(); break;
                default: break;
            }
        };
        addAndMakeVisible(s.holder);
    };

    // Controls grew the knob row on 2026-07-30, so its floor grew with it. Bottom up: the two
    // header rows and the knob row (headerH + knobGap + knobRowH = 228), the 12 px the holder
    // insets them by, the 38 px strip a detached holder carries for its own Detach button,
    // the window's 38 px title bar and its 8 px of resizable border. 324, so 330.
    wire(secControls, controlsBar, lay.controls, lay.controlsDetached, lay.controlsDetachedBounds,
         "Controls", "Keys Controls", { 900, 330 }, { 980, 370 });
    wire(secArp, arpBar, lay.arp, lay.arpDetached, lay.arpDetachedBounds,
         "Arp", "Keys Arpeggiator", { 900, 300 }, { 1100, 520 });
    wire(secPads, padsBar, lay.pads, lay.padsDetached, lay.padsDetachedBounds,
         "Pads", "Keys Chord Pads", { 620, 180 }, { 940, 300 });
    wire(secKeyboard, keyboardBar, lay.keyboard, lay.detached, lay.detachedBounds,
         "Keyboard", "Keys Keyboard", { 420, 190 }, { 1000, 300 });

    // Wheels and the second Size selector belong to the keybed, not to the window it happens
    // to be in, so they follow it out. Owen asked for this: with them left behind, the
    // keyboard window had nothing on it but a close box.
    section(secKeyboard).travellers = { { &wheelsButton, 84, false }, { &detachedSizeBox, 104, true } };

    // The pad strip paints no card of its own, so its holder draws one behind it.
    padsHolder.painter = [this](juce::Graphics& g)
    {
        if (chordPads.isVisible())
            paintModule(g, chordPads.getBounds().expanded(4, 4));
    };
    // The header band, the wordmark under the title, and the raised card under the knob row.
    // Painted by the holder rather than by the editor so it travels with the section instead
    // of being left behind as a gradient over nothing. The knobs get their card from here too
    // now that they are the bottom row of this band: their own holder used to draw it, and
    // without this they would sit straight on the header gradient with no module under them.
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

        if (knobBank.isVisible())
            paintModule(g, knobBank.getBounds().expanded(4, 4));
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
                             "that is already ringing strikes it again. Right-click a ringing "
                             "key to drop just that one; All Off stops the lot.");
    latchButton.setTooltip("Click a key to hold it, click it again to release it. Use this to "
                           "build a chord a note at a time, or to take one apart.");
    // Accessible name, not the button text: the arp panel has a Latch of its own (arpLatch),
    // and both are in the tree whenever the arp is open. UI Automation takes the first match,
    // so two controls reading "Latch" is the same collision the per-section Detach names exist
    // to avoid - the screenshot script would toggle whichever one it happened to reach first.
    latchButton.setTitle("Latch keys");
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
    // The knobs are a row of the Controls band, not a section: their old one held nothing
    // else once the arp and the generator moved out, and a bar plus a gap plus a caption is
    // 44 px the window was spending on a chevron for one row (2026-07-30).
    controlsHolder.addAndMakeVisible(knobBank);

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

    // The generator's bulk actions, on the same bar. They were buttons on a panel that no
    // longer exists, and they had to survive it: a right-click menu is not a left click, so
    // without these the only way to fill a page would be sixteen New chords one card at a
    // time. On the bar they cost no height at all, which is the trade that let the panel go.
    //
    // Two of them, not three. **Clear** was here and is in the generator's window now: it
    // empties every unlocked pad on the page, there is no undo anywhere in Keys, and it was
    // sitting 4 px from Regen and a few px from the page buttons - the two things on this bar
    // a user clicks constantly. Fill and Regen stay because they are constructive and they are
    // the only left-click path into generation; a destructive bulk action is worth the extra
    // click of opening a window. It spent a few hours on the card menu in between, which is
    // where the older comments about it put it. `ChordGenMenu::clearPage()` is unchanged
    // throughout, only what reaches it.
    //
    // Each carries a setTitle: "Fill" and "Regen" are unique today, but an accessible name is
    // what the capture script drives (`scripts/capture-window.ps1 -InvokeButtons`) and UI
    // Automation takes the first match, so the ones that can be automated say what they do.
    const auto genChip = [this](juce::TextButton& b, const juce::String& name,
                                const juce::String& tip, std::function<void()> action)
    {
        b.setTitle(name);
        b.setTooltip(tip);
        b.onClick = std::move(action);
        addAndMakeVisible(b);
    };
    // Fill is the safe one and Regen is the destructive one, and the tooltips say so in those
    // words: Fill only ever writes empty pads, Regen replaces chords. Each also greys out when
    // it would find nothing to do (see timerCallback), which says the same thing without a
    // hover.
    genChip(fillButton, "Fill chord page",
            "Fill the empty pads on this page. Nothing already on the page is touched.",
            [this] { chordGen.fillPage(); });
    genChip(regenButton, "Regenerate unlocked chords",
            "Replace the chords on this page with new ones, except on locked pads. This is the "
            "one that overwrites; lock a card to keep it.",
            [this] { chordGen.regeneratePage(); });
    // The third chip is the door to everything else: Octave, Source, note counts, inversions,
    // Lock Influence, the Markov chains and the audition tray, in a window of their own. A second
    // click while it is already up raises it rather than building another - there is exactly
    // one of these, and a window you cannot find is worse than one that is shut.
    // Where a clicked chord card goes. Not a combo: three values, and a chip that cycles is
    // one click where a combo is a click, a travel and a second click - the same argument the
    // < > steppers beside Shape are made of.
    genChip(arpTargetButton, "Arp target line",
            "Which arpeggiator line a click on a chord card feeds. Click to cycle A, B, C. "
            "The same choice as the A/B/C tabs in the arp panel, kept here so it is reachable "
            "with the arp folded away.",
            [this] { cycleArpTargetLine(); });
    refreshArpTargetButton();
    genChip(chordGenButton, "Chord generator window",
            "Open the chord generator: every setting it has, plus a tray of chords to audition. "
            "It is a window of its own, so it can sit beside the plugin while you audition.",
            [this]
            {
                if (chordGenWindow != nullptr)
                    chordGenWindow->toFront(true);
                else
                    setChordGenWindowOpen(true);
            });

    // The three settings that get changed while you are auditioning a page, on the bar beside
    // the two chips (see the member declarations for why these three, and why Compliance is the
    // one that does not take a ComboBoxAttachment). The accessible names all say "Generator" or
    // "Scale compliance": the Controls section already
    // has combos titled "Root" and "Scale", both alive at the same time as these, and UI
    // Automation takes the first match.
    const auto genCombo = [this](juce::ComboBox& box, const juce::String& name, const juce::String& tip,
                                 const juce::StringArray& items, const char* paramID,
                                 std::unique_ptr<ComboAtt>& att)
    {
        box.addItemList(items, 1);
        box.setTitle(name);
        box.setTooltip(tip);
        addAndMakeVisible(box);
        att = std::make_unique<ComboAtt>(processor.apvts, paramID, box);
    };
    genCombo(genRootBox, "Generator key",
             "The key the chord generator writes in. Separate from the Root that drives Scale "
             "Lock; also on a pad's right-click menu.",
             okstudio::scales::noteNames(), "genRoot", genRootAtt);
    genCombo(genModeBox, "Generator mode",
             "The mode the chord generator writes in, which decides the quality of every "
             "degree. Also on a pad's right-click menu, where each mode carries its character.",
             barModeNames(), "genMode", genModeAtt);
    // Compliance is the odd one out on this bar and takes no ComboBoxAttachment. Its parameter
    // is a continuous 0-100 while this box is five steps of it, so the box shows the nearest
    // step: at 60, set from the window's slider, it reads "50 %". An attachment then made
    // picking "50 %" do nothing at all - juce::ComboBox swallows a pick of the item already
    // showing, so the write never happened and 50 was unreachable from the bar (see
    // StepComboBox.h). So the two directions are wired separately: a plain ParameterAttachment
    // reads the parameter back onto the box, and onPick writes it. setValueAsCompleteGesture is
    // one begin/set/end, so no pick can leave a host automation gesture open, and a move on the
    // window's slider still lands here through the attachment.
    genComplianceBox.addItemList(complianceItems(), 1);
    genComplianceBox.setTitle("Scale compliance");
    genComplianceBox.setTooltip("How far outside the key the generator may wander: 100 % is "
                                "diatonic only, and each step down lets in borrowed chords, "
                                "secondary dominants, then chromatic ones. Five steps of a "
                                "continuous setting: the generator window has the fine control, "
                                "and this shows the step nearest to it.");
    addAndMakeVisible(genComplianceBox);
    if (auto* complianceParam = processor.apvts.getParameter("genCompliance"))
    {
        const float steps = (float) (complianceItems().size() - 1);
        genComplianceAtt = std::make_unique<juce::ParameterAttachment>(
            *complianceParam,
            [this, steps](float v)
            {
                const int i = juce::jlimit(0, (int) steps, juce::roundToInt(v / 100.0f * steps));
                genComplianceBox.setSelectedItemIndex(i, juce::dontSendNotification);
            });
        genComplianceBox.onPick = [this, steps](int id)
        { genComplianceAtt->setValueAsCompleteGesture(100.0f * (float) (id - 1) / steps); };
        genComplianceAtt->sendInitialUpdate();
    }

    // On rides on the Arp *bar*, not inside the section, so folding the editor away does not
    // take the arp's power switch with it. Same reasoning as Sustain and All Off living on
    // the Keyboard bar.
    // One chip per line, so bringing a line in or out of a polyrhythm is a single click on a
    // bar that is still there with the section folded. A is the switch that has always been
    // here, under the same parameter; B and C are new and start off, which is what makes a
    // session saved before them sound identical.
    for (int n = 0; n < KeysProcessor::numArpLines; ++n)
    {
        const auto letter = juce::String::charToString((juce::juce_wchar) ('A' + n));
        auto& b = arpOnButtons[(size_t) n];
        b.setButtonText(letter);
        // Distinct accessible names: three buttons reading "On" are three identical names to
        // UI Automation, which takes the first match (see the Detach buttons for the same rule).
        b.setTitle("Arp line " + letter);
        b.setTooltip("Arpeggiator line " + letter + ". Lit, it arpeggiates what you play and "
                     "whatever chord card you send it. Three lines at three rates is the "
                     "polyrhythm; Hold off, at the end of this bar, lets all three go.");
        addAndMakeVisible(b);
        arpOnAtts[(size_t) n] = std::make_unique<ButtonAtt>(
            processor.apvts, KeysProcessor::arpParamId(n, KeysProcessor::apOn), b);
    }

    // Hold off rides the same bar, for the same reason (see the member declaration). It is
    // the only exit from a held chord that is on screen in the default layout, so it cannot
    // be inside the section it belongs to.
    arpHoldOffButton.setTitle("Arp hold off"); // "Hold off" alone says nothing to automation
    arpHoldOffButton.setTooltip("Let go of the chord being held into the arpeggiator, and stop "
                                "the Chain if it is running. The arp keeps running and goes back "
                                "to arpeggiating whatever you play. Greyed out when nothing is "
                                "held and nothing is chaining.");
    arpHoldOffButton.setEnabled(false); // the timer owns this from here on
    // releaseArpHold, not releaseArpChord: the latter leaves the chain running, which relaunches
    // the next slot at the following bar line and puts a chord straight back. See the processor.
    arpHoldOffButton.onClick = [this] { processor.releaseArpHold(); };
    addAndMakeVisible(arpHoldOffButton);

    themeButton.setTooltip("Colour this instance, to tell it from Keys on other tracks.");
    themeButton.setTitle("Theme");
    themeButton.onClick = [this] { showThemeMenu(); };
    addAndMakeVisible(themeButton);

    // The detached keyboard's own Size selector (see the member declaration for why).
    detachedSizeBox.addItemList(sizeItems(), 1);
    // "Keybed size", not "Size": addCombo already titles the Controls-section one "Size", and
    // both exist at once while the keyboard is detached. UI Automation takes the first match,
    // so a script setting one would have been writing to whichever it found.
    detachedSizeBox.setTitle("Keybed size");
    detachedSizeBox.setTooltip("How many keys the keybed shows.");
    detachedSizeAtt = std::make_unique<ComboAtt>(processor.apvts, "size", detachedSizeBox);

    const auto chip = [this](juce::TextButton& b, bool& flag, const juce::String& tip)
    {
        b.setClickingTogglesState(true);
        b.setTooltip(tip);
        b.onClick = [this, &b, &flag] { flag = b.getToggleState(); applyLayout(); };
        addAndMakeVisible(b);
    };
    chip(knobsButton, lay.knobs, "Show or hide the eight CC knobs, the bottom row of "
                                 "the controls band.");
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

    // The generator's half of a pad's card menu - New chord and what could follow it, the two
    // things that are about one card. Unconditional, and it has to stay that way: `chordGen` is
    // a member, so there is no window whose absence could take these items off the menu. They
    // were offered only while the Chords view was up once, and that is the exact bug this
    // arrangement exists to prevent. Everything about the page or the settings is in the
    // generator's window instead (chordGenButton, above).
    // A card leaving the strip is offered to the generator's reference box, when that window is
    // open. Guarded on the panel existing rather than wired and unwired as it opens: the strip
    // outlives every window, and a hook that had to be taken back down on close is a hook that
    // gets left dangling one day.
    // ...and to the arp panel, which is the left-click twin "Send to arp slot" never had: a
    // drag is a target picker, which is the whole reason that menu item was allowed to be
    // right-click-only. Drop on a slot to bind the chord there; drop on a line tab to hand it
    // to that line now. Both live here rather than in either surface because the editor is the
    // one object that holds both, and either can be in a window of its own.
    chordPads.onDragOutside = [this](juce::Point<int> p)
    {
        if (chordGenPanel)
            chordGenPanel->showReferenceDropTarget(p);
        if (arpPanel)
            arpPanel->setExternalDropTarget(arpPanel->externalDropSlotAt(p),
                                            arpPanel->externalDropLineAt(p));
    };
    chordPads.onDropOutside = [this](juce::Point<int> p, const KeysProcessor::ChordPad& pad)
    {
        if (arpPanel != nullptr)
        {
            arpPanel->setExternalDropTarget(-1, -1);
            const int line = arpPanel->externalDropLineAt(p);
            if (line >= 0 && ! pad.notes.empty())
            {
                // Straight into that line, and it becomes the current one: you aimed at it, so
                // the next card click should follow the same aim.
                processor.holdArpChordFromPad(chordPads.draggedSlot(), line);
                arpPanel->setEditLine(line);
                refreshArpTargetButton();
                return true; // suppresses the strip's drag-off-to-clear
            }
            if (const int slot = arpPanel->externalDropSlotAt(p); slot >= 0 && ! pad.notes.empty())
            {
                processor.setArpSlotChord(slot, pad.notes, pad.name, arpPanel->editLine());
                arpPanel->repaint();
                return true;
            }
        }
        return chordGenPanel != nullptr && chordGenPanel->offerReferenceDrop(p, pad);
    };

    chordPads.onExtraMenuItems = [this](int slot, juce::PopupMenu& m) { chordGen.addPadMenuItems(slot, m); };
    chordPads.onExtraMenuChoice = [this](int slot, int id) { chordGen.handlePadMenuChoice(slot, id); };

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
    // The floor is everything folded away: four bars and the margins. What used to be
    // the minimum (560) is now roughly the *default*, and Owen can go far below it.
    // The ceiling is stated once, in maxEditorHeight - see there for why 1400 was a bug.
    setResizeLimits(820, 150, 2600, maxEditorHeight);
    setSize(980, 724);

    // Children configured before they were parented (slider textboxes especially)
    // baked colours from the default LookAndFeel; re-resolve everything under ours.
    sendLookAndFeelChange();

    // Restore the folds this session was saved with, then pop back out whichever sections
    // were left in windows of their own.
    applyAccent(lay.accent); // before the first layout, so nothing paints cyan then repaints
    syncSectionControls();
    refreshSectionPanels();
    applyLayout();
    for (int i = 0; i < numSections; ++i)
        if (*sections[(size_t) i].detached)
            setSectionDetached((SectionId) i, true);
    // And the generator's window, if the session was saved with it up. After the sections, so
    // it opens in front of them.
    if (lay.chordGen)
        setChordGenWindowOpen(true);

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
    // Same rule for the generator's window, and the same order: the window holds the panel as
    // non-owned content, so it has to unwind first.
    rememberChordGenBounds();
    chordGenWindow.reset();
    chordGenPanel.reset();
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

void KeysEditor::refreshSectionPanels()
{
    // Called on every fold and every detach. The arp is the only section left with a panel
    // to build or throw away: the knob bank is cheap enough to keep alive behind a fold, and
    // the generator has no panel at all any more.
    refreshArpPanel();
}

void KeysEditor::cycleArpTargetLine()
{
    const int next = (processor.arpCurrentLine() + 1) % KeysProcessor::numArpLines;
    processor.setArpCurrentLine(next);
    // Through the panel when it is open, so its tabs, its attachments and its step lanes all
    // move with the chip. setEditLine writes the processor too, which is harmlessly the value
    // it already holds.
    if (arpPanel != nullptr)
        arpPanel->setEditLine(next);
    refreshArpTargetButton();
    chordPads.repaint(); // the corner marks say which line each card was last sent to
}

void KeysEditor::refreshArpTargetButton()
{
    const int line = processor.arpCurrentLine();
    arpTargetButton.setButtonText(juce::String::charToString((juce::juce_wchar) ('A' + line)));
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
    // The tabs and the Pads-bar chip are one state behind two surfaces, so each has to move
    // the other. This is the tabs' half; cycleArpTargetLine is the chip's.
    arpPanel->onEditLineChanged = [this] { refreshArpTargetButton(); };
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
    if (chordGenWindow != nullptr)
        chordGenWindow->sendLookAndFeelChange(); // its own tree too, sharing the same lnf

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
        // Four buttons reading "Detach" are four identical accessible names, which is no use
        // to a screen reader and made UI Automation pick whichever it found first. Say which
        // section each one moves.
        s.detachButton.setTitle(s.detachButton.getButtonText() + " " + s.name);
        // Detach goes away with the section it detaches (2026-07-27, Owen's ask). Folded
        // away, it was the loudest thing left on a bar whose whole job is to be quiet, and
        // it offered a gesture with nothing behind it: detaching a folded section built a
        // window that opened hidden. Every other control on a bar already hides with its
        // section - the pad pages, the Knobs chip, Wheels - so this was the odd one out
        // rather than a rule being broken. The deliberate exceptions stay: the arp's On
        // (folding the panel must not stop the arpeggiator) and the theme swatch (it belongs
        // to the plugin, not the section).
        s.detachButton.setVisible(*s.open);
        // A holder is visible whenever its section is open, wherever it is parented: being
        // detached is a change of parent, not of visibility. Folding a detached section
        // hides its window instead of its slot, so one control means one thing.
        s.holder.setVisible(*s.open);
        if (s.window != nullptr)
            s.window->setVisible(*s.open);
    }

    knobsButton.setToggleState(lay.knobs, juce::dontSendNotification);
    wheelsButton.setToggleState(lay.wheels, juce::dontSendNotification);

    // The theme button is its own swatch: it wears the colour it sets, so the control and
    // the thing it controls are the same object.
    const auto ac = skin::accentAt(lay.accent);
    themeButton.setButtonText(skin::accentChoices()[juce::jlimit(0, skin::numAccents - 1, lay.accent)].name);
    themeButton.setColour(juce::TextButton::buttonColourId, ac.deep);
    themeButton.setColour(juce::TextButton::textColourOffId, ac.hot);

    // Knobs rides the Controls bar and folds the bottom row of that section, so it hides
    // with it: a chip that hid a row of a band that is not on screen would be a control with
    // nothing behind it. The bank itself only has to answer for its own fold - the holder is
    // already hidden with the section, and being detached is a change of parent, not of
    // visibility.
    knobsButton.setVisible(lay.controls);
    knobBank.setVisible(lay.knobs);

    // The pads' page buttons stay on their bar in the main window even when the strip is
    // off in one of its own: they page the cards, and paging from the window you are
    // already looking at is one click either way.
    for (auto& b : pageButtons)
        b.setVisible(lay.pads); // pointless without pads
    // Fill, Regen, the Generator button and the three combos beside them are deliberately
    // *not* in the list above, and are never hidden: they are the arp On of this bar. Fill and
    // Regen used to hide with the strip, on the reasoning that generating into cards you cannot
    // see is not a thing anyone means to do - but the other way into the generator was a
    // right-click on a pad card, which is gone with the same fold, so folding Pads away made
    // the whole generator unreachable. Generating into a folded strip is a fine thing to mean:
    // you unfold and the page is written. Losing the feature is not. The Generator button
    // inherits exactly that and needs it most: it is the only door to the settings.
    //
    // It carries a lit state rather than a fold, and nothing here owns that - setChordGenWindowOpen
    // does, because the window can also be closed from its own title bar.

    wheelsButton.setVisible(lay.keyboard);
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &modWheel, &modLabel, &pitchWheel, &pitchLabel })
        c->setVisible(lay.wheels);

    // Detached, the keybed owns its whole window, so the piano proportions cap comes off
    // and dragging the window taller genuinely makes the keys taller.
    keyboard.setKeyHeightCap(lay.detached ? 4000.0f : 185.0f);
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
        // The two header rows, plus the knob row when it is unfolded. This one expression is
        // the whole answer for Controls: idealHeight() sums it, resized() hands it back to the
        // holder, and layoutControlsHolder() carves it up in the same order. Write the
        // arithmetic anywhere else and the window is the wrong size with nothing to say so.
        case secControls:   return headerH + (processor.layout.knobs ? knobGap + knobRowH : 0);
        case secArp:        return arpHeight();
        case secPads:       return padRowH;
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
    // One floor now, and the Pads bar is what set it (2026-07-30). It used to be 960, with the
    // arp asking 1010 while it was docked because it carries far more controls than the player
    // and every one of them has to stay at a full-size target. Then Root, Mode and Compliance
    // joined Fill and Regen on the Pads bar, and that bar came out wanting the same 1010, folded
    // or not - its controls are laid out whether or not the strip is open, and the three new
    // ones never hide at all. So the two floors met and there is a single number again.
    //
    // The arithmetic, at the bar's own contentArea() (the window less 20 of margin, less the
    // 92 px fold zone and the 8 px each side of it):
    //     right   Detach 104, 6, Regen 70, 4, Fill 62, 6, Generator 90, 10,
    //             Compliance 74, 6, Mode 148, 6, Key 58                        = 644
    //     left    four pages at 46 + 4, 14                                     = 214
    //                                                                    total = 858
    // 1070 hands it 942, so the bar fits with 84 px of caption zone left over when the section
    // is docked. Nothing is drawn in that zone while it is: paint() only writes "IN ITS OWN
    // WINDOW" there, needs 90 px for it, and gets Detach's 104 back the moment the section
    // leaves - which is 188 and fits.
    //
    // The left group was 286 until the Big switch left it (2026-07-31). The floor stays at
    // 1070 rather than following it down: what set 1070 is the right-hand group, which is the
    // whole generator's reach and has not moved.
    //
    // It was 1010 until 2026-07-30, and moved when the Generator button joined Fill and Regen
    // on this bar. The floor is worth spending on that: everything on this end of the bar
    // survives folding the pads away, so it is the whole generator's reach.
    //
    // The knob bank does not raise it. It wants 532 px (eight columns of 64, so each knob
    // clears the kit's 48 px rotary floor after the column's own 16 px of inset), and the
    // Controls holder is the full window width less 20, so this hands it 1050.
    return 1070;
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
        setResizeLimits(minWidthForView(), h, 2600, maxEditorHeight);

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

void KeysEditor::rememberChordGenBounds()
{
    if (chordGenWindow != nullptr)
        processor.layout.chordGenBounds = chordGenWindow->getBounds();
}

// The chord generator's window. It reuses DetachedWindow rather than adding a second window
// class: everything that window does is wanted here too - the skinned 38 px title bar with
// mouse-only-sized buttons, resize limits, remembering its frame as it is dragged, and the
// off-screen clamp that stops a session saved on another display coming back somewhere Owen
// can never reach with a mouse.
//
// It is not a section, and deliberately not in `sections`: it never docks, so it has no bar,
// no fold, no caption and no Detach button, and every one of those is something the table
// walks. What it shares with the sections is the contract, not the plumbing.
//
// The panel is built here and destroyed here, which is the whole lifetime. It holds a 15 Hz
// timer for its own display and nothing else - no note, no preview, no audio device - so the
// only thing a close has to unwind is the timer, and ~ChordGenPanel does that. The suggestion
// audition, which is the one path in the generator that calls noteOn with no pad behind it,
// belongs to `chordGen` and is stopped by its 800 ms timer or its destructor; `chordGen`
// outlives every window, so no close can strand a preview note.
void KeysEditor::setChordGenWindowOpen(bool open)
{
    processor.layout.chordGen = open;
    if (open == (chordGenWindow != nullptr))
        return; // already in the asked-for state

    if (open)
    {
        chordGenPanel = std::make_unique<ChordGenPanel>(processor, chordGen);
        // Both ways out run this same call: the panel's Close button and the title bar's X.
        // Deferred a message-loop turn because both of them are *inside* what it destroys -
        // a Button's click callback returns to the button, and the window's own
        // closeButtonPressed returns to the window.
        juce::Component::SafePointer<KeysEditor> safe(this);
        const auto close = [safe]
        {
            juce::MessageManager::callAsync([safe]
            {
                if (auto* e = safe.getComponent())
                    e->setChordGenWindowOpen(false);
            });
        };
        chordGenPanel->onClose = close;

        // The audition tray's drag reaches the pad strip through here, and only through here.
        // The tray is in this window's *sibling*, so JUCE keeps the whole gesture on the tray
        // and neither component can see the other; the editor is the one object that holds
        // both, which is what makes it the place the two ends meet. Screen coordinates all the
        // way across - see ChordTray. ChordPads owns the hit test, the refusals and the paint.
        chordGenPanel->onCandidateDragOver = [this](juce::Point<int> p)
        { chordPads.setExternalDropSlot(chordPads.externalDropSlotAt(p)); };
        chordGenPanel->onCandidateDropped = [this](juce::Point<int> p, const KeysProcessor::ChordPad& pad)
        { return chordPads.dropExternalChord(p, pad); };
        chordGenPanel->onCandidateDragEnd = [this] { chordPads.setExternalDropSlot(-1); };
        // And the same crossing for the card menu's aimless commit.
        chordGenPanel->onCandidateToFirstEmptyPad = [this](const KeysProcessor::ChordPad& pad)
        { return chordPads.sendChordToFirstEmptyPad(pad); };
        chordGenPanel->onPageHasEmptyPad = [this] { return chordPads.firstEmptyPadOnPage() >= 0; };
        chordGenWindow = std::make_unique<DetachedWindow>(
            "Keys Chord Generator", lnf, *chordGenPanel, processor.layout.chordGenBounds,
            ChordGenPanel::minWindowSize(), ChordGenPanel::defaultWindowSize(),
            close, [this] { rememberChordGenBounds(); });
        chordGenPanel->sendLookAndFeelChange(); // built before it was ever parented
    }
    else
    {
        rememberChordGenBounds();
        chordGenWindow.reset(); // clears its content first, so the panel is unparented
        chordGenPanel.reset();
        // Close it mid-drag and the tray never gets to run its own cleanup, so the pad it was
        // hovering would stay lit with nothing left to drop on it.
        chordPads.setExternalDropSlot(-1);
    }

    chordGenButton.setToggleState(open, juce::dontSendNotification);
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
        // A detachedOnly traveller has no parent at all while the section is docked, so
        // spending bar width on it buys nothing but a hole. The keybed's second Size box is
        // 104 px of one: laid out on the bar it left a visible gap between Wheels and All Off,
        // for a combo that is only ever shown inside the detached window.
        if (onBar && t.detachedOnly)
            continue;
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

    // The knob row comes off the bottom first, in the same order and by the same numbers
    // sectionHeight(secControls) added them, so the two cannot drift. Before the title
    // column below, which takes its full height: with the knobs still in it, the wordmark
    // would centre itself over the whole band instead of over the two rows.
    if (processor.layout.knobs)
    {
        auto knobRow = header.removeFromBottom(knobRowH);
        header.removeFromBottom(knobGap);
        knobBank.setBounds(knobRow);
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

void KeysEditor::layoutPadsHolder()
{
    chordPads.setBounds(holderContent(secPads).reduced(4, 4));
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
    chordGen.setEditingSlot(slot);  // and its two menu items step off that card
}

void KeysEditor::endPadEdit()
{
    if (editingPad < 0)
        return;
    editingPad = -1;
    lastEditNotes.clear();
    chordPads.setEditingSlot(-1);
    chordGen.setEditingSlot(-1);
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

    // Hold off is only a button while there is a hold to let go of; the rest of the time it
    // is the display that says there is not one. Polled rather than pushed because a hold
    // arrives from four places (a chord card, the live card, a slot launch, the chain) and
    // leaves from two more (the heartbeat when the arp is switched off, All Off), and none of
    // them owe the editor a callback.
    //
    // A running chain counts as a hold even in the gap where no chord happens to be sounding
    // (a pattern-only slot, or the instant after one was released): it will fire the next
    // chord at the coming bar line, so there is something to let go of and the button has to
    // be live to let go of it. Enabling has to match what the click can do, or the one
    // control that stops a runaway progression greys itself out at the moment it is needed.
    // The same three-way test ArpPanel's Stop uses, and it has to be, because the tooltip on
    // both says they are one button. A launched slot counts on its own: a slot holding a
    // pattern and no chord lights its ring with nothing sounding and no chain running, and
    // releaseArpChord() is what clears it, so the click has work to do and the chip was
    // greying itself out in front of it.
    // Across all three lines, because the button releases all three. One line holding is
    // enough for there to be something to let go of.
    {
        bool anyHold = processor.anyArpHold();
        for (int n = 0; n < KeysProcessor::numArpLines && ! anyHold; ++n)
            anyHold = processor.arpLaunchedSlot(n) >= 0;
        arpHoldOffButton.setEnabled(anyHold);
    }

    // Mode and Scale Compliance are the generator's, and the Markov brain reads neither: it
    // walks a table of transitions instead of a scale. The generator's window already hides
    // them under Source: Markov, so the bar greys them or the same setting is live in one
    // place and dead in the other. A control that accepts the click and changes nothing reads as broken, which
    // is the Octavium behaviour this generator was written not to repeat. Key stays live, since
    // the chains do transpose to it.
    const bool scaleDriven = chordGen.readsScaleSettings();
    genModeBox.setEnabled(scaleDriven);
    genComplianceBox.setEnabled(scaleDriven);

    // Each chip says whether it would do anything, which is also the clearest statement of
    // which of the two is which: Fill lights only while there is a blank to fill, Regen only
    // while there is an unlocked chord to replace. Polled here rather than pushed because the
    // pads change from six places (a drag, a capture, an edit, Clear pad, a tray drop, a
    // session load) and none of them owe the editor a callback.
    fillButton.setEnabled(chordGen.pageHasEmptyPads());
    regenButton.setEnabled(chordGen.pageHasRegeneratablePads());

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
        // Knobs folds the bottom row of this section. It sits at the left end of the bar's
        // free space, which on this bar was several hundred px of caption zone doing nothing:
        // a chip riding a bar costs the window no height, which is what let the knobs give up
        // a section of their own without giving up the fold.
        knobsButton.setBounds(bar.removeFromLeft(66).reduced(0, 2));
        bar.removeFromLeft(10);
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

    // --- Arp ------------------------------------------------------------------------
    area.removeFromTop(6);
    arpBar.setBounds(area.removeFromTop(SectionBar::height));
    {
        // On and Hold off sit on the bar, so they survive folding the section away. 24 px
        // tall, like every other control on a bar that acts rather than folds (Sustain, All
        // Off, Fill, Regen): contentArea() is the 34 px strip less 4 at each end, so 26 is
        // the ceiling here and the mouse-only floor is bought in width instead - 86 px of
        // Hold off is a bigger target than a 34 px square. The fold chips that hide with
        // their section - the pad pages, Knobs - are still 22 (reduced(1, 2)).
        auto bar = layoutDetachRow(secArp, arpBar.contentArea(), true);
        bar.removeFromRight(6);
        // Three line switches where one On used to be, C rightmost so they read A B C left to
        // right. 40 px each rather than the old 68: a letter needs no more, and three of them
        // plus Hold off is already most of what this end of the bar can hold.
        for (int n = KeysProcessor::numArpLines - 1; n >= 0; --n)
        {
            arpOnButtons[(size_t) n].setBounds(bar.removeFromRight(42).withSizeKeepingCentre(40, 24));
            bar.removeFromRight(2);
        }
        bar.removeFromRight(6);
        arpHoldOffButton.setBounds(bar.removeFromRight(88).withSizeKeepingCentre(86, 24));
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
        auto bar = layoutDetachRow(secPads, padsBar.contentArea(), true);
        // The generator - three chips, three combos and two items on a card menu (2026-07-30)
        // - comes off the *right*, which is where every control that outlives its section's
        // fold sits: On and Hold off on the arp bar, the theme swatch on the Controls bar. It
        // has to be that end. The page buttons are laid out from the left and hide when the
        // strip folds, so anything placed after them would keep a couple of hundred px of hole
        // where they were. 24 px like the other bar controls that act rather than fold.
        bar.removeFromRight(6);
        regenButton.setBounds(bar.removeFromRight(70).withSizeKeepingCentre(68, 24));
        bar.removeFromRight(4);
        fillButton.setBounds(bar.removeFromRight(62).withSizeKeepingCentre(60, 24));
        // The generator's window, left of the two actions that do not need it. Same end of the
        // bar, same 24 px, same unconditional placement: it never hides either, and for the
        // sharper version of the same reason - with the pads folded and this gone, the only
        // things left that could reach the generator would be Fill and Regen, and every setting
        // behind them would be off the screen.
        bar.removeFromRight(6);
        chordGenButton.setBounds(bar.removeFromRight(90).withSizeKeepingCentre(88, 24));
        // The target line, left of the generator's three: it belongs to what a card *click*
        // does rather than to what generation does, and putting it at the far end would leave
        // the two groups indistinguishable.
        bar.removeFromRight(8);
        arpTargetButton.setBounds(bar.removeFromRight(38).withSizeKeepingCentre(36, 24));
        // Key, Mode and Compliance, left of the three chips and reading in that order, which is
        // the order the generator's window lists them in. Same end of the bar and the same
        // unconditional placement as Fill and Regen, for the same two reasons: they outlive
        // the fold, and the left end is where the hole appears when the page buttons go.
        //
        // 24 px like everything on a bar that acts rather than folds, so the whole group is
        // one height. What the bar spends, at its floor and above, is worked out in
        // minWidthForView() - these three, and the Generator chip above them, are why that
        // floor moved from 1010 to 1070.
        bar.removeFromRight(10);
        genComplianceBox.setBounds(bar.removeFromRight(74).withSizeKeepingCentre(72, 24));
        bar.removeFromRight(6);
        genModeBox.setBounds(bar.removeFromRight(148).withSizeKeepingCentre(146, 24));
        bar.removeFromRight(6);
        genRootBox.setBounds(bar.removeFromRight(58).withSizeKeepingCentre(56, 24));
        // The page buttons ride on the Pads bar, where they used to sit in a row of their
        // own under the strip. One click still reaches any page, and the section keeps its
        // height for pads instead of spending 34 px on a transport.
        for (auto& b : pageButtons)
        {
            b.setBounds(bar.removeFromLeft(46).reduced(1, 2));
            bar.removeFromLeft(4);
        }
        bar.removeFromLeft(14);
        section(secPads).caption = bar;
    }
    if (const int h = sectionHeight(secPads); h > 0)
    {
        area.removeFromTop(4);
        padsHolder.setBounds(area.removeFromTop(h).expanded(4, 4));
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

} // namespace keys
