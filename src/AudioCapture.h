#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h> // PropertiesFile
#include <atomic>

namespace keys
{
// Records audio from a device Keys opens itself, for the Transcribe section to turn into
// notes. Keys is an instrument: a DAW sends it MIDI, never audio, so there is no track input
// to record and the only way to hear a voice or a guitar is to open a device directly. That
// also means this behaves identically in the plugin and in the standalone.
//
// The device is only open while the Transcribe section is on screen or while recording, so
// Keys never sits on a microphone in the background. The chosen device is remembered between
// sessions in a settings file, not in plugin state: it is a property of this machine, not of
// the song.
//
// Audio lands in one preallocated mono buffer. Mono because that is what transcription and
// the waveform both want, and preallocated because the alternative is allocating on the audio
// thread. Recording stops by itself at maxSeconds rather than growing without bound.
class AudioCapture : private juce::AudioIODeviceCallback
{
public:
    AudioCapture();
    ~AudioCapture() override;

    static constexpr double maxSeconds = 120.0;

    // --- Picking a device ----------------------------------------------------------------
    // These scan for devices, so they are not const and are not cheap; call them when the
    // panel opens or a combo changes, not every frame.

    juce::StringArray driverNames();
    juce::String selectedDriver() const { return driver; }
    void setDriver(const juce::String&);

    juce::StringArray inputNames();
    juce::String selectedInput() const { return input; }
    void setInput(const juce::String&);

    bool hasInput() const { return input.isNotEmpty(); }
    bool isDeviceOpen() const;
    double deviceSampleRate() const;
    juce::String error() const { return lastError; }

    // Hold the device open while the section is showing, so the level meter can prove signal
    // is arriving before anything is committed to tape.
    void setPanelOnScreen(bool);

    // Peak seen since the previous call, 0 to 1.
    float peakLevel() { return peak.exchange(0.0f); }

    // --- Recording -----------------------------------------------------------------------

    // Opens the device if needed. False if there is no device or it would not open, in which
    // case error() says why and nothing has started.
    bool startRecording();
    void stopRecording();
    bool isRecording() const { return recording.load(); }

    // True once the recording hit maxSeconds and stopped itself.
    bool hitLimit() const { return limitReached.load(); }

    void clear();

    // What has been recorded, mono, at recordedSampleRate(). Only read this when not
    // recording: while recording it is being written from the device thread.
    const juce::AudioBuffer<float>& recorded() const { return captured; }
    int recordedSamples() const { return (int) written.load(); }
    double recordedSampleRate() const { return capturedRate; }
    double recordedSeconds() const;

private:
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*, int, int,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String&) override;

    void loadSelection();
    void saveSelection() const;
    juce::AudioIODeviceType* driverType();
    bool openDevice();
    void closeDevice();

    juce::AudioDeviceManager devices;
    std::unique_ptr<juce::PropertiesFile> settings;

    // The device manager is only initialised when a device is first opened: initialising it
    // opens whatever it lands on, and listing inputs should never do that.
    bool managerInitialised = false;
    bool panelOnScreen = false;

    juce::String driver, input, lastError;

    juce::AudioBuffer<float> captured;
    double capturedRate = 48000.0;

    // Written from the device thread, read from the message thread.
    std::atomic<int> written { 0 };
    std::atomic<bool> recording { false };
    std::atomic<bool> limitReached { false };
    std::atomic<float> peak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioCapture)
};
} // namespace keys
