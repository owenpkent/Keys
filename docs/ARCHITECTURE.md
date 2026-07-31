# Architecture

Keys is a small JUCE plugin. It makes no sound; it turns mouse gestures on an
on-screen piano into MIDI. Everything shared with the rest of the line comes from
[`../okstudio-juce-kit`](../../okstudio-juce-kit).

## Files

```
src/
├── PluginProcessor.{h,cpp}   # AudioProcessor: params, MIDI output, chord pads, state
├── PluginEditor.{h,cpp}      # folding/detaching sections, layout, updater button
├── NoteMath.h                # pure note resolution (snap + transpose), unit-tested
├── Chords.h                  # pure chord detector (names a note set), unit-tested
├── ScaleModes.h              # 12 modes + a chord quality per degree; emotion lines
├── ChordGen.h                # pure chord generation (weighted pool), unit-tested
├── ChordSuggest.h            # pure "what could follow this chord", unit-tested
├── ChordMarkov.h             # pure Markov progression source, unit-tested
├── MarkovData.h              # the bundled progression corpus ChordMarkov walks
├── ArpEngine.h               # pure arpeggiator core, unit-tested; the one playhead
│                             # reader in Keys (docs/ARP_DESIGN.md)
├── ui/
│   ├── NoteSurface.{h,cpp}   # shared note bookkeeping every playable surface derives
│   ├── PianoKeyboard.{h,cpp} # the piano surface (geometry + paint over NoteSurface);
│   │                         # built by Keys and Keys Host
│   ├── KnobBank.{h,cpp}      # eight assignable CC rotary knobs, the bottom row of the
│   │                         # Controls section
│   ├── CCMenu.h              # the one-click CC picker the knob row uses
│   ├── ChordPads.{h,cpp}     # chord-pad rows + live chord card (capture / recall)
│   ├── ChordGenMenu.{h,cpp}  # the chord generator (algorithmic + Markov). No panel:
│   │                         # two chips on the Pads bar and items on the pad menu
│   ├── ArpPanel.{h,cpp}      # the arp section: Shape gates a tabbed lane editor,
│   │                         # plus the control band and twelve launchable slots
│   ├── SectionBar.h          # the fold/unfold header above a section of the editor
│   ├── RangeSlider.h         # two-value slider whose band drags as one (velocity, strum)
│   ├── DetachedWindow.h      # a section popped out into its own resizable window
│   │                         # (any of them since 2026-07-27; was KeyboardWindow.h)
│   └── KeysLookAndFeel.{h,cpp} # the skin: tokens, raised fills, accent glow
├── host/                     # Keys Host only (docs/KEYS_HOST_DESIGN.md)
│   ├── KeysHostProcessor.{h,cpp} # KeysProcessor + one hosted instrument VST3
│   └── KeysHostEditor.{h,cpp}    # top bar, instrument picker, floating instrument window
└── mcp/
    └── KeysMcp.{h,cpp}       # MCP tool registrations; every handler runs on the
                              # message thread (docs/MCP.md)
```

## Keys consumes no audio

Every part of Keys produces MIDI and none of it reads any. There was a **Transcribe**
section for a while, built on the kit's basic-pitch engine, and it was removed on
2026-07-30: `TranscribePanel`, `AudioCapture`, the `KEYS_TRANSCRIBE` option and everything
they dragged in are gone from this repo.

