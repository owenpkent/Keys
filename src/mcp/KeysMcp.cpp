#include "KeysMcp.h"
#include "../PluginProcessor.h"
#include <algorithm>
#include <array>
#include <utility>

namespace keys
{
namespace
{
    const char* laneNames[ArpEngine::numLanes] = { "note", "octave", "velocity", "gate", "ratchet", "probability" };

    // Legal per-lane range, straight from ArpEngine.h's Lanes comment: note is
    // -1 (mute) .. 8 (fixed chord-note index, 0 = follow direction mode); octave is
    // added octaves; velocity/gate are percentages; ratchet is sub-hits; probability
    // is a percent chance.
    struct LaneRange
    {
        int lane;
        int lo, hi;
    };
    const LaneRange laneRanges[ArpEngine::numLanes] = {
        { ArpEngine::laneNote, -1, 8 },
        { ArpEngine::laneOctave, -3, 3 },
        { ArpEngine::laneVelocity, 10, 200 },
        { ArpEngine::laneGate, 5, 200 },
        { ArpEngine::laneRatchet, 1, 4 },
        { ArpEngine::laneProbability, 0, 100 },
    };

    juce::var intVectorToVar(const std::vector<int>& values)
    {
        juce::Array<juce::var> arr;
        for (int v : values)
            arr.add(v);
        return juce::var(arr);
    }

    // Parses a juce::var array of numbers into ints; returns false (leaving out alone)
    // if the var isn't an array or holds anything that isn't a number.
    bool readIntArray(const juce::var& v, std::vector<int>& out)
    {
        const auto* arr = v.getArray();
        if (arr == nullptr)
            return false;
        out.clear();
        out.reserve((size_t) arr->size());
        for (const auto& e : *arr)
        {
            if (! (e.isInt() || e.isInt64() || e.isDouble()))
                return false;
            out.push_back((int) e);
        }
        return true;
    }

    // Fills a result object with the six per-step lanes (trimmed to each lane's
    // length), plus "lengths" and "clockDivs" sub-objects. Shared by get_arp_pattern's
    // live-lane and stored-slot branches.
    void writeArpPatternInto(juce::DynamicObject& obj,
                             const std::array<std::array<int, ArpEngine::maxSteps>, ArpEngine::numLanes>& values,
                             const std::array<int, ArpEngine::numLanes>& lengths,
                             const std::array<int, ArpEngine::numLanes>& clockDivs)
    {
        auto* lengthsObj = new juce::DynamicObject();
        auto* clockDivsObj = new juce::DynamicObject();
        for (int l = 0; l < ArpEngine::numLanes; ++l)
        {
            const int len = juce::jlimit(1, ArpEngine::maxSteps, lengths[(size_t) l]);
            std::vector<int> laneVals(values[(size_t) l].begin(), values[(size_t) l].begin() + len);
            obj.setProperty(laneNames[l], intVectorToVar(laneVals));
            lengthsObj->setProperty(laneNames[l], len);
            clockDivsObj->setProperty(laneNames[l], clockDivs[(size_t) l]);
        }
        obj.setProperty("lengths", juce::var(lengthsObj));
        obj.setProperty("clockDivs", juce::var(clockDivsObj));
    }
} // namespace

KeysMcp::KeysMcp(KeysProcessor& p)
    : processor(p), server(productSlug(), JucePlugin_VersionString)
{
    server.addTool(toolGetState());
    server.addTool(toolListParams());
    server.addTool(toolSetParams());
    server.addTool(toolPlayNotes());
    server.addTool(toolPlaySequence());
    server.addTool(toolAllNotesOff());
    server.addTool(toolGetChordPads());
    server.addTool(toolSetChordPad());
    server.addTool(toolClearChordPad());
    server.addTool(toolPressChordPad());
    server.addTool(toolReleaseChordPad());
    server.addTool(toolGetArpPattern());
    server.addTool(toolSetArpPattern());
    server.addTool(toolRecallArpPattern());
    server.addTool(toolStoreArpPattern());
    server.start();
    startTimer(30);
}

KeysMcp::~KeysMcp()
{
    server.stop();  // message thread; no request can be mid-flight into `processor` after this
    stopTimer();
    pendingReleases.clear();
}

juce::String KeysMcp::productSlug()
{
   #if defined(KEYS_HEX) && KEYS_HEX
    return "hex-host";
   #elif defined(KEYS_HOST) && KEYS_HOST
    return "keys-host";
   #elif defined(KEYS_MIDI_EFFECT) && KEYS_MIDI_EFFECT
    return "keys-fx";
   #else
    return "keys";
   #endif
}

void KeysMcp::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (int i = (int) pendingReleases.size() - 1; i >= 0; --i)
    {
        if (pendingReleases[(size_t) i].releaseAtMs <= now)
        {
            const int slot = pendingReleases[(size_t) i].slot;
            pendingReleases.erase(pendingReleases.begin() + i);
            processor.releaseChordPad(slot);
        }
    }
}

