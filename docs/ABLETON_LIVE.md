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