The engine itself is not lost. It stays in the kit (`okstudio/Transcribe.h`, its own
`okstudio_basicpitch` target, the kit's `docs/TRANSCRIPTION.md`) for whichever product wants
it next; Keys simply stops asking for it. What Keys sheds with it is that target's cost: a
multi-gigabyte ONNX Runtime download on the first configure, and the static MSVC runtime that
library forces on the *whole* binary. Keys is back on the default dynamic CRT, and `build/` is
cheap to delete again. See `docs/BUILD.md`.

## Threading: UI → audio note path

The keyboard runs on the message (UI) thread. It must not write to the outgoing
`MidiBuffer` directly. Instead:

- `PianoKeyboard` calls `KeysProcessor::noteOn/noteOff/allNotesOff`.
- Those build a `juce::MidiMessage`, stamp it with `Time::getMillisecondCounterHiRes()`,
  and hand it to a `juce::MidiMessageCollector`.
- **One note-on per sounding pitch.** `noteRefs` counts how many sources own each pitch,
  and MIDI is emitted only on the 0→1 transition and released only on 1→0. Four sources
  can want the same pitch at once (a chord pad, the live chord card, a chord held into the
  arpeggiator, and the keybed); emitting a second note-on for a pitch already sounding
  means one source's release ends it for everybody, which left keys lit with nothing
  sounding and leaked the arpeggiator's held set. Any new chord source must go through
  these two functions rather than the collector.
- `processBlock` calls `collector.removeNextBlockOfMessages(midi, numSamples)`, which
  drops the queued events into the block at the right offsets. Any MIDI already on
  the track (a clip, another device) passes through untouched.
- Before that drain, `watchInputNotes()` notes which pitches the *incoming* stream turns
  on and off (`inputNoteOn`, a flag per pitch, never a count — a missed note-off would
  leak a refcount into a key lit forever). It has to run first: afterwards the same buffer
  also holds this plugin's own notes, which `noteRefs` already tracks. Nothing is
  consumed or altered; `isNoteSounding()` simply also answers true for those pitches, so
  the keybed lights up for someone playing a physical keyboard through Keys and the live
  chord card can name what is under their hands. Added 2026-07-27.

The audio thread does nothing else: `buffer.clear()` (silence), watch the input, drain the
collector, and, with the arp on, run the arp stage over what came out (`docs/ARP_DESIGN.md`).
No allocation, no locks.

## Playing surface: one product, one piano

The **playing surface** is not a choice: `KeysEditor` builds a `PianoKeyboard` (which
derives from `NoteSurface`), and that is the only surface Keys and Keys Host ship. There
used to be five tabbed *surfaces* (Keys/Hex/Pads/Faders/XY, switched by a `surface`
parameter); the Pad Grid was cut outright (drums belong to Beatform), the Faders and XY
surfaces were replaced by the knob row, and the Hex surface moved out to its own repo
(`../Hex`) along with Hex Host. Their parameters are still registered — see **Parameters
and state**.

There are no tabs left anywhere in the editor. The centre view that used to sit between the
Controls and the Arp went with the chord generator's panel on 2026-07-30; the knob bank it
held is the bottom row of the Controls section now, and what is left is a plain stack of
four sections. See **Folding layout**.

## Note bookkeeping: one union, one diff (NoteSurface)

Chords and holds make "which notes should sound" non-trivial. `NoteSurface` keeps
three sets of **drawn** ids (a MIDI note for the piano, a cell index for the grids):

- `pressed` — under the active mouse gesture (a click, or the current key in a glide),
- `latched` — toggled on by the Latch button or right-click (or by the forced latch
  during pad editing),
- `sustained` — captured by the Sustain pedal when the mouse released.

**Sustain and Latch differ only in what a second click does**, and that difference is the
whole reason both exist. Latch is a switch: a click on a key already in `latched`
**releases** it. Sustain is a pedal: a click on a key already in `sustained` **strikes it
again**, which `mouseDown` gets by dropping it from `sustained`, calling `refresh()` to
emit the note-off, and only then pressing it (`refresh()` emits deltas, so without the
first call nothing would go out at all). Gliding back over a sustained key does the same.

The Latch button was briefly retired, on the reasoning that a plain click already both
held and released — which is true, but it made the pedal behave like a switch, so a
repeated note under a held chord was impossible. Restored 2026-07-30 as its own toggle on
the Keyboard bar, reading the `latch` parameter that had survived for pad editing (which
still forces it on regardless of the button).

`refresh()` computes `want = pressed ∪ latched ∪ sustained`, diffs it against
`sounding` (drawn id → the output MIDI note currently on), and emits exactly the
delta: note-offs for keys that left `want`, note-ons for keys that entered it. Every
gesture mutates the sets then calls `refresh()`, so notes never double-fire or stick,
and panic is just "clear all three, refresh". When a polyphony limit is set, `refresh()`
first steals the oldest voices (FIFO `voiceOrder`) until `want` fits the cap. Changing
MIDI channel panics, so a note can't stick on the channel it was played on.

**Right-click latch** is an optional accelerator on every note surface, and it works on the
same three sets, so panic clears whatever it did. Octavium kept right-click latches in a
private set no panic ever cleared; that was its worst stuck-note bug, not a behaviour to
keep.

What it does depends on whether the key is already ringing *on this surface*, and **release
beats latch**: a right-click on a key held in either `latched` or `sustained` erases it from
both and lets it go; only a silent key latches. Erasing both unconditionally is also what
fixes a key caught by both, which used to leave one set and keep sounding out of the other.
The point of the sustained half is that a chord the pedal is holding comes apart a note at a
time without lifting Sustain, which is a thing one mouse could not otherwise do. Nothing
here can release a key lit by a chord pad, the arp or MCP: those never enter these sets, so
`refresh()` finds no `sounding` entry and their refcounts are left alone.

**One sanctioned exception to the left-click twin rule** (Owen, 2026-07-30): the latched
case has one, because a left click on a latched key releases it too (that path is keyed on
`latched`, not on Latch mode, so it works with every button off), but the *sustained* case
does not. Under Sustain a left click on a ringing key strikes it again by design, so a
left-click twin for "let this one pedal note go" would have to overwrite the behaviour the
pedal exists for.

A subclass provides only geometry: `drawnAt` (hit test), `outputNote` (resolution),
and optionally `noteChannel` (defaults to the global channel param; no built-in
surface overrides it any more since the Pad Grid was cut, and the Hex surface moved
to its own repo).

## Note resolution: snap then transpose, remembered

When a key starts sounding, `outputNote(drawnKey)` applies scale-lock
(`okstudio::scales::snapToScale`) then the octave shift, clamped to 0..127. The
result is stored in `sounding[drawnKey]`. Note-off uses that stored value, so a note
turns off correctly even if you change octave or scale while it is held.

## Humanize

The velocity range is the only velocity control. `KeysProcessor::noteOn` draws a
uniform-random velocity in `[humanizeVelMin, humanizeVelMax]` per note when Humanize is on;
with it off, `baseVelocity01` returns the band's midpoint. There used to be a fixed
Velocity slider *as well*, but it only ever applied while Humanize was off, so the two
were one control in two costumes. Collapsing the band onto a single value is now how you
ask for a fixed velocity. A `juce::Random`, touched only on the message thread, drives it.

The timing half of Humanize is gone. It nudged the message timestamp, which
`MidiMessageCollector` flattens (see **Scheduling** below), so it never worked; and even
working it earned nothing that Strum does not do better.

## Scheduling: why a delay cannot go through the collector

`noteOn`'s `delaySeconds` is not a scheduler. `juce::MidiMessageCollector` empties its
**entire** queue into the current block on every callback and clamps each event into it,
so anything beyond one buffer (~10 ms at 512 samples) lands on the end of that buffer.

Chord-pad Strum asks for up to 200 ms and was doing exactly that: every chord arrived as a
block however far the slider was pushed. `scheduleNoteOn` holds the note and emits it when
it comes due, on the message thread, the same way the MCP bridge defers notes. Its timer
runs only while something is pending. Every path that stops sound (`stopChordPad`,
`allNotesOff`) also calls `cancelScheduledNotes`, because a note-on that fires after its
note-off is a stuck note nothing clears.

`pressChordPad` and `pressLiveChord` share `fireChord`, so the polyphony cap, the strum
ordering and the scheduling are one implementation rather than two that drift.

## Chord pads

Pads live in the processor (`ChordPad`), so they persist and keep
sounding independent of the editor; `ChordPads` is just the view. They are arranged as
four pages of sixteen (`padsPerPage` × `numPadPages`, Octavium's 4x4 per page; drawn as two
rows of eight, or as that 4x4 with the full chord card on each when **Big** is on). The strip shows the page `padPage`
selects and indexes by **absolute slot**, so a chord left ringing on another page keeps
sounding and a drag can't land on the wrong pad. Sessions saved when pages held eight
carry a `padsPerPage` marker (absent = 8) and each slot is re-based on load, so every
pad stays on the page it was on. Build a chord on the
keyboard (Sustain or right-click), drag the live card onto a pad to `setChordPad` the notes
(named by `keys::chords::detect`), then play it beat-pad style: mouse-down calls
`pressChordPad` (fire, honouring the `chordExclusive` choke) and mouse-up calls
`releaseChordPad` (stop, unless Sustain is holding it — the editor releases held pad
chords when the pedal lifts). Dropping a pad on the live card runs the other
direction: `onRecall` hands its notes to the active note surface, which latches the
drawn keys that produce them (`recallOutputNotes`), so a stored chord comes back for
editing. Playback reuses `noteOn`, so Humanize colours each tone,
and `chordStrum` / `chordStrumMax` / `chordStrumDir` order the notes and stagger their
note-ons into a strum: the two ends are a band, and each chord draws one spread from it
(via `scheduleNoteOn`, for the reason in **Scheduling** above), so repeated stabs do not
all rake at exactly the same speed. Detection (`Chords.h`) rotates a pitch-class set
over 12 candidate roots and scores each against a template library — root mandatory,
3rd and 5th omittable — so it is unit-testable with no UI.

A pad fires as one gesture, so there is no "oldest" voice within it: `pressChordPad`
honours the Voices cap by dropping its highest notes and keeping the lowest, which is
how `refresh()` resolves a too-big simultaneous chord on the keyboard. The cap applies
per source — a pad and the keyboard each fit under it separately.

## Chord generation

`ChordGen.h` builds a **weighted pool** of candidate chords for a key and mode, then
samples it. That one idea carries both sliders in the generator:

- **Scale Compliance** decides which tiers enter the pool: diatonic (always, weight 1),
  then borrowed from parallel modes, then secondary dominants, then any chromatic root,
  each fading in as compliance drops and weighted by how far it opened.
- **Lock Influence** re-weights the whole pool toward the *families* (triad / seventh /
  sixth / add / extended) of the chords you locked — so regenerating keeps their
  character without copying them.

A fill seeds the plain diatonic chord on each degree in order, then weighted-samples the
rest and shuffles only that tail. Each chord remembers the `degree` it came from, which
is what lets **New** hand back a different chord *for the same degree*.

The modes (`ScaleModes.h`) are deliberately **not** the kit's `okstudio::scales`. That
table answers "is this note in the scale", which is all Scale Lock needs; generation also
needs a chord quality per degree. They stay separate rather than one pretending to be the
other, and `kitScaleIndex` pairs them by comparing intervals — so a rename on either side
cannot silently mis-pair them. That mapping is what let a Feel preset move Root and
Scale along with the generator's own key; the Feel row went in the 2026-07-22 generator
redesign, so today `modes::emotions()` and `kitScaleIndex` have no caller but the test
that keeps every mode pairable, and they are kept for whatever brings the presets back.

`ChordSuggest.h` answers "what could follow this chord". Octavium writes each transform
as its own function; they are all the same shape (shift the root by an interval, maybe
flip the quality), so here they are one table. Whether a transform lands on major or
minor is read off the source chord's third rather than a name list, so it stays right as
types are added.

`ChordMarkov.h` is the second generator source: bigram transition tables built from
`MarkovData.h`'s progression corpus per mode (Major / Minor / Modal), sampled with a
temperature (`count^(1/T)`, clamped 0.3-2.0), an optional mood pre-filter, and an
optional start token; the walk repeats-and-truncates to fill the page. Per-pad
regeneration follows the chain from the previous pad's roman numeral (stored on the
pad as `numeral`), avoiding the chord it replaces when alternatives exist. The
algorithm is Octavium's, verified line-by-line; the corpus is authored for Keys,
because Octavium's was never in its repo or installer — its shipped Markov source
degenerated to I-I-I-I for every user. Realisation is root-position through
`chordgen::chordNotes` at the generator octave (Octavium hardcoded octave 4).

