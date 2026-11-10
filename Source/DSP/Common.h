#pragma once
// JUCE-free DSP primitives shared by every module.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dsq
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kDenormalGuard = 1.0e-30;
constexpr double kMinDb = -200.0;

inline double dbToGain (double db) noexcept { return std::pow (10.0, db * 0.05); }

inline double gainToDb (double g) noexcept
{
    return g > 1.0e-10 ? 20.0 * std::log10 (g) : kMinDb;
}

/** Sample-rate correct one-pole coefficient: fraction of the distance covered per sample. */
inline double onePoleCoef (double timeSeconds, double sampleRate) noexcept
{
    const double samples = timeSeconds * sampleRate;
    if (samples <= 1.0e-9)
        return 1.0;
    return 1.0 - std::exp (-1.0 / samples);
}

/** One-pole parameter smoother. Fixed-time, sample-accurate, no allocation. */
class Smoother
{
public:
    void prepare (double sampleRate, double timeSeconds) noexcept { coef = onePoleCoef (timeSeconds, sampleRate); }
    void setTarget (double t) noexcept { target = t; }
    void snap (double v) noexcept { value = target = v; }
    double next() noexcept
    {
        value += (target - value) * coef;
        if (std::abs (target - value) < 1.0e-12)
            value = target;
        return value;
    }
    double current() const noexcept { return value; }
    bool settled() const noexcept { return value == target; }

private:
    double coef = 1.0, value = 0.0, target = 0.0;
};

/** Fixed-delay line. Storage is allocated in prepare(), never on the audio thread. */
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + 2)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0);
        mask = size - 1;
        writeIndex = 0;
    }

    void reset (double value = 0.0) noexcept { std::fill (buffer.begin(), buffer.end(), value); writeIndex = 0; }

    void push (double x) noexcept
    {
        buffer[(size_t) writeIndex] = x;
        writeIndex = (writeIndex + 1) & mask;
    }

    /** Sample written `delay` pushes ago, with delay 0 meaning the most recent sample. */
    double read (int delay) const noexcept { return buffer[(size_t) ((writeIndex - 1 - delay) & mask)]; }

private:
    std::vector<double> buffer;
    int mask = 0, writeIndex = 0;
};

/** Running maximum over the last `window` values (monotonic queue, O(1) amortised, no allocation
    after prepare). Used for look-ahead: the detector holds the largest level of the preview window,
    so gain reduction starts ahead of a transient but does not release before it has passed. */
class SlidingMax
{
public:
    void prepare (int maxWindow)
    {
        int size = 1;
        while (size < maxWindow + 2)
            size <<= 1;
        values.assign ((size_t) size, 0.0);
        stamps.assign ((size_t) size, 0);
        mask = size - 1;
        reset();
    }

    void reset() noexcept { head = tail = 0; counter = 0; }

    double process (double v, int window) noexcept
    {
        if (window <= 1 || values.empty())
        {
            reset();
            return v;
        }
        window = std::min (window, mask);
        while (tail != head && values[(size_t) ((tail - 1) & mask)] <= v)
            tail = (tail - 1) & mask;
        values[(size_t) tail] = v;
        stamps[(size_t) tail] = counter;
        tail = (tail + 1) & mask;
        while (stamps[(size_t) head] <= counter - window)
            head = (head + 1) & mask;
        ++counter;
        return values[(size_t) head];
    }

private:
    std::vector<double> values;
    std::vector<long long> stamps;
    int head = 0, tail = 0, mask = 0;
    long long counter = 0;
};
} // namespace dsq
