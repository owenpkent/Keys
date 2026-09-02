#include "TakeRecorder.h"
#include <juce_events/juce_events.h> // JUCE_ASSERT_MESSAGE_THREAD
#include <cmath>

namespace keys
{
// Audio thread. Writes into the ring and publishes one index; allocates nothing (the ring is
// sized in the constructor) and takes no lock.
//
// The armed check is in here rather than at the call site so that the **acquire** cannot be
// dropped by a caller reaching for isRecording() instead: it pairs with the release store in
// setRecording, and is what guarantees this thread sees the zeroed captureSamples that arming
// wrote just before raising the flag. Disarmed, this is one atomic load and a return, which is
// exactly what the branch around the call used to cost.
void TakeRecorder::captureBlock(const juce::MidiBuffer& midi, int numSamples, double sampleRate)
{
    if (! recording.load(std::memory_order_acquire))
        return;

    const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    auto w = captureWrite.load(std::memory_order_relaxed);
    // Read once into a local: this is the only thread that advances it, and the message thread
    // only ever zeroes it before publishing `recording` with release ordering.
    const auto elapsed = captureSamples.load(std::memory_order_relaxed);

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int size = m.getRawDataSize();
        // Three bytes or fewer covers every channel voice message. Sysex, MTC and the rest are
        // not what a played take is made of, and admitting them would mean a variable-size slot
        // and an allocation somewhere on this thread.
        if (size <= 0 || size > 3 || ! m.getChannel())
            continue;

        auto& e = captureRing[(size_t) (w % (juce::uint32) captureCapacity)];
        e.atSec = (double) (elapsed + meta.samplePosition) / sr;
        e.size = (juce::uint8) size;
        const auto* raw = m.getRawData();
        for (int i = 0; i < size; ++i)
            e.bytes[i] = raw[i];
        ++w;
    }

    captureWrite.store(w, std::memory_order_release); // slots filled before the index admits them
    captureSamples.store(elapsed + numSamples, std::memory_order_relaxed);
}

// Message thread, off the 50 Hz heartbeat.
void TakeRecorder::drainCapture()
{
    const auto w = captureWrite.load(std::memory_order_acquire);
    if (w == captureRead)
        return;

    // Lapped: the audio thread has overwritten slots we never read. Cannot happen at any rate a
    // keyboard produces, but reading them anyway would hand back torn events, so skip to the
    // oldest slot still intact and lose the gap rather than the take.
    //
    // The margin is not decoration. `w - captureCapacity` lands on `w % captureCapacity`, which is
    // the exact slot the audio thread is about to fill, so recovering to it read the one torn
    // event this branch exists to skip. `captureLapMargin` blocks' worth of slots past it puts the
    // reader clear of anything a writer could still be inside.
    if (w - captureRead > (juce::uint32) captureCapacity)
        captureRead = w - (juce::uint32) captureCapacity + captureLapMargin;

    for (; captureRead != w; ++captureRead)
        capturedTake.push_back(captureRing[(size_t) (captureRead % (juce::uint32) captureCapacity)]);
}

void TakeRecorder::setRecording(bool shouldRecord, double tempoAtArm)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (shouldRecord == recording.load(std::memory_order_relaxed))
        return;

    if (shouldRecord)
    {
        // Arming starts a new take. Nothing is lost by that: the previous one was written to
        // disk the moment it stopped (see writeTake), so the only thing being cleared here is a
        // copy of a file that already exists.
        //
        // No drain first. There was one for a build, immediately above a `clear()` of the vector
        // it had just filled and a re-read of the cursor it had just advanced - up to 32768
        // push_backs, and the reallocations behind them, to be thrown away one statement later.
        // Skipping to the writer's published index is the whole of what arming needs.
        capturedTake.clear();
        captureRead = captureWrite.load(std::memory_order_acquire);
        captureSamples.store(0, std::memory_order_relaxed);
        // Frozen here rather than read at build time: the file is written once, at stop, and a
        // host tempo that moved afterwards would make every later preview disagree with the
        // bytes already on disk. This is also the tempo you actually played to.
        takeBpm = juce::jlimit(20.0, 999.0, tempoAtArm);
        // **Release, not relaxed.** Everything above has to be visible to the audio thread before
        // the flag that lets it start writing is: a relaxed store orders nothing, so the zeroed
        // captureSamples could land after it and the first block of a new take would be stamped
        // at the previous take's elapsed time.
        recording.store(true, std::memory_order_release);
        return;
    }

    recording.store(false, std::memory_order_relaxed);
    drainCapture(); // the last block or two the audio thread wrote before the flag went down
}

// Where the take actually begins: the first captured **note-on**. Not the first event - Keys' own
// mod wheel and pitch bend land on the same stream, so a wheel nudged before you played would
// otherwise be the take's zero and push every note that far off the top of the clip.
// Returns nullptr when nothing has been played yet, which is what "no take" means.
const TakeRecorder::CapturedEvent* TakeRecorder::firstCapturedNote() const
{
    for (const auto& e : capturedTake)
        if (juce::MidiMessage(e.bytes, (int) e.size).isNoteOn())
            return &e;
    return nullptr;
}

bool TakeRecorder::capturedHasNotes() const
{
    return firstCapturedNote() != nullptr;
}

double TakeRecorder::capturedSeconds() const
{
    const auto* first = firstCapturedNote();
    if (first == nullptr)
        return 0.0;
    // First note to last event, so arming and then thinking for a minute costs the take nothing -
    // the same offset buildTakeMidiFile shifts away.
    return juce::jmax(0.0, capturedTake.back().atSec - first->atSec);
}

