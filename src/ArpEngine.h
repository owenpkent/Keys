#pragma once

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
            static constexpr int defaults[numLanes] = { 0, 0, 100, 100, 1, 100 };
            for (int l = 0; l < numLanes; ++l)
            {
                for (auto& v : value[(size_t) l])
                    v.store(defaults[l], std::memory_order_relaxed);
                length[(size_t) l].store(8, std::memory_order_relaxed);
                clockDiv[(size_t) l].store(0, std::memory_order_relaxed);
            }
        }
    };

    enum class Direction { up = 0, down, upDown, downUp, upAndDown, downAndUp, asPlayed, asPlayedReverse };

    struct Params
    {
        bool enabled = false;
        int rateIndex = 8;        // index into rateInBeats(), default 1/16
        bool dotted = false;
        bool triplet = false;
        bool anchored = true;     // affixed to the host bar grid vs free-running
        Direction direction = Direction::up;
        int octaveRange = 1;      // 1..4, for direction modes
        float swing = 0.0f;       // 0..0.75, delays odd steps by that fraction of a step
        bool latch = false;
        bool retrigger = true;    // restart at step 1 when a note arrives on an empty set
        double fallbackBpm = 120.0; // internal clock when the transport is stopped/absent
    };

    struct HostClock
    {
        bool playing = false;
        bool hasPpq = false;
        double ppq = 0.0;  // position at the start of the block, in quarter notes
        double bpm = 120.0;
    };

    Lanes lanes;

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
    }

    void hardReset()
    {
        activeCount = 0;
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
                flushInto(out);
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

        // Retire owed note-offs that fall inside this block.
        retireActive(numSamples, out);

        if (! p.enabled || heldCount == 0 || stepBeats <= 0.0)
            return;

        // Fire every step boundary inside [pos, pos + blockBeats).
        double nextIndexF = std::ceil(pos / stepBeats - 1.0e-9);
        for (;;)
        {
            const double boundaryBeats = nextIndexF * stepBeats;
            const long long globalStep = (long long) llround(nextIndexF);
            double fireBeats = boundaryBeats;
            if ((globalStep & 1) != 0)
                fireBeats += stepBeats * p.swing; // swing delays the offbeats
            const double offsetBeats = fireBeats - pos;
            const int offset = (int) std::floor(offsetBeats / beatsPerSample);
            if (offset >= numSamples)
                break;
            if (offset >= 0)
                fireStep(p, globalStep, offset, stepBeats / beatsPerSample, out);
            nextIndexF += 1.0;
        }
    }

    int heldNoteCount() const noexcept { return heldCount; }

private:
    struct Held { int note; float velocity; int channel; bool physical; };
    struct Active { int note; int channel; int samplesLeft; };

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
                held[(size_t) i].velocity = vel;
                held[(size_t) i].physical = true;
                ++physicallyHeld;
                return;
            }
        if (heldCount < maxHeld)
        {
            held[(size_t) heldCount++] = { note, vel, channel, true };
            ++physicallyHeld;
        }
    }

    void noteLeft(int note, const Params& p)
    {
        for (int i = 0; i < heldCount; ++i)
            if (held[(size_t) i].note == note && held[(size_t) i].physical)
            {
                held[(size_t) i].physical = false;
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

    int laneValue(Lane l, long long globalStep) const
    {
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
                  juce::MidiBuffer& out)
    {
        const int noteVal = laneValue(laneNote, globalStep);
        if (noteVal < 0)
            return; // muted step
        if ((int) (rng() % 100u) >= laneValue(laneProbability, globalStep))
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
                                          + 12 * juce::jlimit(-3, 3, laneValue(laneOctave, globalStep)));
        const float vel = juce::jlimit(0.05f, 1.0f,
                                       src.velocity * (float) laneValue(laneVelocity, globalStep) / 100.0f);
        const int ratchets = juce::jlimit(1, 4, laneValue(laneRatchet, globalStep));
        const double gate = juce::jlimit(5, 200, laneValue(laneGate, globalStep)) / 100.0;

        const double subLen = stepSamplesF / ratchets;
        for (int r = 0; r < ratchets; ++r)
        {
            const int on = offset + (int) std::floor(subLen * r);
            const int durSamples = juce::jmax(1, (int) std::floor(subLen * gate));
            // Same pitch already ringing: close it just before the retrigger.
            for (int i = 0; i < activeCount; ++i)
                if (active[(size_t) i].note == note && active[(size_t) i].channel == src.channel)
                {
                    out.addEvent(juce::MidiMessage::noteOff(src.channel, note), juce::jmax(0, on - 1));
                    active[(size_t) i] = active[(size_t) --activeCount];
                    break;
                }
            out.addEvent(juce::MidiMessage::noteOn(src.channel, note, vel), on);
            if (activeCount < maxActive)
                active[(size_t) activeCount++] = { note, src.channel, on + durSamples };
            else
                out.addEvent(juce::MidiMessage::noteOff(src.channel, note), on + durSamples - 1);
        }
        ++stepCounter;
    }

    void retireActive(int numSamples, juce::MidiBuffer& out)
    {
        for (int i = 0; i < activeCount;)
        {
            auto& a = active[(size_t) i];
            if (a.samplesLeft < numSamples)
            {
                out.addEvent(juce::MidiMessage::noteOff(a.channel, a.note), juce::jmax(0, a.samplesLeft));
                a = active[(size_t) --activeCount];
            }
            else
            {
                a.samplesLeft -= numSamples;
                ++i;
            }
        }
    }

    struct SeqEntry { int heldIndex; int octaveOffset; };

    double sr = 44100.0;
    std::array<Held, maxHeld> held {};
    int heldCount = 0;
    int physicallyHeld = 0;
    std::array<Active, maxActive> active {};
    int activeCount = 0;
    std::array<SeqEntry, maxHeld * 4> seq {};
    int seqCount = 0;
    long long stepCounter = 0;
    int dirCursor = 0;
    double freePhaseBeats = 0.0;
    double expectedNextPpq = 0.0;
    bool havePrevPpq = false;
    std::mt19937 rng { 0xFAB1E5EDu }; // fixed seed: deterministic tests, free variation live
};
} // namespace keys
