#include "TakePanel.h"
#include "KeysLookAndFeel.h"
#include <okstudio/MouseOnly.h>
#include <algorithm>

namespace keys
{
namespace
{
    juce::String clockText(double seconds)
    {
        const int s = juce::jmax(0, (int) seconds);
        return juce::String(s / 60) + ":" + juce::String(s % 60).paddedLeft('0', 2);
    }
} // namespace

void TakePanel::dragTakeOut(juce::Component* source, const juce::File& f)
{
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles({ f.getFullPathName() },
                                                                   false, source);
}

TakePanel::TakePanel(KeysProcessor& p) : processor(p)
{
    okstudio::ui::makeMouseOnly(*this);

    stats.setJustificationType(juce::Justification::centredLeft);
    stats.setColour(juce::Label::textColourId, skin::textDim);
    stats.setFont(skin::micro(11.0f).withExtraKerningFactor(0.16f));
    addAndMakeVisible(stats);

    addAndMakeVisible(roll);

    saveButton.setTooltip("Write a copy of this take wherever you like. The take is already "
                          "saved in Documents\\OK Studio\\Keys Takes; this is for putting one "
                          "somewhere else, under a name that means something.");
    saveButton.setTitle("Save take as");
    saveButton.onClick = [this] { saveAs(); };
    addAndMakeVisible(saveButton);

    revealButton.setTooltip("Show the take's file in Explorer, so it can be dragged from there.");
    revealButton.setTitle("Show take in Explorer");
    revealButton.onClick = [this]
    {
        const auto f = processor.lastTakeFile();
        if (f.existsAsFile())
            f.revealToUser();
    };
    addAndMakeVisible(revealButton);

    closeButton.setTitle("Close take");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeButton);

    refresh();
}

void TakePanel::refresh()
{
    // Polled from the editor's 30 Hz timer, so the cheap identity check comes first: rebuilding
    // the notes means building the whole MidiFile, which is not a thing to do thirty times a
    // second at rest. A take is only ever replaced wholesale, so its file plus its event count
    // name it - there is no edit that could change the notes and leave both.
    const auto file = processor.lastTakeFile();
    const int events = processor.capturedEventCount();
    if (file == shownFile && events == shownEvents)
        return;
    shownFile = file;
    shownEvents = events;

    auto next = processor.takeNotes();
    notes.swap(next);

    span = 0.0;
    lowNote = 127;
    highNote = 0;
    for (const auto& n : notes)
    {
        span = juce::jmax(span, n.startSec + n.lengthSec);
        lowNote = juce::jmin(lowNote, n.note);
        highNote = juce::jmax(highNote, n.note);
    }
    if (notes.empty())
    {
        lowNote = 60;
        highNote = 72;
    }
    // An octave of headroom minimum, so a take of one note is not drawn as one full-height bar
    // and a two-note take does not read as an octave apart.
    if (highNote - lowNote < 12)
    {
        const int pad = (12 - (highNote - lowNote)) / 2 + 1;
        lowNote = juce::jmax(0, lowNote - pad);
        highNote = juce::jmin(127, highNote + pad);
    }

    const bool has = ! notes.empty();
    stats.setText(has ? clockText(span) + "   .   " + juce::String((int) notes.size()) + " notes"
                          + "   .   " + juce::String(juce::roundToInt(processor.takeTempo())) + " BPM"
                      : juce::String("Nothing recorded yet - press REC on the Keyboard bar."),
                  juce::dontSendNotification);
    saveButton.setEnabled(has);
    revealButton.setEnabled(has && processor.lastTakeFile().existsAsFile());
    roll.repaint();
}

void TakePanel::saveAs()
{
    const auto source = processor.lastTakeFile();
    if (! source.existsAsFile())
        return;

    chooser = std::make_unique<juce::FileChooser>("Save this take as...", source, "*.mid");
    // Async, and the chooser is a member: launchAsync returns immediately and the callback runs
    // a turn later, so a stack-local one would be gone by the time it fired.
    chooser->launchAsync(juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [source](const juce::FileChooser& fc)
                         {
                             const auto target = fc.getResult();
                             if (target == juce::File())
                                 return; // cancelled
                             // Copied rather than rebuilt: the bytes already on disk are the
                             // ones the preview drew, and rebuilding could only introduce a
                             // difference between what was shown and what was saved.
                             source.copyFileTo(target.withFileExtension("mid"));
                         });
}

