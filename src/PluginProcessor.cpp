#include "PluginProcessor.h"
#include "EuclidGen.h"
#include "KeysParams.h"
#include "PluginEditor.h"
#include "ScaleModes.h"
#include "mcp/KeysMcp.h"
#include <okstudio/Scales.h>
#include <okstudio/StateHelpers.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace keys
{
namespace
{
    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
} // namespace

// The parameter layout itself is src/KeysParams.{h,cpp}: six hundred lines of ids, ranges,
// defaults and the reasons behind them, none of which ever touched an instance - createLayout is
// called from the member initialiser list below, before there is a processor to touch. What
// stays here is the name every caller already spells. The order it registers in and the item
// lists it carries are the session-compatibility contract; tests/StateTests.cpp holds the golden
// list, and KeysParams.h says why nothing in it may be reordered.
juce::AudioProcessorValueTreeState::ParameterLayout KeysProcessor::createLayout()
{
    return keysparams::createLayout();
}

void KeysProcessor::addArpLineParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int line)
{
    keysparams::addArpLineParams(layout, line);
}

// The N a choice index means, the harmony interval list and its two semitone columns. All four
// are keysparams' now; they keep their names here because the panel, the macro cards, the MCP
// bridge and the tests all call them through this class. tupletFor and harmonySemisFor are
// audio-thread reads (runArpLines), which is why their tables are in KeysParams.h rather than
// its .cpp - they stay indexed reads of a constant table with nothing allocated.
int KeysProcessor::tupletFor(int choiceIndex) { return keysparams::tupletFor(choiceIndex); }
juce::StringArray KeysProcessor::harmonyChoices() { return keysparams::harmonyChoices(); }
int KeysProcessor::harmonySemisFor(int choiceIndex) { return keysparams::harmonySemisFor(choiceIndex); }
int KeysProcessor::harmonySemisSecondFor(int choiceIndex)
{
    return keysparams::harmonySemisSecondFor(choiceIndex);
}

// The id suffix of each per-line parameter, one table so the audio thread's cached pointers,
// the UI's attachments and createLayout's registrations cannot drift apart. These strings are
// the parameter ids: renaming one loses that setting out of every saved session.
const char* KeysProcessor::arpParamSuffix(int which)
{
    static const char* const suffixes[numArpParams] = {
        "On", "Rate", "RateFree", "RateHz", "Dot", "Trip", "Anchor", "Direction", "Pattern",
        "LinkLanes", "Octaves", "Swing", "Latch", "Retrigger", "Gate", "Chance", "Distance",
        "Offset", "RetrigBars", "VelRamp", "RampBeats", "Humanize", "Keys", "Channel",
        "OctShift", "Volume", "HumanVel", "VelTrim", "Tuplet", "HumanizeSpan", "HumanVelSpan",
        "Drift", "VelLevel", "Mutate", "MutateLock",
        "Harm1", "Harm1Chance", "Harm2", "Harm2Chance",
        "Stray", "Legato", "Follow", "Duck", "ResetFollow"
    };
    return suffixes[(size_t) juce::jlimit(0, (int) numArpParams - 1, which)];
}

float KeysProcessor::arpParam(int line, ArpParam which) const
{
    const auto* p = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].param[(size_t) which];
    return p != nullptr ? p->load() : 0.0f;
}

// Line 0 is the arpeggiator Keys has always had, so its ids are the bare ones every saved
// session already carries. B and C take a digit after "arp" - "arp2Rate" - which is a name no
// earlier version ever wrote, so nothing collides and nothing has to be migrated.
juce::String KeysProcessor::arpParamId(int line, juce::StringRef suffix)
{
    const int n = juce::jlimit(0, numArpLines - 1, line);
    return n == 0 ? "arp" + juce::String(suffix)
                  : "arp" + juce::String(n + 1) + juce::String(suffix);
}

ArpEngine& KeysProcessor::arpLine(int line)
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].engine;
}

const ArpEngine& KeysProcessor::arpLine(int line) const
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].engine;
}

bool KeysProcessor::arpLineOn(int line) const
{
    // The one choke point for "does this line exist". A line the UI does not show has no way to
    // be switched off either, so it must not be able to sound: a session saved while a hidden
    // line was running would otherwise arpeggiate forever with no control anywhere on screen.
    // Answering false here makes it inert everywhere at once - runArpLines skips its engine and
    // its keys, cardsFeedArp stops counting it, and the bar's Hold off greys correctly - while
    // its stored parameter keeps whatever value it had. All four lines show today (2026-08-19),
    // so the guard is only the range check; it stays because uiArpLines can move again.
    if (line < 0 || line >= uiArpLines)
        return false;
    // runArpLines calls this per line, per block, on the audio thread, so it must read the
    // cached atomic pointer rather than build arpParamId's juce::String and hash it through
    // apvts::getRawParameterValue - the allocation and the string-keyed lookup are both things
    // the audio thread may not do. arpParam(line, apOn) is that same cached pointer arpParamId
    // would have resolved, read the way every other per-block arp read already is.
    return arpParam(line, apOn) > 0.5f;
}

bool KeysProcessor::cardsFeedArp() const
{
    for (int n = 0; n < numArpLines; ++n)
        if (arpLineOn(n))
            return true;
    return false;
}

int KeysProcessor::arpCurrentLine() const
{
    // Clamped to what the UI shows, not to what exists: a session saved with C current has to
    // come back pointing at a line the letter chip can name and a card click can reach.
    return juce::jlimit(0, uiArpLines - 1, layout.arpLine);
}

void KeysProcessor::setArpCurrentLine(int line)
{
    layout.arpLine = juce::jlimit(0, uiArpLines - 1, line);
}

KeysProcessor::KeysProcessor()
   #if defined(KEYS_MIDI_EFFECT) && KEYS_MIDI_EFFECT
    // MIDI-effect variant: JUCE requires a MIDI effect to declare no audio buses.
    : AudioProcessor(BusesProperties()),
   #else
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
   #endif
      apvts(*this, nullptr, "PARAMS", createLayout())
{
    // Resolve every line's parameters once. The audio thread reads through these pointers and
    // therefore never builds an id string or hashes one; see ArpParam.
    for (int n = 0; n < numArpLines; ++n)
        for (int p = 0; p < numArpParams; ++p)
        {
            lines[(size_t) n].param[(size_t) p] = apvts.getRawParameterValue(arpParamId(n, arpParamSuffix(p)));
            jassert(lines[(size_t) n].param[(size_t) p] != nullptr); // a suffix with no parameter behind it
        }

    // 50 Hz. It only ever notices things, so the rate is about how late a chord change is
    // allowed to be: 20 ms is comfortably inside a 1/16 step at any tempo anyone plays at,
    // and the arp itself stays anchored to the bar grid regardless.
    heartbeat.tick = [this] { heartbeatTick(); };
    heartbeat.startTimerHz(50);

    // The generator's key and mode drive the keyboard's Root and Scale, so the keybed greys to
    // the key you are generating in (see parameterChanged). Registered after everything else
    // exists, since the callback reaches back into apvts.
    apvts.addParameterListener("genRoot", this);
    apvts.addParameterListener("genMode", this);

    // Last thing the constructor does: everything else this processor owns already
    // exists by the time the MCP bridge can be reached from another thread.
    mcpBridge = std::make_unique<KeysMcp>(*this);
}

void KeysProcessor::parameterChanged(const juce::String&, float)
{
    // Which of the two moved does not matter - both are re-applied together, so one listener
    // body covers them and a session load that changes both settles on one pass.
    pendingGenKeyMirror.set(1);
}

void KeysProcessor::mirrorGenKeyToScale()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (pendingGenKeyMirror.exchange(0) == 0)
        return;

    // Written through setValueNotifyingHost so a host sees the move and any attached combo
    // follows it, and only when it actually differs: writing an unchanged value would push a
    // gesture-less parameter change at the host on every heartbeat this fires from.
    const auto setChoice = [this](const char* id, int index)
    {
        auto* param = apvts.getParameter(id);
        if (param == nullptr || index < 0)
            return;
        const float wanted = param->convertTo0to1((float) index);
        if (std::abs(param->getValue() - wanted) > 1.0e-4f)
            param->setValueNotifyingHost(wanted);
    };

    setChoice("root", (int) apvts.getRawParameterValue("genRoot")->load());
    setChoice("scale", modes::kitScaleIndexFor((int) apvts.getRawParameterValue("genMode")->load()));
}

KeysProcessor::~KeysProcessor()
{
    apvts.removeParameterListener("genRoot", this);
    apvts.removeParameterListener("genMode", this);
    // A take in progress is written here rather than lost. "Stopping writes the file, so a take
    // is never a thing a click can lose" is the feature's own contract, and closing the set,
    // deleting the plugin from the track or quitting the host is exactly the click that lost it:
    // nothing else on the way down calls writeTake. Cheap, and it runs before the timers stop
    // because writeTake needs neither.
    if (take.isRecording())
    {
        setRecording(false); // drains the last blocks the audio thread wrote
        writeTake();
    }

    // Stop taking MCP calls before anything else tears down.
    mcpBridge.reset();
    heartbeat.stopTimer();
    stopTimer();
    deferred.clear();
}

void KeysProcessor::updateTrackProperties(const TrackProperties& props)
{
    // **Merge, never replace, and that is the entire trick.** Ableton does not hand a plugin
    // its track once: it makes three calls per instance as a set loads - an empty name with a
    // default colour, then the real name with a default colour, then an empty name with the
    // real colour. So the obvious implementation (store what you were given) is handed the
    // name in call two and throws it away in call three, and the symptom is a track name that
    // works until something else on the track changes and then silently goes blank.
    //
    // JUCE hands over a freshly constructed TrackProperties each call, filling only the fields
    // the host actually provided (juce_audio_plugin_client_VST3.cpp, setChannelContextInfos),
    // so an absent field is nullopt. But Live's own empty calls report a *present, empty*
    // string, which is why nullopt alone is not enough of a test and the emptiness is checked
    // as well. The colour is guarded the same way: a default-constructed juce::Colour is
    // transparent black, and Live's placeholder is transparent, so alpha is what separates a
    // real colour from a spacer.
    if (props.name.has_value() && props.name->isNotEmpty())
        trackName = *props.name;
    if (props.colour.has_value() && props.colour->getAlpha() != 0)
        trackColour = props.colour->toDisplayString(false);
}

void KeysProcessor::resetAllParameters()
{
    // Silence first, and it is not politeness. A reset moves Root, Octave, Scale Lock and the
    // arp's whole routing underneath whatever is currently sounding, and the played note is
    // resolved at press time and remembered - so a note held across this would be released
    // against settings that no longer exist, or not released at all. allNotesOff() is the one
    // choke point that clears every source: the pads, the live card, each line's held chord,
    // the chain and anything waiting on a quantize boundary.
    allNotesOff();

    // setValueNotifyingHost, not a direct write: the host has to see these move or its
    // automation lane and its own generic editor go on showing the old values, and every
    // attachment in the editor is listening for exactly this notification. The value is
    // normalised 0..1, which is what getDefaultValue already returns.
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
            ranged->setValueNotifyingHost(ranged->getDefaultValue());

    // **The six settings that are not parameters, because the row says "settings" and three of
    // them are in the same popup it is on.** A Reset that leaves *Sustained notes propose
    // chords* ticked three rows above itself is a button whose name is wrong in the one place
    // anybody reads it. These six are behaviour - they change what a click does or what the
    // keybed shows - which is what puts them in and keeps the rest of LayoutState out: the
    // theme, the folds, the detached windows, the current page and the library favourites are
    // *where you left the furniture*, and a reset that rearranged the window would be doing
    // something nobody asked a settings reset for. The defaults are LayoutState's own, taken
    // from a default-constructed one rather than written out a second time here, so they cannot
    // drift from the struct.
    const LayoutState d {};
    layout.holdVisualsOnSustain  = d.holdVisualsOnSustain;
    layout.dragWhileSustain      = d.dragWhileSustain;
    layout.sustainProposesChords = d.sustainProposesChords;
    layout.arpLights             = d.arpLights;
    layout.padsPlayOnClick       = d.padsPlayOnClick;
    layout.padsKeepArpRunning    = d.padsKeepArpRunning;
    // No refresh call: all three of these that have a control on screen (Light keys, Play,
    // Keep arp) are pulled from `layout` by KeysEditor::timerCallback, and the other three are
    // read at the moment they are used - the settings menu rebuilds its ticks every time it
    // opens. Nothing here has a stale copy to invalidate.
}

bool KeysProcessor::arpTrackMidiOn() const
{
    return apvts.getRawParameterValue("arpTrackMidi")->load() > 0.5f;
}

int KeysProcessor::midiChannel() const
{
    return (int) apvts.getRawParameterValue("channel")->load() + 1;
}

int KeysProcessor::octaveShift() const
{
    return (int) apvts.getRawParameterValue("octave")->load();
}

int KeysProcessor::polyphonyCap() const
{
    return (int) apvts.getRawParameterValue("polyphony")->load(); // 0 = unlimited
}

int KeysProcessor::padPage() const
{
    return juce::jlimit(0, numPadPages - 1, (int) apvts.getRawParameterValue("padPage")->load());
}

float KeysProcessor::baseVelocity01() const
{
    // The Humanize range *is* the velocity control. There used to be two: a fixed Velocity
    // slider that only applied while Humanize was off, and this range that only applied
    // while it was on - the same control wearing different clothes, which is what made
    // them feel redundant. Now Humanize on picks a random value inside the range per note
    // (see noteOn), and off plays its midpoint. Collapsing the band onto a single value
    // therefore gives a plain fixed velocity, which is exactly what the slider did.
    const float a = apvts.getRawParameterValue("humanizeVelMin")->load();
    const float b = apvts.getRawParameterValue("humanizeVelMax")->load();
    const float mid = (juce::jmin(a, b) + juce::jmax(a, b)) * 0.5f;
    return juce::jlimit(0.0f, 1.0f, mid / 127.0f); // 0..127 straight through; see the layout
}

bool KeysProcessor::chordPadActive(int i) const
{
    return i >= 0 && i < numChordPads && ! chordPadOn[(size_t) i].empty();
}

void KeysProcessor::setChordPad(int i, const std::vector<int>& notes, const juce::String& name)
{
    if (i < 0 || i >= numChordPads)
        return;
    // A hand-captured chord: keep the lock, drop any generator metadata the slot carried,
    // since the notes no longer come from that degree.
    const bool wasLocked = chordPads[(size_t) i].locked;
    chordPads[(size_t) i] = {};
    chordPads[(size_t) i].notes = notes;
    chordPads[(size_t) i].name = name;
    chordPads[(size_t) i].locked = wasLocked;
}

void KeysProcessor::setChordPad(int i, const ChordPad& pad)
{
    if (i < 0 || i >= numChordPads)
        return;
    chordPads[(size_t) i] = pad;
}

void KeysProcessor::clearChordPad(int i)
{
    if (i < 0 || i >= numChordPads)
        return;
    stopChordPad(i);
    // If this card is the one feeding the arp, let its chord go with it. Left holding, it
    // rang on with no owner: the strip only draws the "feeding the arp" ring on a *filled*
    // pad, and only a filled pad accepts the click that releases it, so the chord became
    // unreachable except through All Off.
    // Whichever line is holding it; a card can only be feeding one at a time.
    if (const int line = arpLineHoldingPad(i); line >= 0)
        releaseArpChord(line);
    chordPads[(size_t) i] = {};
}

void KeysProcessor::clearChordPadPage()
{
    // One entry for the whole page, not one per pad: this is a single gesture and the twelve
    // cards it empties are what you would want back together. UndoGesture absorbs the nested
    // pushes clearChordPad's own callers would otherwise make, the same way Fill and Regen use
    // it. Nothing is pushed at all when there is nothing to clear, so a click on a row that
    // should have been greyed cannot bury a real entry under an empty one.
    if (! pageHasClearablePads())
        return;

    const UndoGesture undoable { *this, "Clear page", UndoScope::pads };
    const int offset = padPageOffset();
    for (int v = 0; v < padsPerPage; ++v)
        if (! chordPads[(size_t) (offset + v)].locked)
            clearChordPad(offset + v); // releases a ringing card and any line it is feeding
}

