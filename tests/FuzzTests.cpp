// Randomised invariant testing of Keys' pure cores: the kind of input nobody wrote a case for.
//
// Every target below is a function of one `juce::Random`: it draws an input, runs the code under
// test, and throws `Fail` if an invariant breaks. The harness owns the seed. The normal suite
// (`ctest`) runs this binary on a fixed seed with a small budget, so a pull request is judged on a
// fixed set of inputs and never goes red for a reason it did not cause; the nightly workflow
// (`.github/workflows/fuzz-nightly.yml`) runs it on a fresh seed with a large budget, and that is
// where new inputs are allowed to surprise us. See "Nightly fuzzing" in docs/BUILD.md.
//
// A failure prints the exact command that replays it:
//
//     Keys_fuzz --target chordsym --seed 1234 --case 56
//
// Case `i` of a target seeds its own generator from (seed, target, i), so one case replays alone
// and the fix can start from the input rather than from the night's whole run. Fix the bug, then
// add the minimal input as a named test in the normal suite.
//
// Deliberately not JUCE UnitTest: that framework reports pass counts and messages, and what a
// fuzz run needs to report is a reproducer.
#include "ArpEngine.h"
#include "ChordGen.h"
#include "ChordSymbol.h"
#include "ChordVoicing.h"
#include "Chords.h"
#include "EuclidGen.h"
#include "NoteMath.h"
#include "PluginProcessor.h"
#include "ScaleModes.h"
#include <okstudio/Scales.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using keys::KeysProcessor;

