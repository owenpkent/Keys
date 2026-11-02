# Do the library's mood tags match what emotion-labelled music does?

2026-11-02. A check, not a retag: nothing in `src/` was changed. Raw per-row scores are in `datasets/derived/library_mood_check.csv` (gitignored).

## Method

Chords were extracted from MIDI, so everything below is **automatic chord recognition and noisy**. Each clip is cut into 2-beat segments (4/4 assumed), the duration-weighted pitch classes are matched against 11 templates (major, minor, dim, aug, sus2, sus4, maj7, min7, dom7, m7b5, dim7) with a bonus for the lowest note as root, and repeated chords are collapsed. The key is the Krumhansl-Kessler best fit per clip, then every chord is written as a **Keys-convention numeral** (fixed major-scale degrees, so natural minor shows bIII, bVI, bVII, case carries the quality). VGMIDI labels belong to 4-bar phrases, so the key was fitted on the whole piece and the phrase read against it. Corpora: EMOPIA 1,078 pop piano clips (Q1 250, Q2 265, Q3 253, Q4 310), VGMIDI 204 labelled phrases (Q1 76, Q2 39, Q3 27, Q4 62; quadrant = sign of valence and arousal).

Library rows: 358 parsed from `ChordLibrary.h` (the header says 355) and 88 from `MarkovData.h`. A row's "key mode" is major for Ionian, Lydian, Mixolydian and minor for the rest. Each row is scored per quadrant as (mode log-lift + mean chord log-lift) against EMOPIA, chords counted as (degree, triad class) within the matching key mode. "Valence lift" = mean(Q1, Q4) minus mean(Q2, Q3).

Mood word mapping (valence, arousal), used for everything below:

- Q1: Happy, Triumphant, Energetic, Fun, Playful, Animated, Lighthearted, Uplifting, Funky, Heroic, Joyful, Excited, Empowered.
- Q2: Tense, Intense, Ominous, Suspenseful, Rebellious.
- Q3: Sad, Melancholic, Tragic, Longing, Sombre, Solemn, Haunting.
- Q4: Calm, Chill, Mellow, Dreamy, Peaceful, Relaxed, Tender, Smooth, Romantic.
- Valence only: Hopeful, Beautiful, Resolved, Confident (+); Dark, Eerie, Mysterious (-). Arousal only: Driving, Epic, Dramatic.
- Unmapped: Atmospheric, Cinematic, Contemplative, Inconclusive, Nostalgic, Reflective, Serious, Spiritual.

## Per-quadrant tables

EMOPIA (tokens are recognised chords, about 22 per clip):

| Quad | Major-key | Minor triads | Maj triads | 7ths | Borrowed in major keys* | Top chords, major keys | Top chords, minor keys |
|---|---|---|---|---|---|---|---|
| Q1 happy | 81% | 34% | 48% | 48% | 3.0% | I 19, vi 15, IV 14, V 9 | i 20, bVI 16, iv 10 |
| Q2 tense | 29% | 40% | 42% | 45% | 7.8% | I 17, vi 12, IV 10, ii 8 | i 22, bVI 15, iv 11 |
| Q3 sad | 42% | 37% | 42% | 44% | 4.9% | vi 17, I 16, IV 14, V 8 | i 20, bVI 14, iv 11, bIII 7 |
| Q4 calm | 73% | 33% | 48% | 48% | 2.4% | I 21, IV 15, vi 13, ii 8 | i 18, bVI 18, iv 13, bIII 10 |

*bIII, bVI, bVII or iv, as a share of chords in major-key clips.

VGMIDI (game piano phrases):

| Quad | Phrases | Major-key | Borrowed in major keys | Notes |
|---|---|---|---|---|
| Q1 | 76 | 58% | 10.1% | I 18%, minor-key side leans on bVI, bVII, bIII |
| Q2 | 39 | 28% | 10.6% | dim share 11%, the highest |
| Q3 | 27 | 30% | 16.4% | few chords per phrase, thin |
| Q4 | 62 | 63% | 8.9% | I, IV, Isus |

Pooled by axis, major-key share: EMOPIA valence+ 76% vs valence- 35%, arousal+ 54% vs arousal- 59%; VGMIDI 60% vs 29%, and 48% vs 53%. **Harmony carries valence and almost nothing about arousal.**

