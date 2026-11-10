#pragma once
// The DS-COUNT signal path, JUCE-free:
//
//   Input gain -> Precision Dynamics -> Output gain -> Dither -> Global bypass
//
// The global bypass keeps the latency and crossfades to a delayed copy of the input, so it can be
// automated without moving audio in time.
#include "Engine.h"
#include "Loudness.h"
#include "NoiseShaper.h"
#include "TruePeak.h"

namespace dsq
{
struct ChainParams
{
    EngineParams dynamics;
    double inputDb = 0.0, outputDb = 0.0;
    bool bypass = false, truePeakMeters = true;
    int ditherBits = 0, ditherShape = 0;
};

class Chain
{
public:
    static constexpr int kBlock = 256;

    struct Meters
    {
        std::atomic<float> inPeak[2] { 0, 0 }, inRms[2] { 0, 0 }, outPeak[2] { 0, 0 }, outRms[2] { 0, 0 };
        std::atomic<float> truePeakMax { 0.0f };
        LoudnessMeter loudness;
    };

    void prepare (double sampleRate, bool lowLatency);
    void reset();

    void setParams (const ChainParams& p) noexcept;
    void setParamsImmediately (const ChainParams& p) noexcept;

    int latencySamples() const noexcept { return dynamics.latencySamples(); }
    bool isLowLatency() const noexcept { return lowLatency; }
    double getSampleRate() const noexcept { return sampleRate; }

    template <typename T>
    void process (const T* const* in, T* const* out, const T* const* sidechain, int numChannels, int numSidechain, int numSamples) noexcept
    {
        for (int pos = 0; pos < numSamples; pos += kBlock)
        {
            const int n = std::min (kBlock, numSamples - pos);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    buffer[c][i] = c < numChannels ? (double) in[c][pos + i] : 0.0;
                    sc[c][i] = sidechain != nullptr && numSidechain > 0 ? (double) sidechain[std::min (c, numSidechain - 1)][pos + i] : 0.0;
                }
            processBlock (numChannels, sidechain != nullptr && numSidechain > 0, n);
            for (int c = 0; c < numChannels; ++c)
                for (int i = 0; i < n; ++i)
                    out[c][pos + i] = (T) buffer[c][i];
        }
    }

    Engine dynamics;
    Meters meters;

private:
    void processBlock (int numChannels, bool hasSidechain, int n) noexcept;

    double sampleRate = 48000.0;
    bool lowLatency = false;
    ChainParams params;

    DelayLine bypassDelay[2];
    Smoother inputGain, outputGain, globalBypass;
    Dither dither[2];
    NoiseShaper shapes;
    TruePeak truePeak[2];
    double inRms[2] {}, outRms[2] {}, rmsCoef = 0.0;

    double buffer[2][kBlock] {}, sc[2][kBlock] {};
};
} // namespace dsq
