#pragma once
// Linear-phase band splitter.
//
// A symmetric windowed-sinc FIR extracts the selected band. The complement is defined as
// (delayed input - band), so band + complement equals the delayed input to numerical
// precision for ANY kernel, including while kernels are crossfading.
//
// The FIR runs as a uniform partitioned overlap-save convolution with a fixed internal
// block, independent of the host block size. New kernels are built a slice per internal
// block on the audio thread: no allocation, no locks, no spikes, and fully deterministic
// in offline rendering.
#include "FFT.h"

namespace dsq
{
// lrBandPass and lrHighPass are zero-phase versions of 8th-order Linkwitz-Riley slopes (48 dB/oct,
// -6 dB at the edges).
enum class FilterType { fullBand = 0, lowPass, bandPass, highPass, bell, lrBandPass, lrHighPass };

struct BandSpec
{
    FilterType type = FilterType::fullBand;
    double frequency = 3200.0;
    double bandwidthOct = 1.0;
    bool operator== (const BandSpec& o) const noexcept
    {
        return type == o.type && frequency == o.frequency && bandwidthOct == o.bandwidthOct;
    }
    bool operator!= (const BandSpec& o) const noexcept { return ! (*this == o); }
};

namespace firdesign
{
inline double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    const double q = x * x * 0.25;
    for (int k = 1; k < 64; ++k)
    {
        term *= q / ((double) k * k);
        sum += term;
        if (term < sum * 1.0e-16)
            break;
    }
    return sum;
}

inline void kaiser (std::vector<double>& w, double beta)
{
    const int n = (int) w.size();
    const double m = (n - 1) * 0.5, denom = besselI0 (beta);
    for (int i = 0; i < n; ++i)
    {
        const double r = (i - m) / m;
        w[(size_t) i] = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / denom;
    }
}

/** Band edges in Hz clamped to what the sample rate allows. */
inline void bandEdges (const BandSpec& s, double sampleRate, double& low, double& high) noexcept
{
    const double nyquist = sampleRate * 0.5, maxF = nyquist * 0.98, minF = 1.0;
    const double half = std::pow (2.0, s.bandwidthOct * 0.5);
    low = std::clamp (s.frequency / half, minF, maxF);
    high = std::clamp (s.frequency * half, minF, maxF);
}

/** Windowed-sinc low-pass coefficient at index i. */
inline double lowPassTap (int i, int centre, double fc, double sampleRate, double win) noexcept
{
    const double w = 2.0 * kPi * fc / sampleRate;
    const int d = i - centre;
    return (d == 0 ? w / kPi : std::sin (w * d) / (kPi * d)) * win;
}

/** Bell band: Gaussian magnitude centred on the frequency, full width at half maximum set by the
    bandwidth in octaves. Its impulse response is a Gaussian-windowed cosine, so it has a closed form. */
inline double bellTap (int i, int centre, const BandSpec& s, double sampleRate) noexcept
{
    const double half = std::pow (2.0, s.bandwidthOct * 0.5);
    const double fwhm = std::max (1.0, s.frequency * (half - 1.0 / half));
    const double sigma = fwhm / 2.35482004503;
    const double t = (i - centre) / sampleRate;
    const double a = 2.0 * kPi * kPi * sigma * sigma;
    return 2.0 * sigma * std::sqrt (2.0 * kPi) / sampleRate * std::exp (-a * t * t) * std::cos (2.0 * kPi * s.frequency * t);
}

inline bool isSpectral (FilterType t) noexcept { return t == FilterType::lrBandPass || t == FilterType::lrHighPass; }

/** Magnitude of the spectral (frequency-sampled) band types. */
inline double spectralMagnitude (double f, const BandSpec& s, double sampleRate) noexcept
{
    if (f <= 0.0)
        return 0.0;
    double low, high;
    bandEdges (s, sampleRate, low, high);
    if (s.type == FilterType::lrHighPass)
        low = std::clamp (s.frequency, 1.0, sampleRate * 0.49);
    const double hp = 1.0 / (1.0 + std::pow (low / f, 8.0));
    const double lp = s.type == FilterType::lrHighPass ? 1.0 : 1.0 / (1.0 + std::pow (f / high, 8.0));
    return hp * lp;
}

/** Zero-phase FIR from a magnitude response: inverse FFT, centred, windowed. `buf` must hold `fft` points. */
inline void designSpectral (double* h, int numTaps, const std::vector<double>& window, const BandSpec& s, double sampleRate,
                            const FFT& fft, int fftSize, FFT::Complex* buf) noexcept
{
    for (int k = 0; k <= fftSize / 2; ++k)
        buf[k] = FFT::Complex (spectralMagnitude ((double) k * sampleRate / fftSize, s, sampleRate), 0.0);
    for (int k = fftSize / 2 + 1; k < fftSize; ++k)
        buf[k] = buf[fftSize - k];
    fft.inverse (buf);
    const int c = (numTaps - 1) / 2;
    for (int i = 0; i < numTaps; ++i)
        h[i] = buf[(i - c + fftSize) % fftSize].real() * window[(size_t) i];
}

inline int spectralFftSize (int numTaps) noexcept
{
    int n = 1;
    while (n < numTaps * 4)
        n <<= 1;
    return n;
}

/** Writes tap range [from, to). Symmetric with centre (N-1)/2 for every band type. */
inline void designRange (double* h, int from, int to, int numTaps, const std::vector<double>& window,
                         const BandSpec& s, double sampleRate) noexcept
{
    const int c = (numTaps - 1) / 2;
    double low, high;
    bandEdges (s, sampleRate, low, high);

    for (int i = from; i < to; ++i)
    {
        const double win = window[(size_t) i];
        double v = 0.0;
        switch (s.type)
        {
            case FilterType::fullBand: v = i == c ? 1.0 : 0.0; break;
            case FilterType::lowPass:  v = lowPassTap (i, c, s.frequency, sampleRate, win); break;
            case FilterType::highPass: v = (i == c ? 1.0 : 0.0) - lowPassTap (i, c, s.frequency, sampleRate, win); break;
            case FilterType::bandPass: v = lowPassTap (i, c, high, sampleRate, win) - lowPassTap (i, c, low, sampleRate, win); break;
            case FilterType::bell:     v = bellTap (i, c, s, sampleRate) * win; break;
            case FilterType::lrBandPass:
            case FilterType::lrHighPass: v = 0.0; break;   // designed whole by designSpectral
        }
        h[i] = v;
    }
}
} // namespace firdesign

