# Using Keys in Ableton Live

Keys is an instrument that outputs MIDI. It goes on one track; the instrument it
drives goes on another, fed from the Keys track.

**Keys Host** is the one-window variant: it hosts a (third-party) instrument VST3
*inside* itself, so keyboard and synth live on a single track with no routing. See
"Keys Host" below for how it fits with everything on this page.

## Routing

1. Create a MIDI track (call it **Keys**) and drop the **Keys** device on it.
2. Create a second MIDI track and load your instrument (a piano, a synth, a Drum
   Rack, anything).
3. On the instrument track:
   - **MIDI From** → the **Keys** track, and in the second chooser pick **Keys**.
   - **Monitor** → **In**.
4. Click the on-screen keys. The instrument track plays.

You do not need loopMIDI or any virtual port — the routing is internal to Live.

## Making one Keys track drive any instrument (recommended)

You do not have to rewire routing per song. Keep one **Keys** track loaded
permanently, then on each synth track set **MIDI From → Keys** and **Monitor → In**.
Now the clicked keys play whichever synth track is selected/armed — one keyboard, any
instrument, no re-patching. Switching instruments is just clicking a different track.

## Set it up once: save a template

Do the routing above once, then **File → Save Live Set As Template…** (or set it as
the default set in Options → Preferences → File/Folder → "Save Current Set as
Default"). Every new project then opens with the Keys track, the routing, and the
monitor settings already in place. Nothing to rewire, ever.

## The knob row and CC mappings persist too

The eight knobs above the keyboard send MIDI CCs down the same routing as the notes.
To tie a knob to a control on your instrument, use the **instrument's own MIDI
Learn** (most synths: right-click the knob → Learn, then move the Keys knob). That
mapping is stored in the instrument's plugin state, which Live saves inside the set —
so it comes back with the project. Assign once per project (or once in your
template) and it stays. Unlike an external controller app, there is no virtual MIDI
cable and nothing to reassign after a restart.

## Why Keys can't sit *before* an instrument on one track

The obvious wish is to drop Keys in front of a synth on the same track, the way
Ableton's **Arpeggiator** sits before an instrument. Ableton does not allow it, and it
is a Live limitation, not a Keys bug:

- Live's built-in MIDI effects (Arpeggiator, Chord, Scale, …) are **native devices
  baked into Live's engine** — not plugins, not Max for Live.
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
MIDI) and pass the plugin's notes to an instrument on the same track — so Keys *could*
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

Drop **Keys Host** on a MIDI track and click **Load Instrument…**: an in-window list
of every installed VST3 (the same folders Live's browser scans), filed into a
collapsible folder per publisher, one click to open a folder and one to load. The synth's UI opens in its **own floating window**
above the keyboard — two windows, not one stack; the top bar's **Show/Hide
Instrument** controls it (the window's close button just hides it). Audio comes out
of the Keys Host track. The
instrument's complete state — including its own MIDI Learn mappings for the Keys
knobs — is saved inside the Live set. Dragging a `.vst3` file from Windows
Explorer onto the Keys Host window also works; dragging from **Live's own browser**
does not — Live's browser drags are internal and never reach a plugin's window
(true for every hosting plugin, not just Keys Host), which is exactly why the picker
brings the list inside instead.

**Ableton's own instruments (Operator, Wavetable, Simpler, Drum Rack…) cannot be
loaded inside Keys Host** — or inside any plugin host. They are not plugins; they
exist only inside Live. To play them from the same keyboard, use the same routing as
plain Keys: on the Ableton-instrument track set **MIDI From → Keys Host** (and
**Monitor → In**). Keys Host always sends the played notes out of its track, even
while it is also playing its hosted VST3 — so one Keys Host can drive its own synth
*and* Ableton devices on other tracks; arm/monitor per track decides who listens.

Recording note: what you play into the *hosted* instrument stays inside the plugin,
so Live doesn't capture it as a MIDI clip on the Keys Host track itself. To record,
use a listener track ("MIDI From: Keys Host"), exactly like the routing above.

## Channels

If you want Keys to drive different instruments on different channels, set **MIDI
Ch** on Keys and filter by channel on the receiving tracks. Otherwise leave it at 1.

## Recording what you play

Arm the instrument track and record; the notes you click land as a normal MIDI clip.
Scale Lock, octave, and velocity all apply to what's recorded, because Keys sends the
already-resolved notes. With any of the Arp section's line switches (**A**, **B**, **C**) lit,
what lands is the arpeggiated stream rather than the chord you clicked: the arp rewrites the
note stream on its way out of the plugin, so the clip holds what you heard — both lines
at once, if both are running.

Each line can also name its own **Channel** (Global, or 1–16). That is what makes two lines
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
- **Wrong octave.** The Octave control transposes the keys you click, not the chord
  cards: those carry absolute notes, fixed by the generator's own Octave at the moment
  they were made. Also check the receiving instrument's own range.
