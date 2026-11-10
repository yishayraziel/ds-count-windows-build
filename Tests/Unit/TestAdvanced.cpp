// Brief section 18, tests 13-25.
#include "TestHarness.h"
#include <cstring>
using namespace dsq;

static double firMagnitudeDb (const std::vector<double>& h, double f, double sr)
{
    double re = 0, im = 0;
    for (size_t i = 0; i < h.size(); ++i)
    {
        re += h[i] * std::cos (2 * kPi * f * (double) i / sr);
        im -= h[i] * std::sin (2 * kPi * f * (double) i / sr);
    }
    return gainToDb (std::sqrt (re * re + im * im));
}

TEST (t13_crossover_frequency_response)
{
    const double sr = 48000;
    const auto cfg = LinearPhaseCrossover::configFor (sr, false);
    std::vector<double> h;
    BandSpec bp; bp.type = FilterType::bandPass; bp.frequency = 3200; bp.bandwidthOct = 1.0;
    LinearPhaseCrossover::designForDisplay (h, bp, sr, cfg);
    NEAR (firMagnitudeDb (h, 3200, sr), 0, 0.01);
    const double edge = 3200 * std::pow (2.0, 0.5);
    NEAR (firMagnitudeDb (h, edge, sr), -6.02, 0.1);
    NEAR (firMagnitudeDb (h, 3200 / std::pow (2.0, 0.5), sr), -6.02, 0.1);
    const double stop = std::max (firMagnitudeDb (h, 1000, sr), firMagnitudeDb (h, 10000, sr));
    test::record ("bandpass_stopband_dB", stop);
    CHECK (stop < -80);

    BandSpec lp; lp.type = FilterType::lowPass; lp.frequency = 1000;
    LinearPhaseCrossover::designForDisplay (h, lp, sr, cfg);
    NEAR (firMagnitudeDb (h, 200, sr), 0, 0.01);
    NEAR (firMagnitudeDb (h, 1000, sr), -6.02, 0.1);
    CHECK (firMagnitudeDb (h, 2000, sr) < -80);

    BandSpec hp; hp.type = FilterType::highPass; hp.frequency = 1000;
    LinearPhaseCrossover::designForDisplay (h, hp, sr, cfg);
    NEAR (firMagnitudeDb (h, 5000, sr), 0, 0.01);
    CHECK (firMagnitudeDb (h, 300, sr) < -80);

    // The running convolver matches the designed kernel.
    LinearPhaseCrossover x;
    x.prepare (sr, cfg);
    x.setSpecImmediately (bp);
    LinearPhaseCrossover::designForDisplay (h, bp, sr, cfg);
    std::vector<double> imp (20000, 0.0);
    imp[0] = 1;
    double err = 0;
    for (size_t i = 0; i < imp.size(); ++i)
    {
        const double y = x.process (imp[i]);
        const int k = (int) i - cfg.blockSize;
        const double want = k >= 0 && k < cfg.numTaps ? h[(size_t) k] : 0.0;
        err = std::max (err, std::abs (y - want));
    }
    test::record ("convolver_vs_kernel_error", err);
    CHECK (err < 1e-12);
}

TEST (t14_linear_phase)
{
    for (auto type : { FilterType::lowPass, FilterType::bandPass, FilterType::highPass })
    {
        const auto cfg = LinearPhaseCrossover::configFor (48000, false);
        std::vector<double> h;
        BandSpec s; s.type = type; s.frequency = 700; s.bandwidthOct = 2.0;
        LinearPhaseCrossover::designForDisplay (h, s, 48000, cfg);
        CHECK (h.size() % 2 == 1);
        double asym = 0;
        for (size_t i = 0; i < h.size() / 2; ++i)
            asym = std::max (asym, std::abs (h[i] - h[h.size() - 1 - i]));
        CHECK (asym < 1e-15);  // Type I symmetric FIR: exactly linear phase.
    }
}

