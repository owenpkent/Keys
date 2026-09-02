#include "MacroRow.h"
#include "ArpPanel.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>
#include <cmath>

namespace keys
{
namespace
{
    // The knobs a macro row carries, in the order they are laid out. Heading, parameter and
    // tooltip in one place so the columns cannot drift out of step with what they set.
    struct MacroKnobSpec { KeysProcessor::ArpParam param; const char* heading; const char* tip; };
    const MacroKnobSpec macroKnobSpecs[] = {
        { KeysProcessor::apOctShift, "OCT",
          "Moves this whole line up or down whole octaves. Centred: nothing at 12 o'clock, down "
          "to the left, up to the right. Put one line an octave under the other and they stop "
          "fighting for the same register." },
        { KeysProcessor::apGate, "GATE",
          "How much of each step this line's notes fill. Short gates let another line through." },
        // DENSITY (2026-09-01, Owen: "a density knob where it controls, like, how much notes
        // there are"). It is the Chance parameter - the Play page's slider, given a face here -
        // and it reads as Density on both, because "chance" is what the *lane* is called and
        // this is the one knob that thins a whole line. The lower half of Owen's ask; the half
        // that would add notes is the Ratchet lane and the harmony voices, which already exist.
        { KeysProcessor::apChance, "DENSITY",
          "How many of this line's steps actually play. At 100 every step fires; turn it down "
          "and steps drop out at random, thinning the run. Multiplies the Chance lane. With "
          "LEGATO on, the note before a dropped step holds through it instead of leaving a gap." },
        // DUCK (2026-09-01): the first knob of the line bus - see the Knob enum for why it is
        // here and not on the Play page. Live on every card, line A included, rather than greyed
        // when there is nothing to follow: the card's rule is that nothing on it is ever
        // disabled, and the From chip below says whether it is doing anything.
        { KeysProcessor::apDuck, "DUCK",
          "How often this line skips a step right after the line it follows played one - the "
          "hocket: where that line plays, this one leaves the gap. Pick the line with From on "
          "the strip below. At 0, or with From off, nothing changes. LOCK holds what it finds." },
        // MUTATE and LOCK replaced CHANCE here on 2026-08-18 (Owen: "explore the chance knob
        // being a drift instead where it explores other patterns and notes... could be multiple
        // knobs. want notes. mutations"). Chance lost nothing by it: it is still a step lane and
        // still has its own slider in the Play page's PLAYBACK group, which is where a control
        // you set once and leave belongs. These two are the ones you sit and turn.
        { KeysProcessor::apMutate, "MUTATE",
          "How often the run swaps a step for a different note of the chord you are holding, "
          "and how far along the chord it reaches to find one. It never plays a note that is "
          "not in your chord, at any setting - that is what STRAY beside it is for." },
        // STRAY (2026-08-21) is Mutate's own upper half, moved out to a control of its own.
        // Owen: "it's adding additional notes in the arpeggiator ... it should just change the
        // existing ones" - the additional notes were this stage, reachable only by turning
        // Mutate past halfway, so there was no way to explore the chord hard without them.
        // Zero is off and is the default, which is what makes turning MUTATE up safe again.
        { KeysProcessor::apStray, "STRAY",
          "How often a step is allowed to land on a note that is not in your chord. At 0 - "
          "where it starts - this line plays your chord and nothing else. Turned up, strays "
          "are in-scale neighbours at first and go chromatic towards the top. LOCK holds them "
          "like any other variation, so a wrong note worth keeping stays." },
        { KeysProcessor::apMutateLock, "LOCK",
          "How long it keeps what it finds. Left, a new variation every time round; right, the "
          "first one it finds repeats for good. In between it holds an idea, then moves on." },
        { KeysProcessor::apSwing, "SWING",
          "Shifts this line's offbeats late (right) or early (left). The quickest way to stop "
          "two lines landing on top of each other." },
        { KeysProcessor::apOffset, "OFFSET",
          "Starts this line's pattern from a different foot. Two lines on the same rate and "
          "different offsets are out of phase rather than in unison." },
        // VEL replaced VOL on 2026-08-02 (Owen: "it should start in the middle so you can
        // turn it up or down. But really, the volume is controlling velocity"): bipolar,
        // centred at "as played", and named for what it actually touches. The old arpVolume
        // parameter still exists for saved sessions; migrateVelTrim folds it into this.
        // Humanize Velocity folded into its ring on 2026-08-17 (Owen: "I didn't realize there
        // was a separate velocity knob. I only want one velocity knob, and I want this
        // humanize section to be the outer ring") - see the RangeKnob wiring in the
        // constructor for how the ring reaches arpHumanVel instead of a span of VEL itself.
        // Absolute MIDI velocity since 2026-08-18 (Owen, on the readout reading "-31 ~20":
        // "still wrong"). It was apVelTrim, a bipolar *percentage* trim on the velocity that
        // arrived - percentages on a control called VEL, beside a pads knob that had just become
        // an absolute 0-127 band. One quantity, one unit, one number you can read.
        { KeysProcessor::apVelLevel, "VEL",
          "This line's velocity, 0-127. The knob is the middle of the band and the ring is how "
          "far either side of it a hit can land, so the two read as one band - the same as the "
          "pads' own Humanize knob. At 0 the line is silent. The way to balance two lines "
          "against each other without playing one of them softer." },
        // HUMAN split into its halves on 2026-08-02 (Owen: "maybe we could split it up into
        // two knobs"), so timing and dynamics randomize independently. H.VEL, the velocity
        // half, moved onto VEL's own ring above; only the timing half is still its own knob.
        { KeysProcessor::apHumanize, "H.TIME",
          "Nudges each hit a little late, by a different amount every time. At 0 the line is "
          "dead on the grid." },
    };
    static_assert(sizeof(macroKnobSpecs) / sizeof(macroKnobSpecs[0])
                      == (size_t) MacroRow::numKnobs,
                  "every macro knob needs a heading and a parameter");
} // namespace

void MacroRow::HarmonyBox::showPopup()
{
    auto menu = ArpPanel::buildHarmonyMenu(*this);
    // The box's own LookAndFeel, exactly as ComboBox::showPopup hands its internal menu:
    // a PopupMenu does not inherit the target component's skin on its own, and without this
    // the two columns came up in JUCE's stock grey. Set here rather than in the builder so the
    // builder needs no live component and a test can call it against a box alone.
    // The standard item height JUCE's own ComboBox::showPopup passes, so the popup's geometry
    // tracks the box it belongs to rather than falling through to whatever the LookAndFeel
    // happens to answer. Those agree today at the 34 px mouse-only height; this keeps them
    // agreeing if either moves.
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withStandardItemHeight(juce::jmax(34, getHeight()))
                           .withTargetComponent(this)
                           .withItemThatMustBeVisible(getSelectedId())
                           // What LookAndFeel_V2::getOptionsForComboBoxPopupMenu passes for
                           // every stock combo, and `StepComboBox` passes too: it highlights the
                           // current row on open, which on a 27-row two-column menu is the
                           // difference between seeing where you are and hunting for a tick.
                           .withInitiallySelectedItem(getSelectedId())
                           .withMinimumWidth(getWidth()),
                       [safe = juce::Component::SafePointer<HarmonyBox>(this)](int result)
                       {
                           if (safe == nullptr)
                               return;
                           // hidePopup first: it is what resets the box's own menu-active
                           // state, which showPopupIfNotActive set before calling us.
                           safe->hidePopup();
                           if (result != 0)
                               safe->setSelectedId(result);
                       });
}