Bigrams: Q1 and Q4 are I>vi, vi>IV, I>IV, IV>I (the Axis neighbourhood). Q2 and Q3 are i>bVI, bVI>i, bVI>iv, i>bIII. Q3 also has major-key vi>IV and vi>vi (vi-start sadness). Loops: a 3 or 4 chord cycle repeated twice shows up in 1 to 2 clips per quadrant at best (`vi IV I V` in Q1, Q4 once each), so **EMOPIA cannot rank progressions as loops**; the clips are 30 s excerpts and recognition is too noisy. 7th share is flat across quadrants (44 to 48%) and `Isus` at 5 to 6% everywhere is a recognition artefact (open fifths), so neither means anything here.

## Library against the data

Valence is where the check has teeth. Base rates in EMOPIA: a major-key clip is valence+ 70% of the time, a minor-key clip 28%. Mode alone predicts valence 71% of the time. The library's tags agree with mode alone **80%** of the time (306 rows carry a valenced word), so the library is *more* mode-faithful than real emotion-labelled music. Valence- rows in major mode: 19 of 113 (17%, data: 30%). Valence+ rows in minor mode: 42 of 193 (22%, data: 28%). Borrowed-chord share in the library's major rows: 6.4% for valence+, 12.3% for valence-, the same direction as EMOPIA (Q2 7.8% against Q1 3.0%, Q4 2.4%).

Major-key share of rows tagged by a word in each quadrant, against the clips:

| Quadrant words | Library rows | Library major | Markov rows | Markov major | EMOPIA clips major |
|---|---|---|---|---|---|
| Q1 | 109 | 77% | 35 | 74% | 81% |
| Q2 | 66 | 20% | 12 | 0% | 29% |
| Q3 | 91 | 35% | 13 | 0% | 42% |
| Q4 | 131 | 66% | 29 | 83% | 73% |

