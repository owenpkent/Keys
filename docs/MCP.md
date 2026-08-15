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
| `get_state` | Snapshot the load-bearing controls, whether the arp is reading its step lanes (`arpPattern`), the active arp pattern, which slot the chain is playing (`arpChainSlot`, -1 when it is not running), pad page, and how many chord pads are loaded. Also `arpLines`, an entry per arpeggiator line (see below) with its on/off, active pattern, held chord, launched slot and whether it is chaining. Call this first. |
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
| `get_arp_pattern` | Read a pattern's ten per-step lanes, its rhythm dividers and harmony mode: the live lanes, or a stored slot (0..11). |
| `set_arp_pattern` | Write one or more lanes of a pattern, and/or its rhythm dividers and harmony mode: the live lanes, or a stored slot. Lane names are `note`, `octave`, `velocity`, `gate`, `ratchet`, `probability`, and — since 2026-07-30 — `transpose` (scale degrees), `late` (percent of a step), `harmony` (chord tones above) and `chord` (0 = off, 1..12 = play that arp slot's stored chord on this step). Three more since 2026-08-14: `rand` (−8..+8, how far this step's note selection may stray and which way), `mute` (0/1, silences a step without touching what it holds) and `chain` (0 always, 1 only if the step before it sounded, 2 only if it did not). Note also takes 9..12 now — Prev, Highest, Lowest, Random — which ask the held chord a question instead of counting into it. |
| `recall_arp_pattern` | Make a stored slot's lanes the active/live ones. Not the same as clicking the slot in the editor: that *launches* it, which also applies the shape and rate the slot remembers and holds its chord. No tool here reaches a slot's chord, shape or rate. |
| `store_arp_pattern` | Snapshot the live lanes into the active pattern slot. |
| `apply_euclid` | Write a Euclidean rhythm (Bjorklund's algorithm) into the active pattern's probability lane: 100 on a hit, 0 on a rest, and set that lane's length to `steps`. |

### The two arpeggiator lines

Keys runs two arpeggiators (`docs/ARP_DESIGN.md`). All four arp tools take an optional
**`line`**, 0 or 1 — A or B — and **default to 0**, the arpeggiator Keys has always had. Every
script written before the lines existed therefore still drives the line it was written for,
unchanged.

There were three until 2026-08-02, and **line C's parameters are still registered**: `arp3On`,
`arp3Rate` and the rest still appear in `list_params` and still accept a write, because dropping
them from the layout would break every saved session. Nothing reaches them — `arpLineOn` answers
false above the UI's count, so line C has no engine running, no chip and no row. Writing an
`arp3*` id is accepted and does nothing audible. Passing `"line": 2` is clamped to B.

Each line owns its own live lanes, its own twelve slots, its own held chord and its own chain,
so `slot` is read *within* a line: `{ "line": 1, "slot": 3 }` is B's fourth slot, a different
place from A's. `set_arp_pattern` and `get_arp_pattern` echo the `line` they acted on.

Two arp parameters are **not** per line, because they are about both of them together:
`bpm` (the tempo they run at with no transport to follow) and `arpQuantize` (Launch Quantize -
Off, or the boundary a chord card, a slot launch or a drag onto a line waits for before it
lands). Setting `arpQuantize` from a script is worth knowing about: with it on, a
`press_chord_pad` that feeds a line will not sound until the next boundary.

The parameters follow the same rule. Line A registers under the ids it always had — `arpOn`,
`arpRate`, `arpSwing` — and B repeats that whole list as `arp2*`: `arp2On`, `arp2Rate`,
`arp2Direction`, and so on. Five ids are newer than the original arp and worth knowing:
`arpKeys` (does this line arpeggiate what you play, or only the chords handed to it),
`arpChannel` (Global, or 1-16), `arpOctShift` (-3..+3, transposes the whole run; **not**
`arpOctaves`, which stacks copies upward), `arpVelTrim` (-100..+100, this line's level as a
velocity trim around "as played", with a squared response so half travel sounds about half as
loud; it replaced `arpVolume` on screen on 2026-08-02 — the old id still exists but every load
folds it into `arpVelTrim` and resets it to 100, so scripts should write the new one), and
`arpHumanVel` (0..100, the velocity half of Humanize; `arpHumanize` is the timing half alone
since the same day).

**A line that is off still takes chords in.** Handing a chord to a line that is not running
makes no sound and is not lost: the engine holds it silently, and setting that line's `On`
starts it arpeggiating what it is already holding. This changed on 2026-08-02 - the chord used
to sustain like a pad and the engine never saw it - so a script that switched a line on and then
fed it a chord can now do the two in either order.

A polyrhythm from a cold start is two `set_params` calls and a chord:

```
set_params { "values": { "arpOn": true,  "arpRate": "1/8" } }
set_params { "values": { "arp2On": true, "arp2Rate": "1/8", "arp2Tuplet": "Triplet" } }
play_notes { "notes": [60, 64, 67], "durationMs": 4000 }
```

### Euclidean rhythms, rhythm dividers and subharmonic harmony

Three generative additions (2026-08-14), none of them a new parameter - they live in
`rhythmDivs` / `harmonyMode` on `get_arp_pattern` / `set_arp_pattern`, and the `apply_euclid`
tool. All are per line and per slot, same as the lanes.

`apply_euclid { "hits": 3, "steps": 8 }` writes the tresillo (`x..x..x.`) into the probability
lane and sets its length to 8. `rotation` (default 0) walks the pattern's start point around
the circle. Only the probability lane has a hit/rest mapping that means anything, so this is
the one lane it writes.

`rhythmDivs` is up to four integers, 1..16, 0 = off (all four default off - the pattern behaves
exactly as it always has). With any enabled, a step boundary fires only if it is a multiple of
*at least one* of them - a Subharmonicon-style OR of clocks, not a shared modulus, so
`{ "rhythmDivs": [3, 4] }` fires on steps 0, 3, 4, 6, 8, 9, 12... A suppressed boundary plays
nothing and does not advance the lanes; a firing one advances them by one step regardless of
how many raw steps it skipped, so a pattern under a divider still reads step to step in order
rather than leaving silent gaps in the middle of it.

`harmonyMode` switches what the Harmony lane's second voice is: `0` (default) is today's chord
tone above the played note; `1` is subharmonic, one voice at the undertone series below it
(f/2 down to f/8, quantized to 12-TET) - a deliberately non-diatonic voicing, best heard with
Scale Lock off. A voice that would clamp onto the note it is harmonizing (very low notes running
out of MIDI range below 0) is dropped rather than folded back on top of it.

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

`arpPattern` is not optional there. The step lanes are only read when it is on (it is the
Shape menu's "Pattern" entry); with it off the arp runs as a plain shape and lanes you
have written sit silent. `get_state` reports it for exactly this reason. It is per line like
everything else, so the same call against line B needs `arp2Pattern`.

5. `set_params { "values": { "arp2On": true, "arp2Rate": "1/4" } }`: bring line B in
   underneath at half the speed. Both lines chew on the same held chord, because `arpKeys`
   and `arp2Keys` both default to on.
