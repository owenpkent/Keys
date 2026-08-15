# Keys Host — design

**Status: implemented (2026-07-19).** VST3 + Standalone build; the standalone
launches and renders correctly. The hosting side is exercised daily: `run.py` launches
the Keys Host standalone with a real instrument VST3 in-process, and that is this repo's
default dev loop. Still owed: a real Ableton Live load test (the MIDI input bus invariant
is only provable in Live).
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
bus that it clears every block (`PluginProcessor.cpp` ~274, ~807). The subclass fills
that bus with the hosted instrument's audio.

- Members: `juce::AudioPluginFormatManager` (VST3 format only),
  `std::unique_ptr<juce::AudioPluginInstance> instrument`, preallocated
  `juce::AudioBuffer<float> hostBuffer` (max of instrument's in/out channel counts),
  instrument file path. Processor is a `juce::ChangeBroadcaster` so the editor
  follows load/eject.
- `processBlock`: call `KeysProcessor::processBlock(buffer, midi)` first (clears
  audio, drains the collector's UI notes/CCs into `midi`), then
  `instrument->processBlock(proxy, instrumentMidi)` (a copy of `midi`, see the trade-off
  below) on a channel-count-matched proxy over
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
(`PluginProcessor.cpp:959-1018`) into protected `chordPadsToTree()` /
`chordPadsFromTree(root)`, used by the base get/setState unchanged (same "KEYS" root
tag — sessions stay interchangeable between Keys and Keys Host). The host override
saves `okstudio::state::save(apvts, "KEYS", dest, { chordPadsToTree(), arpToTree(),
layoutToTree(), hosted })` where `hosted` is a locally built `hostedInstrument` tree
holding the `.vst3` path, name, and the instrument's full state blob base64'd. All four
trees matter: drop one and a Keys Host session comes back without its arp slots or its
folds. Restore: call `KeysProcessor::restoreSharedState(root)`, then re-load the instrument
from the path and apply its blob.

**Call the shared one; never re-list what it does.** This used to read "load pads, arp and
layout", and the override was three calls copied out of the base class. On 2026-07-27 a
session repair was added to that base list, worked in Keys, and did nothing at all in Keys
Host, which nobody would have noticed except that it showed up in a screenshot. Anything
session shaped belongs in `restoreSharedState`, so both products get it or neither does.

Because the instrument's *complete* state is saved in the Live set, its own MIDI
Learn mappings (e.g. Keys fader CC → filter cutoff) persist with the project. This
is the answer to the "reassign CCs every session" pain: assign once, saved forever.

## Editor: `KeysHostEditor : AudioProcessorEditor`

- **No top bar of its own** (removed 2026-08-02, Owen: "the load instrument section with all
  that should go in the controls submenu"). `KeysHostEditor::resized()` is just
  `keysEditor.setBounds(getLocalBounds())` - the embedded `KeysEditor` fills the whole window
  edge to edge, and `paint()` is a plain background fill behind it. **Load Instrument…**,
  **Show/Hide Instrument** and **Eject** live behind an **Instrument** chip on
  `KeysEditor`'s own Controls bar instead, opened as a `juce::PopupMenu`: a section header with
  the instrument's name if one is loaded, "Load instrument...", "Show/Hide instrument GUI"
  (enabled only if something is loaded), a separator, then "Eject" (enabled only if loaded).
  The mechanism is a new public hook on `KeysEditor` - `onBuildInstrumentMenu` (fills the
  menu), `instrumentName` (supplies the chip's caption), `refreshInstrumentChip()` (call after
  a load or an eject so the caption catches up) - the first extension point `KeysEditor` has
  ever exposed to something embedding it. Plain Keys never sets these, so the chip stays
  invisible there and its Controls bar is unchanged; only Keys Host wires them up, in its
  constructor. `KeysHostEditor::updateBar()` is renamed `refreshInstrumentUi()` to match: there
  is no bar left to update, only the chip.
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
- The embedded `KeysEditor` (it is just a Component) fills the whole plugin window. After
  constructing it, call `keysEditor.setResizable(false, false)` to kill its own
  corner-resizer and `keysEditor.setEmbedded(true)` so it never calls `setSize` on itself: here
  the host owns geometry. Its height is no longer a floor, because every section folds. It
  reports what the current folds add up to through `keysEditor.onIdealHeightChanged`, and the
  host follows it in both directions, resizing to exactly `wanted` inside
  `setResizeLimits(keysEditor.minWidthForView(), absMinKeysHeight, 2600, maxWindowHeight())`,
  and opens at that same width. There is no `barHeight` added to any of this any more (deleted
  2026-08-02 along with the bar itself) - the window's height is purely
  `KeysEditor::idealHeight()`, clamped to that floor and ceiling. **Ask, do not copy**: the
  width floor was a literal 1010 until 2026-07-30, when a Generator chip joined Fill and Regen
  on the Pads bar and moved the editor's own floor to 1070, then 1280 on 2026-08-02, when the
  Controls bar overtook the Pads bar as the binding constraint (BPM's caption, Voices' and CH's
  captions, and a Tempo Sync chip all joining that bar the same day) - a host window narrower
  than the editor it embeds carves controls off the right-hand end of that bar with nothing on
  screen to say so. Detaching any section drops its height out of that number, since a detached
  section lives in its own window.
- `InstrumentPicker` files instruments into one **collapsible folder per publisher**:
  bundle `moduleinfo.json` "Factory Info"/"Vendor", else the DLL version-resource
  CompanyName (Windows, `version.lib` via `#pragma comment`), else the vendor
  subfolder name; unknowns group last as "Other". Folders open closed, since a large
  library listed flat is a long scroll and scrolling is the expensive gesture here.
  Which folders are open survives Rescan (`openFolderNames`, by name). Closed folders
  hide their rows and take no layout space.
- **Folders and instruments must not share a look.** Both are `TextButton`s, and on the
  skin's default that means one centred raised pill each, leaving a 26 px indent and a
  triangle as the only difference — instruments read as more folders. So the picker
  carries two LookAndFeels: `FolderLookAndFeel` keeps the raised chip with a bright
  semibold left-aligned caption, while `ItemLookAndFeel` draws **no background at all**
  at rest and lights the row with a `skin::accent` edge on hover. Dimming the
  instrument chip instead of removing it was tried first and was not enough.
- **Updater gating**: the embedded `KeysEditor` runs the Keys updater check in its
  ctor (`PluginEditor.cpp:594-609`). The KeysHost target compiles its own copy of the
  sources, so gate it with a `KEYS_HOST=1` compile definition (same pattern as
  `KEYS_MIDI_EFFECT`).

## CMake

Follow the KeysFX pattern exactly (`CMakeLists.txt:84-95`): `option(KEYS_BUILD_HOST)`,
`okstudio_add_plugin(KeysHost PRODUCT_NAME "Keys Host" PLUGIN_CODE KyHo
BUNDLE_SUFFIX keyshost ${_keysCopy})`, `keys_configure_target(KeysHost)`, then
additionally: the two `src/host/*.cpp` sources, `KEYS_HOST=1`, and
`JUCE_PLUGINHOST_VST3=1` (set nowhere in Keys or the kit today; JUCE bundles the
VST3 hosting headers, no external SDK needed). `createPluginFilter` lives at
`PluginProcessor.cpp:1673`, and it must return `KeysHostProcessor` for this target
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
- **One surface, folding sections** (see CHANGELOG): Keys dropped the five tabbed
  surfaces and the `uiLayout`-selected Classic/Performer arrangement. The surface is
  picked at compile time now, and the rest of the editor is a stack of **four** foldable
  sections: Controls (one header row plus the knob bank, unconditional now that the Knobs
  chip that used to fold it is gone), Arp, Pads, Keyboard. There
  are no tabs anywhere: the centre view went on 2026-07-30 with the chord generator's panel,
  and the Transcribe section was removed the same day, which also took Keys off the static
  MSVC runtime that its ONNX Runtime forced on the whole binary. Every section detaches into
  a window of its own (titled `Keys Controls`, `Keys Arpeggiator`, `Keys Chord Pads`,
  `Keys Keyboard`), and every fold changes the height Keys Host is asked for. The chord
  generator opens a window too (`Keys Chord Generator`), but it is not a section and never
  docks, so it changes no height at all. Keys and Keys
  Host build the piano only; the Harmonic Table and Hex Host moved out to their own repo
  (`../Hex`). `surface`/`uiLayout`/`padChannel`/`xyCC*` stay registered for session
  compatibility but are no longer read by the UI.
