#pragma once
// Noise-shaping filters for the dither stage.
//
// Independent design, not POW-R. POW-R is a licensed, unpublished algorithm. These three curves
// only follow the same general aims as public descriptions of such dithers:
//   Shape 1  flat through the audible band, noise pushed just below Nyquist
//   Shape 2  low-order psychoacoustic curve
//   Shape 3  high-order psychoacoustic curve
//
// Each curve is designed for the actual sample rate. A target log magnitude is turned into a
// minimum-phase filter by the real-cepstrum method, which yields a first tap of exactly 1, so the
// filter is a valid noise transfer function that obeys the Gerzon-Craven log-area limit.
#include "FFT.h"

namespace dsq
{
class NoiseShaper
{
public:
    static constexpr int kMaxOrder = 16, kNumShapes = 3;

    struct Filter
    {
        int order = 0;
        double taps[kMaxOrder + 1] {};   // taps[0] == 1
    };

    void prepare (double sampleRate)
    {
        filters[0] = design (sampleRate, 0, 16);
        filters[1] = design (sampleRate, 1, 5);
        filters[2] = design (sampleRate, 2, 9);
    }

    /** @param shape 1, 2 or 3 */
    const Filter& get (int shape) const noexcept { return filters[std::clamp (shape, 1, kNumShapes) - 1]; }

    /** Threshold of hearing, Terhardt approximation, in dB. */
    static double hearingThresholdDb (double hz) noexcept
    {
        const double k = std::max (hz, 300.0) * 0.001;   // below 300 Hz the shaper would only waste headroom
        const double d = k - 3.3;
        return 3.64 * std::pow (k, -0.8) - 6.5 * std::exp (-0.6 * d * d) + 0.001 * k * k * k * k;
    }

    static double targetDb (double hz, double sampleRate, int shapeIndex) noexcept
    {
        if (shapeIndex == 0)
        {
            const double nyquist = sampleRate * 0.5;
            const double start = std::min (19000.0, nyquist * 0.85);
            const double t = std::clamp ((hz - start) / (nyquist - start), 0.0, 1.0);
            return 30.0 * t * t;
        }
        const double strength = shapeIndex == 1 ? 0.55 : 0.85;
        return strength * std::min (hearingThresholdDb (hz), 50.0);
    }

    static Filter design (double sampleRate, int shapeIndex, int order)
    {
        constexpr int size = 4096;
        FFT fft;
        fft.prepare (size);
        std::vector<FFT::Complex> d ((size_t) size);

        // Log magnitude in nepers. The mean is removed, which is the log-area limit.
        double mean = 0.0;
        for (int i = 0; i < size; ++i)
        {
            const int bin = std::min (i, size - i);
            const double hz = bin * sampleRate / size;
            const double v = targetDb (hz, sampleRate, shapeIndex) * std::log (10.0) / 20.0;
            d[(size_t) i] = v;
            mean += v;
        }
        mean /= size;
        for (auto& c : d)
            c -= mean;

        // Real cepstrum, folded to the causal side, gives the minimum-phase log spectrum.
        fft.inverse (d.data());
        for (int i = 1; i < size / 2; ++i)
            d[(size_t) i] *= 2.0;
        for (int i = size / 2 + 1; i < size; ++i)
            d[(size_t) i] = 0.0;
        fft.forward (d.data());
        for (auto& c : d)
            c = std::exp (c);
        fft.inverse (d.data());

        Filter f;
        f.order = order;
        for (int k = 0; k <= order; ++k)
        {
            const double fade = 0.5 + 0.5 * std::cos (kPi * k / (order + 1.0));
            f.taps[k] = d[(size_t) k].real() * fade;
        }
        f.taps[0] = 1.0;
        return f;
    }

private:
    Filter filters[kNumShapes];
};
} // namespace dsq
