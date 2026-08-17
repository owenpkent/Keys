# Using Keys in Ableton Live

## Which one do I use?

Keys ships as two products, and picking the wrong one is the single most common way to
end up frustrated. **The question that decides it is whether you want Live to record
what you play.**

| | Use **Keys** | Use **Keys Host** |
|---|---|---|
| Tracks | Two: Keys, and your instrument | One |
| Your instrument is | a normal Live device | loaded inside the plugin |
| Live records what you play | **Yes**, natively, on the grid | **No** - use Keys' own REC |
| Setup | routing, once, into a template | none |

**Want Live to record it? Use plain Keys.** The instrument sits on its own track as an
ordinary Live device, so you get the browser, presets, effects after it, freezing, all
of it - and, crucially, **the clip you record lands on the track that makes the sound**,
so it plays straight back with no further routing.

**Want one track and no routing at all? Use Keys Host**, and record with **REC** on its
Keyboard bar (see "Recording what you play"). Keys Host also drives Ableton's own
instruments on other tracks, and it is what the standalone build uses.

Note the tension, because it is real and it is not going away: Keys Host exists to avoid
routing, and Live's recording *requires* routing. You cannot have both from one product.
Adding a listener track to a Keys Host set gets you a recording, but the clip lands on a
track with no instrument on it - so you would then need a third routing to hear it back.
At that point plain Keys is simpler and strictly better.

The rest of this page assumes plain Keys unless it says otherwise.

## Routing

1. Create a MIDI track (call it **Keys**) and drop the **Keys** device on it.
2. Create a second MIDI track and load your instrument (a piano, a synth, a Drum
   Rack, anything).
3. On the instrument track:
   - **MIDI From** → the **Keys** track, and in the second chooser pick **Keys**.
   - **Monitor** → **In**.
4. Click the on-screen keys. The instrument track plays.

You do not need loopMIDI or any virtual port - the routing is internal to Live.

## Making one Keys track drive any instrument (recommended)

You do not have to rewire routing per song. Keep one **Keys** track loaded
permanently, then on each synth track set **MIDI From → Keys** and **Monitor → In**.
Now the clicked keys play whichever synth track is selected/armed - one keyboard, any
instrument, no re-patching. Switching instruments is just clicking a different track.

## Set it up once: save a template

Do the routing above once, then **File → Save Live Set As Template…** (or set it as
the default set in Options → Preferences → File/Folder → "Save Current Set as
Default"). Every new project then opens with the Keys track, the routing, and the
monitor settings already in place. Nothing to rewire, ever.

## Keeping Keys on screen while you work on other tracks

By default, selecting a different track makes the Keys window disappear. That is a Live
setting, not a Keys bug, and two switches fix it:

**Preferences (Ctrl+,) → Plug-Ins:**

- **Auto-Hide Plug-In Windows → OFF.** The manual: *"Using the Auto-Hide Plug-In Windows
  option, you can choose to have Live display only those plug-in windows belonging to the
  track that is currently selected."* That restriction is the symptom; off, Keys stays.
- **Multiple Plug-In Windows → ON**, so opening another plugin joins Keys on screen
  rather than replacing it.

**This matters more for Keys than for most plugins.** Auto-hide is built for effects you
set and forget - you open the compressor, adjust it, move on. Keys is *played*. Wanting
it on screen while a different track is selected is not an edge case here, it is the
normal way to use it, which is why this is worth setting once and forgetting.

The catch is Live's: that switch is **global**, so every plugin window then stays open and
a busy set fills the screen. Live has no per-plugin "pin" - it is a long-standing feature
request and it does not exist.

### Why some plugins seem to ignore auto-hide

Plugins that open their **own** desktop windows are never managed by Live's auto-hide,
because Live does not own those windows. You can watch both behaviours at once in Keys
Host: the hosted synth's GUI is a separate floating window and stays put, while Keys' own
window vanishes with the track selection.

Keys' **Detach** buttons work the same way. Every section (Controls, Arp, Chord Pads,
Keyboard) can be detached into a desktop window of its own, so detaching just the
**Keyboard** keeps the keys reachable without turning auto-hide off globally. That is
effectively the per-plugin pin Live lacks, for the part of Keys you actually play.

## The knob row and CC mappings persist too

