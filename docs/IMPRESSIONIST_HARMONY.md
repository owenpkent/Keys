# Impressionist harmony: how Debussy, Ravel, Satie and their heirs find and voice chords

Status: **research and corpus measurement, nothing built.** Written 2026-11-02, a sibling of
`docs/DANCE_HARMONY.md`. Every gap in section 6 is **proposed and unbuilt**.

**Verification.** (verified) means the claim was read on a cited page in this pass. (unverified)
means from memory or a single weak source. The web search tool returned summaries, not page
text, so "verified" here means a search result stated it, not that the page was read end to end.
The sources for pieces are mostly Wikipedia mirrors, teaching blogs and student theses: good for
"what people say the piece does", weak as scholarship. **Note spellings in section 2 are examples
of the technique, not transcriptions of the score**, unless the table says otherwise. Check against
the score before quoting. The modern descendants (section 3) are thin: no source found gave a
reliable chord reading of an Arnalds or Frahm track, and that is stated rather than papered over.

---

## 1. The short version

- Impressionist harmony is **colour first, function second.** A chord is chosen for its sound, so
  7ths, 9ths and added notes stop being tensions that must resolve and become the chord itself.
  The corpus shows this plainly (section 4): Ravel labels 61% of its chords as 7ths, against 25 to
  30% in Mozart and Beethoven.
- The workhorse technique is **planing (parallel motion)**: one interval stack slid through
  different roots. Sometimes it is a shape slid diatonically, sometimes chromatically, so the
  chord quality never changes while the key is ignored.
- **Roots move by thirds, seconds and tritones as readily as by fifths.** In the DCML data,
  fourth/fifth moves are 57% of non-repeating root motion in Mozart, Beethoven and the Beethoven
  quartets, but only 29% in Ravel and 20% in Bartok (section 4).
- **Scales other than major and minor** supply the colour: whole-tone, pentatonic, the church
  modes, and later the octatonic. The chords are whatever those scales stack.
- **Voicing is half of it**: open fifths low, a cluster or a melody high, and the sustain pedal
  blending everything. A clean 5-note shape in the middle of the keyboard is the opposite of the
  sound.
- **The modern heirs keep the colour and drop the harmony.** Neo-classical and film piano is
  mostly a diatonic four-chord loop with an added 9th, a slow tempo and a lot of pedal and room.

---

## 2. Techniques, with concrete examples

All pitch examples are written out in C or the piece's own key for comparison. "Keys numerals"
use Keys' fixed major-scale degree table (`bVI` is always 8 semitones above the tonic).

