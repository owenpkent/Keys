#pragma once

#include "KeysLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <okstudio/RotaryKnob.h>
#include <functional>

namespace keys
{
// A rotary that holds a *range* rather than a value: the knob face sets one end, and the span
// reaches back from that end. **The knob's own arc is the range** - there is no second ring
// (2026-08-03, Owen: "it looks like there's two rings around the knob ... just have the inner
// ring have the features. Everything should be reflected on that single ring"). The lit stretch
// runs from the range's bottom to its top and **travels with the face**: turn the knob and the
// whole range moves, keeping its width.
//
// Drawing it is one trick: the face paints its arc from the sweep's start to its value as any
// rotary does, and `paintOverChildren` masks the stretch below the range's bottom back to the
// track colour. So the kit's knob is untouched - no LookAndFeel to subclass, no second copy of
// its look to keep in step - and the only thing this file knows about the face's drawing is
// where that arc sits, which `faceArc()` holds alone.
//
// This is Serum's modulation-depth ring, and it is built from its manual rather than from a
// guess at it (2026-08-03, Owen: "the little blue dot ... when you click on it and drag it,
// that moves the outer ring ... and then when the outer ring is enabled, moving the dial moves
// the outer ring with it"). Serum's own words, page 195:
//
//   "A smaller blue halo appears to the top left of the knob. Hovering over this small halo
//    displays an Up/Down arrow control. Click and drag the arrow control to change the
//    modulation depth amount. As you drag the arrow, notice how the halo shrinks or expands to
//    show the range of modulation."
//
// So the grab is a **satellite at the top left**, dragged vertically - not a dot sitting on the
// ring, which is what a first reading of the picture suggested. That detail is the whole reason
// this is buildable here: a satellite is a component of its own, so it can be sized to a real
// target instead of Serum's few pixels, and it goes in the corner a round knob leaves empty in
// a rectangular cell. It is a child *above* the face in z-order, because the face is a Slider
// and a Slider eats every press inside its rectangle, corners included.
//
// Two departures from Serum, both forced by the mouse-only contract:
//   - Serum's fallback for the fiddly satellite is Option/Alt-click-drag on the knob body.
//     A modifier key is not a gesture Keys may require, so the fallback here is that **the
//     whole margin drags the span too** - every pixel of this component the face does not
//     cover, corners included. The satellite is the affordance; the margin is the forgiveness.
//   - No negative span. Serum flips the halo's hue for an inverted depth; there is nothing for
//     a range to invert into, so the span is unsigned.
//
// **The knob is the band's centre and the halo reaches equally both ways** (2026-08-19, Owen:
// "moving the halo shouldn't move knob. should be equal from center"). The halo drag touches
// only the span; the face never moves under it, and the band is value +/- span on both sides.
// The band opened downward from the face for its first sixteen days, which read as "lower the
// floor" rather than "more range".
//
// **A wall clips the side that meets it, and only that side** (2026-08-23, Owen: "moving knob
// moves halo weird"). Reading "equal from center" as *strictly* equal meant the nearer wall
// governed both sides, so turning the face toward either end squeezed the band shut and let it
// back out again - the halo moving under a hand that was on the knob, which is 2026-08-19's own
// complaint arriving from the other direction. See rangeLo/rangeHi.
//
// The span is not a parameter this class owns. It comes in with setSpan() and goes out through
// the three callbacks, so the consumer keeps the parameter, the gesture brackets and the undo
// story in one place - and so this stays usable against an APVTS, a plain value or anything
// else. Written kit-ready (skin tokens, no Keys types) but deliberately not in the kit yet:
// promoting it means moving this file beside okstudio/RotaryKnob.h and swapping `skin::` for
// the theme's own tokens.
class RangeKnob : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    RangeKnob()
    {
        addAndMakeVisible(knob);
        // The readout is this component's, not the Slider's: it has to say both ends, and a
        // Slider's text box only knows the one value it is attached to.
        knob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        // The lit arc starts at the range's bottom, which moves with the value as well as with
        // the span - so both writers have to say so.
        knob.onValueChange = [this]
        {
            syncArcOrigin();
            repaint();
            // The consumer's turn: a range stored as a low/high pair has to move its *low* when
            // the face moves, or the range would not travel with the knob at all. RangeKnob
            // owns this callback, so a consumer gets a hook here rather than fighting for it.
            if (onValueChanged)
                onValueChanged();
        };
        // Last, so it is on top of the face and gets the press first.
        addAndMakeVisible(handle);
        handle.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    // The knob face. Attach it, range it, name it exactly as any other rotary.
    okstudio::RotaryKnob& face() { return knob; }
    // The satellite, so a consumer can name it for a screen reader, and its own tooltip: it is
    // a separate control from the face and the ring, and it has to say so on hover.
    juce::Component& spanHandle() { return handle; }
    void setSpanTooltip(const juce::String& t) { handle.setTooltip(t); }