// A die showing five, which is the face that reads at this size: four corners and a centre stay
// distinct where six pips smear into two columns and three reads as a diagonal smudge. Drawn
// from the button's own bounds rather than carrying a magic size, so it follows whatever cell
// the row gives it.
//
// That cell is **34 x 26**, not 34 square: `centred` in MacroRow::resized fixes every control on
// this line at 26 px tall, and the dice matching its neighbours is what keeps the row reading as
// one row. So it is under CLAUDE.md's 34 px floor in one axis, exactly as the four steppers and
// the two combos beside it have always been - a standing property of this line rather than
// anything the dice introduced. Worth fixing, and worth fixing for the whole row at once.
//
// **What that costs, so the decision is one somebody can actually take** (measured 2026-08-21,
// in review): `centred` would have to go and `arpMacroLine` grow by 8 px, which is 8 px on every
// card, two card rows, so 16 px on the All view - and the All view is what sets the window's
// minimum height. That lands on a figure the fold bullet in CLAUDE.md already shows is tight
// against Owen's own 1392 px work area. It is affordable; it is not free, and it is a product
// call rather than a tidy-up, which is why this note says what it would take instead of
// quietly taking it.
void MacroRow::DiceButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced(5.0f);
    const bool live = isEnabled();
    const auto ink = live ? (highlighted ? skin::text : skin::textDim) : skin::textFaint;

    if (live && (highlighted || down))
    {
        g.setColour(skin::accentOf(*this).base.withAlpha(down ? 0.22f : 0.12f));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 4.0f);
    }

    g.setColour(ink);
    g.drawRoundedRectangle(r, 3.0f, 1.4f);

    const float pip = juce::jmax(1.6f, r.getWidth() * 0.11f);
    const float ox = r.getWidth() * 0.26f, oy = r.getHeight() * 0.26f;
    const auto c = r.getCentre();
    for (auto p : { juce::Point<float>(c.x - ox, c.y - oy), juce::Point<float>(c.x + ox, c.y - oy),
                    c,
                    juce::Point<float>(c.x - ox, c.y + oy), juce::Point<float>(c.x + ox, c.y + oy) })
        g.fillEllipse(juce::Rectangle<float>(pip * 2.0f, pip * 2.0f).withCentre(p));
}

