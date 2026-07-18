#pragma once

#include <vector>

// The Markov chord-progression corpus. Hand-authored for Keys, not ported from Octavium.
//
// Octavium's Markov generator (app/chord_progression.py) builds its transition tables by
// scanning resources/{Major,Minor,Modal}/ for MIDI files named
// "{Key} - {Numerals} - {Moods}.mid". That tree was never committed to the Octavium repo in
// the mood-tagged, Modal-inclusive form the code actually reads: it's gitignored, the only
// history under resources/ is the raw upstream MIDI library (Major/Minor only, no mood
// tags) which was later deleted outright, and the commit that added the Markov generator
// touched no data files at all. In a real Octavium install the generator's index is
// therefore empty and every generate() call silently falls back to sixteen repeats of the
// tonic (see the port spec, section A0, for the full paper trail). There is nothing to port.
//
// So this corpus is authored fresh: recognisable, well-established progressions (pop, jazz
// ii-V-I families, a 12-bar blues, doo-wop, the Andalusian cadence, minor-key anthems, and
// modal vamps for Dorian/Mixolydian/Lydian/Phrygian) rather than a transcription of data
// that never existed. Roughly a third of the entries are tagged Major, a third Minor, a
// third Modal. Each numeral sequence follows the token grammar in ChordMarkov.h exactly
// (accidental + roman numeral + optional quality suffix), so the algorithm can't tell this
// corpus apart from a real one.
//
// A note on spelling minor/modal degrees: the roman-numeral-to-semitone table (ChordMarkov.h,
// mirroring Octavium's parse_numeral_token) is a fixed *major-scale* degree table used for
// every mode -- VI is always 9 semitones above the tonic, never 8. So natural minor's flat
// submediant/mediant/subtonic must be spelled bVI/bIII/bVII here, not VI/III/VII; the same
// goes for the modal vamps below (Phrygian's characteristic major triad a half-step up from
// the tonic is bII, Mixolydian's characteristic flat-seven is bVII, and so on). Plain
// uppercase numerals in a minor/modal entry (e.g. the raised "V" in a harmonic-minor
// cadence) are deliberate, not typos.
namespace keys::markov
{
    // Corpus category. Matches the markovMode parameter order used throughout ChordMarkov.h
    // (0 = Major, 1 = Minor, 2 = Modal); named here only to keep the table below readable,
    // the public API takes a plain int.
    constexpr int kMajor = 0;
    constexpr int kMinor = 1;
    constexpr int kModal = 2;

    struct Progression
    {
        int mode;
        std::vector<const char*> moods;     // 1-3 tags, drawn from that mode's vocabulary
        std::vector<const char*> numerals;  // 3-8 tokens, grammar per ChordMarkov.h's A3 parser
    };

