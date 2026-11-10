#include "Engine.h"

namespace dsq
{
namespace
{
constexpr double kSqrtHalf = 0.70710678118654752440;
constexpr double kSineCalibration = 2.0; // power to squared peak of a sine
constexpr double kParamSmoothSeconds = 0.02;

bool sameTimes (const EnvelopeTimes& a, const EnvelopeTimes& b) noexcept
{
    return a.attackMs == b.attackMs && a.releaseDelayMs == b.releaseDelayMs && a.releaseFastMs == b.releaseFastMs
        && a.averageMs == b.averageMs && a.releaseSlowMs == b.releaseSlowMs && a.rmsDetectMs == b.rmsDetectMs;
}
} // namespace

void Engine::prepare (double sr, bool lowLatency, int oversamplingOverride)
{
    sampleRate = sr;
    lowLatencyMode = lowLatency;

    const auto config = LinearPhaseCrossover::configFor (sampleRate, lowLatency);
    previewMaxSamples = (int) std::ceil ((lowLatency ? kPreviewMaxMsLowLatency : kPreviewMaxMs) * 0.001 * sampleRate);

    auto osConfig = Oversampler::configFor (sampleRate, lowLatency);
    if (oversamplingOverride == 1)
        osConfig = { 1, 0 };
    osFactor = osConfig.factor;
    osLatency = osConfig.tapsPerPhase;

    limiter.prepare (sampleRate, lowLatency);
    shapes.prepare (sampleRate);

    int seed = 1;
    for (auto& c : channels)
    {
        c.crossover.prepare (sampleRate, config);
        c.scCrossover.prepare (sampleRate, config);
        crossoverLatency = c.crossover.latency();
        totalLatency = crossoverLatency + previewMaxSamples + osLatency + limiter.latency();

        c.crossoverDelay.prepare (crossoverLatency + 2);
        c.scCrossoverDelay.prepare (crossoverLatency + 2);
        c.bandDelay.prepare (previewMaxSamples + osLatency + 2);
        c.dryDelay.prepare (previewMaxSamples + osLatency + 2);
        c.osBandDelay.prepare ((previewMaxSamples + 2) * osFactor);
        c.osSidechainDelay.prepare ((previewMaxSamples + 2) * osFactor);
        c.lookahead.prepare ((previewMaxSamples + 2) * osFactor);
        c.oversampler.prepare (osConfig);
        c.scOversampler.prepare (osConfig);
        c.bypassDelay.prepare (totalLatency + 2);

        for (auto* s : { &c.threshold, &c.slope, &c.knee, &c.makeup, &c.mix })
            s->prepare (sampleRate, kParamSmoothSeconds);

        c.inTp.prepare (4);
        c.outTp.prepare (4);
        c.dither.reset ((uint64_t) seed++);
    }

    for (auto* s : { &inputGain, &outputGain, &preLimiterGain })
    {
        s->prepare (sampleRate, kParamSmoothSeconds);
        s->snap (1.0);
    }
    bypassMix.prepare (sampleRate, 0.003);
    bypassMix.snap (0.0);
    limiterMix.prepare (sampleRate, 0.003);
    limiterMix.snap (0.0);

    rmsCoef = onePoleCoef (0.3, sampleRate);
    fadeStep = 1.0 / std::max (1.0, 0.005 * sampleRate);
    flushSamples = crossoverLatency + previewMaxSamples + osLatency + 8;
    columnPeriod = std::max (1, (int) (sampleRate / 90.0));

    reset();
}

void Engine::reset()
{
    for (auto& c : channels)
    {
        c.crossover.reset();
        c.scCrossover.reset();
        for (auto* d : { &c.crossoverDelay, &c.scCrossoverDelay, &c.bandDelay, &c.dryDelay, &c.osBandDelay, &c.osSidechainDelay, &c.bypassDelay })
            d->reset();
        c.oversampler.reset();
        c.scOversampler.reset();
        c.detect.reset();
        c.average.reset();
        c.envelope.reset();
        c.inTp.reset();
        c.outTp.reset();
        c.lookahead.reset();
        c.level = c.averageRef = c.band = c.dry = c.inRmsPower = c.outRmsPower = c.curveDb = c.makeupDb = c.wet = 0.0;
        c.overRun = 0;
    }
    int seed = 1;
    for (auto& c : channels)
        c.dither.reset ((uint64_t) seed++);
    limiter.reset();
    fade = FadeState::idle;
    fadeGain = 1.0;
    column = {};
    columnCount = 0;
    activityCount = 0;
}

void Engine::applyChannelSettings (Channel& c, const ChannelSettings& s, bool immediate) noexcept
{
    const double makeup = effectiveMakeupDb (s.curve);

    if (immediate)
    {
        c.threshold.snap (s.curve.thresholdDb);
        c.slope.snap (s.curve.slope);
        c.knee.snap (s.curve.knee);
        c.makeup.snap (makeup);
        c.mix.snap (s.mix);
        c.crossover.setSpecImmediately (s.band);
        c.scCrossover.setSpecImmediately (s.band);
    }
    else
    {
        c.threshold.setTarget (s.curve.thresholdDb);
        c.slope.setTarget (s.curve.slope);
        c.knee.setTarget (s.curve.knee);
        c.makeup.setTarget (makeup);
        c.mix.setTarget (s.mix);
        c.crossover.setSpec (s.band);
        c.scCrossover.setSpec (s.band);
    }

    if (immediate || ! sameTimes (c.times, s.times))
    {
        c.times = s.times;
        const double detectorRate = sampleRate * osFactor;
        c.envelope.configure (s.times, detectorRate);
        c.average.setTimes (s.times.averageMs * 0.002, s.times.averageMs * 0.00035, detectorRate);
        c.rmsMode = s.times.rmsDetectMs > 0.0;
        c.detect.setTime (s.times.rmsDetectMs * 0.0004, detectorRate);   // measured: power time constant 0.4 x RMS
    }

    c.previewTap = previewMaxSamples
                 - std::clamp ((int) std::lround (s.previewMs * 0.001 * sampleRate), 0, previewMaxSamples);
    c.lookWindow = (previewMaxSamples - c.previewTap) * osFactor + 1;
}

bool Engine::modeDiffers (const EngineParams& a, const EngineParams& b) const noexcept
{
    return a.midSide != b.midSide || a.ch[0].parallel != b.ch[0].parallel || a.ch[1].parallel != b.ch[1].parallel || a.monitor != b.monitor
        || a.ganged != b.ganged || (a.monitor && a.selectedChannel != b.selectedChannel);
}

void Engine::setParams (const EngineParams& p) noexcept
{
    pending = p;
    if (fade == FadeState::idle && modeDiffers (params, p))
        fade = FadeState::out;

    if (fade == FadeState::idle)
    {
        params = p;
    }
    else
    {
        // Mode switches take effect when the fade reaches silence. Everything else applies now.
        const auto keep = params;
        params = p;
        params.midSide = keep.midSide;
        params.ch[0].parallel = keep.ch[0].parallel;
        params.ch[1].parallel = keep.ch[1].parallel;
        params.monitor = keep.monitor;
        params.ganged = keep.ganged;
        params.selectedChannel = keep.selectedChannel;
    }

    applyChannelSettings (channels[0], p.ch[0], false);
    applyChannelSettings (channels[1], params.ganged ? p.ch[0] : p.ch[1], false);

    inputGain.setTarget (dbToGain (p.inputDb));
    outputGain.setTarget (dbToGain (p.outputDb));
    preLimiterGain.setTarget (dbToGain (p.preLimiterDb));
    bypassMix.setTarget (p.bypass ? 1.0 : 0.0);
    limiterMix.setTarget (p.limiterOn ? 1.0 : 0.0);
    limiter.setType (p.limiterType);
    limiter.setCeilingDb (p.ceilingDb);
}

void Engine::setParamsImmediately (const EngineParams& p) noexcept
{
    params = pending = p;
    fade = FadeState::idle;
    fadeGain = 1.0;
    encodeMidSide = decodeMidSide = p.midSide;

    applyChannelSettings (channels[0], p.ch[0], true);
    applyChannelSettings (channels[1], p.ganged ? p.ch[0] : p.ch[1], true);

    inputGain.snap (dbToGain (p.inputDb));
    outputGain.snap (dbToGain (p.outputDb));
    preLimiterGain.snap (dbToGain (p.preLimiterDb));
    bypassMix.snap (p.bypass ? 1.0 : 0.0);
    limiterMix.snap (p.limiterOn ? 1.0 : 0.0);
    limiter.setType (p.limiterType);
    limiter.snapCeilingDb (p.ceilingDb);
    limiter.reset();
}

void Engine::processSample (double* io, const double* sc, bool hasSidechain, int numChannels) noexcept
{
    const bool stereo = numChannels > 1;

    // ---- input ------------------------------------------------------------------------
    for (int c = 0; c < numChannels; ++c)
    {
        if (! std::isfinite (io[c]))
            io[c] = 0.0;
        channels[c].bypassDelay.push (io[c]);
    }

    const double gIn = inputGain.next();
    double x[2] = { io[0] * gIn, stereo ? io[1] * gIn : 0.0 };

    for (int c = 0; c < numChannels; ++c)
    {
        auto& ch = channels[c];
        const double peak = params.truePeakMeters ? ch.inTp.process (x[c]) : std::abs (x[c]);
        MeteringEngine::raise (meters.channels[(size_t) c].inPeak, (float) peak);
        ch.inRmsPower += (x[c] * x[c] - ch.inRmsPower) * rmsCoef + kDenormalGuard;
        column.peak = std::max (column.peak, (float) std::abs (x[c]));
    }

    double side[2] = { sc[0], sc[1] };
    if (encodeMidSide && stereo)
    {
        const double m = (x[0] + x[1]) * kSqrtHalf, s = (x[0] - x[1]) * kSqrtHalf;
        x[0] = m;
        x[1] = s;
        const double sm = (side[0] + side[1]) * kSqrtHalf, ss = (side[0] - side[1]) * kSqrtHalf;
        side[0] = sm;
        side[1] = ss;
    }

    // ---- band split and detector ------------------------------------------------------
    const bool external = params.externalSidechain && hasSidechain;

    for (int c = 0; c < numChannels; ++c)
    {
        auto& ch = channels[c];
        const double filtered = ch.crossover.process (x[c]);
        ch.crossoverDelay.push (x[c]);
        const double dry = ch.crossoverDelay.read (crossoverLatency);
        const bool transparent = ch.crossover.isTransparent();
        const double band = transparent ? dry : filtered;

        ch.bandDelay.push (band);
        ch.dryDelay.push (dry);
        ch.band = ch.bandDelay.read (previewMaxSamples + osLatency);
        ch.dry = ch.dryDelay.read (previewMaxSamples + osLatency);

        double oversampled[8];
        ch.oversampler.up (band, oversampled);
        for (int p = 0; p < osFactor; ++p)
            ch.osBandDelay.push (oversampled[p]);

        if (external)
        {
            const double scFiltered = ch.scCrossover.process (side[c]);
            ch.scCrossoverDelay.push (side[c]);
            const double scDry = ch.scCrossoverDelay.read (crossoverLatency);
            ch.scOversampler.up (params.sidechainFilter && ! ch.scCrossover.isTransparent() ? scFiltered : scDry, oversampled);
            for (int p = 0; p < osFactor; ++p)
                ch.osSidechainDelay.push (oversampled[p]);
        }

        ch.live.thresholdDb = ch.threshold.next();
        ch.live.slope = ch.slope.next();
        ch.live.knee = ch.knee.next();
        ch.makeupDb = ch.makeup.next();
        ch.curveDb = 0.0;
    }

    // ---- oversampled detector, envelope, transfer curve, gain --------------------------
    for (int p = 0; p < osFactor; ++p)
    {
        const int age = osFactor - 1 - p;   // oldest of this host sample's group first

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[c];
            const auto& line = external ? ch.osSidechainDelay : ch.osBandDelay;
            const double detector = line.read (ch.previewTap * osFactor + age);
            const double ahead = ch.rmsMode ? ch.detect.process (detector) : PeakDetector::level (detector);
            ch.level = ch.lookahead.process (ahead, ch.lookWindow);
            ch.average.process (detector);
            ch.averageRef = ch.average.rms() * std::sqrt (kSineCalibration);
        }

        if (params.link && stereo)
        {
            const double level = std::max (channels[0].level, channels[1].level);
            const double ref = std::max (channels[0].averageRef, channels[1].averageRef);
            channels[0].level = channels[1].level = level;
            channels[0].averageRef = channels[1].averageRef = ref;
        }

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[c];
            const double env = ch.envelope.process (ch.level, ch.averageRef);
            const double curveDb = curveGainDb (gainToDb (env), ch.live);
            const double totalDb = clampTotalGainDb (curveDb + ch.makeupDb);
            const double g = totalDb == 0.0 ? 1.0 : dbToGain (totalDb);

            // Only band * (g - 1) is filtered, so g == 1 leaves the output bit-exact.
            ch.oversampler.pushDown (ch.osBandDelay.read (previewMaxSamples * osFactor + age) * (g - 1.0));
            if (p == 0)
                ch.wet = ch.oversampler.down();
            ch.curveDb = std::min (ch.curveDb, curveDb);

            if (ch.envelope.isReleasing() && curveDb < -0.05)
            {
                const double w = ch.envelope.fastWeightNow();
                ch.fastActivity += w;
                ch.slowActivity += 1.0 - w;
            }
        }
    }

    double y[2] = { 0.0, 0.0 };
    double monitored[2] = { 0.0, 0.0 };

    for (int c = 0; c < numChannels; ++c)
    {
        auto& ch = channels[c];
        const double m = ch.mix.next();
        y[c] = channelIsParallel (c) ? ch.dry + ch.band + ch.wet : ch.dry + (m == 1.0 ? ch.wet : ch.wet * m);
        monitored[c] = ch.crossover.isTransparent() ? y[c] : ch.band + ch.wet;

        MeteringEngine::lower (meters.channels[(size_t) c].compGrDb, (float) ch.curveDb);
        meters.channels[(size_t) c].envelopeDb.store ((float) gainToDb (ch.envelope.value()), std::memory_order_relaxed);
        column.grDb = std::min (column.grDb, (float) ch.curveDb);
    }

    // ---- monitor and channel decode ---------------------------------------------------
    if (params.monitor)
    {
        if (params.ganged)
        {
            y[0] = monitored[0];
            y[1] = monitored[1];
        }
        else
        {
            y[0] = y[1] = monitored[std::clamp (params.selectedChannel, 0, numChannels - 1)];
        }
    }

    const bool soloChannel = params.monitor && ! params.ganged;
    if (decodeMidSide && stereo && ! soloChannel)
    {
        const double l = (y[0] + y[1]) * kSqrtHalf, r = (y[0] - y[1]) * kSqrtHalf;
        y[0] = l;
        y[1] = r;
    }

    // ---- click-free mode switching ----------------------------------------------------
    if (fade != FadeState::idle)
    {
        if (fade == FadeState::out)
        {
            fadeGain -= fadeStep;
            if (fadeGain <= 0.0)
            {
                fadeGain = 0.0;
                const bool msChanged = pending.midSide != params.midSide;
                params.midSide = pending.midSide;
                params.ch[0].parallel = pending.ch[0].parallel;
                params.ch[1].parallel = pending.ch[1].parallel;
                params.monitor = pending.monitor;
                params.ganged = pending.ganged;
                params.selectedChannel = pending.selectedChannel;
                applyChannelSettings (channels[1], params.ganged ? pending.ch[0] : pending.ch[1], false);
                encodeMidSide = params.midSide;
                if (msChanged)
                {
                    // Audio already inside the delay lines is in the old domain.
                    flushCounter = flushSamples;
                    fade = FadeState::flush;
                }
                else
                {
                    fade = FadeState::in;
                }
            }
        }
        else if (fade == FadeState::flush)
        {
            if (--flushCounter <= 0)
            {
                decodeMidSide = params.midSide;
                fade = FadeState::in;
            }
        }
        else
        {
            fadeGain += fadeStep;
            if (fadeGain >= 1.0)
            {
                fadeGain = 1.0;
                fade = FadeState::idle;
            }
        }
        const double g = fadeGain * fadeGain * (3.0 - 2.0 * fadeGain);
        y[0] *= g;
        y[1] *= g;
    }

    // ---- limiter, output, dither ------------------------------------------------------
    const double gPre = preLimiterGain.next();
    y[0] *= gPre;
    y[1] *= gPre;

    const double mix = limiterMix.next();
    limiter.process (y[0], y[1], numChannels, mix);
    if (mix > 0.0)
        MeteringEngine::lower (meters.limGrDb, (float) gainToDb (limiter.gain()));

    const double gOut = outputGain.next();

    for (int c = 0; c < numChannels; ++c)
    {
        auto& ch = channels[c];
        double out = y[c] * gOut;

        const double peak = params.truePeakMeters ? ch.outTp.process (out) : std::abs (out);
        auto& m = meters.channels[(size_t) c];
        MeteringEngine::raise (m.outPeak, (float) peak);
        ch.outRmsPower += (out * out - ch.outRmsPower) * rmsCoef + kDenormalGuard;

        ch.overRun = std::abs (out) > 1.0 ? ch.overRun + 1 : 0;
        if (ch.overRun == params.oversCount)
            m.overs.fetch_add (1, std::memory_order_relaxed);

        if (params.ditherBits > 0)
            out = params.ditherShape > 0 ? ch.dither.processShaped (out, params.ditherBits, shapes.get (params.ditherShape))
                                         : ch.dither.process (out, params.ditherBits);

        const double b = bypassMix.current();
        const double delayedInput = ch.bypassDelay.read (totalLatency);
        io[c] = b <= 0.0 ? out : b >= 1.0 ? delayedInput : out + (delayedInput - out) * b;
    }
    bypassMix.next();

    activityCount += osFactor;

    if (++columnCount >= columnPeriod)
        measureBlock();
}

void Engine::measureBlock() noexcept
{
    column.rms = (float) std::sqrt (std::max (channels[0].inRmsPower, channels[1].inRmsPower));
    meters.pushColumn (column);
    column = {};
    columnCount = 0;

    const int counted = activityCount;
    for (int c = 0; c < 2; ++c)
    {
        auto& m = meters.channels[(size_t) c];
        m.inRms.store ((float) std::sqrt (channels[c].inRmsPower), std::memory_order_relaxed);
        m.outRms.store ((float) std::sqrt (channels[c].outRmsPower), std::memory_order_relaxed);
        m.releaseFast.store ((float) (channels[c].fastActivity / std::max (1, counted)), std::memory_order_relaxed);
        m.releaseSlow.store ((float) (channels[c].slowActivity / std::max (1, counted)), std::memory_order_relaxed);
        channels[c].fastActivity = channels[c].slowActivity = 0.0;
    }
    activityCount = 0;
}
} // namespace dsq
