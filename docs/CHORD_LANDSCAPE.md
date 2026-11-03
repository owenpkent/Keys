# The chord landscape

Status: **research, nothing built.** Written 2026-11-02 after Owen's "how do people find chords?
what books or libraries are available?"

Same job as `docs/SEQUENCER_LANDSCAPE.md`, for harmony: a map of how chords get found, what has
been written about it, and what Keys has and lacks. It does not re-survey what is already
recorded. Scaler's tagging scheme is in `docs/CHORD_LIBRARY.md` section 2 and `docs/REFERENCES.md`
(Scaler 3), and the Chordonomicon is the open dataset `docs/CHORD_LIBRARY.md` section 7 already
points at.

**Verification.** Licences marked (verified) were read from a project page or dataset card in this
pass. Anything marked (unverified) is from memory or a single secondary source and must be checked
at the source before Keys ships or links anything. A licence on a dataset covers the file, not the
songs behind it; see the note under the datasets table.

---

## 1. How people actually find chords

| Method | What it is | Keys today |
| --- | --- | --- |
| By ear / transcription | Play along until it fits; copy a record. The oldest method and still the commonest. | Chord naming from sounding notes helps check an answer; no input from audio. |
| Functional harmony (T-S-D) | Chords as Tonic, Subdominant, Dominant; tension resolves. Progressions are paths through the three. | Progression templates, the Markov chain, the library's roman numerals. |
| Modal interchange / borrowing | Take a chord from the parallel minor or another mode (bVI, bVII, iv in a major key). | **No** dedicated source. Negative harmony is a distant cousin. |
| Secondary dominants | Aim a V7 at any chord other than I (V/V, V/vi). Makes an "approach" to a target. | **No.** |
| Substitution | Tritone sub (bII7 for V7), relative major/minor, diminished passing chords. | Relative moves exist inside Neo-Riemannian PLR; tritone sub does not. |
| Voice-leading first | Move one or two notes by step and name the result. Pop guitarists and jazz pianists both do it. | `applyVoiceLeading`; PLR is exactly this. |
| Pedal point / planing | Hold a bass note under changing chords, or slide one shape in parallel. | Planing source; no pedal source. |
| Shapes on the instrument | Guitar grips, piano hand shapes; the hand finds the chord before the theory names it. | Pads and the keybed. |
| Reharmonising a melody | Take a tune and choose chords that fit each melody note or bar. The hardest of these to automate. | **No.** |
| Copying songs and songbooks | Lift a progression from a record, fake book or tab site. | The 355-progression library (tagged by mood and genre). |
| Random / dice / generative | Roll chords inside a key and keep what sounds good. | The generator's weighted pool and tiers. |
| Data-driven "what follows" | Look up what usually comes next in a corpus (Hooktheory Trends is the famous one). | `ChordMarkov.h` over 88 progressions; `ChordSuggest.h`. |
| Chord-scale theory | Pick the scale a chord implies, then play or stack tensions from it. | Scale and mode reads in `ScaleModes.h`; no display of the implied scale. |

The pattern: beginners copy and use shapes, intermediate writers use function and borrowing,
jazz players use substitution and chord-scales, and everybody uses their ear as the final filter.
A tool serving a mouse-first player mostly helps with "what could I try next, and does it sound
like what I want", which is audition plus a short list.

---

## 2. Books

Ranked for a songwriter-producer working in a plugin, not for a conservatory student. Authors and
titles were checked against publisher or library records from memory of standard catalogues;
verify an edition before quoting a page.

| # | Book | Good for | For |
| --- | --- | --- | --- |
| 1 | *Hooktheory I: Music Theory for Pop Songwriting* and *Hooktheory II*, Dave Carlton, Chris Anderson and Ryan Miyakawa | Roman-numeral thinking applied to hit songs, with real examples. The nearest print match to what Keys does. | Pop writers, beginners to intermediate |
| 2 | *Harmony and Voice Leading*, Edward Aldwell and Carl Schachter | The standard classical text: function, voice leading, chromatic harmony. | Serious students; reference |
| 3 | *The Jazz Theory Book*, Mark Levine | Chords, substitutions, reharmonisation and chord-scales explained in plain prose. | Jazz and any player who wants substitutions |
| 4 | *The Jazz Piano Book*, Mark Levine | Voicings by hand shape, rootless voicings, comping. | Pianists |
| 5 | *Tonal Harmony*, Stefan Kostka and Dorothy Payne | Widely used undergraduate text; many exercises. | Students, self-teachers |
| 6 | *Geometry of Music*, Dmitri Tymoczko | Voice-leading as distance between chords; the maths under planing and smooth moves. | Theory-minded, and anyone building voice-leading code |
| 7 | *Audacious Euphony*, Richard Cohn | Neo-Riemannian theory (PLR) and chromatic third-relations. Keys' PLR source comes from here. | Film and chromatic writers |
| 8 | *Chord Chemistry*, Ted Greene | An enormous catalogue of guitar voicings and moves. | Guitarists |
| 9 | *Chord-Scale Theory and Jazz Harmony*, Barrie Nettles and Richard Graf | The standard chord-scale text (Berklee tradition). | Jazz students |
| 10 | *Hearing Harmony: Toward a Tonal Theory for the Rock Era*, Christopher Doll (2017) | Rock's own chord grammar, built from listening rather than borrowed from classical rules. | Pop and rock |
| 11 | *The Foundations of Rock*, Walter Everett (2009); *Form as Harmony in Rock Music*, Drew Nobile (2020); Nicole Biamonte's articles on modal and pentatonic rock harmony | Detailed analyses of how rock uses harmony, and how harmony shapes a song's sections. | Analysts |
| 12 | *Structural Functions of Harmony*, Arnold Schoenberg | Roots, regions and "monotonality"; useful for key-area thinking. | Advanced |
| 13 | *Harmony*, Walter Piston | Compact classical text, Roman-numeral norm. | Reference |
| 14 | *Writing Better Lyrics*, Pat Pattison; *Modern Jazz Voicings*, Ted Pease and Ken Pullig (Berklee Press) | Songwriting craft, section logic, voicing for arrangers. | Songwriters, arrangers |
| 15 | *Open Music Theory* (v2, Mark Gotham, Megan Lavengood and others) and musictheory.net | Free, online, versioned. | Everybody, first stop |