void KeysMcp::cancelPendingRelease(int slot)
{
    pendingReleases.erase(std::remove_if(pendingReleases.begin(), pendingReleases.end(),
                                          [slot](const PendingRelease& r) { return r.slot == slot; }),
                          pendingReleases.end());
}

okstudio::mcp::Tool KeysMcp::toolGetState()
{
    okstudio::mcp::Tool t;
    t.name = "get_state";
    t.description = "Snapshot of Keys' current performance state: product and version, "
                     "the load-bearing controls (root, scale, scale lock, octave, sustain, "
                     "latch, velocity, arp on/rate/direction/octaves/latch, which arp "
                     "pattern is active, which chord-pad page is showing, and the UI "
                     "layout), plus how many chord pads currently hold a chord. Call this "
                     "first to orient before changing anything.";
    t.run = [this](const juce::var&, juce::String&) -> juce::var
    {
        auto text = [this](const char* id) { return processor.apvts.getParameter(id)->getCurrentValueAsText(); };
        auto* obj = new juce::DynamicObject();
        obj->setProperty("product", productSlug());
        obj->setProperty("version", JucePlugin_VersionString);
        obj->setProperty("root", text("root"));
        obj->setProperty("scale", text("scale"));
        obj->setProperty("scaleLock", text("scaleLock"));
        obj->setProperty("octave", text("octave"));
        obj->setProperty("sustain", text("sustain"));
        obj->setProperty("latch", text("latch"));
        obj->setProperty("velocity", text("velocity"));
        obj->setProperty("arpOn", text("arpOn"));
        obj->setProperty("arpRate", text("arpRate"));
        obj->setProperty("arpDirection", text("arpDirection"));
        obj->setProperty("arpOctaves", text("arpOctaves"));
        obj->setProperty("arpLatch", text("arpLatch"));
        obj->setProperty("activeArpPattern", processor.arpActivePattern());
        obj->setProperty("padPage", processor.padPage());
        obj->setProperty("uiLayout", text("uiLayout"));
        int padCount = 0;
        for (int i = 0; i < KeysProcessor::numChordPads; ++i)
            if (! processor.chordPad(i).notes.empty())
                ++padCount;
        obj->setProperty("padCount", padCount);
        return juce::var(obj);
    };
    return t;
}

okstudio::mcp::Tool KeysMcp::toolListParams()
{
    okstudio::mcp::Tool t;
    t.name = "list_params";
    t.description = "Every automatable parameter Keys exposes: its id, display name, "
                     "current value as text, and either its choice list (combo-box "
                     "params) or its numeric min/max (everything else). Use this to see "
                     "what set_params can change and what values are legal before calling it.";
    t.run = [this](const juce::var&, juce::String&) -> juce::var
    {
        juce::Array<juce::var> list;
        for (auto* param : processor.getParameters())
        {
            auto* rap = dynamic_cast<juce::RangedAudioParameter*>(param);
            if (rap == nullptr)
                continue;
            auto* obj = new juce::DynamicObject();
            obj->setProperty("id", rap->getParameterID());
            obj->setProperty("name", rap->getName(100));
            obj->setProperty("value", rap->getCurrentValueAsText());
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param))
            {
                juce::Array<juce::var> choices;
                for (auto& c : choice->choices)
                    choices.add(c);
                obj->setProperty("choices", choices);
            }
            else
            {
                const auto range = rap->getNormalisableRange();
                obj->setProperty("min", range.start);
                obj->setProperty("max", range.end);
            }
            list.add(juce::var(obj));
        }
        return juce::var(list);
    };
    return t;
}

