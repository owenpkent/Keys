#include "TranscribePanel.h"

namespace keys
{
namespace
{
constexpr int rowH = 26;
constexpr int pad = 10;
constexpr int waveH = 46;
constexpr int bottomH = 30;

// A recording shorter than this cannot produce a note: the constant-Q transform needs more
// than a second before its low bins mean anything.
constexpr double minUsefulSeconds = 1.2;
} // namespace

// Runs the model away from the message thread. The panel owns one and throws it away when it
// finishes; the callback is posted back to the message thread, and is dropped if the panel
// died in the meantime.
class TranscribePanel::Job : public juce::Thread
{
public:
    Job(okstudio::transcribe::Transcriber& t, juce::AudioBuffer<float> a, double rate,
        okstudio::transcribe::Options o, std::function<void(std::vector<okstudio::transcribe::Note>)> done)
        : juce::Thread("Keys transcription"), transcriber(t), audio(std::move(a)), sampleRate(rate),
          options(o), onDone(std::move(done))
    {
    }

    ~Job() override { stopThread(4000); }

    void run() override
    {
        const auto mono = okstudio::transcribe::Transcriber::resampleForModel(audio, sampleRate);

        if (threadShouldExit())
            return;

        auto result = transcriber.transcribe(mono.getReadPointer(0), mono.getNumSamples(), options);

        if (threadShouldExit())
            return;

        juce::MessageManager::callAsync(
            [done = onDone, r = std::move(result)]() mutable { done(std::move(r)); });
    }

private:
    okstudio::transcribe::Transcriber& transcriber;
    juce::AudioBuffer<float> audio;
    double sampleRate;
    okstudio::transcribe::Options options;
    std::function<void(std::vector<okstudio::transcribe::Note>)> onDone;
};

TranscribePanel::TranscribePanel()
{
    auto setupCombo = [this](juce::ComboBox& box, juce::Label& label, const juce::String& text,
                             const juce::String& tip) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(skin::micro(10.0f));
        label.setColour(juce::Label::textColourId, skin::textDim);
        addAndMakeVisible(label);

        box.setTooltip(tip);
        box.setTextWhenNothingSelected("None");
        addAndMakeVisible(box);
    };

    setupCombo(driverBox, driverLabel, "DRIVER", "Which audio driver to list inputs from.");
    setupCombo(inputBox, inputLabel, "INPUT", "The audio input to record from: a microphone, or an interface.");

    driverBox.onChange = [this] {
        if (refreshing)
            return;
        capture.setDriver(driverBox.getText());
        refreshDevices();
    };

    inputBox.onChange = [this] {
        if (refreshing)
            return;
        const int index = inputBox.getSelectedItemIndex();
        capture.setInput(juce::isPositiveAndBelow(index, inputDeviceNames.size()) ? inputDeviceNames[index]
                                                                                  : juce::String());
        refreshDevices();
    };

    recordButton.setTooltip("Record from the chosen input, then transcribe what you played.");
    recordButton.onClick = [this] { toggleRecording(); };
    addAndMakeVisible(recordButton);

    clearButton.setTooltip("Throw away the recording and the notes.");
    clearButton.onClick = [this] {
        capture.clear();
        notes.clear();
        message.clear();
        if (transcriber != nullptr)
            transcriber->reset();
        updateEnablements();
        repaint();
    };
    addAndMakeVisible(clearButton);

    dragSource.setTooltip("Drag from here to a track in your DAW to drop the notes as a MIDI file.");
    dragSource.onDrag = [this] { dragMidiOut(); };
    addAndMakeVisible(dragSource);

    sensitivityLabel.setText("SENSITIVITY", juce::dontSendNotification);
    sensitivityLabel.setFont(skin::micro(10.0f));
    sensitivityLabel.setColour(juce::Label::textColourId, skin::textDim);
    addAndMakeVisible(sensitivityLabel);

    sensitivitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    sensitivitySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 20);
    sensitivitySlider.setRange(0.05, 0.95, 0.01);
    sensitivitySlider.setValue(0.7, juce::dontSendNotification);
    sensitivitySlider.setTooltip("Higher finds more notes. Changing it re-reads the same recording, "
                                 "so it is quick.");
    sensitivitySlider.onValueChange = [this] { applySensitivity(); };
    addAndMakeVisible(sensitivitySlider);

    updateEnablements();
}

TranscribePanel::~TranscribePanel()
{
    stopTimer();
    job.reset(); // joins the thread before the transcriber it borrows goes away
}