MacroRow::MacroRow(ArpPanel& o, KeysProcessor& p, int n) : owner(o), processor(p), line(n)
{
    okstudio::ui::makeMouseOnly(*this);
    const auto letter = juce::String::charToString((juce::juce_wchar) ('A' + n));
    const auto id = [n](KeysProcessor::ArpParam w) { return KeysProcessor::arpParamId(n, w); };
    // Every card carries its own headings since the cards went side by side (2026-08-02):
    // "written once on the top row" only worked while the rows stacked and B's columns sat
    // exactly under A's.
    const auto heading = [this](juce::Label& l, const char* text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(skin::micro(9.0f));
        l.setColour(juce::Label::textColourId, skin::textFaint);
        addAndMakeVisible(l);
    };

    // No Latch, PLAY or Chain on the row since the second 2026-08-02 pass (Owen: "I think we
    // can remove the chain button, maybe the play and the [latch] button"): the rows keep
    // what you reach for while both lines run, and the set-and-forget switches live on the
    // line's own tab - Latch and Play on the band, Chain on the action row. PLAY's history
    // (it was KEYS, and collided with the bar's Light keys) moved to keysBandButton with it.
    // The line switch itself (onButton) left the same way on 2026-08-02, the day the A/B
    // chips on the ARP section bar became the per-line On toggles: the card's own On/Off is
    // shown instead as a scrim over the whole body (paintOverChildren), never as a second
    // control bound to the same parameter.

    // The rate's three modifiers, on the sub-row under it. Same three the band carries, same
    // parameters, and greyed by the same question - see refreshRateMode.
    dotButton.setTitle("Macro dot " + letter);
    dotButton.setTooltip("Dotted: each step lasts half again as long, so 1/8 becomes a dotted "
                         "1/8. Stacks with Tuplet, which is a different question. Greyed with "
                         "the rate in Hz, where there is no beat to dot.");
    // Tuplet is a combo here too, and uncaptioned: the sub-row is one 34 px strip with no
    // caption anywhere on it, so the entries have to name themselves - which is why the list
    // reads "Triplet" and "5-tuplet" rather than "3" and "5".
    tupletBox.addItemList(KeysProcessor::tupletChoices(), 1);
    tupletBox.setTitle("Macro tuplet " + letter);
    tupletBox.setTooltip("Fits an odd number of steps into the space a power of two would take: "
                         "Triplet is three where two go, 5-tuplet five where four go. The rate "
                         "readout shows the result, so 1/4 in fives reads \"1/5\". Greyed with "
                         "the rate in Hz, where there is no beat to divide. Run one line straight "
                         "and the other in fives and you have the polyrhythm this view is for.");
    addAndMakeVisible(tupletBox);
    // The dot changes the rate readout as well as the timing, and its attachment does not know
    // that: the dial's text is a function of three parameters and bound to one.
    dotButton.onClick = [this] { refreshTuplet(); };
    anchorButton.setTitle("Macro anchor " + letter);
    // Anchor's tooltip is written by refreshRateMode, beside its enablement: it says something
    // different in Hz, where there is no bar grid to anchor to. Same split as the band's.
    for (auto* b : { &dotButton, &anchorButton })
        addAndMakeVisible(*b);
    dotAtt = std::make_unique<ButtonAtt>(processor.apvts, id(KeysProcessor::apDot), dotButton);
    tupletAtt = std::make_unique<ComboAtt>(processor.apvts, id(KeysProcessor::apTuplet), tupletBox);
    anchorAtt = std::make_unique<ButtonAtt>(processor.apvts, id(KeysProcessor::apAnchor), anchorButton);

    // Legato (2026-09-01): the other half of Density's ask. A step that does not fire - Density
    // turned down, a Chance cell, a mute, a rest, a Chain condition - is a silence with this
    // off; on, the note before it holds through and is released just after the next fired
    // step's note-on, the overlap a synth's legato or glide mode needs. Not greyed in Hz or on
    // any shape: skips happen on every clock and every shape alike.
    legatoButton.setTitle("Macro legato " + letter);
    legatoButton.setTooltip("Hold a note through the steps that do not fire - Density turned "
                            "down, a Chance cell, a mute, a rest - and let it go just after the "
                            "next note that does, so a synth in legato or glide mode slides "
                            "instead of restarting. Gate still ends a note when the next step "
                            "fires; this only fills the gaps.");
    addAndMakeVisible(legatoButton);
    legatoAtt = std::make_unique<ButtonAtt>(processor.apvts, id(KeysProcessor::apLegato), legatoButton);

    // From: who this line listens to (2026-09-01, the line bus). Every entry the parameter can
    // hold is in the list - the attachment maps index to item - and the ones this line may not
    // pick are greyed in place: item 2 is A, item 3 is B, item 4 is C, and a line may only
    // follow a letter above it, which the processor enforces as well (see runArpLines).
    followsBox.addItemList(KeysProcessor::followChoices(), 1);
    for (int src = 0; src < 3; ++src)
        followsBox.setItemEnabled(src + 2, src < line);
    followsBox.setTitle("Macro follows " + letter);
    followsBox.setTooltip("Which line this one listens to - only a letter above it, so nothing "
                          "can loop. What it does with what it hears: DUCK on the knob strip, "
                          "Follow in the Play page's Retrigger list (restart when that line comes "
                          "round), and 3 and 4 on the Draw page's Chain lane (play only with, or "
                          "only against, that line's last step). "
                          "A line that is off or silent leaves this one playing as if it followed "
                          "nobody." + juce::String(line == 0 ? " Line A has nothing above it." : ""));
    addAndMakeVisible(followsBox);
    followsAtt = std::make_unique<ComboAtt>(processor.apvts, id(KeysProcessor::apFollow), followsBox);

    // Opens this line's own detailed view - the band, and the step editor once Shape is
    // Pattern. With the A/B tabs gone as navigation (they are the section bar's On switches
    // now), this is the only way from a macro card back to the deep controls.
    detailsButton.setTitle("Macro details " + letter);
    detailsButton.setTooltip("Open line " + letter + "'s detailed view.");
    detailsButton.onClick = [this] { owner.setEditLine(line); };
    addAndMakeVisible(detailsButton);

    // Rate: the same detented dial the band uses, so a division is a detent and the readout
    // under it says which one. The steppers beside it are the click-only path to every value,
    // in both units - a dial is a drag target, and this panel may not require a drag.
    rateKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    rateKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 58, 15);
    rateKnob.setTitle("Macro rate " + letter);
    rateKnob.setTooltip("This line's rate: a time division in Sync, a frequency in Hz. Polyrhythm "
                        "lives here - put one line on 1/8 and another on a 1/8 triplet.");
    rateKnob.onDragStart = [this] { rateDragging = true; };
    rateKnob.onDragEnd = [this] { rateDragging = false; refreshRateMode(); };
    addAndMakeVisible(rateKnob);
    heading(knobLabels[kOctShift], macroKnobSpecs[kOctShift].heading); // laid out with the rest
    // Names over the top line's two stepper groups (2026-08-02, Owen: "the arrows to adjust
    // certain parameters are not clear as to what they're adjusting"): `< dial > Sync < combo >`
    // is two flanked pairs touching, and without words above them the middle two arrows read
    // as one orphaned pair.
    heading(rateHeadLabel, "RATE");
    heading(shapeHeadLabel, "SHAPE");

    ratePrev.onClick = [this] { stepRate(-1); };
    rateNext.onClick = [this] { stepRate(1); };
    for (auto* b : { &ratePrev, &rateNext })
    {
        b->setTooltip("Step this line's rate: one division in Sync, a quarter of an octave in Hz.");
        addAndMakeVisible(*b);
    }
    rateModeButton.setClickingTogglesState(true);
    rateModeButton.setTitle("Macro rate mode " + letter);
    rateModeButton.setTooltip("Sync follows the tempo and its bar grid; Hz free-runs whatever "
                              "the transport is doing.");
    addAndMakeVisible(rateModeButton);
    rateModeAtt = std::make_unique<ButtonAtt>(processor.apvts, id(KeysProcessor::apRateFree), rateModeButton);

    shapeBox.addItemList({ "Up", "Down", "Up-Down", "Down-Up", "Up & Down", "Down & Up",
                           "As Played", "Reversed", "Random", "Random Other", "Random Once",
                           "Chord", "Fingered Bottom", "Fingered Top" }, 1);
    shapeBox.addItem("Pattern", ArpEngine::numDirections + 1);
    shapeBox.onChange = [this] { applyShape(); };
    shapeBox.setTitle("Macro shape " + letter);
    shapeBox.setTooltip("What this line plays. \"Pattern\" hands it to the step editor on that "
                        "line's own tab.");
    addAndMakeVisible(shapeBox);
    shapePrev.onClick = [this] { stepShape(-1); };
    shapeNext.onClick = [this] { stepShape(1); };
    for (auto* b : { &shapePrev, &shapeNext })
        addAndMakeVisible(*b);

    // The dice deals this line's Random Once shape a new order. It writes no parameter - the
    // order is engine state, not something a session stores - so there is no attachment and no
    // gesture to bracket; it is one call, and the audio thread picks it up on its next block.
    diceButton.setTitle("Macro reroll " + letter);
    diceButton.setTooltip("Deal this line a new random order. Only Random Once has an order to "
                          "deal again - Random and Random Other draw a fresh note every step, "
                          "so there is nothing stored for this to change.");
    diceButton.onClick = [this] { processor.rerollArpRandom(line); };
    addAndMakeVisible(diceButton);

    // Keybed: whether what you play feeds this line, where you can see it (2026-09-01). The
    // same parameter as the Play page's toggle, one name on both. Live on every card, off line
    // or on, like everything else here.
    keybedButton.setTitle("Macro keybed " + letter);
    keybedButton.setTooltip("Does this line arpeggiate what you play on the keybed? On, the keys "
                            "you hold feed it. Off, it ignores the keybed and plays only the chord "
                            "cards you hand it - which is what lets one line follow your hands "
                            "while another runs a card. The same switch as Keybed on this line's "
                            "Play page. MIDI arriving on the track is a separate question: Track "
                            "MIDI, on the arp bar.");
    addAndMakeVisible(keybedButton);
    keybedAtt = std::make_unique<ButtonAtt>(processor.apvts, id(KeysProcessor::apKeys), keybedButton);
    // Seed the combo *before* asking the dice about it. `addItemList` selects nothing and the
    // shape has no attachment to seed it either (applyShape writes two parameters by hand), so
    // the selection is -1 until something sets it - and refreshDice reading -1 opened the
    // button greyed on a Random Once session, live only after the panel's first 10 Hz tick.
    // The comment here claimed the opposite for an afternoon, which is the tell: a card built
    // from a saved session must be *right* when it is built, not a hundred milliseconds later.
    syncShapeBox();
    refreshDice(); // so a card built on a Random Once session opens with it live

    // The settings a regular arpeggiator has, as the skin's machined rotary - the same
    // knob the band above draws the same parameters with (Owen, 2026-08-01: "what other knobs
    // can we have? should be like regular arp settings").
    for (int k = 0; k < numKnobs; ++k)
    {
        const auto name = juce::String(macroKnobSpecs[(size_t) k].heading);
        // Two of the knobs are ranges. Everything below is written against knobFace(), so a
        // range knob is attached, titled and tooltipped exactly as a plain one - the ring is
        // the only extra, and it is wired once, here. The two rings are not the same shape:
        // H.TIME's is a genuine span of its own face (the parameter is the draw's ceiling,
        // the ring is its floor); VEL's ring is a *different* parameter's own value -
        // Humanize Velocity, folded in here on 2026-08-17 (Owen: "I only want one velocity
        // knob, and I want this humanize section to be the outer ring") so loudness has one
        // knob per line instead of two two cells apart.
        if (isRangeKnob(k))
        {
            auto& rk = *(ranges[(size_t) k] = std::make_unique<RangeKnob>());
            rk.setFaceInset(arpRingPx);
            rk.setReadoutHeight(15); // the plain knobs' text box, so the row's numbers line up
            rk.setTitle("Macro " + name + " range " + letter);
            rk.spanHandle().setTitle("Macro " + name + " range handle " + letter);
            addAndMakeVisible(rk);

            if (k == kVel)
            {
                // arpHumanVel (0-100, "Human Velocity") *is* the ring here, not a span of
                // VEL's own value - VEL can sit anywhere from -100 to 100 and the ring still
                // reaches straight down from wherever that is. Its own former ring,
                // arpHumanVelSpan, is pinned to 100 by every write below rather than removed:
                // the draw was already uniform between the floor and the knob before this
                // merge (the parameter's own default), and pinning it is what keeps that true
                // with one fewer number on screen. The engine reads both parameters exactly
                // as it always did; only the card stops exposing the span as its own control.
                rk.setTooltip("This line's velocity band, 0-127: the knob is the middle of the "
                              "band and the ring is how far either side of it a hit can land, "
                              "by a different amount every time. Click the little dial at the "
                              "top left to switch Humanize Velocity on or off; drag it, or "
                              "anywhere on the ring, to set how far it reaches.");
                rk.setSpanTooltip("Drag up for a wider velocity band, down for a tighter one - "
                                  "it opens equally both ways around the knob, which stays "
                                  "put. The wheel works too. Click to switch Humanize Velocity "
                                  "on or off.");
                // **The plain lo-hi readout, same as every other range knob** (2026-08-18). It
                // read "level ~reach" while the face was a bipolar trim and the ring a percentage
                // of shave: two different quantities in two different units, which no two-number
                // format can honestly join, and which produced things like "-31 ~20" - numbers
                // that look like velocities and are not. Both ends are MIDI velocity now, so the
                // band names itself and the default format is the right one.

                const auto velId = id(KeysProcessor::apHumanVel);
                const auto spanId = id(KeysProcessor::apHumanVelSpan);
                // **The ring's own range, not the face's** (see RangeKnob::setSpanMax). VEL's
                // face is a bipolar trim, -100..100, so the default would calibrate the drag
                // to 200 units while arpHumanVel stops at 100: the top half of the satellite's
                // travel wrote nothing, and refresh() below read the clamped value back at
                // 10 Hz and yanked the arc down under a hand still moving up. Taken from the
                // parameter rather than written as 100 so the two cannot drift apart.
                // Both are 0..127 now, so this lands on the face's own travel anyway - it stays
                // read from the ring's parameter because that is the rule (see setSpanMax), not
                // because the two happen to differ today.
                // spanMax() takes half the face's travel off this again, which is what makes
                // the top of the sweep reachable: the band is centred on the face, so it can
                // never open wider than room() allows, and room() tops out at half. Before
                // that landed (2026-08-23) about four fifths of this drag wrote a parameter
                // the ring could not draw.
                const auto velRange = processor.apvts.getParameterRange(velId);
                rk.setSpanMax((double) (velRange.end - velRange.start));
                // A lamp, unlike H.TIME's ring: VEL keeps a level even with Humanize off, and
                // its knob at zero means *silent* rather than "no wander" - so no position of
                // the face can double as Humanize Velocity's own off switch the way a plain
                // Humanize knob's zero does. isOn/setOn give the lamp that job instead (2026-08-17, Owen:
                // "whether humanize velocity is on at all"); off parks the amount at zero and
                // remembers what it was, the same shape ChordPads' Strum lamp uses.
                rk.isOn = [this, velId]
                { return processor.apvts.getRawParameterValue(velId)->load() > 0.0f; };
                rk.setOn = [this, velId, spanId](bool on)
                {
                    auto* vp = processor.apvts.getParameter(velId);
                    if (vp == nullptr)
                        return;
                    if (! on)
                        lastHumanVelAmount = juce::jmax(1.0, (double) processor.apvts
                                                 .getRawParameterValue(velId)->load());
                    vp->beginChangeGesture();
                    vp->setValueNotifyingHost(vp->convertTo0to1((float) (on ? lastHumanVelAmount : 0.0)));
                    vp->endChangeGesture();
                    if (auto* sp = processor.apvts.getParameter(spanId))
                    {
                        sp->beginChangeGesture();
                        sp->setValueNotifyingHost(sp->convertTo0to1(100.0f));
                        sp->endChangeGesture();
                    }
                };
                // By hand, with the gesture brackets an attachment would have given it: the
                // ring is not a Slider, so there is nothing for a SliderAttachment to bind to.
                // The same shape Shape and the rate steppers use a few hundred lines up - and
                // now two parameters move together instead of one, since every drag also pins
                // the span.
                rk.onSpanDragStart = [this, velId, spanId]
                {
                    if (auto* p = processor.apvts.getParameter(velId))
                        p->beginChangeGesture();
                    if (auto* p = processor.apvts.getParameter(spanId))
                        p->beginChangeGesture();
                };
                rk.onSpanChanged = [this, velId, spanId](double v)
                {
                    if (auto* p = processor.apvts.getParameter(velId))
                        p->setValueNotifyingHost(p->convertTo0to1((float) v));
                    if (auto* p = processor.apvts.getParameter(spanId))
                        p->setValueNotifyingHost(p->convertTo0to1(100.0f));
                };
                rk.onSpanDragEnd = [this, velId, spanId]
                {
                    if (auto* p = processor.apvts.getParameter(velId))
                        p->endChangeGesture();
                    if (auto* p = processor.apvts.getParameter(spanId))
                        p->endChangeGesture();
                };
            }
            else // kHTime
            {
                rk.setTooltip("The knob is the typical lateness; the ring around it is how far "
                              "either side of that a hit can land. Drag the little dial at the "
                              "top left - or anywhere on the ring - to open and close it. Wide "
                              "open, hits wander from dead on the grid to twice the knob; "
                              "closed, every hit is exactly that late. Turn the knob and the "
                              "whole range moves with it.");
                rk.setSpanTooltip("Drag up to open this knob's range, down to close it - it "
                                  "opens equally both ways around the knob, which stays put. "
                                  "The wheel works too.");
                // Both ends in one readout, in the knob's own units - a range that only shows one
                // of its ends is the readout problem the arp rate had this morning.
                rk.textFromRange = [](double lo, double hi)
                { return juce::String((int) lo) + "-" + juce::String((int) hi); };

                const auto spanId = id(KeysProcessor::apHumanizeSpan);
                // Set for the same reason as VEL's above, and here it names exactly the
                // number the default already produced: face and ring are both 0..100. It is
                // kept so the rule reads the same on both knobs rather than as a special case
                // living on one of them - spanMax() halves it either way, which is what stopped
                // 228 px of this 300 px drag being inert at the shipping default of 24.
                const auto humanRange = processor.apvts.getParameterRange(spanId);
                rk.setSpanMax((double) (humanRange.end - humanRange.start));
                // By hand, with the gesture brackets an attachment would have given it: the ring
                // is not a Slider, so there is nothing for a SliderAttachment to bind to. The
                // same shape Shape and the rate steppers use a few hundred lines up.
                rk.onSpanDragStart = [this, spanId]
                {
                    if (auto* p = processor.apvts.getParameter(spanId))
                        p->beginChangeGesture();
                };
                rk.onSpanChanged = [this, spanId](double v)
                {
                    if (auto* p = processor.apvts.getParameter(spanId))
                        p->setValueNotifyingHost(p->convertTo0to1((float) v));
                };
                rk.onSpanDragEnd = [this, spanId]
                {
                    if (auto* p = processor.apvts.getParameter(spanId))
                        p->endChangeGesture();
                };
            }
        }

        auto& knob = knobFace(k);
        knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        // Read-only text box, like every other knob here: the value belongs under the knob, and
        // an editable box is a keyboard target on a surface that has none. A range knob draws
        // its own readout instead, since it has two numbers to show.
        if (! isRangeKnob(k))
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 52, 15);
        knob.setTooltip(macroKnobSpecs[(size_t) k].tip);
        knob.setTitle("Macro " + name + " " + letter);
        if (! isRangeKnob(k))
            addAndMakeVisible(knob); // a range knob's face is already its own child
        if (k != kOctShift)          // its heading is already made, above
            heading(knobLabels[(size_t) k], macroKnobSpecs[(size_t) k].heading);
        knobAtts[(size_t) k] = std::make_unique<SliderAtt>(
            processor.apvts, id(macroKnobSpecs[(size_t) k].param), knob);
        // **The attachment is what gives the face its range, and spanMax() reads that range.**
        // So the setSpanMax() calls above - a hundred lines up, inside the range-knob branch -
        // ran while the face was still on JUCE's default 0..10 and re-clamped at a ceiling of
        // five. It is inert today only because the span is still zero at that point, which is
        // luck rather than design: the whole point of that re-clamp is to bite when a ceiling
        // narrows under a live span. Re-running it here, once the face knows its own range, is
        // what makes the ordering stop mattering.
        if (auto& rk = ranges[(size_t) k])
            rk->reclampSpan();
    }

    // The two fixed harmony voices (2026-08-19, BigSky's shimmer list). The combo picks the
    // interval, the knob beside it says how often that voice fires; both are ordinary
    // attachments to this line's own appended parameters, bound for good like everything else
    // on a card. The knob matches the strip above it - same rotary, same read-only readout -
    // so the row reads as more of the card rather than a different instrument.
    for (int s = 0; s < 2; ++s)
    {
        const auto v = juce::String(s + 1);
        auto& box = harmBoxes[(size_t) s];
        box.addItemList(KeysProcessor::harmonyChoices(), 1);
        box.setTitle("Macro harmony " + v + " " + letter);
        box.setTooltip("A fixed interval this line adds " + juce::String(s == 0 ? "as its first"
                       : "as its second") + " harmony voice, above or below every note it "
                       "plays. Off is silence; the list is chromatic on purpose, so a Major "
                       "3rd is a Major 3rd whatever the scale says.");
        addAndMakeVisible(box);
        harmAtts[(size_t) s] = std::make_unique<ComboAtt>(
            processor.apvts, id(s == 0 ? KeysProcessor::apHarm1 : KeysProcessor::apHarm2), box);

        auto& knob = harmChanceKnobs[(size_t) s];
        knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 52, 15);
        knob.setTitle("Macro harmony " + v + " chance " + letter);
        knob.setTooltip("How often harmony voice " + v + " actually fires, per step. 100 is "
                        "every note, lower thins it into something that glints rather than "
                        "doubles.");
        addAndMakeVisible(knob);
        harmChanceAtts[(size_t) s] = std::make_unique<SliderAtt>(
            processor.apvts,
            id(s == 0 ? KeysProcessor::apHarm1Chance : KeysProcessor::apHarm2Chance), knob);

        heading(harmLabels[(size_t) (s * 2)], s == 0 ? "HARMONY 1" : "HARMONY 2");
        heading(harmLabels[(size_t) (s * 2 + 1)], "CHANCE");
    }

    chordLabel.setJustificationType(juce::Justification::centred);
    chordLabel.setFont(skin::uiSemi(13.0f));
    addAndMakeVisible(chordLabel);

    refreshRateMode();
    refresh();
}

