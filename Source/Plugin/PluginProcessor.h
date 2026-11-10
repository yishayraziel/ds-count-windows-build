#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../DSP/Chain.h"
#include "../Parameters/ParameterManager.h"
#include "../Presets/PresetManager.h"

class DSCountProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    DSCountProcessor();
    ~DSCountProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout&) const override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    /** Applies a pending Low Latency change now. Message thread. Used by tests. */
    void applyStructureNow() { handleAsyncUpdate(); }

    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;
    dsq::ParameterManager params;
    dsq::PresetManager presets;
    dsq::Chain chain;

    int uiWidth = 1296;

private:
    template <typename T> void process (juce::AudioBuffer<T>&);
    void handleAsyncUpdate() override;

    std::atomic<bool> prepared { false };
    int preparedBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DSCountProcessor)
};
