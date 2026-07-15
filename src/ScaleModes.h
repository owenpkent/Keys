#pragma once

#include <juce_core/juce_core.h>
#include <okstudio/Scales.h>
#include <vector>

// Scale modes for chord generation, ported from Octavium (app/chord_autofill.py SCALE_MODES).
//
// This is deliberately NOT the same table as the kit's okstudio::scales, which drives Scale
// Lock. That one answers "is this note in the scale"; this one additionally answers "what
// chord quality sits on each degree", which is what the chord generator is built on. The
// two lists overlap but are not identical (Octavium has no Whole Tone; the kit has no
// per-degree qualities), so they stay separate rather than one pretending to be the other.
namespace keys::modes
{
    // The chord qualities a degree can carry. Generation expands these into the richer
    // types in ChordGen.h; naming stays in Chords.h.
    enum class Quality
    {
        major,
        minor,
        diminished,
        augmented
    };

    struct Mode
    {
        const char* name;
        std::vector<int> intervals;        // semitones from the root
        std::vector<Quality> qualities;    // chord quality per scale degree, parallel to intervals
        const char* emotion;               // the character this mode carries, shown in the UI
        const char* category;              // for grouping in the mode list
    };

    inline const std::vector<Mode>& all()
    {
        using Q = Quality;
        static const std::vector<Mode> m = {
            // --- Major modes ---
            { "Major (Ionian)", { 0, 2, 4, 5, 7, 9, 11 },
              { Q::major, Q::minor, Q::minor, Q::major, Q::major, Q::minor, Q::diminished },
              "Happy, Bright, Uplifting", "Major Modes" },
            { "Lydian", { 0, 2, 4, 6, 7, 9, 11 },
              { Q::major, Q::major, Q::minor, Q::diminished, Q::major, Q::minor, Q::minor },
              "Dreamy, Ethereal, Magical", "Major Modes" },
            { "Mixolydian", { 0, 2, 4, 5, 7, 9, 10 },
              { Q::major, Q::minor, Q::diminished, Q::major, Q::minor, Q::minor, Q::major },
              "Bluesy, Relaxed, Rock", "Major Modes" },

            // --- Minor modes ---
            { "Natural Minor (Aeolian)", { 0, 2, 3, 5, 7, 8, 10 },
              { Q::minor, Q::diminished, Q::major, Q::minor, Q::minor, Q::major, Q::major },
              "Sad, Melancholic, Reflective", "Minor Modes" },
            { "Dorian", { 0, 2, 3, 5, 7, 9, 10 },
              { Q::minor, Q::minor, Q::major, Q::major, Q::minor, Q::diminished, Q::major },
              "Jazzy, Sophisticated, Chill", "Minor Modes" },
            { "Phrygian", { 0, 1, 3, 5, 7, 8, 10 },
              { Q::minor, Q::major, Q::major, Q::minor, Q::diminished, Q::major, Q::minor },
              "Spanish, Dark, Exotic", "Minor Modes" },
            { "Locrian", { 0, 1, 3, 5, 6, 8, 10 },
              { Q::diminished, Q::major, Q::minor, Q::minor, Q::major, Q::major, Q::minor },
              "Unstable, Tense, Dissonant", "Minor Modes" },

            // --- Other scales ---
            { "Harmonic Minor", { 0, 2, 3, 5, 7, 8, 11 },
              { Q::minor, Q::diminished, Q::augmented, Q::minor, Q::major, Q::major, Q::diminished },
              "Classical, Dramatic, Middle Eastern", "Other Scales" },
            { "Melodic Minor", { 0, 2, 3, 5, 7, 9, 11 },
              { Q::minor, Q::minor, Q::augmented, Q::major, Q::major, Q::diminished, Q::diminished },
              "Jazz, Smooth, Complex", "Other Scales" },
            { "Blues", { 0, 3, 5, 6, 7, 10 },
              { Q::minor, Q::major, Q::diminished, Q::diminished, Q::minor, Q::major },
              "Bluesy, Gritty, Expressive", "Other Scales" },

            // --- Pentatonic ---
            { "Pentatonic Major", { 0, 2, 4, 7, 9 },
              { Q::major, Q::minor, Q::minor, Q::major, Q::minor },
              "Folk, Simple, Universal", "Pentatonic" },
            { "Pentatonic Minor", { 0, 3, 5, 7, 10 },
              { Q::minor, Q::major, Q::minor, Q::minor, Q::major },
              "Blues, Rock, Soulful", "Pentatonic" },
        };
        return m;
    }