bool KeysProcessor::pageHasClearablePads() const
{
    const int offset = padPageOffset();
    for (int v = 0; v < padsPerPage; ++v)
    {
        const auto& pad = chordPads[(size_t) (offset + v)];
        if (! pad.locked && ! pad.notes.empty())
            return true;
    }
    return false;
}

void KeysProcessor::setChordPadLocked(int i, bool locked)
{
    if (i < 0 || i >= numChordPads)
        return;
    chordPads[(size_t) i].locked = locked;
}

void KeysProcessor::moveChordPad(int from, int to)
{
    if (from < 0 || from >= numChordPads || to < 0 || to >= numChordPads || from == to)
        return;
    stopChordPad(from);
    stopChordPad(to);
    std::swap(chordPads[(size_t) from], chordPads[(size_t) to]);
    // The "feeding the arp" ring belongs to the card, not to the slot it happens to sit in.
    // Both directions on every line: two lines can be holding the two cards being swapped.
    for (auto& ln : lines)
    {
        if (ln.padSlot == from)
            ln.padSlot = to;
        else if (ln.padSlot == to)
            ln.padSlot = from;
    }
}

void KeysProcessor::stopChordPad(int i)
{
    if (i < 0 || i >= numChordPads)
        return;
    releaseNotes(chordPadOn[(size_t) i], i);
}

void KeysProcessor::releaseNotes(std::vector<int>& sounding, int tag, int dest)
{
    // Drop what this source still has queued, and release the rest — but *only* the rest.
    //
    // `sounding` is what fireChord was asked to play, which during a strum is ahead of what
    // it has actually played: the notes still sitting in `deferred` never reached noteOn and
    // so own no reference to release. Calling noteOff for one of those takes a reference that
    // belongs to somebody else, and since a pitch now ends when its last owner lets go (see
    // noteOn), that emits a real note-off and silences another source's note while its key
    // stays lit. Releasing a pad mid-strum while the keybed held one of the same pitches did
    // exactly that - the failure this refcount rewrite exists to remove, arriving by a
    // different door.
    //
    // Matched one-for-one rather than by set membership, so a chord carrying the same pitch
    // twice still ends up with one note-off per reference actually taken.
    auto cancelled = cancelScheduledNotes(tag);
    for (int n : sounding)
    {
        const auto it = std::find(cancelled.begin(), cancelled.end(), n);
        if (it != cancelled.end())
            cancelled.erase(it); // never fired; there is no reference here to give back
        else
            noteOff(n, 0, 0.0, dest); // the stream its note-on went into, or the reference leaks
    }
    sounding.clear();
}

void KeysProcessor::stopAllChordPads(bool includeArpHolds, bool includeKeybed)
{
    for (int i = 0; i < numChordPads; ++i)
        stopChordPad(i);
    // The keybed's own holds, through the editor, because they are the one chord source the
    // processor does not own (2026-08-16). `includeKeybed` false has exactly one caller and it
    // is pressLiveChord: that chord *is* what the keybed is holding, so clearing it there would
    // unlatch the keys in the same breath as firing them and leave the card playing a chord the
    // surface has just forgotten.
    if (includeKeybed && releaseKeybedHolds)
        releaseKeybedHolds();
    // Every chord source, not just the pads. Exclusive is a rule about *sources* - one chord
    // at a time, whichever surface started it - so it has to reach the live card and the
    // chord held into the arp as well, or a lit Exclusive quietly does nothing in one
    // direction and the chords pile up.
    releaseLiveChord(true);
    if (includeArpHolds)
        for (int n = 0; n < numArpLines; ++n)
            releaseArpChord(n);
}

// What a scheduling tag is still sounding, or nullptr if the tag names no chord source. The
// tags are the ones fireChord already takes, so this is a lookup rather than a second notion of
// which sources exist.
const std::vector<int>* KeysProcessor::soundingForTag(int tag) const
{
    if (tag >= 0 && tag < numChordPads)
        return &chordPadOn[(size_t) tag];
    if (tag == liveChordTag)
        return &liveChordOn;
    if (const int line = arpChordTag - tag; tag <= arpChordTag && line < numArpLines)
        return &arpHeldNotes(line);
    return nullptr;
}

// The read half of the same list. See the header for why this is not isNoteSounding().
//
// **One chord, not the union of every source** (2026-08-16, Owen: "the currently held chord
// should disappear when you play a new chord pad"). It unioned all of them for a few hours,
// which is wrong on the plainest reading of its own name: with Sustain down, or Exclusive off,
// pressing a second pad left the card naming the pile of both rather than the chord you just
// played. "The currently held chord" is singular, so this answers with one.
std::vector<int> KeysProcessor::heldChordNotes() const
{
    // The last source to start, while it is still sounding.
    if (const auto* v = soundingForTag(lastChordSource); v != nullptr && ! v->empty())
        return *v;

    // It has been released, so fall back to anything still holding a chord - a pad left ringing
    // by Sustain is genuinely still held, and going empty here would be a card that forgets a
    // chord you can still hear. Pads first, then the live card, then the lines; ordering only
    // decides which of several *older* holds wins, and there is no recency left to consult.
    for (int i = 0; i < numChordPads; ++i)
        if (! chordPadOn[(size_t) i].empty())
            return chordPadOn[(size_t) i];
    if (! liveChordOn.empty())
        return liveChordOn;
    // Not gated on arpLineOn: a line that is off still takes chords in and still holds what it
    // was handed (see holdArpChord), and the card is asking what is *held*. A chord you dropped
    // onto a line before switching it on is one you must still be able to pick back up.
    for (int n = 0; n < numArpLines; ++n)
        if (! arpHeldNotes(n).empty())
            return arpHeldNotes(n);
    return {};
}

void KeysProcessor::pressChordPad(int i)
{
    if (i < 0 || i >= numChordPads)
        return;
    if (chordPads[(size_t) i].notes.empty())
        return;

    // **A pad always chokes the other pads** (2026-08-16, Owen: "when you click a pad it should
    // clear other presses"). It used to stop only the pad being re-pressed unless Exclusive was
    // lit, so with Exclusive off - or with Sustain holding them - clicking round a page stacked
    // chord on chord into a pile that is neither of them and cannot be named or dragged as
    // either. Pads are one surface and one voice: the strip is a palette you pick *from*.
    //
    // Exclusive keeps its job and it is now a sharper one: whether a pad also chokes the *other*
    // sources - the live card's own gesture and the chord held into each arp line. Those are
    // different instruments, so stacking them is a real thing to want; stacking two pads was not.
    if (apvts.getRawParameterValue("chordExclusive")->load() > 0.5f)
    {
        // Every source at once - except the arp lines while **Keep arp running** is ticked
        // (2026-08-26). See LayoutState::padsKeepArpRunning: a press on this strip is playing a
        // chord, and a line's held chord is the input to a machine rather than something you
        // are playing, so leaning on a card to hear it should not stop the lines. A *drop* on a
        // line still replaces that line's chord, and still chokes the pads and the live card.
        stopAllChordPads(/*includeArpHolds*/ ! layout.padsKeepArpRunning);
    }
    else
    {
        for (int j = 0; j < numChordPads; ++j)
            stopChordPad(j); // including `i` itself, so re-pressing a sounding pad re-triggers
    }

    // Honour the Voices cap. The keyboard steals oldest-first across its own notes; a pad
    // fires as one gesture, so there is no "oldest" within it — drop the highest notes and
    // keep the lowest, matching how the keyboard resolves a too-big simultaneous chord.
    // (The cap applies per source: a pad and the keyboard each fit under it separately.)
    chordPadOn[(size_t) i] = fireChord(chordPads[(size_t) i].notes, i);
    lastChordSource = i; // the live card names *this* chord now, not the pile of every pad
}

std::vector<int> KeysProcessor::fireChord(const std::vector<int>& source, int tag, int dest)
{
    // Honour the Voices cap. The keyboard steals oldest-first across its own notes; a chord
    // fires as one gesture, so there is no "oldest" within it — drop the highest notes and
    // keep the lowest, matching how the keyboard resolves a too-big simultaneous chord.
    // (The cap applies per source: a chord and the keyboard each fit under it separately.)
    std::vector<int> notes = source;
    std::sort(notes.begin(), notes.end());
    const int cap = polyphonyCap();
    if (cap > 0 && (int) notes.size() > cap)
        notes.resize((size_t) cap);

    const float vel = baseVelocity01();

    // Strum (Octavium "Drift"): spread the note-ons over `chordStrum` ms in a direction.
    std::vector<int> order = notes; // low -> high
    const int dir = (int) apvts.getRawParameterValue("chordStrumDir")->load();
    if (dir == 1)
        std::reverse(order.begin(), order.end());              // Down: high -> low
    else if (dir == 2)
        for (int k = (int) order.size() - 1; k > 0; --k)       // Random: Fisher-Yates
            std::swap(order[(size_t) k], order[(size_t) rng.nextInt(k + 1)]);

    // One spread per chord, not per note: a strum is a single rake, and re-rolling inside it
    // would scramble the order the direction just decided.
    // **Nothing bound for a line's queue is raked** (2026-08-23, forced by Strum's own default
    // going nonzero). `dest` > 0 means these notes are the *input* to an arp line: only that
    // engine sees them, so a rake there is inaudible by construction and all it does is stagger
    // when the engine learns each note. At 30-80 ms that is most of a 1/16 at 120 bpm, so the
    // first steps of a run would fire on half a chord. Exactly the rule the Humanize velocity
    // range already follows in noteOn, and for the same reason: a line has its own feel controls
    // (Swing, H.TIME) and its input is not the place to apply the strip's.
    const double strumLo = dest == 0 ? apvts.getRawParameterValue("chordStrum")->load() : 0.0;
    const double strumHi = dest == 0 ? apvts.getRawParameterValue("chordStrumMax")->load() : 0.0;
    const double strumMs = strumLo == strumHi
                               ? strumLo
                               : juce::jmin(strumLo, strumHi)
                                     + rng.nextDouble() * std::abs(strumHi - strumLo);
    const int count = (int) order.size();
    for (int k = 0; k < count; ++k)
    {
        // Spread across the whole strum time, first note now. Scheduled rather than
        // stamped: see scheduleNoteOn for why noteOn's own delay could not do this.
        const double delayMs = (count > 1 && strumMs > 0.0)
                                   ? strumMs * (double) k / (double) (count - 1)
                                   : 0.0;
        // asChord true: this is a chord, not something played on the keys, so on the track
        // output it takes the queue a listening arp line cannot lift. It says nothing when
        // `dest` names a line - that chord was routed to it on purpose. See noteOn.
        scheduleNoteOn(order[(size_t) k], vel, 0, delayMs, tag, dest, /*asChord*/ true); // noteOn adds Humanize per note
    }
    return notes;
}

void KeysProcessor::pressLiveChord(const std::vector<int>& notes)
{
    // The live card fires the chord you are holding as one gesture, so you hear it the way
    // a pad would play it — strummed, humanized, capped — rather than as the sum of the
    // individual keys you happen to be holding down. Re-pressing re-triggers.
    if (notes.empty())
        return;
    releaseLiveChord(true);
    if (apvts.getRawParameterValue("chordExclusive")->load() > 0.5f)
        stopAllChordPads(/*includeArpHolds*/ ! layout.padsKeepArpRunning, /*includeKeybed*/ false);
    liveChordOn = fireChord(notes, liveChordTag);
    lastChordSource = liveChordTag;
}

void KeysProcessor::releaseLiveChord(bool force)
{
    if (! force && apvts.getRawParameterValue("sustain")->load() > 0.5f)
        return; // pedal down: leave it ringing, same as a pad
    releaseNotes(liveChordOn, liveChordTag);
}

void KeysProcessor::scheduleNoteOn(int note, float vel01, int channel, double delayMs, int padSlot,
                                   int dest, bool asChord)
{
    if (delayMs <= 0.0)
    {
        noteOn(note, vel01, 0.0, channel, dest, asChord);
        return;
    }

    const double at = juce::Time::getMillisecondCounterHiRes() + delayMs;
    const DeferredNote d { note, vel01, channel, at, padSlot, dest, asChord };
    // Keep sorted by due time, so timerCallback only ever inspects the front.
    deferred.insert(std::upper_bound(deferred.begin(), deferred.end(), at,
                                     [](double t, const DeferredNote& n) { return t < n.atMs; }),
                    d);
    if (! isTimerRunning())
        startTimer(1); // 1 ms: a strum step can be as short as 200/8 = 25 ms
}

std::vector<int> KeysProcessor::cancelScheduledNotes(int padSlot)
{
    // A note-on that fires *after* its note-off is a stuck note that nothing clears, so
    // every path that stops sound has to drop what is still queued.
    //
    // `padSlot < 0` used to mean "everything", which was true only while -1 was the sole
    // negative tag. The live card (-2) and the chord held into the arp (-3) are sources like
    // any other, so releasing either wiped every other source's un-fired strum notes too: a
    // pad mid-strum lost the rest of its chord. Only the panic tag means all.
    //
    // Returns the pitches actually dropped, because a caller about to release the chord needs
    // to know which of its notes never sounded; see releaseNotes.
    std::vector<int> cancelled;
    const auto mine = [padSlot](const DeferredNote& n)
    { return padSlot == panicTag || n.padSlot == padSlot; };

    for (const auto& n : deferred)
        if (mine(n))
            cancelled.push_back(n.note);

    deferred.erase(std::remove_if(deferred.begin(), deferred.end(), mine), deferred.end());

    if (deferred.empty())
        stopTimer();
    return cancelled;
}

void KeysProcessor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    // Launch Quantize rides this timer rather than the 50 Hz heartbeat: a 20 ms tick is a sixth
    // of a 1/16 at 120 bpm, which is exactly the sloppiness this feature exists to remove.
    firePendingLaunches(now);
    size_t due = 0;
    while (due < deferred.size() && deferred[due].atMs <= now)
        ++due;

    if (due > 0)
    {
        const std::vector<DeferredNote> firing(deferred.begin(), deferred.begin() + (long) due);
        deferred.erase(deferred.begin(), deferred.begin() + (long) due);
        for (const auto& n : firing)
            noteOn(n.note, n.vel01, 0.0, n.channel, n.dest, n.asChord);
    }

    if (deferred.empty() && pendingLaunches.empty())
        stopTimer();
}

void KeysProcessor::releaseChordPad(int i)
{
    if (i < 0 || i >= numChordPads)
        return;
    if (apvts.getRawParameterValue("sustain")->load() > 0.5f)
        return;                  // pedal down: leave the chord ringing until Sustain lifts
    stopChordPad(i);
}

juce::MidiMessageCollector& KeysProcessor::collectorFor(int dest)
{
    if (dest >= 1 && dest <= numArpLines)
        return lines[(size_t) (dest - 1)].collector;
    return collector;
}

// The same answer, split one finer: the track output has two queues and `asChord` picks between
// them. A line's own input has one, so `asChord` says nothing there - a chord handed to a line is
// meant for that line and has already been routed by `dest`.
juce::MidiMessageCollector& KeysProcessor::chordQueueFor(int dest, bool asChord)
{
    if (dest == 0 && asChord)
        return chordCollector;
    return collectorFor(dest);
}

