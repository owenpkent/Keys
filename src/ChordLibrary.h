#pragma once

#include "ChordMarkov.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <vector>

// The chord library: named progressions you can ask for by how they feel, what they sound like,
// and what they do. See docs/CHORD_LIBRARY.md for the design and the paper trail.
//
// Keys already had two progression libraries that did not know about each other. `MarkovData.h`
// holds 88 hand-authored, mood-tagged progressions that no user can look at, because they exist
// only to be shredded into bigrams - ask for "Nostalgic" and you get a statistical blur of the
// moves the nostalgic progressions have in common rather than the progressions. `ChordSources.h`
// holds seven named ones with no tags at all. This is the table that joins them up and grows the
// result.
//
// ---------------------------------------------------------------------------------------------
// Storage: roman numerals, and why
// ---------------------------------------------------------------------------------------------
//
// Every entry is a space-separated numeral string in the grammar `ChordMarkov.h` already parses
// (accidental + numeral + optional quality suffix: `i`, `bVII`, `V7`, `iim7`, `IM7`). Three
// reasons, in order of weight:
//
//   1. **The parser exists and is tested.** `markov::detail::parseNumeralToken` handles the
//      accidental, longest-match numeral and suffix lookup, and *refuses* an unrecognised suffix
//      rather than silently case-falling-back - a deliberate departure from Octavium logged in
//      that file. A second storage format would need a second parser and would drift from it.
//   2. **It is key-independent by construction**, so one row serves twelve keys.
//   3. **It is what the cards now display.** A chord card carries its numeral in the corner since
//      2026-08-18, so the library's storage format and the thing on screen are one notation.
//
// **The degree table is a fixed major-scale one** (`VII` is always 11 semitones above the tonic,
// never 10), for every mode, exactly as `MarkovData.h` documents at length. So a minor entry
// spells its flat degrees: `i bVII bVI V`, never `i VII VI V`. Get this wrong and the progression
// still parses - it just plays the wrong chords, which is the worst kind of wrong. `mode` below
// is **metadata**, not something that changes the notes; the numerals fully determine the pitches
// given a key root.
//
// ---------------------------------------------------------------------------------------------
// Three axes, and the third one is the point
// ---------------------------------------------------------------------------------------------
//
// Mood and Genre are Scaler 3's own vocabularies, which Owen already reads (his copy is at
// `E:\Ableton\Scaler 3 Moods and Genres\`; their "Uplifiting" typo is fixed here). A word list is
// a taxonomy rather than a compilation - what is authored here is the *content*, from the named
// canon, the modal vamps and the statistical corpora, never another product's curated list.
//
// **Function** is the axis Scaler does not really have, and it is the one that turns browsing into
// composing: "sad" gets you forty candidates and no way to choose, where "sad, and it loops" and
// "sad, and it ends" are different requests and every composer has one of them in mind. Two words
// on Scaler's own mood list give the game away - Inconclusive and Resolved are not emotions, they
// are what the progression *does*.
namespace keys::chordlib
{
    // ---- Function ------------------------------------------------------------------------
    //
    // What the progression does structurally. Eight, kept small deliberately: a picker you can
    // read at a glance beats one that is exhaustive. `count` is last so a loop can use it.
    enum class Function
    {
        loop,       // repeats forever, no strong landing
        cadence,    // arrives, and stays arrived
        turnaround, // ends a section by handing back to its start
        vamp,       // two or three chords, static, usually modal
        lift,       // raises energy into the next section
        descent,    // steps down - a lament bass, a line cliche
        turn,       // changes key or colour mid-phrase
        open,       // deliberately unresolved
        count
    };

    inline const char* functionName(Function f)
    {
        switch (f)
        {
            case Function::loop:       return "Loop";
            case Function::cadence:    return "Cadence";
            case Function::turnaround: return "Turnaround";
            case Function::vamp:       return "Vamp";
            case Function::lift:       return "Lift";
            case Function::descent:    return "Descent";
            case Function::turn:       return "Turn";
            case Function::open:       return "Open";
            default:                   return "";
        }
    }

    inline const char* functionBlurb(Function f)
    {
        switch (f)
        {
            case Function::loop:       return "Repeats forever. No strong landing.";
            case Function::cadence:    return "Arrives, and stays arrived.";
            case Function::turnaround: return "Ends a section by handing back to its start.";
            case Function::vamp:       return "Two or three chords, static, usually modal.";
            case Function::lift:       return "Raises energy into the next section.";
            case Function::descent:    return "Steps down - a lament bass, a line cliche.";
            case Function::turn:       return "Changes key or colour mid-phrase.";
            case Function::open:       return "Deliberately unresolved.";
            default:                   return "";
        }
    }

    // ---- Vocabularies --------------------------------------------------------------------
    //
    // Fixed lists, so a picker can offer them and a test can catch a typo in a tag - a misspelt
    // mood is a row that can never be found, and nothing on screen would say so. Sorted, because
    // the combo boxes show them in this order.
    // Scaler 3's 41 plus five of Keys' own. The five are the words `MarkovData.h` already tags
    // with that Scaler simply does not have - Haunting, Nostalgic, Rebellious, Spiritual, Tender -
    // and they are here rather than mapped onto near neighbours because the 88 progressions in
    // that file are the first 88 rows of this table, and retagging "Nostalgic" as "Longing" on the
    // way in would lose the distinction the original author drew. Longing is wanting something;
    // nostalgia is having had it. The rest of Keys' 22 do map cleanly (Joyful -> Happy, Peaceful
    // -> Calm, Relaxed -> Mellow, Empowered -> Confident, Excited -> Energetic), and "Cinematic"
    // moves axis entirely: on Scaler's list, and on this one, it is a *genre*.
    //
    // The first row of this table to use a word that is not here fails `firstUnknownTag` and the
    // build goes red, which is how that "Nostalgic" gap was found in the first place.
    inline const std::vector<const char*>& moods()
    {
        static const std::vector<const char*> m = {
            "Animated", "Atmospheric", "Beautiful", "Calm", "Chill", "Confident", "Contemplative",
            "Dark", "Dramatic", "Dreamy", "Driving", "Eerie", "Energetic", "Epic", "Fun", "Funky",
            "Happy", "Haunting", "Heroic", "Hopeful", "Inconclusive", "Intense", "Lighthearted",
            "Longing", "Melancholic", "Mellow", "Mysterious", "Nostalgic", "Ominous", "Playful",
            "Rebellious", "Reflective", "Resolved", "Romantic", "Sad", "Serious", "Smooth",
            "Solemn", "Sombre", "Spiritual", "Suspenseful", "Tender", "Tense", "Tragic",
            "Triumphant", "Uplifting",
        };
        return m;
    }

    inline const std::vector<const char*>& genres()
    {
        static const std::vector<const char*> g = {
            "80s", "Alternative", "Ballads", "Blues", "Bossa", "Chillout", "Cinematic", "Classical",
            "Country", "Deep House", "Disco", "Downtempo", "Drum & Bass", "Easy Listening", "EDM",
            "Electronica", "Folk", "Funk", "Future Bass", "Gospel", "Hip Hop", "House", "Jazz",
            "Latin", "Lo-fi", "Minimal", "Neo Soul", "Pop", "Progressive House", "Progressive Rock",
            "Punk", "Reggae", "RnB", "Rock", "Slaphouse", "Synthwave", "Techno", "Theatre", "Trance",
            "Trap", "World Music",
        };
        return g;
    }

    // ---- An entry ------------------------------------------------------------------------
    //
    // **One spelling per chord.** A plain triad's quality is carried by the *case* of the numeral
    // and never by a suffix, so it is `i`, never `im`; `bii`, never `bIIm`. Both parse to exactly
    // the same chord, which is the problem: two rows holding the same progression under two
    // spellings are invisible to the duplicate check in `ChordLibraryTests.cpp`, which compares
    // the strings. That check is the only thing standing between this table and the same four
    // chords appearing three times in one filtered list, so the spelling has to be canonical for
    // it to work at all. Suffixed forms are untouched - `im7`, `im9`, `imM7` all keep their `m`,
    // because there the suffix is carrying a type that case cannot.
    struct Entry
    {
        const char* name;       // "Andalusian Cadence"
        const char* numerals;   // space-separated, ChordMarkov.h's grammar
        Function function;
        int mode;               // a modes:: index - metadata and a picker hint, not the notes
        std::vector<const char*> moods;   // 1-4, from moods() above
        std::vector<const char*> genres;  // 1-4, from genres() above
    };

    // `modes::all()` indices, named so the table below reads. Not an enum: `Entry::mode` is a
    // plain int because that is what `modes::get` and the genMode parameter both take, and a
    // second numbering to keep in step with that table is a bug waiting for its first new mode.
    constexpr int kIonian = 0, kLydian = 1, kMixolydian = 2, kAeolian = 3, kDorian = 4,
                  kPhrygian = 5, kLocrian = 6, kHarmMinor = 7, kMelMinor = 8, kBlues = 9;

