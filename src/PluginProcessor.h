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
class KeysProcessor : public juce::AudioProcessor
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
    // good for sub-block waits: chord-pad strum spread is the one real use. It is NOT a
    // scheduler. juce::MidiMessageCollector empties its queue into the current block on
    // every callback and clamps each event into it, so a delay long enough to matter is
    // simply thrown away. Anything that has to happen meaningfully later has to be held
    // and emitted at real time (see src/mcp/KeysMcp.cpp).
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
    float baseVelocity01() const; // velocity slider through the curve, 0..1 (Humanize aside)
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
    int padPage() const;                 // 0-based; the page the editor is showing
    int padPageOffset() const { return padPage() * padsPerPage; }

    juce::AudioProcessorValueTreeState apvts;

    // The arpeggiator (docs/ARP_DESIGN.md). The editor writes lane atomics directly;
    // globals live in the APVTS ("arpOn", "arpRate", "arpDot", "arpTrip",
    // "arpAnchor", "arpDirection", "arpOctaves", "arpSwing", "arpLatch",
    // "arpRetrigger"). Patterns A-H are message-thread snapshots of the lanes.
    ArpEngine arp;
    static constexpr int numArpPatterns = 8;
    int arpActivePattern() const { return activeArpPattern; }
    void storeActiveArpPattern();          // lanes -> snapshot slot (call before switching)
    void recallArpPattern(int index);      // snapshot slot -> lanes, becomes active
    void copyArpPattern(int from, int to); // whole-pattern copy (the no-modifier answer)
    void randomizeActiveArpPattern();

    // A stored pattern slot (A-H), independent of whichever pattern is currently
    // active/live. Public so the MCP bridge can read or write an arbitrary slot
    // without disturbing the live lanes (unless it happens to be the active one) -
    // the editor never needs this, it only ever touches the live lanes directly.
    struct ArpPattern
    {
        std::array<std::array<int, ArpEngine::maxSteps>, ArpEngine::numLanes> value {};
        std::array<int, ArpEngine::numLanes> length {};
        std::array<int, ArpEngine::numLanes> clockDiv {};
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
        bool knobs = true;      // the CC knob bank
        bool pads = true;       // the chord-pad strip
        bool wheels = true;     // mod + pitch, left of the keybed
        bool keyboard = true;   // the keybed itself
        bool detached = false;  // keybed lives in its own resizable window
        int  view = 0;          // centre view: 0 = perform, 1 = chords, 2 = arp
        juce::Rectangle<int> detachedBounds {}; // empty = never detached yet, so centre it
    };
    LayoutState layout;

protected:
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
    float curved(float pos01) const; // shape a 0..1 velocity position by the Curve param

    juce::MidiMessageCollector collector; // thread-safe UI -> audio message queue
    juce::Random rng; // humanize jitter; touched only on the message thread

    // Display-only refcount of what is sounding, per MIDI note (see isNoteSounding).
    // Atomic because the emitting side is the message thread while readers are paint
    // and timer callbacks; nothing here reaches the audio thread.
    std::array<std::atomic<int>, 128> noteRefs {};
    std::atomic<juce::uint32> soundingGen { 0 };

    std::array<ArpPattern, numArpPatterns> arpPatterns; // message thread only
    int activeArpPattern = 0;                            // message thread only
    juce::MidiBuffer arpScratch;   // audio thread; sized in prepareToPlay
    bool lastArpOn = false;        // audio thread; to flush cleanly on bypass
    double lastKnownBpm = 120.0;   // audio thread; feeds the internal-clock fallback

    std::array<ChordPad, numChordPads> chordPads;          // captured pad definitions
    std::array<std::vector<int>, numChordPads> chordPadOn;  // notes currently sounding per pad

    // Declared last so it tears down first: it binds an ephemeral loopback MCP server
    // (src/mcp/KeysMcp.h) letting Claude Code or any local MCP client drive Keys
    // directly. Harmless during plugin scans (loopback-only, OS-assigned port).
    std::unique_ptr<KeysMcp> mcpBridge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysProcessor)
};
} // namespace keys
