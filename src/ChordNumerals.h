#pragma once

#include "ScaleModes.h"
#include <juce_core/juce_core.h>

// The roman numeral for a chord, in one place.
//
// Three surfaces want the same answer and had one implementation between them: SourceViz's
// Progressions diagram, which is where this logic was written and where it lived privately in an
// anonymous namespace. The generator's tray cards and the chord pads show it too now, and a
// second copy of "which degree is this, and how is it cased in this mode" is exactly the kind of
// thing that drifts - the Progressions diagram drew sixteen `?` for a whole build because one
// caller read `numeral` where every source but Markov writes `degree`, and a duplicate would put
// that same trap back once per surface.
//
// UI-free, so it unit-tests beside NoteMath.h and ChordGen.h. It takes the three fields of a
// ChordPad it actually needs rather than the struct, which lives in PluginProcessor.h and would
// drag the whole processor in behind it.
namespace keys::numerals
{
    // One scale degree, cased by the quality the mode gives it: upper for major/augmented, lower
    // for minor/diminished, with a small degree sign appended on diminished. The degree sign is
    // built from its code point rather than typed as a literal, so the file stays plain ASCII
    // regardless of the compiler's source encoding.
    inline juce::String forDegree(int degree, int modeIdx)
    {
        static const char* upper[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
        static const char* lower[] = { "i", "ii", "iii", "iv", "v", "vi", "vii" };
        const auto& qualities = modes::get(modeIdx).qualities;
        if (degree < 0 || degree >= (int) qualities.size())
            return {};
        const auto q = qualities[(size_t) degree];
        const bool major = q == modes::Quality::major || q == modes::Quality::augmented;
        juce::String s = major ? upper[degree % 7] : lower[degree % 7];
        if (q == modes::Quality::diminished)
            s += juce::String::charToString((juce::juce_wchar) 0x00B0);
        return s;
    }

    // The numeral for one chord, from whatever provenance it happens to carry. Order: the numeral
    // itself, which only the Markov source writes; else the degree it was generated from, which
    // every other source writes; else a degree worked out from its root against the current key,
    // which is all a hand-captured or hand-edited pad can offer.
    //
    // Empty when none of the three resolve - which also covers a genuinely non-diatonic root (a
    // secondary dominant, a borrowed chord) that no degree lookup will ever answer. Empty rather
    // than "?" on purpose: a card draws nothing at all there, and a "?" in the corner of every
    // hand-captured pad is noise standing in for information. A caller with room to say "I looked
    // and could not tell", like the Progressions diagram, appends its own.
    inline juce::String forChord(const juce::String& numeral, int degree, int rootPc,
                                 int keyRootPc, int modeIdx)
    {
        if (numeral.isNotEmpty())
            return numeral;

        if (degree < 0 && rootPc >= 0)
        {
            const auto& intervals = modes::get(modeIdx).intervals;
            const int iv = ((rootPc - keyRootPc) % 12 + 12) % 12;
            for (int i = 0; i < (int) intervals.size(); ++i)
                if (intervals[(size_t) i] == iv)
                {
                    degree = i;
                    break;
                }
        }

        return forDegree(degree, modeIdx);
    }
} // namespace keys::numerals