void TranscribePanel::resized()
{
    auto r = getLocalBounds().reduced(pad, 8);

    auto row = r.removeFromTop(rowH);

    driverLabel.setBounds(row.removeFromLeft(46));
    driverBox.setBounds(row.removeFromLeft(150).reduced(0, 2));
    row.removeFromLeft(10);
    inputLabel.setBounds(row.removeFromLeft(40));
    inputBox.setBounds(row.removeFromLeft(210).reduced(0, 2));

    // Right-aligned: the buttons keep their place as the window widens, and the level meter
    // takes up whatever slack is left in the middle (see paint()).
    dragSource.setBounds(row.removeFromRight(96).reduced(0, 2));
    row.removeFromRight(6);
    clearButton.setBounds(row.removeFromRight(64).reduced(0, 2));
    row.removeFromRight(6);
    recordButton.setBounds(row.removeFromRight(84).reduced(0, 2));

    auto bottom = getLocalBounds().reduced(pad, 8).removeFromBottom(bottomH);
    sensitivityLabel.setBounds(bottom.removeFromLeft(70));
    sensitivitySlider.setBounds(bottom.removeFromLeft(240).reduced(0, 3));
}

juce::Rectangle<int> TranscribePanel::waveformBounds() const
{
    return getLocalBounds().reduced(pad, 8).withTrimmedTop(rowH + 6).withHeight(waveH);
}

juce::Rectangle<int> TranscribePanel::pianoRollBounds() const
{
    auto r = getLocalBounds().reduced(pad, 8).withTrimmedTop(rowH + 6 + waveH + 6);
    return r.withTrimmedBottom(bottomH + 4);
}

void TranscribePanel::paint(juce::Graphics& g)
{
    g.fillAll(skin::bgBot);

    // Level meter, in the gap the row layout leaves between the input combo and the buttons.
    const int meterX = inputBox.getRight() + 12;
    const int meterW = recordButton.getX() - 12 - meterX;

    if (meterW > 30)
    {
        auto meter = juce::Rectangle<int>(meterX, inputBox.getY(), meterW, inputBox.getHeight()).toFloat();
        g.setColour(skin::well);
        g.fillRoundedRectangle(meter, skin::radius);

        if (level > 0.001f)
        {
            const auto accent = skin::accentOf(*this).base;
            g.setColour(level > 0.95f ? juce::Colours::orangered : accent);
            g.fillRoundedRectangle(meter.withWidth(meter.getWidth() * juce::jmin(level, 1.0f)), skin::radius);
        }
    }

    paintWaveform(g, waveformBounds());
    paintPianoRoll(g, pianoRollBounds());

    g.setColour(skin::textDim);
    g.setFont(skin::micro(10.0f));
    g.drawText(statusText(), getLocalBounds().reduced(pad, 8).removeFromBottom(bottomH).withTrimmedLeft(320),
               juce::Justification::centredLeft);
}

void TranscribePanel::paintWaveform(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(skin::well);
    g.fillRoundedRectangle(area.toFloat(), skin::radius);

    const int numSamples = capture.recordedSamples();

    if (numSamples <= 0)
    {
        g.setColour(skin::textDim.withAlpha(0.6f));
        g.setFont(skin::micro(10.0f));
        g.drawText(capture.hasInput() ? "Record something and it will appear here."
                                      : "Choose an input above to record.",
                   area, juce::Justification::centred);
        return;
    }

    // One vertical bar per pixel column, from the peak of the samples behind it.
    const auto* samples = capture.recorded().getReadPointer(0);
    const auto plot = area.reduced(4, 4).toFloat();
    const int columns = juce::jmax(1, (int) plot.getWidth());
    const float mid = plot.getCentreY();

    g.setColour(skin::accentOf(*this).base.withAlpha(0.75f));

    for (int x = 0; x < columns; ++x)
    {
        const int from = (int) ((juce::int64) numSamples * x / columns);
        const int to = juce::jmax(from + 1, (int) ((juce::int64) numSamples * (x + 1) / columns));

        float highest = 0.0f;
        for (int i = from; i < juce::jmin(to, numSamples); ++i)
            highest = juce::jmax(highest, std::abs(samples[i]));

        const float half = juce::jmax(0.5f, highest * plot.getHeight() * 0.5f);
        g.fillRect(plot.getX() + (float) x, mid - half, 1.0f, half * 2.0f);
    }
}

