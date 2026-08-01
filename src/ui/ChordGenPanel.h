#pragma once

#include "../PluginProcessor.h"
#include "ChordGenMenu.h"
#include "ChordTray.h"
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
//     with no undo had exactly one home and this was it;
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
class ChordGenPanel : public juce::Component,
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

    // The audition tray's cross-window drag, passed straight through to the editor - this class
    // is the only thing that holds the tray, and the editor is the only thing that holds both it
    // and the pad strip. Screen coordinates; see ChordTray for why there is no other option.
    std::function<void(juce::Point<int> screenPos)> onCandidateDragOver;
    std::function<bool(juce::Point<int> screenPos, const KeysProcessor::ChordPad&)> onCandidateDropped;
    std::function<void()> onCandidateDragEnd;

    // "Send to first empty pad" on a tray card's menu: the drag with the aim taken out. Same
    // pass-through, and the same reason for it - this window cannot see the pad strip.
    std::function<bool(const KeysProcessor::ChordPad&)> onCandidateToFirstEmptyPad;
    std::function<bool()> onPageHasEmptyPad;

    // A chord dragged *out* of the main window's pad strip and offered to the reference card:
    // the mirror of the commit drag, and the only route by which anything outside this window
    // puts something into it. Screen coordinates again. `offerReferenceDrop` returns true when
    // the reference took it, which is what tells ChordPads not to treat the drag as a clear.
    void showReferenceDropTarget(juce::Point<int> screenPos);
    bool offerReferenceDrop(juce::Point<int> screenPos, const KeysProcessor::ChordPad&);
    void clearReferenceDropTarget();

    // What the layout below actually needs, so the window's minimum is derived rather than
    // guessed. Widest row is the algorithmic settings row; tallest is all four rows plus the
    // gaps between them. See resized() for the arithmetic each of these adds up.
    static juce::Point<int> contentSize();
    static juce::Point<int> minWindowSize();     // contentSize + the window's own furniture
    static juce::Point<int> defaultWindowSize(); // where it opens the first time

private:
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void buildControls();
    void refreshMoodItems(); // the Mood list belongs to the chain that is up

    // The seven sets of controls that share row B, and the one place that decides which is on
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
    juce::Label title, pageLabel;
    juce::TextButton closeButton { "Close" };

    juce::ComboBox rootBox, modeBox;
    juce::Label rootLabel, modeLabel;
    juce::Slider octaveSlider, complianceSlider, lockInfluenceSlider;
    juce::Label octaveLabel, complianceLabel, lockInfluenceLabel;

    juce::Label notesLabel, invLabel;
    juce::ToggleButton triadsButton { "3" }, seventhsButton { "4" }, ninthsButton { "5" };
    juce::ToggleButton inv0Button { "R" }, inv1Button { "1st" }, inv2Button { "2nd" }, inv3Button { "3rd" };

    // The tray's three actions. They read Fill / Regen / Clear rather than the Fill Page / Regen
    // Unlocked / Clear Page they were until 2026-08-01, because the word Page is exactly what
    // stopped being true: none of them touches a pad any more.
    juce::TextButton fillButton, regenButton, clearButton;

    // The audition tray. Owned here rather than by ChordGenMenu, and that is consistent with the
    // rest of this class rather than an exception to it: a tray card is not state, so throwing
    // the sixteen away with the window costs nothing. Mood and Start live on the brain because
    // losing *them* would silently change what the next generation produces.
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
    juce::ComboBox sourceBox, chainBox, moodBox, startBox;
    juce::Label sourceLabel, chainLabel, moodLabel, startLabel;
    juce::Slider tempSlider, lengthSlider;
    juce::Label tempLabel, lengthLabel;

    // The five brains added on 2026-08-01, one band each. Negative Harmony has no band at all:
    // it reflects the key about the axis between tonic and dominant, and Key, Mode and Octave in
    // row A are the whole of what that needs, so an empty row B is the honest answer rather than
    // a control invented to fill it.
    juce::ComboBox circleDirBox, progressionBox;
    juce::Label circleDirLabel, progressionLabel;
    juce::Slider plrPSlider, plrLSlider, plrRSlider;
    juce::Label plrPLabel, plrLLabel, plrRLabel;
    juce::ToggleButton planingDiatonicButton { "Diatonic" };
    juce::Label planingLabel;

    // Voice leading is not a band. It is a pass over whatever a source produced, so it belongs to
    // all seven and sits in row A where none of them can hide it.
    juce::Slider smoothSlider;
    juce::Label smoothLabel;

    std::unique_ptr<ComboAtt> rootAtt, modeAtt, sourceAtt, chainAtt;
    std::unique_ptr<ComboAtt> circleDirAtt, progressionAtt;
    std::unique_ptr<SliderAtt> octaveAtt, complianceAtt, lockInfluenceAtt, tempAtt, lengthAtt;
    std::unique_ptr<SliderAtt> plrPAtt, plrLAtt, plrRAtt, smoothAtt;
    std::unique_ptr<ButtonAtt> triadsAtt, seventhsAtt, ninthsAtt, planingDiatonicAtt;
    std::unique_ptr<ButtonAtt> inv0Att, inv1Att, inv2Att, inv3Att;

    int lastChainMode = -1; // rebuild the Mood list when the chain mode changes
    int shownSource = -1;   // last band applied, to relayout on a source change

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordGenPanel)
};
} // namespace keys
