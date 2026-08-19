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

    // A `barModeNames()` (the generator's mode names with the parenthetical alias dropped, so
    // they fit a bar-width combo) and a five-step `complianceItems()` ladder lived here. Both
    // went with the Mode and Scale Compliance combos when those left the Pads bar on
    // 2026-08-02: the generator's window spells its modes out in full and puts the whole
    // 0-100 range on a slider, so neither has a caller any more.

    // Fixed heights of the editor's bands, shared by idealHeight() and resized() so the
    // window the folds ask for and the layout they get can never drift apart.
    constexpr int rowH = 46;                          // one row of header controls
    // One row now, not two (2026-08-02, Owen: "I think we can remove the octave setting and
    // the size can go down to the header of the keyboard button"): Size and Octave left for
    // the Keyboard bar and Humanize left for the Pads bar, so what used to be Row A emptied
    // out and Row B - Strum and its direction - is the whole band now. The few px above rowH
    // are what the title's 48 px pair needs to centre in the band without touching either edge.
    constexpr int headerH = rowH + 6;
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
    // Worst case, everything open and docked, the arp in Pattern shape (the one that opens
    // the step editor - the knob row no longer has an off state to leave out of this):
    //     margins            10 + 10                        =   20
    //     four bars          4 * SectionBar::height (34)     =  136
    //     three gaps         3 * 6                           =   18
    //     Controls           4 + headerH 52 + 6 + 110        =  172
    //     Arp                4 + ArpPanel::preferredHeight() =  584   (arpPatternH 564 + 16)
    //     Pads               4 + padRowH                     =  100
    //     Keyboard           4 + dockedKeybedH               =  193
    //                                                          ----
    //                                                          1223
    // It was 1283 with the Controls band's second row still in it, and 1473 before that while
    // Big cards existed and the Pads line could read 290. 1800 leaves room for the arp to grow
    // a lane row or two without this becoming a bug again, and the slack above idealHeight()
    // is all instrument body under the keys.
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

    // Wheels belongs to the keybed, not to the window it happens to be in, so it follows it
    // out. Owen asked for this: with it left behind, the keyboard window had nothing on it
    // but a close box. Size travelled the same way here until 2026-08-02, via a second combo
    // built for the detached window alone; it is a plain bar control now (sizeBox, laid out
    // above) and travels through the ordinary bar mechanism instead.
    section(secKeyboard).travellers = { { &wheelsButton, 84 } };

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

    // Size moved off this band entirely on 2026-08-02 (Owen: "the size can go down to the
    // header of the keyboard button") - see the Keyboard section below, where it is built as
    // an editor child alongside Octave rather than here.
    // Root, Scale, Voices and MIDI Ch ride the Controls *bar* (2026-08-02, Owen's ask), so
    // they are the editor's children rather than the holder's: a holder travels into a
    // detached window and these must stay on the bar in the main one, the theme swatch's
    // rule. Scale Lock goes with them, below, for the same reason. Their labels are restyled
    // to the bar's own micro-caps after this - `addCombo` writes the band's 10 px style.
    addCombo(*this, rootBox, rootLabel, "Root", okstudio::scales::noteNames(), "root", rootAtt);
    addCombo(*this, scaleBox, scaleLabel, "Scale", okstudio::scales::names(), "scale", scaleAtt);
    addCombo(*this, channelBox, channelLabel, "MIDI Ch", channelItems(), "channel", channelAtt);
    addCombo(*this, polyphonyBox, polyphonyLabel, "Voices",
             { "Off", "1", "2", "3", "4", "5", "6", "7", "8" }, "polyphony", polyphonyAtt);
    // 9 px faint micro-caps, the weight every other caption riding a bar wears (BPM,
    // QUANTIZE). The band's 10 px textDim is a size louder because a band has the room.
    for (juce::Label* l : { &rootLabel, &scaleLabel, &channelLabel, &polyphonyLabel })
    {
        l->setFont(skin::micro(9.0f));
        l->setColour(juce::Label::textColourId, skin::textFaint);
        l->setJustificationType(juce::Justification::centredRight);
    }
    // "MIDI CH" does not fit the width this bar can spare; the tooltip on the combo says the
    // whole of it, and "CH" beside a channel number is unambiguous in context.
    channelLabel.setText("CH", juce::dontSendNotification);

    // Octave went with Size the same day, to the Keyboard bar - see below.

    // Scale Lock rides the Controls bar beside the Scale it locks to (2026-08-02). Humanize
    // rides the *Pads* bar now (2026-08-02, second pass - see below). Sustain and Exclusive
    // ride the Keyboard bar, outside every fold, because they are what you reach for while
    // playing.
    //
    // "Lock", not "Scale Lock": the bar cannot spare the width for both words, and it sits
    // immediately right of the Scale box it belongs to. The accessible name keeps the full
    // phrase so the capture script and a screen reader still hear the control's real name.
    scaleLockButton.setButtonText("Lock");
    scaleLockButton.setTitle("Scale Lock");
    addAndMakeVisible(scaleLockButton);
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
    //
    // Humanize and this range moved to the *Pads* bar on 2026-08-02 (Owen picked that bar,
    // "make smaller to fit"), so both are editor children rather than the holder's - the
    // theme swatch's rule, since a holder travels into a detached window and these must stay
    // on the bar in the main one. "VELOCITY" does not fit the 36 px cell the label gets
    // beside a 24 px button, so it reads as a bare number/range now; the slider's own tooltip
    // still spells the whole thing out.
    // Strum and Humanize are **range knobs in the pads section** since 2026-08-03 (Owen:
    // "reduce the pads grid to 12 and move strum and humanize into that with the same style").
    // Both were already ranges - a two-handle RangeSlider each, Strum on the Controls band and
    // Humanize on the Pads bar - and both belong to what a chord *pad* does, which is where
    // they now sit: in the two columns the grid gave up going from sixteen pads to twelve.
    //
    // Each is stored as a low/high pair, and **the face is the pair's centre since 2026-08-19**
    // (Owen, on the halo: "moving the halo shouldn't move knob. should be equal from center").
    // A centre is not a parameter, so no attachment can bind the face: it and the span are set
    // from the pair here, both ends are pushed back on every change - a face move slides the
    // whole band, a halo move opens it equally both ways - and syncPadRangeKnobs() re-pulls
    // when the pair changes under us (a session load). The parameters keep meaning what they
    // always meant, which is what keeps the engine and every saved session untouched.
    const auto wireRange = [this](RangeKnob& rk, juce::Label& head, const juce::String& name,
                                  const juce::String& loId, const juce::String& hiId,
                                  double lo, double hi, const juce::String& unit,
                                  const juce::String& tip)
    {
        styleLabel(head, name);
        padsHolder.addAndMakeVisible(head);
        rk.face().setRange(lo, hi, 1.0);
        rk.face().setTitle(name);
        rk.face().setTooltip(tip);
        rk.setTitle(name + " range");
        rk.setTooltip(tip);
        rk.spanHandle().setTitle(name + " range handle");
        rk.setSpanTooltip("Drag up to open the range, down to close it - it opens equally "
                          "both ways around the knob, which stays put. The wheel works too. "
                          "Closed is a fixed value; open, every chord draws a new one inside "
                          "it.");
        rk.textFromRange = [unit](double a, double b)
        { return juce::String((int) a) + "-" + juce::String((int) b) + unit; };
        const auto loV = (double) processor.apvts.getRawParameterValue(loId)->load();
        const auto hiV = (double) processor.apvts.getRawParameterValue(hiId)->load();
        rk.face().setValue((loV + hiV) * 0.5, juce::dontSendNotification);
        rk.setSpan((hiV - loV) * 0.5);
        const auto pushBoth = [this, &rk, loId, hiId]
        {
            writeParam(loId.toRawUTF8(), rk.rangeLo());
            writeParam(hiId.toRawUTF8(), rk.rangeHi());
        };
        rk.onValueChanged = pushBoth;
        rk.onSpanChanged = [pushBoth](double) { pushBoth(); };
        const auto gesture = [this, loId, hiId](bool begin)
        {
            for (const auto& id : { loId, hiId })
                if (auto* p = processor.apvts.getParameter(id))
                    begin ? p->beginChangeGesture() : p->endChangeGesture();
        };
        rk.onSpanDragStart = [gesture] { gesture(true); };
        rk.onSpanDragEnd = [gesture] { gesture(false); };
        // The face writes parameters too now and has no attachment to bracket its drags.
        rk.face().onDragStart = [gesture] { gesture(true); };
        rk.face().onDragEnd = [gesture] { gesture(false); };
        padsHolder.addAndMakeVisible(rk);
    };

    wireRange(strumKnob, strumHead, "Strum", "chordStrum", "chordStrumMax", 0.0, 200.0, " ms",
              "How long a chord takes to rake, in milliseconds. The knob is the middle of the "
              "band and the ring is how far either side of it a chord may land, so every chord "
              "strums at its own speed instead of sounding stamped out. Closed is a fixed "
              "rake; at zero the chord lands all at once.");
    wireRange(humanKnob, humanHead, "Humanize", "humanizeVelMin", "humanizeVelMax", 0.0, 127.0, "",
              "Velocity. The knob is the middle of the band and the ring is how far either "
              "side of it a note may land, so a part stops sounding typed in. Closed is one "
              "fixed velocity for everything.");
    // **The lamp is the switch** (2026-08-03, Owen: "clicking the blue satellite button should
    // turn on or off the feature. And then I don't think we need the humanized check mark
    // anymore"). Humanize had a tick box; it is gone, and the lamp says lit-or-not instead,
    // which is what a lamp is for. The tick box's parameter is unchanged and so is its
    // meaning - only the control went.
    humanKnob.isOn = [this]
    { return processor.apvts.getRawParameterValue("humanize")->load() > 0.5f; };
    // Off, every note takes the band's **midpoint** - not the knob's own value - so that is
    // what the readout has to say. The knob still points at the top of the band, which is the
    // one place this control is not literal; the number under it is what you will hear.
    humanKnob.textWhenOff = [this](double)
    {
        auto& a = processor.apvts;
        const auto lo = a.getRawParameterValue("humanizeVelMin")->load();
        const auto hi = a.getRawParameterValue("humanizeVelMax")->load();
        return juce::String((int) ((lo + hi) * 0.5f));
    };
    humanKnob.setOn = [this](bool on)
    {
        if (auto* p = processor.apvts.getParameter("humanize"))
            p->setValueNotifyingHost(on ? 1.0f : 0.0f);
    };

    // Strum has no on/off parameter and does not need one: a strum of zero *is* off, since the
    // chord lands all at once. So the lamp parks the range at zero and puts it back where it
    // was - remembered here rather than stored, because "what it was" is a UI convenience and
    // a session that was saved off should open off.
    strumKnob.isOn = [this] { return strumKnob.face().getValue() > 0.0; };
    strumKnob.setOn = [this](bool on)
    {
        if (! on)
            lastStrumMax = juce::jmax(1.0, strumKnob.face().getValue());
        strumKnob.face().setValue(on ? lastStrumMax : 0.0, juce::sendNotificationSync);
    };

    // Strum direction: `<` `>` beside the caption, not a combo (Owen, same day: "could we make
    // the random drop down just like a left and right toggle by the strum text?"). Three
    // values with an order to them is what a stepper pair is for, and the caption says which
    // one is live - see refreshStrumCaption(), so the word is never a second thing to read.
    for (auto* b : { &strumDirPrev, &strumDirNext })
    {
        b->setTooltip("Which way a chord rakes: up, down, or a fresh direction each time.");
        padsHolder.addAndMakeVisible(*b);
    }
    strumDirPrev.setTitle("Previous strum direction");
    strumDirNext.setTitle("Next strum direction");
    strumDirPrev.onClick = [this] { stepStrumDir(-1); };
    strumDirNext.onClick = [this] { stepStrumDir(1); };

    // Tempo used to be a labelled drag slider here in row B of the band. It is a number on
    // this section's *bar* now (2026-08-02, Owen: "the bpm should live in the controls
    // header. I want it to be like the bpm in ableton, just a number"), which also means it
    // survives folding the Controls section - see bpmField.

    // --- Keyboard section ------------------------------------------------------------
    // Size and Octave, on this bar (2026-08-02, Owen: "the size can go down to the header of
    // the keyboard button"). Editor children, not the holder's, so both stay on the bar when
    // the section detaches - the theme swatch's rule. "25 keys" etc. need no caption of their
    // own, the way "Major" needs none beside it on the Controls bar.
    addCombo(*this, sizeBox, sizeLabel, "Size", sizeItems(), "size", sizeAtt);
    sizeLabel.setVisible(false);

    // Octave is not a slider here: a bar control is 24 px tall, and JUCE's IncDecButtons
    // arrows would stack to 12 px each, under the mouse-only floor. It is the BPM field's own
    // shape instead - a caption, a `<` `>` pair and a plain read-out kept current by
    // timerCallback(), since nothing here is an APVTS attachment.
    octaveBarLabel.setText("OCT", juce::dontSendNotification);
    octaveBarLabel.setFont(skin::micro(9.0f));
    octaveBarLabel.setColour(juce::Label::textColourId, skin::textFaint);
    addAndMakeVisible(octaveBarLabel);
    octPrevButton.setTitle("Octave down");
    octNextButton.setTitle("Octave up");
    octPrevButton.setTooltip("Shift the keybed down an octave.");
    octNextButton.setTooltip("Shift the keybed up an octave.");
    octPrevButton.onClick = [this] { nudgeOctave(-1); };
    octNextButton.onClick = [this] { nudgeOctave(1); };
    addAndMakeVisible(octPrevButton);
    addAndMakeVisible(octNextButton);
    octaveReadout.setTitle("Octave");
    octaveReadout.setTooltip("How far the keybed is shifted from its default range, in octaves.");
    octaveReadout.setFont(skin::uiSemi(14.0f));
    octaveReadout.setColour(juce::Label::textColourId, skin::text);
    octaveReadout.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(octaveReadout);

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

    // REC. Live cannot record a plugin's own MIDI onto that plugin's own track, so Keys keeps
    // its own take instead - see KeysProcessor::setRecording for why that is a DAW limit and
    // not something a Keys setting can turn off. Stopping writes the file, so the take is on
    // disk before the chip beside this updates and there is nothing to lose by clicking again.
    recButton.setClickingTogglesState(false); // refreshTakeControls owns the lit state
    recButton.setTooltip("Record what Keys plays into a MIDI file. Ableton cannot record a "
                         "plugin's own notes onto its own track, so Keys records itself; each "
                         "take is written to Documents\\OK Studio\\Keys Takes.");
    recButton.setTitle("Record take");
    recButton.onClick = [this]
    {
        const bool wasRecording = processor.isRecording();
        processor.setRecording(! wasRecording);
        if (wasRecording)
            processor.writeTake(); // stopping and saving always go together; see writeTake
        refreshTakeControls();
    };
    addAndMakeVisible(recButton);

    takeChip.setTooltip("The last take. Click to open it - a picture of what was captured, with "
                        "Save as and Show in Explorer - or drag it straight onto a track. Add "
                        "Documents\\OK Studio\\Keys Takes to Live's Places and every take is one "
                        "short drag inside Live's own browser.");
    takeChip.setTitle("Last take");
    takeChip.getFile = [this] { return processor.lastTakeFile(); };
    // Click opens the window rather than jumping to Explorer. Reveal is still one click, it is
    // just inside the window now, next to the picture that tells you whether you want it.
    takeChip.onClick = [this] { setTakeWindowOpen(takeWindow == nullptr); };
    addAndMakeVisible(takeChip);
    refreshTakeControls();

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
    // where the older comments about it put it. `ChordGenMenu::clearPage()` was deleted
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
    //
    // There was a fourth chip here, a cycling letter naming the arp line a card fed. It went on
    // 2026-08-02 with Mode and Compliance ("remove the scale and percentage and letter b from
    // pads header"), and it had already lost most of its job earlier the same day, when a card
    // click stopped feeding a line at all: what remained was naming the target of the card
    // menu's *Send to arp slot*, which the A/B tabs on the arp bar say just as well.
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
             "The key the chord generator writes in. Setting it also moves the Root that drives "
             "Scale Lock, so the keybed greys to the key you are generating in.",
             okstudio::scales::noteNames(), "genRoot", genRootAtt);
    genCombo(genModeBox, "Generator mode",
             "The mode the chord generator writes in. Setting it also moves the Scale that "
             "drives Scale Lock, wherever the two lists have the same scale.",
             modes::names(), "genMode", genModeAtt);
    // Mode and Scale Compliance sat here beside it until 2026-08-02 (Owen: "remove the scale
    // and percentage and letter b from pads header"). Both are still in the Generator window,
    // which holds every setting the generator has, so nothing became unreachable; the Key
    // stays because it is the one you change between fills. The StepComboBox dance Compliance
    // needed on a bar - a ParameterAttachment reading and `onPick` writing, because a
    // ComboBoxAttachment made picking the step already showing a no-op - went with it; the
    // window drives that parameter with a plain slider and never needed it.

    // The arp's power switch used to be a separate lettered chip here, one per line, right
    // beside the A/B/All tabs that also named a line - Owen called that redundant on
    // 2026-08-02 and it is gone. The tabs (`arpBarTabs`, built below) are the switch now.

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

    // All Off for the arp, beside Hold off and doing strictly more: the lines go off too, so
    // the run actually ends instead of picking straight back up on whatever the keybed holds.
    arpAllOffButton.setTitle("Arp all off"); // "All Off" alone collides with the Keyboard bar's
    arpAllOffButton.setTooltip("Stop the arpeggiator: both lines off, every held chord let go, "
                               "every chain stopped. Hold off beside it lets go of the chords "
                               "and leaves the lines running.");
    arpAllOffButton.onClick = [this] { processor.allArpOff(); };
    addAndMakeVisible(arpAllOffButton);

    // Light the keybed for what the arp is playing. Not an APVTS attachment: it changes what is
    // drawn and nothing that is heard, so it is layout state and the button drives it directly.
    // "Light keys", not "Show notes": this only ever changes what is *drawn*, and the verb has
    // to say so, because the arp panel's per-line PLAY switch is a routing control sitting a few
    // pixels away. They read as one idea until each label names what it touches (2026-08-02).
    arpLightsButton.setButtonText("Light keys");
    arpLightsButton.setTitle("Arp light keys");
    arpLightsButton.setTooltip("Display only, changes nothing you hear: lights the keyboard at "
                               "the bottom for the notes the arpeggiator is playing, as it plays "
                               "them. The chord you hand a line lights it either way; this is the "
                               "run itself. For what a line *plays*, see PLAY on its row.");
    arpLightsButton.setToggleState(processor.layout.arpLights, juce::dontSendNotification);
    arpLightsButton.onClick = [this]
    {
        processor.layout.arpLights = arpLightsButton.getToggleState();
        keyboard.repaint(); // the flags did not move, so nothing else will ask for this frame
    };
    addAndMakeVisible(arpLightsButton);

    // The A/B tabs, BPM and Launch Quantize, at the left end of the same bar (2026-08-02,
    // Owen: "move the bpm and the a b and all into the header also. remove the 'lines'
    // text" - and later the same day, "I want those to be on and off buttons to turn on or
    // off the ARP"). See the header for the ownership and visibility story: A and B are the
    // arp's own On switches now (the ArpBarTab ctor builds the ButtonAttachment), never hide,
    // and no longer navigate anything - that job moved to each macro card's own Details
    // button - so there is no onClick left to give them here.
    for (int n = 0; n < KeysProcessor::uiArpLines; ++n)
    {
        auto tab = std::make_unique<ArpBarTab>(*this, n);
        const auto letter = juce::String::charToString((juce::juce_wchar) ('A' + n));
        tab->setTooltip("Arpeggiator line " + letter + ". Lit, it arpeggiates what you play "
                        "and whatever chord card you send it. Four lines at four rates is the "
                        "polyrhythm; Hold off, at the end of this bar, lets them all go. The "
                        "stripe is line " + letter + "'s own colour, the same one its card "
                        "wears. Drop a chord card here to hand it to line " + letter
                        + " whether it is on or off.");
        addAndMakeVisible(*tab); // never hides - see syncSectionControls
        arpBarTabs[(size_t) n] = std::move(tab);
    }
    arpBarAllTab = std::make_unique<ArpBarTab>(*this, -1);
    arpBarAllTab->setTooltip("All four lines, one card each in two rows: the view for building "
                             "a polyrhythm. The letters beside it are where you go deep on one.");
    arpBarAllTab->onClick = [this]
    {
        if (arpPanel != nullptr)
            arpPanel->setMacroView(true);
        refreshArpBarTabs();
    };
    addChildComponent(*arpBarAllTab);

    // The page tabs, right of All, most-used first. See arpPageForTab for why the bar's order
    // is not the enum's, and for where the names came from.
    {
        static const char* const names[3] = { "Play", "Cards", "Draw" };
        static const char* const tips[3] = {
            "How the line plays: rate, shape, swing, gate and feel. Most of what you want is here.",
            "The twelve slot cards, plus Copy, Clear, Randomize, Euclid, Clocks and Chain.",
            "Draw a rhythm by hand, one step at a time, on any of the ten lanes. Pattern shape only."
        };
        for (int i = 0; i < 3; ++i)
        {
            auto b = std::make_unique<juce::TextButton>(names[i]);
            b->setTitle(juce::String("Arp page ") + names[i]);
            b->setTooltip(tips[i]);
            b->onClick = [this, i]
            {
                if (arpPanel != nullptr)
                {
                    // A page click also leaves the macro view: you cannot be on a page of a
                    // line's deep view and looking at both lines at once. setMacroView first,
                    // so setPage's own relayout is the last one and sees the final state.
                    arpPanel->setMacroView(false);
                    arpPanel->setPage(arpPageForTab(i));
                }
                refreshArpBarTabs();
            };
            addChildComponent(*b); // navigates the panel, so it hides with the fold
            arpPageTabs[(size_t) i] = std::move(b);
        }
    }

    bpmBarLabel.setText("BPM", juce::dontSendNotification);
    bpmBarLabel.setFont(skin::micro(9.0f));
    bpmBarLabel.setColour(juce::Label::textColourId, skin::textFaint);
    bpmBarLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(bpmBarLabel);

    bpmField.setTitle("Tempo");
    bpmField.setTooltip("The tempo Keys runs at when there is no transport to follow, or when "
                        "Tempo Sync beside it is off: always in the standalone, and whenever the "
                        "host is stopped. With Sync on, a host that is playing always wins, and "
                        "an arp line whose rate is in Hz follows neither. Drag up or down to "
                        "sweep it, or step it with < and >.");
    addAndMakeVisible(bpmField);
    bpmAtt = std::make_unique<SliderAtt>(processor.apvts, "bpm", bpmField);
    bpmPrevButton.onClick = [this] { nudgeBpm(-1); };
    bpmNextButton.onClick = [this] { nudgeBpm(1); };
    for (auto* b : { &bpmPrevButton, &bpmNextButton })
    {
        b->setTooltip("Nudge the tempo by one BPM.");
        addAndMakeVisible(*b);
    }

    bpmSyncButton.setTitle("Tempo sync");
    bpmSyncButton.setClickingTogglesState(true);
    bpmSyncButton.setTooltip("On follows the host's tempo whenever it is playing one - what Keys "
                             "has always done. Off keeps Keys at its own BPM control regardless "
                             "of what the host's transport is doing.");
    addAndMakeVisible(bpmSyncButton);
    bpmSyncAtt = std::make_unique<ButtonAtt>(processor.apvts, "bpmSync", bpmSyncButton);

    quantizeBarLabel.setText("QUANTIZE", juce::dontSendNotification);
    quantizeBarLabel.setFont(skin::micro(9.0f));
    quantizeBarLabel.setColour(juce::Label::textColourId, skin::textFaint);
    addAndMakeVisible(quantizeBarLabel);
    // The list has to match the parameter's choices exactly, and in order: an attachment binds
    // to items that already exist rather than creating them, so an empty box stays empty.
    quantizeBarBox.addItemList({ "Off", "1/16", "1/8", "1/4", "1/2", "1 Bar", "2 Bars" }, 1);
    quantizeBarBox.setTitle("Arp launch quantize");
    quantizeBarBox.setTooltip("Hold a chord card, a slot launch or a drag onto a line until the "
                              "next boundary, so it can only ever land on the grid - Ableton's "
                              "Quantization, for the arp. Off fires the instant you click. It "
                              "never delays the keys you play.");
    addAndMakeVisible(quantizeBarBox);
    quantizeBarAtt = std::make_unique<ComboAtt>(processor.apvts, "arpQuantize", quantizeBarBox);
    refreshArpBarTabs();

    themeButton.setTooltip("Colour this instance, to tell it from Keys on other tracks.");
    themeButton.setTitle("Theme");
    themeButton.onClick = [this] { showThemeMenu(); };
    addAndMakeVisible(themeButton);

    // The settings gear, immediately left of the theme swatch - both are plugin-level
    // rather than section-level, which is why they sit together and neither hides with a
    // fold. Empty button text: GearButton::paintButton draws the icon itself.
    gearButton.setTitle("Settings");
    gearButton.setTooltip("UI scale, sustain visuals and drag, updates, the guide and about.");
    gearButton.onClick = [this] { showSettingsMenu(); };
    addAndMakeVisible(gearButton);

    // The Instrument chip (2026-08-02, Owen: "the load instrument section with all that
    // should go in the controls submenu"). Hidden until a host supplies
    // onBuildInstrumentMenu; plain Keys never does. refreshInstrumentChip() owns the visible
    // flag and the caption from there on, but the click handler and the accessible name are
    // fixed for the chip's whole life, so they are set once, here.
    undoButton.setTitle("Undo");
    undoButton.onClick = [this] { processor.undo(); refreshUndoButtons(); };
    addAndMakeVisible(undoButton);
    redoButton.setTitle("Redo");
    redoButton.onClick = [this] { processor.redo(); refreshUndoButtons(); };
    addAndMakeVisible(redoButton);
    refreshUndoButtons();

    instrumentChip.setTitle("Instrument");
    instrumentChip.setTooltip("Load or eject the hosted instrument, or show its own window. "
                              "Only appears when Keys is hosting one, in Keys Host.");
    instrumentChip.onClick = [this]
    {
        if (! onBuildInstrumentMenu)
            return; // the chip is hidden whenever this is null; a stray click is a no-op
        juce::PopupMenu menu;
        menu.setLookAndFeel(&lnf);
        onBuildInstrumentMenu(menu);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&instrumentChip));
    };
    addChildComponent(instrumentChip);

    const auto chip = [this](juce::TextButton& b, bool& flag, const juce::String& tip)
    {
        b.setClickingTogglesState(true);
        b.setTooltip(tip);
        b.onClick = [this, &b, &flag] { flag = b.getToggleState(); applyLayout(); };
        addAndMakeVisible(b);
    };
    chip(wheelsButton, lay.wheels, "Show or hide the mod and pitch wheels.");

    updateButton.setColour(juce::TextButton::buttonColourId, okstudio::theme::good.withAlpha(0.85f));
    addChildComponent(updateButton); // hidden until the updater finds a newer release

    const auto velocity = [this] { return processor.baseVelocity01(); };
    keyboard.getVelocity = velocity;
    padsHolder.addAndMakeVisible(chordPads);

    // Dropping a pad on the live card latches its notes back onto the keyboard for
    // editing (Octavium's drag-to-edit).
    chordPads.onRecall = [this](const std::vector<int>& notes) { keyboard.recallOutputNotes(notes); };
    // The keybed's half of Exclusive. stopAllChordPads() chokes every chord source the processor
    // owns; `latched` and `sustained` live in NoteSurface and are the one it cannot reach, so it
    // calls back out here. Cleared in the destructor - the processor outlives this editor.
    processor.releaseKeybedHolds = [this] { keyboard.releaseHolds(); };

    // Right-click "Edit on keyboard": the pad's notes latch onto the piano and every
    // latch change writes straight back to the pad, name re-detected live.
    chordPads.onEditToggle = [this](int slot) { toggleEditPad(slot); };

    // The generator's half of a pad's card menu - New chord and what could follow it, the two
    // things that are about one card. Unconditional, and it has to stay that way: `chordGen` is
    // a member, so there is no window whose absence could take these items off the menu. They
    // were offered only while the Chords view was up once, and that is the exact bug this
    // arrangement exists to prevent. Everything about the page or the settings is in the
    // generator's window instead (chordGenButton, above).
    // A card leaving the strip is offered to the generator's reference box and to the arp
    // panel's slots, tabs and macro rows, and **none of that is wired here any more**
    // (2026-08-02). Each of those is a `juce::DragAndDropTarget` and JUCE delivers to it
    // directly, across a window boundary or not; the editor used to be the one object holding
    // both ends, forwarding screen positions between them in three `std::function`s, because the
    // code believed the framework could not do this. It can - see ChordDrag.h.
    //
    // Two bug classes went with the plumbing. Every target now gets `itemDragExit` on every path
    // a drag can end, so a highlight lit on the way out cannot be left glowing at nothing (drag
    // out over the reference box, change your mind, drop back on a pad); and a target whose
    // window is closed mid-drag simply stops being found, which is what the explicit cleanup
    // below setChordGenWindowOpen used to have to do by hand.

    chordPads.onExtraMenuItems = [this](int slot, juce::PopupMenu& m) { chordGen.addPadMenuItems(slot, m); };
    chordPads.onExtraMenuChoice = [this](int slot, int id) { chordGen.handlePadMenuChoice(slot, id); };
    // followAim false: this is the menu row, which routes a chord and must not navigate.
    chordPads.onSendToArpLine = [this](int slot, int line)
    { sendPadToArpLine(slot, line); };

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
    // First, and before any child dies: this captures `this` and the processor outlives us, so a
    // pad pressed from MCP after the window closed would call into a destroyed editor.
    processor.releaseKeybedHolds = nullptr;
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
    // And the take window, same order and same reason.
    takeWindow.reset();
    takePanel.reset();
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

