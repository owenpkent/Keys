#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

// Where the editor's furniture is remembered, and the two functions that put it in a session
// and take it back out.
//
// It left KeysProcessor because none of it is the processor's business: it is message-thread UI
// state, the audio thread never reads a field of it, and the pair of tree functions beside it
// only ever touched `layout` and the arp line count. The processor still owns the one live
// `LayoutState` and still answers to layoutToTree()/layoutFromTree(), which are forwarders onto
// these now - so every `processor.layout.xxx` reader in the editor compiles exactly as it did,
// and the property names a saved session carries are byte for byte the same.

namespace keys
{
// How the editor is folded up. Deliberately not parameters: none of it changes a
// note, and exposing five booleans to host automation would only add ways to break
// a session. It lives here rather than in the editor because the editor is created
// and destroyed every time the window opens, and Owen should get the same layout
// back. Message thread only; the audio thread never reads it.
struct LayoutState
{
    bool controls = true;   // the header rows and the knob bank under them
    // Vestigial since 2026-08-02: the Knobs chip that folded the CC knob bank off the
    // bottom of the Controls band is gone, and the bank is unconditional whenever the
    // section itself is open. Kept, always true, so layoutToTree()/layoutFromTree() keep
    // round-tripping a session's tree without a special case for one dropped field.
    bool knobs = true;
    bool pads = true;       // the chord-pad strip
    bool arp = false;       // the arpeggiator section (off by default: it is tall)
    bool wheels = true;     // mod + pitch, left of the keybed
    bool keyboard = true;   // the keybed itself

    // Every section can also be popped out into a window of its own (2026-07-27; the
    // keybed and the arp could already, and Owen asked for the rest to follow). One
    // flag and one remembered frame each, in the editor's top-to-bottom order.
    // `detached` keeps its bare name: it is the keybed's, and renaming it would drop
    // the setting out of every session saved before this.
    bool controlsDetached = false;
    bool arpDetached = false;
    bool padsDetached = false;
    bool detached = false;  // keybed lives in its own resizable window

    // The chord generator's window (2026-07-30, Owen's call). Not a section: it is never
    // docked, has no bar and no fold, and opens from a button on the Pads bar. It is in
    // here all the same because it is the same question - where did Owen leave a window,
    // and was it open - and the answer has to survive the editor closing.
    bool chordGen = false;
    // And the same for the Library window (2026-08-18), which is the second surface onto
    // ChordLibrary.h and is opened by its own chip on the same bar.
    bool chordLib = false;

    // Which arp line the panel is editing and a chord card feeds. See arpCurrentLine().
    int  arpLine = 0;
    // ...and whether it is showing the macro view instead of that line's own controls.
    // Same kind of state and the same reason it lives here: the panel is destroyed every
    // time the section folds, and Owen should get back the view he left.
    //
    // **Default true from 2026-08-02**, which is what "view two arpeggiators" means in
    // practice: a fresh instance opens with both lines on screen as rows, over the chord
    // strip you drag from. The A / B tabs are still there for the step lanes and the twelve
    // slots, which are per-line and have nowhere to live in a row.
    bool arpMacro = true;
    // Whether the All view's **bottom row of macro cards is collapsed to a strip**
    // (2026-08-19, Owen: "maybe you should be able to minimize bottom arps"). Four lines in
    // a 2x2 grid is two card rows where it was one, and a card is 323 px, so the All view
    // alone sets a 1349 px minimum window - against a 1392 px work area on Owen's own
    // screen, and more than a 1080p one has at all. Collapsed, the bottom row is a 34 px
    // strip and the minimum falls to 1060.
    //
    // **The lines keep playing.** This is the macro card's scrim rule read one level up:
    // collapsing is about what is on screen, never about what is running, so C and D keep
    // their chords, their patterns and their output. It is also why the strip carries no On
    // switches of its own - those are on the arp bar, and a second writer for one parameter
    // is exactly the mistake that deleted MacroRow's own On toggle on 2026-08-02.
    //
    // Here rather than in the panel for the reason arpMacro and arpPage are: the panel is
    // destroyed every time the section folds, and the view you left is the one to get back.
    bool arpMacroBottomFolded = false;
    // Which page of a line's deep view is showing (2026-08-14, Owen: "can we simplify the
    // detail view or organize into pages"). Values are ArpPanel::Page: 0 = Draw (the step
    // lanes), 1 = Cards (the twelve slots), 2 = Play (rate, shape, feel).
    //
    // **Defaults to 2, Play**, and that is not arbitrary: Draw does nothing until you have
    // drawn on it *and* set Shape to Pattern, so opening there is opening on a blank page
    // with no way to tell why. Owen landed on exactly that - one step long, Shape on
    // Pattern - and said "I don't understand this layout. how to get the sound I want".
    // Play is where rate and shape are, which is the answer to that question.
    //
    // The deep view used to be all four blocks at once - band, lanes, slots, actions - at
    // 612 px against the macro view's 240, so clicking Details grew the *window* by 372 px
    // and clicking All shrank it back. Paged, every page fits inside one fixed panel
    // height, and the window stops moving between views entirely. Same reason this lives
    // here rather than in the panel as arpMacro does: the panel is destroyed every time
    // the section folds, and Owen should get back the page he left.
    int  arpPage = 2;
    // Whether the keybed lights up for the notes the arp is *playing*, as opposed to the
    // chord it was handed (which lights it either way, through noteRefs). Layout state and
    // not a parameter: it changes what is drawn and nothing that is heard, so there is
    // nothing here for a host to automate. On by default - it is the thing Owen asked to
    // be able to see - and one click on the arp bar turns it off when the flicker of a
    // 1/16 run is not what you want to be looking at.
    bool arpLights = true;

