#pragma once

#include "NoteSurface.h"

namespace keys
{
// The on-screen piano. Mouse-only: click a key to play it, drag to glide across
// keys. Chords come from Latch (click keys to toggle them on/off), Sustain (hold
// the pedal, click several notes), or right-click (optional per-note latch).
//
// Which MIDI note a key sends is resolved at press time: scale-lock snaps it to the
// nearest in-scale note, then the octave shift transposes it. The note actually
// sent is remembered so its note-off always matches, even if you change octave or
// scale while it sounds. All of that lives in NoteSurface; this class is the piano
// geometry and paint.
class PianoKeyboard : public NoteSurface
{
public:
    explicit PianoKeyboard(KeysProcessor&);

    void paint(juce::Graphics&) override;
    void resized() override;

    void setRange(int lowNote, int numKeys);

    // How tall a white key may grow. Docked, the keybed keeps piano proportions and
    // extra height reads as instrument body; detached into its own window the whole
    // point is that dragging the window resizes the keys, so the cap comes off.
    void setKeyHeightCap(float px);

protected:
    int drawnAt(juce::Point<float>) const override;  // drawn note under the point, or -1
    int outputNote(int drawnNote) const override;    // scale-lock + octave applied, clamped
    int drawnForOutputNote(int note) const override; // undo the octave; -1 if off the keybed

private:
    struct Key
    {
        int note;
        bool black;
        juce::Rectangle<float> bounds;
    };

    void layoutKeys();

    int lowNote = 36, numKeys = 61;
    float keyHeightCap = 185.0f;
    std::vector<Key> keys;
    float keysTop = 0.0f; // y of the top of the keybed (keys anchored to the bottom)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoKeyboard)
};
} // namespace keys