    // ---- The table -------------------------------------------------------------------------
    //
    // **Where these came from, stated plainly, because it is the first thing anyone extending the
    // table needs to know.** They were *written out* - from the named canon that is common musical
    // knowledge (the axis, doo-wop, ii-V-I, the Andalusian cadence, Pachelbel, rhythm changes, the
    // twelve-bar blues and its variants, the classical schemas), from modal vamps derived per mode
    // against `ScaleModes.h` and picked by ear, and from the loops that characterise each genre.
    // Sixty of them are `MarkovData.h`'s, which was itself hand-authored for Keys.
    //
    // **No corpus was queried and no statistics were computed.** Hooktheory's Trends tool and the
    // open Chordonomicon dataset are both real and both relevant - they are cited in
    // `docs/CHORD_LIBRARY.md` as what a *future* pass should rank and section-tag against - but
    // nothing here was measured against either. Which progressions are common is an authoring
    // judgement, and it should be read as one. If a row looks wrong, it is wrong because somebody
    // thought it was right, not because a dataset said so.
    //
    // Order is not load-bearing anywhere - nothing stores an index into this table, and the two
    // pickers filter rather than index - so entries may be inserted as well as appended. That is
    // the one place in Keys where that is true, and it is true only because of that: `genSource`,
    // the lane indices and `chordgen::types()` are all append-only for exactly the opposite
    // reason. If a session ever stores "which library entry", this comment stops being true.
    inline const std::vector<Entry>& table()
    {
        static const std::vector<Entry> t = {
            // ==========================================================================
            // The named canon - progressions with their own names, which is theory rather
            // than anyone's expression. Alphabetical-ish by family.
            // ==========================================================================
            { "Axis (I-V-vi-IV)", "I V vi IV", Function::loop, kIonian,
              { "Hopeful", "Uplifting", "Happy" }, { "Pop", "Rock", "EDM" } },
            { "Axis, vi start (vi-IV-I-V)", "vi IV I V", Function::loop, kIonian,
              { "Longing", "Hopeful", "Melancholic" }, { "Pop", "Ballads", "Alternative", "80s" } },
            { "Axis, IV start (IV-I-V-vi)", "IV I V vi", Function::loop, kIonian,
              { "Uplifting", "Animated", "Hopeful" }, { "Pop", "House", "EDM", "Alternative" } },
            { "Axis, V start (V-vi-IV-I)", "V vi IV I", Function::loop, kIonian,
              { "Driving", "Energetic", "Hopeful" }, { "Pop", "Rock", "Trance" } },
            { "50s / Doo-wop (I-vi-IV-V)", "I vi IV V", Function::loop, kIonian,
              { "Nostalgic", "Romantic", "Lighthearted" }, { "80s", "Ballads", "Pop" } },
            { "Ragtime (I-vi-ii-V)", "I vi ii V", Function::turnaround, kIonian,
              { "Playful", "Fun", "Animated" }, { "Jazz", "Classical", "Theatre" } },
            { "ii-V-I", "iim7 V7 IM7", Function::cadence, kIonian,
              { "Smooth", "Resolved", "Confident" }, { "Jazz", "Bossa", "Easy Listening" } },
            { "Minor ii-V-i", "iim7b5 V7 im7", Function::cadence, kAeolian,
              { "Serious", "Dark", "Smooth" }, { "Jazz", "Latin", "Cinematic" } },
            { "Rhythm changes A", "IM7 vim7 iim7 V7", Function::turnaround, kIonian,
              { "Fun", "Animated", "Confident" }, { "Jazz" } },
            { "Coltrane changes", "IM7 bVIM7 IIIM7", Function::turn, kIonian,
              { "Intense", "Mysterious", "Serious" }, { "Jazz" } },
            { "Backdoor cadence (bVII7-I)", "IM7 IVm7 bVII7 IM7", Function::cadence, kIonian,
              { "Smooth", "Beautiful", "Reflective" }, { "Jazz", "Neo Soul", "RnB" } },
            { "Andalusian cadence (i-bVII-bVI-V)", "i bVII bVI V", Function::descent, kPhrygian,
              { "Dark", "Dramatic", "Intense" }, { "World Music", "Latin", "Rock" } },
            { "Pachelbel's canon", "I V vi iii IV I IV V", Function::loop, kIonian,
              { "Beautiful", "Solemn", "Hopeful" }, { "Classical", "Ballads", "Pop" } },
            { "Circle progression (I-IV-vii-iii-vi-ii-V-I)", "I IV viidim iii vi ii V I",
              Function::cadence, kIonian, { "Serious", "Beautiful", "Resolved" },
              { "Classical", "Jazz" } },
            { "Twelve-bar blues", "I7 I7 I7 I7 IV7 IV7 I7 I7 V7 IV7 I7 V7", Function::loop, kBlues,
              { "Confident", "Funky", "Fun" }, { "Blues", "Rock", "Country" } },
            { "Quick-change blues", "I7 IV7 I7 I7 IV7 IV7 I7 I7 V7 IV7 I7 V7", Function::loop, kBlues,
              { "Funky", "Confident", "Driving" }, { "Blues", "Rock" } },
            { "Eight-bar blues", "I7 V7 IV7 IV7 I7 V7 I7 V7", Function::loop, kBlues,
              { "Mellow", "Confident", "Fun" }, { "Blues", "Country" } },
            { "Minor blues", "im7 im7 im7 im7 ivm7 ivm7 im7 im7 bVI7 V7 im7 V7", Function::loop, kAeolian,
              { "Dark", "Serious", "Intense" }, { "Blues", "Jazz" } },
            { "V-IV-I turnaround", "V IV I", Function::turnaround, kMixolydian,
              { "Confident", "Driving", "Resolved" }, { "Rock", "Blues", "Country" } },
            { "Royal road (IV-V-iii-vi)", "IV V iii vi", Function::loop, kIonian,
              { "Longing", "Dramatic", "Beautiful" }, { "Pop", "Cinematic", "Theatre" } },
            { "Montgomery-Ward bridge", "I IV II V", Function::lift, kIonian,
              { "Hopeful", "Animated", "Lighthearted" }, { "Jazz", "Pop", "Theatre" } },
            { "Folia", "i V i bVII bIII bVII i V", Function::loop, kAeolian,
              { "Solemn", "Dramatic", "Serious" }, { "Classical", "World Music" } },
            { "Romanesca", "I V vi iii IV I V I", Function::loop, kIonian,
              { "Beautiful", "Solemn", "Reflective" }, { "Classical" } },
            { "Passamezzo antico", "i bVII i V bIII bVII i V i", Function::loop, kAeolian,
              { "Solemn", "Dark", "Serious" }, { "Classical", "World Music" } },
            { "Passamezzo moderno", "I IV I V I IV I V I", Function::loop, kIonian,
              { "Lighthearted", "Confident", "Happy" }, { "Classical", "Folk" } },
            { "Prinner", "IV iii ii I", Function::descent, kIonian,
              { "Reflective", "Beautiful", "Calm" }, { "Classical" } },
            { "Lament bass (i-bVII-bVI-V)", "i bVII bVI V", Function::descent, kAeolian,
              { "Tragic", "Sombre", "Longing" }, { "Classical", "Cinematic", "Ballads" } },
            { "Omnibus (chromatic wedge)", "I V7 bIIIdim7 IV V7 I", Function::turn, kIonian,
              { "Mysterious", "Dramatic", "Tense" }, { "Classical", "Theatre" } },
            { "Bird changes (ii-V chain)", "iim7 V7 im7 IV7 bVIIM7 bIIIM7 bVI7 bIIM7",
              Function::turn, kIonian, { "Intense", "Confident", "Serious" }, { "Jazz" } },
            { "Neapolitan cadence", "i iv bII V i", Function::cadence, kHarmMinor,
              { "Dramatic", "Tense", "Solemn" }, { "Classical", "Cinematic", "Theatre" } },
            { "Picardy third", "i iv V I", Function::cadence, kHarmMinor,
              { "Hopeful", "Solemn", "Beautiful" }, { "Classical", "Cinematic" } },
            { "Plagal (amen) cadence", "I IV I", Function::cadence, kIonian,
              { "Calm", "Solemn", "Resolved" }, { "Gospel", "Classical", "Folk" } },
            { "Deceptive cadence", "IM7 iim7 V7 vim7", Function::open, kIonian,
              { "Inconclusive", "Longing", "Reflective" }, { "Classical", "Jazz", "Ballads" } },
            { "Line cliche (i-i+5-i6-i7)", "i imM7 im7 im6", Function::descent, kHarmMinor,
              { "Mysterious", "Suspenseful", "Ominous" }, { "Cinematic", "Theatre", "Jazz" } },

            // ==========================================================================
            // Songwriting: pop, rock, ballads, country, folk, 80s, synthwave, punk.
            // The bulk of the table, because it is the bulk of what gets written. Ordered
            // roughly by how often each turns up, which is an authoring judgement rather than
            // a measurement - see the table's own note above.
            // ==========================================================================
            { "Three chords (I-IV-V)", "I IV V", Function::cadence, kIonian,
              { "Happy", "Confident", "Fun" }, { "Rock", "Folk", "Country" } },
            { "Three chords, resolved (I-IV-V-I)", "I IV V I", Function::cadence, kIonian,
              { "Happy", "Resolved", "Uplifting" }, { "Rock", "Folk", "Country" } },
            { "I-V-IV", "I V IV", Function::loop, kMixolydian,
              { "Driving", "Confident", "Fun" }, { "Rock", "Country", "Punk" } },
            { "Mixolydian rock (I-bVII-IV)", "I bVII IV", Function::loop, kMixolydian,
              { "Confident", "Driving", "Animated" }, { "Rock", "Progressive Rock", "Alternative" } },
            { "Mixolydian rock, closed (I-bVII-IV-I)", "I bVII IV I", Function::loop, kMixolydian,
              { "Confident", "Energetic", "Resolved" }, { "Rock", "Progressive Rock", "Blues", "Country" } },
            { "vi-V-IV-V", "vi V IV V", Function::loop, kIonian,
              { "Longing", "Dramatic", "Intense" }, { "Pop", "Alternative", "Ballads" } },
            { "I-iii-IV-V", "I iii IV V", Function::lift, kIonian,
              { "Hopeful", "Beautiful", "Uplifting" }, { "Pop", "Ballads", "Easy Listening" } },
            { "I-IV-vi-V", "I IV vi V", Function::loop, kIonian,
              { "Hopeful", "Animated", "Happy" }, { "Pop", "Country", "Folk" } },
            { "I-vi-iii-IV", "I vi iii IV", Function::descent, kIonian,
              { "Reflective", "Beautiful", "Longing" }, { "Ballads", "Pop", "Alternative" } },
            { "vi-IV-V-I", "vi IV V I", Function::cadence, kIonian,
              { "Hopeful", "Uplifting", "Triumphant" }, { "Pop", "Rock", "Cinematic" } },
            { "I-V-vi-iii-IV", "I V vi iii IV", Function::descent, kIonian,
              { "Beautiful", "Reflective", "Hopeful" }, { "Ballads", "Pop", "Classical" } },
            { "ii-V-I pop (no sevenths)", "ii V I", Function::cadence, kIonian,
              { "Resolved", "Calm", "Happy" }, { "Pop", "Folk", "Easy Listening" } },
            { "I-IV-I-V", "I IV I V", Function::loop, kIonian,
              { "Lighthearted", "Fun", "Happy" }, { "Country", "Folk", "Punk" } },
            { "IV-V-I-vi", "IV V I vi", Function::turnaround, kIonian,
              { "Hopeful", "Animated", "Uplifting" }, { "Pop", "Theatre" } },
            { "I-bIII-IV (borrowed lift)", "I bIII IV", Function::lift, kMixolydian,
              { "Confident", "Driving", "Heroic" }, { "Rock", "Alternative", "Progressive Rock" } },
            { "I-bVI-bVII (heroic borrow)", "I bVI bVII", Function::lift, kMixolydian,
              { "Heroic", "Triumphant", "Epic" }, { "Rock", "Cinematic", "Synthwave" } },
            { "I-bVI-bVII-I", "I bVI bVII I", Function::loop, kMixolydian,
              { "Heroic", "Epic", "Confident" }, { "Rock", "Cinematic", "80s" } },
            { "bVI-bVII-I (Mario cadence)", "bVI bVII I", Function::cadence, kMixolydian,
              { "Triumphant", "Heroic", "Uplifting" }, { "Rock", "Cinematic", "Synthwave", "Theatre" } },
            { "I-V-bVII-IV", "I V bVII IV", Function::loop, kMixolydian,
              { "Driving", "Confident", "Animated" }, { "Rock", "Alternative", "Country" } },
            { "vi-bVI-I-V", "vi bVI I V", Function::turn, kIonian,
              { "Dramatic", "Longing", "Mysterious" }, { "Alternative", "Cinematic" } },
            { "I-I/vii-vi-V (descending bass)", "I viidim vi V", Function::descent, kIonian,
              { "Beautiful", "Reflective", "Sombre" }, { "Ballads", "Classical", "Theatre" } },
            { "i-bVI-bIII-bVII (minor axis)", "i bVI bIII bVII", Function::loop, kAeolian,
              { "Melancholic", "Driving", "Longing" }, { "Rock", "Pop", "Alternative" } },
            { "i-bVII-bVI-bVII", "i bVII bVI bVII", Function::loop, kAeolian,
              { "Dark", "Longing", "Intense" }, { "Rock", "Cinematic", "Alternative" } },
            { "i-iv-bVII-bIII", "i iv bVII bIII", Function::loop, kAeolian,
              { "Melancholic", "Serious", "Reflective" }, { "Alternative", "Ballads" } },
            { "i-bIII-bVII-iv", "i bIII bVII iv", Function::loop, kAeolian,
              { "Melancholic", "Longing", "Atmospheric" }, { "Alternative", "Chillout" } },
            { "i-v-bVI-bVII", "i v bVI bVII", Function::loop, kAeolian,
              { "Sombre", "Longing", "Dark" }, { "Ballads", "Alternative" } },
            { "i-bVII-bIII-bVI", "i bVII bIII bVI", Function::loop, kAeolian,
              { "Melancholic", "Reflective", "Atmospheric" }, { "Alternative", "Chillout", "Lo-fi" } },
            { "iv-i-v-i", "iv i v i", Function::loop, kAeolian,
              { "Sombre", "Solemn", "Serious" }, { "Folk", "World Music", "Cinematic" } },
            { "i-iv-v (natural minor)", "i iv v", Function::cadence, kAeolian,
              { "Sad", "Sombre", "Serious" }, { "Folk", "Blues", "World Music" } },
            { "i-iv-V (harmonic cadence)", "i iv V", Function::cadence, kHarmMinor,
              { "Dramatic", "Tense", "Solemn" }, { "Classical", "Cinematic", "Theatre" } },
            { "i-V-i (minor authentic)", "i V i", Function::cadence, kHarmMinor,
              { "Dramatic", "Serious", "Resolved" }, { "Classical", "Theatre", "Cinematic" } },
            { "80s synth (I-V-vi-iii)", "I V vi iii", Function::descent, kIonian,
              { "Nostalgic", "Dreamy", "Beautiful" }, { "80s", "Synthwave", "Pop" } },
            { "Synthwave minor drive (i-bVI-bIII-bVII)", "i bVI bIII bVII", Function::loop, kAeolian,
              { "Driving", "Dark", "Energetic" }, { "Synthwave", "80s" } },
            { "Synthwave neon (i-bVII-bVI-bVII)", "i bVII bVI bVII", Function::loop, kAeolian,
              { "Atmospheric", "Driving", "Mysterious" }, { "Synthwave", "80s", "Electronica" } },
            { "Country walk-up (I-ii-iii-IV)", "I ii iii IV", Function::lift, kIonian,
              { "Happy", "Animated", "Uplifting" }, { "Country", "Folk", "Gospel" } },
            { "Nashville turnaround (I-V-vi-IV-I)", "I V vi IV I", Function::turnaround, kIonian,
              { "Happy", "Resolved", "Confident" }, { "Country", "Pop" } },
            { "Folk modal (i-bVII-i)", "i bVII i", Function::vamp, kDorian,
              { "Contemplative", "Mysterious", "Calm" }, { "Folk", "World Music", "Cinematic" } },
            { "Folk Dorian (i-IV-i)", "i IV i", Function::vamp, kDorian,
              { "Contemplative", "Beautiful", "Mellow" }, { "Folk", "Chillout", "World Music" } },
            { "Punk three-chord (I-IV-V-IV)", "I IV V IV", Function::loop, kIonian,
              { "Energetic", "Fun", "Driving" }, { "Punk", "Rock", "Alternative" } },
            { "Punk minor drive (i-bVI-bVII-i)", "i bVI bVII i", Function::loop, kAeolian,
              { "Intense", "Energetic", "Dark" }, { "Punk", "Rock", "Alternative" } },
            { "Grunge (I-bIII-IV-bVI)", "I bIII IV bVI", Function::loop, kMixolydian,
              { "Ominous", "Intense", "Sombre" }, { "Alternative", "Rock" } },
            { "Prog odd turn (i-bVI-bVII-v)", "i bVI bVII v", Function::open, kAeolian,
              { "Mysterious", "Serious", "Inconclusive" }, { "Progressive Rock", "Cinematic" } },
            { "Prog Lydian lift (I-II-I)", "I II I", Function::vamp, kLydian,
              { "Dreamy", "Epic", "Atmospheric" }, { "Progressive Rock", "Cinematic", "Electronica" } },
            { "Ballad descent (I-V/vii-vi-I/v-IV)", "I viidim vi v IV", Function::descent, kIonian,
              { "Beautiful", "Longing", "Sombre" }, { "Ballads", "Classical", "Theatre" } },
            { "Ballad lift (IV-V-vi-IV)", "IV V vi IV", Function::lift, kIonian,
              { "Longing", "Dramatic", "Hopeful" }, { "Ballads", "Pop", "Theatre" } },
            { "Anthem (IV-I-V)", "IV I V", Function::lift, kIonian,
              { "Triumphant", "Uplifting", "Epic" }, { "Rock", "Pop", "Cinematic" } },
            { "Anthem, closed (IV-V-I)", "IV V I", Function::cadence, kIonian,
              { "Triumphant", "Resolved", "Uplifting" }, { "Rock", "Gospel", "Cinematic" } },
            { "Open on IV", "I V IV", Function::open, kIonian,
              { "Inconclusive", "Contemplative", "Calm" }, { "Alternative", "Chillout" } },
            { "Open on V (I-IV-ii-V)", "I IV ii V", Function::open, kIonian,
              { "Inconclusive", "Longing", "Hopeful" }, { "Pop", "Ballads" } },
            { "Sus resolution (Isus4-I)", "Isus4 I", Function::cadence, kIonian,
              { "Calm", "Beautiful", "Resolved" }, { "Folk", "Chillout", "Ballads" } },
            { "Sus vamp (Isus2-I-Isus4-I)", "Isus2 I Isus4 I", Function::vamp, kIonian,
              { "Calm", "Atmospheric", "Mellow" }, { "Chillout", "Folk", "Downtempo" } },
            { "Add9 shimmer (Iadd9-IVadd9)", "Iadd9 IVadd9", Function::vamp, kIonian,
              { "Dreamy", "Beautiful", "Calm" }, { "Chillout", "Alternative", "Downtempo" } },
            { "Alternative wander (I-iii-vi-IV)", "I iii vi IV", Function::loop, kIonian,
              { "Reflective", "Longing", "Contemplative" }, { "Alternative", "Chillout" } },
            { "Alternative minor wander (i-bIII-iv-bVI)", "i bIII iv bVI", Function::loop, kAeolian,
              { "Melancholic", "Atmospheric", "Sombre" }, { "Alternative", "Downtempo" } },
            { "Bridge lift (vi-ii-V-I)", "vi ii V I", Function::lift, kIonian,
              { "Hopeful", "Smooth", "Resolved" }, { "Pop", "Jazz", "Theatre" } },
            { "Bridge to V (IV-V-vi-V)", "IV V vi V", Function::open, kIonian,
              { "Longing", "Inconclusive", "Dramatic" }, { "Pop", "Ballads", "Theatre" } },
            { "Secondary dominant lift (I-V/V-V)", "I II V", Function::lift, kIonian,
              { "Hopeful", "Animated", "Confident" }, { "Country", "Jazz", "Theatre" } },
            { "Secondary dominant to vi (I-III-vi)", "I III vi", Function::turn, kIonian,
              { "Dramatic", "Longing", "Romantic" }, { "Jazz", "Ballads", "Theatre" } },
            { "Truck driver's gear change (I-V-I up a tone)", "I V I II VI II", Function::turn, kIonian,
              { "Triumphant", "Animated", "Fun" }, { "Pop", "Theatre", "80s" } },

            // ==========================================================================
            // Jazz, bossa, neo soul, RnB, gospel, funk, disco. Where the sevenths live.
            // The standards below are named for the *harmonic move* they are famous for,
            // not transcribed wholesale: a tune's changes are its own, a turnaround is
            // everybody's.
            // ==========================================================================
            { "iii-vi-ii-V", "iiim7 vim7 iim7 V7", Function::turnaround, kIonian,
              { "Smooth", "Serious", "Confident" }, { "Jazz", "Bossa" } },
            { "I-VI-ii-V turnaround", "IM7 VI7 iim7 V7", Function::turnaround, kIonian,
              { "Smooth", "Animated", "Fun" }, { "Jazz", "Easy Listening" } },
            { "Tritone sub (ii-bII7-I)", "iim7 bII7 IM7", Function::cadence, kIonian,
              { "Smooth", "Mysterious", "Confident" }, { "Jazz", "Neo Soul" } },
            { "Tritone sub turnaround", "IM7 bIII7 iim7 bII7", Function::turnaround, kIonian,
              { "Smooth", "Mysterious", "Serious" }, { "Jazz" } },
            { "Circle bridge (III7-VI7-II7-V7)", "III7 VI7 II7 V7", Function::lift, kIonian,
              { "Animated", "Confident", "Fun" }, { "Jazz", "Theatre" } },
            { "Falling ii-V chain", "iim7 V7 im7 IV7 bVIIM7", Function::turn, kIonian,
              { "Smooth", "Serious", "Intense" }, { "Jazz" } },
            { "Descending half-step ii-Vs", "iim7 V7 biim7 bV7 IM7", Function::turn, kIonian,
              { "Mysterious", "Intense", "Serious" }, { "Jazz" } },
            { "Autumn-leaves turn (major to relative minor)", "iim7 V7 IM7 IVM7 #ivm7b5 VII7 vi",
              Function::turn, kIonian, { "Melancholic", "Smooth", "Reflective" },
              { "Jazz", "Ballads", "Bossa" } },
            { "Bossa minor cycle", "im7 ivm7 iim7b5 V7 im7 bIIIm7 bVI7 bIIM7", Function::turn, kAeolian,
              { "Smooth", "Melancholic", "Romantic" }, { "Bossa", "Jazz", "Latin" } },
            { "A-train lift (I-II7-ii-V)", "IM7 II7 iim7 V7", Function::lift, kIonian,
              { "Confident", "Smooth", "Animated" }, { "Jazz", "Bossa", "Easy Listening" } },
            { "Satin turnaround", "iim7 V7 iiim7 VI7", Function::turnaround, kIonian,
              { "Smooth", "Mellow", "Romantic" }, { "Jazz", "Easy Listening" } },
            { "Modal jazz vamp (im7-bIIm7)", "im7 bIIm7", Function::vamp, kDorian,
              { "Contemplative", "Mysterious", "Chill" }, { "Jazz", "Downtempo", "Electronica" } },
            { "Dorian jazz vamp (im7-IV7)", "im7 IV7", Function::vamp, kDorian,
              { "Chill", "Smooth", "Funky" }, { "Jazz", "Funk", "Lo-fi" } },
            { "Minor ii-V-i with bVI", "iim7b5 V7 im7 bVIM7", Function::cadence, kAeolian,
              { "Dark", "Serious", "Melancholic" }, { "Jazz", "Cinematic" } },
            { "Jazz blues head", "I7 IV7 I7 I7 IV7 IV7 I7 VI7 iim7 V7 I7 VI7", Function::loop, kBlues,
              { "Confident", "Funky", "Smooth" }, { "Jazz", "Blues" } },
            { "Minor jazz blues", "im7 ivm7 im7 im7 ivm7 ivm7 im7 im7 iim7b5 V7 im7 V7",
              Function::loop, kAeolian, { "Dark", "Serious", "Smooth" }, { "Jazz", "Blues" } },
            { "Cadence to the relative minor", "IM7 iim7 III7 vim7", Function::turn, kIonian,
              { "Longing", "Romantic", "Reflective" }, { "Jazz", "Ballads", "Neo Soul" } },
            { "Rootless colour cadence (IVM7-V7-iiim7-vim7)", "IVM7 V7 iiim7 vim7", Function::loop, kIonian,
              { "Beautiful", "Longing", "Smooth" }, { "Neo Soul", "Jazz", "RnB" } },
            { "Neo soul loop (IM9-iiim7-vim9-iim7)", "IM9 iiim7 vim9 iim7", Function::loop, kIonian,
              { "Smooth", "Mellow", "Beautiful" }, { "Neo Soul", "RnB", "Lo-fi" } },
            { "Neo soul two-chord (IM9-IVM9)", "IM9 IVM9", Function::vamp, kIonian,
              { "Mellow", "Dreamy", "Smooth" }, { "Neo Soul", "Lo-fi", "Chillout" } },
            { "Neo soul minor (im9-IVM9)", "im9 IVM9", Function::vamp, kDorian,
              { "Smooth", "Atmospheric", "Chill" }, { "Neo Soul", "Lo-fi", "Downtempo" } },
            { "Neo soul lift (iim9-V7-IM9)", "iim9 V7 IM9", Function::cadence, kIonian,
              { "Smooth", "Beautiful", "Resolved" }, { "Neo Soul", "Jazz", "RnB" } },
            { "RnB loop (vim9-IM9-iim9-V7)", "vim9 IM9 iim9 V7", Function::loop, kIonian,
              { "Smooth", "Romantic", "Mellow" }, { "RnB", "Neo Soul" } },
            { "RnB minor loop (im7-bVIM7-bVIIM7)", "im7 bVIM7 bVIIM7", Function::loop, kAeolian,
              { "Longing", "Smooth", "Melancholic" }, { "RnB", "Neo Soul", "Trap" } },
            { "Slow jam (IM7-iiim7-IVM7-V7)", "IM7 iiim7 IVM7 V7", Function::loop, kIonian,
              { "Romantic", "Smooth", "Tender" }, { "RnB", "Ballads", "Easy Listening" } },
            { "Quiet storm (IVM9-iiim7-iim9-IM9)", "IVM9 iiim7 iim9 IM9", Function::descent, kIonian,
              { "Smooth", "Tender", "Mellow" }, { "RnB", "Easy Listening", "Neo Soul" } },
            { "Gospel amen (IV-I)", "IV I", Function::cadence, kIonian,
              { "Spiritual", "Calm", "Resolved" }, { "Gospel", "Classical", "Folk" } },
            { "Gospel borrowed plagal (I-IV-iv-I)", "I IV iv I", Function::cadence, kIonian,
              { "Spiritual", "Tender", "Beautiful" }, { "Gospel", "Ballads", "Classical" } },
            { "Gospel circle (I-III7-vi-II7-ii-V-I)", "I III7 vi II7 ii V I", Function::cadence, kIonian,
              { "Spiritual", "Triumphant", "Uplifting" }, { "Gospel", "Theatre" } },
            { "Gospel walk-up (I-I7-IV-#ivdim)", "I I7 IV #ivdim", Function::lift, kIonian,
              { "Spiritual", "Animated", "Uplifting" }, { "Gospel", "Jazz", "Country" } },
            { "Gospel bVII plagal (bVII-IV-I)", "bVII IV I", Function::cadence, kMixolydian,
              { "Spiritual", "Confident", "Resolved" }, { "Gospel", "Rock", "Blues" } },
            { "Gospel 2-5-1 with colour", "iim9 V7 IM9 vim9", Function::turnaround, kIonian,
              { "Spiritual", "Smooth", "Uplifting" }, { "Gospel", "Neo Soul", "Jazz" } },
            { "One-chord funk (I9)", "I9 I9", Function::vamp, kMixolydian,
              { "Funky", "Driving", "Confident" }, { "Funk", "Disco", "Hip Hop" } },
            { "Funk two-chord (I9-IV9)", "I9 IV9", Function::vamp, kMixolydian,
              { "Funky", "Fun", "Energetic" }, { "Funk", "Disco" } },
            { "Funk minor vamp (im7-bVII7)", "im7 bVII7", Function::vamp, kDorian,
              { "Funky", "Driving", "Confident" }, { "Funk", "Hip Hop" } },
            { "Funk chromatic push (im7-bIIM7-im7)", "im7 bIIM7 im7", Function::vamp, kPhrygian,
              { "Funky", "Mysterious", "Intense" }, { "Funk", "Hip Hop", "Electronica" } },
            { "Disco loop (iim7-V7-IM7-vim7)", "iim7 V7 IM7 vim7", Function::loop, kIonian,
              { "Fun", "Energetic", "Smooth" }, { "Disco", "House", "Funk" } },
            { "Disco minor loop (im7-bVIIM7-bVIM7-V7)", "im7 bVIIM7 bVIM7 V7", Function::loop, kHarmMinor,
              { "Driving", "Dramatic", "Energetic" }, { "Disco", "House", "80s" } },
            { "Disco rise (bVIM7-bVIIM7-IM7)", "bVIM7 bVIIM7 IM7", Function::lift, kIonian,
              { "Energetic", "Uplifting", "Fun" }, { "Disco", "House", "Pop" } },
            { "Bossa two-five vamp", "iim7 V7", Function::vamp, kIonian,
              { "Smooth", "Mellow", "Romantic" }, { "Bossa", "Jazz", "Easy Listening" } },
            { "Bossa minor vamp (im6-iim7b5-V7)", "im6 iim7b5 V7", Function::vamp, kHarmMinor,
              { "Romantic", "Melancholic", "Smooth" }, { "Bossa", "Latin", "Jazz" } },
            { "Ipanema lift (IM7-II7-iim7-bII7)", "IM7 II7 iim7 bII7", Function::turnaround, kIonian,
              { "Romantic", "Smooth", "Beautiful" }, { "Bossa", "Latin", "Easy Listening" } },
            { "Samba cadence (IM7-II7-iim7-V7-IM7)", "IM7 II7 iim7 V7 IM7", Function::cadence, kIonian,
              { "Happy", "Animated", "Romantic" }, { "Latin", "Bossa", "World Music" } },
            { "Salsa two-chord (im7-V7)", "im7 V7", Function::vamp, kHarmMinor,
              { "Energetic", "Driving", "Dramatic" }, { "Latin", "World Music" } },
            { "Reggae one-drop (I-V-vi-IV)", "I V vi IV", Function::loop, kIonian,
              { "Chill", "Happy", "Mellow" }, { "Reggae", "World Music" } },
            { "Reggae minor skank (i-bVII)", "i bVII", Function::vamp, kAeolian,
              { "Chill", "Confident", "Mellow" }, { "Reggae", "Downtempo", "Chillout" } },
            { "Reggae bubble (i-iv)", "i iv", Function::vamp, kAeolian,
              { "Chill", "Serious", "Mellow" }, { "Reggae", "World Music" } },

            // ==========================================================================
            // Cinematic, classical, theatre. The chromatic mediants and the planing here
            // are the moves Keys' own Neo-Riemannian and Planing sources already generate
            // and cannot name - this is where a nameless PLR step becomes "that Hollywood
            // third", which is most of what a library is for.
            // ==========================================================================
            { "Chromatic mediant up (I-III)", "I III", Function::turn, kIonian,
              { "Epic", "Heroic", "Dramatic" }, { "Cinematic", "Theatre", "Classical" } },
            { "Chromatic mediant down (I-bVI)", "I bVI", Function::turn, kIonian,
              { "Mysterious", "Dreamy", "Atmospheric" }, { "Cinematic", "Electronica" } },
            { "Chromatic mediant flat third (I-bIII)", "I bIII", Function::turn, kIonian,
              { "Ominous", "Suspenseful", "Dark" }, { "Cinematic", "Theatre" } },
            { "Chromatic mediant sixth (I-VI)", "I VI", Function::turn, kIonian,
              { "Hopeful", "Epic", "Beautiful" }, { "Cinematic", "Theatre" } },
            { "Hollywood third chain (I-bVI-bIII-I)", "I bVI bIII I", Function::loop, kIonian,
              { "Epic", "Atmospheric", "Mysterious" }, { "Cinematic", "Progressive Rock" } },
            { "Rising mediants (I-III-#V)", "I III #V", Function::lift, kIonian,
              { "Epic", "Intense", "Heroic" }, { "Cinematic", "Theatre" } },
            { "Minor mediant pair (i-bvi)", "i bvi", Function::turn, kAeolian,
              { "Ominous", "Eerie", "Sombre" }, { "Cinematic" } },
            { "Trailer rise (i-bVI-bIII-bVII)", "i bVI bIII bVII", Function::lift, kAeolian,
              { "Epic", "Intense", "Driving" }, { "Cinematic", "Theatre" } },
            { "Trailer hit (i-bVI-bVII-i)", "i bVI bVII i", Function::loop, kAeolian,
              { "Epic", "Heroic", "Intense" }, { "Cinematic", "EDM", "Synthwave" } },
            { "Trailer descent (i-bVII-bVI-v)", "i bVII bVI v", Function::descent, kAeolian,
              { "Tragic", "Sombre", "Epic" }, { "Cinematic", "Ballads" } },
            { "Heroic fanfare (I-V-IV-I)", "I V IV I", Function::lift, kIonian,
              { "Heroic", "Triumphant", "Epic" }, { "Cinematic", "Theatre", "Classical" } },
            { "Heroic bVII lift (I-bVII-I)", "I bVII I", Function::vamp, kMixolydian,
              { "Heroic", "Confident", "Epic" }, { "Cinematic", "Progressive Rock" } },
            { "Wonder (I-II)", "I II", Function::vamp, kLydian,
              { "Dreamy", "Beautiful", "Epic" }, { "Cinematic", "Electronica", "Chillout" } },
            { "Lydian float (I-II-I-vii)", "I II I vii", Function::loop, kLydian,
              { "Dreamy", "Atmospheric", "Beautiful" }, { "Cinematic", "Downtempo", "Chillout" } },
            { "Lydian lift (IM7-IIM7)", "IM7 IIM7", Function::vamp, kLydian,
              { "Dreamy", "Epic", "Hopeful" }, { "Cinematic", "Electronica" } },
            { "Suspense pedal (i-bII-i)", "i bII i", Function::vamp, kPhrygian,
              { "Suspenseful", "Ominous", "Tense" }, { "Cinematic", "Theatre" } },
            { "Phrygian dread (bII-i)", "bII i", Function::cadence, kPhrygian,
              { "Ominous", "Dark", "Tense" }, { "Cinematic", "Trap", "World Music" } },
            { "Diminished climb (idim7-biiidim7-bvdim7)", "idim7 bIIIdim7 bVdim7", Function::lift, kHarmMinor,
              { "Tense", "Suspenseful", "Intense" }, { "Cinematic", "Theatre", "Classical" } },
            { "Tritone dread (i-bV)", "i bV", Function::vamp, kLocrian,
              { "Eerie", "Ominous", "Tense" }, { "Cinematic" } },
            { "Augmented drift (I-Iaug-vi)", "I Iaug vi", Function::turn, kIonian,
              { "Mysterious", "Suspenseful", "Dreamy" }, { "Cinematic", "Theatre", "Jazz" } },
            { "Whole-tone unease (I-II-#IV)", "I II #IV", Function::open, kLydian,
              { "Eerie", "Mysterious", "Inconclusive" }, { "Cinematic", "Classical" } },
            { "Haunting minor float (i-bVIM7)", "i bVIM7", Function::vamp, kAeolian,
              { "Haunting", "Atmospheric", "Sombre" }, { "Cinematic", "Downtempo", "Chillout" } },
            { "Haunting descent (i-bVII-bVI-bV)", "i bVII bVI bV", Function::descent, kLocrian,
              { "Haunting", "Eerie", "Tragic" }, { "Cinematic" } },
            { "Ostinato minor (i-bIII-iv-i)", "i bIII iv i", Function::loop, kAeolian,
              { "Serious", "Driving", "Dark" }, { "Cinematic", "Minimal", "Classical" } },
            { "Minimalist cell (i-bVII-bIII-iv)", "i bVII bIII iv", Function::loop, kAeolian,
              { "Contemplative", "Atmospheric", "Serious" }, { "Minimal", "Cinematic", "Electronica" } },
            { "Emotional swell (IV-I-vi-V)", "IV I vi V", Function::lift, kIonian,
              { "Beautiful", "Hopeful", "Epic" }, { "Cinematic", "Ballads", "Theatre" } },
            { "Tragic turn (I-i-bVI-bVII)", "I i bVI bVII", Function::turn, kIonian,
              { "Tragic", "Dramatic", "Sombre" }, { "Cinematic", "Alternative" } },
            { "Hope turn (i-I)", "i I", Function::turn, kAeolian,
              { "Hopeful", "Beautiful", "Uplifting" }, { "Cinematic", "Classical", "Ballads" } },
            { "Perfect authentic cadence (V-I)", "V I", Function::cadence, kIonian,
              { "Resolved", "Confident", "Solemn" }, { "Classical", "Theatre" } },
            { "Half cadence (I-ii-V)", "I ii V", Function::open, kIonian,
              { "Inconclusive", "Serious", "Hopeful" }, { "Classical", "Theatre" } },
            { "Phrygian half cadence (iv6-V)", "iv V", Function::open, kHarmMinor,
              { "Solemn", "Dramatic", "Inconclusive" }, { "Classical", "Theatre" } },
            { "Quiescenza (I-bVII-IV-I)", "I bVII IV I", Function::cadence, kIonian,
              { "Solemn", "Calm", "Resolved" }, { "Classical", "Theatre" } },
            { "Monte (rising sequence)", "IV III vi II V", Function::lift, kIonian,
              { "Animated", "Dramatic", "Serious" }, { "Classical", "Theatre" } },
            { "Fonte (falling sequence)", "II v I IV", Function::descent, kIonian,
              { "Reflective", "Serious", "Beautiful" }, { "Classical" } },
            { "Ponte (standing on the dominant)", "V IV V", Function::open, kIonian,
              { "Inconclusive", "Suspenseful", "Dramatic" }, { "Classical", "Theatre" } },
            { "Baroque sequence (descending fifths)", "I IV viidim iii vi ii V", Function::descent, kIonian,
              { "Beautiful", "Serious", "Solemn" }, { "Classical" } },
            { "Baroque minor sequence", "i iv viidim bIII bVI iim7b5 V", Function::descent, kHarmMinor,
              { "Solemn", "Dramatic", "Serious" }, { "Classical", "Cinematic" } },
            { "Pastoral (I-V-I-IV-I)", "I V I IV I", Function::loop, kIonian,
              { "Calm", "Beautiful", "Happy" }, { "Classical", "Folk", "Easy Listening" } },
            { "Hymn (I-IV-I-V-I)", "I IV I V I", Function::cadence, kIonian,
              { "Spiritual", "Solemn", "Resolved" }, { "Classical", "Gospel", "Folk" } },
            { "Theatre lift (I-V-vi-III)", "I V vi III", Function::lift, kIonian,
              { "Dramatic", "Epic", "Longing" }, { "Theatre", "Cinematic" } },
            { "Theatre modulation up a semitone", "I V I bII bVI bII", Function::turn, kIonian,
              { "Triumphant", "Dramatic", "Epic" }, { "Theatre", "Pop" } },
            { "Villain's cadence (bVI-V-i)", "bVI V i", Function::cadence, kHarmMinor,
              { "Ominous", "Dramatic", "Dark" }, { "Theatre", "Cinematic", "Classical" } },
            { "Waltz turn (I-V-I-IV-V-I)", "I V I IV V I", Function::loop, kIonian,
              { "Playful", "Nostalgic", "Beautiful" }, { "Classical", "Theatre", "Easy Listening" } },
            { "Music-box minor (i-V-i-iv)", "i V i iv", Function::loop, kHarmMinor,
              { "Haunting", "Eerie", "Nostalgic" }, { "Cinematic", "Theatre" } },
            { "Planing major triads up a tone", "I II III", Function::lift, kLydian,
              { "Epic", "Dreamy", "Intense" }, { "Cinematic", "Electronica" } },
            { "Planing major triads down a semitone", "I bVII bVI bV", Function::descent, kAeolian,
              { "Eerie", "Atmospheric", "Ominous" }, { "Cinematic", "Electronica", "Downtempo" } },
            { "Planing minor triads (Debussy)", "i bii biii", Function::lift, kPhrygian,
              { "Dreamy", "Mysterious", "Atmospheric" }, { "Classical", "Cinematic", "Electronica" } },
            { "Impressionist wash (IM9-bVIIM9)", "IM9 bVIIM9", Function::vamp, kMixolydian,
              { "Dreamy", "Atmospheric", "Calm" }, { "Classical", "Chillout", "Downtempo" } },

            // ==========================================================================
            // Electronic: house and its family, trance, techno, DnB, future bass, trap,
            // hip hop, lo-fi, chillout, downtempo, minimal, synthwave, EDM, slaphouse.
            // Short loops, because that is the form - four bars that survive being heard
            // sixty times, which is a different design problem from a cadence.
            // ==========================================================================
            { "House classic (im7-bVIM7-bIIIM7-bVIIM7)", "im7 bVIM7 bIIIM7 bVIIM7", Function::loop, kAeolian,
              { "Driving", "Atmospheric", "Energetic" }, { "House", "Deep House", "Progressive House" } },
            { "House uplift (IVM7-V-iiim7-vim7)", "IVM7 V iiim7 vim7", Function::loop, kIonian,
              { "Uplifting", "Energetic", "Happy" }, { "House", "EDM", "Disco" } },
            { "Deep house pad (im9-IVM9-bVIM9-bVIIM9)", "im9 IVM9 bVIM9 bVIIM9", Function::loop, kDorian,
              { "Chill", "Atmospheric", "Smooth" }, { "Deep House", "Downtempo", "Chillout" } },
            { "Deep house two-chord (im9-IV9)", "im9 IV9", Function::vamp, kDorian,
              { "Chill", "Mellow", "Driving" }, { "Deep House", "Minimal", "House" } },
            { "Deep house minor lift (im7-bVIIM7)", "im7 bVIIM7", Function::vamp, kAeolian,
              { "Atmospheric", "Driving", "Contemplative" }, { "Deep House", "Progressive House" } },
            { "Progressive house build (i-bVI-bIII-bVII)", "i bVI bIII bVII", Function::lift, kAeolian,
              { "Epic", "Driving", "Uplifting" }, { "Progressive House", "Trance", "EDM" } },
            { "Progressive house wash (IVM7-IM7-vim7-V)", "IVM7 IM7 vim7 V", Function::loop, kIonian,
              { "Atmospheric", "Uplifting", "Beautiful" }, { "Progressive House", "Trance" } },
            { "Trance anthem (vi-IV-I-V)", "vi IV I V", Function::loop, kIonian,
              { "Epic", "Uplifting", "Energetic" }, { "Trance", "EDM", "Progressive House" } },
            { "Trance breakdown (i-bVII-bVI-bVII)", "i bVII bVI bVII", Function::loop, kAeolian,
              { "Atmospheric", "Longing", "Dreamy" }, { "Trance", "Progressive House" } },
            { "Techno cell (i-bII)", "i bII", Function::vamp, kPhrygian,
              { "Dark", "Driving", "Intense" }, { "Techno", "Minimal", "Electronica" } },
            { "Techno minor stab (i-bVI)", "i bVI", Function::vamp, kAeolian,
              { "Dark", "Driving", "Serious" }, { "Techno", "Minimal" } },
            { "Minimal one-chord (im9)", "im9 im9", Function::vamp, kDorian,
              { "Contemplative", "Atmospheric", "Chill" }, { "Minimal", "Techno", "Downtempo" } },
            { "Slaphouse loop (i-bVI-bVII)", "i bVI bVII", Function::loop, kAeolian,
              { "Driving", "Longing", "Energetic" }, { "Slaphouse", "House", "EDM" } },
            { "Slaphouse hook (bVI-bVII-i)", "bVI bVII i", Function::cadence, kAeolian,
              { "Longing", "Driving", "Dramatic" }, { "Slaphouse", "EDM", "Pop" } },
            { "EDM drop (i-bVII-bVI-V)", "i bVII bVI V", Function::lift, kHarmMinor,
              { "Intense", "Epic", "Dramatic" }, { "EDM", "Trance" } },
            { "Future bass loop (IVM9-IM9-vim9-iim9)", "IVM9 IM9 vim9 iim9", Function::loop, kIonian,
              { "Dreamy", "Uplifting", "Beautiful" }, { "Future Bass", "Pop", "Electronica" } },
            { "Future bass lift (IVM9-V-iiim7-vim9)", "IVM9 V iiim7 vim9", Function::lift, kIonian,
              { "Uplifting", "Energetic", "Dreamy" }, { "Future Bass", "EDM", "Pop" } },
            { "Future bass sus wash (IVadd9-Isus2)", "IVadd9 Isus2", Function::vamp, kIonian,
              { "Dreamy", "Atmospheric", "Beautiful" }, { "Future Bass", "Chillout", "Electronica" } },
            { "Drum & bass minor roll (im7-bIIIM7-bVIIM7-ivm7)", "im7 bIIIM7 bVIIM7 ivm7",
              Function::loop, kAeolian, { "Driving", "Atmospheric", "Intense" },
              { "Drum & Bass", "Electronica" } },
            { "Liquid DnB (im9-bVIM9-bIIIM9-ivm9)", "im9 bVIM9 bIIIM9 ivm9", Function::loop, kDorian,
              { "Smooth", "Atmospheric", "Chill" }, { "Drum & Bass", "Downtempo", "Chillout" } },
            { "Jungle stab (im7-bVII7)", "im7 bVII7", Function::vamp, kDorian,
              { "Driving", "Funky", "Intense" }, { "Drum & Bass", "Electronica" } },
            { "Trap minor loop (i-bVI-bVII-bVI)", "i bVI bVII bVI", Function::loop, kAeolian,
              { "Dark", "Ominous", "Confident" }, { "Trap", "Hip Hop" } },
            { "Trap Phrygian (i-bII-i-bVII)", "i bII i bVII", Function::loop, kPhrygian,
              { "Ominous", "Dark", "Intense" }, { "Trap", "Hip Hop", "Cinematic" } },
            { "Trap minor cadence (bVI-bVII-i)", "bVI bVII i", Function::cadence, kAeolian,
              { "Dark", "Dramatic", "Confident" }, { "Trap", "Hip Hop" } },
            { "Drill minor (i-bVI-bIII-bVII)", "i bVI bIII bVII", Function::loop, kAeolian,
              { "Ominous", "Dark", "Driving" }, { "Trap", "Hip Hop" } },
            { "Boom bap loop (im7-ivm7-bVIIM7-bIIIM7)", "im7 ivm7 bVIIM7 bIIIM7", Function::loop, kAeolian,
              { "Mellow", "Reflective", "Smooth" }, { "Hip Hop", "Lo-fi", "Downtempo" } },
            { "Hip hop jazz loop (iim9-V7-IM9-vim9)", "iim9 V7 IM9 vim9", Function::loop, kIonian,
              { "Smooth", "Chill", "Mellow" }, { "Hip Hop", "Lo-fi" } },
            { "Lo-fi loop (IM9-vim9-iim9-V7)", "IM9 vim9 iim9 V7", Function::loop, kIonian,
              { "Chill", "Nostalgic", "Mellow" }, { "Lo-fi", "Downtempo", "Chillout" } },
            { "Lo-fi minor loop (im9-ivm9-bVIIM9-bIIIM9)", "im9 ivm9 bVIIM9 bIIIM9", Function::loop, kDorian,
              { "Chill", "Melancholic", "Reflective" }, { "Lo-fi", "Downtempo", "Neo Soul" } },
            { "Lo-fi two-chord (IM9-vim9)", "IM9 vim9", Function::vamp, kIonian,
              { "Chill", "Nostalgic", "Calm" }, { "Lo-fi", "Chillout" } },
            { "Lo-fi tritone colour (IM9-bIII9-iim9-bII9)", "IM9 bIII9 iim9 bII9", Function::descent, kIonian,
              { "Smooth", "Nostalgic", "Mysterious" }, { "Lo-fi", "Jazz", "Neo Soul" } },
            { "Chillout float (IM9-IVM9-iiim7-vim9)", "IM9 IVM9 iiim7 vim9", Function::loop, kIonian,
              { "Calm", "Dreamy", "Beautiful" }, { "Chillout", "Downtempo", "Easy Listening" } },
            { "Downtempo drift (im9-bVIIM9)", "im9 bVIIM9", Function::vamp, kDorian,
              { "Atmospheric", "Calm", "Contemplative" }, { "Downtempo", "Chillout", "Minimal" } },
            { "Ambient suspension (Isus2-IVsus2)", "Isus2 IVsus2", Function::vamp, kIonian,
              { "Calm", "Atmospheric", "Dreamy" }, { "Chillout", "Minimal", "Electronica" } },
            { "Ambient minor suspension (imadd9-bVIM9)", "imadd9 bVIM9", Function::vamp, kAeolian,
              { "Atmospheric", "Sombre", "Contemplative" }, { "Chillout", "Cinematic", "Minimal" } },
            { "Electronica pulse (i-bIII-bVII-iv)", "i bIII bVII iv", Function::loop, kAeolian,
              { "Driving", "Atmospheric", "Serious" }, { "Electronica", "Techno", "Downtempo" } },
            { "Electronica major pulse (I-iii-IV-vi)", "I iii IV vi", Function::loop, kIonian,
              { "Animated", "Hopeful", "Driving" }, { "Electronica", "House", "Pop" } },
            { "80s arp bed (i-bVII-bVI-V)", "i bVII bVI V", Function::descent, kHarmMinor,
              { "Dramatic", "Driving", "Nostalgic" }, { "80s", "Synthwave" } },
            { "Synthwave chase (i-bIII-bVII-bVI)", "i bIII bVII bVI", Function::loop, kAeolian,
              { "Driving", "Nostalgic", "Intense" }, { "Synthwave", "80s", "Electronica" } },

            // ==========================================================================
            // Modal vamps, one family per mode, plus world, blues and the leftovers.
            //
            // These are the rows a *generator* could have produced - Keys' own weighted pool
            // can build any of them from the mode table - and that is exactly why they are
            // written down instead. A generated Dorian chord is a chord in Dorian; the two
            // chords below are the pair that makes a listener hear Dorian, which is a
            // different claim and not one an interval table knows how to make.
            // ==========================================================================
            { "Dorian vamp (i-IV)", "i IV", Function::vamp, kDorian,
              { "Chill", "Contemplative", "Mellow" }, { "Folk", "Jazz", "Electronica" } },
            { "Dorian minor pair (i-ii)", "i ii", Function::vamp, kDorian,
              { "Contemplative", "Mysterious", "Calm" }, { "Folk", "World Music" } },
            { "Dorian cycle (i-bIII-IV-bVII)", "i bIII IV bVII", Function::loop, kDorian,
              { "Chill", "Hopeful", "Smooth" }, { "Folk", "Neo Soul", "Chillout" } },
            { "Dorian descent (i-bVII-IV-i)", "i bVII IV i", Function::loop, kDorian,
              { "Mellow", "Reflective", "Confident" }, { "Rock", "Blues", "Jazz" } },
            { "Phrygian vamp (i-bII)", "i bII", Function::vamp, kPhrygian,
              { "Dark", "Mysterious", "Intense" }, { "World Music", "Classical" } },
            { "Phrygian cycle (i-bII-bIII-bII)", "i bII bIII bII", Function::loop, kPhrygian,
              { "Dark", "Ominous", "Dramatic" }, { "World Music", "Cinematic", "Progressive Rock" } },
            { "Phrygian dominant vamp (I-bII)", "I bII", Function::vamp, kPhrygian,
              { "Intense", "Dramatic", "Mysterious" }, { "World Music", "Latin", "Blues" } },
            { "Phrygian dominant cycle (I-bII-I-bVII)", "I bII I bVII", Function::loop, kPhrygian,
              { "Intense", "Dark", "Driving" }, { "World Music", "Latin", "Rock" } },
            { "Lydian vamp (I-II)", "IM7 II", Function::vamp, kLydian,
              { "Dreamy", "Hopeful", "Beautiful" }, { "Folk", "Progressive Rock" } },
            { "Lydian cycle (I-II-vii-I)", "I II vii I", Function::loop, kLydian,
              { "Dreamy", "Atmospheric", "Mysterious" }, { "Progressive Rock", "Electronica" } },
            { "Mixolydian vamp (I-bVII)", "I bVII", Function::vamp, kMixolydian,
              { "Confident", "Mellow", "Chill" }, { "Rock", "Folk", "Country" } },
            { "Mixolydian cycle (I-bVII-IV-bVII)", "I bVII IV bVII", Function::loop, kMixolydian,
              { "Confident", "Driving", "Fun" }, { "Rock", "Blues" } },
            { "Aeolian vamp (i-bVI)", "i bVI", Function::vamp, kAeolian,
              { "Sad", "Sombre", "Longing" }, { "Ballads", "Alternative" } },
            { "Aeolian cycle (i-bVII-bVI-bVII)", "i bVII bVI bVII", Function::loop, kAeolian,
              { "Melancholic", "Longing", "Atmospheric" }, { "Ballads", "Downtempo" } },
            { "Locrian unease (i-bII-bV)", "i bII bV", Function::open, kLocrian,
              { "Eerie", "Tense", "Inconclusive" }, { "Progressive Rock", "Theatre" } },
            { "Harmonic minor vamp (i-V)", "i V", Function::vamp, kHarmMinor,
              { "Dramatic", "Dark", "Intense" }, { "Classical", "World Music", "Theatre" } },
            { "Harmonic minor cycle (i-bVI-V-i)", "i bVI V i", Function::loop, kHarmMinor,
              { "Dramatic", "Solemn", "Dark" }, { "Classical", "Cinematic", "Progressive Rock" } },
            { "Melodic minor colour (i-IV-i)", "imM7 IV imM7", Function::vamp, kMelMinor,
              { "Mysterious", "Smooth", "Tense" }, { "Jazz", "Cinematic" } },
            { "Melodic minor cadence (iim7-V7-imM7)", "iim7 V7 imM7", Function::cadence, kMelMinor,
              { "Smooth", "Serious", "Mysterious" }, { "Jazz", "Theatre" } },
            { "Pentatonic major loop (I-IV-V-IV)", "I IV V IV", Function::loop, kIonian,
              { "Happy", "Lighthearted", "Fun" }, { "Folk", "Country", "World Music" } },
            { "Pentatonic minor loop (i-bIII-IV)", "i bIII IV", Function::loop, kBlues,
              { "Confident", "Sombre", "Driving" }, { "Blues", "Rock" } },
            { "Slow blues (I7-IV7-I7-V7)", "I7 IV7 I7 V7", Function::loop, kBlues,
              { "Melancholic", "Mellow", "Sombre" }, { "Blues", "Ballads" } },
            { "Blues turnaround (I7-VI7-II7-V7)", "I7 VI7 II7 V7", Function::turnaround, kBlues,
              { "Confident", "Funky", "Fun" }, { "Blues", "Country" } },
            { "Blues walk-down (I7-bVII7-bVI7-V7)", "I7 bVII7 bVI7 V7", Function::descent, kBlues,
              { "Sombre", "Confident", "Serious" }, { "Blues", "Rock" } },
            { "Gospel blues (I7-IV7-I7-I7)", "I7 IV7 I7 I7", Function::loop, kBlues,
              { "Spiritual", "Confident", "Uplifting" }, { "Blues", "Gospel" } },
            { "Klezmer turn (i-V-i-iv)", "i V i iv", Function::loop, kPhrygian,
              { "Playful", "Dramatic", "Nostalgic" }, { "World Music" } },
            { "Flamenco cycle (i-bVII-bVI-V-i)", "i bVII bVI V i", Function::cadence, kPhrygian,
              { "Intense", "Dramatic", "Dark" }, { "World Music", "Latin" } },
            { "Celtic modal (i-bVII-bVI-bVII)", "i bVII bVI bVII", Function::loop, kDorian,
              { "Contemplative", "Solemn", "Nostalgic" }, { "Folk", "World Music" } },
            { "Celtic major modal (I-bVII-IV-I)", "I bVII IV I", Function::loop, kMixolydian,
              { "Hopeful", "Nostalgic", "Confident" }, { "Folk", "World Music" } },
            { "Afrobeat vamp (I7-iim7)", "I7 iim7", Function::vamp, kMixolydian,
              { "Fun", "Driving", "Energetic" }, { "World Music", "Funk", "Latin" } },
            { "Bhairav drone (I-bII-I-III)", "I bII I III", Function::loop, kPhrygian,
              { "Mysterious", "Spiritual", "Intense" }, { "World Music", "Electronica", "Downtempo" } },
            { "Japanese pentatonic (i-bII-v)", "i bII v", Function::vamp, kPhrygian,
              { "Contemplative", "Tender", "Mysterious" }, { "World Music", "Minimal", "Cinematic" } },
            { "Tango cadence (iv-V-i)", "iv V i", Function::cadence, kHarmMinor,
              { "Dramatic", "Romantic", "Intense" }, { "Latin", "Theatre", "World Music" } },
            { "Tango descent (i-bVII-bVI-V-i-V)", "i bVII bVI V i V", Function::descent, kHarmMinor,
              { "Dramatic", "Longing", "Romantic" }, { "Latin", "Theatre" } },
            { "Cumbia loop (i-iv-V-i)", "i iv V i", Function::loop, kHarmMinor,
              { "Energetic", "Playful", "Driving" }, { "Latin", "World Music" } },
            { "Waltz minor (i-iv-bVI-V)", "i iv bVI V", Function::loop, kHarmMinor,
              { "Nostalgic", "Sombre", "Romantic" }, { "Classical", "Theatre", "Easy Listening" } },
            { "Rebellious drop (i-bVII-iv)", "i bVII iv", Function::loop, kAeolian,
              { "Rebellious", "Intense", "Dark" }, { "Punk", "Alternative", "Rock" } },
            { "Rebellious major (I-bIII-bVII-IV)", "I bIII bVII IV", Function::loop, kMixolydian,
              { "Rebellious", "Confident", "Energetic" }, { "Punk", "Alternative" } },
            { "Tender major (I-IM7-vi-IV)", "I IM7 vi IV", Function::descent, kIonian,
              { "Tender", "Beautiful", "Romantic" }, { "Ballads", "Easy Listening", "Folk" } },
            { "Tender minor (i-imM7-im7-IV)", "i imM7 im7 IV", Function::descent, kDorian,
              { "Tender", "Longing", "Haunting" }, { "Ballads", "Jazz", "Cinematic" } },
            { "Spiritual rise (I-IV-V-vi-IV-I)", "I IV V vi IV I", Function::lift, kIonian,
              { "Spiritual", "Uplifting", "Triumphant" }, { "Gospel", "Theatre" } },
            { "Playful skip (I-vi-IV-ii)", "I vi IV ii", Function::loop, kIonian,
              { "Playful", "Lighthearted", "Fun" }, { "Easy Listening", "Pop", "Theatre" } },
            { "Nostalgic wander (IM7-iii-vi-IVM7)", "IM7 iii vi IVM7", Function::loop, kIonian,
              { "Nostalgic", "Reflective", "Beautiful" }, { "80s", "Easy Listening", "Chillout" } },
            { "Haunting lullaby (i-bVI-iv-V)", "i bVI iv V", Function::loop, kHarmMinor,
              { "Haunting", "Tender", "Sombre" }, { "Cinematic", "Ballads", "Theatre" } },

            // ==========================================================================
            // Found by measuring the table against Chordonomicon (2026-08-18) - the most
            // common four-chord windows in 680,000 songs that had no row here. See
            // `docs/CHORD_LIBRARY.md` §10 and `scripts/corpus/`.
            //
            // Most of them turned out to be **two-chord vamps written across four bars**,
            // a shape the table already used (Mixolydian oscillation, the Lydian pairs) and
            // had simply never written down for the commonest degrees. Which is the useful
            // kind of gap for a corpus to find: not an exotic progression nobody thought of,
            // but the obvious one everybody plays and nobody writes on a list.
            // ==========================================================================
            { "Tonic-dominant vamp (I-V-I-V)", "I V I V", Function::vamp, kIonian,
              { "Confident", "Driving", "Happy" }, { "Country", "Gospel" } },
            { "Tonic-plagal vamp (I-IV-I-IV)", "I IV I IV", Function::vamp, kIonian,
              { "Calm", "Spiritual", "Mellow" }, { "Gospel", "Folk" } },
            { "Minor tonic vamp (i-bVII-i-bVII)", "i bVII i bVII", Function::vamp, kAeolian,
              { "Atmospheric", "Chill", "Mysterious" }, { "Downtempo", "Electronica" } },
            { "Minor full descent (i-bVII-bVI-bIII)", "i bVII bVI bIII", Function::descent, kAeolian,
              { "Melancholic", "Sombre", "Longing" }, { "Alternative", "Trance" } },
            { "Home turnaround (I-vi-IV-I)", "I vi IV I", Function::turnaround, kIonian,
              { "Nostalgic", "Calm", "Resolved" }, { "Ballads", "Country" } },
            { "Double dominant vamp (I-V-II-V)", "I V II V", Function::loop, kIonian,
              { "Animated", "Hopeful", "Driving" }, { "Country", "Pop" } },
            { "Mixolydian plagal vamp (I-IV-bVII-IV)", "I IV bVII IV", Function::loop, kMixolydian,
              { "Confident", "Mellow", "Driving" }, { "Rock", "Blues" } },

            // ==========================================================================
            // Folded in from `MarkovData.h` (2026-08-18), where they had been mood-tagged
            // since the day that corpus was written and unreachable by anyone ever since:
            // the Markov source shreds them into bigrams and throws the sequences away.
            //
            // **Only the ones that were not already here.** Twenty-eight of the 88 turned
            // out to be rows this table already had under a name of their own - the axis,
            // doo-wop, ii-V-I, the Andalusian cadence - which is a decent sign both were
            // drawn from the same canon. The other sixty are below, keeping their original
            // mood tags (mapped onto this file's vocabulary, which is why Nostalgic,
            // Rebellious, Spiritual, Tender and Haunting had to be added to it) and gaining
            // the genre and function they never had.
            //
            // They stay in `MarkovData.h` as well. This is a copy, not a move: the Markov
            // source still wants its transition table, and the two uses do not conflict.
            // ==========================================================================

            // --- Major ---
            { "Simple anthem (I-I-IV-I-V-IV-I-I)", "I I IV I V IV I I", Function::loop, kIonian,
              { "Spiritual", "Triumphant", "Confident" }, { "Gospel", "Folk" } },
            { "Gospel vamp (I-IV-I-IV-V-I)", "I IV I IV V I", Function::cadence, kIonian,
              { "Spiritual", "Triumphant", "Uplifting" }, { "Gospel", "Theatre" } },
            { "Extended plagal (I-IV-I-vi-IV-V-I)", "I IV I vi IV V I", Function::cadence, kIonian,
              { "Triumphant", "Hopeful", "Solemn" }, { "Classical", "Folk" } },
            { "ii insertion (I-IV-ii-V-I)", "I IV ii V I", Function::cadence, kIonian,
              { "Hopeful", "Resolved", "Happy" }, { "Pop", "Easy Listening" } },
            { "Extended cadence (I-vi-IV-I-V-I)", "I vi IV I V I", Function::cadence, kIonian,
              { "Triumphant", "Spiritual", "Resolved" }, { "Gospel", "Ballads" } },
            { "Extended hymn (I-vi-IV-I-IV-V-I)", "I vi IV I IV V I", Function::cadence, kIonian,
              { "Spiritual", "Calm", "Solemn" }, { "Classical", "Theatre" } },
            { "Extended sentimental (I-iii-vi-IV-I-V-I)", "I iii vi IV I V I", Function::cadence, kIonian,
              { "Nostalgic", "Romantic", "Calm" }, { "Ballads", "Easy Listening" } },
            { "Supertonic approach (I-ii-IV-V)", "I ii IV V", Function::lift, kIonian,
              { "Playful", "Hopeful", "Animated" }, { "Pop", "Country" } },
            { "Rock and roll (I-V-IV-V)", "I V IV V", Function::loop, kIonian,
              { "Happy", "Energetic", "Fun" }, { "Rock", "Punk", "Disco" } },
            { "Descending emotional (vi-iii-IV-I-V)", "vi iii IV I V", Function::descent, kIonian,
              { "Nostalgic", "Tender", "Longing" }, { "Ballads", "Theatre" } },
            { "Sixth-chord colour (I6-IV-V-vi)", "I6 IV V vi", Function::loop, kIonian,
              { "Mellow", "Romantic", "Beautiful" }, { "Easy Listening", "Bossa" } },
            { "Lounge 6/9 (I69-IV-I69-V)", "I69 IV I69 V", Function::vamp, kIonian,
              { "Mellow", "Playful", "Smooth" }, { "Easy Listening", "Jazz" } },
            { "Jazz ballad turnaround (IM7-IVM7-iim7-V7)", "IM7 IVM7 iim7 V7", Function::turnaround, kIonian,
              { "Romantic", "Mellow", "Smooth" }, { "Jazz", "Ballads" } },
            { "Add9 colour (Iadd9-IV-V-Iadd9)", "Iadd9 IV V Iadd9", Function::loop, kIonian,
              { "Romantic", "Tender", "Beautiful" }, { "Ballads", "Folk" } },
            { "Sus resolution, long (Isus4-I-IV-V)", "Isus4 I IV V", Function::loop, kIonian,
              { "Tender", "Calm", "Hopeful" }, { "Folk", "Chillout" } },
            { "Soft cadence (I-Vsus4-V-I)", "I Vsus4 V I", Function::cadence, kIonian,
              { "Calm", "Tender", "Resolved" }, { "Ballads", "Easy Listening" } },
            { "Condensed 12-bar", "I7 IV7 I7 I7 IV7 IV7 I7 V7", Function::loop, kBlues,
              { "Fun", "Energetic", "Funky" }, { "Blues", "Rock" } },

            // --- Minor ---
            { "Natural minor cadence (i-iv-v-i)", "i iv v i", Function::cadence, kAeolian,
              { "Melancholic", "Dark", "Resolved" }, { "Folk", "World Music" } },
            { "Harmonic minor cadence (i-iv-i-V)", "i iv i V", Function::cadence, kHarmMinor,
              { "Dramatic", "Tense", "Inconclusive" }, { "Classical", "Theatre" } },
            { "Functional minor cadence (i-iidim-V-i)", "i iidim V i", Function::cadence, kHarmMinor,
              { "Dramatic", "Suspenseful", "Resolved" }, { "Classical", "Cinematic" } },
            { "Minor circle (i-V-bVI-bIII)", "i V bVI bIII", Function::loop, kHarmMinor,
              { "Melancholic", "Dramatic", "Longing" }, { "Cinematic", "Theatre" } },
            { "Melancholic build (i-v-bVI-bIII)", "i v bVI bIII", Function::loop, kAeolian,
              { "Melancholic", "Nostalgic", "Sombre" }, { "Ballads", "Alternative" } },
            { "Epic extended minor", "i V i iv bVII bIII bVII V", Function::loop, kHarmMinor,
              { "Triumphant", "Confident", "Dramatic" }, { "Cinematic", "Progressive Rock" } },
            { "Drone riff (i-bVII-iv-i)", "i bVII iv i", Function::loop, kAeolian,
              { "Haunting", "Mysterious", "Dark" }, { "Alternative", "Downtempo" } },
            { "Minor jazz turnaround (im7-ivm7-V7-im7)", "im7 ivm7 V7 im7", Function::turnaround, kHarmMinor,
              { "Melancholic", "Suspenseful", "Smooth" }, { "Jazz", "Bossa" } },
            { "Ascending tension (i-iidim-bIII-iv)", "i iidim bIII iv", Function::lift, kAeolian,
              { "Tense", "Suspenseful", "Intense" }, { "Cinematic", "Theatre" } },
            { "Dramatic rise (i-bVI-bIII-V)", "i bVI bIII V", Function::lift, kHarmMinor,
              { "Dramatic", "Confident", "Epic" }, { "Trance", "EDM" } },
            { "Simple rock vamp (i-iv-bVII-i)", "i iv bVII i", Function::loop, kAeolian,
              { "Rebellious", "Dark", "Driving" }, { "Rock", "Punk" } },
            { "Deceptive minor cadence (i-iv-V-bVI)", "i iv V bVI", Function::open, kHarmMinor,
              { "Suspenseful", "Tense", "Inconclusive" }, { "Classical", "Cinematic" } },
            { "Rising anthem (i-bIII-bVI-bVII)", "i bIII bVI bVII", Function::lift, kAeolian,
              { "Confident", "Triumphant", "Epic" }, { "Rock", "EDM" } },
            { "Pure natural minor (i-v-iv-i)", "i v iv i", Function::loop, kAeolian,
              { "Melancholic", "Nostalgic", "Solemn" }, { "Folk", "World Music" } },
            { "Jazz-inflected minor (i-iidim-iv-V)", "i iidim iv V", Function::lift, kHarmMinor,
              { "Tense", "Suspenseful", "Serious" }, { "Jazz", "Theatre" } },
            { "Extended minor cadence (i-iv-i-bVII-bVI-V)", "i iv i bVII bVI V", Function::descent, kHarmMinor,
              { "Dramatic", "Rebellious", "Sombre" }, { "Progressive Rock", "Cinematic" } },
            { "Folk-rock minor (i-bIII-iv-V)", "i bIII iv V", Function::loop, kHarmMinor,
              { "Nostalgic", "Melancholic", "Longing" }, { "Folk", "Alternative" } },
            { "Minor with dominant seven", "i V7 i iv V7 i", Function::loop, kHarmMinor,
              { "Dramatic", "Tense", "Confident" }, { "Classical", "Latin" } },
            { "Rock cadence variant (i-bVII-iv-V)", "i bVII iv V", Function::lift, kHarmMinor,
              { "Rebellious", "Tense", "Driving" }, { "Rock", "Alternative" } },

            // --- Dorian ---
            { "Dorian vamp, four bars (i-IV-i-IV)", "i IV i IV", Function::vamp, kDorian,
              { "Mellow", "Mysterious", "Chill" }, { "Downtempo", "Funk" } },
            { "Dorian rise and fall (i-IV-bVII-i)", "i IV bVII i", Function::loop, kDorian,
              { "Confident", "Mellow", "Driving" }, { "Rock", "Deep House" } },
            { "Dorian extended loop", "i IV i bVII IV i", Function::loop, kDorian,
              { "Dreamy", "Atmospheric", "Contemplative" }, { "Cinematic", "Downtempo" } },
            { "Dorian minor-v colour (i-v-IV-i)", "i v IV i", Function::loop, kDorian,
              { "Mysterious", "Dark", "Reflective" }, { "Alternative", "Progressive Rock" } },
            { "Dorian turnaround (i-bVII-i-IV)", "i bVII i IV", Function::turnaround, kDorian,
              { "Mellow", "Nostalgic", "Chill" }, { "Folk", "Lo-fi" } },
            { "Dorian anthem vamp (i-IV-bVII-IV-i)", "i IV bVII IV i", Function::loop, kDorian,
              { "Confident", "Epic", "Driving" }, { "Progressive House", "Trance" } },

            // --- Mixolydian ---
            { "Mixolydian rock, IV first (I-IV-bVII-I)", "I IV bVII I", Function::loop, kMixolydian,
              { "Confident", "Triumphant", "Fun" }, { "Rock", "Country" } },
            { "Mixolydian build (I-IV-bVII-IV-I)", "I IV bVII IV I", Function::lift, kMixolydian,
              { "Confident", "Epic", "Driving" }, { "Cinematic", "Progressive Rock" } },
            { "Mixolydian lift (I-IV-I-bVII)", "I IV I bVII", Function::loop, kMixolydian,
              { "Playful", "Mellow", "Happy" }, { "Rock", "Folk" } },
            { "Mixolydian oscillation (I-bVII-I-bVII)", "I bVII I bVII", Function::vamp, kMixolydian,
              { "Mellow", "Playful", "Chill" }, { "Reggae", "Blues" } },
            { "Mixolydian extended vamp", "I bVII IV I bVII I", Function::loop, kMixolydian,
              { "Triumphant", "Nostalgic", "Confident" }, { "Rock", "Progressive Rock" } },
            { "Mixolydian float (I-bVII-IV-bVII-I)", "I bVII IV bVII I", Function::loop, kMixolydian,
              { "Dreamy", "Playful", "Atmospheric" }, { "Folk", "Chillout" } },

            // --- Lydian ---
            { "Lydian raised-4 colour (I-II-I-II)", "I II I II", Function::vamp, kLydian,
              { "Dreamy", "Playful", "Beautiful" }, { "Progressive Rock", "Downtempo" } },
            { "Lydian cadence (I-II-V-I)", "I II V I", Function::cadence, kLydian,
              { "Dreamy", "Triumphant", "Resolved" }, { "Cinematic", "Theatre" } },
            { "Lydian float (I-V-II-I)", "I V II I", Function::loop, kLydian,
              { "Calm", "Dreamy", "Atmospheric" }, { "Chillout", "Easy Listening" } },
            { "Lydian mediant colour (I-II-iii-I)", "I II iii I", Function::loop, kLydian,
              { "Playful", "Dreamy", "Animated" }, { "Progressive Rock", "Electronica" } },
            { "Lydian submediant colour (I-vi-II-I)", "I vi II I", Function::loop, kLydian,
              { "Nostalgic", "Dreamy", "Longing" }, { "80s", "Synthwave" } },
            { "Lydian gentle vamp (I-II-vi-I)", "I II vi I", Function::vamp, kLydian,
              { "Calm", "Dreamy", "Spiritual" }, { "Folk", "Minimal" } },
            { "Lydian extended float", "I II I V II I", Function::loop, kLydian,
              { "Epic", "Dreamy", "Triumphant" }, { "Cinematic", "Future Bass" } },

            // --- Phrygian ---
            { "Phrygian half-step colour (i-bII-i-bII)", "i bII i bII", Function::vamp, kPhrygian,
              { "Dark", "Mysterious", "Intense" }, { "World Music", "Trap" } },
            { "Phrygian vamp, bVII first (i-bVII-bII-i)", "i bVII bII i", Function::loop, kPhrygian,
              { "Dark", "Ominous", "Serious" }, { "Cinematic", "Techno" } },
            { "Phrygian dark colour (i-bVI-bII-i)", "i bVI bII i", Function::loop, kPhrygian,
              { "Haunting", "Mysterious", "Ominous" }, { "Downtempo", "Theatre" } },
            { "Phrygian rise (i-bII-bVII-i)", "i bII bVII i", Function::loop, kPhrygian,
              { "Dark", "Confident", "Rebellious" }, { "Rock", "Alternative" } },
            { "Phrygian extended loop", "i bII i bVII bII i", Function::loop, kPhrygian,
              { "Haunting", "Intense", "Epic" }, { "Progressive Rock", "Electronica" } },
            { "Phrygian mediant colour (i-bIII-bII-i)", "i bIII bII i", Function::loop, kPhrygian,
              { "Mysterious", "Nostalgic", "Atmospheric" }, { "Minimal", "Lo-fi" } },

            // ==========================================================================
            // Topping up the two thin functions (2026-08-18). `Open` had ten rows and
            // `Turnaround` twelve against `Loop`'s ninety-nine, which is partly honest -
            // loops are what gets written - but thin enough that a filter on either
            // returned nearly the same handful every time. A picker whose answer barely
            // changes is one you stop trusting.
            // ==========================================================================
            { "Open on ii (I-V-ii)", "I V ii", Function::open, kIonian,
              { "Inconclusive", "Contemplative", "Longing" }, { "Alternative", "Jazz" } },
            { "Open on iii (IV-I-iii)", "IV I iii", Function::open, kIonian,
              { "Inconclusive", "Reflective", "Beautiful" }, { "Ballads", "Chillout" } },
            { "Open on bVII (i-iv-bVII)", "i iv bVII", Function::open, kAeolian,
              { "Inconclusive", "Sombre", "Atmospheric" }, { "Cinematic", "Downtempo" } },
            { "Open on the tritone (I-IV-bV)", "I IV bV", Function::open, kLocrian,
              { "Eerie", "Tense", "Inconclusive" }, { "Progressive Rock", "Techno" } },
            { "Question and no answer (vi-ii-V)", "vi ii V", Function::open, kIonian,
              { "Longing", "Inconclusive", "Smooth" }, { "Jazz", "Bossa" } },
            { "Hanging sus (I-IVsus2-V sus4)", "I IVsus2 Vsus4", Function::open, kIonian,
              { "Atmospheric", "Inconclusive", "Dreamy" }, { "Future Bass", "Minimal" } },
            { "Minor hang (i-bVI-iv)", "i bVI iv", Function::open, kAeolian,
              { "Longing", "Sombre", "Inconclusive" }, { "Ballads", "Trap" } },
            { "Modal hang (i-bVII-v)", "i bVII v", Function::open, kDorian,
              { "Contemplative", "Inconclusive", "Mellow" }, { "Folk", "Lo-fi" } },
            { "Blues turnaround, minor (im7-bVI7-V7)", "im7 bVI7 V7", Function::turnaround, kHarmMinor,
              { "Dark", "Smooth", "Serious" }, { "Blues", "Jazz" } },
            { "Doo-wop turnaround (I-vi-IV-V-I)", "I vi IV V I", Function::turnaround, kIonian,
              { "Nostalgic", "Romantic", "Resolved" }, { "80s", "Ballads" } },
            { "Modal turnaround (I-bVII-IV-V)", "I bVII IV V", Function::turnaround, kMixolydian,
              { "Confident", "Fun", "Driving" }, { "Country", "Rock" } },
            { "Neo soul turnaround (IM9-VI7-iim9-V7)", "IM9 VI7 iim9 V7", Function::turnaround, kIonian,
              { "Smooth", "Romantic", "Mellow" }, { "Neo Soul", "RnB" } },
            { "Half-step turnaround (IM7-bVIIM7-IM7)", "IM7 bVIIM7 IM7", Function::turnaround, kMixolydian,
              { "Smooth", "Dreamy", "Mysterious" }, { "Lo-fi", "Downtempo" } },
            { "House turnaround (im7-bVIM7-bVIIM7-im7)", "im7 bVIM7 bVIIM7 im7", Function::turnaround, kAeolian,
              { "Driving", "Longing", "Energetic" }, { "House", "Deep House" } },
            { "Gospel turnaround (I-VI7-ii-V)", "I VI7 ii V", Function::turnaround, kIonian,
              { "Spiritual", "Animated", "Uplifting" }, { "Gospel", "Theatre" } },
            { "Cinematic turnaround (i-bVI-bVII-V)", "i bVI bVII V", Function::turnaround, kHarmMinor,
              { "Epic", "Dramatic", "Intense" }, { "Cinematic", "Theatre" } },
        };
        return t;
    }

