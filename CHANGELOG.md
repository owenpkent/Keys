# Changelog

All notable changes to Keys are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/); versions are semver.

## [Unreleased]

### Added: a script can hand a chord to an arpeggiator line

`hold_arp_chord` and `release_arp_chord` over MCP. The entry below gave the *editor* every
route onto a line; this gives the same thing to anything driving Keys as a tool, and it calls
the same `holdArpChord`, so a drag and a script now arrive by one path.

**It closes a trap that reads as a broken plugin.** A chord *pad* cannot feed a line and never
could: `pressChordPad` fires through `fireChord` with `asChord` true, which on the track output
takes the queue a listening line cannot lift. That is correct - a pad is a chord you are
playing, not the input to a machine - but nothing said so, and `docs/MCP.md` asserted the
opposite in one sentence about Launch Quantize. So a script would set up two lines, write every
lane, press a pad, and get silence with **success on every call**. The only route in was
`play_notes` held open, which is not a hold: it expires, and re-arming is another call, so a
generative cue driven that way cannot be left running.

Either shape of chord goes in - `notes` directly, or `padSlot` to take a pad's (which lights its
card, same as the editor). One chord per line; a second call swaps it. `release_arp_chord` lets
one line go, or every line with `allLines`, and both mean **`releaseArpHold`** - the Hold off
chip, chain stopped and pending quantized launch dropped. The bare `releaseArpChord` is not
that, as `PluginProcessor.h` says at its own declaration: it leaves `chainOn` set, so the next
bar boundary hands the line another chord and the call reads as having done nothing.
`releaseArpHold` gained a per-line form so a script and the chip cannot mean different things
by "release".

**The reply reports the hold it actually took**, including `waitingForQuantize` and `lineOn`.
With Launch Quantize on the gesture waits for the next boundary, so the line is briefly holding
its *previous* chord rather than the one just sent; the flag asks whether **this call** deferred
(`arpLaunchPending`), not whether the parameter happens to be set. `lineOn` covers the other
half: B, C and D default to off, a line that is off still takes a chord in by design, and
without it a hold that worked and a line that will never sound look identical. Telling those
apart is most of the point - the failure this tool replaces was one where every reading looked
right.

**An out-of-range `line` is an error, not a clamp.** Every other argument in these two handlers
is rejected out of range, and a clamp is the one that cannot be noticed: `line: 7` would land
the chord on D and report success, which is the same silent-wrong-target failure the tool exists
to end. The older arp tools still clamp; that is a wider change, deliberately not half-done here.

`docs/MCP.md` also gains **"Which instance am I talking to?"**. Nothing in the API answers it -
no track name, no device index - and the shim picks by recency, so a script can build a whole
patch into a Keys on a track nobody is listening to and be told it succeeded at every step.
Found the hard way: six discovery files for one Live process, four answering tool calls, and the
routed instance among the two that did not. **A bound port is not evidence a Keys will answer,
and neither is a discovery file** - only a reply is.

### Fixed: the MCP pad-slot schema said 0..63 when there are 48 pads

Every `slot` / `padSlot` description advertised `0..63` with "page P slot S is `P*16+S`", both
left over from before the strip went to twelve pads a page on 2026-08-03. The validator has been
rejecting anything past 47 the whole time, so the schema disagreed with its own error message,
and a client computing a slot the documented way landed on the wrong pad for every page but the
first. The strings are built from `padsPerPage` / `numPadPages` / `numChordPads` now, so there is
no second copy of the number to go stale. `docs/MCP.md` also stopped telling readers to set
`padChannel` when pads are silent: it is retained for session compatibility only and is read by
nothing, and pads go out on the global MIDI Channel control like everything else Keys plays.

### Fixed: an instance could be alive, bound, and deaf

The two silent ports above were not stale files, which was the first and wrong theory. They were
live instances holding bound sockets and answering nothing, and the cause was in the kit's
transport: `Server::run()` served each connection **on the listener thread**, in a read that
blocks until that client disconnects. The shim holds a long-lived connection and goes idle
between calls by design, so **one idle shim owned a plugin's server forever** and every later
connect sat unaccepted in the OS backlog. A session came up with no Keys tools at all while Keys
ran fine, and a script built a whole patch into a reachable-but-unrouted instance and heard
silence, with success reported at every step.

Fixed in `okstudio-juce-kit`: one thread per connected client, capped, evicting the oldest rather
than refusing the newest. Tool bodies are still marshalled onto the message thread, so nothing
about handler serialization changes. The shim also quarantines a port that blows its timeout and
reconnects to another instance, since accepting a connection never meant answering one.

**The tests that would have caught it now exist**, and their absence had a clear shape: every MCP
test called `handleLine` directly, with "no socket, no start()", and the whole failure lived in
the transport those tests skipped. The kit gained its first socket-level coverage plus a shim
scenario for a deaf peer advertised newer than a healthy one. Full account in `docs/MCP.md` under
"An instance can be alive, bound, and deaf".


### Fixed: the chord you are holding can be dragged onto an arpeggiator line

Owen: *"I can't drag the held chord onto the arpeggiator."*

The **live chord card** at the left of the pad strip picked up and carried like any other card,
ghost and all, and then no arp target would take it. All four of them - a macro card, a slot
card, the panel itself, and the A/B/C/D switches on the arp bar - asked for a chord that came
from a *pad slot*, and then looked the chord up by that slot's index. The live card has no slot
by construction, so its drops landed on nothing and vanished. Nothing lit on the way, which is
why it read as the drag not working rather than as the drop being refused.

The arp takes a chord from anywhere now: the pad strip, the live card, the generator's audition
tray and its reference box. Every route was already carrying the whole chord in the drag - the
targets were turning it away for not carrying an index as well.

**A tray candidate dropped on a line is copied, not spent.** Committing one to a *pad* empties
its cell, which is how you see what you have taken and what gives Fill something to do; a line
is not storage, so a candidate that vanished into one would be unrecoverable. The reference box
was already a fixed point and stays one.

A line fed this way wears no pad letter, because no pad was involved - **Hold off** and **All
Off** are what stop it. A chord sent from a pad still marks its card, exactly as before.

### Added: Keep arp running, beside Play on the Pads bar

Owen: *"I wanna be able to hold the chord down to build it with my mouse, but then also to drag
a new chord onto the arpeggiator."*

Ticked (the default), pressing a card on the pad strip never releases a running arp line's held
chord. Lean on a pad to hear it, drag it up into the arpeggiator, and the lines that were
already going are still going when you get there.

This is the half of the story **Play** could not tell. Play decides whether the strip makes a
*sound*; what actually cut the lines off was the **choke** - with Chord Exclusive on, firing a
chord stopped every other chord source, arp holds included, and a press that turns out to be a
drag has already done that by the time the drag is recognised. The only way out was to turn Play
off and give up the sound as well, so hold-to-build and drag-into-the-arp were two settings you
had to keep swapping between. They are one now.

It narrows one gesture rather than the rule. **Dropping** a chord on a line still replaces that
line's chord, and Exclusive still chokes the pads and the live card from it: pressing a card is
playing a chord, and a line's held chord is not something you are playing, it is what the machine
is chewing. Untick this and Exclusive reaches the lines as it always did.

Nothing changes at all while Exclusive is off, which is its own default.

### Fixed: Scale Lock reaches the arpeggiator and its harmony voices

Owen: *"does the scale lock button at the top apply to arpeggiators and harmonies?"* It did not.

**Lock** was read in exactly one place - resolving a note at keybed press time - so it shaped
what you played into a line and nothing the line did afterwards. Root and Scale beside it have
always reached the engine (the Transpose lane counts scale degrees, Distance can stack in them,
and Stray's lower zone is defined by them); the *lock* was the piece that never arrived.

Every pitch a line emits is now snapped onto the scale while Lock is on. That is one rule rather
than five, because it lands where every emitted pitch already passes through: the direction walk,
Octave and Transpose, a chord called up by the Chord lane, the octave stacking, both harmony
voices and Stray's strays all go through it.

Two consequences worth knowing:

- **Harmony voices stop being chromatic while Lock is on.** The dropdown still names an exact
  interval, and with Lock off it still plays exactly that; locked, a voice that lands outside the
  key rounds into it, so "+ Minor 3rd" over a chord tone in a major key comes out as the third the
  key has.
- **Lock beats Stray.** Stray's upper zone exists to leave the scale, and locking the output is
  what the toggle says it does, so with Lock on those strays round back in and Stray's two zones
  read as one. Untick Lock to hear the wrong notes it exists to prevent.

Chord pads are untouched: a stored chord is a chord you built, and snapping it would silently
rewrite a borrowed one.

### Added: Clear page, on a pad's card menu

Owen: *"we need to be able to clear all the chords on a pad page."*

A **Clear page** row at the foot of a chord pad's right-click menu, in a group of its own behind
a separator: it is the one row on that menu about the page rather than the card it was opened
from. It empties every unlocked pad on the page you are looking at, in **one undo entry**, and
greys when there is nothing on the page a clear would take.

Keys had this until 2026-08-01 and lost it rather than rehoused it: the generator's window
stopped writing pads that day, and a wipe of sixteen cards with no undo behind it had no other
home anyone was happy with. Two things changed in the meantime. **Undo arrived** on 2026-08-14
and covers the pad tree, so the wipe is one click back. And the card menu is somewhere you go on
purpose, which is the property the window was standing in for - where the Pads bar, the other
candidate, would have put a page wipe 4 px from Regen and a few px from the page buttons, the two
things on that bar you click constantly. Owen picked the menu when asked.

**Locked pads are spared**, the same rule Regen follows: a lock is what says "not this one" to
anything that takes a page at a time. Clearing a single card is still Clear pad, or a drag off
the strip. If the pad linked to the keyboard is one of the cards going, the edit ends first -
left live, the next latched chord would write itself straight back into a slot you had just
emptied.

The wipe itself is `KeysProcessor::clearChordPadPage()`, on the processor rather than on the
generator's brain where the old one lived: it is data work on the pad table and nothing else,
which is what makes it testable without an editor.

### Changed: Strum, Humanize, VEL and H.TIME open with their ranges on

Owen: *"I want the default strum up, humanize, velocity, and H.TIME to have the range on and
enabled by default."*

Keys has four range knobs and all four opened dark, so a fresh instance played every chord
stamped out at one velocity, landing all at once, dead on the grid. The switch on three of them
is a lamp on the knob itself, which means the only way to find the feature was to already know
it was there. New defaults:

| Knob | Was | Now | What that plays |
| --- | --- | --- | --- |
| Strum (pads) | 0-0 ms, unlit | **30-80 ms**, direction Up | a rake with its own speed per chord |
| Humanize (pads) | off, 64-88 | **on**, 56-96 | velocity varies around the same 76 |
| VEL ring (arp) | 0 | **20** | hits land +/-20 either side of a level of 42, so 22-62 |
| H.TIME (arp) | 0 | **11**, ring already open | 0 to about 5 ms late |

The two arp rows read **18** and **24** for a few hours between this entry being written and
*the arp opens quieter and steadier* below, which is where the level moved 100 -> 42 and took
them with it. Nothing shipped on those numbers; this table lists what a new instance opens on.

Humanize's band was **widened around its centre, not moved**: Humanize *off* plays the band's
midpoint, and `migrateVelLevel` converts an old session's arp level against that same 76, so
both keep meaning exactly what they meant. Strum's direction is unchanged - "Up" has been the
default since the day it was a parameter.

These are default changes only. **A saved session stores every one of these parameters and keeps
whatever it said**; what moves is what a new instance opens on, and a session old enough to
predate one of the arp parameters, which takes the new default for it (lines B, C and D are off
by default, so in practice that is line A a few milliseconds behind the grid).

### Changed: run.py says what it just launched

Owen: *"update run py."*

It prints the binary's build time and the commit under it on every launch, and - the point of it -
warns in yellow when the exe is older than the source beside it, which is only reachable through
`--no-build`:

```
Launched Keys Host.exe (Release)
  built 16:43, main @ 7703f43
  "A page of chords clears from one row, and every range knob opens lit"
```

A day-old Keys Host was mistaken for a current one for most of an afternoon, and a stale build is
indistinguishable from a fresh one by looking at it. The stamp is the *working tree's* commit
rather than something baked into the exe: nothing records what a binary was built from, and a
stamp compiled in would mean a relink on every commit. The staleness check is what covers the gap.


### Fixed: the pad range knobs were fighting the hand that dragged them

Owen: *"feels like it's fighting me... when I drag the halo. is there a race condition."* There
was, and it had been mistaken for four different bugs before anyone asked that question.

`KeysEditor::timerCallback` pushed a span into Strum's and Humanize's knobs on every tick, beside
`syncPadRangeKnobs()` which already does that job - and it passed **`abs(hi - lo)`, the band's
full width**, to a control whose span is the reach on *each* side of the knob. So the band doubled
**thirty** times a second until it saturated against the nearer wall, and it did it while the
halo was under the hand. It is a leftover from before the band was centred on the knob
(2026-08-19), when the span really was the whole width reaching back from one end; that call site
was never updated, and the correct pull was added beside it rather than replacing it.

The arithmetic leaves a signature, which is what identified it: a saturated band is exactly
`[0, 2 x the knob]`. Strum was photographed reading **0-128 ms with its knob at 64**, and Humanize
**0-82 with its knob at 41**.

**Everything else chased that afternoon was this.** A halo that would not open, a knob that seemed
to drag its band about, a band that reached across the whole range from one small drag - one
arithmetic slip, seen from four angles. Two attempts to fix the *geometry* went in and came back
out again the same day, and the reason they are worth recording is that both were plausible and
both made things worse:

- Clipping each end at its own wall instead of keeping the band symmetric. It broke an invariant
  the pad knobs are built on: they are stored as nothing but their two ends and derive the knob as
  the **midpoint** of them, so symmetry is what makes that derivation exact. Clip one end and the
  midpoint slides off the knob - the knob crept under the halo, and the pointer sat outside the
  middle of its own lit arc, which is precisely what centring the band was for.
- Making the halo's ceiling track a wall, first the nearer and then the farther. Both made the
  same gesture worth a different amount depending on where the knob had been left.

`LayoutTests` runs the real editor's timer and checks that a band nobody is touching still draws
where its parameters say. **It has to watch what the knobs draw, not the parameters**:
`RangeKnob::setSpan` fires no callback, so a wrong span corrupts the picture and leaves the
parameters untouched - which is how this hid from the first version of that test. Reinstate the
three lines and it fails with Strum's stored 50-150 drawing as 0-200.

### Changed: every halo's sweep is band you can actually see

Strum's halo ran over the knob's whole 200 ms range, so a single drag threw the band across
everything the control can express. It is capped at **half the knob's travel** - 100 ms on Strum,
63 on Humanize - which is the widest a band centred on the knob can be, and the shape the arp's
VEL ring already had: `arpHumanVel` is a fixed 0-127 however the level beside it moves (Owen:
*"the arp have it right"*).

**The bound lives in `RangeKnob::spanMax()` now, not at a call site.** It shipped as
`setSpanMax((hi - lo) * 0.5)` passed by hand in `wireRange`, which fixed the pads and left the
arp's own two range knobs - which needed exactly the same reasoning - carrying the ceiling they
had before. At H.TIME's shipping default of 24 that was **228 px of a 300 px drag** writing a
parameter the ring could not draw, and about four fifths of VEL's sweep.

The half is arithmetic rather than taste: `room()` is the smaller of two distances that sum to
the face's whole travel, so it can never exceed half of it, and `reach()` stops at `room()`. A
ceiling above half is inert *by construction*, wherever the face is standing. **No band narrows**
- `reach()` was already bounded by `room()`; what changes is how far a gesture can wind the span
past the point where winding it stops meaning anything.

### Fixed: a halo parked at a rail wrote automation nobody could hear

At either end of a knob's travel there is no band at any setting: `room()` is zero, so `reach()`
is zero however far the span is wound. Dragging the halo there ran the gesture to completion
anyway - it moved the parameter and wrapped a `beginChangeGesture`/`endChangeGesture` pair round
it, so a host recorded a lane, while nothing changed on screen, in the readout or in the sound.

Keys reaches that state by an ordinary route rather than a corner one: H.TIME's face at 0 is
simply "no lateness". The guard existed until 2026-08-23 as one half of `min(spanMax(), room())`,
and taking the face out of the *ceiling* - which was right, and is the entry above - took it with
it. They are two questions now and two functions: `spanMax()` is how far the gesture reaches and
never reads the face; `haloIsLive()` is whether there is a band to open and has to. The stored
span survives untouched, so a step off the rail brings it straight back.

### Changed: the arp opens quieter and steadier

`arpVelLevel` 100 -> 42, and, adjusting the ranges-on entry above from within the same unreleased
round, `arpHumanVel` 18 -> 20 and `arpHumanize` 24 -> 11 (Owen: "want default arp settings").
H.TIME draws **0-22** and plays 0 to about 5 ms late where it drew 0-48 and played up to 12; VEL
draws **22-62**. The table in that entry lists the three as they ship.

The three are one decision. Humanize Velocity's reach stops at `min(level, 127 - level)`, so a
level of 100 capped its own ring at +/-27 however far the ring was wound; at 42 the ring reaches
+/-42. Lowering the level is what gives it somewhere to go.

**Defaults only.** A saved session stores all three and keeps what it said, and one old enough to
predate `arpVelLevel` does not take the default at all - `migrateVelLevel` computes a level that
plays it at the loudness it was saved at. The pads' Humanize band is untouched, so the 76 midpoint
that migration converts against still means what it meant.

### Fixed: a harmony voice was as loud as it liked

The Humanize Velocity draw was made per *emitted* hit, inside the ratchet loop, and by then a
harmony voice is an ordinary hit - so every voice rolled its own number and could land up to
`2 * humanVel` from the note it was thickening. At the shipping defaults that is the full width of
the band: a voice at 62 against the note it thickens at 22, a second player rather than a
thickening of the first. Wind `humanVel` up against a level near the middle and the gap is 2 x 60,
most of the MIDI range.

`ArpEngine::Hit` carries `src` now, the index of the hit it harmonises or its own, and the draw is
made once per source hit and read by its voices. It stays **inside** the ratchet loop on purpose,
so each repeat of a ratcheted step still draws afresh - what is shared is a hit and its harmony
within one repeat, not a whole step flattened to one velocity.

This is `Hit::vel` doing a job again, and the dead field is why the bug was invisible: it had been
written at every call site and never read since `velLevel` replaced the incoming chord's velocity
(2026-08-18), and the harmony loop dutifully copied it - so the code read exactly as though a
voice already took its source's loudness. `addHit` no longer takes a velocity at all.

Timing is unchanged: a voice still takes its own H.TIME lateness draw and can flam against its
source. Same question one axis over, deliberately left alone.

### Fixed: the harmony fix above missed the Harmony lane

`Hit::src` reached the two *fixed* per-line harmony voices and stopped there. The Harmony **lane**'s
two modes - the chord tone above and the subharmonic below - still called `addHit` without naming a
source, so each stayed its own source and went on rolling its own Humanize Velocity draw: the same
bug, by the one route the fix did not cover. It was also inconsistent, since a fixed voice stacked
on a lane-harmony hit *did* inherit that hit's velocity, so within one step some voices shared and
some did not. `addHit` returns the index it wrote or found now, and the lane's voices name it.

Also from that review, none of it audible: `Hit` grew to 16 bytes when it gained `src` and the
comment budgeting the per-step memset still said 12; the assert meant to pin VEL's ring against
what its level allows compared a hardcoded 20 rather than reading the parameter, so raising that
default would have shipped a ring past its own ceiling with a green suite; VEL's documented 22-62
band gained the drawn-band test H.TIME already had; and the per-ratchet rule the round argued for
in three places gained the ratchet case that distinguishes it. The user guide and the two arp rows
in *every range knob opens lit* above still quoted the defaults as they read for a few hours
mid-round (VEL ring 18, H.TIME 24) rather than as they ship, and the claim that a stray voice could
arrive "at MIDI 105 against a source at 22" was taken from the test's exaggerated settings - at the
shipping defaults the reach clamp puts the whole band inside 22-62.

### Fixed: five things the round above got half-right

Found reviewing the entries below, and each is the same shape as the bug it sits beside - a guard
or a claim that stopped one step short of where it had to be.

- **The halo guard moved to where the gesture opens.** `haloIsLive()` gated the value write and
  not `beginSpanDrag`, so a press and release at a rail still fired
  `onSpanDragStart`/`onSpanDragEnd` with nothing between them - a `beginChangeGesture` and
  `endChangeGesture` pair on the ring's parameter, which in a host with the lane armed in Touch or
  Latch is a write. The dead halo did still write something; it wrote it into the automation lane
  instead of the parameter, which is the one place the entry below did not look. `wheelSpan` had
  refused to bracket a gesture it could not fulfil all along, and this is that rule on the other
  path.
- **`syncPadRangeKnobs()` sorts the pair.** Nothing orders `chordStrum`/`chordStrumMax` or
  `humanizeVelMin`/`humanizeVelMax` - `migrateStrumRange` says in as many words that a host or an
  MCP client may write max below min, and `baseVelocity01` sorts them before playing them. Handed
  them inverted, the new fixed-point guard could never be satisfied: the derived span goes
  negative, `setSpan` clamps it to zero, and the pull re-ran every tick while drawing a
  zero-width band over an engine spreading across the whole pair. The sorting comment had outlived
  the code it described and was sitting two hundred lines away in `timerCallback`.
- **`run.py` sees untracked files again, and says when it cannot see anything.** `git diff --quiet
  HEAD` compares tracked paths only, so a new source file added but not staged - the ordinary
  state halfway through a feature - stamped the tree as matching its commit. And `returncode == 1`
  filed exit 128 (no HEAD, not a work tree, a corrupt index) under "no differences", which is the
  same "a git that cannot answer reads as clean" fault the change was written to fix, one route
  over. Three states now, on one `status --porcelain`.
- **`RangeKnob`'s drawn-state cache holds what was written**, the normalised pair, rather than the
  ends it was derived from. The face's range is an input to that normalisation and not to the
  ends, so a range change was invisible to the cache - and invisible for good, since no later tick
  could see a difference either. `MacroRow` also re-clamps its rings after the `SliderAttachment`
  that gives each face its range, instead of before it.
- **The test that names the regression tests for it.** The per-face-position loop was left
  asserting `spanMax()` alone, which is `jmin(override, travel * 0.5)` - a pure function of the
  slider's range that cannot read the face, so the loop could not fail. Reintroducing
  `min(spanMax(), room())` in `spanFromDrag` would have kept the suite green at every off-centre
  face. The gestures are asked at five positions again, the inverted pair and the automation
  brackets are pinned, and `skipUpdateCheckForTest` is scoped so the network guard no longer
  depends on which test registered first.

### Fixed: two timer pulls that never stopped pulling

Neither was audible; both were work the editor did thirty times a second for the life of the
window, and each sat under a comment saying it did not. `run.py`'s git calls are counted with them
below because they were trimmed in the same pass, not because they are a timer pull.

- **`RangeKnob::refresh()` compares before it writes.** `strumKnob.refresh(); humanKnob.refresh();`
  ran unconditionally in `timerCallback`, and each call sets two component properties and asks two
  components to repaint - four repaint requests a tick across the pair, moved or not. It caches
  the drawn state now, the shape `MacroRow`'s `lastLineOn` already used for its scrim.
- **`syncPadRangeKnobs()` stops at the fixed point.** An odd-width pair has no exact
  representation here - the face snaps to whole units and the band is symmetric about it, so 30
  and 81 give a centre of 55.5, whose ends round back to 31 and 82 and never match what is stored.
  Written for it, the pull re-ran every tick for the rest of the session. Such a pair arrives from
  a host lane, MCP or a session file; it is pulled once now and recognised next time.
- **`run.py` asks git three things instead of four**, on a loop this project advertises at about
  a second for a no-op. The saving is `--format=%h%n%s`, which answers the sha and the subject in
  one call; the dirty check stays on `status --porcelain`, since it is the only one of the two
  candidates that can see an untracked file.

### Fixed: run.py's stale-build warning could not be cleared

It scanned `tests/` and all of `src/` whatever was being launched. Neither target compiles the
test suite, and plain Keys does not compile `src/host/` - so editing a test file left the exe
legitimately older than a source file, and the yellow *"this binary is older than 1 source file -
it is NOT what you just changed"* fired on every launch and **could not be cleared by rebuilding**.
A warning you cannot act on teaches you to ignore the one that matters.

It scans exactly what the launched target compiles now, and names the newest offender rather than
only counting them. Two smaller things beside it: the launch line's build-time stamp is guarded,
so an exe that goes unreadable between launching and printing can no longer report a failure with
the app open in front of you; and a git that cannot answer at all no longer reads as a clean tree,
saying `+?` instead of nothing.


### Fixed: a chord handed to an arp line is no longer raked

Found on the way to the defaults above and worth fixing on its own. A chord routed to a line
goes through `fireChord`, so Strum applied to it - but those notes go into that line's queue and
make **no sound of their own**, so the rake was inaudible by construction. All it did was stagger
when the engine learned each note, and at 30-80 ms that is most of a 1/16 at 120 bpm: the first
steps of a run would fire on half a chord. `dest > 0` takes no strum now, which is the rule the
Humanize velocity range has followed on that same path since 2026-08-02, for the same reason - a
line has its own feel controls, and its input is not the place to apply the strip's.


### Changed: Play on the pads means press-and-hold

Owen: *"When the play mode is checked on the pads, I want it to trigger as soon as you click on
it and stay held until you let go."*

With **Play** on, a chord pad fires the moment you press it and holds until you let go, so a stab
is short and a lean is long - which is most of what a pad is for. It used to fire on the *release*
for a fixed 800 ms, with press-and-hold available only as **Chord pads play while held** on the
settings gear.

**That tick is gone, because Play is what it did.** Two switches for one question is one switch
too many, and a control called Play should play for as long as you are playing it.

What the old default was protecting against is real, and is now Play's own job. Firing on the
press means a press that turns out to be a *drag* has already choked the other chord sources, and
with Exclusive on that reaches each arp line's held chord - which is exactly the report Play came
out of. The answer is to switch **Play off** while you are dragging cards into the arpeggiator,
which makes the strip drag-only and is the one gesture it was built for. Turning Exclusive off
alongside it costs the drag nothing at all.

**A drag no longer stops the chord**, and the drag threshold went from 6 px to 14. With the
press owning the note, cutting it on travel made the *length of a chord* depend on the hand
staying inside a small circle: a tremor ended the note and put a drag ghost under the cursor, so
a lean stopped for no visible reason. That is the one thing a surface driven by one mouse must
not require. The chord now runs to the release whatever the gesture turned into - dropping a card
ends it like any other release - and nothing is left ringing, since `mouseUp` and the destructor
both end the audition on every path.

**A right-click while a pad is sounding releases it.** That branch returns before the
`endAudition()` guard, which was harmless while the press was silent: now the popup takes the
mouse, the pending left mouse-up never arrives, and the chord would ring until the next left
press on the strip.

Sustain and Latch are untouched: the release still goes through `releaseChordPad` /
`releaseLiveChord`, so a pedalled chord keeps ringing exactly as before. The generator's audition
tray keeps its own 800 ms - a tray card is a candidate you are sampling, a pad is an instrument
you are playing, which was never the same question. With nothing on the strip on a clock any
more, `ChordPads` is no longer a `juce::Timer`.


### Added: each arp line lights the keyboard in its own colour

Owen: *"new branch for each arp to play different colors on the keyboard."*

**Light keys** has lit the keybed for the arp's output since it landed, and with four lines
running that was one colour blinking for all of them - you could see *that* the arp was playing
and not *which line*. The four line colours already existed and are already worn by the macro
cards, the bar's letter switches and the Draw grid's playhead; this is the same palette on the
one surface that was still answering in the singular. A is cyan, B magenta, C amber, D lime.

**Two lines on one key: the lower letter wins.** Not a blend - the palette's whole job is telling
lines apart, and cyan mixed with magenta is a fifth colour belonging to neither. The winner is
stable while the note is held, so a key never changes colour as lines come and go underneath it.

Notes from every other source - your own clicks, a latch, a chord pad, the MIDI input, an MCP
tool - are drawn exactly as before: **cyan**, whatever the theme swatch says, which is what
those keys have always been. Only the glow strokes around them follow the accent, as they
always did. A first cut derived their gradients from the theme accent instead, which quietly
changed the colour of your own presses on any non-default swatch.

The honest limit, since the palette is meant to *mean* something: on the default cyan swatch a
chord pad's key is the same colour as line A's, because line A **is** that cyan. B, C and D are
unambiguous. So a colour says "an arp line, or the keybed's own" rather than "an arp line".

Under it, `arpNoteOn` became `arpNoteLines`, a **bitmask per pitch with one bit per line**,
updated with `fetch_or` / `fetch_and` rather than a load-modify-store, because `clearArpNotes()`
writes zeroes from the *message* thread (All Off, a panic, the MCP tool) and a clear landing
between a read and its write-back would resurrect other lines' bits for a pitch whose engines
that same panic is about to flush - a key lit for the rest of the session. It
which is also a small fix: two lines sounding one pitch used to share a single flag, so whichever
released first put the key out while the other was still playing it. Each line clears only its
own bit now. Within a line it is still a flag rather than a count - a line's two harmony voices
on one pitch, first note-off wins - the same trade at a smaller scope, and still the reason this
is not a refcount.

The four cyan gradients the keybed hard-coded moved into `skin::keyLit` and its three companions,
which is where they should always have been: **cyan keeps its shipped values byte for byte** (the
keybed was tuned against them, and A is the line Keys has always had) and every other accent is
derived to sit in the same relationship.


### Fixed: the harmony dropdown's **Off** row was greyed out

Owen: *"I can't turn off the harmony. off is grey."*

`ComboBox::isItemEnabled` takes an item **ID**. `getItemText` and `getItemId` take an **index**.
The harmony popup - rebuilt by hand since 2026-08-19 to get its two columns - passed the loop
index into the one call of the three that wants an id.

The list is added with `addItemList(..., 1)`, so index 0 is id 1: every row was checked against
its *neighbour's* enabled flag. For twenty-six of the twenty-seven rows that is invisible, since
they are all enabled anyway. For the first it is not, because `isItemEnabled` answers **false**
for an id no item has - and index 0 is **Off**. So the single row you need in order to silence a
voice was the single row greyed out, which is why it read as a harmony that would not turn off
rather than as a bug in a popup.

**The loop itself moved to `src/ui/ComboMenu.h` and is shared**, because Keys hand-rolls a
ComboBox popup in two places and the other one - `StepComboBox` - carried a hard-coded `true`
where the enabled flag goes, which is the same silent lie one call over: `setItemEnabled(id,
false)` is the ordinary way to grey a row, and a row drawn enabled there would have been
clickable and fired its callback with the value the caller meant to forbid. One loop now, with
the index-versus-id rule written where a third popup would be read.

**The column break is derived from the semitones, not the label text.** Matching on a leading
`"+"` reads as data-driven and is not: the harmony table's own rule is that appending is the only
safe edit, and an appended *descending* interval lands after every ascending one, so no break
fires for it and it draws at the foot of the wrong column. `ArpTests` pins that the table stays
grouped, so an append that breaks the grouping fails a test rather than mis-columning quietly.

`LayoutTests` walks the **live** combos on a real panel - all eight of them - and pins every
row's own **item id**, its text and its enabled flag. The ids matter most: a first cut of this
test checked text, enablement and the break, and passed green against `addItem(i, ...)`, the
identical index-for-id slip one call over. The id is the value this bug class turns on.

The dropdown also opens with the current row highlighted now (`withInitiallySelectedItem`), which
every stock ComboBox does and this one had stopped doing.


### Fixed: one harmony table instead of three that must agree

`harmonyChoices()` and the two semitone tables were three parallel lists indexed by the same
number, each of the latter two carrying a `jassert` comparing its length against the first. That
is the shape CLAUDE.md already logs under `buildLaneRow` versus `laneRange` - three tables that
must agree is three tables that will not - and a comment naming the hazard does not remove it.
One table with a name and both intervals per row now, with `harmonyChoices()` built from it, so
appending an interval is a single edit that cannot be half done.

The jasserts went with it, and that is a fix rather than a loss: they called `harmonyChoices()`,
which builds a 27-entry StringArray of heap Strings, and `harmonySemisFor` is called from
`runArpLines` on the **audio thread** four times a line every block. A Debug build was allocating
sixteen StringArrays a block to check a drift that is now impossible by construction. Nothing may
allocate on that thread.

### Fixed: the per-note stray salt cancelled the hash's own avalanche constant

The hit-index salt added the same day multiplied by `2654435761`, which is `0x9E3779B1` - one bit
away from the `0x9e3779b9` XORed in on the next line. At hit index 0, which is every shape except
Chord and so very nearly every step, the two collapsed to `0x8` and the fixed avalanche term the
expression was written to carry simply was not there. Decorrelation between voices still worked
and the notes were still in range, so nothing sounded wrong; the hash was just weaker than it
reads. Different multiplier. **Salts XORed against each other have to be checked, not just
chosen.**

### Fixed: "Octave & 5th" plays an octave and a fifth

Owen: *"in the harmony, when you select octave plus fifth, it looks like it only just does
octave."*

Every entry in the harmony dropdown names a single interval except one, and that one says
**&**. `harmonySemisFor` read it as a compound interval - a single note 19 semitones up -
so the entry played one note where its name promises two, and that one note was neither of
the two it names. The ampersand had been the promise all along.

A voice may now carry a second interval (`Params::harmSemisB`, `harmonySemisSecondFor`),
and "+ Octave & 5th" is the only entry that has one: **12 and 19** - the octave, and the
fifth above it. It stays **one voice**, so both pitches sit inside its slot's single chance
roll and either both fire or neither - a slot that half-fired would be the same bug wearing
the chance knob. Every other entry is untouched, which a sweep in `StateTests` now pins,
along with the rule that no entry past Off may name nothing.

The fifth is the one *above* the octave rather than the one below it, and the list itself is
the argument: it is a single ascending ramp from "- Octave" to "+ 2 Octaves", and this entry
sits between "+ Octave" and "+ 2 Octaves". Spelled 12-and-7 it would have reached *lower*
than the plain "+ Octave" directly above it, so reading down the list would have made the
shimmer go down - and the pair would have been an exact duplicate of two rows the list
already has. `StateTests` now checks that ramp across the whole list rather than spot-checking
this one entry, since a spot check only ever repeats whatever the table currently says.

### Added: a dice on each arp card, and a Shape dropdown that knows when to stop

Owen: *"the shape of the arpeggiator drop down doesn't need to be so big. Make it smaller.
And I use the random ones a lot, and I'd like to have a dice button when those are active
nearby to regenerate their pattern."*

- **The Shape combo is capped at 170 px.** It was the one elastic control on the card's top
  row, so on any window wider than the editor's floor it swallowed everything left over -
  about 540 px to hold "Fingered Bottom", the longest of its fifteen names. This is a
  ceiling, not a size, so above the floor the row simply stops giving it more. It is 48 px
  narrower than before at the floor itself, though, because the dice's cell and its gap come
  out of the same row - which is affordable only because the panel's minimum width is now
  set by the knob strip below, and that is wider than this line needs.
- **A dice sits beside it**, in part of the width that freed up. One click deals that line a
  new **Random Once** order. That shape shuffles the held chord once and then walks the
  shuffle, which is what makes it a pattern rather than a coin flip - and until now the only
  ways to deal it again were to change the chord or restart the line.
- **It greys outside Random Once.** Random and Random Other draw a fresh note every step and
  have no stored order for a dice to deal again, so a button that stayed lit on them would
  promise something it cannot do. Its cell is reserved on every shape, so nothing on the row
  moves as you page through them.
- The reroll is a counter the UI bumps and `runArpLines` matches, so every write to engine
  state stays on the audio thread, and each side writes only its own variable. Clicks
  arriving inside one audio block coalesce into a single reroll, which is correct rather than
  a loss: dealing twice before the next step is inaudible by construction.
- The dice reads the Shape combo to decide its greying, and the combo is now seeded from the
  line's own parameters when a card is built. It was not, so on a session saved in Random
  Once the dice opened greyed and only came live on the panel's next 10 Hz tick.

### Fixed: the arpeggiator's own window was too narrow for what is in it

The macro view puts two cards side by side, and a card's knob strip is the widest thing in
Keys for the window it occupies. The strip went to nine knobs when STRAY landed, and that
was measured against the docked editor - where a card column is about 614 px - but not
against the **detached** Arp window, whose 900 px minimum gives a column of 420 against a
strip that asks for 422. JUCE answers a row asking for more than it has by clamping, and the
shortfall all lands on the last cell, so H.TIME's face was drawn at 16 px with nothing on
screen to say why.

Two of the deep pages were already starving a control apiece at that width, before the ninth
knob arrived - a band slider on Play and a lane tab on Draw. So the window's floor is now
**asked for rather than written down**: `ArpPanel::minPanelWidth()` takes the larger of the
macro view's requirement, which is derived from the knob count and moves on its own when a
knob is added, and the deep pages', which is measured. `LayoutTests` sweeps every view at
that width, so the measured half cannot rot.

The general lesson, which is the part worth keeping: **a view that can be drawn in two
windows has two floors, and only the smaller one is ever tested by accident.**

**And two axes.** The first cut of that fix derived the width and left the height at the literal
300 it had always been, which is the same mistake one axis over: 38 px of title bar, 8 of border
and a section bar come off before the panel sees any of it, leaving about 216 px against the
macro view's 690. The second card row - lines C and D - laid out at zero height. It is asked for
now too, and it clears the **tallest** view rather than the one showing, because a window has one
floor and a view switched inside an already-minimised window has nowhere to grow into.

### Fixed: Stray moved the whole chord instead of straying one note

Stray's roll was hashed from the step alone, so under **Chord** shape - where one step sounds
several notes - every note of the step drew the same answer and shifted the same way. A held
C-E-G came out as a parallel D-F-A, which is the line changing key, not a step landing on a
note outside your chord. The roll is now salted with which note of the step it is, so each
strays on its own. **Lock is unaffected**: the cell is still (step, era) and the salt only
picks a voice inside it, so a transport jump lands on the same answer note for note.

### Added: a single note is a chord card

Owen: *"I also like to allow one note to show up in the chord pad and the chord preview."*

One line was refusing it. `ChordPads`' own `isChord` answered `notes.size() >= 2`, and it
gated three things at once: the live card **named** a held note only from two up (one key
down read "hold a chord"), the card could not be **pressed**, and - because an empty card is
not draggable - a single note could not be **carried onto a pad** at all.

Nothing downstream ever needed two. `chords::detect` already names a lone pitch class by its
note name, `applyInversion` and `applySpread` both return a one-note chord unchanged,
`fitVoicing`'s shrink already guarded `want >= 1`, `setChordPad` has always stored whatever
it was handed, and the arp builds a one-entry sequence quite happily. It was a gate refusing
something the rest of Keys could already do.

- The predicate split in two, because it was asking two questions under one name. **`hasNotes`**
  - is there anything here to show, play or drag - is what the live card, its press and its drag
  now use. **`canRevoice`** keeps the real two-note requirement, and is what the **Next voicing**
  row greys on: a voicing moves notes about within a chord, and one note has nowhere to go. A
  one-note pad is legal; that row just has nothing to do with it.
- The generator's **Notes** range now starts at **1** rather than 2, so single notes can be asked
  for deliberately - bass lines, pedal tones, single-note stabs. Widening the bottom of an int
  parameter is safe for *sessions* where reordering a choice list is not: every value a saved
  session could hold is still in range and still means what it said, and the 3/4 defaults are
  untouched, so there is no migration.

  **Host automation is the exception, and it is worth knowing before you reopen an old set.** A
  DAW stores automation normalised rather than denormalised, so a lane written against 2..11 is
  read back against 1..11 and lands about one note lower - a point recorded at 3 was 0.111, which
  now denormalises to 2. Nothing here can tell an old lane from a new one, so it cannot be
  migrated the way a session can. If you have automated **Notes Min** or **Notes Max** in a Live
  set, check those lanes after updating.
- **An unticked Notes range rolls 1..11 too**, so the tray turns up the odd single note among its
  twelve candidates. This went the other way first, on the reading that a bare note would read as
  the tray having failed; Owen's call, and the right one - a tray is candidates you sample and drag
  the good ones out of, so a page with the odd pedal tone in it is a better spread than a page with
  none. It also restores the rule that was there before: **an unticked gate rolls the whole range
  its parameter can express**, never a hand-picked sub-range, or the tick box stops meaning "you
  decide" and starts meaning "you decide, within limits nobody wrote down".

### Changed: Mutate stays inside your chord, and the strays get a knob of their own

Owen: *"the mutate doesn't really work the way I want ... it's adding additional notes in
the arpeggiator ... it should just change the existing ones."*

Mutate had carried two stages on one dial since 2026-08-19. Below halfway it moved the run
to a **different note of the chord you are holding**; above halfway a second stage took the
pitch that walk had chosen and shoved it **off the chord** - an in-scale neighbour first,
then chromatic semitones near the top. Nothing was ever *added*: a step fires exactly as
many notes at Mutate 100 as at 0, and `ArpTests.cpp` now pins that outright. What was
arriving was pitches from no chord anyone played, which is what "additional notes" sounds
like from the listening chair.

The two halves were answering different questions - *explore this chord harder* and *leave
this chord* - and one dial could not offer the first without eventually forcing the second.
So they are two controls now:

- **MUTATE** means one thing across its whole travel again: how often a step swaps for
  another note of the held chord, and how far along that chord it reaches to find one. It
  **cannot leave the chord at any setting**, which is the 2026-08-18 promise restored and
  extended to the top of the dial. Its reach now grows the whole way up (one chord entry at
  the bottom, four at the top) instead of widening only past 50, so the upper half buys
  something rather than spending itself on strays. A saved session at Mutate 40 explores a
  little further than it used to and still cannot play a note outside its chord.
- **STRAY**, new, is the ninth knob on each macro card, sitting between MUTATE and LOCK so
  the three read as one sentence: how hard it explores, how far outside it may go, how long
  it keeps what it finds. **Zero is off and is the default**, so nothing that predates this
  can acquire a note it was not already playing, and off is now a position you can *stay* at
  while turning Mutate all the way up. Its own two zones: to 50 a stray is an in-scale
  neighbour a degree or two away, past 50 a growing share are chromatic, all of them at 100.
- **LOCK** is untouched and still holds both, because both stages hash the same (step, era)
  cell. A wrong-note lick the machine finds still hardens into the part.

`arpStray` appended per line (and `arp2Stray`, `arp3Stray`, `arp4Stray`), default 0.

**A migration, and it needs one for a two-day window rather than for the usual reason.** For
every session saved before 2026-08-19 the default is the whole repair: Mutate could not leave
the chord then either, so off reproduces what those sessions played. But between 2026-08-19
and this change Mutate's *upper half* was this feature, so a session saved in those two days
holds a Mutate above 50 that meant "and leave the chord" and a Stray that is simply absent -
and 0 would open it strictly in-chord, a different part, with nothing on screen to say why.
`migrateStray` folds that forward: `(mutate - 50) * 2`, which maps the old in-scale/chromatic
boundary at 75 exactly onto Stray's own at 50. `apHarm1` is what dates the session, since the
harmony voices landed in the same 2026-08-19 round - Harm1 present with Stray absent is that
window and nothing else, so sessions older than it are left at the default they want.

The macro card's knob strip is **nine**, which is the width the 38 px knob floor was chosen to
survive: nine knobs plus the two rings and eight gaps is 422 px, so every knob clears the 34 px
mouse-only floor in the docked editor. Note that is the *docked* case only - the detached Arp
window is narrower and was not re-measured here.
### Documented: three ways to feed an arpeggiator line a sequence of chords

Owen: *"for arpeggiators if I wanted to feed in a sequence of chords for it to play. How
would we do that?"*

No behaviour changed. It turns out Keys already does this three times over, and only one of
the three was written down: **Chain** walks the twelve slots a bar at a time, the **Chord**
lane swaps a slot's chord in for a single step, and a line with **Play** on arpeggiates the
chords in an ordinary MIDI clip on the Keys track, letting the DAW own the durations. That
last one was never a feature, it falls out of the routing, and it is the only one of the
three that can hold one chord for two bars and the next for two beats.

The user guide gains a section covering all three and saying which to reach for.
`docs/CHORD_SEQUENCE.md` is the research behind it: what Scaler, Cthulhu, Ripchord,
InstaChord, Captain Chords and Hapax each do instead, why Kirnu Cream's chord memories store
intervals rather than chords ("the chord is a stamp where the base note is the handle") and
what that would cost Keys to copy, and five options for the thing that *is* missing, which is
a way to load a progression into the slots in fewer than one drag per chord. Proposed and
unbuilt, awaiting a pick.

Worth naming while it is being written down: because a slot remembers its pattern, shape and
rate alongside its chord, a Keys chain can change the **rhythm** on the chord change. Every
other plug-in in that list holds the pattern still and swaps the harmony under it.

## [0.2.1] - 2026-08-20

### Added: Keys has a logo

Owen: *"need logo"*, then, shown twelve marks, *"10"*.

**Three white keys, two black keys between them, and the middle one lit.** Keys is the
played keyboard of the OK Studio line - you click a key and it sounds - so the mark is that
gesture rather than the initial. It is drawn entirely from `src/ui/KeysLookAndFeel.h` -
obsidian ground, ivory key faces, the one cyan - so the mark and the plugin are the same
object rather than two things that happen to ship together.

A letter **K** was drawn first, in eight variations, and abandoned. It said *Keys* and
nothing about a piano; the "white key" its stem was documented as was a rounded rectangle
that nobody would ever read as one; and below 32 px it came out as a lowercase k.

`assets/Keys.svg` is the master. `installer/generate_brand.py` draws the same six shapes
with Pillow and emits everything else: `assets/Keys.ico` (16 through 256), the two Inno
wizard bitmaps, and `assets/logo-1024.png`. **All of it is committed**, so a build - and CI -
never needs Pillow; the script runs only when the mark itself changes.

Three things the drawing had to get right, and only the first is something a vector renderer
would have done for free. **Each icon frame is drawn at its own size** rather than
downsampled from one 256, because a resized 256 goes muddy at 16. **Keys are square at the
shoulder and round at the foot** - Pillow rounds four corners or none, so the top pair is
squared off by overdrawing - and that single detail is what separates a keyboard from three
rounded bars. And **the block fills about 79% of the tile's width**, which is a legibility
floor rather than a taste: drawn at half the width, three keys and two gaps across a 16 px
icon put every gap under half a pixel and the keys blurred into one bar.

### Added: release.py, because signing cannot be automated from here

The EV certificate lives on the SafeNet token, and once its cached PIN expires the driver
puts up a **GUI prompt**. A build launched by an agent runs non-interactively, so that prompt
is never shown and signtool returns `ERROR_CANCELLED` (`0x800704c7`) at once - which reads
exactly like a cancelled PIN and is not one. Double-clicking `release.py` runs the same
`build.ps1 -Installer` from a session that can show the dialog.

It also **verifies what came out**, because "signature valid" has already been misleading
once here: a correctly signed installer from an earlier build sat in `release/` looking
publishable while missing the branding entirely. It checks the file is fresh rather than
left over, that it is signed, that the binary's reported version matches `CMakeLists.txt`
(the JUCE `resources.rc` trap), and that the branding files exist at all.

### Fixed: the installer never asked where to put the plug-in

Owen, on the 0.2.0 installer: *"it didn't ask where to save the vst"*.

`DisableDirPage=yes` meant the wizard went Welcome, License, Ready, with no say in the
destination at all. That is defensible for a VST3, which has one canonical folder every host
scans, right up until a DAW is pointed somewhere else - and Ableton's **VST3 Plug-In Custom
Folder** is exactly that case, the one this machine has been using the whole time. An
installer that cannot reach the folder your host actually scans is one that installs a
plug-in you cannot load.

There is now a **Select VST3 folder** page, defaulting to `{commoncf64}\VST3` with Browse
alongside it, and the page's own text says to point Setup at a custom folder if the DAW uses
one.

**`{app}` changed meaning, and that is what makes this more than a one-line flag.** Until
0.2.0 `{app}` *was* the bundle (`...\VST3\Keys.vst3`); it is now the VST3 **folder**, with
`[Files]` appending `Keys.vst3` itself. So `UsePreviousAppDir` has to be **off**: Inno
remembers a previous install's `{app}` and would otherwise hand 0.2.0's remembered bundle
path to a rule that appends the bundle name again, installing to
`...\VST3\Keys.vst3\Keys.vst3` - a path no host looks in, from an installer that reported
success. `DirExistsWarning` is off for the same family of reason: the VST3 folder always
already exists, so the "install to an existing folder anyway?" prompt would fire on the
default path every time and train the user to click through it.

## [0.2.0] - 2026-08-20

### Added: a documented release pipeline, and the first build Keys has ever published

Keys had every part of a release except the release. `build.ps1 -Installer` already
produced a signed `release\KeysSetup-<version>.exe`, `installer/keys.iss` already took the
version from `CMakeLists.txt`, and the in-plugin updater was already hard-pinned to
`okstudio1/keys-releases` - but nothing had ever been tagged, nothing had ever been
published, and no document said how. 0.1.0 was cut in the changelog on 2026-07-14 and got
no further than a local installer.

`docs/RELEASE.md` is the checklist, mirroring alpha-osk's pipeline and Beatform's JUCE
adaptation of it. It leads with the four contracts rather than the steps, because **all
four fail silently**: renaming the releases repo orphans every installed copy (they report
"up to date" for ever instead of erroring), an asset not named exactly
`KeysSetup-<version>.exe` is ignored by the updater without complaint, a tag that is not
`v<version>` breaks the comparison against `KEYS_VERSION`, and a `gh release create`
missing `--repo okstudio1/keys-releases` cheerfully publishes to the *source* repo where no
updater will ever look. The one loud failure is publishing unsigned, since the updater's
verification is fail-closed at every gate.

`scripts/downloads.ps1` sums each release's asset download counts, alpha-osk's
`scripts/downloads.py` in PowerShell. The repo is hard-coded rather than a parameter: it is
the same pinned constant the updater, the checklist and this entry all carry, and a fifth
place to state it is a fifth place for it to drift.



### Added: the All view's bottom row of arps collapses to a strip

Owen: *"maybe you should be able to minimize bottom arps"*.

Four lines in a 2x2 grid is two card rows where it was one, and a macro card is 323 px, so the
All view alone was setting a **1349 px minimum window** - `applyLayout` passes `idealHeight()`
in as the resize *minimum*, and this view is the default. That is 43 px under a 1440p work area
and more than a 1080p screen has at all, which is the 2026-08-02 "the keyboard is cut off at the
bottom" failure waiting to happen: the keybed is laid out last, so every missing pixel comes off
it with nothing on screen to say why.

**Lines C D** on the arp bar collapses the bottom row to a 34 px strip, and the minimum falls to
**1060**. The strip names the lines it is standing in for, each in its own accent colour and
dimmed when that line is switched off, and the whole strip is a click target that opens the row
again - a big target, which is the point on a surface driven with one mouse.

**It folds the view, never the lines.** C and D keep their chords, their patterns and their
output while collapsed; this is the macro card's scrim rule read one level up. That is also why
the strip carries no On switches of its own: those are the letters at the other end of the same
bar, they stay reachable with the whole section folded, and a second control bound to one
parameter is exactly the mistake that deleted the macro card's own On toggle on 2026-08-02.

The toggle rides the bar rather than the panel for the standing reason - the bar is 34 px that
already exists, and spending 34 px inside the panel to buy back 289 would be absurd. It shows
only in the All view, where there is a row to fold, and hides with the section like All and the
page tabs, since it navigates a panel.

Two traps worth recording. The cards in a folded row are **hidden, not resized**: a card squeezed
into 34 px draws its knobs over one another and its controls still take the mouse. And
`setMacroView` has to hide the strip as well as the cards, because `resized()`'s whole macro block
sits behind `if (macroView)` and never runs in a deep view - so a folded bottom row went on
drawing its strip over the band. `LayoutTests` pins that the fold actually buys the height back
rather than hiding two cards inside a box that stayed the same size, and that unfolding returns
the height exactly.


### Fixed: review pass on the four-arps round

**Two harmony voices on the same interval hung a note for the rest of the session.** A duplicate
pitch inside one step is not a doubled attack: both hits land at the same sample offset, so the
second goes down `emitHit`'s tie branch, which writes a note-off at `on - 1` - *before* the first
one's note-on. `MidiBuffer` sorts by sample position, so `ArpMerge` saw off, on, on with only one
parked off, the refcount climbed to 2, and the real note-off took it to 1 rather than 0 and was
suppressed. The pitch was never released and stayed pinned above zero, so every later hit on it
hung too. Reachable without trying: both harmony dropdowns on one interval, or a + Perfect 5th
voice over a chord-lane triad, where C's fifth is the G already in the step. The harmony loop's
own guard only caught a copy that clamped onto its own source. `addHit` now refuses a duplicate
`{note, channel}` outright, which covers every route into a step at once, and `ArpTests` pins it
by walking the stream the way `ArpMerge` does and checking every note-on is released.

**Strum and Humanize repainted forever and read half a unit wrong.** Their faces snap to whole
units while the span stays continuous, so any halo drag storing a fractional reach into two
integer parameters left the derived ends exactly 0.5 from the stored pair - and the sync's
`< 0.5` test can never be satisfied by exactly 0.5. It re-pulled and repainted both knobs on
every editor tick for the life of the session, with the readout permanently disagreeing with the
parameters it mirrors. It compares what the ends round to now, still comparing the ends rather
than the raw span so a latent span held back by a rail is still preserved.

**The Pads bar's Play toggle disagreed with the session.** It reads `LayoutState`, which no
attachment drives, so loading a set saved with Play off left the button ticked while the strip
was actually drag-only - and the first click wrote the stale button state back over the loaded
value, making the control do the opposite of its face. Both it and Light keys are pulled on the
editor timer now, beside the range knobs, since a state restore has no hook of its own.

**The harmony dropdowns were 26 px targets**, under the 34 px floor that CLAUDE.md states as an
invariant with no exceptions. They are 34 now and the strip grew to fit them, which is the rule:
give the cell the height the target needs rather than shrinking the target into the cell.

**The mouse wheel on a range knob's halo fired a full step per event.** It tested only the sign of
`deltaY`, so a precision touchpad or a free-spinning wheel slammed the span from nothing to full
in one flick and wrote a begin/end gesture pair into the host for each of the dozens of sub-notch
events on the way. It scales by the delta (capped at one notch per event) and honours the OS's
natural-scrolling flag, without which the tooltip's "up is more" was wrong on that setting.

Also: `HarmonyBox::showPopup` passes `isItemEnabled(i)` rather than a hard-coded `true` and hands
the menu a standard item height, so ordinary `ComboBox` APIs keep working through the override;
and three documented budgets were re-measured against the code rather than assumed. The editor's
worst-case height is 1341, not 1223, and the macro view is now the tallest of them - it fits a
1440p work area and does not fit a 1080p one, which is written down where the number lives. The
card menu is sixteen rows and 578 px, not fourteen and 510, and its row count is a function of
`uiArpLines`. The Pads bar's left group is 288, not 214, since the Play toggle took 74 px of it.


### Fixed: review pass on the step sequencer round

**`set_arp_pattern` silently flattened the Note lane.** The MCP bridge kept its own hand-copied
lane-range table, declared for every lane and filled in for ten of them. The four lanes appended
since arrived value-initialised as `{lane = 0, lo = 0, hi = 0}`, and `laneNote` is 0, so each of
them aliased the Note lane and clamped it to zero: a call carrying a `note` array applied the real
edit and then four that wiped it, and reported success. The same table's Note range was stale at
8, so Prev/Hi/Low/Rnd and all eight per-step shapes were unreachable through MCP. It reads
`ArpEngine::laneRange` now, which is the rule the Draw grid was already put on.

**A mute silenced the wrong step.** Mute is the Note lane's companion, not a lane of its own: it
has no tab, and the MUTE strip under the grid is drawn and edited against the Note lane's cells.
Its *length* has been kept in step since it arrived, for exactly that reason, but lanes grew a
loop window and a direction this round and only the length was carried across. Set the Note lane's
Dir to Down and the cell under the note drawn at step 0 silenced the note drawn at step 7, with no
way to correct it because Mute cannot be selected. It borrows the Note lane's whole shape now, in
`laneValue`, which is the one place lane data is read.

**The loop bar did nothing on twelve of the thirteen lanes.** It wrote only the selected lane,
while `enforceLinkedLengths` copies the Note lane's window over every lane on the 10 Hz tick, so
any drag outside the Note lane was undone within 100 ms. It fans out under Link exactly as Steps
and Speed already do, and Link off is still left alone. It also pushes an undo entry on the press
now, like every other lane edit: the window rides the arp tree, so without one the next Undo
reverted a window drag the user had not aimed at.

**Mutate's variation drifted off the cells it varies.** The step and the era came off the raw step
index rather than the lane's own walk, so at Speed x2 the era advanced twice per pass and a
"locked" variation changed halfway round the loop, and a non-zero Offset shifted which drawn cell
each stored variation belonged to. Both now use `laneStepIndex`'s own walk position.

**The fingered shapes took the ends of the sequence as the chord's extremes.** Under "As Played"
the sequence is in arrival order, so "the high note of the chord" could come out as the lowest
note held. Scanned now, the way `noteHi` and `noteLow` already are.

**The Note lane kept naming a chord nobody was holding.** `uiSeq` is written only where a step
fires, and nothing cleared it when the chord came up, so the cells went on reading "E3" until the
next step - which never came if the line was off. It goes quiet with the playhead now.

Also: a switched-off lane no longer draws a playhead for cells the engine never reads; the
out-of-loop dim is painted over the contour and the note name instead of under them, so excluded
steps stop reading as brighter than the ones that play; the lane strip's fourteen-lane scan moved
under the `isShowing()` gate, leaving only the length repair running while the panel is hidden;
`dirNames` is pinned to `numLaneDirs` with a `static_assert`; a `loopTo` written at the end of a
lane is stored as the "to the end" sentinel, so a get/set round trip stops pinning the window; and
the Reset lane's documentation now says what it does when Offset is set.


### Fixed: review pass on the pad/arp routing round

Six things the review caught, all of them in code this round touched.

**Changing a line's MIDI channel could hang a note for the rest of the session.** The
channel-change flush emitted its note-offs straight into the outgoing buffer, past `ArpMerge`.
Every note-on that line played had been counted by those refcounts, so closing them anywhere else
left `held[ch][note]` stuck above zero, and a count that never returns to 0 suppresses the *real*
note-off of every later hit on that pitch. It merges through `arpMerged` like every other path
now. The flush emits at sample 0 and a `MidiBuffer` keeps its events in sample order, so those
note-offs still close ahead of whatever the block goes on to play.

**The oldest sessions came back at the wrong loudness.** `migrateVelLevel` read the line's old
VelTrim out of the *saved* tree, but `migrateVelTrim` runs immediately before it and, for a session
old enough to predate VelTrim as well, synthesises that trim from the session's Volume and writes
it to the live parameter, never back into the tree. So exactly the sessions the chain exists for
fell through to the "as played" default. It reads the live parameter now, which answers both cases.

**A session load overwrote its own Root and Scale.** `replaceState` pushes genRoot and genMode
through the parameter listener like any other write, which raised the Key/Mode mirror and had the
next heartbeat write them over the Root and Scale that same session had just restored. Root and
Scale are still ordinary parameters you can set on the Controls bar after picking a generator key,
so a saved keybed setting was lost on every load, plus two gesture-less parameter writes at the
host about 20 ms in. The mirror is for the user turning Key or Mode, and it stays that.

**The arp bar's Draw tab relied on an accident.** `ArpPanel`'s constructor calls `refreshShape()`,
where `lastPatternMode` starting at -1 was supposed to report the initial shape and open a session
saved in Pattern shape with Draw already live. The constructor runs before `onShapeChanged` is
assigned, so that report went into a null `std::function` every time; it worked only because
`syncSectionControls()` happens to call `refreshArpBarTabs()` afterwards. Reported explicitly now.

**Two answers to one question on the generator's Send all to pads.** The button greyed off the
brain's own scan of the page while the commit it guards goes through `onSendToFirstEmpty`. Both
routed to the same hook now, so they cannot disagree.

**Removed:** the dead `velTrim` engine field and its per-block atomic load per line, an orphaned
duplicate comment block in `ChordGenPanel.h`, and the stale `followAim` documentation on
`sendPadToArpLine`, whose parameter this round deleted. `minWidthForView()`'s Pads-bar arithmetic
was re-measured: it had itemised a Compliance chip and a Mode combo that left the bar on
2026-08-02 while never accounting for the 124 px Mode combo that came back. The real total is 752,
not 858, and the floor never moved.

### Added: four arpeggiators, each in its own colour, with harmony voices and a mutate that can leave the scale

The 2026-08-19 pass, all four of Owen's asks in one round ("I want 4 arps. and each one should
have a color. and I want new knobs, 2 harmony drop down like the photo. and each of those has a
chance knob... and I want a mutate knob... higher values can go out of scale").

- **Four arp lines, A to D.** Line D's `arp4*` parameters are appended the way B's and C's were,
  so every earlier session opens sounding identical; line C, inert since 2026-08-02, is back on
  screen with it. The All view is a **2x2 grid of cards** now - each card keeps the full width
  the two-across layout gave it, and the panel takes the height, which is the cheap axis there.
  The arp bar carries four letter switches.
- **Each line has its own colour**: A cyan, B magenta, C amber, D lime. Worn by the card's frame,
  fill and caption, by a stripe under the letter on the arp bar, and by the Draw grid's playhead,
  so "which machine is this" has one answer everywhere. The marks are tinted, never the controls,
  which is how the one-accent skin law survives it.
- **Two harmony voices per line**, on each card: an interval dropdown (BigSky's shimmer list,
  Off / -Octave up through +2 Octaves; the two cents rows cannot exist in MIDI and are dropped)
  and a **chance knob** each, saying how often that voice fires. The intervals are chromatic on
  purpose - a Major 3rd is four semitones whatever the scale says - and the voice follows the
  note the run actually played, Mutate's strays included. New parameters `arp*Harm1/2` and
  `arp*Harm1/2Chance`, appended, defaulting to Off / 100.
- **Mutate reaches out of the scale at the top.** Three zones on the one knob: to 50 it is
  exactly the 2026-08-18 control and cannot leave the held chord; past 50 a mutated step may
  stray onto in-scale neighbours; past 75 a growing share of the strays are chromatic, out of
  scale on purpose. LOCK holds the strays exactly as it holds the in-chord variations, so a
  wander that found a wrong-note lick can keep it.
- **The harmony dropdown opens as two columns**, Off and the descending intervals on the left,
  the ascending ones on the right - the split BigSky's own panel draws for the same list, and
  what "make harmony 2 columns" turned out to mean.
- **A Play toggle on the Pads bar.** Off, clicking a chord card makes no sound and the strip is
  drag-only: a press that meant to become a drag toward the arpeggiator can no longer fire a
  chord and, with Exclusive on, choke every running line on the way past. Dragging, dropping
  and the card menu are untouched; on is today's behaviour and the default, and the setting
  rides the session like every layout choice.
- **The range knobs' halo opens the band equally both ways around the knob, which stays put.**
  The knob is the band's centre now: dragging the halo up widens the range on both sides at
  once, down tightens it, and the knob itself never moves under the gesture. The lit arc shows
  both halves, the reach stops where a rail is nearer (so the band never goes lopsided against
  an end of the travel), and the mouse wheel over the halo or the ring nudges the range too. A
  drag on the knob face still moves the whole band. Applies to VEL and H.TIME on the arp cards
  and to the pads' Strum and Humanize - and the engines follow: VEL's hits and H.TIME's
  lateness now wander either side of their knob rather than only below it, so the knob reads
  as "typical", not "maximum".

### Added: the step sequencer says what it is doing, and the lanes stopped being one grid each

A usability and functionality pass over the Draw page, drawn from Kirnu Cream's manual. The page
had thirteen lanes, one of them visible at a time, and nothing on screen answered the two questions
you actually have while drawing: **where is it now**, and **what is in the eleven lanes I cannot
see**.

- **A playhead, in the grid and in the MUTE strip.** Nothing published the arp's step position, so
  no grid could draw one. Each lane wraps by its own length and its own clock divider, so "which
  step is sounding" has a different answer in every lane and none of them is the transport's - with
  Link off, which is the whole point of per-lane lengths, the page was unreadable without it. The
  engine publishes the one index all the answers derive from, and the UI runs `laneStepIndex`, the
  engine's own arithmetic, rather than a second copy that could drift from it.
- **The lane tabs report on their lanes.** A dot when the lane holds anything but its default, over
  its own length; struck through when the lane is switched off. Both are Kirnu's marks (its manual
  p12) and they are what makes eleven hidden lanes legible without opening them.
- **A lane can be switched off.** Kirnu's per-control on/off: "when control is off all it's values
  are ignored". The drawing stays exactly where it is and the engine reads the lane's default. Keys
  had no way to take a lane out - Reset flattens it, which sounds the same and loses the work, the
  very trap Cthulhu's mute-preserves-value rule already fixed one level down on a single step.
- **A loop window and a direction, per lane.** Kirnu's loop control (p11) and its Loop direction:
  Up, Down, Up alt, Down alt. Keys had length alone, which is polymeter without phrasing - three
  lanes of eight can only ever be three lanes of eight in step. The window is a bar under the grid
  on the same cells, click or drag, and the nearer end moves; steps outside it stay editable and
  draw dimmed. The alt pair does not repeat its turning points. With Link on the window travels with
  the length, because that is what sharing one grid means; the **direction** deliberately does not,
  since two lanes crossing the same steps in opposite directions is the point of having one.
- **Steps, Speed and Link moved onto the Draw page**, into a lane strip beside the new On and Dir.
  They were in the STEPS group of the *Play* page's band, so changing how long the lane you were
  drawing runs meant leaving the page you were drawing it on. They are per-lane controls and they
  belong beside the lane. The band is two groups now instead of three, and STEPS' width went back
  to the two that stayed.
- **Copy and Paste over the Select span**, the last of Kirnu's palette (p8) that Keys was missing.
  Paste tiles: two copied steps fill eight, which is how a figure gets repeated. Only into the same
  lane, as in Kirnu - a Velocity lane pasted into Note would read as chord indices. Kirnu's **Clear**
  is not here on purpose: Keys' Reset already narrows to the span and already means "set to
  default", so a Clear beside it would be a second button doing its neighbour's job.
- **Note names in the Note lane.** A cell saying "3" is an index into a sorted chord nobody can see.
  The engine publishes what the run's indices currently name, so with a chord held the lane reads
  `E3` instead. Falls back to the number when nothing is held or the cell is too narrow.

The Draw page is 150 px taller for it - it is now the tallest of the three pages, where Play was.
Paging already moves the window, and this is the page the height buys something on.

### Fixed: restarting Keys no longer hangs the MCP bridge

Claude Code launches `keys-mcp` once per session and keeps it for hours; `run.py` closes and
relaunches Keys on every build, and the in-plugin server takes a new OS-assigned port each time.
The shim connected once and then wrote into a dead socket forever, so a tool call got **no
response at all** and the client sat on its idle timeout - half an hour of looking exactly like a
slow tool. It reconnects on demand now, and never leaves a request unanswered: with nothing to
connect to, with a connection that dropped mid-call, or with Keys alive but wedged, the call comes
back as a JSON-RPC error instead of as silence.

The fix is in the kit (`src/McpShimMain.cpp`), so every OK Studio plugin with a shim gets it.
`docs/MCP.md` has the detail and how to tell a broken bridge from a broken plugin.

### Added: the Note lane is an arpeggiator you draw, not a list of note numbers

Cthulhu's Note graph, which is the thing that makes that graph worth having. Its manual p23: the
top half of the lane is *"various arpeggiator patterns, which act like a typical arpeggiator, where
the note output varies consecutively one step after another"*.

- **Eight shapes live at the top of the Note lane** - up, down, up/down, down/up, up & down,
 down & up, fingered bottom and fingered top - appended above the existing values, in Cthulhu's
  own bottom-to-top order, so a drag up the lane meets them in the order its manual lists them.
  Keys had exactly one shape, the line's, and a `follow` value to defer to it. Now a lane can run
  four steps up, jump to a fixed note, and come back down, all drawn and all visible.
- **They share one walk.** Four steps of Up followed by four of Down comes back down the line it
 went up, rather than restarting - one cursor read by whichever shape the step names, which is
  what Cthulhu means by "consecutively one step after another".
- **Fingered bottom and fingered top are new shapes for the whole line too**, not only per step:
  *"every 2nd note is the high note of the chord"*, with the walk between them covering the notes
  that are not that extreme, so a triad comes out C–G–E–G rather than C–G–G–G. `Direction` gained
 two entries, which is safe in that direction only - a slot stores its shape as an index and the
  tree records what `numDirections` was when it was written.
- **The Note lane draws markers at a height, not bars filled up to one.** A Velocity of 120 is a
  magnitude and a column filled to 120 says so; a Note of 5 is a *name*, and a column filled to 5
  reads as "more than 4", which is not something a chord entry can be. It is also what makes room
  for the shape contours, which are drawn as pictures inside their own taller markers.
- **A Reset lane**, Cthulhu's Position Reset: the step restarts the shape's walk, so it plays the
 first note of its shape again. It restarts the *walk*, not the lanes - the manual's own example
  is about which note of the chord comes out, and rebasing the lanes would leave the lane reading
  its own reset cell for ever. It is a lane because Cthulhu reaches it by alt-click and Keys has no
  modifier to spend.

### Fixed: the panel kept a second copy of every lane's range

`buildLaneRow` took a low and a high per lane, and those thirteen pairs were a duplicate of
`ArpEngine::laneRange` - the table whose own comment says three tables that must agree is three
tables that will not. It bit immediately: the engine accepted the new Note values and every grid
still stopped at the old ceiling, so the shapes existed and could be neither drawn nor set. The
parameters are gone and the grid reads `laneRange`.

The lane tab row divided its width by a hard-coded twelve, so the Reset lane's tab was laid out
four pixels wide - the same invisible starvation the Chain lane caused when it made twelve. It
counts its tabs now, and the layout test caught it.

### Added: MUTATE and LOCK, a wander that stays in the chord and can be kept

Two knobs on each macro card, in the cell CHANCE used to hold. Chance lost nothing by it: it is
still a step lane and still has its own slider in the Play page's PLAYBACK group, which is where a
control you set once and leave belongs.

- **MUTATE** is how far a line explores other notes of the chord you are holding. `Drift` is barred
  from touching which note plays, and this does not reverse that rule - it meets it. The fear behind
  it was a machine wandering onto notes nobody aimed at, and Mutate moves the run to a different
  entry of **the sequence already built from the held chord**, so every note it can reach is a note
  that chord contains. There is no setting at which it plays something you did not put there, only
  a different one of the ones you did. `laneRand` is still the only thing allowed to change a note
  you *drew*, because you drew it there.
- **LOCK** is how long it keeps what it finds - the Turing Machine's own control, and the one
  randomness `docs/SEQUENCER_LANDSCAPE.md` ranked as missing. Left, a new variation every pass;
  right, the first one found repeats for good; in between it holds an idea for a while and moves on.
  That is what makes Mutate a composer rather than a noise source: a wander you can keep.

Both are stateless from the playhead, like everything in the engine bar `laneChain`. The variation
is a hash of the step and of which *era* the current pass falls in, not a shift register carried
between blocks, so a transport jump lands on the variation it would have walked to.

### Parameter layout

`arpMutate` / `arpMutateLock` appended per line (and `arp2*`, `arp3*`), both defaulting to 0, which
is the engine exactly as it was without them - no migration needed, and a session saved before this
sounds identical. The lane's on/off, loop window and direction are **not** parameters: they are lane
data, and they ride the `"lane"` node of the arp tree beside `length` and `clockDiv`, each reading
back as the old behaviour when the property is absent.


### Fixed: with the generator open, a chord could not be dragged into the arpeggiator

JUCE resolves a cross-window drop through `Desktop::findComponentAt`, which walks its own list of
desktop windows from the top and **returns from the first one whose bounds contain the point** - it
never falls through to a lower window when the one it picked has nothing interested there. That
list is kept in order by `Component::toFront` and by a window peer reporting its own activation.

The generator window calls `toFront` when it opens, so it goes to the top of that list. The plugin
editor never reports the same thing: it is a *child* window inside the host's, so clicking it
raises it on screen without any peer activation JUCE hears about, and the list keeps the generator
on top for good. The generator window is nearly full screen, so every drop aimed at the
arpeggiator landed inside its bounds, was resolved against it, found nothing interested, and
vanished.

Starting a drag from the pad strip now reports that window as the front one, which is simply true:
the press that began the drag was in it. Dragging the other way - a tray candidate onto a pad - was
never affected and is unchanged, because the generator is a real top-level window whose activation
JUCE does hear.

### Fixed: the greyed keys answered a different question than the one you were asking

There were two independent key settings. The Controls bar's **Root + Scale** drive Scale Lock and
the keybed greying; the generator's **Key + Mode** are its own, because the kit's scale table
answers "is this note in the scale" and carries no per-degree chord qualities to generate from.
Both read as "the key", so setting the generator to C Mixolydian left the keybed greying whatever
Scale happened to say - accurate, and about something else.

The generator drives the keyboard now: changing its Key or Mode moves Root and Scale with it, so
the keybed greys to the key you are generating in. One-way on purpose - every generator mode has a
kit scale, but Whole Tone and Chromatic are scales the generator cannot express, so picking one of
those on the Controls bar leaves the generator where it is rather than snapping it somewhere
arbitrary. The two lists are matched by name, not index, since they live in different repositories
and either can grow.

### Added: Mode on the Pads bar, page tabs and "Send all to pads" in the generator

**Mode** is back beside Key on the Pads bar, where it sat until 2026-08-02: a key without its mode
is half a key, and this is the pair you reach for between fills.

The generator's header had a read-only "Page 2 of 4" label - it told you where a committed card
would land and gave you no way to change it, so choosing a page meant leaving the window. It is
**four page tabs** now, bound to the same parameter the Pads bar's buttons drive.

**Send all to pads** joins Fill, Regen and Clear on the tray header. The tray had exactly one route
to a pad per card - a drag, or *Send to first empty pad* on its menu - so filling a page took
sixteen gestures. It writes only empty pads, like every other generative action, and each card it
places leaves its cell, so what stays behind is exactly what would not fit.

### Changed: the generator's constraint gates are SET / ROLL chips, not check marks

Asked which of three faults the check marks had, Owen said "all", and one change answers all
three. Six of the boxes were *gates* - ticked, generation obeys the setting beside them; unticked,
it rolls that setting itself - and they were drawn identically to the four inversion ticks, which
are values rather than gates. On the fixed row that read as five boxes in a row with two meanings
and one look. They also sat hard against the left edge of whatever cell they gated, landing at a
different x on every row, and six cryptic boxes is a lot to ask anyone to remember the meaning of.

They are word chips now, reading **SET** or **ROLL**. A word cannot be mistaken for a tick, it
says what it does without a tooltip, and every one is the same shape and width so they read as a
column. Each gets a gap after it, so a gate stops reading as the leading edge of the control
beside it. The parameters and their ids are untouched.

### Fixed: the generator window was far taller than anything in it could use

Every block in that layout has a size of its own, so a taller window bought nothing - it spread
the same controls over more glass. There was no ceiling on it, only a floor, so a session saved
large reopened large.

The window has a maximum height now, equal to what the layout actually adds up to, and it clamps
a saved size on the way in rather than waiting for you to drag it back. Nothing absorbs slack any
more either: the tray was the elastic block until 2026-08-18, the diagram was for one build after
that, and the diagram was the worse of the two - the bar chart grew to the height of a hand while
the window stayed enormous.

### Changed: the audition tray is twelve cards, not sixteen

It was four by four from the day it was built, and the pad strip went from sixteen slots a page to
twelve on 2026-08-03 without this following. So Fill generated four candidates that could never be
committed, and *Send all to pads* left exactly four behind however empty the page was - arithmetic
nobody should have to do.

Three rows of four rather than the strip's own two rows of six: the cards carry a note list under
the name, and at six across the layout's floor gives each one 100 px, which will not hold
"G#3 C4 C#4 E4 F4". Same count, same commit, one row less height.

### Fixed: the generator's tray ate every spare pixel

The tray took whatever height was left over, so on any window taller than the layout's floor the
sixteen disposable cards grew to twice the height a name over a note list needs, while the settings
above stayed at their minimum. Cards are capped at the height the pad strip draws, and the slack
goes to the source diagram instead, which is the one thing in that window that reads better bigger.

### Changed: the arp's VEL is MIDI velocity, 0-127

It was a bipolar percentage *trim* on whatever velocity arrived, centred on "as played", so its
readout showed things like `-31 ~20` - numbers that look like velocities and are not - on a
control called VEL, next to a pads knob that had just become an absolute 0-127 band. Two velocity
controls in two units is one too many.

VEL is now the velocity outright: the knob is the hardest a hit ever lands, the ring is how far
under that it can fall in the same units, and the readout names the band. 0 mutes the line,
exactly as full-left trim did. The Velocity lane, the ramp and Drift still shape it exactly as
before - only the base moved, from the incoming chord to the knob.

What it costs is "as played", a line following the velocity of the chord fed to it. With one
mouse that was already a constant: every chord Keys fires leaves at the pads' own Humanize
velocity.

Fixed alongside it: the 0.05 audibility floor is gone from this path. It existed because a *trim*
had to be able to reach silence past a floor protecting programmed dynamics; a level is the
velocity itself, so a band drawn low is meant to be quiet. The clamp still stops at one MIDI step,
since zero would be a note-off in disguise.

**Parameter layout.** `arpVelLevel` is appended per line and `arpHumanVel` widens to 0-127;
`arpVelTrim` stays registered and is read by nothing. `migrateVelLevel` converts a saved session
through the trim's own squared curve against 76 - the midpoint of the pads' default Humanize band,
which is the velocity every chord Keys fires actually left at - so a session that never touched VEL
opens playing exactly as loud as it did. A host automation lane for VEL or Humanize Velocity does
not survive the change of units.

### Fixed: two arp lines sharing a pitch cut each other short

Within one line the engine has always been careful: each hit closes whatever it lands on top of
immediately before its own note-on, so a repeated note re-attacks cleanly. Across two lines there
was no such rule - each line's output went straight into the outgoing stream, so two lines on one
channel sharing a pitch sent **two note-ons for it, and whichever line released first ended it for
both**. The other line's note was cut short and its own note-off arrived later as a stray. The
lines are usually fed related chords, so shared pitches are the common case, and it read as random
dropouts rather than as a fault.

The lines' outputs are now merged in time order and folded under one rule: one note-on per
sounding pitch, released by the last line holding it, and a line striking a pitch another one
already holds re-strikes it - a note-off at the same sample offset immediately before the note-on,
which is exactly the tie rule the engine already uses inside one line. No doubling, no cut-short
notes, and every attack you drew still sounds.

Giving a line its own **Channel** is still the other answer and is untouched: the same pitch on two
channels is two notes to the instrument downstream, and they never interact.

The rule lives beside the engine as `ArpMerge` rather than inside the processor, so it is driven
directly by four new tests - the overlap, the non-overlap, the two-channel case, and the panic that
would otherwise strand a count and hang a note.

### Fixed: the Draw page stayed greyed after setting Shape to Pattern

Draw is the lane editor - Harmony, Velocity, Gate, Ratchet and the rest - and it greys outside
Pattern shape, because a plain shape has no lanes to draw. The tab's enabled state was written
only when the *line* or the *page* changed, so choosing Pattern left Draw greyed until you
happened to visit another page and come back. The panel knew on every 10 Hz tick; nothing carried
it across to the section bar, where the tab lives.

The panel already detects that crossing exactly once (it is what sends you back to Play when you
leave Pattern with Draw up), so the notification goes there. A session saved in Pattern shape now
opens with Draw already live, too.

### Fixed: two arpeggiator lines could not be brought into phase without a transport

Launch Quantize aligns when a *chord* lands on a line, not where that line's steps fall. What puts
two lines on one grid is **Anchor** - and anchoring needed a rolling host transport, so with none
(the standalone, or a DAW sitting stopped) every line fell back to a phase of its own that is
zeroed when the line is switched on. Two lines switched on a second apart were then a second out
of phase for good, and no setting could bring them together.

The processor already keeps a beat count for exactly this shape of problem - the host's position
while it rolls, its own count otherwise, which is what Launch Quantize measures its boundaries
from. It is now offered to the engines as well, so there is always a grid to anchor to and every
line reads the same one in the same block. Two anchored lines walk in lockstep with no transport
at all, and a line joining late lands on the grid instead of starting its own.

**Anchor is still the switch**: off, a line keeps its own free-running phase and drifts on
purpose. What changes for an anchored line with no transport is that switching it on now drops it
onto the shared grid rather than restarting the pattern at step one - which is what it has always
done in a DAW with the transport rolling, so the standalone and the plugin finally agree.

### Changed: the velocity range spans 0-127

The Humanize range knob is the velocity control, and it stopped at 1 rather than at MIDI's own
floor. It reaches 0 now. The wire never does: a note-on at velocity 0 *is* a note-off, so 0 means
"as quiet as MIDI can say" and what leaves is velocity 1.

Fixed on the way past, and audible: the emitted velocity had a floor of about 5, so the bottom of
the band said one thing and played another. The range is taken literally now across its whole
span.

Saved sessions are unaffected - a stored velocity of 64 still means 64 - but a **host automation
lane** for Velocity Min or Velocity Max shifts by one unit at the bottom, since normalised 0.0
used to mean velocity 1 and now means 0.

### Fixed: dropping a chord on an arp line appeared to change its steps and tuplet

Nothing was written. No drop path touches a pattern, a rate or a tuplet - what the drop also did
was re-point the panel at the line it landed on, and in a line's deep view every readout is per
line, so STEPS, Tuplet, Shape and the rate all jumped to that line's own settings the instant the
card landed. A view change and a data change look identical when you are watching the numbers,
and this one arrived under the hand that was routing a chord.

Routing a chord no longer navigates to the line, for every route alike - a drop on the A / B
switch, on a macro card, on the panel, and the pad menu's *Send to arp A / B*, which had already
been given this behaviour for the same complaint. The way to look at a line is still its tab or
its macro card's **Details** button.

The reason a drop moved the aim is worth recording, because it had one and it expired: you aimed
at that line, so the next card click should follow the same aim. A card click stopped feeding a
line at all on 2026-08-02, so there was no next click left for the aim to serve.

### Changed: a chord pad sounds on release, and a drag makes no sound at all

Hold-to-play (2026-08-16) fired the chord on the press and let it go on the release, so a press
that turned out to be a *drag* had already sounded. The blurt was the visible half; the damaging
half is that firing a chord chokes the other chord sources, and with Exclusive on that reaches
each line's held chord. Dragging a card toward arp B therefore stopped arp A before the card had
moved, and silencing the blurt when the drag begins does not put that back - so aiming at one
arpeggiator stopped the other, with nothing on screen to say why.

The gesture is now decided on the way up. A press sounds nothing; a click sounds the chord on
release for 800 ms, the same length the generator's tray has always used; a drag is routing and
stays silent from beginning to end. Sustain and Latch still decide what the release means.

The cost bought a tick rather than a decision: **Chord pads play while held** on the settings
gear puts the press back, so a stab is short and a lean is long again. It is off by default,
because in hold mode a press that turns out to be a drag has already choked the other chord
sources - the thing this change is fixing - and turning Exclusive off alongside it is what makes
that free.

### Fixed: clicking a chord pad fed the arpeggiator

A click has not been allowed to hand a chord to an arp line since 2026-08-02, and `ChordPads`
duly stopped doing it - but the chord got there anyway, one level down. A line with **Play** on
lifts note events out of the outgoing stream so the arp replaces them rather than doubling them,
and a pad's chord was in that stream alongside the keys you play, indistinguishable from them.
Play defaults to on, so an arp line switched on arpeggiated every pad you clicked.

Play means the keys you play. Pads, the live chord card and the generator's audition now queue
into a second output collector that drains *after* the lift, so no line can reach them; the
keybed, the MIDI input and the MCP bridge are unchanged. A chord still reaches a line the way it
always has - dragged onto its switch, its macro card or a slot, or through the pad menu's
*Send to arp A / B*.

Both queues feed the same buffer and the same instrument, so nothing else moves: same channel,
same strum, same Humanize, and a recorded take still holds what actually left. One note-on per
sounding pitch still holds across both, and each pitch remembers which queue opened it so the
note-off follows it there - a release sent down the other one would strand the note in a
listening line's engine and arpeggiate it forever.

### Fixed: dragging a chord card off the strip could delete it

Dropping a card where nothing accepted it cleared the pad, on the reading that off the row means
bin it. Too much of the window is neither the strip nor a target: the section bars above and
below the arp panel, the Controls band, the keybed, every gap between them. Dragging a card *up*
into the arpeggiator crosses the Pads bar on the way, so a release a few pixels short of the
panel destroyed the chord instead of routing it - and because the arp's own drop targets always
vetoed the clear correctly, it only ever bit on a near miss and so read as random.

A drag that lands on nothing is now a cancelled drag and puts the card back. Clearing a pad is
*Clear pad* on the card's right-click menu, which is where it was already documented to live.

### Fixed: review pass on the chord library round

**The Library generator source produced pads with the wrong numerals.** It built chords through
`chordlib::chordsFor` against the row's own mode, correctly, and then dropped the row name and the
position within it on the floor - so the strip fell back to resolving `degree` against the
*session's* mode and drew an Aeolian row's bVI as "vi", a major chord labelled minor. That is the
exact fault `numeralAt` was added to fix, reached by the one route that skipped the stamp. The
stamp moved into `chordsFor` itself, which is the one function that knows both the row and the
position, so every route out of the library carries it: the library window's Tray and Pads
buttons, and the generator's own source. A Library fill also gets a bracket and a seed for
Follows now, which it never had.

**A progression's numerals could all be off by one.** `chordsFor` skips a token it cannot parse
and the empty entries `StringArray::fromTokens` emits for consecutive separators, while
`numeralAt` indexed the raw token list. A row with a double space, a trailing space or a typo'd
suffix therefore shifted every later chord's numeral by one, under a bracket correctly naming the
progression. Both go through one `playableNumerals` now, and a test walks all 355 rows checking
that every chord's `progressionStep` names its own numeral.

**A bracket on the second pad row drew under the first.** The Y was clamped with `jmin` against
row 0's bottom, which is always the smaller value, so a run in the lower row drew its bracket up
in the gap under the upper one, spanning columns it has nothing to do with - and two runs, one per
row, drew two identical brackets on the same line. The run loop already refuses to let a run cross
a row, so either end answers for it.

**Closing the library window cut off the generator's audition.** `ChordGenMenu` owns one preview
path for both the progression walk and the tray's 800 ms single-chord audition, and the destructor
stopped it unconditionally - so closing this window inside those 800 ms killed a sound it had
nothing to do with. It stops only a walk now.

**A progression's last chord read as finished while it was still sounding.** `auditioningProgression`
answered off the queue, which is popped as each chord *fires*, so it went false the moment the
final chord started. The library row went dark about 100 ms into an 800 ms chord, and clicking it
during those 800 ms restarted the whole progression instead of stopping it. A flag that lives
until the walk actually ends replaces the queue test.

**The generator's Library band never noticed the filters moving.** `ChordLibraryPanel` polls the
shared mood/genre/function signature and re-selects its combos; the generator band polled only the
last entry name, so a filter set in the library window left its three combos and its match count
stale for ever while Fill quietly generated under the new one. It polls the same signature now,
through one `adoptLibraryFilters` shared with the window's own construction.

**Every maj7 in the corpus was counted as minor.** `rest.startswith("m")` is true for `"maj7"`,
in `rank_against_chordonomicon.py`, `validate_moods.py` and `corpus.py`'s SQL alike, so rows
written with M7 never joined the corpus windows they match and the major/minor valence the mood
table is gated on was computed over mislabelled data. `compare_midi_pack.py` had the sibling bug:
it read `upper` *after* the suffix branch had already lowercased the numeral, so a minor-quality
token could never get the minor-context flattening and landed in the "only in pack" bucket under
the wrong spelling.

Also: `couldFollow`'s shared-mood bonus could reach +4, because its `break` left only the inner
loop - enough to outweigh the +3 for staying in the mode and to close most of the join score's own
spread, so a tag-heavy row on a tritone could outrank a same-mode falling fifth. It is one point
however many moods overlap. The library window's Close button was a 26 px target in a 28 px
header, under the 34 px mouse-only floor, on the only on-screen way out of the window; the header
is 34 like its sibling's. And `numeralAt` builds its per-row token lists once rather than scanning
355 rows and re-tokenising on every call, which it takes per filled pad per repaint of the strip.


### Added: a pad remembers which progression it is a step of, and the strip brackets the run

`ChordPad` gains `progression` and `progressionStep` - the first fields added to that struct since
Markov's numeral, and for a reason of the same kind: a pad knew what chord it was and not what it
was *part of*, so a strip holding the Andalusian cadence looked identical to four unrelated minor
chords. A run of adjacent pads sharing a library row in step order now carries a hairline bracket,
with the row's name along the top of its first card.

The row's **name**, never an index into `chordlib::table()`. That table is explicitly free to be
inserted into - it is the one append-*and*-insert-safe table in Keys, because nothing stores an
index into it - and an index here would quietly take that freedom away and move every saved pad the
first time a row was added in the middle.

**A run breaks on a row change, a step that does not follow, and a row break.** The last is the one
worth stating: pads wrap from the sixth to the seventh, so 5 and 6 are adjacent by index and nowhere
near each other on screen, and a bracket spanning them would be a line drawn across the strip to
nothing. A run of one draws nothing at all - that is a chord that remembers where it came from, not
a progression on the strip.

**The numerals under that bracket were wrong for one build, and the fix is the interesting part.**
`degree` is an index into the mode a chord was *generated* in, and a library row is generated
against its own mode - so an Andalusian cadence dropped into a C major session read back as
`I vii vi V` underneath a bracket correctly naming it. Four wrong numerals is worse than none.
`chordlib::numeralAt` asks the row directly: a row and a step name a chord exactly and need no mode.

### Added: Follows - a progression that could follow a progression

The relational layer `docs/CHORD_LIBRARY.md` §7 has been promising, as `chordlib::couldFollow` and a
toggle in the library window. **Two signals, because "could follow" is two questions.**

**Structure decides which rows are eligible.** `functionsAfter` is a small grammar of song form:
what follows a Cadence is not what follows a Turnaround, and nothing follows an Open with another
Open. It is a **gate rather than a weight** on purpose - a row that does not belong after this one
is not a weak answer, it is the wrong one, and letting it in on a good harmonic join is how a
suggestion list stops meaning anything.

**The harmonic join orders what is left.** The last chord of one against the first of the next: a
falling fifth scores highest, a repeat lowest without being disqualified, since two progressions on
the tonic do follow each other - it is just the dullest answer. Staying in the mode is worth about a
rank; a shared mood nudges.

It points at the **pads**, not at a row you select, because that is where the question comes from -
you have laid a progression down and want the next one. It scans the current page backwards, so the
*last* progression is the one being followed, and greys when no pad carries one.

With the Andalusian cadence on the pads it answers with the Flamenco cycle, Trap Phrygian, the
Phrygian cycle and the Phrygian dominant cycle - every one starting on the tonic, which is the V-i
resolution, and every one Phrygian.

### Added: Favourites in the library window

A star at the left of every row, and **Starred only** beside the three pickers, narrowing whatever
they matched rather than replacing it - so a star and a mood together mean "the sad ones I kept".
The gap Scaler's own manual surfaced (p43): 355 rows and no way to keep the six you actually use.

Kept **by name** in `LayoutState::libraryFavourites`, the same call `ChordPad::progression` makes and
for the same reason. **Per session, which is the honest weakness** - Scaler's favourites are global,
and a star set in one project is gone in the next. Keys has no global store for anything, the
settings gear's three switches included, so a global one would be new machinery for one feature.

The star is **painted, not a `TextButton`**, the same call the lock dot on a chord card makes:
twelve more Components to lay out, hide and re-title on every page turn, for a two-state mark. Its
cell is the mouse-only 34 px all the same, reserved out of the row before anything else.

### Fixed: the library row reads as a table

A UI pass, and three of the four are fixes rather than polish.

**Numerals over the chords they come out as, one column per chord.** The row had a wide dead strip
between the numerals and the tags that read as a table with a hole in it. What belongs there is the
other half of the question: the numeral says what the progression *is* whatever key you are in, the
chord says what you will hear. Drawn as **measured columns** rather than two strings - which they
were for one build, and the two lines use different fonts at different sizes, so `I V vi IV` over
`C G Am F` drifted apart along the row until the pairing was something you worked out rather than
saw.

**`Font::getStringWidthFloat` under-measures, badly.** Sizing those columns with it made every cell
as wide as the *chord* underneath, so `iim7` drew as `iim`, `V7` as `V`, and a bare `V` as nothing
at all - a table quietly deleting the last character of half its content. `GlyphArrangement`
measures what actually draws, and is what JUCE 8's deprecation names as the replacement.

**A name drops its own numerals when the row already shows them**, so it reads "Axis" rather than
"Axis (I-V-vi-IV)". The names carry them because they were written for a combo box, where the name
is the only thing on screen; in a table with two columns of the same information a third copy is
noise, and it is what pushed the longest names into an ellipsis. The test is exact - the
parenthetical must *be* the numerals - because plenty are not: "i-iv-v (natural minor)",
"Autumn-leaves turn (major to relative minor)". Display only; `Entry::name` is still the identity a
favourite and a pad store.

**"To tray" / "To pads" become "Tray" / "Pads"** at 66 px. Twenty-four buttons repeating two phrases
down the window is a lot of text saying one thing, and in a column the preposition carries nothing
the position does not. Accessible names keep the whole phrase.

### Changed: the library says where its progressions came from, and it is not what it first claimed

The table's provenance was overstated in seven places. It said the rows were "ranked and
section-tagged against Hooktheory's published Trends and the open Chordonomicon corpus". **They were
not.** Both datasets are real and relevant, and summaries of them informed the design, but nothing
had been downloaded, no query run and no statistic computed.

What actually happened: the rows were **written out from music-theory knowledge** - the named canon,
modal vamps derived per mode and picked by ear, jazz turnarounds, film-score mediants, and the loops
that characterise each genre. Sixty rows are `MarkovData.h`'s, itself hand-authored for Keys.
`ChordLibrary.h` now says so at the point somebody would add a row, which is where it matters.

`docs/CHORD_LIBRARY.md` §3's **Section** tag (Intro / Verse / Chorus) is marked **unbuilt** rather
than described as though it already had corpus data behind it. That is the one axis that genuinely
could be derived from evidence, which is exactly the reason not to invent it by hand.

### Added: the corpora, and seven rows they found missing

`datasets/` and `scripts/corpus/`, with `datasets/README.md` as the manifest. Payloads are
gitignored like `manuals/`, so a fresh clone gets the manifest and nothing else and Keys
redistributes none of it.

Two sources, and **the licences are the headline**. Ludovic Drolez's `free-midi-chords` is **MIT**
and can feed shipped content freely; **Chordonomicon** (680,000 songs, chord progressions with
structural-part annotation) is **CC-BY-NC-4.0**, and Owen's call is that Keys is personal use, which
that licence permits squarely. If Keys ever ships commercially, anything derived from it has to come
out or be re-derived from a permissive source.

**The finding that could have done damage: two roman-numeral conventions.** Keys uses a fixed
major-scale degree table for every mode, so minor's flat degrees are `bIII`, `bVI`, `bVII`. The MIT
pack spells minor progressions against the *minor* scale, so its `III`, `VI` and `VII` are already
flat and written unadorned. The pack's `i VII VI V` and Keys' `i bVII bVI V` are **the same
progression**. Comparing them without translating first reported an overlap of 19 rows out of 138,
which is nonsense for two collections that are both mostly canon; translated it is 25 and every
minor row lines up. The dangerous direction is importing a pack row verbatim: it parses perfectly
and plays the wrong chords, and `ChordLibraryTests.cpp` cannot catch it, because a well-formed
numeral is all it can check for.

**Seven rows added**, and they are the useful kind of gap. The commonest four-chord windows in the
corpus with no row here turned out to be **two-chord vamps written across four bars** - `I V I V`,
`I IV I IV`, `i bVII i bVII` - a shape the table already used and had never written down for the
commonest degrees. Not an exotic progression nobody thought of; the obvious one everybody plays and
nobody puts on a list.

**Eight rows never occur** in the corpus, all seven chords or longer. Nothing was deleted: the count
*fell* from eleven to eight when the sample went from 40,000 songs to 150,000, which is the tell -
they are rare rather than absent, and an exact twelve-chord window is rare in user-entered chord
sheets.

### Added: a check on the mood tags, and a clear statement of what it can settle

Chordonomicon carries a Spotify track id on 73% of its songs and the audio-feature dumps carry
valence and energy per id, so every progression can be placed on Russell's circumplex and each row
scored by the mean valence of the songs that play it.

**The control is the part that matters, and it failed first.** Minor should read sadder than major.
The first split counted how many of a row's chords were minor - the obvious test, and wrong, because
a minor key is full of *major* triads: `bIII`, `bVI` and `bVII` all are, so the Andalusian cadence
counts three major against one minor and landed on the major side of a test meant to identify it as
minor. With that split the control said minor was *happier* and the whole run was noise. Split on
the row's declared mode instead and it passes at **+0.018** in the right direction.

That 0.018 is the yardstick, and it reframes the result: it is roughly the most a purely *harmonic*
fact moves an **audio** valence measure. The mood ordering comes out sensible - Lighthearted, Tender
and Playful at the top, Longing, Haunting, Sad and Melancholic at the bottom, energy running the
other way - but its spread is 0.054, three times that ceiling. So most of the spread is **not
harmony**; it is genre and production riding along.

**Verdict, recorded rather than glossed:** the tags are directionally right, and the measurement
mostly reflects the company a progression keeps rather than the progression itself. And some of it
can never be settled this way - valence and arousal are two numbers where the vocabulary is 46
words, so Haunting and Eerie will always land in the same place. **No tag was changed.**

### Added: a Library window you can actually browse

Owen chose both surfaces when the library was designed - the generator source first, a window
after. This is the window, opened by a **Library** chip on the Pads bar beside Generator.

The source that shipped first is genuinely useful, but a filter is not browsing: you can see what
came *out* of the table and never what is *in* it. This shows you the table.

**Paged, not scrolled.** Twelve rows and a `<` `>` pair, exactly the shape the pad strip already
uses. 355 rows is a scroll, and a scroll is the gesture the mouse-only contract is worst at - a
scrollbar thumb is a small target that has to be dragged, and a wheel is not a gesture Keys may
require. A page is two clicks and every target on it is full size.

**A row is a chord card that happens to hold several chords.** It shows the name, what it does, the
progression in roman numerals as *written* rather than resolved into a key ("i bVII bVI V" says
what the Andalusian cadence is in a way "Cm Bb Ab G" only says if you already knew the key), and
its mood and genre tags. The whole row is the Hear button, and a second click on the row that is
walking stops it - the way out is the same target that started it rather than a hunt for a Stop
button.

**Hearing a progression is the thing the tray could not do.** ii-V-I and ii-V-vi start identically,
so one chord of a progression tells you almost nothing. `ChordGenMenu::auditionProgression` walks
the chords at 550 ms each and gives the last one the full 800 ms a single chord gets, so a cadence
is heard arriving rather than stopping. It lives on the brain rather than in the window for the
reason every audition does: it calls noteOn with no pad behind it, and the brain outlives every
window, so no close can strand a note. One timer serves both auditions and the queue is what tells
a tick which kind it is - a second timer would be a second thing able to leave a note on.

**Two buttons per row place it**: into the generator's tray, where each chord becomes a candidate
you can hear and drag one at a time, or straight onto the page's empty pads. The pads route goes
through the same `sendChordToFirstEmptyPad` every generation uses, so **nothing overwrites a chord
you already have**, and it is one undo entry for the whole progression rather than one per chord.
"To tray" greys when the generator window is shut rather than opening it behind your back: a button
that summons a window you did not ask for is a surprise.

**The three filters are the generator's own state**, so this window and the Library band are one
thing rather than two that drift - pick a mood here and Fill on the Pads bar obeys it. Each window
polls the other's changes, because neither knows the other exists and neither should have to.

The Pads bar is two pixels past what the *old* 1070 floor would have handed it, and comfortably
inside the current 1280. Noted in `minWidthForView` rather than tidied away: it means the Pads bar
is one chip from being the binding constraint again.
### Added: Library, a source that looks a progression up instead of computing one

Owen: "an outstanding library that makes it easy to compose, maybe organized by emotion or
something. Scaler, the other VST has done a great job of this."

**271 named progressions** in `src/ChordLibrary.h`, tagged on three axes, reached as an eighth
entry on the generator's Source row. Its band is Mood / Genre / Does-what plus a readout of how
many rows the filter matches and which one the last generation landed on.

**Function is the third axis and it is the point.** Scaler tags on two, mood and genre, and "sad"
returns forty candidates with no way to choose between them. **Loop, Cadence, Turnaround, Vamp,
Lift, Descent, Turn, Open** is the axis that separates "sad, and it loops" from "sad, and it ends",
and every composer has one of those two in mind rather than the other. Two words on Scaler's own
mood list give the game away: Inconclusive and Resolved are not emotions, they are what the
progression *does*.

Mood and Genre are Scaler 3's own vocabularies, from Owen's copy of them, plus five words Keys
already used that Scaler has no equivalent for: Haunting, Nostalgic, Rebellious, Spiritual, Tender.
A word list is a taxonomy rather than a compilation. What is authored here is the *content* - the
named canon (Pachelbel, Andalusian, backdoor, rhythm changes, folia, the classical schemas), modal
vamps per mode, jazz turnarounds, film-score chromatic mediants, and the loops that carry each
electronic genre. **Written out from music-theory knowledge, not measured against a corpus** -
`ChordLibrary.h` says so at the point somebody would extend the table, and sixty of the rows are
`MarkovData.h`'s, itself hand-authored for Keys. None of it is copied from another product's
curated list.

**Stored as roman numerals**, in the grammar `ChordMarkov.h` already parses, so one row serves
twelve keys and the storage format is the same notation the cards now print in their corner. Six
suffixes were appended to make that possible - `m7b5`, `mM7`, `m6`, `madd9`, `M9`, `m9` - and
half-diminished is the one whose absence was not cosmetic: it is the ii of every minor ii-V, so
the most common cadence in the minor key could not be written down at all. Appending there is
safe, unlike almost everywhere else in Keys, because that table is looked up by exact string and
nothing stores an index into it. `genSource` itself is appended to, which is the usual rule.

**The table validates itself on every build**, and that is not ceremony: a hand-typed numeral has
two silent failure modes - a token that will not parse is skipped at play time so the progression
just comes out short, and a misspelt tag is a row the picker that wanted it can never find.
`tests/ChordLibraryTests.cpp` walks every row for both, checks each one transposes identically
into all twelve keys, and refuses two rows that hold the same chords **and** share a genre. That
last rule caught fourteen redundancies while the table was being written, and forced a canonical
spelling: a plain triad is `i`, never `im`, because both parse to the same chord and a duplicate
wearing two spellings is invisible to a string compare.

Reusing one progression under two names is still correct and deliberate - the same four chords are
how you find it from the Disco end and from the Neo Soul end - so the rule is genre overlap, not a
flat ban.

**A generation lays whole progressions end to end rather than looping one.** The first cut looped
a single row to fill the sixteen tray cells, the way `sources::progressions` does with its own
templates, and it was wrong here for a reason only visible on screen: the library holds vamps, and
rolling the two-chord "Minimal one-chord" filled every card with the same Cm9. Laid end to end, a
**Vamp** filter gives eight different vamps to compare and a **12-Bar Blues** fills the tray on its
own - the same rule right at both ends. Rows are drawn shuffled and without replacement, so Regen
is never inert under a narrow filter.

**Degrees resolve against the row's own mode, not the session's**, which is the opposite of what
every other source wants and the right answer here: a minor row read against a major session
resolves nothing, and the tray came back with half its cards labelled `?` about a progression
perfectly in *its* own key. `degree` is stored on the pad, so this is what the strip shows
afterwards too.

The three picks are not parameters, the shape Markov's Mood and Start already use, and for one
reason of their own: a choice parameter's item list is append-only forever once a session stores
an index into it, and locking a 46-word mood vocabulary into the layout before the library has
settled would mean never being able to drop or rename one. The cost is that a pick does not
survive reopening the session, exactly as a Markov mood does not.

### Fixed: the generator's diagram drew the wrong source for anything past Planing

`SourceViz::setSource` clamped its argument to 6. That is the same trap `ChordGenMenu::sourceIndex`
documents and had already been fixed for - an upper clamp has to be a literal count of the sources,
so appending an eighth brain, the one growth the parameter's own comment calls safe, arrived
silently drawing Planing's diagram under Library's chords with nothing on screen to say so. Floor
only now; a source the diagram does not know falls through to an empty well, which is honest.

### Added: every chord card says which degree of the key it is

Owen: "I want the progression number to show up in the generator on the chord pad." A filled card
now carries its roman numeral in the top-left corner - on the chord pads and on the generator's
audition tray both, because those are the same card read at two moments, the chord you kept and
the chord you are trying, and a numeral that sat differently on each would say they were different
things.

"Am" tells you what a chord *is*; "vi" tells you what it *does*. The second is what makes a row of
cards read as a progression rather than as four unrelated names, which is the whole reason to have
sixteen of them side by side.

Top-left is the one corner a card had left: the lock dot owns the top-right and the arp line's
letter the bottom-right. Micro caps at the note list's own size, 0.62 alpha - the numeral is
provenance, not the chord, and the name is still what you read.

`src/ChordNumerals.h` is the one implementation, moved out of `SourceViz.cpp` where it was private
to the Progressions diagram. A copy per surface would have re-armed a trap that file has already
paid for: the diagram drew sixteen `?` for a build because it read `numeral`, which only the Markov
source writes, where every other source writes `degree`. Resolution order is numeral, then degree,
then a degree derived from the chord's root against the current key.

It answers **empty** rather than `?` when none of the three resolve, and the two surfaces part
there on purpose. A card draws nothing at all: a `?` in the corner of every hand-captured pad is
noise standing in for information, and a chord borrowed from outside the key saying nothing is
itself the useful answer. The diagram keeps its `?`, because it draws one chip per step and an
empty chip would read as a gap in the walk rather than as a chord whose degree is outside the key.

Pads resolve against the `genRoot` / `genMode` **parameters** rather than through
`ChordGenMenu::genRoot()`, which answers with whatever an unticked Key or Mode rolled for the last
generation. A pad outlives that roll, and the key you are composing in is the one on the Pads bar.

### Added: docs/CHORD_LIBRARY.md, the design behind the library

The design and its paper trail, in the shape `docs/ACID_DESIGN.md` uses, including the two parts
not built yet - a browsable Library window, and the relational layer that would make **Could
follow** mean "a progression that could follow this progression" rather than "a chord that could
follow this chord".

The finding that shapes all of it: Keys already ships **88 mood-tagged progressions** in
`src/MarkovData.h`, and no user can look at one. They exist only to be shredded into Markov
bigrams, so asking for "Nostalgic" returns a statistical blur of the nostalgic progressions rather
than the progressions themselves. Seven more sit in `ChordSources.h` with no tags at all. Folding
those 88 into the new table is the next job.

### Fixed: half of the arp VEL knob's outer ring did nothing

Caught in review of the merge that folded Humanize Velocity into VEL's ring. `RangeKnob` took
both the span's ceiling and its drag sensitivity from the **face's** range, which is right for
every ring that is a span of its own knob - H.TIME, where face and ring are both "how late", both
0..100. VEL is the first ring that is not: its face is a bipolar trim, -100..100, and its ring is
Humanize Velocity, 0..100.

So the drag was calibrated to 200 units while the parameter it wrote stopped at 100. The ring hit
its real maximum after about half the satellite's travel, and past that point the arc kept growing
under the hand while the card's own 10 Hz refresh read the clamped parameter back and yanked it
down again - a control whose top half did nothing but stutter.

`RangeKnob::setSpanMax` is the fix, and it is the general one rather than a special case: the span
now has a ceiling of its own, defaulting to the face's travel so nothing that already worked
changed. Both range knobs set it from their own ring parameter's range, so H.TIME lands on exactly
the number the default already gave it and the two cannot drift apart.

### Fixed: "check for updates" blamed your connection for a release still uploading

*Check for updates* had three ways to fail and one sentence for all of them: *"Could not reach the
update server."* One of the three is not a failure at all - a strictly newer version is tagged and
only its installer has not finished uploading, which is what the minutes after a release look like
from here. That is the case it is most likely to hit, and the case where telling somebody to check
a working connection is most useless.

`CheckResult::notReady` is its own outcome in the kit now
([okstudio-juce-kit#9](https://github.com/owenpkent/okstudio-juce-kit/pull/9)), and Keys says what
is actually true: the version has been announced, its installer is not published yet, try again
shortly. A genuine network or API failure still reads as one.


### Fixed: the tempo followed the DAW only while the transport was rolling

Owen: *"and bpm isn't syncing with daw."* A DAW's tempo is its tempo stopped or rolling - Ableton
reads 120 with everything parked - and Keys followed the host only inside `getIsPlaying()`. So the
number on the Controls bar disagreed with Live's for exactly as long as you were setting up, which
is the whole time you are looking at it.

Two places had to agree and both did the same thing: `ArpEngine::process`'s tempo choice and
`KeysProcessor::advanceChainClock`. Both now follow whenever the host *reports* a tempo. The
**position** keeps its `playing` test, because a position genuinely means nothing while stopped -
only the tempo was over-gated.

The check could not simply be deleted, and the reason is the kind that bites quietly.
`HostClock::bpm` defaults to **120, not 0**, so a `bpm > 0` test would have read that default as a
host answer - and in the standalone, where there is no playhead to ask at all, Keys would have sat
at 120 and ignored its own BPM control for good. `HostClock::hasBpm` says whether the host actually
answered. Verified after the change: the standalone set to 90 still plays at 90.

### Changed: a chord pad plays for as long as you hold it

Owen: *"when you click a pad cord, it should only play it for the amount of time that you're
holding it, not a fixed value."* The press fires the chord and the release lets it go, so a stab is
short and a lean is long. Sustain and Latch are untouched: the release still goes through
`releaseChordPad`, so a pedalled chord keeps ringing exactly as before. The live card got the same
treatment, since leaving it on a fixed blip would make two cards on one strip answer the same press
differently.

This reverses 2026-08-02's "a card sounds on release, never on press", so it is worth being exact
about what that fixed. **It was never the noise**: the press branch also handed the card to a
running arp line and *cleared `dragSource`*, so a card could not be dragged in the one mode where
dragging it onto a line is the point. That branch is gone - a click no longer feeds a line at all -
so sounding on press costs nothing this time.

The honest cost: a press cannot yet know whether it is a play or a drag, so a drag blurts for
however long it takes to travel six pixels. `mouseDrag` silences it there. Waiting for the drag
threshold before sounding would put a lag on every note, which is the worse trade, and it is the
same one every drum pad makes. `auditionMs` (800) is gone; **the generator's audition tray keeps
its own fixed 800 ms**, which is not an inconsistency - a tray card is a candidate you are
sampling, where a length that does not depend on your hand is the point, and a pad is an
instrument you are playing.

### Added: copy/paste a chord between pads, and get one out as a MIDI file

Owen: *"need to be able to copy paste chords."* Asked to choose between copying inside Keys and
getting a chord out into Ableton, he picked both.

**Copy chord / Paste chord** join a pad's right-click card menu. Copy carries the whole
`KeysProcessor::ChordPad` - notes, name and the generator metadata (`rootPc`, `type`, `degree`,
`numeral`) - into a UI-only clipboard on `ChordPads` itself, never the session tree, so it needs
no migration and simply outlives a page flip, which is most of what it is for: the four pages are
12 pads apart, further than a drag reaches. `locked` is stripped at copy time rather than checked
at paste time - a lock protects the *slot* a chord sits in, not the chord passing through it, and
carrying it forward would silently lock whatever pad the chord later landed on. Paste goes through
the same choke a drop does (`clearChordPad` then `setChordPad`, one undo entry), so a pad left
ringing by Sustain or feeding an arp line gives its old notes up before the pasted chord lands
rather than stranding them. Paste greys on an empty clipboard and on a locked target, matching
Clear pad's own refusal.

**Save chord as MIDI**, on the same menu, writes one bar of the pad's chord - all notes on
together, at `baseVelocity01()`, the same level `pressChordPad` already plays that pad at - into
`KeysProcessor::takeFolder()` and reveals it in Explorer selected, ready to drag onto an Ableton
track. Live's own clipboard is internal to Live and will not accept a paste from the Windows
clipboard, so a dropped `.mid` file is the only way a chord built here reaches a Live clip; a menu
row cannot start a drag, so reveal-then-drag is the closest one click gets, the same shape
`TakePanel::dragTakeOut` already uses for a recorded take. It is its own small writer rather than
a call into `KeysProcessor::buildTakeMidiFile`: that helper plays back a recorded performance from
`capturedTake`, trimmed to the first note and frozen at the tempo recording was armed at, and has
no note list to hand a chord through - reusing it would mean faking a take by writing synthetic
events into real recording state from a menu click.

The menu grew from eleven rows to fourteen, still two separators, still no submenu: 14 * 34 + 2 *
17 = 510 px, checked against `KeysEditor::idealHeight()` (899-957 px across the arp panel's own
range of view heights) rather than assumed, since a pad's card sits well down the window - roughly
y=656 to y=714 depending on the arp view - with several hundred px of headroom either way for the
menu to grow into above it.

### Changed: one velocity knob per arp macro card, with Humanize Velocity as its outer ring

Owen, looking at the LINE A macro card: *"I didn't realize there was a separate velocity knob. I
only want one velocity knob, and I want this humanize section to be the outer ring."* VEL and
H.VEL sat two cells apart and both changed loudness - VEL the level, H.VEL how far a hit could
fall under it - which read as two controls for one question. They are one control now.

**VEL is a `RangeKnob`.** The face is still `arpVelTrim`, bipolar and unchanged; the outer ring
is `arpHumanVel`, Humanize Velocity's own amount, not a span of VEL's own value the way the
band's H.TIME ring works. VEL can sit anywhere from -100 to 100 and the ring still reaches
straight down from wherever that is - `RangeKnob`'s bipolar case already had everything this
needed, so nothing in `RangeKnob.h` changed. `arpHumanVelSpan`, H.VEL's own old ring, is pinned
to 100 by every write the new ring makes rather than removed: the draw was already uniform
between the floor and the knob at that default, and pinning it is what keeps that true with one
fewer number on screen. The parameter stays registered, the engine reads it exactly as before,
and `migrateVelTrim` is untouched - only the macro card stops exposing it as a second knob.

**The lamp is new**, unlike H.TIME's ring, which has never had one: H.TIME's knob at zero already
means "no wander", but VEL's knob at zero means "as played", so there is no position of the level
that could double as Humanize Velocity's off switch. Clicking the satellite now toggles it
instead, off parking the amount at zero and remembering what it was - the same shape ChordPads'
Strum lamp uses, and, like that one, the remembered value is a UI convenience, not persisted.

The knob strip is seven now, not eight: **OCT, GATE, CHANCE, SWING, OFFSET, VEL, H.TIME**. Row
height is unaffected - every one of `arpMacroCard`'s constants is independent of how many knobs
share the row, only their individual widths, which `MacroRow::resized` already computed from
`numKnobs`. `Macro H.VEL A` / `B` and their range and handle names are gone; the ring answers to
`Macro VEL range A` / `B` and the satellite to `Macro VEL range handle A` / `B`, matching what
H.TIME already used. The per-line band's own **Human Vel** slider, on a line's Details view, is
untouched - it was not part of this merge, the same carve-out the original `RangeKnob` entry
left for it.

### Added: a settings gear and its menu

Owen: *"we need a settings icon and menu. populate menu."* A 34 px square button, drawn as
vector paint (no asset, no emoji, the same self-drawn-chrome rule the fold chevron follows in
SectionBar), sits immediately left of the theme swatch on the Controls bar - plugin-level like
the swatch, so it never hides with that section's fold. It costs the bar 40 px (the button plus
the gap that now separates it from Theme), reserved before the elastic Instrument chip the same
way Detach and Theme already are; the bar's floor moves from 1280 to 1320 to pay for it, and
`KeysEditor::minWidthForView()` carries the updated arithmetic in its own comment, the standing
rule when a bar outgrows its floor.

The menu, four groups Owen chose, nothing else added:

- **Hold visuals during sustain**, default on, which is exactly what Keys already did.
  `PianoKeyboard::paint`'s `stateOf` reads `LayoutState::holdVisualsOnSustain`, and with it off
  a key caught only by the pedal (not pressed, not latched) rests visually while it keeps
  sounding - paint only, the `sustained` set and therefore what is actually heard is untouched.
  Octavium has this same menu item and it is wired to nothing there, so it has never once worked;
  this is the first time it does anything.
- **Sustained drag leaves a trail**, default on, also today's behaviour. Owen asked for
  Octavium's "Drag while sustain" and the name did not survive contact: Octavium describes that
  option as deciding whether a click-drag glides across the keys *at all*, and Keys' drag has
  always glided, unconditionally, on every build. A switch by that name would either do nothing
  or take gliding away, and the label promises neither. What is genuinely left to choose is
  whether the sustained run piles up behind you or stays monophonic, so the item is named for
  that and gates exactly that branch of `NoteSurface::mouseDrag`. The field keeps the name
  `dragWhileSustain`.
- **Sustained notes propose chords**, default **off**, and this one changes behaviour rather than
  just exposing it (Owen: *"sustain shouldn't propose chords should be a menu option"*). A key held
  **only** by the pedal no longer counts toward the chord the keybed is *offering* - what the live
  card names, and what an "Edit on keyboard" pad is written from. It still sounds; it just stops
  proposing. `NoteSurface::proposedChordNotes` is the filter, and a key that is also pressed or
  latched still counts, because then something other than the pedal is holding it.
  The default is the point. Keys is played with one mouse, so a chord has to be built one click at
  a time, and Latch and Sustain were the two ways to make a click stick. Reading them the same way
  let the pedal's passing notes keep rewriting the card and the pad under edit. Now **Latch builds
  a chord, Sustain plays one**, and the menu item is there for anyone who wants the old reading.
- **Check for updates**, an explicit re-check rather than a second updater. It reuses
  `updaterConfig` and reports found / up to date / failed with a small dismissable message box -
  which the existing `okstudio::updater::checkAsync` cannot do on its own, since it is gated
  once-per-product-per-process and only ever speaks when it finds something newer. The kit
  gains `checkNowAsync` and a `CheckResult` enum for this (`okstudio-juce-kit/include/okstudio/
  Updater.h`, `src/Updater.cpp`), refactored out of the same GitHub lookup `checkAsync` already
  ran rather than a parallel implementation; `checkAsync` itself, and its once-per-process
  guarantee, are unchanged. Greyed on the menu (not hidden) in Keys Host, which never builds an
  `updaterConfig` - a different product, its own release channel still to come.
- **User guide**, straight to `docs/CONTROLS.md` on GitHub (the repo URL this project already
  carries in `installer\keys.iss` and this file's own footer), and **About**, a small
  mouse-dismissable dialog reading the product name and version live off `processor.getName()`
  (so it reads "Keys Host" there by ordinary virtual dispatch) and `KEYS_VERSION` - the same
  macro the updater itself is built from, not written out a second time - plus the OK Studio
  line.

### Changed: the generator's seven source diagrams are pictures again

Owen: *"reexamine the graphic visuals on the chord generator. They don't make any sense."* He is
right, and it was one layout mistake wearing seven faces. `SourceViz` had 112 px of height and
the window's full width; each branch gave the **diagram** a square the height of the box - about
80 px - and spent the remaining ~1500 px on a row of chips restating the chord names already
printed on the sixteen tray cards below it. The informative half was tiny and the redundant half
was enormous.

That geometry caused the two outright failures rather than merely looking odd. The circle-of-
fifths and negative-harmony wheels worked out to a **24 px radius** and then drew twelve labels
around it, which is about 17 px of arc for text needing 18 - so `F C G` and `A# D# G# C#`
overlapped into a smudge. The Neo-Riemannian triad was clipped by its own frame, and its chips
read `P P P P L L L L` with no chord names, so you could not tell what turned into what.

Every chip row is deleted, the strip is 160 px, the diagram takes the width, and each source
carries a one-line legend saying what you are looking at. Algorithmic keeps its degree bars and
gains counts; Markov puts the chord under each numeral; Neo-Riemannian is now `C —P→ Cm —L→ G#`;
Progressions shows roman numerals with a bracket per repetition of the template; Planing gains a
pitch gutter and per-chord spines. The two wheels anchor left, legible at last, with the walk
beside them as pills whose **arrows carry the relationship** - the signed step distance round the
circle (`-1` a fifth flat-ward, `-2` a leap), or `root → mirror` pairs. That is the distinction
from the rows that were deleted: those were bare names with `>` between them.

One bug found on the way and worth knowing: **`ChordPad::numeral` is written only by the Markov
source**. Progressions read it directly and drew sixteen `?`; and because every numeral then
compared equal, the repeat-period search found period 1 and drew one meaningless bracket per
card. It resolves through `degree` and the mode's own per-degree quality now, with `?` as a last
resort rather than a first one.

### Changed: a chord pad chokes the other chord pads, always

Owen: *"when you click a pad it should clear other presses."* `pressChordPad` used to stop only the
pad being re-pressed unless **Exclusive** was lit, so with Exclusive off - or with Sustain holding
them - clicking round a page stacked chord on chord into a pile that is neither of them, cannot be
named, and cannot be dragged as either. The pads are one surface and one voice: the strip is a
palette you pick *from*.

Exclusive keeps its job and it is a sharper one now: whether a pad also chokes the **other**
sources, the live card's own gesture and the chord held into each arp line. Those are different
instruments, so stacking them is a real thing to want. Stacking two pads was not.

### Fixed: the live chord card only ever watched the keybed

Owen: *"I'm not able to drag the currently held chord into the chord pad"*. Reproduced on the
first try: hold a chord pad and the keys light up, while the card beside it still reads
**hold a chord**. An empty card fails `ChordPads::sourceIsDraggable`, so there was nothing to
pick up.

The card was fed from `keyboard.soundingOutputNotes()` and the MIDI input alone, and the first
of those answers only for keys clicked on the *keybed surface*. Every other chord source - a
pad, a chord held into an arp line - went straight through `KeysProcessor::noteOn` and never
touched it, so the keybed lit (that reads `isNoteSounding`) and the card did not. Two views of
the same chord disagreeing, with only one of them draggable.

`KeysProcessor::heldChordNotes()` is the read half of `stopAllChordPads`, and the symmetry is
the definition: what Exclusive can choke as "a chord" is what the card can show as one - the
pads, the live card's own gesture, and the chord handed to each arp line, on or off, because a
line that is off still takes chords in. Deliberately **not** `isNoteSounding()`, which counts
the arp's *output* and would rewrite "the current chord" as whichever step the arp is on - the
distinction `keybedLit()` already draws for the keybed lights. Also not the generator window's
800 ms audition, which is a monitor rather than something you are holding.

**It answers with one chord, not the union of every source.** It shipped as a union for a few
hours and Owen caught it at once - *"the currently held chord should disappear when you play a new
chord pad"* - because a union names the pile of everything ringing rather than the chord you just
played, which is not what a card called *hold a chord* is for. `heldChordNotes()` returns the last
source to **start** while it is still sounding, falling back to anything else still holding one so
a pad left ringing by Sustain is not forgotten. The editor then picks between that answer and the
keybed by which of the two last *changed*, because that is what "currently" means; whichever loses,
an empty answer falls through to the other rather than blanking the card.

### Added: the generator's reference card is a drag source

It held a chord and could not give it up - **Similar** and **Could follow** fill the *tray*, and
neither puts the reference chord itself anywhere. Dragging it off now commits it to a pad, the
same gesture a tray card makes, with one difference: it **copies**. The reference is the tray's
fixed point ("so when you regenerate everything, it doesn't erase your reference chord"), so it
keeps its chord however many pads it fills. That is a `From::refCard` of its own rather than a
reuse of `From::trayCell` - the two are identical to every target except on `consumed`, and
reusing trayCell would have emptied the box the first time you used it.

### Changed: the arp panel is exactly as tall as what is in it

Owen: *"there's some deadspace I want to remove at bottom"*, then, shown the same fault one view
over: *"fix arp"*. The panel took **one** fixed height for every view and page, which is what
stopped Details resizing the window on 2026-08-14. That constant was a `max` over five sums, and
the cost of a max is paid silently by everything under the tallest: the macro view carried 58 px
of dead panel, and the **Cards page carried 174** - 124 px of slot cards in a 298 px reservation.

The 18 px gap it was designed with was a fair trade. It stopped being 18 the same day it was
written, when the lane-tools strip pushed the Draw page from 258 to 298 and nobody re-measured
what that opened underneath every other view. Nothing on screen says a constant has drifted.

`contentHeight()` returns `pageHeight()` now, so nothing reserves room it does not use. The
honest accounting, because this is a real cost and not a free win: All ↔ Details moves the
window by 58 px, and paging Play / Cards / Draw moves it by up to 174. That is not the 2026-08-14
problem returning - that was 372 px on a fold - but if paging ever feels unsettled, pinning the
three pages back to one height is one line, and `ArpPanel::contentHeight` says which.

### Added: REC, because Ableton cannot record Keys onto the Keys track

Owen: *"host in ableton does not record midi"*. Arming the Keys Host track in Live and pressing
record captures an empty clip, and **it is a Live limit rather than a Keys bug**: Live records
what arrives at a track's *input*, and Keys' notes are made inside the plugin, downstream of
that input. No plugin-side setting changes it, in Keys or in any other MIDI-generating plugin.
The listener-track routing in `docs/ABLETON_LIVE.md` has always worked and still does - it is
also a second track, a re-patch and an arm, which on a one-track Keys Host set is the entire
thing that set was arranged to avoid.

So Keys records itself. **REC** and a take chip ride the *Keyboard* bar, beside Exclusive /
Sustain / Latch / All Off, and neither hides when the section folds - a stop button that folds
away mid-take is not a stop button. That bar had the room; unlike the Controls bar, whose budget
is accounted for down to the pixel, this pair costs no change to the window's minimum width.

**What is captured is the stream that leaves `processBlock`** - after the arp, after strum, on
the channels the lines sent it on. That is what you heard, and it is deliberately not what the
UI asked for: recording the note path instead would capture the chord you clicked rather than
the arpeggio it produced, which is the wrong take by exactly the interesting part.

**The audio thread writes into a ring and publishes one index**; the 50 Hz heartbeat drains it
into a vector on the message thread. Nothing on the audio thread allocates, locks, or touches
that vector. Events longer than three bytes are skipped - every channel voice message fits, and
admitting sysex would mean a variable-size slot and an allocation on the wrong thread.

**The take is trimmed to its first note**, so arming and then thinking costs the file nothing,
and it carries the tempo Keys was running at, so the clip lands on the grid. Its first *note* and
not its first event, which matters because Keys' own mod wheel and pitch bend land on the very
stream the capture reads: a wheel nudged before you played would otherwise become the take's zero
and push every note that far off the top of the clip, which is exactly what the frozen tempo
exists to prevent. A take with no note in it is not a take and writes no file, so an accidental
arm-and-stop cannot replace the one you meant to keep. Anything still ringing when recording
stops is given an end: left alone that is a hanging note Live holds until the next stop, which
reads as Keys emitting a stuck note.

**Stopping writes the file, so a take is never a thing a click can lose.** Pressing REC again
starts a fresh one, and the only thing cleared is a copy of a file already on disk. Closing the
set or deleting the plugin mid-take writes it too, from the destructor, since that is otherwise
the one click that *could* lose one. A write that fails - no folder, no stream, a read-only
Documents - keeps the take you already had and says **"Take failed"** on the chip rather than
going on offering the previous file captioned with the new take's length. Every take
goes to one fixed folder (`Documents\OK Studio\Keys Takes`) rather than through a save dialog,
and that is the point: add the folder to Live's Places once and every take afterwards is a short
drag inside Live's own browser. Dragging out of a plugin window and across the screen works too
(drag the chip), and clicking the chip shows the file in Explorer - but the long drag is the
fallback here, not the intended gesture.

`setRecording` and `writeTake` are two calls on purpose, so the capture can be tested without
writing into the user's Documents folder. `tests/TakeTests.cpp` pins the pair, the trim, the
hanging-note repair, the tempo, that arming again starts a new take rather than appending, that a
controller move before the first note does not shift the take off the grid, and that a capture of
nothing but controller moves is not a take at all.

**The take window** (`src/ui/TakePanel.h`) is what stops a take being a filename you have to
trust. Clicking the chip opens a picture of what was captured - length, note count, tempo, and
the notes as bars - with **Save MIDI as…**, **Show in Explorer**, and the roll itself as the
drag source, because the thing you are dragging should be the thing you can see. It is a *view*
and not an editor: editing a take belongs in a piano roll, Keys has a sibling for that
(Lattice), and half of one here would be a second, worse one.

**`takeNotes()` is built from `buildTakeMidiFile`'s own sequence**, not from the raw capture, so
the trim, the pairing and the supplied note-offs are applied once and the picture is provably
the bytes. That property has a test, and the test earned its keep immediately: `takeNotes` first
shipped with a 10 ms floor on note length so short notes stayed visible, which made every short
note in the preview disagree with the file. The floor belongs in `Roll::paint`, which already
floors the *bar* at 2 px. A minimum length is a question about drawing, not about data.

**The take's tempo is frozen when recording arms** rather than read when the file is built. The
file is written once, at stop; a host tempo that moved afterwards would have made every later
preview disagree with bytes already on disk. It is also simply the tempo you played to.

Unlike the generator window, the take window's bounds are **not** kept in `LayoutState`: a take
is transient, and a session reopening onto this window would be reopening onto a take that no
longer exists.

### Documented: keeping Keys on screen, and which product to reach for

Two things that read as Keys bugs and are neither, both now answered in `docs/ABLETON_LIVE.md`
rather than rediscovered:

**Keys vanishing when another track is selected** is Live's **Auto-Hide Plug-In Windows**, on by
default, plus **Multiple Plug-In Windows** being off. Worth its own section because auto-hide is
designed for effects you set and forget, and Keys is *played* - wanting it on screen while a
different track is selected is the normal way to use it here, not an edge case. Confirmed working
by Owen, 2026-08-17. Live's switch is global and Live has no per-plugin pin; Keys' own **Detach**
buttons are the nearest thing, since a detached section is a desktop window Live does not manage.
That is also why the hosted synth's GUI in Keys Host stays put while Keys' own window goes: two
window kinds, one screen.

**Which product to use** now leads the page, because picking wrong is the most common way to end
up stuck. The deciding question is whether Live should record what you play: plain Keys puts the
clip on the instrument's own track so it plays straight back, while Keys Host exists to avoid the
routing that recording requires. Those two cannot both come from one product, and the page says
so instead of leaving it to be found.

### Fixed: the editor was much more expensive to paint than it needed to be

Owen: *"sluggish overall"*, in Ableton, with the standalone fine. Three things, each defensible
on its own:

**The keybed repainted in full on every note change.** Every key is a rounded `Path`, a vertical
gradient, a clipped bevel and two seam strokes, and `paint()` rasterises all of them - up to 33
times a second while an arpeggio runs, and once per key crossed during a drag glide.
`NoteSurface` now diffs the lit keys and repaints only the ones that moved (`repaintLitChanges`,
via a new `drawnBounds` a surface may decline to answer, in which case it gets the old full
repaint). `paint()` itself is untouched: JUCE hands it the clip region either way, so keys
outside it cost a bounds test instead of a rasterisation.

It diffs a **map of state and not a set of lit keys**, which is the whole correctness of it: the
keybed draws `pressed` in a hotter accent than latched, sustained or externally sounding, so a
set says nothing changed for every move *between* those. Releasing a key under Sustain, a latch
toggle, a sustained drag glide and a key going from your own press to the arp sounding it are all
that move - the first of them would have left the key in the bright press colour permanently.
The dirty rectangle is also grown by the widest thing `paint()` draws outside a key rather than
by a guessed two pixels: a lit black key's glow is a 4 px stroke centred 2.5 px out, so it
reaches 4.5, and two pixels of margin left a ring of accent hanging in the air after note-off.

**Nothing in Keys was opaque**, so every one of those repaints also redrew the editor's
full-window gradient underneath. `PianoKeyboard` fills its whole bounds before it draws a key,
so it says so now.

**Two timers ran flat out with nothing to do.** The MCP bridge polled at 5 ms - 200 message-thread
wake-ups a second for a queue that is empty unless an MCP client is actively driving the plugin -
and now starts on demand and stops when its queues run dry. `ArpPanel` refreshed its whole
control set at 10 Hz whether or not it was on screen, and now returns early when it is not; the
lane-length repair stays outside that gate, because a session can load with the section folded.

**And three costs this pass had added itself.** The take chip stat'ed the take file twice per
30 Hz tick, forever, for an answer only a REC click can change (cached against its path now, so
a network or OneDrive-backed Documents folder is not asked sixty times a second); the take window
rebuilt the entire `MidiFile` on every tick while recording, because its identity check keys on
an event count that is by definition growing, at a cost rising with the take's length (throttled
to 5 Hz while a take runs, which is invisible on a picture of something still being played); and
`refreshLaneReadouts` ended up called twice per tick, once either side of the new gate.

In a plugin, that message thread is the DAW's UI thread, which is why this is felt in Live and
not in the standalone.

### Added: Send a chord pad to arp A or B from its own menu

Owen: *"I'd like to be able to right click on a chord pad and say send to ARP a or b"*. Two rows
on a pad's card menu, beside the Send to arp slot submenu that has always been there: the chord
goes straight into that line, which is what a card **dragged** onto that line's switch or its
macro card has done since 2026-08-02.

**An accelerator, not a new right-click-only path.** It is the drag with the aim taken out of the
mouse, exactly the relationship Send to arp slot has with a drop on a slot card, and both now run
through one method - `KeysEditor::sendPadToArpLine` - so the two gestures cannot drift.
`ArpPanel::takeChordOnLine` takes a pad slot rather than a drag payload to make that possible,
which is all the drop ever wanted out of one.

**It routes; it does not navigate.** The drop and the menu row part company on exactly one point,
carried by `followAim`: a drop *aimed* at the line, so the aim follows it and the panel starts
editing that line, while a row reading "Send to arp B" promises to move a chord and nothing else.
Sharing the drop's behaviour meant picking that row from line A's Draw page tore you off the page
and lane you had open, rebuilt every attachment, and left you several clicks from getting back.

One row per line the UI shows, so C returns here the day it returns anywhere. Both rows are live
on a line that is switched **off**, on purpose: a line that is off still takes a chord in, and
switching it on then plays what it was handed.

The menu is 11 rows and 408 px now, up from 9 and 340. Two rows rather than a submenu costing
one, because Owen asked for the two by name and this menu can afford the height.

**Binding a chord to an arp slot is undoable at last**, from the menu row and from a drop on a
slot card alike. Both replace that slot's chord, name, shape and rate in place, and an arp slot
is one of the two trees undo covers - Copy slot and Randomize pattern beside them have always
pushed an entry, and these two never had. The menu's own id ranges moved into `ChordPads.h` with
a `static_assert` that they stay disjoint: the slot ids and the new line ids were bare literals
100 and 120 at their call sites, and growing `numArpPatterns` past 20 would have run one range
into the other with nothing failing to compile and "Send to arp A binds the pad to slot 21" as
the symptom.

### Fixed: the hole a taken tray card leaves can be filled again

Owen: *"when you are generating chords and you move one off, there's an empty space, and then you
can't regenerate it"*. **Clicking an empty tray cell now generates a chord into it and auditions
it**, so taking a card and getting another one back is the same gesture twice.

The hole itself was never the bug and has not changed: it is the record of which of the sixteen
you have already taken, and it is what gives Fill something to do. The bug is that the way back
was **invisible and unaimed**. Fill on the tray's header did reach it, but Fill does every empty
cell at once; Regen deliberately rerolls only the cards you kept; the cell's right-click menu
returned before it was built because every one of its eight rows needs a seed chord; and
`ChordTray::mouseDown` returned before even reaching that. Meanwhile the cell painted as a plain
unmarked well - no hover, no mark - so it read as scenery rather than as somewhere to click.

So: an empty cell hovers like a card and carries a `+`, a left click fills and sounds it, and a
right click offers the two rows that need no seed (New chord here, Fill every empty card) instead
of nothing at all. The tray's caption says the third gesture out loud.

**A one-cell fill no longer clears the whole tray's stale caption.** `writeInto` stamped the
settings signature on every write, so filling a single hole declared the other fifteen candidates
fresh: sweep Source to Markov with a full tray, take one card, click the hole it left, and the
warning vanished while fifteen chords from the old Source sat there unchanged. It now stamps only
when the write covered every chord on screen, which is stated exactly rather than by a flag per
caller - a cell carrying a chord this call did not write is a cell the current settings have never
seen. Regen and a Fill of an empty tray still clear it; a one-cell fill and a Fill around cards
you kept do not.

**The freshly generated card lights while it sounds.** The hole-filling press clears `pressed` at
once so the card cannot be dragged by the press that made it, and the lit state was keyed to
`pressed` - so the one card you had just asked for was the only audition in the window that stayed
dark for its whole 800 ms. There is a separate `auditioning` cell now, which is what paint reads.

**A middle-click over the tray does nothing again.** The old guard tested "no cell, or an empty
cell" and so made every non-left button inert as a side effect; once a hole became a live target
that press started rolling a chord and taking the room from every sounding pad. The left-button
test is explicit now, ahead of both the hole path and the audition below it.

### Added: Undo and Redo

Owen: *"we should have undo"*. **"There is no undo anywhere in Keys" was the stated reason for
at least four design compromises** - Reset beside Roll, Clear page living in a window rather
than on a bar, locks on pads, and the drag guard in `ChordPads::mouseUp`. All four stay, because
all four are still good behaviour; they simply stop being load-bearing.

**Content only, and that is the design rather than a shortcut.** Undo covers what destroys
music - chord pads, arp lanes, arp slots - and deliberately not parameters. Sweeping the rate
dial would otherwise push forty entries onto the stack and shove the pad you actually wanted
back off the end of it, which is an undo that cannot undo anything. A knob you can always turn
back; a cleared pad you cannot.

An entry is a **snapshot of the affected subtree before the edit**, taken with the same
`chordPadsToTree` / `arpToTree` the session file already uses. That is what makes it affordable:
no action needs a hand-written inverse, so no action can have a *wrong* one, and anything added
later is undoable the moment its data lands in one of those two trees.

**One entry per gesture, not per change.** A lane drag pushes on the press and not again, or a
single stroke would fill the stack by itself; `KeysProcessor::UndoGesture` is the RAII guard
that lets a high-level action (a drop clears a pad and then sets it) cost one entry rather than
two. Depth is 32, oldest dropped first.

Covered: clear / overwrite / move a pad, capture the live chord, drop a tray candidate, Fill,
Regen, Clear page, drag a card off the strip, Roll, Reset, Randomize, Euclid, copy a slot, draw
a lane, mute steps.

**Undo and Redo ride the Controls bar**, at the left end, because that bar never hides with its
fold - the same rule that keeps the tempo and Root on it. An undo you cannot reach because a
section is collapsed is an undo you cannot trust. They grey rather than vanish when their stack
is empty, so the pair never reflows the bar under the mouse, and each carries a tooltip naming
what would come back - which is why every push site passes a label rather than a bare marker.

Undoing releases every sounding chord first (`stopAllChordPads`), for the same reason an
audition does: restoring pads can rewrite the chord a sustained card is holding, and restoring
the arp can rewrite the lanes under a running line.

Six new test cases (211 total, 3,801 checks), including the one that matters most - an open
gesture costs one entry however many edits are inside it.


### Added: tests that could have caught this week's bugs

Two new test files, and the link that makes them possible: `Keys_tests` now links the plugin
itself, so a test can hold a real `KeysProcessor` and a real `ArpPanel`.

**`StateTests.cpp` pins the migrations.** That mechanism was silently dead for months - the
kit's `state::load` handed `replaceState` a shared node, so `onExtra` saw a tree in which every
parameter existed whether the session saved it or not (fixed 2026-08-14, kit PR #6). Nothing in
Keys exercised a migration, so a dead one and a working one looked identical from outside. Each
test now loads a session with a parameter *removed* and asserts the migration noticed:
`migrateVelTrim` folding Volume 25 into VelTrim -50 through the same curve, `migrateTuplet`
folding a set Trip into Triplet and retiring it, the spans and Drift resetting to their defaults
rather than inheriting the live value, and `bpmSync` backfilling while a *saved* off survives
untouched. Every one of these would have failed before the kit fix.

**`LayoutTests.cpp` pins the layout rules.** Every bug of 2026-08-14 was a layout bug and the
engine suite caught none of them - the Chain tab at zero width, Mute's phantom tab eating a
cell, the Voice button at 22 px, three lanes at the wrong length. Screenshots and UI Automation
found all four, which needs a running app. The rules those bugs broke are now tests: no visible
control is starved in any view or page, all twelve lane tabs are laid out at the same width and
above their floor, the panel is one height in every view and page, and opening the panel repairs
lanes that disagree about length while leaving a deliberate polymeter alone.

They are **rules, not pixel snapshots**: a snapshot of a layout still being designed fails every
time the design moves, which trains people to delete tests.

201 -> 206 cases, 3,787 checks.

### Fixed: a chord card let go mid-drag destroyed the chord

Dragging a card off the strip clears it, which is the documented gesture. The test for it was
"nobody claimed the drop" - so letting a card go anywhere that merely *happens* not to be a drop
target destroyed the chord: the gap between two sections, a bar, the keybed. Changing your mind
mid-drag and putting the card back down on the strip is the commonest way to do that, and it is
the one gesture that most obviously should not delete anything. There is no undo anywhere in
Keys.

The release now has to have actually left the strip as well. A locked card was already immune,
and still is.


### Fixed: lanes of different lengths, and a draw that slid sideways

Owen: *"Sometimes the steps do not match each other"* and *"when you're drawing, I don't want
you to be able to jump from step to step. I just want it to be for that one when you're moving
up and down."*

**Every lane shares a length again with Link on.** `nudgeLength` has always written all of them,
which was never the problem - the problem is lanes that were not there when it last ran. Rand,
Mute and Chain were appended on 2026-08-14 and arrive at `ArpPattern`'s default 8, so a pattern
whose other lanes were at 16 or 32 had three lanes a fraction of the length of the rest, drawn
as a different number of cells with nothing to say why. A session saved before any of them has
the same hole. `ArpPanel::enforceLinkedLengths` now pushes the Note lane's length and speed onto
every lane whenever the readouts refresh, so the repair happens on load and on every lane
change rather than waiting for the next nudge. Link **off** is polymeter and is left alone
entirely - that is the whole point of the switch.

**A draw gesture edits one step.** `LaneGrid`'s drag painted whatever step was under the
pointer, so a hand moving up to set a height and drifting sideways on the way rewrote the
neighbours it crossed. The step is captured on the press and held for the rest of the gesture;
horizontal travel is ignored. The MUTE row still paints across steps, and should: there the
value is a toggle and a swipe means "all of these", where here it is a height that the pointer
has to travel vertically to set.


### Added: a richer Note lane, a Chain lane, and a selection for the grid

The three unbuilt ideas `docs/REFERENCES.md` ranked, built.

**The Note lane speaks Kirnu's vocabulary** (its manual p9, the ORDER lane). Alongside `-1` rest,
`0` follow-the-shape and `1..8` a fixed entry, it now has **P**rev (repeat what last sounded),
**H**ighest, **L**owest and **R**andom. Those four *ask the chord a question* rather than
counting into it, so they keep meaning the same thing when the chord under them changes - Hi is
the top note of whatever is held, not entry 3. Appended **above 8**, not below -1, for two
reasons: every saved session's values stay exactly what they were, and dragging a cell to the
bottom of the grid still reaches the rest rather than landing on a mode.

**The Chain lane** is Stochas' chain dependency (its manual p3) in the one form that needs no
second coordinate: `0` always, `1` only if the step before it sounded, `2` only if it did not.
Chance says "maybe"; this says "only if", and that is what turns a probabilistic pattern into
one that answers itself. It is **the only thing in the engine that is not stateless from the
playhead** - a condition has to remember one bit about the previous step - and it self-corrects
within a single step, which is the cheapest possible break of that rule and is why this form was
chosen over an arbitrary cell-to-cell reference.

**Select** is the missing primitive (Kirnu's tool palette, p8, whose Random tool acts on
*selected* steps). With it lit a drag on the grid marks a span instead of drawing on it, and
Roll and Reset narrow to that span. A **mode**, not a modifier: the mouse-only contract has no
Alt-drag to offer, and Kirnu itself models this as a tool you pick.

**The lane tools moved to their own strip.** Twelve tabs at the 70 px floor need 884 px and the
row had 784 left beside the buttons, so Rand was squeezed to 63 px and Chain was laid out at
zero width - present in the tree, invisible on screen, absent from the accessibility tree with
nothing to say why. *A crowded row grows a strip; it does not squeeze its targets* - the rule
logged twice before and paid for a third time. All twelve tabs are uniform again, and the Draw
page went 258 -> 298, so the window grew 40 px once and stopped.

Five new test cases (195 total, 3,607 checks), pinning Hi against two different chords, Prev
holding a note, both chain conditions, and chain-0 being bit-identical to the feature not
existing.


### Added: a Rand lane, and mute stops eating your steps

Owen: *"look at reference manuals"*. `Cthulhu_Manual_v1_1.pdf` is in the repo root and page 25
says two things Keys had wrong.

**Cthulhu's randomness is a lane, not a knob.** Its "Rand Sel" tab is a graph where you draw
*how random each step is* - default centred (no randomising), above centre the output step may
come out higher than the one assigned, below centre lower, and the size is how far. So step 3
can be locked and step 7 wide open. Roll and Drift are both global; this is per-step, which is
the whole point.

`laneRand` is that lane, appended as lane 10, bipolar -8..+8, default 0. It is **the one
randomness in Keys allowed to change which note plays**, and the reason is that you drew it on
that step: it is intent, where Drift is a knob wandering over a part you did not aim at. It
applies only to a fixed Note index - a Note lane of 0 means "follow the shape", and randomising
the number zero would silently turn Up into a fixed entry.

**Mute is its own lane** (`laneMute`, lane 11). It used to toggle the Note lane between -1 and
0, which destroyed whatever that step held - and the manual names preserving it as the reason
mute buttons exist at all: *"you can experiment with mutes, without losing the set-value of the
steps, in case you wish to undo this action."* Note keeps its value now and the mute lane says
whether the step is heard. A Note of -1 is still a *drawn* rest, which is a different thing and
stays; Cthulhu has both too.

The mute lane is the Note lane's companion rather than a polymetric lane of its own: it reads,
writes and syncs at the Note lane's length. The engine wraps every lane read by that lane's own
length, so if the two disagreed a mute drawn at step 20 of a 32-step pattern would read back
modulo 8 and silence the wrong step. It gets no tab - the MUTE row under the grid has always
been its editor, and a tab as well would be two ways to draw one lane.

**Both are append-only.** A lane's index is what a saved session stores it under, so an older
session simply has no child for 10 or 11 and reads back as `ArpPattern`'s defaults - inert,
both of them. Old sessions' mutes are `-1`s in the Note lane and still play as rests,
identically; they are not migrated, because a drawn rest and a mute are now different things
and guessing which one an old `-1` meant would be inventing intent.

Three new test cases (190 total, 3,560 checks).


### Fixed: a value at the edge of its lane ignored Roll and Drift

Owen: *"0 value seems to ignore roll"*. Both built a window of
`[value - reach/2, value + reach/2]` and **clamped the result**, which is fine in the middle of
a lane and broken at its edges: a value sitting at the bottom had half of every draw fall
outside the range and clamp straight back to where it started. Late, Harmony and Chord all
default to 0 and Chance sits at its *top*, so "a lane of zeroes barely moves" was the common
case, not a corner one.

`ArpEngine::strayWithin` is the one copy of the rule now, and **the window slides instead of
the result clamping**: the full width of the draw survives wherever the value sits, and it only
narrows if the reach is wider than the lane itself. Pinned by a test at both the floor and the
ceiling.

### Added: Reset, and the whole arp panel takes a chord

**Reset** sits beside Roll on the Draw page and writes the lane's own default across its whole
length - the state a lane is in before you touch it. Roll is destructive and Keys has no undo
anywhere, so a roll you did not want cost a redraw until now.

**The panel itself is a drop target on every page** (Owen: *"need to be able to drag chords to
not just the main arp window"*). This is a paging regression: the slot cards moved to the Cards
page and the macro cards only exist in the All view, so on Play or Draw the only target left
was a 40 px letter on the bar. A chord dropped anywhere on the panel goes to the line being
edited; a slot card still wins on Cards and a macro card still wins in All, because JUCE walks
up from whatever is under the pointer and those are deeper. The card outlines while a chord is
over it, so "anywhere here" is visible rather than something you have to be told.


### Added: Roll, Drift, and per-step odds that say what they are

Owen: *"there should be, like, a more random feature in the drawing, like cthulu"* - three
answers, because the ask splits three ways and they are different things.

**Roll** sits in the lane-tab row on the Draw page and rerolls the lane you are *looking at*,
straying from what is drawn by its own amount (5..100%: a nudge low down, a uniform scramble at
100). Randomize on the Cards page stays and is the other kind - six lanes at once, to a musical
recipe, for a part you did not have. Roll is what you reach for on a lane you already like.

It is also a regression fix: Randomize used to sit in the action row directly under the grid,
and paging the deep view put it on another page, so the lane and the button that rerolls it
were never on screen together.

**Drift** (`arpDrift`, appended, default 0) is the opposite of Roll in every way: it strays
from the lanes *while it plays*, so the part never repeats exactly and the lane on screen never
changes. It rides the FEEL group on the Play page beside Humanize, because the two are the same
question asked twice - Humanize is a **player** wandering (late and quieter, never early and
never louder) and Drift is a **machine** wandering (either way, evenly around the drawn value).

The rule it rests on is one line: **drift changes how a step plays, never which note it
plays.** Octave, velocity, gate, lateness and chance wander; Note, Ratchet, Harmony, Chord and
**Transpose** do not. Transpose was in the drifting set for one build, and the new
"pitches are unchanged" test caught it on pitch class - transpose moves by scale degrees, so it
picks a different note however you look at it. Octave stays in, because whole octaves keep the
pitch class.

`ArpEngine::laneRanges` is now the one copy of what each lane can hold, shared by the grid that
draws a lane, the reroll that randomizes one and the drift that strays from one. A
`static_assert` ties the Chord lane's high end to `ChordTable::numSlots`.

**The Prob lane is called Chance**, matching the CHANCE knob on the Play page that it
multiplies with. Two names in two places for one idea was most of why per-step odds were not
findable.

Four new test cases (186 total, 3,033 checks).

### Changed: a line's deep view is three pages, and the window stops resizing

Owen, looking at the un-paged view: *"when you click details it shouldn't resize the whole
window, just the full arp section. and we need a way to get out the detail view"*, then, asked
where the height should go instead: *"can we simplify the detail view or organize into pages"*.

The deep view used to be every block at once - the band's two rows, the ten lane tabs, the
grid, the mute row, the twelve slots and the action row - which came to **612 px** against the
macro view's 240. So clicking **Details** grew the *window* by 372 px and clicking **All**
shrank it back, and on a screen that could not afford the 372 the keybed lost it off the
bottom instead.

Split by what you are doing rather than by what fits, the blocks come apart cleanly:

| page | contents | height |
|---|---|---|
| **Draw** | the ten lane tabs, the selected lane's grid, the mute row | 258 |
| **Cards** | the twelve slots, Copy / Clear / Stop / Randomize / Euclid / Clocks / Chain | 124 |
| **Play** | the band's two rows: Pattern, Playback, Steps, Spread, Feel | 208 |

The tallest is 258 - eighteen more than the macro view, and **354 less** than the un-paged deep
view - so the panel now takes one fixed height (`arpFixedH`) for every view and page it has and
the window does not move between them at all. `contentHeight()` returns that constant and feeds
the editor; a new `pageHeight()` feeds `cardBounds()`, so the drawn card is only as tall as the
page showing and the leftover is panel background rather than an empty box.

The page tabs ride the **ARP section bar**, right of All, which is what makes paging pay for
itself: the bar is 34 px that already exists, so the picker costs the panel no height. They
appear only in a line's deep view, so the bar reads `A B All` in the overview and
`A B All Play Cards Draw` in a page - which is the answer to *"we need a way to get out"*.
All is not a third letter beside A and B any more, it is the first entry of the view group and
visibly the way back. Draw greys outside Pattern shape rather than vanishing, and leaving
Pattern with it up falls back to Play. The page survives a fold and a session
(`LayoutState::arpPage`), and **defaults to Play**: Draw does nothing until you have drawn on
it *and* set Shape to Pattern, so opening there is opening on a blank page with no way to tell
why. The tabs were Steps / Slots / Setup for one build - five letters each, all starting with
S, which is unreadable at a glance. These name what you do.

**Voice moved to the lane-tab row**, where it costs no height at all and sits beside the
Harmony lane it is contextual on, instead of the 42 px row inside the STEPS group it had for
one day. It reads `Voice: Chord` / `Voice: Sub` and carries its own word now: a 12 px caption
over it left the button 22 px, under the mouse-only floor. Its cell is reserved out of the row
before the tabs take their cut, and reserved whether or not it is showing, so the ten tabs do
not resize under the mouse when you select Harmony.

### Added: a Euclidean rhythm generator, MCP-only for now

`src/EuclidGen.h` is a pure, allocation-free `euclidHit(i, hits, steps, rotation)` (Bjorklund's
algorithm, the Bresenham-line formulation), plus the new MCP tool `apply_euclid`, which writes
the result into the active pattern's probability lane - 100 on a hit, 0 on a rest - and sets
that lane's length to `steps`. Only the probability lane has a hit/rest mapping that means
anything, so this is the one lane it touches; a mouse-only UI for it is a later pass.

### Added: Subharmonicon-style rhythm dividers, up to four per line

Up to four dividers (1..16, 0 = off) per arp line, live as plain atomics on `ArpEngine`
(`rhythmDiv`) the same way the step lanes already are - no new APVTS parameter. With any
enabled, a step boundary fires only if it is a multiple of at least one of them (an OR of
clocks, not a shared modulus), and the lanes advance one step per boundary that actually fires
rather than skipping ahead by whatever the divider passed over. The position is computed fresh
from the raw step index every time (`ArpEngine::firedCountBefore`, by inclusion-exclusion over
the enabled divisors), so a transport jump lands on the right step instead of drifting. All
four off, the default, is bit-identical to the feature not existing. Persisted per slot as
`rhythmDivs`; readable and writable through `get_arp_pattern` / `set_arp_pattern`.

### Added: a subharmonic mode for the Harmony lane

`harmonyMode` (0 = today's chord tone above the played note, 1 = subharmonic) switches the
Harmony lane's second voice to the undertone series below it instead - f/2 down to f/8,
quantized to 12-TET - meant to be heard with Scale Lock off, since it deliberately leaves the
chord. A voice that would clamp onto the note it is harmonizing (running out of MIDI range at
the bottom) is dropped rather than folded back on top of it. Same storage and MCP shape as the
rhythm dividers: a live atomic on `ArpEngine`, persisted per slot, no new parameter.

### Added: mouse-only controls for the Euclidean generator, the rhythm dividers and the subharmonic voice

The UI pass the three generative additions above were waiting on. **Euclid** and **Clocks**
are two buttons on the arp panel's action row, each opening a strip of steppers above it -
Hits/Steps/Rotate for Euclid, four dividers for Clocks - rather than a dialog; opening a strip
previews nothing, only a stepper click writes, and the two are mutually exclusive so the panel
never grows by more than one strip at once. Euclid stays scoped to Pattern shape, since it is
writing into the probability lane; Clocks stays available in every shape, since the dividers
act regardless of what Shape draws, and its own button retitles to "Clocked" while any divider
is running, the same way Chain retitles to "Chaining". **Voice** switches the Harmony lane's
second voice between chord tones and the subharmonic series, and is the panel's first control
that depends on which lane is selected rather than which shape is - it shows up in its own row
under Steps and Speed/Link, but only with the Harmony tab itself picked.

### Changed: twelve pads a page, with Strum and Humanize in the columns that freed up

Owen: "reduce the pads grid to 12 and move strum and humanize into that with the same style."

The chord strip is **two rows of six**, and the two columns it gave up carry **Strum** and
**Humanize** as `RangeKnob`s - the knob is the top of each range and the lamp beside it opens
and closes the range. Both were already ranges (a two-handle `RangeSlider` each), and both shape
what a chord *pad* does, so the strip is where they belong; the bar and the band were only where
they fitted.

**The lamp is also the switch**: a click turns the feature on or off, a drag still sets the
range, and unlit means off (Owen: "clicking the blue satellite button should turn on or off the
feature. And then I don't think we need the humanized check mark anymore"). So Humanize's tick
box is gone - same parameter, same meaning, one fewer control. Strum needs no on/off parameter,
since a strum of zero *is* off; its lamp parks the range at zero and puts back what it was.
Four pixels of slop separate a click from a drag, because a click on a mouse-only surface is
allowed to be untidy. **Switched off, the knob stops looking like a range** - the arc goes back
to an ordinary one and the readout drops to a single number, since an unlit lamp over a range
arc is the control saying two things at once.

Strum's **Dir** combo became a `<` `>` pair beside the caption, and the caption reads the live
direction (`STRUM UP` / `STRUM DOWN` / `STRUM RAND`) so nothing else has to. They **wrap**,
unlike the arp's steppers: three values with no scale to them are a ring, not a ladder. With
both the tick box and the combo gone, the row under each knob went too and that height is the
face's - it lands near 60 px, comfortably past the arp's 40.

One oddity this surfaced rather than caused: **with Humanize off, Keys plays the band's
midpoint**, not the knob's value. The readout says that midpoint, so the number under the knob
is what you hear; the knob itself still points at the top of the band.

**This drops pads, and it is not reversible.** 12 x 4 pages is 48 slots where it was 64.
`chordPadsFromTree` re-bases a saved session's slots into the current page width, and that code
was written for 8 -> 16, where every old position still had a home. Narrowing does not: the old
formula wrapped positions 12-15 onto the *front of the next page*, where they silently
overwrote that page's own pads as the loop went on. A pad past the end of its page is now
dropped instead, so each page keeps its first twelve and loses its last four. Owen's call, asked
before it was built.

With Strum gone the **Controls band has no rows left** - it was down to Strum and its direction
after the 2026-08-02 passes - so that section is now its CC knob row alone. The Pads bar gets
back the 232 px Humanize held, which goes to the section caption.

### Added: `RangeKnob`, and Humanize Time and Velocity became ranges

Owen: "for the humanized time and velocity knobs, I want to build a serum style knob where you
can set a range in the knob. In serum they have like a little light next to it that sets the
range ... I think this is gonna be a reusable component."

**`src/ui/RangeKnob.h`** is a rotary that holds two values: the knob face sets one end and the
span reaches back from it. **The knob's own arc is the range** - the lit stretch runs from the
range's bottom to its top, and **travels with the face**, so turning the knob moves the whole
range at its width. There is no second ring: a concentric one outside the face was tried and
was one ring too many.

Drawing it is one new line in the skin. `KeysLookAndFeel::drawRotarySlider` already computes
where a lit arc *starts* - normally zero, or a bipolar knob's centre - so a slider can now
override that proportion through the `skin::arcFromProperty` component property, and
`RangeKnob` sets it to the range's bottom. Nothing is subclassed and no copy of the knob's look
has to be kept in step.

Painting over the arc afterwards was tried first and does not work: Keys draws a value arc as
**three strokes** - a halo at 2.1x the line width, a body at 1.15, a hot core at 0.55 - so a
mask sized to the line leaves the halo's edges showing all the way round. Owen's word for it
was "a shadow of blue on the inner ring that isn't just the range".

It is built from Serum's manual rather than from a guess at its screenshot, and the manual
corrected the guess. Page 195: *"A smaller blue halo appears to the top left of the knob...
Click and drag the arrow control to change the modulation depth amount. As you drag the arrow,
notice how the halo shrinks or expands to show the range of modulation."* So the grab is a
**satellite at the top left**, dragged vertically - not a dot sitting on the ring. That detail
is what makes it buildable here: a satellite is a component of its own, so it can be sized to a
real target instead of Serum's few pixels, and it goes in the corner a round knob leaves empty
in a rectangular cell. It is a child *above* the face in z-order, because a Slider eats every
press inside its rectangle, corners included.

Two departures from Serum, both forced by the mouse-only contract. Serum's fallback for the
fiddly satellite is Option/Alt-click-drag on the knob body; a modifier is not a gesture Keys may
require, so the fallback here is that **the whole margin around the face drags the span too** -
every pixel the face does not cover, corners included. The satellite is the affordance; the
margin is the forgiveness. And there is no negative span: Serum flips the halo's hue for an
inverted depth, but a range has nothing to invert into, so a `Direction` picks which side of the
value it reaches instead. The satellite itself is a **plain LED** - solid, lit, unchanging. It
carried a miniature arc filling with the span for one build (the span drawn twice), then an
outline and a pip (which read as a tiny knob); the knob's own ring is the one that reads.

The span is not a parameter the component owns - it comes in through `setSpan()` and goes out
through three callbacks - so a consumer keeps the parameter, the gesture brackets and the undo
story in one place. Written kit-ready but kept in Keys for now; promoting it means moving the
file beside `okstudio/RotaryKnob.h` and swapping `skin::` for the theme's tokens.

**`arpHumanizeSpan` and `arpHumanVelSpan`** (and their B and C twins) are the spans, appended,
default 100. Humanize always drew uniformly between nothing and the knob; now it draws between
`knob - span` and `knob`, so the knob keeps meaning "the most this ever does" and the range
travels with it. A line can then be *always* a little late and a little softer with the
variation on top, instead of everything anchored to dead-on. Span closed is a fixed offset with
no randomness left; span at 100 puts the floor at zero wherever the knob sits, which is exactly
what these did alone and is what makes them safe to append. The engine clamps the floor to its
own ceiling, since either can be automated past the other and it is the only place that sees
both at once. `migrateHumanSpans` backfills the defaults for an older session, the
`migrateRateMode` shape - and matters more than most, since the default is the *top* of this
range, so an absent parameter would inherit something narrower rather than wider.

The macro card's knob row is 16 px taller to hold the rings, and the two range knobs reserve
their ring width out of the row rather than taking it off a neighbour - so the face inside a
range knob is exactly as wide as every plain one and the row still reads as eight knobs of one
size. `ArpTests.cpp` pins the behaviour (169 cases, all passing), including the half that is
easy to build the other way round: halving the knob with the span closed halves the offset, so
the range provably travels with the dial rather than growing up from zero.

Not done: the band's own **Human Time** and **Human Vel** sliders, on a line's Details view,
still set the ceiling alone - they are linear sliders in a four-cell group, not knobs, so the
floor is reachable from the macro card only for now.

### Changed: Trip became a Tuplet combo, and the rate readout is a plain fraction

Owen: "I think when triplet mode is enabled the division text should reflect. what if I want
1/5 or other division?", then, on the first cut: "confusing UI. it's a check box but it changes.
fraction confusing too. shouldn't it just be 1/5 not 1/4:5?"

Two problems, one of them the other's cause. The dial's readout came straight from the
`arpRate` choice parameter, which knows nothing about Dot or Trip, so a line playing dotted
triplet 1/8s said "1/8" - and once the readout cannot describe a modified rate, there is not
much point adding more modifiers.

**The readout is now the step length as an exact fraction of a bar**, `ArpEngine::rateSyncText`:
`1/8` straight, `1/12` in threes, `1/10` in fives, `1/5` for a quarter in fives, `1/8.` dotted,
`1/10.` for both. This is the one notation that survives tuplets. The convention every DAW uses,
`1/16T` and `1/16D`, has a letter for triplets and dotted and *no form at all* for a quintuplet,
which is exactly why the first cut invented `1/4:5`; the fraction needs no letters, because a
quarter-note quintuplet is five in the space of four quarters, which is four fifths of a beat,
which is one fifth of a bar. FL Studio's grid ("1/3 beat", "1/6 beat") is the same system. Dot
stays a dot rather than folding in - a dotted 1/8 is 3/16 of a bar, but `1/8.` is universal and
`3/16` has to be worked out. Straight, every reading is byte-identical to the division names the
parameter already carried. Hz is untouched: the engine ignores both modifiers there, so
"4.00 Hz" was already the whole truth. Installed onto the dial after every attachment swap,
since `SliderParameterAttachment` writes `textFromValueFunction` in its own constructor.

**Trip is now Tuplet, an ordinary combo box** - Straight / Triplet / 5-tuplet / 7-tuplet /
9-tuplet - on the band and on each macro card. It was briefly a check box that cycled its own
text, which was a control lying about its own shape; a combo is what Keys already means by
"pick from a list", the same idiom as Shape, Distance and Retrigger, and it takes an ordinary
`ComboBoxAttachment` where a button could not bind a choice at all. A tuplet is N steps in the
space of the largest power of two at or below N, so Triplet is exactly what the old toggle did.
Dot stays a separate control: it lengthens a step by half where a tuplet divides a span, and the
two compose. Both grey out in Hz as before.

`arpTuplet` (and `arp2Tuplet`, `arp3Tuplet`) is **appended** to the parameter layout, so a
saved session still loads. `arpTrip` stays registered but is read by nothing: `migrateTuplet`
folds a set Trip into a Triplet - exact, not approximate, since `tupletFactor(3)` is the
same 2/3 the old branch multiplied by - and returns Trip to its default, the same retirement
`migrateVelTrim` gave Volume. `ArpTests.cpp` pins the notation and the multiplier for every
value, the two axes composing, and the span identity (168 cases, all passing).

Fixed on the way past: switching the panel from one line to another **left the rate dial
attached to the line you had just left** whenever both were in the same rate mode.
`refreshRateMode()` early-outs when the mode has not changed, and the dial's attachment lives
there rather than in `buildAttachments()`, so it was the one control that never rebound.

Not built, and deliberately: folding the tuplets into the rate list itself, so the dial walks
1/4, 1/5, 1/6, 1/8, 1/10 and the second control disappears. It is the cleanest reading of "it
should just say 1/5", but the list goes to about two dozen entries, which turns 1/4 to 1/64
from four stepper clicks into fifteen - and the rate's click-only path is not something to make
four times longer. Keeping the division and the tuplet as two short controls is also what Reaper,
Cubase and Studio One do. See `docs/ARP_DESIGN.md`.

### Added: a Tempo Sync toggle, and labels for BPM, Voices and MIDI Ch on the Controls bar

Owen: "BPM and Off and one in the controls header needs labels. and we need BPM sync toggle to
sync with DAW."

Keys already followed the host's tempo whenever the transport rolled and reported one - the new
`bpmSync` parameter (default on, appended last, `migrateBpmSync` backfills it for an older
session the same way `migrateVelTrim` does) is not what adds that, it is the escape hatch from
it. Off pins every arp line and the chain clock to the "bpm" control even while the host rolls;
on reproduces exactly what Keys always did. `ArpEngine::Params::followHost` carries it into the
engine, `KeysProcessor::advanceChainClock` carries it into the chain, and neither touches the
Hz rate path, which was never listening to the host's tempo to begin with.

A **Sync** chip beside the tempo field is the on-screen switch, and while it is on and the host
is actually the one setting the tempo this block (`KeysProcessor::hostTempoLive()`, published
next to the existing `arpBeatsBpm`), the field shows the host's own number and its drag and its
`<` `>` steppers grey out - none of the three can change anything while the host owns the tempo.
`BpmField::paint` dims itself for this rather than relying on the LookAndFeel, which it never
consults.

**BPM**, **VOICES** and **CH** are now honest captions rather than bare controls: BPM gets its
own label beside the tempo field, and Voices and CH are captioned in *both* of the bar's
existing width tiers now, not only the roomy one Keys Host never reaches. Root's caption -
which Owen did not ask for - stays roomy-only and is the one that drops first under width
pressure, exactly the priority order asked for.

Fitting BPM's label, the Sync chip and two more captions onto a bar that already had 87 px of
slack at the old 1070 px floor and no more took 186 px, not 87, so **the floor is 1280 now**.
The arithmetic is written out in full in `KeysEditor::minWidthForView()`: at 1280 the ordinary
day clears every caption including Root's with the Instrument chip at its full width, and the
one day the update button also claims its 170 px, Root's caption is what gives way and the chip
shrinks to a still-comfortable 85 px rather than being starved to an ellipsis.

### Changed: the arp bar's A/B tabs are now the line On switches

Owen: "the A and B on the left side of the header, I want those to be on and off buttons to
turn on or off the ARP ... we can remove the a and b check mark on the right side of the
header."

The lettered On chip that used to sit beside Hold off is gone; the A/B tabs at the left end of
the arp bar are the switch now, each bound to that line's `arpOn` / `arp2On` parameter through
an ordinary attachment. They no longer navigate the panel - that job moved to each macro card's
own Details button, added the same day (see the next entry) - and because they are the arp's
own power switch they never hide with the section, the same "reach for it while playing" case
BPM and Quantize have always had on this bar. The All tab is unaffected: it still only chooses
the macro view, so it still hides and collapses its cell when the section folds. Every
chord-drop behaviour on A and B is unchanged, including dropping onto a line that is switched
off.

### Changed: the arp macro card drops its own On toggle; an off line is scrimmed, and gains a Details button

Owen, the same ask, continued: "and if it's turned off, gray it out below ... maybe we can add
another button on the bottom by anchor, like details, and that can open up the detailed
arpeggiator view."

`MacroRow::onButton` - the small on/off toggle each macro card carried, bound to the same `On`
parameter the bar's A/B tabs now answer to - is deleted outright: two switches for one
parameter, one of them buried in a card, was a control to get wrong twice. In its place,
`MacroRow::paintOverChildren` scrims the card body (not the `LINE A` / `LINE B` caption strip,
which stays legible) with a translucent fill whenever that line is off, skipped while the card
is a drag-and-drop target so a drop highlight is never muddied by it. Nothing on the card is
ever `setEnabled(false)`: every knob, the rate dial and the card itself as a drop target stay
fully live while greyed, both so a rate can be dialled in before switching the line on and
because a chord dropped onto an off line has to land (a line that is off still takes chords in).

Each card also gains a **Details** button beside Anchor in its bottom sub-row - the only way
left from a macro card to that line's full detailed view (the band, and the step editor on
Pattern shape) now that A and B navigate nothing. It calls the same `setEditLine` a tab click
used to call. The per-line panel itself gained a small `LINE A` / `LINE B` caption in its own
top margin, drawn only outside the macro view, so something on screen still says which line you
are editing.

### Changed: Size, Octave and Humanize move off the Controls band; the Knobs chip is gone

Owen: "I think we can remove the octave setting and the size can go down to the header of the
keyboard button", and later the same day, "remove the knobs button and make the knobs visible
when you open controls."

**Size and Octave** left the Controls band for the **Keyboard** bar, which never hides with
the section - Octave is the keybed's only pitch-range control, and folding the band away is
exactly when you still want it. Octave is a `<` value `>` stepper rather than a slider (a bar
control is 24 px tall, under what IncDecButtons needs), reading "+2" / "0" / "-3". **Humanize**
and its velocity range left for the **Pads** bar instead, at Owen's pick, reworded to fit a
much narrower cell. With both rows emptied, the Controls band drops to a single row (Strum and
its direction), shrinking the section by 60 px. **The Knobs chip** that folded the CC knob row
is deleted outright; the row is unconditional now whenever Controls itself is open. A session
saved with the knobs hidden opens with them visible again - there is no control left that could
turn them back off, so the persisted flag is ignored on load rather than honoured.

### Added: an Instrument chip on the Controls bar; Keys Host's own top bar is gone

Owen: "the load instrument section with all that should go in the controls submenu."

`KeysEditor` grows `onBuildInstrumentMenu`, `instrumentName` and `refreshInstrumentChip()` -
a host that embeds Keys (Keys Host) can set the first two to get an Instrument chip on the
Controls bar; plain Keys never does, so the bar is unchanged there. The chip takes the cell
the Knobs chip vacated and is the one elastic control on the bar: the tempo group and the
keyboard-settings combos beside it are measured first, and the chip gets whatever is left,
clamped to a readable range. This is the first extension point `KeysEditor` has ever exposed
to something embedding it.

Keys Host's own 44 px top bar - **Load Instrument...**, the instrument name/error label,
**Show/Hide Instrument**, **Eject** - is deleted along with it. `KeysHostEditor::resized()` is
now just `keysEditor.setBounds(getLocalBounds())`, the embedded editor fills the whole window,
and Load/Show-Hide/Eject move into a popup menu off the new chip instead. `barHeight` is gone
from every height calculation in `KeysHostEditor.cpp` - `maxWindowHeight()`, `fitToKeysHeight()`
and the constructor's initial `setResizeLimits` no longer add it to `KeysEditor::idealHeight()`,
since there is no bar left to account for. `KeysHostEditor::updateBar()` is renamed
`refreshInstrumentUi()` to match: there is no bar left to update, only the chip's caption.

### Changed: the keyboard's own settings ride the Controls bar, and the Pads bar sheds three

Owen: "let's also add the scale, root and scale lock, voices and MIDI channel into the controls
header. remove the scale and percentage and letter b from pads header."

**Onto the Controls bar**, right of the tempo: **Root**, **Scale**, **Scale Lock** (shown as
"Lock", accessible name unchanged), **Voices** and the **MIDI channel**. All five left the
Controls band, whose first row is now just Size and Octave. Like the tempo beside them they
never hide when the section folds, which is the point: these are what you set while playing,
and the band they used to live in went away with the fold.

They did not fit at the editor's minimum width, and Owen's call was "I think we can resize the
elements down" rather than a wider window, so the group has **two sizes and measures which one
it can afford**. The roomy set captions Root, Voices and CH, since "C", "Off" and "1" say
nothing alone; Scale and Lock never get a caption because "Major" and "Lock" are their own. The
tight set drops every caption and is what fits on the day an update notification claims 170 px
of the same bar. Deciding by measurement rather than assumption is what keeps that day from
starving the last control to zero width, which is the trap this layout has now paid for twice.

**Off the Pads bar**: the generator's **Mode** and **Scale Compliance** combos, and the
**arp target-line letter**. Both combos are still in the Generator window, which holds every
setting the generator has, so nothing became unreachable; the **Key** stays on the bar as the
one you change between fills. The letter had already lost its job earlier the same day, when a
card click stopped feeding an arp line at all, and what remained (naming the target of the card
menu's *Send to arp slot*) is what the A/B tabs on the arp bar say. `genModeBox`,
`genComplianceBox`, `arpTargetButton`, `cycleArpTargetLine()` and `refreshArpTargetButton()`
are deleted rather than hidden, along with the `StepComboBox` two-way wiring that only a
Compliance box on a bar ever needed.

### Changed: the tempo is a plain number in the Controls header

Owen: "I think the bpm should live in the controls header. I want it to be like the bpm in
ableton, just a number."

It was a labelled drag slider in row B of the Controls *band*, spent one build on the arp bar,
and now sits on the Controls *bar* as a recessed field showing nothing but the number, dragged
vertically the way Ableton's tempo is. That is the right home on the merits and not only by
taste: **the tempo is the plugin's clock, not the arpeggiator's**, and the arp is merely its
loudest consumer - Launch Quantize stayed behind on the arp bar, which is the same distinction
read the other way. Riding a bar means it survives folding Controls away, which the band copy
never did.

`KeysEditor::BpmField` is a `juce::Slider` subclass that overrides `paint`: a Slider so the
APVTS attachment still drives it, `paint` overridden rather than a style chosen because every
built-in style draws a track, a bar or a knob. The `<` `>` pair beside it stays - a drag is a
drag, and the mouse-only contract wants a click-only path to every value, which is the one
part of "just a number" Keys cannot copy from a DAW that expects a keyboard for that field.

### Changed: each line is its own boxed card, the bar carries what they share, and a pad click never feeds a line

Owen, on the side-by-side first cut: "we need a bit more clear delineation between the two
arpeggiators. They kinda look like one right now" - and two more calls in the same breath.

- **LINE A and LINE B are boxes now.** Each card draws its own captioned, ruled frame with a
  fill behind it - the band's group-frame look, one per line - and the outer LINES frame and
  its caption are gone: a box drawn around both cards was the single strongest cue that they
  were one thing.
- **The A/B/All tabs, BPM and Launch Quantize moved up onto the ARP section bar**, left of
  the line switches, so the All view is nothing but the two cards (~30 px shorter again; the
  window has gone 1450 to 1192 across the day). The tabs are editor-owned now - the bar
  outlives the panel - still named `Arp line A tab` / `Arp all tab` for the capture script,
  still chord drop targets, and they hide when the section folds, the pad-pages rule. BPM
  (a value bar with `<` `>` steppers) and Quantize stay when it folds: they are what you
  reach for while playing.
- **RATE and SHAPE are written over their arrows.** Two flanked `< >` pairs sitting side by
  side read as one puzzle ("the arrows to adjust certain parameters are not clear as to what
  they're adjusting"); the top line has the same micro-caps heading strip the knobs always
  had.
- **A click on a chord card never feeds a line any more** ("when an arpeggiator's running and
  you click on a pad, I don't want it to send it to the arpeggiator unless you drag it").
  Feeding a line is the drag - onto a line's card, its letter tab, or a slot - and the
  per-card menu's *Send to arp slot* stays as the aimed accelerator. A click just plays the
  pad, whatever the lines are doing. One stop survives on the left button: clicking a
  *cleared* card that is still feeding a line (the ring with no notes behind it) still lets
  that hold go. The Pads bar's letter chip now only names the line *Send to arp slot*
  targets, cycles without yanking the panel out of the All view, and says so in its tooltip.

### Changed: the two lines sit side by side, and VEL actually gets quiet

The follow-up to the entry below, both Owen's calls on the same day ("I was at negative 96,
and it was still pretty loud", "I'd like to consider having the arpeggiators parallel to each
other instead of one on top of the other").

**Side by side.** Each line is now a card - rate and shape on top, the eight knobs under
their own headings, Dot / Trip / Anchor with the held chord along the bottom - and the two
cards share the panel's width. Two parallel instruments that read as such, each a drop target
half the panel wide, and the view is another ~30 px shorter. Every card carries its own knob
headings now; "written once on the top row" only worked while the rows stacked.

**Three fixes under the VEL knob, one complaint.** -96 was still plainly audible because
three things compounded:

- **The curve was linear and hearing is not.** The multiplier is now squared
  (`((100+VEL)/100)^2`), so halfway down plays a quarter of the velocity, which *sounds*
  about half as loud, and the travel spends its change evenly instead of cramming it into
  the last few degrees.
- **The floor pinned the bottom.** The engine's 0.05 audibility floor (there to keep a
  Velocity lane at 0 or a hard H.VEL draw from turning into silence) used to sit *after*
  the level control, so everything from about -90 down emitted identical velocity-6 notes.
  The fader now multiplies after the floor and bottoms out at MIDI velocity 1.
- **The input was being re-randomized.** A chord handed to a line went through the
  keyboard's own Humanize velocity range on the way in, so VEL's "as played" reference
  wandered per note. A note bound for a line's queue now skips that replacement - the line
  has H.VEL for randomness and VEL for level; what you play on the keybed keeps Humanize,
  because that is playing.

`migrateVelTrim` now folds an old session's Volume through the curve
(`trim = 100*(sqrt(volume%) - 1)`), still level-exact to within the 1/127 velocity quantum.
One cohort moves: VEL values set under the few-hours-old linear build (dev machines only)
now play quieter than they did, since the same number means less under the squared curve.
And past all of it: how loud MIDI velocity 1 *sounds* is the synth patch's decision - a
preset with no velocity sensitivity flattens every velocity control Keys has, and only the
-100 mute cuts through that.

### Changed: the All view is the two lines and their header, and the level/humanize knobs reworked

Owen, on the macro view: "Your arpeggiator needs some work ... we need to make the window
shorter." Four calls, all his:

**Humanize is two knobs.** One HUMAN knob randomized timing and velocity together; it is now
**H.TIME** (the late-nudge, up to 25 ms) and **H.VEL** (the velocity shave, up to 30%), each its
own random draw per hit. The per-line tab's FEEL group grew the same split: **Human Time** and
**Human Vel** sliders where Human sat.

**VOL became VEL, bipolar, centred on "as played".** VOL was 0-100 defaulting to 100, so it
could only cut - and what it cut was velocity, which the old name never said. VEL runs -100 to
+100 around a neutral centre: right pushes the notes louder, left quieter, full left is a mute
exactly as VOL 0 was.

**Each line row slimmed to what you reach for while both lines run.** LTCH, PLAY and Chain left
the rows; all three still live with the line - Latch on the per-line band as before, **Play**
newly beside Retrigger in PLAYBACK (same `arpKeys` parameter the row's PLAY wrote), Chain on the
action row under the slots.

**The All view lost its bottom half.** The twelve slot cards and the Copy / Clear / Stop /
Chain row belong to the per-line tabs now; the A/B/All tabs moved up into the LINES header
alongside the BPM cell and Launch Quantize, whose shared row is gone with them. The view is a
34 px header and the two line rows, which takes ~110 px off the window Keys opens in.

**Parameter layout changed - loudly.** Two per-line parameters are appended: `arpHumanVel` /
`arp2HumanVel` / `arp3HumanVel` (0-100, default 0) and `arpVelTrim` / `arp2VelTrim` /
`arp3VelTrim` (-100..+100, default 0). Appended, so nothing existing moves. A session saved
before this opens sounding identical: its Humanize value carries on as the timing half, and
`migrateVelTrim` folds each line's old Volume into VelTrim exactly (volume% and
1 + (trim)/100 are the same multiplier) before putting Volume back to 100. `arpVolume` stays
registered and the engine still honours it, but nothing in the UI writes it any more.
`ArpTests.cpp` pins all of it: H.TIME leaves velocities alone, H.VEL only shaves, -50 halves,
+100 doubles into the 1.0 ceiling, -100 emits nothing.

**Writing that migration found every absence-detecting migration silently dead.** The kit's
`okstudio::state::load` handed `apvts.replaceState()` the parameter child of the parsed root
itself; ValueTrees share nodes, and replaceState synchronously backfills a child for every
registered parameter the session did not carry - into that same shared tree - so by the time
the `onExtra` callback (where `restoreSharedState` and all three migrations run) looked, every
parameter existed and "absent" was unobservable. `migrateStrumRange` and `migrateRateMode`
have been no-ops on every load since they moved into that callback; they worked when written
because they then ran before `replaceState`. Fixed in the kit (`StateHelpers.h`): replaceState
now gets `params.createCopy()`, so the root the callback receives stays exactly what the
session saved. Found because `migrateVelTrim` no-opped on the exact session shape it was
written for, with the tell being a VEL knob reading 0 over a line still playing at 38%.

### Changed: the chord drag is stock JUCE, and the ghost now follows your cursor between windows

Every chord drag in Keys - a tray candidate onto a pad, a pad onto the reference box, a card onto
an arp slot, a tab or a macro row, a card off the row to clear it - was hand-rolled on
`mouseDown` / `mouseDrag` / `mouseUp` plus `juce::Desktop::findComponentAt`, with the editor in
the middle forwarding screen positions between two windows that could not see each other. It is
now `juce::DragAndDropContainer` / `DragAndDropTarget`, which is what it should have been all
along.

- **The premise the workaround rested on was false.** The code and the docs asserted, as settled
  fact, that no `DragAndDropContainer` can deliver a drop across two top-level windows.
  `startDragging` takes a fourth parameter, `allowDraggingToOtherJuceWindows`, defaulting to
  false; pass **true** and the drag image is added to the desktop rather than to the container,
  which makes `getParentComponent()` null inside JUCE's own `findTarget` and routes target
  lookup through `findDesktopComponentBelow` - every desktop component in z-order, walking up
  each parent chain for an interested target. That is the same hit test the workaround performed
  by hand. Verified against JUCE 8.0.8; a docs PR is open upstream as juce-framework/JUCE#1692.
- **User-visible: the ghost crosses the window boundary.** It used to be an 84x26 chip painted at
  the cursor inside whichever component owned the gesture, so it vanished at the window edge -
  exactly where the drop you were aiming for lived. The card itself now travels, at full size, as
  a window of its own. The dimmed hole it leaves behind stays, because that is what says which
  card is in the air.
- **Nothing else about any gesture changed.** A drop still refuses a locked pad, still calls
  `clearChordPad` before `setChordPad` so a pad left ringing by Sustain or feeding the arp gives
  its notes up properly, still keeps the candidate when it misses, and dragging a card off the
  row still clears it *unless* something took it. That last one is the sharp edge: reaching for
  the reference box means dragging a card off the strip, and JUCE has no opinion about it, so the
  veto rides on the drag payload as `taken`. `consumed` is the separate answer for a tray
  candidate - committed to a pad its cell empties, copied to the reference it does not.
- **Two hit tests written twice are now written none.** `ChordPads::externalDropSlotAt` and
  `ArpPanel::externalDropSlotAt` / `externalDropLineAt` are gone, along with `onDragOutside`,
  `onDropOutside`, `onDragEnd`, `setExternalDropSlot`, `dropExternalChord`,
  `setExternalDropTarget` and the three `onCandidate*` pass-throughs on `ChordGenPanel`.
- **Two bug classes went with them.** A target's highlight is now put out by JUCE's own
  `itemDragExit` on every path a drag can end, including the far window being closed mid-gesture,
  which the editor used to have to remember by hand; and the reference box no longer lights up
  through a window sitting on top of it, because z-order is now the framework's answer rather
  than a bounds test.

### Changed: two arpeggiator lines, both on screen, and cards that sound on release

Owen: "I only wanna view two arpeggiators in this window, and I wanna be able to drag a chord
from below to each one. So the chord shouldn't play right away when you click it. You should be
able to drag it."

**Two lines, A and B.** The C line, its chip, its tab and its macro row are gone. Keys now opens
in the **All** view, so both lines are in front of you over the chord strip you drag from, and
the arp panel is 66 px shorter than it was.

- **No saved session breaks.** Line C's parameters (`arp3*`) are still registered and still
  written, exactly where they were; nothing reaches them. A session that had C switched on opens
  with C silent rather than arpeggiating something no control on screen can stop, and a session
  that had C as the current line opens on B. One constant, `KeysProcessor::uiArpLines`, is the
  whole of it.
- The A/B tabs still open a line's deep controls: the step lanes and the twelve slots are
  per-line and have nowhere to live in a macro row.

**A line that is off now remembers its chord silently, and starts on the switch.** Dropping a
card on a line that is not running used to sustain the chord like a pad - so the drop made a
noise - and the engine never saw it, so switching that line on sat silent until you dropped
another card. Both were the same cause: the off case merged the line's input straight to the
output instead of handing it to the engine. The engine now runs every block and its `enabled`
flag gates only whether it *fires*; taking notes in was never gated. So a drop is silent, and
the switch starts the chord that is already there. Playing the keyboard is untouched - a line
that is off never takes the keybed, exactly as before.

**OCT is now a transpose, centred at zero** (Owen: "the octave should start in the middle so you
can go up or down"). Nothing at 12 o'clock, three octaves down to the left, three up to the
right. The old OCT was the *stacking range*, which only ever widened the run upward and had no
middle; it stays on the per-line tab beside Distance, the rest of that same feature.

**VOL replaces RAMP and TIME in the macro rows.** A plain output level per line, 0 to 100 - the
way to balance two lines against each other without playing one of them softer. Ramp and Time
were one feature between them and both stay on the per-line tab; a row with Time in it and no
Ramp would be a control with nothing to time. **VOL at 0 stops the line**, which it did not at
first: the engine floors every note's velocity at 5% so a Velocity lane at 0 or a hard Humanize
draw cannot turn a note-on into a note-off, and the line's own level was landing under that
floor and coming out quiet instead of silent. The run keeps walking while it is muted, so
turning VOL back up picks it up where it would have been rather than restarting it.

**PLAY, was KEYS; Light keys, was Show notes.** Two unrelated controls that read as one idea:
PLAY routes the keybed *into* a line, Light keys only decides whether the keybed lights *up*.
Each label now names what it touches. The parameter id is unchanged.

**A tightening pass over the macro view.** The shared row was 56 px for a knob that needs 44,
and three separate gaps stacked into one band of nothing between the lines and the slot cards.
Only slack moved - no target got smaller, and the 34 px floor still decides that.

**Dot, Trip and Anchor on every macro row** (Owen: "I need to have options for dots and triplets
as well"). The same three the band carries, the same parameters, greyed by the same question -
in Hz there is no beat to dot or divide and no bar grid to anchor to. Run one line straight and
the other in triplets and you have the polyrhythm this view is for.

They sit on a strip of their own under the rate rather than in the main line, because that line
is already at every floor it has: two more 34 px targets in it would have driven the eight knobs
under the mouse-only minimum. The row is taller by exactly that strip, which the view can afford
- two rows at 102 against the three at 66 it used to be.

**All Off on the arp bar**, beside Hold off. Both lines off, every held chord let go, every
chain stopped, every quantized launch dropped - one click. Switching the lines off is what makes
it different from Hold off: release the chords without it and the engines simply pick back up on
whatever the keybed is holding, so the button would have silenced the room for a sixteenth note.

**Show notes**, also on the arp bar: the keyboard at the bottom lights up for the notes the
arpeggiator is *playing*, as it plays them. On by default, and one click turns it off when a
1/16 run is not what you want to be watching. It is a view toggle, not a parameter - nothing
about it is heard - and it is deliberately kept out of the "current chord" card, which must keep
naming the chord rather than whichever note of it the arp is on.

With it on, **the chord handed to a running line is no longer lit**. That chord is the run's
input, so lighting it held down every pitch the arp was chewing and the arpeggio moving inside
it was invisible - "it just shows the chords that are being played". Hiding the input is what
makes the output visible. Turn Show notes off, or the line, and the held chord lights as before.

**Fixed: Keys Host opened too short and cut the keyboard off the bottom.** The window opened at
a hardcoded height that had nothing to do with what the editor contained, and its resize floor
was a separate literal, so nothing stopped the window sitting shorter than its own content. The
keyboard is the last section laid out, so every missing pixel came off it, silently. Two
fail-safes now: the window opens at the content's height and the resize floor tracks it, so it
can never be left or restored shorter than what is in it; and the ceiling is measured from the
display's work area rather than assumed, so a window sized to its content still fits the screen
it has to live on. On a display too short for every section, fold one - the floor being real
means folding now visibly shrinks the window instead of quietly taking up slack.

**Fixed: the shape name was cut off in the macro rows**, and the `>` stepper beside it was
missing entirely. Both had one cause: Shape's width was expressed as a subtraction inside the
knob-size clamp, and on a narrower window the knobs hit their floor, the clamp threw the
subtraction away, and Shape got whatever happened to be left - about 77 px, enough for "Up" and
not for "Random Other". The stepper was starved to zero width by the same shortfall, which is
the worse half: a mouse-only control that was not on screen at all. Shape's cell is now reserved
first and the knobs take what remains, which is the ordering that cannot fail.

**Each line holds its own chord.** Exclusive no longer reaches across the arp lines: handing a
chord to B used to silently take A's away, so the second drag undid the first and a polyrhythm
could not be built at all. It still chokes the pads and the live card in both directions, and a
line's own previous hold still goes when you hand it a new one. Nothing collides by allowing it
- each line's chord is fired into that line's own queue, and `noteRefs` is per destination
stream, so two lines holding the same pitch are two independent references.

**A chord card sounds when you let go of it, not when you press it.** Every gesture that starts
on a card now begins silently, and the release decides what it was:

- **Click** a card and it auditions for 800 ms - the same length, and now the same gesture, as
  the generator tray's own audition. Exclusive, Sustain and Latch apply exactly as before.
- **Drag** a card and it makes no sound at all. Onto another pad to rearrange, onto an arp slot
  to bind it there, onto a line's row or tab to hand that line the chord, off the strip to
  clear it.
- With an arp line switched on, a click still hands the card to that line and holds it there.
  **This is the fix that matters**: that path used to fire on press *and* cancel the drag, so a
  card could not be dragged at all in the one mode where dragging it onto a line is the point.
- An audition never outlives the strip that started it. The 800 ms runs with the button already
  up, unlike the press-and-hold it replaced, so closing the window or folding the section inside
  it used to leave the chord sounding with nothing left owning the notes - reachable only by All
  Off. The strip releases what it started on its way out.

### Added: a macro view, so a polyrhythm is built from one screen

> **Superseded in part by "two arpeggiator lines" above, in this same unreleased batch.** Line C came out on 2026-08-02 and the macro rows changed which knobs they carry. Both entries are kept because they describe one release between them, and the machinery below is still what runs.

Owen: "I want a poly arp view where you can view the rate and shape of all three arpeggiators
at once ... the goal is to be able to create complex polyrhythms from one view."

A **fourth tab** joins A, B and C at the left of the slot row: **All**. It swaps the panel's
per-line band and step editor for **three rows, one per line**, each carrying that line's
switch, rate (with its Sync/Hz unit), shape, **gate**, **chance** and **swing**, the chord it
is holding, and its own **Chain** button. Under them sits what all three share: a **BPM** knob
and **Launch Quantize**.

- **It is a view, not a fourth line.** The current line stays whatever it was, so a chord card
  click still has one unambiguous target while all three are on screen. Clicking A, B or C
  goes back to that line's deep controls.
- **The panel does not grow.** The macro rows take the band's space rather than joining it.
- **Eight knobs a row** - Oct, Gate, Chance, Swing, Offset, Ramp, Time, Human - plus **Latch**
  and **Keys** switches: every setting a regular arpeggiator has, three lines deep, on one
  screen. They are the same machined rotary the band above uses for the same parameters, with
  each column heading written once at the top rather than repeated down every row.
- Rate is a knob too, detented onto the divisions, but it keeps its `<` `>` steppers: a knob is
  a drag target, and those are the click-only path to every division in both units.
- Clicking **All** selects the view; you leave it by clicking A, B or C. A tab selects, it does
  not toggle.
- The view you left is remembered, like the line you left.

Fixed on the way: an empty **STEPS** box was ruled beside the band on any plain shape, because
coming back from the macro view restored every group instead of leaving STEPS to follow Shape.

### Added: Launch Quantize, so a chord can only land on the grid

Owen: "there's a setting in Ableton where the arpeggiator, if you start a new note or something
that goes into the next sequence, so it sounds good always. I don't know what it's called."

It is Ableton's transport-bar **Quantization**, and Keys has it now as `arpQuantize`:
**Off / 1/16 / 1/8 / 1/4 / 1/2 / 1 Bar / 2 Bars**, in the macro view's shared row.

With it set, a gesture that *fires* something - clicking a chord card, launching a slot,
dragging a card onto a line tab - is held until the next boundary and then happens whole: the
pattern, the shape, the rate and the chord all land together, on the grid. The line's row shows
`...` while one is waiting. **Off is the default**, which is exactly what Keys did before.

**It never delays the keys you play.** Playing a note is playing an instrument, and an
instrument that waits half a bar before it sounds is broken. It is also global rather than
per line: the whole value of it is that the three lines land *together*.

Retrigger, the other half of "sounds good always", was already here - it restarts a line's
*pattern* every N beats where Quantize decides when a new *chord* starts. It stays in the
per-line Playback group.

The **BPM knob** in the macro view is the same `bpm` parameter the Controls section has, not a
second tempo. Worth knowing what it does and does not do: it is the tempo the lines run at when
there is no transport to follow - always in the standalone, and whenever the host is stopped.
A host that is *playing* always wins, and a line whose rate is in Hz follows neither.

### Added: three arpeggiators, so Keys can hold a polyrhythm

> **Superseded in part by "two arpeggiator lines" above, in this same unreleased batch.** Line C came out on 2026-08-02 and the macro rows changed which knobs they carry. Both entries are kept because they describe one release between them, and the machinery below is still what runs.

Owen: "I had the idea of having three arpeggiators so we can get polyrhythms and keep keeping
what we currently have, but having three of them, and then being able to feed cards into
different lines so we can really get some interesting things."

Keys now runs **three independent arpeggiator lines, A, B and C**. Each has its own rate, shape,
step pattern, twelve slots, chord and chain, so 1/8 against a 1/8 triplet against 1/4 is three
chips and two dials rather than three instances of the plugin.

- **The arp bar carries A, B and C** where a single On used to be. Each is that line's power
  switch, and it stays on the bar so a line can be brought in or out with the section folded.
  **Hold off is still one button** and still means "let go": it releases every line and stops
  every chain, because a hold you cannot see is a hold you cannot find.
- **Three tabs at the left of the slot row pick the line the panel edits.** The band, the step
  lanes, the twelve slot cards, Bars and Chain all follow the tab. **The panel is exactly as
  tall as it was** - a tab is 34 px inside a row that was already 58.
- **Cards go where you aim them.** A click on a chord card feeds the *current* line, shown as a
  letter chip on the Pads bar next to Fill and Regen (click to cycle A, B, C) and mirrored by
  the tabs. A card that is feeding a line wears that line's letter in its ring.
- **Drag a chord card onto a line's row in the macro view** to hand it straight to that line -
  a target the size of the row rather than the size of a tab, lighting anywhere on it, knobs
  included. Three rows on screen with a different chord dropped on each is how a polyrhythm gets
  built out of chord cards. The view does not move when you let go: you dropped onto the line
  itself, and being thrown into that line's deep controls is not what the gesture asked for.
- **Drag a chord card onto an arp slot to bind it there**, or onto a line tab to hand it over
  now. This is the left-click twin *Send to arp slot* has never had - that menu item was an
  owner-sanctioned exception because binding to one slot needs a target picker, and a drag is
  one. The menu item stays, as the accelerator it always was.
- **Keys**, per line: whether that line arpeggiates what you play, or only the chords you hand
  it. On by default for all three, so switching a line on and playing does something.
- **Channel**, per line: Global, or 1-16, for driving three different sounds in a multitimbral
  rack. It buys nothing in Keys Host until the hosted instrument is itself multitimbral.

**Nothing about the arpeggiator you already have has changed.** Line A registers under exactly
the parameter ids it always has - `arpRate`, `arpSwing`, `arpDirection` - so every saved session,
every automation lane and every MCP script still lands on the arp it was written for. B and C are
`arp2*` / `arp3*`, appended, and both start switched off: a session saved before this opens
sounding identical. Its twelve slots still sit exactly where they did in the saved tree, with B
and C's hanging off child nodes an older build simply ignores.

**Two parameters do appear on line A**: `arpKeys` and `arpChannel`. Both default to what Keys did
before there were lines (listen to the keys; use the global channel), so an older session is
unaffected either way.

`ArpEngine.h` is untouched. It never knew how many of it there were, which is why three of them
cost a routing layer and no engine work. That layer is per-line MIDI queues rather than a
per-pitch ownership mask: a mask lets the message thread clear a pitch's owner before the
matching note-off has been drained, which strands that note in an engine's held set with nothing
left that can release it. The `noteRefs` refcount is now per destination stream for the same
reason - a pitch held into line B must not suppress the same pitch played to the track output.

MCP: `get_arp_pattern`, `set_arp_pattern`, `recall_arp_pattern` and `store_arp_pattern` take an
optional `line` (0-2), defaulting to 0, and `get_state` reports all three.

### Fixed: Notes now really does go to 11

The Notes range advertises 2 to 11 and the tooltip promises that above five "the stack keeps
climbing in thirds through the mode, so 11 covers every degree". It did not: every count above
seven came back as seven, and under a pentatonic mode as *three*, with the two-octave stack
flattened into a one-octave cluster.

`fitVoicing` grew the chord and *then* normalised it to root position for the inversion pass.
`chordgen::rootPosition` collapses repeated pitch classes, and stacking thirds through a
seven-note mode arrives back at the root's own pitch class on the eighth note, so notes 8 to 11
were dropped every time and the register spread with them. The normalisation now runs first, the
note count is fitted on root position (so shrinking still keeps the root and the third), and the
inversion rotates the chord you actually asked for. Nothing about what the controls mean changed.

### Fixed: the generator's reference box stayed lit after a drag that went elsewhere

Dragging a pad off the strip lights the reference card so you can see where it would land. That
highlight was only put back out by the drop itself, which runs only when the card is released
*off* the row - so reaching for the reference box and then changing your mind, dropping back onto
a pad or onto the live card, left the box glowing at nothing until the next drag. `ChordPads` now
reports the end of every drag, whatever the gesture turned out to mean.

Also fixed: a chord leaned by **Lean** kept the chord type it had before the third moved, so a
major triad leaned minor still stored "Major" - the name under it is detected from the notes, so
the two disagreed on the same card, and Next voicing and the suggestion table both read the stale
one. And `sourceIndex()` clamped to a literal 6, which would have made an eighth generator source
arrive silently reading as Planing; an unknown source now falls through to the weighted pool, as
`generateChords` was always written to do.

### Removed: genTriads / genSevenths / genNinths

The three note-count tick boxes became the Notes range, which left their parameters unreachable
from any control while generation still obeyed them: they filtered which chord *types* the
weighted pool could draw from. That is the worst of both, so they are deleted rather than left
unread. The pool now draws from every type and `fitVoicing` decides the note count afterwards,
for every source rather than for one, which is also a wider pool: a request for five notes used
to be answerable only by a type that already had five, and is now "any chord, grown to five". An
old session carries three entries nothing reads, which APVTS ignores.

**The tray's staleness check now watches every generator setting**, not the handful it was
written against. It was still keyed on the three deleted parameters and had never been extended to
the sources, the ranges, Lean or the tick boxes, so "settings changed since these were generated"
was silent for most of what you can change.

### Added: two character sliders, and tick boxes that let the generator off the leash

**Brightness** sweeps the seven modes from brightest to darkest: Lydian, Major, Mixolydian,
Dorian, Minor, Phrygian, Locrian. That is the circle-of-fifths ordering of the modes, where each
step flattens exactly one more degree, which is what brighter and darker actually mean and why the
axis is a line rather than a taste. Major and minor are two points on it, so sliding past either
lands somewhere real. It is a **view onto `genMode`**, not a second parameter, because two
parameters for one thing is how they end up disagreeing. It greys when Mode is one of the scales
off that axis (harmonic minor, blues, the pentatonics) and keeps its last position rather than
snapping to an end, because either end would be a lie about where you are.

**Lean** is the other half of "a slider that goes between major and minor": it moves generated
chords' **thirds** major or minor whatever mode you are in. The size of the lean is how often a
chord gets pushed, so 40% colours a page without flattening it into one shade. Only the third
moves, so a major ninth leaned minor is still a ninth.

**Six tick boxes.** Ticked, the setting constrains generation. Unticked, the generator picks that
one freely: an unticked Key wanders, an unticked Notes range rolls anywhere in 2 to 11, an
unticked Scale Compliance strays by a different amount every time. Key and Mode roll **once per
generation** rather than once per chord, because every source takes a single root and mode for a
whole batch (a circle walk, a chain step, a progression transposed), so a per-chord roll would be
sixteen unrelated one-chord walks rather than one wandering progression.

Only six settings have a box, and the omissions are deliberate: **Lock Influence**, **Smooth
Voicing** and **Lean** already have an off position on their own dial, so a box beside them would
be a second control for what 0 already says.

A tick box is 34 px wide and the full height of its cell. The mouse-only floor applies to a check
box exactly as it does to a button, and a tick parked in the 14 px caption strip would be a target
you cannot hit.

**The band row collapses when a source has no band**, which is now Algorithmic (everything that
was its own moved to the fixed rows) and Negative Harmony (a reflection needs only Key, Mode and
Octave). The height goes to the tray, so the window does not resize as you switch source.

### Changed: the generator stops generating behind your back, and shows its working

Four asks from Owen on 2026-08-01, in one pass.

**Changing a setting no longer generates anything.** The tray rerolled itself whenever a setting
moved, which meant sweeping Source to hear the seven of them threw the tray away six times on the
way past. A control you cannot explore without destroying your work is a control you stop
touching. The tray caption now just says *"settings changed since these were generated. Regen for
new ones."* Generating is **Fill** and **Regen** and nothing else.

**Source and Direction are always-visible buttons**, not dropdowns: one click instead of two, and
seven answers on screen instead of six hidden behind the first. They write the same parameters the
combo boxes did, so nothing underneath changed.

**Scale Compliance is back on screen under every source**, on a fixed row with Lock Influence,
greying where a source does not read them rather than vanishing. **Notes and Inversions moved to
that row too**, which fixed a deeper mistake: both are facts about the *voicing* rather than about
which chord it is, so they were never the weighted pool's property. They are now post-passes the
generator applies to whatever any of the seven produced.

**Notes is a range from 2 to 11**, replacing the 3/4/5 tick boxes. Below three you get dyads;
above five the stack keeps climbing in thirds **through the mode**, so eleven is a chord covering
every degree and still in the key. **Octave is a range too**, so a page can spread across
registers instead of stacking up in one. Both are steppers rather than sliders: a slider is a drag
target, and steppers are the click-only path to every value.

**Voice Leading is now "Smooth Voicing"** (Owen: "I don't understand what the voice reading
does"). The name was the problem. It keeps consecutive chords close together on the keyboard:
C-E-G then F-A-C becomes C-E-G then C-F-A, the same two chords with less jumping. It never changes
which chords you get or which notes they contain, only which octave each note sits in.

### Added: a diagram of what each source is doing

`SourceViz` draws the current source under the buttons that choose it, and highlights the walk
that produced whatever is in the tray. A circle-of-fifths wheel with the walk traced round it, the
Neo-Riemannian P/L/R triangle with the actual sequence of transforms as chips, a mirror axis and
reflection pairs for Negative Harmony, a numeral strip for Progressions and Markov, degree bars
for Algorithmic, sliding note-stacks for Planing. Every one still draws its static figure with an
empty tray, so the picture explains the source before you have generated anything.

It is a picture and nothing else: click-through, takes no input, writes nothing.

### Fixed: small text was too dark to read

`skin::textDim` and `skin::textFaint` were chosen by eye against `skin::text`, which is the wrong
comparison. Almost everything wearing them is 9 to 11 px uppercase with letter spacing (the
section captions, the note list under every chord name), and small letterforms need far more
contrast than large ones to read at the same effort. Both lifted; `skin::text` is unchanged
because it was never the problem.

### Added: the chord generator opens with sixteen chords you can hear before you keep one

Owen: "when you open the chord generator page, it should open up. I have four by four pad where
you can audition new chords. We want to be able to try a bunch out. And then in the pad section,
when you right click on the slot, you should have a generate new chord button. And you should be
able to drag new chord to the pads."

The generator's window now carries an **audition tray**: a 4x4 grid of sixteen candidate chords,
generated from the settings above it and named the way a pad card is, with the notes each one
would play listed underneath.

- **Click a card to hear it.** The chord sounds for 800 ms through the same audition path the
  suggestion list has always used, so Humanize and the base velocity colour it exactly as a pad
  would. Nothing is written anywhere.
- **Drag a card onto a pad to keep it.** The pad lights while the candidate is over it, and the
  cell it came from goes empty, which is how you see what you have already taken.
- **Fill**, **Regen** and **Clear** act on the tray and on nothing else.

(This entry described a **Reroll** button and a tray that rerolled itself on any settings change.
Both were replaced later the same day by the two entries above, which is why they are described
here in their final form rather than as they first shipped: nothing in between was ever released.)

**A tray card is not on a pad, and that is the whole point.** These chords belong to no slot,
are not in the session, and go away with the window. Until now the only way to hear what the
generator would produce was to let it write a pad, so comparing eight chords meant either
filling the page with seven you did not want or rerolling one slot eight times and losing each
candidate as you looked at the next.

This is **not** the 4x4 grid that was removed on 2026-07-30. That one drew the current *page* -
the same sixteen pads, through the same `setChordPad`, as the strip already on screen - and the
cards downstairs were the better view of it. The tray is where a chord comes from; the pads are
where it goes.

Both gestures are left-button, so the right-click list stays closed. The drag is the only one
that can name a slot, which is why it and not a second click is what commits. It crosses two
windows, which JUCE does not do for free: the tray is in the generator's own `DetachedWindow`
and the strip is in the main editor, so no drag-and-drop container spans them and the source
never sees the target. The editor holds both and passes a *screen* position across;
`ChordPads::externalDropSlotAt` does the hit test with `Desktop::findComponentAt`, so the
generator window sitting on top of the strip correctly means "not over a pad", and folding the
Pads section correctly means nothing is found at all.

A drop **refuses a locked pad** (the lock is the thing that stops a chord being destroyed) and
clears the target before writing, so a pad left ringing by Sustain - or one feeding the arp -
gives its old notes up properly instead of stranding them. A drop that lands anywhere else keeps
the candidate and does nothing: this is the one drag in Keys shaped like the pad strip's
"drag off to clear" that must never lose work by missing.

**New chord on a pad's right-click menu is unchanged** and was already there; so was Next: could
follow. The tray is the bulk way to do what that item does one card at a time.

### Changed: nothing in the generator window writes a pad any more

Owen, the same day: "when you click on regenerate unlocked, I don't want it to regenerate the
ones in the host window, only in the card generator window."

The window's three buttons pointed at the current pad page, which put the one action that
overwrites sixteen chords a few pixels from the tray you are working in. They now act on the
**tray**, and the only way a chord in that window reaches a pad is a drag you made yourself.

- **Fill** writes the empty cells of the tray and only those.
- **Regen** replaces the candidates that are there.
- **Clear** empties the tray.

They keep the safe/destructive split they had, because that split was worth keeping; what
changed is what they are destructive *to*. A tray card is not in the session and is one drag from
a pad if you want it, so none of the three can lose work, and Clear needs no lock to respect and
no confirmation. They also moved off a row of their own and onto the tray's own header, which is
the row that says what they belong to. Fill greys when the tray is full; Regen and Clear grey
when it is empty.

**A committed card now leaves its cell empty** instead of refilling itself. The hole is how you
see which of the sixteen you have already taken, and it is what gives Fill something to do: a
cell that refilled instantly left Fill permanently greyed and made Regen mean "reroll
everything".

The Pads bar still carries **Fill** and **Regen** for the page itself, next to the pads they
write, which is where a page-wide action belongs.

**Clear Page is removed.** It had exactly one home and this window was it, deliberately, because
emptying sixteen pads at once with no undo wanted to be somewhere you went on purpose. Repointing
the window at the tray leaves it nowhere to live. A page can still be emptied a card at a time
(**Clear pad** on a card's right-click menu, or drag a card off the strip) and replaced wholesale
by **Regen** on the Pads bar.

### Added: a reference chord the tray cannot erase

Owen: "I think we should have another box for the reference chord where we can drag in something
from the main window or one of the other chords. So when you regenerate everything, it doesn't
erase your reference chord."

A single card above the tray, outside it. Fill, Regen and Clear all stop at the tray, so what is
in the reference survives every answer you ask for. Two ways to fill it, both drags: a **tray
card**, or a **pad from the main window**. Left-click it to hear it.

Beside it, **Similar** and **Could follow** fill the whole tray from it, and **Clear** empties the
slot. All three grey out when it is empty. That is the loop the tray was missing: keep a chord,
ask what is like it, keep one of *those*, ask what follows. Seeding the tray from a candidate used
to consume the seed, so the chord you liked was gone the moment it told you what came next.

**A drop on the reference copies, it never moves.** Dragging a card off the pad strip clears it,
and the reference box is off the strip, so without care the one gesture for keeping a chord would
have been the gesture for deleting it. `ChordPads::onDropOutside` returning true suppresses the
clear; the pad stays exactly where it was. A tray card dropped there stays in the tray for the
same reason: you should not pay a candidate for keeping one.

### Added: a right-click menu on tray cards

Owen: "when you right click on a chord in there, I want you to have a whole bunch of options about
trying to find similar ones or what might come next."

Eight rows: **Send to first empty pad**, the two seeded fills (**similar chords** / **what could
follow**), the three shaping edits (**Octave down**, **Octave up**, **Next voicing**), **New chord
here** and **Clear this card**.

This is a new entry on the closed right-click list in CLAUDE.md, added on Owen's explicit say-so
rather than drifted in. Most of it has a left-click twin: Send to first empty pad is the commit
drag with the aim taken out (useful, since landing on one card of sixteen in another window wants
a steady hand), and the two fills are the buttons beside the reference card.

**Similar** keeps the root and varies the colour: the same chord as a seventh, a ninth, a sus, the
parallel major or minor. **Could follow** changes the root, and reuses the same eighteen-move
table the pad card menu already offers, so the two can never give different answers to the same
question.

**Opening the menu makes no sound.** It auditioned the card for a few minutes on the day it was
built and came straight back out (Owen: "when you right click, it plays the chord. We don't want
it to play"): the left click is already how you hear a card, so right-clicking one you had just
auditioned replayed it, and right-clicking to reach Clear made a noise on the way to throwing the
chord away. The shaping edits are silent for the same reason.

### Fixed: auditioning a chord that a ringing pad already owned was silent

Owen: "when I drag a chord from the main window to this window and then click on it, it doesn't
play. And some of the generated chords sound like they're only one note even though they're saying
there's three."

Two symptoms, one cause, and it was not the chords. Keys emits one note-on per **pitch**, only on
the 0 to 1 transition of `noteRefs`, so that releasing one source can never silence another's
notes. With Sustain on, a pad left ringing owns its pitches, so:

- an audition of *that same chord* asked for five pitches that were all already owned and emitted
  nothing at all;
- an audition that merely **overlapped** it sounded only the pitches the pad did not own, which is
  why a card could truthfully list three notes and play one.

`previewChord` now calls `stopAllChordPads()` first, the same call Exclusive makes, reaching the
pads, the live card and a chord held into the arp. An audition is a monitor, not a performance, so
it takes the room.

Unconditional rather than only-when-the-pitches-collide, deliberately: which pitches overlap is
invisible, and a "hear this chord" button that works or does not depending on an overlap you
cannot see is the same bug in a quieter form. **The cost, accepted:** auditioning stops a chord you
were deliberately sustaining, and stops the arp if it was running off a held chord.

### Changed: the generator window no longer prints the mode's character

Owen: "we don't want it to say, like, bruised, relaxed, jazz at the top related to the key."

The line beside the title read `modes::get(mode).emotion` ("Bluesy, Relaxed, Rock" for Mixolydian).
It is a claim about how a mode feels, in a window whose whole job is to let you hear chords and
decide that for yourself. `modes::get().emotion` is untouched and still used elsewhere.

### Added: five new generation sources, and voice leading over all of them

**Source** was Algorithmic or Markov. It is now seven, in a new UI-free, unit-tested header
(`src/ChordSources.h`, `tests/ChordSourceTests.cpp`):

- **Circle of Fifths** walks the circle from the tonic, taking the quality each degree has in your
  mode, and occasionally doubles a step or reverses so sixteen chords are not one mechanical lap.
  Its band is the direction: flat-ward is the falling fifth most progressions are built on.
- **Neo-Riemannian** starts on the tonic triad and moves by P, L or R, each shifting exactly one
  note and keeping the common tones in place. Its band is the three weights. This is the one to
  reach for when you want smooth and key-ambiguous.
- **Progressions** transposes a real progression to your key: ii-V-I, the axis (I-V-vi-IV), 12-bar
  blues, Andalusian, Royal Road, rhythm changes, and the Coltrane major-third cycle. Random picks
  a different one each time.
- **Negative Harmony** mirrors the key about the axis between tonic and dominant, so C major
  becomes C minor and G major becomes F minor. It is the one source with **no band at all**: Key,
  Mode and Octave are the whole of what a reflection needs, and an empty row is more honest than a
  control invented to fill it.
- **Planing** takes one chord shape and slides it, through the scale (the quality bends to fit) or
  chromatically (the shape is preserved exactly, the Debussy sound).

**Voice Leading** is a percentage in the top row, not a source, because it is a pass over whatever
a source produced: each chord is revoiced to move the least from the one before it. It applies to
all seven, Markov and Algorithmic included. It never changes which notes a chord contains, only
which octave they sit in, so a chord's name is as true after it as before.

**Saved sessions are safe.** The new sources are *appended* to the `genSource` list, and APVTS
stores a choice parameter's plain index rather than a normalised fraction, so a session saved as
Markov still reopens as Markov. This is why the parameter's comment says never to reorder or
insert into that list: doing so would silently reopen every saved session on a different brain,
and there is no migration hook for it the way `migrateRateMode` covers the arp's clock. (An
earlier note in this file warned that adding sources would break sessions. It does not.)

Three simplifications worth knowing: the Coltrane entry is the bare major-third root cycle rather
than full Giant Steps machinery, the 12-bar blues has no quick-change or turnaround, and Locrian's
diminished tonic gives PLR no proper triad to start from, so it starts minor. One characteristic
rather than a bug: a four-chord template **loops** to fill sixteen, which is what you want when
filling a page and means the tray shows four distinct chords rather than sixteen.

### Changed: every chord card shows its notes, and the Big switch is gone

Owen: "I think we can remove the big button in the chord section, and I want it to just show
what notes are being played in the small button below the chord name."

A pad card now reads its chord's **name**, and under it the **notes that pressing it plays**,
with octave numbers: `Dm` over `D3  F3  A3`. Every card, all sixteen of a page, in the two
rows of eight the strip has always been. The live card at the left says the same about what is
under your hand, so the notes of a chord you are holding are named before you capture it.

**Big is removed.** It was a chip on the Pads bar that gave four rows of four, each card tall
enough for a note list and a mini keyboard of the shape being held: the tall arrangement the
chord generator used to draw over the top of these same pads, before that duplicate grid went
on 2026-07-30. The note list is the part of it worth reading, and it costs 11 px, which the
short card has. What 190 px of extra section height still bought was the mini keyboard, and
that is not worth a mode switch, a bar chip and a window that resizes under you.

So the section is `padRowH` (96) whatever else is happening, `padBigRowH` (286) is gone, and
the worst case the editor has to be able to grow to fell from **1473 px to 1283**. The Pads
bar's left-hand group dropped from 286 px to 214; `minWidthForView()` stays at 1070, because
what set that floor is the right-hand group (Detach, the generator's three chips and its three
combo boxes) and none of that moved.

**Sessions saved with Big on open with it off**, which is the only state there is now. The
`padsBig` layout property is no longer written and no longer read; an unread ValueTree
property is simply dropped, so an older session loads exactly as it always did.

### Added: the chord generator opens in a window of its own

Owen: "I think the chord generator should just pop out a new window instead of being in the
right click menu."

A third 24 px chip, **Generator**, joins Fill and Regen at the right-hand end of the Pads bar
and opens the generator as a window: **Key**, **Mode**, **Octave**, **Source**, note counts,
inversions, **Scale Compliance**, **Lock Influence**, the five Markov controls, and **Fill
Page** / **Regen Unlocked** / **Clear Page** as full-size buttons. Clicking the chip while the
window is already up brings it to the front rather than opening a second one, and it closes
from its own **Close** button or the X in its title bar - both run the same teardown. It
remembers where it was left and whether it was open, in the session, exactly the way a
detached section does.

The panel itself is the one deleted earlier the same day, recovered from git and adapted: a
layout Owen had used and liked, not a fresh guess at one. It reuses `DetachedWindow` rather
than adding a second window class, and its minimum size is **derived** from the layout
(`ChordGenPanel::contentSize()` adds up the same rows `resized()` places) rather than chosen.

**The window is a view, and the generator does not live in it.** `ChordGenMenu`, the brain, is
still a plain member of the editor with the editor's own lifetime, and the panel is built when
the window opens and destroyed when it closes. That split is load-bearing: **New chord** and
**Next: could follow** are items on a pad's card menu, and while the generator *was* a panel
those items came and went with it. Nothing the window owns may be the only copy of anything -
even the two transient picks, Markov **Mood** and **Start**, live on the brain, so closing the
window does not quietly change what the next Fill will generate. Every other control reads and
writes an APVTS parameter, which for the three the Pads bar also carries is the same parameter
both of them drive, so **the bar is the fast path and the window is the complete one**. Key
and Mode hold the same set of values in both places.
**Scale Compliance shows the nearest of the bar's five steps**: the parameter is a continuous
0-100 and the window's slider steps by 1, so set 60 there and the bar reads "50 %". Picking a
step from the bar always writes that step, including the one already showing.

Nothing in the panel plays a note. The one path in the generator that calls `noteOn` with no
pad behind it is the suggestion audition, released by an 800 ms timer, and it stays on the
brain, whose destructor stops it - so no close of this window can strand a preview note.

**The settings leave the pad right-click menu**, and so does **Clear page**, which is beside
Fill and Regen in the window where the other page-wide actions are. That takes the card menu
from 23 rows back to nine (below).

The docked window is unchanged at **699 px**: everything added here rides a bar that already
exists, or is in a separate window. The **Generator** chip is the last thing to widen the
*width* floor, which lands at 1070 and is worked out once in "the Centre section, the views
and the tabs" below; Keys Host asks the editor for that number instead of carrying its own
copy.

### Fixed: the generator window painted both sets of settings at once

Source = **Markov** replaces the algorithmic settings with the chain controls, in the same band
of the window. Only the Markov half of that swap was ever wired: the algorithmic set was shown
unconditionally and merely greyed, so both were painted into the same row. At the window's own
minimum width the algorithmic set is 804 px against the Markov set's 742, which left 62 px of a
dead **Lock Influence** slider and its percent box sticking out past **Length**, and a sliver of
its label showing between **Temperature** and Length. One band is on screen now, whichever the
source names, and the swap has one owner instead of two half-implementations of it.

Opening the window with Source already on Markov showed the same overlap for a frame, because
the enable-and-hide logic lived only in the 15 Hz timer. It runs once in the constructor now,
before anything is painted.

### Fixed: picking the step the Scale Compliance box was already showing did nothing

`genCompliance` is a continuous 0-100 parameter. The generator window's slider steps by 1, so it
can set 60; the box on the Pads bar has five steps and shows the nearest, so it read "50 %" at
60. Picking that "50 %" was then **a dead click** - `juce::ComboBox` finishes a pick through
`setSelectedId`, which returns early when the id has not moved, so the attachment never wrote
and the value stayed at 60. The only route to 50 from the bar was to pick a different step and
come back.

The box is a `keys::StepComboBox` now: it overrides the virtual `showPopup()`, builds the same
menu with the same tick, and reports **every** pick, including the one already showing. The
parameter is read back onto it by a plain `juce::ParameterAttachment` and written by
`setValueAsCompleteGesture`, one begin/set/end, so a move on the window's slider still shows on
the bar and neither side can leave a host automation gesture open.

Four places said the bar and the window could never disagree. They can, harmlessly and by
design: the bar offers five steps, the window is continuous, the bar shows the nearest step.
`src/PluginEditor.h`, `docs/ARCHITECTURE.md`, `docs/CONTROLS.md` and this file now say that.

### Fixed: a locked chord card could be wiped by dragging it off the strip

**Clear pad** has always greyed on a locked card. Dragging that same card off the strip cleared
it anyway - a wider gesture quietly overriding the menu item beside it, and squarely against
what Owen asked the lock to be. It does nothing now.

The **drag itself is still allowed**: `moveChordPad` swaps two slots and destroys nothing, so a
locked card stays arrangeable and only the wipe is refused. The ghost says which - it carries
the same corner dot the card does, and fades once the pointer is over nothing, the spot where an
unlocked card would be cleared.

### Fixed: Next voicing on a chord played with two hands

Two hands on the keybed produce a doubled note - the root at the bottom and again an octave
up is the usual one - and the voicing walk could not read that chord back. It stacked the
repeat an octave higher instead of collapsing it, so C major played as C E G C came back as a
chord in the *wrong* register, and **Next voicing** answered by writing the whole thing an
octave up. Press it again and it climbed again, until the chord ran off the top of the
keyboard and the item greyed out for good with no way back to root position. The same fault
from the other side put the same MIDI note on a pad twice, which spends two voices of the
polyphony cap and makes a four-note chord name itself as though it had four different notes.

A repeated note now collapses on the first press: a chord's voicings are arrangements of its
*pitch classes*, and there is no arrangement of a doubled note that survives the walk (the
last inversion of a doubled root is literally root position an octave up, which is where the
drift came from). The cost is that doubled note, once. What it buys is a cycle that closes in
one register however the chord was built, and no arrangement that can ever play a pitch
twice.

### Fixed: New chord on the card you are editing, and Hold off for a pattern-only launch

Two found reviewing this branch, both the same shape as fixes already in it.

**New chord** is greyed on the card linked to the keyboard, and a picked suggestion lands past
that card rather than in it. It is the rule the three items above it on the same menu follow
(see below): the keybed rewrites the pad it is editing on every latch change, so a chord
generated into it lasted until the next click on a key and then went, with no undo.

**Hold off** on the arp bar now lights for a launched slot that holds a pattern and no chord,
which is the third condition ArpPanel's **Stop** has always tested. The two are one button,
and the chip was greying itself out in front of work it could do: the ring stays lit, nothing
is sounding, no chain is running, and releasing is exactly what clears it.

### Fixed: Octave and Next voicing on the card you are editing

**Octave down**, **Octave up** and **Next voicing** are greyed while that card is the one
linked to the keyboard for editing. They write the pad's stored chord and cannot reach the
keybed, so the card moved and the keys did not - and then the next note you latched wrote the
old octave straight back over the shift, through the capture path, which also throws away the
generator metadata that lets **New chord** know which degree the card was. **Done editing** is
the row above them on the same menu.

### Fixed: a card that was both ringing and feeding the arp fired twice

With **Exclusive** on, moving such a card's chord (an octave shift or a new voicing) played
it, choked it, and played it again: both halves of putting the card back call the same
choke-everything path, so each undid the other, and the card ended up holding the arp while
silent - neither of the states it started in. Exclusive means one chord source at a time, so
there is one state to put back now, and it is the arp hold: the arp goes on playing off a held
chord until something replaces it, where a pad still ringing is only what Sustain left behind.
With Exclusive off both come back, as before.

### Fixed: picking a suggestion could overwrite a chord

**Next: could follow** places its pick in the first empty pad on the page. With the page full
it used to fall through to the pad right after the one you asked about and replace what was
there, which is the last path in the generator that could lose a chord you already had. It
greys out instead, the same answer **Fill** gives when it has nowhere to write. "Empty" here
means empty, locked or not: that is the definition `emptyPadsOnPage()` and Fill already use,
because a lock protects a chord and a blank slot has none to protect.

### Fixed: the pad card menu fits on the screen again

The menu had grown to 23 top-level rows plus four section headers. At the mouse-only item
height of 34 px that is about 820 px of menu, and it hangs off a pad near the *bottom* of a
699 px window, so it grew upwards past the top of the screen. JUCE answers a menu taller than
the space it has by splitting it into columns or turning it into a hover-scrolling one, and a
scrolling popup cannot be used with a single mouse at all: hovering the arrow scrolls the
list, and moving to click scrolls the item you wanted away. That is the "isn't working, too
overwhelming" report, and it was both things at once.

It is **nine rows and two rules, 340 px**, and it fits above a pad at the bottom of the window
on a 1080p screen with room to spare, in one column, with nothing to scroll:

```
Edit on keyboard / Clear pad / Lock
Octave down / Octave up / Next voicing
New chord / Next: could follow > / Send to arp slot >
```

Three things paid for it. The section headers went, because a rule says the same thing at
half the height - and a JUCE section header is not even a row, it is an item and a half
(51 px), which is what the last of them, **This pad**, was costing to name the card you had
just right-clicked. The four **Next** families went behind a single **Next: could follow** row,
which is the one thing on the menu three levels deep and the right place to spend that: it is
the exploratory path, not the one you take twenty times an hour. And **the settings left the
menu altogether**, for a window of their own (below), taking **Clear page** with them. What is
left is exactly the items that act on the card you right-clicked.

### Fixed: Fill no longer overwrites chords you already have

**Fill** wrote to every unlocked pad on the page, which made the one constructive button on
the bar the fastest way to lose sixteen chords, with no undo anywhere in Keys behind it. It
writes to the **empty** pads now and never replaces a chord that is already there, locked or
not: a blank needs no protection.

**Regen** keeps its job and is the only destructive one: it rerolls the pads that already
carry a chord and skips the locked ones. That is what "regenerate" means, and the lock is
what says "not this one". The two used to be one function with a flag, which is exactly how
the safe button ended up being the dangerous one, so they are two functions now.

Each chip also **greys out when it would do nothing** - Fill with no blanks left on the page,
Regen with nothing unlocked to reroll - so which of the two is which is readable from the bar
without a tooltip. The tooltips say it too.

### Changed: the lock comes off the card surface, and Lock is a right-click item only

Owen: "I don't want the lock button to be visible. I only want it to be in right click."

A clickable lock chip lived in the top-right corner of a filled card for part of the same day.
It is gone: `lockBadgeBounds`, the chip painter and the `mouseDown` branch that tested it are
all removed, and **the whole card surface plays, drags and feeds the arpeggiator again with no
dead corner**. That corner was roughly a quarter of a docked card, and because the lock branch
had to be tested ahead of every other one, a quarter of the card had stopped answering the
three gestures a card exists for.

**A locked card still says so.** The small corner dot the strip painted before the chip
existed is back, drawn only when the lock is set and never on an unlocked card - a lock you
cannot see is worse than one you cannot click. It is a marking and not a target: nothing
happens if you click it, and the click plays the chord like the rest of the card. The card
being edited paints no dot, because the tick that ends the edit owns that same corner.

**What the lock stops is replacement and destruction, and that is the whole of it.** Regen
skips a locked pad, so does Clear page, Clear pad greys out on one, and dragging it off the
strip no longer wipes it (below). It stops nothing else: the card still plays, still drags to
another slot, still edits on the keyboard, and still takes Octave and Next voicing, because
moving a chord you asked for by name is not generation. Fill never overwrites anything at all,
lock or no lock.

**Lock / Unlock on a pad's right-click menu is now the only way to set it.** That makes it one
of the three paths in Keys with no left-click twin - the other two are **Send to arp slot** and
releasing a pedal-held note from the keybed, below - and the only one where a twin was built
and then deliberately taken away. It is recorded as an owner-directed exception in
`CLAUDE.md`, dated, so it does not get "fixed" back.

### Added: Octave down, Octave up and Next voicing on a pad's card menu

Three items acting on the one card's stored chord:

- **Octave down** / **Octave up** move every note of the chord by an octave. They grey out
  rather than wrap when a note would fall off the ends of MIDI: a chord that cannot move in
  one piece does not move at all.
- **Next voicing** cycles the same pitch classes through different arrangements - root
  position, one inversion per note above the root, then a spread that opens the chord out with
  the root left in the bass, then round to root again. The item says which arrangement the
  card is in now. The inversions are the generator's own (`genInv0..genInv3`), not a second
  vocabulary; the spread is the one addition, because it is the voicing those four cannot
  express.

Nothing is remembered on the card for the cycle to work: the arrangement is read back off the
notes by shape, so a chord captured from the keyboard picks up the cycle wherever it happens
to be sitting. If the card is ringing, or is the one held into the arpeggiator, it changes to
the new notes where it stands - both go through the existing stop-then-press paths, so no
note-on is left without its release. **All three work on a locked card**, deliberately: a lock
protects a chord from being *generated over*, and moving a card by name is not generation.

### Added: the arp rate can free-run in Hz

The rate list stays exactly as it was, eleven tempo-synced divisions from "16 bars" to
"1/64", and a new **Sync / Hz** switch beside it hands the timing to a **Hz** value instead.
In Hz the arp free-runs always, transport rolling or stopped, and the playhead is not read
for step timing at all: no bar grid, no tempo, just a frequency. That is a thing the synced
list cannot express and every ambient patch eventually wants.

**Rate is a dial now**, not a drop-down list. A frequency has no list to be, so the control
became the kit's rotary, and the unit switch sits beside it: the chip reads **Sync** or
**Hz**, whichever is live, and lights in Hz. In Sync the dial detents onto the eleven
divisions and cannot land between two, and its readout says "1/8" or "4 bars"; in Hz it
sweeps the frequency and says "4.00 Hz". Whatever moves the rate moves the dial, including a
slot launch, the host and the MCP bridge.

The `<` and `>` beside it stay, and stay meaningful in both units, because a dial is a drag
target and these are the click-only way to reach every value it holds. In Sync a click is one
division, as before. In Hz it is a quarter of an octave, so four clicks halve or double the
rate, which is exactly the jump one entry of the Sync list makes; the ladder is anchored on
1 Hz, so both ends of the range and every power of two sit on it and repeated clicks always
land on the same values. **Dot**, **Trip** and **Anchor** grey out while Hz is on, since the
engine ignores all three there: the first two subdivide a beat and the third follows a bar
grid, and a free-running rate has neither.

The readout under the dial is **read-only**, like every other value box in the arp panel.
JUCE's `setTextBoxStyle` takes `isReadOnly`, and passing `false` there makes the box
edit-on-*single*-click: one click opened a text editor and took keyboard focus, which in a
plugin built for a mouse alone is a trap with no way out but clicking elsewhere. In Sync it
was worse than a trap, since `AudioParameterChoice::getValueForText` returns -1 for anything
that is not an exact division name and the rate would drop to "16 bars". The `<` and `>`
reach every value either unit holds, so nothing was lost.

The dial spans both rows of the PATTERN group, so the arp section is not one pixel taller
than it was. It needed 72 px of width: about half came from the STEPS group, which had some
50 px spare in each of its rows, and the rest from PATTERN's own two rows, where the Shape
list and the Trip and Dot toggles all had room over their longest text.

**Two parameters are new: `arpRateFree` (off) and `arpRateHz` (8 Hz).** Both are additive
and `arpRate` is untouched, so a saved session loads in Sync and plays precisely what
it always did. Automation lanes on any existing parameter are unaffected. That holds for a
*live* instance too, not only a freshly created one: APVTS does not reset a parameter absent
from a restored tree - it creates the child and flushes whatever value is currently there -
so recalling an older session while the dial sat in Hz used to leave the arp free-running
while the panel showed the division it had just restored. `migrateRateMode` writes both
parameters back to their defaults when the tree carries neither, the same shape as the strum
migration that ships beside it.

The Hz range is 0.03125 to 32 Hz, which is not a round number by choice: it is exactly what
the eleven divisions span at 120 bpm ("1/64" is 32 steps a second, "16 bars" is one step per
32 seconds), so Hz reaches everything the list does. The ends are ten octaves apart, and the
dial maps them **exponentially** - `value = lo * (hi/lo)^t` - so each octave gets a tenth of
the travel and 1 Hz ("1/2" at 120 bpm, the geometric mean of the ends) falls at the centre.
Without that, everything from "1 bar" down would live in the last two degrees of travel.
Dot and Trip do nothing in Hz mode, deliberately: they subdivide a beat and there is no beat,
so a dotted 8 Hz would only make the number on the dial a lie. Anchor is the same story with
the bar grid. Retrigger Every and Ramp Time are counted in beats, so while Hz is on they read
as seconds.

The twelve arp slots carry the mode and the Hz value alongside the division they already
remembered, so a slot captured in Hz launches in Hz rather than silently dropping back to
Sync, and its card says so - to the same decimals-by-decade rule the parameter uses, so a
card can never round 0.031 Hz down to "0.0Hz" and name a stopped arp. A slot captured in
**Sync** leaves the Hz value alone entirely rather than installing the 8 Hz that a pre-Hz
session synthesises for it. Slots in a session saved before this read back as Sync at the
division they stored.

### Removed: the Centre section, the views and the tabs

The centre had been whittled down to one row. The arpeggiator became a section of its own on
2026-07-25 and the chord generator lost its panel and its view earlier the same day, which
left a whole section (a bar, a gap, a caption and a chevron) wrapped around the eight CC
knobs. The knobs are the bottom row of the **Controls** band now, under the two rows of
settings they always sat with, and the editor is four sections: **Controls**, **Arp**,
**Pads**, **Keyboard**. There are no centre tabs and no Perform / Chords views; there is
nothing left to switch between, so the **Perform** tab goes with the section that held it.

Nothing is lost from the fold. The **Knobs** chip moved onto the Controls bar, which had
several hundred px of caption zone doing nothing, so the knob row still folds away with one
click and now costs the window no bar of its own to do it.

The row keeps its 110 px, so the knobs stay 60 px square. It was briefly cut to `KnobBank`'s
own 98 px floor to reclaim 12 px of window height, which put all eight rotaries at exactly
48x48. That is the kit's *recommended minimum* for a rotary (`okstudio/RotaryKnob.h`), set
deliberately above the 34 px mouse-only floor because a knob's usable drag arc shrinks faster
than a linear slider's track as the control gets smaller. Keys is played with one mouse, and
36% less knob area is a bad trade for 12 px, so it went straight back.

**The default docked window is 699 px tall, down from 800, and the minimum width is 1070, up
from 960.** Both numbers moved more than once today and both are stated here once rather than
in every entry that touched them.

The height is three changes: Transcribe going took 40 (a section costs its bar and the 6 px
gap above it, and nothing else), the keybed being measured rather than guessed took 23, and
this merge takes 38 (the centre's bar, gap and top margin, less the 6 px gap the knob row
needs inside Controls). The width is one bar. It had dropped to 960 when the generator's panel
went, with 1010 kept for the arpeggiator alone; **Key**, **Mode** and **Scale Compliance**
joining Fill and Regen brought the Pads bar up to that same 1010, so the two floors met and
`minWidthForView()` is a single number again, and the **Generator** chip took it to 1070.
Nothing shrank at either step, and the knob bank does not raise the floor: it wants 532 px and
gets the window width less 20. The worst case, everything open and docked with Big cards on
and the arp in Pattern shape, is 1473; see the resize-ceiling fix below.

Detached, **Keys Centre** is no longer a window. The four sections are **Keys Controls**,
**Keys Arpeggiator**, **Keys Chord Pads** and **Keys Keyboard**, and the knobs travel with
Keys Controls, whose minimum height rises from 190 to 330 to fit them (112 header + 6 + 110
knob row, plus the holder's 12 px inset, the 38 px strip a detached section carries, and the
window's own title bar and border). There is a fifth window, **Keys Chord Generator**, but it
is not a section: it has no bar and no fold, and it opens from a chip on the Pads bar.

Sessions saved before this carry `centre`, `centreDetached`, `centreDetachedBounds` and
`view` in their layout tree. Nothing reads them now, and an unread `ValueTree` property is
simply dropped, so old sessions open unchanged: the knob fold comes back from `knobs`, which
those sessions already carry. The `view` migrations retire with them, including the one that
turned a session saved on the old Arp *view* into an open Arp *section*; that mapping has
been in every release since 2026-07-25. No parameters moved.

### Changed: one set of chord cards, and the generator reaches them from the Pads bar

Three passes over one idea, landing as one change: **there is exactly one set of chord cards.**
The generator draws none of its own, in the window it later got back or anywhere else.

Opening **Chords** used to put a second copy of the pads on screen. Not a similar grid, the
*same sixteen pads* of the *same page*, written through the same `setChordPad`, drawn once at
full size in the generator and again as the strip below it. That made sense when the
generator covered the whole plugin and the pads had nowhere else to live; the pads have had a
section of their own, on screen whatever else is open, since 2026-07-25. What was left once
the duplicate went was a band of combo boxes and sliders sitting above the very cards it
wrote to, and a whole centre view spent on it. That band is gone too, and the Chords view
with it.

`ChordGenMenu` is a plain value member of the editor now, alive for as long as the editor is,
and it reaches the cards three ways, none of which costs the window a pixel:

- **Fill, Regen and Generator are chips at the right end of the Pads bar.** A bar is 34 px the
  window is already spending, so a control riding one is free, and these three are the whole
  left-click path into generation. They are never hidden, the way the arp's **On** is never
  hidden: the only other way in is a right-click on a pad card, which folds away with the
  strip, so hiding them made folding Pads (exactly what you do when the screen is busy) leave
  no route to generation at all. It survived that before the panel went, because it lived in
  the centre section. They come off the *right* for the same reason On does: the page buttons
  and **Big** are laid out from the left and disappear with the fold, so anything placed after
  them would keep their hole. 24 px tall like the other bar controls that act rather than
  fold; Fill and Regen were 22.
- **Key, Mode and Scale Compliance are combo boxes on the same end of the bar**, an answer to
  the complaint that reaching a generator setting cost too much pointer travel. Those are the
  three you change while auditioning a page, and on the bar each is one click to open and one
  to pick. They are 24 px like the chips, so the window is not a pixel taller, and they never
  hide with the fold either: with the cards away they are the only settings on screen at all.
  Mode drops the parenthetical alias to fit, so "Natural Minor (Aeolian)" reads **Natural
  Minor** on the bar and in full in the generator's window. Compliance is the one that cannot
  be a plain attachment - the parameter is a continuous 0-100 and the bar is five steps of it
  (0 / 25 / 50 / 75 / 100 %), so it shows the nearest step; see the fix for the dead click
  that came out of that, below.
- **The per-card actions are on a pad's right-click card menu.** **New chord** and **Next:
  could follow** are on every pad, on every page, always. They used to appear only while the
  Chords view was open, because the panel *was* the generator and the menu asked whether it
  existed; there is nothing left to be closed. **Lock** is there too: the strip has painted a
  lock dot since the pads existed and never been able to set it, because the toggle lived on
  the generator's copy of the card, so a state you could see while playing could only be
  changed from another view.

The settings themselves took two more rounds the same day before they settled. They were
flattened onto the pad's card menu first, each a submenu of ticked values, and that was taken
back out: it made a 23-row, 820 px menu, which JUCE turns into a hover-scrolling one that a
single mouse cannot work. They are in the generator's own window now, with **Clear page**,
which is the entry at the top of this release. What survives from this one is the part that
mattered: exactly one set of chord cards, a generator that outlives every window, two per-card
actions always on the card menu, and the bar as the fast path.

**Big**, on the Pads bar, is what the generator's grid used to be: four rows of four with the
full chord card on each, the chord's notes with octave numbers and a mini keyboard of the
shape under your hand. It works whatever else is on screen now, rather than only under Chords.

### Removed: Transcribe

The audio-to-MIDI section is gone, at Owen's request. It was built earlier in this same
unreleased stretch - record a phrase, get a piano roll, drag the MIDI out - and it is being
taken out before any of it ships, so no release ever had it. Keys produces MIDI and no longer
consumes audio at all, so `AudioCapture` and `TranscribePanel` are deleted and the editor is
down to four sections (Controls, Arp, Pads, Keyboard). A section costs its bar and the 6 px
gap above it and nothing else while it is folded, so the docked window is 40 px shorter for
it.

The build gets the bigger win. `KEYS_TRANSCRIBE`, the multi-gigabyte prebuilt ONNX Runtime
the engine linked, and the static MSVC runtime that library forced on *every* object in the
binary are all gone with it, so a first configure is quick again and Keys links against the
default DLL runtime. The engine itself stays in the kit (`okstudio/Transcribe.h`) for the
other plugins in the line - Spotify's basic-pitch, ported from
[NeuralNote](https://github.com/DamRsn/NeuralNote) (Apache-2.0); Keys just stops asking for
it. **An existing `build/` needs
`-DOKSTUDIO_KIT_BASICPITCH=OFF` once**, because Keys used to set that variable with
`CACHE FORCE` and a cache remembers what it was forced to.

Sessions saved before this carry `transcribe`, `transcribeDetached` and
`transcribeDetachedBounds` in their layout tree. Nothing reads them now, and a property no one
asks for is simply dropped, so old sessions open unchanged. No parameters moved.

### Added: Hold off, on the arp bar

There was no way left to let go of a chord held into the arpeggiator without stopping
something else as well. A click on a chord card retriggers the hold rather than ending it (see
below), the arp panel's **Stop** button is destroyed the moment its section folds and that
section starts folded, and **All Off** is a sledgehammer that also silences the pads and the
live card. In the default layout the only exit was switching the arp off.

**Hold off** rides the Arp section bar beside **On**, for the same reason On is out there: it
survives folding the section away. One left click lets the chord go *and* stops the **Chain**
if one is running. Both halves are the button. Releasing the chord alone leaves `chainOn` set,
so the next bar line launches the following slot and the chord comes straight back, which is a
button that undoes itself a bar later and reads as broken. `KeysProcessor::releaseArpHold()`
does the pair and is the one call the UI makes; the arp panel's **Stop** goes through it too,
which its own tooltip already claimed ("same button as Hold off on the section bar"). The arp
itself keeps running and goes back to arpeggiating whatever you play.

It greys out only when there is nothing to let go of, which means no chord held **and** no
chain running: the gap between two chain chords is exactly when you want out.

### Changed: a click on the card feeding the arp strikes it again

Owen's call. With the arp **On**, clicking a chord card hands its chord over and holds it
there; clicking that same card again used to take it back out. It **retriggers** now, the way
a second press on a beat pad re-fires it, which is what a lit card under your finger looks
like it should do. The retrigger never becomes a second owner of the same pitches:
`holdArpChord` releases the previous hold before it fires, so the per-pitch refcount and the
arpeggiator's own held set both unwind first, and Exclusive still applies to the new one.

The way to stop a hold outright is **Hold off** on the arp bar, above. One case keeps the old
meaning: a card cleared while it was still the one feeding the arp wears the ring with no
notes behind it, so there is nothing to re-play and the click releases. That is the ring's own
way out, and the reason it is drawn on a cleared card at all.

This is the chord *pads*, not the arp's twelve slot cards. Clicking the slot that is already
holding its chord still releases it, because a slot is one control that both starts and stops
itself and there is nothing else on the row that undoes a launch.

### Changed: right-click lets go of a note the keybed is holding

A right-click on a note surface has been a per-note latch since Octavium. On a key **that
surface is already holding** it now releases that key instead, out of whichever set has it:
`latched`, `sustained`, or both at once. On any other key it latches, exactly as before.

The point is the pedal. Under **Sustain** a chord rings on after you let go, and there was no
way to drop one note of it without lifting the pedal and losing the rest. Now there is, and
Sustain mode itself stays on. Taking the note out of *both* sets unconditionally also fixes a
key caught by both, which used to leave one set and keep sounding from the other.

This is a sanctioned exception to the rule that a right-click is only ever an accelerator with
a left-click twin (Owen, 2026-07-30). The latch half still has its twin: a left click releases
a latched note, with both toggles off. The `sustained` half cannot have one, because under
Sustain a left click on a ringing key **strikes it again**, which is the whole point of
Sustain being a pedal rather than a toggle. Owen took that trade explicitly, to get per-note
release without giving up the restrike.

Nothing here can release a key lit by a chord pad, the arp, MCP or the watched MIDI input:
those never enter the keybed's own sets, so a right-click on one of them takes the latch path
instead and adds the keybed as a second owner, which changes nothing you can see or hear until
the other owner lets go.

### Changed: a section bar folds from its left end again

**This reverses the widening of 2026-07-27**, three days old, which is recorded under "the
section bars read as headers" below. The reasoning there was sound as far as it went: a 34 px-tall full-width band reads as one target,
the mouse-only contract is about making targets bigger, and z-order already stops a bar
stealing its own controls' clicks, because they are its siblings sitting in front of it. That
last part is still true. A click that lands *on* Detach has always reached Detach.

What it missed is that z-order only defends each control's own rectangle. It says nothing
about the gaps around them, and on a bar whose right end is mostly gap, a click aimed at
Detach that landed a few px off it hit bar, and the bar folded the section away. The cost of
that miss is asymmetric: hitting Detach does what you wanted, missing it hides the thing you
were reaching into. Bigger is only kinder when the extra area does what the target does.

So the fold target is the chevron and its caption again, 92 px wide at the narrowest caption,
and the only part of the bar that lights under the mouse or answers to one. A hairline marks
where it ends, drawn whether or not the mouse is anywhere near the bar, because a boundary you
cannot see is a boundary you find by being surprised by it. The controls' own strip is
measured off that same rectangle, so the clickable end and the free end cannot drift apart.

### Changed: the pad strip and the keybed give back 57 px

Both bands were guessed high and are measured now.

- **Big cards: 320 to 286 px.** A card draws its note list and mini keyboard while its text
  area clears 58 px, which makes a pad 66 and four rows of them 286. The extra 34 px bought
  4 px of mini keyboard.
- **Keybed: 212 to 189 px.** A white key is capped at 185 docked and the keys are anchored to
  the bottom, so 185 is the real floor; the fallboard rail and its shadow are painted
  *downwards* over the keys and need no clearance above them. The remaining 4 px is body, and
  any window taller than the folds ask for still hands all its slack to this section.

Only the keybed's 23 px shows in the default layout, where Big is off. Both are already in the
699 px figure above.

### Fixed: the Chain kept coming back after things that should have stopped it

A running chain launches the next slot at every bar line, so any control that let go of the
chord without also stopping the chain was undone a bar later. Two did exactly that, a third
was greyed out at the moment you needed it, and a fourth made the chain start in the wrong
place.

- **All Off.** `allNotesOff()` forgot the chord held into the arp but left `chainOn` set, so
  the progression resumed out of the one button whose entire job is silence. It stops the
  chain last, after the panic loop has zeroed the note references, so the stop cannot emit
  note-offs for notes already gone. The MCP `all_notes_off` tool calls straight into
  `allNotesOff()` and inherits the fix rather than carrying a copy of the bug.
- **Hold off, and the arp panel's Stop.** Both go through `releaseArpHold()`, which stops the
  chain and then releases the chord. See the entry above.
- **The panel's Stop greyed out at the moment the bar chip lit.** Stop polled
  `arpLaunchedSlot() >= 0 || arpHeldNotes()` and never gained the `chainRunning()` clause the
  chip has, even though its own tooltip calls the two the same button. Pressing a chord pad
  with **Exclusive** on clears the launched slot and the held chord while the chain keeps
  running, so the panel's own way out of a running chain was disabled while it ran. Both
  controls read the same three-way test now.
- **A chain could start on its second slot.** `startChain()` armed `chainActive` without
  clearing `chainAdvance`. The audio thread can raise that flag once more in the block that
  straddles a stop, and nothing consumes it while the chain is off, so a stale `true` survived
  into the next chain and stepped it forward on its first heartbeat: click **Chain** and the
  first chord you asked for was gone before you heard it.

### Fixed: two ways the window could be the wrong size

- **Big cards resized the pads without resizing the window.** Toggling **Big** on the Pads bar
  changed the section's height by 190 px and never told the window: it did three of the four
  things a fold has to do and skipped `setSize` and `setResizeLimits`, so the cards grew into
  a band that had not grown and the keybed was carved off the bottom edge. It goes through the
  same `applyLayout()` every other fold uses now, which is a strict superset of what was
  there.
- **The maximum height was below the minimum.** `setResizeLimits` was capped at 1400 px while
  a fully open editor (knobs, Big cards and the arp in Pattern shape) asks for 1473, and
  `applyLayout()` passes that same figure in as the *minimum*, so every such layout handed
  JUCE a minimum above its maximum. The ceiling is 1800 now, stated once in `maxEditorHeight`
  with the worst case worked out line by line beside it.

### Fixed: a 110 px hole in the Keyboard bar

The detached keyboard window's own **Size** selector is a traveller marked "detached only", so
while the section is docked it has no parent at all. `layoutDetachRow` placed it anyway,
spending 110 px of the docked bar on a combo nobody can see and leaving a visible gap between
**Wheels** and **All Off**. Detached-only travellers are skipped on the bar now.

### Fixed: the names UI Automation sees

The screenshot script drives Keys by accessible name (`scripts/capture-window.ps1
-InvokeButtons`) and UI Automation returns the *first* control that matches, so a name that is
missing, vague or shared is a script that clicks the wrong thing.

- **Two Latch toggles and two Size combos.** The keybed's Latch and the arp panel's Latch both
  answered to "Latch" whenever the arp was open, and the detached keyboard window's Size combo
  answered to "Size" alongside the one in Controls. They are "Latch keys", "Arp latch" and
  "Keybed size" now. Every one of them still reads the same on screen.
- **The bar chips say what they do.** Fill and Regen are "Fill chord page" and "Regenerate
  unlocked chords", Generator is "Chord generator window", and Hold off is "Arp hold off",
  because "Fill" and "Hold off" on their own say nothing to a screen reader or a script. The
  generator's window has a second Fill, Regen, Key, Mode and Scale Compliance of its own, and
  each of those five is suffixed "(window)" so a script can say which copy it means.
- **A section bar is reachable, and always was.** `SectionBar` is a `juce::Button` calling
  `setTitle(caption + " section")`, so "Controls section", "Arp section", "Pads section" and
  "Keyboard section" each fold or unfold that section from a script, and no capture needs Owen
  to click a bar first. CLAUDE.md claimed the opposite (a plain Component with no accessibility
  handler) and was simply wrong; it is corrected, along with the worked example in
  `scripts/capture-window.ps1`, which still opened the Chords tab.
- **Detach is still named per section** ("Detach Pads", "Re-dock Keyboard"), and there are four
  of them now rather than six.

### Fixed: stale tooltips

The arp **On** tooltip still promised that a second click on a chord card takes the chord back
out of the arp; it strikes it again, and Hold off beside it is what lets go. **Sustain** now
mentions that a right-click drops a single ringing note, which is how a pedal-held chord comes
apart without lifting the pedal. The arp panel's **Stop** said "the chord a slot is holding"
when it releases any hold, whatever put it there, and now says it stops the Chain too.
Comments describing the Transcribe section, the centre view and the chord generator's own grid
of pads went with them.

### Added: Chain — the twelve arp slots play as a progression

Each slot card already held a chord, a shape and a rate. Give it a number of bars as well and
the row becomes a song: **Chain** walks the slots that hold a chord, launching each in turn
for the bars its card shows. One click plays a twelve-chord progression, which is what a row
of cards showing chord names has looked like it should do since the slots stopped being eight
lettered buttons.

**Bars** (the `-` `+` beside Chain) sets the **selected** slot's length, 1 to 16. Clicking a
card selects it as well as launching it, so setting a length is: click the card, click the
plus. A card shows `x2` and up; a one-bar slot stays quiet about it. Slots with no chord are
skipped, and switching the arp Off stops the chain.

The bar count is kept on the audio thread, the only place with a tempo — and the only place
that can ask the host for a time signature, so a bar is three beats in 3/4 rather than
always four. The launch itself happens on the message thread, because it moves host
parameters and fires notes.

### Fixed: a chord held into the arp could drone with nothing left to release it

Switching the arp off is supposed to release a chord a chord-card handed to it. That check
lived in the editor's timer and had two holes it could not close: it only fired for a chord
that came from a **pad**, so one handed over from the **live card** was never released — and
with no plugin window open, nothing polled at all, so a host or an MCP client writing `arpOn`
false left the chord sounding with no gesture left to stop it but All Off.

The processor owns that chord, so the processor now watches for it, on a heartbeat that runs
whether or not anyone is looking. A chord an arp *slot* launched is still left alone
deliberately: its lit card is on screen and still releases it on a click.

### Fixed: adding arp shapes renamed what stored slots would play

A slot's shape is a direction index in which "Pattern" is the number *after* the last
direction — so adding four shapes moved it, and every slot that had stored "Pattern" quietly
came back as "Random". Sessions now record which number meant Pattern when they were written,
and remap on load, so slots saved before this branch open as what they were. Caught by a
screenshot, not by a test: nothing about it fails to compile or crash.

### Added: four more arp lanes — Transpose, Late, Harmony and Chord

Six step lanes became ten, on the same tab bar and edited exactly the same way.

- **Transpose** (−7 – +7) counts **scale degrees**, not semitones. Everyone else's transpose
  lane is chromatic, which makes it a machine for leaving the key; this one follows Root and
  Scale, so +2 lifts every note a third *of your key* and can never land outside it.
- **Late** (0–90%) pushes a step later by that share of a step — a little on the offbeats for
  a lazy feel, a lot on one step to make a bar stumble. Late only: Swing is the control that
  can also rush, and an early half would let two steps swap order, which the note-off
  bookkeeping cannot survive.
- **Harmony** (0–7) adds a second voice that many chord tones above the note the step plays,
  so it stays inside the chord you are holding.
- **Chord** (1–12) plays the chord stored in that **arp slot** instead of a note of what you
  are holding. Draw four across a lane and the arp runs a progression by itself. A slot with
  no chord in it leaves the step alone rather than silencing it.

**Fixed on the way past:** a slot nobody had ever stored recalled as every lane at zero,
which is not "empty" — velocity 0 clamps to a near-silent 0.05 and gate 0 to 5%, so
launching an untouched slot made the arp whisper instead of doing nothing. Slots start at
the lane defaults now.

Lane data is serialized by lane index, so the four are appended and every saved pattern
still means what it did. Six new engine tests.

### Added: the arpeggiator grew four shapes, a spread, a ramp and a feel

A second research pass (Ableton Live 12's Arpeggiator, the Kirnu Cream manual, Cthulhu,
Scaler 3, the NDLR) against what Keys already had. Everything here is additive and defaults
to exactly what the arp did before it, so an existing session sounds identical until you
move one of them. Full notes in `docs/ARP_DESIGN.md`.

**Four new shapes.** **Random**, **Random Other** (never the same note twice running) and
**Random Once** (a shuffled order, held for as long as the chord is) join the eight
directions. So does **Chord**, which plays *every* note of the held chord on every step —
that one changes what the arp is for, turning gate, ratchet, chance and swing into a comping
part instead of an ornament. A fixed Note-lane step still means that one note, so an edited
pattern does not silently become block chords.

**A SPREAD group: Repeats, Distance, Offset.** "Octaves" was always "stack the chord N
times", with the interval hardcoded to an octave. It is **Repeats** now, and **Distance**
says how far each stack goes: Octave, 5th, 4th, Maj 3rd, min 3rd — or **Scale 2nd / 3rd /
5th / 7th**, which count degrees of Root and Scale rather than semitones. A Scale 3rd lifts C
to E and D to F; a fixed +4 would have given F# and left the key. No stock arp does this, and
it costs Keys nothing, because Root and Scale were already here. **Offset** starts the run
further in, rotating the step lanes and the walk together.

**A FEEL group: Ramp, Time, Human.** Ramp scales velocity toward ±100% over Time (1–32
beats) from the moment a chord starts, so a held chord can fade away or swell. **Human**
nudges every hit a little late and a little quieter, differently each time — the first
control in Keys that touches the arp's feel at all, since Humanize proper lives on a path the
arp's own notes never take. It is late-only and quieter-only by design, and clamped so a
nudge can never carry one ratchet sub-hit past the next.

**Retrigger is a list, not a toggle**, after Ableton: Off, Note, or a clock window (1 or 2
beats, 1, 2 or 4 bars). A clock window is what lets a five-step lane still land on the bar.
**This also fixes what Retrigger claimed to do**: it used to reset only the direction walk,
because lane reads came off the absolute step index, so a control whose tooltip said "restart
at step 1" never restarted the steps. It does now.

New parameters (`arpDistance`, `arpOffset`, `arpRetrigBars`, `arpVelRamp`, `arpRampBeats`,
`arpHumanize`) are all appended, and the four shapes are appended to `arpDirection`'s list,
so no saved session or automation lane moves. The arp panel is 64 px taller for the second
band row.

### Changed: Sustain is a pedal again, and Latch is back as its own toggle

Owen's call. Sustain had quietly become a per-note switch: with it on, clicking a key that
was already ringing *released* that key. That was deliberate once — it gave right-click's
per-note latch a left-click way out — but it cost the thing a pedal is for. You could not
play a note twice. A repeated melody note over a chord you were holding came out as one
note and then a silence, because the second click turned it off.

So the two behaviours are two controls again, and the whole difference between them is
what a second click does:

- **Sustain** (unchanged in every other way) is the pedal. Notes ring on after you let go,
  a glide still leaves a trail, and **clicking a ringing key strikes it again**. Playing
  the same key four times over a held chord now gives four attacks.
- **Latch** is a new toggle on the Keyboard bar, next to Sustain. Click a key to hold it,
  click it again to release it. This is the one for building a chord a note at a time, or
  taking one apart, and it is what Sustain had been doing by accident.

Both can be on at once; Latch wins on the keys it holds. Right-click's per-note *latch* keeps
its left-click twin: clicking a note held that way releases it, with both toggles off, so the
accessibility contract never depended on Sustain's old behaviour. (Right-click itself grew a
second meaning later the same day, on a key the keybed is already holding; see "right-click
lets go of a note the keybed is holding" above, which is where the one sanctioned exception to
that contract is set out.) **All Off** and turning either toggle off still clear everything.

Under the hood, a restrike is a note-off followed by a note-on, not a second note-on, so it
goes through the same refcount the chord pads, the live card and the arp share: a key
struck again while another source is holding that pitch leaves the other source's note
alone rather than cutting it short.
The `latch` parameter this reads has been in the state since the first release (it stayed
registered while the toggle was gone, and pad editing kept forcing it on), so no saved
session or automation lane moves.

### Changed: the section bars read as headers

Two of Owen's asks, both about the same strip. (A third, widening the fold target to the whole
bar, was taken back on 2026-07-30; see "a section bar folds from its left end again" above,
which says why the wider target cost more than it bought.)

**Open and folded bars look different now.** An open one is a solid ruled band: a gradient
fill, a hairline above, an accent rule below where it meets its content, a brighter caption
and a tick of accent at its left end. A folded one is flat, dim and outlined. A stack of these
reads as a shape before you have read a caption, which is the point: the window is mostly
bars when it is squeezed small. They all stay the one accent colour; the skin has exactly one,
and per-section tints would have been one per section.

**Detach hides with its section.** Folded away, it was the loudest thing left on a bar whose
whole job is to be quiet, and it offered a gesture with nothing behind it: detaching a folded
section built a window that opened hidden. Every other control on a bar already hid with its
section — the pad pages, the Knobs chip, Wheels — so this was the odd one out rather than a
rule being broken. The deliberate exceptions stay put: the arp's **On** (folding the panel
must never stop the arpeggiator), the centre's two tabs (they are how a folded centre comes
back) and the theme swatch (it belongs to the plugin, not to a section). The centre and its
tabs are gone since; **Hold off**, **Fill**, **Regen**, **Generator** and the generator's
three combo boxes joined the list of controls that outlive their section's fold, for reasons
given in their own entries above.

### Fixed: run.py hung with a blank console instead of launching

Smart App Control gates every freshly linked unsigned build, and it does so **two different
ways**: with a verdict already cached it fails `CreateProcess` outright, and while the
reputation lookup is still in flight it *blocks the call*. `run.py` only ever handled the
first — its "waiting for Smart App Control" message lived in the `except OSError` branch, so
the blocking form printed nothing at all. The build would finish, the script would sit
inside `CreateProcess` with an empty console, and the only way out was Ctrl+C, which then
dumped a traceback because `except Exception` does not catch `KeyboardInterrupt`.

The launch now runs on a worker thread with the main one reporting: a live counter, a
deadline that is actually enforced, and a Ctrl+C that is heard and exits cleanly. The wait
is twenty minutes, because that is what was measured here (19m44s across 175 CodeIntegrity
block events) rather than the three minutes previously assumed. `docs/BUILD.md` now carries
the measurements and the ways out.

### Fixed: the console never held open on failure

`console_is_ours()` asked `GetConsoleProcessList` for a count of exactly one, which never
happens: the `.py` association is `py.exe`, and `py.exe` spawns `python.exe` and stays in
the same console to relay the exit code, so a double-click is always at least two. The count
never matched, `hold_window_open()` never held, and **every** failure run.py can report — no
cmake, a failed build, a missing exe, a blocked launch, a traceback — flashed past and
vanished. That is precisely what the script exists to prevent, and what both CLAUDE.md and
docs/BUILD.md promised it did. It now identifies the processes instead of counting them, and
holds unless an actual shell shares the console. Verified both ways: two processes and no
shell when launched the way a double-click launches it, four with `powershell.exe` among
them from a terminal.

`SystemExit` skipped the hold for the same reason `KeyboardInterrupt` did, and that is the
path a file *dropped onto run.py in Explorer* takes — argparse calls it an unrecognised
argument, prints usage, and exits 2. Both are handled now, and `--hold` forces the pause for
a console that has a shell in it but is still going to close (`run.ps1 -Hold`).

### Fixed: smaller traps around the dev loop

Found while auditing the hang above; each one turns a legible failure into a confusing one.

- **run.py blamed Smart App Control for everything.** The `OSError` was discarded, so an exe
  still held by the linker, a missing dependent DLL and a file deleted underneath us all got
  the same "blocked by Smart App Control" story and the same useless advice. It now prints
  what Windows actually said.
- **A failed force-kill was silent.** If the running standalone could not be closed *or*
  terminated, `run.py` walked into the build anyway and let the linker report it as LNK1104
  a few thousand lines later. It now says the app is still running, names the pid, and says
  what is about to happen.
- **The cmake probe could hang with nothing on screen.** `subprocess.run([cand, "--version"])`
  had no timeout, so a candidate that starts and wedges — a stale UNC path, a disconnected
  mapped drive — hung before a single line of output, looking exactly like the launch hang.
  Twenty second timeout, and a slow-but-successful probe now names itself.
- **run.ps1 could report success for a run that never happened.** `exit $LASTEXITCODE` exits
  **0** when `$LASTEXITCODE` is `$null`, which is what it is if py.exe never started. And
  `$ErrorActionPreference = "Stop"` made `Write-Error` terminating, so the `exit 1` beneath
  the "needs Python on PATH" message was unreachable.
- **The screenshot script photographed the wrong window.** `capture-window.ps1` shot
  `MainWindowHandle`, and for Keys Host that heuristic lands on the hosted instrument's GUI,
  which is a top-level window of the same process: the docs nearly gained a picture of
  somebody else's synth. It takes a `-WindowTitle` now. Its window titles also all read as
  their own first letter, because `GetWindowTextW` was marshalled as ANSI and the UTF-16 it
  writes stops at the first NUL.

### Added: the keybed shows what is played *into* Keys

Play a hardware keyboard through Keys and its notes now light up on the on-screen keys, and
the live chord card names the chord under your hands — so a chord you found on the piano can
go straight onto a pad. In the standalone, tick your device under **Options → Audio/MIDI
Settings**; in a DAW, anything feeding the track (a clip, another device) lights up the same
way.

Nothing about the MIDI path changed: Keys has always passed the incoming stream through
untouched, and it still does. It only watches it go by, on the same display-only path
that already lights a key for a chord pad or an MCP tool. **All Off** clears the lights, in
case a note-off ever goes missing.

### Added: a check mark on the pad to finish editing it

**Edit on keyboard** links a pad to the keys and writes every change straight back to it.
Finishing meant going back into the same right-click menu, which made the last step of the
job the hardest one to reach. The pad being edited now carries a **✓** at its right-hand
end for as long as the link lasts: click it and the edit is done. It is on the card itself
rather than on the section bar, because the edit is a thing happening to *that card* and
finishing it should not mean looking somewhere else.

Folding the Pads section away while a pad is linked ends the edit too, since the tick goes
with it. Nothing is lost either way — the pad is written as you play, so the tick ends the
link rather than performing the save.

### Changed: Strum is a range

**Strum** was one number, so every chord raked at exactly the same speed. It is now a
two-handle band like the Velocity range beside it, and each chord takes a spread drawn from
it: drag an end to resize the band, or the middle to move it. Both ends together is a fixed
strum, which is what a session saved with the old single value loads as.

That last sentence was not true when it was written, and the screenshot for the docs is what
caught it: Keys Host came back reading **STRUM 0-68 MS** for a session that had asked for a
fixed 68 ms rake. The new high end arrived at its default of 0 under an existing low end of
68, which is not "unchanged", it is a random 0 to 68 ms spread on every chord. Old sessions
are now repaired on load. The tell is unambiguous: the pair comes out of a slider that
cannot put the high end below the low one, so out of order means the session predates the
range, and copying the single value across restores exactly the strum it asked for.

The repair worked in Keys and did nothing at all in Keys Host, because
`KeysHostProcessor::setStateInformation` had its own copy of the base class's restore list.
Both now call one `restoreSharedState()`, so the next session-shaped fix cannot land in one
product and miss the other.

### Fixed: swing dropped most of the notes it swung

The arpeggiator's **Swing** looked for every step boundary inside the current audio buffer
and fired it there. A swung offbeat does not sound on its boundary, though — it sounds a
fraction of a step later, which at any normal buffer size is several buffers later. The
scheduler gave up on those, and the next buffer had already moved past them, so the note
never played at all. With a 512-sample buffer that silenced very nearly every offbeat: swing
did not shuffle the pattern so much as thin it out.

It now schedules by each step's *fire time* rather than by its boundary. Covered by tests at
a realistic buffer size — the old code passes a test whose buffer is exactly one step long,
which is why this was not caught when the arp landed.

### Changed: swing goes both ways from the centre

**Swing** was 0 to 0.75, all of it delay. It is now −0.75 to +0.75 and starts centred:
right of centre delays the offbeats for the usual shuffle, left pulls them *early*, which
rushes them on top of the beat and is a feel you cannot get by delaying anything. Centre is
dead straight. The knob fills from the centre out and carries a detent mark, so "no swing"
looks like no swing instead of half full; any knob whose range straddles zero gets that
automatically from now on. Sessions keep the swing they were saved with.

### Added: a BPM control, in Controls

The arpeggiator is the only thing in Keys timed in beats, and it had no tempo of its own: it
followed the host, and fell back to whatever the host last reported. In the standalone that
meant 120 forever, with nothing to change it. **BPM** (40–240) is what it runs at whenever
there is no transport to follow — always in the standalone, and any time the host is
stopped. A host that is *playing* still wins, so tempo sync is untouched.

### Removed: the To Arp toggle

Getting a chord card into the arpeggiator needed arming **To Arp** on the Pads bar first.
With the arp switched off, doing so looked identical to sustaining a chord — so the button
read as doing nothing, which is exactly how Owen found it. It is gone, and the arp's own
**On** is the mode: with the arp running, clicking a chord card hands that chord over and
leaves it there; with it off, cards play beat-pad style as they always have. Switching the
arp off releases a chord a card was holding into it, rather than leaving it droning with no
click left to stop it.

The generator's chord grid followed the same rule while it existed, and a left click on a card
with the arp **On** is now the left-click way to get a chord into the arp, so right-click
**Send to arp slot** stays the plugin's one item with no left-click twin rather than becoming
two.

### Added: every section detaches into a window of its own

The keyboard and the arpeggiator could already be pulled out into their own resizable
windows. Now every section can: **Controls**, **Arp**, **Pads** and **Keyboard** each have a
**Detach** button at the right-hand end of their bar. (Six sections when this landed; the
centre and Transcribe have gone since, and the machinery never named a section, so the four
that remain need no detach code of their own.) One screen's worth of plugin can be spread
across as many windows as the desk has room for: the arp wide on one monitor, the pads under
your hand, the keybed as large as it will go.

Inside each detached window, a **Re-dock** button sits at the top, and the close box does the
same thing. Both windows and both routes were deliberate: leaving the control that undoes a
detach behind in the main editor puts it in the window you are not looking at, which is the
complaint that first moved the keyboard's Detach into its window.

Each window's position and size are remembered with the session, and one saved on a monitor
you no longer have is pulled back on screen before it opens. A detached section takes no
height in the main window, so this is the way to keep a tall section open and the plugin
window small at the same time; folding a detached section hides its window rather than its
empty slot, so the chevron still means one thing. A bar whose section is away says
**IN ITS OWN WINDOW** in the space its own controls were using.

What stays behind on a bar is whatever belongs to the editor rather than to the section: the
arp's **On** toggle, the pad page buttons and the theme swatch (and the Perform / Chords tabs,
while there were tabs) keep working while the section they name is off in a window. The
keyboard window keeps its own **Size** and **Wheels**, because those are the keybed's.

Under the hood this stopped being two special cases and became one table: each section owns a
holder its content lives in, and detaching is a single re-parent of that holder. Layout,
folding and height are written once and looped over, so the next section will detach without
anyone writing detach code for it.

### Changed: the arpeggiator is a section of its own, and it detaches

It was one of three centre views, so picking it put the knobs and the generator away — the
opposite of what you want from a thing that runs while you play. It has its own bar and
chevron now, above the pads, so the arp, the knobs, the chord cards and the keyboard are all
on screen together. **Detach** puts it in its own resizable window, the way the keyboard
already does; its **On** toggle and Detach ride on the bar, so both survive folding the panel
away. (Perform and Chords were the two remaining tabs when this landed. The centre section and
both views have gone since; see the top of this release.)

The panel's own title, On and Close are gone with it — the bar says all three, and two On
toggles bound to one parameter is just a thing to get wrong.

The section starts folded, because open it is the tallest thing in the editor. A session
saved on the old Arp *view* opens with the section unfolded instead, so it looks the same.
`KeyboardWindow` became `DetachedWindow`; nothing in it was keyboard-specific but the title
and the minimum size, which are parameters now.

### Fixed: Exclusive and Sustain went wrong once a card was held in the arp

Handing a chord to the arpeggiator added a fourth chord source (pads, the live card, the arp
hold, the keybed) and they had no shared rule. Eight defects, three of them stuck notes:

- **Two sources owning one pitch left notes stuck.** The processor emitted a second note-on
  for a pitch already sounding, so downstream the first note-off ended it for everyone: the
  arp lost notes out of its chord, keys stayed lit with nothing sounding, and the arpeggiator's
  own held-set counter leaked permanently — with Latch on, a released chord arpeggiated forever
  and every card after it stacked on top. **A pitch is now emitted once and released when the
  last owner lets go.** Re-pressing still retriggers, because every path that re-fires releases
  first. `ArpEngine` counts owners per held pitch to match.
- **Exclusive only worked in one direction.** It never choked a chord held in the arp, and
  handing a card to the arp never choked a sounding pad. It is a rule about *sources* now: one
  chord at a time whichever surface started it.
- **Clearing the card that was feeding the arp orphaned the chord.** The ring vanished, the
  empty pad stopped accepting clicks, and the only way out was All Off. Clearing releases the
  chord now, and the ring is drawn and stays clickable on a card cleared any other way. Since
  a click on a *filled* holder retriggers it (see "a click on the card feeding the arp strikes
  it again" above), that empty case is the one place a card click still means release.
- **To Arp was lost when the plugin window closed**, while the chord stayed held — leaving the
  pads back in momentary mode with a lit ring and no gesture that released it. The mode lives
  on the processor now and persists with the session.
- **Only the pad strip honoured To Arp.** The live chord card and the generator's pad grid
  played momentarily instead, and their note-offs silenced whatever the arp was chewing on.
- **Moving a card left the ring behind**, pointing at the wrong slot.
- **Releasing the live card or an arp-held chord wiped every other source's pending strum
  notes**, because `cancelScheduledNotes` treated any negative tag as "cancel everything" —
  true only while -1 was the sole negative tag. A pad mid-strum lost the rest of its chord.
  This one predates the arp work.
- **Releasing a chord mid-strum silenced somebody else's note.** A source remembers the whole
  chord it asked for, which during a strum is ahead of what it has actually played — the rest
  is still queued. Stopping it dropped the queue and then sent a note-off for every note in
  the chord *including* the ones that never sounded, and since a pitch now ends when its last
  owner lets go, that took a reference belonging to another source: release a pad mid-strum
  while holding one of the same pitches on the keybed and the keybed's note stopped with the
  key still down and still lit. Every chord source releases through one `releaseNotes()` now,
  which cancels and releases together and can therefore tell the two apart.

Sustain is deliberately unchanged: it defers the release a mouse-up would have caused, and a
chord held into the arp is not released by a mouse-up at all.

### Fixed: All Off did not match the buttons beside it

It was laid out 20 px tall where Sustain, Exclusive, Wheels and Detach are 24–26, and the
skin's button font scales with height, so its label came out smaller too.

### Fixed: every arp note-off was one audio block late

`ArpEngine::process()` retires owed note-offs at the *top* of the block, before any step
fires — so a note-off parked by `fireStep()` was first judged a block later, with its
countdown still measured from the block it was born in. Every note the arpeggiator played
therefore ran one buffer long, and a note whose gate ended inside its own block never ended
inside it at all: it was held until the next one.

`active[].samplesLeft` now has one stated meaning — an offset from the start of the block being
processed — and the drain moved. Each hit closes what it lands on top of immediately before its
own note-on, and whatever is still owed is drained at the end of the block and rebased once.
A note that ends inside the block it started in is emitted there and then, never parked.

Doing the drain up front instead is what **broke ties**: at gate 101–200% a note overlaps into
the next step, and on a repeating pitch the owed note-off has to be pulled back to just before
the retrigger. Drained early, it landed *after* the note-on that superseded it — two note-ons
for one pitch with nothing between them, which hangs a voice on any synth that allocates per
note-on. That is why the close happens per hit rather than per block.

Mostly inaudible at 512 samples (~11 ms) until now, which is why it survived: it took making
**Gate** a knob you can turn to 5% on any shape to make it easy to hear. Nine regression tests
cover it, including one across uneven block sizes (512 / 480 / 1024), where the old code put
the note-off at sample 2012 instead of 1500 — late by exactly the first block — and five that
pin the tie and event-ordering behaviour so this cannot be "fixed" the wrong way again. One of
those deliberately puts *both* hits of a tie inside a single buffer, which is the shape the rest
of the suite structurally could not reach and where an earlier attempt at this fix stacked two
note-ons on one pitch.

Out of note-tracking slots (64 sounding arp notes), the fallback now ends the note at the edge
of the block rather than stamping an event past the end of the buffer.

### Fixed: ratchets did nothing at any buffer size a host actually uses

A ratchet subdivides one step, and a step is normally longer than a buffer: at 1/16 and
120 bpm a step is 6000 samples against a 512-sample block. Sub-hits were stamped at their
offset from the step regardless, so a ratchet of 4 put note-ons at samples 1500, 3000 and
4500 of a 512-sample buffer — out of range, and dropped or clamped by whatever came next.
Only the first hit of any ratchet was ever heard.

The mistake is deciding an event belongs to this block because the thing that *caused* it
did. A sub-hit that lands past the end of its block is carried into the one
it belongs to now, in the same frame `active[]` already uses and rebased by the same
`advanceBlock()`. The step is still resolved exactly once — the RNG draw, the sequence walk
and the step counter must not repeat — so what is carried is the finished hit, not the step.
Un-fired sub-hits are dropped on bypass, on a transport jump and when the keys come up: a
ratchet is one gesture and does not outlive the step that decided it.

Three tests, each confirmed to fail before: sub-hits land at the right absolute samples
across 512-sample blocks, the same run comes out identical at 512 / 480 / 8192, and no event
is ever stamped past the end of its buffer (the old code emitted six that were). The existing
ratchet test passed throughout because its block is exactly one step long, which is a blind
spot worth remembering: a test whose buffer is a whole step cannot see a timing bug that only
appears when an event crosses a buffer edge.

### Fixed: run.py crashed with a raw traceback instead of building

Smart App Control is enforced on the dev machine, and the `cmake` on PATH is pip's *launcher*
(`Scripts\cmake.exe`), which is unsigned. SAC blocks it, `CreateProcess` fails, and Python
turned that into an `OSError` traceback out of `subprocess` — the exact thing run.py exists to
avoid, since it is meant to be double-clicked and read.

run.py now looks for a cmake that will actually start, preferring signed ones: `$CMAKE`, then
Visual Studio's bundled copy, then pip's real signed payload in `site-packages/cmake/data/bin`
(which the blocked launcher was merely wrapping), then a normal install, then PATH. If none
starts it says which were blocked and why, instead of a traceback.

The launch retry after a build was also too impatient — 5 tries over 3.5 s, tuned when SAC's
reputation check cleared almost immediately. It has been measured at ~3 minutes on this
machine, so a slow launch read as "it's broken" and the advice printed was to run the same
command again. It now waits up to 4 minutes and says what it is waiting for.

### Changed: the chord pads are their own section

The pads used to live inside the Perform view, which meant picking Chords or Arp put them
away — so the arpeggiator, whose whole job is to chew on a chord, was the one place you
could not reach a chord. They now sit in a **Pads** section of their own above the keyboard,
on screen whatever else is open and folding on their own chevron. Their
page buttons moved onto the Pads bar, where the row of four used to cost 34 px under the
strip. Perform is the knob bank alone now, so the Pads chip on the view bar is gone.

### Added: cards go into the arpeggiator, two ways

- **To Arp**, a toggle on the Pads bar. Lit, a click on a chord card hands its chord to the
  arp and *leaves it there* — the notes are held with no note-off until you click the lit
  card again or click another. (The toggle went on 2026-07-27 and that second click strikes
  the chord afresh since 2026-07-30; both have entries of their own above.) The card wears a
  bright ring while it is the one feeding the
  arp. Unlit, pads behave exactly as before; this is a visible toggle rather than an implicit
  "the arp is on" mode, so a pad never quietly does something different than it did a minute
  ago. (With the arp bypassed the chord simply sustains, which is honest.)
- **Send to arp slot**, in a pad's right-click card menu. Parks a copy of the chord in one of
  the twelve slots, to be launched later. A copy, not a reference: regenerating the pad page
  must not silently rewrite what a slot plays.

### Changed: the arpeggiator is laid out like an arpeggiator

Owen asked for the hardware-arp arrangement: controls gathered into ruled, captioned groups
instead of strung across two loose rows.

- **PATTERN** (Shape, Rate, Trip, Dot), **PLAYBACK** (Swing, Gate, Chance, Octaves, Anchor,
  Latch, Retrigger) and **STEPS** (Steps, Speed, Link), the last appearing only in Pattern
  shape, where it belongs with the editor it drives.
- **`<` and `>` step buttons beside Shape and Rate.** Walking to the next shape is the
  commonest thing you do to an arp and it cost a click, a travel down a menu and a second
  click. They clamp at the ends rather than wrapping, so one click too many on "Up" cannot
  drop you in "Pattern" and throw the whole step editor open.
- Swing, Gate and Chance are knobs with their value beneath them.

### Added: Gate and Chance work on any shape

Both existed only as per-step lanes, and the lanes are gated behind Shape being "Pattern" —
so on a plain "Up" there was no way to shorten the notes or thin the run out at all. Two new
parameters, `arpGate` (5–200%) and `arpChance` (0–100%), **multiply** the lane value, so the
defaults leave an edited pattern exactly as drawn and the controls mean the same thing in
both shapes. **Parameter layout change: sessions saved before this will load, but the two
new parameters come up at their defaults, which is a no-op.**

The parameters that stopped being read — `velocity`, `curve`, `latch`, `humanizeTime` — also
moved to the end of the layout, beside the other retained-but-unread ones, so what the plugin
actually uses reads top to bottom. Saved state and existing automation are unaffected: Keys
ships VST3 and Standalone only, and JUCE derives a VST3 parameter's id by hashing its string
id, not from its position. What does change is **order** — where these appear in a host's
generic parameter list, and the numbering in any host UI that counts them. Nothing is renamed
and nothing is removed.

### Changed: arp patterns became twelve launchable slots

A–H were eight lettered memories that only appeared in Pattern shape. They are now twelve
cards that show what they will play — the chord they hold, the shape and the rate they will
install — with a launch triangle, and they are on screen in **both** shapes, because
launching a chord through "Up" is as much a thing you do as launching one through an edited
pattern. One click launches: it installs the pattern, applies the slot's shape and rate, and
holds the slot's chord. Clicking the launched slot again releases it, and a new **Stop**
button releases it without reaching for All Off. A slot with no chord launches the pattern
alone and arpeggiates whatever you are already holding.

Right-clicking a slot opens Launch / Clear chord / Copy / Randomize — an accelerator only;
every one of those has a left-click path on the buttons beside the row.

Slots 9–12 come up empty in a session saved with eight.

### Added: each instance wears its own colour

A session with Keys on the pad track and Keys on the bass track gave you two identical
windows. Every instance now picks from eight accents (Cyan, Amber, Lime, Violet, Magenta,
Orange, Rose, Ice) from a swatch on the Controls bar, and it colours the whole plugin:
knobs, keys, the fallboard rail, tick marks, slider tracks, the wheel LEDs.

- **The accent is per instance, not global.** A DAW loads every instance into one
  process, so a global would have repainted every track's Keys at once. It hangs off each
  editor's `KeysLookAndFeel` and components resolve it through the LookAndFeel chain JUCE
  already walks up to the editor (`skin::accentOf`).
- **The swatch sits on the Controls *bar*, not inside the section**, so it stays reachable
  with Controls folded away. Telling instances apart is the point; hiding the control
  behind a fold would defeat it.
- Saved with the session. Cyan stays the default and the line's colour.

### Added: clicking a held key releases it

Both ways of holding a note were one-way doors. A key the pedal caught stayed on until
Sustain came off entirely; a key toggled on with a right-click needed a second right-click,
which is an accelerator not everyone reaches for. A plain left click now releases either,
so a chord with a wrong note in it can be taken apart a note at a time instead of started
over.

### Changed: the performance controls live on the Keyboard bar

Exclusive, Sustain and All Off now sit next to Wheels and Detach, and — unlike those two —
they stay visible whatever the Keyboard section is doing. They are what you reach for
*while playing*, so a fold must not take them away. They no longer follow the centre
view either, so they are there in Chords and Arp too.

### Added: the live chord card plays

Holding a chord sounds the keys you are holding. Clicking the "hold a chord" card now
fires those same notes as one chord, so you hear it the way a pad would play it: strummed,
humanized, and capped by Voices. Press and hold to sound it, release to stop (Sustain
holds it). Dragging still captures the chord onto a pad — the drag wins the moment the
mouse actually moves.

### Changed: chord-pad pages are four buttons, under the pads

`< 1/4 >` sat up on the view bar, far from the pads it paged, and read as a fraction. It
is now four numbered buttons directly beneath the pad strip: one click reaches any page,
and what they page is obvious from where they are.

### Fixed: Strum was doing nothing

Chord-pad Strum spreads a chord's note-ons over up to 200 ms. It never did. It passed the
delay to `noteOn`, which timestamps the message and hands it to `juce::MidiMessageCollector`
— and that empties its **entire** queue into the current block on every callback, clamping
each event into it. Anything beyond one buffer (~10 ms at 512 samples) was flattened onto
the end of that buffer, so every chord landed as a block however far the slider was pushed.

Strum now schedules its notes on the message thread and emits each when it comes due, the
same approach the MCP bridge already used for deferred notes. The timer only runs while
something is pending. Every path that stops sound (stopping a pad, panic) also drops what
is still queued, because a note-on that fires after its note-off is a stuck note nothing
clears.

A comment in `PluginProcessor.h` had named strum as the "one real use" for that delay
argument, which is what kept the bug hidden; it now says the opposite.

### Removed: the Humanize Timing spread

It rode the same broken path, and fixing it would not have been worth it: a random 0-30 ms
nudge is inaudible on a single clicked note, and on a chord it is Strum's job, done better
and with a direction. The parameter is retained but no longer read.

### Changed: only the chevron folds a section

The whole section bar was the target, so a click that missed a tab or a chip by a few
pixels folded the section instead. Now just the chevron end does — still a full 40x34 hit
box, and the hover highlight sits on it rather than lighting the whole bar, so where to
click is visible rather than remembered.

> **Reversed on 2026-07-27, and restored on 2026-07-30.** The bar was the target for three
> days in between. Z-order does stop a bar stealing a control's clicks, which is what the
> reversal was argued on, but it defends only each control's own rectangle and not the gaps
> around them. The fold zone is the chevron and its caption, 92 px at the narrowest caption,
> with a hairline where it ends; see "a section bar folds from its left end again" above.

### Changed: one velocity control, not two

There were two: a fixed Velocity slider that only applied while Humanize was **off**, and
the Humanize range that only applied while it was **on**. The same control in two costumes,
which is what made them feel redundant. The range absorbed it: Humanize on picks a random
value inside the band per note, off plays its midpoint. Collapse the band onto one value
and you have a plain fixed velocity — which is all the slider ever did.

The header is two rows instead of three as a result, so the whole window is 49 px shorter.

**This changes how an existing session sounds.** `velocity` is retained but no longer read,
so a session that set it loads with the velocity the Humanize band describes instead: with
the default band that is 76, whatever the slider used to say. Sessions saved at a loud fixed
velocity come back quieter. Drag the band to where you want it once and it stays.

### Changed: the Latch toggle is gone

Once a left click releases a note it is holding, a whole *mode* for holding notes earned
nothing: right-click holds, left-click releases. Latch survives internally for chord-pad
editing, which still forces it on.

Both parameters are **retained but no longer read**, alongside `curve`, `surface`,
`padChannel` and the XY pad's, so existing sessions and host automation still load.

### Fixed: long names were clipped in every menu, not just the colour picker

`drawPopupMenuItem` insets its text by 26 px on each side for the tick gutter, but the
base class sizes menu items from the text alone — so every menu in the plugin came out
52 px too narrow and ellipsised its longest entry. "Magenta" was the visible symptom; the
CC picker and the chord-card menus had it too.

### Changed: Velocity Curve is gone from the UI

It shaped the Velocity slider's own constant, so it only ever remapped one fixed number to
another — which is what moving the slider does. Between it, the slider and the Humanize
range there were three overlapping ways to set velocity, and this was the one that earned
nothing. The parameter is **retained but no longer read**, alongside `surface`,
`padChannel` and the XY pad's, so existing sessions and any host automation still load.

### Changed: the Humanize velocity range drags as a band

Only the two ends were grabbable, so shifting a range meant dragging one end, then the
other, then fixing the width by eye: three careful gestures for what is conceptually one.
Grabbing between the ends now moves the whole band and keeps its width. The ends still
resize it, and nothing needs a modifier key.

### Changed: Sustain and All Off moved next to Exclusive

They are the two controls you reach for *while playing*, and they were in the Controls
section — the one most likely to be folded away, which was taking them with it.

### Fixed: the window could be dragged smaller than its own contents

The minimum size was fixed while the layout is not, so with every section open the window
could be pulled well under what it needed and rows were simply carved off the bottom. The
floor now moves with the folds: the content's own height *is* the minimum.

### Fixed: tooltips were oversized, and long colour names were clipped

Tooltips used JUCE's 13 px default in a box up to 400 px wide, which next to this skin's
10 px micro-caps read like a different application. Smaller type, tighter padding, a
narrower wrap, and the longest tooltip strings cut down. The theme swatch also grew enough
to fit "Magenta".

### Changed: the centre section folds like the others

Perform / Chords / Arp used to fold by clicking whichever tab was already lit, which was
its own gesture to learn. The centre now has a `SectionBar` with a chevron, the same as
Controls and Keyboard, and the three tabs ride on that bar. They stay visible while it is
folded, so picking one both unfolds and switches.

> **Superseded on 2026-07-30.** The centre section, its bar and its tabs are gone; the arp
> and the pads are sections of their own and the knobs are the bottom row of Controls. See the
> top of this release.

### Added: the detached keyboard carries its own Size selector

Key count lives in the Controls section, which is exactly the section you fold away once
the keyboard is in its own window. The detached window now has its own, on the same
parameter.

### Changed: the dev loop is `run.py`, so it can be launched with a double-click

`run.ps1` had to be typed at a prompt. Typing is real effort here, and the dev loop is the
one command that gets run dozens of times a day. `run.py` does exactly the same work and
Explorer will run it on a double-click (or right-click → Open), with no arguments needed.

- Same behaviour throughout: the polite WM_CLOSE aimed at the window titled after the
  product (a force-kill loses the synth Keys Host has loaded), configure only on a cold
  build tree, and the Smart App Control launch retry.
- **The console holds open if anything fails**, so the error can be read instead of
  flashing past as the window closes. It only does this when the console was created for
  the script, so running it from a terminal never pauses.
- `run.ps1` is now a shim that forwards to `run.py`. One copy of the logic, and
  `./run.ps1 -Keys -NoBuild` still works exactly as before.

### Changed: every section folds, and the keyboard can leave the window

The editor was one fixed stack: three rows of controls, knobs, pads, keys, all of it
always on screen, with a floor of 820x560 whether or not you were using any of it. On a
busy screen that is a lot of plugin for a keyboard you mostly want to click.

- **Controls, Knobs, Pads, Wheels and Keyboard each fold away**, from a `SectionBar`
  (a full-width 34 px header with a disclosure chevron) or a chip on the bar the section
  belongs to. The window resizes itself to whatever the folds add up to, so the minimum
  height drops from 560 to 150: bars only, if that is all you want on screen.
- **The keyboard detaches into its own resizable window.** Docked, the keybed is one row
  of a fixed layout and key size is a compromise with everything above it. Detached, its
  size is entirely yours, and the 185 px key-height cap comes off so dragging the window
  taller genuinely makes the keys taller. Its close button re-docks it.
- **Folds, the current view and the detached window's position are saved with the
  session**, so a session comes back looking the way it was left. They are session state,
  not parameters: none of it changes a note.
- **Wheels and Detach travel with the keybed.** Detached, they sit on a strip inside the
  keyboard window: leaving the control that undoes a detach on the main editor put it in
  the window you were not looking at, and left the keyboard window with nothing on it but
  a close box.
- **Keys Host follows the folds too**, in both directions. Its window used to only ever
  grow, so minimizing a section there did nothing except hand the freed space to the
  keybed. Its minimum height drops with it (664 -> 194).
- **Hiding the wheels widens the keyboard.** The toggle hid them but left the keys where
  they were: the keybed holder's own bounds do not move when only its contents change, so
  JUCE never called its `resized()` and the keys kept their old width.

### Changed: Chords and Arp are views, not sheets over the whole plugin

Both opened as an overlay that dimmed and covered the entire editor, including the
keyboard. Editing an arpeggiator while unable to play a note is backwards for an
instrument you perform, and it made the plugin feel like it had opened a second window.

- **The tool row is now a view bar**: `Perform | Chords | Arp`. Each swaps what the middle
  of the editor shows; the header rows and the keyboard stay put and stay playable.
- **Clicking the lit tab folds the centre away**, which is how the middle section
  minimizes. It needs no chevron of its own.
- The panels' `Close` buttons now return to Perform rather than dismissing an overlay.

> **Superseded on 2026-07-30.** There are no views and no tabs left: the arp and the pads
> each became a section, the generator's panel became a window of its own, and the centre went
> with them. See the top of this release.

### Fixed: a grey band smeared across the bottom of every key

The white keys' front lip was a 10 px band two steps darker than the key body with a 30%
black line above it. Meant as the 3D step under the playing surface, it read as a shadow
someone had left on the keybed. It is now a thin, barely-darker bevel with a hairline
separator: the keys still have a front face, without the dirt.

### Added: the keyboard lights up for notes you did not play

The on-screen piano only ever showed your own mouse gestures. Its three states come
from `pressed`, `latched` and `sustained`, which are filled by the surface's own mouse
handling, so a note from an MCP tool or a chord pad sounded with the keybed sitting
completely still. Driving Keys from Claude was audible but invisible, and a chord pad
gave no indication of which notes it was holding.

- **`KeysProcessor` now refcounts what is sounding**, per MIDI note, whichever source
  asked for it, exposed as `isNoteSounding()` plus a `soundingGeneration()` counter that
  bumps on every change. Display only; nothing here touches the audio thread. The count
  clamps at zero so an unmatched note-off (a panic, a pad released twice) cannot leave a
  key lit forever.
- **`NoteSurface` polls that generation every 30ms** and repaints only when it moves,
  and offers `externallySounding()`, which maps sounding notes back to drawn ids through
  the existing `drawnForOutputNote()`. Every surface in the line gets this, not just the
  piano.
- **Those keys paint as `held`**, the state that already means "ringing with no finger on
  it", and they are checked after `pressed`/`latched`, so a key you are genuinely holding
  still reads as your own gesture.
- **Notes the surface is already playing are excluded**, rather than inverse-mapped back
  onto a key. `drawnForOutputNote()` is only the inverse of `outputNote()` while nothing
  has moved between them, and two ordinary things move: Scale Lock snaps an out-of-scale
  key onto its neighbour (those keys are dimmed, not disabled, so clicking one is normal
  use), and the octave can change while a note is latched and still ringing at its
  press-time pitch. Both would otherwise light a second, wrong key next to the one you
  actually touched.

### Fixed: `play_notes` and `play_sequence` made no sound at all

Both MCP note tools were silent from the day they shipped. `play_notes` reported
`{"played": 1}` and `play_sequence` reported its full step count and horizon, so from
the client side the failure looked like a synth or routing problem. Clicking the
on-screen keyboard worked, and so did `press_chord_pad`, which is what made it
confusing.

The cause is that both tools handed their timing to `juce::MidiMessageCollector`, which
cannot do it. The collector is built for live input: `removeNextBlockOfMessages()` ends
in `incomingMessages.clear()` and places every event with
`jlimit (0, numSamples - 1, pos)`, so it empties its whole queue into the block that
happens to be playing and clamps anything in the future into that same block. A
`play_notes` note-on and its delayed note-off therefore landed microseconds apart, and
an entire `play_sequence` phrase collapsed into a single buffer: a 113-second phrase
played correctly, in about eleven milliseconds. `press_chord_pad` escaped it only
because its release comes from this bridge's timer rather than from a delayed message.

- **Scheduled notes are now held in `KeysMcp` and emitted at real time**, the same way
  timed chord-pad releases always were. `play_notes` fires its note-on immediately and
  schedules only the release; `play_sequence` schedules every event against one base
  timestamp taken when the tool runs, so a phrase's internal timing is exact and the
  poll interval costs each event at most one tick of lateness instead of accumulating
  drift across the phrase.
- **The bridge's timer now polls at 5ms rather than 30ms.** Chord-pad releases never
  cared; notes do.
- **`all_notes_off` abandons anything still scheduled**, so it stops a phrase
  mid-flight. Previously it could only silence the current note while the rest of the
  queue carried on.
- **`play_sequence` accepts steps in any order.** The queue sorts by time, and at equal
  times a note-off goes before a note-on, so a note that repeats back-to-back releases
  before it re-attacks. Ordering by time alone let an unsorted phrase drop notes: the
  second attack could land ahead of the first release, which then killed it.
- `noteOn`/`noteOff` keep their `delaySeconds` parameters, which remain correct for the
  sub-block use they were written for (chord strum spread). Nothing outside this bridge
  relied on them for longer waits.

### Changed: the arpeggiator leads with a Shape, and the step lanes are tabbed

**Saved sessions: two new parameters (`arpPattern`, `arpLinkLanes`).** Both are additive,
so an older session still loads, but `arpPattern` defaults **off**. A session that had
per-step lane edits will now play as a plain arpeggiator until you set **Shape** back to
**Pattern**; the step data itself is untouched and comes back with it.

- **Shape now decides whether there is a step editor at all.** The Shape menu holds the
  eight directions plus "Pattern"; only "Pattern" shows the grid. Opening the arp on a
  shape is now one row of controls, not six lanes of teal bars. Modelled on Serum 2,
  whose pattern editor likewise only exists while SHAPE is "Pattern".
- **The six lanes are tabs, one on screen at a time** (Note, Octave, Velocity, Gate,
  Ratchet, Probability), which is the Cthulhu design `docs/ARP_DESIGN.md` always
  claimed to follow. Stacking all six is what forced six copies of the length and
  speed controls onto the right edge with no room to label any of them.
- **One Steps control and one Speed control**, labelled, for the lane you are looking
  at, plus the **Link lanes** switch the design spec called for and that was never
  built. Link on (the default) keeps every lane the same length and speed; off is
  polymeter, per-lane.
- **Fixed: the bottom of the arp panel was cut off at ordinary window sizes.** Six lanes
  needed about 750 px of panel height, more than the editor's 660 px default and more
  than Keys Host leaves once its top bar is in, so the Probability lane and the entire
  pattern row (A-H, Copy, Randomize) sat below the window edge. You had to enlarge the
  window to reach them, and nothing said so.
- **Fixed: tooltips never appeared anywhere in the plugin.** JUCE only shows them when a
  `TooltipWindow` exists and there was none, so 19 written explanations across the arp
  and chord panels were dead code.
- Shape brackets its writes in `beginChangeGesture`/`endChangeGesture`. It spans two
  parameters, so it cannot be an APVTS attachment, and the attachment is what normally
  supplies those: without them a host in touch or latch mode would not arm on a Shape
  change the way it does on every other arp control.

### Changed: the instrument picker files VSTs into folders
- **One collapsible folder per publisher**, opening closed, so a big library reads as a
  short list of publishers instead of one long scroll. The header shows how many
  instruments are inside; one click opens it, another closes it, and several can be
  open at once. Which folders you left open survives Rescan.
- **Folders are the only raised chips; instruments are plain indented text.** On the
  standard button both were the same centred pill, so an indent and a small triangle
  were all that separated them and the instruments still read as more folders. Rows are
  left-aligned, folder captions are bright and semibold, and an instrument lights up
  with an accent edge under the mouse instead of carrying a chip of its own.

### Fixed: the hosted instrument's window could open unmovable
- **Keys Host's instrument window opened with its title bar off the top of the screen**,
  and since that window has no resize frame, its title bar is the only thing you can
  drag: the window was stuck wherever it landed, permanently. Two causes, both fixed.
  `placeInstrumentWindow` clamped the window into the display work area using *component*
  coordinates, which exclude a native title bar, so pinning to the top edge put the bar
  itself at y = -30. And it ran before the editor had a screen position, so it read the
  keyboard window's origin as (0, 0) and took that clamp path on every single launch.
- The clamp now accounts for the window frame (`okstudio::ui::ensureWindowReachable` in
  the kit, so the whole line gets it), and placement defers one message-loop turn when
  the editor isn't on screen yet, with a single retry rather than an unbounded re-post.

### Added: edit chord pads on the keyboard, and chord cards that show their notes
- **Right-click a pad on the main page → "Edit on keyboard".** The pad's notes latch
  onto the piano (latch behaviour is forced on while editing), clicking keys adds and
  removes notes, and every change writes straight back to the pad with its name
  re-detected live. The pad glows with an EDIT tag while linked; "Done editing" (or
  flipping the pad page) ends the link and silences the editing chord. Removing every
  note does not clear the pad — "Clear pad" in the same menu is the explicit wipe
  (locked pads keep their lock through edits).
- **The generator's chord cards are full cards now**: chord name, the note list with
  octave numbers ("C4 E4 G4 B4"), and a mini two-octave keyboard with the held keys
  lit, so you can see what a chord contains and how many notes it has before pressing it.

### Changed: chord generator, first-pass redesign
- **The per-pad Lock / New / Next buttons are gone; they live in each pad's
  right-click menu now** (Lock/Unlock, New chord, and the Next suggestion families
  with per-row preview), restoring Octavium's card menu at Owen's request. The pads
  are plain full-size cards again — hold to audition, right-click for actions — and
  the grid reclaims the button rows' space. An empty pad's menu offers New chord, so
  single slots can be filled without a page fill. This is a deliberate, owner-directed
  exception to the "right-click only as accelerator" rule; CLAUDE.md is amended, and
  the page-wide Fill / Regen Unlocked / Clear buttons remain the left-click bulk path.
- **The Feel preset row (Happy/Sad/Dreamy/...) is removed.** The emotion labels
  weren't meaningful in practice; key and mode are set directly (the mode's emotion
  line by the title stays). The preset table remains in `ScaleModes.h` (still
  unit-tested) in case a future affordance wants it.

### Changed: the "Obsidian" skin — a full visual redesign
- **Every surface is restyled by a new Keys-local LookAndFeel**
  (`src/ui/KeysLookAndFeel.{h,cpp}`, subclassing the kit theme): near-black neutral
  chrome, one cyan accent family for every lit state, machined 3D knobs with glowing
  value arcs, ball-thumb sliders, inset toggle wells with a check, chevron combos,
  and restyled popup menus. All vector-drawn (gradients + layered strokes, no
  images, no OpenGL), so it scales with the resizable editor.
- **The keyboard is dimensional now:** white keys with a front lip and seams, black
  keys as stepped glossy blocks with a catch-light edge, drop shadows, and a cyan
  "felt" strip along the fallboard. Pressed/held keys glow in the accent (the old
  blues are gone; one accent everywhere).
- **Chrome:** "KEYS / OK STUDIO" wordmark, micro-caps section labels, a header band,
  and the knob row + chord pads unified on one raised panel. Chord pads are inset
  wells (empty), raised chips (filled), or lit accent (sounding); the wheels got
  ridged grooves and LED-striped grab bars. The Chords/Arp door buttons light up
  while their overlay is open. The Chord Generator, Arpeggiator, Keys Host bar, and
  instrument picker all follow the same language.
- Interaction, layout, hit targets, and parameters are untouched: this is paint
  only. Sessions are unaffected.
- **The standalone window chrome follows the skin too**: title bar band, tracked-caps
  window title, and thin-glyph minimise/close buttons on 38 px targets (the stock
  JUCE wrapper drew its own default-theme bar above the editor). DAW builds are
  unaffected; the host owns the window there.
- **Layout: slimmer wheels, taller keys.** The Mod/Pitch wheel column narrows from
  112 to 84 px (each slider 36 px wide, still above the 34 px mouse-only floor) and
  the key-height cap rises from 150 to 185 px, closing the dead band that sat
  between the pad strip and the keybed at the default window size.
- **Clickable keys: the default Size is 49 keys now (was 61).** At the default
  window, 61 keys left ~24 px per white key — too narrow to hit accurately with a
  mouse; 49 keys is ~30 px. Existing sessions keep their stored Size (flip the combo
  once to adopt 49); 61-88 remain available when range matters more than width.
  Black keys are also slightly shorter (56% of white height, was 62%), so more of
  every white key is the full-width, accurate-to-click zone.
- New `scripts/capture-window.ps1` implements the CLAUDE.md screenshot procedure
  (PrintWindow + UI Automation, no focus/cursor theft) for the docs.

### Changed: one view, no tabs — the Faders and XY surfaces are now eight knobs
- **Keys collapsed from five tabbed surfaces (Keys/Hex/Pads/Faders/XY) to one view:**
  header controls, eight rotary CC knobs, the chord-pad strip, then the playing
  surface. No more surface tabs, and the **Classic**/**Performer** layout switch is
  gone with them.
- **The knob row (`src/ui/KnobBank.{h,cpp}`) replaces the Faders and XY surfaces.**
  Same CC assignments (`faderCC1`-`faderCC8`) and the same auto-assign-to-hosted-
  instrument-parameter behaviour on Keys Host / Hex Host, just eight
  `okstudio::RotaryKnob`s in a row above the keyboard instead of a separate tab. XY's
  two-CC drag pad has no equivalent — reassign the two knobs it used (Mod/Cutoff by
  default) instead.
- **The Pad Grid is gone outright** (`src/ui/PadGrid.{h,cpp}` deleted): the 4x4 drum
  grid belongs to Beatform, not Keys.
- **The Hex surface is exclusive to Hex Host now.** Keys and Keys Host build the
  piano only; Hex Host builds the Harmonic Table only. Which one a product builds is
  now a compile-time choice (`KEYS_HEX`), not a runtime tab.
- **Sessions load fine either way.** The `surface`, `uiLayout`, `padChannel`, and
  `xyCCX`/`xyCCY` parameters are still registered — dropping them would break older
  saved sessions that carry them — but nothing in the UI reads them any more.
- **Hex Host moved out to its own repo (`../Hex`).** Now that a product builds only
  one playing surface, there is no reason to keep the Harmonic Table variant in this
  repo: it ships from `../Hex` instead, unchanged (`KyHx` plugin code, same engine).
  Keys no longer builds `HexHost`, `KEYS_BUILD_HEX_HOST`, or `src/ui/HarmonicTable.*`;
  `KEYS_HEX` is gone from the remaining two targets.

### Added: MCP (Claude can drive Keys directly)
- Keys now embeds an MCP server (`okstudio::mcp::Server`, from the kit) so Claude
  Code or any local MCP client can read and drive it: set parameters, play notes or
  a whole timed phrase, capture/fire/clear chord pads, and read/write arp patterns
  (`src/mcp/KeysMcp.h`/`.cpp`, `docs/MCP.md`). A new `keys-mcp` stdio bridge exe
  (`KEYS_BUILD_MCP_SHIM`, on by default) serves all three products; the in-plugin
  server binds loopback only, on an OS-assigned port, and starts automatically (no
  new UI, no session/state changes).
- `KeysProcessor::noteOff` gained an optional `delaySeconds` parameter (mirroring
  `noteOn`), used to schedule MCP-triggered note-offs through the same collector
  timestamp mechanism as everything else; existing call sites are unaffected
  (defaulted to 0).

### Added — Arpeggiator (all three products)
- A pattern-lane arpeggiator in the MIDI path (keyboard/pads -> **arp** -> hosted
  instrument or MIDI out), designed from a verified research pass over Cthulhu,
  Kirnu Cream, Stepic, and Serum's manuals (`docs/ARP_DESIGN.md`). Six per-step
  lanes (note/order, octave, velocity, gate, ratchet, probability), each with its
  own length and clock divider for polymeter; 8 directional modes; 1..4 octave
  range; swing; latch; retrigger; rate 16 bars..1/64 with separate Dot/Trip
  toggles and a bar-Anchor switch (Serum's clock model). Keeps playing on an
  internal clock when the transport is stopped. Patterns A-H with on-screen
  Copy/Paste/Randomize (no modifier gestures anywhere, unlike Cthulhu/Serum).
  Engine is pure and unit-tested (`ArpEngine.h`, `tests/ArpTests.cpp`).
- **Parameter layout gained the `arp*` parameters and the earlier `uiLayout`** -
  new sessions save fine either way, but sessions saved with this build will not
  fully restore in older builds. Upgrade all instances together.
- Contract note: the arp stage is now the one place Keys reads the host playhead
  (tempo sync); `CLAUDE.md` amended.

### Added — Layouts, auto-assigned faders, Hex Host
- **Named UI layouts** via a new Layout combo (and `uiLayout` parameter, saved with
  the session): **Classic** is the existing arrangement; **Performer** keeps the
  8 CC faders and the XY pad in a permanent control strip between the header and
  the keyboard, hardware-controller style (their surface tabs disappear since they
  are always up). New layouts get appended over time; existing ones never change
  meaning. Top-level editors grow to fit Performer; inside Keys Host the host
  window grows instead.
- **Auto-assigned faders (Keys Host / Hex Host):** loading an instrument scans its
  parameter list and binds the 8 faders to the likeliest targets by name (cutoff,
  resonance, attack, decay, sustain, release, reverb/wet, drive). A bound fader
  drives that parameter directly (its label shows the target) while still sending
  its CC. Bindings are recomputed on every load, deliberately not persisted.
- **Hex Host**, a third product (`KyHx`): the Keys Host engine with the Harmonic
  Table as the default surface, so a hex-grid instrument and a piano instrument can
  each live on their own track.
- UI polish: the Mod and Pitch wheels are now wide hardware-style wheels with a
  chunky grab bar; the chord-pad "Excl" toggle is labeled **Exclusive** and no
  longer truncates; Keys Host opens at its compact size instead of needing an
  immediate resize.
- Performer refinements from first real use: taller fader strip and a bigger XY pad
  (room to breathe), and the chord pads become a **4x4 grid beside the keyboard**
  (capture card on top, **All Off** parked underneath).
- **All Off is no longer harsh**: it now sends per-note note-offs on every channel
  plus CC123, so everything ends through its release envelope. CC120 (All Sound
  Off), which choked releasing tails dead, is gone.

### Added — Keys Host (keyboard + your instrument in one window)
- A third product, **Keys Host**: the full Keys UI with one hosted instrument VST3
  above it, in a single plugin on a single track. From Live's point of view it is just
  an instrument that makes sound, which sidesteps the "no plugin MIDI effects before
  an instrument" rule without any track routing. Design notes:
  `docs/KEYS_HOST_DESIGN.md`.
  - **Load Instrument…** opens an in-window, mouse-only list of every installed VST3
    from the folders Live scans, **grouped by publisher** (read from the bundle's
    `moduleinfo.json` or the DLL's version resource — metadata only, so no plugin is
    instantiated until clicked, listing can't crash, and no scanner is needed), with
    a file-browser fallback for odd install locations. Dropping a `.vst3` from
    Explorer onto the window also loads it. (Dragging from Live's own browser is
    impossible for any plugin — Live's browser drags never leave Live.) **Eject**
    and **Show/Hide Instrument** live on the same top bar.
  - The hosted instrument's GUI opens in its **own floating window** above the
    keyboard (two windows, not one stacked editor). Show/Hide Instrument toggles it;
    the window's close button only hides, never ejects.
  - Clicked notes, chord pads, faders, and the XY pad all feed the hosted instrument
    directly. The instrument's **complete state (including its own MIDI Learn
    mappings) is saved inside the Live set**, so CC assignments persist with the
    project — assign once, keep forever.
  - Sessions share the same `KEYS` state root as plain Keys, so pads and settings are
    interchangeable between the two products.
  - Playing inside Keys Host is internal to the plugin: Live doesn't record it as
    MIDI clips on the same track (add a listener track with "MIDI From: Keys Host" to
    capture it). The two-track workflow in `docs/ABLETON_LIVE.md` still records
    natively.
  - Keys Host always emits the played notes as track MIDI output, even with an
    instrument loaded (the hosted synth gets its own copy of the MIDI, so it can't
    eat the track's). "MIDI From: Keys Host" therefore drives Ableton's own
    instruments on other tracks — native Live devices can't be hosted inside any
    plugin, so that routing is the supported way to play them from Keys Host.
  - Built by default (`-DKEYS_BUILD_HOST=OFF` to skip). New plugin code `KyHo`;
    plain Keys' parameter layout and sessions are untouched.
- Internal: `KeysProcessor`'s chord-pad serialization is now the protected
  `chordPadsToTree()`/`chordPadsFromTree()` pair (no format change), and the
  auto-updater check is compiled out of the editor when embedded in Keys Host.

### Added — Keys FX (experimental MIDI-effect variant, off by default)
- A second product, **Keys FX**, built from the same UI and logic but classified as a
  **MIDI effect** (VST3 sub-category `Fx`) instead of an instrument. The intent was a
  build that sits *before* an instrument in the device chain (drop it in front of a
  synth and play that synth with it) rather than taking the single instrument slot and
  replacing the synth.
  - Enabled by an optional `MIDI_EFFECT` flag on the kit's `okstudio_add_plugin`
    (flips `IS_SYNTH`/`IS_MIDI_EFFECT` and the VST3 category). The processor drops its
    audio output bus and reports `isMidiEffect()` when built with `KEYS_MIDI_EFFECT=1`.
  - **Ableton Live rejects it.** Live classifies a third-party VST3 MIDI effect as an
    *audio* effect and refuses to place it before an instrument ("insert audio effects
    after instruments") — that slot is reserved for native and Max for Live devices.
    Keys FX therefore has no use in Live today; it remains valid for DAWs that allow
    VST3 MIDI effects (Bitwig, Cubase, Reaper). See `docs/ABLETON_LIVE.md` for the full
    findings and the same-track workarounds.
  - Off by default (`-DKEYS_BUILD_MIDI_EFFECT=ON` to build it) so dev builds don't drop
    a dead audio effect into Live.

### Added — Octavium parity overhaul
- **Five playing surfaces**, switchable by an on-screen tab row, replacing Octavium's
  separate windows: **Keys** (the piano), **Hex** (the Harmonic Table), **Pads** (4x4
  note grid), **Faders**, and **XY**. Latch, Sustain, Voices, Octave, Humanize, and
  All Off apply to whichever note surface is up; switching away from a surface
  silences it so nothing rings unseen. Chord pads sit above the tabs and keep
  sounding regardless.
  - **Harmonic Table**: Octavium's isomorphic hex grid (9 rows x 18 columns, C1 at the
    bottom left). Up a row is +7, upper-right +4, upper-left +3, so chord shapes are
    the same everywhere. Every hex sharing a sounding note lights together. Scale Lock
    snaps and dims here exactly like the piano (Octavium's table had no scale
    awareness).
  - **Pad Grid**: 16 note pads from C1, ascending left-to-right bottom-to-top, on its
    own MIDI channel (**Pad Ch**, default 10) so drums land where drum instruments
    listen while the keyboard plays elsewhere. Follows Octave; deliberately ignores
    Scale Lock (snapping would silently swap which drum a pad hits).
  - **Faders**: eight CC faders (defaults: Mod, Volume, Cutoff, Pan, Resonance,
    Attack, Expression, Reverb). The label under each fader reassigns it in one click;
    Octavium needed a menu-bar dialog. Assignments persist; positions are performance
    state and nothing is sent until a fader moves.
  - **XY Pad**: one drag sends two CCs (X default Mod, Y default Cutoff, both
    assignable). Up is more. **Lock X / Lock Y** freeze an axis; **Reset** recentres
    to 64/64 and says so in MIDI.
- **Right-click per-note latch** on the piano, hex grid, and pad grid, at Owen's
  request: right-click toggles a note held, independent of the drag gesture, exactly
  like Octavium. It is an optional accelerator: the on-screen Latch toggle remains the
  left-click path, per the accessibility contract (amended in the kit to say exactly
  that). Unlike Octavium, panic and Latch-off actually clear right-click-latched notes
  (Octavium's survived All Notes Off forever).
- **Markov chord source** in the generator (Source: Algorithmic / Markov): walks
  bigram tables of real progressions per mode (Major / Minor / Modal) with
  **Temperature** (0.30-2.00, conservative to adventurous), **Length** (4-16 unique
  chords, looped to fill the page), a **Mood** filter, and a **Start chord** picker.
  Per-pad **New** regenerates through the chain from the previous pad's numeral.
  The progression corpus is authored fresh for Keys (88 progressions: 30 Major, 30
  Minor, 28 Modal, mood-tagged with Octavium's documented vocabulary): Octavium's
  corpus was never in its repo or installer, so its shipped Markov source silently
  produced I-I-I-I for every user. A corpus lint test parses every numeral, and a
  typo'd chord suffix is a test failure rather than silently becoming a plain triad.
- **Chord pads: 16 per page** (two rows of eight, 64 slots across the 4 pages),
  matching Octavium's 4x4 grid; the generator overlay shows the full 4x4 and Fill
  seeds all seven degrees in order before sampling the other nine. Sessions saved
  with 8-a-page pages load with every pad on the page it was on.
- **Recall**: drag a pad onto the live chord card to latch its notes back onto the
  keyboard (or hex grid) for editing, the reverse of capture. From a non-note surface
  it hops to the piano first so the chord is visible.
- **Suggestion preview**: every entry in the **Next** menu has a play button that
  auditions the suggested chord for 800 ms without closing the menu, like Octavium's.

### Changed
- **Parameter layout changed (loudly)**: 16 parameters added — `surface`,
  `padChannel`, `faderCC1`-`faderCC8`, `xyCCX`, `xyCCY`, `genSource`, `markovMode`,
  `markovTemp`, `markovLength`. Sessions saved before load fine and keep their
  settings; the new parameters take their defaults. Chord-pad slots migrate from
  8-a-page saves automatically.
- **All Off** now sends CC120 (All Sound Off) as well as CC123 on every channel, as
  Octavium's chord-pad panic did, so releasing envelopes cut too.
- The **pitch wheel glides back to centre** over ~160 ms (Octavium's eased return)
  instead of snapping. Both wheels and all CC faders move by **relative drag** and
  never jump to a click, matching Octavium's deliberate slider feel: a stray click
  can't slam a value to an extreme.
- The editor default size grew to 960x600 (minimum 820x480): the playing area now
  flexes to fit the hex grid and pad surfaces; the piano still caps its key height
  and anchors to the bottom.
- **Chord generator** (`Chords` button): fills the chord pads for a key and mode,
  ported from Octavium's Autofill and Options dialogs. Everything Octavium reached by
  right-click is an on-screen button, so it stays mouse-only.
  - 12 **scale modes** with a chord quality per degree (Ionian, Dorian, Phrygian,
    Lydian, Mixolydian, Aeolian, Locrian, Harmonic/Melodic Minor, Blues, and both
    pentatonics), each showing the character it carries.
  - 10 **Feel presets** (Happy, Sad, Dreamy, Dark, Jazzy, Bluesy, Epic, Chill,
    Mysterious, Smooth): one click sets the generator's key and mode *and* moves Root
    and Scale to match, so Scale Lock agrees with the pads.
  - **Scale Compliance** (0-100%): how far outside the key the generator may reach.
    100% stays diatonic; lower opens up modal interchange, then secondary dominants,
    then any chromatic root.
  - **Lock** a pad to keep it through a regenerate. **Lock Influence** (0-100%) steers
    new chords toward the character of what you locked.
  - **New** gives a pad a different chord for the same scale degree; **Next** offers
    chords that could follow it (Neo-Riemannian P/L/R/N/S/H, circle-of-fifths moves,
    diatonic degrees, and chromatic substitutions) and drops the pick into the next
    free pad.
  - Note-count filter (triads / 7ths / 9ths), inversions (root / 1st / 2nd / 3rd),
    and a generator octave. Press a chord in the grid to audition it.
- **Chord pad pages**: four pages (was a single row of eight pads), with `<` / `>`
  navigation. Chords left ringing on another page keep sounding. (Pages later grew
  to 16 pads each; see the overhaul entry above.)
- **All Off** flashes blue when clicked, matching Octavium.

### Fixed
- **All Off** left the chord pads drawn as active: it silenced the notes but never
  cleared the pads' own state.
- Chord pads ignored the **Voices** limit entirely, since voice stealing lived only in
  the keyboard. A pad now drops its highest notes to fit the cap, keeping the lowest.
- The chord generator's diatonic tier is genuinely diatonic. Octavium offers Sus2 and
  Add9 on every major/minor degree without checking the added note is in the key (E
  Sus2 in C major wants F#), so its "strictly diatonic" setting was not. Ported with
  that filtered, since Keys is built on never playing a wrong note.
- Regenerating a chord no longer breaks the note-count filter. Octavium drops the
  filter when a degree has no alternative, which could return a chord of the wrong size
  or the same chord it was asked to replace.

### Changed
- **Parameter layout changed**: 13 parameters added (`padPage`, and the `gen*` set for
  the generator). Sessions saved with 0.1.0 load and keep their settings; the new
  parameters take their defaults.
- Unit tests now print their failures. JUCE's default logger writes to the debugger,
  so a failing test used to exit non-zero and say nothing.
- `src/ScaleModes.h`, `src/ChordGen.h`, `src/ChordSuggest.h`: generation and suggestion
  logic kept free of UI, so it unit-tests like `NoteMath.h` and `Chords.h`.

### Not ported
- Octavium's **MIDI Library** chord source: it reads the external ~30 MB MIDI chord
  pack, which a plugin has no business shipping or hunting for on disk. (This entry
  previously claimed the **Markov** source shared that dependency; it does not — its
  algorithm is self-contained, and the data it expected never shipped at all. It is
  now ported, with a corpus authored for Keys; see Added.)
- Octavium behaviours found to be bugs and fixed rather than reproduced, beyond the
  two generator fixes below: right-click latches surviving panic; the harmonic
  table's Latch-off orphaning latched notes; the pad grid resolving note-offs at
  release time (a held pad's note-off could target the wrong note after an octave
  change); the fader window silently resetting faders 5-8 on rebuild; Markov's
  note-count and inversion checkboxes silently doing nothing (Keys greys them out
  when the Markov source is active).

## [0.1.0] - 2026-07-14

### Added
- Initial Keys plugin: mouse-only playable MIDI keyboard, VST3 + Standalone.
- On-screen piano at 25 / 49 / 61 / 73 / 76 / 88 keys; click to play, drag to glide.
- **Latch** (toggle notes on/off to hold a chord) and **Sustain** (pedal) as
  on-screen toggles, not modifier keys.
- **Scale Lock**: snap played notes to the nearest note in a chosen root and scale;
  out-of-scale keys dimmed.
- Octave shift (-5..+5), velocity with Soft / Linear / Hard curve, MIDI channel 1–16.
- **Mod wheel** (CC1) and **Pitch bend** wheels left of the keyboard; the pitch wheel
  springs back to centre when released.
- **Polyphony** limit (Off / 1–8 voices) with oldest-note voice stealing.
- **Humanize**: each note gets a random velocity within a Min/Max range plus a
  micro-timing offset, an on-screen toggle, so latched or dragged chords feel played
  rather than quantized. With the pedal down, a glide leaves a sustained trail.
- **Chord pads**: build a chord (Latch on, click the notes), drag the live chord card
  onto one of eight pads to capture it (auto-named, e.g. `Cm7`), then play a pad
  beat-pad style: press to fire, release to stop (Sustain holds it). Drag a pad to
  rearrange, or off the row to clear. Exclusive mode
  chokes the previous chord; pads persist with the session. A Strum control (Octavium
  "Drift") spreads a pad's notes over 0-200ms, Up / Down / Random.
- Humanize velocity is a two-handle Min/Max range slider (was two separate sliders).
- **All Off** panic across every channel.
- State (size, scale-lock, root, scale, octave, channel, velocity, curve, sustain,
  latch) persists with the DAW session.
- Fail-closed auto-updater from the shared kit (pinned OK Studio EV cert).
- Build script, Inno Setup installer, and CI (Windows + macOS) off the OK Studio line.
- Unit tests (JUCE UnitTest + ctest) for note resolution; CI builds and runs them.
- `src/NoteMath.h`: note-resolution logic factored out of the keyboard widget so it
  is unit-testable without a UI.

[Unreleased]: https://github.com/owenpkent/Keys/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/owenpkent/Keys/releases/tag/v0.1.0