namespace
{
struct Fail : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

void check(bool ok, const juce::String& what)
{
    if (! ok)
        throw Fail(what.toStdString());
}

juce::String show(const std::vector<int>& v)
{
    juce::String s;
    for (int n : v)
        s += (s.isEmpty() ? "" : " ") + juce::String(n);
    return "{" + s + "}";
}

// splitmix64: a case's generator is a function of (seed, target, case) and nothing else.
juce::int64 mix(juce::uint64 seed, juce::uint64 target, juce::uint64 caseIndex)
{
    juce::uint64 z = seed * 0x9E3779B97F4A7C15ull + target * 0xBF58476D1CE4E5B9ull + caseIndex + 1;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (juce::int64) (z ^ (z >> 31));
}

int pick(juce::Random& r, int lo, int hi) { return lo + r.nextInt(hi - lo + 1); } // inclusive

// ---------------------------------------------------------------------------------------------
// chordsym: the typed-chord parser. Three kinds of input: chord-ish atoms glued together, a
// valid symbol with a few characters mutated, and arbitrary Unicode. The invariant that matters
// is the round trip: the canonical name a pad prints must parse back to the same chord, or a
// pad's own caption would mean something else when retyped.
void fuzzChordSym(juce::Random& r)
{
    using namespace keys::chordsym;
    static const char* atoms[] = { "C", "D", "E", "F", "G", "A", "B", "c", "d", "e", "f", "g", "a",
                                   "b", "#", "b", "\xe2\x99\xad", "\xe2\x99\xaf", "m", "M", "maj",
                                   "min", "dim", "aug", "sus", "add", "7", "9", "11", "13", "6",
                                   "5", "2", "4", "/", "+", "-", " ", "(", ")", "x", "H", "0",
                                   "\xc3\xb8", "\xc2\xb0", "\xce\x94", "b5", "#5", "#9", "b9", "/9" };
    static const char* roots[] = { "C", "C#", "Db", "D", "D#", "Eb", "E", "F", "F#", "Gb", "G",
                                   "G#", "Ab", "A", "A#", "Bb", "B", "Cb", "E#", "Fb", "B#" };

    juce::String s;
    switch (r.nextInt(3))
    {
        case 0:
        {
            const int n = r.nextInt(8);
            for (int i = 0; i < n; ++i)
                s += juce::String::fromUTF8(atoms[r.nextInt(juce::numElementsInArray(atoms))]);
            break;
        }
        case 1:
        {
            const auto& qs = qualities();
            const auto& q = qs[(size_t) r.nextInt((int) qs.size())];
            s = roots[r.nextInt(juce::numElementsInArray(roots))];
            if (! q.aliases.empty() && r.nextBool())
                s += q.aliases[(size_t) r.nextInt((int) q.aliases.size())];
            else
                s += q.suffix;
            if (r.nextInt(3) == 0)
                s += juce::String("/") + roots[r.nextInt(juce::numElementsInArray(roots))];
            const int mutations = r.nextInt(3);
            for (int i = 0; i < mutations && s.isNotEmpty(); ++i)
            {
                const int at = r.nextInt(s.length());
                const auto atom = juce::String::fromUTF8(atoms[r.nextInt(juce::numElementsInArray(atoms))]);
                switch (r.nextInt(3))
                {
                    case 0: s = s.substring(0, at) + atom + s.substring(at); break;
                    case 1: s = s.substring(0, at) + s.substring(at + 1); break;
                    default: s = s.substring(0, at) + atom + s.substring(at + 1); break;
                }
            }
            break;
        }
        default:
        {
            const int n = r.nextInt(16);
            for (int i = 0; i < n; ++i)
                s += juce::String::charToString((juce::juce_wchar) (1 + r.nextInt(0x2FFF)));
            break;
        }
    }

    const auto p = parse(s);
    if (! p)
        return;

    const auto where = juce::String(" for \"") + s + "\"";
    check(! p->intervals.empty(), "parsed chord has no intervals" + where);
    check(p->intervals.front() == 0, "first interval is not the root" + where);
    check(p->rootPc >= 0 && p->rootPc < 12, "root pitch class out of range" + where);
    check(p->bassPc >= -1 && p->bassPc < 12, "bass pitch class out of range" + where);
    check(p->name.isNotEmpty(), "canonical name is empty" + where);
    for (int octave = 0; octave <= 9; ++octave)
    {
        const auto n = notes(*p, octave);
        check(! n.empty(), "notes() is empty" + where);
        for (int x : n)
            check(x >= 0 && x <= 127, "note out of MIDI range: " + show(n) + where);
    }

    const auto q = parse(p->name);
    check(q.has_value(), "canonical name \"" + p->name + "\" does not parse" + where);
    check(q->rootPc == p->rootPc && q->bassPc == p->bassPc && q->intervals == p->intervals,
          "canonical name \"" + p->name + "\" re-parses to a different chord" + where);
    check(q->name == p->name, "canonical name is not a fixed point: \"" + p->name + "\" -> \"" + q->name + "\"" + where);
}

// ---------------------------------------------------------------------------------------------
// detect: naming a chord from its notes. The name must not depend on the order the notes
// arrived in or on which octave they sit, since the same pad is captured both ways.
void fuzzDetect(juce::Random& r)
{
    std::vector<int> notes;
    const int n = r.nextInt(11);
    const bool clustered = r.nextBool();
    const int base = pick(r, 24, 84);
    for (int i = 0; i < n; ++i)
        notes.push_back(clustered ? juce::jlimit(0, 127, base + r.nextInt(25)) : r.nextInt(128));

    const auto name = keys::chords::detect(notes);
    if (notes.empty())
    {
        check(name.isEmpty(), "empty input named something");
        return;
    }
    check(name.isNotEmpty(), "non-empty input " + show(notes) + " got no name");

    auto shuffled = notes;
    for (int i = (int) shuffled.size() - 1; i > 0; --i)
        std::swap(shuffled[(size_t) i], shuffled[(size_t) r.nextInt(i + 1)]);
    check(keys::chords::detect(shuffled) == name,
          "order changes the name: " + show(notes) + " -> " + name + ", " + show(shuffled) + " -> "
              + keys::chords::detect(shuffled));

    const int shift = r.nextBool() ? 12 : -12;
    auto shifted = notes;
    bool inRange = true;
    for (auto& x : shifted)
    {
        x += shift;
        inRange = inRange && x >= 0 && x <= 127;
    }
    if (inRange)
        check(keys::chords::detect(shifted) == name,
              "octave changes the name: " + show(notes) + " -> " + name + ", " + show(shifted) + " -> "
                  + keys::chords::detect(shifted));
}

// ---------------------------------------------------------------------------------------------
// notemath: which note a drawn key sends. Always a MIDI note, and under Scale Lock always in
// the scale unless the clamp at either end of the range got there first.
void fuzzNoteMath(juce::Random& r)
{
    const auto& scales = okstudio::scales::all();
    const int note = pick(r, -16, 143);
    const bool lock = r.nextBool();
    const int root = r.nextInt(12);
    const int scale = r.nextInt((int) scales.size());
    const int octave = pick(r, -3, 3);

    const int out = keys::resolveOutputNote(note, lock, root, scale, octave);
    const auto what = juce::String("resolveOutputNote(") + juce::String(note) + ", " + (lock ? "lock" : "free")
                      + ", root " + juce::String(root) + ", " + scales[(size_t) scale].name + ", oct "
                      + juce::String(octave) + ") = " + juce::String(out);
    check(out >= 0 && out <= 127, what + ": out of MIDI range");
    if (lock && out > 0 && out < 127)
    {
        const int pc = ((out - root) % 12 + 12) % 12;
        const auto& iv = scales[(size_t) scale].intervals;
        check(std::find(iv.begin(), iv.end(), pc) != iv.end(), what + ": not in the scale");
    }
}

// ---------------------------------------------------------------------------------------------
// euclid: Bjorklund. The fill and the per-step predicate are one function, the hit count is
// what was asked for, and anything out of range is clamped rather than read out of bounds.
void fuzzEuclid(juce::Random& r)
{
    const int steps = pick(r, -2, 40);
    const int hits = pick(r, -4, 48);
    const int rotation = pick(r, -100, 100);
    const int n = juce::jmax(0, steps);
    std::vector<int> out((size_t) n + 1, 7); // the sentinel past the end must survive
    keys::euclidFill(out.data(), hits, steps, rotation);
    check(out[(size_t) n] == 7, "euclidFill wrote past steps");
    int count = 0;
    for (int i = 0; i < n; ++i)
    {
        check(out[(size_t) i] == 0 || out[(size_t) i] == 1, "euclidFill wrote something other than 0/1");
        check((out[(size_t) i] == 1) == keys::euclidHit(i, hits, steps, rotation), "euclidHit disagrees with euclidFill");
        count += out[(size_t) i];
    }
    if (steps >= 1)
        check(count == juce::jlimit(0, steps, hits),
              "wrong hit count: " + juce::String(count) + " for hits " + juce::String(hits) + " steps "
                  + juce::String(steps));
}

// ---------------------------------------------------------------------------------------------
// chordgen: the weighted pool and the voicing pipeline, with the settings any gate can express
// and a few it cannot (empty allow-lists, inverted ranges), since a session file can say either.
void fuzzChordGen(juce::Random& r)
{
    using namespace keys::chordgen;
    const int rootPc = r.nextInt(12);
    const int modeIndex = r.nextInt(keys::modes::count());
    const int count = r.nextInt(17);
    Options opts;
    opts.octave = pick(r, 2, 6); // genOctave's range; generate() does not clamp, so 8 would overflow
    opts.noteCounts.clear();
    for (int c = 1; c <= 8; ++c)
        if (r.nextInt(3) == 0)
            opts.noteCounts.push_back(c);
    if (opts.noteCounts.empty() && r.nextInt(10) != 0)
        opts.noteCounts = { 3, 4 };
    opts.inversions.clear();
    for (int inv = 0; inv <= 4; ++inv)
        if (r.nextInt(3) == 0)
            opts.inversions.push_back(inv);
    if (opts.inversions.empty() && r.nextInt(10) != 0)
        opts.inversions = { 0 };
    opts.scaleCompliance = r.nextFloat();
    opts.lockInfluence = r.nextFloat();
    std::vector<int> locked;
    for (int i = r.nextInt(4); i > 0; --i)
        locked.push_back(r.nextInt((int) types().size()));

    auto chords = generate(rootPc, modeIndex, count, opts, locked, r);
    check((int) chords.size() <= count, "generate() made more chords than asked");
    for (const auto& c : chords)
    {
        check(! c.notes.empty(), "generate() made an empty chord");
        for (int n : c.notes)
            check(n >= 0 && n <= 127, "generate() made a note out of range: " + show(c.notes));
    }

    const auto single = generateSingle(rootPc, modeIndex, pick(r, -1, 7), pick(r, -1, (int) types().size() - 1),
                                       opts, locked, r);
    for (int n : single.notes)
        check(n >= 0 && n <= 127, "generateSingle() made a note out of range: " + show(single.notes));

    const std::pair<int, int> noteCountRange { pick(r, 1, 8), pick(r, 1, 8) };
    const std::pair<int, int> octaveRange { pick(r, 2, 6), pick(r, 2, 6) };
    keys::chordvoicing::applyVoicingPipeline(chords, pick(r, -100, 100), rootPc, modeIndex, noteCountRange,
                                             octaveRange, r.nextBool(), r.nextBool(), opts.inversions,
                                             r.nextFloat(), r);
    for (const auto& c : chords)
    {
        check(! c.notes.empty(), "the voicing pipeline emptied a chord");
        for (int n : c.notes)
            check(n >= 0 && n <= 127, "the voicing pipeline moved a note out of range: " + show(c.notes));
    }
}

// ---------------------------------------------------------------------------------------------
// arp: one engine, random parameters, lanes, chord and transport, driven for a few dozen blocks
// of random size. What is checked is the MIDI leaving it: every event inside its block, every
// pitch a MIDI note, every note-on audible, no note-off for a pitch that is not sounding, no
// second note-on for one that is, and nothing still held once the chord is gone and Latch is off.
struct ArpOutputState
{
    std::map<int, int> active; // pitch -> sounding count
};

void checkArpBlock(const juce::MidiBuffer& out, int numSamples, ArpOutputState& st, const juce::String& where)
{
    // KEYS_FUZZ_TRACE=1 prints every event, for reading a failing case.
    static const bool trace = juce::SystemStats::getEnvironmentVariable("KEYS_FUZZ_TRACE", "").isNotEmpty();
    if (trace)
        for (const auto meta : out)
            std::cout << "      " << where << " @" << meta.samplePosition << "  " << meta.getMessage().getDescription() << std::endl;
    int lastSample = -1;
    for (const auto meta : out)
    {
        const auto m = meta.getMessage();
        check(meta.samplePosition >= 0 && meta.samplePosition < juce::jmax(1, numSamples),
              where + ": event at sample " + juce::String(meta.samplePosition) + " in a block of "
                  + juce::String(numSamples));
        check(meta.samplePosition >= lastSample, where + ": events out of order");
        lastSample = meta.samplePosition;
        // isNoteOn(true): a note-on at velocity 0 is a note-off on the wire, and the engine
        // must never emit one as a note-on; asking the lenient way is what sees it.
        if (m.isNoteOn(true))
        {
            const int n = m.getNoteNumber();
            check(n >= 0 && n <= 127, where + ": note-on out of range " + juce::String(n));
            check(m.getVelocity() > 0, where + ": note-on with velocity 0 for " + juce::String(n));
            check(st.active[n] == 0, where + ": second note-on for a sounding " + juce::String(n));
            st.active[n] += 1;
        }
        else if (m.isNoteOff())
        {
            const int n = m.getNoteNumber();
            check(st.active[n] > 0, where + ": note-off for a silent " + juce::String(n));
            st.active[n] -= 1;
        }
    }
}

void fuzzArp(juce::Random& r)
{
    using E = keys::ArpEngine;
    static const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const double sr = rates[r.nextInt(3)];
    E e;
    e.prepare(sr);

    E::Params p;
    p.enabled = r.nextInt(10) != 0;
    p.rateIndex = pick(r, 0, 10);
    p.rateFree = r.nextInt(4) == 0;
    p.rateHz = std::exp(std::log(E::minRateHz) + r.nextDouble() * (std::log(E::maxRateHz) - std::log(E::minRateHz)));
    p.dotted = r.nextBool();
    p.tuplet = pick(r, 0, 9);
    p.anchored = r.nextBool();
    p.usePattern = r.nextInt(3) != 0;
    p.octaveRange = pick(r, 1, 4);
    p.direction = (E::Direction) r.nextInt(E::numDirections);
    p.swing = -0.75f + 1.5f * r.nextFloat();
    p.gate = pick(r, 5, 200);
    p.chance = pick(r, 0, 100);
    p.latch = r.nextInt(4) == 0;
    p.retrigger = r.nextBool();
    p.retrigBeats = r.nextDouble() * 4.0;
    p.offset = pick(r, 0, 31);
    p.spread = pick(r, 0, 24);
    p.spreadDegrees = r.nextBool();
    p.rootPc = r.nextInt(12);
    p.scaleMask = r.nextBool() ? 0xFFFu : (unsigned) r.nextInt(0x1000);
    p.scaleLock = r.nextBool();
    p.velRamp = pick(r, -100, 100);
    p.rampBeats = r.nextDouble() * 16.0;
    p.humanize = pick(r, 0, 100);
    p.humanVel = pick(r, 0, 127);
    p.humanizeSpan = pick(r, 0, 100);
    p.humanVelSpan = pick(r, 0, 100);
    p.drift = pick(r, 0, 100);
    p.mutate = pick(r, 0, 100);
    p.mutateLock = pick(r, 0, 100);
    p.mutateSeed = pick(r, 0, 3);
    p.stray = pick(r, 0, 100);
    p.legato = r.nextBool();
    for (int i = 0; i < 2; ++i)
    {
        p.harmSemis[i] = pick(r, -24, 24);
        p.harmSemisB[i] = pick(r, -24, 24);
        p.harmChance[i] = pick(r, 0, 100);
    }
    p.octShift = pick(r, -3, 3);
    p.volume = pick(r, 0, 100);
    p.velLevel = pick(r, 1, 127);
    p.fallbackBpm = 20.0 + r.nextDouble() * 280.0;
    p.followHost = r.nextBool();
    p.duck = pick(r, 0, 100);
    p.resetFollow = r.nextBool();
    p.clockFollow = r.nextInt(3) == 0;

    // KEYS_FUZZ_OFF=legato,clock,harm,late,lock,duck,stray,mutate,drift,humanvel,ramp,swing,dividers:
    // switch named features off on top of the draw, for bisecting a failing case.
    static const juce::StringArray off = juce::StringArray::fromTokens(
        juce::SystemStats::getEnvironmentVariable("KEYS_FUZZ_OFF", ""), ",", "");
    if (off.contains("legato")) p.legato = false;
    if (off.contains("clock")) p.clockFollow = false;
    if (off.contains("harm")) { p.harmChance[0] = p.harmChance[1] = 0; }
    if (off.contains("late")) p.humanize = 0;
    if (off.contains("lock")) p.scaleLock = false;
    if (off.contains("duck")) p.duck = 0;
    if (off.contains("stray")) p.stray = 0;
    if (off.contains("mutate")) p.mutate = 0;
    if (off.contains("drift")) p.drift = 0;
    if (off.contains("humanvel")) p.humanVel = 0;
    if (off.contains("ramp")) p.velRamp = 0;
    if (off.contains("swing")) p.swing = 0.0f;

    static const bool trace = juce::SystemStats::getEnvironmentVariable("KEYS_FUZZ_TRACE", "").isNotEmpty();
    if (trace)
        std::cout << "      params: dir " << (int) p.direction << " pattern " << p.usePattern << " rate " << p.rateIndex
                  << (p.rateFree ? " Hz" : " sync") << " tuplet " << p.tuplet << " gate " << p.gate << " chance " << p.chance
                  << " latch " << p.latch << " lock " << p.scaleLock << " mask " << p.scaleMask << " velRamp " << p.velRamp
                  << " ramp " << p.rampBeats << " humanize " << p.humanize << " humanVel " << p.humanVel << " drift " << p.drift
                  << " mutate " << p.mutate << " stray " << p.stray << " legato " << p.legato << " harm " << p.harmSemis[0]
                  << "/" << p.harmSemis[1] << " octShift " << p.octShift << " volume " << p.volume << " velLevel " << p.velLevel
                  << " duck " << p.duck << " clockFollow " << p.clockFollow << " spread " << p.spread << std::endl;

    E::ChordTable table;
    for (int s = 0; s < E::ChordTable::numSlots; ++s)
    {
        const int n = r.nextInt(E::ChordTable::maxNotes + 1);
        for (int i = 0; i < n; ++i)
            table.note[(size_t) s][(size_t) i].store(r.nextInt(128));
        table.count[(size_t) s].store(n);
    }
    p.chords = r.nextBool() ? &table : nullptr;

    for (int lane = 0; lane < E::numLanes; ++lane)
    {
        const auto range = E::laneRange(lane);
        const auto li = (size_t) lane;
        for (int s = 0; s < E::maxSteps; ++s)
            e.lanes.value[li][(size_t) s].store(r.nextInt(4) == 0 ? E::laneDefaults[lane] : pick(r, range.lo, range.hi));
        e.lanes.length[li].store(pick(r, 1, E::maxSteps));
        e.lanes.clockDiv[li].store(pick(r, 0, 2));
        e.lanes.on[li].store(r.nextInt(5) != 0 ? 1 : 0);
        e.lanes.loopFrom[li].store(pick(r, 0, E::maxSteps - 1));
        e.lanes.loopTo[li].store(pick(r, 0, E::maxSteps - 1));
        e.lanes.dir[li].store(r.nextInt(E::numLaneDirs));
    }
    for (auto& d : e.rhythmDiv)
        d.store(r.nextInt(4) == 0 && ! off.contains("dividers") ? pick(r, 0, 16) : 0);

    E::HostClock clock;
    clock.playing = r.nextInt(4) != 0;
    clock.hasPpq = r.nextInt(4) != 0;
    clock.hasBpm = r.nextBool();
    clock.hasGrid = r.nextInt(4) == 0;
    clock.bpm = 20.0 + r.nextDouble() * 280.0;
    clock.ppq = r.nextDouble() * 64.0;

    const bool followed = r.nextBool();
    E::LineRecord source;
    p.follow = followed ? &source : nullptr;

    // The chord, 1..18 notes so the held set overflows now and then. Each note is released
    // exactly once, at a random point in the middle of the run or at the release block.
    struct Voice { int note; int channel; bool down; };
    std::vector<Voice> voices;
    const int chordSize = pick(r, 1, E::maxHeld + 2);
    for (int i = 0; i < chordSize; ++i)
        voices.push_back({ r.nextInt(128), pick(r, 1, 16), false });

    ArpOutputState st;
    const int runBlocks = pick(r, 12, 30);
    const int releaseAt = runBlocks / 2;
    const int drainBlocks = 12;
    for (int b = 0; b < runBlocks + drainBlocks; ++b)
    {
        const int numSamples = r.nextInt(50) == 0 ? 0 : pick(r, 1, 4096);
        juce::MidiBuffer in;
        if (b == 0)
        {
            for (auto& v : voices)
            {
                in.addEvent(juce::MidiMessage::noteOn(v.channel, v.note, 0.05f + 0.95f * r.nextFloat()), 0);
                v.down = true;
            }
        }
        else if (b < releaseAt)
        {
            // Some comings and goings mid-run, on real note-on/off pairs.
            for (auto& v : voices)
                if (r.nextInt(12) == 0)
                {
                    const int at = numSamples > 0 ? r.nextInt(numSamples) : 0;
                    if (v.down)
                        in.addEvent(juce::MidiMessage::noteOff(v.channel, v.note), at);
                    else
                        in.addEvent(juce::MidiMessage::noteOn(v.channel, v.note, 0.05f + 0.95f * r.nextFloat()), at);
                    v.down = ! v.down;
                }
            if (r.nextInt(8) == 0)
                in.addEvent(juce::MidiMessage::controllerEvent(1, r.nextInt(128), r.nextInt(128)), 0);
        }
        else if (b == releaseAt)
        {
            for (auto& v : voices)
                if (v.down)
                {
                    in.addEvent(juce::MidiMessage::noteOff(v.channel, v.note), 0);
                    v.down = false;
                }
        }
        if (b == runBlocks || b == runBlocks + 1)
        {
            // A latched chord is meant to ring on: turning Latch off releases nothing, by design
            // (that is Hold off's job, `releaseArpHold`), and only a fresh note starts the next
            // chord. So the drain plays one note while Latch is still on, which replaces the
            // latched chord with that note, then turns Latch off and plays and releases it again.
            if (b == runBlocks + 1)
                p.latch = false;
            in.addEvent(juce::MidiMessage::noteOn(1, 60, 0.5f), 0);
            in.addEvent(juce::MidiMessage::noteOff(1, 60), juce::jmax(0, numSamples - 1));
        }

        if (followed)
        {
            // What a real source's record can hold: its fires are distinct, increasing offsets
            // inside this block (two steps never land on one sample), and its step length is
            // positive (stepLengthBeats clamps the rate). Fires may still be far denser than the
            // step length says, as a Late lane at 90% followed by a step at 0% makes them.
            source.firedBefore += source.firedCount;
            source.firedCount = 0;
            const int wanted = r.nextInt(E::maxStepsPerBlock + 1);
            for (int at = -1, i = 0; i < wanted; ++i)
            {
                if (at + 1 > numSamples - 1)
                    break;
                at = pick(r, at + 1, numSamples - 1);
                source.firedAt[(size_t) source.firedCount++] = at;
            }
            source.lastStepFired = r.nextBool();
            source.lastNote = pick(r, -1, 127);
            source.lastVelocity = pick(r, 1, 127);
            source.stepSamples = 1.0 + r.nextDouble() * sr;
            source.sounding = r.nextInt(4) != 0;
        }

        juce::MidiBuffer out;
        e.process(p, clock, numSamples, in, out);
        const auto where = juce::String("block ") + juce::String(b);
        checkArpBlock(out, numSamples, st, where);

        const double blockBeats = clock.bpm / 60.0 / sr * numSamples;
        if (r.nextInt(10) == 0)
            clock.ppq = r.nextDouble() * 64.0; // a transport jump
        else
            clock.ppq += blockBeats;
        if (r.nextInt(20) == 0)
            clock.playing = ! clock.playing;
    }

    check(e.heldNoteCount() == 0, "the engine still holds " + juce::String(e.heldNoteCount())
                                      + " notes after every note-off with Latch off");

    // Whatever the drain did not finish, a transport jump must flush: that is the engine's own
    // rule for owed note-offs.
    clock.playing = true;
    clock.hasPpq = true;
    clock.ppq += 1000.0;
    p.enabled = false;
    juce::MidiBuffer out;
    e.process(p, clock, 512, {}, out);
    checkArpBlock(out, 512, st, "flush block");
    for (const auto& [note, count] : st.active)
        check(count == 0, "note " + juce::String(note) + " still sounding after the chord was released and the clock jumped");
}

// ---------------------------------------------------------------------------------------------
// state: a saved session that is not quite the one Keys wrote. Byte-level damage (a truncated
// or bit-flipped file) and tree-level damage (an attribute holding a value the UI could never
// produce, a child duplicated or gone), loaded into a live processor that is then asked to run
// a few blocks with a chord in. Loading must never throw, and a processor that has loaded
// anything must still save and reload.
struct StateVictim
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    KeysProcessor processor;
    juce::MemoryBlock base;
    juce::AudioBuffer<float> audio;

