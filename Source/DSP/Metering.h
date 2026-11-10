#pragma once
// Lock-free data path from the audio thread to the interface. All values are measured.
#include "Common.h"
#include <array>
#include <atomic>

namespace dsq
{
struct WaveColumn
{
    float peak = 0.0f, rms = 0.0f, grDb = 0.0f;
};

class MeteringEngine
{
public:
    static constexpr int kColumns = 2048;

    struct ChannelMeters
    {
        std::atomic<float> inPeak { 0 }, inRms { 0 }, outPeak { 0 }, outRms { 0 };
        std::atomic<float> compGrDb { 0 }, envelopeDb { -120.0f };
        std::atomic<float> releaseFast { 0 }, releaseSlow { 0 };   // share of time each release is acting
        std::atomic<int> overs { 0 };
    };

    std::array<ChannelMeters, 2> channels;
    std::atomic<float> limGrDb { 0 };

    static void raise (std::atomic<float>& a, float v) noexcept
    {
        float old = a.load (std::memory_order_relaxed);
        while (v > old && ! a.compare_exchange_weak (old, v, std::memory_order_relaxed)) {}
    }

    static void lower (std::atomic<float>& a, float v) noexcept
    {
        float old = a.load (std::memory_order_relaxed);
        while (v < old && ! a.compare_exchange_weak (old, v, std::memory_order_relaxed)) {}
    }

    /** Single producer: audio thread. */
    void pushColumn (const WaveColumn& c) noexcept
    {
        const int w = writeCount.load (std::memory_order_relaxed);
        columns[(size_t) (w & (kColumns - 1))] = c;
        writeCount.store (w + 1, std::memory_order_release);
    }

    /** Single consumer: message thread. Returns the new column count. */
    int popColumns (WaveColumn* dest, int maxColumns) noexcept
    {
        const int w = writeCount.load (std::memory_order_acquire);
        int available = w - readCount;
        if (available > kColumns - 16)
        {
            readCount = w - (kColumns - 16);
            available = kColumns - 16;
        }
        const int n = std::min (available, maxColumns);
        for (int i = 0; i < n; ++i)
            dest[i] = columns[(size_t) ((readCount + i) & (kColumns - 1))];
        readCount += n;
        return n;
    }

private:
    std::array<WaveColumn, kColumns> columns {};
    std::atomic<int> writeCount { 0 };
    int readCount = 0;
};
} // namespace dsq