    inline int count() { return (int) all().size(); }

    inline const Mode& get(int index)
    {
        const auto& m = all();
        return m[(size_t) juce::jlimit(0, (int) m.size() - 1, index)];
    }

    inline int indexOf(const juce::String& name)
    {
        const auto& m = all();
        for (int i = 0; i < (int) m.size(); ++i)
            if (name == m[(size_t) i].name)
                return i;
        return 0;
    }

    inline juce::StringArray names()
    {
        juce::StringArray out;
        for (const auto& m : all())
            out.add(m.name);
        return out;
    }

    // Modes worth borrowing from for each mode (modal interchange). Ported from
    // Octavium's _PARALLEL_MODES; drives the mid-range of Scale Compliance.
    inline std::vector<int> parallelModes(int modeIndex)
    {
        const auto idx = [](const char* n) { return indexOf(n); };
        switch (modeIndex)
        {
            case 0:  return { idx("Natural Minor (Aeolian)"), idx("Dorian"), idx("Mixolydian"), idx("Lydian") };
            case 1:  return { idx("Major (Ionian)"), idx("Mixolydian") };
            case 2:  return { idx("Major (Ionian)"), idx("Dorian") };
            case 3:  return { idx("Major (Ionian)"), idx("Dorian"), idx("Harmonic Minor") };
            case 4:  return { idx("Natural Minor (Aeolian)"), idx("Mixolydian"), idx("Major (Ionian)") };
            case 5:  return { idx("Natural Minor (Aeolian)"), idx("Harmonic Minor") };
            case 6:  return { idx("Phrygian"), idx("Natural Minor (Aeolian)") };
            case 7:  return { idx("Natural Minor (Aeolian)"), idx("Phrygian") };
            case 8:  return { idx("Dorian"), idx("Mixolydian"), idx("Harmonic Minor") };
            case 9:  return { idx("Mixolydian"), idx("Dorian"), idx("Pentatonic Minor") };
            case 10: return { idx("Major (Ionian)"), idx("Mixolydian") };
            case 11: return { idx("Natural Minor (Aeolian)"), idx("Blues") };
            default: return {};
        }
    }

    // The kit scale with the same intervals as this mode, for keeping Scale Lock in step
    // with the generator. Every mode here has an exact match in the kit table; matching on
    // intervals rather than a name map means a rename on either side can't silently
    // mis-pair them. Returns -1 if a future mode has no equivalent.
    inline int kitScaleIndex(int modeIndex)
    {
        const auto& want = get(modeIndex).intervals;
        const auto& kit = okstudio::scales::all();
        for (int i = 0; i < (int) kit.size(); ++i)
            if (kit[(size_t) i].intervals == want)
                return i;
        return -1;
    }

    // One-click key + mode pairs (Octavium EMOTION_PRESETS): the fastest way to a usable
    // key without knowing any theory, which is the point of the feature.
    struct Emotion
    {
        const char* label;
        int rootPc;        // 0 = C
        const char* mode;
    };

    inline const std::vector<Emotion>& emotions()
    {
        static const std::vector<Emotion> e = {
            { "Happy",     0,  "Major (Ionian)" },
            { "Sad",       9,  "Natural Minor (Aeolian)" },
            { "Dreamy",    7,  "Lydian" },
            { "Dark",      4,  "Phrygian" },
            { "Jazzy",     2,  "Dorian" },
            { "Bluesy",    9,  "Blues" },
            { "Epic",      2,  "Harmonic Minor" },
            { "Chill",     5,  "Mixolydian" },
            { "Mysterious", 11, "Locrian" },
            { "Smooth",    0,  "Melodic Minor" },
        };
        return e;
    }
} // namespace keys::modes
