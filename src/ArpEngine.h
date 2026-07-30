#pragma once

#include "ChanceEngine.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <random>

namespace keys
{
// The arpeggiator core: pure, UI-free, and allocation/lock-free on the audio
// thread (see docs/ARP_DESIGN.md). It consumes note on/off from the block's merged
// MIDI stream as its input (keyboard, latch, chord pads all arrive the same way),
// holds the sounding set, and emits its own stream; everything else in the stream
// passes through untouched.
//
// Lane data is written by the editor on the message thread and read here on the
// audio thread; every cell is an atomic int, so there is never a lock and never a
// torn read that matters (a half-updated pattern is just a pattern mid-edit).
//
// Timing: with a playing host transport, step boundaries are derived fresh every
// block from ppqPosition and bpm, so tempo changes and transport jumps self-correct
// (no accumulating counters). Anchored mode affixes steps to the host bar grid;
// free mode (and the internal clock used when the transport is stopped or absent)
// advances its own phase and is allowed to drift, by design.
class ArpEngine
{
public:
    static constexpr int maxSteps = 32;
    static constexpr int maxHeld = 16;
    static constexpr int maxActive = 64; // sounding arp notes awaiting their note-off
    static constexpr int numLanes = 6;

    enum Lane { laneNote = 0, laneOctave, laneVelocity, laneGate, laneRatchet, laneProbability };

    // The value each lane holds when it is doing nothing. Also what every lane reads as
    // while Params::usePattern is false, which is how "Shape: Up" behaves like a plain
    // arpeggiator even after the step lanes have been edited.
    static constexpr int laneDefaults[numLanes] = { 0, 0, 100, 100, 1, 100 };

    // Per-step values, editor-writable. Meanings per lane:
    //   note:        0 = follow the direction mode, 1..8 = fixed chord-note index,
    //                -1 = muted step
    //   octave:      -3..+3 (added octaves)
    //   velocity:    10..200 (% of the played velocity)
    //   gate:        5..200 (% of the step length; >100 overlaps into the next step)
    //   ratchet:     1..4 sub-hits within the step
    //   probability: 0..100 (% chance the step fires)
    struct Lanes
    {
        std::array<std::array<std::atomic<int>, maxSteps>, numLanes> value;
        std::array<std::atomic<int>, numLanes> length;   // 1..32
        std::array<std::atomic<int>, numLanes> clockDiv; // 0 = every step, 1 = every 2nd, 2 = every 4th

        Lanes() { resetToDefaults(); }

        void resetToDefaults()
        {
            for (int l = 0; l < numLanes; ++l)
            {
                for (auto& v : value[(size_t) l])
                    v.store(laneDefaults[l], std::memory_order_relaxed);
                length[(size_t) l].store(8, std::memory_order_relaxed);
                clockDiv[(size_t) l].store(0, std::memory_order_relaxed);
            }
        }
    };

    enum class Direction { up = 0, down, upDown, downUp, upAndDown, downAndUp, asPlayed, asPlayedReverse };
    static constexpr int numDirections = 8; // keep in step with Direction; the Shape combo lists these then "Pattern"

    struct Params
    {
        bool enabled = false;
        int rateIndex = 8;        // index into rateInBeats(), default 1/16
        bool dotted = false;
        bool triplet = false;
        bool anchored = true;     // affixed to the host bar grid vs free-running
        Direction direction = Direction::up;
        bool usePattern = false;  // false: plain arpeggiator, the step lanes are not read
        int octaveRange = 1;      // 1..4, for direction modes
        // -0.75..0.75, shifts the odd (offbeat) steps by that fraction of a step: positive
        // late, the usual shuffle; negative early, which rushes them on top of the beat.
        float swing = 0.0f;
        // Gate and Chance as globals, not only as step lanes. The lanes are gated behind
        // Shape == "Pattern", so on a plain shape there was no way to shorten a note or
        // thin a run out at all; these multiply the lane value, so 100 leaves an edited
        // pattern exactly as drawn and the two controls mean the same thing either way.
        int gate = 100;           // 5..200 (% of the lane's own gate)
        int chance = 100;         // 0..100 (% of the lane's own probability)
        bool latch = false;
        bool retrigger = true;    // restart at step 1 when a note arrives on an empty set
        double fallbackBpm = 120.0; // internal clock when the transport is stopped/absent