    // ---- Queries -----------------------------------------------------------------------
    //
    // Filters, not an index: a caller narrows by whichever axes it has a pick for and gets the
    // rows back. An empty string means "any" on that axis, which is what the pickers' first item
    // sends - the same convention `markov::buildTable` already uses for its mood filter, and worth
    // matching rather than inventing a second one.
    inline bool matches(const Entry& e, const juce::String& mood, const juce::String& genre,
                        int function)
    {
        if (mood.isNotEmpty())
        {
            bool found = false;
            for (const auto* m : e.moods)
                if (mood == m) { found = true; break; }
            if (! found)
                return false;
        }
        if (genre.isNotEmpty())
        {
            bool found = false;
            for (const auto* g : e.genres)
                if (genre == g) { found = true; break; }
            if (! found)
                return false;
        }
        return function < 0 || function == (int) e.function;
    }

    // `function < 0` is "any". Returns pointers into the static table, which outlives everything.
    inline std::vector<const Entry*> find(const juce::String& mood, const juce::String& genre,
                                          int function)
    {
        std::vector<const Entry*> out;
        for (const auto& e : table())
            if (matches(e, mood, genre, function))
                out.push_back(&e);
        return out;
    }

    // The tags actually in use, for a picker to offer instead of the full vocabulary. A mood with
    // no rows behind it is a pick that can only disappoint, and the table grows unevenly by
    // design - some words earn forty rows and some earn three.
    inline juce::StringArray moodsInUse()
    {
        juce::StringArray out;
        for (const auto& e : table())
            for (const auto* m : e.moods)
                out.addIfNotAlreadyThere(m);
        out.sort(false);
        return out;
    }

