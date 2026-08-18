# The chord library

Proposed 2026-08-18 (Owen: "collections or books of chords and progressions, things that go well
together ... an outstanding library that makes it easy to compose, maybe organized by emotion or
something. Scaler, the other VST has done a great job of this").

**Built the same day, as far as the first half of §9's first question.** `src/ChordLibrary.h`
holds **271 tagged progressions**; **Library** is appended to the generator's Source list; its
band carries Mood / Genre / Does-what and a readout of what the filter matched. Still ahead: the
browsable window, folding `MarkovData.h`'s 88 rows in, and the relational layer in §7.

The design and the paper trail behind it follow, in the shape `docs/ACID_DESIGN.md` uses: what
exists, what the references do, what Keys took and what it deliberately did not.

---

## 1. Keys already has two progression libraries, and they do not know about each other

This is the finding that shapes everything below, and it was a surprise.

**`src/MarkovData.h`** holds **88 hand-authored progressions** - 30 Major, 30 Minor, 28 Modal -
and **every one of them is already mood-tagged**, one to three tags apiece, from a vocabulary of
22 words: Cinematic, Dark, Dramatic, Dreamy, Empowered, Excited, Haunting, Hopeful, Joyful,
Melancholic, Mysterious, Nostalgic, Peaceful, Playful, Rebellious, Relaxed, Romantic, Spiritual,
Suspenseful, Tender, Tense, Triumphant.

And you cannot browse a single one of them. The corpus exists only to be **shredded into bigrams**:
`ChordMarkov.h::buildTable` filters by mode and mood, counts every adjacent numeral pair, and
throws the sequences away. Ask for "Nostalgic" and you do not get the nostalgic progressions, you
get a statistical blur of the moves they have in common. That is a legitimate generator and it
should stay - but it means Keys ships a curated, tagged, emotion-organised progression library
that no user can ever look at, pick from, or hear as written.

**`src/ChordSources.h::progressionLibrary()`** holds **7 named progressions** - ii-V-I, the Axis,
12-bar blues, the Andalusian cadence, the Royal Road, rhythm-changes A, Coltrane changes - stored
as explicit semitone-plus-type steps and played back literally. These are browsable, in the sense
that a flat combo box of seven names is browsing. They carry **no tags at all**.

So: 88 tagged sequences you cannot see, and 7 visible sequences with no tags. The library is not a
new feature so much as the one that joins those two up and then grows the result.

---

## 2. What Scaler actually does, from Owen's own copy

`E:\Ableton\Scaler 3 Moods and Genres\` has the vocabulary as two CSVs, which settles the
guesswork: Scaler 3 tags on **two** axes.

**41 moods:** Animated, Atmospheric, Beautiful, Calm, Chill, Confident, Contemplative, Dark,
Dramatic, Dreamy, Driving, Eerie, Energetic, Epic, Fun, Funky, Happy, Heroic, Hopeful,
Inconclusive, Intense, Lighthearted, Longing, Melancholic, Mellow, Mysterious, Ominous, Playful,
Reflective, Resolved, Romantic, Sad, Serious, Smooth, Solemn, Sombre, Suspenseful, Tense, Tragic,
Triumphant, Uplifiting *(their typo, not ours)*.

**40 genres:** 80s, Alternative, Ballads, Blues, Bossa, Chillout, Cinematic, Classical, Country,
Deep House, Disco, Downtempo, Drum & Bass, Easy Listening, EDM, Electronica, Folk, Funk, Future
Bass, Gospel, Hip Hop, House, Jazz, Latin, Lo-fi, Minimal, Neo Soul, Pop, Progressive House,
Progressive Rock, Punk, Reggae, RnB, Rock, Slaphouse, Synthwave, Techno, Theatre, Trance, Trap,
World Music.

Two of those 41 are worth pausing on, because they are not emotions and they are the most useful
words on the list: **Inconclusive** and **Resolved**. That is not how the progression *feels*, it
is what the progression *does* - whether it lands or hangs. Scaler has smuggled a structural axis
into its mood list, which is a hint that two axes were one short.

Keys' own 22 tags are close to a subset: 18 of them appear on Scaler's list verbatim or near it
(Haunting/Eerie, Relaxed/Mellow, Peaceful/Calm). Adopting Scaler's vocabulary costs Keys almost
nothing and buys a producer who owns both a word list they already read.

---

## 3. The proposal: three axes, and the third one is the point

**Mood** - how it feels. Scaler's 41, minus their typo.

**Genre** - what it sounds like. Scaler's 40.

**Function** - *what it does*, which is the axis Scaler does not really have and the one that turns
browsing into composing. A first vocabulary of eight:

| Function | What it means | Example |
|---|---|---|
| **Loop** | Repeats forever, no strong landing | i-bVII-bVI-bVII |
| **Cadence** | Arrives and stays arrived | ii-V-I |
| **Turnaround** | Ends a section by handing back to its start | I-vi-ii-V |
| **Vamp** | Two or three chords, modal, static | I-bVII (Mixolydian) |
| **Lift** | Raises energy into the next section | IV-V-vi |
| **Descent** | Steps down, a lament bass or a line cliché | i-bVII-bVI-V |
| **Turn** | Changes key or colour mid-phrase | chromatic mediant pairs |
| **Open** | Deliberately unresolved | ends on IV or V |

Why this is the axis that earns its keep: "sad" gets you a hundred candidates and no way to choose
between them. **"Sad, and it loops"** and **"sad, and it ends"** are different requests, and every
composer has one of them in mind. It is also the axis that makes the strip of twelve pads mean
something - a Loop for the verse, a Lift into the chorus, a Turnaround back.

**Section** - an optional fourth tag (Intro / Verse / Chorus / Bridge / Outro), free where the
source names one. Chordonomicon annotates structural parts, so this comes with the statistical
evidence rather than needing invention. It is what makes "what could follow this" answerable, and
it is the tie into the thing Keys already has half-built (see §7).

---

## 4. Storage: numerals, not semitones

Store every entry as a **roman-numeral token string**, the grammar `ChordMarkov.h` already parses
(accidental + numeral + optional quality suffix: `i`, `bVII`, `V7`, `iim7`, `IM7`).

Three reasons, in order of weight:

1. **A parser already exists and is already tested.** `parseNumeralToken` handles the accidental,
   longest-match numeral and suffix lookup, and refuses an unrecognised suffix rather than
   silently case-falling-back - a deliberate departure from Octavium's version, logged in that
   file. A second storage format would need a second parser.
2. **It is key- and mode-independent by construction**, so one entry serves twelve keys, which is
   how a library of 400 becomes a library of 4,800 without 4,800 rows.
3. **It is what the pads now display.** As of this same day a chord card carries its numeral in the
   corner (§8), so the library's storage format and the thing on screen are the same notation.
   Nothing has to be translated to be read back.

**The suffix table needs extending, and extending it is safe.** It currently holds ten:
`M7 m7 dom7 7 dim7 dim aug sus2 sus4 add9`, against `chordgen::types()`'s twenty. Missing and
wanted: `m7b5` (half-diminished - the ii of every minor ii-V, so its absence is not cosmetic),
`6`, `m6`, `9`, `M9`, `m9`, `6/9`, `mM7`, `madd9`. It is a **lookup keyed by string**, not an
indexed list, so appending to it cannot move anything already saved - unlike `genSource` and the
lane indices, where append-only is a hard rule for exactly that reason.

`sources::progressionLibrary()`'s seven entries become derived rather than authored: a numeral
string with explicit suffixes says everything the semitone-plus-type pairs said. `ii-V-I` stored as
`iim7 V7 IM7` is the same chord character the current table hard-codes, and the comment defending
explicit types over mode-derived ones stays true, because a suffix *is* an explicit type.

---

## 5. Where the content comes from, and where it must not

**Chord progressions are not copyrightable.** They are common musical stock; a compilation of them
can attract thin copyright in its *selection and arrangement*, and in the EU a database right can
attach to the compiler's effort. So the line is: **the theory and the statistics are free, another
product's curated list is not.**

Sourced from:

- **The named canon**, which is theory rather than anyone's expression: the 50s progression,
  Andalusian cadence, backdoor, bird changes, circle progression, Coltrane changes, eight-bar
  blues, folia, ii-V-I, Montgomery-Ward bridge, omnibus, Pachelbel, passamezzo antico and moderno,
  Axis, ragtime, rhythm changes, romanesca, 12-bar blues, V-IV-I. Plus the classical schemas
  (Prinner, Romanesca, Monte, Fonte, Ponte) that Open Music Theory sets out in a pop context.
- **Modal vamps and cadences** per mode, which Keys can generate the skeleton of from
  `ScaleModes.h` and then have curated by hand.
- **Statistical ranking, not statistical content.** Hooktheory's published Trends probabilities and
  the open **Chordonomicon** dataset (666,000 songs, chord progressions with structural-part and
  genre annotation, released as an open benchmark) say *which* progressions are worth a row and
  *which genre and section* they belong to. They rank and tag a canon that is authored here.
- **Film-score harmony** as its own seam, since Owen is scoring a film with this: chromatic
  mediants, planing, and the neo-Riemannian pairs that Keys' PLR source already generates but
  cannot name. The library is where a nameless PLR move becomes "that Hollywood third".

Not sourced from: Scaler's chord sets, or any other product's curated library, copied across. The
two CSVs of *vocabulary* are a different thing from a curated list of *content* - a word list is
not a compilation - and are used here as the taxonomy only.

---

## 6. Size

**Target ~400 entries** for the first complete cut, growing after.

Scaler ships 1,000+ chord sets, but a large share of those are artist and genre packs where the
value is the name attached. A curated 400 tagged on three axes is more useful than a flat 1,000,
and 400 is a number that can be **verified by ear and by theory one row at a time**, which a
scraped 5,000 cannot. The 88 already in `MarkovData.h` are the first 88, retagged onto the new
vocabulary; nothing is thrown away.

---

## 7. The feature under the feature: what follows what

A library organised by emotion is **browsing**. What makes it *composing* is the library knowing
what goes after what, and Keys already has three quarters of that machinery built:

- `ChordSuggest.h` and the **Could follow** button beside the reference card,
- the **reference card** itself, the fixed point a tray is generated against,
- the Markov chain, which is literally a model of what follows what,
- and, now, `Section` tags from Chordonomicon saying which progressions are verses and which are
  choruses.

So the library's relational layer is not new construction, it is joining those up: pick a
progression, and **Could follow** stops meaning "a chord that could follow this chord" and starts
meaning "a progression that could follow this progression". That is the thing Scaler does not do
well, and it is worth more than the next hundred rows of content.

---

## 8. Built already: the numeral on the card

Shipped 2026-08-18 alongside this design, because it is the library's notation showing up on the
surface the library will fill.

Every filled chord card - a pad on the strip, a candidate in the generator's tray - carries its
roman numeral in the **top-left corner**, micro caps at the note list's own size and 0.62 alpha.
Top-left is the one corner a card had left: the lock dot owns the top-right, the arp line's letter
the bottom-right.

`src/ChordNumerals.h` is the one implementation. It was private to `SourceViz.cpp`, and duplicating
it per surface would have re-armed a trap that file has already paid for once: the Progressions
diagram drew sixteen `?` for a build because it read `numeral`, which only the Markov source
writes, where every other source writes `degree`. The resolution order is numeral, then degree,
then a degree derived from the root against the current key.

**It answers empty rather than `?` when nothing resolves**, and the surfaces differ on what to do
with that. A card draws nothing - a `?` in the corner of every hand-captured pad is noise standing
in for information, and a chord genuinely outside the key saying nothing is itself the answer. The
diagram appends its own `?`, because it draws one chip per step and an empty chip would read as a
gap in the walk.

Pads read the **`genRoot`/`genMode` parameters** rather than `ChordGenMenu::genRoot()`, which
answers with whatever an unticked Key or Mode rolled for the last generation. A pad outlives that
roll; the key you are composing in is the one on the Pads bar.

---

## 8b. What the built source actually does

**Whole progressions laid end to end, not one looped.** The first cut looped a single row to fill
the sixteen tray cells, which is what `sources::progressions` does with its own templates, and it
was wrong here for a reason that only appeared on screen: the library holds *vamps*, and rolling
the two-chord "Minimal one-chord" filled all sixteen cards with the same Cm9. Sixteen copies of one
chord is not a trayful of candidates, it is one candidate wasting fifteen cells, and the tray
exists so you can compare.

Laid end to end, a **Vamp** filter gives you eight different vamps to audition and a **12-Bar
Blues** fills the tray on its own - the same rule producing the right answer at both extremes. Rows
are drawn without replacement and shuffled, so a shortlist of six yields six different progressions
before any repeats, and Regen is never inert under a narrow filter. Only the last row may be cut
short by the cell count; every one before it arrives whole.

**A filter that matches nothing falls back to the whole table**, and says so ("no match - any
progression"). The two word pickers only ever offer tags with rows behind them, so the only way to
reach that state is a *combination* nobody has written yet - "Funky" and "Classical" - where the
honest answer is "not that, but here is something" rather than a blank tray with no explanation.

**Degrees resolve against the row's own mode, not the session's.** Every other source passes the
session mode there, and it is right for them: they generate *in* that mode, so a chord outside it
genuinely is a borrowing. A library row arrives with a mode of its own, and a minor row read
against a major session resolves nothing - the first build came back with half the tray labelled
`?` about a progression perfectly in *its* key. `degree` is stored on the pad, so this is what the
strip shows afterwards too, and `i bVII bVI` is worth more there than four question marks. No
pitch moves either way; the numerals are absolute, which the tests pin.

The library's chords go through `fitVoicing`, `applyMajorMinorBias` and `applyVoiceLeading` like
every other source's, so Notes, Inversions, Octave, Lean and Smooth Voicing all still apply. That
is what "everything downstream is the same either way" buys.

---

## 9. Open questions

1. **Reached how?** A new entry appended to the `genSource` list, with Mood / Genre / Function
   combos in the generator panel exactly where the Progression combo sits today - or its own
   browsable window off a Library chip on the Pads bar, with a list you scroll and preview. The
   first is nearly free and fits the architecture as written; the second is the one that feels like
   a library.
2. **Does the library write the tray, the pads, or an arp slot?** The tray is the safe answer (a
   candidate is one drag from a pad and costs nothing), and it is what every other generator source
   does since 2026-08-01.
3. **Does a progression keep its identity after it lands?** A pad remembers `degree` and `numeral`
   but not "you came from the Andalusian cadence, chord 3 of 4". A `progressionId` + `step` on
   `ChordPad` would let the strip draw the bracket the Progressions diagram already draws - and
   would be the first field added to that struct since Markov's numeral.