void TranscribePanel::paintPianoRoll(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(skin::well);
    g.fillRoundedRectangle(area.toFloat(), skin::radius);

    if (notes.empty())
    {
        g.setColour(skin::textDim.withAlpha(0.6f));
        g.setFont(skin::micro(10.0f));
        g.drawText(transcribing ? "Transcribing..." : "Notes appear here once a recording is transcribed.",
                   area, juce::Justification::centred);
        return;
    }

    // Fit the notes that exist rather than showing the whole MIDI range: a sung line covers
    // an octave or two, and a fixed range would draw it as a thin band in the middle.
    int lowest = 127, highest = 0;
    double lastEnd = 0.0;

    for (const auto& note : notes)
    {
        lowest = juce::jmin(lowest, note.pitch);
        highest = juce::jmax(highest, note.pitch);
        lastEnd = juce::jmax(lastEnd, note.endSeconds);
    }

    lowest = juce::jmax(0, lowest - 2);
    highest = juce::jmin(127, highest + 2);

    const auto plot = area.reduced(4, 4).toFloat();
    const int span = juce::jmax(1, highest - lowest + 1);
    const float rowHeight = plot.getHeight() / (float) span;
    const double duration = juce::jmax(0.5, lastEnd);

    // Faint lane stripes on the C's, so the pitch axis reads as pitch rather than as rows.
    g.setColour(skin::text.withAlpha(0.05f));
    for (int pitch = lowest; pitch <= highest; ++pitch)
        if (pitch % 12 == 0)
            g.fillRect(plot.getX(), plot.getBottom() - (float) (pitch - lowest + 1) * rowHeight,
                       plot.getWidth(), rowHeight);

    const auto accent = skin::accentOf(*this).base;

    for (const auto& note : notes)
    {
        const float x = plot.getX() + (float) (note.startSeconds / duration) * plot.getWidth();
        const float w = juce::jmax(2.0f, (float) ((note.endSeconds - note.startSeconds) / duration)
                                             * plot.getWidth());
        const float y = plot.getBottom() - (float) (note.pitch - lowest + 1) * rowHeight;

        g.setColour(accent.withAlpha(0.35f + 0.65f * (float) juce::jlimit(0.0, 1.0, note.amplitude)));
        g.fillRoundedRectangle(x, y + 0.5f, w, juce::jmax(2.0f, rowHeight - 1.0f), 1.5f);
    }
}

void TranscribePanel::visibilityChanged()
{
    capture.setPanelOnScreen(isVisible());

    if (isVisible())
    {
        refreshDevices();
        startTimerHz(20);
    }
    else
    {
        stopTimer();
        level = 0.0f;
    }
}

void TranscribePanel::timerCallback()
{
    const float peak = capture.peakLevel();
    level = peak > level ? peak : level * 0.8f; // rise now, fall back slowly

    if (level < 0.001f)
        level = 0.0f;

    // The device can stop the recording itself when it runs out of tape.
    if (recordButton.getToggleState() && ! capture.isRecording())
    {
        recordButton.setToggleState(false, juce::dontSendNotification);
        startTranscription();
    }

    repaint();
}

void TranscribePanel::toggleRecording()
{
    if (capture.isRecording())
    {
        capture.stopRecording();
        recordButton.setToggleState(false, juce::dontSendNotification);
        startTranscription();
        return;
    }

    notes.clear();
    message.clear();

    if (! capture.startRecording())
    {
        message = capture.error();
        updateEnablements();
        repaint();
        return;
    }

    recordButton.setToggleState(true, juce::dontSendNotification);
    updateEnablements();
}

void TranscribePanel::startTranscription()
{
    updateEnablements();

    if (capture.recordedSeconds() < minUsefulSeconds)
    {
        message = "Too short to transcribe: the model needs more than a second of audio.";
        repaint();
        return;
    }

    if (transcriber == nullptr)
        transcriber = std::make_unique<okstudio::transcribe::Transcriber>();

    okstudio::transcribe::Options options;
    options.noteSensitivity = (float) sensitivitySlider.getValue();

    // Hand the job its own copy of just the audio that was recorded, so the capture buffer is
    // free to be cleared or recorded over while the model runs.
    juce::AudioBuffer<float> audio(1, capture.recordedSamples());
    audio.copyFrom(0, 0, capture.recorded(), 0, 0, capture.recordedSamples());

    transcribing = true;
    message.clear();

    job = std::make_unique<Job>(*transcriber, std::move(audio), capture.recordedSampleRate(), options,
                                [this](std::vector<okstudio::transcribe::Note> result) {
                                    notes = std::move(result);
                                    transcribing = false;

                                    if (notes.empty())
                                        message = "No notes found. Try a higher sensitivity, or "
                                                  "record with more level.";

                                    updateEnablements();
                                    repaint();
                                });
    job->startThread();

    updateEnablements();
    repaint();
}