void KeysProcessor::noteOn(int midiNote, float velocity01, double delaySeconds, int channelOverride,
                           int dest, bool asChord)
{
    if (midiNote < 0 || midiNote > 127)
        return;
    dest = juce::jlimit(0, numArpLines, dest);
    const int channel = (channelOverride >= 1 && channelOverride <= 16) ? channelOverride : midiChannel();

    // Humanize (Octavium logic): pick a uniform-random velocity within the [min, max]
    // range per note, and nudge the note-on slightly late so simultaneous (latched or
    // dragged) notes stop landing perfectly quantized. Note-offs are never delayed, so
    // a note can never release before it has sounded. delaySeconds adds the strum offset.
    double when = nowSeconds() + delaySeconds;
    // ...but never for a note bound for an arp line's queue (2026-08-02, Owen: "is it
    // passing it through the humanized volume range? ... this might need its own separate
    // thing"). A line has H.VEL for randomness and VEL for level now, and re-randomizing
    // its input velocity here made VEL's "as played" reference wander per note. dest 0 is
    // the track output - actually playing - and keeps the Humanize range as ever; what the
    // keybed feeds a line keeps it too, because those notes pass through here as playing
    // before runArpLines lifts them.
    if (dest == 0 && apvts.getRawParameterValue("humanize")->load() > 0.5f)
    {
        const int a = (int) apvts.getRawParameterValue("humanizeVelMin")->load();
        const int b = (int) apvts.getRawParameterValue("humanizeVelMax")->load();
        const int lo = juce::jmin(a, b), hi = juce::jmax(a, b);
        const int rnd = rng.nextInt(juce::Range<int>(lo, hi + 1));
        velocity01 = (float) rnd / 127.0f;
    }

    // One note-on per *pitch*, not per owner. Four sources can ask for the same pitch at
    // once (a pad, the live card, a chord held into the arp, and the keybed), and emitting a
    // second note-on for a pitch already sounding is what made Exclusive and Sustain look
    // broken: downstream, one note-off ends the pitch for everyone, so releasing whichever
    // owner happens to go first silenced the others while noteRefs still counted them - keys
    // lit on the keybed with nothing sounding. Worse, the arpeggiator sits downstream of this
    // stream and counts note-ons it never gets matching note-offs for, which leaked its held
    // set and left chords arpeggiating forever (see ArpEngine::noteArrived).
    //
    // Re-pressing a source that already owns the pitch still retriggers, because every such
    // path releases before it fires (pressChordPad -> stopChordPad, holdArpChord ->
    // releaseArpChord), taking the count to zero on the way.
    //
    // The count is per pitch, NOT per (pitch, channel): two sources holding one pitch on two
    // different channels collapse to a single note on whichever channel got there first, and
    // the eventual note-off goes out on whichever channel released last. Nothing on screen can
    // reach that today - no NoteSurface overrides noteChannel(), so everything here is on the
    // global channel - and the MCP bridge is the only caller that passes one (see
    // KeysMcp.cpp). Anything that gives a surface a channel of its own has to make noteRefs
    // per channel first, or it will silently eat the second source's notes.
    //
    // The count is per *destination* since the arp lines arrived: the rule is about one
    // stream, and a pitch held into line B has nothing to say about the same pitch being
    // played to the track output. See the declaration of noteRefs.
    const bool alreadySounding = noteRefs[(size_t) dest][(size_t) midiNote].fetch_add(1) > 0;
    if (! alreadySounding)
    {
        // **Never 0, and never lifted either.** A note-on at velocity 0 is a note-off, so the
        // floor is one MIDI unit and not zero. It was 0.04 - about velocity 5 - which quietly
        // lifted the whole bottom of the Humanize band: the control said 1 and you heard 5, and
        // with the band now reaching 0 that lie would have covered its lowest six values. The
        // range *is* the velocity control here, so it has to be taken literally.
        auto m = juce::MidiMessage::noteOn(channel, midiNote,
                                           juce::jlimit(1.0f / 127.0f, 1.0f, velocity01));
        m.setTimeStamp(when);
        // Which of dest 0's two queues this pitch is opening on, recorded before it is sent so
        // the matching note-off can find it again. Only the note-on that actually sounds writes
        // it: a second owner of a pitch already ringing emits nothing, so it neither needs nor
        // gets a say in where the release goes. See chordStream.
        if (dest == 0)
            chordStream[(size_t) midiNote].store(asChord);
        chordQueueFor(dest, asChord).addMessageToQueue(m);
    }
    soundingGen.fetch_add(1);
}

void KeysProcessor::noteOff(int midiNote, int channelOverride, double delaySeconds, int dest)
{
    if (midiNote < 0 || midiNote > 127)
        return;
    dest = juce::jlimit(0, numArpLines, dest);
    const int channel = (channelOverride >= 1 && channelOverride <= 16) ? channelOverride : midiChannel();

    // The other half of the ownership rule in noteOn: the pitch ends when the LAST owner
    // lets go, not the first. Clamp at zero, because a note-off with no matching note-on
    // (panic, a pad released twice) must not push the count negative and leave the key lit
    // forever - and must not emit a stray note-off either.
    auto& ref = noteRefs[(size_t) dest][(size_t) midiNote];
    int cur = ref.load();
    while (cur > 0 && ! ref.compare_exchange_weak(cur, cur - 1)) {}
    if (cur == 1) // this owner was the last one
    {
        auto m = juce::MidiMessage::noteOff(channel, midiNote);
        m.setTimeStamp(nowSeconds() + delaySeconds);
        // Down whichever queue the note-on went, not whichever the *releasing* source would
        // choose. The last owner is often not the first, and a note-off that took the other
        // queue would strand the note in a listening line's engine - see chordStream.
        chordQueueFor(dest, dest == 0 && chordStream[(size_t) midiNote].load())
            .addMessageToQueue(m);
    }
    soundingGen.fetch_add(1);
}

bool KeysProcessor::isNoteSounding(int midiNote) const
{
    if (midiNote < 0 || midiNote > 127)
        return false;
    // Notes arriving on the input count as sounding for display: Keys passes them through
    // to the same instrument its own notes go to, so on screen they *are* sounding, and the
    // keybed lights them through exactly the path a chord pad's notes already take.
    //
    // Any destination, not only the track output: a chord held into an arp line is being
    // played, and the card that holds it and the keybed under it both have to say so. It is
    // the arp's own notes that are not counted here, and never have been - they never pass
    // through noteOn at all.
    for (const auto& dest : noteRefs)
        if (dest[(size_t) midiNote].load() > 0)
            return true;
    return inputNoteOn[(size_t) midiNote].load();
}

bool KeysProcessor::keybedLit(int midiNote) const
{
    if (midiNote < 0 || midiNote > 127)
        return false;
    // The track output and the MIDI input are what they are, whatever the arp is doing.
    if (noteRefs[0][(size_t) midiNote].load() > 0 || inputNoteOn[(size_t) midiNote].load())
        return true;
    // A chord handed to an arp line: lit unless that line is running it with Light keys on,
    // in which case it is the run's input and showing it would drown the run. See the header.
    for (int n = 0; n < numArpLines; ++n)
        if (noteRefs[(size_t) (1 + n)][(size_t) midiNote].load() > 0
            && ! (layout.arpLights && arpLineOn(n)))
            return true;
    return arpNoteLit(midiNote);
}

unsigned int KeysProcessor::arpLitLineMask(int midiNote) const
{
    if (midiNote < 0 || midiNote > 127 || ! layout.arpLights)
        return 0u;
    return arpNoteLines[(size_t) midiNote].load();
}

bool KeysProcessor::arpNoteLit(int midiNote) const
{
    return arpLitLineMask(midiNote) != 0u;
}

int KeysProcessor::arpLitLine(int midiNote) const
{
    const unsigned int mask = arpLitLineMask(midiNote);
    if (mask == 0u)
        return -1;
    // Lowest set bit: A over B over C over D. Deterministic and stable while the note is held,
    // which matters more than which line "deserves" it - a key that changed colour as lines
    // came and went on the same pitch would read as the arp having moved, not as an overlap.
    for (int n = 0; n < numArpLines; ++n)
        if ((mask & (1u << n)) != 0u)
            return n;
    return -1;
}

// Audio thread, on one arp line's output buffer just before it is merged. Same shape as
// watchInputNotes and for the same reasons: a flag per pitch rather than a count, because a
// missed note-off would leak a refcount into a key lit forever, and a `changed` bump so the
// surface repaints only when something actually moved.
//
// **One bit per line since 2026-08-22**, which is what lets the keybed paint the key in that
// line's colour. It also fixes what the old single flag had to accept: two lines on one pitch
// shared it, so whichever released first put the key out under the line still playing it.
// Each line clears only its own bit now. Within a line it is still a flag and not a count -
// two harmony voices on one pitch, first note-off wins - the same trade at a smaller scope.
//
// `runArpLines` walks the lines in order on this thread, so no two *lines* race with each other
// - but the message thread does more than read: `clearArpNotes()` writes zeroes from there. The
// per-bit updates below are therefore atomic read-modify-writes rather than load-modify-stores;
// see the note on `set` for what the difference buys.
void KeysProcessor::watchArpNotes(const juce::MidiBuffer& midi, int line)
{
    if (line < 0 || line >= numArpLines)
        return;
    const unsigned int bit = 1u << line;
    bool changed = false;
    const auto set = [&](int note, bool on)
    {
        if (note < 0 || note > 127)
            return;
        auto& cell = arpNoteLines[(size_t) note];
        // **fetch_or / fetch_and, not load-modify-store.** `clearArpNotes()` runs on the
        // *message* thread - `allNotesOff()`, so the All Off chip, a panic and the MCP tool -
        // and stores 0 into every cell. A read, a clear landing between, and a write back would
        // resurrect the bits this line read a moment ago: bits belonging to *other* lines,
        // whose engines the same panic is about to flush, so no further note-off for that pitch
        // would ever arrive and the key would stay lit for the rest of the session. An atomic
        // read-modify-write has no window for the clear to land in, so a clear can only be
        // followed by a line setting its own bit for a note that genuinely is sounding.
        //
        // The previous value comes back from the same call, so `changed` costs no extra load.
        const unsigned int was = on ? cell.fetch_or(bit) : cell.fetch_and(~bit);
        changed = changed || (on ? (was & bit) == 0u : (was & bit) != 0u);
    };

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            set(m.getNoteNumber(), true);
        else if (m.isNoteOff())
            set(m.getNoteNumber(), false);
    }

    if (changed)
        soundingGen.fetch_add(1);
}

void KeysProcessor::clearArpNotes()
{
    for (auto& f : arpNoteLines)
        f.store(0u);
    soundingGen.fetch_add(1);
}

void KeysProcessor::watchInputNotes(const juce::MidiBuffer& midi)
{
    // Audio thread, and deliberately the first thing processBlock does: after the collector
    // drains, the buffer also holds the notes this plugin is playing, and lighting those
    // here would double-count what noteRefs already tracks.
    bool changed = false;
    const auto set = [&](int note, bool on)
    {
        if (note < 0 || note > 127 || inputNoteOn[(size_t) note].load() == on)
            return;
        inputNoteOn[(size_t) note].store(on);
        changed = true;
    };

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            set(m.getNoteNumber(), true);
        else if (m.isNoteOff())
            set(m.getNoteNumber(), false);
        else if (m.isAllNotesOff() || m.isAllSoundOff() || m.isResetAllControllers())
            for (int n = 0; n < 128; ++n)
                set(n, false);
    }

    if (changed)
        soundingGen.fetch_add(1); // the surface polls this and repaints only when it moves
}

void KeysProcessor::clearInputNotes()
{
    bool changed = false;
    for (auto& n : inputNoteOn)
        if (n.exchange(false))
            changed = true;
    if (changed)
        soundingGen.fetch_add(1);
}

std::vector<int> KeysProcessor::inputNotes() const
{
    std::vector<int> out;
    for (int n = 0; n < 128; ++n)
        if (inputNoteOn[(size_t) n].load())
            out.push_back(n);
    return out; // ascending by construction
}

void KeysProcessor::allNotesOff()
{
    // Per-note offs for every note on every channel, then CC123 (All Notes Off) for
    // synths that prefer the controller form. Notes end through their normal release
    // envelopes. Octavium also sent CC120 (All Sound Off), but that chokes tails
    // that are still releasing, which reads as a glitch rather than a stop, so it
    // is deliberately gone.
    cancelScheduledNotes(panicTag); // nothing queued may fire after a panic
    const double t = nowSeconds();
    // Every stream, not only the track output. An arp line's input queue can be holding a
    // chord this panic is meant to end, and its engine only lets go when a note-off reaches
    // it - so a panic that skipped the line collectors would silence the output while three
    // engines carried on arpeggiating chords nothing could release.
    // dest 0 twice: the track output has two queues since 2026-08-18 and a panic that flushed
    // only one would leave whatever the other is holding to sound on unreleased.
    for (int pass = 0; pass <= numArpLines + 1; ++pass)
    {
        const int dest = pass <= numArpLines ? pass : 0;
        auto& queue = chordQueueFor(dest, /*asChord*/ pass > numArpLines);
        for (int ch = 1; ch <= 16; ++ch)
        {
            for (int note = 0; note < 128; ++note)
            {
                auto off = juce::MidiMessage::noteOff(ch, note);
                off.setTimeStamp(t);
                queue.addMessageToQueue(off);
            }
            auto m = juce::MidiMessage::allNotesOff(ch);
            m.setTimeStamp(t);
            queue.addMessageToQueue(m);
        }
    }

    for (auto& dest : noteRefs)
        for (auto& ref : dest)
            ref.store(0);
    for (auto& q : chordStream)
        q.store(false); // nothing is sounding, so no pitch has a queue to go back to
    // ...and the arp lines' shared output counts, which the audio thread clears on its next
    // block. See ArpMerge::reset.
    arpOutClear.store(true, std::memory_order_relaxed);
    soundingGen.fetch_add(1);

    // All Off clears the input lights too. Keys cannot make someone's physical keyboard let
    // go, but if a note-off went missing the lit key it left behind is exactly the kind of
    // stuck thing this button exists to clear; the next key they press lights again.
    clearInputNotes();
    // ...and the arp's own lights, for the same reason: the engine is about to be silenced
    // from under them, so whatever they are showing is already not true.
    clearArpNotes();

    // The chord held into the arp is the one thing here that outlives a note-off, so a
    // panic has to forget it too - otherwise All Off silences it while the launched slot
    // still paints as playing and the next launch tries to release notes already gone.
    // Every line: a panic that left B holding is a panic that did not happen.
    for (auto& l : lines)
    {
        l.chordOn.clear();
        l.chordName = {};
        l.launchedSlot = -1;
        l.padSlot = -1;
    }
    // And anything Launch Quantize is still holding back. A panic that let a queued chord land
    // half a bar later would be a panic you have to press twice.
    pendingLaunches.clear();

    // And the Chain, for the same reason releaseArpHold() stops it: forgetting the chord is
    // only true until the next bar line, when heartbeatTick() launches the following slot and
    // the progression comes back out of a button whose whole job is silence. Last, not first,
    // so the clears above have already emptied every chordOn: stopChain() ends in
    // releaseArpChord(), which would otherwise emit note-offs for references the panic loop
    // has just zeroed.
    for (int n = 0; n < numArpLines; ++n)
        stopChain(n);
}

void KeysProcessor::sendCC(int controller, int value)
{
    auto m = juce::MidiMessage::controllerEvent(midiChannel(), controller, juce::jlimit(0, 127, value));
    m.setTimeStamp(nowSeconds());
    collector.addMessageToQueue(m);
}

void KeysProcessor::sendPitchBend(int value14)
{
    auto m = juce::MidiMessage::pitchWheel(midiChannel(), juce::jlimit(0, 16383, value14));
    m.setTimeStamp(nowSeconds());
    collector.addMessageToQueue(m);
}

void KeysProcessor::prepareToPlay(double sampleRate, int)
{
    collector.reset(sampleRate);
    chordCollector.reset(sampleRate);
    // Every buffer the arp stage touches is sized here and never grown on the audio thread.
    // Seven of them now rather than one: three inputs, three outputs, and the keybed's notes
    // lifted out of the merged stream for the lines that listen to it.
    keyNotes.ensureSize(8192);
    trackMidiAside.ensureSize(8192);
    trackNotesForArp.ensureSize(8192);
    for (auto& line : trackHeldByLine) // nothing is held across a prepare, the track included
        line.fill(false);
    streamRest.ensureSize(8192);
    arpMerged.ensureSize(8192);
    arpOut.reset(); // nothing is sounding across a prepare
    arpOutClear.store(false, std::memory_order_relaxed);
    for (auto& l : lines)
    {
        l.collector.reset(sampleRate);
        l.engine.prepare(sampleRate);
        l.in.ensureSize(8192);
        l.out.ensureSize(8192);
    }
}

bool KeysProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
   #if defined(KEYS_MIDI_EFFECT) && KEYS_MIDI_EFFECT
    juce::ignoreUnused(layouts); // no audio buses in the MIDI-effect variant
    return true;
   #else
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
   #endif
}

void KeysProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear(); // Keys makes no sound.

    // Before anything is added to the buffer: note what came *in*, so the keybed can show
    // someone playing a physical keyboard (or a clip) through Keys. Display only.
    watchInputNotes(midi);

    // Drain queued UI note events into the outgoing buffer. Anything already on the
    // track's MIDI (a clip, another device) is left in place and passes through.
    // **The track's MIDI is a door of its own, and the chip on the arp bar is the handle.**
    // Held aside here rather than gated inside runArpLines, because by the time that runs the
    // collector has merged and a note from the keybed is indistinguishable from one the DAW
    // sent - the same reason `dest` exists, and the same shape as the chordCollector split one
    // step further down. Everything goes aside, note-offs included: keeping only the note-ons
    // back would let the arp lift a note-off out of the stream whose note-on passed straight
    // through, and the instrument downstream would hang that note for good.
    const bool trackMidiToArp = arpTrackMidiOn();
    trackMidiJustClosed = lastTrackMidiToArp && ! trackMidiToArp;
    lastTrackMidiToArp = trackMidiToArp;

    trackMidiAside.clear();
    trackNotesForArp.clear();
    if (! trackMidiToArp)
    {
        trackMidiAside.addEvents(midi, 0, -1, 0);
        midi.clear();
    }
    else
    {
        // Open: nothing is held back, but the notes are noted, because this is the last point
        // at which they are still distinguishable. One block later the collector has merged and
        // a clip's C4 and a clicked C4 are the same MidiMessage - the same reason `dest` exists.
        // runArpLines turns this into per-line ownership so the falling edge can release what a
        // line took from the *track* without touching what it took from anywhere else.
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn() || m.isNoteOff())
                trackNotesForArp.addEvent(m, meta.samplePosition);
        }
    }

    collector.removeNextBlockOfMessages(midi, buffer.getNumSamples());

    // Arp stage: one engine per line, each consuming its own note stream and emitting its own; CCs
    // pass through. The engines read the host playhead (the one deliberate exception to Keys'
    // old never-reads-the-playhead rule; see docs/ARP_DESIGN.md) and free-run on an internal
    // clock at the last-known tempo when the transport is stopped.
    runArpLines(midi, buffer.getNumSamples());

    // Back into the stream, after the arp has taken its copy of what was played and before the
    // chords join it. Untouched and in sample order, so a clip on this track reaches the
    // instrument exactly as it always did - the chip decides what the *arpeggiator* hears, not
    // what the track plays.
    if (! trackMidiToArp)
        midi.addEvents(trackMidiAside, 0, -1, 0);

    // Chords last, and that is the whole of "a click never feeds the arpeggiator" (2026-08-18).
    // A pad, the live card and the generator's audition queue here instead of into `collector`,
    // so by the time they join the outgoing stream every line has already taken its copy of what
    // was played on the keys. They are otherwise ordinary output: same buffer, same channel, same
    // instrument downstream, and captureBlock below still records them as part of the take.
    chordCollector.removeNextBlockOfMessages(midi, buffer.getNumSamples());

    advanceChainClock(buffer.getNumSamples());

    // Last, so the take holds the stream that actually leaves: arpeggiated where a line is
    // running, strummed where a pad strummed, on whichever channel the line sent it on.
    // Recording what the *UI* asked for instead would capture the chord you clicked and not
    // the arpeggio you heard, which is the wrong take by exactly the interesting part.
    // Unconditional: captureBlock does its own armed check, and the acquire load that check
    // makes is load-bearing (it pairs with the release store in setRecording), so it lives with
    // the ring rather than out here where it could be reached for with the wrong ordering.
    take.captureBlock(midi, buffer.getNumSamples(), getSampleRate());
}

// The take itself lives in src/TakeRecorder.h/.cpp: the ring the audio thread writes, the
// vector the message thread drains it into, and the MIDI file both of them agree on. What is
// left here is the processor's half of the contract - the two facts the recorder cannot know
// on its own (this instance's sample rate, and its tempo at the moment you arm) and the
// forwarders every caller already had.
void KeysProcessor::setRecording(bool shouldRecord)
{
    take.setRecording(shouldRecord, currentTempo());
}

double KeysProcessor::capturedSeconds() const { return take.capturedSeconds(); }
bool KeysProcessor::capturedHasNotes() const { return take.capturedHasNotes(); }
bool KeysProcessor::buildTakeMidiFile(juce::MidiFile& out) const { return take.buildTakeMidiFile(out); }
std::vector<KeysProcessor::TakeNote> KeysProcessor::takeNotes() const { return take.takeNotes(); }
juce::File KeysProcessor::takeFolder() { return TakeRecorder::takeFolder(); }
juce::File KeysProcessor::writeTake() { return take.writeTake(); }

namespace
{
// Merge one arp line's notes into the outgoing stream, on the channel that line names. The
// restamp covers the *whole* buffer rather than the note-ons alone: a note that ends on a
// channel it never started on is a note that never ends, since the instrument downstream is
// matching pitch and channel. Channel 0 means "leave it alone", which is the global channel
// the notes already carry and by far the common case, so it costs a branch and no copy.
void mergeArpOut(juce::MidiBuffer& dest, const juce::MidiBuffer& src, int channel)
{
    if (channel <= 0)
    {
        dest.addEvents(src, 0, -1, 0);
        return;
    }
    for (const auto meta : src)
    {
        auto m = meta.getMessage();
        if (m.getChannel() > 0) // 0 = a message with no channel of its own; leave those be
            m.setChannel(juce::jlimit(1, 16, channel));
        dest.addEvent(m, meta.samplePosition);
    }
}
} // namespace

// Audio thread. One line's worth of engine parameters, read through the cached pointers so
// nothing here builds a string or takes a lock.
// The dice on a line's macro card (2026-08-21, Owen: "I use the random ones a lot, and I'd like
// to have a dice button when those are active nearby to regenerate their pattern"). All it does
// is bump a counter; runArpLines below notices and asks that engine for a new order, so nothing
// on the message thread ever writes engine state. See ArpEngine::rerollRandomOrder.
void KeysProcessor::rerollArpRandom(int line)
{
    if (line < 0 || line >= numArpLines)
        return;
    lines[(size_t) line].rerollRequest.fetch_add(1, std::memory_order_relaxed);
}

void KeysProcessor::runArpLines(juce::MidiBuffer& midi, int numSamples)
{
    // Which lines want the keys you play. A line that is off gets nothing: its input still
    // arrives (a chord held to it sustains, below), but there is no engine running to hand
    // the keybed to.
    bool anyListens = false;
    bool listens[numArpLines] = {};
    for (int n = 0; n < numArpLines; ++n)
    {
        listens[(size_t) n] = arpLineOn(n) && arpParam(n, apKeys) > 0.5f;
        anyListens = anyListens || listens[(size_t) n];
    }

    // Each line's own queue first. These are the chords handed to it - by a card, a slot or a
    // chain - and they belong to that line whether or not it is also listening to the keys.
    arpMerged.clear();
    for (auto& l : lines)
    {
        l.in.clear();
        l.collector.removeNextBlockOfMessages(l.in, numSamples);
    }

    // The keybed and a clip on the track: what arrives in the merged stream by now is what was
    // *played*, and any line that listens gets a copy. Lifting the notes out is what makes the
    // arp replace them rather than double them, and it is skipped entirely when nobody is
    // listening - which is exactly the behaviour of the arp being off.
    //
    // **A pad's chord is not in this stream** (2026-08-18, Owen: "as soon as you click a chord in
    // the pad, it automatically sends it to the arpeggiator ... we only want the arpeggiator to
    // go if you drag a chord on top of it"). It used to be, along with the live card and the
    // generator's audition, and a line with Play on lifted all three - so clicking a pad fed the
    // arpeggiator exactly as pressing a key did, which is the one thing a click has not been
    // allowed to do since 2026-08-02. Play means the keys you play; a chord reaches a line by
    // being dragged onto it, or through the pad menu's Send to arp rows, and either way it goes
    // to that line's own queue above. Those three sources now drain from `chordCollector` in
    // processBlock, after this runs, which is what puts them out of reach here.
    if (anyListens)
    {
        keyNotes.clear();
        streamRest.clear();
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn() || m.isNoteOff())
                keyNotes.addEvent(m, meta.samplePosition);
            else
                streamRest.addEvent(m, meta.samplePosition);
        }
        // Copied back rather than swapped. `swapWith` is the cheaper move and was what this did,
        // but it hands our `ensureSize`d storage to the host's buffer and keeps the host's in
        // `streamRest` - so the one buffer prepareToPlay sized for this is the one we give away
        // on the first block, and every block after that partitions into whatever capacity the
        // host happened to have. `clear()` does not shrink, so it settles, but "settles" is not
        // the guarantee the no-allocation-on-the-audio-thread rule asks for. The copy is of the
        // non-note events alone (CCs, bend, clock), which is a handful of bytes a block.
        midi.clear();
        midi.addEvents(streamRest, 0, -1, 0);
        for (int n = 0; n < numArpLines; ++n)
            if (listens[(size_t) n])
                lines[(size_t) n].in.addEvents(keyNotes, 0, numSamples, 0);
    }

    // Ownership, kept per line, and it is what the falling edge below is allowed to release.
    // Updated inside the same `listens` gate that hands the notes over, so a line that was not
    // listening when a clip's note arrived never acquires a bit for it, and a line that stops
    // listening keeps the bits for what it is still holding - which is exactly the set that
    // would otherwise hang. Walked in event order rather than folded into two masks, because a
    // block can carry an off and a fresh on for one pitch and only the order says which won.
    if (! trackNotesForArp.isEmpty())
        for (int n = 0; n < numArpLines; ++n)
            if (listens[(size_t) n])
                for (const auto meta : trackNotesForArp)
                {
                    const auto m = meta.getMessage();
                    const int note = m.getNoteNumber();
                    if (note >= 0 && note < 128)
                        trackHeldByLine[(size_t) n][(size_t) note] = m.isNoteOn();
                }

    // The falling edge of the Track MIDI chip, and it is the one thing that switch cannot be
    // implemented without. A line that had already taken a clip's notes in is holding pitches
    // whose note-offs are now being routed around it for good, so it would arpeggiate them
    // forever under a switch that says the door is shut. Their releases are synthesised here,
    // into the lines' own input and nowhere else: the real note-offs still travel down the
    // output stream when the clip lets go, so the instrument downstream never notices.
    //
    // **It fires from the ownership mask above, never from `inputNoteOn`, and that is not
    // tidying.** `inputNoteOn` is every pitch the *track* is holding, whether or not any line
    // ever received its note-on - a clip note that began while the door was shut is in it. But
    // ArpEngine::Held::ons is a count over every source that asked for a pitch and noteLeft
    // matches on pitch alone, so an off synthesised for one of those decrements whichever owner
    // is there: hold C4 on the keybed over a clip already sounding C4, close the door, and the
    // line drops the note under your hand. The mask holds only what this line actually took
    // from the track, so every off fired here has an owner of its own to spend.
    if (trackMidiJustClosed)
        for (int n = 0; n < numArpLines; ++n)
        {
            auto& held = trackHeldByLine[(size_t) n];
            for (int note = 0; note < 128; ++note)
                if (held[(size_t) note])
                {
                    lines[(size_t) n].in.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                    held[(size_t) note] = false;
                }
        }

    for (int n = 0; n < numArpLines; ++n)
    {
        auto& l = lines[(size_t) n];
        const bool arpOn = arpLineOn(n);
        l.out.clear();

        // 0 = the global channel, 1..16 = this line's own. Read once a block, like everything
        // else here, so a host automating it is seen at the next boundary.
        const int channel = (int) arpParam(n, apChannel);

        if (arpOn != l.lastOn)
        {
            l.lastOn = arpOn;
            if (arpOn)
                // restart(), not hardReset(): the chord handed to this line while it was off
                // is sitting in the engine's held set, and it is the whole reason there is
                // something to play the instant the switch goes on (2026-08-02, Owen: "when
                // you turn on the arp, it should start playing whatever card is loaded ...
                // right now it only plays when you drop a new line on"). hardReset() threw
                // that chord away, which is exactly what made a freshly switched-on line sit
                // there silent.
                l.engine.restart();
            else
                l.engine.flushInto(l.out); // nothing may ring after bypassing
        }
        // A change of channel is a change of where the notes are going, so what is still
        // ringing has to be closed on the channel it started on. Flushed and merged under the
        // *old* channel before lastChannel moves, because restamping this line's whole output
        // with the new one would send those note-offs somewhere the notes never sounded.
        //
        // Into `arpMerged`, never straight into `midi`: every note-on this line emitted was
        // counted by `arpOut`, so its note-offs have to be counted back down by the same
        // refcounts. Writing them past the merge left `held[ch][note]` stuck above zero for
        // good, and a count that never returns to 0 suppresses the *real* note-off of every
        // later hit on that pitch - a note hung on the instrument for the rest of the session.
        // The flush emits at sample 0, and MidiBuffer keeps its events in sample order, so
        // these still close ahead of anything this block goes on to play.
        if (channel != l.lastChannel)
        {
            l.engine.flushInto(l.out);
            watchArpNotes(l.out, n);
            mergeArpOut(arpMerged, l.out, l.lastChannel);
            l.out.clear();
            l.lastChannel = channel;
        }

        // **There is no bypass branch.** The engine runs every block and `ap.enabled` decides
        // only whether it *fires* steps - `noteArrived` is outside that gate, so a line that is
        // off still takes the chord in and remembers it. That one fact answers both halves of
        // what Owen asked for on 2026-08-02: dropping a card on a line that is off makes no
        // sound ("I don't want it to play the chord sound when you release"), because those
        // note-ons are consumed by the engine instead of passing through to the output; and
        // switching that line on starts it arpeggiating what it is already holding.
        //
        // What this replaces: an `if (! arpOn)` branch that merged `l.in` straight into the
        // output, so the chord sustained like a pad and the engine never saw it. That was the
        // honest reading while a line was a thing you switched *between*; with two of them fed
        // by dragging cards on, "hand it over now, start it when I say" is the gesture.
        //
        // The keybed is unaffected either way: `listens[n]` is false with the line off, so
        // notes you play are never lifted out of the merged stream and sound as they always
        // have. Playing the instrument is never gated on an arp switch.
        ArpEngine::Params ap;
        ap.enabled = arpOn;
        ap.rateIndex = (int) arpParam(n, apRate);
        // Free: the rate is a frequency and the engine free-runs at it whatever the transport
        // is doing. Both read every block like every other arp global, so the mode can be
        // automated and the engine sees the change on the next boundary.
        ap.rateFree = arpParam(n, apRateFree) > 0.5f;
        ap.rateHz = (double) arpParam(n, apRateHz);
        ap.dotted = arpParam(n, apDot) > 0.5f;
        // apTrip is not read here any more: migrateTuplet folds it into Tuplet on load, and
        // nothing on screen writes it. It stays registered so a saved session still parses.
        ap.tuplet = tupletFor((int) arpParam(n, apTuplet));
        ap.humanizeSpan = (int) arpParam(n, apHumanizeSpan);
        ap.humanVelSpan = (int) arpParam(n, apHumanVelSpan);
        ap.drift = (int) arpParam(n, apDrift);
        ap.mutate = (int) arpParam(n, apMutate);
        ap.mutateLock = (int) arpParam(n, apMutateLock);
        ap.mutateSeed = n; // so two lines at the same Mutate never explore in lockstep
        // The two fixed harmony voices, handed over as semitones: the choice index means
        // nothing to the engine, and keeping the interval table out of it is the same
        // decision as the scale mask below.
        ap.harmSemis[0] = harmonySemisFor((int) arpParam(n, apHarm1));
        ap.harmSemisB[0] = harmonySemisSecondFor((int) arpParam(n, apHarm1));
        ap.harmChance[0] = (int) arpParam(n, apHarm1Chance);
        ap.harmSemis[1] = harmonySemisFor((int) arpParam(n, apHarm2));
        ap.harmSemisB[1] = harmonySemisSecondFor((int) arpParam(n, apHarm2));
        ap.harmChance[1] = (int) arpParam(n, apHarm2Chance);
        ap.stray = (int) arpParam(n, apStray);
        ap.legato = arpParam(n, apLegato) > 0.5f;
        // The line bus: a record is handed over only for a line that ran *before* this one in
        // this loop, whatever the parameter says. A later letter's record would be last block's,
        // and A ducking to B ducking to A is the loop the downward rule exists to forbid. The UI
        // greys the letters it may not pick; this is the rule for everything that is not the UI.
        {
            const int src = (int) arpParam(n, apFollow) - 1;
            ap.follow = (src >= 0 && src < n) ? &lines[(size_t) src].engine.record : nullptr;
            ap.duck = (int) arpParam(n, apDuck);
            ap.resetFollow = arpParam(n, apResetFollow) > 0.5f;
        }
        ap.anchored = arpParam(n, apAnchor) > 0.5f;
        ap.direction = (ArpEngine::Direction) (int) arpParam(n, apDirection);
        ap.usePattern = arpParam(n, apPattern) > 0.5f;
        ap.octaveRange = (int) arpParam(n, apOctaves);
        ap.swing = arpParam(n, apSwing);
        ap.latch = arpParam(n, apLatch) > 0.5f;
        ap.retrigger = arpParam(n, apRetrigger) > 0.5f;
        ap.gate = (int) arpParam(n, apGate);
        ap.chance = (int) arpParam(n, apChance);
        ap.offset = (int) arpParam(n, apOffset);
        ap.velRamp = (int) arpParam(n, apVelRamp);
        ap.rampBeats = (double) (int) arpParam(n, apRampBeats);
        ap.humanize = (int) arpParam(n, apHumanize);
        ap.humanVel = (int) arpParam(n, apHumanVel);
        ap.octShift = (int) arpParam(n, apOctShift);
        ap.volume = (int) arpParam(n, apVolume);
        ap.velLevel = (int) arpParam(n, apVelLevel);
        ap.chords = &l.chordTable; // what the Chord lane calls up, this line's slots

        // Distance: what each repeat past the first adds. The list names intervals rather
        // than numbers because "5th" is the thing you want and "+7 semitones" is the way you
        // would have had to ask for it; the scale-relative half of the list is the part no
        // stock arp offers, and it costs Keys nothing because Root and Scale are already here.
        {
            static constexpr int dist[]  = { 12, 7, 5, 4, 3, 1, 2, 4, 6 };
            static constexpr bool degs[] = { false, false, false, false, false, true, true, true, true };
            const int di = juce::jlimit(0, (int) std::size(dist) - 1,
                                        (int) arpParam(n, apDistance));
            ap.spread = dist[di];
            ap.spreadDegrees = degs[di];
        }
        // Restart every N beats, on top of the restart a new chord asks for.
        {
            static constexpr double bars[] = { 0.0, 1.0, 2.0, 4.0, 8.0, 16.0 };
            ap.retrigBeats = bars[juce::jlimit(0, (int) std::size(bars) - 1,
                                               (int) arpParam(n, apRetrigBars))];
        }
        // The scale, as a mask of pitch classes, for a Distance counted in scale degrees.
        // Built here rather than in the engine so ArpEngine.h stays free of the scale tables
        // and its tests can state a scale as a number.
        ap.rootPc = (int) apvts.getRawParameterValue("root")->load();
        {
            const int scaleIdx = (int) apvts.getRawParameterValue("scale")->load();
            unsigned int mask = 0;
            for (int k = 0; k < 12; ++k)
                if (okstudio::scales::isInScale(ap.rootPc + k, ap.rootPc, scaleIdx))
                    mask |= 1u << k;
            ap.scaleMask = mask != 0 ? mask : 0xFFFu; // an empty table would silence degree walking
        }
        // Scale Lock reaches the line's output as well as the keybed's (2026-08-26). Global,
        // like Root and Scale beside it and unlike everything else in this loop: it is the
        // keybed's own toggle on the Controls bar, and a lock that held on one line and not
        // another would not be a lock. See ArpEngine::snapToMask.
        ap.scaleLock = apvts.getRawParameterValue("scaleLock")->load() > 0.5f;

        ArpEngine::HostClock hc;
        if (auto* playHead = getPlayHead())
            if (auto pos = playHead->getPosition())
            {
                hc.playing = pos->getIsPlaying();
                if (auto bpm = pos->getBpm(); bpm && *bpm > 0.0)
                {
                    hc.bpm = *bpm;
                    hc.hasBpm = true; // an answer from the host, not HostClock's own default
                }
                if (auto ppq = pos->getPpqPosition())
                {
                    hc.ppq = *ppq;
                    hc.hasPpq = true;
                }
            }
        // Every line anchors to one grid, and there is always a grid (2026-08-18). While the host
        // rolls that is its own position, read fresh above; otherwise it is `arpBeats`, the count
        // this processor keeps for Launch Quantize to measure from - the host's position while it
        // has one, its own beats when it does not, which is the only clock the standalone has.
        //
        // Read *before* advanceChainClock adds this block to it, so it is the position at the
        // start of the block, which is what `ppq` means to the engine. All three lines are handed
        // the same number in the same block, which is the entire point: it is what lets two
        // anchored lines walk in lockstep with no transport to follow. `anchored` off still opts a
        // line out into a free-running phase of its own.
        hc.hasGrid = true;
        if (! (hc.playing && hc.hasPpq))
            hc.ppq = arpBeats.load(std::memory_order_relaxed);
        // No transport to follow: run at the BPM control in the Controls section. It used to
        // be the host's last-known tempo, which was unreachable in the standalone (where
        // there is no host at all) and unchangeable everywhere.
        ap.fallbackBpm = (double) apvts.getRawParameterValue("bpm")->load();
        // Tempo Sync: off pins every line to fallbackBpm above even with the host rolling.
        ap.followHost = apvts.getRawParameterValue("bpmSync")->load() > 0.5f;

        // The dice, picked up here so every write to the engine's own state stays on this
        // thread (2026-08-21). Compared rather than tested-and-cleared: the counter is the
        // message thread's alone to write and this side only ever asks whether it moved, so
        // neither thread has to write the other's variable.
        //
        // **It does not mean two clicks in one block are two rerolls** - the comparison fires
        // once however far the counter jumped, and it is right to. rerollRandomOrder() only
        // sets permDirty, so it is idempotent: dealing twice before a step reads the order is
        // indistinguishable from dealing once, and the second deal would be a shuffle nobody
        // could ever hear. What the counter buys over a flag is only the single-writer split
        // above.
        if (const int req = l.rerollRequest.load(std::memory_order_relaxed); req != l.rerollSeen)
        {
            l.rerollSeen = req;
            l.engine.rerollRandomOrder();
        }

        // The engine's input is this line's buffer alone, never the merged stream: that is the
        // whole of the routing. Its output goes into midi with everything the other lines and
        // the pass-through left there, so two lines at two rates simply sum.
        l.engine.process(ap, hc, numSamples, l.in, l.out);
        watchArpNotes(l.out, n);
        // Into the shared buffer rather than straight out: the lines have to be interleaved in
        // time before the overlap rule below can run over them. MidiBuffer keeps its events
        // sorted by sample position, and equal positions stay in insertion order, so a line's
        // note-off and a later line's note-on at the same offset settle in line order.
        mergeArpOut(arpMerged, l.out, channel);
    }

    mergeArpLines(midi);
}

