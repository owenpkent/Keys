#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace keys::cc
{
// Friendly names for the controllers a fader is likely to drive; everything else
// still shows by number in the full list.
inline juce::String name(int cc)
{
    switch (cc)
    {
        case 1:  return "Mod";
        case 2:  return "Breath";
        case 5:  return "Porta Time";
        case 7:  return "Volume";
        case 10: return "Pan";
        case 11: return "Expression";
        case 64: return "Sustain";
        case 65: return "Portamento";
        case 71: return "Resonance";
        case 72: return "Release";
        case 73: return "Attack";
        case 74: return "Cutoff";
        case 91: return "Reverb";
        case 93: return "Chorus";
        default: return {};
    }
}

inline juce::String label(int cc)
{
    const auto n = name(cc);
    return "CC" + juce::String(cc) + (n.isEmpty() ? juce::String() : " " + n);
}

// Mouse-only CC picker: the named controllers up top, then every CC in banks of 16.
// Octavium buried this in a config dialog; here it is one click on the assignment.
inline void showMenu(juce::Component& anchor, int current, std::function<void(int)> pick)
{
    juce::PopupMenu m;
    for (int cc : { 1, 2, 5, 7, 10, 11, 64, 65, 71, 72, 73, 74, 91, 93 })
        m.addItem(label(cc), true, cc == current, [pick, cc] { pick(cc); });
    m.addSeparator();
    for (int base = 0; base < 128; base += 16)
    {
        juce::PopupMenu bank;
        for (int cc = base; cc < base + 16; ++cc)
            bank.addItem(label(cc), true, cc == current, [pick, cc] { pick(cc); });
        m.addSubMenu(juce::String(base) + "-" + juce::String(base + 15), bank, true,
                     nullptr, current >= base && current < base + 16, 0);
    }
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor));
}
} // namespace keys::cc
