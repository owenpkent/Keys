#pragma once

#include "PluginProcessor.h"
#include "ui/ArpPanel.h"
#include "ui/ChordGenPanel.h"
#include "ui/ChordPads.h"
#include "ui/DetachedWindow.h"
#include "ui/KeysLookAndFeel.h"
#include "ui/KnobBank.h"
#include "ui/RangeSlider.h"
#include "ui/SectionBar.h"
#if KEYS_TRANSCRIBE
#include "ui/TranscribePanel.h"
#endif
#include <okstudio/Updater.h>
#include <array>
#include <memory>

// The playing surface, the piano, is picked at compile time, not by a tab: one
// product, one surface; see CHANGELOG for why the old five-tab arrangement is gone.
#include "ui/PianoKeyboard.h"

namespace keys
{
class KeysEditor : public juce::AudioProcessorEditor,
                   private juce::Timer
{
public:
    explicit KeysEditor(KeysProcessor&);
    ~KeysEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override; // skins the standalone wrapper's window chrome

    // True when this editor lives inside another editor (Keys Host). An embedded
    // editor must never setSize() itself; the parent owns geometry and reacts to
    // layout changes on its own.
    void setEmbedded(bool b) { embedded = b; }

    // Embedded only: how tall this editor would like to be, after a fold or a change of
    // centre view. The centre views (the generator, the arp with its step editor) need
    // far more room than the player, so a parent that ignores this clips the keybed off
    // the bottom. Keys Host grows itself to fit.
    std::function<void(int)> onIdealHeightChanged;

private:
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void addCombo(juce::ComboBox&, juce::Label&, const juce::String& text, const juce::StringArray& items,
                  const juce::String& paramID, std::unique_ptr<ComboAtt>&);
    void showUpdate(const okstudio::updater::UpdateInfo&);
    void toggleEditPad(int slot); // link a chord pad to the keyboard for editing
    void endPadEdit();

    // --- Folding layout ------------------------------------------------------------
    // Which panel occupies the centre of the editor. The generator used to be a sheet thrown
    // over the whole plugin, which hid the keyboard behind the thing you were editing; it is
    // now a view that swaps in where the knob bank sits. `folded` is the third state: no
    // centre at all.
    //
    // The arpeggiator was a third view here until Owen asked for it to stop competing with
    // the other two (2026-07-25). It is a section of its own now, like the pads, so the arp
    // and the knobs (or the generator) can be on screen together.
    enum CentreView { viewPerform = 0, viewChords = 1 };

    void setCentreView(int view);      // picks a view, unfolding the section if needed
    void refreshCentrePanels();        // creates/destroys the panel the state calls for
    void refreshArpPanel();            // the arp section's panel follows its own fold
    void refreshTranscribePanel();     // ditto the Transcribe section
    void syncSectionControls();        // toggle states + visibility from processor.layout
    int  centreHeight() const;         // height the current centre view asks for, 0 if folded
    int  arpHeight() const;            // height the arp section asks for, 0 if folded/detached
    int  transcribeHeight() const;     // height the Transcribe section asks for, 0 if folded
    int  idealHeight() const;          // total height with the current folds
    int  minWidthForView() const;      // the centre views carry more controls than the player
    void applyLayout();                // resize to fit the folds (unless embedded), then resized()
    void setKeyboardDetached(bool);    // move the keybed in/out of its own window
    void setArpDetached(bool);         // move the arp panel in/out of its own window
    void rememberDetachedBounds();
    void rememberArpDetachedBounds();
    void layoutKeybed();               // wheels + keys inside keybedHolder, wherever it lives
    void layoutArpHolder();            // the arp panel inside arpHolder, wherever it lives
    void layoutToolRow(juce::Rectangle<int>);

    // Wheels + keybed as one unit, so detaching re-parents a single component instead of
    // shuffling three. Always the parent of `keyboard`, `modWheel` and `pitchWheel`;
    // its own parent is either this editor or the detached window's content slot.
    struct KeybedHolder : juce::Component
    {
        std::function<void()> layout;
        void resized() override { if (layout) layout(); }
        void paint(juce::Graphics& g) override { g.fillAll(skin::bgBot); }
    };

    KeysProcessor& processor;
    KeysLookAndFeel lnf;

    // The default LookAndFeel_V4 linear slider caps its track at ~6 px no matter how
    // wide the component is; the performance wheels want a hardware-wheel look — a
    // wide groove with a chunky grab bar.
    struct WheelLookAndFeel : KeysLookAndFeel
    {
        void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                              float minPos, float maxPos, juce::Slider::SliderStyle,
                              juce::Slider&) override;
    };
    WheelLookAndFeel wheelLnf;