// The pull half of the pad range knobs' hand wiring (see wireRange): their face is the band's
// centre, which no attachment can bind, so a session load or an automation write lands here on
// the editor's timer. Pulled only when the stored pair disagrees with the knob's own derived
// ends - pushing our derived ends back through unconditionally would erase the latent span a
// rail is holding back - and never mid-gesture, which would yank the band out from under the
// hand.
void KeysEditor::syncPadRangeKnobs()
{
    const auto sync = [this](RangeKnob& rk, const char* loId, const char* hiId)
    {
        if (rk.spanDragging() || rk.face().isMouseButtonDown())
            return;
        const double lo = (double) processor.apvts.getRawParameterValue(loId)->load();
        const double hi = (double) processor.apvts.getRawParameterValue(hiId)->load();
        if (std::abs(lo - rk.rangeLo()) < 0.5 && std::abs(hi - rk.rangeHi()) < 0.5)
            return;
        rk.face().setValue((lo + hi) * 0.5, juce::dontSendNotification);
        rk.setSpan((hi - lo) * 0.5);
        rk.refresh();
    };
    sync(strumKnob, "chordStrum", "chordStrumMax");
    sync(humanKnob, "humanizeVelMin", "humanizeVelMax");
}

void KeysEditor::refreshSectionPanels()
{
    // Called on every fold and every detach. The arp is the only section left with a panel
    // to build or throw away: the knob bank is cheap enough to keep alive behind a fold, and
    // the generator has no panel at all any more.
    refreshArpPanel();
}