    StateVictim()
    {
        processor.prepareToPlay(48000.0, 512);
        audio.setSize(juce::jmax(1, processor.getTotalNumOutputChannels()), 512);
        processor.getStateInformation(base);
    }
};

StateVictim& victim()
{
    static StateVictim v;
    return v;
}

juce::var randomVar(juce::Random& r)
{
    switch (r.nextInt(9))
    {
        case 0: return juce::var(r.nextInt());
        case 1: return juce::var(pick(r, -40, 200));
        case 2: return juce::var((double) r.nextInt() * 1.0e6);
        case 3: return juce::var(std::numeric_limits<double>::quiet_NaN());
        case 4: return juce::var(std::numeric_limits<double>::infinity());
        case 5: return juce::var(juce::String());
        case 6: return juce::var(r.nextBool());
        case 7: return juce::var(juce::String::charToString((juce::juce_wchar) (1 + r.nextInt(0x2FFF))) + juce::String(r.nextInt()));
        default: return juce::var(juce::String(r.nextDouble() * 1000.0 - 500.0));
    }
}

void collectNodes(juce::ValueTree t, std::vector<juce::ValueTree>& out)
{
    out.push_back(t);
    for (int i = 0; i < t.getNumChildren(); ++i)
        collectNodes(t.getChild(i), out);
}