All these headers are pure logic with no UI, so they unit-test like `NoteMath.h`.

## Folding layout

The editor is a stack of **four** sections, each of which folds away so the window can be
squeezed small when the screen is busy, and, since 2026-07-27, each of which also detaches
into a window of its own: **Controls** (the two header rows plus the knob bank under them,
which has its own Knobs sub-fold), the **Arp**, the **Pads**, and the **Keyboard** (with the
wheels as a sub-fold). It was six until 2026-07-30, when the centre view and Transcribe both
went; the centre's knob bank became the bottom row of Controls rather than a section of its
own, because it is two rows of settings and eight knobs, not a view you switch to.

`SectionBar` is the fold affordance: a `juce::Button`, so the mouse-only contract and the
accessible name come for free. It calls `setTitle(caption + " section")`, which means the
capture script's UI Automation path *can* fold and unfold a section (a bar answers to
"Arp section", never to the bare caption a control riding on it might share). The section's
own small controls are laid out as its siblings in `contentArea()`. One trap:
`captionWidth()` feeds both the caption's own text box and `contentArea()`, so it and
`paintButton()` have to measure with the same `captionFont()`. Measure narrower than you
draw and the longest caption ellipsises; vary the font with the fold state and every control
on the bar shifts when the section folds.

The **arpeggiator is a section of its own** rather than a centre view (changed 2026-07-25).
Competing with the knobs and the generator was backwards for a panel that runs while you
play, and the arp is the one thing you want on screen *next to* a chord. Its bar carries the
**On** toggle, the **Hold off** chip and a **Detach**; the first two survive folding the
panel away, because folding it destroys the view and never the arpeggiator, and a chord held
into a folded arp needs a way out that is still on screen (see `docs/ARP_DESIGN.md`).