TEST (t15_band_reconstruction_null)
{
    for (double sr : { 44100.0, 96000.0, 192000.0 })
        for (bool ll : { false, true })
            for (auto type : { FilterType::lowPass, FilterType::bandPass, FilterType::highPass })
            {
                Rig rig (sr, ll);
                auto& c = rig.params.ch[0];
                c.band.type = type; c.band.frequency = 1200; c.band.bandwidthOct = 1.5;
                rig.apply();
                auto l = sig::pink ((int) sr, 0.9), r = sig::noise ((int) sr, 0.7);
                const auto dl = l;
                rig.run (l, r);
                double err = 0;
                for (size_t i = 0; i < dl.size(); ++i) err = std::max (err, std::abs (l[i] - dl[i]));
                CHECK (err == 0.0);
            }

    // Also holds while a sweeping frequency is crossfading kernels.
    Rig rig;
    auto& c = rig.params.ch[0];
    c.band.type = FilterType::bandPass;
    rig.apply();
    const int n = 96000, block = 128;
    auto in = sig::pink (n + rig.engine.latencySamples(), 0.8);
    std::vector<double> outL (in.size()), outR (in.size());
    for (int pos = 0; pos + block <= (int) in.size(); pos += block)
    {
        rig.params.ch[0].band.frequency = 500.0 + 4000.0 * pos / n;
        rig.engine.setParams (rig.params);
        const double* ins[2] = { in.data() + pos, in.data() + pos };
        double* outs[2] = { outL.data() + pos, outR.data() + pos };
        rig.engine.process<double> (ins, outs, nullptr, 2, 0, block);
    }
    const int L = rig.engine.latencySamples();
    double err = 0;
    for (int i = L; i < n; ++i) err = std::max (err, std::abs (outL[(size_t) i] - in[(size_t) (i - L)]));
    test::record ("null_during_sweep_dB", gainToDb (err));
    CHECK (err < 1e-12);
}

TEST (t15b_band_selective_touches_only_the_band)
{
    Rig rig;
    auto& c = rig.params.ch[0];
    c.band.type = FilterType::bandPass; c.band.frequency = 8000; c.band.bandwidthOct = 1.0;
    c.curve.thresholdDb = -40; c.curve.slope = 0.1; c.curve.knee = 0; c.times.attackMs = 1;
    c.times.releaseFastMs = 500; c.times.releaseSlowMs = 500;
    rig.apply();
    const int n = 48000;
    auto lo = sig::sine (n, 500, 48000, 0.25), hi = sig::sine (n, 8000, 48000, 0.25);
    std::vector<double> mix ((size_t) n);
    for (int i = 0; i < n; ++i) mix[(size_t) i] = lo[(size_t) i] + hi[(size_t) i];
    auto r = mix;
    rig.run (mix, r);
    std::vector<double> residual ((size_t) n);
    for (int i = 0; i < n; ++i) residual[(size_t) i] = mix[(size_t) i] - lo[(size_t) i];
    const double hiOut = sig::rms (residual, 30000, 46000);
    const double hiIn = sig::rms (hi, 30000, 46000);
    test::record ("band_selective_reduction_dB", 20 * std::log10 (hiOut / hiIn));
    CHECK (hiOut < hiIn * 0.2);  // the 8 kHz tone is pulled down hard, the 500 Hz tone stays
}

TEST (t16_parallel_alignment)
{
    // With compression neutral, parallel = dry + dry = +6.02 dB, which only happens if aligned.
    Rig rig;
    rig.params.ch[0].parallel = true;
    rig.apply();
    auto l = sig::pink (48000, 0.3), r = l;
    const auto d = l;
    rig.run (l, r);
    double err = 0;
    for (size_t i = 0; i < d.size(); ++i) err = std::max (err, std::abs (l[i] - 2.0 * d[i]));
    CHECK (err < 1e-14);

    // Make-up sets the compressed contribution.
    Rig mk;
    mk.params.ch[0].parallel = true;
    mk.params.ch[0].curve.makeupDb = -20;
    mk.apply();
    auto a = sig::sine (48000, 1000, 48000, 0.1), b = a;
    const auto d2 = a;
    mk.run (a, b);
    NEAR (sig::rms (a, 2000, 40000) / sig::rms (d2, 2000, 40000), 1.1, 1e-4);

    // Band parallel adds the filtered band to the full dry signal, also phase aligned.
    Rig bp;
    bp.params.ch[0].parallel = true;
    bp.params.ch[0].band.type = FilterType::bandPass;
    bp.params.ch[0].band.frequency = 1000;
    bp.params.ch[0].band.bandwidthOct = 2;
    bp.apply();
    auto s = sig::sine (48000, 1000, 48000, 0.1), t = s;
    bp.run (s, t);
    NEAR (sig::rms (s, 6000, 40000) / (0.1 / std::sqrt (2.0)), 2.0, 1e-3);
}

