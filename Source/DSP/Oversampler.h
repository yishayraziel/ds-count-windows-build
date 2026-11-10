#pragma once
// Linear-phase polyphase oversampling for the gain path.
//
// Only the wet term band * (gain - 1) passes through the down-sampler, so neutral settings stay
// bit-exact: gain 1 gives exactly zero through the whole filter.
// Up plus down delay is exactly `tapsPerPhase` host samples.
#include "LinearPhaseCrossover.h"

namespace dsq
{
class Oversampler
{
public:
    struct Config { int factor = 1, tapsPerPhase = 0; };

    /** 4x at 44.1 and 48 kHz, 2x at 88.2 and 96 kHz. At 176.4 and 192 kHz the host rate already does the job. */
    static Config configFor (double sampleRate, bool lowLatency) noexcept
    {
        if (sampleRate > 100000.0)
            return { 1, 0 };
        if (lowLatency)
            return { 2, 16 };
        return { sampleRate > 50000.0 ? 2 : 4, 64 };
    }

    void prepare (const Config& c)
    {
        // Less than 0.001 dB of passband ripple at the normal setting.
        const double beta = c.tapsPerPhase >= 64 ? 10.0 : 7.0;
        factor = c.factor;
        taps = c.tapsPerPhase;
        length = factor * taps + 1;

        prototype.assign ((size_t) length, 0.0);
        std::vector<double> window ((size_t) length, 1.0);
        if (length > 1)
            firdesign::kaiser (window, beta);

        const int centre = (length - 1) / 2;
        double sum = 0.0;
        for (int i = 0; i < length; ++i)
        {
            const double t = (double) (i - centre) / factor;
            const double sinc = std::abs (t) < 1.0e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
            prototype[(size_t) i] = sinc * window[(size_t) i];
            sum += prototype[(size_t) i];
        }
        for (auto& v : prototype)
            v /= sum;

        inputHistory.assign ((size_t) taps + 1, 0.0);
        outputHistory.assign ((size_t) length, 0.0);
        reset();
    }

    void reset() noexcept
    {
        std::fill (inputHistory.begin(), inputHistory.end(), 0.0);
        std::fill (outputHistory.begin(), outputHistory.end(), 0.0);
        inputPos = outputPos = 0;
    }

    int getFactor() const noexcept { return factor; }
    int getLatency() const noexcept { return taps; }

    /** One host sample in, `factor` oversampled values out, oldest first. */
    void up (double x, double* out) noexcept
    {
        if (factor == 1)
        {
            out[0] = x;
            return;
        }

        const int size = taps + 1;
        inputPos = inputPos == 0 ? size - 1 : inputPos - 1;
        inputHistory[(size_t) inputPos] = x;

        for (int p = 0; p < factor; ++p)
        {
            double sum = 0.0;
            int index = inputPos;
            for (int i = p; i < length; i += factor)
            {
                sum += prototype[(size_t) i] * inputHistory[(size_t) index];
                index = index + 1 == size ? 0 : index + 1;
            }
            out[p] = sum * factor;
        }
    }

    /** Pushes one oversampled value. */
    void pushDown (double x) noexcept
    {
        outputPos = outputPos == 0 ? length - 1 : outputPos - 1;
        outputHistory[(size_t) outputPos] = x;
    }

    /** Filter output at the most recently pushed position. */
    double down() const noexcept
    {
        if (factor == 1)
            return outputHistory[(size_t) outputPos];

        double sum = 0.0;
        int index = outputPos;
        for (int i = 0; i < length; ++i)
        {
            sum += prototype[(size_t) i] * outputHistory[(size_t) index];
            index = index + 1 == length ? 0 : index + 1;
        }
        return sum;
    }

private:
    int factor = 1, taps = 0, length = 1, inputPos = 0, outputPos = 0;
    std::vector<double> prototype, inputHistory, outputHistory;
};
} // namespace dsq
