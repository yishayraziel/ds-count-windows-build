#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../DSP/Chain.h"

namespace dsq
{
namespace ids
{
// Prefix of the dynamics parameters (kept from 2.0 development so IDs stay stable).
inline constexpr const char* dynamics = "dy";

// Per-channel suffixes. ID is "<prefix>_ch1_<suffix>".
inline constexpr const char* threshold = "threshold", *ratio = "ratio", *knee = "knee", *makeup = "makeup",
    *autoMakeup = "automakeup", *attack = "attack", *relDelay = "reldelay", *relFast = "relfast",
    *average = "average", *relSlow = "relslow", *rmsDetect = "rmsdetect", *preview = "preview",
    *filterType = "filtertype", *frequency = "frequency", *bandwidth = "bandwidth", *parallel = "parallel";

// Module-wide suffixes. ID is "<prefix>_<suffix>".
inline constexpr const char* monitor = "monitor", *link = "link",
    *midSide = "midside", *ganged = "ganged", *selChannel = "selchannel", *limiter = "limiter", *limType = "limtype",
    *ceiling = "ceiling", *preLimiter = "prelimiter", *scSource = "scsource", *scFilter = "scfilter";

// Global.
inline constexpr const char* inGain = "in_gain", *outGain = "out_gain", *globalBypass = "bypass", *dither = "dither",
    *ditherType = "dithertype", *lowLatency = "lowlatency", *peakMeter = "peakmeter";
} // namespace ids

inline juce::String gid (const char* prefix, const char* suffix) { return juce::String (prefix) + "_" + suffix; }
inline juce::String cid (const char* prefix, int channel, const char* suffix) { return juce::String (prefix) + "_ch" + juce::String (channel + 1) + "_" + suffix; }

double ratioFromNormalised (double n) noexcept;
double normalisedFromRatio (double r) noexcept;
juce::String ratioText (double r);
juce::String timeText (double ms);

class ParameterManager
{
public:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    explicit ParameterManager (juce::AudioProcessorValueTreeState& state);

    /** Real-time safe snapshot of every DSP parameter. */
    ChainParams read() const noexcept;

    float raw (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    /** Parameters that describe the session, not the sound. Kept out of presets and A/B. */
    static bool isSessionParameter (const juce::String& id);

    /** Maps a DS-COUNT 1.x parameter ID (single dynamics module) to its 2.x ID, or returns {} if it has none. */
    static juce::String migrateV1Id (const juce::String& oldId);

private:
    static constexpr int kChannelParams = 16;
    struct ChannelPtrs
    {
        std::atomic<float>* p[kChannelParams] {};
    };
    struct EnginePtrs
    {
        ChannelPtrs ch[2];
        std::atomic<float>* monitor, *link, *midSide, *ganged, *selChannel, *limiter, *limType, *ceiling, *preLimiter, *scSource, *scFilter;
    };

    void readEngine (const EnginePtrs& e, EngineParams& out) const noexcept;

    juce::AudioProcessorValueTreeState& apvts;
    EnginePtrs dyn;
    std::atomic<float>* inGain, *outGain, *globalBypass, *dither, *ditherType, *peakMeter;
};
} // namespace dsq