TEST (t16c_parallel_per_channel)
{
    // Unganged M/S with Parallel on Side only: Side doubles, Mid is untouched.
    auto run = [] (bool sideSignal)
    {
        Rig rig;
        rig.params.midSide = true;
        rig.params.ganged = false;
        rig.params.link = false;
        rig.params.ch[1].parallel = true;
        rig.apply();
        auto a = sig::sine (24000, 500, 48000, 0.2), b = a;
        if (sideSignal)
            for (auto& v : b) v = -v;
        const double in = sig::rms (a, 6000, 20000);
        rig.run (a, b);
        return sig::rms (a, 6000, 20000) / in;
    };
    NEAR (run (false), 1.0, 1e-12);
    NEAR (run (true), 2.0, 1e-6);

    // Ganged: Channel 1 decides for both.
    Rig g;
    g.params.ch[0].parallel = true;
    g.apply();
    auto l = sig::sine (24000, 500, 48000, 0.1), r = sig::sine (24000, 700, 48000, 0.1);
    const double inR = sig::rms (r, 6000, 20000);
    g.run (l, r);
    NEAR (sig::rms (r, 6000, 20000) / inR, 2.0, 1e-6);
}

TEST (t16d_release_indicators_follow_the_comparator)
{
    // Largest reading of a lamp after the burst ends, while the release runs.
    auto activity = [] (double burstMs, bool wantFast)
    {
        Rig rig;
        auto& c = rig.params.ch[0];
        c.curve.thresholdDb = -30; c.curve.slope = 0.25; c.curve.knee = 0;
        c.times.attackMs = 0.5; c.times.releaseFastMs = 30; c.times.releaseSlowMs = 2000; c.times.averageMs = 300;
        c.previewMs = 0;
        rig.apply();
        const int start = 24000, burst = (int) (burstMs * 48), end = start + burst;
        const int total = end + rig.engine.latencySamples() + 9600;
        auto l = sig::sine (total, 1000, 48000, dbToGain (-50));
        for (int i = start; i < end; ++i) l[(size_t) i] *= 300.0;
        float best = 0.0f;
        for (int pos = 0; pos < total; pos += 64)
        {
            const int n = std::min (64, total - pos);
            const double* ins[2] = { l.data() + pos, l.data() + pos };
            std::vector<double> o0 ((size_t) n), o1 ((size_t) n);
            double* outs[2] = { o0.data(), o1.data() };
            rig.engine.setParams (rig.params);
            rig.engine.process<double> (ins, outs, nullptr, 2, 0, n);
            if (pos > end)
            {
                auto& m = rig.engine.meters.channels[0];
                best = std::max (best, wantFast ? m.releaseFast.load() : m.releaseSlow.load());
            }
        }
        return best;
    };
    const float fast = activity (2, true), slow = activity (800, false);
    test::record ("release_lamp_fast_after_short_burst", fast);
    test::record ("release_lamp_slow_after_long_burst", slow);
    CHECK (fast > 0.2f);     // short burst: Release Fast is acting
    CHECK (slow > 0.2f);     // long burst: Release Slow carries the recovery
}

TEST (t17_stereo_linking)
{
    auto run = [] (bool link)
    {
        Rig rig;
        rig.params.link = link;
        rig.params.ch[0].curve.thresholdDb = -30;
        rig.params.ch[0].curve.slope = 0.25;
        rig.params.ch[0].curve.knee = 0;
        rig.apply();
        auto l = sig::sine (48000, 1000, 48000, 0.5), r = sig::sine (48000, 1000, 48000, 0.01);
        const double inR = sig::rms (r, 20000, 40000);
        rig.run (l, r);
        return 20 * std::log10 (sig::rms (r, 20000, 40000) / inR);
    };
    CHECK (run (true) < -10);        // quiet side follows the loud side
    NEAR (run (false), 0, 0.05);     // quiet side is below threshold on its own
}

TEST (t18_midside_round_trip)
{
    Rig rig;
    rig.params.midSide = true;
    rig.apply();
    auto l = sig::pink (48000, 0.8), r = sig::noise (48000, 0.3);
    const auto dl = l, dr = r;
    rig.run (l, r);
    double err = 0;
    for (size_t i = 0; i < dl.size(); ++i) err = std::max ({ err, std::abs (l[i] - dl[i]), std::abs (r[i] - dr[i]) });
    test::record ("ms_round_trip_error_dB", gainToDb (err));
    CHECK (err < 1e-14);

    // Independent M and S settings: only Side is compressed, so mono content is untouched.
    Rig ms;
    ms.params.midSide = true;
    ms.params.ganged = false;
    ms.params.link = false;
    ms.params.ch[1].curve.thresholdDb = -40;
    ms.params.ch[1].curve.slope = 0.1;
    ms.params.ch[1].curve.knee = 0;
    ms.apply();
    auto a = sig::sine (48000, 500, 48000, 0.4), b = a;
    const auto d = a;
    ms.run (a, b);
    err = 0;
    for (size_t i = 0; i < d.size(); ++i) err = std::max (err, std::abs (a[i] - d[i]));
    CHECK (err < 1e-14);
}

