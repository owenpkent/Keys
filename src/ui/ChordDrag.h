#pragma once

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <utility>

namespace keys::chorddrag
{
// What a chord drag carries, and the two answers that come back on it.
//
// Every chord drag in Keys is stock `juce::DragAndDropContainer` / `DragAndDropTarget`
// (2026-08-02). It was hand-rolled on mouseDown/mouseDrag/mouseUp plus
// `Desktop::findComponentAt` until then, on the stated belief that JUCE could not deliver a drop
// across two top-level windows. **That belief was wrong.**
// `DragAndDropContainer::startDragging` takes a fourth parameter,
// `allowDraggingToExternalWindows`, defaulting to false; pass true and the drag image goes on the
// desktop rather than inside the container, which makes `getParentComponent()` null inside JUCE's
// own `findTarget` and routes the lookup through `findDesktopComponentBelow` - every desktop
// component in z-order, walking up each parent chain for an interested target. That is exactly
// the hit test the workaround was performing by hand, and it was there the whole time. Anything
// still claiming otherwise is out of date.
//
// The payload is a `ReferenceCountedObject` boxed in a `var` rather than an index the target
// re-reads. Boxing is what keeps the old semantics: a tray candidate belongs to no slot and is
// not in the session, so there is no index the far end could look it up by, and both ends of a
// pad drag already spoke in whole chords. It also makes the drag immune to the page flipping
// under it. The index below is provenance, not a way to fetch the chord.
struct Payload : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<Payload>;

    enum class From
    {
        trayCell, // a candidate in the generator's audition tray, in its own window
        padSlot,  // a card on the pad strip, by *absolute* slot so a page flip cannot move it
        liveCard, // the live "current chord" card at the left of the strip
        // The generator's reference box (2026-08-17). A kind of its own rather than trayCell,
        // because the two differ on the one question a target has to answer about them:
        // committing a tray candidate empties its cell (`consumed`), and the reference is a
        // *fixed point* that keeps its chord however many pads it fills. Reusing trayCell would
        // have emptied the box on the first drop out of it, which is the opposite of what it is.
        refCard
    };

    Payload(From f, int i, KeysProcessor::ChordPad c)
        : from(f), index(i), chord(std::move(c))
    {
    }

    const From from;
    const int index; // tray cell, absolute pad slot, or -1 for the live card
    const KeysProcessor::ChordPad chord;

    // Set by whichever target accepted the drop. **This is the veto**, and it is the one piece
    // of app semantics JUCE has no opinion about: dragging a card off the pad strip clears it,
    // and reaching for the generator's reference box means dragging a card off the strip, so
    // without an answer here the one gesture that keeps a chord would be the gesture that
    // deletes it. `DragAndDropContainer::dragOperationEnded` does not say whether anyone
    // accepted, so the targets say it here instead.
    bool taken = false;

    // Set only by a target that means "this candidate now lives somewhere else", which is a
    // *different* question from `taken`. A tray card dropped on a pad is committed and its cell
    // goes empty; the same card dropped on the reference box is copied and the candidate stays,
    // because a reference is a copy of a chord you like and taking the candidate away as payment
    // for keeping it would be backwards. Same gesture, opposite ownership, so two flags.
    bool consumed = false;
};

// Pull the payload back out, or nullptr when the drag is not one of ours.
inline Payload* of(const juce::var& description)
{
    return dynamic_cast<Payload*>(description.getObject());
}

inline Payload* of(const juce::DragAndDropTarget::SourceDetails& details)
{
    return of(details.description);
}

// What every chord target's `isInterestedInDragSource` starts from. An empty chord is refused
// everywhere: there is nothing to take, and lighting a target for it would promise otherwise.
inline Payload* chordBeingDragged(const juce::DragAndDropTarget::SourceDetails& details)
{
    auto* p = of(details);
    return (p != nullptr && ! p->chord.notes.empty()) ? p : nullptr;
}

// Read the two flags above, one message-loop turn after the button comes up.
//
// **Why a callAsync and not `DragAndDropContainer::dragOperationEnded`.** Both ends of a drag
// need to know how it finished, and the source's own `mouseUp` is too early: JUCE dispatches a
// component's own `mouseUp` before its mouse *listeners* (juce_Component.cpp, `internalMouseUp`),
// and the drag image is a listener - so `itemDropped` has not run yet when the source is asked.
// `dragOperationEnded` is late enough, but it fires from `~DragImageComponent`, which waits out
// a 120 ms dismissal animation and then a timer: a third of a second in which a card dragged off
// the row still looks like it is there. Posting from `mouseUp` lands after the same event's
// listener dispatch - so after `itemDropped` - and before the next frame, which is the only
// window that is both correct and invisible.
//
// The payload is captured by `Ptr`, so it outlives the drag image whatever order things die in,
// and the source is held by SafePointer so a window closed on the way through is a no-op.
template <typename ComponentType, typename Fn>
void whenDragSettles(ComponentType& source, Payload::Ptr payload, Fn&& fn)
{
    juce::Component::SafePointer<ComponentType> safe(&source);
    juce::MessageManager::callAsync(
        [safe, payload, fn = std::forward<Fn>(fn)]
        {
            if (auto* c = safe.getComponent())
                fn(*c, *payload);
        });
}
} // namespace keys::chorddrag