    // The widest the span may open, in the face's own units. **Defaults to the face's own
    // full travel**, which is the right answer whenever the ring is a span *of the face's
    // value* - Keys' H.TIME, where knob and ring are both "how late, in the same unit", so
    // the ring can in principle reach all the way down the face's travel.
    //
    // It is the wrong answer the moment the ring is an independent quantity with a range of
    // its own, and Keys has one: the arp macro card's VEL knob is a bipolar trim (-100..100,
    // 200 of travel) whose ring is Humanize Velocity (0..100). Left on the default, the drag
    // was calibrated to 200 while the parameter it wrote stopped at 100 - so the top half of
    // the satellite's travel did nothing, and the arc drawn from it fought the consumer's own
    // refresh, which reads the clamped parameter back ten times a second. Set this from the
    // ring's parameter and both halves of the gesture line up again.
    //
    // Non-positive means "follow the face", which is how a consumer asks for the default back.
    void setSpanMax(double maxSpan)
    {
        spanMaxOverride = maxSpan;
        setSpan(span); // re-clamp: narrowing this under the current span has to bite now
    }
    // What the span is actually clamped and calibrated against, the override resolved.
    double spanMax() const
    {
        return spanMaxOverride > 0.0 ? spanMaxOverride
                                     : juce::jmax(0.0, knob.getMaximum() - knob.getMinimum());
    }

    // How far the range reaches back from the face's value, in the face's own units.
    void setSpan(double v)
    {
        const auto clamped = juce::jlimit(0.0, spanMax(), v);
        if (std::abs(clamped - span) < 1.0e-9)
            return;
        span = clamped;
        syncArcOrigin();
        repaint();
    }
    double getSpan() const { return span; }

    // How much travel the face has on its **nearer** side, which is what the band may use on
    // both. See rangeLo/rangeHi for why it is the nearer one and not each side's own.
    double room() const
    {
        return juce::jmax(0.0, juce::jmin(knob.getValue() - knob.getMinimum(),
                                          knob.getMaximum() - knob.getValue()));
    }

    // How far the band actually reaches either side of the value: the span, until a wall is
    // nearer. One number for both sides, so the band is always symmetric about the face.
    double reach() const { return juce::jmax(0.0, juce::jmin(span, room())); }

    // **What the halo gesture is calibrated and clamped against, and it does not depend on
    // where the face is** (2026-08-23, Owen: "moving knob moves halo weird", then "the arp have
    // it right").
    //
    // It read a wall for a few hours - first the nearer, then the farther - and both were
    // wrong for the same reason, which is the lesson worth keeping: **a gesture whose range
    // depends on another control changes meaning when that control moves.** Turning the knob
    // silently re-scaled the halo's sensitivity and moved its ceiling, so the halo behaved
    // differently depending on where the knob happened to be, and one drag near a wall could
    // throw a 0..200 ms band across Strum's whole range.
    //
    // The arp's VEL ring is the shape that was already right: it carries `arpHumanVel`, whose
    // 0..127 is the same however the level beside it moves. So the ceiling is the span's own
    // maximum and nothing else - `setSpanMax` for a ring with a parameter of its own, half the
    // face's travel for the pads, since that is the widest a band centred on the knob can be.
    double usefulSpanMax() const { return spanMax(); }

