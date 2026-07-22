# Keys Host — design

**Status: implemented (2026-07-19).** VST3 + Standalone build; the standalone
launches and renders correctly. Still owed: a real Ableton Live load test (the MIDI
input bus invariant is only provable in Live) and a first hosted-instrument session.
The design below is as-built; deviations would be bugs.

## What it is

A third plugin target in this repo: **Keys Host** — a VST3 instrument that embeds the
whole Keys UI *and* hosts one third-party instrument VST3 inside itself, both visible
in one window on one track. Solves Live's "no plugin MIDI effects before an
instrument" limitation from the other side: from Live's point of view it is just one
instrument that makes sound.

Prior art proving DAWs accept the category: Blue Cat PatchWork, DDMF Metaplugin,
Kushview Element.

## Key insight

Keys does not need to be *hosted* — it's our code. Embed it statically; host only the
instrument. So this is a one-slot host, not a chainer.

## Processor: `KeysHostProcessor : KeysProcessor` (new files `src/host/`)

Subclassing works because the playing surface takes a concrete `KeysProcessor&`
(`src/ui/NoteSurface.h`), and `KeysProcessor` already declares a real stereo output
bus that it clears every block (`PluginProcessor.cpp` ~113, ~368). The subclass fills
that bus with the hosted instrument's audio.

- Members: `juce::AudioPluginFormatManager` (VST3 format only),
  `std::unique_ptr<juce::AudioPluginInstance> instrument`, preallocated
  `juce::AudioBuffer<float> hostBuffer` (max of instrument's in/out channel counts),
  instrument file path. Processor is a `juce::ChangeBroadcaster` so the editor
  follows load/eject.
- `processBlock`: call `KeysProcessor::processBlock(buffer, midi)` first (clears
  audio, drains the collector's UI notes/CCs into `midi`), then
  `instrument->processBlock(proxy, midi)` on a channel-count-matched proxy over
  `hostBuffer`, then copy the first channels into `buffer` (duplicate ch0 if the
  instrument is mono-out). No instrument loaded → silence, exactly today's Keys.
- **Swap without audio-thread locks** (repo invariant): loading/ejecting happens on
  the message thread wrapped in `suspendProcessing(true/false)`; the audio thread
  never takes a lock.
- Loading (message thread, synchronous is fine for VST3):
  `findAllTypesForFile` on the chosen `.vst3` → prefer `desc.isInstrument` → sync
  `formatManager.createPluginInstance(desc, sr, block, err)` → best-effort force main
  output bus to stereo via `setBusesLayout` → apply saved state if restoring →
  `setRateAndBufferSizeDetails` + `prepareToPlay` → size `hostBuffer` →
  `setLatencySamples(instrument->getLatencySamples())`.
- `prepareToPlay` override re-prepares the instrument; `getTailLengthSeconds`
  forwards from the instrument.
- **No plugin scanner.** "Load Instrument…" opens `InstrumentPicker`: an overlay
  listing every `.vst3` in the DAW-scanned folders **by filename only** — nothing is
  instantiated until a row is clicked, so listing can't crash and there is nothing
  to rescan. A file-browser fallback covers odd locations, and the editor is a
  `FileDragAndDropTarget` for `.vst3` dragged from Explorer. (Dragging from Live's
  own browser is impossible: its drags are internal to Live.) If real descriptions
  (instrument-vs-effect filtering) are ever wanted, carve Watershed's out-of-process
  scanner (see "Watershed carve-outs").

## State

Small refactor in `KeysProcessor`: extract pad serialization
(`PluginProcessor.cpp:375-437`) into protected `chordPadsToTree()` /
`chordPadsFromTree(root)`, used by the base get/setState unchanged (same "KEYS" root
tag — sessions stay interchangeable between Keys and Keys Host). The host override
saves `okstudio::state::save(apvts, "KEYS", dest, { chordPadsToTree(),
hostedInstrumentTree() })` where `hostedInstrumentTree` holds the `.vst3` path, name,
and the instrument's full state blob base64'd. Restore: load pads, then re-load the
instrument from the path and apply its blob.

Because the instrument's *complete* state is saved in the Live set, its own MIDI
Learn mappings (e.g. Keys fader CC → filter cutoff) persist with the project. This
is the answer to the "reassign CCs every session" pain: assign once, saved forever.

## Editor: `KeysHostEditor : AudioProcessorEditor`

- Top bar (~40 px): **Load Instrument…** button, instrument-name label,
  **Show/Hide Instrument** toggle, **Eject**. All ≥34 px targets, single left-click.
- The hosted instrument's editor (`createEditorIfNeeded()`, fall back to
  `GenericAudioProcessorEditor` when the plugin has no GUI) lives in
  `InstrumentWindow`, a **floating native-titlebar `DocumentWindow`** placed above
  the keyboard window — two windows by Owen's request, not a stacked editor. Its
  close button hides (never ejects); `setContentNonOwned(&ed, true)` keeps the
  window sized to the GUI, including plugin-initiated resizes. Teardown order is
  window → `instance->editorBeingDeleted(editor)` → editor, and it must run
  **before** the instance can be destroyed — the processor exposes an "instrument
  about to change" callback so the editor closes the old GUI first, then the
  processor swaps, then broadcasts.
- Below the bar: an embedded `KeysEditor` (it is just a Component) fills the plugin
  window. After constructing it, call `keysEditor.setResizable(false, false)` to
  kill its own corner-resizer; give it ≥1010×640 (its gen-panel growth floor,
  `PluginEditor.cpp:317`).
- `InstrumentPicker` groups by publisher: bundle `moduleinfo.json` "Factory
  Info"/"Vendor", else the DLL version-resource CompanyName (Windows,
  `version.lib` via `#pragma comment`), else the vendor subfolder name; unknowns
  group last as "Other".
