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

Added 2026-11-02 (Owen: "investigate house and dance"). Every payload is in its own subfolder, all
gitignored (`datasets/*/`); the licence column was read from the LICENSE file or dataset card, not
from memory. Owen's standing call applies to all of them: Keys is personal use, so NC is fine for
now, but anything NC has to come out of shipped content if Keys is ever sold.

| Source | What it is | Licence | Can it feed shipped content? | Get it |
| --- | --- | --- | --- | --- |
| `chordonomicon_v2.csv` (252 MB) | **679,807 rows** (the card says 666k). Columns: `chords` (with `<verse_1>` style part tags), `genres` (Spotify tag list), `main_genre`, `decade`, `rock_genre`, `release_date`, Spotify ids. **Genre is labelled**, but `main_genre` is empty for 327,696 rows. `main_genre = electronic` on **2,814** songs; free-text `genres` mentions house 2,266, EDM 2,188, trance 638, techno 385, eurodance 540, dubstep 238, drum and bass 72, dance 11,263 (mostly "dance pop") | **CC-BY-NC-4.0** (dataset card, re-confirmed) | Not commercially. Personal use is fine | same HuggingFace page as above |
| `mcgill_billboard/` (9 MB) | Billboard Hot 100 1958-1991 expert chord annotations. `salami_chords/` (SALAMI-style text, with structure), `lab/` (MIREX `.lab`), `billboard-2.0-index.csv` (1,300 chart slots, 890 with title and artist). Chord syntax is Harte. No genre column, and it predates house | **CC0** (stated on the DDMAL page) | **Yes**, no restriction (cite Burgoyne et al., ISMIR 2011) | [DDMAL page](https://ddmal.ca/research/The_McGill_Billboard_Project_(Chord_Analysis_Dataset)/) |
| `pop909/` (96 MB) | 909 Chinese pop songs: MIDI with melody / bridge / piano tracks, plus `chord_midi.txt`, `chord_audio.txt`, `key_audio.txt`, beats, and `index.xlsx`. Chords are time-aligned labels. Pop only, no genre column | **MIT** (LICENSE file) | Yes | [music-x-lab/POP909-Dataset](https://github.com/music-x-lab/POP909-Dataset) |
| `choco/` (326 MB) | ChoCo, the Chord Corpus: **20,280 JAMS files** (2,283 audio, 17,997 symbolic) from 18 partitions: biab-internet 4,990, wikifonia 6,288, real-book 2,846, ireal-pro 2,048, nottingham 1,022, billboard 890, weimar 456, when-in-rome 449, rock-corpus 200, isophonics 225, uspop2002 195, jaah 89, jazz-corpus 76, and others. `meta.csv` has a `genre` column but it is filled for only 5,866 files and is mostly jazz tempo words. **No electronic or dance partition.** The release zip's knowledge-graph `kg/` folder was dropped (about 2 GB, not needed) | **Dual: CC-BY-4.0**, except the Chordify, Mozart Piano Sonata and JAAH-derived data, which are **CC-BY-NC-SA-4.0** (`choco/LICENSE.md`) | Mostly yes. The NC-SA partitions are personal use only, and the SA clause would also bind anything derived from them | [smashub/choco release data-v0.1.0](https://github.com/smashub/choco/releases/tag/data-v0.1.0), [Zenodo](https://doi.org/10.5281/zenodo.7706751) |
| `rock_corpus/` (2 MB) | de Clercq and Temperley Rolling Stone corpus, v2.1: harmonic analyses by two analysts (`_dt.har`, `_tdc.har`) for **200 songs** (`rs200.txt`), plus melodies and timing. Roman-numeral `.har` files. Rock 1950s-1990s, no genre column. Note ChoCo repackages these as its `rock-corpus` partition | **CC BY 4.0** (stated on the site) | **Yes**, with attribution | [rockcorpus.midside.com](https://rockcorpus.midside.com/) |
| `isophonics/chordlab/` (13 MB) | Chord `.lab` files (plus `.svl`) for **225 songs**: Beatles 180, Queen 20, Zweieck 18, Carole King 7. The Michael Jackson download has segmentation only, no chords, so it was dropped. Harte syntax. No genre column | **No licence is stated** on any of the pages or in the archives. Treat as research-use only | Do not ship. Personal analysis only until a licence is found | [isophonics.net/datasets](https://isophonics.net/datasets) |
| `giantsteps/gs-key/` and `giantsteps/gs-mtg-key/` (15 MB) | **Electronic dance, key only, no chords.** GiantSteps Key: 604 two-minute Beatport previews, key and genre files, JAMS. GiantSteps MTG Key: **1,486 tracks** with a manual key plus Beatport metadata (`BP GENRE`, `BP BPM`, `BP KEY`, label). Genre is labelled, spread evenly over Trance, Hard Dance, Progressive House, Tech House, Techno, Minimal, Breaks, Electro House, Psy-Trance, Dubstep, House (93), Electronica, Drum and Bass, Chill Out, Hip-Hop, Deep House (83). The audio is not included (a download script for Beatport previews is, and was not run) | **No licence in either repo or in the GitHub metadata.** Annotations only, from the ISMIR 2015 paper; audio rights stay with Beatport | Research use only. Useful as a key and mode prior for house and techno, not as a chord source | [GiantSteps/giantsteps-key-dataset](https://github.com/GiantSteps/giantsteps-key-dataset), [giantsteps-mtg-key-dataset](https://github.com/GiantSteps/giantsteps-mtg-key-dataset) |
| `raveform/` (1.4 GB) | **EDM structure, not chords.** 4,911 DJ mixes, 61,886 distinct tracks (`tracks.jsonl`), beat grids, mix-to-track alignments, and functional structure labels (drop, build, breakdown) in `structures/` for 1,426 files. Genre comes from MixesDB categories (`mixesdb_genres.jsonl`, 110 genres: house, deep house, techno, trance, drum and bass and so on). Tells you how dance tracks are laid out in time, which a harmony generator can use for section lengths | **CC-BY-4.0** (HuggingFace card), code MIT | Yes, with attribution | [mir-aidj.github.io/raveform](https://mir-aidj.github.io/raveform/), [HuggingFace](https://huggingface.co/datasets/taejunkim/raveform) |

### Not downloaded, and why

| Source | Reason |
| --- | --- |
| Lakh MIDI | Skipped on instruction: huge, little chord data, and no small chord-labelled derivative was found |
| Hooktheory / TheoryTab (`m-a-p/HookTheory`, `wayne391/lead-sheet-dataset`) | Gated on HuggingFace, tagged CC-BY-NC-4.0 and academic-only, and Hooktheory's Terms of Service forbid scraping, mining and model training. It is the best-known pop and EDM lead-sheet source, so it is worth a look if Owen wants to accept the terms himself |
| An EDM chord-annotation set | **None exists** that a search could find. The EDM resources are key (GiantSteps), structure (Raveform, the 109-track TimeCues set) or paid MIDI packs. For house chords, Chordonomicon's 2,814 `electronic` rows and its house / EDM tagged songs are the only large, free source |
| Isophonics, Michael Jackson | Downloaded, then removed from the folder: it carries segmentation labels but no chord files |

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