    // The arithmetic behind the two gestures, public so a test can ask it the question directly
    // rather than synthesising mouse events - and so both gestures answer out of one place.
    // `pixelsUp` is positive for a drag upward, which is wider.
    double spanFromDrag(double startSpan, double pixelsUp) const
    {
        const auto full = usefulSpanMax();
        if (full <= 0.0)
            return startSpan; // no band available here: leave what is stored alone
        // The same 300 px of travel per full sweep okstudio::RotaryKnob asks for, so the ring
        // and the face each cross their own range under the same hand.
        return juce::jlimit(0.0, full, juce::jmin(startSpan, full) + pixelsUp * (full / dragPixels));
    }

    // One notch is a twentieth of the sweep, as it has been since the wheel arrived here.
    double spanFromWheel(double startSpan, double notches) const
    {
        const auto full = usefulSpanMax();
        if (full <= 0.0)
            return startSpan;
        return juce::jlimit(0.0, full, juce::jmin(startSpan, full) + notches * full * wheelNotch);
    }

    // The two ends the span and the value work out to. This is what the consumer's engine
    // should reproduce, and what the readout says.
    //
    // **The band is symmetric about the face, always, and that is load-bearing rather than
    // decorative.** Letting each end stop at its own wall was tried on 2026-08-23 and reverted
    // the same afternoon (Owen: "dragging halo is weird too"), because a consumer may store
    // the band as nothing but its two ends - Keys' Strum and Humanize are two int parameters
    // and no more - and derive the face as their midpoint. Symmetry is what makes that
    // derivation exact: midpoint == face, so a halo drag can never move the knob. Clip one end
    // and the midpoint slides off the face, so the knob crept under the halo and the pointer
    // sat outside the middle of its own lit arc - which is the very thing the band was centred
    // on the face to stop (2026-08-19, "moving the halo shouldn't move knob").
    //
    // The cost, which is real and was weighed: near a wall the band narrows as the face
    // approaches it, since the nearer side governs both. A band that keeps its width there
    // needs somewhere to record a centre that is not the midpoint of its ends, and neither the
    // pads' pair nor anything else here has one.
    double rangeLo() const { return knob.getValue() - reach(); }
    double rangeHi() const { return knob.getValue() + reach(); }

    std::function<void(double)> onSpanChanged;
    std::function<void()> onSpanDragStart, onSpanDragEnd;
    // Fired after the *face* moves, for a consumer whose range is stored as a low/high pair
    // and whose low therefore has to follow. Keys' Strum and Humanize are both that shape.
    std::function<void()> onValueChanged;

    // Set both and the lamp becomes a **switch** as well as a handle: a click toggles the
    // feature, a drag still opens and closes the range (2026-08-03, Owen: "clicking the blue
    // satellite button should turn on or off the feature. And then I don't think we need the
    // humanized check mark anymore"). Unlit means off. Leave them null and the lamp is a
    // handle only, which is what the arp's two Humanize knobs want - there "off" is just the
    // knob at zero, and a switch would be a second way to say the same thing.
    std::function<bool()> isOn;
    std::function<void(bool)> setOn;

    // How the pair reads under the knob. The default is the plainest thing that says "two
    // ends"; a consumer with units overrides it.
    std::function<juce::String(double lo, double hi)> textFromRange =
        [](double lo, double hi) { return juce::String((int) lo) + "-" + juce::String((int) hi); };
    // What the readout says while the switch is off, when there is no range to report. The
    // default is the face's own value; a consumer whose engine plays something else off (Keys'
    // Humanize plays the band's midpoint) should say so here rather than let the knob imply it.
    std::function<juce::String(double value)> textWhenOff =
        [](double v) { return juce::String((int) v); };

