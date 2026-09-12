#include "ChordShiftPanel.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>
#include <cmath>

namespace keys
{
namespace
{
    // The panel's geometry, written once and read by both resized() and contentSize(), so the
    // window's minimum size is the layout's own arithmetic and not a number somebody chose -
    // the same rule ChordGenPanel's constants follow.
    constexpr int kInset = 12;
    constexpr int kHeaderH = 34; // title, Close
    constexpr int kAfterHeader = 10;

    // The On switch. 40, not the 24 a bar chip gets away with: this is new UI real estate
    // rather than a bar this codebase already ships at 24, and CLAUDE.md's mouse-only floor
    // names no exception for a toggle.
    constexpr int kOnRowH = 40;
    constexpr int kOnW = 100;
    constexpr int kAfterOn = 12;

    // The shape row: a caption over ten equal buttons, the ChordGenPanel Source-row shape
    // (a row of buttons standing in for a choice parameter with no JUCE attachment, lit by
    // polling rather than by their own click). kShapeBtnW is a *minimum* - resized() spreads
    // whatever width the row actually has across the ten evenly, so a wider window buys a
    // bigger target rather than empty margin.
    constexpr int kShapeLabelH = 14;
    constexpr int kAfterShapeLabel = 4;
    constexpr int kShapeBtnH = 40;
    constexpr int kShapeBtnW = 70; // >= 34 with room to read "Sus4" / "Maj7" / "Dom7"
    constexpr int kShapeGap = 6;
    constexpr int kAfterShapeRow = 14;

    // The six shift rows. No Slider anywhere here: IncDecButtons needs real width to keep its
    // arrows from stacking into a sub-floor pair (CLAUDE.md's own paid-for trap, "note-count
    // steppers collapsed to 22 px in a 120 px cell"), and a hand-built `-`/`+` pair is the one
    // shape that lets every target be sized without asking a LookAndFeel to guess. Every row is
    // 40 px tall throughout, so nothing in it needs centring to clear the 34 px floor.
    constexpr int kShiftRowH = 40;
    constexpr int kShiftGap = 8;
    constexpr int kShiftCaptionW = 90;
    constexpr int kShiftStepW = 40;   // the `-` / `+` buttons: square, >= 34 both ways
    constexpr int kShiftStepGap = 8;
    constexpr int kShiftReadoutW = 80; // fits "-36 st" at the readout's own font

    constexpr int kTitleW = 140, kCloseW = 90, kHeaderGap = 8;

    void styleLabel(juce::Label& l, const juce::String& text)
    {
        l.setText(text.toUpperCase(), juce::dontSendNotification);
        l.setFont(skin::micro(10.0f));
        l.setColour(juce::Label::textColourId, skin::textDim);
    }

