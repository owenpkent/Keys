#pragma once

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace keys
{
// The on-screen piano. Mouse-only: click a key to play it, drag to glide across
// keys. Chords come from Latch (click keys to toggle them on/off) or Sustain (hold
// the pedal, click several notes). No keyboard, right-click, or modifiers.
//
// Which MIDI note a key sends is resolved at press time: scale-lock snaps it to the
// nearest in-scale note, then the octave shift transposes it. The note actually
// sent is remembered so its note-off always matches, even if you change octave or
// scale while it sounds.
class PianoKeyboard : public juce::Component
{
public:
    explicit PianoKeyboard(KeysProcessor&);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // Config pushed from the editor when the matching parameters change.
    void setRange(int lowNote, int numKeys);
    void setScaleLock(bool on, int rootPitchClass, int scaleIndex);
    void setSustain(bool on);
    void setLatch(bool on);
    void panic(); // stop everything

    // The MIDI notes currently sounding (post scale-lock + octave), sorted, for chord capture.
    std::vector<int> soundingOutputNotes() const;

    // Supplied by the editor: current note velocity, 0..1.
    std::function<float()> getVelocity;

private:
    struct Key
    {
        int note;
        bool black;
        juce::Rectangle<float> bounds;
    };

    void layoutKeys();
    int keyAt(juce::Point<float>) const; // drawn note under the point, or -1
    int outputNote(int drawnNote) const; // scale-lock + octave applied, clamped
    void refresh();                      // diff the sounding set, emit note on/off

    KeysProcessor& processor;
    int lowNote = 36, numKeys = 61;
    bool scaleLock = false;
    int rootPc = 0, scaleIndex = 0;
    bool sustain = false, latch = false;

    std::vector<Key> keys;
    float keysTop = 0.0f; // y of the top of the keybed (keys anchored to the bottom)
    std::set<int> pressed;      // drawn notes under the active mouse gesture
    std::set<int> latched;      // drawn notes toggled on
    std::set<int> sustained;    // drawn notes captured by the sustain pedal
    std::map<int, int> sounding; // drawn note -> output note currently on
    int dragDrawn = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoKeyboard)
};
} // namespace keys