    // How far the face is inset inside this component - the margin the satellite and its stem
    // live in, *not* a ring that gets drawn - and the height reserved under it for the
    // readout. Both are set by the consumer's layout rather than assumed, since the row that
    // hosts this decides what it can afford.
    void setFaceInset(int px) { ring = juce::jmax(4, px); resized(); repaint(); }
    void setReadoutHeight(int px) { readout = juce::jmax(0, px); resized(); repaint(); }

    void resized() override
    {
        auto b = getLocalBounds();
        b.removeFromBottom(readout);
        // The face is inset by the ring on every side, so the ring is a frame around it and
        // the two never overlap - which is what lets "did the press reach me?" be the whole
        // hit test for the frame, with no geometry in mouseDown at all.
        knob.setBounds(b.reduced(ring));

        // The satellite sits **outside** the ring, in the corner - not on it. Serum's is a
        // small thing floating up and to the left of the knob with a short stem back to it,
        // and a first build that put it on the ring at the ring's own thickness read as a lump
        // growing out of the dial (Owen: "the satellite should not be on the wheel").
        const auto dial = b.toFloat();
        const auto c = dial.getCentre();
        const auto arc = faceArc();
        // A fifth of the face. It was 1.9x the inset (some 40% of the face) and read as a
        // second knob; a lamp wants to be small enough to be a marker.
        satSize = juce::jmax(8.0f, (float) ring * 1.0f);

        // Toward the dial box's own top-left *corner*, not along a 45 degree line: the box is
        // wider than it is tall, so the pocket a circle leaves empty points at the corner.
        auto dir = dial.getTopLeft() - c;
        dir /= juce::jmax(1.0f, dir.getDistanceFromOrigin());
        // Clear of the *halo*, not of the line - and then a gap the width of the lamp again on
        // top, because sitting exactly at the edge still reads as touching. Never so far it
        // leaves the cell, though: on a narrow column the corner arrives before the clearance
        // does, and a lamp half outside its own bounds is worse than one a little close in.
        const float clear = arc.outer + satSize * 1.5f;
        const float most = juce::jmin(std::abs((dial.getWidth() * 0.5f - satSize * 0.5f) / dir.x),
                                      std::abs((dial.getHeight() * 0.5f - satSize * 0.5f) / dir.y));
        satCentre = c + dir * juce::jmin(clear, most);

        // Drawn small, hit large: the component is bigger than the dot so it stays reachable
        // with one mouse. The padding only ever overlaps the ring, and a press on the ring does
        // this same job, so nothing is stolen from anything.
        const float hit = satSize + 8.0f;
        handle.setBounds(
            juce::Rectangle<float>(hit, hit).withCentre(satCentre).getSmallestIntegerContainer());
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        const auto textArea = b.removeFromBottom(readout);

        // The stem: a hairline from the knob's own arc out to the satellite, so the dot reads
        // as this knob's rather than as something floating in the gap between two columns.
        // Serum draws one, and without it the dot is an orphan.
        const auto arc = faceArc();
        auto toDot = satCentre - arc.centre;
        const auto len = juce::jmax(1.0f, toDot.getDistanceFromOrigin());
        toDot /= len;
        // From the halo's edge to the lamp, and faint: it is a tether, not a feature. Any
        // brighter and the eye reads a spoke sticking out of the knob.
        g.setColour(skin::textFaint.withAlpha(isEnabled() ? 0.30f : 0.12f));
        g.drawLine({ arc.centre + toDot * (arc.outer + 1.0f),
                     arc.centre + toDot * (len - satSize * 0.5f - 1.0f) },
                   1.0f);

        if (readout > 0)
        {
            // Off, the readout is one number too: "28-59" over an unlit lamp is the range
            // still claiming to be a range. What that single number *means* is the consumer's
            // - textWhenOff, since only it knows what its engine plays when the switch is off.
            g.setColour(isEnabled() ? skin::textDim : skin::textFaint);
            g.setFont(skin::ui(11.0f));
            g.drawText(switchedOff() ? textWhenOff(knob.getValue())
                                     : textFromRange(rangeLo(), rangeHi()),
                       textArea, juce::Justification::centred, false);
        }
    }