// Audio thread. The rule itself is ArpMerge, beside the engine and free of this class so it can be
// driven from a test with two hand-built buffers; all this does is hand it the block.
void KeysProcessor::mergeArpLines(juce::MidiBuffer& midi)
{
    if (arpOutClear.exchange(false, std::memory_order_relaxed))
        arpOut.reset();
    arpOut.merge(arpMerged, midi);
}

void KeysProcessor::advanceChainClock(int numSamples)
{
    // Tempo and bar length are the same question whichever line is asking, so they are asked
    // once and the three chains are stepped against the same answer.
    double bpm = (double) apvts.getRawParameterValue("bpm")->load();
    // Tempo Sync: off keeps the parameter above even with the host rolling, the same escape
    // hatch ArpEngine::process reads via Params::followHost. hostBpmLive is published below
    // for the Controls bar to show and to grey its own stepper: neither the parameter nor a
    // stepper drag can change anything while the host is the one actually setting the tempo.
    const bool followHost = apvts.getRawParameterValue("bpmSync")->load() > 0.5f;
    bool hostBpmLive = false;
    double beatsPerBar = 4.0;
    if (auto* playHead = getPlayHead())
        if (auto pos = playHead->getPosition())
        {
            // Not gated on getIsPlaying() (2026-08-16, Owen: "bpm isn't syncing with daw"). A
            // DAW's tempo is its tempo stopped or rolling, and following it only while rolling
            // meant the number on the bar disagreed with Live's for exactly as long as you were
            // setting up - which is when you look at it. The *position* read below keeps its
            // `playing` test, because a position genuinely means nothing while stopped.
            if (followHost)
                if (auto hostBpm = pos->getBpm(); hostBpm && *hostBpm > 0.0)
                {
                    bpm = *hostBpm;
                    hostBpmLive = true;
                }
            // A bar is four beats only in four-four. Asking the host costs nothing and is
            // the difference between a chain that lands on the bar in 3/4 and one that does
            // not; with no host to ask, four it is.
            if (auto sig = pos->getTimeSignature(); sig && sig->denominator > 0)
                beatsPerBar = 4.0 * (double) sig->numerator / (double) sig->denominator;
        }
    arpHostBpmLive.store(hostBpmLive, std::memory_order_relaxed);

    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const double blockBeats = bpm / 60.0 / sr * numSamples;

    // Publish where we are, in beats, for Launch Quantize to measure the next boundary from.
    // The host's own position while it is rolling, so a quantized launch lands on the *host's*
    // bar line and not on one of our own invention; otherwise a count of our own, which is the
    // only clock there is in the standalone. Both are read on the message thread, which turns
    // "beats until the boundary" into a wall-clock deadline and waits that out on a 1 ms timer -
    // so this only has to be right to within a block, not to a sample.
    {
        bool haveHostPos = false;
        if (auto* playHead = getPlayHead())
            if (auto pos = playHead->getPosition())
                if (pos->getIsPlaying())
                    if (auto ppq = pos->getPpqPosition())
                    {
                        arpBeats.store(*ppq, std::memory_order_relaxed);
                        haveHostPos = true;
                    }
        if (! haveHostPos)
            arpBeats.store(arpBeats.load(std::memory_order_relaxed) + blockBeats,
                           std::memory_order_relaxed);
        arpBeatsBpm.store(bpm, std::memory_order_relaxed);
    }

    for (auto& ln : lines)
    {
        if (! ln.chainActive.load(std::memory_order_relaxed))
        {
            ln.chainBeatsPlayed = 0.0;
            continue;
        }

        const int epoch = ln.chainEpoch.load(std::memory_order_acquire);
        if (epoch != ln.chainSeenEpoch)
        {
            ln.chainSeenEpoch = epoch;
            ln.chainBeatsPlayed = 0.0; // a slot was just launched: its bars start now
        }

        ln.chainBeatsPlayed += blockBeats;
        const double target = ln.chainTargetBeats.load(std::memory_order_relaxed) * beatsPerBar / 4.0;
        if (ln.chainBeatsPlayed >= target)
        {
            ln.chainBeatsPlayed = 0.0; // the epoch bump that follows will zero it again, harmlessly
            ln.chainAdvance.store(true, std::memory_order_release);
        }
    }
}

// --- Undo -------------------------------------------------------------------------------
//
// See the header for why this is content-only and why an entry is a whole-subtree snapshot
// rather than a hand-written inverse of each action.

juce::ValueTree KeysProcessor::snapshotFor(UndoScope scope) const
{
    return scope == UndoScope::pads ? chordPadsToTree() : arpToTree();
}

void KeysProcessor::restore(const UndoEntry& e)
{
    // Both of these want the *root* the session file uses, and each snapshot is already that
    // subtree - so wrap it back up in a root of the right shape before handing it over.
    juce::ValueTree root { "KEYS" };
    root.appendChild(e.before.createCopy(), nullptr);
    if (e.scope == UndoScope::pads)
        chordPadsFromTree(root);
    else
        arpFromTree(root);
}

void KeysProcessor::pushUndo(const juce::String& label, UndoScope scope)
{
    if (undoGestureDepth > 0)
        return; // inside an open gesture: the outermost push already took the "before"

    undoStack.push_back({ label, scope, snapshotFor(scope) });
    if ((int) undoStack.size() > maxUndoDepth)
        undoStack.erase(undoStack.begin());

    // A new edit ends the redo branch, which is what every undo stack does and what people
    // expect: once you change something after undoing, the future you undid is gone.
    redoStack.clear();
    undoGen.fetch_add(1, std::memory_order_relaxed);
}

KeysProcessor::UndoGesture::UndoGesture(KeysProcessor& p, const juce::String& label, UndoScope scope)
    : processor(p)
{
    p.pushUndo(label, scope);   // the outermost one; any nested push is absorbed
    ++p.undoGestureDepth;
}

KeysProcessor::UndoGesture::~UndoGesture()
{
    --processor.undoGestureDepth;
}

void KeysProcessor::undo()
{
    if (undoStack.empty())
        return;
    auto entry = undoStack.back();
    undoStack.pop_back();

    // The current state becomes the redo entry, taken *before* the restore overwrites it.
    redoStack.push_back({ entry.label, entry.scope, snapshotFor(entry.scope) });
    if ((int) redoStack.size() > maxUndoDepth)
        redoStack.erase(redoStack.begin());

    // Undoing must never leave a note ringing that nothing owns any more. Restoring pads can
    // rewrite the chord a sustained card is holding, and restoring the arp can rewrite the
    // lanes under a running line, so let go of everything first - the same choke point an
    // audition uses, and for the same reason.
    stopAllChordPads();
    restore(entry);
    undoGen.fetch_add(1, std::memory_order_relaxed);
}

void KeysProcessor::redo()
{
    if (redoStack.empty())
        return;
    auto entry = redoStack.back();
    redoStack.pop_back();

    undoStack.push_back({ entry.label, entry.scope, snapshotFor(entry.scope) });
    if ((int) undoStack.size() > maxUndoDepth)
        undoStack.erase(undoStack.begin());

    stopAllChordPads();
    restore(entry);
    undoGen.fetch_add(1, std::memory_order_relaxed);
}

void KeysProcessor::clearUndoHistory()
{
    undoStack.clear();
    redoStack.clear();
    undoGen.fetch_add(1, std::memory_order_relaxed);
}

juce::ValueTree KeysProcessor::chordPadsToTree() const
{
    juce::ValueTree pads { "chordPads" };
    // Written since pads went 16-a-page; loaders remap older 8-a-page slots by it.
    pads.setProperty("padsPerPage", padsPerPage, nullptr);
    for (int i = 0; i < numChordPads; ++i)
    {
        const auto& p = chordPads[(size_t) i];
        if (p.notes.empty())
            continue;
        juce::StringArray ns;
        for (int n : p.notes)
            ns.add(juce::String(n));
        juce::ValueTree pad { "pad" };
        pad.setProperty("slot", i, nullptr);
        pad.setProperty("notes", ns.joinIntoString(","), nullptr);
        pad.setProperty("name", p.name, nullptr);
        pad.setProperty("locked", p.locked, nullptr);
        pad.setProperty("rootPc", p.rootPc, nullptr);
        pad.setProperty("type", p.type, nullptr);
        pad.setProperty("degree", p.degree, nullptr);
        if (p.numeral.isNotEmpty())
            pad.setProperty("numeral", p.numeral, nullptr);
        // Written only when there is one, like `numeral` above: most pads are not part of a named
        // progression, and a property on every pad saying so would be noise in the session file.
        if (p.progression.isNotEmpty())
        {
            pad.setProperty("progression", p.progression, nullptr);
            pad.setProperty("progressionStep", p.progressionStep, nullptr);
        }
        pads.appendChild(pad, nullptr);
    }
    return pads;
}