// One dial, two parameters, two units - the band's `refreshRateMode` for one row. The
// attachment that is alive decides the dial's range, its detents and its readout, so the swap
// has to happen on every mode change; it cannot happen under an open drag, because the
// attachment that started the gesture has to be the one that ends it.
void MacroRow::refreshRateMode()
{
    const bool free = processor.apvts.getRawParameterValue(
                          KeysProcessor::arpParamId(line, KeysProcessor::apRateFree))->load() > 0.5f;
    rateModeButton.setButtonText(free ? "Hz" : "Sync");

    // Dot, Tuplet and Anchor all subdivide or align against a *beat*, and in Hz there is no beat:
    // the engine ignores all three there, so they grey out rather than sitting lit and doing
    // nothing. Same rule and the same words as the band's - see ArpPanel::refreshRateMode.
    // Outside the early-out below, which only guards the attachment swap: these have to be
    // right on the first call too, when lastRateFree is still -1 and a Hz session has just
    // been restored.
    dotButton.setEnabled(! free);
    tupletBox.setEnabled(! free);
    anchorButton.setEnabled(! free);
    anchorButton.setTooltip(free ? "Nothing to anchor to in Hz: a free-running rate follows no "
                                   "bar grid. Switch the rate to Sync to lock the steps to one."
                                 : "Anchored: locked to the host bar grid. Free: never jumps, "
                                   "may drift.");

    // The swap itself, and the drag guard around it, are arprate::applyMode - one copy for
    // this and for the band, which used to carry the same rule in two places.
    if (arprate::applyMode(processor.apvts, line, rateKnob, { rateSyncAtt, rateHzAtt },
                           lastRateFree, rateDragging, free))
        installRateText(); // the new attachment has just overwritten the readout's text function
}