// cycleArpTargetLine() and refreshArpTargetButton() lived here until 2026-08-02, when the
// Pads bar's cycling letter came off. The arp bar's A/B tabs are the one surface that names
// the current line now, and refreshArpBarTabs() is what keeps them honest.

// ---------------------------------------------------------------------------
// The arp bar's A/B/All tabs (2026-08-02). TextButtons like every other bar chip, with a drop
// target apiece. A and B are the arp's own On switches (2026-08-02, second pass); All is a
// plain view toggle with a toggle state that says whether the macro view is showing.

KeysEditor::ArpBarTab::ArpBarTab(KeysEditor& o, int n)
    : juce::TextButton(n < 0 ? juce::String("All")
                             : juce::String::charToString((juce::juce_wchar) ('A' + n))),
      owner(o), line(n)
{
    if (line < 0)
    {
        // The " tab" suffix is what used to keep this from colliding with an On chip sharing
        // its letter; there is no such chip on the All tab (it never had one), but the name
        // stays for the capture script that already knows it.
        setTitle("Arp all tab");
    }
    else
    {
        // A and B dropped the " tab" suffix along with the job it named: they no longer
        // navigate the panel (that moved to each macro card's own Details button), they are
        // the line's power switch, so the name is just the line now - no collision left to
        // avoid. setClickingTogglesState plus this attachment is the whole control: no
        // onClick is set anywhere, the same pattern Sustain and Exclusive use.
        setTitle("Arp line " + getButtonText());
        setClickingTogglesState(true);
        onAtt = std::make_unique<ButtonAtt>(owner.processor.apvts,
            KeysProcessor::arpParamId(n, KeysProcessor::apOn), *this);
    }
}

void KeysEditor::ArpBarTab::paintButton(juce::Graphics& g, bool over, bool down)
{
    juce::TextButton::paintButton(g, over, down);
    // The line's own colour (2026-08-19), as an underline rather than a repaint of the chip:
    // the toggle's lit state still reads exactly as every other bar chip's does, and the
    // stripe is the mark that says which card below is this letter's. Brighter while the
    // line is on, present either way so the letters name their colours even at rest.
    if (line >= 0)
    {
        g.setColour(skin::lineAccent(line).base.withAlpha(getToggleState() ? 0.95f : 0.40f));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(7.0f, 0.0f)
                                   .removeFromBottom(3.0f), 1.5f);
    }
    if (dropTarget)
    {
        g.setColour(skin::accentOf(*this).base);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), skin::radius, 2.0f);
    }
}

