#include "KeysHostProcessor.h"
#include "KeysHostEditor.h"
#include <okstudio/StateHelpers.h>
#include <vector>

namespace keys
{
KeysHostProcessor::KeysHostProcessor()
{
    formatManager.addFormat(new juce::VST3PluginFormat());
}

KeysHostProcessor::~KeysHostProcessor()
{
    // The editor (and with it the instrument's GUI) is always destroyed first by the
    // wrapper, so the instance can go down without a dangling editor.
    instrument = nullptr;
}

juce::String KeysHostProcessor::instrumentName() const
{
    return instrument != nullptr ? instrument->getName() : juce::String();
}

double KeysHostProcessor::getTailLengthSeconds() const
{
    return instrument != nullptr ? instrument->getTailLengthSeconds() : 0.0;
}

void KeysHostProcessor::resizeHostBuffer(int samplesPerBlock)
{
    int channels = 2;
    if (instrument != nullptr)
        channels = juce::jmax(channels, instrument->getTotalNumInputChannels(),
                              instrument->getTotalNumOutputChannels());
    hostBuffer.setSize(channels, juce::jmax(1, samplesPerBlock));
}

void KeysHostProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    KeysProcessor::prepareToPlay(sampleRate, samplesPerBlock);
    instrumentMidi.ensureSize(4096); // grown here so the audio-thread copy doesn't allocate
    if (instrument != nullptr)
    {
        instrument->releaseResources();
        instrument->setRateAndBufferSizeDetails(sampleRate, samplesPerBlock);
        instrument->prepareToPlay(sampleRate, samplesPerBlock);
        setLatencySamples(instrument->getLatencySamples());
    }
    resizeHostBuffer(samplesPerBlock);
}

void KeysHostProcessor::releaseResources()
{
    if (instrument != nullptr)
        instrument->releaseResources();
}

void KeysHostProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Base clears the audio and drains the clicked notes/CCs into `midi`; host MIDI
    // already in `midi` (a clip, another track) stays and reaches the instrument too.
    KeysProcessor::processBlock(buffer, midi);

    auto* inst = instrument.get();
    if (inst == nullptr)
        return;

    const int numSamples = buffer.getNumSamples();
    if (numSamples > hostBuffer.getNumSamples() || hostBuffer.getNumChannels() == 0)
        return; // block bigger than we prepared for; stay silent rather than allocate here

    juce::AudioBuffer<float> proxy(hostBuffer.getArrayOfWritePointers(),
                                   hostBuffer.getNumChannels(), numSamples);
    proxy.clear();
    instrumentMidi.clear();
    instrumentMidi.addEvents(midi, 0, numSamples, 0);
    inst->processBlock(proxy, instrumentMidi);

    for (int ch = juce::jmin(buffer.getNumChannels(), proxy.getNumChannels()); --ch >= 0;)
        buffer.copyFrom(ch, 0, proxy, ch, 0, numSamples);
    if (buffer.getNumChannels() > 1 && instrumentOutChannels == 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples); // mono instrument to both ears
}

juce::String KeysHostProcessor::loadInstrument(const juce::File& vst3File)
{
    return loadInstrumentInternal(vst3File, nullptr);
}

juce::String KeysHostProcessor::loadInstrumentInternal(const juce::File& vst3File,
                                                       const juce::MemoryBlock* stateToApply)
{
    JUCE_ASSERT_MESSAGE_THREAD
    loadError.clear();

    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* format : formatManager.getFormats())
        if (format->fileMightContainThisPluginType(vst3File.getFullPathName()))
            format->findAllTypesForFile(types, vst3File.getFullPathName());

    if (types.isEmpty())
    {
        loadError = "No VST3 plugin found in " + vst3File.getFileName();
        sendChangeMessage();
        return loadError;
    }

    const juce::PluginDescription* chosen = types.getFirst();
    for (auto* d : types)
        if (d->isInstrument)
        {
            chosen = d;
            break;
        }

    const double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    const int bs = getBlockSize() > 0 ? getBlockSize() : 512;
    juce::String error;
    auto inst = formatManager.createPluginInstance(*chosen, sr, bs, error);
    if (inst == nullptr)
    {
        loadError = error.isNotEmpty() ? error : "Could not open " + vst3File.getFileName();
        sendChangeMessage();
        return loadError;
    }

    attachInstrument(std::move(inst), vst3File, stateToApply);
    return {};
}

void KeysHostProcessor::ejectInstrument()
{
    JUCE_ASSERT_MESSAGE_THREAD
    loadError.clear();
    attachInstrument(nullptr, juce::File(), nullptr);
}

