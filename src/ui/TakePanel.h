#pragma once

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

namespace keys
{
// The take, before it leaves.
//
// REC writes a file the moment you stop, which is what stops a take being something a click can
// lose - but a file you cannot see is a file you have to trust. Scaler keeps its capture as an
// editable roll inside the plugin for exactly this reason, and the gap between "here is a file"
// and "here is what you played" is the whole of what this window closes.
//
// It is a *view*, not an editor. Nothing here changes the take: it draws what
// KeysProcessor::takeNotes() reports - which is built from the same MidiFile that was written,
// so the picture is the bytes - and offers the three ways out. Editing a take belongs in a
// piano roll, and Keys has a sibling for that (Lattice); building half of one here would be a
// second, worse one.
//
// Like ChordGenPanel it is not a Section: it never docks, so it has no bar, fold or caption, and
// it reuses DetachedWindow. Unlike ChordGenPanel its bounds are *not* kept in LayoutState - a
// take is transient, so a session that reopens with this window up would be reopening onto a
// take that no longer exists.
class TakePanel : public juce::Component
{
public:
    explicit TakePanel(KeysProcessor&);

    void paint(juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;

    // Called by the editor's timer, so a take recorded while this is open appears in it.
    void refresh();

    static juce::Point<int> minWindowSize() { return { 560, 320 }; }
    static juce::Point<int> defaultWindowSize() { return { 760, 420 }; }

    // The one place the external file drag is spelled out, shared with the Keyboard bar's take
    // chip so the two can never disagree about what a drag hands over. `canMoveFiles` is false:
    // Live copies a dropped .mid into the set, and moving it would empty the takes folder the
    // moment it was used - the opposite of what one fixed folder is for.
    static void dragTakeOut(juce::Component* source, const juce::File&);

private:
    // The roll. Read-only and click-through to nothing: its only gesture is the drag that hands
    // the take over, which is why it is the drag source rather than a button beside it - the
    // thing you are dragging is the thing you can see.
    struct Roll : public juce::Component
    {
        explicit Roll(TakePanel& o) : owner(o) {}
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;

        TakePanel& owner;
        bool wasDrag = false;
    };

    void saveAs();

    KeysProcessor& processor;
    // Exactly what the file holds, lengths included: `notes` carries no minimum length, so a
    // note shorter than a pixel is floored by Roll::paint when it draws and nowhere else.
    std::vector<KeysProcessor::TakeNote> notes;
    // What `notes` was built from, so the 30 Hz refresh can tell "same take" from "new take"
    // without rebuilding the MidiFile to find out.
    juce::File shownFile;
    int shownEvents = -1;
    double span = 0.0; // seconds, first note to last release
    int lowNote = 60, highNote = 72;

    Roll roll { *this };
    juce::Label stats;
    juce::TextButton saveButton { "Save MIDI as..." };
    juce::TextButton revealButton { "Show in Explorer" };
    juce::TextButton closeButton { "Close" };
    std::unique_ptr<juce::FileChooser> chooser; // outlives saveAs(); launchAsync is not modal

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TakePanel)
};
} // namespace keys
