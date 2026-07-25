#pragma once

#include "KeysLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <okstudio/MouseOnly.h>
#include <functional>

namespace keys
{
// The keybed, popped out into its own resizable window.
//
// Owen plays with one mouse, so key width is the accuracy budget: he wants to drag the
// keyboard as wide and as tall as the screen allows without every other section of the
// editor stretching to match. Docked, the keybed is one row of a fixed layout and the
// window's aspect is a compromise between the controls and the keys. Detached, it is the
// only thing in the window, so its size is entirely his.
//
// It owns nothing. The editor keeps owning the keyboard and wheel components and simply
// re-parents them in and out of `holder`; this window must therefore be destroyed before
// those components are (KeysEditor's destructor does it explicitly).
//
// This is the one place Keys opens an OS window. It is justified by the resize being the
// whole point of the feature, and it stays a plain always-on-top-of-nothing document
// window so a DAW can put it wherever the user drags it.
class KeyboardWindow : public juce::DocumentWindow
{
public:
    // `holder` is where the editor re-parents the keybed; the window does not own it.
    KeyboardWindow(juce::LookAndFeel& lnf, juce::Component& holder,
                   juce::Rectangle<int> startBounds, std::function<void()> onCloseButton,
                   std::function<void()> onMoved)
        : juce::DocumentWindow("Keys Keyboard", skin::bgBot,
                               juce::DocumentWindow::closeButton, false),
          closeHandler(std::move(onCloseButton)), movedHandler(std::move(onMoved))
    {
        setLookAndFeel(&lnf);
        setTitleBarHeight(38); // window buttons become 38 px targets (mouse-only floor is 34)
        setUsingNativeTitleBar(false);
        setContentNonOwned(&holder, false);
        setResizable(true, true);

        // Two octaves of 88-key white keys at a usable width, and enough height for the
        // control strip plus a keybed you can still hit. Below this the keys stop being
        // clickable, which for a one-mouse player is the same as broken.
        setResizeLimits(420, 190, 4000, 900);

        if (startBounds.getWidth() >= 420 && startBounds.getHeight() >= 190)
            setBounds(startBounds);
        else
            centreWithSize(1000, 300);

        setVisible(true);
        addToDesktop();

        // The bounds above came out of a saved session, which may have been saved on a
        // different display. A title bar off-screen is a window Owen can never move or
        // close again: there is no Alt+Space escape hatch here (okstudio/MouseOnly.h).
        // Must run after addToDesktop, so the frame it clamps is the real one.
        okstudio::ui::ensureWindowReachable(*this);

        toFront(true);
    }

    ~KeyboardWindow() override
    {
        clearContentComponent(); // hand the keybed back before the window unwinds
        setLookAndFeel(nullptr);
    }

    void closeButtonPressed() override
    {
        if (closeHandler)
            closeHandler(); // the editor re-docks the keyboard and deletes this window
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyboardWindow)
};
} // namespace keys