// A knob column is either a plain rotary or a RangeKnob wrapping one. These two are the only
// places that care which, so nothing else in the row has to branch.
juce::Component& MacroRow::knobCell(int k)
{
    if (auto& r = ranges[(size_t) juce::jlimit(0, numKnobs - 1, k)])
        return *r;
    return knobs[(size_t) juce::jlimit(0, numKnobs - 1, k)];
}

juce::Slider& MacroRow::knobFace(int k)
{
    if (auto& r = ranges[(size_t) juce::jlimit(0, numKnobs - 1, k)])
        return r->face();
    return knobs[(size_t) juce::jlimit(0, numKnobs - 1, k)];
}

// The band's two, for one row. Same rules, same words - see ArpPanel::refreshTuplet and
// ArpPanel::installRateText, which carry the reasoning.
void MacroRow::refreshTuplet()
{
    auto& apvts = processor.apvts;
    const int n = KeysProcessor::tupletFor((int) apvts.getRawParameterValue(
        KeysProcessor::arpParamId(line, KeysProcessor::apTuplet))->load());
    const int dotted = apvts.getRawParameterValue(
                           KeysProcessor::arpParamId(line, KeysProcessor::apDot))->load() > 0.5f;
    if (n == lastTuplet && dotted == lastDotted)
        return;
    lastTuplet = n;
    lastDotted = dotted;
    rateKnob.updateText();
}

void MacroRow::installRateText()
{
    if (rateSyncAtt == nullptr)
        return;
    rateKnob.textFromValueFunction = [this](double v)
    {
        auto& apvts = processor.apvts;
        const int n = KeysProcessor::tupletFor((int) apvts.getRawParameterValue(
            KeysProcessor::arpParamId(line, KeysProcessor::apTuplet))->load());
        const bool dot = apvts.getRawParameterValue(
                             KeysProcessor::arpParamId(line, KeysProcessor::apDot))->load() > 0.5f;
        return ArpEngine::rateSyncText((int) std::lround(v), dot, n);
    };
    rateKnob.updateText();
}