    inline juce::StringArray genresInUse()
    {
        juce::StringArray out;
        for (const auto& e : table())
            for (const auto* g : e.genres)
                out.addIfNotAlreadyThere(g);
        out.sort(false);
        return out;
    }

    // ---- Playing one --------------------------------------------------------------------
    //
    // An entry's numerals resolved into chords in a key. This is the whole bridge between the
    // library and the rest of Keys: everything downstream takes `chordgen::Chord` already.
    //
    // `mode` is the mode to resolve `Chord::degree` against - what the numeral badge and the
    // Progressions diagram read - and is a parameter rather than `e.mode` because the two are not
    // always the same question. `ChordGenMenu` passes **the entry's own** mode: a minor row read
    // against a major session resolves nothing, so the tray came back with half its cards labelled
    // "?" about a progression that is perfectly in *its* key. A caller that wants the session's
    // reading instead can have it.
    //
    // The pitches do not depend on it either way - the numerals are absolute (see the file header)
    // - and `ChordLibraryTests.cpp` pins exactly that, because if it ever stopped being true every
    // minor row in the table would be silently wrong.
    //
    // A token that will not parse is **skipped, not substituted**. Markov's own generator falls
    // back to the tonic for an unparseable token, which is right there - a chain has to keep
    // walking - and wrong here, where a silent tonic in the middle of a named progression would
    // be a typo that plays as music. `ChordLibraryTests.cpp` walks every row so no such typo
    // reaches a build.
    inline std::vector<chordgen::Chord> chordsFor(const Entry& e, int rootPc, int mode, int octave)
    {
        std::vector<chordgen::Chord> out;
        const auto tokens = juce::StringArray::fromTokens(juce::String(e.numerals), " ", "");
        const auto& modeObj = modes::get(mode);

        for (const auto& token : tokens)
        {
            if (token.isEmpty())
                continue;
            const auto parsed = markov::detail::parseNumeralToken(token, rootPc);
            if (! parsed.valid)
                continue;

            chordgen::Chord c;
            c.rootPc = parsed.rootPc;
            c.type = parsed.type;
            c.notes = chordgen::chordNotes(parsed.rootPc, parsed.type, octave);
            c.degree = -1;
            const int iv = ((parsed.rootPc - rootPc) % 12 + 12) % 12;
            for (int i = 0; i < (int) modeObj.intervals.size(); ++i)
                if (modeObj.intervals[(size_t) i] == iv)
                {
                    c.degree = i;
                    break;
                }
            out.push_back(c);
        }
        return out;
    }

