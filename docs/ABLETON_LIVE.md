# Using Keys in Ableton Live

Keys is an instrument that outputs MIDI. It goes on one track; the instrument it
drives goes on another, fed from the Keys track.

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

## Channels

If you want Keys to drive different instruments on different channels, set **MIDI
Ch** on Keys and filter by channel on the receiving tracks. Otherwise leave it at 1.

## Recording what you play

Arm the instrument track and record; the notes you click land as a normal MIDI clip.
Scale Lock, octave, and velocity all apply to what's recorded, because Keys sends the
already-resolved notes.

## Troubleshooting

- **"This VST3 plug-in could not be opened."** Usually the plugin files changed on
  disk while Live was open. Quit Live fully, relaunch, and if needed hold **Alt**
  while clicking **Rescan** in Options → Preferences → Plug-Ins, then re-add it.
- **No sound.** Check the instrument track's **Monitor** is **In** and its **MIDI
  From** points at the Keys track → Keys. Keys itself never makes sound; it only
  sends notes.
- **Wrong octave.** The Octave control transposes everything; also check the
  receiving instrument's own range.