    // **One ring, not two** (2026-08-03, Owen: "it looks like there's two rings around the
    // knob ... just have the inner ring have the features. Everything should be reflected on
    // that single ring"). The face's own arc *is* the range: this tells the LookAndFeel where
    // to start lighting it, and it draws the range instead of everything below the value.
    //
    // Painting over it afterwards was the first attempt and does not work. Keys draws a value
    // arc as **three strokes** - a halo at 2.1x the line width, a body at 1.15, a hot core at
    // 0.55 - so a mask sized to the line leaves the halo's edges showing all the way round.
    // Owen's word for it was "a shadow of blue on the inner ring that isn't just the range",
    // and it is the reason `skin::arcFromProperty` exists rather than a paintOverChildren.
    void syncArcOrigin()
    {
        const auto lo = knob.getMinimum(), hi = knob.getMaximum();
        // Switched off, there is no range to draw and the knob goes back to being an ordinary
        // one - lit from the start of its sweep to its value (2026-08-03, Owen: "when humanize
        // is off, there's still a range appearance"). An unlit lamp over a range arc was the
        // control saying two things at once, and the arc is the louder of the two.
        const auto from = switchedOff() ? lo : rangeLo();
        const auto to = switchedOff() ? knob.getValue() : rangeHi();
        const auto norm = [lo, hi](double v)
        { return hi > lo ? juce::jlimit(0.0, 1.0, (v - lo) / (hi - lo)) : 0.0; };
        knob.getProperties().set(skin::arcFromProperty, norm(from));
        // The high half of the band sits past the pointer, which an end-at-value arc cannot
        // light - skin::arcToProperty is what reaches it (2026-08-19).
        knob.getProperties().set(skin::arcToProperty, norm(to));
        knob.repaint();
    }

    // Both halves of "what does the switch say", in one call for a consumer's timer: the lamp
    // and the arc are driven by a parameter no attachment here watches.
    void refresh()
    {
        syncArcOrigin();
        repaint();
    }

    // Whether the halo gesture is open, for a consumer's timer that syncs the knob from
    // parameters: pulling mid-gesture would yank the band out from under the hand.
    bool spanDragging() const { return dragging; }

    // Every press that reaches this component missed the face and missed the satellite, and
    // everything that is neither of those is the ring. No radius test: a corner press is a
    // ring press, on purpose - this is the forgiveness that replaces Serum's Alt+drag.
    void mouseDown(const juce::MouseEvent& e) override { beginSpanDrag(e); }
    void mouseDrag(const juce::MouseEvent& e) override { dragSpan(e); }
    void mouseUp(const juce::MouseEvent&) override { endSpanDrag(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        wheelSpan(w);
    }

private:
    // The satellite. A component of its own so it sits above the face in z-order and takes the
    // press there; every gesture is the owner's, so the ring and the satellite cannot drift.
    struct Handle : juce::Component,
                    public juce::SettableTooltipClient
    {
        explicit Handle(RangeKnob& o) : owner(o) {}
        void paint(juce::Graphics& g) override { owner.paintHandle(g, getLocalBounds().toFloat()); }
        void mouseDown(const juce::MouseEvent& e) override
        {
            moved = false;
            owner.beginSpanDrag(e);
        }
        void mouseDrag(const juce::MouseEvent& e) override
        {
            // Four pixels of slop before a press counts as a drag, so a click that shivers
            // still switches rather than nudging the range by a hair and doing nothing else.
            // The whole point of a mouse-only surface is that a click is allowed to be untidy.
            if (e.getDistanceFromDragStart() > 4)
                moved = true;
            owner.dragSpan(e);
        }
        void mouseUp(const juce::MouseEvent&) override
        {
            owner.endSpanDrag();
            if (! moved && owner.isOn && owner.setOn)
            {
                owner.setOn(! owner.isOn());
                owner.repaint();
            }
        }
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            owner.wheelSpan(w);
        }
        RangeKnob& owner;
        bool moved = false;
    };