void KeysProcessor::chordPadsFromTree(const juce::ValueTree& root)
{
    stopAllChordPads();
    for (auto& p : chordPads)
        p = {};
    const auto pads = root.getChildWithName("chordPads");
    if (! pads.isValid())
        return;
    // A session stores its slots in its own page width; keep each pad on the page it was on by
    // re-basing that slot into the current width.
    //
    // **Narrowing drops the overflow, and must.** This was written for 8 -> 16, where every old
    // position still had a home; 16 -> 12 (2026-08-03) does not, and the old formula wrapped
    // positions 12..15 onto the *front of the next page*, where they overwrote that page's own
    // pads as the loop went on - silent, and worse than the loss it was trying to avoid. A pad
    // past the end of its page now has nowhere to go and is dropped, which is Owen's call
    // (2026-08-03) and is called out in the changelog: it is not reversible.
    const int savedPerPage = (int) pads.getProperty("padsPerPage", 8);
    for (int c = 0; c < pads.getNumChildren(); ++c)
    {
        const auto pad = pads.getChild(c);
        int slot = (int) pad.getProperty("slot", -1);
        if (slot >= 0 && savedPerPage > 0 && savedPerPage != padsPerPage)
        {
            const int pos = slot % savedPerPage;
            if (pos >= padsPerPage)
                continue; // the page got shorter under it
            slot = (slot / savedPerPage) * padsPerPage + pos;
        }
        if (slot < 0 || slot >= numChordPads)
            continue;
        ChordPad p;
        for (const auto& s : juce::StringArray::fromTokens(pad.getProperty("notes").toString(), ",", ""))
            if (s.trim().isNotEmpty())
                p.notes.push_back(s.trim().getIntValue());
        p.name = pad.getProperty("name").toString();
        p.locked = (bool) pad.getProperty("locked", false);
        p.rootPc = (int) pad.getProperty("rootPc", -1); // absent in pre-pages sessions
        p.type = (int) pad.getProperty("type", -1);
        p.degree = (int) pad.getProperty("degree", -1);
        p.numeral = pad.getProperty("numeral").toString(); // absent in pre-Markov sessions
        p.progression = pad.getProperty("progression").toString(); // absent in pre-library sessions
        p.progressionStep = (int) pad.getProperty("progressionStep", -1);
        chordPads[(size_t) slot] = p;
    }
}

// Everything below takes a line index. Line 0 is the arpeggiator Keys has always had, and
// every one of these defaults to it, so a caller that has not learned about lines yet still
// drives the same arp it always did.

void KeysProcessor::storeActiveArpPattern(int line)
{
    auto& ln = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)];
    auto& pat = ln.patterns[(size_t) ln.activePattern];
    for (int l = 0; l < ArpEngine::numLanes; ++l)
    {
        for (int s = 0; s < ArpEngine::maxSteps; ++s)
            pat.value[(size_t) l][(size_t) s] = ln.engine.lanes.value[(size_t) l][(size_t) s].load();
        pat.length[(size_t) l] = ln.engine.lanes.length[(size_t) l].load();
        pat.clockDiv[(size_t) l] = ln.engine.lanes.clockDiv[(size_t) l].load();
        pat.on[(size_t) l] = ln.engine.lanes.on[(size_t) l].load();
        pat.loopFrom[(size_t) l] = ln.engine.lanes.loopFrom[(size_t) l].load();
        pat.loopTo[(size_t) l] = ln.engine.lanes.loopTo[(size_t) l].load();
        pat.dir[(size_t) l] = ln.engine.lanes.dir[(size_t) l].load();
    }
    for (int i = 0; i < 4; ++i)
        pat.rhythmDivs[(size_t) i] = ln.engine.rhythmDiv[(size_t) i].load();
    pat.harmonyMode = ln.engine.harmonyMode.load();
}

int KeysProcessor::arpActivePattern(int line) const
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].activePattern;
}

void KeysProcessor::recallArpPattern(int index, int line)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    storeActiveArpPattern(line);
    auto& ln = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)];
    ln.activePattern = index;
    const auto& pat = ln.patterns[(size_t) index];
    for (int l = 0; l < ArpEngine::numLanes; ++l)
    {
        for (int s = 0; s < ArpEngine::maxSteps; ++s)
            ln.engine.lanes.value[(size_t) l][(size_t) s].store(pat.value[(size_t) l][(size_t) s]);
        ln.engine.lanes.length[(size_t) l].store(juce::jlimit(1, ArpEngine::maxSteps, pat.length[(size_t) l]));
        ln.engine.lanes.clockDiv[(size_t) l].store(juce::jlimit(0, 2, pat.clockDiv[(size_t) l]));
        ln.engine.lanes.on[(size_t) l].store(pat.on[(size_t) l] != 0 ? 1 : 0);
        ln.engine.lanes.loopFrom[(size_t) l].store(juce::jlimit(0, ArpEngine::maxSteps - 1, pat.loopFrom[(size_t) l]));
        ln.engine.lanes.loopTo[(size_t) l].store(juce::jlimit(0, ArpEngine::maxSteps - 1, pat.loopTo[(size_t) l]));
        ln.engine.lanes.dir[(size_t) l].store(juce::jlimit(0, (int) ArpEngine::numLaneDirs - 1, pat.dir[(size_t) l]));
    }
    for (int i = 0; i < 4; ++i)
        ln.engine.rhythmDiv[(size_t) i].store(juce::jlimit(0, 16, pat.rhythmDivs[(size_t) i]));
    ln.engine.harmonyMode.store(juce::jlimit(0, 1, pat.harmonyMode));
}

bool KeysProcessor::arpQuantizeOn() const
{
    return apvts.getRawParameterValue("arpQuantize")->load() > 0.5f;
}

double KeysProcessor::arpQuantizeDelayMs() const
{
    // Off, 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars - in beats. Four to the bar, which is the same
    // assumption `chainTargetBeats` makes; a chain in 3/4 is corrected on the audio thread by
    // the host's time signature, and this is not, because a launch waiting one beat too long is
    // a launch that still landed on a beat. Worth revisiting if anyone works in 7/8.
    static constexpr double beats[] = { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
    const int idx = juce::jlimit(0, (int) std::size(beats) - 1,
                                 (int) apvts.getRawParameterValue("arpQuantize")->load());
    const double div = beats[idx];
    if (div <= 0.0)
        return 0.0;

    const double now = arpBeats.load(std::memory_order_relaxed);
    const double bpm = juce::jmax(1.0, arpBeatsBpm.load(std::memory_order_relaxed));
    // The next boundary strictly ahead of us. `+ 1e-9` before the floor rather than a plain
    // ceil, so a click that lands exactly on a boundary fires now instead of waiting a whole
    // division for the next one.
    const double next = (std::floor(now / div + 1.0e-9) + 1.0) * div;
    return juce::jmax(0.0, (next - now) / bpm * 60000.0);
}

int KeysProcessor::arpPendingSlot(int line) const
{
    for (const auto& p : pendingLaunches)
        if (p.line == line)
            return p.slot;
    return -1;
}

bool KeysProcessor::arpLaunchPending(int line) const
{
    for (const auto& p : pendingLaunches)
        if (p.line == line)
            return true;
    return false;
}

// The gesture, with quantize already settled. Called either straight from deferLaunch (off) or
// from the timer at the boundary (on), so there is exactly one description of what a launch
// does and the wait cannot change it.
void KeysProcessor::fireLaunchNow(const PendingLaunch& p)
{
    if (p.slot >= 0)
        launchArpSlotNow(p.slot, p.line);
    else if (p.padSlot >= 0)
        holdArpChordFromPadNow(p.padSlot, p.line);
    else
        holdArpChordNow(p.notes, p.name, p.line);
}

bool KeysProcessor::deferLaunch(PendingLaunch p)
{
    const double wait = arpQuantizeDelayMs();
    if (wait <= 0.0)
    {
        fireLaunchNow(p);
        return false;
    }

    // One pending launch per line. A second click before the boundary *replaces* the first
    // rather than queueing behind it: you changed your mind, and two chords landing on one
    // boundary is not something anybody asked for by clicking twice.
    pendingLaunches.erase(std::remove_if(pendingLaunches.begin(), pendingLaunches.end(),
                                         [&p](const PendingLaunch& q) { return q.line == p.line; }),
                          pendingLaunches.end());
    p.atMs = juce::Time::getMillisecondCounterHiRes() + wait;
    pendingLaunches.push_back(std::move(p));
    if (! isTimerRunning())
        startTimer(1);
    return true;
}

void KeysProcessor::firePendingLaunches(double nowMs)
{
    if (pendingLaunches.empty())
        return;
    // Copied out before firing: a launch moves parameters and fires notes, and anything it
    // touches could in principle come back through deferLaunch and reallocate this vector.
    std::vector<PendingLaunch> due;
    for (auto& p : pendingLaunches)
        if (p.atMs <= nowMs)
            due.push_back(p);
    if (due.empty())
        return;
    pendingLaunches.erase(std::remove_if(pendingLaunches.begin(), pendingLaunches.end(),
                                         [nowMs](const PendingLaunch& q) { return q.atMs <= nowMs; }),
                          pendingLaunches.end());
    for (const auto& p : due)
        fireLaunchNow(p);
}

void KeysProcessor::holdArpChord(const std::vector<int>& notes, const juce::String& name, int line)
{
    if (notes.empty())
    {
        holdArpChordNow(notes, name, line); // an empty hold is a release; never worth waiting for
        return;
    }
    PendingLaunch p;
    p.line = juce::jlimit(0, numArpLines - 1, line);
    p.notes = notes;
    p.name = name;
    deferLaunch(std::move(p));
}

void KeysProcessor::holdArpChordNow(const std::vector<int>& notes, const juce::String& name, int line)
{
    line = juce::jlimit(0, numArpLines - 1, line);
    releaseArpChord(line);
    if (notes.empty())
        return;
    // Exclusive still reaches the pads and the live card from here: handing a card to the arp
    // is a *drop*, and a drop replaces what the line was chewing, so the chord it displaces has
    // to go quiet.
    //
    // **The other direction is no longer symmetric, as of 2026-08-26**, and that asymmetry is
    // the feature rather than an oversight. A press on the strip leaves a running line alone
    // while **Keep arp running** is ticked (LayoutState::padsKeepArpRunning, default on),
    // because pressing a card is playing a chord and a line's held chord is not something you
    // are playing. Untick it and the old both-directions reading comes back.
    //
    // **It does not reach the other arp lines** (2026-08-02, Owen: "I want each arpeggiator to
    // play different chords"). It used to, on the reading that Exclusive means one chord at a
    // time whichever surface started it - which was right while the lines were something you
    // switched between, and is wrong now that they are two instruments you feed side by side.
    // Handing B a chord silently took A's away, so the second drag undid the first and a
    // polyrhythm could not be built at all: the feature and the rule wanted opposite things,
    // and the feature is the reason the lines exist.
    //
    // Nothing collides by allowing it. Each line's chord is fired into that line's own queue
    // (`dest` is line + 1) and `noteRefs` is per destination stream, so two lines holding the
    // same pitch are two independent references and neither release touches the other. This
    // line's own previous hold is already gone: releaseArpChord(line), above, is unconditional.
    if (apvts.getRawParameterValue("chordExclusive")->load() > 0.5f)
        stopAllChordPads(/*includeArpHolds*/ false);
    auto& ln = lines[(size_t) line];
    ln.chordName = name;
    // Fired into this line's own queue, not the track output: only its engine sees it. Note
    // it still goes through fireChord, so the Voices cap applies and the keybed lights up for
    // it exactly as before. The two feel controls do **not**: Humanize's velocity range has
    // skipped a note bound for a line since 2026-08-02, and Strum has skipped one since
    // 2026-08-23 - see fireChord and noteOn for why neither is audible on this path and both
    // cost the engine something.
    ln.chordOn = fireChord(notes, arpChordTagFor(line), line + 1);
    lastChordSource = arpChordTagFor(line);
}

void KeysProcessor::releaseArpChord(int line)
{
    line = juce::jlimit(0, numArpLines - 1, line);
    auto& ln = lines[(size_t) line];
    // No Sustain check, unlike a pad: this chord is held on purpose until something
    // replaces it, so the pedal has nothing to say about when it stops.
    releaseNotes(ln.chordOn, arpChordTagFor(line), line + 1);
    ln.chordName = {};
    ln.launchedSlot = -1;
    ln.padSlot = -1;
}

void KeysProcessor::releaseArpHold(int line)
{
    // What "let go" means for one line, in the order that makes it stick.
    // Anything of this line's still waiting on a quantize boundary goes first: it has not
    // sounded yet, but it is a chord on its way, and letting go means "nothing is coming".
    pendingLaunches.erase(std::remove_if(pendingLaunches.begin(), pendingLaunches.end(),
                                         [line](const PendingLaunch& p) { return p.line == line; }),
                          pendingLaunches.end());
    // The chain next, and for the same reason the heartbeat stops it when the arp goes off:
    // releasing the chord without it only wins until the next bar boundary, when
    // heartbeatTick() launches the following slot and hands the arp another one. stopChain()
    // is a no-op when nothing is chaining, and releases the chord it was holding when it is.
    stopChain(line);
    // Whatever a card or a lone slot launch left behind. Idempotent after stopChain().
    releaseArpChord(line);
}

void KeysProcessor::releaseArpHold()
{
    // Every line, because this is one button and it means "let go". A Hold off that released
    // only the line the panel happened to be showing would leave the other three droning, with
    // nothing on a folded bar to stop them.
    for (int n = 0; n < numArpLines; ++n)
        releaseArpHold(n);
}

void KeysProcessor::allArpOff()
{
    // The switches first, then the letting go. This order is what makes one click enough:
    // with a line still on, releasing its chord only hands the engine back to whatever the
    // keybed is holding, and the run carries on under a button that said Off. Switched off
    // first, the engine is flushed by runArpLines on the very next block (the arpOn edge in
    // there calls flushInto), so nothing is left ringing to release.
    for (int n = 0; n < uiArpLines; ++n)
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(
                apvts.getParameter(arpParamId(n, arpParamSuffix(apOn)))))
        {
            p->beginChangeGesture();
            *p = false;
            p->endChangeGesture();
        }

    // ...and everything a line was holding on to, which the switch alone does not undo: a
    // chord handed over by a card, a chain mid-progression, a launch still waiting out
    // Launch Quantize. This is Hold off's whole job, so it is Hold off that does it.
    releaseArpHold();
}

const std::vector<int>& KeysProcessor::arpHeldNotes(int line) const
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].chordOn;
}

const juce::String& KeysProcessor::arpHeldName(int line) const
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].chordName;
}

bool KeysProcessor::anyArpHold() const
{
    for (int n = 0; n < numArpLines; ++n)
        if (! lines[(size_t) n].chordOn.empty() || lines[(size_t) n].chainOn)
            return true;
    return false;
}

void KeysProcessor::holdArpChordFromPad(int padSlot, int line)
{
    if (padSlot < 0 || padSlot >= numChordPads)
        return;
    if (chordPads[(size_t) padSlot].notes.empty())
        return;
    PendingLaunch p;
    p.line = juce::jlimit(0, numArpLines - 1, line);
    p.padSlot = padSlot;
    deferLaunch(std::move(p));
}

void KeysProcessor::holdArpChordFromPadNow(int padSlot, int line)
{
    if (padSlot < 0 || padSlot >= numChordPads)
        return;
    const auto& pad = chordPads[(size_t) padSlot];
    if (pad.notes.empty())
        return;
    line = juce::jlimit(0, numArpLines - 1, line);
    holdArpChordNow(pad.notes, pad.name, line); // clears this line's previous holder, slot or pad
    lines[(size_t) line].padSlot = padSlot;
}

int KeysProcessor::arpHeldPad(int line) const
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].padSlot;
}

int KeysProcessor::arpLineHoldingPad(int padSlot) const
{
    if (padSlot < 0)
        return -1;
    for (int n = 0; n < numArpLines; ++n)
        if (lines[(size_t) n].padSlot == padSlot)
            return n;
    return -1;
}

void KeysProcessor::launchArpSlot(int index, int line)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    PendingLaunch p;
    p.line = juce::jlimit(0, numArpLines - 1, line);
    p.slot = index;
    deferLaunch(std::move(p));
}

