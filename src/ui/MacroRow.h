#pragma once

// One arpeggiator line, as one card, and the numbers the All view lays that card out with.
//
// It lived inline in ArpPanel.cpp until 2026-09-02, which by then was 4,687 lines carrying the
// whole arp UI in one translation unit. MacroRow is the largest self-contained piece of it: it
// binds its own attachments to its own line for the row's whole life, reads the panel for two
// public answers only (which line to edit, and where a dropped chord goes) and touches nothing
// else on it. It is still `ArpPanel::MacroRow` to everyone who names it - ArpPanel re-exports
// it as a member alias, because that is the name LayoutTests measures the card by.
//
// The card's layout constants live here with it. They were file-local to ArpPanel.cpp and are
// read from both sides - the card lays itself out with them, and the panel's own height and
// width floors are sums over them - so this header is the one copy and ArpPanel.cpp gets them
// by including it. Never restate one at a call site: the floor and the layout have to be one
// number, which is exactly what went wrong when STRAY made the knob strip nine and only the
// docked case was re-measured (2026-08-21).

#include "../PluginProcessor.h"
#include "ArpRateMode.h"
#include "RangeKnob.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <okstudio/RotaryKnob.h>
#include <array>
#include <memory>

namespace keys
{
class ArpPanel;

// One control line inside a macro card: 40 px of knob plus its 15 px readout, the same
// spend the old single-line row made. ArpPanel.cpp builds arpMacroCard out of this and the
// three strips below it, which is why they are in a header both files see rather than in
// either one of them.
constexpr int arpMacroLine = 55;
// ...and the card's other strips: the LINE A / LINE B caption rule at the top (the same
// punched-through-the-frame caption the band's groups draw - each card wears its own name
// since 2026-08-02, Owen: "we need a bit more clear delineation between the two
// arpeggiators"), 11 px heading strips for the RATE / SHAPE and knob columns, and the
// rate's Dot / Tuplet / Anchor at the full 34 px hit height.
constexpr int arpMacroCap = 18;
constexpr int arpMacroMods = 34;
constexpr int arpMacroHeads = 11;
// The widest the Shape combo is allowed to get (2026-08-21). "Fingered Bottom" is the
// longest of the fifteen entries and measures about 105 px in the popup font, so this is
// that plus its chevron, its own insets and room to spare - and it is a *cap*, so above
// the floor the row simply stops giving the combo more.
//
// It is **not** true that a narrow window shrinks it exactly as it used to, and the
// comment said so for an afternoon: the dice's 34 px cell and its 14 px gap come out of
// the same row, so at any width where the cap is not biting, Shape is 48 px narrower than
// before. That is affordable only because `minMacroWidth()` guarantees the row enough
// width to seat the knob strip, which is wider than this line needs.
//
// **The "~166 px at the floor" figure that stood here was carried over from before the
// dice and never re-derived** - it is nearer 151 at `minPanelWidth()`, and less again at
// `minMacroWidth()`. It still clears the longest name, so nothing was broken, but a number
// asserted in a comment is a number that goes stale silently. `LayoutTests` measures the
// combo against its own longest entry at both floors now, so appending a shape name or
// adding a control to this row fails a test instead of quietly ellipsising a label.
// Narrow the floor and this is still the second thing that breaks, after the knobs.
constexpr int arpMacroShapeMaxW = 170;
// The ring a RangeKnob draws around its face, and therefore what the knob row is taller
// than the row above it by (2026-08-03). The row grows rather than the faces shrinking:
// squeezing a ring out of the space the knob already had would have taken those two under
// the kit's 48 px advice and every other knob with them, which is the trap this file has
// logged twice already. Height is the cheap axis in this view.
constexpr int arpRingPx = 8;
constexpr int arpMacroKnobLine = arpMacroLine + 2 * arpRingPx;
// The narrowest a macro knob may be drawn, and therefore what sets the whole view's
// minimum width. It is over the 34 px mouse-only floor with a little to spare, which is
// the point: a knob at exactly 34 has no room for the ring inset a range knob adds.
// `ArpPanel::minMacroWidth()` below turns this into the panel width the view needs, so
// the floor and the layout are one number rather than two that can drift - which is
// exactly what happened when STRAY made the strip nine knobs and only the *docked* case
// was re-measured (2026-08-21).
constexpr int arpMacroKnobMinW = 38;
// The gap between the two cards in a row, and the insets the panel puts around them.
// Named because minMacroWidth() and the layout both need them and must agree - and
// **substituted at every layout site**, not merely written down beside one. Naming a
// literal that the layout still spells out by hand buys nothing: the derivation then
// agrees with a comment rather than with the code, which is the `buildLaneRow` versus
// `laneRange` trap this file already pays for once. Change one of these and the floor,
// the card and the row all move together.
constexpr int arpMacroCardGap = 12;
constexpr int arpMacroAreaInset = 12; // the panel's inset inside cardBounds()
constexpr int arpMacroCardInset = 8;  // the card's own inset inside the panel
constexpr int arpMacroRowInset = 10;  // MacroRow's side inset inside the card

// The macro card's bottom strip: five fixed cells left to right, a gap after each, and the
// chord readout at the right end. Named rather than written into the setBounds calls so
// minMacroWidth() can ask what the *row* needs and not only what the knob strip needs
// (2026-09-01, when Legato made five). Before that the row's width was asserted in a
// comment that was two floors out of date - measured against the docked window's card and
// never the detached Arp window's, where the card is exactly the knob strip wide and
// Details had been ten pixels short since the dice arrived, with nothing on screen to say
// so. A fifth chip there would have laid Details out at zero.
constexpr int arpMacroModDot = 62;
// 92 where the tick box had 66: a combo showing "5-tuplet" needs the word plus a chevron.
constexpr int arpMacroModTuplet = 92;
constexpr int arpMacroModAnchor = 84;
constexpr int arpMacroModLegato = 84; // "Legato" is "Anchor"'s length: the same cell
constexpr int arpMacroModFollows = 82; // "From C" plus a chevron, measured in LayoutTests
constexpr int arpMacroModDetails = 76;
constexpr int arpMacroModGap = 8;
constexpr int arpMacroChordW = 64;    // the held-chord readout; ellipsises gracefully
constexpr int arpMacroChordGap = 6;
constexpr int arpMacroModsW = arpMacroModDot + arpMacroModTuplet + arpMacroModAnchor
                            + arpMacroModLegato + arpMacroModFollows + arpMacroModDetails
                            + 6 * arpMacroModGap
                            + arpMacroChordGap + arpMacroChordW;
// The harmony area's dropdown row (2026-08-19, second pass): a 34 px combo centred in it.
// Its chance knob sits below it at arpMacroLine, one column per voice.
//
// 34, not the 26 it shipped at for a few hours. CLAUDE.md's floor is an invariant and names
// no exceptions ("a check box, a stepper's -/+ pair and a caption-row button are targets
// exactly as a TextButton is"), and this is a brand-new target on a card whose height budget
// was being rewritten in the same stroke, so the eight pixels were there for the asking. It
// is also the only route to the two-column interval popup, so a missed click here is a
// missed feature rather than a cosmetic annoyance.
constexpr int arpMacroHarmCombo = 38;

// One arpeggiator line, as one card: the settings that decide how it sits against the
// other line, what it is holding, and a way to start it. These cards are the **macro
// view**, which is what the fourth tab selects (2026-08-01, Owen: "a fourth option for a
// simplified version that shows a little bit of all of them ... the goal is to be able to
// create complex polyrhythms from one view"). Side by side since 2026-08-02 (Owen:
// "parallel to each other instead of one on top of the other"), which is why a card is
// three stacked lines: half the panel's width cannot hold the single row this used to be.
//
// Each row's attachments are bound to its own line for the row's whole life, unlike the
// band above, which rebinds every time the tabs move. That is the point of the row: three
// lines on screen at once cannot each be "the current line".
class MacroRow : public juce::Component,
                 public juce::DragAndDropTarget
{
public:
    MacroRow(ArpPanel&, KeysProcessor&, int line);

    void paint(juce::Graphics&) override;
    // Scrims the card body (not the LINE A / LINE B caption strip) when this line is off.
    // Drawn over the children rather than gating setEnabled(false) on them: every control
    // has to stay live and clickable even while the line is off, both to dial in a rate
    // before switching it on and because a chord dropped onto an off line is load-bearing
    // (CLAUDE.md: "A line that is off still takes chords in") - a disabled component takes
    // no mouse events, which would kill the drop target along with everything else.
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    // Readouts that no attachment drives: the rate text (it spans two parameters and two
    // units), the shape, and the chord this line is holding. Called by the panel's timer.
    void refresh();

    // A chord card dragged out of the pad strip. The row is the line here, laid out large,
    // so it is a far easier target than the tab that names it - and because JUCE walks up
    // from whatever is under the point, the knobs sitting on the row are part of it.
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

    // The knobs a row carries, left to right. One table so the labels, the
    // parameters and the layout cannot drift apart; the headings are drawn once, on the
    // top row, and every row reserves the same strip so the columns line up.
    // OCT is the *transpose* (centred at zero, down as readily as up) rather than the
    // upward-only stacking range, which stays on the per-line tab beside Distance, the
    // rest of that same feature. Seven since the merge below: VEL is the bipolar velocity
    // trim that replaced VOL (centred, up boosts, down cuts) and now carries Humanize
    // Velocity as its own outer ring (2026-08-17, Owen: "I only want one velocity knob,
    // and I want this humanize section to be the outer ring") - H.VEL is gone as a
    // separate knob, so there is one loudness control per line rather than two two cells
    // apart. H.TIME stays its own RangeKnob for the timing nudge; Ramp and Time still
    // live on the per-line tab.
    // Eight again as of 2026-08-18: CHANCE became MUTATE and LOCK joined it. The strip was
    // eight until H.VEL folded into VEL's ring on 2026-08-17, so this is a width the row
    // has already carried - and the reserve-before-Shape rule below is what made that safe.
    // **Nine from 2026-08-21**, STRAY between MUTATE and LOCK: the three read left to
    // right as one sentence - how hard the run explores, how far outside the chord it may
    // go, how long it keeps what it finds - which is why STRAY is inserted rather than
    // appended to the end of the row. This enum is UI indexing and nothing stores it, so
    // inserting here is free; the *parameter* was appended, which is the order that is
    // not free (see KeysProcessor::apStray).
    // **Ten from 2026-09-01**, DENSITY between GATE and MUTATE (Owen: "a density knob
    // where it controls, like, how much notes there are"). It is `arpChance` - the per-line
    // slider the Play page has carried since 2026-08-18 - given a face on the card and the
    // name it reads as here: how long each note is, then how many of them there are, then
    // how the run explores. No new parameter; the tenth cell is the whole cost, and
    // minMacroWidth() moved on its own the moment numKnobs did.
    // **Eleven from 2026-09-01, later the same day**: DUCK beside DENSITY, the first knob of
    // the line bus (docs/LINE_INTERACTION.md). Designed for the Play page's FEEL group and
    // moved here before it was built, because the band is exactly full at its floor - both
    // PLAYBACK rows and all five FEEL sliders spend every pixel at arpDeepPageMinW - where
    // the card's floor is its bottom strip's 598 px and the knob strip needs 486 of that
    // for eleven, so the eleventh cell was free. Read as a sentence with its neighbour: how
    // many of my steps play, how many I give up to the line I follow.
    enum Knob { kOctShift = 0, kGate, kDensity, kDuck, kMutate, kStray, kLock, kSwing, kOffset,
                kVel, kHTime, numKnobs };

private:
    void applyShape();
    void stepShape(int delta);
    void stepRate(int delta);
    // The rate readout under the dial, which says what the engine is actually playing
    // rather than the bare division. The combo drives itself through its attachment; this
    // exists because the readout depends on three parameters and is bound to one. Cached,
    // since it runs off the 10 Hz timer.
    void refreshTuplet();
    void refreshDice();
    // Point the Shape combo at what this line's parameters actually say. One reader,
    // called from the constructor as well as from refresh(), because everything that
    // keys off the combo's selection - the dice's greying above all - reads -1 until
    // something selects an item, and addItemList selects nothing.
    void syncShapeBox();
    // The dial's readout, reinstalled after every attachment swap - see ArpPanel's.
    void installRateText();
    // Rate is one knob over two parameters and two units, exactly as the band's is: which
    // attachment exists depends on Sync or Hz, and the swap has to wait out an open drag. The
    // swap and that guard are arprate::applyMode, shared with the band; what is left here is
    // the greying this card does and the band does differently.
    void refreshRateMode();

    KeysProcessor& processor;
    int line;

    // LTCH, PLAY and Chain were on the row until 2026-08-02, when Owen had the rows
    // slimmed to what you reach for while two lines are running. All three still live
    // with the line - Latch and PLAY on its tab's band, Chain on the action row under its
    // slots. The line switch itself (onButton) left on 2026-08-02 too, the day the A/B
    // chips on the ARP section bar became the per-line On toggles: two on-switches for the
    // same parameter, one of them buried in a card, was a control to get wrong twice.
    okstudio::RotaryKnob rateKnob;
    juce::TextButton ratePrev { "<" }, rateNext { ">" };
    juce::TextButton rateModeButton { "Sync" };
    // The rate's three modifiers, the same three the band carries and greyed by the same
    // question (2026-08-02, Owen: "I need to have options for dots and triplets as well").
    // They sit on the card's bottom line with the held chord, at the full 34 px hit
    // height: putting them beside the rate would drive the knobs under the mouse-only
    // minimum, and height is the cheap axis inside a card.
    juce::ToggleButton dotButton { "Dot" }, anchorButton { "Anchor" };
    // Legato (2026-09-01, Owen: "a legato button. So when the density is lower or a note is
    // skipped, it continues nicely"): holds a note through the steps that do not fire and
    // lets it go just after the next one that does. Beside Dot and Anchor because it is the
    // same kind of thing - a per-line switch you flip while listening - at the full 34 px,
    // on the strip that exists for exactly those. Card-only, like Mutate, Stray and Lock.
    juce::ToggleButton legatoButton { "Legato" };
    // The line bus's source picker (2026-09-01): Off, or a letter above this one, worded
    // "From A" because the strip has no caption to say what a bare letter would mean. Here
    // rather than on the Play page beside Retrigger, where the design put it, because that
    // row is exactly full at the deep floor and this strip had a cell to spare at the docked
    // one; being next to DUCK's card is the better home anyway. Letters at or below this
    // line are greyed in the popup rather than missing from it - the attachment maps index
    // to item, so the list must hold every entry the parameter can - and on line A that is
    // all three, so the combo opens to Off and three reasons why.
    juce::ComboBox followsBox;
    // **Keybed** (2026-09-01, Owen, with line A's switch off and no way to see it: "I
    // thought it was on"): the per-line arpKeys switch, on the card. Whether what you play
    // on the keybed feeds this line was decided on the Play page alone, two clicks inside a
    // view nothing on the card or the bar reports on - the 2026-08-27 round's "it's so
    // unclear where that's hiding", one axis over. It sits on the top row after the dice,
    // in the slack Shape's cap leaves at every floor, so it costs no width and no height.
    // On-screen word Keybed, on both surfaces: "Play" read as the line's own On, and
    // "Keys" collided with the bar's Light keys (the 2026-08-02 record).
    juce::ToggleButton keybedButton { "Keybed" };
    // Tuplet is a combo, not a tick: it picks one of five, and a check box that cycled its
    // own text was a control lying about its own shape (2026-08-03, Owen: "it's a check box
    // but it changes"). A combo is what Keys already means by "pick from a list" - Shape,
    // Distance and Retrigger are all one - so it needs no explaining, and it takes an
    // ordinary ComboBoxAttachment where a button could not bind a choice at all.
    juce::ComboBox tupletBox;
    // Opens this line's detailed view (the band and, on Pattern, the step editor). Added
    // beside Anchor once the A/B chips stopped navigating anything: with the tabs gone,
    // this button is the only way back from the macro cards to the deep view.
    juce::TextButton detailsButton { "Details" };
    juce::ComboBox shapeBox;
    juce::TextButton shapePrev { "<" }, shapeNext { ">" };

    // **The dice** (2026-08-21, Owen: "I use the random ones a lot, and I'd like to have a
    // dice button when those are active nearby to regenerate their pattern"). Drawn rather
    // than lettered or iconned: the same self-drawn-chrome rule SectionBar's fold chevron
    // and the settings gear follow, and there is no word for this that fits in 34 px.
    //
    // It greys outside Random Once instead of vanishing. Random and Random Other draw fresh
    // every step and have no stored order for a dice to deal again, so a button that stayed
    // lit on them would promise something it cannot do - and the house rule is that a
    // control which would reflow its neighbours greys rather than disappears. Its cell is
    // reserved on every shape for exactly that reason.
    struct DiceButton : juce::Button
    {
        DiceButton() : juce::Button("dice") {}
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    };
    DiceButton diceButton;
    // Five of the seven are plain rotaries. H.TIME and VEL are RangeKnobs, because each
    // of them is a random draw and a draw has two ends (2026-08-03) - `ranges` holds one
    // for those two indices and nullptr for the rest, and `knobFace()` is what everything
    // else walks so the layout, the headings and the attachments stay one loop. VEL's ring
    // is Humanize Velocity (2026-08-17), not a span of its own value the way H.TIME's is -
    // see the wiring in the constructor for why the two range knobs are not symmetric.
    std::array<juce::Slider, numKnobs> knobs;
    std::array<std::unique_ptr<RangeKnob>, numKnobs> ranges;
    juce::Component& knobCell(int k);
    juce::Slider& knobFace(int k);
    static bool isRangeKnob(int k) { return k == kHTime || k == kVel; }
    std::array<juce::Label, numKnobs> knobLabels;
    // The two fixed harmony voices (2026-08-19, Owen holding up BigSky's shimmer list:
    // "2 harmony drop down like the photo. and each of those has a chance knob"). An
    // interval combo and a chance knob each, on their own strip between the knobs and the
    // rate's modifiers - per card because Owen asked for them "in the arps", and a combo
    // because "pick from a list" is a combo in Keys. Labels are HARMONY 1 / CHANCE /
    // HARMONY 2 / CHANCE, in that order.
    //
    // **The dropdown opens as two columns**, descending intervals on the left and
    // ascending on the right, which is how the BigSky panel lays the same list out and
    // what "make harmony 2 columns" turned out to mean (2026-08-19; a first reading put
    // the *card's* controls in two columns, and Owen, shown the popup: "still one
    // column"). A ComboBox builds its own single-column menu internally, so the subclass
    // rebuilds it with a column break - same items, same ids, nothing else changed.
    struct HarmonyBox : juce::ComboBox
    {
        void showPopup() override;
    };

    std::array<HarmonyBox, 2> harmBoxes;
    std::array<juce::Slider, 2> harmChanceKnobs;
    std::array<juce::Label, 4> harmLabels;
    std::array<std::unique_ptr<ComboAtt>, 2> harmAtts;
    std::array<std::unique_ptr<SliderAtt>, 2> harmChanceAtts;
    // RATE and SHAPE, over the top line's two stepper groups (2026-08-02, Owen: "the
    // arrows to adjust certain parameters are not clear as to what they're adjusting"):
    // two flanked `< >` pairs side by side read as one puzzle without names above them.
    juce::Label rateHeadLabel, shapeHeadLabel;
    juce::Label chordLabel;

    std::unique_ptr<ButtonAtt> rateModeAtt;
    std::unique_ptr<ButtonAtt> dotAtt, anchorAtt, legatoAtt, keybedAtt;
    std::unique_ptr<ComboAtt> tupletAtt, followsAtt;
    std::array<std::unique_ptr<SliderAtt>, numKnobs> knobAtts;
    void setDropTarget(bool);
    // What VEL's ring puts back when its lamp switches Humanize Velocity back on - the
    // same shape ChordPads' lastStrumMax uses for Strum's own zero-is-off lamp. UI-only
    // and deliberately not persisted: a session saved with it off should open off.
    double lastHumanVelAmount = 20.0;

    ArpPanel& owner;
    // Exactly one of these is ever non-null; refreshRateMode owns that invariant.
    std::unique_ptr<SliderAtt> rateSyncAtt, rateHzAtt;
    int lastRateFree = -1;      // -1 = no attachment installed yet
    bool rateDragging = false;  // an open gesture; the swap defers until it closes
    bool dropTarget = false;
    // The scrim's own cache, compared in refresh() (driven by the panel's 10 Hz timer while
    // the macro view is up) so repaint() is only called on an actual change rather than
    // every tick.
    bool lastLineOn = true;
    // The same trick for the rate readout, which no attachment drives: -1 = nothing drawn yet.
    int lastTuplet = -1, lastDotted = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroRow)
};

} // namespace keys