okstudio::mcp::Tool KeysMcp::toolSetParams()
{
    okstudio::mcp::Tool t;
    t.name = "set_params";
    t.description = "Set one or more parameters at once, by id (see list_params). A "
                     "string value is parsed the way typing it into the control would "
                     "(e.g. a scale name); a number is the parameter's real-world value, "
                     "not normalized; true/false sets a toggle. Every id is validated "
                     "before anything changes: if any id is unknown, the whole call "
                     "fails and nothing is applied. Returns each parameter's new text value.";
    t.params = { { "values", "object", "Map of parameter id -> new value (string, number, or boolean).", true } };
    t.run = [this](const juce::var& args, juce::String& error) -> juce::var
    {
        auto* values = args.getProperty("values", juce::var()).getDynamicObject();
        if (values == nullptr)
        {
            error = "values must be an object of paramID -> value";
            return {};
        }

        // Validate every id before touching anything.
        juce::StringArray unknown;
        for (const auto& prop : values->getProperties())
            if (processor.apvts.getParameter(prop.name.toString()) == nullptr)
                unknown.add(prop.name.toString());
        if (! unknown.isEmpty())
        {
            error = "unknown parameter id(s): " + unknown.joinIntoString(", ");
            return {};
        }

        auto* result = new juce::DynamicObject();
        for (const auto& prop : values->getProperties())
        {
            auto* param = processor.apvts.getParameter(prop.name.toString());
            const juce::var& v = prop.value;
            float normalized;
            if (v.isBool())
                normalized = ((bool) v) ? 1.0f : 0.0f;
            else if (v.isString())
                normalized = param->getValueForText(v.toString());
            else
                normalized = param->convertTo0to1((float) v);

            param->beginChangeGesture();
            param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalized));
            param->endChangeGesture();
            result->setProperty(prop.name, param->getCurrentValueAsText());
        }
        return juce::var(result);
    };
    return t;
}

okstudio::mcp::Tool KeysMcp::toolPlayNotes()
{
    okstudio::mcp::Tool t;
    t.name = "play_notes";
    t.description = "Play one or more MIDI notes right now and release them after "
                     "durationMs. Notes go through the same collector the on-screen "
                     "keyboard uses, so they interact normally with Sustain, Latch and "
                     "the arp (if it's on).";
    t.params = {
        { "notes", "array", "MIDI note numbers to play, 0..127.", true },
        { "velocity", "integer", "Note-on velocity, 1..127. Default: the Velocity control's current value.", false },
        { "durationMs", "integer", "How long to hold each note before its note-off, in ms (clamped 10..60000). Default 500.", false },
        { "channel", "integer", "MIDI channel 1..16 to send on. Default: the MIDI Channel control.", false },
    };
    t.run = [this](const juce::var& args, juce::String& error) -> juce::var
    {
        std::vector<int> notes;
        if (! readIntArray(args.getProperty("notes", juce::var()), notes) || notes.empty())
        {
            error = "notes must be a non-empty array of integers 0..127";
            return {};
        }
        for (int n : notes)
        {
            if (n < 0 || n > 127)
            {
                error = "note out of range 0..127: " + juce::String(n);
                return {};
            }
        }

        const int defaultVel = (int) processor.apvts.getRawParameterValue("velocity")->load();
        const int velocity = juce::jlimit(1, 127, (int) args.getProperty("velocity", defaultVel));
        const int durationMs = juce::jlimit(10, 60000, (int) args.getProperty("durationMs", 500));
        const int channel = (int) args.getProperty("channel", 0);
        const float vel01 = velocity / 127.0f;

        for (int n : notes)
        {
            processor.noteOn(n, vel01, 0.0, channel);
            processor.noteOff(n, channel, durationMs * 0.001);
        }

        auto* obj = new juce::DynamicObject();
        obj->setProperty("played", (int) notes.size());
        obj->setProperty("durationMs", durationMs);
        return juce::var(obj);
    };
    return t;
}

