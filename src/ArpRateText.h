#pragma once

// Two juce::String formatters for the arpeggiator's rate, split out of ArpEngine.h: neither is
// on the audio thread's process path, and ArpEngine.h's own top comment calls the engine
// UI-free - these are display text for a dial, not signal. `rateSyncText` reads
// ArpEngine::tupletSpace/gcdOf and `rateHzText` reads nothing beyond the engine's own numeric
// conventions, so this header includes ArpEngine.h rather than duplicating either helper:
// tupletSpace has a second, audio-thread caller (ArpEngine::tupletFactor) and gcdOf has one too
// (ArpEngine::firedCountBefore), so both stay put and this header simply reads them.

#include "ArpEngine.h"

namespace keys::arptext
{

// How a rate in Hz is written, wherever it is written: decimals by decade, so 0.031 Hz
// and 32.0 Hz both read as themselves. A fixed 2 prints the bottom octave of the range as
// "0.03", and a fixed 1 collapses the bottom *two* octaves onto "0.0" - which is how the
// slot cards came to label a running arp as a stopped one. One copy of the rule, spent by
// the arpRateHz parameter's text function and by ArpPanel's slot card; the unit suffix is
// the caller's, since the two do not space it the same way.
inline juce::String rateHzText(double hz)
{
    return juce::String(hz, hz < 1.0 ? 3 : (hz < 10.0 ? 2 : 1));
}

// How a tempo-synced rate is written under the dial: **the step length as an exact fraction
// of a bar**, one copy of the rule for the band and for every macro card.
//
// This is the one notation that survives tuplets (2026-08-03, Owen: "shouldn't it just be
// 1/5 not 1/4:5?", and he is right - `1/4:5` was invented here, which is the tell). The
// convention every DAW uses, `1/16T` and `1/16D`, has a letter for triplets and dotted and
// *no form at all* for a quintuplet; the fraction needs no letters, because a quarter-note
// quintuplet is five in the space of four quarters, which is four fifths of a beat, which
// is one fifth of a bar. So it is simply "1/5". FL Studio's grid ("1/3 beat", "1/6 beat")
// is the same system.
//
// 4/4 is assumed exactly as much as it already was: "1/4" has always meant a quarter of a
// bar here. Straight, this reproduces the division names byte for byte, which is why the
// dial reads the same as it ever did until a modifier is set.
//
// Dot stays a dot rather than folding in. It *could* - a dotted 1/8 is 3/16 of a bar - but
// "1/8." is universal and instantly read, where "3/16" has to be worked out. The tuplet is
// folded because it has no such symbol.
inline juce::String rateSyncText(int rateIndex, bool dotted, int tuplet)
{
    static constexpr int barsNum[11] = { 16, 8, 4, 2, 1, 1, 1, 1, 1, 1, 1 };
    static constexpr int barsDen[11] = {  1, 1, 1, 1, 1, 2, 4, 8, 16, 32, 64 };
    const int i = juce::jlimit(0, 10, rateIndex);
    int num = barsNum[i], den = barsDen[i];
    if (tuplet >= 2)
    {
        num *= ArpEngine::tupletSpace(tuplet);
        den *= tuplet;
    }
    // Reduce, or an 1/8 in threes prints as "2/24" instead of "1/12". Most combinations
    // come back to a unit fraction; a few honestly do not (a 1/2 in fives is two fifths of
    // a bar) and print as "2/5", which is exact and still reads as a length.
    if (const int g = ArpEngine::gcdOf(num, den); g > 1)
    {
        num /= g;
        den /= g;
    }
    juce::String s = den == 1 ? juce::String(num) + (num == 1 ? " bar" : " bars")
                              : juce::String(num) + "/" + juce::String(den);
    if (dotted)
        s << ".";
    return s;
}

} // namespace keys::arptext
