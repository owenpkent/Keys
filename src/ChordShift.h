#pragma once

// The keybed's chord: the six fixed semitone shifts that turn one clicked key into a chord, the
// one-click shapes that fill them, and the caption the Keyboard bar's chip prints for the result.
//
// Pure and header-only for the reason `ArpLineKey.h` and `ArpRateText.h` are: the shifts are read
// on the message thread by the surface that fires the notes *and* by the chip that names them,
// and two copies of a rule with one copy on each surface is the drift this file exists to
// prevent.
//
// **The shifts are semitones, not scale degrees, and that is the ask** (2026-09-11, Owen showed
// Ableton's Chord device feeding its Scale device and said "doesn't need to stay in scale"). Keys
// could have made them diatonic - it knows the key, which is the only reason Ableton needs two
// devices where this is one - and deliberately did not: a shift that is always +4 gives you the
// major third over a minor degree, which is the sound you reached for the device to get. Scale
// Lock still governs the key you *click*, through `NoteMath::resolveOutputNote`; the shifts are
// stacked on the note that comes out of it and are never re-snapped. So Lock chooses the root and
// the shape chooses the chord, which are two different questions and now have two different
// answers.
//
// **A shift of zero is a voice that is off.** Ableton's device reads the same way and it saves a
// parameter per voice: six shifts is six parameters rather than twelve, and every one of them
// defaults to 0, so the whole feature is absent from a session saved before today without a
// migration having anything to backfill. The cost is that a voice cannot double the root at
// unison - the one chord this cannot play, and Ableton cannot play it either.

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <vector>

namespace keys::chordshift
{

// Six, because that is what the device Owen pointed at has, and because six plus the played note
// is seven voices - one more than the six-note chords the pads already store, so nothing on the
// keybed can ask for a chord the rest of Keys cannot hold.
inline constexpr int numShifts = 6;

// Ableton's own range, and wide enough for the two uses that want more than an octave: a bass
// note three octaves down, and a voice that answers three octaves up.
inline constexpr int minShift = -36;
inline constexpr int maxShift = 36;

// A shape is the six shifts written down, and clicking one writes all six. This is the "one
// click" the whole feature is named for: the manual rows below exist for the chord that is not
// on this list, not as the normal way in.
//
// Every one is spelled from the played note upward, so the note you click is always the root and
// the shape's name is the chord you hear. Unused voices are 0, which is off.
struct Shape
{
    const char* name;
    std::array<int, numShifts> shifts;
};

inline const std::array<Shape, 10>& shapes()
{
    static const std::array<Shape, 10> table {{
        { "Off",   { 0,  0,  0,  0, 0, 0 } },
        { "5th",   { 7,  0,  0,  0, 0, 0 } },
        { "Oct",   { 12, 0,  0,  0, 0, 0 } },
        { "Maj",   { 4,  7,  0,  0, 0, 0 } },
        { "Min",   { 3,  7,  0,  0, 0, 0 } },
        { "Sus2",  { 2,  7,  0,  0, 0, 0 } },
        { "Sus4",  { 5,  7,  0,  0, 0, 0 } },
        { "Maj7",  { 4,  7,  11, 0, 0, 0 } },
        { "Min7",  { 3,  7,  10, 0, 0, 0 } },
        { "Dom7",  { 4,  7,  10, 0, 0, 0 } },
    }};
    return table;
}

// Which shape these shifts are, or -1 for a set that is none of them. The chip prints a name when
// it can and a count when it cannot, and this is the one comparison both answers come from.
// Order matters only in that "Off" is index 0, so all-zero always finds it.
inline int shapeIndexFor(const std::array<int, numShifts>& shifts)
{
    const auto& table = shapes();
    for (int i = 0; i < (int) table.size(); ++i)
        if (table[(size_t) i].shifts == shifts)
            return i;
    return -1;
}

// The notes one click sends, played note first. Out-of-range voices are dropped rather than
// clamped, because a shift that runs off the top of the keyboard should go quiet, not pile a
// second voice onto note 127 where it would fight the refcount for the same pitch.
//
// A duplicate pitch is dropped for the same reason one level up: two voices landing on one note
// is one sounding note with two owners, and while `noteOn`'s refcount would survive it, the
// second voice's velocity would be thrown away silently. Better to send six notes and mean it.
inline std::vector<int> voicesFor(int playedNote, const std::array<int, numShifts>& shifts)
{
    std::vector<int> out;
    out.reserve(numShifts + 1);
    out.push_back(playedNote);

    for (const int s : shifts)
    {
        if (s == 0)
            continue; // a shift of zero is a voice that is off

        const int n = playedNote + s;
        if (n < 0 || n > 127)
            continue;
        if (std::find(out.begin(), out.end(), n) == out.end())
            out.push_back(n);
    }
    return out;
}

// How many voices these shifts actually sound, the played note included. One means the feature is
// on but every voice is off, which the chip has to be able to say: "Chord" with nothing under it
// is the state that would otherwise look broken.
inline int voiceCount(const std::array<int, numShifts>& shifts)
{
    int n = 1;
    for (const int s : shifts)
        if (s != 0)
            ++n;
    return n;
}

// What the Keyboard bar's chip prints. The shape's name when the shifts spell one, the voice count
// when they do not, and the bare word when the feature is off - so the chip answers "am I on" and
// "what will I play" in the one glance, the same two-questions-one-caption rule the arp cards'
// KEY chip follows.
inline juce::String chipText(bool on, const std::array<int, numShifts>& shifts)
{
    if (! on)
        return "Chord";

    const int idx = shapeIndexFor(shifts);
    if (idx > 0)
        return shapes()[(size_t) idx].name;
    if (idx == 0)
        return "Chord"; // on, but every voice off: nothing to name

    return juce::String(voiceCount(shifts)) + " notes";
}

// Every caption the chip can ever print, for the one measurement that sets its width. A width
// written down goes stale in silence, so the bar asks for the longest of these and a test measures
// the same list - the rule `arpkey::allChipTexts()` established for the card's KEY chip.
inline juce::StringArray allChipTexts()
{
    juce::StringArray out { "Chord" };
    for (const auto& s : shapes())
        out.add(s.name);
    for (int n = 2; n <= numShifts + 1; ++n)
        out.add(juce::String(n) + " notes");
    return out;
}

} // namespace keys::chordshift