    // ---- What could follow what ----------------------------------------------------------
    //
    // The relational layer, and the thing a library of progressions can do that a library of
    // chords cannot: given one progression, which others make sense after it.
    //
    // **Two signals, because "could follow" is two questions.** A progression has to follow
    // *structurally* - a Cadence has landed, so what comes next starts something - and it has to
    // follow *harmonically*, since the last chord of one and the first of the next are adjacent in
    // time whatever the labels say. Either alone gives bad answers: function alone will happily
    // hand you a Lift in a key three fifths away, and harmony alone will follow a cadence with
    // another cadence because the roots happen to line up.

    // Which functions read as a sensible next move after each one. This is a grammar of song
    // form, not of harmony, and it is deliberately generous: it says what *could* follow, and the
    // harmonic half narrows it. The asymmetries are the content - a Cadence has arrived, so what
    // follows it opens something new, where a Turnaround has explicitly handed back and what
    // follows it is where you came from.
    inline std::vector<Function> functionsAfter(Function f)
    {
        switch (f)
        {
            case Function::loop:       return { Function::lift, Function::turnaround,
                                                Function::cadence, Function::turn };
            case Function::lift:       return { Function::loop, Function::cadence,
                                                Function::turnaround };
            case Function::cadence:    return { Function::loop, Function::vamp, Function::turn };
            case Function::turnaround: return { Function::loop, Function::vamp, Function::lift };
            case Function::vamp:       return { Function::lift, Function::turn, Function::loop };
            case Function::descent:    return { Function::cadence, Function::loop, Function::open };
            case Function::turn:       return { Function::loop, Function::cadence, Function::vamp };
            // An Open has asked a question, so what follows it answers: something that lands, or
            // something that gets on with it. Never another Open.
            case Function::open:       return { Function::cadence, Function::loop, Function::lift };
            default:                   return {};
        }
    }