// Shape spans two parameters here exactly as it does in the band above (arpDirection plus
// arpPattern), so it cannot be an attachment and the gestures are bracketed by hand.
void MacroRow::applyShape()
{
    const int chosen = shapeBox.getSelectedItemIndex();
    auto& apvts = processor.apvts;
    if (auto* pat = dynamic_cast<juce::AudioParameterBool*>(
            apvts.getParameter(KeysProcessor::arpParamId(line, KeysProcessor::apPattern))))
    {
        pat->beginChangeGesture();
        *pat = chosen >= ArpEngine::numDirections;
        pat->endChangeGesture();
    }
    if (chosen >= 0 && chosen < ArpEngine::numDirections)
        if (auto* dir = dynamic_cast<juce::AudioParameterChoice*>(
                apvts.getParameter(KeysProcessor::arpParamId(line, KeysProcessor::apDirection))))
        {
            dir->beginChangeGesture();
            *dir = chosen;
            dir->endChangeGesture();
        }
    refreshDice();
}

// Only Random Once has a stored order for the dice to deal again (2026-08-21). Random and
// Random Other draw a fresh note every step, and every other shape is a walk with no randomness
// in it at all - so on those the button greys rather than vanishing, which keeps the row from
// reflowing under the mouse and keeps the dice discoverable when you do land on the shape.
//
// One method rather than the test written twice: applyShape calls it the instant the combo
// moves (the 10 Hz refresh would otherwise leave the button a tick behind the shape it belongs
// to) and refresh calls it for every other route a shape can change - a host lane, a session
// load, an MCP client, the steppers.
void MacroRow::syncShapeBox()
{
    auto& apvts = processor.apvts;
    const bool pattern = apvts.getRawParameterValue(
                             KeysProcessor::arpParamId(line, KeysProcessor::apPattern))->load() > 0.5f;
    const int dir = (int) apvts.getRawParameterValue(
                        KeysProcessor::arpParamId(line, KeysProcessor::apDirection))->load();
    // Pattern is the entry past the directions, which is why it is added separately above.
    const int wanted = pattern ? ArpEngine::numDirections
                               : juce::jlimit(0, ArpEngine::numDirections - 1, dir);
    if (shapeBox.getSelectedItemIndex() != wanted)
        shapeBox.setSelectedItemIndex(wanted, juce::dontSendNotification); // never re-enters applyShape
}

void MacroRow::refreshDice()
{
    const bool canDeal = shapeBox.getSelectedItemIndex() == (int) ArpEngine::Direction::randomOnce;
    // No guard needed: Component::setEnabled opens with `if (flags.isEnabledFlag != shouldBe)`
    // and returns without repainting otherwise, so a 10 Hz call on an unchanged value already
    // costs nothing. A wrapper here would only teach the next timer-driven control to add one.
    diceButton.setEnabled(canDeal);
}

void MacroRow::stepShape(int delta)
{
    const int n = shapeBox.getNumItems();
    if (n <= 0)
        return;
    // Stops at the ends rather than wrapping, like the band's own pair: a stepper that wraps
    // turns "one past the last" into "the first", which is never what the click meant.
    const int next = juce::jlimit(0, n - 1, shapeBox.getSelectedItemIndex() + delta);
    if (next != shapeBox.getSelectedItemIndex())
        shapeBox.setSelectedItemIndex(next); // fires onChange -> applyShape
}

void MacroRow::stepRate(int delta)
{
    auto& apvts = processor.apvts;
    const auto rateId = KeysProcessor::arpParamId(line, KeysProcessor::apRate);
    const auto hzId = KeysProcessor::arpParamId(line, KeysProcessor::apRateHz);

    if (apvts.getRawParameterValue(KeysProcessor::arpParamId(line, KeysProcessor::apRateFree))->load() > 0.5f)
    {
        // A click is a quarter of an octave, so four halve or double the rate - the same jump
        // one entry of the Sync list makes. Identical rule to ArpPanel::stepRate; see there.
        if (auto* hz = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(hzId)))
        {
            const double cur = juce::jlimit((double) ArpEngine::minRateHz, (double) ArpEngine::maxRateHz,
                                            (double) hz->get());
            const double rungs = std::log2(cur) * 4.0;
            const double wanted = std::pow(2.0, (std::floor(rungs + 1.0e-6) + delta) / 4.0);
            hz->beginChangeGesture();
            *hz = (float) juce::jlimit((double) ArpEngine::minRateHz, (double) ArpEngine::maxRateHz, wanted);
            hz->endChangeGesture();
        }
        return;
    }

    if (auto* rate = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(rateId)))
    {
        const int next = juce::jlimit(0, rate->choices.size() - 1, rate->getIndex() + delta);
        if (next != rate->getIndex())
        {
            rate->beginChangeGesture();
            *rate = next;
            rate->endChangeGesture();
        }
    }
}

void MacroRow::refresh()
{
    refreshRateMode(); // a host can automate the unit out from under us
    refreshTuplet();   // ... and the tuplet, which has no attachment to hear it change

    auto& apvts = processor.apvts;
    // ... and the two ring values, for the same reason: the ring writes its parameter but
    // nothing reads it back, so a session load, a host lane or an MCP client would otherwise
    // move the value and leave the arc where it was. setSpan no-ops when nothing changed, so
    // this costs a comparison per tick and never fights an open drag. H.TIME's ring reads its
    // own span parameter, same as ever; VEL's reads arpHumanVel directly, since that is what
    // its ring now *is* rather than a span of VEL's own value - and the same read is what the
    // lamp's isOn() already uses, so the two can never disagree about whether Humanize
    // Velocity is on.
    for (int k = 0; k < numKnobs; ++k)
        if (auto& rk = ranges[(size_t) k])
            rk->setSpan(apvts.getRawParameterValue(
                                 KeysProcessor::arpParamId(line, k == kHTime
                                                                     ? KeysProcessor::apHumanizeSpan
                                                                     : KeysProcessor::apHumanVel))
                            ->load());
    syncShapeBox();
    refreshDice();

    // What this line is holding, and whether something is on its way. A launch waiting on a
    // quantize boundary says so, because otherwise the click looks like it did nothing.
    const auto& held = processor.arpHeldName(line);
    if (processor.arpLaunchPending(line))
        chordLabel.setText("...", juce::dontSendNotification);
    else
        chordLabel.setText(held.isNotEmpty() ? held : juce::String("--"), juce::dontSendNotification);
    chordLabel.setColour(juce::Label::textColourId,
                         held.isNotEmpty() ? skin::text : skin::textFaint);

    // The scrim in paintOverChildren reads processor.arpLineOn() live, so this cache exists
    // only to decide when to ask for a repaint - driven by the panel's 10 Hz timer while the
    // macro view is up, which is the same tick that already moves the chord readout above.
    const bool lineOn = processor.arpLineOn(line);
    if (lineOn != lastLineOn)
    {
        lastLineOn = lineOn;
        repaint();
    }
}

void MacroRow::setDropTarget(bool b)
{
    if (dropTarget == b)
        return;
    dropTarget = b;
    repaint();
}

// Same source rule as a slot card, and one thing more that used to need saying by hand: a row
// that is not on screen is never hit, so the `macroView` test the old screen hit-test carried
// is now the row's own visibility.
bool MacroRow::isInterestedInDragSource(const SourceDetails& details)
{
    return chorddrag::chordBeingDragged(details) != nullptr;
}

void MacroRow::itemDragEnter(const SourceDetails&) { setDropTarget(true); }
void MacroRow::itemDragExit(const SourceDetails&) { setDropTarget(false); }

