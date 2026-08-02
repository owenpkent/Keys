#pragma once

#include "KeysHostProcessor.h"
#include "../PluginEditor.h"
#include <memory>

namespace keys
{
// "Load Instrument..." opens this overlay: every VST3 in the folders the DAW scans,
// filed into one collapsible folder per publisher, one left-click to load. Live's
// browser can't drag into a plugin window (its drags are internal to Live), so the
// browser comes to Keys Host instead. Publisher comes from the bundle's
// moduleinfo.json or the DLL's version resource — metadata only, no plugin is
// instantiated until a row is clicked, so listing can't crash and needs no scanner.
//
// Folders open closed. A big library listed flat is a long scroll to reach anything,
// and scrolling is the expensive gesture here; collapsed, the whole library is a
// short list of publishers with every instrument two clicks away.
class InstrumentPicker : public juce::Component
{
public:
    InstrumentPicker();

    std::function<void(const juce::File&)> onPick;
    std::function<void()> onBrowse; // the file-dialog fallback for odd install locations
    std::function<void()> onClose;

    void refresh(); // re-enumerate the VST3 folders and rebuild the rows

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override; // click outside the panel closes

private:
    juce::Rectangle<int> panelBounds() const;

    // One publisher = one folder: a clickable header, plus rows that are only on
    // screen while it is open.
    struct Folder
    {
        juce::String name;
        std::unique_ptr<juce::TextButton> header;
        juce::OwnedArray<juce::TextButton> rows;
        bool open = false;
    };

    // A folder and the instruments inside it must not look alike. On the skin's default
    // button both are the same raised pill with centred text, so an indent and a small
    // triangle were the only difference and the list read as one flat run of buttons.
    // Folders keep the raised chip with a bright semibold caption; instruments are
    // recessed and dim, so the eye sorts containers from contents before reading a word.
    struct FolderLookAndFeel : KeysLookAndFeel
    {
        void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    };

    struct ItemLookAndFeel : KeysLookAndFeel
    {
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                                  bool highlighted, bool down) override;
        void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    };

    void applyFolderState(Folder&); // header caption + row visibility, no relayout
    void setFolderOpen(Folder&, bool open);

    FolderLookAndFeel folderLnf;
    ItemLookAndFeel itemLnf;

    juce::Label title;
    juce::TextButton rescanButton { "Rescan" }, browseButton { "Browse files..." }, closeButton { "Close" };
    juce::Viewport viewport;
    juce::Component rowHolder;
    juce::OwnedArray<Folder> folders;
    std::unique_ptr<juce::TextButton> emptyRow; // stands in when nothing was found
    juce::StringArray openFolderNames;          // so Rescan doesn't re-collapse everything

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentPicker)
};

// Floating native-titlebar window for the hosted instrument's GUI, so keyboard and
// synth are two windows rather than one stack. Its close button only hides it; the
// top bar's Show/Hide toggle is the primary, always-visible control.
class InstrumentWindow : public juce::DocumentWindow
{
public:
    InstrumentWindow(const juce::String& name, juce::Component& content,
                     std::function<void()> onCloseClick);
    void closeButtonPressed() override;

private:
    std::function<void()> onCloseClick;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentWindow)
};

// The plugin window itself is the full Keys editor plus a slim top bar; the hosted
// instrument's GUI lives in the floating InstrumentWindow above. The top bar is
// mouse-only like everything else: single left-click, targets >= 34 px.
class KeysHostEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       private juce::ChangeListener
{
public:
    explicit KeysHostEditor(KeysHostProcessor&);
    ~KeysHostEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override; // skins the standalone wrapper's window chrome

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    // Size the window to what the embedded Keys editor needs, and move the resize *floor* with
    // it so nothing can leave the window shorter than its content. The keyboard is the last
    // section laid out, so every pixel the window is short by comes off the bottom of it with
    // nothing on screen to say so. See the definition.
    void fitToKeysHeight(int keysWanted);

    void openPicker();
    void closePicker();
    void loadAndReport(const juce::File&);
    void chooseInstrument();
    void openInstrumentEditor();  // (re)build the hosted GUI + its window from the current instance
    void closeInstrumentEditor(); // must run before the instance goes away
    void setInstrumentShown(bool shown);
    void updateBar();
    void placeInstrumentWindow(); // above the keyboard window, clamped on-screen

    KeysHostProcessor& host;

    // The bar and the picker draw with the same skin as the embedded editor (which
    // only applies its own LookAndFeel to its subtree, not to this parent).
    KeysLookAndFeel hostLnf;
    juce::Component::SafePointer<juce::DocumentWindow> styledWindow; // standalone chrome we skinned

    juce::TextButton loadButton { "Load Instrument..." };
    juce::TextButton showHideButton;
    juce::TextButton ejectButton { "Eject" };
    juce::Label instLabel;

    std::unique_ptr<juce::AudioProcessorEditor> instEditor;
    std::unique_ptr<InstrumentWindow> instWindow;
    bool instShown = true;
    bool instPlaceDeferred = false; // one retry only; see placeInstrumentWindow

    KeysEditor keysEditor;

    std::unique_ptr<InstrumentPicker> picker; // only alive while the overlay is open
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysHostEditor)
};
} // namespace keys