The **chord pads are a section of their own** too, below the arp. They used to live inside
the centre view, which meant the arpeggiator (the one panel whose whole job is to chew on a
chord) was also the one place you could not reach a chord. Their page buttons ride on the
Pads bar, and so do the generator's **Fill** and **Regen** chips, at the right end. What a
card click *means* is the arp's own On state (`KeysProcessor::cardsFeedArp`): with the arp
running, a click hands that chord over and leaves it there instead of playing it while the
button is down, and a click on the card *already* feeding the arp retriggers it.

**Only the left end of a bar folds it** (2026-07-30, Owen's ask). `SectionBar::hitTest`
narrows the button to `foldZone()`, the chevron and the caption, 92 px wide at the narrowest
caption; a hairline is painted where that target ends, and only that end lights under the
mouse. The bars are still full-width Buttons sent `toBack()` after construction, and the
controls riding them are siblings sitting in front, so z-order has always meant that a click
landing *on* Detach reaches Detach.

This **reverses** the 2026-07-27 change that removed the same override. For three days the
whole strip folded, on the reasoning that a 34 px-tall full-width band is a bigger target and
that z-order already protected the controls. The second half was true and still is; what it
missed is that z-order only defends each control's own rectangle. It says nothing about the
gaps around them, and on a bar whose right end is mostly gap, a click aimed at Detach that
missed by a few pixels hit bar, and the bar hid the thing being reached into. That cost is
asymmetric, so bigger is only kinder when the extra area does what the target does. Docs
elsewhere in the line that still say "the whole bar is the target" are describing that
three-day window.

Open and folded bars are painted at deliberately different weights (the open one a solid
ruled band with an accent tick, the folded one flat and dim), so a stack of four reads as a
shape before any caption is read. The Detach button hides with its section for the same
reason, and because detaching a folded section only ever built a window that opened hidden.

## The accent is per instance

Keys used to have exactly one accent, the OK Studio cyan. It is now one of eight, chosen
per instance, because a session with Keys on two tracks gave two identical windows.

This is deliberately **not** a global. A DAW loads every instance into one process, so a
mutable global would repaint every track's Keys together. The live triple (base / hot /
deep) hangs off each editor's `KeysLookAndFeel`; components call `skin::accentOf(*this)`,
which resolves through the LookAndFeel chain JUCE already walks up to the editor. Paint
loops hoist that call rather than paying a `dynamic_cast` per key or per step.