double TakeRecorder::takeTicksPerSecond() const
{
    // One place, so the file and the preview drawn from it can never disagree about time.
    return (double) takeTicksPerQuarter * takeBpm / 60.0;
}

std::vector<TakeRecorder::TakeNote> TakeRecorder::takeNotes() const
{
    std::vector<TakeNote> out;

    // Built from the file's own sequence rather than from `capturedTake`, so the trim, the
    // pairing and the supplied note-offs are applied once and the picture is the bytes.
    juce::MidiFile file;
    if (! buildTakeMidiFile(file) || file.getNumTracks() < 1)
        return out;

    const double ticksPerSecond = takeTicksPerSecond();
    if (ticksPerSecond <= 0.0)
        return out;

    const auto& seq = *file.getTrack(0);
    out.reserve((size_t) seq.getNumEvents() / 2);
    for (int i = 0; i < seq.getNumEvents(); ++i)
    {
        const auto* ev = seq.getEventPointer(i);
        if (! ev->message.isNoteOn())
            continue;

        const double startTicks = ev->message.getTimeStamp();
        const double endTicks = ev->noteOffObject != nullptr ? seq.getTimeOfMatchingKeyUp(i)
                                                             : startTicks;
        // The true length, with no floor under it. A short note has to stay *visible*, but that
        // is a question about drawing and belongs in Roll::paint, which already floors the bar
        // at 2 px. Putting the floor here instead made the preview disagree with the file by up
        // to 10 ms on every short note - which is the one thing this function must never do,
        // and is exactly what the "the preview is the file" test caught.
        out.push_back({ startTicks / ticksPerSecond,
                        (endTicks - startTicks) / ticksPerSecond,
                        ev->message.getNoteNumber(),
                        ev->message.getChannel(),
                        ev->message.getFloatVelocity() });
    }
    return out;
}

bool TakeRecorder::buildTakeMidiFile(juce::MidiFile& out) const
{
    // The take starts when you **played**, not when you armed and not when you brushed a wheel.
    // No note means no take: a file of nothing but controller moves is not something to write,
    // and it is what stops an accidental arm-and-stop overwriting the take you meant to keep.
    const auto* first = firstCapturedNote();
    if (first == nullptr)
        return false;

    constexpr short ticksPerQuarter = takeTicksPerQuarter;
    const double bpm = takeBpm;
    const double ticksPerSecond = takeTicksPerSecond();
    const double zero = first->atSec;

    juce::MidiMessageSequence seq;
    seq.addEvent(juce::MidiMessage::tempoMetaEvent((int) std::llround(60000000.0 / bpm)), 0.0);
    seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
    for (const auto& e : capturedTake)
        // Clamped at zero rather than dropped: a controller move made before the first note is a
        // value the take should open with, and a negative tick would put the sequence out of
        // order and pair the wrong events in updateMatchedPairs.
        seq.addEvent(juce::MidiMessage(e.bytes, (int) e.size),
                     juce::jmax(0.0, (e.atSec - zero) * ticksPerSecond));

    seq.updateMatchedPairs();

    // Anything still ringing when recording stopped has no note-off in the take. Left alone
    // that is a hanging note in the clip - Live holds it until the next stop, which sounds like
    // Keys emitted a stuck note. Give each one an end just past the last event instead.
    const double end = seq.getEndTime() + (double) ticksPerQuarter / 4.0;
    for (int i = seq.getNumEvents(); --i >= 0;)
    {
        const auto* ev = seq.getEventPointer(i);
        if (ev->message.isNoteOn() && ev->noteOffObject == nullptr)
            seq.addEvent(juce::MidiMessage::noteOff(ev->message.getChannel(),
                                                    ev->message.getNoteNumber()), end);
    }
    seq.updateMatchedPairs();

    out.setTicksPerQuarterNote(ticksPerQuarter);
    out.addTrack(seq);
    return true;
}

juce::File TakeRecorder::takeFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("OK Studio")
               .getChildFile("Keys Takes");
}

// Write the take and, **only on success, make it the one the UI offers.**
//
// Every failure here leaves `lastTake` exactly as it was, and that is deliberate in both
// directions. It used to be neither: three of the paths below returned an empty File without
// touching `lastTake`, so a failed write left the chip enabled, captioned with the *new* take's
// duration, and dragging it handed the host the **previous** take - a wrong file reported as the
// right one, which is worse than an error. And the one path that did assign
// (`return lastTake = {}` when there was nothing to record) threw away a good take already on
// disk because you armed REC by accident and stopped again.
//
// So: a take you cannot write does not replace the take you have. `takeWriteFailed` is what the
// editor reads to say so out loud, since silence would be the same bug one step quieter.
juce::File TakeRecorder::writeTake()
{
    JUCE_ASSERT_MESSAGE_THREAD
    takeWriteFailed = false;

    juce::MidiFile file;
    if (! buildTakeMidiFile(file))
        return lastTake; // nothing was played; not a failure, and not a reason to forget a take

    const auto fail = [this]
    {
        takeWriteFailed = true;
        return lastTake;
    };

    auto folder = takeFolder();
    if (! folder.createDirectory().wasOk())
        return fail();

    const auto stamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d-%H%M%S");
    auto out = folder.getChildFile("Keys take " + stamp + ".mid").getNonexistentSibling();

    juce::FileOutputStream stream(out);
    if (! stream.openedOk())
        return fail();
    if (! file.writeTo(stream))
        return fail();
    stream.flush();
    return lastTake = out;
}

} // namespace keys