        // Chance, the third note source (docs/CHANCE_DESIGN.md). With it enabled the step
        // lanes and the direction walk are both bypassed, because only one source can own a
        // step; everything downstream of the decision is shared. `harmony` is the pitch-class
        // weight table it selects against, rebuilt on the message thread from Root, Mode and
        // the live chord, and copied in here per block.
        ChanceEngine::Params chanceParams;
        ChanceEngine::Harmony harmony;
    };

    struct HostClock
    {
        bool playing = false;
        bool hasPpq = false;
        double ppq = 0.0;  // position at the start of the block, in quarter notes
        double bpm = 120.0;
    };

    Lanes lanes;
    // Public so the processor can hand it a new seed (Generate) without going through Params:
    // a seed is state, not an automatable value.
    ChanceEngine chance;

    void prepare(double sampleRate)
    {
        sr = sampleRate > 0 ? sampleRate : 44100.0;
        hardReset();
    }

    // Silence everything the arp currently owes, immediately (offset 0 of the next
    // buffer it is given). Call when bypassing or on a transport jump.
    void flushInto(juce::MidiBuffer& out)
    {
        for (int i = 0; i < activeCount; ++i)
            out.addEvent(juce::MidiMessage::noteOff(active[(size_t) i].channel, active[(size_t) i].note), 0);
        activeCount = 0;
        pendingCount = 0; // un-fired ratchet hits must not survive a bypass or a transport jump
    }

    void hardReset()
    {
        activeCount = 0;
        pendingCount = 0;
        heldCount = 0;
        physicallyHeld = 0;
        stepCounter = 0;
        dirCursor = 0;
        freePhaseBeats = 0.0;
        havePrevPpq = false;
    }

    // Process one block. `midi` is the merged stream from the collector (and host);
    // note on/off are consumed as arp input, everything else passes through into
    // `out`. The arp's own notes are added to `out` at sample-accurate offsets.
    // `out` must be a different buffer to `midi`.
    void process(const Params& p, const HostClock& clock, int numSamples,
                 const juce::MidiBuffer& midi, juce::MidiBuffer& out)
    {
        // Consume input notes; pass through the rest.
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
                noteArrived(m.getNoteNumber(), m.getFloatVelocity(), m.getChannel(), p);
            else if (m.isNoteOff())
                noteLeft(m.getNoteNumber(), p);
            else
                out.addEvent(m, meta.samplePosition);
        }

        const double bpm = (clock.playing && clock.bpm > 0) ? clock.bpm : p.fallbackBpm;
        const double beatsPerSample = bpm / 60.0 / sr;
        const double stepBeats = stepLengthBeats(p);
        const double blockBeats = beatsPerSample * numSamples;

        // Position in beats at the start of this block.
        double pos;
        if (clock.playing && clock.hasPpq && p.anchored)
        {
            pos = clock.ppq;
            // A jump (loop, relocate) means owed note-offs' timelines are invalid.
            if (havePrevPpq && std::abs(clock.ppq - expectedNextPpq) > blockBeats + 1.0e-3)
            {
                flushInto(out);
                // A looped bar has to sound the same on its second pass, so Chance restarts
                // from its seed rather than carrying a stale loop position over the jump.
                // Marbles never needs this because hardware has no transport; a plugin does.
                chance.resync();
            }
            havePrevPpq = true;
            expectedNextPpq = clock.ppq + blockBeats;
            freePhaseBeats = clock.ppq; // keep free phase seeded for a later stop
        }
        else
        {
            pos = freePhaseBeats;
            freePhaseBeats += blockBeats;
            havePrevPpq = false;
        }

