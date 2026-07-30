# MCP: letting Claude drive Keys

Keys embeds an MCP (Model Context Protocol) server, so Claude Code (or any other
local MCP client) can read and drive it directly: set the scale, write a chord pad,
play a phrase, edit an arp pattern, all as tool calls instead of mouse clicks. This
does not change Keys' own accessibility contract (nothing above requires it); it's an
additional way in, for when you'd rather describe what you want than click it.

## Architecture

```
Claude Code  ->  keys-mcp.exe (stdio shim)  ->  discovery file in the OK Studio
app-data mcp directory  ->  loopback TCP (127.0.0.1, OS-assigned port)  ->  the
in-plugin MCP server (okstudio::mcp::Server, embedded in KeysProcessor)  ->  every
tool handler runs on the message thread, calling straight into KeysProcessor/APVTS
the same way the editor does.
```

The transport (`okstudio::mcp::Server`, `okstudio/Mcp.h`) lives in
`../okstudio-juce-kit`; Keys only registers tools and owns the small timer that emits
scheduled notes and fires delayed chord-pad releases. See `src/mcp/KeysMcp.h` and `.cpp`.

## Tools

| Tool | Purpose |
|------|---------|
| `get_state` | Snapshot the load-bearing controls, whether the arp is reading its step lanes (`arpPattern`), the active arp pattern, pad page, and how many chord pads are loaded. Call this first. |
| `list_params` | Every parameter Keys exposes, with its current value and legal range/choices. |
| `set_params` | Set one or more parameters at once, by id. |
| `play_notes` | Play one or more notes now, release them after a duration. |
| `play_sequence` | Schedule a whole phrase (an array of timed steps) in one call: the "write a melody" tool. |
| `all_notes_off` | Stop everything, gently, same as the on-screen All Off button. |
| `get_chord_pads` | List every chord pad that currently holds a chord. |
| `set_chord_pad` | Write a chord into a pad slot by hand. |
| `clear_chord_pad` | Empty a pad slot. |
| `press_chord_pad` | Fire a pad now, optionally auto-releasing after a duration. |
| `release_chord_pad` | Stop a pad (unless Sustain is holding it). |
| `get_arp_pattern` | Read a pattern's six per-step lanes: the live lanes, or a stored slot (0..11). |
| `set_arp_pattern` | Write one or more lanes of a pattern: the live lanes, or a stored slot. |
| `recall_arp_pattern` | Make a stored slot's lanes the active/live ones. Not the same as clicking the slot in the editor: that *launches* it, which also applies the shape and rate the slot remembers and holds its chord. No tool here reaches a slot's chord, shape or rate. |
| `store_arp_pattern` | Snapshot the live lanes into the active pattern slot. |
| `chance_state` | Read the whole Chance module back: every knob, both mode names, the seed, and whether a phrase has been captured or anything learned. |
| `chance_set` | Set any subset of Chance's knobs in one call, on short names (the parameter ids minus the `chance` prefix). Every name is validated before anything is applied, so a typo cannot leave a half-installed sound. Note that turning `on` true does **not** also turn the arp on: Chance has no clock of its own, so send `set_params { "values": { "arpOn": true } }` too or it generates in silence. (The Chance section's On button does cascade; this tool deliberately does not second-guess a caller.) |
| `chance_generate` | A new seed, hence a new phrase. Returns the seed. |
| `chance_freeze` | Hand the phrase Chance just played to an arp slot (0..11, default the active one) as an ordinary editable pattern. Reports an explicit failure if nothing has been generated yet, rather than a silent success. |
| `chance_learned` | Read the twelve learned pitch-class weights behind Learn, or clear them. |

## How scheduled notes are timed

`play_notes` sounds its notes immediately and schedules only the release. `play_sequence`
schedules every event against a single base timestamp taken when the call arrives, so the
phrase's internal timing is exact: the start of the whole phrase can be late by up to one
poll (5ms), but that lateness does not accumulate from step to step.

Both are dispatched by `KeysMcp`'s timer on the message thread, deliberately, not by
timestamping messages into the audio path. `juce::MidiMessageCollector` cannot hold a
future message: it empties its queue into the current block on every callback and clamps
each event into that block, so a delayed note-off arrives alongside its own note-on and
the note never sounds. Anything that must happen later has to be held and emitted at real
time. If you add a tool that plays notes, follow the same route.

This also means timing is message-thread timing, not sample-accurate. It is well inside
what a sparse phrase needs; it is not a sequencer, and a dense pattern that has to lock to
the host grid belongs in the arp lanes or in Contour, not here.

Steps may be given in any order. The queue sorts by time, and at equal times a note-off
is emitted before a note-on, so a note repeated back-to-back releases before it
re-attacks instead of the new attack being cut by the old release.

`all_notes_off` discards anything still queued, so it stops a phrase mid-flight.

## Setting it up in Claude Code

```
claude mcp add keys -- <path-to>\keys-mcp.exe
```

Build `keys-mcp` alongside the plugin (`KEYS_BUILD_MCP_SHIM`, on by default); a
local build puts it at `build\Release\keys-mcp.exe`. Point `claude mcp add` at
wherever your build (or the installer) put it.

## Multiple instances

One `keys-mcp` shim serves Keys and Keys Host alike: it has no product filter. If more than one instance is loaded (several tracks, several DAW windows),
the shim connects to whichever advertised itself most recently; pass `--port=N` to
pin it to a specific instance's port instead (read the port from that instance's
discovery file if you need to find it).

## Security

The server only binds `127.0.0.1` and never authenticates a connection: any local
process that can reach the loopback port can call these tools. That is an accepted
trade-off for a local creative tool, not a hardened remote API. Don't expose the
port beyond loopback.

## What to try

A short session once `keys mcp` is connected:

1. `set_params { "values": { "scale": "Dorian", "root": "D" } }`: set the key.
2. `set_chord_pad { "slot": 0, "notes": [50, 53, 57, 60], "name": "Dm7" }`: write a chord to the first pad.
3. `press_chord_pad { "slot": 0, "durationMs": 1500 }`: hear it.
4. `set_arp_pattern { "gate": [60, 80, 100, 100], "ratchet": [1, 1, 2, 1] }`: write a four-step feel into the live arp lanes, then `set_params { "values": { "arpOn": true, "arpPattern": true } }` to hear it against whatever's held.
5. `set_params { "values": { "arpOn": true } }` then `chance_set { "values": { "on": true, "density": 100, "dejaVu": 50, "loopLen": "8" } }`: a locked eight-step generated loop. Hold something (`play_notes { "notes": [50, 53, 57, 60], "durationMs": 8000 }`) and `chance_freeze {}` turns what you just heard into an editable pattern in the active slot.

Driving Chance this way is worth knowing about beyond convenience: it is how two bugs in it
were found that the unit tests could not reach, one needing a real session save racing the
audio thread and the other a process restart. A bridge that can hold notes, read state back
and restart the host is a test harness for everything that happens *outside* an engine.

`arpPattern` is not optional there. The step lanes are only read when it is on (it is the
Shape menu's "Pattern" entry); with it off the arp runs as a plain shape and lanes you
have written sit silent. `get_state` reports it for exactly this reason.
