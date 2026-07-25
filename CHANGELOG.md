# Changelog

All notable changes to Keys are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/); versions are semver.

## [Unreleased]

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
