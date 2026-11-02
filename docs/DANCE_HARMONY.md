# Dance harmony: how house and its family find their chords

Status: **research, nothing built.** Written 2026-11-02 after Owen asked for house and dance
chord practice in depth. It extends `docs/CHORD_LANDSCAPE.md` (the general survey) and reads the
library described in `docs/CHORD_LIBRARY.md`. Every gap in section 7 is **proposed and unbuilt**.

**Verification.** (verified) means the claim was read on the cited page in this pass. (unverified)
means from memory or a single weak source. Several of the web sources are producer blogs and
sample-pack pages: good for what producers say they do, weak as music theory. Track examples
that are not tied to a cited page are marked unverified, and every chord reading of a track is
from a user-submitted tab or a blog unless noted. Check against the record before quoting.

---

## 1. The short version

- Dance harmony is mostly **one to four chords, looped for minutes**. The interest is voicing,
  rhythm, filtering and register, not changes. A progression has to survive being heard sixty
  times.
- The default colour is **minor 7, then minor 9 or 11**, over a bass that holds the root.
  Mixed In Key states that most house, whatever the subgenre, is written in a minor key
  ([Mixed In Key](https://mixedinkey.com/captain-plugins/music-composition-plugins/), verified
  as a statement on that page; it is guidance on key, not a chord set).
- The **rhythm is half the harmony**: offbeat stabs, a held pad, a one-bar syncopated pattern.
  The same chord reads as deep house or trance depending on the gate.
- The **hand finds the chord before theory names it**. Chord-memory synths and plug-ins exist
  because a house producer often plays one shape and slides it.

---

## 2. What Keys already holds for dance (library counts)

`src/ChordLibrary.h` holds **355** progressions. Counts below are by genre tag (one progression
carries up to three tags, so rows overlap). Counted with a script against the file, 2026-11-02.

| Genre tag | Count | | Genre tag | Count |
| --- | --- | --- | --- | --- |
| House | 10 | | Minimal | 13 |
| Deep House | 6 | | Electronica | 26 |
| Progressive House | 7 | | Chillout | 29 |
| Trance | 9 | | Downtempo | 29 |
| Techno | 6 | | Lo-fi | 15 |
| EDM | 12 | | Synthwave | 9 |
| Future Bass | 5 | | Slaphouse | 2 |
| Disco | 7 | | Drum & Bass | 3 |
| **Garage** | **0 (no such tag exists)** | | | |

The tag list is closed at 41 genres in `chordlib::genres()`. There is no Garage, Tropical, Afro
House, Amapiano, Acid, Italo or Piano House tag.

**The dedicated house, trance, techno, DnB and future bass rows** (about 30 progressions; the
rest of each count is general canon such as Axis that carries a dance tag). Roman numerals as stored:

- House: `im7 bVIM7 bIIIM7 bVIIM7` (House classic); `IVM7 V iiim7 vim7` (uplift); `im7 bVIM7 bVIIM7 im7`
  (turnaround); Disco loop `iim7 V7 IM7 vim7`; Disco minor loop `im7 bVIIM7 bVIM7 V7`; Disco rise
  `bVIM7 bVIIM7 IM7`.
- Deep house: `im9 IVM9 bVIM9 bVIIM9` (pad, Dorian); `im9 IV9` (two-chord vamp); `im7 bVIIM7` (minor lift).
- Progressive and trance: `i bVI bIII bVII` (build); `IVM7 IM7 vim7 V` (wash); `vi IV I V` (anthem);
  `i bVII bVI bVII` (breakdown); `i IV bVII IV i` (Dorian anthem vamp); `i bVII bVI V` (EDM drop).
- Techno and minimal: `i bII` (Phrygian cell); `i bVI` (minor stab); `im9 im9` (one-chord);
  `i bVII bII i` and `i bIII bII i` (Phrygian).
- Future bass: `IVM9 IM9 vim9 iim9`; `IVM9 V iiim7 vim9`; `IVadd9 Isus2`.
- DnB: `im7 bIIIM7 bVIIM7 ivm7` (minor roll); `im9 bVIM9 bIIIM9 ivm9` (liquid); `im7 bVII7` (jungle stab).
- Slaphouse: `i bVI bVII` and `bVI bVII i`.

**Reading.** The library already has the right *family* (minor 7/9 loops, Dorian vamps, Phrygian
cells). Thin spots: Disco and House have no tracked-from-record entries, Garage and Amapiano do
not exist, Drum & Bass has three, and nothing is tagged for chord *rhythm* (the library stores
chords, not stab patterns).

---

## 3. How producers actually find chords, by technique

### 3.1 Chord memory and one-finger chords

Press one key, get a stored shape. This is how a lot of classic house was played, and it is
why house chords are often *one shape slid around*.

- **Korg Poly-800** has a Chord Memory function (verified as a feature mention on
  [SynthMania](https://synthmania.com/poly-800.htm) and others). A vendor page for a Juno-106
  emulation states that the chord memory "produces those classic house chord stabs"
  ([Rekkerd](https://rekkerd.org/glowstix-64-retro-techno-and-house-sounds-for-the-dco-106-synthesizer/),
  verified as a vendor claim about the hardware's reputation, not a producer interview).
- A search for MS-20 and Ensoniq chord stabs found nothing usable. **The MS-20 and Ensoniq
  chord-memory claim is unverified.** What is documented: the string stabs on Rhythim Is Rhythim's
  "Strings of Life" came from an Ensoniq Mirage sampler
  ([Wikipedia](https://en.wikipedia.org/wiki/Strings_of_Life), verified via search result).
- **Korg M1 Organ 2** is the founding house organ sound; the preset is cited as I17 in most
  sources, and "Show Me Love" (Robin S, 1990) is the standard example
  ([MusicRadar](https://www.musicradar.com/how-to/classic-house-organ-robin-s-show-me-love),
  verified via search result). The M1 "Piano 16'" is the common house piano of the same era
  (verified for the era; not confirmed for any one track).
- Modern equivalents: Ableton Live 12's Chord device is scale aware, so shapes can be set as
  scale degrees; a Sound on Sound article describes a "chord memorizer" device for Live
  ([Sound on Sound](https://www.soundonsound.com/node/4920680), verified via search result;
  confirm it still runs in Live 12).

### 3.2 Parallel shapes (planing)

The same interval stack on every root. In dance music this is the tool that makes four bars from
one hand shape: Cm9 to Fm9 to Gm9 with no thought for key. Attack Magazine's deep house article
starts from minor 7 and adds a 9th and/or 11th on top
([Attack](https://www.attackmagazine.com/technique/passing-notes/passing-notes-deep-house-chords/),
verified). What Keys' planing does today is in section 7.

### 3.3 Voicings: tight, close, one common tone

The Attack article's rules, which are the clearest verified statement of house voicing:

- Keep the **7th of every chord in**; it is the tone that makes the loop "deep".
- Move a 7th **down an octave** to keep a voicing tight; mute inessential notes so the mix is clear.
- The **lowest note decides the chord's name**: C Eb G Bb is Cm7 over a C bass but reads as Eb add6
  over an Eb bass.
- The **top note is the melody**; shape the top line (their final loop descends E, D, C).
- Their deep house worked example is Masters at Work "To Be In Love" (MAW Dub): **Fm7, Abmaj7,
  Cm7**. Another loop: Am7, Em7, Dm7. (Both verified on the page.)

### 3.4 Stab voicings with added notes

The Attack "Levelling up your chord stabs" piece
([Attack](https://www.attackmagazine.com/technique/passing-notes/levelling-up-your-chord-stabs/),
verified) is note-level:

- **Add the 4th (11th) for a cluster:** Cm7 gets F between Eb and G, so Eb F G Bb. The tighter the
  cluster, the darker and blurrier the stab.
- **Add the 2nd (9th) for colour:** D on top of Cm7 (Eb G Bb D over C). In Gm, A goes in the middle.
- Base shapes in the article: Cm7 as Eb G Bb over a C bass; Gm in third inversion (F, Ab..., see
  below); Em7 as G B D.
- Vary the rhythm in bar 4 and bar 8 so the loop turns; add a short melody in the gaps.

### 3.5 One-chord and two-chord vamps

The deep house, techno, minimal and garage default. A single minor 9, or two chords a step
apart, does the job and leaves the space for the bass, vocal chop and filter. The library
already has `im9 im9` and `im9 IV9`.

### 3.6 Pedal bass under moving chords

The bass holds the tonic (or a short pattern on it) while the chords move above. The chord on top
can then be a *non-chord of the key*: Fm7 over C, Abmaj7 over C. Roots stay the producer's
concern, not the chord pad's. Attack's note that the lowest note re-names the chord is the same
fact seen from the other side. The inverse is also common in house: the bass **follows the chord
roots** on the offbeat, one note per stab. Both are normal; the point is that the chord part
alone rarely tells you the full harmony.

### 3.7 Stab rhythm

The chord part is a pattern as much as a chord: offbeat eighths ("and" of each beat) for house,
syncopated sixteenths for garage and 2-step, a long held pad for trance, a gated chord for
progressive. A Production Music Live video on stab patterns is the nearest verified lead
([Recapio digest](https://recapio.com/digest/house-stab-patterns-like-a-pro-peggy-gou-southstar-by-production-music-live),
found via search, not opened; unverified).

### 3.8 Borrowed chords, modes and the "uplift"

Uplifting house and trance borrow from the parallel mode: bVI, bVII and iv in minor are the
standard lift. Hooktheory's own Trends analysis of a House-tagged track notes it "uses chords
from parallel modes for contrast and emotion" (verified via search result, user-tagged genre,
so reliability is mixed). The Dorian IV (major IV over a minor tonic: Am7 to Dmaj7, or Cm7 to F7)
is the one most worth knowing for deep house and techno.

---

## 4. By subgenre

Chord readings are from tabs and blogs; treat them as starting points.

| Subgenre | What the chords do | Voicing, register, rhythm | Examples |
| --- | --- | --- | --- |
| **Chicago / classic house** | Minor-key piano riffs, three to four chords, often i-iv-v. | Piano and M1 organ stabs, mid register, spread. | Marshall Jefferson, "Move Your Body": E minor, with sites listing Em, Am, Bm ([Wikipedia](https://en.wikipedia.org/wiki/Move_Your_Body_(Marshall_Jefferson_song)); key and chord set are tab-site claims, sources disagree). "Show Me Love" (Robin S) for the organ. |
| **Deep house** | Minor 7 loops that descend or ride a pedal; 9th and 11th on top; Dorian IV. | Tight voicings, 7th kept, soft pad plus Rhodes, offbeat stab. | MAW "To Be In Love" Fm7, Abmaj7, Cm7 (Attack, verified). An A minor loop Am7, Dmaj7, E9, Am7 from one blog ([D-Lish](https://dlishdance.com/blog/deep-house-chord-progressions), weak source). |
| **UK garage / 2-step** | Minor 7/9 with a jazz ii-V flavour; sus chords; organ and Rhodes stabs. | Short syncopated stabs on swung sixteenths. | "Jazzy minor walk" im7, iim7b5, V7, im7 (Cm7, Dm7b5, G7, Cm7) as a producer-guide pattern ([BeatKey](https://beatkey.pages.dev/how-to-make-uk-garage-music), weak, tied there to Artful Dodger style). Nothing citable for speed garage. |
| **French / filter house** | Sampled disco chords, often major 7 and 9, a four-bar loop. | Looped through a filter and a compressor; the filter sweep is the arrangement. | Daft Punk "Something About Us": tabs give Bbmaj7, Am7, Dm9, G7 with others (user-submitted tab site, unverified). "Digital Love" A major, tabs list D, C#m, F#, E (unverified). Da Funk not researched. |
| **Detroit techno** | Often a few minor chords; strings and piano riffs. | Sampled string stabs; "Strings of Life" built on a Michael James piano riff, with Ensoniq Mirage strings ([Wikipedia](https://en.wikipedia.org/wiki/Strings_of_Life), verified via search result). Chord content itself unverified. | Derrick May, Rhythim Is Rhythim. |
| **Dub techno** | One chord, echoed. | A minor chord or minor 7th, long delay, sidechain; Attack rebuilds a Basic Channel chord from a sample pitched to D# minor ([Attack](https://www.attackmagazine.com/technique/beat-dissected/basic-channel-style-dub-techno/), verified via search result). | Basic Channel. |
| **Acid** | Not researched; the 303 line carries the harmony. | Mostly a single-line riff over a drone. | (unverified: no source found for mode or chords) |
| **Trance / progressive** | Minor key loops with the anthemic vi-IV-I-V family, sus and add9 stacks, long pads. | Arpeggiated or gated supersaw; 16th arpeggio per chord. | Table below. |
| **Piano house / Italo** | Piano-led, often a rolling stab in minor, 7ths implied. | Mid register piano stabs; chord changes every bar or two. | Black Box "Ride On Time" (unverified, from memory). |
| **90s rave stabs** | Single big chord stabs, often major or minor triads with a fifth or octave. | Detuned sawtooth stab, hard envelope. | The Prodigy, Rotterdam hardcore (unverified, no source searched). |
| **Tech house / minimal** | One chord, or two a step apart. | Short gated stab, mostly sub-register chord thrown into delay. | `im9 im9` already in the library. Peggy Gou-style stab patterns (unverified lead above). |
| **Melodic techno / afro house** | Minor key, 3 to 4 chord loop, pad plus pluck. | Wide pad with a melodic top line. | Nothing sourced in this pass (unverified). |
| **Future bass** | Major 7 and 9 chords, big supersaw, IVM9-IM9 type loops. | Detuned supersaw, 7 to 9 voices, sidechained. | Generic loop Imaj7, Vmaj7, vim7, IVmaj7 from a guide ([Unison](https://unison.audio/future-bass-chord-progressions/), weak); Hooktheory lists Flume tracks without a retrieved analysis. |
| **Tropical house** | Not researched. | Marimba, plucks over a major loop. | (unverified, no source found) |
| **Disco / nu-disco** | Minor 7 to dominant 9 moves, circle-of-fifths walks; the Library's `iim7 V7 IM7 vim7` is the type. | Strings, Rhodes, funky guitar, offbeat. | Library tags already cover it. |
| **Drum & bass / liquid** | Lush minor 9 and major 9, a Dm9, Gm7, Bbmaj9, Cmaj7 type loop. | Rhodes or electric piano; long chords over a break. | A liquid DnB guide and a MusicRadar tutorial which ends the minor loop on E major in A minor ([MusicRadar](https://musicradar.com/how-to/how-to-create-uplifting-liquid-dnb-chords), verified via search result). LTJ Bukem described as jazz-voiced Rhodes (unverified for chords). |
| **Lo-fi house** | Detuned jazz chords (maj7, m9) with tape wobble. | Soft, behind the beat. | Not researched; `Lo-fi` has 15 library tags. |
| **Amapiano** | Extended minor chords (m7, m9, m11) on Rhodes-style keys; two to four chords; the log drum leads. | Voicings and inversions provide the movement. | Examples quoted by one guide: C#m, F#m, B, E; A minor Am, F, G, Em (blog sources; [Roland](https://articles.roland.com/?p=67237) notes the extended ii-V-I form is jazz, not typical). |

### Trance progressions and tracks (one source, secondary)

A Unison article ([unison.audio](https://unison.audio/trance-chord-progressions/), verified as
its claims, weak as analysis) pairs these with tracks. Letters are the author's, not checked
against the records.

| Progression | Track named |
| --- | --- |
| vi-IV-I-V | "Till The Sky Falls Down", Dash Berlin |
| I-V-vi-IV | "Sun & Moon", Above & Beyond |
| IV-I-V-vi | "Clarity", Zedd feat. Foxes |
| vi-V-IV-III | "My Reflection", Andrew Rayel |
| I-vi-IV-V | "Anahera", Ferry Corsten |
| vi-ii-V-I | "Strobe", deadmau5 |
| I-IV-vi-V | "Destiny", Markus Schulz |

Note `vi V IV III` against a minor key is `i bVII bVI V` in Keys' convention, which is already
a library row ("EDM drop").

---

## 5. Books, courses and articles

| Source | What it is | Status |
| --- | --- | --- |
| *Dance Music Manual*, Rick Snoman (Focal Press / Routledge; 5th ed. listed as September 2024) | The standard dance production manual; sound design, arrangement, genre chapters. Harmony is a part, not the focus. | Verified: author, title, edition and publisher on retailer and Routledge listings ([Routledge](https://www.routledge.com/Dance-Music-Manual/Snoman/p/book/9781032646787)). Chapter content unverified. |
| *Music Theory for Electronic Music Producers*, Dr. J. Anthony Allen (Slam Academy, 2018) | Theory taught in the piano roll: harmony, chord progressions and song structure in the MIDI grid. A reviewer wanted more genre-specific examples. | Verified from retailer and a [Cycling '74 interview](https://cycling74.com/articles/an-interview-with-j-anthony-allen). |
| *Making Music: 74 Creative Strategies for Electronic Music Producers*, Dennis DeSantis (Ableton, 2015) | Process and workflow strategies. Not a harmony book. | Verified ([Ableton](https://www.ableton.com/en/press/press-archive/ableton-publishes-making-music-book)). |
| Attack Magazine, "Passing Notes": [deep house chords](https://www.attackmagazine.com/technique/passing-notes/passing-notes-deep-house-chords/), [levelling up your chord stabs](https://www.attackmagazine.com/technique/passing-notes/levelling-up-your-chord-stabs/), [house piano chords with EZKeys](https://www.attackmagazine.com/technique/tutorials/creating-house-piano-chords-with-toontracks-ezkeys/) | The best voicing-level material found. | Verified (first two opened; EZKeys not opened). |
| MusicRadar, [Chicago house spread piano chords](https://www.musicradar.com/how-to/how-to-create-classic-chicago-house-spread-piano-chords) and [Korg M1 house sounds](https://www.musicradar.com/news/how-to-create-classic-house-sounds-with-the-korg-m1-and-when-to-use-them) | Hands-on how-tos. | Title found; page body would not load, so content unverified. |
| Ableton, [Strike a Chord](https://www.ableton.com/en/blog/chords-theory-racks-push) | Chords, Racks and Push; uses "Strings of Life" as a simple repeated progression. | Verified as search result. |
| Hooktheory Trends ([blog](https://www.hooktheory.com/blog/trends-tool/)) | "What follows what" over ~40,000 songs; genre tags are user-contributed. No per-genre filter was found. | Verified; the genre filter question is unverified. |
| Point Blank | Courses on dance production and theory. | Not searched. Unverified. |
| Simon Reynolds, *Energy Flash*; Bill Brewster and Frank Broughton, *Last Night a DJ Saved My Life* | Cultural histories of rave and DJ culture, not harmony manuals. From memory. | Unverified. |

No "Mark Fisher-style" essay on dance harmony specifically was found. No peer-reviewed
analysis of house harmony turned up; the sources above are practitioner writing.

---

## 6. Tools

| Tool | Dance relevance | Status |
| --- | --- | --- |
| Ableton Live 12 **Chord** MIDI effect | Scale-aware chord shapes by degree; the Live 12.2 "Expressive Chords" grid plays chords from a single key | Verified via the [Scaler forum](https://forum.scalermusic.com/t/export-import-ableton-live-12-2-expressive-chords/20811) thread (beta-period source). |
| Korg Poly-800 / Juno-106 chord memory | The origin of the one-finger house stab. | Section 3.1. |
| Scaler 2 and 3 | Chord sets by mood and genre, a House groove tutorial module; no dedicated EDM chord set confirmed. | Partly verified; already covered in `REFERENCES.md`. |
| Captain Chords (Mixed In Key) | A progressions tab of preset sequences, custom rhythms sorted by genre; marketed genre-neutral. | Verified ([Mixed In Key](https://mixedinkey.com/captain-plugins/how-to-guide/captain-chords/)). |
| Cthulhu (Xfer) | Chord memory and a strum/arp; already studied. | See `REFERENCES.md`; not re-searched. |
| Hookpad | Roman-numeral sketching; genre filters not confirmed. | Unverified. |
| Sample packs | One-shot chord stabs and MIDI chord packs, e.g. a deep house MIDI collection ([Unison](https://unison.audio/product/unison-house-deep-house-midi-chord-collection/)). | Product page exists; contents unverified. |

---

## 7. What this means for Keys

### 7.1 What Keys has, by practice

| Practice | Keys today |
| --- | --- |
| Chord memory, one shape on one key | Chord pads (12 a page) play a stored chord per click. The keybed is single notes. |
| Planing | `planing()` in `src/ChordSources.h`: one shape slid by scale degree (diatonic) or by semitone (chromatic). Its shape vocabulary is **seven**: Major, Minor, Sus2, Sus4, Major 7th, Minor 7th, Dominant 7th. No 9th, 11th or add9 shape, which is the dance shape. |
| Minor 7/9/11, sus, add9 stacks | Chord types exist in the generator's table; library rows use `m9`, `M9`, `add9`, `sus2`. |
| Voicing tight, 7th kept, top note | The voicing pipeline: Lean, `fitVoicing`, `applyVoiceLeading`. Nothing says "keep the 7th" or "cluster up the 9th and 11th". |
| Pedal bass under moving chords | Not present as a source. Chord pads hold a chord, not a chord plus a fixed bass. |
| Offbeat stab rhythm | The four arp lines with Gate, Ratchet, Chance, Offset and Swing lanes can make one; no one-click "House stab" pattern. |
| Looped one and two chord vamps | The library (`im9 im9`, `im9 IV9`). |
| Modes (Dorian, Phrygian) | `ScaleModes.h` and library rows. |
| Human feel | Strum and Humanize on the pads. |

### 7.2 Gaps, ranked (all proposed, unbuilt)

1. **Extended shapes in planing (built 2026-11-02: Major 9th, Minor 9th, Add9, Minor 11th, plus Quartal and Quintal; diatonic planing stacks scale steps so every note stays in key).** Add Minor 9, Minor 11, Major 9 and add9
   to planing's shape list, so one-finger sliding makes Cm9, Dm9, Em9 and so on. Smallest
   change, largest dance payoff; a minor-9 planed diatonically is the "deep house chord"
   in one click. The existing comment warns that a wide chord planed chromatically "turns to mush", so
   the diatonic path needs the rule that these shapes stay tight (the Attack voicing rules).
2. **Pedal / slash-bass option (proposed, unbuilt).** A generator or pad setting that holds a
   chosen bass note (tonic or the first chord's root) under every chord in the set, so
   Fm7, Abmaj7, Cm7 over F is one move. Needed to make the "bass note renames the chord" fact
   playable.
3. **Dance voicing pass (proposed, unbuilt).** A fourth pass in the voicing pipeline: keep the
   7th, drop it an octave to tighten, place the 9th as the top note, option to add the 11th as a
   cluster. All five rules are from the Attack articles.
4. **Stab patterns for the arp lines (proposed, unbuilt).** A small set of named gate presets:
   offbeat eighth, syncopated sixteenth (2-step), long pad, gated trance. The lane machinery
   exists; the gap is a one-click starting point.
5. **Library coverage and tags (proposed, unbuilt).** Add Garage, Amapiano, Afro House, Acid
   and Tropical to `genres()` (an append-only list, so the order test must be extended), then
   the rows in 7.3. Also a Stab or Rhythm tag, since the library stores chords and not feel.
6. **Keybed chord memory (proposed, unbuilt).** Press one key and the stored shape sounds
   transposed, as the Poly-800 did. This is a click-and-drag path on the existing keybed, so it
   passes the one-left-click rule.
7. **Borrowed-chord "uplift" source (proposed, unbuilt).** Already item 1 in `CHORD_LANDSCAPE.md`
   section 6; dance music makes the case stronger because bVI and bVII are the lift.

### 7.3 Progressions worth adding

Keys' convention: roman numerals against the fixed **major-scale** degree table, so natural
minor's flat degrees are `bIII`, `bVI`, `bVII` (`datasets/README.md`, "the trap"). A pack row
written against the minor scale would parse and play wrong. Every row below was translated from
the chords in the cited source. **Check the suffix is in the parser** (`m7b5`, `V9`, `add9`)
before adding; `m7b5` and `V9` were not confirmed. `ChordLibrary.h` is not edited here.

| Name | Numerals | Mode | Tags | Reading and source |
| --- | --- | --- | --- | --- |
| Deep house, To Be In Love | `im7 bIIIM7 vm7` | Aeolian | Deep House, House | Fm7, Abmaj7, Cm7 in F minor (Attack, verified). `bIIIM7` is Ab as 3 semitones above F. |
| Deep house descent | `im7 vm7 ivm7` | Dorian | Deep House, Minimal | Am7, Em7, Dm7 in A (Attack, verified). |
| Deep house Dorian IV | `im7 IVM7 V9 im7` | Dorian | Deep House, House | Am7, Dmaj7, E9, Am7 (one blog, weak). |
| Garage minor walk | `im7 iim7b5 V7 im7` | Aeolian | Garage (new), House | Cm7, Dm7b5, G7, Cm7 (guide, weak). Needs `m7b5`. |
| Chicago minor triads | `i iv v` | Aeolian | House | Em, Am, Bm (tab sites; sources disagree). |
| Amapiano, rising | `i iv bVII bIII` | Aeolian | Amapiano (new), Deep House | C#m, F#m, B, E (one blog). |
| Amapiano, descent | `i bVI bVII v` | Aeolian | Amapiano (new) | Am, F, G, Em. Same numerals as the library's "Slaphouse loop" plus `v`. |
| Liquid minor 9 | `im9 ivm7 bVIM9 bVIIM7` | Aeolian | Drum & Bass, Chillout | Dm9, Gm7, Bbmaj9, Cmaj7 (marketing page, weak). Close to the library's liquid row. |
| Future bass wash | `IM7 VM7 vim7 IVM7` | Ionian | Future Bass, EDM | Cmaj7, Gmaj7, Am7, Fmaj7 (guide, weak). |
| French house lift | `bVIM7 vm7 im9 IV7` | Aeolian | House, Disco | Bbmaj7, Am7, Dm9, G7 from "Something About Us" tabs (user tab site). Read with care. |
| Dorian techno stab | `im7 IV7` | Dorian | Techno, Deep House | Cm7, F7. A standard Dorian vamp; the pair is from memory, not a cited track. |
| Dub techno chord | `im7 im7` | Aeolian | Techno, Minimal | One minor 7th, echoed. Dub techno chord content from Attack (verified); the row is a loop, not a record. |

Not added because the library already has them: `vi IV I V` (Trance anthem), `i bVII bVI V`
(EDM drop), `i bII` (Phrygian cell), `im9 IV9` (deep house two-chord).

---

## 8. Open questions

- Per-genre Hooktheory statistics: no filter found. Needs the site opened by hand.
- Primary sources for Chicago, acid and Detroit chord content: nothing found beyond Wikipedia
  summaries. The Michael James riff and Mirage strings are documented; the harmony is not.
- MS-20 and Ensoniq chord-memory claims: unverified.
- Tropical house, afro house, melodic techno, acid, 90s rave and lo-fi house: no usable harmony
  sources in this pass.

---

## 9. What the corpus says (Chordonomicon, 2026-11-02)

**Method.** From `datasets/chordonomicon_v2.csv` (679,807 rows, CC-BY-NC-4.0) I took songs whose `main_genre` is electronic or whose free-text `genres` tag list has a dance tag, matched per tag (so "garage rock" is not garage). Each chord symbol became (root, quality). Key was estimated per song with Krumhansl-Schmuckler templates over chord-tone histograms (root, third, fifth), with the first and last chord counted double so the tonic is favoured; the best of 24 keys decides major or minor. Chords were then written as Keys numerals (fixed major-scale table, so minor's flats are `bIII bVI bVII`). **Loops are triad-level**: 7ths, 9ths, sus and add chords collapse to major, minor, dim or aug, because tabs drop extensions; power chords take the key's diatonic quality. Consecutive repeats are collapsed; a loop is a 2, 3 or 4 chord cycle that occurs twice in a row anywhere in the song, rotation-normalised, **counted once per song**. Percentages are of songs with at least 4 parsable chords. The baseline is a 10% random sample of every non-dance row (66,321 songs). Library comparison treats `ChordLibrary.h` rows as cycles at the same triad level (220 distinct cycles from 355 rows). Full counts: `datasets/derived/chordonomicon_dance_loops.csv`.

### 9.1 Subset sizes (songs; groups overlap)

| Group | Songs | Minor | Borrowed* | Any 7th | 9th or higher |
| --- | --- | --- | --- | --- | --- |
| **Dance core (union of all below)** | **6,913** | 37% | 38% | 28% | 9% |
| electronic (`main_genre`) | 2,814 | 38% | 29% | 22% | 8% |
| house (any house tag) | 2,265 | 41% | 29% | 22% | 7% |
| EDM (incl. electro house, big room) | 1,869 | 40% | 23% | 18% | 7% |
| disco | 1,861 | 28% | 55% | 49% | 15% |
| trance | 638 | 49% | 34% | 18% | 7% |
| eurodance | 573 | 38% | 43% | 21% | 5% |
| garage (UK, speed, house; not rock) | 421 | 34% | 43% | 14% | 4% |
| techno | 385 | 38% | 39% | 22% | 6% |
| progressive house | 334 | 45% | 28% | 22% | 9% |
| dubstep | 238 | 33% | 21% | 24% | 13% |
| drum and bass | 155 | 46% | 41% | 23% | 8% |
| synthwave | 111 | 50% | 45% | 26% | 10% |
| future bass | 107 | 36% | 36% | 47% | 12% |
| tech house | 90 | 36% | 34% | 31% | 6% |
| deep house | 72 | 33% | 18% | 24% | 3% |
| generic "dance pop" (not in core) | 7,597 | 40% | 36% | 24% | 8% |
| **Baseline (non-dance sample)** | 66,321 | 28% | 43% | 36% | 11% |

*Borrowed: a major-key song using bVI, bVII or iv; a minor-key song using IV or V (major). Minor keys are about a third more common in dance than elsewhere (37% vs 28%); trance and synthwave sit near half. Dance tabs carry fewer 7ths than the baseline, which is the guitar-tab filter and the loop-heavy writing both, not evidence that dance is plain. Under 100 songs (deep house, tech house, future bass, synthwave) the tables below are anecdotes.

### 9.2 Dance core, top loops (6,913 songs)

`!` = not in the library as a cycle. "Base" = the baseline's rate for the same cycle.

| 4-chord loop | Commonest start | Songs | % | Base % | |
| --- | --- | --- | --- | --- | --- |
| I V vi IV | vi | 626 | 9.1 | 6.4 | |
| I vi IV V | I | 202 | 2.9 | 3.0 | |
| I IV I V | I | 170 | 2.5 | 5.2 | |
| I vi V IV | IV | 163 | 2.4 | 2.0 | |
| I IV V vi | IV | 116 | 1.7 | 1.0 | |
| I IV vi V | I | 109 | 1.6 | 1.7 | |
| I V IV vi | vi | 102 | 1.5 | 0.9 | ! |
| I V ii IV | ii | 101 | 1.5 | 1.2 | ! |
| i bVII iv bVI (written bVI i bVII iv) | iv | 87 | 1.3 | 0.4 | ! |
| i bVII bVI iv (written bVI iv i bVII) | bVI | 79 | 1.1 | 0.4 | ! |

| 3-chord loop | Start | Songs | % | Base % | |
| --- | --- | --- | --- | --- | --- |
| I V IV | IV | 400 | 5.8 | 8.7 | |
| I IV V | I | 360 | 5.2 | 8.4 | |
| bVI bVII i | i | 262 | 3.8 | 2.1 | |
| I vi IV | vi | 218 | 3.2 | 2.8 | |
| IV V vi | IV | 204 | 3.0 | 2.5 | |
| i bVII bVI (written bVI i bVII) | bVI | 193 | 2.8 | 1.7 | ! |
| IV vi V | IV | 150 | 2.2 | 2.3 | ! |
| I V vi | I | 140 | 2.0 | 1.5 | ! |
| bVI iv i | i | 95 | 1.4 | 0.7 | |
| I vi V | I | 95 | 1.4 | 1.6 | ! |

2-chord: I IV 15.1% (base 25.1), I V 7.8, IV V 5.2, bVI i 4.1, i iv 3.6, I vi 3.0, bVII i 2.6, **bVI bVII 2.3 !**, I bVII 2.3, **V vi 2.3 !**. Library coverage of the top ten per length: 8 of 10 (2-chord), 6 of 10 (3), 6 of 10 (4), so **10 of 30 are missing**. Under a key-agnostic merge (a loop read in its relative major or minor counts as one) the Axis is 10.3% of the core, 12.6% of house, 15.5% of EDM and 13.3% of trance.

### 9.3 Top 4-chord loops by subgenre (% of songs; `!` = missing from library)

| # | House (2,265) | Trance (638) | EDM (1,869) | Techno (385) | Disco (1,861) | Electronic (2,814) |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | I V vi IV 11.2 | I V vi IV 10.8 | I V vi IV 14.2 | I V vi IV 7.0 | I IV I V 4.7 | I V vi IV 11.7 |
| 2 | I vi V IV 2.7 | I vi IV V 2.7 | I vi V IV 3.8 | I vi IV V 3.9 | I V vi IV 4.1 | I vi V IV 3.3 |
| 3 | I IV V vi 2.6 | i bVII bVI iv 2.7 ! | I IV V vi 3.5 | I IV I V 2.1 | I vi IV V 3.1 | I vi IV V 2.9 |
| 4 | I vi IV V 2.2 | bIII bVII i bVI 2.5 | I V IV vi 2.7 ! | I vi V IV 1.8 | I vi ii V 1.5 | I IV V vi 2.3 |
| 5 | i bVII iv bVI 2.1 ! | I V IV vi 2.5 ! | I vi IV V 2.6 | I V IV vi 1.8 ! | I IV V IV 1.3 | I V ii IV 2.0 ! |
| 6 | I V ii IV 2.0 ! | I IV V vi 2.4 | i bVII iv bVI 2.5 ! | I IV V vi 1.6 | I IV I bVII 1.2 | i bVII iv bVI 2.0 ! |
| 7 | I V IV vi 2.0 ! | IV V vi V 2.0 | I IV vi V 2.2 | I V ii IV 1.6 ! | I IV ii V 1.1 | I IV vi V 2.0 |
| 8 | I IV I V 1.9 | I vi V IV 1.9 | I V ii IV 2.1 ! | I IV vi V 1.6 | I V IV V 0.9 | I V IV vi 1.9 ! |
| 9 | I IV vi V 1.8 | i bIII bVI iv 1.9 ! | I iii vi IV 1.7 | I V IV V 1.3 | I vi V IV 0.9 | I IV I V 1.8 |
| 10 | i bVII bVI iv 1.6 ! | i bVII bVI bVII 1.7 | i bVII bVI iv 1.7 ! | I IV I bVII 1.0 | I IV ii v 0.9 ! | i bVII bVI iv 1.5 ! |

Smaller groups (CSV has all): garage leads with I vi IV V 5.0%; drum and bass, future bass, dubstep, synthwave all lead with I V vi IV (9 to 19%). Top 3-chord loop: trance bVI bVII i 9.6% (library has it), DnB i bVII bVI 7.1% (**missing**), disco I IV V 8.0%.

Reading: dance is the Axis first and everything else a distant second. What is *distinctive* against the baseline is the minor-key family around bVI and bVII (bVI bVII i, i bVII bVI, and loops that add iv) at 2 to 6 times the baseline rate, strongest in trance, EDM and progressive house. The rock shape I IV I V is half as common as in the baseline. Disco is the one subgenre that is not Axis-led.

### 9.4 Check against 7.3

Counts are songs in the dance core as written in that key; "Base" is the baseline rate.

| 7.3 row | Core | House | Base | Verdict |
| --- | --- | --- | --- | --- |
| To Be In Love `i bIII v` | 2 | 0 | 27 | Cannot judge. The chords exist (as vi I iii, 1.0% core, 1.2% house vs 0.7%), but the key estimate rarely reads them as minor. |
| Descent `i v iv` | 11 | 3 | 116 | Rare, no dance lift. |
| Dorian IV `i IV V` | 6 | 1 | 45 | Not supported. Also needs 7ths, which tabs drop. |
| Garage `i iidim V` | 0 | 0 | 9 | Cannot judge (m7b5 never tabbed). |
| Chicago `i iv v` | 37 (0.5%) | 15 (0.7%) | 197 (0.3%) | **Supports**, mildly (about 2x in house, 3x in disco). Already in library. |
| Amapiano rising `i iv bVII bIII` | 19 | 8 | 160 | Cannot judge; rate equals baseline. |
| Amapiano descent `i bVI bVII v` | 24 (0.3%) | 11 (0.5%) | 97 (0.1%) | **Supports**, mildly (about 3x). |
| Liquid `i iv bVI bVII` | 28 (0.4%) | 15 (0.7%) | 100 (0.2%) | **Supports**: trance 0.8%, EDM 0.6%. Not in library as a cycle. |
| Future bass wash `I V vi IV` | 626 (9.1%) | 253 (11.2%) | 6.4% | **Strongly supports**; it is the Axis. Note the proposed numerals are the Axis in 7ths, already in the library. |
| French lift `bVI v i IV` | 3 | 0 | 8 | Not supported. |
| Dorian techno `i IV` | 103 (1.5%) | 26 (1.1%) | 800 (1.2%) | Neutral: common, no dance lift. In library. |
| Dub techno `im7 im7` | n/a | n/a | n/a | Cannot judge (a one-chord loop is not a loop here). |

Tally: **4 supported** (Chicago, Amapiano descent, Liquid, Future bass wash), **3 rare or unsupported** (Descent, Dorian IV, French lift), **5 cannot judge or neutral**. Eight of the twelve are already in the library as cycles (Descent, Garage, Chicago, both Amapiano rows, Dorian techno, Future bass wash, Dub techno), so 7.3's own "worth adding" is smaller than it reads: the real gaps are To Be In Love, Dorian IV, Liquid and French lift.

### 9.5 Rows worth adding (Keys numerals)

| Numerals | Evidence | Tags |
| --- | --- | --- |
| `i bVII bVI` | 2.8% core, 4.8% EDM, 6.3% trance, 7.1% DnB (base 1.7%). Descending cousin of the library's `bVI bVII i`. | Trance, EDM, Drum & Bass |
| `i bVII iv bVI` | 1.3% core, 2.1% house, 2.5% EDM, 2.7% trance (base 0.4%), the strongest lift of any missing 4-loop | Trance, EDM, House |
| `i bVII bVI iv` | 1.1% core, 1.6% house, 2.7% trance (base 0.4%) | Trance, Progressive House |
| `i bIII bVI iv` | 1.9% trance, future bass and synthwave (base 0.4%); small samples | Trance, Synthwave, Future Bass |
| `i iv bVI bVII` | Liquid row of 7.3, confirmed 2x baseline | Drum & Bass, EDM |
| `I V IV vi` | 1.5% core, 2.7% EDM, 2.5% trance (base 0.9%) | EDM, Pop |

Check each against `ChordLibrary.h`'s suffix table before entering; none needs a 7th, since the corpus cannot say which 7ths dance uses.

### 9.6 Caveats

- Chordonomicon is scraped from user tabs and is guitar-first. Electronic is under-tagged (2,814 of 679,807 `main_genre` rows are electronic) and the tags are Spotify artist genres, so "house" includes pop artists with a house single. Disco (1,861) is mostly 1970s band songs, not DJ tools, and weighs heavily on the core. Real club tracks are mostly absent.
- Chord extensions are dropped or simplified, so this is evidence about **roots and triads, not voicings**. It cannot test the 7.3 rows that live on m7, M7 or 9 colour.
- Key estimation is the weak link. Relative major and minor are often confused (Am C Em reads as vi I iii), which splits one loop across two numeral spellings; counts above are single-reading and so understate such loops. No song-length weighting exists, and tab transposition is unknown.
- A loop here is a twice-repeated cycle in the chord stream, not a verified hook; sections that merely alternate are counted.
- Licence: CC-BY-NC-4.0. Derived counts are personal-use evidence; any row added from this must be justified from the canon, or re-derived from a permissive source, if Keys is ever sold.
