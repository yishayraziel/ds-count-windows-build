#pragma once
// Complete signal path. JUCE-free so that every DSP test runs without a host.
#include "Dither.h"
#include "Envelope.h"
#include "Limiter.h"
#include "Metering.h"
#include "Oversampler.h"
#include "TransferCurve.h"

namespace dsq
{
struct ChannelSettings
{
    TransferParams curve;
    EnvelopeTimes times;
    double previewMs = 0.3;
    BandSpec band;
    bool parallel = false;
    double mix = 1.0;   // dry/wet of the gain change, 1 = fully processed
};

struct EngineParams
{
    ChannelSettings ch[2];
    double inputDb = 0.0, outputDb = 0.0, ceilingDb = -0.1, preLimiterDb = 0.0;
    bool bypass = false, monitor = false, link = true, midSide = false, ganged = true;
    bool limiterOn = false, externalSidechain = false, sidechainFilter = false, truePeakMeters = false;
    int selectedChannel = 0, ditherBits = 0, ditherShape = 0, oversCount = 3;   // ditherShape 0 = flat TPDF
    LimiterType limiterType = LimiterType::safe;
};

class Engine
{
public:
    static constexpr double kPreviewMaxMs = 63.0, kPreviewMaxMsLowLatency = 1.0;

    /** @param oversamplingOverride 0 selects the normal factor for the rate. Tests use 1 to compare. */
    void prepare (double sampleRate, bool lowLatency, int oversamplingOverride = 0);
    void reset();

    /** Call once per host block before process(). Real-time safe. */
    void setParams (const EngineParams& p) noexcept;

    /** Applies parameters with no smoothing or crossfades. Call after prepare(), not during playback. */
    void setParamsImmediately (const EngineParams& p) noexcept;

    int latencySamples() const noexcept { return totalLatency; }
    int oversamplingFactor() const noexcept { return channels[0].oversampler.getFactor(); }
    bool isLowLatency() const noexcept { return lowLatencyMode; }
    double getSampleRate() const noexcept { return sampleRate; }

    /** @param sidechain may be nullptr. Inputs and outputs may alias. */
    template <typename T>
    void process (const T* const* inputs, T* const* outputs, const T* const* sidechain,
                  int numChannels, int numSidechainChannels, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
        {
            double in[2] = { (double) inputs[0][n], numChannels > 1 ? (double) inputs[1][n] : 0.0 };
            double sc[2] = { 0.0, 0.0 };
            if (sidechain != nullptr && numSidechainChannels > 0)
            {
                sc[0] = (double) sidechain[0][n];
                sc[1] = numSidechainChannels > 1 ? (double) sidechain[1][n] : sc[0];
            }
            processSample (in, sc, sidechain != nullptr && numSidechainChannels > 0, numChannels);
            outputs[0][n] = (T) in[0];
            if (numChannels > 1)
                outputs[1][n] = (T) in[1];
        }
    }

    MeteringEngine meters;

private:
    struct Channel
    {
        LinearPhaseCrossover crossover, scCrossover;
        DelayLine crossoverDelay, scCrossoverDelay, bandDelay, dryDelay, osBandDelay, osSidechainDelay, bypassDelay;
        SlidingMax lookahead;
        int lookWindow = 1;
        Oversampler oversampler, scOversampler;
        RMSDetector detect;
        AsymmetricAverage average;
        EnvelopeFollower envelope;
        Smoother threshold, slope, knee, makeup, mix;
        EnvelopeTimes times;
        int previewTap = 0;
        bool rmsMode = false;
        TruePeak inTp, outTp;
        double level = 0, averageRef = 0, band = 0, dry = 0, inRmsPower = 0, outRmsPower = 0, curveDb = 0, makeupDb = 0, wet = 0;
        TransferParams live;
        int overRun = 0;
        Dither dither;
        double fastActivity = 0, slowActivity = 0;
    };

    enum class FadeState { idle, out, flush, in };

    void processSample (double* io, const double* sc, bool hasSidechain, int numChannels) noexcept;
    void applyChannelSettings (Channel& c, const ChannelSettings& s, bool immediate) noexcept;
    bool modeDiffers (const EngineParams& a, const EngineParams& b) const noexcept;
    void measureBlock() noexcept;
    bool channelIsParallel (int c) const noexcept { return params.ch[params.ganged ? 0 : c].parallel; }

    double sampleRate = 48000.0, rmsCoef = 0.0;
    bool lowLatencyMode = false;
    int crossoverLatency = 0, previewMaxSamples = 0, totalLatency = 0, osFactor = 1, osLatency = 0;
    int flushSamples = 0, flushCounter = 0, columnPeriod = 512, columnCount = 0;
    EngineParams params, pending;
    bool encodeMidSide = false, decodeMidSide = false;
    FadeState fade = FadeState::idle;
    double fadeGain = 1.0, fadeStep = 0.0;
    int activityCount = 0;

    Channel channels[2];
    Limiter limiter;
    NoiseShaper shapes;
    Smoother inputGain, outputGain, preLimiterGain, bypassMix, limiterMix;
    WaveColumn column;
};
} // namespace dsq
