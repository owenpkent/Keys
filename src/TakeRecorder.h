#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

// Keys recording itself: the ring the audio thread writes, the take the message thread keeps,
// and the MIDI file both of them agree on.
//
// It left KeysProcessor because it never needed one. The audio thread hands it the block that
// is leaving and the sample rate; arming hands it the tempo to freeze; everything after that is
// its own ring, its own vector and its own file, and nothing in it reads a parameter, a chord
// pad or an arp line. KeysProcessor owns one as a member and keeps the whole public take API as
// forwarders, so the editor, the MCP bridge and tests/TakeTests.cpp call exactly what they
// always called - `KeysProcessor::TakeNote` included, which is an alias onto the struct here.
//
// The thread contract is unchanged and is the reason to read this file before editing it:
// captureBlock is the audio thread and allocates nothing and takes no lock; drainCapture and
// everything below it are the message thread.

namespace keys
{
class TakeRecorder
{
public:
    // **Ableton cannot record a plugin's own MIDI onto that plugin's own track**, and this is
    // the whole reason this exists (2026-08-17, Owen: "host in ableton does not record midi").
    // Live records what arrives at a track's *input*; Keys' notes are made inside the plugin,
    // downstream of that input, so arming the Keys track and pressing record captures an empty
    // clip. It is not a Keys bug and there is no plugin-side setting that changes it - the
    // listener-track routing in docs/ABLETON_LIVE.md is the DAW-correct answer and always
    // worked. It is also a second track, a re-patch and an arm, for something that should be
    // one click, and on a one-track Keys Host set it is the whole reason the set had one track.
    //
    // So Keys keeps its own take. What is captured is the stream leaving processBlock - after
    // the arp, after strum, on the channels the lines sent it on - which is what you heard, not
    // what you clicked. The take lands on disk the moment recording stops, so it is never a
    // thing you can lose by clicking the wrong chip; see takeFolder().
    //
    // `tempoAtArm` is the tempo to freeze into this take and is read only when arming. It is a
    // parameter rather than a call back into the processor for the reason everything else here
    // is: the recorder has no opinion about where a tempo comes from.
    void setRecording(bool shouldRecord, double tempoAtArm);
    bool isRecording() const { return recording.load(std::memory_order_relaxed); }

    // The take as it stands, message thread. `capturedSeconds` is first **note** to last event, so
    // an armed-but-silent minute before you played does not count and is not written.
    int capturedEventCount() const { return (int) capturedTake.size(); }
    double capturedSeconds() const;

    // Whether the take holds a note yet, which is a different question from whether it holds an
    // *event*. Keys' own wheels emit CC and pitch bend on the same stream captureBlock reads, so
    // nudging the mod wheel and then thinking for ten seconds used to start the clock and, worse,
    // set the trim point - putting every note ten seconds off the top of the clip, which is
    // exactly what the frozen tempo exists to prevent. A take is made of notes.
    bool capturedHasNotes() const;

    // The take as a type-0 MIDI file at the tempo it was played to, note-offs supplied for
    // anything still ringing when recording stopped, and shifted so the first **note** sits at
    // zero. False when there is nothing to write, which means no note was played.
    bool buildTakeMidiFile(juce::MidiFile& out) const;

    // The take's notes, for drawing it. **Built from buildTakeMidiFile's own sequence**, not
    // from the raw capture, so what the preview draws is provably what the file holds - the
    // trim, the pairing and the supplied note-offs are all applied once, in one place, and a
    // preview that disagreed with the file would be worse than no preview at all.
    struct TakeNote
    {
        double startSec = 0.0, lengthSec = 0.0;
        int note = 0, channel = 1;
        float velocity = 0.0f;
    };
    std::vector<TakeNote> takeNotes() const;

    // The tempo the take was played to, frozen when recording armed. Not the processor's live
    // tempo: the file is written once, at stop, and a host tempo that moved afterwards would
    // make every later preview disagree with the bytes already on disk.
    double takeTempo() const { return takeBpm; }

