#pragma once

#include "KeysHostProcessor.h"
#include "../PluginEditor.h"
#include <memory>

namespace keys
{
// "Load Instrument..." opens this overlay: every VST3 in the folders the DAW scans,
// grouped under publisher headers, one left-click to load. Live's browser can't drag
// into a plugin window (its drags are internal to Live), so the browser comes to
// Keys Host instead. Publisher comes from the bundle's moduleinfo.json or the DLL's
// version resource — metadata only, no plugin is instantiated until a row is
// clicked, so listing can't crash and needs no scanner.
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

    juce::Label title;
    juce::TextButton rescanButton { "Rescan" }, browseButton { "Browse files..." }, closeButton { "Close" };
    juce::Viewport viewport;
    juce::Component rowHolder;
    juce::OwnedArray<juce::Component> items; // publisher headers (Labels) and plugin rows (TextButtons)

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

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

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

    juce::TextButton loadButton { "Load Instrument..." };
    juce::TextButton showHideButton;
    juce::TextButton ejectButton { "Eject" };
    juce::Label instLabel;

    std::unique_ptr<juce::AudioProcessorEditor> instEditor;
    std::unique_ptr<InstrumentWindow> instWindow;
    bool instShown = true;

    KeysEditor keysEditor;

    std::unique_ptr<InstrumentPicker> picker; // only alive while the overlay is open
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysHostEditor)
};
} // namespace keys