| Technique | What it is | Example (piece, pitches) | Keys numerals |
| --- | --- | --- | --- |
| **Parallel open fifths and octaves** | Chords of bare fifths (and fourths) moved in parallel; no third, so no major or minor. | Debussy, "La cathédrale engloutie" (Preludes Book 1, 1910): opens on open G and D fifths and rises in parallel fifths outlining a G major pentatonic collection (verified, [Wikipedia](https://en.wikipedia.org/wiki/La_cath%C3%A9drale_engloutie), [blog analysis](https://cecilysebastian.blogspot.com/2011/01)). Commentators link this to organum and bells (an interpretation, not a fact). | none: a fifth is not a numeral. Closest is `Isus2` / `Isus4`. |
| **Planed triads** | Triads of one quality on successive roots, ignoring the key. | Same piece, middle section from about m. 28: a run of parallel triads (verified, same sources). | `I II III` (major, up a tone), `I bVII bVI bV` (down by semitones). Both in the library. |
| **Planed dominant 7ths** | Dominant 7th shape slid diatonically; roots follow the key signature, every chord stays dominant. | "La cathédrale engloutie" about m. 63 climax: planed dominant 7th chords (verified, same sources). Debussy's "Sarabande" (Pour le piano, 1901) is the classical citation for parallel 7th and 9th chords (**unverified in this pass**). | `I7 bVII7 bVI7`, or ascending `I7 II7 III7 #IV7` |
| **Planed 7ths and 9ths, diatonic** | Same stack, diatonic degrees, so quality bends with the scale (maj7 on I and IV, min7 on ii, iii, vi). | Sarabande again (unverified). The pop and gospel form of the same idea is in `docs/DANCE_HARMONY.md` section 3.2. | `IM7 iim7 iiim7 IVM7` |
| **Whole-tone** | Six notes, no half steps, no perfect fifth above the root, so no tonic gravity. Chords are augmented triads and 7th chords with a flat fifth or sharp fifth. | Debussy, "Voiles" (Preludes Book 1): whole-tone throughout apart from a six-bar pentatonic episode from about m. 42 (verified; the key of that episode is disputed, Eb minor pentatonic vs Gb pentatonic, [Wikipedia](https://en.wikipedia.org/wiki/Voiles), [blog](https://themusicsalon.blogspot.mx/2011/12/debussy-voiles.html)). Whole-tone notes from C: C D E F# G# A#. | `I II III #IV` (library "Whole-tone unease" is `I II #IV`), `Iaug` |
| **Pentatonic** | Five notes; stacking them gives open, mutually consonant chords with no tritone. | "Pagodes" (Estampes, 1903): built on pentatonic material from the gamelan Debussy heard at the 1889 Exposition; sources disagree on whether it is Javanese or Balinese and on how many pentatonic sets it uses ([Wikipedia](https://en.wikipedia.org/wiki/Estampes), verified). One teaching source cites the set B C# D# F# G#. "Voiles" B section as above. "La fille aux cheveux de lin" (Preludes Book 1) is the usual pentatonic-melody citation (**unverified here**). | `Isus2`, `Iadd9`, modes `Pentatonic Major/Minor` |
| **Modal** | Dorian (raised 6th), Mixolydian (flat 7), Lydian (raised 4), Phrygian (flat 2). The mode, not a cadence, sets the home. | Dorian and Lydian dominate the film-score heirs (section 3). Debussy's modal use is widely documented; no specific bar was verified here. | `i IV` (Dorian), `I bVII` (Mixolydian), `I II` (Lydian), `i bII` (Phrygian). All in the library. |
| **Quartal and quintal stacks** | Chords built from perfect fourths (or fifths) instead of thirds. Root is ambiguous: any of the notes could be it. | The "So What" chord: stacked fourths with a major third on top, e.g. D G C F A (D and G in the left hand, C F A in the right), an Em11 shape per Wikipedia (verified, [So What chord](https://en.wikipedia.org/wiki/So_What_chord)). Miles Davis reportedly encouraged Bill Evans to use fourths and seconds as well as thirds (same page). Debussy's fifths above are the quintal case. | none. Needs a new chord type. |
| **Added 6ths and 9ths** | A major triad with a 6th or 9th added is a stable colour, not a tension to resolve. | DCML Ravel: `+6` on 79 chords and `9` on 174 chords of 861 (section 4). Satie's "Gymnopedie No. 1" opens on Gmaj7 / Dmaj7 in D (verified, [Pianist Musings](https://pianistmusings.com/2019/06/14/what-makes-gymnopedie-no-1-so-special/)), with F# common to both as an inner pedal. | `Iadd9`, `IM9`, `I6`, `IM7` |
| **Unresolved 7ths to 13ths** | The 7th, 9th, 11th and 13th are heard as the chord's colour, left hanging. | Ravel, "Jeux d'eau" (1901): built on arpeggiated 7th and 9th chords, opens and closes on the major 7th chord, which Orenstein calls privileged (a quote on a Wikipedia mirror, verified; a University of Washington dissertation lists "tonic ninth chord", "successive ninths chord" and "eleventh chord" as analytical topics, [table of contents](https://digital.lib.washington.edu/researchworks/bitstreams/6aaba116-4b59-49f3-b076-19c205828d62/download), verified as a listing only). | `IM7(9)` is DCML's label for the opening: in Keys, `IM9` |
| **Chromatic mediants** | Roots a major or minor third apart with common tones, no functional link. | The corpus shows it (section 4): 17 to 29% of Debussy, Ravel and Bartok root moves are thirds, against 8 to 10% in the baseline. | `I bVI bIII I` (library "Hollywood third chain"), `I III bVI I` |
| **Tritone root motion** | Roots a tritone apart; the dominant and its substitute share a tritone. | Corpus: 4 to 8% of non-repeating moves in the impressionist set, 2 to 3% in the baseline. | `I bV`, `IM7 #IVM7` |
| **Pedal point and ostinato** | A bass note held or repeated while harmony moves above it. | Satie's F# in Gymnopedie No. 1 (above); "Voiles" has a persistent Bb pedal through the pentatonic section (verified, same Wikipedia and blog sources). | needs a slash or pedal concept: see 6.3 |
| **Bitonality and polychords** | Two chords from different keys stacked. | Ravel is the usual citation (Persichetti covers polychords, below). No bar verified here. | none |
| **Sustain pedal blur** | Notes are left ringing so successive chords overlap. | A performance practice, not a harmony: it is why a dissonant planed run sounds like one wash. Frahm's *Felt* piano is muffled with felt over the strings and the pedal thump is audible on the record (reviews, [Textura](https://www.textura.org/reviews/frahm_felt.htm), verified). | Keys has Sustain and Latch already. |
| **Register spacing** | Open fifths or octaves low, a melody or a cluster high, little in the middle. | "La cathédrale engloutie" opens with a wide low fifth under a sparse upper layer (inferred from the sources; **bar-level voicing unverified**). | a voicing pass: see 6.2 |
| **Rootless voicings (Evans)** | Left hand plays only the 3rd and 7th (or a 4-note shape on 3, 5, 7, 9), the bass supplies the root. | Type A 3-5-7-9 and Type B 7-9-3-5 on a minor 7th (a teaching site, [Earl MacDonald](https://www.earlmacdonald.com/jazz-piano-lessons/rootless-left-hand-piano-voicings/), search result only); Evans is credited with popularising them. | needs a voicing pass: see 6.2 |

### Books (verified by catalogue listing unless noted)

| Book | Detail | Use |
| --- | --- | --- |
| Roy Howat, *Debussy in Proportion: A Musical Analysis* | Cambridge University Press, 1983, about 250 pages. About proportion and the golden section in Debussy ([RCS record](https://pure.rcs.ac.uk/en/publications/debussy-in-proportion-a-musical-analysis/)). | **Form, not chord vocabulary.** Do not cite it for voicings. |
| Richard S. Parks, *The Music of Claude Debussy* | Yale University Press, 1989 (a 1990 date appears on some listings), xiv + 366 pages. Described as a deep structural, Schenkerian study ([catalogue](https://ccu.marmot.org/Record/.b1769839x), [bibliography](https://stevepur.com/music/debussy_biblio.html)). | Best scholarly reading of how the pitch structure holds together. |
| Vincent Persichetti, *Twentieth-Century Harmony: Creative Aspects and Practice* | W. W. Norton, 1961, 287 or 288 pages. Covers chords by thirds, fourths and seconds, added-note chords, polychords, and harmonic motion in tonality, polytonality and beyond ([WorldCat](https://search.worldcat.org/oclc/398434), [Norton](https://wwnorton.co.uk/books/9780393095395-twentieth-century-harmony)). | The textbook for **quartal, secondal, added-note and polychord** vocabulary. No search result confirmed its chapter on planing. |
| Stefan Kostka and Matthew Santa, *Materials and Techniques of Post-Tonal Music*, 5th ed. | Routledge, 2018, 338 pages. First edition Prentice Hall, 1989 ([Routledge](https://www.routledge.com/Materials-and-Techniques-of-Post-Tonal-Music/Kostka-Santa/p/book/9781138714199)). **Not "Kostka, Payne and Almén": that trio wrote *Tonal Harmony*** (McGraw-Hill), whose last part introduces 20th-century music. | Whole-tone, octatonic, pentatonic and quartal vocabulary in one place. |
| Mark Levine, *The Jazz Piano Book* | Sher Music (publisher and year **unverified**). Voicings, scales, comping ([Wikipedia](https://en.wikipedia.org/wiki/The_Jazz_Piano_Book)). The Type A / Type B labels in a handout cite Levine's *The Jazz Theory Book*, not this one. | Rootless voicings. The A/B names should be checked against the book. |

---

## 3. The modern heirs

The finding here is mostly negative: **no source found gave a reliable chord reading for an Arnalds
or Frahm track**, and the readings found for Einaudi and Hisaishi are one-paragraph tutorials.

| Artist | What was found | Note |
| --- | --- | --- |
| **Einaudi**, "Nuvole Bianche" | Sources disagree on the key. One gives F minor with Fm, Db, Ab, Eb (i, bVI, bIII, bVII) as a looping four-chord pattern ([MusicTales](https://musictales.club/node/560)); another analyses it around Ab; a tutorial calls it a 6-4-1-5 order with one slash chord ([Pianote](https://www.pianote.com/blog/nuvole-bianche-tutorial/)). Fm Db Ab Eb and Ab-based 6-4-1-5 are the **same four chords** with a different home. | The ambiguity is the point: the loop has no strong tonic. Library check: `i bVI bIII bVII` already exists. |
| **Hisaishi**, "One Summer's Day" | A student paper says vi - V (or iii) - IV - I is a signature progression of his, with a stepwise bass or melody descent ([SUNY paper](https://soar.suny.edu/server/api/core/bitstreams/a33d708d-1d97-47b4-b333-2d1890a29169/content)). Musicnotes lists the piano arrangement in C major at 98 BPM. | `vi V IV I` and `vi iii IV I`. Nothing found for "Merry-go-round of Life". |
| **Nils Frahm** | Felt-damped piano, audible pedal and hammer noise, repeating patterns, and (per one blog on "More") a missing connecting chord. No harmonic analysis. | The voicing and the sound do the work, not the progression. |
| **Olafur Arnalds** | Nothing found. | Do not describe his harmony from this document. |
| **Bill Evans** | The rootless and quartal material in section 2. | |
| Video-game piano | Not searched. | Out of scope for this pass. |

---

## 4. What the DCML corpus says

**Data:** DCML harmony annotations in `datasets/dcml/` (CC BY-NC-SA 4.0, personal use; see
`datasets/README.md`). Script: `scratchpad/an.py`, read the `harmonies/*.tsv` files only, run with
`py -I`, nothing executed from the data. Counts are **chord labels** (one per annotated change),
not duration. A label with no numeral or chord type was skipped. The root-motion interval is
computed on the tonic-relative root plus the local key's offset, so a modulation does not create
a false jump.

**Corpora sizes.** Debussy is `debussy_suite_bergamasque` only: four movements (Prelude, Menuet,
Clair de lune, Passepied), 1,013 labels. Ravel is three pieces (Jeux d'eau, Une barque sur
l'ocean, Alborada del gracioso), 861 labels. Poulenc is three movements, 269 labels. These are
small, so read them as indications. The DCML Debussy repos that carry Preludes and Estampes hold
scores with no annotations (`datasets/README.md`), so **"La cathédrale engloutie", "Voiles" and
"Pagodes" are not in the data at all.**

### 4.1 Chord types and chromaticism (share of labels, %)

| Corpus | Labels | Triads | 7th chords | Aug. 6th | Has 9/11/13 in `changes` | Chromatic numeral (b or # prefix) | Secondary (relative root) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **Debussy** Suite bergamasque | 1,013 | 53.4 | 46.2 | 0.4 | 0.0 | 7.0 | 22.0 |
| **Ravel** piano (3 pieces) | 861 | 38.8 | **61.2** | 0.0 | **25.6** | 12.3 | 13.6 |
| Poulenc Mouvements perpetuels | 269 | 57.2 | 42.8 | 0.0 | 36.4 | 0.0 | 4.5 |
| Bartok Bagatelles | 1,175 | 51.8 | 46.3 | 1.9 | **39.0** | **19.9** | 21.4 |
| Rachmaninoff piano | 1,116 | 65.9 | 31.6 | 2.4 | 9.2 | 14.4 | 31.0 |
| Liszt Pelerinage | 5,037 | 62.7 | 35.8 | 1.5 | 8.9 | 11.2 | 16.5 |
| Medtner Tales | 6,448 | 56.5 | 41.4 | 2.1 | 10.9 | 6.3 | 31.7 |
| Schulhoff Suite dansante en jazz | 488 | **10.5** | **88.3** | 1.2 | **60.2** | 11.1 | **64.8** |
| *Baseline* Mozart piano sonatas | 15,030 | 73.7 | 25.4 | 0.9 | 1.5 | 3.2 | 10.6 |
| *Baseline* Beethoven piano sonatas | 21,680 | 69.9 | 29.3 | 0.8 | 3.1 | 5.4 | 19.1 |
| *Baseline* ABC (Beethoven quartets) | 28,089 | 67.8 | 31.5 | 0.6 | 1.6 | 4.4 | 13.2 |

Also measured as comparison, not asked: Grieg Lyric Pieces 8,151 labels, 41.6% 7ths, 11.4%
extended, 5.7% chromatic; Chopin Mazurkas 8,993, 39.6%, 8.8%, 4.1%; Schumann Kinderszenen 919,
34.3%, 6.1%, 3.2%. The Romantic piano repertoire sits **between** the Classical baseline and the
impressionists on 7ths but near the baseline on extensions and chromaticism.

**Reading.**

- **7ths:** Ravel 61% and Schulhoff 88% against 25 to 31% in the baseline. Debussy's Suite
  bergamasque (46%) is a 1890 piece and sits closer to Grieg and Medtner than to Ravel, which is
  not a surprise: it is Debussy before the Preludes.
- **Extensions (9/11/13 in the `changes` column):** 25.6% Ravel, 39% Bartok, 36% Poulenc, 60%
  Schulhoff, against 1.5 to 3.1% in the baseline. This is the clearest separator in the data.
  Debussy's 0.0 is the **annotation convention, not the music**: the Suite bergamasque labels
  record added 2nds (`+2`, 75 times), `2` (41), `+6` (37) and `4` (30) as added tones, not 9ths.
- **Added notes:** Ravel's top `changes` are `9` (174), `+6` (79), `b5` (43), `+#4` (34), `+#6`
  (27), `+4` (25). Bartok's are `9` (114), `b9` (64), `11` (53), `#11` (39), `4`, `13`. The
  baseline's are inversions and suspensions (`64` 846 to 889 times in Mozart and the quartets).
- **Chromatic numerals:** 12 to 20% in Ravel, Bartok, Rachmaninoff and Liszt, against 3 to 5% in
  Mozart and Beethoven. Debussy's Suite is 7.0%.
- **Schulhoff** is a jazz suite and behaves like jazz: 88% 7th chords, 328 of 488 labels are plain
  dominant 7ths, 65% secondary. It is a calibration point for "what jazz looks like in this
  format", not an impressionist.

### 4.2 Root motion (share of consecutive label pairs, %)

"Same root" is a different label on the same root: an inversion, an added note, a quality
change. It is often a colour change on a held root, which is itself an impressionist signature.
The last three columns are **of the non-repeating moves only**, rounded.

| Corpus | Pairs | Same root | Fifth/fourth | Third (m3, M3, m6, M6) | Tritone | Second | Of non-repeating: 4th/5th | Third | Tritone |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Debussy Suite | 1,009 | 24.3 | 32.9 | 18.4 | 4.7 | 19.7 | 43 | 24 | 6 |
| **Ravel** | 849 | **39.6** | 17.7 | 17.4 | 3.9 | 21.4 | 29 | 29 | 6 |
| Poulenc | 257 | 13.2 | 61.5 | 5.4 | 0.0 | 19.8 | 71 | 6 | 0 |
| **Bartok** | 1,148 | 13.8 | **17.1** | **28.5** | 6.7 | 34.0 | 20 | 33 | 8 |
| Rachmaninoff | 1,076 | 10.9 | 33.0 | 22.7 | 4.5 | 29.0 | 37 | 26 | 5 |
| Liszt | 4,754 | 32.6 | 31.5 | 14.6 | 3.1 | 18.2 | 47 | 22 | 5 |
| Medtner | 6,130 | 25.1 | 37.1 | 12.0 | 3.9 | 21.8 | 50 | 16 | 5 |
| Schulhoff | 481 | 7.5 | 22.7 | 23.3 | 6.7 | 39.9 | 25 | 25 | 7 |
| *Mozart* | 14,692 | 20.9 | 44.9 | 8.3 | 2.3 | 23.7 | 57 | 10 | 3 |
| *Beethoven sonatas* | 21,084 | 26.9 | 39.9 | 8.9 | 2.4 | 21.9 | 55 | 12 | 3 |
| *ABC* | 27,175 | 24.9 | 42.8 | 10.3 | 2.2 | 19.7 | 57 | 14 | 3 |

**Reading.** Fifths and fourths fall from 55 to 57% of real moves in the baseline to 29% in Ravel
and 20% in Bartok. Thirds roughly triple (10 to 14% to 29 to 33%), and tritones double (3% to
6 to 8%). Ravel's 40% same-root is the "colour change on a held chord" fingerprint. Poulenc is the
outlier in the other direction (71% fifths, 6% thirds): it is neo-classical, a reminder that
"early 20th century French" does not mean impressionist harmony.

### 4.3 Most common bigrams (Keys-style: numeral and DCML chord type, repeats of one exact label skipped)

| Corpus | Top bigrams (count) |
| --- | --- |
| Debussy | V Mm7 to V Mm7 (80), i m to i m (24), V Mm7 to i m (20), V Mm7 to I M (18), V Mm7 to III M (17) |
| Ravel | V Mm7 to V Mm7 (93), i mm7 to i mm7 (87), I M to I M (51), I Mm7 to I Mm7 (26), I MM7 to IV MM7 (10) |
| Bartok | I M to I M (36), V Mm7 to V Mm7 (24), V M to V M (20), vi m to IV M (9), I M to vii o (9) |
| Schulhoff | V Mm7 to V Mm7 (188), I MM7 to V Mm7 (20), V Mm7 to I MM7 (18), bIV Mm7 to V Mm7 (13), V Mm7 to ii %7 (12) |
| Mozart | V Mm7 to I M (1,561), I M to V Mm7 (944), V M to V Mm7 (755) |
| ABC | V Mm7 to I M (2,538), I M to V Mm7 (1,718), V M to V Mm7 (1,391) |

**Reading.** The baseline's top bigram is the dominant 7th resolving to tonic. In the impressionists
the top bigrams are **the same chord to itself with a different label** (Ravel's `V Mm7` to
`V Mm7` 93 times, `i mm7` to `i mm7` 87 times). That is dwelling on one sonority while the
voicing or added tone changes, not progressing. `I MM7 to IV MM7` (the Satie / Ravel major-7th
alternation) is the one clear two-chord colour pair, at 10 hits.

### 4.4 What DCML labels cannot see

- **Planing is invisible as such.** A planed run appears as successive labels (`V Mm7 to III M`
  in Debussy) with nothing marking that the shape stayed constant. A planing detector would have
  to compare the `chord_tones` columns, not the labels. **Not done here.**
- **Whole-tone and pentatonic passages are often unlabelled or forced into a function.** The
  columns `chord_type` and `numeral` assume a tertian chord. Missing rows are dropped by this
  analysis, so a long whole-tone passage is simply absent, which biases impressionist figures
  *toward* the tonal moments.
- **Open fifths and quartal stacks have no label.** A bare fifth is read as a triad with a
  missing third, if at all.
- **The annotators are conventionalising.** Added tones are logged as `+6` or `9` on a triad
  or 7th. That is accurate to the voice-leading but it is the *annotator's* chord, not the
  composer's colour-first intent.
- **Tiny samples.** 1,013 and 861 labels for Debussy and Ravel; one early Debussy suite; Ravel
  three pieces. They suggest, they do not establish.
- **The unannotated majority.** The Preludes, Images, Estampes, Gymnopedies and Gnossiennes are
  not in DCML. A proper study would need another source (for example a MusicXML analysis).

---

## 5. What Keys already has

Mapped by reading `src/`, 2026-11-02.

| Need | Keys today | Status |
| --- | --- | --- |
| Church modes | `ScaleModes.h` has Ionian, Lydian, Mixolydian, Aeolian, Dorian, Phrygian, Locrian, Harmonic and Melodic Minor, Blues, plus Pentatonic Major and Minor. | **Covered** for modal and pentatonic. |
| Whole-tone and octatonic modes | **Absent from the generator.** `ScaleModes.h` states that Whole Tone and Chromatic exist as kit scales but have no per-degree chord quality, so the generator cannot express them. Octatonic is in neither list. | **Gap** |
| add9, add6, 6/9, maj9, sus | `ChordGen.h` `types()` has 20 types including Sus2, Sus4, Major 6th, Minor 6th, Add9, Minor Add9, Major 9th, Minor 9th, Dominant 9th, 6/9. | **Covered** for add9, 6th, 6/9, sus, maj9. |
| 11th, 13th, maj7#11, quartal, quintal, cluster | **None.** No type has an 11th or 13th, no sharp-11, and no stack of fourths. The largest chord is 5 notes. | **Gap** |
| Planing, diatonic and chromatic | `planing()` in `ChordSources.h`, selected as genSource "Planing". **Seven shapes only**: Major, Minor, Sus2, Sus4, Major 7th, Minor 7th, Dominant 7th. No 9th, add9 or fifth-only shape. | **Partial** (the same gap `DANCE_HARMONY.md` section 7 raised) |
| Chromatic mediants | Neo-Riemannian PLR source (`neoRiemannian`) covers parsimonious voice-leading; the library has "Chromatic mediant down" and "Hollywood third chain". | **Covered** (PLR's own moves are the one-semitone cases; third-cycles are in the library) |
| Negative harmony, circle of fifths, Markov, library | Present: eight sources (`Algorithmic`, `Markov`, `Circle of Fifths`, `Neo-Riemannian`, `Progressions`, `Negative Harmony`, `Planing`, `Library`). | n/a |
| Voicing pipeline | `ChordVoicing.h`: Lean (`applyMajorMinorBias`), `fitVoicing` (root position, note count, inversion), `applyVoiceLeading`. No pass for open spacing, rootless shapes, or a held bass note. | **Gap** |
| Pedal point | No slash or pedal-bass concept in a chord card. | **Gap** |
| Library tags | **358 rows** by my parse (docs say 355; the three-row difference is not investigated). Genre tags (41, closed list): **Classical 52, Cinematic 77**, Chillout 29, Downtempo 29, Jazz 52. **No Impressionist, Ambient or Neo-classical genre tag**; Mood tags include Dreamy 31, Atmospheric 37, Contemplative 17, Mysterious 34, Beautiful 41. Rows already impressionist in intent: "Impressionist wash (IM9-bVIIM9)", "Planing minor triads (Debussy)" (`i bii biii`), "Whole-tone unease (I-II-#IV)", "Planing major triads up a tone", "Planing major triads down a semitone", "Lydian lift (IM7-IIM7)", "Ambient suspension (Isus2-IVsus2)", "Ambient minor suspension (imadd9-bVIM9)". | **Partial**: about 8 rows by name, no genre handle. |

---

## 6. Gaps, ranked (all **proposed and unbuilt**)

Ranking is by how much impressionist sound each unlocks, divided by how much of the existing
machinery has to change. The append-only rules apply to everything that touches `types()`,
`genSource` or `genres()`.

1. **Extended chord types: 11th, 13th, maj7#11, 7sus4, quartal and quintal stacks.**
   Append to `chordgen::types()` (saved pads store the index, so append only; `StateTests` pins
   the list). Suggested additions, in semitones from the root: `Quartal 3` {0, 5, 10},
   `Quartal 4` {0, 5, 10, 15}, `Quintal 3` {0, 7, 14}, `Major 7#11` {0, 4, 7, 11, 18},
   `Minor 11` {0, 3, 7, 10, 14, 17}, `Dominant 13` {0, 4, 10, 14, 21}, `So What` {0, 5, 10, 15, 19}
   (stacked fourths and a major third on top). Why first: this is a table edit, and it feeds every
   source that picks a type (the weighted pool, planing, the library renderer). The cost is
   naming: `Chords.h` has to recognise the new shapes on a played chord, and a quartal stack has no
   unique root.
2. **A planing source that takes the extended shapes, plus a "constant structure" mode.**
   `planing()` picks from seven fixed shapes. Add the new types (the `docs/DANCE_HARMONY.md`
   section 7 gap, same fix: Minor 9, Major 9, Add9, and now Quartal and Quintal) and offer a
   Planing option that is **fifths only** (the "cathedrale" sound). The comment on
   `planing()` in `ChordSources.h` already treats chromatic planing as Debussy-style, so the
   design intent exists; the shape list is the only limit.
3. **An open-spread voicing pass.** A fourth pass in `chordvoicing::applyVoicingPipeline`,
   before voice leading: put the root and fifth an octave or two low, leave a gap, and put the
   upper structure (9th, 3rd, 7th) in the middle register. A second mode drops the root for the
   Evans shapes (3-5-7-9, 7-9-3-5). This is the one that changes how every existing chord sounds
   and needs the most listening tests. It must slot **after** `fitVoicing` and **before**
   `applyVoiceLeading`, per the ordering rule recorded in `ChordVoicing.h`.
4. **Whole-tone and octatonic as scales the generator can express.** Today the generator cannot
   because it assigns a chord quality to each degree. The fix is to add a mode with
   per-degree qualities that are all `augmented` (whole-tone, 6 degrees) or alternate
   diminished and minor/major (octatonic, 8 degrees). The mode count is hard-coded in places
   (`kBlues = 9` in `ChordLibrary.h`), so append, do not insert. Moderate cost, narrow benefit:
   whole-tone chords are mostly augmented triads and 7(b5) shapes.
5. **A pedal-bass option on a chord card.** Hold the tonic (or a chosen note) in the bass while
   the upper chord changes. Needs a slash-bass concept and a decision about the `noteRefs`
   refcount (a held bass is one owner across several chords). This is the largest change and the
   one with design questions; do not start until 1 to 3 are heard.
6. **A genre or mood handle.** Append "Impressionist" and "Ambient" to `chordlib::genres()` (an
   append-only closed list; the order test must be extended) so the existing rows and new rows are
   findable. Cheap, but only worth doing with the rows below.

### 6.1 Library rows worth adding (Keys numerals, the fixed major-scale table)

These are **proposals for the table**, written in Keys' convention. Compare with the library before
adding: several close cousins exist (see the left column of section 5). Chord names that the
library already has as bare numerals (`im7`, `IM9`, `Iadd9`, `Isus2`) are available as-is; **a
numeral using a 11th, 13th or quartal stack needs gap 1 first.**

| Name | Numerals | Mode | Tags (mood / genre) | Basis |
| --- | --- | --- | --- | --- |
| Gymnopedie alternation | `IVM7 IM7` | Ionian | Contemplative, Tender / Classical, Chillout | Gmaj7 to Dmaj7 in D (verified, Pianist Musings) |
| Diatonic planed 7ths, ascending | `IM7 iim7 iiim7 IVM7` | Ionian | Dreamy, Beautiful / Classical | Sarabande-type diatonic parallel 7ths (unverified for the piece) |
| Planed dominants, down | `I7 bVII7 bVI7 bVII7` | Mixolydian | Atmospheric, Dramatic / Classical, Cinematic | "La cathedrale" climax planed dominants (verified in outline; the root choice here is illustrative) |
| Whole-tone dominant planing | `I7 II7 III7 #IV7` | Ionian | Mysterious, Eerie / Classical | Whole-tone roots; the "Voiles" territory |
| Major-third cycle (chromatic mediants) | `IM7 IIIM7 bVIM7 IM7` | Ionian | Epic, Beautiful / Cinematic | Third-cycle; roots C, E, Ab |
| Minor third drift | `im7 bIIIM7 bVIM7 bVII` | Aeolian | Haunting, Melancholic / Cinematic | Common film-score move |
| Tritone shadow | `IM9 #IVM9` | Lydian | Eerie, Dreamy / Cinematic, Classical | Tritone colour pair |
| Ravel tonic-ninth dwell | `IM9 IVM9 IM9 bVIIM9` | Ionian | Dreamy, Nostalgic / Classical | Opening of "Jeux d'eau" is a tonic ninth (verified via listing) |
| Hisaishi signature | `vi V IV I` and `vi iii IV I` | Ionian | Nostalgic, Hopeful / Cinematic | One student paper (verified) |
| Quintal wash | `Isus2 V IVsus2 I` | Ionian | Calm, Contemplative / Chillout | Fifth-heavy colours; needs quintal types to be exact |
| Quartal Dorian vamp | `i IV` as quartal stacks | Dorian | Smooth, Mysterious / Jazz | "So What" is D Dorian over D then Eb Dorian a semitone up (**unverified in this pass**) |

Everything marked "unverified" in the Basis column should not be quoted as a source claim, only
as a plausible row. The library's own header says its rows are "written out" from canon, not
ranked against a corpus (`datasets/README.md`).

---

## 7. Open questions

- Are the 3 labelled Ravel pieces and 4 Debussy movements representative? Almost certainly not
  for Debussy (the Suite is early). A second annotated source for the Preludes is the real fix.
- Does a **planing detector** on `chord_tones` change the picture? It could measure how often
  consecutive labels share an interval stack, which is the one thing section 4 cannot see.
- Should Keys' "Planing" source offer fifths-only and quartal shapes, or should those live as a
  separate eighth-and-a-half source? The `genSource` list is append-only, so the cheapest route
  is more shapes inside Planing.
- The library count: 355 in the docs, 358 by my regex. Not investigated.
- Video-game piano was not researched in this pass.