bool KeysEditor::ArpBarTab::isInterestedInDragSource(const SourceDetails& details)
{
    // The All tab refuses everything: it selects a view, and there is no line behind it to
    // hand a chord to.
    auto* p = chorddrag::chordBeingDragged(details);
    return line >= 0 && p != nullptr && p->from == chorddrag::Payload::From::padSlot;
}

void KeysEditor::ArpBarTab::itemDragEnter(const SourceDetails&) { dropTarget = true; repaint(); }
void KeysEditor::ArpBarTab::itemDragExit(const SourceDetails&) { dropTarget = false; repaint(); }

void KeysEditor::ArpBarTab::itemDropped(const SourceDetails& details)
{
    dropTarget = false;
    repaint();
    if (auto* p = isInterestedInDragSource(details) ? chorddrag::of(details) : nullptr)
    {
        owner.sendPadToArpLine(p->index, line);
        p->taken = true;
    }
}

// Through the panel when it is open, the same path a drop on a macro card takes. A / B are power
// switches now and stay on the bar with the section folded, so the fallback below is reachable in
// earnest: it is what a drop, or the pad menu's "Send to arp A", does while the arp panel does
// not exist.
//
// **Nothing that routes a chord moves the panel** (2026-08-18, Owen: "the number of steps
// changes, straight vs triplet etc, when you drag a chord onto a new arpeggiator"). Nothing was
// written: dropping on B took the panel to B, and STEPS, Tuplet, Shape and the rate then read B's
// own settings instead of A's, which looks exactly like the drop having changed them.
//
// `followAim` used to separate a drop from the pad menu's Send to arp rows, and its whole
// justification was that a drop aimed at a line so "the next card click should follow the same
// aim". That justification has been stale since 2026-08-02, when a card click stopped feeding a
// line at all - there is no next click for the aim to serve. So both callers now pass false and
// the flag is gone: routing a chord routes a chord, and the way to *look* at a line is its tab or
// its Details button. See ArpPanel::takeChordOnLine.
void KeysEditor::sendPadToArpLine(int padSlot, int line)
{
    if (arpPanel != nullptr)
    {
        arpPanel->takeChordOnLine(line, padSlot);
        return;
    }

    processor.holdArpChordFromPad(padSlot, line);
}

ArpPanel::Page KeysEditor::arpPageForTab(int tabIndex)
{
    static const ArpPanel::Page order[3] = { ArpPanel::Page::setup,   // "Play"
                                             ArpPanel::Page::slots,   // "Cards"
                                             ArpPanel::Page::steps }; // "Draw"
    return order[(size_t) juce::jlimit(0, 2, tabIndex)];
}

// Enabled state and tooltip, derived from the processor's stacks. Polled rather than pushed:
// undo entries are created all over the UI, and one place reading a generation counter is far
// less to get wrong than every one of those sites remembering to call back here.
void KeysEditor::refreshUndoButtons()
{
    const auto gen = processor.undoGeneration();
    if (gen == lastUndoGen)
        return;
    lastUndoGen = gen;

    undoButton.setEnabled(processor.canUndo());
    redoButton.setEnabled(processor.canRedo());
    // The tooltip names what would come back, which is the only way to know before clicking -
    // and the reason every pushUndo call site passes a label rather than a bare marker.
    const auto u = processor.undoLabel();
    undoButton.setTooltip(u.isEmpty() ? juce::String("Nothing to undo")
                                      : "Undo: " + u.toLowerCase());
    const auto r = processor.redoLabel();
    redoButton.setTooltip(r.isEmpty() ? juce::String("Nothing to redo")
                                      : "Redo: " + r.toLowerCase());
}

void KeysEditor::refreshArpBarTabs()
{
    // A and B answer to their own ButtonAttachment now (each bound to that line's On
    // parameter), so writing their toggle state here would fight it. Only the All tab is
    // ours to drive: whether the macro view is showing, derived from the processor so a
    // click, a drop and a session load all agree.
    if (arpBarAllTab != nullptr)
        arpBarAllTab->setToggleState(processor.layout.arpMacro, juce::dontSendNotification);

    // The page tabs are ours to drive too, and their *visibility* is derived here rather than
    // in syncSectionControls alone: entering or leaving the macro view has to show or hide
    // them at once, not on the next 10 Hz tick, or the bar visibly catches up after the view
    // has already changed. Steps greys outside Pattern shape - it is the lane editor and a
    // plain shape has no lanes - rather than vanishing, so the group never reflows under the
    // mouse and the tab is still there to say why.
    const bool onPage = processor.layout.arp && ! processor.layout.arpMacro;
    bool shownChanged = false;
    for (int i = 0; i < 3; ++i)
    {
        auto* t = arpPageTabs[(size_t) i].get();
        if (t == nullptr)
            continue;
        const auto page = arpPageForTab(i); // bar order is not enum order - see the header
        if (t->isVisible() != onPage)
        {
            t->setVisible(onPage);
            shownChanged = true;
        }
        t->setToggleState(onPage && processor.layout.arpPage == (int) page,
                          juce::dontSendNotification);
        t->setEnabled(arpPanel == nullptr || arpPanel->pageAvailable(page));
    }
    // Their cell collapses with them (see resized()), so BPM and Quantize slide back rather
    // than orbiting a hole - the pageButtons lesson, one more time.
    if (shownChanged)
        resized();
}

// ---------------------------------------------------------------------------
// The tempo field: Ableton's, which is a number you drag and nothing else.

KeysEditor::BpmField::BpmField()
{
    // Vertical drag, which is the gesture Ableton's tempo field takes, and no text box: the
    // number below is painted by us, not by a child Label, so nothing can draw a second copy
    // of it or a frame around it.
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    // A whole BPM per 4 px of travel: Ableton's own feel is about this, and the parameter is
    // an int over a range wide enough that the JUCE default made a short drag jump ten.
    setMouseDragSensitivity(juce::jmax(1, (int) (4 * 200)));
    okstudio::ui::makeMouseOnly(*this);
}

void KeysEditor::BpmField::paint(juce::Graphics& g)
{
    // Overriding paint means the LookAndFeel is never asked for a knob or a track; this is
    // the whole of the control's appearance. A recessed well plus the number, which is what
    // makes it read as a field you can put a value into rather than as a label.
    const auto b = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(skin::well);
    g.fillRoundedRectangle(b, skin::radius);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(b.withHeight(1.5f).reduced(skin::radius, 0.0f), 0.75f);
    if (isMouseOverOrDragging())
    {
        g.setColour(skin::accentOf(*this).base.withAlpha(0.35f));
        g.drawRoundedRectangle(b.reduced(0.5f), skin::radius, 1.0f);
    }

    // The LookAndFeel's own disabled dim never reaches this control - it is only ever applied
    // by the base Slider::paint, which overriding paint replaces outright - so a disabled
    // field (Tempo Sync showing the host's number) dims its own text here instead.
    g.setColour(isEnabled() ? skin::text : skin::textDim);
    g.setFont(skin::uiSemi(15.0f));
    g.drawText(showingHost ? juce::String(juce::roundToInt(hostBpm)) : getTextFromValue(getValue()),
               getLocalBounds(), juce::Justification::centred, false);
}

void KeysEditor::nudgeBpm(int delta)
{
    if (auto* p = dynamic_cast<juce::AudioParameterInt*>(processor.apvts.getParameter("bpm")))
    {
        p->beginChangeGesture();
        *p = juce::jlimit(p->getRange().getStart(), p->getRange().getEnd(), p->get() + delta);
        p->endChangeGesture();
    }
}

