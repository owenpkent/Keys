#pragma once

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace keys
{
// Shared note bookkeeping for the piano surface (the only playable surface Keys
// builds; the base class is still generic since Keys Host reuses it unchanged).
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
// keeps ringing).
//
// Sustain and Latch are two different holds, and the difference is what a *second* click
// on a ringing key does. Sustain is a pedal: the click strikes the note again, so playing
// the same key four times over a held chord gives four attacks. Latch is a toggle: the
// click releases that note, which is how a chord gets taken apart a note at a time.
//
// Right-click is a per-note hold/release: an optional accelerator on top of the on-screen
// Latch toggle. On a key *this surface* is holding it releases that key, out of whichever
// set has it (`latched` or `sustained`, or both at once), and it leaves Sustain mode itself
// on, so a pedal-held chord can be taken apart a note at a time without lifting the pedal
// (Owen's ask, 2026-07-30). Otherwise it latches, feeding the same `latched` set, so a plain
// left click releases it and Latch-off and panic clear it; Octavium's right-latched notes
// survived panic forever, which was a bug, not a behaviour to keep.
//
// Read "this surface is holding" strictly: it only ever moves our own sets. A key can be lit
// while we hold nothing, because a chord pad, the arp, MCP or the watched MIDI input is
// sounding that pitch (see externallySounding). Right-clicking one of those takes the
// *latch* path, not the release path, because the keybed does not own that note and refcounts
// are per owner. That adds us as a second owner and changes nothing you can see or hear until
// the other owner lets go, at which point the note stays because we are still holding it. A
// left click releases it, since it is in `latched` like any other latched note.
//
// This release has no left-click twin for the `sustained` case, and that is deliberate: under
// Sustain a left click on a ringing key restrikes it, which is the whole point of Sustain
// being a pedal rather than a toggle. Owen took that trade explicitly (2026-07-30) to get
// per-note release without giving up the restrike. It is a sanctioned exception to the
// right-click-needs-a-left-click-twin rule, not an oversight; see CLAUDE.md.
class NoteSurface : public juce::Component,
                    private juce::Timer
{
public:
    explicit NoteSurface(KeysProcessor&);
    ~NoteSurface() override;

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

    // Where one drawn id sits, so a note going on or off repaints that key and not the whole
    // surface. An empty rectangle means "this surface cannot say", which falls the caller back
    // to repainting everything - the behaviour every surface had before this existed.
    virtual juce::Rectangle<int> drawnBounds(int drawn) const
    { juce::ignoreUnused(drawn); return {}; }

    void refresh(); // diff the sounding set, emit note on/off

    // Drawn ids whose output note is sounding right now but which this surface did not
    // play: MCP tools, chord pads, anything else that emits through the processor. A
    // subclass's paint() should treat these as held, AFTER checking its own pressed and
    // latched sets, so a key the user is actually holding still paints as their gesture.
    // Built from the processor's refcounts and mapped back through drawnForOutputNote,
    // so a note with no key on this surface is simply skipped, and notes already in
    // `sounding` are excluded outright (see the .cpp for why that matters).
    std::set<int> externallySounding() const;

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
    bool rightGesture = false;   // a right-click hold/release is in flight; ignore its drag/up
    bool releaseGesture = false; // this click released a sustained note; ignore its drag/up

private:
    void timerCallback() override; // polls the processor's sounding generation, repaints on change
    juce::uint32 lastSoundingGen = 0;

    // Repaint the keys whose lit state actually moved, and only those. `lastLit` maps each key
    // this surface last drew as lit to *which* of the two lit colours it drew - a set of lit keys
    // is not enough, because `pressed` paints hotter than latched / sustained / external and a key
    // moving between them leaves the set unchanged. See the .cpp.
    void repaintLitChanges();
    static constexpr int stateHeld = 1;   // latched, sustained, or sounding from elsewhere
    static constexpr int stateActive = 2; // under the mouse right now, drawn hotter
    // How far past a key's own rectangle paint() can reach: a lit black key's outer glow is a
    // 4 px stroke centred 2.5 px outside it. Anything less leaves a ring of accent behind when
    // the note goes off.
    static constexpr int litOverdrawPx = 5;
    std::map<int, int> lastLit;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteSurface)
};
} // namespace keys
