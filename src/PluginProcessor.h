#pragma once

#include "ArpEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace keys
{
class KeysMcp; // src/mcp/KeysMcp.h; only PluginProcessor.cpp needs the full type.

// Keys is a played instrument, not a generator: it makes no sound and emits no
// notes on its own. You click the on-screen piano; it sends MIDI note on/off to
// the track output, to drive whatever instrument sits downstream. All settings
// (size, scale-lock, octave, channel, velocity, sustain, latch) persist with the
// DAW session via the APVTS.
class KeysProcessor : public juce::AudioProcessor,
                      private juce::Timer // strum scheduling; see scheduleNoteOn
{
public:
    KeysProcessor();
    ~KeysProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Keys"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
   #if defined(KEYS_MIDI_EFFECT) && KEYS_MIDI_EFFECT
    bool isMidiEffect() const override { return true; }
   #else
    bool isMidiEffect() const override { return false; }
   #endif
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Note I/O: called from the UI thread; queued and drained on the audio thread.
    // delaySeconds nudges the note-on (or, on noteOff, the note-off) later, and is only
    // good for sub-block waits. It is NOT a scheduler. juce::MidiMessageCollector empties
    // its queue into the current block on every callback and clamps each event into it, so
    // a delay longer than one buffer (~10 ms at 512 samples) is simply thrown away.
    //
    // This comment used to name chord-pad strum as its "one real use". That was wrong, and
    // it hid the bug: Strum asks for up to 200 ms, so the spread was flattened and every
    // chord landed as a block. Strum now goes through scheduleNoteOn instead. Anything
    // that has to happen meaningfully later has to be held and emitted at real time (see
    // scheduleNoteOn here, and src/mcp/KeysMcp.cpp).
    // channelOverride 1..16 sends on that channel instead of the global param (0 = global);
    // the Pad Grid surface uses it so drums land on their own channel.
    void noteOn(int midiNote, float velocity01, double delaySeconds = 0.0, int channelOverride = 0);
    void noteOff(int midiNote, int channelOverride = 0, double delaySeconds = 0.0);
    void allNotesOff();

    // What is sounding, for display only. Every note this processor emits is counted
    // here, whichever source asked for it: the surface, a chord pad, or an MCP tool.
    // The surface paints its own gestures from `pressed`/`latched`/`sustained`, so this
    // is what lets a key light up for a note nobody clicked. Refcounted, because the
    // same note can be asked for twice (a pad and the keyboard) before either releases.
    //
    // soundingGeneration() bumps on every change, so a component can poll cheaply and
    // repaint only when something actually moved.
    bool isNoteSounding(int midiNote) const;
    juce::uint32 soundingGeneration() const { return soundingGen.load(); }

    // What is arriving on the MIDI *input* - a physical keyboard through the standalone's
    // MIDI settings, or a clip and any device above Keys in a DAW track. Keys has always
    // passed that stream through untouched; this only watches it go by, so the on-screen
    // keybed lights up for what someone else is playing and the live chord card can name
    // the chord under their hands. Display only, and a flag per pitch rather than a count:
    // a missed note-off would leak a refcount into a key lit forever.
    std::vector<int> inputNotes() const; // sorted, message thread
    void sendCC(int controller, int value); // e.g. mod wheel = CC1
    void sendPitchBend(int value14);         // 0..16383, centre 8192

    // Hooks for a hosted-instrument subclass (Keys Host): empty defaults, so plain
    // Keys (no hosted instrument) is unaffected. faderMoved is called on every fader
    // move with the position normalized 0..1; faderTargetName lets the fader bank
    // show what a fader is bound to, instead of (or alongside) its CC label.
    virtual void faderMoved(int faderIndex, float value01) { juce::ignoreUnused(faderIndex, value01); }
    virtual juce::String faderTargetName(int faderIndex) const { juce::ignoreUnused(faderIndex); return {}; }

    // Live settings read by the editor / keyboard widget.
    int midiChannel() const;   // 1..16
    int octaveShift() const;   // -5..+5, in octaves
    float baseVelocity01() const; // midpoint of the velocity range, 0..1 (Humanize re-rolls it per note)
    int polyphonyCap() const;  // 0 = unlimited, else max simultaneous notes

    // Chord pads: capture a chord's notes into a slot, click to play/stop it. The pad
    // definitions persist with the session; playback goes through the same note path.
    //
    // Pads are arranged in pages of `padsPerPage`; the editor shows one page at a time and
    // indexes by absolute slot, so a chord left ringing on another page keeps sounding.
    struct ChordPad
    {
        std::vector<int> notes; // absolute MIDI notes captured for this pad
        juce::String name;      // detected label, e.g. "Cm7"
        bool locked = false;    // locked pads survive Regenerate and bias what it produces
        // What the generator made this chord out of. Hand-captured pads leave these at -1;
        // the suggestion UI works them out from the notes on demand.
        int rootPc = -1;
        int type = -1;
        int degree = -1;        // scale degree, so regenerating gives a new chord for the same degree
        juce::String numeral;   // Markov roman numeral ("" = not from the Markov source);
                                // regeneration walks the chain from the previous pad's numeral
    };
    static constexpr int padsPerPage = 16; // Octavium's 4x4 grid per page
    static constexpr int numPadPages = 4;
    static constexpr int numChordPads = padsPerPage * numPadPages;

    const ChordPad& chordPad(int i) const { return chordPads[(size_t) i]; }
    bool chordPadActive(int i) const;
    void setChordPad(int i, const std::vector<int>& notes, const juce::String& name);
    void setChordPad(int i, const ChordPad& pad);
    void clearChordPad(int i);
    void setChordPadLocked(int i, bool locked);
    void moveChordPad(int from, int to); // swap two slots
    void pressChordPad(int i);           // fire the chord now (beat-pad); honours Exclusive
    void releaseChordPad(int i);         // stop it, unless Sustain is holding
    void stopAllChordPads();

    // The live card's chord: whatever the keyboard is holding, fired as one gesture so it
    // is heard strummed and humanized the way a pad plays it, rather than as the sum of
    // the keys under your mouse. Same press/release contract as a pad.
    void pressLiveChord(const std::vector<int>& notes);
    void releaseLiveChord(bool force = false);
    int padPage() const;                 // 0-based; the page the editor is showing
    int padPageOffset() const { return padPage() * padsPerPage; }

    juce::AudioProcessorValueTreeState apvts;

    // The arpeggiator (docs/ARP_DESIGN.md). The editor writes lane atomics directly;
    // globals live in the APVTS ("arpOn", "arpRate", "arpDot", "arpTrip",
    // "arpAnchor", "arpDirection", "arpOctaves", "arpSwing", "arpLatch",
    // "arpRetrigger"). Patterns A-H are message-thread snapshots of the lanes.
    ArpEngine arp;
    // Twelve, not the original eight: the slots stopped being lettered pattern memories and
    // became launchable cards carrying a chord as well as a pattern, and twelve of them fit
    // a bar of the strip. Slots 9-12 come up empty in a session saved with eight.
    static constexpr int numArpPatterns = 12;
    int arpActivePattern() const { return activeArpPattern; }
    void storeActiveArpPattern();          // lanes -> snapshot slot (call before switching)
    void recallArpPattern(int index);      // snapshot slot -> lanes, becomes active
    void copyArpPattern(int from, int to); // whole-pattern copy (the no-modifier answer)
    void randomizeActiveArpPattern();

    // Launch a slot: recall its pattern, apply the shape and rate it remembers, and hold
    // its chord into the arp. That is the whole "pass a card into the arpeggiator" gesture
    // in one click. A slot with no chord launches the pattern alone and arpeggiates
    // whatever is already sounding.
    void launchArpSlot(int index);
    void stopArpSlot();                  // release the launched chord; the pattern stays
    int arpLaunchedSlot() const { return launchedSlot; }
    void setArpSlotChord(int index, const std::vector<int>& notes, const juce::String& name);
    void clearArpSlotChord(int index);

    // Progression mode: walk the slots that hold a chord, giving each one its own number of
    // bars, and launch each in turn. One click plays a twelve-chord song, which is what the
    // slot row has looked like it should do since it stopped being eight lettered buttons.
    // Bars are counted on the audio thread (the only place with a tempo) and the launch is
    // done on the message thread, because launching moves host parameters and fires notes.
    void startChain();
    void stopChain();
    bool chainRunning() const { return chainOn; }
    int chainSlot() const { return chainOn ? chainIndex : -1; }
    void setArpSlotBars(int index, int bars);
    int arpSlotBars(int index) const;

    // Hold a chord into the arp without going through a slot: the Pads section's "To Arp"
    // toggle sends a card here. Held means exactly that - the note-ons are emitted and no
    // note-off follows until the next call, so the arp keeps chewing on it whether or not
    // its own Latch is on. With the arp bypassed the chord simply sustains, which is honest.
    void holdArpChord(const std::vector<int>& notes, const juce::String& name);
    void releaseArpChord();
    const std::vector<int>& arpHeldNotes() const { return arpChordOn; }

    // Hold a chord pad's chord into the arp, remembering which pad it came from so the
    // strip can light it. Same one-at-a-time rule as a slot: a second call swaps.
    void holdArpChordFromPad(int padSlot);
    int arpHeldPad() const { return arpPadSlot; }

    // True when a left-click on a chord card should hand that chord to the arpeggiator and
    // leave it there, rather than play it beat-pad style for as long as the button is down.
    // That used to be its own "To Arp" toggle on the Pads bar; it is simply *the arp being
    // on* since 2026-07-27, because a mode you had to arm separately from the arp read as
    // doing nothing whenever the arp happened to be off. Every surface that shows a chord
    // card asks here, so the pad strip and the generator's grid can never disagree.
    bool cardsFeedArp() const { return apvts.getRawParameterValue("arpOn")->load() > 0.5f; }

    // A stored pattern slot (A-H), independent of whichever pattern is currently
    // active/live. Public so the MCP bridge can read or write an arbitrary slot
    // without disturbing the live lanes (unless it happens to be the active one) -
    // the editor never needs this, it only ever touches the live lanes directly.
    struct ArpPattern
    {
        // Lane defaults, not zeroes. A never-written slot used to recall as every lane at 0,
        // which is not "empty": velocity 0 clamps to a near-silent 0.05 and gate 0 to 5%, so
        // launching a slot nobody had stored made the arp whisper. Found 2026-07-30 while
        // adding lanes 7-10, which would have quietly widened the same hole.
        ArpPattern()
        {
            for (int l = 0; l < ArpEngine::numLanes; ++l)
            {
                value[(size_t) l].fill(ArpEngine::laneDefaults[l]);
                length[(size_t) l] = 8;
                clockDiv[(size_t) l] = 0;
            }
        }

        std::array<std::array<int, ArpEngine::maxSteps>, ArpEngine::numLanes> value {};
        std::array<int, ArpEngine::numLanes> length {};
        std::array<int, ArpEngine::numLanes> clockDiv {};

        // What launching the slot plays, and how. The chord is a copy of a card, not a
        // reference to one: re-generating the pad page must not silently rewrite what a
        // slot launches. -1 on shape or rate means "leave that control alone", which is
        // what a slot that has never been told a shape does.
        std::vector<int> chordNotes;
        juce::String chordName;
        int shape = -1;  // 0..numDirections-1 a direction, numDirections = Pattern
        int rate = -1;   // index into the arpRate choice list
        int bars = 1;    // how long the chain holds this slot before moving on
    };
    const ArpPattern& arpPatternSlot(int index) const;
    void setArpPatternSlot(int index, const ArpPattern& pattern); // refreshes live lanes too if index == active

    // How the editor is folded up. Deliberately not parameters: none of it changes a
    // note, and exposing five booleans to host automation would only add ways to break
    // a session. It lives here rather than in the editor because the editor is created
    // and destroyed every time the window opens, and Owen should get the same layout
    // back. Message thread only; the audio thread never reads it.
    struct LayoutState
    {
        bool controls = true;   // the three header rows
        bool centre = true;     // the centre view (Perform / Chords)
        bool knobs = true;      // the CC knob bank
        bool pads = true;       // the chord-pad strip
        bool arp = false;       // the arpeggiator section (off by default: it is tall)
        bool transcribe = false; // the Transcribe section (off by default: it is tall too)
        bool wheels = true;     // mod + pitch, left of the keybed
        bool keyboard = true;   // the keybed itself

        // Every section can also be popped out into a window of its own (2026-07-27; the
        // keybed and the arp could already, and Owen asked for the rest to follow). One
        // flag and one remembered frame each, in the editor's top-to-bottom order.
        // `detached` keeps its bare name: it is the keybed's, and renaming it would drop
        // the setting out of every session saved before this.
        bool controlsDetached = false;
        bool centreDetached = false;
        bool arpDetached = false;
        bool padsDetached = false;
        bool transcribeDetached = false;
        bool detached = false;  // keybed lives in its own resizable window

        // Two rows of eight, or four rows of four with the full chord card on each. The tall
        // arrangement is what the chord generator used to draw over the top of these same
        // pads; it belongs to the pads now, so it is available under every centre view.
        bool padsBig = false;

        int  view = 0;          // which centre view: 0 = perform, 1 = chords
        int  accent = 0;        // index into skin::accentChoices(); 0 is the OK Studio cyan

        // Where each window was left. Empty = never detached yet, so centre it.
        juce::Rectangle<int> controlsDetachedBounds {};
        juce::Rectangle<int> centreDetachedBounds {};
        juce::Rectangle<int> arpDetachedBounds {};
        juce::Rectangle<int> padsDetachedBounds {};
        juce::Rectangle<int> transcribeDetachedBounds {};
        juce::Rectangle<int> detachedBounds {};     // the keybed's, named for the flag above
    };
    LayoutState layout;

protected:
    // Everything both products restore from a saved session. Keys Host adds its instrument
    // on top of this rather than repeating the list; see the definition.
    void restoreSharedState(const juce::ValueTree& root);
    // Repairs a session saved before Strum became a range; see the definition for the tell.
    void migrateStrumRange(const juce::ValueTree& root);

    juce::ValueTree layoutToTree() const;
    void layoutFromTree(const juce::ValueTree& root);

    juce::ValueTree arpToTree() const;              // all patterns + the live lanes
    void arpFromTree(const juce::ValueTree& root);
    // Chord-pad state as a "chordPads" ValueTree, shared with subclasses (Keys Host
    // nests it next to its own hosted-instrument tree under the same "KEYS" root, so
    // sessions stay interchangeable between Keys and Keys Host).
    juce::ValueTree chordPadsToTree() const;
    void chordPadsFromTree(const juce::ValueTree& root); // clears pads, then loads the "chordPads" child if present

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void stopChordPad(int i);

    // Shared by the pads and the live card: cap, order, strum-schedule a chord. Returns
    // the notes actually fired (post polyphony cap), for the caller to remember.
    std::vector<int> fireChord(const std::vector<int>& notes, int tag);
    // Scheduling tags. Pads use their own slot (>= 0); everything else is negative, and
    // cancelScheduledNotes must compare against the exact tag, never `< 0` - see the comment
    // there for the stuck-note this caused.
    static constexpr int panicTag = -1;     // cancelScheduledNotes only: cancel *everything*
    static constexpr int liveChordTag = -2; // the live "current chord" card
    static constexpr int arpChordTag = -3;  // the chord held into the arp
    std::vector<int> liveChordOn;
    std::vector<int> arpChordOn;   // notes currently held into the arp (empty = none)
    juce::String arpChordName;
    int launchedSlot = -1;         // arp slot whose chord is held, or -1
    int arpPadSlot = -1;           // chord pad whose chord is held, or -1

    // Hold a note-on and emit it `delayMs` from now, on the message thread.
    //
    // noteOn's own delaySeconds cannot do this. It timestamps the message and hands it to
    // juce::MidiMessageCollector, which empties its *entire* queue into the current block
    // on every callback and clamps each event into it - so anything beyond one buffer
    // (~10 ms at 512 samples) collapses onto the end of that buffer. Strum asks for up to
    // 200 ms, so the spread was simply thrown away and every chord landed as a block.
    //
    // Message thread only, same approach as the MCP bridge's deferred notes. The timer
    // runs only while something is pending, so an idle plugin costs nothing.
    void scheduleNoteOn(int note, float vel01, int channel, double delayMs, int padSlot);
    // Drops this tag's un-fired notes (panicTag drops everything) and returns the pitches it
    // dropped, so a caller releasing the chord can tell which of its notes never sounded.
    std::vector<int> cancelScheduledNotes(int padSlot);
    // Cancel `tag`'s queued note-ons, release the notes that did sound, and empty `sounding`.
    // Every chord source releases through here; see the definition for why the two halves
    // cannot be done independently.
    void releaseNotes(std::vector<int>& sounding, int tag);
    void timerCallback() override;

    struct DeferredNote
    {
        int note;
        float vel01;
        int channel;
        double atMs;
        int padSlot; // so stopping one pad drops only its own un-fired notes
    };
    std::vector<DeferredNote> deferred; // sorted by atMs; message thread only

    juce::MidiMessageCollector collector; // thread-safe UI -> audio message queue
    juce::Random rng; // humanize jitter; touched only on the message thread

    // Display-only refcount of what is sounding, per MIDI note (see isNoteSounding).
    // Atomic because the emitting side is the message thread while readers are paint
    // and timer callbacks; nothing here reaches the audio thread.
    std::array<std::atomic<int>, 128> noteRefs {};
    std::atomic<juce::uint32> soundingGen { 0 };

    // Notes seen arriving on the MIDI input (see inputNotes). Written on the audio thread,
    // read on the message thread; a plain flag per pitch, never a count.
    std::array<std::atomic<bool>, 128> inputNoteOn {};
    void watchInputNotes(const juce::MidiBuffer&); // audio thread, before anything consumes it
    void clearInputNotes();

    std::array<ArpPattern, numArpPatterns> arpPatterns; // message thread only
    // The slot chords, mirrored into atomics for the Chord lane to read on the audio thread.
    // Rebuilt whole by syncArpChordTable() from every message-thread path that can change a
    // slot's chord - there is no single choke point, so the call sites are the contract.
    ArpEngine::ChordTable arpChordTable;
    void syncArpChordTable();

    // --- Progression mode ---------------------------------------------------------------
    // The heartbeat is a second timer, separate from the strum scheduler above (which stops
    // itself the moment nothing is queued). It runs for the life of the processor because
    // two things need a pulse that outlives the editor: the chain, and releasing a chord
    // held into the arp when the arp is switched off. That release used to live in the
    // editor's timer, so it did nothing at all with no window open - a host or an MCP client
    // writing arpOn false left the chord droning with no click left to stop it.
    struct Heartbeat : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { if (tick) tick(); }
    };
    Heartbeat heartbeat;
    void heartbeatTick();

    bool chainOn = false;       // message thread
    int chainIndex = -1;        // message thread: the slot currently playing
    bool lastArpOnHeartbeat = false;
    std::atomic<bool> chainActive { false };   // message -> audio: count bars at all
    std::atomic<bool> chainAdvance { false };  // audio -> message: this slot's bars are up
    std::atomic<int> chainEpoch { 0 };         // message -> audio: restart the count
    std::atomic<double> chainTargetBeats { 4.0 };
    double chainBeatsPlayed = 0.0; // audio thread only
    int chainSeenEpoch = 0;        // audio thread only
    void advanceChainClock(int numSamples); // audio thread; raises chainAdvance, never launches
    int nextChainSlot(int from) const; // the next slot holding a chord, wrapping; -1 if none
    int activeArpPattern = 0;                            // message thread only
    juce::MidiBuffer arpScratch;   // audio thread; sized in prepareToPlay
    bool lastArpOn = false;        // audio thread; to flush cleanly on bypass

    std::array<ChordPad, numChordPads> chordPads;          // captured pad definitions
    std::array<std::vector<int>, numChordPads> chordPadOn;  // notes currently sounding per pad

    // Declared last so it tears down first: it binds an ephemeral loopback MCP server
    // (src/mcp/KeysMcp.h) letting Claude Code or any local MCP client drive Keys
    // directly. Harmless during plugin scans (loopback-only, OS-assigned port).
    std::unique_ptr<KeysMcp> mcpBridge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysProcessor)
};
} // namespace keys
