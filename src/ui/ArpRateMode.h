#pragma once

// The rate dial's Sync/Hz swap, and the APVTS attachment aliases the arp UI binds through.
//
// Two surfaces turn a rate dial - the band on a line's deep view and every macro card - and
// both of them face the same problem: the mode is a change of *unit*, so the dial is attached
// to one parameter in Sync and a different one in Hz, and exactly one attachment may be alive
// at a time. That swap was written out twice, the second copy carrying a comment saying "same
// rule and the same words as the band's", which is a rule with two implementations and no way
// to notice when they part. It is written once here (2026-09-02), parameterised by the line;
// what stays at each call site is what genuinely differs - the band greys a Tuplet caption and
// retitles three tooltips the card has no room for, and it does that work only on an actual
// change, where the card does it on every call.

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace keys
{
// The three APVTS attachment types, spelled once for the whole arp UI. ArpPanel re-exports
// them as member aliases, so `ArpPanel::ComboAtt` still names this type.
using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

namespace arprate
{
    // The dial's two attachments, handed over as a pair because the invariant is about the
    // pair: exactly one of them is ever non-null, and applyMode below is what owns that.
    struct Attachments
    {
        std::unique_ptr<SliderAtt>& sync;
        std::unique_ptr<SliderAtt>& hz;
    };

    // Point `dial` at whichever of the line's two rate parameters `free` names, and answer
    // whether anything actually moved - true means the caller owes the readout its text
    // function back, because the new attachment has just overwritten it.
    //
    // **Never swap the dial's attachment out from under a live drag.** A drag is a parameter
    // *gesture*: SliderParameterAttachment turns sliderDragStarted into beginChangeGesture and
    // sliderDragEnded into endChangeGesture, and its destructor only removes the listener - it
    // never closes one in flight. Both callers run this off a 10 Hz timer, so anything writing
    // arpRateFree from outside the panel (a Chain launching a slot on the bar line, host
    // automation, an MCP client) could otherwise destroy the live attachment mid-drag, leaving
    // a begin with no end on one parameter and an end with no begin on the other: a debug
    // assertion in JUCE, and a host latched in automation-write in a release build. So the
    // mode change waits. It is not deferred by a whole timer tick either: the dial's onDragEnd
    // calls back the moment the button comes up, after the attachment has closed its own
    // gesture (Slider invokes onDragEnd only once every listener's sliderDragEnded has run).
    //
    // `lastRateFree` is the caller's own cache and starts at -1, which is what forces the very
    // first call to install an attachment however the parameter reads. Setting it back to -1 is
    // also how a caller *forces* a rebuild it would otherwise skip - what ArpPanel::setEditLine
    // does, since a line change leaves the mode equal and the ids different.
    //
    // Swap, don't hand-sync. Destroy first so only one attachment is ever listening to the
    // dial, then build the other: it brings the parameter's range, its interval (eleven detents
    // in Sync, continuous in Hz), its skew and its text formatting with it, which is why the
    // readout reads "1/8" in one mode and "4.00 Hz" in the other with no code here.
    inline bool applyMode(juce::AudioProcessorValueTreeState& apvts, int line, juce::Slider& dial,
                          Attachments att, int& lastRateFree, bool dragging, bool free)
    {
        if (lastRateFree == (int) free)
            return false;
        if (dragging)
            return false; // onDragEnd calls back

        lastRateFree = (int) free;
        att.sync.reset();
        att.hz.reset();
        if (free)
            att.hz = std::make_unique<SliderAtt>(
                apvts, KeysProcessor::arpParamId(line, KeysProcessor::apRateHz), dial);
        else
            att.sync = std::make_unique<SliderAtt>(
                apvts, KeysProcessor::arpParamId(line, KeysProcessor::apRate), dial);
        return true;
    }
} // namespace arprate
} // namespace keys