    // The settings menu (2026-08-17, Owen: "we need a settings icon and menu. populate
    // menu."), reached from the gear on the Controls bar. Three fields, none of them a
    // parameter for the same reason arpLights is not: nothing here changes a note.
    //
    // UI scale, Octavium's Zoom submenu ported over (same eight presets). Persisted so a
    // choice survives a reopen; the editor does not yet resize or transform itself to
    // match it - see KeysEditor::showSettingsMenu for why that half is deliberately not
    // built alongside the menu, rather than guessed at against this window's own
    // extensively-documented, pixel-exact resize floor.
    int  uiScalePercent = 100;

    // Hold Visuals During Sustain: on by default, because on **is** today's behaviour and
    // has been since before this flag existed - a key the pedal is holding paints in the
    // held colour, same as latched. Off is Octavium's own menu item, wired for the first
    // time (it read `self.keyboard.visual_hold_on_sustain` there but nothing ever wrote
    // it): a sustained-only key rests visually while it keeps sounding, so the eye can
    // separate "the pedal is holding this" from "this is down right now" even though both
    // are true. A note that is also pressed or latched is unaffected - see
    // PianoKeyboard::paint's stateOf.
    bool holdVisualsOnSustain = true;

    // Whether a glide made with the pedal down leaves every key it crossed ringing.
    // **Default true, which is exactly what Keys has always done** - see the trail branch
    // in NoteSurface::mouseDrag.
    //
    // This started life as Octavium's "Drag While Sustain" and the name did not survive
    // contact. Octavium describes that option as letting a click-drag glide across the keys
    // at all, and Keys' drag has *always* glided, unconditionally, on every build - so a
    // switch by that name would either do nothing or take gliding away, and neither is what
    // the label promises. The one thing genuinely left to decide is whether the run piles up
    // behind you or stays monophonic, so the setting is named for that instead. Default true
    // and the gate reads `sustain && this`, so no session that opens after the update plays
    // differently than it did before it.
    bool dragWhileSustain = true;

    // Whether a key held **only** by the pedal counts as part of the chord the keybed is
    // offering - what the live card names, and what an "Edit on keyboard" pad is written
    // from. **Default false** (2026-08-16, Owen: "sustain shouldn't propose chords ...
    // should be a menu option"). It still sounds either way; this is only about proposing.
    //
    // The default is the interesting half. Keys is played with one mouse, so a chord has to
    // be built one click at a time, and there are two ways to make a click stick: Latch and
    // Sustain. Reading them the same way meant the pedal's passing notes kept rewriting the
    // card and any pad being edited. Splitting them gives each a job - **Latch builds a
    // chord, Sustain plays one** - and the menu item is there for anyone who wants the old
    // reading back. See NoteSurface::proposedChordNotes.
    bool sustainProposesChords = false;