void fuzzState(juce::Random& r)
{
    auto& v = victim();
    juce::MemoryBlock blob;

    if (r.nextInt(3) == 0)
    {
        // Byte-level damage.
        blob = v.base;
        switch (r.nextInt(3))
        {
            case 0:
                for (int i = pick(r, 1, 8); i > 0 && blob.getSize() > 0; --i)
                    ((juce::uint8*) blob.getData())[r.nextInt((int) blob.getSize())] ^= (juce::uint8) (1 << r.nextInt(8));
                break;
            case 1:
                blob.setSize((size_t) r.nextInt((int) blob.getSize() + 1));
                break;
            default:
                for (int i = pick(r, 1, 64); i > 0; --i)
                {
                    const auto byte = (juce::uint8) r.nextInt(256);
                    blob.append(&byte, 1);
                }
                break;
        }
    }
    else
    {
        // Tree-level damage.
        auto xml = juce::AudioProcessor::getXmlFromBinary(v.base.getData(), (int) v.base.getSize());
        check(xml != nullptr, "the processor's own state does not read back as XML");
        auto root = juce::ValueTree::fromXml(*xml);
        std::vector<juce::ValueTree> nodes;
        collectNodes(root, nodes);
        for (int edits = pick(r, 1, 6); edits > 0; --edits)
        {
            auto node = nodes[(size_t) r.nextInt((int) nodes.size())];
            switch (r.nextInt(5))
            {
                case 0:
                case 1:
                    if (node.getNumProperties() > 0)
                        node.setProperty(node.getPropertyName(r.nextInt(node.getNumProperties())), randomVar(r), nullptr);
                    break;
                case 2:
                    if (node.getNumChildren() > 0)
                        node.removeChild(r.nextInt(node.getNumChildren()), nullptr);
                    break;
                case 3:
                    if (node.getNumChildren() > 0)
                        node.appendChild(node.getChild(r.nextInt(node.getNumChildren())).createCopy(), nullptr);
                    break;
                default:
                    if (node.getNumProperties() > 0)
                        node.removeProperty(node.getPropertyName(r.nextInt(node.getNumProperties())), nullptr);
                    break;
            }
        }
        if (auto edited = root.createXml())
            juce::AudioProcessor::copyXmlToBinary(*edited, blob);
    }

    v.processor.setStateInformation(blob.getData(), (int) blob.getSize());

    // Then play it: a chord into the track and into every line, a few blocks, release.
    for (int line = 0; line <= KeysProcessor::numArpLines; ++line)
        for (int i = pick(r, 0, 4); i > 0; --i)
            v.processor.noteOn(r.nextInt(128), 0.05f + 0.95f * r.nextFloat(), 0.0, 0, line);
    for (int b = 0; b < 4; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, pick(r, 36, 84), 0.8f), 0);
        v.audio.clear();
        v.processor.processBlock(v.audio, midi);
    }
    v.processor.allNotesOff();
    for (int b = 0; b < 2; ++b)
    {
        juce::MidiBuffer midi;
        v.audio.clear();
        v.processor.processBlock(v.audio, midi);
    }

    // A processor that has loaded anything must still save, and the save must load.
    juce::MemoryBlock again;
    v.processor.getStateInformation(again);
    check(again.getSize() > 0, "getStateInformation wrote nothing after a damaged load");
    v.processor.setStateInformation(again.getData(), (int) again.getSize());
    for (auto* param : v.processor.getParameters())
        check(std::isfinite(param->getValue()), "parameter " + param->getName(64) + " is not finite after a damaged load");

    // Back to a clean slate, so the cases stay independent of each other.
    v.processor.setStateInformation(v.base.getData(), (int) v.base.getSize());
}