class LinearPhaseCrossover
{
public:
    static constexpr int kCrossfadeBlocks = 6;

    struct Config { int numTaps = 4095, blockSize = 256; double kaiserBeta = 9.0; };

    /** Taps and block size for a sample rate. Scales with rate so that the frequency resolution is constant. */
    static Config configFor (double sampleRate, bool lowLatency) noexcept
    {
        int factor = 1;
        while (48000.0 * factor < sampleRate - 1.0)
            factor *= 2;
        Config c;
        c.numTaps = lowLatency ? 255 * factor + (factor - 1) : 4095 * factor + (factor - 1);
        c.blockSize = lowLatency ? 32 * factor : 256 * factor;
        c.kaiserBeta = lowLatency ? 5.0 : 9.0;
        return c;
    }

    void prepare (double sampleRateIn, const Config& c)
    {
        sampleRate = sampleRateIn;
        numTaps = c.numTaps;
        block = c.blockSize;
        numPartitions = (numTaps + block - 1) / block;

        fft.prepare (block * 2);
        const size_t fftSize = (size_t) block * 2;
        designSize = firdesign::spectralFftSize (numTaps);
        designFft.prepare (designSize);
        designBuffer.assign ((size_t) designSize, {});

        window.resize ((size_t) numTaps);
        firdesign::kaiser (window, c.kaiserBeta);
        taps.assign ((size_t) numTaps, 0.0);

        inputBuffer.assign (fftSize, 0.0);
        inputBlock.assign ((size_t) block, 0.0);
        outputBlock.assign ((size_t) block, 0.0);
        fifoIndex = 0;

        spectra.assign (fftSize * (size_t) numPartitions, {});
        kernels[0].assign (fftSize * (size_t) numPartitions, {});
        kernels[1].assign (fftSize * (size_t) numPartitions, {});
        work.assign (fftSize, {});
        workB.assign (fftSize, {});
        partitionCursor = 0;

        activeSpec = {};
        wantedSpec = {};
        buildKernelNow (0, activeSpec);
        active = 0;
        crossfadePos = -1;
        buildStage = BuildStage::idle;
        transparent = true;
    }