    inline const std::vector<Progression>& corpus()
    {
        static const std::vector<Progression> c = {
            // ============================================================================
            // Major -- mood tags: Hopeful, Romantic, Joyful, Triumphant, Nostalgic, Peaceful,
            // Playful, Relaxed, Tender, Spiritual, Excited, Empowered.
            // ============================================================================
            { kMajor, { "Hopeful", "Joyful" },              { "I", "V", "vi", "IV" } },                    // the "Axis" progression
            { kMajor, { "Nostalgic", "Hopeful" },            { "vi", "IV", "I", "V" } },                    // Axis, vi start
            { kMajor, { "Joyful", "Triumphant" },            { "I", "IV", "V", "I" } },                     // classic three-chord cadence
            { kMajor, { "Nostalgic", "Romantic" },           { "I", "vi", "IV", "V" } },                    // 50s progression (doo-wop)
            { kMajor, { "Playful", "Nostalgic" },            { "I", "vi", "ii", "V" } },                    // ragtime turnaround
            { kMajor, { "Relaxed", "Spiritual" },            { "iim7", "V7", "IM7" } },                     // jazz ii-V-I
            { kMajor, { "Spiritual", "Peaceful" },           { "I", "IV", "I", "V" } },                     // hymn cadence
            { kMajor, { "Romantic", "Tender" },              { "I", "iii", "IV", "V" } },                   // mediant colour
            { kMajor, { "Hopeful", "Excited" },              { "I", "IV", "vi", "V" } },                    // pop lift
            { kMajor, { "Joyful", "Excited" },               { "I", "V", "IV", "V" } },                     // rock and roll
            { kMajor, { "Playful", "Hopeful" },              { "I", "ii", "IV", "V" } },                    // supertonic approach
            { kMajor, { "Nostalgic", "Tender" },             { "vi", "V", "IV", "V" } },                    // emotional build
            { kMajor, { "Triumphant", "Empowered" },         { "I", "V", "vi", "iii", "IV", "I", "IV", "V" } }, // extended pop anthem
            { kMajor, { "Playful", "Excited" },              { "I7", "IV7", "I7", "I7", "IV7", "IV7", "I7", "V7" } }, // 12-bar blues (condensed)
            { kMajor, { "Triumphant", "Spiritual" },         { "I", "vi", "IV", "I", "V", "I" } },          // extended cadence
            { kMajor, { "Romantic", "Relaxed" },             { "IM7", "vim7", "iim7", "V7" } },             // circle progression (jazz pop)
            { kMajor, { "Spiritual", "Triumphant" },         { "I", "IV", "I", "IV", "V", "I" } },          // gospel vamp
            { kMajor, { "Hopeful", "Empowered" },            { "vi", "IV", "V", "I" } },                    // pop cadence
            { kMajor, { "Nostalgic", "Romantic", "Peaceful" }, { "I", "iii", "vi", "IV", "I", "V", "I" } }, // extended sentimental
            { kMajor, { "Tender", "Peaceful" },              { "Isus4", "I", "IV", "V" } },                 // sus resolution
            { kMajor, { "Peaceful", "Tender" },              { "I", "Vsus4", "V", "I" } },                  // soft cadence
            { kMajor, { "Romantic", "Tender" },              { "Iadd9", "IV", "V", "Iadd9" } },             // add9 colour
            { kMajor, { "Relaxed", "Romantic" },             { "I6", "IV", "V", "vi" } },                   // sixth-chord colour
            { kMajor, { "Relaxed", "Playful" },              { "I69", "IV", "I69", "V" } },                 // lounge 6/9
            { kMajor, { "Hopeful", "Triumphant" },           { "I", "IV", "ii", "V", "I" } },               // ii insertion
            { kMajor, { "Spiritual", "Peaceful" },           { "I", "vi", "IV", "I", "IV", "V", "I" } },    // extended hymn
            { kMajor, { "Romantic", "Relaxed" },             { "IM7", "IVM7", "iim7", "V7" } },             // jazz ballad
            { kMajor, { "Triumphant", "Hopeful" },           { "I", "IV", "I", "vi", "IV", "V", "I" } },    // extended plagal
            { kMajor, { "Nostalgic", "Tender" },             { "vi", "iii", "IV", "I", "V" } },             // descending emotional
            { kMajor, { "Spiritual", "Triumphant", "Empowered" }, { "I", "I", "IV", "I", "V", "IV", "I", "I" } }, // simple anthem

            // ============================================================================
            // Minor -- mood tags: Dark, Mysterious, Melancholic, Dramatic, Tense, Rebellious,
            // Haunting, Suspenseful, Nostalgic, Empowered, Triumphant.
            // ============================================================================
            { kMinor, { "Dark", "Dramatic" },                { "i", "bVI", "bIII", "bVII" } },              // epic minor-key rock
            { kMinor, { "Dramatic", "Tense" },               { "i", "bVII", "bVI", "V" } },                 // Andalusian cadence
            { kMinor, { "Melancholic", "Dark" },             { "i", "iv", "v", "i" } },                     // natural minor cadence
            { kMinor, { "Mysterious", "Haunting" },          { "i", "iv", "bVII", "bIII" } },                // rock minor vamp
            { kMinor, { "Empowered", "Triumphant" },         { "i", "bVI", "bVII", "i" } },                 // minor anthem
            { kMinor, { "Melancholic", "Nostalgic" },        { "i", "v", "bVI", "bIII" } },                 // melancholic build
            { kMinor, { "Dramatic", "Tense" },               { "i", "iv", "i", "V" } },                     // harmonic minor cadence
            { kMinor, { "Dark", "Mysterious" },              { "i", "bIII", "bVII", "iv" } },                // aeolian vamp
            { kMinor, { "Dramatic", "Suspenseful" },         { "i", "iidim", "V", "i" } },                   // functional minor cadence
            { kMinor, { "Dramatic", "Triumphant" },          { "i", "bVI", "iv", "V" } },                    // dramatic rise
            { kMinor, { "Triumphant", "Empowered", "Dramatic" }, { "i", "V", "i", "iv", "bVII", "bIII", "bVII", "V" } }, // epic extended minor
            { kMinor, { "Tense", "Dark" },                   { "i", "iv", "bVI", "V" } },                    // minor pop-rock cadence
            { kMinor, { "Haunting", "Mysterious" },          { "i", "bVII", "iv", "i" } },                   // drone-like riff
            { kMinor, { "Melancholic", "Nostalgic" },        { "i", "bIII", "iv", "i" } },                   // folk minor
            { kMinor, { "Melancholic", "Suspenseful" },      { "im7", "ivm7", "V7", "im7" } },               // minor jazz turnaround
            { kMinor, { "Tense", "Suspenseful" },            { "i", "iidim", "bIII", "iv" } },               // ascending tension
            { kMinor, { "Dramatic", "Empowered" },           { "i", "bVI", "bIII", "V" } },                  // dramatic rise variant
            { kMinor, { "Rebellious", "Dark" },              { "i", "iv", "bVII", "i" } },                   // simple rock vamp
            { kMinor, { "Haunting", "Mysterious" },          { "i", "bVII", "bVI", "bVII" } },               // oscillating dark vamp
            { kMinor, { "Melancholic", "Dramatic" },         { "i", "V", "bVI", "bIII" } },                  // minor circle
            { kMinor, { "Suspenseful", "Tense" },            { "i", "iv", "V", "bVI" } },                    // deceptive cadence
            { kMinor, { "Empowered", "Triumphant" },         { "i", "bIII", "bVI", "bVII" } },               // rising anthem
            { kMinor, { "Melancholic", "Nostalgic" },        { "i", "v", "iv", "i" } },                      // pure natural minor
            { kMinor, { "Dramatic", "Dark" },                { "i", "bVII", "bIII", "bVI" } },               // descending epic
            { kMinor, { "Tense", "Suspenseful" },            { "i", "iidim", "iv", "V" } },                  // jazz-inflected minor
            { kMinor, { "Dramatic", "Rebellious" },          { "i", "iv", "i", "bVII", "bVI", "V" } },       // extended minor cadence
            { kMinor, { "Dark", "Dramatic" },                { "i", "bVI", "V", "i" } },                     // short dramatic cadence
            { kMinor, { "Nostalgic", "Melancholic" },        { "i", "bIII", "iv", "V" } },                   // folk-rock minor
            { kMinor, { "Dramatic", "Tense", "Empowered" },  { "i", "V7", "i", "iv", "V7", "i" } },          // minor with dominant seven
            { kMinor, { "Rebellious", "Tense" },             { "i", "bVII", "iv", "V" } },                   // rock cadence variant

            // ============================================================================
            // Modal -- mood tags reuse Major/Minor tags where they fit (Mysterious, Dark,
            // Haunting, Peaceful, Playful, Relaxed, Spiritual, Triumphant, Nostalgic,
            // Empowered), plus two new ones for the floaty/soundtrack character this bucket
            // often has: Dreamy, Cinematic.
            // ============================================================================
            // --- Dorian: minor tonic, major IV is the signature colour ---
            { kModal, { "Relaxed", "Mysterious" },           { "i", "IV", "i", "IV" } },                     // Dorian vamp
            { kModal, { "Dreamy", "Peaceful" },              { "i", "bVII", "IV", "i" } },                   // Dorian descent
            { kModal, { "Empowered", "Relaxed" },            { "i", "IV", "bVII", "i" } },                   // Dorian rise and fall
            { kModal, { "Dreamy", "Cinematic" },             { "i", "IV", "i", "bVII", "IV", "i" } },        // Dorian extended loop
            { kModal, { "Mysterious", "Dark" },              { "i", "v", "IV", "i" } },                      // Dorian minor-v colour
            { kModal, { "Relaxed", "Nostalgic" },            { "i", "bVII", "i", "IV" } },                   // Dorian turnaround
            { kModal, { "Cinematic", "Empowered" },          { "i", "IV", "bVII", "IV", "i" } },             // Dorian anthem vamp

            // --- Mixolydian: major tonic, flat-seven is the signature colour ---
            { kModal, { "Playful", "Empowered" },            { "I", "bVII", "IV", "I" } },                   // Mixolydian vamp
            { kModal, { "Relaxed", "Playful" },              { "I", "bVII", "I", "bVII" } },                 // Mixolydian oscillation
            { kModal, { "Empowered", "Triumphant" },         { "I", "IV", "bVII", "I" } },                   // Mixolydian rock
            { kModal, { "Triumphant", "Nostalgic" },         { "I", "bVII", "IV", "I", "bVII", "I" } },      // Mixolydian extended vamp
            { kModal, { "Playful", "Relaxed" },              { "I", "IV", "I", "bVII" } },                   // Mixolydian lift
            { kModal, { "Empowered", "Cinematic" },          { "I", "IV", "bVII", "IV", "I" } },             // Mixolydian build
            { kModal, { "Dreamy", "Playful" },               { "I", "bVII", "IV", "bVII", "I" } },           // Mixolydian float

            // --- Lydian: major tonic, major II (raised 4th) is the signature colour ---
            { kModal, { "Dreamy", "Playful" },               { "I", "II", "I", "II" } },                     // Lydian raised-4 colour
            { kModal, { "Dreamy", "Triumphant" },            { "I", "II", "V", "I" } },                      // Lydian cadence
            { kModal, { "Cinematic", "Peaceful" },           { "I", "V", "II", "I" } },                      // Lydian float
            { kModal, { "Playful", "Dreamy" },               { "I", "II", "iii", "I" } },                    // Lydian mediant colour
            { kModal, { "Nostalgic", "Dreamy" },             { "I", "vi", "II", "I" } },                     // Lydian submediant colour
            { kModal, { "Peaceful", "Dreamy", "Spiritual" }, { "I", "II", "vi", "I" } },                     // Lydian gentle vamp
            { kModal, { "Cinematic", "Triumphant" },         { "I", "II", "I", "V", "II", "I" } },           // Lydian extended float

            // --- Phrygian: minor tonic, major bII (flat-two) is the signature colour ---
            { kModal, { "Dark", "Mysterious" },              { "i", "bII", "i", "bII" } },                   // Phrygian half-step colour
            { kModal, { "Haunting", "Dark" },                { "i", "bII", "i" } },                          // Phrygian cadence
            { kModal, { "Cinematic", "Dark" },               { "i", "bVII", "bII", "i" } },                  // Phrygian vamp
            { kModal, { "Haunting", "Mysterious" },          { "i", "bVI", "bII", "i" } },                   // Phrygian dark colour
            { kModal, { "Dark", "Empowered" },               { "i", "bII", "bVII", "i" } },                  // Phrygian rise
            { kModal, { "Cinematic", "Haunting" },           { "i", "bII", "i", "bVII", "bII", "i" } },      // Phrygian extended loop
            { kModal, { "Mysterious", "Nostalgic" },         { "i", "bIII", "bII", "i" } },                  // Phrygian mediant colour
        };
        return c;
    }
} // namespace keys::markov
