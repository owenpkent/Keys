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

1. **Extended shapes in planing (proposed, unbuilt).** Add Minor 9, Minor 11, Major 9 and add9
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