TEST (t19_true_peak_limiting)
{
    const double ceilingDb = -1.0;
    // fs/4 at 45 degrees: samples at 0.707, true peak 3 dB higher. The classic intersample case.
    for (auto type : { LimiterType::type2 })
        for (int variant = 0; variant < 3; ++variant)
        {
            Rig rig;
            rig.params.limiterOn = true;
            rig.params.limiterType = type;
            rig.params.ceilingDb = ceilingDb;
            rig.apply();
            std::vector<double> l;
            if (variant == 0) l = sig::sine (48000, 12000, 48000, 1.0, kPi / 4);
            if (variant == 1) l = sig::sine (48000, 11025.7, 48000, 0.95, 0.3);
            if (variant == 2) l = sig::percussive (48000, 48000, 1.8);
            auto r = l;
            rig.run (l, r);
            const double tp = gainToDb (sig::truePeak16 (l, 2000, 46000));
            test::record ("tp_type2_variant" + std::to_string (variant) + "_dBTP", tp);
            CHECK (tp <= ceilingDb + 0.05);
        }

    // Sample-peak limiters hold the ceiling on samples.
    for (auto type : { LimiterType::safe, LimiterType::type1 })
    {
        Rig rig;
        rig.params.limiterOn = true;
        rig.params.limiterType = type;
        rig.params.ceilingDb = ceilingDb;
        rig.params.preLimiterDb = 12;
        rig.apply();
        auto l = sig::percussive (48000, 48000, 1.0), r = sig::pink (48000, 1.0);
        rig.run (l, r);
        CHECK (gainToDb (sig::peak (l, 100, 47000)) <= ceilingDb + 1e-6);
        CHECK (gainToDb (sig::peak (r, 100, 47000)) <= ceilingDb + 1e-6);
    }

    // Below the ceiling every limiter is bit-transparent.
    for (auto type : { LimiterType::safe, LimiterType::type1, LimiterType::type2 })
    {
        Rig rig;
        rig.params.limiterOn = true;
        rig.params.limiterType = type;
        rig.params.ceilingDb = 0;
        rig.apply();
        auto l = sig::sine (48000, 997, 48000, 0.5), r = l;
        const auto d = l;
        rig.run (l, r);
        double err = 0;
        for (size_t i = 0; i < d.size(); ++i) err = std::max (err, std::abs (l[i] - d[i]));
        CHECK (err == 0.0);
    }
}

TEST (t20_dither)
{
    for (int bits : { 16, 20, 24 })
    {
        Dither d;
        d.reset (7);
        const double lsb = std::ldexp (1.0, 1 - bits);
        const int n = 2000000;
        double sum = 0, sq = 0;
        const double x = 0.123456789 * lsb;
        bool onGrid = true;
        for (int i = 0; i < n; ++i)
        {
            const double y = d.process (x, bits);
            const double e = (y - x) / lsb;
            sum += e; sq += e * e;
            const double k = y / lsb;
            onGrid = onGrid && k == std::round (k);
        }
        CHECK (onGrid);
        NEAR (sum / n, 0.0, 0.003);          // no DC error: quantisation linearised
        NEAR (sq / n, 0.25, 0.01);           // TPDF + quantisation = 3 LSB^2/12
    }

    // A tone 10 dB below one LSB survives dither. Plain rounding would erase it.
    Dither d;
    d.reset (1);
    const double lsb = std::ldexp (1.0, -15), a = lsb * 0.316;
    double corr = 0;
    const int n = 4800000;
    for (int i = 0; i < n; ++i)
    {
        const double s = std::sin (2 * kPi * 1000.0 * i / 48000.0);
        corr += d.process (a * s, 16) * s;
    }
    NEAR (2 * corr / n, a, a * 0.03);

    d.reset (2);
    for (int i = 0; i < 100; ++i) d.process (0.0, 16);
    CHECK (d.process (0.0, 16) == 0.0);  // auto-black
}

// Error spectrum of a shaped dither, in dB per band relative to flat TPDF at the same word length.
static double bandDbAgainstTpdf (double sr, int shape, double lo, double hi)
{
    NoiseShaper shapes;
    shapes.prepare (sr);
    constexpr int size = 1 << 15, frames = 40;
    FFT fft;
    fft.prepare (size);
    double powerShaped = 0, powerFlat = 0;
    Dither a, b;
    a.reset (11);
    b.reset (12);
    const double lsb = std::ldexp (1.0, -15);
    std::vector<FFT::Complex> s ((size_t) size), f ((size_t) size);
    for (int fr = 0; fr < frames; ++fr)
    {
        for (int i = 0; i < size; ++i)
        {
            const double x = 0.31 * lsb, win = 0.5 - 0.5 * std::cos (2 * kPi * i / size);
            s[(size_t) i] = (a.processShaped (x, 16, shapes.get (shape)) - x) * win;
            f[(size_t) i] = (b.process (x, 16) - x) * win;
        }
        fft.forward (s.data());
        fft.forward (f.data());
        for (int k = (int) (lo / sr * size); k < (int) (hi / sr * size); ++k)
        {
            powerShaped += std::norm (s[(size_t) k]);
            powerFlat += std::norm (f[(size_t) k]);
        }
    }
    return 10 * std::log10 (powerShaped / powerFlat);
}

