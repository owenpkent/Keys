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
    //
    // `dest` says which stream the note is queued into: 0 is the track output, 1..numArpLines
    // is that arp line's own input, which only its engine ever sees. Routing has to be decided
    // here, at the source, because the audio thread cannot recover who asked for a note from
    // the MidiMessage that arrives - see the note beside `lines` for why the obvious
    // alternative (a per-pitch ownership mask read on the audio thread) races and strands notes.
    void noteOn(int midiNote, float velocity01, double delaySeconds = 0.0, int channelOverride = 0,
                int dest = 0);
    void noteOff(int midiNote, int channelOverride = 0, double delaySeconds = 0.0, int dest = 0);
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

    // Which pitch the arpeggiator is sounding *right now*, so the keybed can light up as it
    // runs (2026-08-02, Owen: "another option for showing it when it's actually playing on the
    // keyboard on the bottom"). Answers false with the option off, which is the whole of what
    // the option does - the flags are kept either way, since they cost one atomic store per
    // note event and a toggle that has to wait for the next note to take effect reads as broken.
    //
    // **Deliberately not part of isNoteSounding.** That answer feeds the live chord card as
    // well, and an arpeggio is a run of single notes: folded in there it would rewrite the
    // "current chord" as whichever note the arp is on. Only the keybed asks this one.
    bool arpNoteLit(int midiNote) const;

    // What the on-screen keybed should light, which is deliberately **not** the same question
    // as isNoteSounding. With Light keys on and a line running, the chord handed to that line
    // is not lit: it is the *input* to the run, so every pitch the arp is chewing stays on and
    // the arpeggio moving inside it is invisible. That is exactly how the option looked when it
    // first landed - "it just shows the chords that are being played" - and hiding the input is
    // what makes the output visible. With the option off, or with the line not running, the
    // held chord lights as it always has.
    //
    // Only NoteSurface asks this. Everything else - the live chord card above all - keeps
    // asking isNoteSounding, which must keep naming the chord.
    bool keybedLit(int midiNote) const;

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
    // Twelve since 2026-08-03 (Owen: "reduce the pads grid to 12"), as two rows of six - the
    // two columns that freed up carry Strum and Humanize as range knobs. It was 16 (Octavium's
    // 4x4 page) and 8 before that; `chordPadsFromTree` re-bases a saved session's slots into
    // whatever this is, and is the one place that has to know the count ever moved.
    static constexpr int padsPerPage = 12;
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
    // Every chord source at once: the pads, the live card and the chord held into each arp
    // line. `includeArpHolds` false leaves the lines alone and stops only the pads and the live
    // card - see holdArpChordNow, the one caller that passes it, and the reason it exists.
    void stopAllChordPads(bool includeArpHolds = true);

    // The live card's chord: whatever the keyboard is holding, fired as one gesture so it
    // is heard strummed and humanized the way a pad plays it, rather than as the sum of
    // the keys under your mouse. Same press/release contract as a pad.
    void pressLiveChord(const std::vector<int>& notes);
    void releaseLiveChord(bool force = false);
    int padPage() const;                 // 0-based; the page the editor is showing
    int padPageOffset() const { return padPage() * padsPerPage; }

    juce::AudioProcessorValueTreeState apvts;

    // The arpeggiator (docs/ARP_DESIGN.md). The editor writes lane atomics directly;
    // globals live in the APVTS ("arpOn", "arpRate", "arpDot", "arpTuplet",
    // "arpAnchor", "arpDirection", "arpOctaves", "arpSwing", "arpLatch",
    // "arpRetrigger"). Patterns A-H are message-thread snapshots of the lanes.
    //
    // Three of them since 2026-08-01, so Keys can hold a polyrhythm: three independent
    // arpeggiators at three rates, each with its own twelve slots, its own chord and its own
    // chain. Line 0 is the arpeggiator that has always been here, down to its parameter IDs,
    // and with lines 1 and 2 off nothing about it is different.
    static constexpr int numArpLines = 3;
    // How many of them the product actually has: **two**, by Owen's call on 2026-08-02 ("I only
    // wanna view two arpeggiators in this window"). Three rows fit, but two fit *comfortably*,
    // and the point of the view is dragging a chord card up from the strip below onto either
    // line - a target you have to aim at is a target he cannot use.
    //
    // Two constants rather than one because line C's *parameters* stay registered. Dropping them
    // from the layout is what breaks every saved session (the invariant in CLAUDE.md), so the
    // storage, the engines and the `arp3*` ids all stay exactly where they were and nothing
    // reaches them: `arpLineOn` answers false above this bound, which makes line C inert at the
    // one place the audio stage asks, and the UI builds no chip, tab or row for it. Raising this
    // back to `numArpLines` is all it takes to bring C back.
    static constexpr int uiArpLines = 2;
    static_assert(uiArpLines <= numArpLines, "the UI cannot show a line that has no engine");
    ArpEngine& arpLine(int line);
    const ArpEngine& arpLine(int line) const;

    // A line's parameter IDs. Line 0 keeps the bare names every saved session already carries
    // ("arpRate"); lines 1 and 2 are "arp2Rate" / "arp3Rate". That is the whole
    // session-compatibility story, so nothing may renumber it - and as ever with the choice
    // parameters behind these names (arpRate, arpDirection), append, never insert.
    static juce::String arpParamId(int line, juce::StringRef suffix);
    // Shorthand for the commonest read of all: is this line switched on.
    bool arpLineOn(int line) const;

    // Every per-line parameter, in one list. It exists so the audio thread never has to build
    // an id: a juce::String per parameter per line per block would be twenty-odd allocations
    // a block, on the one thread that may not allocate at all. Each line caches the raw
    // pointers once (see ArpLine::param) and reads through them.
    //
    // The UI uses it too, to name the parameter an attachment binds to when the current line
    // changes. Order here is free - it is an array index, never serialized - but the *suffix*
    // strings are the ids and must not change.
    enum ArpParam { apOn = 0, apRate, apRateFree, apRateHz, apDot, apTrip, apAnchor,
                    apDirection, apPattern, apLinkLanes, apOctaves, apSwing, apLatch,
                    apRetrigger, apGate, apChance, apDistance, apOffset, apRetrigBars,
                    apVelRamp, apRampBeats, apHumanize, apKeys, apChannel,
                    // Appended 2026-08-02, both defaulting to what the arp did without them.
                    // OctShift transposes the whole run and is centred at 0; Octaves beside it
                    // still *stacks* and still only widens - two questions, two controls.
                    apOctShift, apVolume,
                    // Appended later the same day (Owen's macro-view pass). HumanVel is the
                    // velocity half of Humanize, split out so timing and dynamics randomize
                    // independently; Humanize itself is timing-only from here on. VelTrim is
                    // the bipolar velocity control that replaced VOL on the macro row: centred
                    // at 0 (as played), up boosts, down cuts. Volume stays registered - old
                    // sessions carry it - but migrateVelTrim folds it into VelTrim on load and
                    // nothing in the UI writes it any more.
                    apHumanVel, apVelTrim,
                    // Appended 2026-08-03 (Owen: "what if I want 1/5 or other division?").
                    // Trip could only ever mean 3-in-the-space-of-2; Tuplet is a choice over
                    // five and is what the combo on the sub-row writes now. Trip
                    // stays registered above - old sessions carry it, and migrateTuplet folds
                    // it into this on load - but nothing reads it after that, exactly as
                    // Volume was retired into VelTrim.
                    apTuplet,
                    // Appended 2026-08-03 with the range knobs. Each Humanize control became a
                    // range: the existing two stay the ceiling, and these say how far under it
                    // the draw may fall. Default 100 - a span of the whole scale - leaves
                    // exactly what those two did alone.
                    apHumanizeSpan, apHumanVelSpan, apDrift, numArpParams };
    static const char* arpParamSuffix(int which);
    // The Tuplet choice list, one copy: the strings the parameter offers and the N each index
    // means. Index 0 is straight; the rest are N-in-the-space-of-ArpEngine::tupletSpace(N).
    // Appending here is safe and is how a 11 or a 13 would arrive; inserting renumbers what
    // every saved session and automation lane already holds.
    //
    // "Straight" and "Triplet" are the words Reaper's own straight/triplet/dotted selector
    // uses, so the two everyone already knows read as they do everywhere else; the rest carry
    // the number, since there is no household word for a 7. The combo names the *family* and
    // the dial's readout names the resulting length ("1/10"), which is the division of labour
    // the fraction notation makes possible - see ArpEngine::rateSyncText.
    static juce::StringArray tupletChoices()
    {
        return { "Straight", "Triplet", "5-tuplet", "7-tuplet", "9-tuplet" };
    }
    static int tupletFor(int choiceIndex);
    // `which`'s id on `line`: "arpRate", "arp2Rate", "arp3Rate".
    static juce::String arpParamId(int line, ArpParam which) { return arpParamId(line, arpParamSuffix(which)); }
    float arpParam(int line, ArpParam which) const;

    // Twelve, not the original eight: the slots stopped being lettered pattern memories and
    // became launchable cards carrying a chord as well as a pattern, and twelve of them fit
    // a bar of the strip. Slots 9-12 come up empty in a session saved with eight.
    // Twelve *per line* since the lines arrived: a slot card is one line's launchable card,
    // which is what lets the one row on screen read as whichever line the tabs have selected.
    static constexpr int numArpPatterns = 12;
    int arpActivePattern(int line = 0) const;
    void storeActiveArpPattern(int line = 0);          // lanes -> snapshot slot (call before switching)
    void recallArpPattern(int index, int line = 0);    // snapshot slot -> lanes, becomes active
    void copyArpPattern(int from, int to, int line = 0); // whole-pattern copy (the no-modifier answer)
    void randomizeActiveArpPattern(int line = 0);
    // Reroll one lane of the active pattern, straying `amountPct` (0..100) from what is drawn:
    // a nudge low down, a uniform scramble at 100. The per-lane twin of the whole-pattern
    // randomize above; see the definition for why both exist.
    // `fromStep`/`toStep` are an inclusive span; pass -1 for either to mean the whole lane.
    // The span is what Select on the Draw page marks (2026-08-14, from Kirnu Cream's Random
    // tool, which acts on selected steps rather than everything).
    void rerollArpLane(int line, int laneIndex, int amountPct, int fromStep = -1, int toStep = -1);
    // Put one lane back to its default across its whole length - the state a lane that has
    // never been touched is in. The way back from a reroll, since Keys has no undo anywhere.
    void resetArpLane(int line, int laneIndex, int fromStep = -1, int toStep = -1);
    // Writes a Euclidean rhythm (EuclidGen.h) into one lane of the active pattern: 100 on a
    // hit, 0 on a rest, and sets that lane's length to `steps`. Only the probability lane has
    // a meaningful hit/rest mapping today - anything else returns false and writes nothing.
    bool applyEuclidToActiveArpPattern(int line, int hits, int steps, int rotation,
                                        int laneIndex = ArpEngine::laneProbability);

    // Launch a slot: recall its pattern, apply the shape and rate it remembers, and hold
    // its chord into the arp. That is the whole "pass a card into the arpeggiator" gesture
    // in one click. A slot with no chord launches the pattern alone and arpeggiates
    // whatever is already sounding.
    void launchArpSlot(int index, int line = 0);
    void stopArpSlot(int line = 0);      // release the launched chord; the pattern stays
    int arpLaunchedSlot(int line = 0) const;
    void setArpSlotChord(int index, const std::vector<int>& notes, const juce::String& name, int line = 0);
    void clearArpSlotChord(int index, int line = 0);

    // Progression mode: walk the slots that hold a chord, giving each one its own number of
    // bars, and launch each in turn. One click plays a twelve-chord song, which is what the
    // slot row has looked like it should do since it stopped being eight lettered buttons.
    // Bars are counted on the audio thread (the only place with a tempo) and the launch is
    // done on the message thread, because launching moves host parameters and fires notes.
    void startChain(int line = 0);
    void stopChain(int line = 0);
    bool chainRunning(int line = 0) const;
    int chainSlot(int line = 0) const;
    void setArpSlotBars(int index, int bars, int line = 0);
    int arpSlotBars(int index, int line = 0) const;

    // Hold a chord into the arp without going through a slot: a click on a chord card with
    // the arp On sends it here. Held means exactly that - the note-ons are emitted and no
    // note-off follows until the next call, so the arp keeps chewing on it whether or not
    // its own Latch is on. With the arp bypassed the chord simply sustains, which is honest.
    // Clicking the same card again retriggers the hold rather than ending it, so the way out
    // is releaseArpHold: the Hold off chip on the arp bar, or the panel's Stop button.
    void holdArpChord(const std::vector<int>& notes, const juce::String& name, int line = 0);
    void releaseArpChord(int line = 0);

    // --- Launch Quantize ------------------------------------------------------------------
    // With `arpQuantize` off, every gesture that fires a chord happens the instant you ask,
    // which is what Keys has always done. With it set, the gesture is *held* until the next
    // 1/16, 1/4, bar (and so on) and then happens whole - so a card can only land on the grid.
    // See the parameter's comment in createLayout for why it is global rather than per line.
    //
    // Only the gestures that fire something go through here. Playing the keybed never does.
    bool arpQuantizeOn() const;
    // How long a launch asked for *now* would wait, in milliseconds. 0 when quantize is off or
    // the boundary is already here. Message thread.
    double arpQuantizeDelayMs() const;
    // What is waiting, for the UI to show as pending. -1 = nothing waiting on that line.
    int arpPendingSlot(int line) const;
    bool arpLaunchPending(int line) const;

    // --- Tempo Sync -------------------------------------------------------------------------
    // True the block a rolling host actually overrode the "bpm" parameter (bpmSync on, a
    // transport playing, a valid tempo reported). Published so the Controls bar can show the
    // host's tempo in the field and greys its own stepper and drag: neither can change
    // anything while this is true. Written once a block in advanceChainClock, read on the
    // message thread in KeysEditor::timerCallback - not a sample-accurate answer and does not
    // need to be, the same contract as arpBeats beside it.
    bool hostTempoLive() const { return arpHostBpmLive.load(std::memory_order_relaxed); }
    // The tempo the arp's beat clock is actually running at this block: the host's own while
    // hostTempoLive() is true, otherwise the "bpm" parameter's value (see arpBeatsBpm, which
    // this reads). The Controls bar's tempo field reads this to show the host's number when
    // Tempo Sync has actually taken over, rather than the "bpm" parameter it is not using.
    double currentTempo() const { return arpBeatsBpm.load(std::memory_order_relaxed); }

    // --- Take: Keys records itself ------------------------------------------------------
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
    void setRecording(bool shouldRecord);
    bool isRecording() const { return recording.load(std::memory_order_relaxed); }

    // The take as it stands, message thread. `capturedSeconds` is first event to last, so an
    // armed-but-silent minute before you played does not count and is not written.
    int capturedEventCount() const { return (int) capturedTake.size(); }
    double capturedSeconds() const;

    // The take as a type-0 MIDI file at the tempo the arp clock is running at, note-offs
    // supplied for anything still ringing when recording stopped, and shifted so the first
    // event sits at zero. False when there is nothing to write.
    bool buildTakeMidiFile(juce::MidiFile& out) const;

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

    // What "let go of the held chord" means to a *user*, and the only thing the UI should
    // call. releaseArpChord() alone is not it: with the chain running it drops the chord and
    // leaves chainOn set, so the next bar boundary launches the following slot and the chord
    // is back. A button that undoes itself a bar later reads as broken, so the chain is part
    // of the hold as far as letting go is concerned, and the rule lives here with the state
    // rather than in whichever surface happens to own the button.
    void releaseArpHold();

    // **All Off, for the arpeggiator** (2026-08-02, Owen: "we need an all off button in the
    // arpeggiator section as well"). Switches every line off, then lets go of everything:
    // holds released, chains stopped, pending quantized launches dropped.
    //
    // Switching the lines off is what makes it "off" rather than a second Hold off. Releasing
    // without switching off does not stop an arpeggiator - the engine is still running, so it
    // picks straight back up on whatever the keybed is holding, and the button would silence
    // the room for a sixteenth note. The line switches are what "off" means on this bar, so
    // that is what it turns off.
    void allArpOff();

    // Empty when nothing is held. Hold off reads this - together with chainRunning(), since a
    // chain about to fire the next chord is something to let go of too - to grey itself out.
    const std::vector<int>& arpHeldNotes(int line = 0) const;
    const juce::String& arpHeldName(int line = 0) const;
    // Is *any* line holding something, or chaining? Hold off is one button for all three, so
    // it asks one question. Same reason allNotesOff has to forget all three.
    bool anyArpHold() const;

    // Hold a chord pad's chord into the arp, remembering which pad it came from so the
    // strip can light it. Same one-at-a-time rule as a slot: a second call swaps.
    void holdArpChordFromPad(int padSlot, int line = 0);
    int arpHeldPad(int line = 0) const;
    // Which line is holding this pad, or -1. The strip paints a held card in its line's
    // colour, so it asks by pad rather than by line.
    int arpLineHoldingPad(int padSlot) const;

    // True when a left-click on a chord card should hand that chord to the arpeggiator and
    // leave it there, rather than play it beat-pad style for as long as the button is down.
    // That used to be its own "To Arp" toggle on the Pads bar; it is simply *the arp being
    // on* since 2026-07-27, because a mode you had to arm separately from the arp read as
    // doing nothing whenever the arp happened to be off. Every surface that shows a chord
    // card asks here rather than caching a mode of its own, which is what keeps the answer
    // the same wherever a card is drawn.
    // With more than one line the question is "is any line on", and the answer to "which one gets
    // the chord" is the current line (below) rather than a second mode.
    bool cardsFeedArp() const;

    // The current line: which one the arp panel edits, and which one a click on a chord card
    // hands its chord to. One piece of state behind three surfaces - the A/B tabs on the
    // slot row, the letter chip on the Pads bar, and the corner mark a card wears once it has
    // been sent somewhere. It lives with the layout because it is the same kind of thing: not
    // a parameter (it changes no note by itself), message thread only, and Owen should get it
    // back where he left it.
    int arpCurrentLine() const;
    void setArpCurrentLine(int line);

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
        // The rate's *mode*, captured with it. A slot stored while the rate was in Hz has to
        // bring the Hz value and the switch back with it, or launching it drops the user into
        // Sync at whatever division `rate` happens to hold - silently, since the pattern and
        // the chord would both be right. Read only when `rate >= 0`, which stays the single
        // "this slot remembers a rate at all" flag - and `rateHz` only when `rateFree` is
        // also set, because a Sync slot has no Hz value of its own to install (a session
        // saved before the mode reads back as the default 8, which is not anything the user
        // asked for and must not overwrite what they dialled in).
        bool rateFree = false;
        float rateHz = 8.0f;
        int bars = 1;    // how long the chain holds this slot before moving on
        // Subharmonicon-style rhythm dividers (2026-08-14), stored with the slot the same way
        // the lanes are: 1..16, 0 = off, default all off so an old session reads back inert.
        std::array<int, 4> rhythmDivs { 0, 0, 0, 0 };
        // 0 = chord tones (default, today's Harmony lane), 1 = subharmonic.
        int harmonyMode = 0;
    };
    const ArpPattern& arpPatternSlot(int index, int line = 0) const;
    void setArpPatternSlot(int index, const ArpPattern& pattern, int line = 0); // refreshes live lanes too if index == active

    // How the editor is folded up. Deliberately not parameters: none of it changes a
    // note, and exposing five booleans to host automation would only add ways to break
    // a session. It lives here rather than in the editor because the editor is created
    // and destroyed every time the window opens, and Owen should get the same layout
    // back. Message thread only; the audio thread never reads it.
    struct LayoutState
    {
        bool controls = true;   // the header rows and the knob bank under them
        // Vestigial since 2026-08-02: the Knobs chip that folded the CC knob bank off the
        // bottom of the Controls band is gone, and the bank is unconditional whenever the
        // section itself is open. Kept, always true, so layoutToTree()/layoutFromTree() keep
        // round-tripping a session's tree without a special case for one dropped field.
        bool knobs = true;
        bool pads = true;       // the chord-pad strip
        bool arp = false;       // the arpeggiator section (off by default: it is tall)
        bool wheels = true;     // mod + pitch, left of the keybed
        bool keyboard = true;   // the keybed itself

        // Every section can also be popped out into a window of its own (2026-07-27; the
        // keybed and the arp could already, and Owen asked for the rest to follow). One
        // flag and one remembered frame each, in the editor's top-to-bottom order.
        // `detached` keeps its bare name: it is the keybed's, and renaming it would drop
        // the setting out of every session saved before this.
        bool controlsDetached = false;
        bool arpDetached = false;
        bool padsDetached = false;
        bool detached = false;  // keybed lives in its own resizable window

        // The chord generator's window (2026-07-30, Owen's call). Not a section: it is never
        // docked, has no bar and no fold, and opens from a button on the Pads bar. It is in
        // here all the same because it is the same question - where did Owen leave a window,
        // and was it open - and the answer has to survive the editor closing.
        bool chordGen = false;

        // Which arp line the panel is editing and a chord card feeds. See arpCurrentLine().
        int  arpLine = 0;
        // ...and whether it is showing the macro view instead of that line's own controls.
        // Same kind of state and the same reason it lives here: the panel is destroyed every
        // time the section folds, and Owen should get back the view he left.
        //
        // **Default true from 2026-08-02**, which is what "view two arpeggiators" means in
        // practice: a fresh instance opens with both lines on screen as rows, over the chord
        // strip you drag from. The A / B tabs are still there for the step lanes and the twelve
        // slots, which are per-line and have nowhere to live in a row.
        bool arpMacro = true;
        // Which page of a line's deep view is showing (2026-08-14, Owen: "can we simplify the
        // detail view or organize into pages"). Values are ArpPanel::Page: 0 = Draw (the step
        // lanes), 1 = Cards (the twelve slots), 2 = Play (rate, shape, feel).
        //
        // **Defaults to 2, Play**, and that is not arbitrary: Draw does nothing until you have
        // drawn on it *and* set Shape to Pattern, so opening there is opening on a blank page
        // with no way to tell why. Owen landed on exactly that - one step long, Shape on
        // Pattern - and said "I don't understand this layout. how to get the sound I want".
        // Play is where rate and shape are, which is the answer to that question.
        //
        // The deep view used to be all four blocks at once - band, lanes, slots, actions - at
        // 612 px against the macro view's 240, so clicking Details grew the *window* by 372 px
        // and clicking All shrank it back. Paged, every page fits inside one fixed panel
        // height, and the window stops moving between views entirely. Same reason this lives
        // here rather than in the panel as arpMacro does: the panel is destroyed every time
        // the section folds, and Owen should get back the page he left.
        int  arpPage = 2;
        // Whether the keybed lights up for the notes the arp is *playing*, as opposed to the
        // chord it was handed (which lights it either way, through noteRefs). Layout state and
        // not a parameter: it changes what is drawn and nothing that is heard, so there is
        // nothing here for a host to automate. On by default - it is the thing Owen asked to
        // be able to see - and one click on the arp bar turns it off when the flicker of a
        // 1/16 run is not what you want to be looking at.
        bool arpLights = true;

        int  accent = 0;        // index into skin::accentChoices(); 0 is the OK Studio cyan

        // Where each window was left. Empty = never detached yet, so centre it.
        juce::Rectangle<int> controlsDetachedBounds {};
        juce::Rectangle<int> arpDetachedBounds {};
        juce::Rectangle<int> padsDetachedBounds {};
        juce::Rectangle<int> detachedBounds {};     // the keybed's, named for the flag above
        juce::Rectangle<int> chordGenBounds {};     // the generator's window
    };
    enum class UndoScope { pads, arp };

    // --- Undo (2026-08-14, Owen: "we should have undo") ---------------------------------
    //
    // **Content only, and that is the design, not a shortcut.** Undo covers what destroys
    // music - chord pads, arp lanes, arp slots - and deliberately not parameters. Sweeping the
    // rate dial would otherwise push forty entries onto the stack and shove the pad you
    // actually wanted back off the end of it, which is an undo that cannot undo anything.
    // A knob you can always turn back; a cleared pad you cannot.
    //
    // "no undo anywhere in Keys" was the stated reason for at least four design compromises -
    // Reset beside Roll, Clear page living in a window rather than on a bar, locks on pads, and
    // the drag guard in ChordPads::mouseUp. None of them should be removed on the strength of
    // this: they are all still good behaviour, they just stop being *load-bearing*.
    //
    // An entry is a **snapshot of the affected subtree before the edit**, taken with the same
    // chordPadsToTree/arpToTree the session file uses. That is why this is affordable at all:
    // no action needs a hand-written inverse, so no action can have a *wrong* one, and a
    // feature added later is undoable the moment its data is in one of those two trees.

    // Snapshot before an edit. **One call per gesture, not per change** - a lane drag calls it
    // on mouse-down and not again, or a single stroke would fill the stack by itself. Nested
    // calls are ignored (see UndoGesture), so a high-level action that internally clears and
    // then sets a pad still costs one entry.
    void pushUndo(const juce::String& label, UndoScope scope);

    // RAII around pushUndo, and the reason nesting is safe: a drop calls clearChordPad then
    // setChordPad, and each of those may push on its own behalf. The outermost gesture wins and
    // the inner ones are no-ops, so an entry is always the state before the *whole* action.
    struct UndoGesture
    {
        UndoGesture(KeysProcessor& p, const juce::String& label, UndoScope scope);
        ~UndoGesture();
        KeysProcessor& processor;
    };

    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    // What the next undo/redo would put back, for the button's tooltip. Empty when there is none.
    juce::String undoLabel() const { return undoStack.empty() ? juce::String() : undoStack.back().label; }
    juce::String redoLabel() const { return redoStack.empty() ? juce::String() : redoStack.back().label; }
    void undo();
    void redo();
    void clearUndoHistory();
    // Bumped on every push, undo and redo, so the editor can poll cheaply and only repaint the
    // buttons when something actually moved - the soundingGeneration() pattern.
    juce::uint32 undoGeneration() const { return undoGen.load(); }

    // The stacks the API above drives. Public alongside `layout` rather than tucked away,
    // because that is how this processor already exposes its state to the editor.

    // The undo stacks. Depth 32: deep enough that a run of edits stays recoverable, shallow
    // enough that the arp snapshots (twelve slots x thirteen lanes x thirty-two steps, twice)
    // do not add up to real memory. Oldest is dropped from the front when it overflows.
    struct UndoEntry
    {
        juce::String label;
        UndoScope scope;
        juce::ValueTree before;
    };
    static constexpr int maxUndoDepth = 32;
    std::vector<UndoEntry> undoStack, redoStack;
    int undoGestureDepth = 0;      // >0 means a gesture is open and pushes are absorbed
    std::atomic<juce::uint32> undoGen { 0 };
    juce::ValueTree snapshotFor(UndoScope scope) const;
    void restore(const UndoEntry& e);

    LayoutState layout;