    // A shape's tooltip: what it stacks on top of the played note, in semitones - the same
    // numbers ChordShift.h's own table holds, spelled out rather than left to the name alone.
    juce::String shapeTooltip(const keys::chordshift::Shape& s)
    {
        if (juce::String(s.name) == "Off")
            return "Play exactly the note you press. No extra voices.";
        juce::String out = juce::String(s.name) + ": adds";
        bool any = false;
        for (const int v : s.shifts)
        {
            if (v == 0)
                continue;
            out << (any ? "," : "") << (v > 0 ? " +" : " ") << v;
            any = true;
        }
        out << " semitones above the note you press.";
        return out;
    }
} // namespace

juce::Point<int> ChordShiftPanel::contentSize()
{
    // The widest row wins the width, and the rows plus the gaps between them make the height -
    // ChordGenPanel::contentSize()'s own arithmetic, one window over.
    const int shapeRowW = numShapes * kShapeBtnW + (numShapes - 1) * kShapeGap;
    const int shiftRowW = kShiftCaptionW + kShiftStepGap + kShiftStepW
                         + kShiftStepGap + kShiftReadoutW + kShiftStepGap + kShiftStepW;
    const int headerW = kTitleW + kHeaderGap + kCloseW;
    const int w = juce::jmax(juce::jmax(headerW, kOnW), juce::jmax(shapeRowW, shiftRowW));

    const int h = kHeaderH + kAfterHeader + kOnRowH + kAfterOn
                + kShapeLabelH + kAfterShapeLabel + kShapeBtnH + kAfterShapeRow
                + keys::chordshift::numShifts * kShiftRowH
                + (keys::chordshift::numShifts - 1) * kShiftGap;
    return { w + kInset * 2, h + kInset * 2 };
}

juce::Point<int> ChordShiftPanel::minWindowSize()
{
    // The content, plus what DetachedWindow puts round it: a 38 px title bar (mouse-only, so the
    // window buttons clear the 34 px floor too) and its 1 px resizable border either side. Same
    // sum ChordGenPanel::minWindowSize() and TakePanel::minWindowSize() are built from.
    const auto c = contentSize();
    return { c.x + 8, c.y + 38 + 8 };
}

juce::Point<int> ChordShiftPanel::defaultWindowSize()
{
    // A little over the floor, so the first opening does not look already jammed against its
    // own minimum. No ceiling: unlike ChordGenPanel's audition tray, nothing here is elastic, so
    // there is nothing a taller or wider window could grow that would look wrong for it.
    const auto m = minWindowSize();
    return { m.x + 60, m.y + 30 };
}

ChordShiftPanel::ChordShiftPanel(KeysProcessor& p) : processor(p)
{
    jassert(keys::chordshift::shapes().size() == (size_t) numShapes);
    okstudio::ui::makeMouseOnly(*this);
    buildControls();
    refreshShapeLights();
    refreshShiftReadouts();
    startTimerHz(15); // the shape lights and the six readouts; nothing here needs faster
}

ChordShiftPanel::~ChordShiftPanel()
{
    stopTimer();
    // Nothing else to unwind: every control here is an APVTS attachment or a direct parameter
    // write, and this class never calls noteOn.
}

void ChordShiftPanel::buildControls()
{
    title.setText("Keybed Chord", juce::dontSendNotification);
    title.setFont(skin::uiSemi(16.0f).withExtraKerningFactor(0.04f));
    title.setColour(juce::Label::textColourId, skin::text);
    addAndMakeVisible(title);

    closeButton.setTitle("Close keybed chord window");
    closeButton.setTooltip("Shut this window. The chord keeps playing exactly as set - this is "
                           "a view of the parameters, not their only copy.");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeButton);

    onButton.setTitle("Keybed chord on");
    onButton.setTooltip("Turn the keybed's chord on. Off, one click plays exactly the note you "
                        "pressed and none of the six shifts below is read.");
    addAndMakeVisible(onButton);
    onAtt = std::make_unique<ButtonAtt>(processor.apvts, "keyChord", onButton);

    styleLabel(shapeLabel, "Shape");
    addAndMakeVisible(shapeLabel);

    const auto& shapes = keys::chordshift::shapes();
    for (int i = 0; i < (int) shapes.size(); ++i)
    {
        auto& b = shapeButtons[(size_t) i];
        const auto& shape = shapes[(size_t) i];
        b.setButtonText(shape.name);
        b.setTitle(juce::String("Chord shape ") + shape.name);
        b.setTooltip(shapeTooltip(shape));
        b.setClickingTogglesState(false); // refreshShapeLights owns the lit state, not the click
        b.onClick = [this, i] { applyShape(i); };
        addAndMakeVisible(b);
    }

    for (int i = 0; i < keys::chordshift::numShifts; ++i)
    {
        auto& row = shiftRows[(size_t) i];
        styleLabel(row.caption, "Shift " + juce::String(i + 1));
        addAndMakeVisible(row.caption);

        row.down.setTitle("Chord shift " + juce::String(i + 1) + " down");
        row.up.setTitle("Chord shift " + juce::String(i + 1) + " up");
        row.down.setTooltip("Lower this voice by a semitone.");
        row.up.setTooltip("Raise this voice by a semitone.");
        row.down.onClick = [this, i] { nudgeShift(i, -1); };
        row.up.onClick = [this, i] { nudgeShift(i, 1); };
        addAndMakeVisible(row.down);
        addAndMakeVisible(row.up);

        row.readout.setTitle("Chord shift " + juce::String(i + 1) + " amount");
        row.readout.setFont(skin::uiSemi(14.0f));
        row.readout.setColour(juce::Label::textColourId, skin::text);
        row.readout.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(row.readout);
    }
}

// One click writes all six shift parameters, which is the "one click" the whole feature is
// named for. Each write compares before it writes (see the local writeParam below), so
// clicking the shape that is already loaded costs nothing.
void ChordShiftPanel::applyShape(int shapeIndex)
{
    const auto& shape = keys::chordshift::shapes()[(size_t) shapeIndex];
    for (int i = 0; i < keys::chordshift::numShifts; ++i)
    {
        const auto id = "keyChordShift" + juce::String(i + 1);
        if (auto* param = processor.apvts.getParameter(id))
        {
            const float norm = param->convertTo0to1((float) shape.shifts[(size_t) i]);
            if (std::abs(param->getValue() - norm) > 1.0e-6f)
                param->setValueNotifyingHost(norm);
        }
    }
}