        // Owed note-offs are NOT retired up front. Each hit closes what it lands on top of
        // itself, immediately before its own note-on (see fireStep), because a tie has to be
        // pulled back to just before the pitch retriggers rather than allowed to fire in the
        // middle of the note that replaced it. Draining here instead put a tie's note-off
        // *after* the note-on that superseded it: two note-ons for one pitch with nothing in
        // between, which hangs a voice on any synth that allocates per note-on.
        if (p.enabled && heldCount > 0 && stepBeats > 0.0)
        {
            // Fire every step whose *fire time* lands inside [pos, pos + blockBeats). Not
            // every boundary in that range: swing shifts the offbeats either way, so with a
            // negative swing a step is pulled in front of its own boundary and can belong to
            // the block before it. Walking from one step back and testing the fire time is
            // what makes early swing possible at all - the old ceil-from-pos loop dropped
            // any step it pulled behind the block start, which silenced every other note.
            // Costs at most one extra iteration per block, since |swing| < 1 keeps fire
            // times monotonic and each step therefore still fires in exactly one block.
            double nextIndexF = std::floor(pos / stepBeats + 1.0e-9) - 1.0;
            for (;;)
            {
                const double boundaryBeats = nextIndexF * stepBeats;
                const long long globalStep = (long long) llround(nextIndexF);
                double fireBeats = boundaryBeats;
                if ((globalStep & 1) != 0)
                    fireBeats += stepBeats * p.swing; // swing shifts the offbeats
                const double offsetBeats = fireBeats - pos;
                const int offset = (int) std::floor(offsetBeats / beatsPerSample);
                if (offset >= numSamples)
                    break;
                if (offset >= 0)
                {
                    // Any ratchet sub-hit carried in from an earlier block that is due before
                    // this step goes first, so hits reach emitHit in time order.
                    firePendingBefore(offset, numSamples, out);
                    fireStep(p, globalStep, offset, stepBeats / beatsPerSample, numSamples, out);
                }
                nextIndexF += 1.0;
            }

            firePendingBefore(numSamples, numSamples, out); // the rest of this block's sub-hits
        }
        else
        {
            // Bypassed, or the keys came up. A ratchet is one gesture: its remaining sub-hits
            // die with the step that decided them rather than firing into a silence, and
            // dropping them here is also what keeps them from surfacing a block later.
            pendingCount = 0;
        }

        // Whatever is still owed inside this block, then rebase the survivors onto the next
        // block's timebase. Deliberately outside the guard above: a note owed by the last
        // step before the keys came up, or before the arp was switched off, still has to end
        // on time instead of hanging until the next flush.
        retireDue(numSamples - 1, out);
        advanceBlock(numSamples);
    }

    int heldNoteCount() const noexcept { return heldCount; }

private:
    // `ons` counts how many un-matched note-ons this pitch has arrived with. The engine sits
    // downstream of a merged stream where several chord sources can each ask for the same
    // pitch, so "is it still held" is a count, not a flag. It used to be a flag plus a
    // separate physicallyHeld total, and a duplicated pitch leaked that total above zero
    // permanently - which disabled latch's fresh-chord reset and left chords arpeggiating
    // forever, stacking every card you handed it afterwards.
    struct Held { int note; float velocity; int channel; int ons; };
    struct Active { int note; int channel; int samplesLeft; };
    // A ratchet sub-hit whose fire time landed past the end of the block that decided it.
    // `at` is in the same frame as Active::samplesLeft and is rebased by the same
    // advanceBlock(); see the contract note below.
    struct PendingHit { int note; int channel; float velocity; int at; int durSamples; };

    double stepLengthBeats(const Params& p) const
    {
        static constexpr double base[11] = { 64.0, 32.0, 16.0, 8.0, 4.0,   // 16,8,4,2,1 bars
                                             2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 }; // 1/2..1/64
        double b = base[juce::jlimit(0, 10, p.rateIndex)];
        if (p.dotted)  b *= 1.5;
        if (p.triplet) b *= 2.0 / 3.0;
        return b;
    }

    void noteArrived(int note, float vel, int channel, const Params& p)
    {
        // Latch with nothing physically held: a fresh chord starts over.
        if (p.latch && physicallyHeld == 0 && heldCount > 0)
        {
            heldCount = 0;
            dirCursor = 0;
        }
        if (heldCount == 0 && p.retrigger)
        {
            stepCounter = 0;
            dirCursor = 0;
        }
        for (int i = 0; i < heldCount; ++i)
            if (held[(size_t) i].note == note)
            {
                // Same pitch arriving twice: count it, do not add a second entry. The
                // matching note-off will decrement, so the two stay paired.
                held[(size_t) i].velocity = vel;
                if (held[(size_t) i].ons++ == 0)
                    ++physicallyHeld; // it was latched-but-released; it is physical again
                return;
            }
        if (heldCount < maxHeld)
        {
            held[(size_t) heldCount++] = { note, vel, channel, 1 };
            ++physicallyHeld;
        }
    }

