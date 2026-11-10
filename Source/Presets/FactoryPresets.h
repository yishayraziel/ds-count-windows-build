#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace dsq
{
struct PresetValue { const char* id; float value; };

struct FactoryPreset
{
    juce::String name;
    std::vector<PresetValue> values;   // applied over the defaults
};

const std::vector<FactoryPreset>& factoryPresets();
} // namespace dsq