// The Keyboard bar's octave stepper: the same shape as nudgeBpm, on the "octave" parameter
// instead. No attachment reads this one - octaveReadout is a plain Label, kept current by
// timerCallback() - so a click here only ever has this to go through.
void KeysEditor::nudgeOctave(int delta)
{
    if (auto* p = dynamic_cast<juce::AudioParameterInt*>(processor.apvts.getParameter("octave")))
    {
        p->beginChangeGesture();
        *p = juce::jlimit(p->getRange().getStart(), p->getRange().getEnd(), p->get() + delta);
        p->endChangeGesture();
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
    // The bar tabs are a view of the processor's current line, so every path that moves it
    // has to say so. This is the panel's half - a chord dropped on a macro card lands here.
    arpPanel->onEditLineChanged = [this] { refreshArpBarTabs(); };
    arpPanel->onPageChanged = [this] { refreshArpBarTabs(); };
    // Draw greys outside Pattern shape, so the bar has to hear about a shape change too.
    arpPanel->onShapeChanged = [this] { refreshArpBarTabs(); };
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

void KeysEditor::refreshInstrumentChip()
{
    // Visible iff a host wants the chip at all; plain Keys never sets the hook, so this is
    // permanently false there and the click handler (built once, in the ctor) is dead code
    // it never reaches.
    const bool shown = onBuildInstrumentMenu != nullptr;
    instrumentChip.setVisible(shown);
    if (shown)
    {
        const juce::String name = instrumentName ? instrumentName() : juce::String();
        instrumentChip.setButtonText(name.isNotEmpty() ? name : "No instrument");
    }
    resized(); // the Controls bar's own elastic width depends on whether this chip is shown
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

void KeysEditor::GearButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    // The ordinary chip chrome first - raised fill, hover and down shading, the toggle glow
    // this button never asks for - exactly what every other button on this bar gets from
    // KeysLookAndFeel. Empty button text, so the base class's own drawButtonText draws
    // nothing; only the gear itself is hand-drawn, on top.
    juce::TextButton::paintButton(g, highlighted, down);

    const auto centre = getLocalBounds().toFloat().getCentre();
    const float bodyR = juce::jmin(getWidth(), getHeight()) * 0.24f; // the ring's own radius
    const float holeR = bodyR * 0.48f;                               // the punched centre
    const float toothLen = bodyR * 0.45f;
    const float toothW = bodyR * 0.62f;
    constexpr int teeth = 8;

    // One Path: eight teeth (rounded rects, rotated evenly round the origin) plus the ring
    // as two concentric circles, all still centred on (0,0). Even-odd winding turns the two
    // circles into an annulus - a hole, not a second disc - without needing to know or match
    // whatever colour is behind the button, which fillAll-ing a background-toned dot over the
    // middle would have had to.
    juce::Path gear;
    for (int i = 0; i < teeth; ++i)
    {
        juce::Path tooth;
        tooth.addRoundedRectangle(-toothW * 0.5f, -(bodyR + toothLen), toothW, toothLen, toothW * 0.3f);
        gear.addPath(tooth, juce::AffineTransform::rotation(
            (float) i * juce::MathConstants<float>::twoPi / (float) teeth));
    }
    gear.addEllipse(-bodyR, -bodyR, bodyR * 2.0f, bodyR * 2.0f);
    gear.addEllipse(-holeR, -holeR, holeR * 2.0f, holeR * 2.0f);
    gear.setUsingNonZeroWinding(false);
    gear.applyTransform(juce::AffineTransform::translation(centre.x, centre.y));

    g.setColour(skin::text.withAlpha(down ? 1.0f : (highlighted ? 0.95f : 0.82f)));
    g.fillPath(gear);
}

void KeysEditor::showSettingsMenu()
{
    // **UI scale is deliberately not here yet.** It was built as a submenu of Octavium's eight
    // Zoom presets, radio-ticked, writing `layout.uiScalePercent` - and nothing on screen moved,
    // because making it move means wrapping every child of this editor in one scaled content
    // component and re-deriving minWidthForView() / idealHeight() / KeysHostEditor's own window
    // fitting through the transform. That is a real change to the pixel-exact machinery this
    // file's comments warn about repeatedly, and it is not a submenu's worth of work.
    //
    // A picker that ticks 150% and changes nothing is worse than an absent one, especially here:
    // on a mouse-only surface a control that does not answer is a control you stop trusting, and
    // this menu is new enough to have no credit to spend. The parameter still round-trips in
    // LayoutState so the follow-up has somewhere to land.
    juce::PopupMenu menu;
    menu.setLookAndFeel(&lnf);

    // Both read processor.layout directly and are written back the same way, below - neither
    // is a parameter, the same reason arpLightsButton is not an APVTS attachment either.
    menu.addItem(2001, "Hold visuals during sustain", true, processor.layout.holdVisualsOnSustain);
    // Named for what it decides, not for Octavium's label. Octavium's "Drag While Sustain" is
    // about whether a click-drag glides across the keys at all, and Keys' drag has always
    // glided - so a switch by that name would either do nothing or take gliding away, and the
    // label promises neither. What is actually left to choose is whether the sustained run
    // piles up behind you. See LayoutState::dragWhileSustain, which kept the field name.
    menu.addItem(2002, "Sustained drag leaves a trail", true, processor.layout.dragWhileSustain);
    menu.addItem(2003, "Sustained notes propose chords", true, processor.layout.sustainProposesChords);
    // Ticked, a pad sounds for as long as you hold it; unticked - the default - the press is
    // silent and the release fires the chord for 800 ms. See LayoutState::padHoldToPlay for why
    // the quiet press is the default, and ChordPads' own note for what each mode costs.
    menu.addItem(2004, "Chord pads play while held", true, processor.layout.padHoldToPlay);
    menu.addSeparator();

    // Greyed rather than missing when there is nothing to check: Keys Host never builds
    // updaterConfig (see the constructor - it is a different product, its own release
    // channel still to come), so releasesRepo reads empty there and only there.
    menu.addItem(3001, "Check for updates", updaterConfig.releasesRepo.isNotEmpty());
    menu.addItem(3002, "User guide");
    menu.addItem(3003, "About " + processor.getName());

    juce::Component::SafePointer<KeysEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&gearButton),
                       [safe](int result)
    {
        auto* e = safe.getComponent();
        if (e == nullptr || result == 0)
            return;

        switch (result)
        {
            case 2001:
                e->processor.layout.holdVisualsOnSustain = ! e->processor.layout.holdVisualsOnSustain;
                e->keyboard.repaint(); // the sets did not move; only what they mean visually did
                break;
            case 2002:
                e->processor.layout.dragWhileSustain = ! e->processor.layout.dragWhileSustain;
                break;
            case 2003:
                e->processor.layout.sustainProposesChords = ! e->processor.layout.sustainProposesChords;
                break;
            case 2004:
                // Nothing to repaint or rebuild: the flag is read at the moment a press or a
                // release is handled, so the next click already obeys it.
                e->processor.layout.padHoldToPlay = ! e->processor.layout.padHoldToPlay;
                break;
            case 3001:
                e->checkForUpdatesNow();
                break;
            case 3002:
                // The one URL the repo already carries for itself (CHANGELOG.md's own compare
                // link, installer\keys.iss's AppPublisherURL): blob-linking straight to
                // CONTROLS.md is GitHub's own way of "publishing" a repo doc, and a nicer
                // landing than the bare repo root.
                juce::URL("https://github.com/owenpkent/Keys/blob/main/docs/CONTROLS.md")
                    .launchInDefaultBrowser();
                break;
            case 3003:
                e->showAboutDialog();
                break;
            default:
                break;
        }
    });
}

void KeysEditor::checkForUpdatesNow()
{
    if (updaterConfig.releasesRepo.isEmpty())
        return; // the menu item is greyed for exactly this case; a defensive no-op here too

    juce::Component::SafePointer<KeysEditor> safe(this);
    okstudio::updater::checkNowAsync(updaterConfig,
        [safe](okstudio::updater::CheckResult result, okstudio::updater::UpdateInfo info)
    {
        auto* e = safe.getComponent();
        if (e == nullptr)
            return;
        switch (result)
        {
            case okstudio::updater::CheckResult::found:
                e->showUpdate(info); // the same path the automatic check takes; shows the button too
                juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                    "Check for updates", "Update found: v" + info.version + " is ready to install.");
                break;
            case okstudio::updater::CheckResult::upToDate:
                juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                    "Check for updates",
                    e->processor.getName() + " is up to date (v" + juce::String(KEYS_VERSION) + ").");
                break;
            case okstudio::updater::CheckResult::notReady:
                // A newer version is tagged and its installer is not uploaded yet - the release
                // API answered perfectly, so this must not read as a connection problem. It is
                // its own result in the kit for exactly this sentence's sake. The version is
                // deliberately not named: `notReady` carries no UpdateInfo, because the kit
                // keeps "UpdateInfo is meaningful only when the result is found" true rather
                // than handing back a half-filled one nothing may act on.
                juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                    "Check for updates",
                    "A newer version has been announced, but its installer is not published "
                    "yet. Try again shortly.");
                break;
            case okstudio::updater::CheckResult::failed:
                juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    "Check for updates",
                    "Could not reach the update server. Check the connection and try again.");
                break;
        }
    });
}

void KeysEditor::showAboutDialog()
{
    // Product name and version both read live rather than written out a second time here:
    // getName() is "Keys" or "Keys Host" by virtual dispatch (KeysHostProcessor overrides
    // it), and KEYS_VERSION is the exact macro updaterConfig.currentVersion is built from.
    const juce::String text = processor.getName() + "\nVersion " + juce::String(KEYS_VERSION)
        + "\n\nPart of the OK Studio line: Undertow (bass), Beatform (drums), Keys (the played "
          "keyboard).";
    juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
        "About " + processor.getName(), text);
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

    wheelsButton.setToggleState(lay.wheels, juce::dontSendNotification);

    // The theme button is its own swatch: it wears the colour it sets, so the control and
    // the thing it controls are the same object.
    const auto ac = skin::accentAt(lay.accent);
    themeButton.setButtonText(skin::accentChoices()[juce::jlimit(0, skin::numAccents - 1, lay.accent)].name);
    themeButton.setColour(juce::TextButton::buttonColourId, ac.deep);
    themeButton.setColour(juce::TextButton::textColourOffId, ac.hot);

    // The knob bank is unconditional now (2026-08-02: the Knobs chip that used to fold it is
    // gone), so it only has to answer for its section's own fold - the holder is already
    // hidden with the section, and being detached is a change of parent, not of visibility.
    knobBank.setVisible(true);

    // A and B never hide (2026-08-02, second pass): they are the arp's own On switches now,
    // exactly the "reach for it while playing" case that keeps arp On and BPM live on a
    // folded bar, and their toggle state comes from a ButtonAttachment rather than from here.
    // All still only navigates the panel, so it keeps the pad-pages rule: hide with the fold.
    if (arpBarAllTab != nullptr)
        arpBarAllTab->setVisible(lay.arp);
    // The page tabs follow All's rule and add one of their own: a page picker is meaningless
    // while the macro view is up, since no page is showing. So the bar reads "A B All" in the
    // overview and "A B All Steps Slots Setup" in a line's deep view, which is what makes All
    // visible as the way back rather than as a third letter. refreshArpBarTabs() owns that
    // decision, here as everywhere else.
    refreshArpBarTabs();

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
        // The header row, plus the knob row - unconditional since 2026-08-02, when the Knobs
        // chip that used to fold it went. This one expression is the whole answer for
        // Controls: idealHeight() sums it, resized() hands it back to the holder, and
        // layoutControlsHolder() carves it up in the same order. Write the arithmetic
        // anywhere else and the window is the wrong size with nothing to say so.
        case secControls:   return headerH + knobGap + knobRowH;
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
    // 1280 now (2026-08-02, sixth pass): the Controls bar overtook the Pads bar as the
    // binding constraint the day BPM's caption, Voices' and CH's captions, and the Sync chip
    // joined it. The Pads bar's own arithmetic (below, unchanged) still wants 1070; Controls
    // wants more, and the floor is whichever bar asks for the most.
    //
    // The Pads bar, as it has since 2026-07-30 - the arithmetic, at the bar's own
    // contentArea() (the window less 20 of margin, less the 92 px fold zone and the 8 px each
    // side of it):
    //     right   Detach 104, 6, Regen 70, 4, Fill 62, 6, Generator 90, 10,
    //             Compliance 74, 6, Mode 148, 6, Key 58                        = 644
    //     left    four pages at 46 + 4, 14                                     = 214
    //                                                                    total = 858
    // 1070 hands it 942, so the bar fits with 84 px of caption zone left over when the section
    // is docked.
    //
    // The Controls bar, measured off the running app rather than assumed (Owen's window is
    // 1072 px, essentially the old 1070 floor): at that width, after Detach 104 and Theme's
    // 6 + 112 + 6, the bar hands resized() 711 px, of which 624 was spent (instrument chip
    // 150 + 14, tempo group 114 + 14, tight combos 332) - 87 px of slack.
    //
    // What grew and by how much, all fixed-width and reserved before the elastic Instrument
    // chip (see resized() - "reserve the fixed-size control first, always"):
    //     bpm group   34 ("BPM") + 4, then the same prev/field/next as always (114),
    //                 then 8 + 62 (Sync)              114 -> 222      (+108)
    //     tight cells Voices and CH each gain their roomy label (44+4, 26+4)
    //                                                  332 -> 410      (+78)
    //                                                              total  +186
    // 186 px more than before against 87 px of slack - the shortfall is 99, and CLAUDE.md's
    // rule is that it must not be a starved control that pays it, so the floor rises instead.
    //
    // The floor has to clear two cases at once, both at the *same* window width - Owen's
    // 1070-ish, since that is what "the editor's minimum width" means:
    //   normal day     bar = F - 359 (the offset the 1072-wide/711-bar pair above fixes),
    //                  fixed cost with the tight cells = 222 + 14 + 14 + 410 = 660, and the
    //                  Instrument chip absorbs whatever the bar has beyond that.
    //   update-button  the button itself still costs 170, plus the 6 px gap already
    //   day            reserved ahead of it, so this day has 176 px less bar to spend.
    // Solving for the chip to still clear its own floor (60, unchanged - the one corner it
    // was built for) on the update-button day, with a margin rather than the bare minimum
    // (an estimate this close to the wall is not worth trusting without Owen's own window to
    // measure against): F = 60 + 660 + 176 + 359 = 1255 at zero margin; 1280 leaves the chip
    // at 85 px that day (25 px of slack above its floor) rather than pinned to it.
    //
    // What 1280 buys, worked back out at that width: the normal day actually clears the
    // *roomy* cells (Root's caption too, the one Owen did not ask for and did not need to
    // drop) with the chip at its full 150 px max and 33 px of bar left spare; the
    // update-button day falls back to the tight cells - dropping Root's caption, exactly the
    // one CLAUDE.md says to drop first - and the chip shrinks to 85 px rather than its 60 px
    // floor. One clearly-documented degrade, and it is a tier the bar was already choosing
    // between rather than a third one invented for this.
    //
    // 1400 (checked, not assumed): plenty of slack either way, same as it always was here.
    //
    // 1320 now (2026-08-17, the settings gear): the gear is reserved out of the bar in the
    // same right-to-left block as Detach and Theme, before anything below it measures what
    // is left - "reserve the fixed-size control first, always" applies to a *new* fixed
    // control exactly as it does to an existing one. It costs the bar 34 px for the button
    // plus the 6 px gap that now separates it from Theme: 40 px added to the offset term
    // above (359 -> 399), so the zero-margin floor is 60 + 660 + 176 + 399 = 1295. Same
    // margin as before rather than the bare minimum, for the same reason: 1320 leaves the
    // chip 25 px above its floor on the update-button day, exactly as 1280 left it 25 px
    // above on the day before the gear existed.
    return 1320;
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
        // the editor around it (the keybed's Wheels).
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
            addAndMakeVisible(*t.c);
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

        // The audition tray's drag needs nothing from this class. It leaves the tray as a stock
        // JUCE drag with the drag image on the desktop and lands on whichever target is under
        // the cursor - a pad in this window, or the reference box beside it - so the editor is
        // no longer the place the two ends have to meet. What still crosses here is the *menu*
        // item, because a menu item has no target to hit and no drop to deliver.
        chordGenPanel->onCandidateToFirstEmptyPad = [this](const KeysProcessor::ChordPad& pad)
        { return chordPads.sendChordToFirstEmptyPad(pad); };
        chordGenPanel->onPageHasEmptyPad = [this] { return chordPads.firstEmptyPadOnPage() >= 0; };
        chordGenWindow = std::make_unique<DetachedWindow>(
            "Keys Chord Generator", lnf, *chordGenPanel, processor.layout.chordGenBounds,
            ChordGenPanel::minWindowSize(), ChordGenPanel::defaultWindowSize(),
            close, [this] { rememberChordGenBounds(); }, ChordGenPanel::maxWindowSize());
        chordGenPanel->sendLookAndFeelChange(); // built before it was ever parented
    }
    else
    {
        rememberChordGenBounds();
        chordGenWindow.reset(); // clears its content first, so the panel is unparented
        chordGenPanel.reset();
        // Closing this window mid-drag used to need a line here putting the pad strip's hover
        // highlight back out, because the tray never got to run its own cleanup. JUCE does it:
        // the drag image is a mouse listener on the source, the source dies with the window, and
        // `~DragImageComponent` sends `itemDragExit` to whatever it was last over.
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
    // The holder is the painted band, so the row sits inside it with the same margins the
    // editor uses. One row now, down from two: Size, Octave and Humanize all left for a bar
    // (2026-08-02), which is what emptied Row A and shrank Row B's neighbour to nothing.
    auto header = holderContent(secControls).reduced(10, 6);
    if (header.isEmpty())
        return;

    // The knob row comes off the bottom first, in the same order and by the same numbers
    // sectionHeight(secControls) added them, so the two cannot drift. Before the title
    // column below, which takes its full height: with the knobs still in it, the wordmark
    // would centre itself over the whole band instead of over the row above it. Unconditional
    // since 2026-08-02 - the Knobs chip that used to fold it is gone.
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

    // One row: Strum and its direction, the chord-pad rake. Everything else that used to
    // share these two rows - Size, Octave, Humanize and its velocity range, Root, Scale,
    // Scale Lock, Voices, MIDI Ch, BPM - has a bar of its own now. A control has one home,
    // and a second setBounds would fight it.
    auto row = header.removeFromTop(rowH);

    const auto cell = [](juce::Rectangle<int>& r, int w, juce::Label& lab, juce::Component& ctl)
    {
        auto c = r.removeFromLeft(w);
        r.removeFromLeft(8);
        lab.setBounds(c.removeFromTop(14));
        ctl.setBounds(c);
    };

    juce::ignoreUnused(row, cell); // the band's last row went with Strum on 2026-08-03
}

