#pragma once

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace keys
{
// Shared note bookkeeping for every playable surface (piano, hex grid, pad grid).
//
// A surface draws "keys" identified by a drawn id (a MIDI note for the piano, a cell
// index for grids). Which notes should sound is the union of three drawn-id sets:
// `pressed` (under the active mouse gesture), `latched` (toggled on) and `sustained`
// (caught by the sustain pedal on release). refresh() diffs that union against what is
// currently on and emits exactly the delta, so notes never double-fire or stick.
//
// The output MIDI note is resolved once, at press time (outputNote), and remembered in
// `sounding`, so a note-off always matches its note-on even if octave or scale change
// while it rings.
//
// Mouse: left click plays, left drag glides (monophonic; with the pedal down the trail
// keeps ringing). Right-click toggles a per-note latch — an optional accelerator on top
// of the on-screen Latch toggle, never the only path (accessibility contract). It feeds
// the same `latched` set, so Latch-off and panic clear it; Octavium's right-latched
// notes survived panic forever, which was a bug, not a behaviour to keep.
class NoteSurface : public juce::Component
{
public:
    explicit NoteSurface(KeysProcessor&);

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // Config pushed from the editor when the matching parameters change.
    void setScaleLock(bool on, int rootPitchClass, int scaleIndex);
    void setSustain(bool on);
    void setLatch(bool on);
    void setPolyphony(int cap); // 0 = unlimited, else steal oldest beyond `cap` voices
    void panic();               // stop everything

    // The MIDI notes currently sounding (post resolution), sorted, for chord capture.
    std::vector<int> soundingOutputNotes() const;

    // Chord-pad recall: latch the drawn keys that produce these output notes, so a
    // stored chord comes back onto the surface for editing (Octavium's drag-to-edit).
    // Notes with no drawn key on the current surface are skipped.
    virtual void recallOutputNotes(const std::vector<int>& notes);

    // Supplied by the editor: current note velocity, 0..1.
    std::function<float()> getVelocity;

protected:
    virtual int drawnAt(juce::Point<float>) const = 0; // drawn id under the point, or -1
    virtual int outputNote(int drawn) const = 0;       // resolved MIDI note for a drawn id
    virtual int noteChannel() const { return 0; }      // 0 = the global channel param
    virtual int drawnForOutputNote(int note) const { juce::ignoreUnused(note); return -1; }

    void refresh(); // diff the sounding set, emit note on/off

    KeysProcessor& processor;
    bool scaleLock = false;
    int rootPc = 0, scaleIndex = 0;
    bool sustain = false, latch = false;
    int polyphonyCap = 0; // 0 = unlimited

    std::set<int> pressed;       // drawn ids under the active mouse gesture
    std::set<int> latched;       // drawn ids toggled on (Latch mode or right-click)
    std::set<int> sustained;     // drawn ids captured by the sustain pedal
    std::map<int, int> sounding; // drawn id -> output note currently on
    std::vector<int> voiceOrder; // drawn ids in the order they started (FIFO, for stealing)
    int dragDrawn = -1;
    bool rightGesture = false;   // a right-click toggle is in flight; ignore its drag/up

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteSurface)
};
} // namespace keys
