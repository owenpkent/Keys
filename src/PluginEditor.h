#pragma once

#include "PluginProcessor.h"
#include "ui/ArpPanel.h"
#include "ui/ChordGenMenu.h"
#include "ui/ChordPads.h"
#include "ui/DetachedWindow.h"
#include "ui/KeysLookAndFeel.h"
#include "ui/KnobBank.h"
#include "ui/RangeSlider.h"
#include "ui/SectionBar.h"
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

    // Embedded only: how tall this editor would like to be, after a fold. The arp with its
    // step editor needs far more room than the player, so a parent that ignores this clips
    // the keybed off the bottom. Keys Host grows itself to fit.
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
    // The sections of the editor, in the order they stack down the window. Every one of them
    // folds, and since 2026-07-27 every one of them also detaches into a window of its own,
    // so the machinery below is written once and indexed by this rather than four times over.
    //
    // The centre stopped being one of them on 2026-07-30. It had been whittled down to the
    // knob bank alone - the arp became a section of its own, then the chord generator lost
    // its panel - and a section holding one row is a bar, a gap and a caption spent on
    // nothing. The knobs moved into Controls, which is the other band of settings-shaped
    // controls, and kept their fold chip on that bar.
    enum SectionId { secControls, secArp, secPads, secKeyboard, numSections };

    void refreshSectionPanels();       // builds/destroys the panels the current folds call for
    void refreshArpPanel();            // the arp section's panel follows its own fold
    void syncSectionControls();        // toggle states + visibility from processor.layout
    int  arpHeight() const;            // height the arp section asks for, 0 if folded
    int  sectionHeight(SectionId) const; // 0 when the section is folded or in its own window
    int  idealHeight() const;          // total height with the current folds
    int  minWidthForView() const;      // the arp carries more controls than the player
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

    void layoutControlsHolder();    // the two header rows and the knob bank, wherever the section lives
    void layoutPadsHolder();        // the chord-pad strip
    void layoutArpHolder();         // the arp panel
    void layoutKeybed();            // wheels + keys

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
        // What to call the section out loud: `name` for tooltips and accessible names,
        // `windowTitle` for the title bar of the window it detaches into.
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
    Holder& arpHolder;
    Holder& padsHolder;
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

    // Eight CC knobs, replacing the old Fader/XY surfaces. Parented into the Controls
    // holder since 2026-07-30: it is the bottom row of that band, not a section of its own.
    KnobBank knobBank;
    ChordPads chordPads;
    // The chord generator. A plain object with no panel of its own since 2026-07-30, and a
    // member rather than a unique_ptr for exactly that reason: it is reached from a pad's
    // card menu and from the two chips on the Pads bar, neither of which can be allowed to go
    // looking for it and find nothing. It used to live and die with the Chords view, which
    // is what made "New chord" come and go from the menu.
    ChordGenMenu chordGen;

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

    // Chord-pad page navigation, riding the Pads bar.
    std::array<juce::TextButton, KeysProcessor::numPadPages> pageButtons;
    // Two rows of eight, or four rows of four with the full chord card on each. The tall
    // arrangement is the one the chord generator used to draw over the top of these same
    // pads before its grid was removed; it belongs to the pads, so it works whatever else
    // is on screen.
    juce::TextButton padsBigButton { "Big" };

    // The generator's two bulk actions, riding the Pads bar. They are the whole left-click
    // path into generation (everything else it owns is on a pad's card menu), and they are on
    // a bar because a bar is 34 px that already exists: a control on one costs the window no
    // height, which is what let the generator lose its band without losing its reach.
    //
    // They are also the only two controls on this bar that do *not* hide when the section
    // folds, for exactly the reason the arp's On does not: the other route to the generator
    // is a right-click on a pad card, and that folds away with the strip, so hiding these
    // took the whole generator off the screen along with the cards.
    //
    // Clear used to be the third. It emptied every unlocked pad on the page with one click,
    // there is no undo anywhere in Keys, and it sat between Regen and the page buttons. It is
    // an item on a pad's card menu now (`ChordGenMenu::addPadMenuItems`); clearPage() itself
    // is untouched.
    juce::TextButton fillButton { "Fill" }, regenButton { "Regen" };

    // The three generator settings that get reached for constantly, as combo boxes on the same
    // bar (2026-07-30, Owen's ask). Everything the generator owns is on a pad's card menu, and
    // a menu costs a right-click and then a hover per level; for the settings you change while
    // you are auditioning a page - what key, what mode, how far outside it may wander - that is
    // the wrong price. On the bar each is one click to open and one to pick, and it costs the
    // window no height, same trade as Fill and Regen.
    //
    // They never hide, for the same reason those two do not: the card menu folds away with the
    // strip, so hiding these would take the settings off the screen entirely. Laid out from the
    // right end, again like those two, so they do not float in the hole the page buttons leave.
    //
    // Attachments, not hand-syncing: the same three settings are still on the card menu, which
    // writes the parameter directly, so both places read the one source and neither has to know
    // the other exists. Compliance is a continuous 0-100 parameter and this is five discrete
    // steps of it - a ComboBoxAttachment maps item i of n onto i/(n-1) of the parameter's
    // range, which lands exactly on 0/25/50/75/100 and picks the nearest step back, the same
    // arithmetic ChordGenMenu::addChoice does for the ticked item.
    juce::ComboBox genRootBox, genModeBox, genComplianceBox;
    std::unique_ptr<ComboAtt> genRootAtt, genModeAtt, genComplianceAtt;

    // Alive whenever the arp section is open, wherever that section currently is.
    std::unique_ptr<ArpPanel> arpPanel;

    // Section folds. Every section of the editor can be minimized so the window can be
    // squeezed small when the screen is busy; the state lives on the processor
    // (KeysProcessor::LayoutState) so it survives the editor being closed and reopened.
    SectionBar controlsBar { "Controls" };
    // The chord pads are their own section. Owen asked for this so a card stays reachable
    // while the arp is up: you click a chord and hear it arpeggiated without leaving what you
    // were editing. Its page buttons ride on its own bar, where they used to sit under the strip.
    SectionBar padsBar { "Pads" };
    // The arpeggiator, a section in its own right since 2026-07-25. Its bar carries an On
    // toggle, so the thing you reach for while it runs stays reachable with the section
    // folded shut.
    SectionBar arpBar { "Arp" };
    SectionBar keyboardBar { "Keyboard" };
    // Folds the knob row off the bottom of the Controls band. It rides the Controls bar,
    // which had a wide dead caption zone and now spends it on the one thing inside that
    // section worth hiding on its own: the knob row is 110 px, the two header rows are the
    // section itself. It hides with Controls like every other bar control.
    juce::TextButton knobsButton { "Knobs" };
    // The arp's power switch, and now also the thing that decides what a click on a chord
    // card means: with it lit, a card hands its chord to the arp instead of playing it while
    // the button is down. That used to be a separate "To Arp" toggle on the Pads bar, which
    // did nothing visible whenever the arp was off (removed 2026-07-27, Owen's call).
    juce::ToggleButton arpOnButton { "On" };
    std::unique_ptr<ButtonAtt> arpOnAtt;
    // Lets go of the chord being held into the arp, and nothing else. It rides the arp bar
    // beside On for the same reason On does: with the section folded it is the only way out
    // of a hold. A click on a chord card retriggers the hold rather than ending it, the arp's
    // own Stop button dies with the panel when the section folds (and this section starts
    // folded), and All Off kills the pads and the live card too - so without this there was
    // no on-screen release for just the held chord in the default layout. Disabled while
    // nothing is held, which makes it a state display as well as a button.
    juce::TextButton arpHoldOffButton { "Hold off" };
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
    bool pitchReturning = false; // pitch wheel is gliding back to centre (Octavium's ~160 ms ease)
    float panicFlash = 0.0f;  // 1 -> 0 decay behind the All Off button, on an explicit click only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysEditor)
};
} // namespace keys