okstudio::mcp::Tool KeysMcp::toolPlaySequence()
{
    okstudio::mcp::Tool t;
    t.name = "play_sequence";
    t.description = "Schedule a whole phrase in one call: an array of steps, each "
                     "{note, startMs, durationMs, velocity?}, all timed from now. This is "
                     "the tool for writing a melody rather than triggering single notes. "
                     "Limits: at most 256 steps, and the phrase must finish inside "
                     "120000ms (start + duration of every step).";
    t.params = { { "steps", "array", "Notes to schedule, each {note (0..127), startMs, durationMs, velocity? (1..127, default the Velocity control)}.", true } };
    t.run = [this](const juce::var& args, juce::String& error) -> juce::var
    {
        const auto* arr = args.getProperty("steps", juce::var()).getArray();
        if (arr == nullptr || arr->isEmpty())
        {
            error = "steps must be a non-empty array";
            return {};
        }
        if (arr->size() > 256)
        {
            error = "too many steps (max 256)";
            return {};
        }

        struct Step { int note; double startMs; double durationMs; float vel01; };
        std::vector<Step> steps;
        steps.reserve((size_t) arr->size());
        const int defaultVel = (int) processor.apvts.getRawParameterValue("velocity")->load();
        double horizonMs = 0.0;

        for (const auto& item : *arr)
        {
            if (! item.isObject())
            {
                error = "each step must be an object {note, startMs, durationMs, velocity?}";
                return {};
            }
            const int note = (int) item.getProperty("note", -1);
            if (note < 0 || note > 127)
            {
                error = "step note out of range 0..127";
                return {};
            }
            const double startMs = (double) item.getProperty("startMs", 0.0);
            const double durationMs = (double) item.getProperty("durationMs", 0.0);
            if (startMs < 0.0 || durationMs <= 0.0)
            {
                error = "step startMs must be >= 0 and durationMs must be > 0";
                return {};
            }
            horizonMs = juce::jmax(horizonMs, startMs + durationMs);
            const int velocity = juce::jlimit(1, 127, (int) item.getProperty("velocity", defaultVel));
            steps.push_back({ note, startMs, durationMs, velocity / 127.0f });
        }
        if (horizonMs > 120000.0)
        {
            error = "sequence horizon exceeds 120000ms";
            return {};
        }

        for (const auto& s : steps)
        {
            processor.noteOn(s.note, s.vel01, s.startMs * 0.001);
            processor.noteOff(s.note, 0, (s.startMs + s.durationMs) * 0.001);
        }

        auto* obj = new juce::DynamicObject();
        obj->setProperty("scheduled", (int) steps.size());
        obj->setProperty("horizonMs", horizonMs);
        return juce::var(obj);
    };
    return t;
}

okstudio::mcp::Tool KeysMcp::toolAllNotesOff()
{
    okstudio::mcp::Tool t;
    t.name = "all_notes_off";
    t.description = "Stop every sounding note on every channel gently (per-note note-offs "
                     "plus CC123), exactly what the on-screen All Off button does.";
    t.run = [this](const juce::var&, juce::String&) -> juce::var
    {
        processor.allNotesOff();
        return juce::var(true);
    };
    return t;
}

okstudio::mcp::Tool KeysMcp::toolGetChordPads()
{
    okstudio::mcp::Tool t;
    t.name = "get_chord_pads";
    t.description = "List every chord pad that currently holds a chord: its absolute "
                     "slot (0..63, page P slot S is P*16+S), name, notes, lock state and "
                     "roman numeral (when it came from the Markov generator).";
    t.run = [this](const juce::var&, juce::String&) -> juce::var
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("padsPerPage", KeysProcessor::padsPerPage);
        obj->setProperty("pages", KeysProcessor::numPadPages);
        juce::Array<juce::var> pads;
        for (int i = 0; i < KeysProcessor::numChordPads; ++i)
        {
            const auto& pad = processor.chordPad(i);
            if (pad.notes.empty())
                continue;
            auto* p = new juce::DynamicObject();
            p->setProperty("slot", i);
            p->setProperty("name", pad.name);
            p->setProperty("notes", intVectorToVar(pad.notes));
            p->setProperty("locked", pad.locked);
            p->setProperty("numeral", pad.numeral);
            pads.add(juce::var(p));
        }
        obj->setProperty("pads", pads);
        return juce::var(obj);
    };
    return t;
}

