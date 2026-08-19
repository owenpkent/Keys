#pragma once

#include "PluginProcessor.h"
#include "ui/ArpPanel.h"
#include "ui/ChordGenMenu.h"
#include "ui/ChordGenPanel.h"
#include "ui/ChordPads.h"
#include "ui/DetachedWindow.h"
#include "ui/KeysLookAndFeel.h"
#include "ui/KnobBank.h"
#include "ui/RangeKnob.h"
#include "ui/RangeSlider.h"
#include "ui/SectionBar.h"
#include "ui/TakePanel.h"
// StepComboBox.h went with the Pads bar's Scale Compliance box on 2026-08-02. The class is
// still in the tree: it is the answer for any future value shown as coarse steps of a
// continuous parameter, and the bug it documents (a ComboBoxAttachment swallowing a pick of
// the item already showing) is worth keeping written down.
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

    /** Set by a host that embeds Keys and wants an Instrument chip on the Controls bar.
        While this is set the chip is shown; clicking it calls this to fill the popup. */
    std::function<void (juce::PopupMenu&)> onBuildInstrumentMenu;

    /** Supplies the chip's caption: the loaded instrument's name, or an empty
        string for "nothing loaded". Only read while onBuildInstrumentMenu is set. */
    std::function<juce::String()> instrumentName;

    /** Call after a load or an eject so the chip's caption catches up. */
    void refreshInstrumentChip();

    // The narrowest this editor can be laid out at, which is the Pads bar's own arithmetic
    // (see the definition). Public because Keys Host embeds one of these and has to set its
    // window's floor from it - it held a copy of the number until 2026-07-30, and the copy
    // went stale the first time a control joined that bar.
    int minWidthForView() const;

    // Total height the current folds add up to. Public for exactly the same reason as the
    // width beside it: Keys Host embeds one of these, and it has to open its window at the
    // content's height and floor it there. It held a literal 620 instead until 2026-08-02,
    // and a literal goes stale the first time a section grows - the symptom being the
    // keyboard, which is laid out last, carved off the bottom with nothing to say so.
    int  idealHeight() const;

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
    void applyLayout();                // resize to fit the folds (unless embedded), then resized()

    void setSectionDetached(SectionId, bool); // move a section in or out of its own window
    void rememberSectionBounds(SectionId);

    // The chord generator's window. Not a section - it never docks, so it has no bar, no fold
    // and no entry in `sections` - but it reuses the same DetachedWindow and the same
    // remember-where-it-was-left contract, and its flag and frame live beside the sections' in
    // KeysProcessor::LayoutState. Opening builds the panel, closing destroys it, so nothing of
    // it exists while it is shut.
    void setChordGenWindowOpen(bool);
    void rememberChordGenBounds();
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
    //
    // It is a `DragAndDropContainer` for one thing, and the thing is the Pads section: JUCE wants
    // the container to be an ancestor of whatever starts a drag, and the holder is the one
    // ancestor a section keeps in both places it can live. Put it on the editor instead and every
    // drag would stop working the moment the section was popped out. The other three holders
    // inherit it and never use it, which costs a vtable and is the price of the table staying one
    // kind of thing (see `sections`).
    struct Holder : juce::Component,
                    juce::DragAndDropContainer
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
        // the Re-dock button. Wheels is the only one now (2026-08-02): the keybed's second
        // Size selector, the one traveller that only ever existed for the detached window,
        // is gone - Size lives on the Keyboard bar itself now and travels with the section
        // like every other bar control, so the window needs nothing extra for it.
        struct Traveller
        {
            juce::Component* c = nullptr;
            int width = 0;
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
    // The chord generator's brain. A plain object and a member, not a unique_ptr and not
    // something a window owns: it is reached from a pad's card menu and from the chips on the
    // Pads bar, and neither can be allowed to go looking for it and find nothing. It used to
    // live and die with the Chords view, which is what made "New chord" come and go from the
    // menu, and the window below must not be able to reintroduce that.
    ChordGenMenu chordGen;
    // Its window, and the panel inside it. Both are built when the window opens and torn down
    // when it closes, in that order reversed - DetachedWindow holds the content non-owned, so
    // the window has to go first. The panel is a view and holds no state of its own: every
    // control is an APVTS attachment and the two transient picks (Mood, Start) live on
    // chordGen, so shutting this loses nothing.
    std::unique_ptr<ChordGenPanel> chordGenPanel;
    std::unique_ptr<DetachedWindow> chordGenWindow;

    juce::ComboBox sizeBox, rootBox, scaleBox, channelBox, chordStrumDirBox, polyphonyBox;
    juce::Label sizeLabel, rootLabel, scaleLabel, channelLabel, chordStrumDirLabel, polyphonyLabel;
    // No BPM slider in the band since 2026-08-02: the tempo is a number on the Controls
    // *bar* now (bpmField, below), which is where Owen wanted it and what freed row B's
    // last 170 px.
    //
    // Size and Octave left the band the same day for the *Keyboard* bar (Owen: "the size can
    // go down to the header of the keyboard button"). sizeBox above is reparented there
    // rather than duplicated - see the ctor and resized(). Octave is not a slider on that bar:
    // a bar control is 24 px tall and JUCE's IncDecButtons arrows would stack to 12 px each,
    // under the mouse-only floor - so it is the BPM field's own shape instead, a `<` value `>`
    // trio (octPrevButton / octaveReadout / octNextButton) driven by nudgeOctave().
    juce::Label octaveBarLabel;     // "OCT", the Keyboard bar's own caption
    juce::Label octaveReadout;      // "+2" / "0" / "-3", refreshed in timerCallback()
    juce::TextButton octPrevButton { "<" }, octNextButton { ">" };
    // Strum and Humanize, as RangeKnobs in the pads section since 2026-08-03 - the knob is the
    // top of each range and the ring reaches back from it. Both were two-handle RangeSliders,
    // Strum on the Controls band and Humanize on the Pads bar; both belong with the pads,
    // which is what they shape. See the wireRange lambda in the editor's constructor.
    RangeKnob strumKnob, humanKnob;
    juce::Label strumHead, humanHead;
    // No attachments for the two pad range knobs: their face is the band's *centre* since
    // 2026-08-19, which is not a parameter - see wireRange in the constructor, and
    // syncPadRangeKnobs() for the pull half.
    void syncPadRangeKnobs();
    // The strum direction's `< >` pair, which replaced its combo on 2026-08-03. The caption
    // beside them reads the live direction, so there is no third control saying it.
    juce::TextButton strumDirPrev { "<" }, strumDirNext { ">" };
    void stepStrumDir(int delta);
    // What the Strum lamp puts back when it switches on. A convenience, not state: a session
    // saved with the strum at zero opens at zero, and this starts at the default again.
    double lastStrumMax = 120.0;

    juce::Label chordStrumLabel;
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

    // --- Take: REC and the chip that gets it out -----------------------------------------
    // Both ride the *Keyboard* bar, beside Exclusive / Sustain / Latch / All Off, and neither
    // hides when the section folds: recording is the same reach-for-it-while-playing case
    // those four are, and folding the keybed away mid-take must not take the stop button with
    // it. The bar had the room - unlike the Controls bar, whose budget is spoken for down to
    // the pixel (see minWidthForView) - so this costs no floor.
    juce::TextButton recButton { "REC" };

    // A click reveals the written file in Explorer; a drag hands it to whatever is under the
    // pointer, Live included. Two paths on purpose: a drag out of a plugin window and across
    // the screen is a long, precise gesture, and the reason every take is written to one fixed
    // folder is that the folder can be added to Live's Places once and dragged from *inside*
    // Live afterwards, which is a far kinder gesture with one mouse.
    struct TakeChip : public juce::TextButton
    {
        std::function<juce::File()> getFile;

        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;

    private:
        bool wasDrag = false; // this gesture became a drag, so its mouse-up is not a click
    };
    TakeChip takeChip;
    void refreshTakeControls(); // lit state, caption and enablement; polled from timerCallback
    juce::String lastTakeCaption;

    // Whether lastTakeFile() is actually on disk, cached against the path it was asked about.
    // refreshTakeControls runs at 30 Hz for the life of the editor and stat'ed the file twice on
    // every one of those ticks, on the DAW's UI thread, for an answer only a REC click can
    // change - which is the sort of cost this whole pass exists to remove.
    juce::File lastTakeStatPath;
    bool haveTakeFile = false;

    // The take window: a picture of what was captured, plus Save as / Show in Explorer, so a
    // take is something you can look at before it leaves rather than a filename you trust.
    // Built when opened and destroyed when closed, the ChordGenPanel pattern - and unlike that
    // one its bounds are not kept in LayoutState, because a take is transient and a session
    // reopening onto this window would be reopening onto a take that no longer exists.
    void setTakeWindowOpen(bool open);
    std::unique_ptr<TakePanel> takePanel;
    std::unique_ptr<DetachedWindow> takeWindow;
    juce::Rectangle<int> takeWindowBounds; // this run only; see above
    juce::TextButton updateButton;

    // Chord-pad page navigation, riding the Pads bar.
    std::array<juce::TextButton, KeysProcessor::numPadPages> pageButtons;

    // The generator's two bulk actions, riding the Pads bar. They are the fast path into
    // generation, and they are on a bar because a bar is 34 px that already exists: a control
    // on one costs the window no height, which is what let the generator lose its band without
    // losing its reach.
    //
    // They do *not* hide when the section folds, for exactly the reason the arp's On does not:
    // the other route to a card is a right-click on it, and that folds away with the strip.
    //
    // Clear is not a third one here. It empties every unlocked pad on the page with one click,
    // there is no undo anywhere in Keys, and it sat between Regen and the page buttons. It is a
    // gone entirely as of 2026-08-01 (see ChordGenMenu), since that window stopped writing pads.
    juce::TextButton fillButton { "Fill" }, regenButton { "Regen" };

    // And the way into everything else the generator has: its own window (2026-07-30, Owen's
    // call). It rides the same bar and never hides, for the same reason those two do not -
    // fold the pads away and this is the only thing left on screen that can reach the
    // generator. The window is where Octave, Source, Notes, Inversions, Lock Influence, the
    // Markov chains and the audition tray live; they were submenus of a pad's card menu for a few
    // hours earlier that day, which took the menu to 23 rows and 820 px.
    juce::TextButton chordGenButton { "Generator" };

    // The three generator settings that get reached for constantly, as combo boxes on the same
    // bar (2026-07-30, Owen's ask). Every setting the generator has is also in its window, and
    // opening a window to change the key while auditioning a page is the wrong price for the
    // two or three you touch constantly. On the bar each is one click to open and one to pick,
    // and it costs the window no height, same trade as Fill and Regen.
    //
    // They never hide, for the same reason those two do not. Laid out from the right end, again
    // like those two, so they do not float in the hole the page buttons leave.
    //
    // Attachments, not hand-syncing: the window's own Key, Mode and Scale Compliance are
    // attachments on these same three parameters, so both places read one source and neither
    // has to know the other exists - which is what makes "the bar is the fast path, the window
    // is the complete one" true rather than a promise.
    //
    // Key alone since 2026-08-02: Mode and Scale Compliance came off the Pads bar at Owen's
    // ask and live only in the generator's window now, which holds every setting anyway. Key
    // is a choice parameter, so this combo and the window's hold the same set of values and
    // cannot read differently - which is why a plain ComboBoxAttachment is all it needs.
    // Compliance was the one that could not have one (a continuous 0-100 shown as five steps,
    // where an attachment made picking the step already showing a dead click - see
    // StepComboBox.h), and that wiring went with it.
    juce::ComboBox genRootBox;
    std::unique_ptr<ComboAtt> genRootAtt;
    // Mode, back on the bar beside the key (2026-08-18, Owen: "in the pad section, we also need
    // a drop down for the second part of the key. We already have the letter, but we need the
    // mode"). It sat here until 2026-08-02 and left with Scale Compliance; a key without its
    // mode is half a key, and this is the pair you reach for between fills.
    juce::ComboBox genModeBox;
    std::unique_ptr<ComboAtt> genModeAtt;

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
    // The Knobs chip that used to fold the knob row off the bottom of the Controls band is
    // gone (2026-08-02, Owen: "remove the knobs button and make the knobs visible when you
    // open controls"): the row is unconditional now, whenever the section itself is open. Its
    // 66 + 14 px on the Controls bar became the Instrument chip's cell (instrumentChip, above).
    // The arp's power switch used to be a separate lettered chip here, one per line
    // (2026-08-01), sitting a few pixels from the A/B/All tabs that also named a line. Owen
    // pointed at the redundancy on 2026-08-02 ("I want those to be on and off buttons to turn
    // on or off the ARP ... we can remove the a and b check mark on the right side of the
    // header") and the chip is gone: the tabs are the switch now. See ArpBarTab below.
    //
    // Lets go of the chord being held into the arp, and nothing else. It rides the arp bar
    // beside On for the same reason On does: with the section folded it is the only way out
    // of a hold. A click on a chord card retriggers the hold rather than ending it, the arp's
    // own Stop button dies with the panel when the section folds (and this section starts
    // folded), and All Off kills the pads and the live card too - so without this there was
    // no on-screen release for just the held chord in the default layout. Disabled while
    // nothing is held, which makes it a state display as well as a button.
    juce::TextButton arpHoldOffButton { "Hold off" };
    // The Keyboard bar's All Off, for the arpeggiator (2026-08-02, Owen's ask). Switches both
    // lines off and lets go of everything - see KeysProcessor::allArpOff for why switching off
    // is the part that makes it different from Hold off beside it. Always enabled, unlike Hold
    // off: a stop button that greys itself out is one you have to read before you can trust it,
    // and this one is reached in the moment you want the noise to end.
    juce::TextButton arpAllOffButton { "All Off" };
    // Whether the keybed lights up for the notes the arp is playing. A view toggle over
    // layout.arpLights, not a parameter - see the member. It rides this bar rather than the
    // Keyboard bar because it is a fact about the arp: it is the arp's notes it shows, and it
    // is meaningless with both lines off.
    juce::ToggleButton arpLightsButton;
    // The A/B tabs are the arp's own On switches now (2026-08-02, Owen: "the A and B on the
    // left side of the header, I want those to be on and off buttons to turn on or off the
    // ARP ... we can remove the a and b check mark on the right side of the header"). A
    // lettered On toggle used to live at each end of this bar at once - a navigation tab here,
    // a checkmark by Hold off - and Owen called that redundant; the checkmark is gone
    // (arpOnButtons, above) and clicking a tab now toggles that line's `arpOn` / `arp2On`
    // through an ordinary ButtonAttachment, the pattern every other APVTS-backed toggle in
    // this file already uses. The navigation job the tabs used to do moved to each MacroRow's
    // own Details button (ArpPanel.h), which is why a click on A or B no longer calls
    // setEditLine. That also changes what folding the section means for them: an On switch is
    // exactly the "reach for it while playing" case CLAUDE.md carves out for a folded bar, so
    // A and B never hide and are laid out whether or not the section is open. All is not a
    // line and has nothing to switch on or off - it still only chooses the macro view, so it
    // still hides and its cell still collapses with the fold, the pageButtons rule. BPM and
    // Quantize beside them never hid either, for the same reach-for-it-while-playing reason.
    //
    // Each tab is still a DragAndDropTarget: dropping a chord card on a letter hands the
    // chord to that line whether the line is on or off (CLAUDE.md: "A line that is off still
    // takes chords in"), the same gesture a macro card's own drop takes and still the bigger
    // target for it.
    struct ArpBarTab : public juce::TextButton,
                       public juce::DragAndDropTarget
    {
        ArpBarTab(KeysEditor&, int line); // line < 0 is the All tab
        void paintButton(juce::Graphics&, bool over, bool down) override;
        bool isInterestedInDragSource(const SourceDetails&) override;
        void itemDragEnter(const SourceDetails&) override;
        void itemDragExit(const SourceDetails&) override;
        void itemDropped(const SourceDetails&) override;
        KeysEditor& owner;
        int line;
        bool dropTarget = false;
        // Only for line >= 0: the On/off switch itself, bound to that line's `On` parameter.
        // Null on the All tab, which is a plain view toggle with no parameter behind it.
        std::unique_ptr<ButtonAtt> onAtt;
    };
    std::array<std::unique_ptr<ArpBarTab>, KeysProcessor::uiArpLines> arpBarTabs;
    std::unique_ptr<ArpBarTab> arpBarAllTab;
    // The three pages of a line's deep view (2026-08-14): Steps, Slots, Setup. Plain
    // TextButtons, not ArpBarTabs - they select a page, there is no line behind them to hand a
    // chord to, and that is the same reason the All tab refuses every drop.
    //
    // **On the bar rather than in the panel**, and that is the whole reason paging pays for
    // itself: the bar is 34 px that already exists, so the page picker costs the panel no
    // height at all. Putting it inside the panel would have taken 34 px off the very budget
    // paging was buying back. Same rule that put Fill/Regen/Generator on the Pads bar.
    //
    // They sit immediately right of All, which makes All read as the fourth entry in one view
    // picker - the overview - and therefore as the way back out of a page. That is the answer
    // to "we need a way to get out the detail view": not a second control doing All's job, but
    // All finally sitting with the things it is an alternative to.
    std::array<std::unique_ptr<juce::TextButton>, 3> arpPageTabs;
    // Bar order is most-used first - **Play, Cards, Draw** - which is deliberately *not* the
    // Page enum's own order. That stays steps = 0 / slots = 1 / setup = 2 because
    // LayoutState::arpPage stores the plain value, and renumbering it would move the page
    // every saved session opens on (the same reason genSource's choice list may only be
    // appended to). One table, and both the click and the lit state read it.
    //
    // The names were Steps / Slots / Setup for one build (2026-08-14, same day). All three are
    // five letters starting with S, which is unreadable at a glance - Owen: "I don't
    // understand this layout". These name what you *do*: Play is where rate and shape are, so
    // it is where you go for a sound; Draw says up front that the lane page needs drawing on
    // before it does anything.
    static ArpPanel::Page arpPageForTab(int tabIndex);
    // Which tab is lit, derived from the processor's state so a drop, a session load and a
    // click all land in the same place.
    void refreshArpBarTabs();
    // A pad's chord into one line, whichever surface asked: a card dropped on that line's switch
    // on the bar, and the "Send to arp A / B" rows on a pad's own menu (2026-08-16). One method
    // because the two must not drift - it prefers the panel while the arp section is open, since
    // the panel is what also moves the aim to the line you named.
    // `followAim` true for a drop, which aimed at the line; false for the pad menu's
    // Send to arp A / B, which routes a chord and must leave the panel where it was.
    void sendPadToArpLine(int padSlot, int line);
    void nudgeBpm(int delta);
    void nudgeOctave(int delta); // the Keyboard bar's < > pair beside the octave read-out

    // The tempo, on the **Controls** bar (2026-08-02, Owen: "I think the bpm should live in
    // the controls header. I want it to be like the bpm in ableton, just a number"). It sat
    // on the arp bar for one build, which was where the arp needed it; it belongs here
    // because it is the plugin's clock, not the arpeggiator's - the arp is only its loudest
    // consumer, and Launch Quantize stayed behind with the arp for exactly that distinction.
    //
    // A Slider subclass rather than a bespoke component, so the APVTS SliderAttachment still
    // drives it; a Slider subclass that overrides `paint` rather than a styled Slider,
    // because every built-in style draws a track, a bar or a knob and Ableton's tempo field
    // is *only* the number. Overriding paint means the LookAndFeel is never consulted for it
    // (Slider::paint is what calls the LookAndFeel), while every drag, gesture and attachment
    // behaviour is inherited untouched.
    struct BpmField : public juce::Slider
    {
        BpmField();
        void paint(juce::Graphics&) override;
        // Tempo Sync on and a host tempo actually live (KeysProcessor::hostTempoLive()): the
        // field shows this instead of its own value (the "bpm" parameter's), set every
        // KeysEditor::timerCallback() alongside setEnabled(false) on this and the two
        // steppers beside it - none of the three can change anything while the host is the
        // one setting the tempo. Overriding paint means the LookAndFeel's disabled dim never
        // applies here (that dim is only ever painted by the base Slider::paint this
        // replaces), so paint() has to check isEnabled() itself.
        bool showingHost = false;
        double hostBpm = 0.0;
    };
    BpmField bpmField;
    // "BPM", unconditional like the field it labels (2026-08-02, Owen: "BPM ... needs
    // labels"). Styled like quantizeBarLabel beside it on the arp bar: the same shape, a
    // caption immediately left of the control group it names.
    juce::Label bpmBarLabel;
    // The click-only path. A drag is a drag, and the mouse-only contract says every value a
    // slider holds must also be reachable by clicking - the same reason the arp's rate dial
    // has its pair. This is the part of "just a number" that Keys cannot copy from Ableton,
    // which expects a keyboard for its tempo field. Both grey out with the field itself while
    // a live host tempo is showing (see BpmField::showingHost) - stepping a number the host
    // is setting would lie about what the click just did.
    juce::TextButton bpmPrevButton { "<" }, bpmNextButton { ">" };
    // Tempo Sync (2026-08-02, Owen: "we need a BPM sync toggle to sync with DAW"). On (the
    // default) reproduces what Keys always did - a rolling host with a valid tempo wins over
    // the "bpm" parameter; see ArpEngine::Params::followHost and KeysProcessor::
    // advanceChainClock. Off is the escape hatch: Keys keeps its own tempo regardless of what
    // the host's transport is doing. A plain TextButton toggle, not a checkbox ToggleButton -
    // the same compact chip shape as the arp's A/B line switches, sized like Fill/Regen
    // beside it rather than a checkbox-plus-label control this bar has no width to spare for.
    juce::TextButton bpmSyncButton { "Sync" };
    std::unique_ptr<ButtonAtt> bpmSyncAtt;
    juce::Label quantizeBarLabel;
    juce::ComboBox quantizeBarBox;
    std::unique_ptr<SliderAtt> bpmAtt;
    std::unique_ptr<ComboAtt> quantizeBarAtt;
    // Which arp line a click on a chord card feeds, shown as its letter and cycled A->B->C by
    // clicking. It rides the *Pads* bar, next to Fill / Regen / Generator, because it is a
    // fact about the cards rather than about the arp: the control that says where a card goes
    // belongs beside the cards. It is the same state as the A/B tabs inside the arp panel,
    // so either moves both - and it stays reachable with the arp section folded away, which
    // the tabs do not.
    // The cycling letter that used to sit here is gone (2026-08-02): a card click no longer
    // feeds a line at all, and the arp bar's A/B tabs name the current one.
    juce::TextButton wheelsButton { "Wheels" };

    // The detached keyboard window used to carry a second Size selector of its own, because
    // the keybed's key count lived in the Controls section - exactly the section you fold
    // away once the keyboard is in its own window. Gone on 2026-08-02: Size lives on the
    // Keyboard bar itself now (sizeBox, above), which travels with the section like every
    // other bar control, so the window needs nothing extra.

    // Which colour this instance wears. Lives on the Controls bar rather than inside the
    // Controls section, so it stays reachable with that section folded away - the whole
    // point is telling one track's Keys from another's at a glance.
    juce::TextButton themeButton;
    void showThemeMenu();

    // The settings gear (2026-08-17, Owen: "we need a settings icon and menu. populate
    // menu."). Plugin-level like the theme swatch it sits immediately left of, rather than
    // section-level, and for the same reason: it never hides with a fold (see resized() and
    // "what stays on a folded bar" in CLAUDE.md). A plain TextButton subclass rather than a
    // bespoke Component so it inherits the ordinary chip background every other button on
    // this bar draws (raised fill, hover, down); only the gear itself is drawn by hand, in
    // paintButton, after the base class has painted its chrome - this repo draws its own
    // chrome rather than shipping icon assets, the same rule the fold chevron follows in
    // SectionBar.
    struct GearButton : public juce::TextButton
    {
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    };
    GearButton gearButton;
    void showSettingsMenu();
    // "Check for updates" on the settings menu: an explicit, user-initiated re-check, as
    // opposed to the one silent pass the constructor already runs. Reuses updaterConfig and
    // okstudio::updater::checkNowAsync (the kit's, not a second updater written here) and
    // reports every outcome with a small dismissable message box - found, up to date, or
    // failed - because a button the user just clicked has to say something back, which
    // checkAsync's own once-per-process, found-only callback cannot.
    void checkForUpdatesNow();
    // "About": product name and version, both read live off processor.getName() and
    // KEYS_VERSION (the same macro updaterConfig.currentVersion is built from) rather than
    // written out a second time, plus the OK Studio line. A NativeMessageBox, the same
    // mouse-dismissable shape TakePanel already uses for its own "could not save" alert.
    void showAboutDialog();

    // The Instrument chip (2026-08-02, Owen: "the load instrument section with all that
    // should go in the controls submenu"). Keys Host is the only thing that ever sets
    // onBuildInstrumentMenu / instrumentName; plain Keys (the VST3, the plain standalone)
    // never does, so the chip stays invisible and this bar is exactly what it always was
    // there. It rides the left end, in the cell the Knobs chip vacated the same day, and it
    // is the one *elastic* control on this bar - see resized() for why the tempo group and
    // the keyboard-settings combos beyond it have to be measured first.
    // Undo / Redo, on the **Controls bar** (2026-08-14, Owen: "we should have undo").
    //
    // That bar because it never hides with its fold - the same rule that keeps the tempo, Root
    // and Scale on it. An undo you cannot reach because a section is collapsed is an undo you
    // cannot trust, and the whole value of it is being reachable at the moment you realise.
    //
    // Left end, before everything else, because it is the one control here that is *about* the
    // others: it reads as a header rather than as another setting. They grey when their stack
    // is empty rather than vanishing, so the pair never reflows the bar under the mouse.
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::uint32 lastUndoGen = 0xffffffffu; // forces the first refresh
    void refreshUndoButtons();

    juce::TextButton instrumentChip;
    void applyAccent(int index);

    // Humanize and its velocity range live on the *Pads* bar now (2026-08-02, Owen picked
    // that bar and asked to "make smaller to fit"), not in the Controls band: a playing-feel
    // control, the same reason the arp's On stays on a folded bar. The label lost "VELOCITY"
    // to fit a 36 px cell beside a 24 px button - it reads as a bare number/range now, and
    // the slider's own tooltip still spells the whole thing out.
    RangeSlider humanizeVelSlider; // a two-value range whose band drags as one; see RangeSlider.h
    juce::Label humanizeVelLabel;

    std::unique_ptr<ComboAtt> sizeAtt, rootAtt, scaleAtt, channelAtt, chordStrumDirAtt, polyphonyAtt;
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

    // Which chord the live card shows when both the keybed and another source are holding one.
    // The heartbeat compares each against its last value and prefers whichever just moved, so
    // "currently held" means the most recent gesture rather than everything ringing at once.
    std::vector<int> lastPlayedChord, lastHeldChord;
    bool preferHeldChord = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysEditor)
};
} // namespace keys