    // How well `b` joins onto `a`, from the last chord of one to the first of the other, higher is
    // better. Root motion only: the numerals are absolute, so a semitone distance is all that is
    // needed and it costs no parsing of the chords themselves.
    //
    // The ranking is the ordinary one. A falling fifth is the strongest move in tonal music and
    // scores highest; a step either way is next; a third is a colour change and still good; the
    // tritone is the weakest thing that is not a repeat. **Landing on the same root scores
    // lowest without being disqualified**, because two progressions that both sit on the tonic do
    // follow each other - that is a whole genre - it is just the least *interesting* answer, and
    // it should not crowd out the others.
    inline int joinScore(const Entry& a, const Entry& b)
    {
        const auto endOf = juce::StringArray::fromTokens(juce::String(a.numerals), " ", "");
        const auto startOf = juce::StringArray::fromTokens(juce::String(b.numerals), " ", "");
        if (endOf.isEmpty() || startOf.isEmpty())
            return 0;
        const auto last = markov::detail::parseNumeralToken(endOf[endOf.size() - 1], 0);
        const auto first = markov::detail::parseNumeralToken(startOf[0], 0);
        if (! last.valid || ! first.valid)
            return 0;

        switch (((first.rootPc - last.rootPc) % 12 + 12) % 12)
        {
            case 5:  return 10; // up a fourth - the falling-fifth resolution
            case 7:  return 8;  // up a fifth
            case 2:
            case 10: return 7;  // a step either way
            case 3:
            case 4:
            case 8:
            case 9:  return 6;  // a third either way - a colour change
            case 1:
            case 11: return 5;  // a semitone
            case 6:  return 3;  // the tritone
            default: return 1;  // same root: it follows, it is just the dullest answer
        }
    }

