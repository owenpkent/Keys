# Feeding a sequence of chords to an arpeggiator

**Status: proposed, unbuilt. 2026-08-21.** Owen: *"for arpeggiators if I wanted to feed in a
sequence of chords for it to play. How would we do that?"*

This is the research and the options, not a decision. Nothing here is built beyond what section
1 says is already shipping.

---

## 1. The finding: Keys already does this three times

Before designing anything, the honest answer is that the playback machinery exists, at three
different time scales, and two of the three are undocumented outside the code.

### Chain, at the bar

Each line's twelve slots hold a chord, a pattern, a shape, a rate and a bar count
(`ArpPattern::bars`, 1..16, [PluginProcessor.h:624](../src/PluginProcessor.h#L624)).
`startChain` ([PluginProcessor.cpp:2766](../src/PluginProcessor.cpp#L2766)) walks the slots that
hold a chord, gives each its bars, and launches each in turn; slots with no chord are skipped,
because a pattern-only slot is a place to keep a rhythm rather than a step of a progression. The
bar count runs on the audio thread and the launch on the 50 Hz heartbeat, with an epoch counter
so the chain cannot drift. Per line, so four progressions can run against each other at four
rates. Written up at [ARP_DESIGN.md](ARP_DESIGN.md), *Progression mode: the chain*.

**What nobody else has.** Because a slot carries its pattern as well as its chord, a Keys chain
can change the *rhythm* on the chord change. Scaler, Cthulhu and Ripchord all hold the pattern
still and swap the harmony under it. This is the feature Keys should be selling, and today it is
reachable only by building the slots one at a time.

### The Chord lane, at the step

Lane value 0..12; nonzero means "this step plays the chord in that slot instead of a note of the
held set" ([ArpEngine.h:1681-1694](../src/ArpEngine.h#L1681-L1694)). Draw Cm on steps 1-4 and Ab
on 5-8 and the progression moves inside the pattern rather than on bar lines. It is the only
part of the engine that reads anything outside itself, through the `ChordTable` atomic mirror
([ArpEngine.h:337](../src/ArpEngine.h#L337)), which is why *the call sites of
`syncArpChordTable` are the contract*.

### A clip on the track, at the host's timeline

Least known of the three, and it is the idiom the rest of the industry is built on. A line with
**Play** on lifts note events out of the incoming stream and arpeggiates them, and that stream
is "the keybed **and a clip on the track**"
([PluginProcessor.cpp:1854](../src/PluginProcessor.cpp#L1854)). So a chord progression drawn into
an Ableton clip on the Keys track already drives the arp, with the host owning the timeline,
the durations and the loop. No feature was ever written for this; it falls out of the routing.

| Route | Granularity | Who owns the timeline | Can change pattern per chord |
|---|---|---|---|
| Chain | 1..16 bars per chord | Keys | **yes** |
| Chord lane | one step, up to 32 per lane | Keys | no (it is one pattern) |
| Clip on the track | anything the host can draw | the DAW | no |

The gap between them is real and worth naming: Chain cannot change chord on beat three, and the
Chord lane cannot run a progression longer than one pattern. Two bars of Cm followed by two
beats of Ab is awkward in both.

---

## 2. What everyone else does

Four idioms. Keys sits inside three of them already.

### The trigger note

One MIDI note calls up one chord; the progression is therefore a clip of single notes on the
host timeline, and the arp downstream is stateless. This is **Cthulhu** (its chord pads are
played by incoming notes, and the manual's own pitch is "creating and reworking chord
progressions with single-note presses", p2), **Ripchord**, **InstaChord**, and Kirnu Cream's
**Trigger CM** (p6: *"chords can be played by using keyboard keys. The first CM slot can be
played using the first key from Track Key Area"*).

It is the cheapest possible design because the DAW already has a timeline editor, and it is why
none of these products built a progression editor of their own. Keys reaches this today only
through the clip route above, which passes the *whole chord* rather than a trigger note. A
trigger-note mode (one pitch per pad, or per slot) is a genuinely missing feature and is
option E below.

### The interval stamp

**Kirnu Cream** stores its sixteen chord memories as *intervals only*, and the incoming note
supplies the root (p13-14): *"If chord memory slot has Major triad chord the actually played
chord is determined by the input note... You can think the chord as a stamp where the base note
is the handle. Wherever you put the handle (that is play one note) your chord is copied there."*
Its per-step `CHRDMEM` lane then picks which stamp each step uses.

Keys' Chord lane is the same lane, but its memories are **absolute chords**, not stamps. That is
a deliberate difference, not an oversight, because Keys' slots are filled from the chord pads
and the library, both of which deal in real chords with real voicings. But it costs something:
a Keys chain transposed to another key needs every slot rewritten, where Kirnu's needs one note
moved. Worth knowing before anyone proposes a "transpose the chain" button.

### The progression as a first-class object

**Scaler 3** and **Captain Chords** hold the progression as a timeline object with per-chord
lengths, then play *patterns* over it: Scaler's Performance panel and Motions library apply
arpeggios, basslines and melodies across the progression, and several instrument tracks can bind
to one progression so they all move together. Cubase's chord track is the same idea at the DAW
level.

This is the most powerful model and the most expensive. Keys' Chain is already a cut-down
version of it, one where the twelve slots *are* the timeline and a bar count is the length.

### The track with a MIDI effect chain

**Hapax** and **Ableton** put the notes on a track and the arp in a chain of MIDI effects after
it. The arp is deliberately dumb; the progression is upstream. This is exactly what the clip
route makes Keys today, and it is why the clip route works with no code.

---

## 3. What is actually missing

Not the playback. The **loading**, and the granularity gap.

1. **Loading a progression into the slots is one gesture per chord.** A four-chord progression is
   four drags onto four slot cards, plus a click-and-step on each slot whose length is not one
   bar. Meanwhile [ChordLibrary.h](../src/ChordLibrary.h) holds 355 progressions that already
   know their own chords *and their order*, and a library row's two destinations are **Tray** and
   **Pads** ([ChordLibraryPanel.cpp](../src/ui/ChordLibraryPanel.cpp)) - neither of which is a
   slot.

   **The gap narrowed on 2026-08-26 and did not close.** Every arp target takes a chord from any
   surface now, the generator's tray and its reference box included, so a library row can reach a
   slot by way of the tray without ever touching a pad. What that removes is the *detour*, not the
   count: it is still one drag per chord, and the order a row already knows is still supplied by
   hand, one card at a time, in the right sequence. The table full of progressions can reach the
   mechanism built to play them; it cannot yet tell it what the order was.
2. **The Chord lane's slot numbers are invisible.** A cell reading "3" is an index into a slot
   the Draw page does not show, which is the identical complaint that got the Note lane its note
   names on 2026-08-18. Whatever else is built, this lane needs to say `Cm` rather than `3`.
3. **Nothing spans the bar/step gap.** See the table in section 1.

---

## 4. The options

### A. A **To arp** button (the loading gesture)

A third button on every library row, beside Tray and Pads, plus the same action from the
pad strip: lay the progression across the current line's slots in order, one chord per slot,
starting at the first empty one, then optionally start the Chain.

- **Reuses**: `setArpSlotChord`, `syncArpChordTable`, `startChain`, and the existing undo
  snapshot of `arpToTree()`, so the whole thing is undoable the moment it lands.
- **Costs**: one method on the processor, one button per surface, one greying rule (no line
  selected, or no free slots).
- **Risks**: it must never overwrite a slot that already holds a chord, the same rule
  `sendChordToFirstEmptyPad` follows, or a one-click convenience becomes a way to lose work.
- **Leaves alone**: everything else. Chord changes stay on bar lines.

### B. Fill the Chord lane

Load the progression into the slots as in A, then also write the Chord lane so the chords change
inside the pattern. Needs a rule for how four chords divide a sixteen-step lane, and an answer
for what happens when they do not divide evenly.

- **Buys**: chord changes on any step, which is the half Chain cannot do.
- **Costs**: the division rule is a real design question, not a detail, and it wants the lane to
  show chord names (item 2 above) before it is usable at all.
- **Risk**: a lane written by a button is a lane the user did not draw, and `laneRand` is
  documented as the only randomness allowed to change a note you drew. This does not break that
  rule, but it is the first time anything writes a lane on the user's behalf, so it needs to be
  one undo entry and it needs to be obvious that it happened.

### C. Both, A first

Ship A, live with it, then decide whether B is still wanted. Nothing in A is thrown away by B.

### D. A progression track of its own

Scaler's model: a new timeline object with chords and lengths, independent of the slots.

- **Buys**: arbitrary chord lengths, so the bar/step gap closes properly.
- **Costs**: a third mechanism doing Chain's job, a new editor surface on a window that is
  already at its height budget, and a second thing that can hold a chord into a line, which is
  the exact shape of the bug `noteRefs` and the per-line queues exist to prevent.
- **Verdict**: worth talking you out of until A has been used for a while. Chain already handles
  bars, per-line rates and drift-free timing, and it does the one thing Scaler cannot.

### E. A trigger-note mode

Map each of a line's twelve slots to a pitch, so a clip of single notes on the Keys track walks
the progression, the Cthulhu/Ripchord idiom. The DAW owns the timeline, which closes the
granularity gap for free.

- **Buys**: arbitrary chord lengths with no editor to build, and it is what every producer
  coming from Cthulhu already expects.
- **Costs**: a pitch range to reserve and a rule for what happens to those notes (they must be
  consumed, not passed through), plus an on-screen way to say which range is armed. It is also
  the least mouse-only option on this page: its whole point is that the *DAW* drives it, so it
  is worth less at the Keys window than to a keyboard player.

---

## 5. Recommendation

**A, then re-ask.** It is small, it reuses everything, it makes the one feature Keys has that
nobody else does reachable in a click, and it is the only option on the list that cannot be
wrong: whatever gets built later, a progression still has to get into the slots somehow.

Item 2 of section 3, chord names in the Chord lane, is worth doing at the same time regardless
of which option wins, on the same argument the Note lane's names were.

---

## 6. Open questions

1. Does **To arp** start the Chain, or only load it? Loading and leaving it stopped is the safer
   default and one more click.
2. What bar count does a loaded chord get? One bar each is the honest default; the library's
   rows carry no duration information.
3. Where do the chords go when the line's slots are not empty? First-empty-onward (safe, may
   scatter the progression) or first-twelve (contiguous, may overwrite). First-empty matches
   `sendChordToFirstEmptyPad`, so it should win unless there is a reason.
4. Should a chain step also install the library row's *name*, so the slot cards read as a
   progression the way the pads already bracket one?

---

## Sources

Manuals in `manuals/` (gitignored; see `manuals/README.md`):
Kirnu Cream, p6 (Trigger CM) and p13-14 (Chord memory, the interval stamp);
Cthulhu v1.1, p2 (single-note chord progressions) and p28 (Chord Mode).

Online, read 2026-08-21:
- [Scaler 3 product page](https://scalermusic.com/products/scaler-3/) and
  [Attack Magazine's Scaler 3 tutorial](https://www.attackmagazine.com/technique/tutorials/scaler-3-chart-ready-chords-and-compositions-for-any-skill-level/)
  (Performance panel, Motions, Chord Bind Mode, several tracks bound to one progression)
- [Xfer Cthulhu](https://xferrecords.com/products/cthulhu) and
  [ADSR's Cthulhu tutorial](https://www.adsrsounds.com/music-theory-tutorials/unlock-infinite-chord-progressions-with-cthulhu-vst/)
  (chord memorizer plus pattern arpeggiator, trigger-note workflow)
- [Ripchord](https://trackbout.com/ripchord/) and
  [Captain Chords](https://mixedinkey.com/captain-plugins/captain-chords/) (progression editors)
- [Squarp Hapax](https://squarp.net/hapax/) (chord mode; arp as one of eleven MIDI effects on a track)
- [AudioCipher, arpeggiator tips](https://www.audiocipher.com/post/arpeggiator) and
  [Sound On Sound, Ableton chord machines](https://www.soundonsound.com/techniques/ableton-live-chord-machines)
  (the standard DAW workflow: progression on the track, arp as a MIDI effect in front of it)