okstudio::mcp::Tool KeysMcp::toolSetChordPad()
{
    okstudio::mcp::Tool t;
    t.name = "set_chord_pad";
    t.description = "Write a chord into a pad slot: this is how Claude hand-writes a "
                     "chord pad instead of using the generator. Overwrites whatever the "
                     "slot held (its generator metadata, if any, is dropped, same as a "
                     "hand-captured pad from the keyboard).";
    t.params = {
        { "slot", "integer", "Pad slot 0..63 (16 pads per page x 4 pages; page P slot S is P*16+S).", true },
        { "notes", "array", "1..10 MIDI notes (0..127) making up the chord.", true },
        { "name", "string", "Label for the pad, e.g. \"Cm7\". Optional; left blank if omitted.", false },
        { "locked", "boolean", "Lock the pad so the chord generator's Regenerate leaves it alone.", false },
    };
    t.run = [this](const juce::var& args, juce::String& error) -> juce::var
    {
        const int slot = (int) args.getProperty("slot", -1);
        if (slot < 0 || slot >= KeysProcessor::numChordPads)
        {
            error = "slot out of range 0.." + juce::String(KeysProcessor::numChordPads - 1);
            return {};
        }
        std::vector<int> notes;
        if (! readIntArray(args.getProperty("notes", juce::var()), notes) || notes.empty() || notes.size() > 10)
        {
            error = "notes must be an array of 1..10 integers";
            return {};
        }
        for (int n : notes)
        {
            if (n < 0 || n > 127)
            {
                error = "note out of range 0..127: " + juce::String(n);
                return {};
            }
        }

        const juce::String name = args.getProperty("name", juce::String()).toString();
        processor.setChordPad(slot, notes, name);
        if (args.hasProperty("locked"))
            processor.setChordPadLocked(slot, (bool) args.getProperty("locked", false));

        auto* obj = new juce::DynamicObject();
        obj->setProperty("slot", slot);
        obj->setProperty("name", processor.chordPad(slot).name);
        obj->setProperty("notes", intVectorToVar(processor.chordPad(slot).notes));
        return juce::var(obj);
    };
    return t;
}

okstudio::mcp::Tool KeysMcp::toolClearChordPad()
{
    okstudio::mcp::Tool t;
    t.name = "clear_chord_pad";
    t.description = "Empty a chord pad slot (stops it first if it's sounding).";
    t.params = { { "slot", "integer", "Pad slot 0..63 to clear.", true } };
    t.run = [this](const juce::var& args, juce::String& error) -> juce::var
    {
        const int slot = (int) args.getProperty("slot", -1);
        if (slot < 0 || slot >= KeysProcessor::numChordPads)
        {
            error = "slot out of range 0.." + juce::String(KeysProcessor::numChordPads - 1);
            return {};
        }
        cancelPendingRelease(slot);
        processor.clearChordPad(slot);
        return juce::var(true);
    };
    return t;
}

okstudio::mcp::Tool KeysMcp::toolPressChordPad()
{
    okstudio::mcp::Tool t;
    t.name = "press_chord_pad";
    t.description = "Fire a chord pad now, beat-pad style (honours the Exclusive choke). "
                     "With durationMs, it auto-releases after that many ms; without it, "
                     "the chord keeps ringing until release_chord_pad is called or Sustain lifts.";
    t.params = {
        { "slot", "integer", "Pad slot 0..63 to fire.", true },
        { "durationMs", "integer", "Auto-release after this many ms (clamped 10..60000). Omit to leave it ringing.", false },
    };
    t.run = [this](const juce::var& args, juce::String& error) -> juce::var
    {
        const int slot = (int) args.getProperty("slot", -1);
        if (slot < 0 || slot >= KeysProcessor::numChordPads)
        {
            error = "slot out of range 0.." + juce::String(KeysProcessor::numChordPads - 1);
            return {};
        }
        cancelPendingRelease(slot); // re-pressing resets any earlier scheduled release
        processor.pressChordPad(slot);

        bool scheduled = false;
        int durationMs = 0;
        if (args.hasProperty("durationMs"))
        {
            durationMs = juce::jlimit(10, 60000, (int) args.getProperty("durationMs", 500));
            pendingReleases.push_back({ juce::Time::getMillisecondCounterHiRes() + durationMs, slot });
            scheduled = true;
        }

        auto* obj = new juce::DynamicObject();
        obj->setProperty("slot", slot);
        obj->setProperty("scheduledRelease", scheduled);
        if (scheduled)
            obj->setProperty("durationMs", durationMs);
        return juce::var(obj);
    };
    return t;
}

