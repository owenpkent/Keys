#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace keys
{
// A combo box that reports **every** pick, including a pick of the item already on screen.
//
// juce::ComboBox drops that one. Its popup finishes through ComboBox::setSelectedId, which
// returns early when neither the id nor the label text has moved, so no Listener fires, no
// onChange fires, and an attached parameter is never written. On a stock box that is invisible,
// because a stock box only ever displays an item it is genuinely on: picking what is already
// ticked really is a no-op.
//
// Scale Compliance on the Pads bar is not that box. The parameter behind it is a continuous
// 0-100 and the bar offers five steps of it, so the box shows the *nearest* step: set 60 from
// the generator window's slider and the bar reads "50 %" while the value is 60. Picking "50 %"
// there has to write 50, and with a ComboBoxAttachment it did nothing at all - a dead click on
// a lit control, which is the one thing a mouse-only plugin must never have. The only way back
// to 50 from the bar was to pick a different step and come back.
//
// ComboBox::showPopup() is virtual for exactly this ("so that you can override it with your own
// custom popup"), so this builds the same menu, ticks the same item, and hands the picked id
// straight to `onPick` without going near setSelectedId's early return.
class StepComboBox final : public juce::ComboBox
{
public:
    StepComboBox() = default;

    // The picked item's id, fired even when it is the one the box was already showing. The
    // owner writes the parameter; this class deliberately holds no attachment of its own.
    std::function<void(int)> onPick;

    void showPopup() override
    {
        juce::PopupMenu menu;
        const int current = getSelectedId();
        for (int i = 0; i < getNumItems(); ++i)
        {
            const int id = getItemId(i);
            menu.addItem(id, getItemText(i), true, id == current);
        }

        auto& lf = getLookAndFeel();
        menu.setLookAndFeel(&lf);

        // The same options LookAndFeel_V2 builds for a stock combo, less the one thing that
        // cannot be reached from out here: getOptionsForComboBoxPopupMenu reads the standard
        // item height off ComboBox's private Label, so this passes the box's own height
        // instead. It costs nothing - KeysLookAndFeel::getIdealPopupMenuItemSize raises every
        // item to the 34 px mouse-only floor anyway.
        menu.showMenuAsync(juce::PopupMenu::Options()
                               .withTargetComponent(this)
                               .withItemThatMustBeVisible(current)
                               .withInitiallySelectedItem(current)
                               .withMinimumWidth(getWidth())
                               .withMaximumNumColumns(1)
                               .withStandardItemHeight(getHeight()),
                           [safe = juce::Component::SafePointer<StepComboBox>(this)](int result)
                           {
                               if (safe == nullptr)
                                   return;
                               // Always, dismissed or not: hidePopup() is what clears ComboBox's
                               // private `menuActive`, and without it the next click on the box
                               // opens nothing.
                               safe->hidePopup();
                               if (result == 0)
                                   return;
                               // Show the pick immediately, then let the owner write it. The
                               // parameter round-trip lands on the same item, so this is only
                               // ever the frame in between.
                               safe->setSelectedId(result, juce::dontSendNotification);
                               if (safe->onPick != nullptr)
                                   safe->onPick(result);
                           });
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepComboBox)
};
} // namespace keys