// The whole gesture, and it is a gesture rather than a setting: the pattern, the shape, the
// rate and the chord all land together, which is why Launch Quantize defers this and not just
// the note-ons inside it.
void KeysProcessor::launchArpSlotNow(int index, int line)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    line = juce::jlimit(0, numArpLines - 1, line);

    recallArpPattern(index, line); // snapshots the outgoing slot's lanes first
    const auto& slot = lines[(size_t) line].patterns[(size_t) index];

    // Shape and Rate are ordinary parameters, so a launch has to move them through the
    // host the way the combo boxes do - otherwise automation and the UI disagree about
    // what is playing. Gestures bracket each one; see ArpPanel::applyShapeChoice.
    // They are this *line's* parameters: launching a slot on B must not move A's rate.
    const auto setChoice = [this](const juce::String& id, int value)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id)))
        {
            p->beginChangeGesture();
            *p = value;
            p->endChangeGesture();
        }
    };
    if (slot.shape >= 0)
    {
        if (auto* pat = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(arpParamId(line, apPattern))))
        {
            pat->beginChangeGesture();
            *pat = slot.shape >= ArpEngine::numDirections;
            pat->endChangeGesture();
        }
        if (slot.shape < ArpEngine::numDirections)
            setChoice(arpParamId(line, apDirection), slot.shape); // "Pattern" leaves the direction alone
    }
    if (slot.rate >= 0)
    {
        setChoice(arpParamId(line, apRate), slot.rate);
        // The mode travels with the rate, and both move through the host for the same reason
        // the choice above does. A slot saved before Hz existed reads back rateFree false, so
        // this writes the mode it already had.
        if (auto* free = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(arpParamId(line, apRateFree))))
        {
            free->beginChangeGesture();
            *free = slot.rateFree;
            free->endChangeGesture();
        }
        // Only a slot captured in Hz brings a Hz value with it. `rateHz` is not a value every
        // slot has: arpFromTree synthesises 8.0 for every slot in a session saved before the
        // mode existed, so writing it unconditionally meant opening an old session, dialling
        // 0.5 Hz and clicking any slot silently reset the rate to a fabricated 8 Hz. A Sync
        // slot leaves the Hz control exactly where it found it, which is what the struct
        // comment beside ArpPattern::rateHz already promises.
        if (slot.rateFree)
            if (auto* hz = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(arpParamId(line, apRateHz))))
            {
                hz->beginChangeGesture();
                *hz = slot.rateHz;
                hz->endChangeGesture();
            }
    }

    // Hold last, so the chord starts against the pattern the slot just installed. The *Now
    // path, because this launch has already served whatever wait Launch Quantize asked for -
    // deferring again here would make a quantized slot land a whole division late.
    if (! slot.chordNotes.empty())
        holdArpChordNow(slot.chordNotes, slot.chordName, line);
    else
        releaseArpChord(line); // a pattern-only slot arpeggiates whatever you are already holding
    lines[(size_t) line].launchedSlot = index;
}

void KeysProcessor::stopArpSlot(int line)
{
    releaseArpChord(line); // clears launchedSlot too
}

int KeysProcessor::arpLaunchedSlot(int line) const
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].launchedSlot;
}

int KeysProcessor::nextChainSlot(int from, int line) const
{
    // The chain plays the slots that hold a chord. A pattern-only slot is a place to keep a
    // rhythm, not a step of a progression, and walking through one would leave the previous
    // chord ringing under a pattern that says nothing about it.
    const auto& pats = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].patterns;
    for (int i = 1; i <= numArpPatterns; ++i)
    {
        const int idx = (from + i) % numArpPatterns;
        if (! pats[(size_t) idx].chordNotes.empty())
            return idx;
    }
    return from >= 0 && ! pats[(size_t) from].chordNotes.empty() ? from : -1;
}

void KeysProcessor::startChain(int line)
{
    line = juce::jlimit(0, numArpLines - 1, line);
    const int first = nextChainSlot(numArpPatterns - 1, line); // wraps to the lowest filled slot
    if (first < 0)
        return; // nothing to play: the button does not stick on for an empty row
    auto& ln = lines[(size_t) line];
    ln.chainOn = true;
    ln.chainIndex = first;
    launchArpSlotNow(first, line); // the chain owns its own timing; see heartbeatTick
    ln.chainTargetBeats.store(4.0 * juce::jmax(1, ln.patterns[(size_t) first].bars));
    ln.chainEpoch.fetch_add(1); // tells the audio thread to count this slot from zero
    // Clear the advance flag *before* arming the clock. stopChain() clears it too, but the
    // audio thread can raise it once more in the block that straddles the stop, and nothing
    // consumes it while chainOn is false - so a stale true survived into the next chain and
    // stepped it to its second slot on the first heartbeat. Ordered last but one so there is
    // no window where chainActive is set and the flag is still whatever it was.
    ln.chainAdvance.store(false);
    ln.chainActive.store(true);
}

void KeysProcessor::stopChain(int line)
{
    line = juce::jlimit(0, numArpLines - 1, line);
    auto& ln = lines[(size_t) line];
    if (! ln.chainOn)
        return;
    ln.chainOn = false;
    ln.chainIndex = -1;
    ln.chainActive.store(false);
    ln.chainAdvance.store(false);
    stopArpSlot(line); // the chord the chain was holding is the chain's to release
}

bool KeysProcessor::chainRunning(int line) const
{
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].chainOn;
}

int KeysProcessor::chainSlot(int line) const
{
    const auto& ln = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)];
    return ln.chainOn ? ln.chainIndex : -1;
}

void KeysProcessor::setArpSlotBars(int index, int bars, int line)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    auto& ln = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)];
    ln.patterns[(size_t) index].bars = juce::jlimit(1, 16, bars);
    if (ln.chainOn && index == ln.chainIndex)
        ln.chainTargetBeats.store(4.0 * ln.patterns[(size_t) index].bars); // takes effect now
}

int KeysProcessor::arpSlotBars(int index, int line) const
{
    if (index < 0 || index >= numArpPatterns)
        return 1;
    return juce::jlimit(1, 16, lines[(size_t) juce::jlimit(0, numArpLines - 1, line)]
                                   .patterns[(size_t) index].bars);
}

void KeysProcessor::heartbeatTick()
{
    // The generator's key follows through to the keyboard's here rather than inside
    // parameterChanged: that callback can arrive on the audio thread from host automation, and
    // writing another parameter from there is not something to do mid-block. 20 ms late is
    // invisible for a control that only greys keys.
    mirrorGenKeyToScale();

    // Move the take out of the audio thread's ring while it is still being played into, so the
    // ring never has to hold a whole performance and the chip's duration is live rather than
    // arriving all at once when recording stops.
    if (take.isRecording())
        take.drainCapture();

    // Releasing a chord held into the arp when the arp goes off. This lived in the editor's
    // timer and was gated on the chord having come from a *pad*, so a chord handed over from
    // the live card was never released - and with no editor open nothing polled at all, so
    // automation or an MCP client writing arpOn false left it sounding with no way to stop
    // it but All Off. Both edges close here: the processor owns the chord, so the processor
    // is what should notice. Per line, because each line has a switch of its own.
    for (int n = 0; n < numArpLines; ++n)
    {
        auto& ln = lines[(size_t) n];
        const bool arpOn = arpLineOn(n);
        if (! arpOn && ln.lastOnHeartbeat)
        {
            // The chain goes first: a progression cycling with nothing arpeggiating it is the
            // same drone-with-no-owner this whole check exists to prevent, and its Chain button
            // is on the arp panel, so switching the arp off is switching the chain off.
            stopChain(n);
            // What is left is a chord a *card* handed over - a pad, or the live card, which is
            // the case the editor's version of this missed entirely. A chord an arp *slot*
            // launched is left alone on purpose: the lit card is still on screen and still
            // releases it on a click, so it has an owner and this does not have to be one.
            if (ln.launchedSlot < 0 && ! ln.chordOn.empty())
                releaseArpChord(n);
        }
        ln.lastOnHeartbeat = arpOn;

        if (ln.chainOn && ln.chainAdvance.exchange(false))
        {
            const int next = nextChainSlot(ln.chainIndex, n);
            if (next < 0)
            {
                stopChain(n); // every chord was cleared out from under it
                continue;
            }
            ln.chainIndex = next;
            launchArpSlotNow(next, n); // already on a bar boundary; quantizing it again would drift
            ln.chainTargetBeats.store(4.0 * juce::jmax(1, ln.patterns[(size_t) next].bars));
            ln.chainEpoch.fetch_add(1);
        }
    }
}

// Message thread. The Chord lane reads slot chords on the audio thread, so they live in a
// mirror of atomics; this rebuilds the whole mirror for one line, which is twelve chords of
// eight notes and not worth being clever about. Every path that can change a slot's chord
// ends here, and now has to name the line whose slots it changed - miss one and that line's
// Chord lane plays a stale chord.
void KeysProcessor::syncArpChordTable(int line)
{
    static_assert(ArpEngine::ChordTable::numSlots == numArpPatterns,
                  "the Chord lane addresses slots one for one");
    auto& ln = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)];
    for (int s = 0; s < ArpEngine::ChordTable::numSlots; ++s)
    {
        const auto& notes = ln.patterns[(size_t) s].chordNotes;
        const int n = juce::jlimit(0, ArpEngine::ChordTable::maxNotes, (int) notes.size());
        for (int i = 0; i < ArpEngine::ChordTable::maxNotes; ++i)
            ln.chordTable.note[(size_t) s][(size_t) i].store(i < n ? notes[(size_t) i] : 0,
                                                             std::memory_order_relaxed);
        // Count last, and it is what the engine gates on, so a half-written chord is never
        // reachable: the notes are in place before the count that admits them.
        ln.chordTable.count[(size_t) s].store(n, std::memory_order_release);
    }
}

void KeysProcessor::setArpSlotChord(int index, const std::vector<int>& notes, const juce::String& name, int line)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    line = juce::jlimit(0, numArpLines - 1, line);
    auto& ln = lines[(size_t) line];
    ln.patterns[(size_t) index].chordNotes = notes;
    ln.patterns[(size_t) index].chordName = name;
    syncArpChordTable(line);
    // Capture the shape and rate that are up right now, so launching the slot brings the
    // whole sound back and the card can say what it will play. Nothing else ever wrote
    // these, so every slot painted "--" and a launch left Shape and Rate alone.
    const bool usePattern = arpParam(line, apPattern) > 0.5f;
    ln.patterns[(size_t) index].shape = usePattern
                                            ? ArpEngine::numDirections
                                            : (int) arpParam(line, apDirection);
    ln.patterns[(size_t) index].rate = (int) arpParam(line, apRate);
    // ...and the mode it is in, so a slot captured in Hz launches in Hz.
    ln.patterns[(size_t) index].rateFree = arpParam(line, apRateFree) > 0.5f;
    ln.patterns[(size_t) index].rateHz = arpParam(line, apRateHz);
}

void KeysProcessor::clearArpSlotChord(int index, int line)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    line = juce::jlimit(0, numArpLines - 1, line);
    auto& ln = lines[(size_t) line];
    ln.patterns[(size_t) index].chordNotes.clear();
    ln.patterns[(size_t) index].chordName = {};
    syncArpChordTable(line);
    if (ln.launchedSlot == index)
        releaseArpChord(line);
}

void KeysProcessor::copyArpPattern(int from, int to, int line)
{
    if (from < 0 || from >= numArpPatterns || to < 0 || to >= numArpPatterns || from == to)
        return;
    line = juce::jlimit(0, numArpLines - 1, line);
    storeActiveArpPattern(line);
    auto& ln = lines[(size_t) line];
    ln.patterns[(size_t) to] = ln.patterns[(size_t) from];
    syncArpChordTable(line);
    if (to == ln.activePattern)
        recallArpPattern(to, line);
}

const KeysProcessor::ArpPattern& KeysProcessor::arpPatternSlot(int index, int line) const
{
    static const ArpPattern empty {};
    if (index < 0 || index >= numArpPatterns)
        return empty;
    return lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].patterns[(size_t) index];
}

void KeysProcessor::setArpPatternSlot(int index, const ArpPattern& pattern, int line)
{
    if (index < 0 || index >= numArpPatterns)
        return;
    line = juce::jlimit(0, numArpLines - 1, line);
    auto& ln = lines[(size_t) line];
    ln.patterns[(size_t) index] = pattern;
    syncArpChordTable(line);
    if (index != ln.activePattern)
        return;
    // Refresh the live lanes from the slot just written. Not recallArpPattern(index):
    // that snapshots the live lanes into patterns[activePattern] first, which
    // would clobber the pattern we just wrote before reading it back.
    for (int l = 0; l < ArpEngine::numLanes; ++l)
    {
        for (int s = 0; s < ArpEngine::maxSteps; ++s)
            ln.engine.lanes.value[(size_t) l][(size_t) s].store(pattern.value[(size_t) l][(size_t) s]);
        ln.engine.lanes.length[(size_t) l].store(juce::jlimit(1, ArpEngine::maxSteps, pattern.length[(size_t) l]));
        ln.engine.lanes.clockDiv[(size_t) l].store(juce::jlimit(0, 2, pattern.clockDiv[(size_t) l]));
        ln.engine.lanes.on[(size_t) l].store(pattern.on[(size_t) l] != 0 ? 1 : 0);
        ln.engine.lanes.loopFrom[(size_t) l].store(juce::jlimit(0, ArpEngine::maxSteps - 1, pattern.loopFrom[(size_t) l]));
        ln.engine.lanes.loopTo[(size_t) l].store(juce::jlimit(0, ArpEngine::maxSteps - 1, pattern.loopTo[(size_t) l]));
        ln.engine.lanes.dir[(size_t) l].store(juce::jlimit(0, (int) ArpEngine::numLaneDirs - 1, pattern.dir[(size_t) l]));
    }
    for (int i = 0; i < 4; ++i)
        ln.engine.rhythmDiv[(size_t) i].store(juce::jlimit(0, 16, pattern.rhythmDivs[(size_t) i]));
    ln.engine.harmonyMode.store(juce::jlimit(0, 1, pattern.harmonyMode));
}

void KeysProcessor::randomizeActiveArpPattern(int line)
{
    // Musical randomize, not white noise: mostly direction-following steps, gentle
    // octave jumps, occasional ratchets and rests.
    auto& lanes = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].engine.lanes;
    for (int s = 0; s < ArpEngine::maxSteps; ++s)
    {
        lanes.value[ArpEngine::laneNote][(size_t) s].store(rng.nextInt(10) == 0 ? -1 : 0);
        lanes.value[ArpEngine::laneOctave][(size_t) s].store(rng.nextInt(5) == 0 ? rng.nextInt(3) - 1 : 0);
        lanes.value[ArpEngine::laneVelocity][(size_t) s].store(70 + rng.nextInt(60));
        lanes.value[ArpEngine::laneGate][(size_t) s].store(40 + rng.nextInt(80));
        lanes.value[ArpEngine::laneRatchet][(size_t) s].store(rng.nextInt(8) == 0 ? 2 : 1);
        lanes.value[ArpEngine::laneProbability][(size_t) s].store(rng.nextInt(6) == 0 ? 60 : 100);
    }
}

void KeysProcessor::rerollArpLane(int line, int laneIndex, int amountPct, int fromStep, int toStep)
{
    // Reroll **one** lane, by an amount (2026-08-14, Owen: "there should be, like, a more
    // random feature in the drawing, like cthulu"). randomizeActiveArpPattern above is the
    // other kind and stays: it writes six lanes at once to a musical recipe, which is a way to
    // get a whole part you did not have. This is the one you reach for while looking at a lane
    // you already like - so it is scoped to that lane, and `amountPct` is how far it may stray
    // from what is drawn rather than an all-or-nothing reroll.
    //
    // At 100 the draw is uniform across the lane's whole range, which is the scramble; below
    // that it is a nudge around each step's current value. Either way it only ever writes
    // inside laneRange, so no reroll can put a value in a lane that the lane cannot hold.
    const auto lane = juce::jlimit(0, ArpEngine::numLanes - 1, laneIndex);
    const auto range = ArpEngine::laneRange(lane);
    const int span = range.hi - range.lo;
    if (span <= 0)
        return;
    const double amt = juce::jlimit(0, 100, amountPct) / 100.0;
    auto& lanes = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].engine.lanes;
    // Its own length, not maxSteps: rerolling past the end would quietly rewrite steps the
    // pattern is not playing, and they would appear later if the length ever grew.
    const int len = juce::jlimit(1, ArpEngine::maxSteps, lanes.length[(size_t) lane].load());
    // A span narrows it; -1 on either end means the whole lane. Clamped into the lane's own
    // length, so a span marked before the length shrank cannot write past the end.
    const int lo = fromStep < 0 ? 0 : juce::jlimit(0, len - 1, fromStep);
    const int hi = toStep < 0 ? len - 1 : juce::jlimit(lo, len - 1, toStep);
    for (int s = lo; s <= hi; ++s)
    {
        const int cur = lanes.value[(size_t) lane][(size_t) s].load();
        // The window slides rather than the result clamping - see ArpEngine::strayWithin for
        // why, and for the bug that taught it. At 100% the reach is the whole lane, so the
        // window covers it wherever the value sits and the draw is uniform across it.
        lanes.value[(size_t) lane][(size_t) s].store(
            ArpEngine::strayWithin(cur, span * amt, range, rng.nextDouble()));
    }
}