okstudio::mcp::Tool KeysMcp::toolReleaseChordPad()
{
    okstudio::mcp::Tool t;
    t.name = "release_chord_pad";
    t.description = "Stop a chord pad (unless Sustain is holding it), same as releasing "
                     "the mouse over it. Cancels any pending auto-release you scheduled "
                     "with press_chord_pad's durationMs.";
    t.params = { { "slot", "integer", "Pad slot 0..63 to release.", true } };
    t.run = [this](const juce::var& args, juce::String& error) -> juce::var
    {
        const int slot = (int) args.getProperty("slot", -1);
        if (slot < 0 || slot >= KeysProcessor::numChordPads)
        {
            error = "slot out of range 0.." + juce::String(KeysProcessor::numChordPads - 1);
            return {};
        }
        cancelPendingRelease(slot);
        processor.releaseChordPad(slot);
        return juce::var(true);
    };
    return t;
}

okstudio::mcp::Tool KeysMcp::toolGetArpPattern()
{
    okstudio::mcp::Tool t;
    t.name = "get_arp_pattern";
    t.description = "Read an arp pattern's six per-step lanes (note, octave, velocity, "
                     "gate, ratchet, probability), each trimmed to its own length, plus "
                     "its per-lane clock dividers and which pattern is active. Without "
                     "slot, reads the live lanes (what's currently playing/showing in the "
                     "editor); with slot, reads that stored pattern (0..7, A-H) without "
                     "disturbing what's live.";
    t.params = { { "slot", "integer", "Pattern slot 0..7 (A-H). Omit to read the live lanes.", false } };
    t.run = [this](const juce::var& args, juce::String& error) -> juce::var
    {
        auto* obj = new juce::DynamicObject();
        if (args.hasProperty("slot"))
        {
            const int slot = (int) args.getProperty("slot", -1);
            if (slot < 0 || slot >= KeysProcessor::numArpPatterns)
            {
                error = "slot out of range 0..7";
                return {};
            }
            const auto& pat = processor.arpPatternSlot(slot);
            writeArpPatternInto(*obj, pat.value, pat.length, pat.clockDiv);
        }
        else
        {
            std::array<std::array<int, ArpEngine::maxSteps>, ArpEngine::numLanes> values {};
            std::array<int, ArpEngine::numLanes> lengths {}, clockDivs {};
            for (int l = 0; l < ArpEngine::numLanes; ++l)
            {
                for (int s = 0; s < ArpEngine::maxSteps; ++s)
                    values[(size_t) l][(size_t) s] = processor.arp.lanes.value[(size_t) l][(size_t) s].load();
                lengths[(size_t) l] = processor.arp.lanes.length[(size_t) l].load();
                clockDivs[(size_t) l] = processor.arp.lanes.clockDiv[(size_t) l].load();
            }
            writeArpPatternInto(*obj, values, lengths, clockDivs);
        }
        obj->setProperty("active", processor.arpActivePattern());
        return juce::var(obj);
    };
    return t;
}

