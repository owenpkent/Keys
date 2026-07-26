#include "AudioCapture.h"

namespace keys
{
namespace
{
const juce::String driverKey = "inputDriver";
const juce::String inputKey = "inputDevice";
constexpr int maxCaptureChannels = 32;
} // namespace

AudioCapture::AudioCapture()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "KeysAudioInput";
    options.filenameSuffix = "settings";
    options.folderName = "OK Studio";
    options.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile>(options);

    loadSelection();
}

AudioCapture::~AudioCapture()
{
    recording = false;
    closeDevice();
}

juce::StringArray AudioCapture::driverNames()
{
    juce::StringArray names;
    for (auto* type : devices.getAvailableDeviceTypes())
        names.add(type->getTypeName());
    return names;
}

void AudioCapture::setDriver(const juce::String& name)
{
    if (name == driver)
        return;

    setInput({}); // the previous driver's inputs mean nothing to the new one
    driver = name;
    saveSelection();
}

juce::StringArray AudioCapture::inputNames()
{
    if (auto* type = driverType())
    {
        type->scanForDevices();
        return type->getDeviceNames(true);
    }
    return {};
}

void AudioCapture::setInput(const juce::String& name)
{
    if (name == input)
        return;

    jassert(! recording.load()); // the panel locks the pickers while recording

    closeDevice();
    input = name;
    lastError.clear();
    saveSelection();

    if (panelOnScreen && input.isNotEmpty())
        openDevice();
}

bool AudioCapture::isDeviceOpen() const
{
    auto* device = devices.getCurrentAudioDevice();
    return device != nullptr && device->isOpen();
}

double AudioCapture::deviceSampleRate() const
{
    if (auto* device = devices.getCurrentAudioDevice())
        return device->getCurrentSampleRate();
    return 0.0;
}

void AudioCapture::setPanelOnScreen(bool onScreen)
{
    if (onScreen == panelOnScreen)
        return;

    panelOnScreen = onScreen;

    if (panelOnScreen)
    {
        // Nothing chosen yet: start on this machine's default input, so the section works on
        // first open instead of asking for a decision before showing anything.
        if (input.isEmpty() && ! settings->getBoolValue("configured", false))
        {
            if (driver.isEmpty())
            {
                const auto names = driverNames();
                driver = devices.getCurrentAudioDeviceType().isNotEmpty()
                             ? devices.getCurrentAudioDeviceType()
                             : (names.isEmpty() ? juce::String() : names[0]);
            }

            if (auto* type = driverType())
            {
                type->scanForDevices();
                const auto names = type->getDeviceNames(true);
                const int fallback = type->getDefaultDeviceIndex(true);

                if (juce::isPositiveAndBelow(fallback, names.size()))
                    input = names[fallback];
            }

            settings->setValue("configured", true);
            saveSelection();
        }

        if (input.isNotEmpty())
            openDevice();
    }
    else if (! recording.load())
    {
        closeDevice();
    }
}

bool AudioCapture::startRecording()
{
    if (recording.load())
    {
        jassertfalse;
        return false;
    }

    if (input.isEmpty())
    {
        lastError = "Choose an input first.";
        return false;
    }

    if (! openDevice())
        return false;

    capturedRate = juce::jmax(8000.0, deviceSampleRate());

    // Allocated here, on the message thread, while nothing is writing to it.
    captured.setSize(1, (int) (maxSeconds * capturedRate), false, true, true);
    captured.clear();

    written = 0;
    limitReached = false;
    recording = true;
    return true;
}

void AudioCapture::stopRecording()
{
    if (! recording.load())
        return;

    recording = false;

    if (! panelOnScreen)
        closeDevice();
}

void AudioCapture::clear()
{
    stopRecording();
    written = 0;
    limitReached = false;
    captured.setSize(1, 0);
}

double AudioCapture::recordedSeconds() const
{
    return capturedRate > 0.0 ? (double) written.load() / capturedRate : 0.0;
}

