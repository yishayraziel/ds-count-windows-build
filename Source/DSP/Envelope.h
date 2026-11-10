#pragma once
// Level detection and the dual-release envelope.
//
// Independent algorithm, designed from the documented behaviour:
//   * the release time is selected by comparing the envelope against an RMS average
//   * short bursts barely charge the average, so they release on Release Fast alone
//   * long bursts charge it, so release is fast down to the average and slow below it
//   * a short Average collapses with the signal, so Release Slow has less influence
// The window between the two time constants is continuous, so there is no switching
// discontinuity.
//
// Time scales:
//   every time setting moves the detector level by two nepers (17.4 dB): time constant = setting / 2.
//   This holds for Attack and for Release Fast and Slow. Release is therefore a straight line in dB.
//   The Average reference charges slowly (power time constant 2 x Average) and discharges at
//   0.35 x Average, so a short burst never reaches it (pure Release Fast) and after a long burst the
//   release runs on Release Slow until the reference has fallen away, then follows it.
#include "Common.h"

namespace dsq
{
struct EnvelopeTimes
{
    double attackMs = 0.1, releaseDelayMs = 0.0, releaseFastMs = 10.0;
    double averageMs = 250.0, releaseSlowMs = 250.0, rmsDetectMs = 0.0; // 0 = Peak
};

/** Instantaneous peak level. */
struct PeakDetector
{
    static double level (double x) noexcept { return std::abs (x); }
};

/** Exponentially averaged power. Silence decays to exactly zero through the guard. */
class RMSDetector
{
public:
    void setTime (double seconds, double sampleRate) noexcept { coef = onePoleCoef (seconds, sampleRate); }
    void reset() noexcept { power = 0.0; }

    double process (double x) noexcept
    {
        power += (x * x - power) * coef;
        if (power < kDenormalGuard)
            power = 0.0;
        return std::sqrt (power);
    }

    double rms() const noexcept { return std::sqrt (power); }

private:
    double coef = 1.0, power = 0.0;
};

/** Power average that charges and discharges with different time constants. */
class AsymmetricAverage
{
public:
    void setTimes (double riseSeconds, double fallSeconds, double sampleRate) noexcept
    {
        rise = onePoleCoef (riseSeconds, sampleRate);
        fall = onePoleCoef (fallSeconds, sampleRate);
    }
    void reset() noexcept { power = 0.0; }
    void process (double x) noexcept
    {
        const double p = x * x;
        power += (p - power) * (p > power ? rise : fall);
        if (power < kDenormalGuard)
            power = 0.0;
    }
    double rms() const noexcept { return std::sqrt (power); }

private:
    double rise = 1.0, fall = 1.0, power = 0.0;
};

/** Selects the release coefficient from the envelope to average relationship. */
class ReleaseController
{
public:
    // Release Fast acts while the envelope is more than kOffsetDb above the average reference
    // (crossfaded over +-kWindowDb); below that Release Slow acts. Tuned on 50 ms, 300 ms and 3 s bursts.
    static constexpr double kWindowDb = 0.5, kOffsetDb = 2.5;

    /** Weight of Release Fast in 0..1, continuous in both arguments. */
    static double fastWeight (double envelope, double averageRef) noexcept
    {
        if (averageRef <= 1.0e-12)
            return 1.0;
        if (envelope <= 0.0)
            return 0.0;
        const double db = 20.0 * std::log10 (envelope / averageRef);
        const double t = std::clamp ((db - kOffsetDb + kWindowDb) / (2.0 * kWindowDb), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }
};

/** Attack, hold and adaptive release acting on linear level. */
class EnvelopeFollower
{
public:
    static constexpr double kLn10 = 2.30258509299404568402;
    static constexpr double kTimeScale = 0.5;   // time constant per unit of setting

    void configure (const EnvelopeTimes& t, double sampleRate) noexcept
    {
        attackCoef = onePoleCoef (t.attackMs * 0.001 * kTimeScale, sampleRate);
        fastCoef = onePoleCoef (t.releaseFastMs * 0.001 * kTimeScale, sampleRate);
        slowCoef = onePoleCoef (t.releaseSlowMs * 0.001 * kTimeScale, sampleRate);
        holdSamples = (int) std::lround (t.releaseDelayMs * 0.001 * sampleRate);
        holdCounter = std::min (holdCounter, holdSamples);
    }

    void reset() noexcept { env = 0.0; holdCounter = 0; lastFastWeight = 1.0; }

    /** @param level detector level   @param averageRef sine-calibrated average reference */
    double process (double level, double averageRef) noexcept
    {
        releasing = false;

        if (level >= env)
        {
            env += (level - env) * attackCoef;
            holdCounter = holdSamples;
        }
        else if (holdCounter > 0)
        {
            --holdCounter;
        }
        else
        {
            releasing = true;
            lastFastWeight = ReleaseController::fastWeight (env, averageRef);
            env += (level - env) * (lastFastWeight * fastCoef + (1.0 - lastFastWeight) * slowCoef);
        }
        if (env < 1.0e-15)
            env = 0.0;
        return env;
    }

    double value() const noexcept { return env; }
    double fastWeightNow() const noexcept { return lastFastWeight; }
    bool isReleasing() const noexcept { return releasing; }

private:
    double attackCoef = 1.0, fastCoef = 1.0, slowCoef = 1.0, env = 0.0, lastFastWeight = 1.0;
    int holdSamples = 0, holdCounter = 0;
    bool releasing = false;
};
} // namespace dsq