// Level of one frequency relative to full scale, by correlation over a Hann window.
static double toneDb (const std::vector<double>& v, int from, int to, double f, double sr)
{
    double re = 0, im = 0, w = 0;
    for (int i = from; i < to; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2 * kPi * (i - from) / (to - from));
        re += v[(size_t) i] * win * std::cos (2 * kPi * f * i / sr);
        im += v[(size_t) i] * win * std::sin (2 * kPi * f * i / sr);
        w += win;
    }
    return gainToDb (2 * std::sqrt (re * re + im * im) / w);
}

TEST (t16b_oversampling)
{
    CHECK (Rig (44100).engine.oversamplingFactor() == 4);
    CHECK (Rig (48000).engine.oversamplingFactor() == 4);
    CHECK (Rig (96000).engine.oversamplingFactor() == 2);
    CHECK (Rig (192000).engine.oversamplingFactor() == 1);

    // Fast compression of a high tone: envelope ripple at 2f multiplies the tone. Without
    // oversampling the 3f product folds back to 57.0 - 44.1 = 12.9 kHz.
    auto aliasDb = [] (int oversampling)
    {
        const double sr = 44100, f = 19000;
        Rig rig (sr, false, oversampling);
        auto& c = rig.params.ch[0];
        c.curve.thresholdDb = -30; c.curve.slope = 0.1; c.curve.knee = 0;
        c.times.attackMs = 0.02; c.times.releaseFastMs = 1; c.times.releaseSlowMs = 1;
        rig.apply();
        auto l = sig::sine (88200, f, sr, 0.5), r = l;
        rig.run (l, r);
        return toneDb (l, 22050, 66150, 3 * f - sr, sr) - toneDb (l, 22050, 66150, f, sr);
    };
    const double without = aliasDb (1), with = aliasDb (0);
    test::record ("alias_12k9_no_oversampling_dBc", without);
    test::record ("alias_12k9_oversampled_dBc", with);
    CHECK (with < without - 20.0);

    // Static level at 10 kHz stays correct through the oversampled gain path.
    Rig rig (44100);
    auto& c = rig.params.ch[0];
    c.curve.thresholdDb = -20; c.curve.slope = 0.25; c.curve.knee = 0;
    c.times.attackMs = 1; c.times.releaseDelayMs = 100; c.times.releaseFastMs = 2000; c.times.releaseSlowMs = 2000;
    rig.apply();
    auto l = sig::sine (44100, 10000, 44100, dbToGain (-8)), r = l;
    rig.run (l, r);
    NEAR (toneDb (l, 22050, 40000, 10000, 44100), -17.0, 0.1);
}

