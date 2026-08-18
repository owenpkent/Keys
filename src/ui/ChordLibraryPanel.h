#pragma once

#include "../ChordLibrary.h"
#include "../PluginProcessor.h"
#include "ChordGenMenu.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace keys
{
// The chord library you can actually look at (2026-08-18, the second half of Owen's ask: "an
// outstanding library that makes it easy to compose").
//
// The Library *source* in the generator window shipped first and is genuinely useful - Fill under
// a mood is a fast way to work - but a filter is not browsing. You can see what came out of the
// table and never what is in it. This is the window that shows you the table.
//
// **A view onto `chordlib::table()` and `ChordGenMenu`, owning neither**, the same split
// `ChordGenPanel` documents at length and for the same reason: it is built when the window opens
// and destroyed when it closes, so nothing here may be the only copy of anything. The three
// filters are `ChordGenMenu`'s own, which is what makes this window and the generator's Library
// band one state rather than two that drift - set a mood here and Fill on the Pads bar obeys it.
//
// **Paged, not scrolled.** Twelve rows and a `<` `>` pair, exactly the shape the pad strip already
// uses. 348 rows is a scroll, and a scroll is the gesture the mouse-only contract is worst at: a
// scrollbar thumb is a small target that has to be dragged, and a wheel is not a mouse gesture
// Keys may require. A page is two clicks and every target on it is full size.
//
// **A row is a chord card that happens to hold several chords.** Click it to hear the whole
// progression - which is the question the tray cannot answer, since ii-V-I and ii-V-vi start
// identically - and the two buttons at its right end place it: into the generator's tray, where
// each chord becomes a candidate you can drag one at a time, or straight onto the page's empty
// pads. Nothing here writes over a chord you already have.
class ChordLibraryPanel : public juce::Component,
                          private juce::Timer
{
public:
    ChordLibraryPanel(KeysProcessor&, ChordGenMenu&);
    ~ChordLibraryPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;

    // The on-screen Close button; the title bar's X runs the same teardown through the editor.
    std::function<void()> onClose;

    // Placing a progression is the editor's job, not this window's: this class can no more name a
    // pad slot than the generator's tray could name one from a menu item. Both answer false when
    // there was no room, which is what greys the button.
    std::function<bool(const std::vector<KeysProcessor::ChordPad>&)> onSendToPads;
    std::function<bool()> onPageHasEmptyPad;

    // Filling the generator's tray needs that window to exist. The editor answers for whether it
    // does and does the writing; false greys the button, which is honest - "To tray" with no tray
    // open is a button that would appear to do nothing.
    std::function<bool(const std::vector<KeysProcessor::ChordPad>&)> onSendToTray;
    std::function<bool()> onTrayIsOpen;

    static constexpr int rowsPerPage = 12;

    // Derived from the layout below rather than chosen, the same as ChordGenPanel's.
    static juce::Point<int> contentSize();
    static juce::Point<int> minWindowSize();
    static juce::Point<int> defaultWindowSize();

private:
    void timerCallback() override;
    void buildControls();
    void refreshMatches();  // re-run the filter and clamp the page to it
    void refreshRowButtons();
    juce::Rectangle<float> rowBounds(int indexOnPage) const;
    int rowAt(juce::Point<float>) const;
    int numPages() const;
    const chordlib::Entry* entryAt(int indexOnPage) const;
    std::vector<KeysProcessor::ChordPad> padsFor(const chordlib::Entry&) const;

    KeysProcessor& processor;
    ChordGenMenu& gen;

    juce::Label title, countLabel;
    juce::TextButton closeButton { "Close" };

    juce::ComboBox moodBox, genreBox, functionBox;
    juce::Label moodLabel, genreLabel, functionLabel;

    juce::TextButton prevPage { "<" }, nextPage { ">" };
    juce::Label pageLabel;

    // One pair per row rather than one pair reused: they have to sit at twelve different heights
    // at once, and a button is a Component. `rowsPerPage` of each, laid out or hidden by resized().
    std::array<juce::TextButton, rowsPerPage> trayButtons, padsButtons;

    // What the current filter matches, as pointers into the static table (which outlives this
    // window many times over). Re-run whenever a filter changes, never per paint.
    std::vector<const chordlib::Entry*> matches;
    juce::String lastSignature; // the filter the matches were built for

    int page = 0;
    int hovered = -1;
    int playing = -1; // the row whose progression is sounding, for the lit fill

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordLibraryPanel)
};
} // namespace keys