    // Where a stopped take is written, created on demand. One fixed folder rather than a save
    // dialog per take: add it to Live's Places once and every take afterwards is a short drag
    // inside Live's own browser, which is a far kinder gesture than dragging out of a plugin
    // window and across the screen.
    static juce::File takeFolder();
    juce::File lastTakeFile() const { return lastTake; }

    // Writes the current take and remembers it as lastTakeFile(). Separate from setRecording so
    // that stopping is a pure state change with nothing on disk in it - which is what lets the
    // capture be tested without writing into the user's Documents folder. The UI calls the two
    // together and must keep doing so: a take that stopped and was never written is a take the
    // next REC click throws away.
    juce::File writeTake();

    // Whether the **last** writeTake could not put the take on disk (no folder, no stream, a
    // failed write). Cleared at the top of every writeTake. It exists because the honest answer
    // to a failed write is to keep the take you already had, and a UI that quietly went on
    // offering that older file would be reporting the wrong take as the right one.
    bool lastTakeWriteFailed() const { return takeWriteFailed; }

    // Audio thread, end of the block: the stream that is actually leaving, and the sample rate
    // to stamp it against. Allocates nothing and takes no lock.
    void captureBlock(const juce::MidiBuffer&, int numSamples, double sampleRate);
    // Message thread, off the 50 Hz heartbeat: moves what the audio thread has published into
    // the vector, where allocating is allowed.
    void drainCapture();

private:
    // A single-producer/single-consumer ring: the audio thread appends the block's outgoing
    // events and publishes one index; heartbeatTick drains it into `capturedTake` on the
    // message thread, where the vector is free to allocate. Nothing on the audio thread
    // allocates, takes a lock, or touches `capturedTake`.
    //
    // 32768 events against a 50 Hz drain is about six hundred events a *block* before the
    // writer could lap the reader, which no keyboard produces; the lap is handled anyway
    // (drainCapture drops the oldest) rather than left to read torn events.
    struct CapturedEvent
    {
        double atSec;            // from the start of recording, not from the host's timeline
        juce::uint8 bytes[3] {};
        juce::uint8 size = 0;
    };
    static constexpr int captureCapacity = 1 << 15;
    // How far past the oldest surviving slot drainCapture restarts after a lap. Recovering to the
    // oldest slot exactly means recovering to the slot the writer is inside, so this is the gap
    // that keeps the reader off it - a generous block's worth of events rather than the one slot
    // that would strictly do.
    static constexpr juce::uint32 captureLapMargin = 512;

    std::vector<CapturedEvent> captureRing { (size_t) captureCapacity };
    std::atomic<juce::uint32> captureWrite { 0 }; // audio thread publishes, message thread reads
    juce::uint32 captureRead = 0;                 // message thread only
    std::atomic<bool> recording { false };

    // Samples since arming, which is what stamps each event's `atSec`. Atomic, and **`recording`
    // is stored with release ordering after it is zeroed**, so the audio thread cannot see the
    // flag go up while this still holds the previous take's count. It was a plain int64 written
    // from both threads behind a relaxed store for one build: a data race, and one whose visible
    // form is a take whose first block is stamped a minute in, which makes `buildTakeMidiFile`
    // trim from there and give every later event a negative tick.
    std::atomic<juce::int64> captureSamples { 0 };

    std::vector<CapturedEvent> capturedTake;      // message thread only
    juce::File lastTake;
    bool takeWriteFailed = false;

    static constexpr short takeTicksPerQuarter = 960;
    double takeBpm = 120.0;      // frozen at arm time; see takeTempo()
    double takeTicksPerSecond() const;

    // The take's zero, and the one definition of "this take has something in it". See the .cpp.
    const CapturedEvent* firstCapturedNote() const;
};
} // namespace keys
