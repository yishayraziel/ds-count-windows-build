#pragma once
// Compact feed-forward compressor used inside the maximizer styles: detector (peak or RMS),
// the shared dual-release envelope, and the shared transfer curve. Gain only, no audio path.
#include "Envelope.h"
#include "TransferCurve.h"

namespace dsq
{
class CompStage
{
public:
    struct Settings
    {
        TransferParams curve;
        EnvelopeTimes times;
    };

    void prepare (double sampleRate, const Settings& s)
    {
        sr = sampleRate;
        settings = s;
        envelope.configure (s.times, sr);
        average.setTime (s.times.averageMs * 0.0005, sr);
        rms.setTime (std::max (s.times.rmsDetectMs, 0.01) * 0.001, sr);
        reset();
    }

    void reset() noexcept
    {
        envelope.reset();
        average.reset();
        rms.reset();
    }

    /** Scales the threshold (and therefore the amount of gain change). Real-time safe. */
    void setThreshold (double db) noexcept { settings.curve.thresholdDb = db; }

    /** @return gain in dB, including no make-up */
    double process (double detector) noexcept
    {
        const double level = settings.times.rmsDetectMs > 0.0 ? rms.process (detector) : std::abs (detector);
        average.process (detector);
        const double env = envelope.process (level, average.rms() * std::sqrt (2.0));
        return curveGainDb (gainToDb (env), settings.curve);
    }

    const Settings& getSettings() const noexcept { return settings; }

private:
    double sr = 48000.0;
    Settings settings;
    EnvelopeFollower envelope;
    RMSDetector average, rms;
};

inline CompStage::Settings compSettings (double slope, double knee, double attackMs, double fastMs, double slowMs,
                                         double averageMs, double rmsMs = 0.0, double holdMs = 0.0)
{
    CompStage::Settings s;
    s.curve.slope = slope;
    s.curve.knee = knee;
    s.times.attackMs = attackMs;
    s.times.releaseFastMs = fastMs;
    s.times.releaseSlowMs = slowMs;
    s.times.averageMs = averageMs;
    s.times.rmsDetectMs = rmsMs;
    s.times.releaseDelayMs = holdMs;
    return s;
}
} // namespace dsq