TEST (t20b_noise_shaped_dither)
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        NoiseShaper shapes;
        shapes.prepare (sr);
        for (int shape = 1; shape <= 3; ++shape)
        {
            const auto& f = shapes.get (shape);
            CHECK (f.taps[0] == 1.0);
            CHECK (f.order > 0 && f.order <= NoiseShaper::kMaxOrder);

            // Output stays on the word-length grid and tracks the input with bounded error.
            Dither d;
            d.reset (3);
            const double lsb = std::ldexp (1.0, -15);
            bool onGrid = true;
            double worst = 0, mean = 0;
            const int n = 400000;
            for (int i = 0; i < n; ++i)
            {
                const double x = 0.4 * std::sin (2 * kPi * 997.0 * i / sr);
                const double y = d.processShaped (x, 16, f);
                const double k = y / lsb;
                onGrid = onGrid && k == std::round (k);
                worst = std::max (worst, std::abs (y - x) / lsb);
                mean += y - x;
            }
            CHECK (onGrid);
            CHECK (worst < 16.0);
            NEAR (mean / n / lsb, 0.0, 0.02);

            // Full-scale input hits the rails without the feedback path running away.
            d.reset (4);
            for (int i = 0; i < 200000; ++i)
            {
                const double x = 1.2 * std::sin (2 * kPi * 100.0 * i / sr);
                const double y = d.processShaped (x, 16, f);
                CHECK (std::isfinite (y) && std::abs (y) <= 1.0);
            }
        }
    }

    // A tone 10 dB below one LSB survives.
    NoiseShaper shapes;
    shapes.prepare (44100.0);
    for (int shape = 1; shape <= 3; ++shape)
    {
        Dither d;
        d.reset (5);
        const double lsb = std::ldexp (1.0, -15), a = lsb * 0.316;
        double corr = 0;
        const int n = 2000000;
        for (int i = 0; i < n; ++i)
        {
            const double s = std::sin (2 * kPi * 1000.0 * i / 44100.0);
            corr += d.processShaped (a * s, 16, shapes.get (shape)) * s;
        }
        NEAR (2 * corr / n, a, a * 0.06);
    }

    // Auto-black also clears the feedback path.
    Dither d;
    d.reset (6);
    for (int i = 0; i < 1000; ++i) d.processShaped (0.001, 16, shapes.get (3));
    for (int i = 0; i < 100; ++i) d.processShaped (0.0, 16, shapes.get (3));
    CHECK (d.processShaped (0.0, 16, shapes.get (3)) == 0.0);

    // The curves do what each one is for, at 44.1 kHz.
    for (int shape = 1; shape <= 3; ++shape)
    {
        const double audible = bandDbAgainstTpdf (44100, shape, 2000, 5000);
        const double top = bandDbAgainstTpdf (44100, shape, 20000, 22050);
        test::record ("dither_shape" + std::to_string (shape) + "_2k_5k_dB_vs_tpdf", audible);
        test::record ("dither_shape" + std::to_string (shape) + "_20k_22k_dB_vs_tpdf", top);
        CHECK (top > audible + 6.0);
    }
    const double flatLow = bandDbAgainstTpdf (44100, 1, 200, 2000), flatHigh = bandDbAgainstTpdf (44100, 1, 8000, 14000);
    test::record ("dither_shape1_flatness_dB", flatLow - flatHigh);
    NEAR (flatLow, flatHigh, 1.5);                                 // Shape 1 is flat in the audible band
    CHECK (bandDbAgainstTpdf (44100, 3, 2000, 5000) < bandDbAgainstTpdf (44100, 2, 2000, 5000) - 2.0);
    CHECK (bandDbAgainstTpdf (44100, 2, 2000, 5000) < -3.0);
}

TEST (t21_silence_denormals_and_invalid_input)
{
    Rig rig;
    auto& c = rig.params.ch[0];
    c.curve.thresholdDb = -30; c.curve.slope = 0.5;
    c.times.rmsDetectMs = 50;
    rig.params.limiterOn = true;
    rig.apply();

    std::vector<double> l (96000, 0.0), r (96000, 0.0);
    rig.run (l, r);
    CHECK (sig::peak (l, 0, 96000) == 0.0);

    // Tail decays with no denormals left in the state or the output.
    auto a = sig::sine (96000, 500, 48000, 0.5);
    for (int i = 4800; i < 96000; ++i) a[(size_t) i] = 0.0;
    auto b = a;
    rig.run (a, b);
    CHECK (std::fpclassify (a.back()) != FP_SUBNORMAL);
    CHECK (a.back() == 0.0);

    // NaN and infinity never reach the output, for extreme settings.
    for (double slope : { 0.001, 5.0 })
    {
        Rig x;
        x.params.ch[0].curve.thresholdDb = -60; x.params.ch[0].curve.slope = slope; x.params.ch[0].curve.knee = 1;
        x.params.ch[0].curve.autoMakeup = true;
        x.apply();
        auto p = sig::noise (24000, 1.0);
        p[1000] = std::numeric_limits<double>::quiet_NaN();
        p[2000] = std::numeric_limits<double>::infinity();
        auto q = p;
        x.run (p, q);
        bool finite = true;
        for (double v : p) finite = finite && std::isfinite (v);
        CHECK (finite);
    }
}