okstudio::mcp::Tool KeysMcp::toolSetArpPattern()
{
    okstudio::mcp::Tool t;
    t.name = "set_arp_pattern";
    t.description =
        "Write one or more of an arp pattern's per-step lanes. Lane meanings and legal "
        "ranges (docs/ARP_DESIGN.md): note (-1 = muted step, 0 = follow the direction "
        "mode, 1..8 = fixed chord-note index), octave (-3..3 added octaves), velocity "
        "(10..200, percent of the played velocity), gate (5..200, percent of the step "
        "length; over 100 ties into the next step), ratchet (1..4 sub-hits per step), "
        "probability (0..100, percent chance the step fires). Each lane array you supply "
        "also sets that lane's length to the array's size (clamped 1..32). Shorter "
        "arrays make a shorter pattern for that lane, for polymeter against the others. "
        "clockDivs is an optional map of lane name -> 0 (every step) / 1 (every 2nd) / 2 "
        "(every 4th). Without slot, writes the live lanes directly (the same path the "
        "editor edits through); with slot, writes that stored pattern (0..7, A-H), and "
        "refreshes the live lanes too if that slot happens to be the active one.";
    t.params = {
        { "slot", "integer", "Pattern slot 0..7 to write. Omit to write the live lanes.", false },
        { "note", "array", "Per-step note lane (-1..8).", false },
        { "octave", "array", "Per-step octave lane (-3..3).", false },
        { "velocity", "array", "Per-step velocity lane (10..200).", false },
        { "gate", "array", "Per-step gate lane (5..200).", false },
        { "ratchet", "array", "Per-step ratchet lane (1..4).", false },
        { "probability", "array", "Per-step probability lane (0..100).", false },
        { "clockDivs", "object", "Optional map of lane name -> clock divider 0/1/2.", false },
    };
    t.run = [this](const juce::var& args, juce::String& error) -> juce::var
    {
        struct LaneEdit { int lane; std::vector<int> values; };
        std::vector<LaneEdit> edits;
        for (const auto& r : laneRanges)
        {
            const char* name = laneNames[r.lane];
            if (! args.hasProperty(name))
                continue;
            std::vector<int> vals;
            if (! readIntArray(args.getProperty(name, juce::var()), vals) || vals.empty()
                || (int) vals.size() > ArpEngine::maxSteps)
            {
                error = juce::String(name) + " must be an array of 1.." + juce::String(ArpEngine::maxSteps) + " integers";
                return {};
            }
            for (auto& v : vals)
                v = juce::jlimit(r.lo, r.hi, v);
            edits.push_back({ r.lane, std::move(vals) });
        }

        std::vector<std::pair<int, int>> clockDivEdits; // lane, value
        const juce::var clockDivsVar = args.getProperty("clockDivs", juce::var());
        if (clockDivsVar.getDynamicObject() != nullptr)
            for (int l = 0; l < ArpEngine::numLanes; ++l)
                if (clockDivsVar.hasProperty(laneNames[l]))
                    clockDivEdits.push_back({ l, juce::jlimit(0, 2, (int) clockDivsVar.getProperty(laneNames[l], 0)) });

        if (edits.empty() && clockDivEdits.empty())
        {
            error = "nothing to set: supply at least one lane array or clockDivs";
            return {};
        }

        const bool hasSlot = args.hasProperty("slot");
        const int slot = hasSlot ? (int) args.getProperty("slot", -1) : processor.arpActivePattern();
        if (hasSlot && (slot < 0 || slot >= KeysProcessor::numArpPatterns))
        {
            error = "slot out of range 0..7";
            return {};
        }

        if (hasSlot)
        {
            auto pattern = processor.arpPatternSlot(slot); // copy
            for (const auto& e : edits)
            {
                for (size_t s = 0; s < e.values.size(); ++s)
                    pattern.value[(size_t) e.lane][s] = e.values[s];
                pattern.length[(size_t) e.lane] = (int) e.values.size();
            }
            for (const auto& cd : clockDivEdits)
                pattern.clockDiv[(size_t) cd.first] = cd.second;
            processor.setArpPatternSlot(slot, pattern);
        }
        else
        {
            for (const auto& e : edits)
            {
                for (size_t s = 0; s < e.values.size(); ++s)
                    processor.arp.lanes.value[(size_t) e.lane][s].store(e.values[s]);
                processor.arp.lanes.length[(size_t) e.lane].store((int) e.values.size());
            }
            for (const auto& cd : clockDivEdits)
                processor.arp.lanes.clockDiv[(size_t) cd.first].store(cd.second);
        }

        auto* obj = new juce::DynamicObject();
        obj->setProperty("slot", slot);
        obj->setProperty("lanesSet", (int) edits.size());
        obj->setProperty("clockDivsSet", (int) clockDivEdits.size());
        return juce::var(obj);
    };
    return t;
}

okstudio::mcp::Tool KeysMcp::toolRecallArpPattern()
{
    okstudio::mcp::Tool t;
    t.name = "recall_arp_pattern";
    t.description = "Make a stored pattern (0..7, A-H) the active/live one, same as "
                     "clicking its pattern button. Snapshots the current live lanes into "
                     "their slot first, so nothing being edited is lost.";
    t.params = { { "slot", "integer", "Pattern slot 0..7 (A-H) to make active.", true } };
    t.run = [this](const juce::var& args, juce::String& error) -> juce::var
    {
        const int slot = (int) args.getProperty("slot", -1);
        if (slot < 0 || slot >= KeysProcessor::numArpPatterns)
        {
            error = "slot out of range 0..7";
            return {};
        }
        processor.recallArpPattern(slot);
        return juce::var(true);
    };
    return t;
}

okstudio::mcp::Tool KeysMcp::toolStoreArpPattern()
{
    okstudio::mcp::Tool t;
    t.name = "store_arp_pattern";
    t.description = "Snapshot the live lanes (whatever is currently playing/being edited) "
                     "into the active pattern slot, same as what happens automatically "
                     "just before the editor switches patterns.";
    t.run = [this](const juce::var&, juce::String&) -> juce::var
    {
        processor.storeActiveArpPattern();
        return juce::var(true);
    };
    return t;
}
} // namespace keys
