#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <limits>
#include <random>
#include <utility>

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
    // Sounding arp notes awaiting their note-off. Raised from 64 with the Chord shape and
    // the Harmony lane, which between them can put a spread chord's every note on one step.
    static constexpr int maxActive = 128;
    static constexpr int numLanes = 10;

    // The four after laneProbability arrived 2026-07-30. Appended, like everything else in
    // that round: a slot's lane data is serialized by index, so inserting would silently
    // reinterpret every pattern in every saved session.
    enum Lane { laneNote = 0, laneOctave, laneVelocity, laneGate, laneRatchet, laneProbability,
                laneTranspose, laneLate, laneHarmony, laneChord };

    // The value each lane holds when it is doing nothing. Also what every lane reads as
    // while Params::usePattern is false, which is how "Shape: Up" behaves like a plain
    // arpeggiator even after the step lanes have been edited.
    static constexpr int laneDefaults[numLanes] = { 0, 0, 100, 100, 1, 100, 0, 0, 0, 0 };

    // The Hz mode's range, which is not a round number by choice: it is exactly what the
    // eleven synced divisions span at 120 bpm. "1/64" is 32 steps a second and "16 bars" is
    // one step per 32 seconds, so a Hz mode narrower than this would reach less than the
    // list it sits beside. Ten octaves, and the parameter maps them exponentially, so each
    // one gets a tenth of the dial's travel (1 Hz - "1/2" at 120 bpm - lands dead centre as a
    // consequence, not as the thing being aimed at). Declared here rather than in the
    // parameter layout so the engine's clamp and the dial's ends are the same two numbers.
    static constexpr double minRateHz = 0.03125;
    static constexpr double maxRateHz = 32.0;

    // How a rate in Hz is written, wherever it is written: decimals by decade, so 0.031 Hz
    // and 32.0 Hz both read as themselves. A fixed 2 prints the bottom octave of the range as
    // "0.03", and a fixed 1 collapses the bottom *two* octaves onto "0.0" - which is how the
    // slot cards came to label a running arp as a stopped one. One copy of the rule, spent by
    // the arpRateHz parameter's text function and by ArpPanel's slot card; the unit suffix is
    // the caller's, since the two do not space it the same way.
    static juce::String rateHzText(double hz)
    {
        return juce::String(hz, hz < 1.0 ? 3 : (hz < 10.0 ? 2 : 1));
    }

    // The chords the twelve slots hold, mirrored into atomics so a step can call one up on
    // the audio thread. The processor owns it and refreshes it on the message thread
    // whenever a slot's chord changes; the engine only ever reads.
    struct ChordTable
    {
        static constexpr int numSlots = 12;
        static constexpr int maxNotes = 8;
        std::array<std::array<std::atomic<int>, maxNotes>, numSlots> note {};
        std::array<std::atomic<int>, numSlots> count {};
    };

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

    // The four after asPlayedReverse were appended in 2026-07-30, deliberately at the end:
    // `arpDirection` is a choice parameter and inserting anywhere else would renumber the
    // shapes every saved session already carries. `chord` is the odd one out - it is not a
    // walk order at all but "play the whole held chord on every step", which turns the arp
    // into a comping engine (Ableton calls it Chord Trigger).
    enum class Direction { up = 0, down, upDown, downUp, upAndDown, downAndUp, asPlayed, asPlayedReverse,
                           random, randomOther, randomOnce, chord };
    static constexpr int numDirections = 12; // keep in step with Direction; the Shape combo lists these then "Pattern"

    struct Params
    {
        bool enabled = false;
        int rateIndex = 8;        // index into rateInBeats(), default 1/16
        // The rate as a frequency instead of a division. True and the engine free-runs at
        // `rateHz` always - transport rolling or stopped, anchored or not - and neither the
        // playhead, `rateIndex`, `dotted` nor `triplet` is read for step timing. There is
        // still only one scheduler: the mode picks which clock drives it (see process()).
        bool rateFree = false;
        double rateHz = 8.0;      // 8 Hz is 1/16 at 120 bpm, so the default is the same speed
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
        // Restart every N beats as well, 0 = off (Ableton's Retrigger: Beat). Retrigger-on-
        // note answers "a new chord starts the pattern over"; this answers "the pattern is
        // one bar long whatever the lanes say", which is the other half of the same control.
        double retrigBeats = 0.0;
        // Where the pattern starts: rotates both the lane read index and the direction walk
        // by this many steps, so the same lanes can be heard from a different foot.
        int offset = 0;           // 0..31
        // What each repeat past the first adds, in semitones - or in scale degrees when
        // `spreadDegrees` is set, which is the thing the stock arps cannot do and Keys can,
        // because Root and Scale are already its own. 12 semitones is the octave stacking
        // every arp defaults to, and is what `octaveRange` alone used to mean.
        int spread = 12;
        bool spreadDegrees = false;
        int rootPc = 0;           // for degree walking
        // Bit k set = the pitch class (rootPc + k) % 12 is in the current scale. Passed in
        // rather than looked up so the engine stays free of the scale tables (and testable).
        unsigned int scaleMask = 0xFFFu;
        // Velocity ramp: over `rampBeats` from the moment a chord starts, the played
        // velocity scales toward (100 + velRamp)%. Negative fades a run out, positive swells
        // it. 0 is off, and costs nothing.
        int velRamp = 0;          // -100..+100
        double rampBeats = 8.0;
        // "Played, not programmed", split into its two halves on 2026-08-02 (Owen: "maybe we
        // could split it up into two knobs"). `humanize` nudges each hit late by up to 25 ms;
        // `humanVel` takes up to 30% off its velocity - each scaled by its own knob, each a
        // different random draw per hit. Before the split one value drove both, so a session
        // saved then keeps its amount as the timing half and gets 0 for the velocity half.
        int humanize = 0;         // 0..100, timing only
        int humanVel = 0;         // 0..100, velocity only
        // Move the whole run up or down whole octaves. Distinct from `octaveRange`, which
        // *stacks* copies upward and can only widen: this transposes, so it is centred at 0
        // and goes both ways (2026-08-02, Owen: "the octave should start in the middle so you
        // can go up or down"). It folds into the Octave lane's own shift, so a lane that
        // already moves a step keeps doing it relative to wherever this put the run.
        int octShift = 0;         // -3..+3, in octaves
        // Output level for the whole line, as a percentage of the velocity it would have
        // played. The plain volume control an arpeggiator wants and Keys never had: with two
        // lines running, balancing them was previously only possible by playing one softer.
        // Kept for sessions saved before velTrim below; nothing in the UI writes it any more
        // and KeysProcessor::migrateVelTrim folds it into velTrim on load.
        int volume = 100;         // 0..100
        // The control that replaced it on screen (2026-08-02, Owen: "it should start in the
        // middle so you can turn it up or down. But really, the volume is controlling
        // velocity"). Bipolar so the middle means "as played", and the multiplier is
        // *squared* - ((100+velTrim)/100)^2 - because hearing is logarithmic and the linear
        // version spent nearly all of its audible change in the last few degrees (same day,
        // Owen: "I was at negative 96, and it was still pretty loud"). Halfway down plays a
        // quarter of the velocity, which sounds about half as loud; -100 mutes exactly as
        // volume 0 does. Applied after the 0.05 audibility floor, not before it - see the
        // emit loop - so a deep cut reaches MIDI velocity 1 instead of pinning at 6.
        int velTrim = 0;          // -100..+100
        double fallbackBpm = 120.0; // internal clock when the transport is stopped/absent
        // Tempo Sync (`bpmSync`, KeysProcessor::advanceChainClock/buildArpParams). True
        // reproduces exactly what Keys always did before this parameter existed: a rolling
        // host with a valid bpm wins over fallbackBpm below. False pins the engine to
        // fallbackBpm even while the host is rolling, the escape hatch for someone who wants
        // Keys' own clock regardless of what the DAW's transport says. Read only in Sync -
        // Hz already ignores the host clock outright, so this changes nothing there.
        bool followHost = true;
        // The slot chords, for the Chord lane. Null means the lane does nothing, which is
        // what every caller that has no slots (the tests) wants.
        const ChordTable* chords = nullptr;
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
        pendingCount = 0; // un-fired ratchet hits must not survive a bypass or a transport jump
    }

    // Start the run over without forgetting what is held. This is hardReset() minus the held
    // set, and it is what switching a line *on* wants (2026-08-02): a chord handed to a line
    // while it was off is remembered silently by noteArrived - `enabled` gates only the firing
    // below, never the input - and is the whole reason there is something to play the moment
    // the switch goes on. hardReset() there would have thrown that chord away, which is what
    // made a line switched on sit silent until you dropped a new card on it.
    void restart()
    {
        activeCount = 0;
        pendingCount = 0;
        stepCounter = 0;
        dirCursor = 0;
        stepBase = 0;
        lastRetrigWindow = std::numeric_limits<long long>::min();
        pendingRetrig = false;
        heldBeats = 0.0;
        rampScale = 1.0f;
        lastPicked = -1;
        permDirty = true;   // permCount stays: the walk is rebuilt from the held set, not cleared
        freePhaseBeats = 0.0;
        havePrevPpq = false;
    }

    void hardReset()
    {
        activeCount = 0;
        pendingCount = 0;
        heldCount = 0;
        physicallyHeld = 0;
        stepCounter = 0;
        dirCursor = 0;
        stepBase = 0;
        lastRetrigWindow = std::numeric_limits<long long>::min();
        pendingRetrig = false;
        heldBeats = 0.0;
        rampScale = 1.0f;
        lastPicked = -1;
        permCount = 0;
        permDirty = true;
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

        // Switching between Hz and Sync is a change of *timebase*, not of speed: one counts
        // seconds and the other beats, so the phase carried across means nothing on the far
        // side and the step in flight belongs to a timeline that no longer exists. Treated
        // exactly like a transport jump below - close what is owed at the top of this block,
        // then start the new clock from zero - so nothing is left stranded on note-on.
        if (p.rateFree != lastRateFree)
        {
            lastRateFree = p.rateFree;
            flushInto(out);
            freePhaseBeats = 0.0;
            havePrevPpq = false;
            stepBase = 0;
            lastRetrigWindow = std::numeric_limits<long long>::min();
        }

        // One scheduler, two clocks, and the mode picks which drives it. Hz mode pins the
        // tempo to 60 so that one "beat" of everything below is one second; the step is then
        // 1/hz of that unit, and every quantity measured as a *fraction of a step* (swing,
        // the Late lane, gate, ratchet spacing) keeps its meaning with no second code path.
        // The two things measured in beats outright, Retrigger Every and the velocity ramp's
        // Ramp Time, therefore read as seconds while Hz is on. That is the honest reading:
        // there is no bar to restart on when nothing is following a transport.
        const double bpm = p.rateFree ? 60.0
                                      : ((p.followHost && clock.playing && clock.bpm > 0)
                                             ? clock.bpm
                                             : p.fallbackBpm);
        const double beatsPerSample = bpm / 60.0 / sr;
        const double stepBeats = stepLengthBeats(p);
        const double blockBeats = beatsPerSample * numSamples;

        // Position in beats at the start of this block. Hz mode never takes the anchored
        // branch: following the bar grid is the one thing a free-running rate cannot do.
        double pos;
        if (clock.playing && clock.hasPpq && p.anchored && ! p.rateFree)
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

        // Owed note-offs are NOT retired up front. Each hit closes what it lands on top of
        // itself, immediately before its own note-on (see fireStep), because a tie has to be
        // pulled back to just before the pitch retriggers rather than allowed to fire in the
        // middle of the note that replaced it. Draining here instead put a tie's note-off
        // *after* the note-on that superseded it: two note-ons for one pitch with nothing in
        // between, which hangs a voice on any synth that allocates per note-on.
        // The velocity ramp is sampled once per block, not once per hit: the shortest useful
        // ramp is a bar and the longest block is a few milliseconds, so the stair this leaves
        // is far below the 1/127 the velocity is quantized to anyway.
        rampScale = 1.0f;
        if (p.velRamp != 0 && p.rampBeats > 0.0)
        {
            const double t = juce::jlimit(0.0, 1.0, heldBeats / p.rampBeats);
            rampScale = (float) (1.0 + (double) p.velRamp / 100.0 * t);
        }

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
            // Three steps back, not one. One was enough while swing was the only thing that
            // moved a step (|swing| < 1, so a step could only ever be pulled into the block
            // before its own). The Late lane can push a step up to 0.9 of a step *past* its
            // boundary, and on an odd step that stacks with a positive swing, so a step can
            // now fire as much as 1.65 steps after the one it is named for. Costs two extra
            // iterations that skip on a negative offset and fire nothing.
            double nextIndexF = std::floor(pos / stepBeats + 1.0e-9) - 3.0;
            for (;;)
            {
                const double boundaryBeats = nextIndexF * stepBeats;
                const long long globalStep = (long long) llround(nextIndexF);
                double fireBeats = boundaryBeats;
                if ((globalStep & 1) != 0)
                    fireBeats += stepBeats * p.swing; // swing shifts the offbeats
                // Late: this step's own shift, always forwards. Cthulhu calls the lane Late
                // and so does this - it can only delay, which is what keeps fire times in
                // step order however the lane is drawn (step n at n+0.9 still precedes step
                // n+1 at n+1.0). An early half would need the whole close-what-you-land-on
                // rule in emitHit rewritten, for a shift Swing already offers.
                fireBeats += stepBeats * juce::jlimit(0, 90, laneValue(p, laneLate, globalStep)) / 100.0;
                const double offsetBeats = fireBeats - pos;
                const int offset = (int) std::floor(offsetBeats / beatsPerSample);
                if (offset >= numSamples)
                    break;
                if (offset >= 0)
                {
                    // Restarts, both kinds, decided here rather than where they are asked for:
                    // "step 1" only means anything at a step boundary, and the note that asks
                    // for a retrigger arrives in the middle of a block. Whichever fires first
                    // after the request becomes step 1, for the lanes (stepBase) and for the
                    // direction walk (dirCursor) alike. Before this the lanes never restarted
                    // at all - they were read straight off the absolute step index, so the
                    // Retrigger toggle only ever reset the walk.
                    if (p.retrigBeats > 0.0)
                    {
                        const long long win = (long long) std::floor(boundaryBeats / p.retrigBeats + 1.0e-9);
                        if (win != lastRetrigWindow)
                        {
                            lastRetrigWindow = win;
                            pendingRetrig = true;
                        }
                    }
                    if (pendingRetrig)
                    {
                        pendingRetrig = false;
                        stepBase = globalStep;
                        dirCursor = 0;
                    }

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

        // How long this chord has been up, which is what the velocity ramp rides on. It
        // counts beats rather than seconds so a ramp written at one tempo means the same
        // thing at another, and it stops counting the moment the keys come up.
        if (heldCount > 0)
            heldBeats += blockBeats;
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
        // Hz mode: process() has pinned the clock to 60 bpm, so a "beat" is a second and the
        // step length is simply the period. Dot and Trip are deliberately not applied - they
        // are subdivisions of a beat, and there is no beat here; all a dotted 8 Hz would do
        // is make the number on the dial a lie.
        //
        // The clamp is not only about musical range: it is the only thing keeping the step
        // loop in process() terminating, and this runs on the audio thread. At rateHz 0 the
        // period is +inf, the Late lane's `stepBeats * lane` term goes NaN, every comparison
        // against it is false, the computed offset is INT_MIN on every pass, and the
        // `offset >= numSamples` break never trips. Mutation-tested on 2026-07-30 by deleting
        // this jlimit: the test binary hung and had to be killed. A negative rate is harmless
        // by comparison, since `stepBeats > 0.0` catches it and simply fires nothing. So do
        // not move this clamp to the parameter and trust it: the engine takes whatever a
        // session, a host automation lane or an MCP client hands it.
        if (p.rateFree)
            return 1.0 / juce::jlimit(minRateHz, maxRateHz, p.rateHz);

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
        if (heldCount == 0)
        {
            heldBeats = 0.0; // a new chord restarts the velocity ramp
            if (p.retrigger)
            {
                stepCounter = 0;
                dirCursor = 0;
                pendingRetrig = true; // the next step to fire becomes step 1, lanes included
            }
        }
        permDirty = true; // the locked random order is per chord, and the chord just changed
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
                    permDirty = true;
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
        // Relative to the last restart, not to the absolute step index, so Retrigger means
        // step 1 for the lanes too; plus Offset, which starts the same lanes further in.
        const long long rel = (globalStep - stepBase) >> juce::jlimit(0, 2, div);
        const long long idx = ((rel + p.offset) % len + len) % len; // rel can be negative
        return lanes.value[li][(size_t) idx].load(std::memory_order_relaxed);
    }

    // Walk `degrees` scale steps from `note`, using the mask of in-scale pitch classes. A
    // note that is not itself in the scale lands on the next one in the direction of travel,
    // which is the same rounding Scale Lock does upstream.
    static int shiftByDegrees(int note, int degrees, unsigned int mask, int rootPc) noexcept
    {
        if (degrees == 0 || (mask & 0xFFFu) == 0)
            return note;
        const int dir = degrees > 0 ? 1 : -1;
        int remaining = degrees > 0 ? degrees : -degrees;
        int n = note;
        for (int guard = 0; guard < 128 && remaining > 0; ++guard)
        {
            n += dir;
            if (n < 0 || n > 127)
                return juce::jlimit(0, 127, n);
            if ((mask >> ((((n - rootPc) % 12) + 12) % 12)) & 1u)
                --remaining;
        }
        return n;
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

        // Repeats of the chord, each one `spread` further up. Twelve semitones is the octave
        // stacking this used to hardcode; in degrees it follows Root/Scale, so a spread of a
        // third stays a third of *this* key rather than of the chromatic scale.
        const int octs = juce::jlimit(1, 4, p.octaveRange);
        for (int o = 0; o < octs; ++o)
            for (int i = 0; i < heldCount && seqCount < (int) seq.size(); ++i)
            {
                const int base = held[(size_t) order[i]].note;
                const int shifted = p.spreadDegrees
                                  ? shiftByDegrees(base, o * p.spread, p.scaleMask, p.rootPc)
                                  : base + o * p.spread;
                seq[(size_t) seqCount++] = { order[i], shifted - base };
            }
    }

    // The sequence is always built ascending (or in arrival order); directions are
    // realized here. Exclusive ping-pong (upDown/downUp) plays the endpoints once
    // per cycle; inclusive (upAndDown/downAndUp) repeats them, Cthulhu-style.
    int nextDirectionIndex(const Params& p)
    {
        const int n = seqCount;
        if (n <= 1)
        {
            ++dirCursor;
            return 0;
        }
        // Offset starts the walk further in. Added to the cursor rather than to the result,
        // so a ping-pong starts at the right place *in its cycle* instead of being reflected
        // to some other note.
        const long long cursor = dirCursor++ + p.offset;
        switch (p.direction)
        {
            case Direction::up:
            case Direction::asPlayed:
                return (int) (cursor % n);
            case Direction::down:
            case Direction::asPlayedReverse:
                return n - 1 - (int) (cursor % n);
            case Direction::upDown:
            case Direction::downUp:
            {
                const int period = 2 * (n - 1);
                const int c = (int) (cursor % period);
                const int i = c < n ? c : period - c;
                return p.direction == Direction::upDown ? i : n - 1 - i;
            }
            case Direction::upAndDown:
            case Direction::downAndUp:
            {
                const int period = 2 * n;
                const int c = (int) (cursor % period);
                const int i = c < n ? c : period - 1 - c;
                return p.direction == Direction::upAndDown ? i : n - 1 - i;
            }
            case Direction::random:
                lastPicked = (int) (rng() % (unsigned) n);
                return lastPicked;
            case Direction::randomOther:
            {
                // Draw from the other n-1 notes and skip past the last one, rather than
                // re-drawing until it differs: one draw, always, and no unbounded loop on
                // the audio thread.
                int r = (int) (rng() % (unsigned) (n - 1));
                if (lastPicked >= 0 && lastPicked < n && r >= lastPicked)
                    ++r;
                lastPicked = juce::jlimit(0, n - 1, r);
                return lastPicked;
            }
            case Direction::randomOnce:
                rebuildPermIfNeeded();
                return perm[(size_t) (cursor % n)];
            case Direction::chord:
                return (int) (cursor % n); // fireStep plays them all; this is the fallback
        }
        return 0;
    }

    // A shuffled order held for as long as the chord is: random, but the same random every
    // time round, which is the one random mode that sounds composed rather than sprayed.
    void rebuildPermIfNeeded()
    {
        if (! permDirty && permCount == seqCount)
            return;
        permCount = seqCount;
        for (int i = 0; i < permCount; ++i)
            perm[(size_t) i] = i;
        for (int i = permCount - 1; i > 0; --i) // Fisher-Yates
            std::swap(perm[(size_t) i], perm[(size_t) (rng() % (unsigned) (i + 1))]);
        permDirty = false;
    }

    void fireStep(const Params& p, long long globalStep, int offset, double stepSamplesF,
                  int numSamples, juce::MidiBuffer& out)
    {
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

        // Which sequence entries this step plays. One, normally. All of them on the Chord
        // shape, which is what makes it a comping engine rather than a walk order - a fixed
        // Note-lane index still means that one note, so an edited pattern keeps its meaning
        // over a chord shape instead of silently turning into block chords.
        int playIdx[maxHeld * 4];
        int playCount = 0;
        if (p.direction == Direction::chord && noteVal == 0)
        {
            for (int i = 0; i < seqCount; ++i)
                playIdx[playCount++] = i;
        }
        else
        {
            playIdx[playCount++] = noteVal >= 1 ? (noteVal - 1) % seqCount // fixed index, wraps politely
                                                : nextDirectionIndex(p);
        }

        // The Octave lane's per-step shift plus the line's own, both in octaves. Summed rather
        // than one overriding the other: the knob says where the run sits, the lane says how a
        // particular step departs from that, and they are different questions.
        const int octaveShift = 12 * (juce::jlimit(-3, 3, laneValue(p, laneOctave, globalStep))
                                      + juce::jlimit(-3, 3, p.octShift));
        const int transpose = juce::jlimit(-7, 7, laneValue(p, laneTranspose, globalStep));
        const int harmony = juce::jlimit(0, 7, laneValue(p, laneHarmony, globalStep));
        const int chordSel = juce::jlimit(0, ChordTable::numSlots, laneValue(p, laneChord, globalStep));
        // The line's own fader (velTrim) is deliberately NOT in velScale: velScale feeds the
        // 0.05 audibility floor below, which protects programmed dynamics, and the fader is
        // applied after that floor precisely so it is not protected by it.
        const float velScale = (float) laneValue(p, laneVelocity, globalStep) / 100.0f * rampScale
                             * ((float) juce::jlimit(0, 100, p.volume) / 100.0f);
        const float trimT = (100.0f + (float) juce::jlimit(-100, 100, p.velTrim)) / 100.0f;
        const float trimScale = trimT * trimT; // squared: see the Params comment
        const int ratchets = juce::jlimit(1, 4, laneValue(p, laneRatchet, globalStep));
        const double gate = juce::jlimit(5, 200, laneValue(p, laneGate, globalStep))
                          * juce::jlimit(5, 200, p.gate) / 10000.0;

        const double subLen = stepSamplesF / ratchets;
        // Humanize never reorders anything: it only ever pushes a hit late, and never by more
        // than 40% of the gap to the next sub-hit. Unbounded, a 25 ms nudge at a fast ratchet
        // could carry one sub-hit past the next, and two hits of one pitch arriving out of
        // order is exactly the shape emitHit's close-what-you-land-on rule cannot survive.
        const int maxLate = p.humanize > 0
                          ? (int) juce::jmin(0.025 * sr * (juce::jlimit(0, 100, p.humanize) / 100.0),
                                             subLen * 0.4)
                          : 0;

        // Resolve the step into pitches once, before the ratchet loop repeats them. Three
        // lanes fold in here, and all three want the note *after* the sequence walk has
        // chosen one, not instead of it.
        struct Hit { int note; float vel; int chan; };
        Hit hits[maxHeld * 8 + ChordTable::maxNotes];
        int hitCount = 0;
        const auto& lead = held[0]; // whose velocity and channel a summoned chord borrows
        const auto addHit = [&](int note, float vel, int chan)
        {
            if (hitCount < (int) (sizeof(hits) / sizeof(hits[0])))
                hits[hitCount++] = { juce::jlimit(0, 127, note), vel, chan };
        };
        const auto place = [&](int note)
        {
            // Octave lane, then Transpose - which counts *scale degrees*, not semitones.
            // Everyone else's transpose lane is chromatic and is therefore a machine for
            // leaving the key; Keys already owns Root and Scale, so the musical version is
            // the one that costs nothing (and a chromatic scale mask makes it chromatic).
            const int shifted = note + octaveShift;
            return transpose != 0 ? shiftByDegrees(shifted, transpose, p.scaleMask, p.rootPc) : shifted;
        };

        const int chordCount = (chordSel > 0 && p.chords != nullptr)
                             ? juce::jlimit(0, ChordTable::maxNotes,
                                            p.chords->count[(size_t) (chordSel - 1)].load(std::memory_order_relaxed))
                             : 0;
        if (chordCount > 0)
        {
            // The Chord lane calls up one of the twelve slots' chords for this step alone -
            // Kirnu Cream's Chordmem, except the memories are the slots Keys already has, so
            // a progression can be drawn into a lane without storing a second copy of it.
            // What is held decides only the velocity and the channel.
            for (int i = 0; i < chordCount; ++i)
                addHit(place(p.chords->note[(size_t) (chordSel - 1)][(size_t) i].load(std::memory_order_relaxed)),
                       lead.velocity * velScale, lead.channel);
        }
        else
        {
            for (int k = 0; k < playCount; ++k)
            {
                const int idx = juce::jlimit(0, seqCount - 1, playIdx[k]);
                const auto& entry = seq[(size_t) idx];
                const auto& src = held[(size_t) juce::jlimit(0, heldCount - 1, entry.heldIndex)];
                addHit(place(src.note + entry.semitoneOffset), src.velocity * velScale, src.channel);

                // Harmony: a second voice this many chord tones above the one just played,
                // Cthulhu's lane. Counting in sequence entries rather than semitones is what
                // keeps it inside the chord; running off the top adds an octave instead of
                // folding back onto a note already sounding.
                if (harmony > 0)
                {
                    const int h = idx + harmony;
                    const auto& hEntry = seq[(size_t) (h % seqCount)];
                    const auto& hSrc = held[(size_t) juce::jlimit(0, heldCount - 1, hEntry.heldIndex)];
                    addHit(place(hSrc.note + hEntry.semitoneOffset + 12 * (h / seqCount)),
                           hSrc.velocity * velScale, hSrc.channel);
                }
            }
        }

        // Volume 0 is a mute, and a mute emits nothing. The 0.05 floor below exists so a
        // Velocity lane at 0, or a hard Humanize draw, stays audible rather than turning a
        // note-on into a note-off - but it must not also make the line's own level
        // un-silenceable, and it did: VOL at the bottom of its travel played the line quietly
        // instead of stopping it, which is the one thing a control called VOL has to do.
        //
        // Dropped here rather than by returning early, so the step is still *resolved*: the RNG
        // draw, the sequence walk and stepCounter have all happened above, and unmuting picks
        // the run up where it would have been rather than restarting it.
        if (p.volume <= 0 || p.velTrim <= -100)
            hitCount = 0;

        for (int r = 0; r < ratchets; ++r)
        {
            const int at = offset + (int) std::floor(subLen * r);
            const int durSamples = juce::jmax(1, (int) std::floor(subLen * gate));

            for (int k = 0; k < hitCount; ++k)
            {
                const auto& hit = hits[k];
                const int note = hit.note;

                int on = at;
                float vel = hit.vel;
                // Late and quieter, never early and never louder: a nudge that can also
                // rush is what Swing is for, and a velocity that can also rise makes an
                // edited Velocity lane mean less than it says. Two knobs since 2026-08-02
                // (humanize is the timing, humanVel the velocity), so each half only runs
                // when its own knob is up.
                if (maxLate > 0)
                    on += (int) (rng() % (unsigned) (maxLate + 1));
                if (p.humanVel > 0)
                {
                    const double amt = juce::jlimit(0, 100, p.humanVel) / 100.0;
                    vel *= (float) (1.0 - (double) (rng() % 1000u) / 1000.0 * 0.30 * amt);
                }
                // The 0.05 floor protects programmed dynamics: a Velocity lane at 0 or a
                // hard H.VEL draw must stay audible rather than turn into a note-off. The
                // line's fader multiplies *after* it, because turning a line down is
                // supposed to approach silence - inside the floor it pinned at velocity 6
                // from about -90 downward, which on a patch with a shallow velocity
                // response was still plainly audible (2026-08-02). The final clamp bottoms
                // at one MIDI step; zero would be a note-off in disguise.
                vel = juce::jlimit(0.05f, 1.0f, vel) * trimScale;
                vel = juce::jlimit(1.0f / 127.0f, 1.0f, vel);

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
                    emitHit(note, hit.chan, vel, on, durSamples, numSamples, out);
                else if (pendingCount < maxPending)
                    pending[(size_t) pendingCount++] = { note, hit.chan, vel, on, durSamples };
                // else: out of carry slots, and the hit is dropped rather than mistimed. The
                // capacity is a chord's worth of ratchets over several steps; a step's carry is
                // drained before the next step fires, so nothing that fits reaches it.
            }
        }
        ++stepCounter;
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

    // `semitoneOffset` used to be an octave count times twelve. It is a resolved semitone
    // shift now, because a spread in scale degrees is not a fixed interval: the same repeat
    // is 3 or 4 semitones depending which note of the chord it is lifting.
    struct SeqEntry { int heldIndex; int semitoneOffset; };

    double sr = 44100.0;
    std::array<Held, maxHeld> held {};
    int heldCount = 0;
    int physicallyHeld = 0;
    std::array<Active, maxActive> active {};
    int activeCount = 0;

    // Sub-hits carried into a later block. Sized for the worst case the Chord shape makes
    // reachable - every note of a spread chord ratcheting at once - rather than the single
    // note's three the old sixteen covered.
    static constexpr int maxPending = 96;
    std::array<PendingHit, maxPending> pending {};
    int pendingCount = 0;
    std::array<SeqEntry, maxHeld * 4> seq {};
    int seqCount = 0;
    long long stepCounter = 0;
    long long dirCursor = 0;
    // Where the lanes count from: the step index of the last restart, so Retrigger and the
    // beat-retrigger window both mean "step 1 next", and Offset counts from a known origin.
    long long stepBase = 0;
    long long lastRetrigWindow = std::numeric_limits<long long>::min();
    bool pendingRetrig = false;
    double heldBeats = 0.0;  // how long the current chord has been up, for the velocity ramp
    float rampScale = 1.0f;  // that ramp, resolved once per block
    int lastPicked = -1;     // for Random Other
    std::array<int, maxHeld * 4> perm {}; // the locked order Random Once walks
    int permCount = 0;
    bool permDirty = true;
    double freePhaseBeats = 0.0;
    double expectedNextPpq = 0.0;
    bool havePrevPpq = false;
    // The rate mode the last block ran under, so process() can spot the timebase changing
    // under it. Deliberately not cleared by hardReset(): it is not playback state but a
    // memory of what was read, and zeroing it would report a mode change that never happened
    // (harmless - hardReset has already done the same work - but it would be a lie).
    bool lastRateFree = false;
    std::mt19937 rng { 0xFAB1E5EDu }; // fixed seed: deterministic tests, free variation live
};
} // namespace keys