    juce::Label title;
    juce::Rectangle<int> titleCaption; // "OK STUDIO" wordmark, painted under the title
    juce::Rectangle<int> headerBand;   // the band paint() fills behind the control rows
    juce::Component::SafePointer<juce::DocumentWindow> styledWindow; // standalone chrome we skinned

    // Nothing was displaying the setTooltip text scattered through the arp and chord
    // panels: JUCE only shows tooltips if a TooltipWindow exists, and there was none, so
    // 19 explanations across the plugin were dead code. Parented to the editor rather
    // than the desktop so it stays inside the plugin window in a host, and one instance
    // only (Keys Host embeds exactly one KeysEditor). Short delay: the point is to
    // answer "what is this" quickly, and holding a mouse still is work here.
    juce::TooltipWindow tooltips { this, 450 };

    // The one playing surface this product builds: the piano, for Keys and Keys Host.
    // It and the wheels live inside `keybedHolder`, never directly in the editor, so
    // detaching is one re-parent (see KeybedHolder).
    KeybedHolder keybedHolder;
    PianoKeyboard keyboard;

    KnobBank knobBank; // eight CC knobs, replaces the old Fader/XY surfaces
    ChordPads chordPads;

    juce::ComboBox sizeBox, rootBox, scaleBox, channelBox, chordStrumDirBox, polyphonyBox;
    juce::Label sizeLabel, rootLabel, scaleLabel, channelLabel, chordStrumDirLabel, polyphonyLabel;
    juce::Slider octaveSlider, chordStrumSlider;
    juce::Label octaveLabel, chordStrumLabel;
    juce::Slider modWheel, pitchWheel;  // transient performance wheels (no persistence)
    juce::Label modLabel, pitchLabel;

    juce::ToggleButton scaleLockButton { "Scale Lock" };
    juce::ToggleButton sustainButton { "Sustain" };
    juce::ToggleButton humanizeButton { "Humanize" };
    juce::ToggleButton chordExclusiveButton { "Exclusive" };
    juce::TextButton panicButton { "All Off" };
    juce::TextButton updateButton;

    // Chord-pad page navigation, and the three centre-view tabs. The tabs are toggles:
    // clicking the lit one folds the centre away, which is how the centre section is
    // minimized (it needs no chevron of its own, unlike the other sections).
    std::array<juce::TextButton, KeysProcessor::numPadPages> pageButtons;
    juce::TextButton performButton { "Perform" }, chordsButton { "Chords" };

    // Only the centre view currently showing is alive; the generator builds 16 chord cards
    // and is not worth keeping warm behind the knob bank.
    std::unique_ptr<ChordGenPanel> genPanel;

    // The arp panel lives in its own section, inside a holder so that detaching it is one
    // re-parent (the same trick keybedHolder plays for the keyboard). Alive whenever the
    // section is open, wherever that section currently is.
    struct ArpHolder : juce::Component
    {
        std::function<void()> layout;
        void resized() override { if (layout) layout(); }
        void paint(juce::Graphics& g) override { g.fillAll(skin::bgBot); }
    };
    ArpHolder arpHolder;
    std::unique_ptr<ArpPanel> arpPanel;

