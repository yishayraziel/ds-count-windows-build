#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace dsq;

DSCountProcessor::DSCountProcessor()
    : AudioProcessor (BusesProperties()
          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undo, "PARAMS", ParameterManager::createLayout()),
      params (apvts),
      presets (apvts)
{
    presets.loadFactory (0);
    undo.clearUndoHistory();
}

DSCountProcessor::~DSCountProcessor() { cancelPendingUpdate(); }

bool DSCountProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in != out || (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()))
        return false;
    if (l.inputBuses.size() > 1)
    {
        const auto sc = l.inputBuses.getReference (1);
        return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
    }
    return true;
}

void DSCountProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedBlockSize = samplesPerBlock;
    prepared = false;
    const auto p = params.read();
    chain.prepare (sampleRate, params.raw (ids::lowLatency) > 0.5f);
    chain.setParamsImmediately (p);
    setLatencySamples (chain.latencySamples());
    prepared = true;
}

void DSCountProcessor::reset()
{
    if (prepared)
    {
        chain.reset();
        chain.setParamsImmediately (params.read());
    }
}

void DSCountProcessor::handleAsyncUpdate()
{
    if (! prepared)
        return;

    if ((params.raw (ids::lowLatency) > 0.5f) == chain.isLowLatency())
        return;
    suspendProcessing (true);
    prepareToPlay (chain.getSampleRate(), preparedBlockSize);
    suspendProcessing (false);
    updateHostDisplay (ChangeDetails().withLatencyChanged (true));
}

template <typename T>
void DSCountProcessor::process (juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    const auto main = getBusBuffer (buffer, true, 0);
    const int numChannels = std::min (2, main.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChannels == 0 || numSamples == 0)
        return;
    if (! prepared)
    {
        buffer.clear();
        return;
    }

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    const auto p = params.read();
    if ((params.raw (ids::lowLatency) > 0.5f) != chain.isLowLatency())
        triggerAsyncUpdate();

    chain.setParams (p);

    const T* sc[2] = { nullptr, nullptr };
    int scChannels = 0;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        const auto scBus = getBusBuffer (buffer, true, 1);
        scChannels = std::min (2, scBus.getNumChannels());
        for (int c = 0; c < scChannels; ++c)
            sc[c] = scBus.getReadPointer (c);
    }

    const T* in[2] = { main.getReadPointer (0), numChannels > 1 ? main.getReadPointer (1) : nullptr };
    T* out[2] = { buffer.getWritePointer (0), numChannels > 1 ? buffer.getWritePointer (1) : nullptr };
    chain.process<T> (in, out, scChannels > 0 ? sc : nullptr, numChannels, scChannels, numSamples);
}

void DSCountProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) { process (b); }
void DSCountProcessor::processBlock (juce::AudioBuffer<double>& b, juce::MidiBuffer&) { process (b); }

juce::AudioProcessorParameter* DSCountProcessor::getBypassParameter() const { return apvts.getParameter (ids::globalBypass); }

juce::AudioProcessorEditor* DSCountProcessor::createEditor() { return new DSCountEditor (*this); }

void DSCountProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root ("DSCOUNT");
    root.setProperty ("version", JucePlugin_VersionString, nullptr);
    root.setProperty ("uiWidth", uiWidth, nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    presets.writeTo (root);
    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, dest);
}

void DSCountProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr)
        return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("DSCOUNT"))
        return;
    uiWidth = (int) root.getProperty ("uiWidth", 1296);
    auto state = root.getChildWithName (apvts.state.getType());
    const bool v1 = root.getProperty ("version").toString().startsWith ("1.");
    if (v1)
    {
        // DS-COUNT 1.x used unprefixed IDs: start from defaults and map them.
        for (auto* p : getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                r->setValueNotifyingHost (r->getDefaultValue());
        for (auto child : state)
        {
            const auto id = ParameterManager::migrateV1Id (child.getProperty ("id").toString());
            if (auto* r = id.isNotEmpty() ? apvts.getParameter (id) : nullptr)
                r->setValueNotifyingHost (r->convertTo0to1 ((float) (double) child.getProperty ("value")));
        }
        presets.readFrom (juce::ValueTree ("EMPTY"));
    }
    else
    {
        if (state.isValid())
            apvts.replaceState (state);
        presets.readFrom (root);
    }
    undo.clearUndoHistory();
    triggerAsyncUpdate();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DSCountProcessor(); }
