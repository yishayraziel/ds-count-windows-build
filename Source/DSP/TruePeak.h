#pragma once
// Polyphase reconstruction peak estimator.
#include "LinearPhaseCrossover.h"

namespace dsq
{
class TruePeak
{
public:
    static constexpr int kTapsPerPhase = 64;
    static constexpr int kDelay = kTapsPerPhase / 2; // the estimate covers samples kDelay and kDelay-1 ago

    void prepare (int oversampling)
    {
        factor = oversampling;
        const int total = factor * kTapsPerPhase;
        std::vector<double> window ((size_t) total + 1);
        firdesign::kaiser (window, 6.0);
        table.assign ((size_t) (factor * kTapsPerPhase), 0.0);

        // Phase p interpolates a point p/factor of a sample after the sample kDelay ago.
        for (int p = 0; p < factor; ++p)
        {
            double sum = 0.0;
            for (int k = 0; k < kTapsPerPhase; ++k)
            {
                const double t = (double) (kDelay - 1 - k) + (double) p / factor + 0.0;
                const double arg = kPi * t;
                const double sinc = std::abs (t) < 1.0e-12 ? 1.0 : std::sin (arg) / arg;
                const double pos = (t / (kTapsPerPhase * 0.5) + 1.0) * 0.5 * total;
                const double win = window[(size_t) std::clamp ((int) std::lround (pos), 0, total)];
                const double v = sinc * win;
                table[(size_t) (p * kTapsPerPhase + k)] = v;
                sum += v;
            }
            for (int k = 0; k < kTapsPerPhase; ++k)
                table[(size_t) (p * kTapsPerPhase + k)] /= sum;
        }
        reset();
    }

    void reset() noexcept
    {
        std::fill (std::begin (history), std::end (history), 0.0);
        index = 0;
    }

    /** Pushes one sample. Returns the largest absolute value on [t-kDelay, t-kDelay+1). */
    double process (double x) noexcept
    {
        history[index] = x;
        double peak = 0.0;
        for (int p = 0; p < factor; ++p)
        {
            const double* h = table.data() + (size_t) (p * kTapsPerPhase);
            double sum = 0.0;
            for (int k = 0; k < kTapsPerPhase; ++k)
                sum += h[k] * history[(index - (kTapsPerPhase - 1 - k) + kTapsPerPhase) % kTapsPerPhase];
            peak = std::max (peak, std::abs (sum));
        }
        index = (index + 1) % kTapsPerPhase;
        return peak;
    }

private:
    int factor = 4, index = 0;
    double history[kTapsPerPhase] {};
    std::vector<double> table;
};
} // namespace dsq
