#pragma once
// ITU-R BS.1770-4 / EBU R128 loudness: momentary, short-term and gated integrated.
// The audio thread writes, the interface reads, through atomics only.
#include "Biquad.h"
#include <array>
#include <atomic>

namespace dsq
{
class LoudnessMeter
{
public:
    static constexpr double kAbsoluteGate = -70.0, kBinWidth = 0.1;
    static constexpr int kBins = 800;   // -70 ... +10 LUFS

    void prepare (double sampleRate)
    {
        // K-weighting for any rate, from the analogue prototype of BS.1770 (as derived in pyloudnorm).
        const double sr = sampleRate;
        {
            const double g = 3.999843853973347, f0 = 1681.974450955533, q = 0.7071752369554196;
            const double k = std::tan (kPi * f0 / sr), vh = std::pow (10.0, g / 20.0), vb = std::pow (vh, 0.4996667741545416);
            const double a0 = 1.0 + k / q + k * k;
            shelf = BiquadCoefs::make (vh + vb * k / q + k * k, 2.0 * (k * k - vh), vh - vb * k / q + k * k, a0, 2.0 * (k * k - 1.0), 1.0 - k / q + k * k);
        }
        {
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            const double k = std::tan (kPi * f0 / sr);
            const double a0 = 1.0 + k / q + k * k;
            highPass = { 1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        }
        blockLength = std::max (1, (int) std::lround (sr * 0.1));
        reset();
        resetIntegrated();
    }

    void reset() noexcept
    {
        for (auto* f : { &stage1[0], &stage1[1], &stage2[0], &stage2[1] })
        {
            f->reset();
        }
        stage1[0].set (shelf); stage1[1].set (shelf);
        stage2[0].set (highPass); stage2[1].set (highPass);
        blocks.fill (0.0);
        blockIndex = 0;
        blockSum = 0.0;
        blockCount = 0;
        blocksSeen = 0;
        momentary.store (-200.0f);
        shortTerm.store (-200.0f);
    }

    void requestIntegratedReset() noexcept { resetRequested.store (true); }

    /** @param numChannels 1 or 2. Mono is counted once, as BS.1770 specifies. */
    void process (double l, double r, int numChannels) noexcept
    {
        const double zl = stage2[0].process (stage1[0].process (l));
        double e = zl * zl;
        if (numChannels > 1)
        {
            const double zr = stage2[1].process (stage1[1].process (r));
            e += zr * zr;
        }
        blockSum += e;
        if (++blockCount == blockLength)
            closeBlock();
    }

    static double toLufs (double meanSquare) noexcept { return meanSquare > 1.0e-20 ? -0.691 + 10.0 * std::log10 (meanSquare) : -200.0; }

    /** Integrated loudness from the gating histogram, or -200 when nothing has passed the gate. */
    double integrated() const noexcept
    {
        double power = 0.0, count = 0.0;
        for (int i = 0; i < kBins; ++i)
        {
            power += energy[(size_t) i].load (std::memory_order_relaxed);
            count += counts[(size_t) i].load (std::memory_order_relaxed);
        }
        if (count <= 0.0)
            return -200.0;
        const double relativeGate = toLufs (power / count) - 10.0;
        const int first = std::clamp ((int) std::ceil ((relativeGate - kAbsoluteGate) / kBinWidth), 0, kBins);
        power = count = 0.0;
        for (int i = first; i < kBins; ++i)
        {
            power += energy[(size_t) i].load (std::memory_order_relaxed);
            count += counts[(size_t) i].load (std::memory_order_relaxed);
        }
        return count > 0.0 ? toLufs (power / count) : -200.0;
    }

    std::atomic<float> momentary { -200.0f }, shortTerm { -200.0f };

private:
    void resetIntegrated() noexcept
    {
        for (int i = 0; i < kBins; ++i)
        {
            energy[(size_t) i].store (0.0, std::memory_order_relaxed);
            counts[(size_t) i].store (0.0, std::memory_order_relaxed);
        }
    }

    void closeBlock() noexcept
    {
        if (resetRequested.exchange (false))
            resetIntegrated();

        blocks[(size_t) blockIndex] = blockSum / blockLength;
        blockIndex = (blockIndex + 1) % 30;
        blockSum = 0.0;
        blockCount = 0;
        blocksSeen = std::min (blocksSeen + 1, 30);

        auto meanOf = [this] (int n)
        {
            double s = 0.0;
            for (int i = 1; i <= n; ++i)
                s += blocks[(size_t) ((blockIndex - i + 30) % 30)];
            return s / n;
        };

        if (blocksSeen >= 4)
        {
            const double m = meanOf (4);
            const double lufs = toLufs (m);
            momentary.store ((float) lufs, std::memory_order_relaxed);
            // 400 ms gating blocks with 75 % overlap, one every 100 ms.
            if (lufs > kAbsoluteGate)
            {
                const int bin = std::clamp ((int) ((lufs - kAbsoluteGate) / kBinWidth), 0, kBins - 1);
                energy[(size_t) bin].store (energy[(size_t) bin].load (std::memory_order_relaxed) + m, std::memory_order_relaxed);
                counts[(size_t) bin].store (counts[(size_t) bin].load (std::memory_order_relaxed) + 1.0, std::memory_order_relaxed);
            }
        }
        if (blocksSeen >= 30)
            shortTerm.store ((float) toLufs (meanOf (30)), std::memory_order_relaxed);
    }

    BiquadCoefs shelf, highPass;
    Biquad stage1[2], stage2[2];
    std::array<double, 30> blocks {};
    int blockIndex = 0, blockCount = 0, blockLength = 4800, blocksSeen = 0;
    double blockSum = 0.0;
    std::array<std::atomic<double>, kBins> energy {};
    std::array<std::atomic<double>, kBins> counts {};
    std::atomic<bool> resetRequested { false };
};

/** Single-producer, single-consumer sample FIFO for spectrum displays. */
class SpectrumTap
{
public:
    static constexpr int kSize = 16384;

    void push (float x) noexcept
    {
        const int w = writeIndex.load (std::memory_order_relaxed);
        buffer[(size_t) (w & (kSize - 1))] = x;
        writeIndex.store (w + 1, std::memory_order_release);
    }

    /** Copies the most recent n samples, oldest first. Returns false before enough have arrived. */
    bool latest (float* dest, int n) const noexcept
    {
        const int w = writeIndex.load (std::memory_order_acquire);
        if (w < n || n > kSize / 2)
            return false;
        for (int i = 0; i < n; ++i)
            dest[i] = buffer[(size_t) ((w - n + i) & (kSize - 1))];
        return true;
    }

private:
    std::array<float, kSize> buffer {};
    std::atomic<int> writeIndex { 0 };
};
} // namespace dsq