    /** Input to output delay of band() and complement(). */
    int latency() const noexcept { return block + (numTaps - 1) / 2; }
    int getNumTaps() const noexcept { return numTaps; }

    void reset() noexcept
    {
        std::fill (inputBuffer.begin(), inputBuffer.end(), 0.0);
        std::fill (outputBlock.begin(), outputBlock.end(), 0.0);
        std::fill (spectra.begin(), spectra.end(), FFT::Complex{});
        fifoIndex = 0;
    }

    /** Applies a spec synchronously. Message-thread or prepare-time use only. */
    void setSpecImmediately (const BandSpec& spec) noexcept
    {
        const BandSpec s = normalised (spec);
        wantedSpec = activeSpec = s;
        active = 0;
        buildKernelNow (0, s);
        crossfadePos = -1;
        buildStage = BuildStage::idle;
        transparent = s.type == FilterType::fullBand;
    }

    void setSpec (const BandSpec& s) noexcept { wantedSpec = normalised (s); }

    /** True while the band is the whole signal, so the FFT work is skipped. */
    bool isTransparent() const noexcept { return transparent; }

    /** Pushes one sample and returns the band signal, delayed by latency(). */
    double process (double x) noexcept
    {
        if (fifoIndex == block)
            runBlock();
        inputBlock[(size_t) fifoIndex] = x;
        return outputBlock[(size_t) fifoIndex++];
    }

    /** Frequency response helper for the display: designs into `out` without touching audio state. */
    static void designForDisplay (std::vector<double>& out, const BandSpec& s, double sr, const Config& c)
    {
        out.resize ((size_t) c.numTaps);
        std::vector<double> w ((size_t) c.numTaps);
        firdesign::kaiser (w, c.kaiserBeta);
        if (firdesign::isSpectral (s.type))
        {
            const int n = firdesign::spectralFftSize (c.numTaps);
            FFT f;
            f.prepare (n);
            std::vector<FFT::Complex> buf ((size_t) n);
            firdesign::designSpectral (out.data(), c.numTaps, w, s, sr, f, n, buf.data());
        }
        else
            firdesign::designRange (out.data(), 0, c.numTaps, c.numTaps, w, s, sr);
    }

private:
    enum class BuildStage { idle, taps, spectra };

    static BandSpec normalised (const BandSpec& s) noexcept { return s.type == FilterType::fullBand ? BandSpec{} : s; }

    void designTaps (int from, int to, const BandSpec& s) noexcept
    {
        if (firdesign::isSpectral (s.type))
        {
            if (from == 0)
                firdesign::designSpectral (taps.data(), numTaps, window, s, sampleRate, designFft, designSize, designBuffer.data());
        }
        else
            firdesign::designRange (taps.data(), from, to, numTaps, window, s, sampleRate);
    }

    void buildKernelNow (int slot, const BandSpec& s) noexcept
    {
        designTaps (0, numTaps, s);
        for (int p = 0; p < numPartitions; ++p)
            transformPartition (slot, p);
    }

    void transformPartition (int slot, int p) noexcept
    {
        const int fftSize = block * 2;
        for (int i = 0; i < fftSize; ++i)
        {
            const int idx = p * block + i;
            const double v = (i < block && idx < numTaps) ? taps[(size_t) idx] : 0.0;
            work[(size_t) i] = FFT::Complex (v, 0.0);
        }
        fft.forward (work.data());
        std::copy (work.begin(), work.end(), kernels[slot].begin() + (std::ptrdiff_t) p * fftSize);
    }