    // Where the face draws its own arc, and how far out its ink actually reaches. The one
    // coupling in this file, and it is **KeysLookAndFeel's** geometry, not the kit's: the two
    // differ (the kit reduces by 4 and takes a fifth of the radius; Keys takes 3 off the radius
    // and a seventh of it), and a Keys editor installs Keys'. Using the wrong one put the
    // satellite inside the arc's halo, which is what "too close to the knob" was.
    //
    // `outer` is the *halo* pass, at 2.1x the line width - the widest of the three strokes the
    // value arc is made of, and therefore the edge anything sitting outside has to clear.
    bool switchedOff() const { return isOn && ! isOn(); }

    struct FaceArc { juce::Point<float> centre; float radius, width, outer; };
    FaceArc faceArc() const
    {
        const auto b = knob.getBounds().toFloat();
        const auto radius = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 3.0f;
        const auto width = juce::jmax(2.5f, radius * 0.15f);
        const auto arcR = radius - width * 0.5f;
        return { b.getCentre(), arcR, width, arcR + width * 1.05f };
    }

    float angleFor(double v) const
    {
        const auto lo = knob.getMinimum(), hi = knob.getMaximum();
        const auto p = knob.getRotaryParameters();
        const auto t = hi > lo ? (float) juce::jlimit(0.0, 1.0, (v - lo) / (hi - lo)) : 0.0f;
        return p.startAngleRadians + t * (p.endAngleRadians - p.startAngleRadians);
    }