// The click-only path to a shift's value - a stepper, not a drag, the same reasoning the arp's
// rate steppers and the generator's note-count steppers are built on (CLAUDE.md: "a slider is a
// drag"). Reads the current value off the processor's own accessor rather than a second copy,
// so this can never disagree with what the readout beside it shows.
void ChordShiftPanel::nudgeShift(int index, int delta)
{
    const auto shifts = processor.keybedChordShifts();
    const int next = juce::jlimit(keys::chordshift::minShift, keys::chordshift::maxShift,
                                  shifts[(size_t) index] + delta);
    const auto id = "keyChordShift" + juce::String(index + 1);
    if (auto* param = processor.apvts.getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1((float) next));
}

void ChordShiftPanel::refreshShapeLights()
{
    const int idx = keys::chordshift::shapeIndexFor(processor.keybedChordShifts());
    if (idx == lastShapeShown)
        return;
    lastShapeShown = idx;
    for (int i = 0; i < (int) shapeButtons.size(); ++i)
        shapeButtons[(size_t) i].setToggleState(i == idx, juce::dontSendNotification);
}

// The parameter's own text function is the one place "+3 st" / "0 st" / "-12 st" is spelled out
// (see KeysParams.cpp), so this reads it back through getCurrentValueAsText() rather than
// formatting the number a second time - two copies of that rule is exactly the drift ChordShift.h's
// own header comment exists to rule out, one level up.
void ChordShiftPanel::refreshShiftReadouts()
{
    for (int i = 0; i < keys::chordshift::numShifts; ++i)
    {
        auto& readout = shiftRows[(size_t) i].readout;
        if (auto* param = processor.apvts.getParameter("keyChordShift" + juce::String(i + 1)))
        {
            const auto text = param->getCurrentValueAsText();
            if (readout.getText() != text)
                readout.setText(text, juce::dontSendNotification);
        }
    }
}

void ChordShiftPanel::timerCallback()
{
    refreshShapeLights();
    refreshShiftReadouts();
}

void ChordShiftPanel::paint(juce::Graphics& g)
{
    g.fillAll(skin::bgBot);
    const auto band = getLocalBounds().reduced(kInset).removeFromTop(kHeaderH + kAfterHeader / 2);
    g.setColour(skin::accentOf(*this).base.withAlpha(0.25f));
    g.fillRect((float) band.getX(), (float) band.getBottom(), (float) band.getWidth(), 1.0f);
}

void ChordShiftPanel::resized()
{
    auto area = getLocalBounds().reduced(kInset);
    if (area.isEmpty())
        return;

    auto top = area.removeFromTop(kHeaderH);
    title.setBounds(top.removeFromLeft(kTitleW));
    closeButton.setBounds(top.removeFromRight(kCloseW).withSizeKeepingCentre(kCloseW, kHeaderH));
    area.removeFromTop(kAfterHeader);

    auto onRow = area.removeFromTop(kOnRowH);
    onButton.setBounds(onRow.removeFromLeft(kOnW));
    area.removeFromTop(kAfterOn);

    shapeLabel.setBounds(area.removeFromTop(kShapeLabelH));
    area.removeFromTop(kAfterShapeLabel);
    {
        auto row = area.removeFromTop(kShapeBtnH);
        const int n = (int) shapeButtons.size();
        const int w = (row.getWidth() - kShapeGap * (n - 1)) / n;
        for (int i = 0; i < n; ++i)
        {
            shapeButtons[(size_t) i].setBounds(row.removeFromLeft(w));
            row.removeFromLeft(kShapeGap);
        }
    }
    area.removeFromTop(kAfterShapeRow);

    for (int i = 0; i < keys::chordshift::numShifts; ++i)
    {
        auto row = area.removeFromTop(kShiftRowH);
        auto& sr = shiftRows[(size_t) i];
        sr.caption.setBounds(row.removeFromLeft(kShiftCaptionW));
        row.removeFromLeft(kShiftStepGap);
        sr.down.setBounds(row.removeFromLeft(kShiftStepW));
        row.removeFromLeft(kShiftStepGap);
        sr.readout.setBounds(row.removeFromLeft(kShiftReadoutW));
        row.removeFromLeft(kShiftStepGap);
        sr.up.setBounds(row.removeFromLeft(kShiftStepW));
        if (i + 1 < keys::chordshift::numShifts)
            area.removeFromTop(kShiftGap);
    }
}

} // namespace keys