void MacroRow::itemDropped(const SourceDetails& details)
{
    setDropTarget(false);
    if (auto* p = isInterestedInDragSource(details) ? chorddrag::of(details) : nullptr)
    {
        owner.takeChordOnLine(line, *p);
        p->taken = true;
    }
}

void MacroRow::paint(juce::Graphics& g)
{
    // A chord card is over this card: the whole thing lights, because the card *is* the line
    // here and a target you can only hit by aiming at a 40 px tab is a target a single mouse
    // fights.
    if (dropTarget)
    {
        const auto accent = skin::accentOf(*this).base;
        const auto b = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(accent.withAlpha(0.10f));
        g.fillRoundedRectangle(b, skin::radius);
        g.setColour(accent);
        g.drawRoundedRectangle(b, skin::radius, 2.0f);
        return; // the frame would only fight the outline
    }

    // Each card is its own captioned, ruled box - the band's group-frame look, with the
    // line's name punched through the top rule and a fill behind it (2026-08-02, Owen: "we
    // need a bit more clear delineation between the two arpeggiators. They kinda look like
    // one right now"). The fill and a rule brighter than the groups' 0.07 are what make two
    // side-by-side cards read as two machines rather than one field of knobs.
    const auto r = getLocalBounds().toFloat().reduced(1.0f);
    const float capY = r.getY() + 5.0f;
    const auto caption = "LINE " + juce::String::charToString((juce::juce_wchar) ('A' + line));
    const auto font = skin::micro(9.5f).withExtraKerningFactor(0.16f);
    const auto textW = juce::GlyphArrangement::getStringWidth(font, caption);

    // The line's own colour (2026-08-19, Owen: "each one should have a color"), worn by the
    // fill, the frame and the caption - the marks that say *which line* - and by nothing on
    // the card that is a control, so the skin's one-accent law bends here rather than breaks.
    const auto tint = skin::lineAccent(line).base;
    g.setColour(tint.withAlpha(0.045f));
    g.fillRoundedRectangle(r.withTrimmedTop(capY - r.getY()), skin::radius);

    g.setColour(tint.withAlpha(0.38f));
    juce::Path frame;
    frame.startNewSubPath(r.getCentreX() - textW * 0.5f - 8.0f, capY);
    frame.lineTo(r.getX() + skin::radius, capY);
    frame.quadraticTo(r.getX(), capY, r.getX(), capY + skin::radius);
    frame.lineTo(r.getX(), r.getBottom() - skin::radius);
    frame.quadraticTo(r.getX(), r.getBottom(), r.getX() + skin::radius, r.getBottom());
    frame.lineTo(r.getRight() - skin::radius, r.getBottom());
    frame.quadraticTo(r.getRight(), r.getBottom(), r.getRight(), r.getBottom() - skin::radius);
    frame.lineTo(r.getRight(), capY + skin::radius);
    frame.quadraticTo(r.getRight(), capY, r.getRight() - skin::radius, capY);
    frame.lineTo(r.getCentreX() + textW * 0.5f + 8.0f, capY);
    g.strokePath(frame, juce::PathStrokeType(1.0f));

    g.setFont(font);
    g.setColour(tint);
    g.drawText(caption, getLocalBounds().withHeight(12).withY((int) capY - 6),
               juce::Justification::centred);
}

// A line that is off still takes chords in (CLAUDE.md), so this greys the card without
// touching setEnabled anywhere on it: every knob, the rate dial and the card itself as a drop
// target all have to keep receiving mouse events. Painted over the children rather than under
// them, and skipped while dropTarget is true so the drop highlight in paint() above is never
// muddied by it.
void MacroRow::paintOverChildren(juce::Graphics& g)
{
    if (dropTarget || processor.arpLineOn(line))
        return;

    // Same inset and the same rounded body the fill in paint() uses, trimmed below the LINE A
    // / LINE B caption strip so the name stays legible while everything under it dims.
    const auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(skin::bgBot.withAlpha(0.38f));
    g.fillRoundedRectangle(r.withTrimmedTop((float) arpMacroCap), skin::radius);
}