// ---------------------------------------------------------------------------------------------
struct Target
{
    const char* name;
    std::function<void(juce::Random&)> run;
    double weight; // relative cost; the iteration budget is scaled down by it
};

const std::vector<Target>& targets()
{
    static const std::vector<Target> list {
        { "chordsym", fuzzChordSym, 1.0 },
        { "detect", fuzzDetect, 1.0 },
        { "notemath", fuzzNoteMath, 1.0 },
        { "euclid", fuzzEuclid, 1.0 },
        { "chordgen", fuzzChordGen, 1.0 },
        { "arp", fuzzArp, 8.0 },
        { "state", fuzzState, 40.0 },
    };
    return list;
}

void usage()
{
    std::cout << "Keys_fuzz [--seed N] [--iterations N] [--target NAME] [--case N]\n"
                 "  --seed        the run's seed; every case derives its own from it (default 24301)\n"
                 "  --iterations  cases per target, before each target's cost scaling (default 300)\n"
                 "  --target      run one target: ";
    for (const auto& t : targets())
        std::cout << t.name << " ";
    std::cout << "\n  --case        run exactly that case of one target, the reproducer a failure prints\n";
}
} // namespace

int main(int argc, char** argv)
{
    juce::uint64 seed = 24301;
    int iterations = 300;
    juce::String only;
    int onlyCase = -1;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a(argv[i]);
        const auto next = [&]() -> juce::String { return i + 1 < argc ? juce::String(argv[++i]) : juce::String(); };
        if (a == "--seed") seed = (juce::uint64) next().getLargeIntValue();
        else if (a == "--iterations") iterations = next().getIntValue();
        else if (a == "--target") only = next();
        else if (a == "--case") onlyCase = next().getIntValue();
        else { usage(); return 2; }
    }

    std::cout << "Keys_fuzz seed=" << seed << " iterations=" << iterations << std::endl;
    int failures = 0;
    juce::int64 totalCases = 0;
    for (size_t ti = 0; ti < targets().size(); ++ti)
    {
        const auto& t = targets()[ti];
        if (only.isNotEmpty() && only != t.name)
            continue;
        const int cases = onlyCase >= 0 ? 1 : juce::jmax(1, (int) std::lround(iterations / t.weight));
        const auto started = juce::Time::getMillisecondCounterHiRes();
        bool failed = false;
        for (int c = 0; c < cases && ! failed; ++c)
        {
            const int caseIndex = onlyCase >= 0 ? onlyCase : c;
            juce::Random rng(mix(seed, ti, (juce::uint64) caseIndex));
            try
            {
                t.run(rng);
            }
            catch (const Fail& f)
            {
                failed = true;
                ++failures;
                std::cout << "FAIL  " << t.name << " case " << caseIndex << ": " << f.what() << "\n"
                          << "      reproduce: Keys_fuzz --target " << t.name << " --seed " << seed
                          << " --case " << caseIndex << std::endl;
            }
            catch (const std::exception& ex)
            {
                failed = true;
                ++failures;
                std::cout << "FAIL  " << t.name << " case " << caseIndex << ": threw " << ex.what() << "\n"
                          << "      reproduce: Keys_fuzz --target " << t.name << " --seed " << seed
                          << " --case " << caseIndex << std::endl;
            }
            ++totalCases;
        }
        const auto ms = juce::Time::getMillisecondCounterHiRes() - started;
        std::cout << (failed ? "fail  " : "ok    ") << t.name << "  " << cases << " cases, "
                  << juce::String(ms / 1000.0, 1) << " s" << std::endl;
    }
    std::cout << (failures == 0 ? "No invariant broke." : "Targets failed: " + juce::String(failures).toStdString())
              << "  (" << totalCases << " cases)" << std::endl;
    return failures == 0 ? 0 : 1;
}