- **Updater gating**: the embedded `KeysEditor` runs the Keys updater check in its
  ctor (`PluginEditor.cpp:240-250`). The KeysHost target compiles its own copy of the
  sources, so gate it with a `KEYS_HOST=1` compile definition (same pattern as
  `KEYS_MIDI_EFFECT`).

## CMake

Follow the KeysFX pattern exactly (`CMakeLists.txt:83-94`): `option(KEYS_BUILD_HOST)`,
`okstudio_add_plugin(KeysHost PRODUCT_NAME "Keys Host" PLUGIN_CODE KyHo
BUNDLE_SUFFIX keyshost ${_keysCopy})`, `keys_configure_target(KeysHost)`, then
additionally: the two `src/host/*.cpp` sources, `KEYS_HOST=1`, and
`JUCE_PLUGINHOST_VST3=1` (set nowhere in Keys or the kit today; JUCE bundles the
VST3 hosting headers, no external SDK needed). `createPluginFilter` lives at
`PluginProcessor.cpp:445` — it must return `KeysHostProcessor` for this target
(gate on `KEYS_HOST`). `okstudio_add_plugin` is already called twice in this repo;
nothing in the kit assumes one plugin per repo.

## Watershed carve-outs (`../Watershed`, if/when wanted)

- `src/plugins/PluginScanner.{h,cpp}` — out-of-process VST3 scan + crash quarantine;
  only TE coupling is `engine.getPluginManager().knownPluginList` (a plain
  `juce::KnownPluginList`) and temp-dir access. Mechanical swap.
- `src/ui/PluginBrowserComponent.{h,cpp}` — row-list browser with the
  click-or-drag `DraggableRowButton` (mouse-only contract already applied, 40 px
  rows). Strip the track/command plumbing.
- `src/ui/DeviceWindows.{h,cpp}` — `createEditorIfNeeded` / `editorBeingDeleted` /
  `DocumentWindow` handling is pure JUCE, near copy-paste.
- Plugin *instantiation* in Watershed is Tracktion's; write fresh with
  `AudioPluginFormatManager::createPluginInstance` (above).

## Known trade-offs (decided, revisit consciously)

- **Recording**: playing inside Keys Host is internal, so Live doesn't capture MIDI
  clips on the same track by default; a listener track ("MIDI From: Keys Host")
  still can, since the host passes its MIDI through. The two-track workflow records
  natively — this is the main workflow difference between them.
- **Track MIDI out is always Keys' notes.** The hosted instrument processes a *copy*
  of the block's MIDI (`instrumentMidi`), so a synth that clears its buffer can't
  silence the track output — that output is what lets "MIDI From: Keys Host" drive
  Ableton's native instruments (which no plugin can host) on other tracks. Flip
  side, accepted: a hosted plugin's own MIDI output never reaches the track.
- v1 is VST3-only hosting, one instrument slot, file-picker instead of scanner,
  synchronous instantiation (brief UI freeze on load, same as a DAW insert).
- Multi-out instruments (Kontakt 16-out): v1 takes the main stereo pair only.
- Direct fader binding shipped as **auto-assign** (2026-07-19): on every instrument
  load, `assignFaderParams()` keyword-matches `instrument->getParameters()` names
  and binds the 8 knobs (cutoff, resonance, attack, decay, sustain, release,
  reverb/wet, drive). `KeysProcessor` gained virtual `faderMoved`/`faderTargetName`
  hooks; `KnobBank` (formerly `FaderBank`) calls them and shows the bound name.
  Bindings are message-thread state, recomputed per load, deliberately not
  persisted. Owen: instrument control matters; DAW-wide control does not (decided
  2026-07-19).
- **Single view, no tabs** (see CHANGELOG): Keys dropped the five tabbed surfaces
  and the `uiLayout`-selected Classic/Performer arrangement for one fixed layout —
  header, knob row, chord pads, playing surface. Keys and Keys Host build the piano
  only; the Harmonic Table and Hex Host moved out to their own repo (`../Hex`).
  `surface`/`uiLayout`/`padChannel`/`xyCC*` stay registered for session compatibility
  but are no longer read by the UI.