void MacroRow::resized()
{
    // Three lines inside one card (2026-08-02, Owen: "having the arpeggiators parallel to
    // each other instead of one on top of the other"): what the line plays (On, rate,
    // shape), the nine knobs under their own heading strip (seven when VEL absorbed H.VEL
    // as its own ring, 2026-08-17), and the rate's modifiers with the held chord. Stacked
    // *inside* the card because two cards now share the panel's width, and the old single
    // line needed more width than half the panel has. The heights here are
    // arpMacroCap / arpMacroLine / arpMacroHeads / arpMacroMods; arpMacroCard (ArpPanel.cpp)
    // is their sum and has to agree with this function exactly - the knob *count* changed with the merge,
    // but every one of those heights is independent of it, so arpMacroCard did not move.
    auto full = getLocalBounds().reduced(arpMacroRowInset, 0);
    full.removeFromTop(arpMacroCap); // the LINE A / LINE B caption rule, drawn by paint()
    // The single-height controls sit centred against the knobs beside them.
    const auto centred = [](juce::Rectangle<int> c) { return c.withSizeKeepingCentre(c.getWidth(), 26); };

    const auto heads1 = full.removeFromTop(arpMacroHeads);
    auto line1 = full.removeFromTop(arpMacroLine);
    {
        auto r = line1;
        const auto take = [&r](int w) { auto c = r.removeFromLeft(w); r.removeFromLeft(6); return c; };
        // onButton's 40 px (plus its 6 px gap) is gone from this line since 2026-08-02, and
        // deliberately not replaced with a spacer: everything below simply shifts left, and
        // that freed width lands on shapeBox, the one elastic control on this line and the
        // tightest thing in the card before this.
        ratePrev.setBounds(centred(take(26)));
        rateKnob.setBounds(take(58));
        rateNext.setBounds(centred(take(26)));
        rateModeButton.setBounds(centred(take(42)));
        // Shape's steppers are reserved before its combo takes the rest: they are targets
        // and it is a readout, the same reserve-the-fixed-thing-first ordering the old
        // single-line layout paid for twice (see the 2026-08-02 entries in CLAUDE.md).
        //
        // **The combo is capped now** (2026-08-21, Owen: "the shape of the arpeggiator drop
        // down doesn't need to be so big. Make it smaller"). It was the one elastic control on
        // this line, so on any window wider than the floor it swallowed everything the row had
        // left - about 540 px on Owen's screen to hold "Fingered Bottom", the longest of the
        // fifteen names. `arpMacroShapeMaxW` is that name with room around it; past that the
        // row simply ends, and the slack sits at the right where the dice is rather than
        // stretching a readout that has nothing more to say.
        //
        // The dice's own cell comes out of the row first and on every shape, greyed or not:
        // reserve the fixed thing before the elastic one takes the rest, and never let a
        // control appear or vanish under the mouse. It is *placed* after the shape group
        // rather than at the far end of the row - Owen asked for it "nearby", and pinned right
        // it sat a clear 250 px from the thing it acts on, reading as unrelated to it. The 14
        // px gap is wider than the 6 px between the shape's own parts, which is what keeps it
        // from being mistaken for a third stepper. Whatever the row has left over then falls
        // at the end, where empty space costs nothing.
        // The Keybed chip (2026-09-01) takes the slack after the dice - reserved out of the row
        // before Shape takes its cut, the same rule as the dice's own cell, so Shape shrinks
        // toward its cap rather than the chip being starved. At the docked floor the row has
        // over a hundred pixels past the dice; LayoutTests measures the chip at both floors.
        const int diceW = 34;
        const int keybedW = 88;
        shapePrev.setBounds(centred(r.removeFromLeft(26)));
        r.removeFromLeft(6);
        const int shapeW = juce::jmin(arpMacroShapeMaxW,
                                      juce::jmax(0, r.getWidth() - 26 - 6 - 14 - diceW - 14 - keybedW));
        shapeBox.setBounds(centred(r.removeFromLeft(shapeW)));
        r.removeFromLeft(6);
        shapeNext.setBounds(centred(r.removeFromLeft(26)));
        r.removeFromLeft(14);
        diceButton.setBounds(centred(r.removeFromLeft(diceW)));
        r.removeFromLeft(14);
        keybedButton.setBounds(centred(r.removeFromLeft(keybedW)));
    }
    // The RATE / SHAPE names span their whole group, steppers included, so the arrows can
    // only be read as belonging to the word above them. Placed from the controls, as ever.
    rateHeadLabel.setBounds(ratePrev.getX(), heads1.getY(),
                            rateModeButton.getRight() - ratePrev.getX(), heads1.getHeight());
    shapeHeadLabel.setBounds(shapePrev.getX(), heads1.getY(),
                             shapeNext.getRight() - shapePrev.getX(), heads1.getHeight());

    const auto headStrip = full.removeFromTop(arpMacroHeads);
    auto knobLine = full.removeFromTop(arpMacroKnobLine);
    // `arpMacroKnobMinW` keeps the card solvable at the *narrowest place this view is ever
    // drawn*, and knobs stop growing at 96 as before. **Nine since 2026-08-21**, when STRAY
    // joined the row: 9*38 + the two rings + eight gaps is 422 px, which is what
    // `minMacroWidth()` is derived from, so the floor tracks the count instead of being a
    // number somebody has to remember to re-measure.
    //
    // That is the lesson of the ninth knob rather than a note about it. The docked editor was
    // re-measured and fitted fine (a column there is ~614 px); the **detached** Arp window was
    // not, and its own 900 px minimum left a column of 420 - two pixels of clamp away from
    // starving H.TIME's range knob to a 16 px face. A view that can be drawn in two windows has
    // two floors, and the smaller one is the one that binds. A tenth knob must buy the width -
    // raise the floors, never starve the row.
    //
    // The two range knobs are `each` wide *plus their ring on both sides*, reserved out of the
    // row here rather than taken off a neighbour later: the face inside a range knob is then
    // exactly as wide as every plain one, so the row reads as nine knobs of one size with a
    // ring round two of them, which is what it is.
    const int rings = 2 * 2 * arpRingPx; // two range knobs, a ring either side of each
    const int each = juce::jlimit(arpMacroKnobMinW, 96,
                                  (knobLine.getWidth() - rings - 6 * (numKnobs - 1)) / numKnobs);
    auto knobStrip = knobLine.removeFromLeft(each * numKnobs + rings + 6 * (numKnobs - 1));
    for (int k = 0; k < numKnobs; ++k)
    {
        const bool ranged = isRangeKnob(k);
        auto cell = knobStrip.removeFromLeft(each + (ranged ? 2 * arpRingPx : 0));
        // A plain knob drops its top by the full ring so its *readout* lines up with a range
        // knob's, which is the alignment the eye actually checks along a row of numbers. Its
        // face then sits a few pixels lower inside its cell than a ringed one does, and the
        // ring fills exactly that space, so the two still read as the same size.
        knobCell(k).setBounds(ranged ? cell : cell.withTrimmedTop(2 * arpRingPx));
        knobStrip.removeFromLeft(6);
    }
    // Headings are placed from the knob they name, not by walking a second copy of the
    // layout: one source of truth for where a column is, so they cannot drift apart.
    const auto headFor = [&headStrip](juce::Label& l, const juce::Component& c)
    { l.setBounds(c.getX(), headStrip.getY(), c.getWidth(), headStrip.getHeight()); };
    for (int k = 0; k < numKnobs; ++k)
        headFor(knobLabels[(size_t) k], knobCell(k));

    // The harmony area, two columns (2026-08-19, second pass - Owen: "make harmony 2
    // columns"): one column per voice, its dropdown stacked over its chance knob, the way the
    // BigSky panel pairs a shift with its amount. The first cut ran all four controls in one
    // row, which read as a strip of parts rather than as two voices; a column is the pairing
    // drawn as geometry. Headings are placed from the controls, as ever.
    const auto harmHeads = full.removeFromTop(arpMacroHeads);
    auto harmCombos = full.removeFromTop(arpMacroHarmCombo);
    const auto chanceHeads = full.removeFromTop(arpMacroHeads);
    auto harmKnobs = full.removeFromTop(arpMacroLine);
    {
        const int gap = 12;
        const int half = (harmCombos.getWidth() - gap) / 2;
        for (int s = 0; s < 2; ++s)
        {
            auto comboCell = s == 0 ? harmCombos.removeFromLeft(half) : harmCombos;
            auto knobCol = s == 0 ? harmKnobs.removeFromLeft(half) : harmKnobs;
            if (s == 0)
            {
                harmCombos.removeFromLeft(gap);
                harmKnobs.removeFromLeft(gap);
            }
            auto& box = harmBoxes[(size_t) s];
            box.setBounds(comboCell.withSizeKeepingCentre(comboCell.getWidth(), 34));
            auto& knob = harmChanceKnobs[(size_t) s];
            knob.setBounds(knobCol.withSizeKeepingCentre(52, knobCol.getHeight()));
            harmLabels[(size_t) (s * 2)].setBounds(box.getX(), harmHeads.getY(),
                                                   box.getWidth(), harmHeads.getHeight());
            harmLabels[(size_t) (s * 2 + 1)].setBounds(box.getX(), chanceHeads.getY(),
                                                       box.getWidth(), chanceHeads.getHeight());
        }
    }

    // The rate's modifiers keep their full 34 px hit height - they are targets, and wide
    // enough for the word plus its tick, since a bare tick box beside "Dot" would be two
    // controls' worth of ambiguity in a card that already has nine unlabelled knobs. The
    // held chord sits at the card's bottom-right corner, where a dropped card lands.
    full.removeFromTop(2);
    auto subRow = full.removeFromTop(arpMacroMods);
    chordLabel.setBounds(centred(subRow.removeFromRight(arpMacroChordW)));
    subRow.removeFromRight(arpMacroChordGap);
    const auto takeMod = [&subRow](int w)
    { auto c = subRow.removeFromLeft(w); subRow.removeFromLeft(arpMacroModGap); return c; };
    // Every cell is a named constant and their sum is arpMacroModsW, which minMacroWidth()
    // takes against the knob strip: the row is never asked to fit in less than it needs, so
    // takeMod's clamp on the last cell - which is what a starved row looks like, Details
    // drawn as a sliver with nothing to say why - cannot bite at either window's floor.
    // LayoutTests measures the five chips and the readout at both. Full 34 px height, unlike
    // the band's 28 - the strip is 34 already.
    dotButton.setBounds(takeMod(arpMacroModDot));
    tupletBox.setBounds(takeMod(arpMacroModTuplet));
    anchorButton.setBounds(takeMod(arpMacroModAnchor));
    legatoButton.setBounds(takeMod(arpMacroModLegato));
    followsBox.setBounds(takeMod(arpMacroModFollows));
    detailsButton.setBounds(takeMod(arpMacroModDetails));
}
} // namespace keys