    // Progressions that could follow `from`, best first. `sameModeOnly` keeps the answers in the
    // mode the caller is already in, which is the difference between a suggestion and a modulation.
    //
    // `from` itself is never in the answer: "what could follow this" asking you to play it again
    // is the one reply that cannot help.
    inline std::vector<const Entry*> couldFollow(const Entry& from, bool sameModeOnly = false,
                                                 int limit = 16)
    {
        const auto wanted = functionsAfter(from.function);

        std::vector<std::pair<int, const Entry*>> scored;
        for (const auto& e : table())
        {
            if (&e == &from || juce::String(e.name) == from.name)
                continue;
            if (sameModeOnly && e.mode != from.mode)
                continue;

            // Function is a gate rather than a score: a progression that does not belong after
            // this one is not a weak answer, it is the wrong answer, and letting it in on a good
            // harmonic join is how a list of suggestions stops meaning anything.
            bool functionOk = false;
            for (const auto f : wanted)
                if (f == e.function) { functionOk = true; break; }
            if (! functionOk)
                continue;

            int score = joinScore(from, e);
            if (e.mode == from.mode)
                score += 3; // staying in the mode is worth about a rank

            // Sharing a mood carries the *feel* across a section change, which is usually what you
            // want and never what you must have - so it nudges rather than gates.
            for (const auto* m : from.moods)
                for (const auto* n : e.moods)
                    if (juce::String(m) == n) { score += 1; break; }

            scored.push_back({ score, &e });
        }

        // Stable by construction: equal scores keep table order, so the answer to a given question
        // is the same every time it is asked. A shuffled "best" list is one you cannot learn.
        std::stable_sort(scored.begin(), scored.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });

        std::vector<const Entry*> out;
        for (const auto& [score, e] : scored)
        {
            if ((int) out.size() >= limit)
                break;
            juce::ignoreUnused(score);
            out.push_back(e);
        }
        return out;
    }

    // The numeral one step of a named row is written as, straight out of the table - `bVII`, not a
    // degree index. Empty when the name or the step does not resolve.
    //
    // **This exists because a stored degree does not survive a change of mode.** A pad carries
    // `degree`, an index into whatever mode it was generated in, and the strip draws its numeral
    // against whatever mode the session is in now. For every other source those are the same mode
    // and the index is fine. A library row is generated against its *own* mode (see `chordsFor`'s
    // note on why), so an Andalusian cadence dropped into a C major session had its i, bVII, bVI, V
    // read back as I, vii, vi, V - four wrong numerals under a bracket correctly naming the
    // progression they came from, which is worse than no numeral at all.
    //
    // A row and a step name the chord exactly and need no mode, so a pad that has them should use
    // this and never the degree. `ChordPads` does.
    inline juce::String numeralAt(const juce::String& name, int step)
    {
        if (name.isEmpty() || step < 0)
            return {};
        for (const auto& e : table())
        {
            if (name != e.name)
                continue;
            const auto tokens = juce::StringArray::fromTokens(juce::String(e.numerals), " ", "");
            return step < tokens.size() ? tokens[step] : juce::String();
        }
        return {};
    }

    // The row of a given name, or null. The one lookup by name, for a `ChordPad::progression` to
    // find its way back to the row it came from.
    inline const Entry* byName(const juce::String& name)
    {
        if (name.isEmpty())
            return nullptr;
        for (const auto& e : table())
            if (name == e.name)
                return &e;
        return nullptr;
    }

    // Every token of every row parses, which is the one thing a table of hand-typed numerals
    // cannot be trusted about. Returns the first bad "row name: token", or empty when clean.
    inline juce::String firstParseFailure()
    {
        for (const auto& e : table())
            for (const auto& token : juce::StringArray::fromTokens(juce::String(e.numerals), " ", ""))
                if (token.isNotEmpty() && ! markov::detail::parseNumeralToken(token, 0).valid)
                    return juce::String(e.name) + ": " + token;
        return {};
    }

    // Every tag of every row is in the vocabulary. Same reasoning: a misspelt mood is a row that
    // can never be found, and nothing on screen would ever say so.
    inline juce::String firstUnknownTag()
    {
        const auto known = [](const std::vector<const char*>& list, const char* tag)
        {
            for (const auto* k : list)
                if (juce::String(k) == tag)
                    return true;
            return false;
        };
        for (const auto& e : table())
        {
            for (const auto* m : e.moods)
                if (! known(moods(), m))
                    return juce::String(e.name) + ": mood " + m;
            for (const auto* g : e.genres)
                if (! known(genres(), g))
                    return juce::String(e.name) + ": genre " + g;
        }
        return {};
    }
} // namespace keys::chordlib
