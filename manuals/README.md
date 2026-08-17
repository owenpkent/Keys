# Reference manuals

Third-party PDFs for the hardware and plugins Keys' design borrows from. **They are gitignored**
(`*.pdf`, matched at any depth), so a fresh clone gets this file and nothing else. That is what
this file is for: it is the manifest, with a working download URL for every one of them.

They lived loose in the repo root until 2026-08-17. Nineteen files and about 265 MB is a folder,
not a root.

**Read the relevant one before designing a feature, not after.** Three times now a manual has been
cheaper than the rebuild it would have prevented: the Serum guide corrected a built `RangeKnob`
satellite, the Cthulhu manual corrected a randomness feature that had shipped as a global knob,
and the Acid V manual corrected the assumption that a 303 grid could not fit the arp panel.

Two documents sit on top of these:

- **`../docs/REFERENCES.md`** - what Keys **took** from a manual, and what it deliberately did
  not. Eight manuals are in it.
- **`../docs/SEQUENCER_LANDSCAPE.md`** - the map of sequencer archetypes, from the eleven added on
  2026-08-17. Nothing has been taken from those yet, which is why they are not in the other file.
  Move an entry across the day something ships from it.

---

## In use (`../docs/REFERENCES.md`)

| File | Product | Pages | What Keys took | Get it |
| --- | --- | --- | --- | --- |
| `Cthulhu_Manual_v1_1.pdf` | Xfer Cthulhu | 34 | The whole per-parameter step-lane architecture; Link Lengths; the Rand lane; mute-preserves-value | account, or [mirror](https://medias.audiofanzine.com/files/cthulhu-manual-475318.pdf) |
| `Kirnu-Cream-Manual.pdf` | Kirnu Cream | 18 | Chord memory per step; rate in Hz; lane Reset; the Note lane's Prev/Hi/Low/Rnd; Select | [kirnuarp.com](https://www.kirnuarp.com/Kirnu-Cream-Manual.pdf) |
| `stochas_av.pdf` | Stochas | 17 | Probability per cell; bipolar variance; the Chain conditional | [stochas.org](https://stochas.org/assets/manuals/stochas_av.pdf) |
| `Serum 2 User Guide.pdf` | Xfer Serum 2 | 354 | `RangeKnob`: the modulation-depth halo and its satellite (p195). UI, not sequencing | account ([xferrecords.com](https://xferrecords.com/)) |
| `Subharmonicon_Manual AMZ.pdf` | Moog Subharmonicon | 58 | Rhythm generators as an OR of clock dividers; the undertone series | [moogmusic.com](https://www.moogmusic.com/downloads/?product=Subharmonicon) |
| `matrixbrute_Manual_2_0_3_EN.pdf` | Arturia MatrixBrute | 73 | Nothing yet. Ties and per-step slide are both still on the table | [v2.0.1](https://downloads.arturia.net/products/matrixbrute/manual/MatrixBrute_Manual_2_0_1_EN.pdf) |
| `Numerology4Manual.pdf` | Five12 Numerology 4 | 251 | The skip-versus-mute distinction, and the guard that a sequence's first and last steps cannot be skipped | [five12.com](https://files.five12.com/Numerology4Manual.pdf) |
| `Acid-V-Manual.pdf` | Arturia Acid V | 105 | The source of `../docs/ACID_DESIGN.md`. Proposed, unbuilt | [v1.0.0](https://downloads.arturia.net/products/acid-v/manual/acid-v_Manual_1_0_0_EN.pdf) |

## Surveyed (`../docs/SEQUENCER_LANDSCAPE.md`)

Added 2026-08-17. Nothing taken yet; the fourth column is the archetype each one is here for.

| File | Product | Pages | Archetype | Get it |
| --- | --- | --- | --- | --- |
| `Hapax-Manual.pdf` | Squarp Hapax | 159 | Offline transforms over a selection (Flip, Curve, Shuffle, Randomize) | [squarp.net](https://squarp.net/hapax/manual/) |
| `Metropolix-Manual.pdf` | Intellijel Metropolix | 193 | Stage sequencing, and the accumulator | [intellijel.com](https://intellijel.com/downloads/manuals/metropolix_manual_v1.4_2022.04.04.pdf) |
| `Torso-T-1-Manual.pdf` | Torso T-1 | 230 | Knob-driven generation (Phrase, Range, Style) | [B&H](https://www.bhphotovideo.com/lit_files/976203.pdf) |
| `Digitakt-II-Manual.pdf` | Elektron Digitakt II | 118 | Trig conditions (A:B, 1ST/LST, NEI), parameter locks, Fill mode | [elektron.se](https://www.elektron.se/support-downloads/digitakt-ii) |
| `Deluge-Guidebook.pdf` | Synthstrom Deluge | 338 | Probability and iteration conditions; polymetry | [synthstrom.com](https://synthstrom.com/product/deluge/) |
| `Deluge-Community-Guide.pdf` | Synthstrom Deluge | 28 | Companion to the above | [synthstrom.com](https://synthstrom.com/product/deluge/) |
| `OXI-One-Manual.pdf` | OXI One | 161 | A sequencer **mode** per track (Mono/Poly/Chord/Stochastic/Matriceal) | [oxiinstruments.com](https://oxiinstruments.com/oxi-one) |
| `Rene-2-Manual.pdf` | Make Noise René 2 | 37 | Cartesian / non-linear playback order | [makenoisemusic.com](https://www.makenoisemusic.com/wp-content/uploads/2024/03/renemanual.pdf) |
| `Turing-Machine-Build-Doc.pdf` | Music Thing Turing Machine | 20 | Shift register: randomness that hardens into a loop | [thonk.co.uk](https://www.thonk.co.uk/shop/turingmkii/) |
| `Pamelas-Pro-Workout-Manual.pdf` | ALM Pamela's PRO Workout | 27 | Euclid + probability as a clock utility. Cross-check, not a source | [busycircuits.com](https://busycircuits.com/pages/alm034) |
| `KeyStep-Pro-Manual.pdf` | Arturia KeyStep Pro | 193 | Per-step randomness. Cross-check, not a source | [arturia.net](https://dl.arturia.net/products/keystep-pro/manual/keystep-pro_Manual_2_5_0_EN.pdf) |

---

## Traps in this folder

- **The Turing Machine PDF is a build guide.** Twenty pages of soldering photographs and 61 MB of
  the folder's 265. The concept - the loop knob, and what noon, 3, 7 and 5 o'clock each do - is
  on [musicthing.co.uk/Turing-Machine](https://www.musicthing.co.uk/Turing-Machine/) and is quoted
  in full in `../docs/SEQUENCER_LANDSCAPE.md`. Do not go looking for it in the PDF.
- **Two are account-gated and cannot be re-downloaded from a link.** Cthulhu and Serum 2 are both
  Xfer, both behind a customer login, both reachable from the gearbox icon inside the plugin. The
  Cthulhu mirror above is a v1.1 copy on Audiofanzine and is the one this folder holds.
- **Two Arturia links are a version behind the local copy.** The repo holds MatrixBrute 2.0.3 and
  Acid V 1.1.1; the verified direct links are 2.0.1 and 1.0.0, because Arturia does not keep old
  version URLs alive and the current ones sit behind the product's
  [downloads page](https://www.arturia.com/support/downloads-manuals). Take the newest from there;
  nothing cited from either manual has moved.
- **Ableton has no PDF manual from Live 12 onward.** Follow Actions is cited from
  [the web manual](https://www.ableton.com/en/live-manual/12/launching-clips/).
- **Scaler 2's manual ships inside the plugin** and is not downloadable, so it is not here.
  Anything attributed to it in `../docs/SEQUENCER_LANDSCAPE.md` came from published behaviour and
  should be checked against the real manual before it is built on.
- **Filenames are as downloaded, and inconsistently so** (`stochas_av.pdf`,
  `Subharmonicon_Manual AMZ.pdf`, `matrixbrute_Manual_2_0_3_EN.pdf`). They are deliberately not
  renamed: `../docs/REFERENCES.md` heads each section with the exact filename, `../CLAUDE.md` and
  `../docs/ARP_DESIGN.md` both cite `Serum 2 User Guide.pdf` by name, and `../CHANGELOG.md` cites
  `Cthulhu_Manual_v1_1.pdf` in a dated entry that is a historical record. A tidier name is not
  worth falsifying a changelog for.
