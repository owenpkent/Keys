# Changelog

All notable changes to Keys are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/); versions are semver.

## [Unreleased]

### Added: Send a chord pad to arp A or B from its own menu

Owen: *"I'd like to be able to right click on a chord pad and say send to ARP a or b"*. Two rows
on a pad's card menu, beside the Send to arp slot submenu that has always been there: the chord
goes straight into that line and the line becomes the current one, which is what a card
**dragged** onto that line's switch or its macro card has done since 2026-08-02.

**An accelerator, not a new right-click-only path.** It is the drag with the aim taken out of the
mouse, exactly the relationship Send to arp slot has with a drop on a slot card, and both now run
through one method - `KeysEditor::sendPadToArpLine` - so the two gestures cannot drift.
`ArpPanel::takeChordOnLine` takes a pad slot rather than a drag payload to make that possible,
which is all the drop ever wanted out of one.

One row per line the UI shows, so C returns here the day it returns anywhere. Both rows are live
on a line that is switched **off**, on purpose: a line that is off still takes a chord in, and
switching it on then plays what it was handed.

The menu is 11 rows and 408 px now, up from 9 and 340. Two rows rather than a submenu costing
one, because Owen asked for the two by name and this menu can afford the height.

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