Two things are easy to miss when adding a colour path. The kit bakes several JUCE
`ColourId`s from the accent at construction (tick marks, slider tracks, the popup
highlight), so `setAccent` re-applies every one of them. And `wheelLnf` is a *second*
LookAndFeel instance with its own copy, so it has to be re-tinted alongside `lnf`.

This replaced full-editor overlays that dimmed and covered everything, including the
keyboard. Editing an arp while unable to play a note was backwards for an instrument you
perform. The panels keep an overlay mode (`setInlineMode(false)`) but nothing uses it.

`KeysEditor::idealHeight()` is the single source of truth for what the folds add up to;
`resized()` spends exactly the same constants, so the window a fold asks for and the
layout it gets cannot drift. Standalone and in a DAW the editor resizes itself to that
height; embedded in Keys Host it reports the number through `onIdealHeightChanged` and the
host grows to fit, because an open arp would otherwise push the keybed off the end.

### Every section detaches

Each section's content lives in a `Holder` of its own rather than directly in the editor, so
**Detach** is a single re-parent into a `DetachedWindow` — the holder's parent becomes the
window's content slot and nothing else changes. The window borrows the holder and owns
nothing, so `~KeysEditor` tears every one of them down explicitly before anything else.

`KeysEditor::sections` is the table that makes this generic: one `Section` per bar, holding
its holder, its Detach button, the window it is currently in, the two `LayoutState` flags it
reads (open, detached) and the frame it remembers. `sectionHeight()`, `idealHeight()`,
`resized()` and `syncSectionControls()` all loop over it, so a detached section contributes
no height to the main window and a folded one hides its window instead of its slot — one
control means one thing wherever the section happens to be.

