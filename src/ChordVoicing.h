#pragma once

#include "ChordGen.h"
#include "ChordSources.h"
#include "ChordSuggest.h"
#include "ScaleModes.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

// The three post-passes `ChordGenMenu` runs every generated chord through, lifted out of
// src/ui/ChordGenMenu.cpp so they can be read and unit-tested without a live KeysProcessor.
// Pure logic, no UI, no processor: it unit-tests like ChordGen.h and ChordSources.h.
//
// This is a move, not a rewrite. Every branch, threshold and comment below matches what
// ChordGenMenu.cpp did when the logic lived there as private members; the settings reads
// (parameter lookups, tick-box gates) stay behind in ChordGenMenu, which resolves them into
// the primitives these functions take. tests/ChordSourceTests.cpp used to carry a comment
// saying fitVoicing's own ordering trap could only be pinned by reproducing its shape locally,
// "because fitVoicing itself is private to a class that needs a live KeysProcessor and cannot
// be reached from a console test" -- that is the gap this header closes.
namespace keys::chordvoicing
{
    // Lean every chord's third major or minor, whatever produced it. Positive `amount` pushes
    // major, negative pushes minor, and the magnitude is the chance any given chord is pushed
    // at all, so 40 leans most of a page without flattening it into one colour. Only the third
    // moves: the root, the fifth and every extension stay, so a major ninth leaned minor is
    // still a ninth.
    inline void applyMajorMinorBias(std::vector<chordgen::Chord>& chords, int amount, juce::Random& rng)
    {
        if (amount == 0)
            return;
        const bool wantMajor = amount > 0;
        const float chance = (float) std::abs(amount) * 0.01f;

        for (auto& c : chords)
        {
            if (c.notes.empty() || rng.nextFloat() > chance)
                continue;
            for (auto& n : c.notes)
            {
                const int iv = (((n - c.rootPc) % 12) + 12) % 12;
                if (wantMajor && iv == 3 && n + 1 <= 127)
                    n += 1;
                else if (! wantMajor && iv == 4 && n - 1 >= 0)
                    n -= 1;
            }
            std::sort(c.notes.begin(), c.notes.end());

            // The chord's *label* has to follow its notes. This pass moves a third, which is
            // the one interval a type name is mostly about, so a major triad leaned minor kept
            // reading as "Major" in `Chord::type` -- and that is what `generateCandidates`
            // copies onto a pad, where Next voicing and the suggestion table both read it. The
            // name beside it is re-detected from the notes, so leaving type alone made the two
            // disagree on the same card. Only taken when the reading still calls the same note
            // the root: analyse is free to name any sounding pitch class the root, and letting
            // it move one under fitVoicing -- which stacks and shrinks a chord *around* its
            // root -- would be a worse bug than a stale type. Where it does move, both fields
            // keep the source's answer and stay consistent.
            const auto [readRoot, readType] = suggest::analyse(c.notes);
            if (readRoot == c.rootPc)
                c.type = readType;
        }
    }