    void noteLeft(int note, const Params& p)
    {
        // Match on pitch whatever its count: matching only entries still marked physical was
        // what dropped the count on the floor when two sources owned one pitch, leaving
        // physicallyHeld stuck above zero for the life of the plugin.
        for (int i = 0; i < heldCount; ++i)
            if (held[(size_t) i].note == note && held[(size_t) i].ons > 0)
            {
                if (--held[(size_t) i].ons > 0)
                    return; // another owner still holds this pitch
                physicallyHeld = juce::jmax(0, physicallyHeld - 1);
                if (! p.latch)
                {
                    for (int j = i; j < heldCount - 1; ++j)
                        held[(size_t) j] = held[(size_t) j + 1];
                    --heldCount;
                }
                return;
            }
    }

    // The one place lane data is read, so it is also the one place the pattern gate
    // belongs: with usePattern false every lane reads as its default and the arp runs
    // as a plain shape, leaving edited step data untouched and waiting.
    int laneValue(const Params& p, Lane l, long long globalStep) const
    {
        if (! p.usePattern)
            return laneDefaults[l];

        const auto li = (size_t) l;
        const int div = lanes.clockDiv[li].load(std::memory_order_relaxed);
        const int len = juce::jlimit(1, maxSteps, lanes.length[li].load(std::memory_order_relaxed));
        const long long idx = (globalStep >> juce::jlimit(0, 2, div)) % len;
        return lanes.value[li][(size_t) idx].load(std::memory_order_relaxed);
    }

    // Sorted-note order for direction modes (as-played uses arrival order).
    void buildSequence(const Params& p)
    {
        seqCount = 0;
        int order[maxHeld];
        for (int i = 0; i < heldCount; ++i)
            order[i] = i;
        const bool sortByPitch = p.direction != Direction::asPlayed
                              && p.direction != Direction::asPlayedReverse;
        if (sortByPitch)
            for (int i = 1; i < heldCount; ++i)
                for (int j = i; j > 0 && held[(size_t) order[j]].note < held[(size_t) order[j - 1]].note; --j)
                    std::swap(order[j], order[j - 1]);

        const int octs = juce::jlimit(1, 4, p.octaveRange);
        for (int o = 0; o < octs; ++o)
            for (int i = 0; i < heldCount && seqCount < (int) seq.size(); ++i)
                seq[(size_t) seqCount++] = { order[i], o * 12 };
    }

    // The sequence is always built ascending (or in arrival order); directions are
    // realized here. Exclusive ping-pong (upDown/downUp) plays the endpoints once
    // per cycle; inclusive (upAndDown/downAndUp) repeats them, Cthulhu-style.
    int nextDirectionIndex(const Params& p)
    {
        const int n = seqCount;
        if (n <= 1)
            return 0;
        switch (p.direction)
        {
            case Direction::up:
            case Direction::asPlayed:
                return (int) (dirCursor++ % n);
            case Direction::down:
            case Direction::asPlayedReverse:
                return n - 1 - (int) (dirCursor++ % n);
            case Direction::upDown:
            case Direction::downUp:
            {
                const int period = 2 * (n - 1);
                const int c = (int) (dirCursor++ % period);
                const int i = c < n ? c : period - c;
                return p.direction == Direction::upDown ? i : n - 1 - i;
            }
            case Direction::upAndDown:
            case Direction::downAndUp:
            {
                const int period = 2 * n;
                const int c = (int) (dirCursor++ % period);
                const int i = c < n ? c : period - 1 - c;
                return p.direction == Direction::upAndDown ? i : n - 1 - i;
            }
        }
        return 0;
    }