void AudioCapture::audioDeviceIOCallbackWithContext(const float* const* inputData, int numInputs,
                                                    float* const* outputData, int numOutputs,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    // We asked for an input-only device, but never hand back uninitialised output.
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputData[ch] != nullptr)
            juce::FloatVectorOperations::clear(outputData[ch], numSamples);

    if (numSamples <= 0)
        return;

    const float* channels[maxCaptureChannels];
    int numChannels = 0;

    for (int ch = 0; ch < numInputs && numChannels < maxCaptureChannels; ++ch)
        if (inputData[ch] != nullptr)
            channels[numChannels++] = inputData[ch];

    if (numChannels == 0)
        return;

    float blockPeak = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch)
        blockPeak = juce::jmax(blockPeak, juce::FloatVectorOperations::findMaximum(channels[ch], numSamples),
                               -juce::FloatVectorOperations::findMinimum(channels[ch], numSamples));

    float previous = peak.load();
    while (blockPeak > previous && ! peak.compare_exchange_weak(previous, blockPeak))
    {
    }

    if (! recording.load())
        return;

    const int start = written.load();
    const int room = captured.getNumSamples() - start;

    if (room <= 0)
    {
        // Out of tape. Stop ourselves rather than silently dropping audio; the panel notices
        // through hitLimit() and puts the section back into its stopped state.
        recording = false;
        limitReached = true;
        return;
    }

    const int toWrite = juce::jmin(numSamples, room);
    auto* dest = captured.getWritePointer(0);

    // Downmix as we go: mono is what both transcription and the waveform want.
    juce::FloatVectorOperations::copy(dest + start, channels[0], toWrite);

    for (int ch = 1; ch < numChannels; ++ch)
        juce::FloatVectorOperations::add(dest + start, channels[ch], toWrite);

    if (numChannels > 1)
        juce::FloatVectorOperations::multiply(dest + start, 1.0f / (float) numChannels, toWrite);

    written = start + toWrite;

    if (toWrite < numSamples)
    {
        recording = false;
        limitReached = true;
    }
}

void AudioCapture::audioDeviceAboutToStart(juce::AudioIODevice*)
{
    peak = 0.0f;
}

void AudioCapture::audioDeviceStopped()
{
    peak = 0.0f;
}

void AudioCapture::audioDeviceError(const juce::String& message)
{
    lastError = message;
}

juce::AudioIODeviceType* AudioCapture::driverType()
{
    for (auto* type : devices.getAvailableDeviceTypes())
        if (type->getTypeName() == driver)
            return type;
    return nullptr;
}

bool AudioCapture::openDevice()
{
    if (input.isEmpty())
        return false;

    if (isDeviceOpen() && devices.getAudioDeviceSetup().inputDeviceName == input)
        return true;

    lastError.clear();

    if (! managerInitialised)
    {
        // Two inputs wanted, no outputs. This is the only call that can briefly touch a device
        // other than the chosen one, so name ours as the preferred default.
        devices.initialise(2, 0, nullptr, false, input);
        managerInitialised = true;
    }

    if (devices.getCurrentAudioDeviceType() != driver)
        devices.setCurrentAudioDeviceType(driver, true);

    auto setup = devices.getAudioDeviceSetup();
    setup.inputDeviceName = input;
    setup.outputDeviceName = {};
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();

    lastError = devices.setAudioDeviceSetup(setup, true);

    if (! isDeviceOpen())
    {
        if (lastError.isEmpty())
            lastError = "Could not open " + input + ".";
        return false;
    }

    lastError.clear();
    devices.addAudioCallback(this);
    return true;
}

void AudioCapture::closeDevice()
{
    devices.removeAudioCallback(this);
    devices.closeAudioDevice();
    peak = 0.0f;
}

void AudioCapture::loadSelection()
{
    driver = settings->getValue(driverKey, {});
    input = settings->getValue(inputKey, {});
}

void AudioCapture::saveSelection() const
{
    settings->setValue(driverKey, driver);
    settings->setValue(inputKey, input);
    settings->saveIfNeeded();
}
} // namespace keys