---

## 3. Datasets and corpora

All URLs were returned by search or are the canonical project page; licences as flagged.

| Corpus | What it holds | Licence | Source |
| --- | --- | --- | --- |
| Chordonomicon | 666,000+ user-generated progressions with genre, decade and structure tags | CC-BY-NC-4.0 (see `datasets/README.md`): fine for personal use, which is Owen's call for Keys today; anything derived from it comes out if Keys is ever sold. The songs behind it are scraped user content as well. | [HF](https://huggingface.co/datasets/ailsntua/Chordonomicon), [paper](https://arxiv.org/abs/2410.22046) |
| McGill Billboard | Expert chord annotations of ~890 Billboard-chart songs (1958 to 1991) | CC0 (verified via project statement and mirdata loader) | [McGill](https://ddmal.music.mcgill.ca/research/The_McGill_Billboard_Project_(Chord_Analysis_Dataset)) |
| ChoCo | 20,000+ chord annotations merged from 18 sources, with a conversion workflow | Dual: most subsets CC-BY 4.0, three subsets CC-BY-NC-SA 4.0 (verified); check per subset | [Zenodo](https://zenodo.org/records/7706751) |
| Weimar Jazz Database | Solos of 456 jazz recordings, with chord annotations | ODbL per ChoCo's metadata page (verified, secondary) | [ChoCo licence page](https://smashub.github.io/docs/choco/metadata_license) |
| Hooktheory TheoryTab | Hand-transcribed melody and chords | No dataset licence found; Hooktheory says raw dumps are not offered and access is via API only (verified). Treat as not shippable. | [forum](https://forum.hooktheory.com/t/can-i-get-a-dump-of-hooktheory-data-for-research/287) |
| Rolling Stone corpus (de Clercq and Temperley) | 200 songs, roman-numeral and key annotations | Free for research; no explicit licence found (unverified) | [Temperley page](https://www.theory.esm.rochester.edu/~temperley/) |
| iReal Pro charts | Large community fake-book | Proprietary app format; charts are user-made copyrighted songs (unverified). Do not ingest. | [irealpro.com](https://www.irealpro.com/) |
| Isophonics (Beatles, Queen, Zweieck) | Chord annotations of songs by single artists | Annotations free for research, non-commercial (unverified) | [isophonics.net](http://isophonics.net/) |
| Wikifonia | Lead sheets | Site shut down 2013; copyright status unclear. Avoid. | none |
| POP909 | 909 pop songs as MIDI with melody, bridge, piano and chord labels | Repo licence MIT (unverified) | [GitHub](https://github.com/music-x-lab/POP909-Dataset) |
| Lakh MIDI | ~176,000 MIDI files | CC-BY 4.0 on the collection; the songs are not cleared (unverified) | [colinraffel.com](https://colinraffel.com/projects/lmd/) |

**Note for Keys.** A licence on a file does not clear the songs inside it. In practice Keys needs
chord progressions as bare roman-numeral sequences with no lyrics and no melody, which is the
position `docs/CHORD_LIBRARY.md` section 7 already takes, so the shippable material is a derived
statistics table (bigram counts), not the corpus. McGill Billboard (CC0) is the one that allows that with no licence
friction; Chordonomicon (CC-BY-NC-4.0) allows it only while Keys stays non-commercial. ChoCo is useful but would need a per-subset
check. Hooktheory data is off the table.

---

## 4. Code libraries

| Library | Language | What it does | Licence | URL |
| --- | --- | --- | --- | --- |
| music21 | Python | Full symbolic toolkit: chord naming, roman numeral analysis, corpus access | BSD 3-clause (verified; older copies were dual BSD or LGPL) | [GitHub](https://github.com/cuthbertLab/music21) |
| Tonal.js | TypeScript | Chord and scale dictionaries, detection, progressions; the lightest of these | MIT (verified) | [GitHub](https://github.com/tonaljs/tonal) |
| mingus | Python | Chord, scale and progression generation | GPL-3 (unverified) | [GitHub](https://github.com/bspaans/python-mingus) |
| pychord | Python | Parse and transpose chord names | MIT (unverified) | [GitHub](https://github.com/yuma-m/pychord) |
| Chordino / NNLS-Chroma | C++ (Vamp) | Audio chord recognition | GPL (verified) | [GitHub](https://github.com/shidephen/chordino) |
| Essentia | C++ with Python | Audio analysis including chord estimation | AGPL v3 (verified); commercial licence available | [GitHub](https://github.com/MTG/essentia) |
| madmom | Python | Beat and chord recognition | Code BSD, but trained models CC-BY-NC-SA (verified: models are non-commercial) | [GitHub](https://github.com/CPJKU/madmom) |
| Basic Pitch | Python / ONNX | Audio to MIDI notes (not chords) | Apache 2.0 (unverified) | [GitHub](https://github.com/spotify/basic-pitch) |

**What this means for Keys.** Keys is C++ and ships a closed binary. The GPL and AGPL libraries
cannot be linked in. Tonal.js is the best source to read and port data tables from (MIT, so the
tables may be reused with the notice). No C++ chord library worth linking was found; Keys' own
`Chords.h` is the equivalent and a rewrite from a dictionary costs a day, not a dependency.

---

## 5. Tools and apps people use

- **Hookpad** (Hooktheory): drag-in piano roll with Roman numerals and a Trends lookup. [hooktheory.com](https://www.hooktheory.com/hookpad)
- **Scaler 3** (Plugin Boutique): mood and genre chord sets, detect, arpeggiation. Covered in `REFERENCES.md`.
- **Captain Plugins** (Mixed In Key): Captain Chords, Melody, Deep, Epic. Suggest chords in a key and play them with a performance layer. [mixedinkey.com](https://mixedinkey.com/captain-plugins/)
- **Cthulhu** (Xfer): chord generator and arp. Covered in `REFERENCES.md`.
- **Chordbot**: type a chord chart, hear a backing band. [chordbot.com](https://www.chordbot.com/)
- **ChordChord**: free web tool, a loop of chords from a key with a drag-and-drop editor. [chordchord.com](https://chordchord.com/)
- **Autochords** and **Chord Prism**: sketch pads that show a key's chords and play them. (unverified details)
- **Ableton Live 12**: Scale Mode and chord-aware MIDI tools; Live 12 added chord and scale effects in its MIDI tools. [ableton.com](https://www.ableton.com/en/live/)
- **Logic Pro** Chord Track and Session Players: chord names on a track drive the Session Players. [apple.com](https://www.apple.com/logic-pro/)
- **iReal Pro**: the fake-book app of the jazz world, with a practice band. [irealpro.com](https://www.irealpro.com/)
- **Chordify**: paste a song URL, get chords by audio recognition. [chordify.net](https://chordify.net/)
- **Odesi** (Mixed In Key / Odesi): MIDI idea generator with chord progression engine (unverified). 
- **Instachord**: a small hardware controller that maps chords to buttons. (unverified)

Nothing notable from 2025 and 2026 was found beyond the above; the market gap I could see is in the
last section.

---

## 6. What Keys has, what it lacks

**Has.** Eight sources covering function (templates, circle of fifths), voice-leading (PLR,
planing, `applyVoiceLeading`), data (Markov chain over 88 progressions, the 355-entry library),
dialect (negative harmony) and a naming and "what could follow" layer. By my reading it is
stronger on pop-function and voice-leading than on jazz harmony, which is where the gaps sit.

**Lacks, ranked by value.** All of these are **proposed and unbuilt**.

1. **Modal interchange source (proposed, unbuilt).** A source that offers the borrowed chords (bVI,
   bVII, iv, bII) for the current key. Cheap (a table per mode, which `ScaleModes.h` nearly
   holds), mood-bearing, and the thing songwriters most often reach for next after diatonic.
2. **Secondary-dominant "approach" chord (proposed, unbuilt).** Given a target card, offer the V7
   (and optionally the tritone sub or the ii-V) that leads into it. Natural to slot into the tray's
   "Could follow" path, and gives a one-click way to add motion.
3. **Reharmonise a melody (proposed, unbuilt).** Take a played or recorded melody (the Take) and
   propose chords that contain the notes under each bar. This is the one method in section 1 Keys
   cannot do, and a mouse-first player would use it. It is also the most work: it needs a scoring
   function and a bar grid.
4. **Chord-scale display (proposed, unbuilt).** Show on the keybed which scale a chord implies and
   which notes are tensions. Nearly free given `ScaleModes.h`; value is teaching and soloing.
5. **Tension curve (proposed, unbuilt).** Score a progression for tension by function and
   distance, and let the generator aim at a shape (rise, fall, arch). A research idea more than a
   known need; do it last.

Source for items 1 and 2: the Chordonomicon and McGill Billboard bigram statistics could also
replace or extend the Markov tables, with no licence friction (section 3).
