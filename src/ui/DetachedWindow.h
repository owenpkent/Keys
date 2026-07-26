#pragma once

#include "KeysLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <okstudio/MouseOnly.h>
#include <functional>

namespace keys
{
// A section of the editor, popped out into its own resizable window.
//
// Two sections use this. The **keybed**, because Owen plays with one mouse and key width is
// the accuracy budget: he wants the keyboard as wide and as tall as the screen allows without
// every other section stretching to match. And the **arpeggiator**, because its control band
// and twelve slots want more room than a docked row can spare, and because an arp is something
// you set up once and then leave running while you play.
//
// It owns nothing. The editor keeps owning the components and simply re-parents them in and out
// of `holder`; this window must therefore be destroyed before those components are (KeysEditor's
// destructor does it explicitly).
//
// This is the only place Keys opens OS windows. It is justified by the resize being the whole
// point of the feature, and it stays a plain document window so a DAW can put it wherever the
// user drags it.
//
// (Was KeyboardWindow until the arpeggiator wanted the same trick; nothing in it was ever
// keyboard-specific except the title and the minimum size, which are parameters now.)
class DetachedWindow : public juce::DocumentWindow
{
public:
    // `holder` is where the editor re-parents the section; the window does not own it.
    DetachedWindow(const juce::String& title, juce::LookAndFeel& lnf, juce::Component& holder,
                   juce::Rectangle<int> startBounds, juce::Point<int> minSize,
                   juce::Point<int> defaultSize, std::function<void()> onCloseButton,
                   std::function<void()> onMoved)
        : juce::DocumentWindow(title, skin::bgBot, juce::DocumentWindow::closeButton, false),
          closeHandler(std::move(onCloseButton)), movedHandler(std::move(onMoved))
    {
        setLookAndFeel(&lnf);
        setTitleBarHeight(38); // window buttons become 38 px targets (mouse-only floor is 34)
        setUsingNativeTitleBar(false);
        setContentNonOwned(&holder, false);
        setResizable(true, true);

        // Below the minimum the controls stop being clickable, which for a one-mouse player is
        // the same as broken.
        setResizeLimits(minSize.x, minSize.y, 4000, 1600);

        if (startBounds.getWidth() >= minSize.x && startBounds.getHeight() >= minSize.y)
            setBounds(startBounds);
        else
            centreWithSize(defaultSize.x, defaultSize.y);

        setVisible(true);
        addToDesktop();

        // The bounds above came out of a saved session, which may have been saved on a
        // different display. A title bar off-screen is a window Owen can never move or
        // close again: there is no Alt+Space escape hatch here (okstudio/MouseOnly.h).
        // Must run after addToDesktop, so the frame it clamps is the real one.
        okstudio::ui::ensureWindowReachable(*this);

        toFront(true);
    }

    ~DetachedWindow() override
    {
        clearContentComponent(); // hand the section back before the window unwinds
        setLookAndFeel(nullptr);
    }

    void closeButtonPressed() override
    {
        if (closeHandler)
            closeHandler(); // the editor re-docks the section and deletes this window
    }

    // Remember where Owen put it, so the next session opens it in the same place.
    void moved() override
    {
        juce::DocumentWindow::moved();
        if (movedHandler)
            movedHandler();
    }

    void resized() override
    {
        juce::DocumentWindow::resized();
        if (movedHandler)
            movedHandler();
    }

private:
    std::function<void()> closeHandler, movedHandler;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DetachedWindow)
};
} // namespace keys
