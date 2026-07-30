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
#include <vector>

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
    void addCombo(juce::Component& parent, juce::ComboBox&, juce::Label&, const juce::String& text,
                  const juce::StringArray& items, const juce::String& paramID, std::unique_ptr<ComboAtt>&);
    void showUpdate(const okstudio::updater::UpdateInfo&);
    // Push one end of a two-handle range into its parameter, if it actually moved. The two
    // RangeSliders (velocity, strum) carry two values and so cannot take an APVTS
    // attachment; this is the write half of syncing them by hand.
    void writeParam(const char* paramID, double value);
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

    // The sections of the editor, in the order they stack down the window. Every one of them
    // folds, and since 2026-07-27 every one of them also detaches into a window of its own,
    // so the machinery below is written once and indexed by this rather than six times over.
    enum SectionId { secControls, secCentre, secArp, secPads, secTranscribe, secKeyboard, numSections };

    void setCentreView(int view);      // picks a view, unfolding the section if needed
    void refreshSectionPanels();       // builds/destroys the panels the current folds call for
    void refreshCentrePanels();        // creates/destroys the panel the state calls for
    void refreshArpPanel();            // the arp section's panel follows its own fold
    void refreshTranscribePanel();     // ditto the Transcribe section
    void syncSectionControls();        // toggle states + visibility from processor.layout
    int  centreHeight() const;         // height the current centre view asks for, 0 if folded
    int  arpHeight() const;            // height the arp section asks for, 0 if folded
    int  sectionHeight(SectionId) const; // 0 when the section is folded or in its own window
    int  idealHeight() const;          // total height with the current folds
    int  minWidthForView() const;      // the centre views carry more controls than the player
    void applyLayout();                // resize to fit the folds (unless embedded), then resized()

    void setSectionDetached(SectionId, bool); // move a section in or out of its own window
    void rememberSectionBounds(SectionId);
    // Places a section's Detach button, plus anything travelling with it, at the right-hand
    // end of `row`, and hands back what is left for that section's own controls. `onBar` says
    // which of the two rows this is - the section bar, or the strip a detached window carries
    // - and the call does nothing on the one the buttons are not currently in.
    juce::Rectangle<int> layoutDetachRow(SectionId, juce::Rectangle<int> row, bool onBar);
    // Holder-local bounds for a section's content: the whole holder, less the strip a
    // detached one carries at the top for the controls that came out with it.
    juce::Rectangle<int> holderContent(SectionId);

    void layoutControlsHolder();    // the two header rows, wherever the section lives
    void layoutCentreHolder();      // the knob bank or the generator
    void layoutPadsHolder();        // the chord-pad strip
    void layoutTranscribeHolder();  // the Transcribe panel
    void layoutArpHolder();         // the arp panel
    void layoutKeybed();            // wheels + keys
    // The centre bar's tabs and chip; hands back the bar space they did not use.
    juce::Rectangle<int> layoutToolRow(juce::Rectangle<int>);

    // A section's content lives in a holder rather than directly in the editor, so popping it
    // out is one re-parent instead of a shuffle of every control in the section. The holder's
    // parent is either this editor or a DetachedWindow's content slot; nothing else changes.
    struct Holder : juce::Component
    {
        std::function<void()> layout;
        std::function<void(juce::Graphics&)> painter; // default: the plain section background
        void resized() override { if (layout) layout(); }
        void paint(juce::Graphics& g) override
        {
            if (painter)
                painter(g);
            else
                g.fillAll(skin::bgBot);
        }
    };

    // One section of the editor: its content holder, the button that pops it out, and the
    // window it lives in while it is out. The flags it reads live on the processor, so a
    // window closed and reopened comes back the way it was left.
    struct Section
    {
        Holder holder;
        juce::TextButton detachButton { "Detach" };
        std::unique_ptr<DetachedWindow> window;
        // What to call the section out loud: `name` for tooltips and accessible names (the
        // centre bar's own caption follows the view, so it cannot serve), `windowTitle` for
        // the title bar of the window it detaches into.
        juce::String name, windowTitle;
        juce::Point<int> minSize { 480, 200 }, defaultSize { 900, 420 };

        // Bar controls that follow the content into its window, placed right to left after
        // the Re-dock button. `detachedOnly` ones exist for the window alone and are dropped
        // again when the section re-docks (the keybed's second Size selector).
        struct Traveller
        {
            juce::Component* c = nullptr;
            int width = 0;
            bool detachedOnly = false;
        };
        std::vector<Traveller> travellers;

        SectionBar* bar = nullptr;
        bool* open = nullptr;                 // the fold flag in KeysProcessor::LayoutState
        bool* detached = nullptr;
        juce::Rectangle<int>* bounds = nullptr; // where its window was last left
        juce::Rectangle<int> caption;         // bar space paint() says where the section went
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

    // Declared before every reference into it, and before the components that get parented
    // into its holders, so nothing binds to a member that is not built yet.
    std::array<Section, numSections> sections;
    Section& section(SectionId id) { return sections[(size_t) id]; }
    const Section& section(SectionId id) const { return sections[(size_t) id]; }

    // The holders by name, for the code that only ever means one of them.
    Holder& controlsHolder;
    Holder& centreHolder;
    Holder& arpHolder;
    Holder& padsHolder;
    Holder& transcribeHolder;
    Holder& keybedHolder;

    juce::Label title;
    juce::Rectangle<int> titleCaption; // "OK STUDIO" wordmark, in controlsHolder coordinates
    juce::Component::SafePointer<juce::DocumentWindow> styledWindow; // standalone chrome we skinned

    // Nothing was displaying the setTooltip text scattered through the arp and chord
    // panels: JUCE only shows tooltips if a TooltipWindow exists, and there was none, so
    // 19 explanations across the plugin were dead code. Parented to the editor rather
    // than the desktop so it stays inside the plugin window in a host, and one instance
    // only (Keys Host embeds exactly one KeysEditor). Short delay: the point is to
    // answer "what is this" quickly, and holding a mouse still is work here.
    juce::TooltipWindow tooltips { this, 450 };

    // The one playing surface this product builds: the piano, for Keys and Keys Host.
    // It and the wheels live inside the keybed holder, never directly in the editor, so
    // detaching is one re-parent.
    PianoKeyboard keyboard;

    KnobBank knobBank; // eight CC knobs, replaces the old Fader/XY surfaces
    ChordPads chordPads;

    juce::ComboBox sizeBox, rootBox, scaleBox, channelBox, chordStrumDirBox, polyphonyBox;
    juce::Label sizeLabel, rootLabel, scaleLabel, channelLabel, chordStrumDirLabel, polyphonyLabel;
    juce::Slider octaveSlider, bpmSlider;
    juce::Label octaveLabel, chordStrumLabel, bpmLabel;
    // Strum is a range, the same two-handle band as the humanize velocity beside it: each
    // chord rakes at a speed drawn from it, so repeated stabs are not identical.
    RangeSlider chordStrumSlider;
    juce::Slider modWheel, pitchWheel;  // transient performance wheels (no persistence)
    juce::Label modLabel, pitchLabel;

    juce::ToggleButton scaleLockButton { "Scale Lock" };
    juce::ToggleButton sustainButton { "Sustain" };
    // Latch is back as a toggle of its own (2026-07-30, Owen's call). It rides the Keyboard
    // bar beside Sustain because the two are the same question — how does a note stop? —
    // answered two ways: the pedal restrikes, Latch releases.
    juce::ToggleButton latchButton { "Latch" };
    juce::ToggleButton humanizeButton { "Humanize" };
    juce::ToggleButton chordExclusiveButton { "Exclusive" };
    juce::TextButton panicButton { "All Off" };
    juce::TextButton updateButton;

    // Chord-pad page navigation, and the two centre-view tabs. The tabs are toggles:
    // clicking the lit one folds the centre away, which is how the centre section is
    // minimized (it needs no chevron of its own, unlike the other sections).
    std::array<juce::TextButton, KeysProcessor::numPadPages> pageButtons;
    juce::TextButton performButton { "Perform" }, chordsButton { "Chords" };

    // Only the centre view currently showing is alive; the generator builds 16 chord cards
    // and is not worth keeping warm behind the knob bank.
    std::unique_ptr<ChordGenPanel> genPanel;

    // Alive whenever the arp section is open, wherever that section currently is.
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
    // toggle, so the thing you reach for while it runs stays reachable with the section
    // folded shut.
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
    // The arp's power switch, and now also the thing that decides what a click on a chord
    // card means: with it lit, a card hands its chord to the arp instead of playing it while
    // the button is down. That used to be a separate "To Arp" toggle on the Pads bar, which
    // did nothing visible whenever the arp was off (removed 2026-07-27, Owen's call).
    juce::ToggleButton arpOnButton { "On" };
    std::unique_ptr<ButtonAtt> arpOnAtt;
    juce::TextButton wheelsButton { "Wheels" };

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

    RangeSlider humanizeVelSlider; // a two-value range whose band drags as one; see RangeSlider.h
    juce::Label humanizeVelLabel;

    std::unique_ptr<ComboAtt> sizeAtt, rootAtt, scaleAtt, channelAtt, chordStrumDirAtt, polyphonyAtt;
    std::unique_ptr<SliderAtt> octaveAtt, bpmAtt; // strum has two values; synced by hand
    std::unique_ptr<ButtonAtt> scaleLockAtt, sustainAtt, latchAtt, humanizeAtt, chordExclusiveAtt;

    okstudio::updater::Config updaterConfig;
    okstudio::updater::UpdateInfo pendingUpdate;
    int editingPad = -1;              // pad slot linked to the keyboard for editing, or -1
    std::vector<int> lastEditNotes;   // last content written back, to detect changes cheaply
    int lastChannel = -1;    // to panic on MIDI-channel change (avoids notes stuck on the old channel)
    bool embedded = false;   // see setEmbedded()
    bool lastSustain = false; // to release held pad chords when the sustain pedal lifts
    bool lastArpOn = false;   // to release a chord held into the arp when the arp goes off
    bool pitchReturning = false; // pitch wheel is gliding back to centre (Octavium's ~160 ms ease)
    float panicFlash = 0.0f;  // 1 -> 0 decay behind the All Off button, on an explicit click only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysEditor)
};
} // namespace keys
