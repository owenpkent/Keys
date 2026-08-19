# Corpora

Third-party chord data used to **check** Keys' own progression table, and to look for rows worth
adding. The payloads are gitignored (`datasets/*.csv` and friends), so a fresh clone gets this file
and nothing else - the same arrangement `../manuals/` uses, and for the same reason: they are large,
they are somebody else's, and Keys must never redistribute them.

**Read the licence column before anything derived from a corpus ships.** It is the whole reason
this file exists rather than a folder of downloads.

| Source | What it is | Licence | Can it feed shipped content? | Get it |
| --- | --- | --- | --- | --- |
| `chordonomicon_v2.csv` | 666k songs' chord progressions, annotated with structural part (verse / chorus / bridge), genre and release date | **CC-BY-NC-4.0** | **Not commercially.** Owen's call, 2026-08-18: Keys is personal use, and CC-BY-NC permits that squarely. If Keys is ever sold, anything derived from this has to come out or be re-derived from a permissive source | [HuggingFace](https://huggingface.co/datasets/ailsntua/Chordonomicon) ([paper](https://arxiv.org/abs/2410.22046)) |
| `E:\Ableton\free-midi-progressions-*` | ~1,700 MIDI files, 138 unique progressions across 12 keys each. Roman numerals are in the filenames | **MIT** | **Yes, freely.** *"you can use them in any musical project freely"* | [ldrolez/free-midi-chords](https://github.com/ldrolez/free-midi-chords/releases) |
| `E:\Ableton\free-midi-chords-*` | The same project's chord (rather than progression) half, by key and family | **MIT** | Yes | same |

The two MIT packs live on Owen's E: drive rather than here; they are already unzipped and there is
no reason to keep a second copy.

---

## The trap: two roman-numeral conventions, and they disagree where it matters

Found 2026-08-18 while comparing the MIT pack against `src/ChordLibrary.h`, and it is the single
thing to know before importing a row from anywhere.

- **Keys** uses a fixed **major-scale** degree table for every mode - `ChordMarkov.h`'s
  `numeralDegrees()`, which `MarkovData.h`'s header explains at length. `VII` is *always* 11
  semitones above the tonic. So natural minor's flat degrees must be written `bIII`, `bVI`, `bVII`.
- **The pack** spells a minor-key progression against the **minor** scale, so its `III`, `VI` and
  `VII` are already flat and it writes them unadorned.

The pack's `i VII VI V` and Keys' `i bVII bVI V` are therefore **the same progression** - the
Andalusian cadence - written two ways.

Comparing the two collections without translating first reported an overlap of 19 out of 138, which
is nonsense for two collections that are both mostly canon; translated, it is 25 and every minor row
lines up. **The dangerous direction is the other one**: paste a pack row into `ChordLibrary.h`
verbatim and it parses perfectly and plays the wrong chords, with the table's own tests unable to
tell, because a well-formed numeral is all they can check for.

`scripts/corpus/` holds the translation and the comparison.

---

## What has actually been done with these

Keeping this honest is the point - the table's provenance was once described as "ranked against"
corpora that had only been read *about*, which is corrected in `../docs/CHORD_LIBRARY.md`.

- **The MIT pack: compared.** 25 of Keys' rows are independently present in it, which is
  corroboration of the canonical ones (Pachelbel, doo-wop, ragtime, the axis rotations, the
  Andalusian cadence, the minor axis). 113 pack progressions are not in Keys and are a legitimate
  expansion source.
- **Chordonomicon: see `../docs/CHORD_LIBRARY.md` §10** for what was measured and what came of it -
  seven rows added, eight rows found rare rather than wrong, and a Section axis now answerable.
- **The mood tags: checked, §11.** Chordonomicon's Spotify ids join to audio-feature dumps carrying
  valence and energy, which puts every progression on Russell's circumplex. The control (minor
  should read sadder than major) passes at +0.028 over 121,656 joined songs, and that number is the
  yardstick: it is roughly the most a purely harmonic fact moves an *audio* valence measure. Nothing
  was retagged.