    // Section folds. Every section of the editor can be minimized so the window can be
    // squeezed small when the screen is busy; the state lives on the processor
    // (KeysProcessor::LayoutState) so it survives the editor being closed and reopened.
    SectionBar controlsBar { "Controls" };
    SectionBar centreBar { "Perform" };  // caption follows the view; the tabs ride on it
    // The chord pads are their own section, below the centre view rather than inside the
    // Perform one. Owen asked for this so a card stays reachable while the generator or the
    // arp is up: you click a chord and hear it arpeggiated without leaving the view you are
    // editing. Its page buttons ride on its own bar, where they used to sit under the strip.
    SectionBar padsBar { "Pads" };
    // The arpeggiator, a section in its own right since 2026-07-25. Its bar carries an On
    // toggle and a Detach button, so the two things you reach for while it runs stay reachable
    // with the section folded shut.
    SectionBar arpBar { "Arp" };
    // Audio to MIDI. Built only when the transcription engine is compiled in, and alive only
    // while its section is open: it holds an audio device and a CNN, neither of which is worth
    // keeping warm behind a folded bar.
    SectionBar transcribeBar { "Transcribe" };
#if KEYS_TRANSCRIBE
    std::unique_ptr<TranscribePanel> transcribePanel;
#endif
    SectionBar keyboardBar { "Keyboard" };
    juce::TextButton knobsButton { "Knobs" };
    juce::ToggleButton arpOnButton { "On" };
    juce::TextButton arpDetachButton { "Detach" };
    std::unique_ptr<ButtonAtt> arpOnAtt;
    // Lit, a click on a chord card hands its chord to the arpeggiator and leaves it there.
    // It rides on the Pads bar because that is what it changes the meaning of, and it is a
    // visible toggle rather than an implied "the arp is on" mode: a pad should never
    // quietly start doing a different thing than it did a minute ago.
    juce::TextButton toArpButton { "To Arp" };
    juce::TextButton wheelsButton { "Wheels" }, detachButton { "Detach" };

    // A second Size selector, for the detached keyboard window. The keybed's key count
    // lives in the Controls section, which is exactly the section you fold away once the
    // keyboard is in its own window - so the detached window carries its own, attached to
    // the same parameter. Two attachments on one parameter is fine; both follow it.
    juce::ComboBox detachedSizeBox;
    std::unique_ptr<ComboAtt> detachedSizeAtt;

    // Which colour this instance wears. Lives on the Controls bar rather than inside the
    // Controls section, so it stays reachable with that section folded away - the whole
    // point is telling one track's Keys from another's at a glance.
    juce::TextButton themeButton;
    void showThemeMenu();
    void applyAccent(int index);

    // Alive only while their section is popped out. Declared after the holders so they are
    // destroyed first; ~KeysEditor resets them explicitly too, since relying on member order
    // for a window that borrows a component is too subtle to leave implicit.
    std::unique_ptr<DetachedWindow> keyboardWindow;
    std::unique_ptr<DetachedWindow> arpWindow;

    RangeSlider humanizeVelSlider; // a two-value range whose band drags as one; see RangeSlider.h
    juce::Label humanizeVelLabel;

    std::unique_ptr<ComboAtt> sizeAtt, rootAtt, scaleAtt, channelAtt, chordStrumDirAtt, polyphonyAtt;
    std::unique_ptr<SliderAtt> octaveAtt, chordStrumAtt;
    std::unique_ptr<ButtonAtt> scaleLockAtt, sustainAtt, humanizeAtt, chordExclusiveAtt;

    okstudio::updater::Config updaterConfig;
    okstudio::updater::UpdateInfo pendingUpdate;
    int editingPad = -1;              // pad slot linked to the keyboard for editing, or -1
    std::vector<int> lastEditNotes;   // last content written back, to detect changes cheaply
    int lastChannel = -1;    // to panic on MIDI-channel change (avoids notes stuck on the old channel)
    bool embedded = false;   // see setEmbedded()
    bool lastSustain = false; // to release held pad chords when the sustain pedal lifts
    bool pitchReturning = false; // pitch wheel is gliding back to centre (Octavium's ~160 ms ease)
    float panicFlash = 0.0f;  // 1 -> 0 decay behind the All Off button, on an explicit click only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysEditor)
};
} // namespace keys