    // The library rows you starred, by name (2026-08-18). Scaler's browser has this and Keys'
    // had no answer for it at all: 355 rows, and no way to keep the six you actually use.
    // Names rather than indices, the same call `ChordPad::progression` makes and for the same
    // reason - `chordlib::table()` is free to be inserted into, and an index would take that
    // freedom away. A name that no longer matches any row is simply ignored on load, which is
    // what a row being renamed or dropped should cost.
    //
    // **Per session, like every other preference in Keys**, which is the honest weakness here:
    // Scaler's favourites are global, and a star you set in one project is gone in the next.
    // Keys has no global store for anything - the settings gear's three switches are all in
    // this struct too - so a global one would be new machinery for one feature. Worth
    // revisiting the day a second preference wants to outlive a project.
    juce::StringArray libraryFavourites;
    // The Pads bar's **Play** toggle (2026-08-19, Owen: "I want a toggle above the
    // keyboard to play notes. Because some sometimes when I'm trying to drag a cord into
    // the arpeggiator, it plays instead, and it stops everything"). Off, a click on a
    // card makes no sound at all - the strip is drag-only - so a press that was meant to
    // become a drag toward the arpeggiator cannot fire a chord and, with Exclusive on,
    // choke every running line on the way past. The drag, the drop targets and the card
    // menu are untouched; the one left-click arp behaviour that survives is the stop on a
    // cleared card still feeding a line, which plays nothing either way.
    //
    // **On, it is hold-to-play** (2026-08-22, Owen: "when the play mode is checked on the
    // pads, I want it to trigger as soon as you click on it and stay held until you let
    // go"). The press fires the chord and the release ends it, so a stab is short and a
    // lean is long - which is most of what a pad is for. It used to be release-and-fixed,
    // an 800 ms blip owned by a timer, with holding available only as a separate tick on
    // the settings gear (`padHoldToPlay`, 2026-08-18). **That tick is gone and this is what
    // it did**: two switches for one question is one switch too many, and Owen's reading is
    // the plain one - a control called Play plays for as long as you are playing it.
    //
    // What the retired default was protecting against is real and is now this toggle's own
    // job: firing on the press means a press that turns out to be a *drag* has already
    // choked the other chord sources, and with Exclusive on that reaches each arp line's
    // held chord. That is precisely the report Play itself came out of - so the answer is
    // to turn Play **off** while you are dragging cards into the arpeggiator, which is the
    // one gesture it was built for, rather than to keep a second switch that made the
    // sounding half half-hearted. Turning Exclusive off alongside it costs the drag nothing.
    bool padsPlayOnClick = true;

    // **Keep arp running** (2026-08-26, Owen: "I wanna be able to hold the chord down to
    // build it with my mouse, but then also to drag a new chord onto the arpeggiator").
    // Ticked, pressing a card on the strip - a pad or the live card - never releases an arp
    // line's held chord, however Exclusive is set. Unticked is what Keys did before: with
    // Exclusive on, leaning on a card stopped every running line.
    //
    // This is the half of the Play toggle's story that Play could not fix. Play decides
    // whether the strip makes a *sound*; what actually cut the lines off was the **choke**,
    // and the only way to avoid it was to give up the sound as well - so hold-to-build and
    // drag-into-the-arp were two settings you had to keep swapping between. They are one
    // now: Play stays ticked, and a press that turns out to be a drag has taken nothing
    // away by the time it is recognised.
    //
    // A *drop* on a line still replaces that line's chord, and Exclusive still chokes the
    // pads and the live card from it. This narrows one gesture, not the rule: pressing a
    // card is playing a chord, and a line's held chord is not something you are playing -
    // it is what the machine is chewing. Same distinction `takeChordOnLine` draws when it
    // routes a chord without navigating to the line.
    //
    // Default **on**, so it is the behaviour you get without knowing the switch is there -
    // and an older session, whose tree has no such property, takes it too. Nothing about a
    // saved session changes visibly unless Exclusive is on, which is off by default.
    bool padsKeepArpRunning = true;

    int  accent = 0;        // index into skin::accentChoices(); 0 is the OK Studio cyan

