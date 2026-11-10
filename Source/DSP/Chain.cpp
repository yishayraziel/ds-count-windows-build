#include "Chain.h"

namespace dsq
{
void Chain::prepare (double sr, bool lowLat)
{
    sampleRate = sr;
    lowLatency = lowLat;

    dynamics.prepare (sr, lowLat);
    shapes.prepare (sr);

    for (int c = 0; c < 2; ++c)
    {
        bypassDelay[c].prepare (dynamics.latencySamples() + 4);
        dither[c].reset ((uint64_t) c + 11);
        truePeak[c].prepare (4);
    }

    inputGain.prepare (sr, 0.02);
    outputGain.prepare (sr, 0.02);
    globalBypass.prepare (sr, 0.01);
    rmsCoef = onePoleCoef (0.3, sr);
    meters.loudness.prepare (sr);

    reset();
    setParamsImmediately (params);
}

void Chain::reset()
{
    dynamics.reset();
    for (int c = 0; c < 2; ++c)
    {
        bypassDelay[c].reset();
        truePeak[c].reset();
        dither[c].reset ((uint64_t) c + 11);
        inRms[c] = outRms[c] = 0.0;
    }
    meters.loudness.reset();
}

static EngineParams neutralised (EngineParams p) noexcept
{
    // Gain, bypass and dither are the chain's job.
    p.inputDb = p.outputDb = 0.0;
    p.bypass = false;
    p.ditherBits = 0;
    return p;
}

void Chain::setParams (const ChainParams& p) noexcept
{
    params = p;
    dynamics.setParams (neutralised (p.dynamics));
    inputGain.setTarget (dbToGain (p.inputDb));
    outputGain.setTarget (dbToGain (p.outputDb));
    globalBypass.setTarget (p.bypass ? 1.0 : 0.0);
}

void Chain::setParamsImmediately (const ChainParams& p) noexcept
{
    params = p;
    dynamics.setParamsImmediately (neutralised (p.dynamics));
    inputGain.snap (dbToGain (p.inputDb));
    outputGain.snap (dbToGain (p.outputDb));
    globalBypass.snap (p.bypass ? 1.0 : 0.0);
}

void Chain::processBlock (int numChannels, bool hasSidechain, int n) noexcept
{
    numChannels = std::clamp (numChannels, 1, 2);
    const bool stereo = numChannels > 1;
    const int latency = dynamics.latencySamples();

    // Bypass reference, before any processing.
    double bypassed[2][kBlock];
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < n; ++i)
        {
            if (! std::isfinite (buffer[c][i]))
                buffer[c][i] = 0.0;
            bypassDelay[c].push (buffer[c][i]);
            bypassed[c][i] = bypassDelay[c].read (latency);
        }

    float inPk[2] = { 0, 0 };
    for (int i = 0; i < n; ++i)
    {
        const double g = inputGain.next();
        for (int c = 0; c < numChannels; ++c)
        {
            buffer[c][i] *= g;
            inPk[c] = std::max (inPk[c], (float) std::abs (buffer[c][i]));
            inRms[c] += (buffer[c][i] * buffer[c][i] - inRms[c]) * rmsCoef + kDenormalGuard;
        }
    }

    {
        double* io[2] = { buffer[0], buffer[1] };
        const double* ioIn[2] = { buffer[0], buffer[1] };
        const double* scPtr[2] = { sc[0], sc[1] };
        dynamics.process<double> (ioIn, io, hasSidechain ? scPtr : nullptr, numChannels, hasSidechain ? 2 : 0, n);
    }

    float outPk[2] = { 0, 0 }, tp = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const double g = outputGain.next();
        const double b = globalBypass.next();
        for (int c = 0; c < numChannels; ++c)
        {
            double y = buffer[c][i] * g;
            outRms[c] += (y * y - outRms[c]) * rmsCoef + kDenormalGuard;
            const float pk = (float) (params.truePeakMeters ? truePeak[c].process (y) : std::abs (y));
            outPk[c] = std::max (outPk[c], pk);
            tp = std::max (tp, pk);
            buffer[c][i] = y;
        }
        meters.loudness.process (buffer[0][i], stereo ? buffer[1][i] : 0.0, numChannels);

        for (int c = 0; c < numChannels; ++c)
        {
            double y = buffer[c][i];
            if (params.ditherBits > 0)
                y = params.ditherShape > 0 ? dither[c].processShaped (y, params.ditherBits, shapes.get (params.ditherShape))
                                           : dither[c].process (y, params.ditherBits);
            buffer[c][i] = b <= 0.0 ? y : b >= 1.0 ? bypassed[c][i] : y + (bypassed[c][i] - y) * b;
        }
    }

    for (int c = 0; c < numChannels; ++c)
    {
        MeteringEngine::raise (meters.inPeak[c], inPk[c]);
        MeteringEngine::raise (meters.outPeak[c], outPk[c]);
        meters.inRms[c].store ((float) std::sqrt (inRms[c]), std::memory_order_relaxed);
        meters.outRms[c].store ((float) std::sqrt (outRms[c]), std::memory_order_relaxed);
    }
    MeteringEngine::raise (meters.truePeakMax, tp);
}
} // namespace dsq
