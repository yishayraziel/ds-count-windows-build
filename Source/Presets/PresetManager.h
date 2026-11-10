#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace dsq
{
/** Factory and user presets, A/B workspaces and channel copy. Message thread only. */
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    juce::StringArray factoryNames() const;
    juce::StringArray userNames() const;
    juce::File userFolder() const;

    void loadInit();
    void loadFactory (int index);
    bool loadUser (const juce::String& name);
    bool saveUser (const juce::String& name);
    bool renameUser (const juce::String& oldName, const juce::String& newName);
    bool deleteUser (const juce::String& name);
    void step (int direction);

    juce::String currentName() const { return current; }
    bool currentIsUser() const { return isUser; }

    int activeSlot() const { return slot; }
    void selectSlot (int newSlot);
    void copyActiveToOther();
    void swapSlots();

    /** Copies all per-channel settings of a dynamics module ("dy" or "cl") from one channel to the other. */
    void copyChannel (const char* modulePrefix, int from, int to);

    void writeTo (juce::ValueTree& parent) const;
    void readFrom (const juce::ValueTree& parent);

private:
    void applyDefaults();
    void setPlain (const juce::String& id, float value);
    juce::Array<juce::RangedAudioParameter*> soundParameters() const;
    void restoreSound (const juce::ValueTree& snapshot);
    juce::ValueTree soundSnapshot() const;

    juce::AudioProcessorValueTreeState& apvts;
    juce::String current { "Init" };
    bool isUser = false;
    int slot = 0;
    juce::ValueTree other;
    juce::String otherName { "Init" };
    bool otherIsUser = false;
};
} // namespace dsq