    // Where each window was left. Empty = never detached yet, so centre it.
    juce::Rectangle<int> controlsDetachedBounds {};
    juce::Rectangle<int> arpDetachedBounds {};
    juce::Rectangle<int> padsDetachedBounds {};
    juce::Rectangle<int> detachedBounds {};     // the keybed's, named for the flag above
    juce::Rectangle<int> chordGenBounds {};     // the generator's window
    juce::Rectangle<int> chordLibBounds {};     // the library's window
};
} // namespace keys

// Reading and writing that struct against the session tree. Free rather than members because
// neither ever needed the processor: one walks the struct into a "layout" child, the other walks
// it back, and the single fact about the instance they want - how many arp lines there are, for
// the clamp on `arpLine` - arrives as an argument. Inline here rather than in a .cpp of their
// own: they run once per save and once per load, and a header is one fewer file for two
// functions that are nothing but a list of property names.
namespace keys::layoutstate
{
inline juce::ValueTree toTree(const LayoutState& layout)
{
    juce::ValueTree tree { "layout" };
    tree.setProperty("controls", layout.controls, nullptr);
    tree.setProperty("knobs", layout.knobs, nullptr);
    tree.setProperty("pads", layout.pads, nullptr);
    tree.setProperty("arp", layout.arp, nullptr);
    tree.setProperty("wheels", layout.wheels, nullptr);
    tree.setProperty("keyboard", layout.keyboard, nullptr);
    tree.setProperty("detached", layout.detached, nullptr);
    tree.setProperty("arpDetached", layout.arpDetached, nullptr);
    tree.setProperty("controlsDetached", layout.controlsDetached, nullptr);
    tree.setProperty("padsDetached", layout.padsDetached, nullptr);
    tree.setProperty("chordGen", layout.chordGen, nullptr);
    tree.setProperty("chordLib", layout.chordLib, nullptr);
    // Newline-joined rather than comma: a row name may contain a comma ("Axis, vi start") and
    // may not contain a newline, so this is the separator that cannot collide with the data.
    tree.setProperty("libraryFavourites", layout.libraryFavourites.joinIntoString(juce::newLine), nullptr);
    tree.setProperty("arpLine", layout.arpLine, nullptr);
    tree.setProperty("arpMacro", layout.arpMacro, nullptr);
    tree.setProperty("arpMacroBottomFolded", layout.arpMacroBottomFolded, nullptr);
    tree.setProperty("arpPage", layout.arpPage, nullptr);
    tree.setProperty("arpLights", layout.arpLights, nullptr);
    tree.setProperty("uiScalePercent", layout.uiScalePercent, nullptr);
    tree.setProperty("holdVisualsOnSustain", layout.holdVisualsOnSustain, nullptr);
    tree.setProperty("dragWhileSustain", layout.dragWhileSustain, nullptr);
    tree.setProperty("sustainProposesChords", layout.sustainProposesChords, nullptr);
    tree.setProperty("padsPlayOnClick", layout.padsPlayOnClick, nullptr);
    tree.setProperty("padsKeepArpRunning", layout.padsKeepArpRunning, nullptr);
    tree.setProperty("accent", layout.accent, nullptr);
    tree.setProperty("detachedBounds", layout.detachedBounds.toString(), nullptr);
    tree.setProperty("arpDetachedBounds", layout.arpDetachedBounds.toString(), nullptr);
    tree.setProperty("controlsDetachedBounds", layout.controlsDetachedBounds.toString(), nullptr);
    tree.setProperty("padsDetachedBounds", layout.padsDetachedBounds.toString(), nullptr);
    tree.setProperty("chordGenBounds", layout.chordGenBounds.toString(), nullptr);
    tree.setProperty("chordLibBounds", layout.chordLibBounds.toString(), nullptr);
    return tree;
}

inline void fromTree(LayoutState& layout, const juce::ValueTree& root, int numArpLines)
{
    const auto tree = root.getChildWithName("layout");
    if (! tree.isValid())
        return; // sessions from before folding sections: everything open, as it was
    const auto flag = [&tree](const char* id, bool fallback)
    { return (bool) tree.getProperty(id, fallback); };
    layout.controls = flag("controls", true);
    // The Knobs chip that folded the knob row off is gone (2026-08-02, Owen: "make the knobs
    // visible when you open controls"): the row is unconditional now, so a session saved with
    // it off (knobs=false, from before the chip left) must not reopen with the knobs hidden -
    // there is no control left on screen that could turn them back on. The field stays so the
    // tree still round-trips cleanly; nothing reads it as false again.
    layout.knobs = true;
    layout.pads = flag("pads", true);
    layout.arp = flag("arp", false);
    layout.wheels = flag("wheels", true);
    layout.keyboard = flag("keyboard", true);
    layout.detached = flag("detached", false);
    layout.arpDetached = flag("arpDetached", false);
    layout.controlsDetached = flag("controlsDetached", false);
    layout.padsDetached = flag("padsDetached", false);
    // Absent before the generator had a window of its own; shut is the right default either
    // way, since it is a settings window rather than something you play from.
    layout.chordGen = flag("chordGen", false);
    layout.chordLib = flag("chordLib", false);
    layout.libraryFavourites.clear();
    if (tree.hasProperty("libraryFavourites"))
        layout.libraryFavourites.addLines(tree.getProperty("libraryFavourites").toString());
    layout.libraryFavourites.removeEmptyStrings();
    // Absent before there were three lines, and line A is the right answer for those: it is
    // the only one a session from then can have anything in.
    layout.arpLine = juce::jlimit(0, numArpLines - 1, (int) tree.getProperty("arpLine", 0));
    layout.arpMacro = flag("arpMacro", true);
    // Absent before the bottom row could collapse (2026-08-19). False is the view every
    // session before it had: all four cards open.
    layout.arpMacroBottomFolded = flag("arpMacroBottomFolded", false);
    // Absent before the deep view was paged (2026-08-14). Play is the right answer for those,
    // and for a fresh instance: see the LayoutState comment for why not Draw.
    layout.arpPage = juce::jlimit(0, 2, (int) tree.getProperty("arpPage", 2));
    layout.arpLights = flag("arpLights", true);
    // Absent before the settings menu existed (2026-08-17). 100% and today's behaviour are
    // both the right defaults for a session that predates the flags entirely, the same
    // absent-means-default rule every field on this struct already follows.
    layout.uiScalePercent = juce::jlimit(1, 400, (int) tree.getProperty("uiScalePercent", 100));
    layout.holdVisualsOnSustain = flag("holdVisualsOnSustain", true);
    layout.dragWhileSustain = flag("dragWhileSustain", true);
    layout.sustainProposesChords = flag("sustainProposesChords", false);
    // padHoldToPlay is retired (2026-08-22): the Pads bar's Play toggle is that behaviour now.
    // An older session's property is simply ignored, which is what an unknown key in this tree
    // has always cost - unlike an APVTS parameter, a layout property carries no index anybody
    // stores, so dropping one needs no migration.
    layout.padsPlayOnClick = flag("padsPlayOnClick", true);
    layout.padsKeepArpRunning = flag("padsKeepArpRunning", true);
    // Older sessions carry keys nothing reads any more, and every one of them is simply
    // ignored: an unread ValueTree property is dropped, so the load cannot throw and the
    // rest of the layout still arrives.
    //   "transcribe" / "transcribeDetached" / "transcribeDetachedBounds", from before the
    //   Transcribe section was removed.
    //   "centre" / "centreDetached" / "centreDetachedBounds" and "view", from before the
    //   centre section was removed (2026-07-30). The knob bank it held is a row of the
    //   Controls band now, folded by "knobs", which those sessions already carry. "view"
    //   was the centre's chosen view and had migrations of its own - 2 meaning "the arp",
    //   from before the arp was a section - retired with the section they restored into.
    layout.accent = juce::jlimit(0, 7, (int) tree.getProperty("accent", 0));

    // A frame is only restored if the session actually carried one; an empty rectangle
    // means "never detached yet", which the editor reads as "centre the window".
    const auto frame = [&tree](const char* id, juce::Rectangle<int>& dest)
    {
        const auto r = juce::Rectangle<int>::fromString(tree.getProperty(id).toString());
        if (! r.isEmpty())
            dest = r;
    };
    frame("detachedBounds", layout.detachedBounds);
    frame("arpDetachedBounds", layout.arpDetachedBounds);
    frame("controlsDetachedBounds", layout.controlsDetachedBounds);
    frame("padsDetachedBounds", layout.padsDetachedBounds);
    frame("chordGenBounds", layout.chordGenBounds);
    frame("chordLibBounds", layout.chordLibBounds);
}
} // namespace keys::layoutstate