// Wraps, unlike the arp's steppers, which stop at the ends. Three values with no scale to
// them - up, down, random - are a ring, not a ladder: there is no "past the end" to protect
// anyone from, and stopping would make one of the three reachable from one side only.
void KeysEditor::stepStrumDir(int delta)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(processor.apvts.getParameter("chordStrumDir")))
    {
        const int n = juce::jmax(1, p->choices.size());
        p->beginChangeGesture();
        *p = (p->getIndex() + delta + n) % n;
        p->endChangeGesture();
    }
}

void KeysEditor::layoutPadsHolder()
{
    auto area = holderContent(secPads).reduced(4, 4);

    // Strum and Humanize take the right end of the strip - the space the grid gave up going
    // from sixteen pads to twelve (2026-08-03). Reserved *first*, so the pads take what is
    // left rather than the knobs taking what the pads did not want: the elastic control gives
    // way, never the fixed one, which is this file's oldest lesson about rows.
    auto strip = area.removeFromRight(214);
    area.removeFromRight(8);
    chordPads.setBounds(area);

    // The strip is only about 100 px tall - the pad cards are ~50 - so the knob has to be
    // budgeted for, not given what is left. Its face ends up `height - 11 - 24 - 14 - 2*inset`,
    // and every one of those numbers is chosen to land it at the ~40 px the arp's macro knobs
    // get: the same knob at the same size is what "the same style" means. Handing the column
    // the arp's *cell* numbers instead (12 caption, 34 control, 15 readout, 8 inset) left a
    // 26 px face floating in an empty column, which is what a first cut did.
    // The row under each knob is gone with the Humanize tick box and the Dir combo
    // (2026-08-03) - the lamp switches, and Dir stepped from the caption row - so all of that
    // height is the knob's. The face lands near 60 px, comfortably past the arp's 40.
    const auto column = [](juce::Rectangle<int>& r, int w, juce::Label& head, RangeKnob& knob,
                           juce::Component* prev, juce::Component* next)
    {
        auto c = r.removeFromLeft(w);
        r.removeFromLeft(6);
        // 22, not the caption's usual 11: the row carries the two steppers on the Strum column
        // and both columns keep the same knob size, so both rows are the same height.
        auto cap = c.removeFromTop(22);
        if (prev != nullptr && next != nullptr)
        {
            prev->setBounds(cap.removeFromLeft(22).reduced(1));
            next->setBounds(cap.removeFromRight(22).reduced(1));
        }
        head.setBounds(cap);
        knob.setFaceInset(7);
        knob.setReadoutHeight(14);
        knob.setBounds(c);
    };

    column(strip, 100, strumHead, strumKnob, &strumDirPrev, &strumDirNext);
    column(strip, 100, humanHead, humanKnob, nullptr, nullptr);
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

void KeysEditor::setTakeWindowOpen(bool open)
{
    if (open == (takeWindow != nullptr))
        return;

    if (open)
    {
        takePanel = std::make_unique<TakePanel>(processor);
        // Deferred a message-loop turn, the same reason the generator window defers: both ways
        // out (the panel's Close button, the title bar's X) are *inside* what this destroys.
        juce::Component::SafePointer<KeysEditor> safe(this);
        const auto close = [safe]
        {
            juce::MessageManager::callAsync([safe]
            {
                if (auto* e = safe.getComponent())
                    e->setTakeWindowOpen(false);
            });
        };
        takePanel->onClose = close;
        takeWindow = std::make_unique<DetachedWindow>(
            "Keys Take", lnf, *takePanel, takeWindowBounds,
            TakePanel::minWindowSize(), TakePanel::defaultWindowSize(),
            close, [this] { if (takeWindow != nullptr) takeWindowBounds = takeWindow->getBounds(); });
        takePanel->sendLookAndFeelChange(); // built before it was ever parented
    }
    else
    {
        if (takeWindow != nullptr)
            takeWindowBounds = takeWindow->getBounds();
        takeWindow.reset(); // clears its content first, so the panel is unparented
        takePanel.reset();
    }
}

// The take chip's two gestures. Four pixels of slop tell them apart, the same tolerance
// RangeKnob's lamp uses: a click on a mouse-only surface is allowed to be untidy.
void KeysEditor::TakeChip::mouseDown(const juce::MouseEvent& e)
{
    wasDrag = false;
    juce::TextButton::mouseDown(e);
}

void KeysEditor::TakeChip::mouseDrag(const juce::MouseEvent& e)
{
    if (! wasDrag && e.getDistanceFromDragStart() > 4 && getFile != nullptr)
    {
        const auto f = getFile();
        if (f.existsAsFile())
        {
            wasDrag = true;
            TakePanel::dragTakeOut(this, f); // one spelling of the drag, shared with the roll
            return;
        }
    }
    if (! wasDrag)
        juce::TextButton::mouseDrag(e);
}

void KeysEditor::TakeChip::mouseUp(const juce::MouseEvent& e)
{
    if (wasDrag)
    {
        setState(buttonNormal); // the gesture was a drag; it must not also fire onClick
        return;
    }
    juce::TextButton::mouseUp(e);
}

// Polled rather than pushed: recording stops from the button here, but the take's duration
// grows on the processor's heartbeat and nothing owes the editor a callback for it.
void KeysEditor::refreshTakeControls()
{
    const bool rec = processor.isRecording();
    recButton.setButtonText(rec ? "STOP" : "REC");
    recButton.setToggleState(rec, juce::dontSendNotification);
    recButton.setColour(juce::TextButton::buttonColourId, rec ? skin::recordLit : skin::control);

    // While recording the chip counts the take up; stopped, it names what is on disk. Greyed
    // rather than hidden when there is no take at all, so the pair never reflows the bar - the
    // Undo/Redo rule.
    const auto clock = [](double seconds)
    {
        const int s = juce::jmax(0, (int) seconds);
        return juce::String(s / 60) + ":" + juce::String(s % 60).paddedLeft('0', 2);
    };

    juce::String caption;
    if (rec)
    {
        // "Waiting" until the first **note**, because the take is trimmed to it: arming and then
        // thinking costs the file nothing, and a counter running before anything is captured
        // would say otherwise. Keys' own wheels put CC on the captured stream, so this asks
        // whether a note has arrived and not whether an event has.
        caption = processor.capturedHasNotes() ? clock(processor.capturedSeconds())
                                               : juce::String("Waiting");
        takeChip.setEnabled(false); // there is no file to reveal or drag until it is written
        haveTakeFile = false;       // recomputed on the tick after this one stops
    }
    else if (processor.lastTakeWriteFailed())
    {
        // Say so rather than showing the take *before* this one as though it were the take you
        // just played. writeTake keeps the older file on a failure precisely so nothing is lost;
        // what must not happen is the UI reporting it as the new one.
        caption = "Take failed";
        takeChip.setEnabled(false);
        haveTakeFile = false;
    }
    else
    {
        // One filesystem stat per *change*, not two per tick. This runs at 30 Hz forever on the
        // DAW's UI thread, and nothing here can move except by the user's own REC click, so the
        // answer is cached against the path it was computed for. It was two `existsAsFile()`
        // calls per tick - 60 syscalls a second, and a network or OneDrive-backed Documents
        // folder can block for milliseconds on each.
        const auto f = processor.lastTakeFile();
        if (f != lastTakeStatPath)
        {
            lastTakeStatPath = f;
            haveTakeFile = f.existsAsFile();
        }
        caption = haveTakeFile ? "Take " + clock(processor.capturedSeconds())
                               : juce::String("No take");
        takeChip.setEnabled(haveTakeFile);
    }

    if (caption != lastTakeCaption)
    {
        lastTakeCaption = caption;
        takeChip.setButtonText(caption);
    }

    // So a take recorded with the window already open appears in it. The panel's own check is
    // the cheap one (a file path and an event count); it only rebuilds when those move.
    if (takePanel != nullptr)
        takePanel->refresh();
}

void KeysEditor::timerCallback()
{
    refreshTakeControls();

    refreshUndoButtons(); // cheap: early-outs on an unchanged generation counter

    syncPadRangeKnobs(); // cheap: early-outs unless the stored pair changed under the knob

    const auto& apvts = processor.apvts;
    const int sizeIdx = juce::jlimit(0, 5, (int) apvts.getRawParameterValue("size")->load());
    keyboard.setRange(sizeSpecs[sizeIdx].low, sizeSpecs[sizeIdx].count);

    // The Keyboard bar's octave read-out: no attachment drives it (it is a plain Label, not
    // a slider), so it is kept current here like every other live number on a bar.
    const int oct = (int) apvts.getRawParameterValue("octave")->load();
    octaveReadout.setText((oct > 0 ? "+" : "") + juce::String(oct), juce::dontSendNotification);

    // Tempo Sync (2026-08-02, Owen: "we need a BPM sync toggle to sync with DAW"). Sync on and
    // a host tempo actually live this block: the field shows the host's number and the drag
    // and the < > steppers grey out, since none of the three can change anything while the
    // host is the one setting the tempo. Otherwise the field edits "bpm" exactly as it always
    // has. Polled here like every other live bar readout: hostTempoLive() is written on the
    // audio thread every block and can flip on its own as a host starts or stops rolling.
    {
        const bool hostLive = apvts.getRawParameterValue("bpmSync")->load() > 0.5f
                             && processor.hostTempoLive();
        bpmField.showingHost = hostLive;
        bpmField.hostBpm = processor.currentTempo();
        bpmField.setEnabled(! hostLive);
        bpmPrevButton.setEnabled(! hostLive);
        bpmNextButton.setEnabled(! hostLive);
        if (hostLive)
            bpmField.repaint(); // the only thing that can move this number is the host itself
    }

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
    // Across every line, because the button releases every line. One line holding is enough
    // for there to be something to let go of.
    {
        bool anyHold = processor.anyArpHold();
        for (int n = 0; n < KeysProcessor::uiArpLines && ! anyHold; ++n)
            anyHold = processor.arpLaunchedSlot(n) >= 0;
        arpHoldOffButton.setEnabled(anyHold);
    }

    // Mode and Scale Compliance used to be greyed here whenever the source read neither (the
    // Markov brain walks a table of transitions instead of a scale). Both left this bar on
    // 2026-08-02 and only the generator's window draws them now, which does its own greying;
    // Key, which is still here, stays live under every source, since even the chains transpose
    // to it.

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

    // Keep the two-handle velocity range synced to its params and show the numbers. With
    // Humanize off the two ends are one value as far as playing goes, so read out the
    // midpoint rather than a range that is not being spread over. Both ranges are knobs since
    // 2026-08-03, so neither carries a "VELOCITY" prefix any more. Their *face* is on an
    // attachment and needs nothing here; the **span** is not, so it is pushed in each tick -
    // the same read-back the arp's two Humanize knobs need, and for the same reason: a session
    // load, a host lane or an MCP client moves the low end and nothing else would tell the
    // ring. setSpan no-ops when the value is unchanged, so this never fights an open drag.
    //
    // Sorted, because the pair can arrive the wrong way round from host automation and a
    // negative span is not a thing to draw.
    //
    // **Nothing greys on Humanize any more, here or anywhere.** Both this comment and a
    // `const bool hum` above it described a greying pass that went when Humanize's tick box
    // became the knob's own lamp (2026-08-03) - the variable had been unread ever since, warning
    // on every build, and the comment outlived it saying the timing spread still greys out. The
    // reasoning worth keeping is why the velocity range was never the thing to disable: it is
    // the velocity control whether Humanize is on or off, since it plays the band's midpoint
    // when off, so grey it and there is no way left to set how hard Keys plays. humanKnob.isOn /
    // setOn is the on/off now.
    const auto spanOf = [&apvts](const char* loId, const char* hiId)
    {
        const auto a = apvts.getRawParameterValue(loId)->load();
        const auto b = apvts.getRawParameterValue(hiId)->load();
        return (double) std::abs(b - a);
    };
    humanKnob.setSpan(spanOf("humanizeVelMin", "humanizeVelMax"));
    strumKnob.setSpan(spanOf("chordStrum", "chordStrumMax"));

    // The Strum caption carries its direction, since the `<` `>` beside it have to be stepping
    // something visible and a third control saying so would be one more thing to read. Short
    // forms: "STRUM RAND" is about as wide as a 100 px column takes at 9.5 px micro-caps.
    if (const auto* dir = dynamic_cast<const juce::AudioParameterChoice*>(
            apvts.getParameter("chordStrumDir")))
    {
        static const char* const shortDir[] = { "UP", "DOWN", "RAND" };
        const auto want = "STRUM " + juce::String(shortDir[juce::jlimit(0, 2, dir->getIndex())]);
        if (strumHead.getText() != want)
            strumHead.setText(want, juce::dontSendNotification);
    }
    // Both lamps are switches over a parameter no attachment here watches, so the lamp *and*
    // the arc are refreshed each tick - a host lane or a session load moves them too.
    strumKnob.refresh();
    humanKnob.refresh();

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
            // proposedChordNotes, not soundingOutputNotes: with the pedal down, passing notes
            // were being written straight into the pad you were editing.
            const auto now = keyboard.proposedChordNotes(processor.layout.sustainProposesChords);
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
    // What the keyboard is holding: the keybed surface, plus anything arriving on the MIDI
    // input, merged rather than replaced so a hand on each keyboard still reads as one chord.
    auto played = keyboard.proposedChordNotes(processor.layout.sustainProposesChords);
    for (int n : processor.inputNotes())
        if (std::find(played.begin(), played.end(), n) == played.end())
            played.push_back(n);
    std::sort(played.begin(), played.end());

    // ...and what any *other* chord source is holding (2026-08-16, Owen: "I'm not able to drag
    // the currently held chord into the chord pad"). The keybed surface answers only for keys
    // clicked on it, so a chord fired from a pad or held into an arp line lit the keys and left
    // this card empty - and an empty card is not draggable, which is what he ran into.
    //
    // Not included: the generator window's 800 ms audition, which fires through noteOn with its
    // own bookkeeping in ChordGenMenu. It is a monitor rather than something you are holding,
    // and a tray card is already one drag from a pad, which is the better route anyway.
    auto held = processor.heldChordNotes();

    // **The two are not merged; the more recent one wins** (Owen, same day: "the currently held
    // chord should disappear when you play a new chord pad"). Merging made the card name the
    // pile of everything ringing at once - keys you are still holding plus the pad you just hit
    // - which is neither chord and is not draggable as either. heldChordNotes() already picks
    // one source among the pads and lines; this picks between that answer and the keybed, and
    // the tie-break is which of them last *changed*, because that is what "currently" means.
    //
    // Whichever is preferred, an empty answer falls through to the other rather than blanking
    // the card: letting go of the keys over a sustained pad should show the pad, not nothing.
    if (played != lastPlayedChord)
    {
        lastPlayedChord = played;
        preferHeldChord = false;
    }
    if (held != lastHeldChord)
    {
        lastHeldChord = held;
        preferHeldChord = true;
    }
    const auto& chord = preferHeldChord ? (held.empty() ? played : held)
                                        : (played.empty() ? held : played);
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
        // The gear, immediately left of the swatch. 34 px square - CLAUDE.md's own mouse-only
        // floor, exactly - rather than the 26 px pill height every other control on this bar
        // gets from contentArea()'s own 4 px top/bottom inset: an icon-only target this small
        // needs every pixel the floor asks for, so it is bounded off controlsBar's own full
        // height instead of off `bar`, flush with the bar's top and bottom edges.
        gearButton.setBounds(bar.removeFromRight(34).withY(controlsBar.getY()).withHeight(SectionBar::height));
        bar.removeFromRight(6);
        if (updateButton.isVisible())
            updateButton.setBounds(bar.removeFromRight(170).reduced(0, 1));

        // Root, Scale, Lock, Voices and MIDI Ch's two sizes, decided by measuring rather than
        // assuming (2026-08-02, Owen's ask - and "I think we can resize the elements down"
        // when they would not fit at his window width). Computed *before* laying anything out
        // on the left: the Instrument chip below needs to know how much room this group and
        // the tempo group are about to claim before it can work out what is left for it.
        //
        // Root keeps a caption in the roomy set alone now - "C" says nothing on its own, but
        // it is the one caption Owen did not ask for (2026-08-02: "BPM and Off and [Voices]
        // ... needs labels"), so it is the one that drops first under width pressure. Voices
        // and CH are captioned in *both* sets: Keys Host runs in tight, and a caption that
        // only ever showed in the roomy set nobody reaches was not a caption Owen could see.
        // Scale and Lock never get one: "Major" and "Lock" are their own labels.
        constexpr int gap = 8, lblGap = 4;
        struct Cell { int label, box; };
        // Voices is the widest of the small boxes because "Off" plus a chevron is wider
        // than the digits either side of it in the list - measured, not guessed: at 52 it
        // drew "..." while every other value fitted, which is the failure a combo makes
        // instead of complaining.
        //
        // Root and CH were 48 and 48 here (44 and 44 tight) and both drew "..." in every
        // state, which is the same failure again and had been there since they moved to the
        // bar: a chevron plus JUCE's own padding costs a combo about 38 px before a single
        // glyph is drawn, so 48 cannot hold even "C". Root has to fit "A#" and CH has to fit
        // "16", so they are sized for their *widest* value, not the one selected when you
        // happen to look. The 44/26 label widths are proven at this same font from the
        // roomy set they were built for - reused rather than re-guessed for tight's copies.
        constexpr Cell roomy[] = { { 34, 58 }, { 0, 96 }, { 0, 62 }, { 44, 68 }, { 26, 56 } };
        constexpr Cell tight[] = { {  0, 54 }, { 0, 76 }, { 0, 56 }, { 44, 62 }, { 26, 52 } };
        const auto widthOf = [](const Cell (&cells)[5])
        {
            int w = gap * 4;
            for (const auto& c : cells)
                w += c.box + (c.label > 0 ? c.label + lblGap : 0);
            return w;
        };

        // The Instrument chip (2026-08-02, Owen: "the load instrument section with all that
        // should go in the controls submenu"), in the cell Knobs vacated. It is the only
        // *elastic* control left on this bar, so - "reserve the fixed-size control first,
        // always" - the tempo/sync group and the keyboard-settings combos above (one of two
        // fixed widths) are measured first, and the chip gets whatever the bar has left over,
        // clamped to a readable range. Getting this backwards is the Shape trap CLAUDE.md
        // logs twice: an elastic control asked to leave room for its neighbours can starve
        // one of them to a combo drawing "...". Reserve it at its *widest*, so a long
        // instrument name never pushes the combos to a different size than a short one, but
        // reserve it only when a host has actually supplied the hook: the chip and its
        // trailing gap are both inside the `if` below, so charging plain Keys 164 px for a
        // chip it never shows would drop Voices and CH to the caption-less tight set on the
        // bar Owen ships, to buy nothing.
        //
        // BPM's caption and the Sync chip (2026-08-02, this bullet) grew this group from 114
        // to 222: label "BPM" (34, reusing Root's own roomy width - both are three-to-four
        // letters at the same font, and Root's is already proven not to ellipsise) + 4 px
        // gap, then the same prev/field/next as always, then an 8 px gap and Sync at 62 (Fill
        // and Regen's own proven width for a four-letter bar chip, not a checkbox-style
        // ToggleButton - there is no width here for one).
        constexpr int bpmGroupW = 34 + 4          // "BPM" + gap
                                 + 26 + 3 + 56 + 3 + 26  // prev, gap, field, gap, next
                                 + 8 + 62;          // gap, Sync
        // chipMin stays 60 (unchanged since the update-button corner it was built for) - see
        // minWidthForView() for why the floor, not this clamp, absorbs the growth above.
        // Everywhere but that one corner the clamp still lands on chipMax.
        // Undo / Redo first, and reserved out of the bar before anything measures what is
        // left - the reserve-fixed-size-first rule, which this bar has starved a control by
        // ignoring before now.
        undoButton.setBounds(bar.removeFromLeft(62).withSizeKeepingCentre(60, 24));
        bar.removeFromLeft(4);
        redoButton.setBounds(bar.removeFromLeft(62).withSizeKeepingCentre(60, 24));
        bar.removeFromLeft(14);

        constexpr int chipMin = 60, chipMax = 150, bigGap = 14;
        const int chipCell = onBuildInstrumentMenu != nullptr ? chipMax + bigGap : 0;
        const int spareForChipAndCombos = bar.getWidth() - bpmGroupW - bigGap;
        const bool roomForLabels = (spareForChipAndCombos - chipCell) >= widthOf(roomy);
        const auto& cells = roomForLabels ? roomy : tight;
        const int chipW = juce::jlimit(chipMin, chipMax,
                                       spareForChipAndCombos - bigGap - widthOf(cells));

        instrumentChip.setVisible(onBuildInstrumentMenu != nullptr);
        if (onBuildInstrumentMenu)
        {
            instrumentChip.setBounds(bar.removeFromLeft(chipW).withSizeKeepingCentre(chipW, 24));
            bar.removeFromLeft(bigGap);
        }
        // No host hook: plain Keys. The cell the chip would have taken is simply not spent,
        // and the tempo group below starts where Knobs used to sit - the bar is otherwise
        // exactly what it always was here.

        // The tempo, at the head of the plugin the way a DAW puts it at the head of the
        // transport (2026-08-02, Owen's ask). Never hidden with this section: it is a
        // parameter you reach for while playing, the arp On argument, and the arp reads it
        // with its own section folded away. BPM's caption sits right against the stepper the
        // way Root's does against its combo (place(), below); Sync follows the field it
        // labels rather than leading it, since it is a modifier on the number beside it and
        // not a fifth thing to read before the number itself.
        bpmBarLabel.setBounds(bar.removeFromLeft(34).withSizeKeepingCentre(34, 24));
        bar.removeFromLeft(lblGap);
        bpmPrevButton.setBounds(bar.removeFromLeft(26).withSizeKeepingCentre(26, 24));
        bar.removeFromLeft(3);
        bpmField.setBounds(bar.removeFromLeft(56).withSizeKeepingCentre(56, 24));
        bar.removeFromLeft(3);
        bpmNextButton.setBounds(bar.removeFromLeft(26).withSizeKeepingCentre(26, 24));
        bar.removeFromLeft(gap);
        bpmSyncButton.setBounds(bar.removeFromLeft(62).withSizeKeepingCentre(60, 24));
        bar.removeFromLeft(bigGap);

        {
            const auto place = [&bar](const Cell& c, juce::Label* lab, juce::Component& ctl)
            {
                if (lab != nullptr && c.label > 0)
                {
                    lab->setBounds(bar.removeFromLeft(c.label).withSizeKeepingCentre(c.label, 24));
                    bar.removeFromLeft(lblGap);
                }
                ctl.setBounds(bar.removeFromLeft(c.box).withSizeKeepingCentre(c.box, 24));
                bar.removeFromLeft(gap);
            };
            scaleLabel.setVisible(false); // "Major" is its own caption, in both sets
            // Root's caption is the one Owen did not ask for, so it is the one that tracks
            // the tier switch; Voices and CH are captioned in both cells[] sets now (see
            // above) and so are simply always on.
            rootLabel.setVisible(roomForLabels);
            polyphonyLabel.setVisible(true);
            channelLabel.setVisible(true);
            place(cells[0], &rootLabel, rootBox);
            place(cells[1], nullptr, scaleBox);
            place(cells[2], nullptr, scaleLockButton);
            place(cells[3], &polyphonyLabel, polyphonyBox);
            place(cells[4], &channelLabel, channelBox);
        }
        bar.removeFromLeft(6);
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
        // Hold off, All Off and Light keys sit on the bar, so they survive folding the section
        // away. 24 px tall, like every other control on a bar that acts rather than folds
        // (Sustain, Fill, Regen): contentArea() is the 34 px strip less 4 at each end, so 26
        // is the ceiling here and the mouse-only floor is bought in width instead - 86 px of
        // Hold off is a bigger target than a 34 px square.
        auto bar = layoutDetachRow(secArp, arpBar.contentArea(), true);
        bar.removeFromRight(6);
        arpHoldOffButton.setBounds(bar.removeFromRight(88).withSizeKeepingCentre(86, 24));
        bar.removeFromRight(4);
        arpAllOffButton.setBounds(bar.removeFromRight(84).withSizeKeepingCentre(82, 24));
        bar.removeFromRight(10);
        // Light keys last, so the two stop buttons sit together at the right end where the
        // hand goes for them. A toggle needs its box plus the words, hence the wider cell.
        arpLightsButton.setBounds(bar.removeFromRight(120).withSizeKeepingCentre(118, 24));

        // A and B, from the left (2026-08-02, second pass): the arp's own On switches now, so
        // - unlike All beside them - they are laid out whether or not the section is open,
        // the same "reach for it while playing" case BPM and Quantize already were. 40 px
        // each: a letter needs no more.
        for (auto& t : arpBarTabs)
        {
            if (t == nullptr)
                continue;
            t->setBounds(bar.removeFromLeft(40).withSizeKeepingCentre(38, 24));
            bar.removeFromLeft(4);
        }
        // All still only navigates the panel, so its cell still collapses with the fold
        // rather than leaving Quantize orbiting a hole where it was (the pageButtons lesson).
        if (processor.layout.arp && arpBarAllTab != nullptr)
            arpBarAllTab->setBounds(bar.removeFromLeft(44).withSizeKeepingCentre(42, 24));
        // The three page tabs, in the same collapsing cell as All and for the same reason:
        // they navigate a panel, so with the section folded there is nothing behind them.
        // A small gap first, so All reads as the head of this group rather than as one more
        // letter beside A and B.
        if (processor.layout.arp && ! processor.layout.arpMacro)
        {
            bar.removeFromLeft(6);
            for (auto& t : arpPageTabs)
            {
                if (t == nullptr)
                    continue;
                t->setBounds(bar.removeFromLeft(62).withSizeKeepingCentre(60, 24));
                bar.removeFromLeft(3);
            }
        }
        bar.removeFromLeft(14);
        quantizeBarLabel.setBounds(bar.removeFromLeft(56).withSizeKeepingCentre(56, 24));
        quantizeBarBox.setBounds(bar.removeFromLeft(92).withSizeKeepingCentre(90, 24));
        bar.removeFromLeft(8);
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
        // The generator's Key, alone now. Mode, Scale Compliance and the arp target letter
        // left this bar on 2026-08-02 (Owen: "remove the scale and percentage and letter b
        // from pads header"): both combos are still in the Generator window, which holds
        // every setting it has, and the current arp line is chosen by the A/B tabs on the arp
        // bar. Key stays because it is the one generator setting you reach for between fills.
        //
        // 24 px like everything on a bar that acts rather than folds, so the whole group is
        // one height. What the bar spends, at its floor and above, is worked out in
        // minWidthForView().
        bar.removeFromRight(10);
        // Mode then Key, right to left, so they read Key-then-Mode left to right the way the
        // generator window's own top row does. Mode is the wide one: "Mixolydian" and
        // "Phrygian Dominant" are what has to fit, and a ComboBox ellipsises rather than
        // complaining (the Voices lesson, 2026-08-02).
        genModeBox.setBounds(bar.removeFromRight(124).withSizeKeepingCentre(122, 24));
        bar.removeFromRight(4);
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
        // Humanize and its velocity range rode this bar from 2026-08-02 to 2026-08-03, when
        // they became a range knob *in the strip* with Strum beside them. They are pad
        // controls; the bar was only ever where they fitted. The 232 px they held here is the
        // caption's now, so the section's name has room again.
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

        // Size and Octave, from the left (2026-08-02, Owen: "the size can go down to the
        // header of the keyboard button"). Both stay put with the section folded, the same
        // reach-for-it-while-playing argument as the tempo/Root/Scale group on the Controls
        // bar: Octave is the keybed's only pitch-range control (25 keys cannot pan; it is
        // C3..C5 by construction), and folding the band away is exactly when you still want
        // it. Not a slider - a bar control is 24 px tall, and IncDecButtons' arrows would
        // stack to 12 px each - so it is the BPM field's own shape: caption, `<`, read-out, `>`.
        sizeBox.setBounds(bar.removeFromLeft(104).withSizeKeepingCentre(104, 24));
        bar.removeFromLeft(6);
        octaveBarLabel.setBounds(bar.removeFromLeft(30).withSizeKeepingCentre(30, 24));
        octPrevButton.setBounds(bar.removeFromLeft(26).withSizeKeepingCentre(26, 24));
        bar.removeFromLeft(3);
        octaveReadout.setBounds(bar.removeFromLeft(42).withSizeKeepingCentre(42, 24));
        bar.removeFromLeft(3);
        octNextButton.setBounds(bar.removeFromLeft(26).withSizeKeepingCentre(26, 24));

        // REC and the take chip, off the left after Octave. Fixed widths taken before the
        // caption gets what is left, the standing reserve-first rule - though this bar has
        // room to spare at the floor (the four right-hand toggles and the Size/Octave group
        // spend ~634 px of the ~1048 the bar hands out at 1280), so unlike the Controls bar
        // this pair costs no floor. They stay put when the section folds for the same reason
        // Sustain and All Off do: a stop button that folds away mid-take is not a stop button.
        bar.removeFromLeft(14);
        recButton.setBounds(bar.removeFromLeft(62).withSizeKeepingCentre(62, 24));
        bar.removeFromLeft(6);
        takeChip.setBounds(bar.removeFromLeft(116).withSizeKeepingCentre(116, 24));

        section(secKeyboard).caption = bar;
    }
    if (sectionHeight(secKeyboard) > 0)
    {
        area.removeFromTop(4);
        keybedHolder.setBounds(area); // the slack is instrument body under the keys
    }
}

} // namespace keys