    // Note count and register, applied to every chord whatever made it. These are post-passes
    // for the same reason voice leading is: they are facts about the *voicing* a chord arrives
    // in, not about which chord it is, so asking each of seven brains to honour them separately
    // would be seven places to get it wrong (Owen, 2026-08-01: "all of their options should
    // have the option for how many notes and what inversion").
    //
    // Growing a chord stacks further thirds **through the mode**, so an eleven-note chord is
    // still in the key rather than a chromatic pile. Shrinking drops from the top, which keeps
    // the root and the third and therefore keeps the chord recognisable: a dyad off a major
    // seventh should be the root and its third, not the seventh and the ninth.
    //
    //   * `noteCountRange` / `octaveRange` are the two steppers' min/max, already ordered so
    //     the low end is never above the high end;
    //   * `freeNoteCount` / `freeOctave` are true when the matching tick box is unticked, which
    //     means the roll spans the whole range the parameter can express rather than the two
    //     steppers' own reading (see the caller's own comment on why 1, not 2, is that floor);
    //   * `inversions` is the set of inversions that may be picked; empty leaves a chord in
    //     whatever rotation the grow/shrink step left it in.
    inline void fitVoicing(std::vector<chordgen::Chord>& chords, int rootPc, int modeIndex,
                            std::pair<int, int> noteCountRange, std::pair<int, int> octaveRange,
                            bool freeNoteCount, bool freeOctave, const std::vector<int>& inversions,
                            juce::Random& rng)
    {
        const auto [minN, maxN] = noteCountRange;
        const auto [minOct, maxOct] = octaveRange;
        const auto& scale = modes::get(modeIndex).intervals; // semitone offsets from the tonic

        for (auto& c : chords)
        {
            if (c.notes.empty())
                continue;

            // Unticked means the range is not a constraint at all, so the roll spans the whole
            // 1..11 the parameter can express rather than whatever the two steppers happen to
            // read.
            //
            // **1, not 2, since 2026-08-21** -- Owen's call, made the same day the floor moved
            // and against the first reading of it, which held that a bare note one time in
            // eleven would read as the tray having failed. It does not: a tray is twelve
            // candidates you sample and drag the good ones out of, so a single note among them
            // is one more thing on offer, and a page of chords with the odd pedal tone in it is
            // a *better* spread than a page with none. The rule this restores is the one that
            // was here before and is worth keeping stated: **an unticked gate rolls the whole
            // range its parameter can express**, never a hand-picked sub-range, or the tick box
            // stops meaning "you decide" and starts meaning "you decide, within limits nobody
            // wrote down".
            const int want = freeNoteCount ? 1 + rng.nextInt(11)
                                          : minN + (maxN > minN ? rng.nextInt(maxN - minN + 1) : 0);
            std::sort(c.notes.begin(), c.notes.end());

            // Root position **first**, before the note count is fitted rather than after it.
            //
            // This is the normalisation that makes an inversion *replace* whatever rotation the
            // chord arrived in instead of compounding with it, and it has to happen; the
            // ordering is the part that was wrong. `rootPosition` also collapses repeated pitch
            // classes and restacks what survives inside a single octave, so running it after
            // the grow loop threw the grow loop away: stacking thirds through a seven-note mode
            // comes back round to the root's own pitch class on the eighth note, so every count
            // above seven silently returned seven (five under a pentatonic mode), and the
            // two-octave stack the loop had just built returned as a one-octave cluster.
            // Normalise, then fit, then invert.
            auto voiced = chordgen::rootPosition(c.notes, c.rootPc);
            if (voiced.empty())
                voiced = c.notes; // a root that is not in its own chord; keep what we were given

            // Shrink: from the top, so the root and third survive. On root position, which is
            // the one arrangement where "the top" and "the extensions" are the same notes.
            if ((int) voiced.size() > want && want >= 1)
                voiced.resize((size_t) want);

            // Grow: keep stacking scale thirds above the top note. Stepping two scale degrees
            // at a time is what "a third" means inside a mode, and it is why this stays
            // diatonic where adding a flat 4 semitones would not.
            while ((int) voiced.size() < want)
            {
                const int top = voiced.back();
                int next = top + 3;
                for (int i = 0; i < 12; ++i) // find the next scale tone at least a third above
                {
                    const int norm = ((((next + i) - rootPc) % 12) + 12) % 12;
                    if (std::find(scale.begin(), scale.end(), norm) != scale.end())
                    {
                        next = next + i;
                        break;
                    }
                }
                if (next > 127 || next <= top)
                    break; // off the keyboard, or the search found nothing: stop rather than loop
                voiced.push_back(next);
            }

            // Inversion, for every source rather than the weighted pool alone: tick R alone and
            // you get root position, even from a pool that had already inverted the chord
            // itself. Last, so it rotates the chord you actually asked for rather than the one
            // the source happened to hand over.
            if (! inversions.empty())
            {
                const int inv = inversions[(size_t) rng.nextInt((int) inversions.size())];
                auto rotated = chordgen::applyInversion(voiced, inv);
                bool fits = true;
                for (const int n : rotated)
                    if (n < 0 || n > 127)
                        fits = false;
                if (fits)
                    voiced = std::move(rotated);
            }
            c.notes = std::move(voiced);

            // Register: move the whole chord so its lowest note sits in an octave inside the
            // range. The chord moves in one piece, so its shape and its voice leading are
            // untouched.
            const int wantOct = freeOctave ? 2 + rng.nextInt(5)
                                           : minOct + (maxOct > minOct ? rng.nextInt(maxOct - minOct + 1) : 0);
            const int haveOct = c.notes.front() / 12;
            const int shift = (wantOct - haveOct) * 12;
            if (shift != 0)
            {
                bool fits = true;
                for (const int n : c.notes)
                    if (n + shift < 0 || n + shift > 127)
                        fits = false;
                if (fits)
                    for (auto& n : c.notes)
                        n += shift;
            }
        }
    }

    // The whole post-pass pipeline, in the one order that is correct. `leanAmount` is genMajMin,
    // `smoothAmount` is genSmooth/100, and the rest are fitVoicing's own inputs (see above).
    //
    // Bias first, because it changes *which* notes a chord holds while the other two only move
    // them about -- leaning a third has to land before anything reasons about note count or
    // register, or it would be biasing whatever fitVoicing left behind rather than the chord a
    // source actually produced.
    //
    // Fit second, smooth last, because fitVoicing moves whole chords between octaves (the
    // Register step above) and voice leading's whole job is choosing which octave each chord
    // sits in relative to the one before it. Smoothing before the register step would have its
    // careful placement immediately undone; smoothing has to see fitVoicing's final registers
    // to have anything to smooth.
    //
    // And inside fitVoicing itself, root position has to run before the note count is grown or
    // shrunk (see that function's own comment): it is the normalisation that makes an inversion
    // replace a chord's rotation rather than compound it, but it also collapses repeated pitch
    // classes, so running it after the grow loop throws the grow loop's extra notes away.
    inline void applyVoicingPipeline(std::vector<chordgen::Chord>& chords, int leanAmount,
                                      int rootPc, int modeIndex, std::pair<int, int> noteCountRange,
                                      std::pair<int, int> octaveRange, bool freeNoteCount, bool freeOctave,
                                      const std::vector<int>& inversions, float smoothAmount,
                                      juce::Random& rng)
    {
        applyMajorMinorBias(chords, leanAmount, rng);
        fitVoicing(chords, rootPc, modeIndex, noteCountRange, octaveRange, freeNoteCount, freeOctave,
                   inversions, rng);
        sources::applyVoiceLeading(chords, smoothAmount);
    }
} // namespace keys::chordvoicing