protected:
    // Everything both products restore from a saved session. Keys Host adds its instrument
    // on top of this rather than repeating the list; see the definition.
    void restoreSharedState(const juce::ValueTree& root);
    // Repairs a session saved before Strum became a range; see the definition for the tell.
    void migrateStrumRange(const juce::ValueTree& root);
    // Same shape: puts the arp rate back in Sync when a session predates the Hz mode, since an
    // absent parameter keeps the live instance's current value rather than resetting.
    void migrateRateMode(const juce::ValueTree& root);
    // Same shape again: a session saved before VelTrim existed carries its line levels in
    // Volume. Fold each line's Volume into VelTrim (volume% == 1 + (volume-100)/100 exactly,
    // so the session sounds identical) and put Volume back to 100, and write HumanVel's
    // default explicitly so it does not inherit the live instance's value.
    void migrateVelTrim(const juce::ValueTree& root);
    // Same shape a third time: a session saved before Tempo Sync existed has no "bpmSync" at
    // all, and its absence is not "off" - it is a live instance's *current* value, which the
    // repair overwrites with the parameter's own default (true), reproducing exactly what
    // Keys always did before this parameter existed.
    void migrateBpmSync(const juce::ValueTree& root);
    // Trip became Tuplet on 2026-08-03. Same absence tell, same repair, plus the one fold that
    // makes an old session sound identical: a set Trip is a Tuplet of 3.
    void migrateTuplet(const juce::ValueTree& root);
    // The Humanize spans, appended 2026-08-03. Absence is the tell and the default is the
    // repair; there is no older parameter to fold, unlike the two above.
    void migrateHumanSpans(const juce::ValueTree& root);

    juce::ValueTree layoutToTree() const;
    void layoutFromTree(const juce::ValueTree& root);

    juce::ValueTree arpToTree() const;              // all patterns + the live lanes
    void arpFromTree(const juce::ValueTree& root);
    // One line's twelve slots and which of them is live, under whichever node it lives in.
    // Line 0's sit directly on the "arp" node, exactly where they always have; B and C get a
    // "line" child each. See arpToTree for why that shape.
    void arpLineToTree(juce::ValueTree& dest, int line) const;
    void arpLineFromTree(const juce::ValueTree& src, int line, int savedShapeBase);
    // Chord-pad state as a "chordPads" ValueTree, shared with subclasses (Keys Host
    // nests it next to its own hosted-instrument tree under the same "KEYS" root, so
    // sessions stay interchangeable between Keys and Keys Host).
    juce::ValueTree chordPadsToTree() const;
    void chordPadsFromTree(const juce::ValueTree& root); // clears pads, then loads the "chordPads" child if present

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    // One arp line's parameters, called once per line by createLayout. See its definition.
    static void addArpLineParams(juce::AudioProcessorValueTreeState::ParameterLayout&, int line);

    void stopChordPad(int i);

    // Shared by the pads and the live card: cap, order, strum-schedule a chord. Returns
    // the notes actually fired (post polyphony cap), for the caller to remember.
    std::vector<int> fireChord(const std::vector<int>& notes, int tag, int dest = 0);
    // Scheduling tags. Pads use their own slot (>= 0); everything else is negative, and
    // cancelScheduledNotes must compare against the exact tag, never `< 0` - see the comment
    // there for the stuck-note this caused.
    static constexpr int panicTag = -1;     // cancelScheduledNotes only: cancel *everything*
    static constexpr int liveChordTag = -2; // the live "current chord" card
    // One tag per arp line: -3, -4, -5. Separate tags rather than one, because each line's
    // hold is released independently and cancelScheduledNotes matches the exact tag - sharing
    // one would have letting go of line B drop line A's un-fired strum notes.
    static constexpr int arpChordTag = -3;
    static constexpr int arpChordTagFor(int line) { return arpChordTag - line; }
    std::vector<int> liveChordOn;

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
    void scheduleNoteOn(int note, float vel01, int channel, double delayMs, int padSlot, int dest = 0);
    // Drops this tag's un-fired notes (panicTag drops everything) and returns the pitches it
    // dropped, so a caller releasing the chord can tell which of its notes never sounded.
    std::vector<int> cancelScheduledNotes(int padSlot);
    // Cancel `tag`'s queued note-ons, release the notes that did sound, and empty `sounding`.
    // Every chord source releases through here; see the definition for why the two halves
    // cannot be done independently.
    void releaseNotes(std::vector<int>& sounding, int tag, int dest = 0);
    void timerCallback() override;

    struct DeferredNote
    {
        int note;
        float vel01;
        int channel;
        double atMs;
        int padSlot; // so stopping one pad drops only its own un-fired notes
        int dest;    // which stream it fires into; see noteOn
    };
    std::vector<DeferredNote> deferred; // sorted by atMs; message thread only

    // A launch waiting for its quantize boundary. It carries the *gesture*, not its result:
    // a slot launch moves that line's Shape and Rate as well as its chord, and all of it has
    // to land on the boundary together rather than the parameters moving when you clicked.
    struct PendingLaunch
    {
        int line;
        double atMs;             // wall clock, same frame as DeferredNote::atMs
        int slot = -1;           // >= 0: launch this slot
        int padSlot = -1;        // >= 0: hold this chord pad
        std::vector<int> notes;  // otherwise: hold these notes
        juce::String name;
    };
    std::vector<PendingLaunch> pendingLaunches; // message thread only
    // The three gestures with the wait already served. Everything that must *not* wait calls
    // these directly: the chain (already on a bar line), and a slot launch's own chord.
    void holdArpChordNow(const std::vector<int>& notes, const juce::String& name, int line);
    void holdArpChordFromPadNow(int padSlot, int line);
    void launchArpSlotNow(int index, int line);
    // The gesture, with quantize already decided. The public entry points defer into here.
    void fireLaunchNow(const PendingLaunch&);
    // Queue it, or do it now if quantize is off. Returns true if it was queued.
    bool deferLaunch(PendingLaunch);
    void firePendingLaunches(double nowMs);
    // Beats since the transport started, or since this instance did when there is none. Written
    // once a block on the audio thread, read on the message thread to work out how far the next
    // quantize boundary is. Not a sample-accurate clock and does not need to be: it decides a
    // wall-clock deadline that a 1 ms timer then waits out.
    std::atomic<double> arpBeats { 0.0 };
    std::atomic<double> arpBeatsBpm { 120.0 }; // the tempo those beats are running at
    std::atomic<bool> arpHostBpmLive { false }; // see hostTempoLive()

    juce::MidiMessageCollector collector; // thread-safe UI -> audio message queue
    juce::Random rng; // humanize jitter; touched only on the message thread

    // Where a queued message goes: `collector` for the track output, or the line's own
    // collector for an arp line. One place, so noteOn/noteOff/allNotesOff cannot disagree.
    juce::MidiMessageCollector& collectorFor(int dest);

    // Refcount of what is sounding, per destination stream and per MIDI note.
    // Atomic because the emitting side is the message thread while readers are paint
    // and timer callbacks; nothing here reaches the audio thread.
    //
    // Per *destination*, which is the one thing the arp lines changed about the old rule.
    // "One note-on per sounding pitch" is a statement about a stream: the reason it exists is
    // that downstream, one note-off ends a pitch for everybody. An arp line's input is a
    // different stream with a different consumer (its engine, which counts owners itself in
    // ArpEngine::Held::ons), so a pitch held into line B must not suppress the same pitch
    // being played on the keybed - with one shared counter it did, and the note vanished from
    // the output while the key lit up.
    std::array<std::array<std::atomic<int>, 128>, 1 + numArpLines> noteRefs {};
    std::atomic<juce::uint32> soundingGen { 0 };

    // --- Take capture (see setRecording) ------------------------------------------------
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

    std::vector<CapturedEvent> captureRing { (size_t) captureCapacity };
    std::atomic<juce::uint32> captureWrite { 0 }; // audio thread publishes, message thread reads
    juce::uint32 captureRead = 0;                 // message thread only
    std::atomic<bool> recording { false };
    juce::int64 captureSamples = 0;               // audio thread only; blocks since arming
    std::vector<CapturedEvent> capturedTake;      // message thread only
    juce::File lastTake;

    void captureBlock(const juce::MidiBuffer&, int numSamples); // audio thread, end of the block
    void drainCapture();                                        // message thread, off heartbeatTick

    // Notes seen arriving on the MIDI input (see inputNotes). Written on the audio thread,
    // read on the message thread; a plain flag per pitch, never a count.
    std::array<std::atomic<bool>, 128> inputNoteOn {};
    void watchInputNotes(const juce::MidiBuffer&); // audio thread, before anything consumes it
    void clearInputNotes();

    // The same trick for what the arp *engines* emit (see arpNoteLit). Watched off each line's
    // `out` buffer rather than off the merged stream, because by the time everything is merged
    // the arp's notes are indistinguishable from the pass-through beside them - and a chord
    // held into a line already lights the keybed through noteRefs, so counting the merged
    // stream would light it twice and never put it out.
    std::array<std::atomic<bool>, 128> arpNoteOn {};
    void watchArpNotes(const juce::MidiBuffer&); // audio thread, on one line's output
    void clearArpNotes();

    // Everything one arpeggiator line owns. Three of these; every arp entry point above takes
    // the index that picks one, and line 0 is the arpeggiator Keys has always had.
    //
    // The collector is what makes routing work. A chord handed to line B is fired through the
    // ordinary note path - so it lights the keybed, honours Exclusive and the Voices cap, and
    // sustains honestly when the line is off - but queued *here* rather than into the track
    // output, and only this line's engine ever drains it. The alternative was a per-pitch
    // ownership mask the audio thread consults to decide who a note in the merged stream
    // belongs to, and it races: the message thread can clear a pitch's owner before the
    // matching note-off has been drained, which leaves that note in an engine's held set with
    // nothing left that can release it.
    struct ArpLine
    {
        ArpEngine engine;
        juce::MidiMessageCollector collector; // this line's input queue (message -> audio)
        juce::MidiBuffer in, out;             // audio thread; sized in prepareToPlay
        // This line's parameters, resolved once in the constructor. See ArpParam.
        std::array<std::atomic<float>*, numArpParams> param {};

        std::array<ArpPattern, numArpPatterns> patterns; // message thread only
        int activePattern = 0;                           // message thread only
        // The slot chords, mirrored into atomics for the Chord lane to read on the audio
        // thread. Rebuilt whole by syncArpChordTable() from every message-thread path that
        // can change a slot's chord - there is no single choke point, so the call sites are
        // the contract.
        ArpEngine::ChordTable chordTable;

        std::vector<int> chordOn;   // notes currently held into this line (empty = none)
        juce::String chordName;
        int launchedSlot = -1;      // arp slot whose chord is held, or -1
        int padSlot = -1;           // chord pad whose chord is held, or -1

        bool chainOn = false;       // message thread
        int chainIndex = -1;        // message thread: the slot currently playing
        bool lastOnHeartbeat = false;
        std::atomic<bool> chainActive { false };   // message -> audio: count bars at all
        std::atomic<bool> chainAdvance { false };  // audio -> message: this slot's bars are up
        std::atomic<int> chainEpoch { 0 };         // message -> audio: restart the count
        std::atomic<double> chainTargetBeats { 4.0 };
        double chainBeatsPlayed = 0.0; // audio thread only
        int chainSeenEpoch = 0;        // audio thread only
        bool lastOn = false;           // audio thread; to flush cleanly on bypass
        // The channel override this line ran under last block. A change is a change of where
        // the notes are going, so what is still ringing has to be closed on the old channel
        // first - the same guard ArpEngine::process keeps for the rate mode.
        int lastChannel = 0;           // audio thread; 0 = the global channel
    };
    std::array<ArpLine, numArpLines> lines;
    void syncArpChordTable(int line);

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

    // The whole arp stage: split the merged stream, run the lines, merge them back.
    // Audio thread; see the definition for the routing rules.
    void runArpLines(juce::MidiBuffer& midi, int numSamples);
    void advanceChainClock(int numSamples); // audio thread; raises chainAdvance, never launches
    int nextChainSlot(int from, int line) const; // the next slot holding a chord, wrapping; -1 if none
    // The keybed's notes, lifted out of the merged stream so every listening line can have a
    // copy, and what was left behind when they were. Audio thread; sized in prepareToPlay
    // with the rest. juce::MidiBuffer cannot erase, so a split is two buffers and a swap.
    juce::MidiBuffer keyNotes, streamRest;

    std::array<ChordPad, numChordPads> chordPads;          // captured pad definitions
    std::array<std::vector<int>, numChordPads> chordPadOn;  // notes currently sounding per pad

    // Declared last so it tears down first: it binds an ephemeral loopback MCP server
    // (src/mcp/KeysMcp.h) letting Claude Code or any local MCP client drive Keys
    // directly. Harmless during plugin scans (loopback-only, OS-assigned port).
    std::unique_ptr<KeysMcp> mcpBridge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysProcessor)
};
} // namespace keys