void KeysHostProcessor::attachInstrument(std::unique_ptr<juce::AudioPluginInstance> inst,
                                         const juce::File& file,
                                         const juce::MemoryBlock* stateToApply)
{
    if (onInstrumentWillChange)
        onInstrumentWillChange(); // the editor closes the old instrument GUI first

    suspendProcessing(true);
    if (instrument != nullptr)
        instrument->releaseResources();
    instrument = std::move(inst);
    instrumentFile = file;
    instrumentOutChannels = 0;

    if (instrument != nullptr)
    {
        // Best effort: ask for a stereo main out; if the plugin refuses we copy
        // whatever its first channels are.
        auto layout = instrument->getBusesLayout();
        if (! layout.outputBuses.isEmpty())
        {
            layout.outputBuses.getReference(0) = juce::AudioChannelSet::stereo();
            instrument->setBusesLayout(layout);
        }

        if (stateToApply != nullptr && stateToApply->getSize() > 0)
            instrument->setStateInformation(stateToApply->getData(), (int) stateToApply->getSize());

        const double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
        const int bs = getBlockSize() > 0 ? getBlockSize() : 512;
        instrument->setRateAndBufferSizeDetails(sr, bs);
        instrument->prepareToPlay(sr, bs);
        instrumentOutChannels = instrument->getTotalNumOutputChannels();
        resizeHostBuffer(bs);
        setLatencySamples(instrument->getLatencySamples());
    }
    else
    {
        setLatencySamples(0);
    }
    suspendProcessing(false);

    assignFaderParams();
    updateHostDisplay();
    sendChangeMessage();
}

void KeysHostProcessor::assignFaderParams()
{
    JUCE_ASSERT_MESSAGE_THREAD
    faderParamIndex.fill(-1);
    for (auto& name : faderParamName)
        name.clear();

    if (instrument == nullptr)
        return;

    // First unused match wins, case-insensitive substring on the parameter's display
    // name. Fader 0 prefers a match that also mentions "filter" when several cutoff-ish
    // names are available (e.g. picking "Filter Cutoff" over an unrelated "Frequency").
    static const std::array<std::vector<juce::String>, numFaders> keywords {{
        { "cutoff", "frequency", "freq" },
        { "resonance", "reso" },
        { "attack" },
        { "decay" },
        { "sustain" },
        { "release" },
        { "reverb", "wet", "mix" },
        { "drive", "distortion", "saturation" }
    }};

    auto& params = instrument->getParameters();
    std::vector<bool> used((size_t) params.size(), false);

    for (int fader = 0; fader < numFaders; ++fader)
    {
        int bestIndex = -1;
        bool bestHasFilter = false;

        for (int p = 0; p < params.size(); ++p)
        {
            if (used[(size_t) p])
                continue;

            const auto name = params[p]->getName(64);
            if (name.containsIgnoreCase("bypass"))
                continue;

            bool matches = false;
            for (auto& keyword : keywords[(size_t) fader])
            {
                if (name.containsIgnoreCase(keyword))
                {
                    matches = true;
                    break;
                }
            }
            if (! matches)
                continue;

            const bool hasFilter = name.containsIgnoreCase("filter");
            if (bestIndex < 0 || (fader == 0 && hasFilter && ! bestHasFilter))
            {
                bestIndex = p;
                bestHasFilter = hasFilter;
            }
        }

        if (bestIndex >= 0)
        {
            faderParamIndex[(size_t) fader] = bestIndex;
            faderParamName[(size_t) fader] = params[bestIndex]->getName(64);
            used[(size_t) bestIndex] = true;
        }
    }
}

void KeysHostProcessor::faderMoved(int faderIndex, float value01)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (faderIndex < 0 || faderIndex >= numFaders || instrument == nullptr)
        return;

    const int paramIndex = faderParamIndex[(size_t) faderIndex];
    if (paramIndex < 0)
        return;

    auto& params = instrument->getParameters();
    if (paramIndex >= params.size())
        return; // instrument's parameter list changed under us; ignore rather than crash

    params[paramIndex]->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, value01));
}

juce::String KeysHostProcessor::faderTargetName(int faderIndex) const
{
    if (faderIndex < 0 || faderIndex >= numFaders)
        return {};
    return faderParamName[(size_t) faderIndex];
}

void KeysHostProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree hosted { "hostedInstrument" };
    if (instrument != nullptr)
    {
        hosted.setProperty("file", instrumentFile.getFullPathName(), nullptr);
        hosted.setProperty("name", instrument->getName(), nullptr);
        juce::MemoryBlock blob;
        instrument->getStateInformation(blob);
        hosted.setProperty("state", blob.toBase64Encoding(), nullptr);
    }
    okstudio::state::save(apvts, "KEYS", destData, { chordPadsToTree(), arpToTree(), layoutToTree(), hosted });
}

void KeysHostProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    okstudio::state::load(apvts, data, sizeInBytes, [this](const juce::ValueTree& root)
    {
        // Everything Keys restores, restored the same way; then the one thing only Keys Host
        // has. This used to be the base class's three calls copied out by hand, which is how
        // a session fix landed in Keys and quietly skipped Keys Host.
        restoreSharedState(root);

        const auto hosted = root.getChildWithName("hostedInstrument");
        if (! hosted.isValid() || hosted.getProperty("file").toString().isEmpty())
        {
            if (instrument != nullptr)
                ejectInstrument();
            return;
        }

        const juce::File file(hosted.getProperty("file").toString());
        if (! file.exists())
        {
            loadError = "Instrument not found: " + file.getFullPathName();
            sendChangeMessage();
            return;
        }
        juce::MemoryBlock blob;
        blob.fromBase64Encoding(hosted.getProperty("state").toString());
        loadInstrumentInternal(file, &blob);
    });
}

juce::AudioProcessorEditor* KeysHostProcessor::createEditor()
{
    return new KeysHostEditor(*this);
}
} // namespace keys

#if defined(KEYS_HOST) && KEYS_HOST
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new keys::KeysHostProcessor();
}
#endif