    void fireStep(const Params& p, long long globalStep, int offset, double stepSamplesF,
                  int numSamples, juce::MidiBuffer& out)
    {
        if (p.chanceParams.enabled)
        {
            fireChanceStep(p, globalStep, offset, stepSamplesF, numSamples, out);
            return;
        }

        const int noteVal = laneValue(p, laneNote, globalStep);
        if (noteVal < 0)
            return; // muted step
        const int chance = laneValue(p, laneProbability, globalStep)
                         * juce::jlimit(0, 100, p.chance) / 100;
        if ((int) (rng() % 100u) >= chance)
            return; // 100 always fires, 0 never does

        buildSequence(p);
        if (seqCount == 0)
            return;

        const int seqIndex = noteVal >= 1 ? (noteVal - 1) % seqCount // fixed index, wraps politely
                                          : nextDirectionIndex(p);

        const auto& entry = seq[(size_t) juce::jlimit(0, seqCount - 1, seqIndex)];
        const auto& src = held[(size_t) juce::jlimit(0, heldCount - 1, entry.heldIndex)];

        const int note = juce::jlimit(0, 127,
                                      src.note + entry.octaveOffset
                                          + 12 * juce::jlimit(-3, 3, laneValue(p, laneOctave, globalStep)));
        const float vel = juce::jlimit(0.05f, 1.0f,
                                       src.velocity * (float) laneValue(p, laneVelocity, globalStep) / 100.0f);
        const int ratchets = juce::jlimit(1, 4, laneValue(p, laneRatchet, globalStep));
        const double gate = juce::jlimit(5, 200, laneValue(p, laneGate, globalStep))
                          * juce::jlimit(5, 200, p.gate) / 10000.0;

        const double subLen = stepSamplesF / ratchets;
        for (int r = 0; r < ratchets; ++r)
        {
            const int on = offset + (int) std::floor(subLen * r);
            const int durSamples = juce::jmax(1, (int) std::floor(subLen * gate));

            // A ratchet subdivides one step, and a step is routinely longer than a buffer -
            // at 1/16 and 120 bpm a step is 6000 samples against a 512-sample block - so every
            // sub-hit but the first normally belongs to a *later* block. They used to be
            // stamped at their raw offset anyway, which puts a note-on at sample 4500 of a
            // 512-sample buffer: out of range, dropped or clamped by whatever is downstream,
            // and ratchets therefore silently did nothing at any realistic buffer size. The
            // mistake is deciding an event belongs to this block because the thing that
            // *caused* it did.
            //
            // So park what does not fit and let the next block fire it. The step itself is
            // still decided exactly once, here: the RNG draw, the sequence walk and
            // stepCounter must not be repeated, which is why this parks the resolved hit
            // rather than re-deriving the step later.
            if (on < numSamples)
                emitHit(note, src.channel, vel, on, durSamples, numSamples, out);
            else if (pendingCount < maxPending)
                pending[(size_t) pendingCount++] = { note, src.channel, vel, on, durSamples };
            // else: out of carry slots, and the hit is dropped rather than mistimed. A ratchet
            // parks at most three, and a step's carry is drained before the next step fires,
            // so sixteen is five steps' worth in flight at once - which no rate reaches.
        }
        ++stepCounter;
    }

