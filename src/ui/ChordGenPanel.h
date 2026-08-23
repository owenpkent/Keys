#pragma once

#include "../PluginProcessor.h"
#include "ChordGenMenu.h"
#include "ChordTray.h"
#include "SourceViz.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>

namespace keys
{
// The chord generator's controls, as the content of a window of its own (2026-07-30, Owen: "I
// think the chord generator should just pop out a new window instead of being in the right
// click menu").
//
// This is a **view onto ChordGenMenu, never its owner.** The brain is a member of the editor
// and lives for the editor's whole life; this is built when the window opens and destroyed
// when it closes. That split is the whole design and it is load-bearing rather than tidy:
// "New chord" and "Next: could follow" are items on a pad's card menu, and while the generator
// *was* a panel those items came and went with it, which is the bug that made it a plain
// member in the first place. Nothing here may become the only copy of anything.
//
// Recovered from the panel that was deleted on 2026-07-30 (`git show
// 7261228^:src/ui/ChordGenPanel.cpp`), which is a layout Owen used and liked, and adapted:
//
//   * every control is an APVTS attachment, exactly as it was, so it and the twin on the Pads
//     bar (Key, Mode, Scale Compliance) read and write the one parameter and can never
//     disagree. There is no hand-syncing anywhere in this class;
//   * **nothing in here writes a pad** (2026-08-01, Owen: "when you click on regenerate unlocked,
//     I don't want it to regenerate the ones in the host window, only in the card generator
//     window"). Fill, Regen and Clear act on the audition tray below, and the only way a chord in
//     this window reaches the strip is a drag you made. The Pads bar keeps Fill and Regen for the
//     page itself, next to the pads they write. This window did call `fillPage` /
//     `regeneratePage` / `clearPage` until that day; the first two are still ChordGenMenu's and
//     still reached from the bar, and `clearPage` was deleted outright, because a page-wide wipe
//     with no undo had exactly one home and this was it. The wipe came back on 2026-08-23, as
//     KeysProcessor::clearChordPadPage off a Clear page row on a pad's card menu - undo covers
//     the pad tree now, so it is one entry and one click back. It did not come back *here*;
//     nothing in this window writes a pad, which is the rule that outlived the deletion;
//   * the suggestion audition is *not* here. It calls noteOn with no pad behind it and is
//     released by an 800 ms timer, so it stays in ChordGenMenu where the destructor that
//     stops it cannot be closed away (see ~ChordGenMenu). This class never plays a note;
//   * Mood and Start are not held here either. They are transient picks that belong to the
//     progression being generated, and shutting the window must not reset them, so the combo
//     boxes read and write ChordGenMenu's copies.
//
// There is a 4x4 grid again from 2026-08-01, and it is not the one that was removed. The panel
// drew a copy of the current *page* until 2026-07-30 - the same sixteen pads, through the same
// KeysProcessor::setChordPad, as the Pads section already on screen, so it was a second view of
// one thing and the cards downstairs were the better view. What is here now is ChordTray:
// sixteen candidates that belong to no slot and are not in the session, for hearing a chord
// before it costs you a pad (Owen: "I have four by four pad where you can audition new chords.
// We want to be able to try a bunch out"). The tray is where a chord comes *from*; the pads are
// where it goes. Read ChordTray's own comment before touching it - the distinction is the whole
// reason it is allowed to exist, and a grid here that wrote pads directly would be the removed
// one again under a new name.
//
// This class still never calls noteOn. The tray auditions through ChordGenMenu, the same path
// the suggestion preview takes and for the same reason: the brain outlives every window.
// It is a `DragAndDropContainer` for one reason: the tray inside it drags candidates onto the pad
// strip in the *other* window, and JUCE requires the container to be an ancestor of the source.
// Nothing else about this class is drag machinery - the ends of the gesture belong to the tray
// and to whatever takes the drop, which is how it can now cross a window at all.
class ChordGenPanel : public juce::Component,
                      public juce::DragAndDropContainer,
                      private juce::Timer
{
public:
    ChordGenPanel(KeysProcessor&, ChordGenMenu&);
    ~ChordGenPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // The on-screen Close button. The window's title-bar X runs the same teardown; the editor
    // wires both to one call, so there is exactly one way for this object to die.
    std::function<void()> onClose;

    // "Send to first empty pad" on a tray card's menu: the commit drag with the aim taken out.
    // Passed through to the editor, because a *menu item* has no target and no drop - this
    // window still cannot name a pad on its own. The drag itself needs nothing here any more.
    std::function<bool(const KeysProcessor::ChordPad&)> onCandidateToFirstEmptyPad;
    std::function<bool()> onPageHasEmptyPad;

    // Write a set of chords straight into the tray (2026-08-18, for the Library window's "To
    // tray"). It goes through `ChordTray::setAll`, which is the same door the reference card's
    // Similar and Could follow already use, so a seeded trayful behaves identically whichever of
    // the three seeded it - including not being rerolled by the settings poll, which is exactly
    // what you want for a progression you asked for by name.
    void fillTrayWith(const std::vector<KeysProcessor::ChordPad>& chords) { tray.setAll(chords); }

    // What the layout below actually needs, so the window's minimum is derived rather than
    // guessed. Widest row is the algorithmic settings row; tallest is all four rows plus the
    // gaps between them. See resized() for the arithmetic each of these adds up.
    static juce::Point<int> contentSize();
    static juce::Point<int> minWindowSize();     // contentSize + the window's own furniture
    static juce::Point<int> maxWindowSize();     // ...and the ceiling: see the definition
    static juce::Point<int> defaultWindowSize(); // where it opens the first time

private:
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void buildControls();
    void refreshMoodItems(); // the Mood list belongs to the chain that is up

    // What the Library band's three filters currently match: how many rows, and which one the
    // last generation landed on. Two answers in one line because they answer two halves of the
    // same worry - "is my filter too narrow" and "what did I just get" - and a band with one row
    // of controls has room for one readout.
    void refreshLibraryResult();

    // The eight sets of controls that share row B, and the one place that decides which is on
    // screen. They are laid into the *same* rect and exactly one may be visible: the bands are
    // different widths, so a hidden-but-visible one paints out past the edge of the one that is
    // up. Two of these until 2026-08-01, when the five `sources::` brains arrived; the shape did
    // not change, only the count, and `bandFor` replaced the pair of fixed-size arrays because a
    // switch that has to name every band is a switch somebody forgets to extend.
    std::vector<juce::Component*> bandFor(int source);
    std::vector<juce::Component*> allBandControls();
    void applySource(int source);

    KeysProcessor& processor;
    ChordGenMenu& gen;

    // No mode-character line beside the title since 2026-08-01 (Owen: "we don't want it to say,
    // like, bruised, relaxed, jazz at the top related to the key"). It printed
    // `modes::get(mode).emotion` - "Bluesy, Relaxed, Rock" for Mixolydian - which is a claim
    // about how a mode feels, in a window whose whole job is to let you hear chords and decide
    // that for yourself. `modes::get().emotion` is untouched and still used elsewhere.
    juce::Label title;
    // Page tabs, in the header (2026-08-18, Owen: "when you open the generator, we need to be
    // able to toggle between pages at the top"). This was a read-only "Page 2 of 4" label: it
    // said which page a committed card would land on and gave you no way to change it, so
    // choosing where to put a chord meant leaving the window for the Pads bar and coming back.
    // Bound to `padPage`, the same parameter those four buttons drive, so the two agree.
    std::array<juce::TextButton, KeysProcessor::numPadPages> pageButtons;
    juce::TextButton closeButton { "Close" };

    juce::ComboBox rootBox, modeBox;
    juce::Label rootLabel, modeLabel;
    juce::Slider octaveSlider, complianceSlider, lockInfluenceSlider;
    juce::Label octaveLabel, complianceLabel, lockInfluenceLabel;

    // Notes and Inversions are always on screen from 2026-08-01 (Owen: "all of their options
    // should have the option for how many notes and what inversion"). They were the head of the
    // Algorithmic band and went off screen under the other six, which was wrong twice over: the
    // note count and the register are facts about the *voicing*, so they were never the weighted
    // pool's property in the first place, and the generator now applies both as post-passes over
    // whatever any of the seven produced.
    //
    // The 3/4/5 tick boxes became a min/max pair spanning 1 to 11 (2 to 11 until 2026-08-21, when
    // the floor dropped so an unticked gate could roll a single note). Below three you get dyads, and
    // above five the stack keeps climbing in thirds through the mode. Three tick boxes could not
    // have carried ten values without becoming ten targets.
    juce::Label notesLabel, invLabel, octaveMaxLabel;
    juce::Slider notesMinSlider, notesMaxSlider, octaveMaxSlider;
    juce::ToggleButton inv0Button { "R" }, inv1Button { "1st" }, inv2Button { "2nd" }, inv3Button { "3rd" };

    // The tray's three actions. They read Fill / Regen / Clear rather than the Fill Page / Regen
    // Unlocked / Clear Page they were until 2026-08-01, because the word Page is exactly what
    // stopped being true: none of them touches a pad any more.
    juce::TextButton fillButton, regenButton, clearButton;
    // Commit the whole tray in one click (2026-08-18, Owen: "when you click on fill, I want a new
    // button to send all to pads or something like that or fill empty spots to send a bunch of
    // them to the main app"). The tray had exactly one route to a pad per card - a drag, or Send
    // to first empty pad on its own menu - so filling a page meant sixteen gestures. It writes
    // only empty pads, like every other generative action in Keys, and each card it places leaves
    // its cell, so what stays behind is exactly what would not fit.
    juce::TextButton toPadsButton;

    // The audition tray. Owned here rather than by ChordGenMenu, and that is consistent with the
    // rest of this class rather than an exception to it: a tray card is not state, so throwing
    // the sixteen away with the window costs nothing. Mood and Start live on the brain because
    // losing *them* would silently change what the next generation produces.
    // The picture of what the current source is doing, under the row of source buttons so it
    // explains the one you just pressed. Read-only and click-through: it is a diagram, and it
    // takes no input at all. Fed from the timer, because everything it draws (the source, the
    // key, the tray's contents) can move without this class being told.
    SourceViz viz;

    ChordTray tray;
    juce::Label trayLabel;

    // The reference chord and the three things you can do to it, all left-click. Similar and
    // Could follow are also items on a tray card's right-click menu, seeded by that card instead
    // of by this one; these are the twins that keep those two off the closed right-click list,
    // and they are the reason the reference exists at all - a seed you keep is a seed you can
    // ask twice.
    ChordRefCard refCard;
    juce::Label refLabel;
    juce::TextButton similarButton { "Similar" }, followButton { "Could follow" },
        clearRefButton { "Clear" };

    // The Markov source's controls; visible only while Source is Markov, in the same band as
    // the algorithmic settings they replace. Those settings mean nothing to a chain walk, so
    // the band shows whichever set is live rather than reserving a row that is dead half the
    // time. Sharing the rect makes the visibility swap load-bearing rather than cosmetic:
    // applySource() owns it, and the two sets are never on screen together.
    // Source is seven buttons on a row of their own, not a combo box (Owen, 2026-08-01: "maybe
    // instead of the source being a drop down and the direction being a drop down, maybe those
    // can be, like, always visible"). A combo costs two clicks and hides six of the seven answers
    // behind the first, which for a setting whose whole point is comparison is the wrong shape.
    // Laid out by `sourceButtons()`, and each one writes `genSource` through the same parameter
    // the Pads bar reads, so there is still exactly one source of truth.
    std::array<juce::TextButton, 8> sourceButtons;
    juce::Label sourceLabel;
    std::array<juce::TextButton, 2> circleDirButtons; // same treatment, same reason
    void setSourceParam(int index);
    void setCircleDirParam(int index);
    void refreshRadioStates(); // the tick marks, polled with everything else

    juce::ComboBox chainBox, moodBox, startBox;
    juce::Label chainLabel, moodLabel, startLabel;
    juce::Slider tempSlider, lengthSlider;
    juce::Label tempLabel, lengthLabel;

    // The five brains added on 2026-08-01, one band each. Negative Harmony has no band at all:
    // it reflects the key about the axis between tonic and dominant, and Key, Mode and Octave in
    // row A are the whole of what that needs, so an empty row B is the honest answer rather than
    // a control invented to fill it.
    juce::ComboBox progressionBox;
    juce::Label circleDirLabel, progressionLabel;
    juce::Slider plrPSlider, plrLSlider, plrRSlider;
    juce::Label plrPLabel, plrLLabel, plrRLabel;
    juce::ToggleButton planingDiatonicButton { "Diatonic" };
    juce::Label planingLabel;

    // The Library band (2026-08-18): three filters over the named-progression table, and the row
    // that reads back which entry the last generation actually landed on. Not attachments - the
    // three picks live on ChordGenMenu, the shape Markov's Mood and Start already use and for the
    // same reason plus one of their own (see `ChordGenMenu::libraryMood`'s comment). The readout
    // is a Label rather than a fourth combo on purpose: the pick is a filter, so "which row" is an
    // *answer*, and offering it as a control would invite you to set it and then be overruled.
    juce::ComboBox libMoodBox, libGenreBox, libFunctionBox;
    void adoptLibraryFilters(); // pull the brain's three picks onto the combos above
    juce::Label libMoodLabel, libGenreLabel, libFunctionLabel, libResultLabel;

    // Brightness sweeps the seven diatonic modes from Lydian to Locrian, which is what "a slider
    // between major and minor" turns out to mean once you look at what is between them (Owen,
    // 2026-08-01: "what about a slider that goes between major and minor?"). Major and minor are
    // positions 1 and 4 on it, so sliding past either lands you somewhere real rather than
    // nowhere. It is a **view onto `genMode`**, not a parameter of its own, because two
    // parameters for one thing is how they end up disagreeing; `brightnessOrder` is the map.
    // The mode table's back half (harmonic minor, blues, the pentatonics) is not on this axis at
    // all, so the slider greys when Mode is one of those and says so by doing nothing.
    juce::Slider brightnessSlider;
    juce::Label brightnessLabel;
    void setModeFromBrightness(int position);
    void refreshBrightness();
    int lastBrightnessShown = -1;

    // And the other half of that ask: lean the chords major or minor without moving the mode.
    juce::Slider majMinSlider;
    juce::Label majMinLabel;

    // Voice leading is not a band. It is a pass over whatever a source produced, so it belongs to
    // all seven and sits in row A where none of them can hide it.
    juce::Slider smoothSlider;
    juce::Label smoothLabel;

    // No attachment for Source or Circle Direction: both are button rows now, and a row of
    // buttons on one choice parameter is a thing JUCE has no attachment for. They write the
    // parameter in setSourceParam / setCircleDirParam and read it back in refreshRadioStates,
    // which the timer runs. That is hand-syncing, which this class otherwise never does, and it
    // is confined to those three functions on purpose.
    std::unique_ptr<ComboAtt> rootAtt, modeAtt, chainAtt;
    std::unique_ptr<ComboAtt> progressionAtt;
    std::unique_ptr<SliderAtt> octaveAtt, complianceAtt, lockInfluenceAtt, tempAtt, lengthAtt;
    std::unique_ptr<SliderAtt> plrPAtt, plrLAtt, plrRAtt, smoothAtt;
    std::unique_ptr<SliderAtt> notesMinAtt, notesMaxAtt, octaveMaxAtt, majMinAtt;

    // The tick boxes. Each is 34 px wide and the full height of its cell, because the mouse-only
    // floor applies to a check box exactly as it does to a button, and a tick parked in a 14 px
    // caption strip would be a target you cannot hit.
    // **The six gates are word chips, not check marks** (2026-08-18, Owen, asked which of three
    // faults the check marks had: "all"). They were bare `juce::ToggleButton`s, identical to the
    // four inversion ticks beside them - which are *values*, not gates - so the fixed row read as
    // five boxes in a row with two meanings and one look. They also sat at the left edge of
    // whatever cell they gated, landing at a different x on every row, and six cryptic boxes on
    // one page is a lot to ask anyone to remember the meaning of.
    //
    // One change answers all three. A chip reading **SET** or **ROLL** cannot be mistaken for a
    // tick, says what it does without a tooltip, and reads as a column because every one of them
    // is the same shape and width wherever it sits. The parameters and their ids are untouched:
    // a TextButton in toggle mode takes the same ButtonAttachment a ToggleButton did.
    std::array<juce::TextButton, 6> useBoxes;
    void refreshGateChips();
    std::array<std::unique_ptr<ButtonAtt>, 6> useAtts;
    std::unique_ptr<ButtonAtt> planingDiatonicAtt;
    std::unique_ptr<ButtonAtt> inv0Att, inv1Att, inv2Att, inv3Att;

    int lastChainMode = -1; // rebuild the Mood list when the chain mode changes
    int shownSource = -1;   // last band applied, to relayout on a source change
    // The Library row generation last landed on, cached so the 10 Hz tick repaints the readout on
    // a change rather than every tick. Generation happens outside this window - Fill and Regen
    // ride the tray's header, and the Pads bar has its own pair - so polling is the only way this
    // band learns what it got.
    juce::String lastLibraryEntry;
    // The Library filters can move from the *other* window - they are one piece of state, held on
    // ChordGenMenu - so this band has to notice, exactly as ChordLibraryPanel does. Polled rather
    // than pushed because the two windows do not know about each other and should not have to.
    juce::String lastLibSignature { "" }; // never a real signature, so the first tick adopts

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordGenPanel)
};
} // namespace keys