The eight knobs above the keyboard send MIDI CCs down the same routing as the notes.
To tie a knob to a control on your instrument, use the **instrument's own MIDI
Learn** (most synths: right-click the knob → Learn, then move the Keys knob). That
mapping is stored in the instrument's plugin state, which Live saves inside the set -
so it comes back with the project. Assign once per project (or once in your
template) and it stays. Unlike an external controller app, there is no virtual MIDI
cable and nothing to reassign after a restart.

## Why Keys can't sit *before* an instrument on one track

The obvious wish is to drop Keys in front of a synth on the same track, the way
Ableton's **Arpeggiator** sits before an instrument. Ableton does not allow it, and it
is a Live limitation, not a Keys bug:

- Live's built-in MIDI effects (Arpeggiator, Chord, Scale, …) are **native devices
  baked into Live's engine** - not plugins, not Max for Live.
- Live reserves the pre-instrument MIDI-effect slot for **native devices and Max for
  Live devices only**. Third-party VST3/AU plugins are locked out of it.
- Building Keys as a VST3 MIDI effect (the **Keys FX** experiment) does not help: Live
  classifies it as an *audio* effect, so dropping it before a synth gives "insert audio
  effects after instruments." Other DAWs (Bitwig, Cubase, Reaper) allow VST3 MIDI
  effects before instruments; Live does not.

So the two-track routing above is the Ableton-correct approach. The standard way MIDI
tools ship for Live is exactly what Keys already is: an instrument that outputs MIDI,
routed between tracks.

### The Max for Live escape hatch (advanced, fiddly)

The only DIY way to put anything before an instrument in Live is **Max for Live**. A
small M4L device can wrap a VST (a `vst~` object hosts the plugin and forwards only its
MIDI) and pass the plugin's notes to an instrument on the same track - so Keys *could*
sit in front of a synth this way. Caveats, worst-first:

- The hosted plugin's floating window **disappears every time you click on Live**, so
  you would reopen the Keys window constantly. For a mouse-only workflow that is heavy
  friction.
- Requires **Ableton Live 12.0.2+** (not Live 11) and **Max for Live** (Suite edition
  or the M4L add-on).
- Stability varies per plugin; test before relying on it in a set.

Given the window-reopening friction, the permanent-Keys-track routing is the better
day-to-day setup. The wrapper is recorded here as an option, not a recommendation.