Per word (library rows, major-key %, with the quadrant's clip figure to compare):

| Word | Rows | Major | Compare | Verdict |
|---|---|---|---|---|
| Happy, Lighthearted, Animated | 18, 6, 22 | 100% | Q1 81% | right direction, no minor Happy at all |
| Fun, Heroic, Triumphant, Uplifting | 23, 9, 20, 25 | 83 to 90% | Q1 81% | matches |
| Energetic | 21 | 57% | Q1 81% | arousal word, mode-agnostic, fine |
| **Funky** | 11 | **18%** | Q1 81% | disagrees, but it is a genre idiom (minor funk vamps) |
| Calm, Dreamy | 25, 31 | 88 to 90% | Q4 73% | matches |
| **Chill** | 21 | **29%** | Q4 73% | disagrees, neo soul and lo-fi minor |
| Mellow, Smooth | 32, 45 | 59 to 64% | Q4 73% | slightly minor, plausible |
| Hopeful, Beautiful, Resolved | 30, 41, 26 | 88 to 93% | V+ 70% | matches |
| **Tense, Ominous, Dark, Haunting** | 16, 15, 35, 8 | **0 to 13%** | Q2 29% | **more extreme than the data** |
| Intense, Suspenseful, Rebellious | 34, 11, 6 | 17 to 27% | Q2 29% | matches |
| **Sad, Tragic** | 2, 4 | 0 to 25% | Q3 42% | thin (Sad is 2 rows), and the data's sad is often major |
| Melancholic, Sombre | 22, 24 | 9 to 17% | Q3 42% | too minor-only |
| Longing, Solemn | 40, 20 | 45 to 48% | Q3 42% | matches |

Quadrant-level agreement is weak and should not be read as error: of 264 rows whose tags point to one quadrant, 93 (35%) score highest in that quadrant, against 25% for chance. Q1 and Q4 are indistinguishable harmonically (Q4-tagged rows land best in Q1 55 of 83 times), as are Q2 and Q3, so only the valence half of each tag is testable.

## What the data says

1. **Mode is a real but soft valence cue**, and the library applies it as a hard rule. Data: roughly 30% of minor clips are valence+ and 30% of major clips are valence-. Library: 17 to 22%.
2. **Tension is not minor-key in the data.** 29% of Q2 clips are major-key and 42% of Q3 clips are. The library has almost no major-key sad or tense progression (Dark 3% major, Tense 6%, Sad 0%). Vi-start loops and borrowed bVI, bVII, iv in a major key are what the data offers instead.
3. **Game music is the counter-example for borrowed chords.** VGMIDI Q1 uses 10% borrowed chords in major keys against 3% in EMOPIA Q1, so the Mario cadence (`bVI bVII I`) and the heroic bVI-bVII rows are supported by game music and not by pop piano.
4. No row is *contradicted* by the data. The worst posterior for a tagged valence is about 27%, which is the corpus's own noise floor for a mode-opposed clip. Mistag here means "unusual against the corpus", not "wrong".

## Rows to look at (proposed, not applied)

61 of 306 valence-tagged library rows (20%) lean against their EMOPIA mode base rate, plus 10 of 85 Markov rows (all minor-key rows tagged Triumphant, Empowered, Relaxed or Dreamy). That is less than the corpus's own 29%. The shortlist below keeps the ones that are not explained by a genre idiom.

| Row | Numerals | Current tags | What the data suggests | Proposal |
|---|---|---|---|---|
| Hope turn (i-I) | `i I` | Hopeful, Beautiful, Uplifting | minor key, two chords, Uplifting is Q1 vocabulary | drop Uplifting, add Tender |
| Progressive house build | `i bVI bIII bVII` | Epic, Driving, Uplifting | minor loop, scores Q2/Q3 | drop Uplifting, add Intense |
| Rising anthem | `i bIII bVI bVII` | Confident, Triumphant, Epic | minor, Q1 minor-key clips exist but lean Q2 here | keep Epic, swap Triumphant for Dramatic |
| Epic extended minor | `i V i iv bVII bIII bVII V` | Triumphant, Confident, Dramatic | scores Q2 | swap Triumphant for Intense |
| Cumbia loop | `i iv V i` | Energetic, Playful, Driving | minor, Q2 | keep Energetic and Driving, drop Playful (low confidence) |
| Mixolydian lift | `I IV I bVII` | Playful, Mellow, Happy | borrowed bVII is a Q2 marker in EMOPIA | drop Happy (low confidence) |
| Disco rise | `bVIM7 bVIIM7 IM7` | Energetic, Uplifting, Fun | borrowed, but VGMIDI Q1 tolerates it | keep |
| Descending emotional | `vi iii IV I V` | Nostalgic, Tender, Longing | vi-start major is typical Q3 | add Melancholic |
| Question and no answer | `vi ii V` | Longing, Inconclusive, Smooth | Q3 vi-start major | add Melancholic |
| Alternative wander | `I iii vi IV` | Reflective, Longing, Contemplative | typical Q3 major | add Sad |
| Axis, vi start | `vi IV I V` | Longing, Hopeful, Melancholic | the commonest Q3 loop in both corpora | add Sad |
| Trance anthem | `vi IV I V` | Epic, Uplifting, Energetic | same chords as the row above; data cannot separate them (arousal) | none |

Vocabulary points rather than row edits: treat **Chill, Funky, Smooth** as idiom words, not valence words, since their rows are minor 7th vamps with no sad content; and consider more major-key Tense/Dark rows (the Omnibus and the I-bIII chromatic mediant are the only two) built on borrowed bVI, bVII and iv.

## Caveats

- EMOPIA is pop piano covers (GiantMIDI transcriptions), VGMIDI is 204 game arrangements, 4-bar phrases with 27 to 76 per quadrant. Neither is the dance, jazz and film idioms most library rows come from.
- Labels are about a whole clip. Mood also lives in tempo, register, dynamics and rhythm, which a progression does not carry, and arousal is mostly that.
- Chord recognition from transcribed piano is noisy: passing tones read as 7ths, open fifths as sus, and the key estimate fails on modal clips (a Dorian clip may fit as major or minor).
- A fixed major-degree table was used for corpus and library alike, so numerals are directly comparable; the ambiguity left is Lydian/Mixolydian/Dorian rows being grouped by major or minor third only.
- Loops and 7th-chord use could not be tested; 7th share is flat across quadrants.