    // Chance owns the whole step: whether it fires, which members of the pool sound, and
    // their velocity, gate, ratchet count and timing nudge. So neither the step lanes nor the
    // direction walk is read here - they are the other two note sources, and a step has one
    // owner. What is deliberately *not* rewritten is everything downstream: emitHit, the
    // pending[] carry and active[] are shared with the plain path, because that is where the
    // close-what-you-land-on rule and three separate stuck-note fixes live.
    void fireChanceStep(const Params& p, long long globalStep, int offset, double stepSamplesF,
                        int numSamples, juce::MidiBuffer& out)
    {
        buildSequence(p);
        if (seqCount == 0)
            return;

        // Resolve the pool once, as absolute pitches: Chance weights candidates by pitch
        // class, so it needs the notes rather than indices into the held set.
        for (int i = 0; i < seqCount; ++i)
        {
            const auto& e = seq[(size_t) i];
            poolPitches[(size_t) i] = juce::jlimit(0, 127,
                                                   held[(size_t) e.heldIndex].note + e.octaveOffset);
        }

        // The global Chance knob keeps working with this source on, by thinning Density. The
        // alternatives were both worse: leaving it dead is a control that lies, and taking a
        // second draw for it would have to come from somewhere outside the step bundle, which
        // is exactly what makes a locked loop stop repeating.
        ChanceEngine::Params cp = p.chanceParams;
        cp.density = cp.density * juce::jlimit(0, 100, p.chance) / 100;

        // Once per step, whatever it decides, so the loop stays in phase with the grid.
        const auto d = chance.advance(cp, p.harmony, poolPitches.data(), seqCount,
                                      (juce::int64) globalStep);
        ++stepCounter;
        if (! d.fires || d.voices <= 0)
            return;

        const int jitterSamples = (int) std::llround(d.jitterFrac * stepSamplesF);
        const int ratchets = juce::jlimit(1, 4, d.ratchets);
        const double gate = juce::jlimit(0.05f, 2.0f, d.gateScale)
                          * juce::jlimit(5, 200, p.gate) / 100.0;
        const double subLen = stepSamplesF / ratchets;

        for (int v = 0; v < d.voices; ++v)
        {
            const int idx = juce::jlimit(0, seqCount - 1, d.poolIndex[(size_t) v]);
            const auto& src = held[(size_t) juce::jlimit(0, heldCount - 1, seq[(size_t) idx].heldIndex)];
            const int note = poolPitches[(size_t) idx];
            const float vel = juce::jlimit(0.05f, 1.0f, src.velocity * d.velocityScale);

            for (int r = 0; r < ratchets; ++r)
            {
                // Jitter rides on the same offset the ratchet does, so a nudged hit that falls
                // past the block edge parks in pending[] exactly like a ratchet sub-hit and
                // fires from the next block. Early nudges clamp at 0 rather than reaching back
                // into a block already gone.
                const int on = juce::jmax(0, offset + jitterSamples + (int) std::floor(subLen * r));
                const int durSamples = juce::jmax(1, (int) std::floor(subLen * gate));

                if (on < numSamples)
                    emitHit(note, src.channel, vel, on, durSamples, numSamples, out);
                else if (pendingCount < maxPending)
                    pending[(size_t) pendingCount++] = { note, src.channel, vel, on, durSamples };
            }
        }
    }

    // One ratchet hit: close what it lands on top of, emit its note-on, and park its note-off
    // in active[]. Shared by fireStep and by the carry-over drain, so a sub-hit that waited a
    // block behaves identically to one that fired immediately.
    void emitHit(int note, int channel, float vel, int on, int durSamples, int numSamples,
                 juce::MidiBuffer& out)
    {
        // Close whatever this hit lands on top of, before its note-on goes in, so a
        // note-off can never sort after a note-on it precedes. Two different closes:
        //   - anything already due by now ends at its own offset, on time;
        //   - the pitch being retriggered, if it is still owed *past* this hit (a tie,
        //     gate > 100%), is pulled back to just before the retrigger instead, so one
        //     pitch never stacks two note-ons with nothing between them.
        for (int i = 0; i < activeCount;)
        {
            auto& a = active[(size_t) i];
            if (a.samplesLeft <= on)
            {
                out.addEvent(juce::MidiMessage::noteOff(a.channel, a.note),
                             juce::jmax(0, a.samplesLeft));
                a = active[(size_t) --activeCount];
            }
            else if (a.note == note && a.channel == channel)
            {
                out.addEvent(juce::MidiMessage::noteOff(a.channel, a.note), juce::jmax(0, on - 1));
                a = active[(size_t) --activeCount];
            }
            else
                ++i;
        }
        out.addEvent(juce::MidiMessage::noteOn(channel, note, vel), on);

        // Where this note ends, as an offset from the start of *this* block, which is
        // the frame active[] is kept in all the way through process(); advanceBlock()
        // rebases the survivors once, at the very end.
        const int offAt = on + durSamples;
        if (activeCount < maxActive)
        {
            // Park it whatever its length. Emitting a short note's off straight into the
            // buffer looks like a harmless shortcut and is not: it hides the note from
            // the close loop above, so a same-pitch hit later in the SAME block finds
            // nothing to close and stacks a second note-on with nothing between them -
            // exactly what that loop exists to prevent. It needs a tie (gate > 100%) and
            // a block long enough to hold two hits of one pitch, which is an ordinary
            // 2048-sample buffer at a fast rate.
            //
            // Nothing is lost by parking: a later hit either ends this note at its own
            // gate (the due branch) or, if it is a tie, pulls it back under the retrigger
            // (the tie branch), and retireDue() at the end of process() emits whatever is
            // still owed inside this block.
            active[(size_t) activeCount++] = { note, channel, offAt };
        }
        else
        {
            // Out of tracking slots: end it no later than the edge of this block, rather
            // than stamping an event past the end of the buffer.
            out.addEvent(juce::MidiMessage::noteOff(channel, note),
                         juce::jmax(0, juce::jmin(offAt, numSamples - 1)));
        }
    }

