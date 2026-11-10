#pragma once
// TPDF dither, optional noise shaping, then quantisation to the target word length.
// It runs once, on the final output. In a 32-bit float DAW it is only meaningful when the
// DAW then renders to that same integer word length with its own dither switched off.
#include "NoiseShaper.h"

namespace dsq
{
class Dither
{
public:
    static constexpr int kAutoBlackSamples = 64;

    void reset (uint64_t seed) noexcept
    {
        state = seed * 0x9E3779B97F4A7C15ull + 0x1234567ull;
        zeroRun = 0;
        clearHistory();
    }

    /** @param bits 16, 20 or 24. Digital silence stays digital silence (auto-black). */
    double process (double x, int bits) noexcept
    {
        zeroRun = x == 0.0 ? std::min (zeroRun + 1, kAutoBlackSamples) : 0;
        if (zeroRun >= kAutoBlackSamples)
        {
            clearHistory();
            return 0.0;
        }

        const double lsb = std::ldexp (1.0, 1 - bits);
        const double tpdf = uniform() + uniform() - 1.0;           // triangular, +-1 LSB
        const double q = std::round (x / lsb + tpdf) * lsb;
        return std::clamp (q, -1.0, 1.0 - lsb);
    }

    /** As above, with the total error (dither plus quantisation) spectrally shaped by `filter`. */
    double processShaped (double x, int bits, const NoiseShaper::Filter& filter) noexcept
    {
        zeroRun = x == 0.0 ? std::min (zeroRun + 1, kAutoBlackSamples) : 0;
        if (zeroRun >= kAutoBlackSamples)
        {
            clearHistory();
            return 0.0;
        }

        const double lsb = std::ldexp (1.0, 1 - bits);

        double v = x;
        for (int k = 1; k <= filter.order; ++k)
            v += filter.taps[k] * history[(position - k + kHistory) & (kHistory - 1)];

        const double tpdf = uniform() + uniform() - 1.0;
        const double q = std::clamp (std::round (v / lsb + tpdf) * lsb, -1.0, 1.0 - lsb);

        // Bounded, so that hitting the rails cannot wind up the feedback path.
        history[position] = std::clamp (q - v, -8.0 * lsb, 8.0 * lsb);
        position = (position + 1) & (kHistory - 1);
        return q;
    }

private:
    double uniform() noexcept
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return (double) (state >> 11) * (1.0 / 9007199254740992.0);
    }

    void clearHistory() noexcept
    {
        std::fill (std::begin (history), std::end (history), 0.0);
        position = 0;
    }

    static constexpr int kHistory = 32;

    uint64_t state = 88172645463325252ull;
    int zeroRun = 0, position = 0;
    double history[kHistory] {};
};
} // namespace dsq