TEST (t22_automation_stability_no_clicks)
{
    // Sweeping every smooth control on a low tone must not create steps in the output.
    Rig rig;
    auto& c = rig.params.ch[0];
    c.curve.thresholdDb = -30; c.curve.slope = 0.5; c.times.attackMs = 5; c.times.releaseFastMs = 100;
    rig.apply();
    const int n = 96000, block = 64, L = rig.engine.latencySamples();
    auto in = sig::sine (n + L, 110, 48000, 0.5);
    std::vector<double> out (in.size()), outR (in.size());
    std::mt19937 g (5);
    std::uniform_real_distribution<double> u (0, 1);
    for (int pos = 0; pos + block <= (int) in.size(); pos += block)
    {
        if (pos % 4800 == 0)
        {
            c.curve.thresholdDb = -60 * u (g);
            c.curve.slope = 0.1 + u (g);
            c.curve.knee = u (g);
            c.curve.makeupDb = 20 * u (g);
            c.previewMs = 10 * u (g);
            rig.params.inputDb = -6 + 12 * u (g);
            rig.params.outputDb = -6 + 12 * u (g);
        }
        rig.engine.setParams (rig.params);
        const double* ins[2] = { in.data() + pos, in.data() + pos };
        double* outs[2] = { out.data() + pos, outR.data() + pos };
        rig.engine.process<double> (ins, outs, nullptr, 2, 0, block);
    }
    // A step or corner shows as a large second difference. A clean 110 Hz tone at full scale gives 2e-4.
    double worstBend = 0, peak = 0;
    const size_t end = (in.size() / block) * block;
    for (size_t i = L + 2; i < end; ++i)
    {
        worstBend = std::max (worstBend, std::abs (out[i] - 2 * out[i - 1] + out[i - 2]));
        peak = std::max (peak, std::abs (out[i]));
        CHECK (std::isfinite (out[i]));
    }
    test::record ("automation_worst_second_difference_rel", worstBend / peak);
    CHECK (worstBend / peak < 0.01);

    // Mode switches are faded: no step larger than the signal's own slope allows.
    Rig m;
    m.apply();
    const int L2 = m.engine.latencySamples();
    auto s = sig::sine (96000 + L2, 200, 48000, 0.5);
    std::vector<double> o (s.size()), o2 (s.size());
    for (int pos = 0; pos + block <= (int) s.size(); pos += block)
    {
        if (pos == 24000 || pos == 64000) { m.params.midSide = ! m.params.midSide; }
        if (pos == 40000 || pos == 72000) { m.params.ch[0].parallel = ! m.params.ch[0].parallel; }
        if (pos == 48000) m.params.limiterOn = true;
        if (pos == 80000) m.params.limiterOn = false;
        m.engine.setParams (m.params);
        const double* ins[2] = { s.data() + pos, s.data() + pos };
        double* outs[2] = { o.data() + pos, o2.data() + pos };
        m.engine.process<double> (ins, outs, nullptr, 2, 0, block);
    }
    double step = 0;
    const size_t end2 = (s.size() / block) * block;
    for (size_t i = 1; i < end2; ++i) step = std::max (step, std::abs (o[i] - o[i - 1]));
    test::record ("mode_switch_max_sample_step", step);
    CHECK (step < 0.06);
}

TEST (t23_sample_rates)
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        Rig rig (sr);
        auto& c = rig.params.ch[0];
        c.curve.thresholdDb = -20; c.curve.slope = 0.5; c.curve.knee = 0;
        c.times.attackMs = 1; c.times.releaseDelayMs = 100; c.times.releaseFastMs = 1000; c.times.releaseSlowMs = 1000;
        rig.apply();
        const int n = (int) sr;
        auto l = sig::sine (n, 1000, sr, 0.1), r = l;   // -20 dBFS: at threshold, then step up
        for (auto& v : l) v *= 3.16227766;
        r = l;
        rig.run (l, r);
        NEAR (20 * std::log10 (sig::rms (l, (int) (0.7 * sr), (int) (0.95 * sr)) * std::sqrt (2.0)), -15, 0.05);
        test::record ("latency_ms_" + std::to_string ((int) sr), rig.engine.latencySamples() * 1000.0 / sr);
        // Re-prepare at another rate, as a host does when the session rate changes.
        rig.engine.prepare (sr == 48000.0 ? 96000.0 : 48000.0, false);
        CHECK (rig.engine.latencySamples() > 0);
    }
}

TEST (t24_variable_block_sizes_are_bit_identical)
{
    auto render = [] (std::function<int(int)> nextBlock)
    {
        Rig rig;
        auto& c = rig.params.ch[0];
        c.curve.thresholdDb = -24; c.curve.slope = 0.4; c.band.type = FilterType::bandPass; c.band.frequency = 2000;
        rig.params.limiterOn = true; rig.params.limiterType = LimiterType::type2; rig.params.ditherBits = 16; rig.params.ditherShape = 3;
        rig.params.preLimiterDb = 9;
        rig.apply();
        auto in = sig::pink (96000, 0.9);
        std::vector<double> outL (in.size()), outR (in.size());
        int pos = 0, idx = 0;
        while (pos < (int) in.size())
        {
            const int n = std::min (nextBlock (idx++), (int) in.size() - pos);
            rig.engine.setParams (rig.params);
            const double* ins[2] = { in.data() + pos, in.data() + pos };
            double* outs[2] = { outL.data() + pos, outR.data() + pos };
            rig.engine.process<double> (ins, outs, nullptr, 2, 0, n);
            pos += n;
        }
        return outL;
    };
    const auto a = render ([] (int) { return 512; });
    const auto b = render ([] (int) { return 1; });
    const auto c = render ([] (int i) { return 1 + (i * 37) % 1024; });
    CHECK (a == b);
    CHECK (a == c);
}

