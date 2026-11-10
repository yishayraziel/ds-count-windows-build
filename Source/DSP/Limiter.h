#pragma once
// Three independent lookahead limiters sharing one topology:
//   required gain -> sliding minimum over W samples -> release -> W-sample box average.
// Because every value inside the box is at or below the gain required by a peak when that
// peak reaches the output, the gain is guaranteed to have arrived by then, with no clipping.
// The audio is only ever delayed and multiplied, so below the ceiling it is bit-transparent.
#include "TruePeak.h"

namespace dsq
{
enum class LimiterType { safe = 0, type1, type2 };

class Limiter
{
public:
    void prepare (double sampleRateIn, bool lowLatency)
    {
        sampleRate = sampleRateIn;
        lowLat = lowLatency;
        maxWindow = std::max (2, (int) std::ceil (sampleRate * (lowLatency ? 0.001 : 0.005)));
        for (auto& d : audio)
            d.prepare (maxWindow + TruePeak::kDelay + 4);
        // Audio content stays below 20 kHz, so higher host rates need fewer interpolation points.
        const int oversampling = sampleRate > 150000.0 ? 2 : sampleRate > 80000.0 ? 4 : 8;
        for (auto& t : tp)
            t.prepare (oversampling);
        required.prepare (maxWindow + 4);
        minIndex.assign ((size_t) maxWindow + 4, 0);
        minValue.assign ((size_t) maxWindow + 4, 1.0);
        box.assign ((size_t) maxWindow + 4, 1.0);
        ceilingSmooth.prepare (sampleRate, 0.02);
        ceilingSmooth.snap (1.0);
        setType (LimiterType::safe);
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : audio)
            d.reset();
        for (auto& t : tp)
            t.reset();
        required.reset (1.0);
        std::fill (box.begin(), box.end(), 1.0);
        boxIndex = 0;
        boxSum = window;
        notUnity = 0;
        qHead = qTail = 0;
        counter = 0;
        envelope = average = 1.0;
        lastGain = 1.0;
    }

    void setType (LimiterType t) noexcept
    {
        if (t == type && window != 0)
            return;
        type = t;
        const double ms = t == LimiterType::safe ? (lowLat ? 0.5 : 1.0) : t == LimiterType::type1 ? (lowLat ? 1.0 : 3.0) : (lowLat ? 1.0 : 5.0);
        window = std::clamp ((int) std::lround (ms * 0.001 * sampleRate), 2, maxWindow);

        fastCoef = onePoleCoef (t == LimiterType::safe ? 0.08 : 0.04, sampleRate);
        slowCoef = onePoleCoef (t == LimiterType::safe ? 0.08 : 0.6, sampleRate);
        averageCoef = onePoleCoef (0.3, sampleRate);

        std::fill (box.begin(), box.end(), lastGain);
        boxSum = lastGain * window;
        notUnity = lastGain < 1.0 ? window : 0;
        boxIndex = 0;
    }

    void setCeilingDb (double db) noexcept { ceilingSmooth.setTarget (dbToGain (db)); }
    void snapCeilingDb (double db) noexcept { ceilingSmooth.snap (dbToGain (db)); }

    int latency() const noexcept { return maxWindow + TruePeak::kDelay - 1; }

    double gain() const noexcept { return lastGain; }

    /** @param mix 0 = delay only, 1 = fully limited, between = the fade when switching on or off. */
    void process (double& l, double& r, int numChannels, double mix) noexcept
    {
        const double ceiling = ceilingSmooth.next();
        audio[0].push (l);
        audio[1].push (numChannels > 1 ? r : 0.0);

        double peak = 0.0;
        if (mix <= 0.0)
        {
            // Off: no detection cost. The state relaxes to unity, so switching on is clean.
        }
        else if (type == LimiterType::type2)
        {
            peak = tp[0].process (l);
            if (numChannels > 1)
                peak = std::max (peak, tp[1].process (r));
        }
        else
        {
            peak = std::abs (audio[0].read (TruePeak::kDelay));
            if (numChannels > 1)
                peak = std::max (peak, std::abs (audio[1].read (TruePeak::kDelay)));
        }

        const double wanted = peak > ceiling ? ceiling / peak : 1.0;
        required.push (wanted);

        // Sliding minimum over W+1 samples, covering a peak that lies between two samples.
        const double value = required.read (maxWindow - window);
        const int span = (int) minIndex.size();
        while (qHead != qTail && minValue[(size_t) ((qTail - 1 + span) % span)] >= value)
            qTail = (qTail - 1 + span) % span;
        minValue[(size_t) qTail] = value;
        minIndex[(size_t) qTail] = counter;
        qTail = (qTail + 1) % span;
        while (minIndex[(size_t) qHead] + window < counter)
            qHead = (qHead + 1) % span;
        const double held = minValue[(size_t) qHead];
        ++counter;

        if (held <= envelope)
        {
            envelope = held;
        }
        else
        {
            // Below the average gain a transient is recovering, above it the limiter is
            // tracking sustained level, which releases slowly and so does not pump.
            const double coef = envelope < average ? fastCoef : slowCoef;
            envelope = std::min (held, envelope + (held - envelope) * coef);
            if (held - envelope < 1.0e-12)
                envelope = held;
        }
        average += (envelope - average) * averageCoef;

        const double leaving = box[(size_t) boxIndex];
        if (leaving != 1.0)
            --notUnity;
        if (envelope != 1.0)
            ++notUnity;
        box[(size_t) boxIndex] = envelope;
        boxSum += envelope - leaving;
        boxIndex = (boxIndex + 1) % window;

        if (notUnity == 0)
            boxSum = window;
        else if ((counter & 0xffff) == 0)
        {
            boxSum = 0.0;
            for (int i = 0; i < window; ++i)
                boxSum += box[(size_t) i];
        }

        lastGain = notUnity == 0 ? 1.0 : boxSum / window;

        const int outDelay = maxWindow + TruePeak::kDelay - 1;
        const double g = mix >= 1.0 ? lastGain : 1.0 + (lastGain - 1.0) * mix;
        l = audio[0].read (outDelay) * g;
        r = audio[1].read (outDelay) * g;
        if (mix >= 1.0)
        {
            l = std::clamp (l, -ceiling, ceiling);
            r = std::clamp (r, -ceiling, ceiling);
        }
    }

private:
    double sampleRate = 48000.0;
    bool lowLat = false;
    LimiterType type = LimiterType::safe;
    int maxWindow = 2, window = 0, boxIndex = 0, notUnity = 0, qHead = 0, qTail = 0;
    int64_t counter = 0;
    double fastCoef = 1.0, slowCoef = 1.0, averageCoef = 1.0, envelope = 1.0, average = 1.0, boxSum = 0.0, lastGain = 1.0;

    DelayLine audio[2], required;
    TruePeak tp[2];
    std::vector<int64_t> minIndex;
    std::vector<double> minValue, box;
    Smoother ceilingSmooth;
};
} // namespace dsq
