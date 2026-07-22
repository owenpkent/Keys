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
`../okstudio-juce-kit`; Keys only registers tools and owns the small timer that
fires delayed chord-pad releases. See `src/mcp/KeysMcp.h` and `.cpp`.

## Tools

| Tool | Purpose |
|------|---------|
| `get_state` | Snapshot the load-bearing controls, active arp pattern, pad page, and how many chord pads are loaded. Call this first. |
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
| `get_arp_pattern` | Read a pattern's six per-step lanes: the live lanes, or a stored slot (0..7, A-H). |
| `set_arp_pattern` | Write one or more lanes of a pattern: the live lanes, or a stored slot. |
| `recall_arp_pattern` | Make a stored pattern the active/live one. |
| `store_arp_pattern` | Snapshot the live lanes into the active pattern slot. |

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
4. `set_arp_pattern { "gate": [60, 80, 100, 100], "ratchet": [1, 1, 2, 1] }`: write a four-step feel into the live arp lanes, then `set_params { "values": { "arpOn": true } }` to hear it against whatever's held.