TEST (t25_offline_render_is_deterministic)
{
    auto render = []
    {
        Rig rig;
        rig.params.ch[0].curve.thresholdDb = -24;
        rig.params.ditherBits = 24;
        rig.params.limiterOn = true;
        rig.apply();
        auto l = sig::pink (48000, 0.9), r = sig::noise (48000, 0.5);
        rig.run (l, r);
        l.insert (l.end(), r.begin(), r.end());
        return l;
    };
    CHECK (render() == render());

    // reset() returns the engine to the same state, as hosts do before a bounce.
    Rig rig;
    rig.params.ch[0].curve.thresholdDb = -24;
    rig.params.ditherBits = 16;
    rig.apply();
    auto a = sig::pink (24000, 0.7), a2 = a;
    rig.run (a, a2);
    rig.engine.reset();
    rig.engine.setParamsImmediately (rig.params);
    auto b = sig::pink (24000, 0.7), b2 = b;
    rig.run (b, b2);
    CHECK (a == b);
}

TEST (t27_external_sidechain_and_monitor)
{
    // External key drives gain reduction of an otherwise quiet programme.
    Rig rig;
    rig.params.externalSidechain = true;
    rig.params.ch[0].curve.thresholdDb = -30; rig.params.ch[0].curve.slope = 0.25; rig.params.ch[0].curve.knee = 0;
    rig.apply();
    auto l = sig::sine (48000, 1000, 48000, 0.01), r = l;
    const auto d = l;
    const auto key = sig::sine (48000, 300, 48000, 0.8);
    rig.run (l, r, 512, true, &key);
    CHECK (sig::rms (l, 20000, 40000) < sig::rms (d, 20000, 40000) * 0.3);

    // Monitor: band mode outputs only the active band.
    Rig mon;
    mon.params.monitor = true;
    mon.params.ch[0].band.type = FilterType::highPass;
    mon.params.ch[0].band.frequency = 4000;
    mon.apply();
    auto low = sig::sine (48000, 200, 48000, 0.5), low2 = low;
    mon.run (low, low2);
    CHECK (sig::rms (low, 8000, 40000) < 1e-3);

    // Monitor unganged: the selected channel goes to both outputs, including Side in M/S.
    Rig solo;
    solo.params.midSide = true; solo.params.ganged = false; solo.params.monitor = true; solo.params.selectedChannel = 1;
    solo.apply();
    auto a = sig::sine (48000, 500, 48000, 0.5), b = a;
    for (auto& v : b) v = -v;          // pure side signal
    const auto d2 = a;
    solo.run (a, b);
    NEAR (sig::rms (a, 8000, 40000), sig::rms (d2, 8000, 40000) * std::sqrt (2.0), 1e-9);
    NEAR (sig::rms (b, 8000, 40000), sig::rms (a, 8000, 40000), 1e-12);
}

TEST (t28_detector_reference_signals)
{
    // Impulse: peak detection sees the full level for a one-sample input.
    EnvelopeFollower e;
    EnvelopeTimes t; t.attackMs = 0.02; t.releaseFastMs = 100;
    e.configure (t, 48000);
    const double firstSample = e.process (0.5, 0.0);   // one call: NEAR evaluates its arguments twice
    NEAR (firstSample, 0.5 * (1.0 - std::exp (-1.0 / (0.02e-3 * 48000.0 * EnvelopeFollower::kTimeScale))), 1e-12);

    // RMS calibration on reference signals.
    RMSDetector r;
    r.setTime (0.1, 48000);
    double v = 0;
    for (auto s : sig::sine (96000, 1000, 48000, 1.0)) v = r.process (s);
    NEAR (v, 1 / std::sqrt (2.0), 0.002);
    r.reset();
    for (auto s : sig::noise (240000, 1.0)) v = r.process (s);
    NEAR (v, 1 / std::sqrt (3.0), 0.03);
    r.reset();
    for (int i = 0; i < 192000; ++i) v = r.process (0.25);
    NEAR (v, 0.25, 1e-6);          // DC step settles at its level
    for (int i = 0; i < 480000; ++i) v = r.process (0.0);
    CHECK (v == 0.0);              // silence reaches true zero
    
    // Crest factor separates material: pink and percussion peak well above their average.
    for (auto& s : { sig::pink (96000, 1.0), sig::percussive (96000, 48000, 1.0) })
        CHECK (sig::peak (s, 0, 96000) / sig::rms (s, 0, 96000) > 2.5);
}