void KeysProcessor::resetArpLane(int line, int laneIndex, int fromStep, int toStep)
{
    // Its whole length, not maxSteps, for the reason rerollArpLane gives: the steps past the
    // end are not being played, and rewriting them would surface later if the length grew.
    const auto lane = juce::jlimit(0, ArpEngine::numLanes - 1, laneIndex);
    auto& lanes = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].engine.lanes;
    const int len = juce::jlimit(1, ArpEngine::maxSteps, lanes.length[(size_t) lane].load());
    const int lo = fromStep < 0 ? 0 : juce::jlimit(0, len - 1, fromStep);
    const int hi = toStep < 0 ? len - 1 : juce::jlimit(lo, len - 1, toStep);
    for (int s = lo; s <= hi; ++s)
        lanes.value[(size_t) lane][(size_t) s].store(ArpEngine::laneDefaults[lane]);
}

bool KeysProcessor::applyEuclidToActiveArpPattern(int line, int hits, int steps, int rotation, int laneIndex)
{
    // Only the probability lane has a meaningful hit/rest mapping (100 fires the step as
    // written, 0 never does); any other lane's "on" value depends on what that lane means, so
    // this stays scoped to probability rather than guessing at one.
    if (laneIndex != ArpEngine::laneProbability)
        return false;
    steps = juce::jlimit(1, ArpEngine::maxSteps, steps);
    auto& lanes = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)].engine.lanes;
    for (int s = 0; s < steps; ++s)
        lanes.value[ArpEngine::laneProbability][(size_t) s].store(keys::euclidHit(s, hits, steps, rotation) ? 100 : 0);
    lanes.length[ArpEngine::laneProbability].store(steps);
    return true;
}

void KeysProcessor::restoreSharedState(const juce::ValueTree& root)
{
    // Everything both products restore from a saved session, in one place. Keys Host
    // overrides setStateInformation to add its hosted instrument, and used to repeat this
    // list by hand: the strum migration below was written on 2026-07-27, worked in Keys, and
    // silently did nothing in Keys Host until this became one function. Anything session
    // shaped belongs here, not in either override.
    migrateStrumRange(root);
    migrateRateMode(root);
    migrateVelTrim(root);
    migrateVelLevel(root);
    migrateBpmSync(root);
    migrateTuplet(root);
    migrateHumanSpans(root);
    migrateStray(root);
    chordPadsFromTree(root);
    arpFromTree(root);
    layoutFromTree(root);

    // **A restore is not a move, so the Key/Mode mirror must not fire off one.** replaceState
    // pushes genRoot and genMode through parameterChanged like any other write, which raised
    // the pending flag and had the next heartbeat mirror them onto `root` and `scale` - over
    // the values this very session had just restored. Root and Scale are still ordinary
    // parameters you can set on the Controls bar after choosing a generator key, so that
    // silently threw away a saved keybed setting on every single load, and pushed two
    // gesture-less parameter writes at the host ~20 ms in for good measure. The mirror is
    // for the user turning the generator's Key or Mode; dropped here, it stays that.
    pendingGenKeyMirror.set(0);
}

// The eight session migrations are keysparams' (src/KeysParams.h has the shape they all share:
// an absent parameter is not a reset, so the tell is the absence of an id in the saved tree and
// the repair has to be written explicitly). They are free functions over the APVTS and the saved
// tree, because that is all any of them ever touched; these forwarders keep restoreSharedState
// reading as the list of things a session restores, which is what it is for.
void KeysProcessor::migrateStrumRange(const juce::ValueTree& root) { keysparams::migrateStrumRange(apvts, root); }
void KeysProcessor::migrateRateMode(const juce::ValueTree& root) { keysparams::migrateRateMode(apvts, root); }
void KeysProcessor::migrateVelTrim(const juce::ValueTree& root) { keysparams::migrateVelTrim(apvts, root); }
void KeysProcessor::migrateVelLevel(const juce::ValueTree& root) { keysparams::migrateVelLevel(apvts, root); }
void KeysProcessor::migrateBpmSync(const juce::ValueTree& root) { keysparams::migrateBpmSync(apvts, root); }
void KeysProcessor::migrateTuplet(const juce::ValueTree& root) { keysparams::migrateTuplet(apvts, root); }
void KeysProcessor::migrateHumanSpans(const juce::ValueTree& root) { keysparams::migrateHumanSpans(apvts, root); }
void KeysProcessor::migrateStray(const juce::ValueTree& root) { keysparams::migrateStray(apvts, root); }

// The layout tree, one line each: the property names, the defaults and the absent-means-this
// reasoning all live in src/LayoutState.h now, next to the struct they describe.
juce::ValueTree KeysProcessor::layoutToTree() const
{
    return layoutstate::toTree(layout);
}

void KeysProcessor::layoutFromTree(const juce::ValueTree& root)
{
    layoutstate::fromTree(layout, root, numArpLines);
}

void KeysProcessor::arpLineToTree(juce::ValueTree& dest, int line) const
{
    const auto& ln = lines[(size_t) juce::jlimit(0, numArpLines - 1, line)];
    dest.setProperty("active", ln.activePattern, nullptr);
    for (int pIndex = 0; pIndex < numArpPatterns; ++pIndex)
    {
        const auto& pat = ln.patterns[(size_t) pIndex];
        juce::ValueTree pt { "pattern" };
        pt.setProperty("index", pIndex, nullptr);
        // The chord a slot launches, alongside its lanes. Absent in sessions from before
        // the slots carried one, which reads back as a pattern-only slot.
        if (! pat.chordNotes.empty())
        {
            juce::StringArray notes;
            for (int n : pat.chordNotes)
                notes.add(juce::String(n));
            pt.setProperty("chord", notes.joinIntoString(","), nullptr);
            pt.setProperty("chordName", pat.chordName, nullptr);
        }
        pt.setProperty("shape", pat.shape, nullptr);
        pt.setProperty("rate", pat.rate, nullptr);
        // The rate's mode, written alongside it. Absent in every session saved before Hz
        // existed, which reads back as Sync at the `rate` index above - what those sessions
        // actually played.
        pt.setProperty("rateFree", pat.rateFree, nullptr);
        pt.setProperty("rateHz", (double) pat.rateHz, nullptr);
        pt.setProperty("bars", pat.bars, nullptr); // how long the chain holds this slot
        // Rhythm dividers and the Harmony lane's mode, appended 2026-08-14. Absent in every
        // session saved before them, which reads back as "0,0,0,0" / 0 - inert, exactly what
        // those sessions already played.
        juce::StringArray rd;
        for (int v : pat.rhythmDivs)
            rd.add(juce::String(v));
        pt.setProperty("rhythmDivs", rd.joinIntoString(","), nullptr);
        pt.setProperty("harmonyMode", pat.harmonyMode, nullptr);
        for (int l = 0; l < ArpEngine::numLanes; ++l)
        {
            juce::StringArray vals;
            for (int s = 0; s < ArpEngine::maxSteps; ++s)
                vals.add(juce::String(pat.value[(size_t) l][(size_t) s]));
            juce::ValueTree lt { "lane" };
            lt.setProperty("index", l, nullptr);
            lt.setProperty("values", vals.joinIntoString(","), nullptr);
            lt.setProperty("length", pat.length[(size_t) l], nullptr);
            lt.setProperty("clockDiv", pat.clockDiv[(size_t) l], nullptr);
            // Appended 2026-08-18. Every one of these reads back as what the engine did before
            // it existed when the property is absent, which is what lets a session written by
            // any older build open here with no migration at all.
            lt.setProperty("on", pat.on[(size_t) l], nullptr);
            lt.setProperty("loopFrom", pat.loopFrom[(size_t) l], nullptr);
            lt.setProperty("loopTo", pat.loopTo[(size_t) l], nullptr);
            lt.setProperty("dir", pat.dir[(size_t) l], nullptr);
            pt.appendChild(lt, nullptr);
        }
        dest.appendChild(pt, nullptr);
    }
}

juce::ValueTree KeysProcessor::arpToTree() const
{
    // The live lanes are the active pattern; snapshot them so the tree is current. All three,
    // since all three have live lanes of their own.
    for (int n = 0; n < numArpLines; ++n)
        const_cast<KeysProcessor*>(this)->storeActiveArpPattern(n);

    juce::ValueTree tree { "arp" };
    // A slot's `shape` is a direction index, with numDirections itself meaning "Pattern" -
    // so the number that means Pattern moves every time a shape is added. Writing it down is
    // what lets a session saved when there were eight shapes still open on twelve; without
    // it, the four shapes added on 2026-07-30 silently turned every Pattern slot into Random.
    // One copy for the whole tree: it is a property of the build that wrote it, not of a line.
    tree.setProperty("shapeBase", ArpEngine::numDirections, nullptr);

    // Line 0's slots sit directly on this node, in exactly the shape and place they have
    // always occupied, so a session written here still loads into a build that predates the
    // lines - and, more to the point, every session written *by* those builds loads here with
    // no migration at all. B and C hang off "line" children, which an older build ignores
    // because it never asks for them.
    arpLineToTree(tree, 0);
    for (int n = 1; n < numArpLines; ++n)
    {
        juce::ValueTree lt { "line" };
        lt.setProperty("index", n, nullptr);
        arpLineToTree(lt, n);
        tree.appendChild(lt, nullptr);
    }
    return tree;
}

void KeysProcessor::arpLineFromTree(const juce::ValueTree& src, int line, int savedShapeBase)
{
    line = juce::jlimit(0, numArpLines - 1, line);
    auto& ln = lines[(size_t) line];
    for (int c = 0; c < src.getNumChildren(); ++c)
    {
        const auto pt = src.getChild(c);
        if (! pt.hasType("pattern"))
            continue; // a "line" child of the root; that line reads itself
        const int pIndex = (int) pt.getProperty("index", -1);
        if (pIndex < 0 || pIndex >= numArpPatterns)
            continue;
        auto& pat = ln.patterns[(size_t) pIndex];
        pat.chordNotes.clear();
        for (const auto& n : juce::StringArray::fromTokens(pt.getProperty("chord").toString(), ",", ""))
            if (n.isNotEmpty())
                pat.chordNotes.push_back(juce::jlimit(0, 127, n.getIntValue()));
        pat.chordName = pt.getProperty("chordName").toString();
        pat.shape = (int) pt.getProperty("shape", -1);
        if (pat.shape == savedShapeBase)
            pat.shape = ArpEngine::numDirections; // "Pattern" is wherever Pattern is now
        pat.shape = juce::jlimit(-1, ArpEngine::numDirections, pat.shape);
        pat.rate = (int) pt.getProperty("rate", -1);
        // Both absent before the Hz mode: false and the default 8 Hz, so an old session's
        // slot launches the division it stored, exactly as it always did.
        pat.rateFree = (bool) pt.getProperty("rateFree", false);
        pat.rateHz = (float) juce::jlimit((double) ArpEngine::minRateHz, (double) ArpEngine::maxRateHz,
                                          (double) pt.getProperty("rateHz", 8.0));
        pat.bars = juce::jlimit(1, 16, (int) pt.getProperty("bars", 1)); // absent before the chain
        const auto rd = juce::StringArray::fromTokens(pt.getProperty("rhythmDivs", "0,0,0,0").toString(), ",", "");
        for (int i = 0; i < 4; ++i)
            pat.rhythmDivs[(size_t) i] = i < rd.size() ? juce::jlimit(0, 16, rd[i].getIntValue()) : 0;
        pat.harmonyMode = juce::jlimit(0, 1, (int) pt.getProperty("harmonyMode", 0));
        for (int lc = 0; lc < pt.getNumChildren(); ++lc)
        {
            const auto lt = pt.getChild(lc);
            const int l = (int) lt.getProperty("index", -1);
            if (l < 0 || l >= ArpEngine::numLanes)
                continue;
            const auto vals = juce::StringArray::fromTokens(lt.getProperty("values").toString(), ",", "");
            for (int s = 0; s < ArpEngine::maxSteps && s < vals.size(); ++s)
                pat.value[(size_t) l][(size_t) s] = vals[s].getIntValue();
            pat.length[(size_t) l] = (int) lt.getProperty("length", 8);
            pat.clockDiv[(size_t) l] = (int) lt.getProperty("clockDiv", 0);
            pat.on[(size_t) l] = (int) lt.getProperty("on", 1);
            pat.loopFrom[(size_t) l] = (int) lt.getProperty("loopFrom", 0);
            pat.loopTo[(size_t) l] = (int) lt.getProperty("loopTo", ArpEngine::maxSteps - 1);
            pat.dir[(size_t) l] = (int) lt.getProperty("dir", ArpEngine::dirUp);
        }
    }
    syncArpChordTable(line); // the Chord lane's view of the slots, after a whole session lands
    // Recall the active pattern by hand (recallArpPattern would first snapshot the
    // live lanes over the data we just loaded).
    ln.activePattern = juce::jlimit(0, numArpPatterns - 1, (int) src.getProperty("active", 0));
    const auto& pat = ln.patterns[(size_t) ln.activePattern];
    for (int l = 0; l < ArpEngine::numLanes; ++l)
    {
        for (int s = 0; s < ArpEngine::maxSteps; ++s)
            ln.engine.lanes.value[(size_t) l][(size_t) s].store(pat.value[(size_t) l][(size_t) s]);
        ln.engine.lanes.length[(size_t) l].store(juce::jlimit(1, ArpEngine::maxSteps, pat.length[(size_t) l]));
        ln.engine.lanes.clockDiv[(size_t) l].store(juce::jlimit(0, 2, pat.clockDiv[(size_t) l]));
        ln.engine.lanes.on[(size_t) l].store(pat.on[(size_t) l] != 0 ? 1 : 0);
        ln.engine.lanes.loopFrom[(size_t) l].store(juce::jlimit(0, ArpEngine::maxSteps - 1, pat.loopFrom[(size_t) l]));
        ln.engine.lanes.loopTo[(size_t) l].store(juce::jlimit(0, ArpEngine::maxSteps - 1, pat.loopTo[(size_t) l]));
        ln.engine.lanes.dir[(size_t) l].store(juce::jlimit(0, (int) ArpEngine::numLaneDirs - 1, pat.dir[(size_t) l]));
    }
    for (int i = 0; i < 4; ++i)
        ln.engine.rhythmDiv[(size_t) i].store(pat.rhythmDivs[(size_t) i]);
    ln.engine.harmonyMode.store(pat.harmonyMode);
}

void KeysProcessor::arpFromTree(const juce::ValueTree& root)
{
    const auto tree = root.getChildWithName("arp");
    if (! tree.isValid())
        return; // sessions from before the arp: defaults stand
    // What "Pattern" was numbered when this session was written. Absent means it predates
    // the four shapes added on 2026-07-30, when there were eight directions and Pattern was
    // eight; every save since says so itself.
    const int savedShapeBase = juce::jlimit(1, ArpEngine::numDirections,
                                            (int) tree.getProperty("shapeBase", 8));

    // Line 0 off the root, where it has always been. This is the whole of the migration: a
    // session saved before the lines existed has no "line" children at all, so B and C keep
    // their defaults - twelve empty slots and lanes that have never been drawn - which with
    // both switched off is precisely the arpeggiator that session was saved from.
    arpLineFromTree(tree, 0, savedShapeBase);
    for (int c = 0; c < tree.getNumChildren(); ++c)
    {
        const auto lt = tree.getChild(c);
        if (! lt.hasType("line"))
            continue;
        const int n = (int) lt.getProperty("index", -1);
        if (n >= 1 && n < numArpLines)
            arpLineFromTree(lt, n, savedShapeBase);
    }
}

void KeysProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    okstudio::state::save(apvts, "KEYS", destData, { chordPadsToTree(), arpToTree(), layoutToTree() });
}

void KeysProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    okstudio::state::load(apvts, data, sizeInBytes, [this](const juce::ValueTree& root)
    {
        restoreSharedState(root);
    });
}

juce::AudioProcessorEditor* KeysProcessor::createEditor()
{
    return new KeysEditor(*this);
}
} // namespace keys

// The Keys Host target compiles these same sources with KEYS_HOST=1 and provides its
// own createPluginFilter (returning KeysHostProcessor) in src/host.
#if ! (defined(KEYS_HOST) && KEYS_HOST)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new keys::KeysProcessor();
}
#endif