void TakePanel::paint(juce::Graphics& g)
{
    g.fillAll(skin::bgBot);

    g.setColour(skin::accentOf(*this).base.withAlpha(0.85f));
    g.setFont(skin::micro(10.0f).withExtraKerningFactor(0.32f));
    g.drawText("TAKE", getLocalBounds().reduced(14, 10).removeFromTop(14),
               juce::Justification::centredLeft);
}

void TakePanel::resized()
{
    auto area = getLocalBounds().reduced(14, 10);
    area.removeFromTop(16); // the TAKE caption, painted
    stats.setBounds(area.removeFromTop(18));
    area.removeFromTop(8);

    // The button row is reserved off the bottom *before* the roll takes what is left - the
    // standing rule. Three 34 px targets, which is the mouse-only floor exactly.
    auto buttons = area.removeFromBottom(34);
    area.removeFromBottom(10);
    closeButton.setBounds(buttons.removeFromRight(96));
    buttons.removeFromRight(8);
    revealButton.setBounds(buttons.removeFromRight(150));
    buttons.removeFromRight(8);
    saveButton.setBounds(buttons.removeFromRight(150));

    roll.setBounds(area);
}

void TakePanel::Roll::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour(skin::well);
    g.fillRoundedRectangle(b, 6.0f);

    const auto& notes = owner.notes;
    if (notes.empty())
    {
        g.setColour(skin::textFaint);
        g.setFont(skin::micro(11.0f).withExtraKerningFactor(0.2f));
        g.drawText("NO TAKE", b, juce::Justification::centred);
        return;
    }

    const auto inner = b.reduced(6.0f);
    const double span = juce::jmax(0.25, owner.span);
    const int range = juce::jmax(1, owner.highNote - owner.lowNote);
    const float rowH = inner.getHeight() / (float) (range + 1);

    // A bar line every second: enough to read the shape of a phrase without pretending this is
    // a grid you can edit on.
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    for (int s = 1; (double) s < span; ++s)
    {
        const float x = inner.getX() + inner.getWidth() * (float) ((double) s / span);
        g.fillRect(x, inner.getY(), 1.0f, inner.getHeight());
    }

    const auto accent = skin::accentOf(*this);
    for (const auto& n : notes)
    {
        const float x = inner.getX() + inner.getWidth() * (float) (n.startSec / span);
        const float w = juce::jmax(2.0f, inner.getWidth() * (float) (n.lengthSec / span));
        const float y = inner.getBottom() - (float) (n.note - owner.lowNote + 1) * rowH;
        const auto bar = juce::Rectangle<float>(x, y, juce::jmin(w, inner.getRight() - x),
                                                juce::jmax(2.0f, rowH - 1.0f));
        // Velocity reads as brightness, the one thing a bar can say for free about how it was
        // played. Never fully transparent: a quiet note is still a note that is there.
        g.setColour(accent.base.withAlpha(0.45f + 0.55f * n.velocity));
        g.fillRoundedRectangle(bar, 1.5f);
    }

    g.setColour(skin::textFaint);
    g.setFont(skin::micro(9.0f).withExtraKerningFactor(0.2f));
    g.drawText("DRAG THIS INTO YOUR DAW", b.reduced(8.0f, 4.0f),
               juce::Justification::bottomRight);
}

void TakePanel::Roll::mouseDown(const juce::MouseEvent&)
{
    wasDrag = false;
}

void TakePanel::Roll::mouseDrag(const juce::MouseEvent& e)
{
    // Four pixels of slop before a press becomes a drag, the tolerance the take chip and
    // RangeKnob's lamp both use: a gesture on a mouse-only surface is allowed to be untidy.
    if (wasDrag || e.getDistanceFromDragStart() <= 4)
        return;
    wasDrag = true;
    dragTakeOut(this, owner.processor.lastTakeFile());
}
} // namespace keys
