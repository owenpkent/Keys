#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace keys::combomenu
{
// Build the menu a `juce::ComboBox` would show for itself, item for item.
//
// Keys hand-rolls a ComboBox popup in two places - the harmony dropdown, which wants two
// columns, and `StepComboBox`, which wants a pick callback rather than a selection - and
// **overriding `showPopup` means every ComboBox API has to keep working through it**. That is
// not a small print: on 2026-08-19 the harmony popup passed a loop *index* into
// `isItemEnabled`, which takes an item **ID**, and because `isItemEnabled` answers false for
// an id no item has, the first row - "Off" - was the one row greyed out. It took three days
// and Owen ("I can't turn off the harmony. off is grey") to find, because every other row
// looked correct.
//
// So the loop lives here once, and the two callers differ only in what they pass. The rule the
// bug turned on is worth stating where somebody writing a third popup will read it:
//
//   **`getItemText` and `getItemId` take an index. `isItemEnabled` takes an ID.**
//
// `breakBefore` is called with the index of each row after the first and adds a column break
// above that row when it answers true. Empty means one column, which is what a stock ComboBox
// draws.
inline juce::PopupMenu build(const juce::ComboBox& box,
                             const std::function<bool(int index)>& breakBefore = {})
{
    juce::PopupMenu menu;
    // Hoisted: `getSelectedId` walks the item list, and asking it once per row turned a
    // 27-row menu into 27 scans for an answer that cannot change while this loop runs.
    const int selected = box.getSelectedId();

    for (int i = 0; i < box.getNumItems(); ++i)
    {
        if (i > 0 && breakBefore && breakBefore(i))
            menu.addColumnBreak();

        // The id is fetched once and used for all three of the things that need it - the
        // menu's own result id, the enabled lookup, and the tick - so no future edit can pass
        // one of them an index while its neighbours get an id. That is exactly the shape the
        // 2026-08-19 bug had.
        const int itemId = box.getItemId(i);
        menu.addItem(itemId, box.getItemText(i), box.isItemEnabled(itemId), itemId == selected);
    }
    return menu;
}
} // namespace keys::combomenu
