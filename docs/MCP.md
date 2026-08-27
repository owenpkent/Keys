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
| `press_chord_pad` | Fire a pad now, optionally auto-releasing after a duration. **A pad cannot feed an arpeggiator line**; see "Getting a chord into a line" below. |
| `release_chord_pad` | Stop a pad (unless Sustain is holding it). |
| `hold_arp_chord` | **Hand a chord to an arpeggiator line and hold it**, from `notes` or a `padSlot`, same as dropping a chord card on that line. The hold does not expire. This, not `press_chord_pad`, is how a script feeds a running arp. |
| `release_arp_chord` | Let go of a line's held chord, or every line with `allLines`. |
| `get_arp_pattern` | Read a pattern's ten per-step lanes, its rhythm dividers and harmony mode: the live lanes, or a stored slot (0..11). |
| `set_arp_pattern` | Write one or more lanes of a pattern, and/or its rhythm dividers and harmony mode: the live lanes, or a stored slot. Lane names are `note`, `octave`, `velocity`, `gate`, `ratchet`, `probability`, and — since 2026-07-30 — `transpose` (scale degrees), `late` (percent of a step), `harmony` (chord tones above) and `chord` (0 = off, 1..12 = play that arp slot's stored chord on this step). Three more since 2026-08-14: `rand` (−8..+8, how far this step's note selection may stray and which way), `mute` (0/1, silences a step without touching what it holds) and `chain` (0 always, 1 only if the step before it sounded, 2 only if it did not). Note also takes 9..12 now — Prev, Highest, Lowest, Random — which ask the held chord a question instead of counting into it. |
| `recall_arp_pattern` | Make a stored slot's lanes the active/live ones. Not the same as clicking the slot in the editor: that *launches* it, which also applies the shape and rate the slot remembers and holds its chord. No tool here reaches a slot's chord, shape or rate. |
| `store_arp_pattern` | Snapshot the live lanes into the active pattern slot. |
| `apply_euclid` | Write a Euclidean rhythm (Bjorklund's algorithm) into the active pattern's probability lane: 100 on a hit, 0 on a rest, and set that lane's length to `steps`. |

### The four arpeggiator lines

Keys runs four arpeggiators (`docs/ARP_DESIGN.md`). Every arp tool takes an optional
**`line`**, 0 through 3 - A, B, C or D - and **default to 0**, the arpeggiator Keys has always
had. Every script written before the lines existed therefore still drives the line it was
written for, unchanged.

There were only two on screen between 2026-08-02 and 2026-08-19, and line C's parameters
(`arp3On`, `arp3Rate` and the rest) stayed registered through that whole stretch, because
dropping a parameter from the layout breaks every saved session even while nothing on screen
reaches it. **All four lines are on screen from 2026-08-19**: `numArpLines` and `uiArpLines` are
both 4 now, so line C has an engine running, a letter switch and a macro card again, and a new
line D (`arp4*`, appended) joined it. Passing `"line": 2` reaches C, `"line": 3` reaches D;
older scripts that only ever used 0 or 1 are unaffected.

Each line owns its own live lanes, its own twelve slots, its own held chord and its own chain,
so `slot` is read *within* a line: `{ "line": 1, "slot": 3 }` is B's fourth slot, a different
place from A's. `set_arp_pattern` and `get_arp_pattern` echo the `line` they acted on.

Two arp parameters are **not** per line, because they are about all four lines together:
`bpm` (the tempo they run at with no transport to follow) and `arpQuantize` (Launch Quantize -
Off, or the boundary a chord card, a slot launch or a drag onto a line waits for before it
lands). Setting `arpQuantize` from a script is worth knowing about: with it on, a chord
handed to a line will not sound until the next boundary. (`press_chord_pad` is not
such a hand-off, whatever this file used to say here: see "Getting a chord into a
line" below.)

The parameters follow the same rule. Line A registers under the ids it always had — `arpOn`,
`arpRate`, `arpSwing` - and B, C and D repeat that whole list as `arp2*`, `arp3*` and `arp4*`:
`arp2On`, `arp2Rate`, `arp2Direction`, and so on. Five ids are newer than the original arp and
worth knowing: `arpKeys` (does this line arpeggiate what you play, or only the chords handed to
it), `arpChannel` (Global, or 1-16), `arpOctShift` (-3..+3, transposes the whole run; **not**
`arpOctaves`, which stacks copies upward), `arpVelLevel` (0..127, this line's *typical* velocity,
not a ceiling, whatever velocity the chord arrived with, and 0 mutes the line), and
`arpHumanVel` (0..127, how far either side of that level a hit may land - the velocity half of
Humanize, in the same units, opening equally louder and quieter since 2026-08-19; `arpHumanize`
is the timing half alone since 2026-08-02, and still only ever lands late relative to the grid,
never early).

Two more arrived on 2026-08-18, both defaulting to 0, which is the engine exactly as it was:
`arpMutate` (0..100 - how far the line explores *other notes of the chord it is holding*. To 50
it can only ever reach a note the chord already contains; past 50 a stray may land a scale
degree or two outside the chord, and past 75 a growing share of those strays turn chromatic, so
a value up near 100 can genuinely go out of key - Owen: "higher values can go out of scale") and
`arpMutateLock` (0..100 - how long it keeps what it finds, in-chord variations and out-of-key
strays alike: 0 redraws the variation every pass, 100 locks the first one for good). They took
the CHANCE knob's place on the macro cards; `arpChance` itself is unchanged and still there.

Four more arrived on 2026-08-19: `arpHarm1` / `arpHarm1Chance` / `arpHarm2` / `arpHarm2Chance`
(and their `arp2*` / `arp3*` / `arp4*` twins) - each line's two fixed harmony voices, an
interval choice from BigSky's own shimmer list (Off by default) with a 0..100 chance of firing
per step (default 100). Each voice copies whatever note the step resolved, chord-lane steps and
Mutate's strays included, transposed by its own interval in semitones - chromatic, not scale
degrees, which is what tells it apart from the Harmony lane's chord-tone counting.

**A lane's shape is not a parameter and `set_params` cannot reach it.** Length, clock divider,
on/off, loop window and direction are lane data in the arp tree - use `get_arp_pattern` and
`set_arp_pattern`. Alongside `lengths` and `clockDivs`, those two now carry `on` (true/false),
`loopFrom` / `reset` joined the lane list on 2026-08-18 (1 restarts the shape's walk on that step) and the
`note` lane's range grew with it: -1 rests, 0 follows the line's Shape, 1..8 are fixed chord
entries, 9..12 are Prev/Hi/Low/Random, and **13..20 are per-step shapes** (up, down, up/down,
down/up, up & down, down & up, fingered bottom, fingered top, in that order). A script writing a
note lane may now use any of them.

`loopTo` (0-based and inclusive; `loopTo` past the lane's end means its end, and is
reported clamped to the length so a captured window reads as the window you see) and `dir`
(0 Up, 1 Down, 2 Up alt, 3 Down alt - the alt pair goes out and back without playing the turning
points twice). All four are optional maps of lane name to value, and all four read back as the
old behaviour when absent, so a pattern captured by an older script still applies cleanly.

**Two retired velocity ids are still registered and read by nothing**: `arpVolume`, which
`arpVelTrim` replaced on 2026-08-02, and `arpVelTrim` itself, which `arpVelLevel` replaced on
2026-08-18 when Vel stopped being a bipolar percentage trim around "as played" and became MIDI
velocity. Every load folds each into its successor. Scripts should write `arpVelLevel`; writing
either of the old two changes nothing you can hear.

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

### Getting a chord into a line (a pad will not)

**`hold_arp_chord` is the route.** Give it `notes` or a `padSlot` and a `line`, and
that line holds the chord and keeps arpeggiating it until something replaces it or
`release_arp_chord` lets go, whether or not its Latch is on:

```
set_params      { "values": { "arpOn": true, "arpPattern": true,
                              "arp2On": true, "arp2Pattern": true } }
hold_arp_chord  { "notes": [45, 48, 52, 55], "name": "Am7", "line": 0 }
hold_arp_chord  { "padSlot": 0, "line": 1 }
```

**Note the second line's own `arp2On`.** B, C and D default to off, and a line that is
off still takes a chord in, silently and by design - so handing one to line 1 without
switching it on is a call that returns success and makes no sound, which is the exact
trap this section exists to close. `hold_arp_chord` reports `lineOn` for that reason:
if it comes back `false`, the chord landed and the line is not running.

It calls the same `holdArpChord` the editor calls when you drop a chord card on a
line, so a script and a drag now reach the arp the same way. **It was added on
2026-08-26**, and before it existed the only route was `play_notes` held open,
which is not a hold: it expires, and re-arming is another call. `play_notes` still
works and is still right for auditioning a phrase.

**The trap this closed** is worth knowing, because it cost an afternoon and it is
the shape of thing that will recur: getting it wrong looked exactly like getting it
right. Every call returned success, every value read back correct, and nothing
sounded.

`press_chord_pad` calls `pressChordPad`, which fires through `fireChord` with
`asChord` **true**. On the track output that takes the queue a listening arp line
cannot lift, deliberately: a pad is a chord you are *playing*, not the input to a
machine. So a pad press never reaches a line, no matter what `arpKeys` says.

`play_notes` calls `noteOn` with `asChord` false and `dest` 0, and a line with
`arpKeys` on does lift those. Hold them (a long `durationMs`, or `arpLatch` on to
keep them after the release) and the line arpeggiates:

```
set_params    { "values": { "arpOn": true, "arpPattern": true, "arpLatch": true } }
play_notes    { "notes": [45, 48, 52, 55], "durationMs": 60000 }
```

Three things about what a line reports, which together answer "where are those
notes coming from" - a question this API could not answer at all until 2026-08-26:

- **`heldChord` is only the chord that was *handed* to the line.** A line audibly
  arpeggiating played notes reads as holding nothing, and that is correct rather
  than a fault. Only `hold_arp_chord`, a chord card, or a slot launch fills it.
- **`heldNotes` and `sequence` are what the line is actually sounding**, whatever
  the source. `heldNotes` counts the notes the engine holds; `sequence` is the
  pitches the Note lane's indices `1..n` name, in the order the Shape and octave
  stack put them. So a Note lane of `[4,3,2,1]` against a `sequence` of
  `[57,60,64,67]` is a descending arpeggio, and **a line with an empty `heldChord`
  and a non-empty `sequence` is arpeggiating notes that arrived some other way** -
  which is the state to look for when a line plays and nothing explains why.
- **`hold_arp_chord` can honestly report an empty hold**, for one call, when Launch
  Quantize is on: the whole gesture waits for the next boundary. Its reply carries
  `waitingForQuantize` so the two cases are told apart.

The usual "some other way" is the **track's own MIDI input**, which `arpKeys` feeds
straight to the arp. In Ableton a track set to `MIDI From: All Ins / All Channels`
hands every stray note in the session to a generative device, and none of it shows
up as a held chord. That cost an afternoon: an instance played continuously while
`heldChord` was blank, the chord lane was `0`, and there was no chain and no
launched slot. Nothing survived the panic, because fresh note-ons arrived straight
after it, and the only way to find it was muting lines one at a time and asking a
human what stopped. `arpKeys` off, or `MIDI From: None`, shuts that door;
`sequence` is what makes it visible without either.

**Chord pads go out on the global MIDI Channel control** (the `channel` parameter), like
anything else Keys plays: `fireChord` passes channel 0, which means "follow that
control". There is a `padChannel` parameter in the tree and it is **not** the answer to
silent pads - it is retained for session compatibility only and is read by nothing, so
setting it changes nothing at all.

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

## Which instance am I talking to? (2026-08-26)

**Nothing in the API answers this**, and it is the first thing to establish before
writing anything. `get_state` carries no track name, device name or index, and
`list_params` cannot separate two instances sitting at the same settings. Four
instances in one Live set read as interchangeable.

That matters because the shim picks its instance by *recency*, not by anything you
chose, so a script can build an entire patch into a Keys on a track nobody is
listening to and be told it succeeded at every step. Verification over MCP proves
a parameter took a value. It proves nothing about where the notes go.

**Identify by ear, positionally.** Fire the same chord on each reachable port in a
known order, a few seconds on and a longer gap between, printing the position as
it goes, then ask which **position** sounded. Position is unambiguous where pitch
and pattern are not, and it needs no state changes on instances holding work.

An answer of "none of them" is the useful one: the instance you want is not in the
set you can reach, and no further parameter checking will change that. Make it
re-register (delete the Keys device and drag a fresh one in; or reopen the set,
which re-registers every instance and keeps their state), and watch the discovery
directory for a port that is not already known.

### An instance can be alive, bound, and deaf (fixed 2026-08-26)

Read this before diagnosing anything that looks like a hung plugin, because the
symptom is maximally misleading and the cause was two layers below where it looked.

**The symptom.** `%APPDATA%\OK Studio\mcp` held **six** files for one Live process.
Four answered tool calls. Two accepted a TCP connection and never replied, and the
newest of those was the one the shim chose, so a whole Claude Code session came up
with **no Keys tools at all** while Keys was running fine. Worse, a script driving
one of the four healthy instances built an entire patch, verified every value, and
produced silence, because the instance it could reach was not the one routed to a
synth. Every call returned success throughout.

**The cause, and it was in the kit's transport.** `Server::run()` accepted a
connection and then called `serve()` **on the listener thread**, and `serve()` blocks
in a 1-byte read until that client disconnects. The shim holds a long-lived
connection and goes idle between tool calls, by design. So **one idle shim owned the
server forever**: every later connect completed into the OS backlog and was never
accepted. `netstat` showed it plainly, and is the fastest way to recognise it again:

```
127.0.0.1:55286  LISTENING                      8616   <- the plugin
127.0.0.1:55286  <- 127.0.0.1:60496 ESTABLISHED 8616
127.0.0.1:60496  -> 127.0.0.1:55286 ESTABLISHED 47212  <- an abandoned keys-mcp.exe
```

A deaf port has an ESTABLISHED peer. A healthy one shows `LISTENING` alone.

**The wrong theory, recorded because it was convincing.** The first explanation was
stale discovery files whose ports had been reused by unrelated sockets, on the
reasoning that all six instances share one message thread, so a blocked thread would
have silenced all six rather than two. Both halves were wrong. The files were live:
the plugin deletes its own file on destruction, and one of the deaf files vanished
the instant its device was deleted from its track. And **each instance runs its own
`Server` thread and its own socket** - only the final dispatch of a tool body is
marshalled onto the shared message thread - so a per-instance failure is exactly what
this could be, and was.

**The fix, in `okstudio-juce-kit`.** One thread per connected client
(`Server::ClientConnection`), so an idle client parks only its own thread. Serving
concurrently is safe: the rendezvous state in `toolsCall` is heap-allocated per
request, `tools` is read-only after `start()`, and every tool body is still
marshalled onto the message thread, so handlers stay serialized against each other
and against the editor exactly as before. Connections are capped, and at the cap the
**oldest** is dropped rather than the newest refused, since the shim reconnects
statelessly and on demand.

**The shim gained a fallback too**, because "accepts a connection" was never evidence
of "will answer" and now says so in code: a request that blows `--timeout-ms`
quarantines that port, closes the socket and reconnects, preferring another instance.
The quarantine clears if every candidate ends up in it, so one slow instance cannot
leave the session with nothing. Closing rather than merely dropping the socket is
load-bearing: the reader thread is parked in a blocking read that a deaf peer will
never wake.

**Regression tests exist now, and they are the first socket-level coverage the kit
has had.** `tests/KitTests.cpp` gained `okstudio::mcp::Server transport`, whose
central case is an idle client followed by a second one that must still be answered;
it fails on the old serial loop. `tests/mcp_shim_reconnect.py` gained scenario 6, a
deaf peer advertised newer than a healthy one, which must be abandoned for it. That
this bug survived so long is a straightforward consequence of where the tests were:
every MCP test called `handleLine` directly, with "no socket, no start()", and the
entire failure lived in the transport those tests skipped.

**What still holds regardless:**

- **A bound port is not evidence a Keys will answer, and neither is a discovery
  file.** Only a reply to a tool call is.
- **Probe every port in the directory** when tools are missing or hanging.
  `--port=N` pins the shim to one.
- `tests/mcp_shim_reconnect.py` needs the discovery directory to itself, so it now
  isolates into a temp dir via **`OKSTUDIO_MCP_DIR`**, which `Server::discoveryDir()`
  honours. Before that it shared the real directory with whatever the developer had
  open and failed every scenario against a live Ableton session, including "nothing
  running at startup".

## Restarting Keys does not break the bridge (2026-08-18)

**This is the one thing to know about the shim.** Claude Code launches `keys-mcp` once,
at the start of a session, and keeps that one process for hours. `run.py` closes and
relaunches Keys on every build, and the in-plugin server takes a **new OS-assigned port
each time**. So the socket underneath the shim dies constantly, and the shim has to
survive it.

It does now. A tool call arriving while it is disconnected re-reads the discovery
directory and connects to whatever is running at that moment - the reconnect costs
milliseconds and needs no handshake, because the server is stateless per request.

**Before this it hung.** The shim connected once and then wrote into a dead socket
forever, so a tool call got *no response at all* and the client sat on its idle timeout
 - 30 minutes of looking exactly like a slow tool. If you ever see that again, it is not
the plugin: check `%APPDATA%\OK Studio\mcp` for the live instance's port and talk to it
directly to tell the two apart.

Three more failure modes are closed with it, all of which used to be silence:

- **Nothing running at all** - a call answers with a JSON-RPC error saying so, rather
  than hanging. The shim also no longer exits when it finds no instance at startup; it
  serves errors and connects when Keys appears, since the client only ever launches it
  once and an exit would leave the session with no tools.
- **A call in flight when Keys closes** - answered with an error. A write into a socket
  whose peer has already gone can succeed, so "it sent" never meant "a reply is coming".
- **Keys alive but wedged** - a blocked message thread answers nothing while its socket
  stays open, so a watchdog answers after `--timeout-ms` (default 30 s). Every tool
  handler runs on the message thread, so this is a real case, not a theoretical one.

The fix is in the kit (`src/McpShimMain.cpp`), so every OK Studio plugin with a shim gets
it; `tests/mcp_shim_reconnect.py` there pins all five cases and runs under ctest.

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
   underneath at half the speed. The lines chew on the same held chord, because `arpKeys`
   and `arp2Keys` both default to on.