The keybed was the first to do this and keeps two extras. Detached, `PianoKeyboard`'s 185 px
key-height cap comes off: dragging that window is meant to resize the keys, which is the whole
point of the feature for a player working with one mouse. And the **Wheels** chip and a second
**Size** selector travel with it (`Section::travellers`), because they are the keybed's, not
the editor's. Every detached window carries the button that undoes the detach on a strip at
the top, so the control that re-docks a section is never in the window you are not looking at.

Controls that belong to the *editor* rather than to the content stay behind on the bar: the
arp's **On** and **Hold off**, the pads' page buttons and the generator's **Fill** / **Regen**
chips, the Controls bar's **Knobs** chip and theme swatch. A bar whose section is away says
so, in the space its own controls did not use.

All of it (folds, detached window bounds) is in `KeysProcessor::LayoutState` rather than the
editor, so it survives the window closing, and it is saved in the session tree rather than as
parameters: none of it changes a note, and exposing it to host automation would only add ways
to break a session.

## The chord generator has no panel

`ChordGenMenu` is a plain value member of `KeysEditor` (`chordGen`), not a view and never a
dialog: a plugin editor has no business opening OS windows, and there is nothing left for a
panel to draw. It works on the **current page**, so the four pages can hold four different
keys.

**It lost its cards first** (2026-07-30). `ChordGenPanel` drew a 4x4 grid of the sixteen pads
on the current page (the same pads, through the same `setChordPad`), because it was written
when the generator was a full-screen overlay and the pads had no section of their own. They
have had one since 2026-07-25, so the grid was the same page drawn twice, at two sizes, one
of which could set a pad's lock state and one of which could only paint the dot for it. The
tall arrangement became the Pads section's **Big** switch (`layout.padsBig`, four rows of
four), so the large card (chord name, its notes with octave numbers, a mini keyboard of what
is held) is available whatever else is open.

**Then it lost the panel too**, in the same round that removed the centre view. With the
cards gone, what was left was sliders and combo boxes for settings, sitting in a view you had
to switch to. Everything moved onto the pad's own right-click menu, which is where the chord
it applies to already is:

- **Lock**, **New chord** and **Next** are items, through `addPadMenuItems` /
  `addPageMenuItems` / `handlePadMenuChoice`. They are now offered on **every pad on every
  page, always**: the panel used to gate them on being alive, so the generator's own actions
  were unreachable from a pad whenever the view was folded or another one was up. Making the
  generator a plain member is precisely what eliminated that gate, and the test that asserted
  it went with it.
- Every **setting** is a submenu of a single **Generator settings** wrapper: source, key and
  mode, octave, note counts, inversions, Scale Compliance, Lock Influence, then one **Markov
  chains** submenu for the five that are inert unless the source is Markov. A submenu shows
  its live value in its own caption, so nothing needs a panel to read the state back.
- **The menu has a hard budget, and it is rows.** It is anchored to a pad near the bottom of
  a 699 px window and shown at `withStandardItemHeight(okstudio::ui::minHitPx)`, so each row
  costs 34 px of screen measured *upwards* from there, and a separator 17. The settings were
  flattened onto the top level for part of 2026-07-30 to save a leg of hover; that took the
  menu to 23 rows plus four section headers, about 820 px, and JUCE answers a menu taller than
  the space it has by splitting it into columns (`insertColumnBreaks`) or making it
  hover-scroll. A scrolling popup cannot be operated with one mouse at all: hovering the arrow
  scrolls and moving to click scrolls the item away. It is **11 top-level rows and three
  separators, 429 px** (34 each, 17 each, plus JUCE's 2 px border top and bottom), and it
  stays that way. The section headers went - a rule says the same thing at half the height,
  and a JUCE section header is not a row but an item and a half, 51 px here, since
  `HeaderItemComponent` asks the LookAndFeel for an item size and then adds half of it again.
  The four suggestion families went behind one **Next: could
  follow** row, and the settings went back behind their wrapper - which cost nothing in the
  end, because **Key**, **Mode** and **Scale Compliance** had become combo boxes on the Pads
  bar in the same session and those are the three anybody reaches for mid-audition.
- **Octave down / Octave up / Next voicing** act on one pad's stored chord (menu-only, Owen's
  call). `chordgen::rootPosition` / `applyVoicing` / `voicingOf` in `ChordGen.h` are the
  voicing cycle: root position, one inversion per note above the root (the same inversions
  `genInv0..genInv3` name), then a spread, then round again. Nothing is remembered on the
  card - `voicingOf` reads the arrangement back off the notes by shape, which is what keeps
  the cycle right for a chord captured from the keyboard. `rootPosition` **collapses a
  repeated pitch class**, which two hands on the keybed produce constantly: keeping it read
  the chord back in the wrong register (so every press climbed an octave until the chord left
  the keyboard) and let an inversion stack one copy onto the other, which is the same MIDI
  note twice on one pad. No arrangement of a doubled note survives the walk - the last
  inversion of a doubled root *is* root position an octave up - so the double goes once, on
  the first press. `ChordPads::rewritePadChord` is the one way any of them writes: it goes
  through `holdArpChordFromPad` for a chord held into the arp and `pressChordPad` for one left
  ringing, so every note-on gives its reference back before the new one takes it. In that
  order, and with **Exclusive** on only the hold is restored: both calls choke every chord
  source, so doing both meant firing, killing and firing again, two strum rolls apart, and
  ending in neither state. A locked pad accepts all three, deliberately: a lock protects a
  chord from *generation*, not from its owner. The **card being edited** does not: all three
  grey out while `slot == editingSlot`, because they write the stored chord and cannot reach
  the keybed, and the edit link would write the un-shifted set back on the next latched note.
