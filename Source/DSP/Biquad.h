#pragma once
// Second-order sections, transposed direct form II, double precision.
// Designs follow R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients".
#include "Common.h"
#include <complex>

namespace dsq
{
struct BiquadCoefs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static BiquadCoefs make (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    static double omega (double hz, double sr) noexcept { return 2.0 * kPi * std::clamp (hz, 1.0, sr * 0.49) / sr; }

    static BiquadCoefs lowPass (double hz, double q, double sr) noexcept
    {
        const double w = omega (hz, sr), c = std::cos (w), a = std::sin (w) / (2.0 * q);
        return make ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a);
    }

    static BiquadCoefs highPass (double hz, double q, double sr) noexcept
    {
        const double w = omega (hz, sr), c = std::cos (w), a = std::sin (w) / (2.0 * q);
        return make ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a);
    }

    /** Constant 0 dB peak gain band pass, bandwidth in octaves. */
    static BiquadCoefs bandPass (double hz, double octaves, double sr) noexcept
    {
        const double w = omega (hz, sr), c = std::cos (w), s = std::sin (w);
        const double a = s * std::sinh (std::log (2.0) / 2.0 * std::max (octaves, 0.02) * w / s);
        return make (a, 0, -a, 1 + a, -2 * c, 1 - a);
    }

    /** Magnitude at a frequency, for displays and tests. */
    double magnitude (double hz, double sr) const noexcept
    {
        const double w = 2.0 * kPi * hz / sr;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        return std::abs ((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
    }
};

class Biquad
{
public:
    void set (const BiquadCoefs& c) noexcept { k = c; }
    void reset() noexcept { s1 = s2 = 0.0; }

    double process (double x) noexcept
    {
        const double y = k.b0 * x + s1;
        s1 = k.b1 * x - k.a1 * y + s2;
        s2 = k.b2 * x - k.a2 * y;
        if (std::abs (s1) < 1.0e-25) s1 = 0.0;
        if (std::abs (s2) < 1.0e-25) s2 = 0.0;
        return y;
    }

private:
    BiquadCoefs k;
    double s1 = 0.0, s2 = 0.0;
};
} // namespace dsq
