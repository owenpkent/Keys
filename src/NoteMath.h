#pragma once

#include <okstudio/Scales.h>
#include <juce_core/juce_core.h>

namespace keys
{
// Which MIDI note a drawn key sends: scale-lock snap (when on), then octave
// transpose, clamped to the MIDI range. Pure and free of JUCE UI types so it can be
// unit-tested directly (tests/KeysTests.cpp). PianoKeyboard::outputNote calls this.
inline int resolveOutputNote(int drawnNote, bool scaleLock, int rootPitchClass, int scaleIndex, int octaveShift)
{
    int n = drawnNote;
    if (scaleLock)
        n = okstudio::scales::snapToScale(n, rootPitchClass, scaleIndex);
    n += 12 * octaveShift;
    return juce::jlimit(0, 127, n);
}
} // namespace keys