    void stepBuilder() noexcept
    {
        if (crossfadePos >= 0)
            return;

        if (buildStage == BuildStage::idle)
        {
            if (wantedSpec == activeSpec)
                return;
            buildingSpec = wantedSpec;
            buildCursor = 0;
            buildStage = BuildStage::taps;
        }

        if (buildStage == BuildStage::taps)
        {
            const int end = std::min (numTaps, buildCursor + std::max (1, numTaps / 3));
            designTaps (buildCursor, end, buildingSpec);
            buildCursor = end;
            if (buildCursor == numTaps)
            {
                buildStage = BuildStage::spectra;
                buildCursor = 0;
            }
            return;
        }

        const int end = std::min (numPartitions, buildCursor + 6);
        for (; buildCursor < end; ++buildCursor)
            transformPartition (1 - active, buildCursor);

        if (buildCursor == numPartitions)
        {
            buildStage = BuildStage::idle;
            crossfadePos = 0;
            transparent = false;
        }
    }

    void convolve (int slot, std::vector<FFT::Complex>& out) noexcept
    {
        const size_t fftSize = (size_t) block * 2;
        std::fill (out.begin(), out.begin() + block + 1, FFT::Complex{});

        const auto* h = kernels[slot].data();
        for (int p = 0; p < numPartitions; ++p)
        {
            const int age = (partitionCursor - p + numPartitions) % numPartitions;
            const auto* x = spectra.data() + (size_t) age * fftSize;
            const auto* hp = h + (size_t) p * fftSize;
            for (int k = 0; k <= block; ++k)
            {
                const double xr = x[k].real(), xi = x[k].imag(), hr = hp[k].real(), hi = hp[k].imag();
                out[(size_t) k] += FFT::Complex (xr * hr - xi * hi, xr * hi + xi * hr);
            }
        }

        for (int k = 1; k < block; ++k)
            out[fftSize - (size_t) k] = std::conj (out[(size_t) k]);
        fft.inverse (out.data());
    }

    void runBlock() noexcept
    {
        fifoIndex = 0;
        stepBuilder();

        const size_t fftSize = (size_t) block * 2;
        std::copy (inputBuffer.begin() + block, inputBuffer.end(), inputBuffer.begin());
        std::copy (inputBlock.begin(), inputBlock.end(), inputBuffer.begin() + block);

        partitionCursor = (partitionCursor + 1) % numPartitions;
        auto* slotSpectrum = spectra.data() + (size_t) partitionCursor * fftSize;
        for (size_t i = 0; i < fftSize; ++i)
            slotSpectrum[i] = FFT::Complex (inputBuffer[i], 0.0);
        fft.forward (slotSpectrum);

        // Settled in Full Band the engine uses its aligned dry signal. The input spectra above
        // keep running, so that leaving Full Band has a valid history and cannot click.
        if (transparent && crossfadePos < 0)
            return;

        convolve (active, work);
        if (crossfadePos >= 0)
        {
            convolve (1 - active, workB);
            const double start = (double) crossfadePos / kCrossfadeBlocks;
            const double step = 1.0 / (kCrossfadeBlocks * block);
            for (int i = 0; i < block; ++i)
            {
                const double t = start + step * i;
                const double g = 0.5 - 0.5 * std::cos (kPi * t);
                const double a = work[(size_t) (block + i)].real(), b = workB[(size_t) (block + i)].real();
                outputBlock[(size_t) i] = a + (b - a) * g;
            }
            if (++crossfadePos >= kCrossfadeBlocks)
            {
                crossfadePos = -1;
                active = 1 - active;
                activeSpec = buildingSpec;
                transparent = activeSpec.type == FilterType::fullBand;
            }
        }
        else
        {
            for (int i = 0; i < block; ++i)
                outputBlock[(size_t) i] = work[(size_t) (block + i)].real();
        }
    }

    double sampleRate = 48000.0;
    int numTaps = 0, block = 0, numPartitions = 0, fifoIndex = 0, partitionCursor = 0;
    int active = 0, crossfadePos = -1, buildCursor = 0;
    BuildStage buildStage = BuildStage::idle;
    bool transparent = true;
    BandSpec activeSpec, wantedSpec, buildingSpec;

    FFT fft, designFft;
    int designSize = 0;
    std::vector<FFT::Complex> designBuffer;
    std::vector<double> window, taps, inputBuffer, inputBlock, outputBlock;
    std::vector<FFT::Complex> spectra, kernels[2], work, workB;
};
} // namespace dsq
