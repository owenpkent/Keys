#pragma once

namespace keys
{
// Euclidean rhythm generator (Bjorklund's algorithm, Bresenham-line formulation): spreads
// `hits` pulses as evenly as possible across `steps` slots. Pure and allocation-free, so it
// is safe to call from the audio thread as well as the message thread, even though today
// only the message thread does (see KeysProcessor::applyEuclidToActiveArpPattern).
//
// Step i is a hit iff ((i + rotation) % steps * hits) % steps < hits. `rotation` is wrapped
// into 0..steps-1 first (added to i before the mod) so a negative rotation walks the pattern
// backwards instead of producing a negative modulo.
inline bool euclidHit(int i, int hits, int steps, int rotation) noexcept
{
    if (steps < 1)
        return false;
    hits = hits < 0 ? 0 : (hits > steps ? steps : hits);
    if (hits == 0)
        return false;
    if (hits == steps)
        return true;

    long long idx = ((long long) i + (long long) rotation) % steps;
    if (idx < 0)
        idx += steps;
    return (idx * hits) % steps < hits;
}

// Fills out[0..steps) with 1 (hit) / 0 (rest). `out` must have room for at least `steps` ints.
inline void euclidFill(int* out, int hits, int steps, int rotation) noexcept
{
    for (int i = 0; i < steps; ++i)
        out[i] = euclidHit(i, hits, steps, rotation) ? 1 : 0;
}
} // namespace keys