*Background for this section: Ableton's [Live MIDI Effect
Reference](https://www.ableton.com/en/manual/live-midi-effect-reference/) and [Max for
Live manual](https://www.ableton.com/en/manual/max-for-live/); the wrapper technique is
described at [AudioSwift, MIDI Effects Wrapper for VST/AU using Max for
Live](http://audioswiftapp.com/midi-effects-wrapper-for-vst-au-plugins-using-max-for-live/).*

## Keys Host: one window for VST3 instruments

Drop **Keys Host** on a MIDI track and open the **Instrument** chip on its Controls bar,
then **Load instrument...**: an in-window list of every installed VST3 (the same folders
Live's browser scans), filed into a collapsible folder per publisher, one click to open a
folder and one to load. (Keys Host used to carry a bar of its own for this, above the
keyboard; it folded into that chip 2026-08-02.) The synth's UI opens in its **own floating
window** above the keyboard - two windows, not one stack; the same chip's **Show/Hide
instrument GUI** controls it (the window's close button just hides it). Audio comes out
of the Keys Host track. The
instrument's complete state - including its own MIDI Learn mappings for the Keys
knobs - is saved inside the Live set. Dragging a `.vst3` file from Windows
Explorer onto the Keys Host window also works; dragging from **Live's own browser**
does not - Live's browser drags are internal and never reach a plugin's window
(true for every hosting plugin, not just Keys Host), which is exactly why the picker
brings the list inside instead.

**Ableton's own instruments (Operator, Wavetable, Simpler, Drum Rack…) cannot be
loaded inside Keys Host** - or inside any plugin host. They are not plugins; they
exist only inside Live. To play them from the same keyboard, use the same routing as
plain Keys: on the Ableton-instrument track set **MIDI From → Keys Host** (and
**Monitor → In**). Keys Host always sends the played notes out of its track, even
while it is also playing its hosted VST3 - so one Keys Host can drive its own synth
*and* Ableton devices on other tracks; arm/monitor per track decides who listens.

**Recording note.** Arming the Keys Host track and recording gets you an empty clip, for
the reason under "Recording what you play": Live records at the track input, and the notes
are made downstream of it. Press **REC** on the Keyboard bar instead.

A listener track ("MIDI From: Keys Host") *does* capture it, but think twice - the clip
lands on a track with no instrument, so playing it back needs a further routing back into
Keys Host. If you want Live to record and play it natively, that is what plain Keys is
for; see "Which one do I use?" at the top.

## Channels

If you want Keys to drive different instruments on different channels, set **MIDI
Ch** on Keys and filter by channel on the receiving tracks. Otherwise leave it at 1.

## Recording what you play

**Arming a track that has Keys on it and recording gets you nothing, and that is a Live
limit rather than a Keys bug.** Live records what arrives at a track's *input*. The notes
you click are made inside the plugin, downstream of that input, so there is nothing at
the input to record and the clip comes out empty. No plugin-side setting changes this, in
Keys or in any other MIDI-generating plugin.

The proof is one you can run in ten seconds without Keys at all: put Live's own
**Arpeggiator** on a track and record it. You get the plain chord you played, not the
arpeggio. Same tap point, same result.

This is also why Scaler, Cthulhu and every other chord/arp plugin ship exactly the two
answers below - route to a second track, or capture inside the plugin and drag the file
out. There is no third one hiding in Live's routing.

### The normal way: record the instrument track (plain Keys)

With the routing above already in place, **you are done** - recording is just recording.
Arm your instrument track, hit record, click the keys. The notes land as an ordinary MIDI
clip on the instrument's own track, in time, on the grid, and play straight back.

This is the answer for almost everything. The two-track setup is a *one-time* cost if you
save it into your template (see above): after that, every new project already has the
Keys track and the routing, and there is nothing to set up per song.

Scale Lock, octave and velocity all apply to what is recorded, because Keys sends
already-resolved notes. With either arp line lit (**A**, **B**), what lands is the
arpeggiated stream rather than the chord you clicked - the arp rewrites the note stream
on its way out of the plugin, so the clip holds what you heard, both lines at once if
both are running.

### REC: Keys records itself (for Keys Host, or one-track sets)

Press **REC** on the Keyboard bar, play, press **STOP**. Keys writes the take to
`Documents\OK Studio\Keys Takes` and the chip beside REC names it and how long it is.

What is captured is the stream that *leaves* Keys - arpeggiated where a line is running,
strummed where a pad strummed, on whichever channel each line sent it on. It is what you
heard, not what you clicked. The take carries the tempo Keys was running at, so the clip
lands on the grid; notes still ringing when you stop are given an end, so nothing hangs.

**Click the chip** to open the take: a picture of what was captured, its length, note
count and tempo, and three ways out.

- **Drag the roll** (or the chip) straight onto a Live track. One long drag.
- **Save MIDI as…** to put a copy anywhere, under a name that means something.
- **Show in Explorer**, and drag from there.

Best of all, once: add `Documents\OK Studio\Keys Takes` to Live's browser as a **Place**
(drag the folder into the browser's sidebar, or Add Folder). Every take afterwards
appears there and is a short drag *inside* Live - much the kindest gesture with one mouse.

The window is a view, not an editor. It draws the take from the same MIDI file that was
written, so what you see is what the file holds; trimming or rearranging it is a job for
Live's own piano roll once it is in.

Pressing REC again starts a fresh take. Nothing is lost by that: the previous one was
written to disk the moment you stopped.

### Channels, either way

Each arp line can name its own **Channel** (Global, or 1–16). That is what makes two lines
useful against a multitimbral rack: set A and B to different channels and one Keys drives
three sounds. Live records all of it into the one clip, on the channels the lines sent it on;
splitting it back out afterwards is a job for Live, not for Keys.

## Troubleshooting

- **"This VST3 plug-in could not be opened."** Usually the plugin files changed on
  disk while Live was open. Quit Live fully, relaunch, and if needed hold **Alt**
  while clicking **Rescan** in Options → Preferences → Plug-Ins, then re-add it.
- **No sound.** Check the instrument track's **Monitor** is **In** and its **MIDI
  From** points at the Keys track → Keys. Keys itself never makes sound; it only
  sends notes.
- **Keys disappears when I select another track.** Live's **Auto-Hide Plug-In Windows**,
  on by default. See "Keeping Keys on screen while you work on other tracks" above.
- **I armed the Keys track, recorded, and got an empty clip.** Expected, and it is a
  Live limit rather than a Keys bug - Live records at the track *input*, upstream of
  where Keys makes its notes. See "Recording what you play".
- **Wrong octave.** The Octave control transposes the keys you click, not the chord
  cards: those carry absolute notes, fixed by the generator's own Octave at the moment
  they were made. Also check the receiving instrument's own range.