    void strokeArc(juce::Graphics& g, juce::Point<float> centre, float r, double from, double to,
                   juce::Colour c, float thickness) const
    {
        juce::Path path;
        path.addCentredArc(centre.x, centre.y, r, r, 0.0f, angleFor(from), angleFor(to), true);
        g.setColour(c);
        g.strokePath(path, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }

    // Serum's "smaller blue halo": the satellite is a miniature of the ring, filled in
    // proportion to the span, so it reads as the same control shrunk rather than as a stray
    // dot. Its own arc is what the manual means by the halo shrinking and expanding.
    // A plain LED (2026-08-03, Owen: "the satellite works like a dot with a circle around it.
    // Just make it look like a plain LED light"). Solid, lit, and unchanging: it had an
    // outline ring and a pip inside it, which read as a tiny knob, and before that a miniature
    // arc that filled with the span, which was the span drawn twice. It is a grab point and a
    // place-marker; what it *did* is on the knob's own ring.
    void paintHandle(juce::Graphics& g, juce::Rectangle<float> local)
    {
        // Only the LED is drawn; the rest of this component is invisible hit padding.
        const auto b = juce::Rectangle<float>(satSize, satSize).withCentre(local.getCentre());
        const auto a = skin::accentOf(*this);
        // Unlit when the feature it switches is off - a lamp is the one control that can say
        // so without a word on it, which is why the Humanize tick box could go.
        const bool live = isEnabled() && (! isOn || isOn());

        // Two glow passes then the lamp - the skin's own lit-state recipe, wide and soft
        // before tight and bright, so it sits *in* the panel instead of on top of it. At one
        // hard pass and full accent it read as a sticker (Owen: "blend more").
        g.setColour((live ? a.base : skin::textFaint).withAlpha(live ? 0.10f : 0.05f));
        g.fillEllipse(b.expanded(4.0f));
        g.setColour((live ? a.base : skin::textFaint).withAlpha(live ? 0.20f : 0.08f));
        g.fillEllipse(b.expanded(1.5f));
        g.setColour(live ? (dragging ? a.hot : a.base.withAlpha(0.82f))
                         : skin::textFaint.withAlpha(0.45f));
        g.fillEllipse(b);
    }

    void beginSpanDrag(const juce::MouseEvent& e)
    {
        if (! isEnabled())
            return;
        dragging = true;
        // Screen coordinates, because this same gesture arrives from two different components
        // and their local origins are nowhere near each other.
        dragStartY = (float) e.getScreenPosition().y;
        dragStartSpan = span;
        if (onSpanDragStart)
            onSpanDragStart();
        repaint();
    }

    void dragSpan(const juce::MouseEvent& e)
    {
        if (! dragging)
            return;
        // **usefulSpanMax, not spanMax** - see the note there. Up is wider.
        if (usefulSpanMax() <= 0.0)
            return;
        applySpan(spanFromDrag(dragStartSpan,
                               (double) (dragStartY - (float) e.getScreenPosition().y)));
    }

    // The halo writes the span and nothing else (2026-08-19, Owen: "moving the halo shouldn't
    // move knob. should be equal from center"): the face is the band's centre and stays put
    // under this gesture. It carried half of every span change for one build - the band
    // opened around its centre by moving both the top and the floor - and the moving pointer
    // was exactly the part that was not asked for.
    void applySpan(double wantedSpan)
    {
        if (std::abs(wantedSpan - span) < 1.0e-9)
            return;
        span = wantedSpan;
        syncArcOrigin();
        if (onSpanChanged)
            onSpanChanged(span);
        repaint();
    }

    // The wheel on the halo or the ring margin, because a drag is a drag and the mouse has a
    // wheel: up is more, one notch is a twentieth of the full sweep. Each event is its own
    // bracketed gesture, the same shape a stepper click is.
    void wheelSpan(const juce::MouseWheelDetails& wheel)
    {
        if (! isEnabled() || dragging)
            return;
        // The gesture's own ceiling, the same one the drag uses - see usefulSpanMax.
        if (usefulSpanMax() <= 0.0 || wheel.deltaY == 0.0f)
            return;

        // **Scaled by the delta, and reversed when the OS says so.** This used to test only the
        // sign and apply a flat twentieth of the sweep per event, which is right for a notched
        // wheel and wrong for every smooth one: a precision touchpad, a tilt wheel or a
        // free-spinning mouse emits dozens of sub-notch events per physical gesture, so one
        // flick slammed the span from nothing to full and wrote a begin/endChangeGesture pair
        // into the host for each event on the way. A notch reports |deltaY| of about 1, so
        // multiplying keeps the notched feel identical and makes a tenth of a notch a tenth of
        // a step. `isReversed` is the OS's natural-scrolling flag, which JUCE reports rather
        // than applies - without it the tooltip's "up is more" is a lie on that setting.
        double dy = (double) wheel.deltaY;
        if (wheel.isReversed)
            dy = -dy;
        // A smooth device can report a great deal in one event when it is flung; a notch is the
        // most one event may be worth, so a fling is fast rather than instantaneous.
        const double notches = juce::jlimit(-1.0, 1.0, dy);
        const double wanted = spanFromWheel(span, notches);
        if (std::abs(wanted - span) < 1.0e-9)
            return; // already at the rail: no gesture, no automation write
        if (onSpanDragStart)
            onSpanDragStart();
        applySpan(wanted);
        if (onSpanDragEnd)
            onSpanDragEnd();
    }

    void endSpanDrag()
    {
        if (! dragging)
            return;
        dragging = false;
        if (onSpanDragEnd)
            onSpanDragEnd();
        repaint();
    }

    okstudio::RotaryKnob knob;
    Handle handle { *this };
    // Where the satellite sits and how big it is drawn, both worked out in resized() so paint()
    // and the stem cannot disagree with the component's own bounds about where it is.
    juce::Point<float> satCentre;
    float satSize = 10.0f;
    // The face's own drag sensitivity, so the ring and the knob cross their ranges under the
    // same hand, and the wheel's notch as a fraction of the sweep. Named rather than repeated,
    // since spanFromDrag and spanFromWheel are now the only readers of either.
    static constexpr double dragPixels = 300.0;
    static constexpr double wheelNotch = 0.05;
    double span = 0.0;
    // <= 0 means "follow the face's own travel"; see setSpanMax for the case that needs it.
    double spanMaxOverride = -1.0;
    int ring = 8;
    int readout = 15;

    bool dragging = false;
    float dragStartY = 0.0f;
    double dragStartSpan = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RangeKnob)
};
} // namespace keys