void TranscribePanel::applySensitivity()
{
    if (transcriber == nullptr || transcribing || notes.empty())
        return;

    okstudio::transcribe::Options options;
    options.noteSensitivity = (float) sensitivitySlider.getValue();

    // Cheap: only the note-event stage reruns, so this can follow the slider live.
    notes = transcriber->retranscribe(options);
    updateEnablements();
    repaint();
}

void TranscribePanel::refreshDevices()
{
    const juce::ScopedValueSetter<bool> guard(refreshing, true);

    const auto drivers = capture.driverNames();
    driverBox.clear(juce::dontSendNotification);
    driverBox.addItemList(drivers, 1);
    driverBox.setSelectedItemIndex(juce::jmax(0, drivers.indexOf(capture.selectedDriver())),
                                   juce::dontSendNotification);

    inputDeviceNames = capture.inputNames();
    inputBox.clear(juce::dontSendNotification);
    inputBox.addItemList(inputDeviceNames, 1);

    const int index = inputDeviceNames.indexOf(capture.selectedInput());

    if (index >= 0)
        inputBox.setSelectedItemIndex(index, juce::dontSendNotification);
    else if (capture.hasInput())
    {
        // Remembered from another machine, or unplugged since.
        inputBox.addItem(capture.selectedInput() + " (not found)", inputDeviceNames.size() + 1);
        inputBox.setSelectedItemIndex(inputDeviceNames.size(), juce::dontSendNotification);
    }

    updateEnablements();
    repaint();
}

void TranscribePanel::updateEnablements()
{
    const bool recording = capture.isRecording();

    recordButton.setButtonText(recording ? "Stop" : "Record");
    recordButton.setEnabled(capture.hasInput() && ! transcribing);
    clearButton.setEnabled(! recording && ! transcribing
                           && (capture.recordedSamples() > 0 || ! notes.empty()));
    dragSource.setEnabled(! notes.empty());

    // Changing device mid-recording would splice two different clocks into one buffer.
    driverBox.setEnabled(! recording && ! transcribing);
    inputBox.setEnabled(! recording && ! transcribing);
    sensitivitySlider.setEnabled(! notes.empty() && ! transcribing);
}

juce::String TranscribePanel::statusText() const
{
    if (message.isNotEmpty())
        return message;

    if (capture.isRecording())
    {
        const auto seconds = capture.recordedSeconds();
        return "Recording " + juce::String(seconds, 1) + "s (stops at "
               + juce::String((int) AudioCapture::maxSeconds) + "s)";
    }

    if (transcribing)
        return "Transcribing...";

    if (capture.hitLimit())
        return "Stopped at the " + juce::String((int) AudioCapture::maxSeconds) + "s limit.";

    if (! notes.empty())
        return juce::String(notes.size()) + " notes. Drag them to a track.";

    if (! capture.hasInput())
        return "No input chosen.";

    if (capture.isDeviceOpen())
        return juce::String(capture.deviceSampleRate() / 1000.0, 1) + " kHz, ready.";

    return "Input opens when you record.";
}

void TranscribePanel::MidiDragSource::mouseDrag(const juce::MouseEvent&)
{
    if (isEnabled() && onDrag)
        onDrag();
}

void TranscribePanel::MidiDragSource::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float alpha = isEnabled() ? 1.0f : 0.4f;

    g.setColour(skin::control.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(b, skin::radius);
    g.setColour(skin::controlBot.withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(b.reduced(0.5f), skin::radius, 1.0f);

    g.setColour(skin::text.withMultipliedAlpha(alpha));
    g.setFont(skin::micro(10.0f));
    g.drawText("DRAG MIDI", getLocalBounds(), juce::Justification::centred);
}

void TranscribePanel::dragMidiOut()
{
    if (notes.empty())
        return;

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("Keys transcription.mid");

    // One reused name: the file has to outlive the drag, and a fresh one per drag would
    // litter the temp directory for the rest of the session.
    if (auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream()))
    {
        stream->setPosition(0);
        stream->truncate();
        okstudio::transcribe::Transcriber::toMidiFile(notes).writeTo(*stream);
        stream->flush();
    }
    else
    {
        return;
    }

    juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() }, false, this);
}
} // namespace keys