- **Fill** and **Regen** are the left-click bulk path, two 24 px chips at the **right end of
  the Pads bar**, and **Key**, **Mode** and **Scale Compliance** are three 24 px combo boxes
  beside them: the settings that get changed while a page is being auditioned, one click to
  open and one to pick. They are `ComboBoxAttachment`s on the same parameters the menu writes,
  so neither place has to know the other exists; Compliance is five steps of a continuous
  0-100 parameter, which an attachment maps exactly. All five controls stay live with the Pads
  section folded, so folding the strip cannot take the right-click menu and the bar together,
  which would be the whole generator. They cost the window no height and 302 px of bar, which
  is what moved `minWidthForView()` back to a single 1010 floor.
- **Clear page** is an item on the pad menu and deliberately *not* a chip. It wipes every
  unlocked pad on the page, there is no `juce::UndoManager` anywhere in Keys, and a bulk
  destructive action with no undo does not belong 4 px from Regen.
- **Fill never overwrites** (2026-07-30, Owen: "new generations shouldn't overwrite
  existing"). `fillPage()` writes the *empty* pads and only those, locked or not - a blank
  needs no protection. `regeneratePage()` is the destructive one and the only one: it rerolls
  the pads that already carry a chord and skips the locked ones, which is what "regenerate"
  means and what the lock is for. They were one function with an `onlyUnlocked` flag, and the
  split is the point rather than a tidy-up: a flag on a shared path is exactly how the safe
  button ended up being the one that could lose sixteen chords. Each chip greys itself out
  when its list of targets is empty (`pageHasEmptyPads` / `pageHasRegeneratablePads`, polled
  from the editor's timer), so which of the two is which is readable from the bar.
- **The lock chip is a target**, not only an indicator (2026-07-30), and it is painted at the
  size of that target. `ChordPads::lockBadgeBounds` is a square in the top-right of a filled
  card, `jmin(34, round(h * 0.55), round(w * 0.30))`: 24 px docked at both the default and the
  minimum window width, which is a section-bar control and the size an accelerator is allowed
  to be beside the menu's own Lock item, and the full 34 on a Big card. It was a flat 34,
  which is 27% of a docked card's area at 980 px and 34% at the 820 px floor - most of the
  height across the whole right-hand end - with a 5 px dot as its only mark, so a click 30 px
  below the dot toggled the lock with nothing on screen to say why. `drawLockBadge` fills that
  rectangle: an inset chip with an open shackle while the chord is open to generation, lit and
  closed once it is set. The click is tested before every play/drag branch in `mouseDown`, so
  it never fires the chord, arms a drag or feeds the arp; neither chip nor branch exists on the
  card being edited, where the tick that ends the edit owns that end. Lock on the card menu
  remains the accelerator.

Auditioning a chord reuses `pressChordPad` / `releaseChordPad`. It always did, and now there
is one card doing it rather than two.

## Parameters and state

All settings are `AudioProcessorValueTreeState` parameters (`size`, `root`, `scale`,
`scaleLock`, `octave`, `channel`, `polyphony`, `sustain`, `latch`,
the Humanize set `humanize` / `humanizeVelMin` / `humanizeVelMax`, the
chord-pad settings `chordExclusive` / `chordStrum` / `chordStrumMax` / `chordStrumDir` /
`padPage`, the generator's `gen*` set — `genRoot`, `genMode`, `genOctave`, the
note-count and inversion
toggles, `genCompliance`, `genLockInfluence` — the knob row's `faderCC1`-`faderCC8`
CC assignments, and the Markov set `genSource`, `markovMode`, `markovTemp`,
`markovLength`.

The arp's own set is `arpOn`, `arpRate` / `arpDot` / `arpTrip` / `arpAnchor`,
`arpDirection` (twelve shapes) + `arpPattern`, `arpOctaves` (Repeats) + `arpDistance`,
`arpOffset`, `arpSwing`, `arpLatch`, `arpRetrigger` + `arpRetrigBars`, `arpGate`,
`arpChance`, `arpVelRamp` + `arpRampBeats`, `arpHumanize`, and `arpLinkLanes`. The six
after `arpChance` arrived on 2026-07-30 and are appended, like everything else that round:
a choice parameter's list and an int's range are both load-bearing for sessions, so nothing
before them moved.

Two pieces of arp state are deliberately **not** parameters. Lane data and the twelve slots
live in the `arp` ValueTree beside the chord pads (they are arrays, not knobs). And the
chain's running state is transient: it starts stopped, because a session that reopens
already playing a progression is a session that surprises you.

`bpm` (40..240, default 120) is the newest, and is registered last on purpose: appending
leaves every existing parameter's automation index where the session left it. It is the
tempo anything timed in beats runs at when there is no transport to follow, which is every
moment in the standalone and every stopped transport in a DAW; a host that is *playing*
still wins. It replaced the arp's last-known-host-tempo fallback, which nothing in the
standalone could ever reach and nobody anywhere could change.

A growing set is **registered but no longer read**, kept only so a session (and any host
automation) saved with them loads without error: `surface`, `uiLayout`, `padChannel`,
`xyCCX` / `xyCCY` from the old five-tab arrangement, and `velocity`, `curve`,
`humanizeTime` from the controls this branch retired. Adding to that list is the standing
convention here — removing a parameter outright would shift automation in projects that
already exist. `latch` came back off it in 2026-07-30, which is the other reason to keep
dead parameters registered: a retired control is sometimes only resting.

The folding layout (which of the four sections are open, whether the knobs and the wheels
are, whether the pad cards are Big, and where each detached window was left) and the
instance's accent colour are **not** parameters: they change no note, and
exposing them to automation would only add ways to break a session. They live in
`KeysProcessor::LayoutState` and ride along in the session tree. The Mod and
Pitch wheels, knob positions, and the Markov Mood and
Start pickers are transient performance controls with no parameters (they don't
persist); Pitch glides back to centre over ~160 ms on release, and the wheels and
knobs move by relative drag only (no click-jump), Octavium's deliberate feel. The
editor binds controls
with attachments — except the two two-handle range sliders (velocity, strum), which take
no attachment (two values each) and are synced to their pair of params by hand.
A 30 Hz timer pushes derived
config into every note surface and the live chord into the pads. `getStateInformation` /
`setStateInformation` persist the APVTS via `okstudio::state`, plus the captured chord
pads as an extra state tree (notes, name, lock, the generator metadata a pad carries,
and the Markov `numeral` when it has one),
so the whole setup saves with the DAW session. Pads saved before the generator existed
load fine: the missing metadata reads back as -1, which means "hand-captured", and the
suggestion menu works the chord out from its notes instead.

## Editor

`KeysEditor` owns the controls, the knob row (the bottom band of the Controls section,
`knobRowH` 110, which is what makes each knob 60 px square), the playing surface, the
`ChordPads` rows, the `ChordGenMenu` and the update
button. It sets the shared `LookAndFeel` (retinted locally toward Octavium's neutral
grey), wires the playing surface and the pads to `KeysProcessor::baseVelocity01` (the
midpoint of the velocity range), pushes the surface's sounding notes into the pads
each timer tick, panics the surface on a channel change (so notes can't strand),
animates the pitch wheel home after release, and on construction fires
`okstudio::updater::checkAsync`; if a newer signed
release exists, a one-click "Update to vX.Y.Z" button appears. The wheels column shows
next to the playing surface, as in Octavium, unless the Keyboard bar's Wheels chip
folds it away.
