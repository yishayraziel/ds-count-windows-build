#pragma once
// The static gain computer. This single function drives both the audio path and the
// on-screen transfer graph, so the display cannot disagree with the DSP.
#include "Common.h"

namespace dsq
{
struct TransferParams
{
    double thresholdDb = -12.0;
    double slope = 0.5;   // output dB per input dB above threshold, 1/R. Above 1 means upward expansion.
    double knee = 0.5;    // 0..1. Half-width of the knee = 1.3 * knee * |threshold| dB.
    double makeupDb = 0.0;
    bool autoMakeup = false;
};

/** Gain in dB from the curve, excluding make-up. Continuous and C1 for every parameter set. */
/** Knee half-width per unit of Soft Knee and per dB of threshold:
    a 100 % knee at -20 dB spans -46 to +6 dB, a 50 % knee -33 to -7 dB, both quadratic. */
constexpr double kKneeSpan = 1.3;

inline double curveGainDb (double inputDb, const TransferParams& p) noexcept
{
    const double halfWidth = kKneeSpan * p.knee * std::abs (p.thresholdDb);
    const double over = inputDb - p.thresholdDb;

    if (halfWidth > 1.0e-9)
    {
        if (over <= -halfWidth)
            return 0.0;
        if (over < halfWidth)
        {
            const double t = over + halfWidth;
            return (p.slope - 1.0) * t * t / (4.0 * halfWidth);
        }
    }
    else if (over <= 0.0)
    {
        return 0.0;
    }
    return (p.slope - 1.0) * over;
}

/** Documented "max" make-up: the make-up that returns a 0 dBFS input to exactly 0 dBFS. */
inline double maxMakeupDb (const TransferParams& p) noexcept { return -curveGainDb (0.0, p); }

inline double effectiveMakeupDb (const TransferParams& p) noexcept
{
    return p.autoMakeup ? maxMakeupDb (p) : p.makeupDb;
}

/** Documented knob rule for Max. Pure function of the knob, so it cannot oscillate during a drag:
    Max stays while the knob is at or above the max value, leaves when it is turned below it,
    and locks when the knob reaches it, where locking is allowed. */
inline bool nextAutoMakeup (bool autoOn, double knobDb, double maxDb, bool canLock) noexcept
{
    constexpr double hysteresisDb = 0.05;
    if (autoOn)
        return knobDb >= maxDb - hysteresisDb;
    return canLock && knobDb >= maxDb;
}

constexpr double kMaxTotalGainDb = 60.0;

inline double clampTotalGainDb (double db) noexcept { return std::clamp (db, kMinDb, kMaxTotalGainDb); }

/** Static input to output mapping in dB, exactly as the audio path computes it. */
inline double transferOutputDb (double inputDb, const TransferParams& p) noexcept
{
    return inputDb + clampTotalGainDb (curveGainDb (inputDb, p) + effectiveMakeupDb (p));
}
} // namespace dsq