    // Fire every carried-over ratchet hit due strictly before `limit`, in order. Called from
    // the step loop with the next step's offset, so a sub-hit and a step landing in the same
    // block still reach active[] in time order - which is what the close-what-you-land-on
    // rule in emitHit depends on.
    void firePendingBefore(int limit, int numSamples, juce::MidiBuffer& out)
    {
        int kept = 0;
        for (int i = 0; i < pendingCount; ++i)
        {
            const auto& h = pending[(size_t) i];
            if (h.at < limit)
                emitHit(h.note, h.channel, h.velocity, h.at, h.durSamples, numSamples, out);
            else
                pending[(size_t) kept++] = h; // shift down: pending stays in time order
        }
        pendingCount = kept;
    }

    // THE CONTRACT for active[].samplesLeft: it is the note-off's sample offset measured
    // from the start of the block currently being processed. fireStep writes it in that
    // frame, every read during the block is in that frame, and advanceBlock() rebases the
    // survivors exactly once, at the end of process(). Getting this frame wrong is what
    // shipped every arp note-off one whole buffer late for the life of v1.

    // Emit the note-off for every entry due at or before `limit` (an offset into the block
    // being processed), each at its own offset, and drop it. Entries not yet due are left
    // untouched, so this is safe to call as often as a block needs: an entry is emitted once
    // and removed with it, so there is no double-off and nothing leaks.
    void retireDue(int limit, juce::MidiBuffer& out)
    {
        for (int i = 0; i < activeCount;)
        {
            auto& a = active[(size_t) i];
            if (a.samplesLeft <= limit)
            {
                out.addEvent(juce::MidiMessage::noteOff(a.channel, a.note), juce::jmax(0, a.samplesLeft));
                a = active[(size_t) --activeCount];
            }
            else
                ++i;
        }
    }

    // Rebase the survivors onto the next block's timebase. Called once per block, right
    // after retireDue(numSamples - 1), so every survivor is due at numSamples or later and
    // samplesLeft stays >= 0. numSamples == 0 is a no-op, as it must be.
    void advanceBlock(int numSamples) noexcept
    {
        for (int i = 0; i < activeCount; ++i)
            active[(size_t) i].samplesLeft -= numSamples;
        // Carried ratchet hits live in the same frame and rebase with it. firePendingBefore
        // has already fired everything below numSamples, so every survivor stays >= 0.
        for (int i = 0; i < pendingCount; ++i)
            pending[(size_t) i].at -= numSamples;
    }

    struct SeqEntry { int heldIndex; int octaveOffset; };

    double sr = 44100.0;
    std::array<Held, maxHeld> held {};
    int heldCount = 0;
    int physicallyHeld = 0;
    std::array<Active, maxActive> active {};
    int activeCount = 0;

    static constexpr int maxPending = 16; // ratchet sub-hits carried into a later block
    std::array<PendingHit, maxPending> pending {};
    int pendingCount = 0;
    std::array<SeqEntry, maxHeld * 4> seq {};
    // seq[] resolved to absolute pitches, for Chance to weight by pitch class. A member rather
    // than a local so the audio thread never builds it on the stack per step.
    std::array<int, maxHeld * 4> poolPitches {};
    int seqCount = 0;
    long long stepCounter = 0;
    int dirCursor = 0;
    double freePhaseBeats = 0.0;
    double expectedNextPpq = 0.0;
    bool havePrevPpq = false;
    std::mt19937 rng { 0xFAB1E5EDu }; // fixed seed: deterministic tests, free variation live
};
} // namespace keys
