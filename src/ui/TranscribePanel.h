#pragma once

#include "../AudioCapture.h"
#include "KeysLookAndFeel.h"
#include <okstudio/Transcribe.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace keys
{
// The Transcribe section: record something you sang, hummed or played, and get notes back.
//
// Keys is played, not authored, and this is the one place it listens. The engine is
// basic-pitch, shared through the kit (okstudio/Transcribe.h); everything here is the UI in
// front of it: pick an input, record, look at what came out, drag it to a track.
//
// It is deliberately not live. Transcription needs the whole recording before it can resolve
// a note, so the shape is record -> stop -> notes appear, and the model runs on a background
// thread because it takes a good fraction of the recording's length.
class TranscribePanel : public juce::Component,
                        private juce::Timer
{
public:
    TranscribePanel();
    ~TranscribePanel() override;

    void resized() override;
    void paint(juce::Graphics&) override;
    void visibilityChanged() override;

    // Height the section asks for. Tall enough that the piano roll is worth looking at.
    static constexpr int idealHeight = 268;

private:
    void timerCallback() override;

    void toggleRecording();
    void startTranscription();
    void applySensitivity();  // re-derive notes from the last recording; no model rerun
    void refreshDevices();
    void updateEnablements();
    juce::String statusText() const;

    juce::Rectangle<int> waveformBounds() const;
    juce::Rectangle<int> pianoRollBounds() const;
    void paintWaveform(juce::Graphics&, juce::Rectangle<int>);
    void paintPianoRoll(juce::Graphics&, juce::Rectangle<int>);

    // Writes the notes to a temp .mid and hands it to the OS drag. The file has to outlive
    // the drag, so it lives in the temp directory under a name we reuse, not a scoped file.
    void dragMidiOut();

    AudioCapture capture;

    // Built on the first transcription rather than at construction: it loads the CNN weights
    // and starts an ONNX session, which is not worth doing for a section that may never be
    // opened.
    std::unique_ptr<okstudio::transcribe::Transcriber> transcriber;

    // Runs the model off the message thread, then posts the notes back to it.
    class Job;
    std::unique_ptr<Job> job;

    std::vector<okstudio::transcribe::Note> notes;

    juce::ComboBox driverBox, inputBox;
    juce::Label driverLabel, inputLabel;
    juce::TextButton recordButton { "Record" };
    juce::TextButton clearButton { "Clear" };

    // A drag source, not a button: an external file drag starts from a mouse drag, and there
    // is no click that can do it. Drawn to look like the buttons beside it so it reads as one
    // of them, and greyed the same way when there is nothing to drag.
    struct MidiDragSource : juce::SettableTooltipClient, juce::Component
    {
        std::function<void()> onDrag;
        void mouseDrag(const juce::MouseEvent&) override;
        void paint(juce::Graphics&) override;
    };
    MidiDragSource dragSource;
    juce::Slider sensitivitySlider;
    juce::Label sensitivityLabel;

    juce::StringArray inputDeviceNames;
    bool refreshing = false;

    float level = 0.0f;
    bool transcribing = false;
    juce::String message;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TranscribePanel)
};
} // namespace keys
