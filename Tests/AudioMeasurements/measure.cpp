// Offline measurement utility. Writes CSV files that plot_measurements.py turns into graphs.
#include "../Unit/TestHarness.h"
#include <chrono>

using namespace dsq;

static FILE* open (const std::string& dir, const char* name, const char* header)
{
    FILE* f = std::fopen ((dir + "/" + name).c_str(), "w");
    std::fprintf (f, "%s\n", header);
    return f;
}

static double staticOut (TransferParams c, double inDb)
{
    Rig rig;
    rig.params.ch[0].curve = c;
    rig.params.ch[0].times.attackMs = 1;
    rig.params.ch[0].times.releaseDelayMs = 200;
    rig.params.ch[0].times.releaseFastMs = 2000;
    rig.params.ch[0].times.releaseSlowMs = 2000;
    rig.apply();
    auto l = sig::sine (24000, 1000, 48000, dbToGain (inDb)), r = l;
    rig.run (l, r);
    return 20 * std::log10 (sig::rms (l, 16000, 23000) * std::sqrt (2.0) + 1e-12);
}

static void staticCurves (const std::string& dir)
{
    FILE* f = open (dir, "static_curves.csv", "ratio,knee,input_db,measured_db,computed_db");
    struct Case { double slope, knee; } cases[] = { { 0.5, 0 }, { 0.25, 0 }, { 0.001, 0 }, { 0.25, 0.5 }, { 0.25, 1.0 }, { 2.0, 0 } };
    for (auto& k : cases)
    {
        TransferParams c;
        c.thresholdDb = -24; c.slope = k.slope; c.knee = k.knee;
        for (double in = -48; in <= 0.01; in += 3)
            std::fprintf (f, "%g,%g,%g,%.4f,%.4f\n", 1.0 / k.slope, k.knee, in, staticOut (c, in), transferOutputDb (in, c));
    }
    std::fclose (f);
}

static void burstResponses (const std::string& dir)
{
    FILE* f = open (dir, "burst_response.csv", "burst_ms,average_ms,t_ms,gain_db");
    struct Case { double burst, avg; } cases[] = { { 5, 300 }, { 50, 300 }, { 200, 300 }, { 1000, 300 }, { 1000, 20 } };
    for (auto& k : cases)
    {
        const double sr = 48000;
        EnvelopeFollower e;
        RMSDetector a;
        EnvelopeTimes t;
        t.attackMs = 0.5; t.releaseFastMs = 30; t.releaseSlowMs = 700; t.averageMs = k.avg;
        e.configure (t, sr);
        a.setTime (k.avg * 0.0005, sr);
        TransferParams c; c.thresholdDb = -30; c.slope = 0.25; c.knee = 0;
        const auto background = sig::sine ((int) (3 * sr), 1000, sr, dbToGain (-40));
        for (int i = 0; i < (int) (3 * sr); ++i)
        {
            const double ms = i / 48.0 - 500;
            const double x = background[(size_t) i] * (ms >= 0 && ms < k.burst ? 31.6 : 1.0);
            a.process (x);
            const double env = e.process (std::abs (x), a.rms() * std::sqrt (2.0));
            if (i % 48 == 0)
                std::fprintf (f, "%g,%g,%.1f,%.3f\n", k.burst, k.avg, ms - k.burst, curveGainDb (gainToDb (env), c));
        }
    }
    std::fclose (f);
}

static void crossover (const std::string& dir)
{
    FILE* f = open (dir, "crossover.csv", "mode,freq,band_db,complement_db");
    const double sr = 48000;
    struct Case { const char* name; FilterType t; double f, bw; bool ll; } cases[] = {
        { "Band Pass 3.2 kHz 1 oct", FilterType::bandPass, 3200, 1, false },
        { "Low Pass 200 Hz", FilterType::lowPass, 200, 1, false },
        { "High Pass 8 kHz", FilterType::highPass, 8000, 1, false },
        { "Low Latency Band Pass 3.2 kHz", FilterType::bandPass, 3200, 1, true } };
    for (auto& k : cases)
    {
        const auto cfg = LinearPhaseCrossover::configFor (sr, k.ll);
        LinearPhaseCrossover x;
        x.prepare (sr, cfg);
        BandSpec s; s.type = k.t; s.frequency = k.f; s.bandwidthOct = k.bw;
        x.setSpecImmediately (s);
        constexpr int size = 1 << 17;
        std::vector<FFT::Complex> band ((size_t) size), comp ((size_t) size);
        for (int i = 0; i < size; ++i)
        {
            const double in = i == 0 ? 1.0 : 0.0;
            const double y = x.process (in);
            band[(size_t) i] = y;
            comp[(size_t) i] = (i == x.latency() ? 1.0 : 0.0) - y;
        }
        FFT fft; fft.prepare (size);
        fft.forward (band.data());
        fft.forward (comp.data());
        for (int i = 0; i < 400; ++i)
        {
            const double fr = 20.0 * std::pow (1100.0, i / 399.0);
            const int bin = (int) (fr / sr * size + 0.5);
            std::fprintf (f, "%s,%.2f,%.3f,%.3f\n", k.name, fr, gainToDb (std::abs (band[(size_t) bin])), gainToDb (std::abs (comp[(size_t) bin])));
        }
    }
    std::fclose (f);
}

static void truePeak (const std::string& dir)
{
    FILE* f = open (dir, "true_peak.csv", "signal,limiter,input_dbtp,input_sample_db,output_dbtp,output_sample_db");
    const char* names[] = { "fs/4 at 45 degrees", "11.03 kHz tone", "Percussive noise, +5 dB" };
    const char* types[] = { "Safe", "Type 1", "Type 2 (TP)" };
    for (int v = 0; v < 3; ++v)
        for (int t = 0; t < 3; ++t)
        {
            Rig rig;
            rig.params.limiterOn = true;
            rig.params.limiterType = (LimiterType) t;
            rig.params.ceilingDb = -1;
            rig.apply();
            std::vector<double> l = v == 0 ? sig::sine (48000, 12000, 48000, 1.0, kPi / 4) : v == 1 ? sig::sine (48000, 11025.7, 48000, 0.95, 0.3) : sig::percussive (48000, 48000, 1.8);
            const auto in = l;
            auto r = l;
            rig.run (l, r);
            std::fprintf (f, "%s,%s,%.3f,%.3f,%.3f,%.3f\n", names[v], types[t], gainToDb (sig::truePeak16 (in, 2000, 46000)), gainToDb (sig::peak (in, 2000, 46000)),
                          gainToDb (sig::truePeak16 (l, 2000, 46000)), gainToDb (sig::peak (l, 2000, 46000)));
        }
    std::fclose (f);
}

static void cpuAndLatency (const std::string& dir)
{
    FILE* f = open (dir, "cpu_latency.csv", "configuration,sample_rate,latency_samples,latency_ms,realtime_percent_one_core");
    struct Case { const char* name; double sr; bool ll, band, tp; } cases[] = {
        { "Full band, no limiter", 48000, false, false, false },
        { "Band, Type 2 limiter", 48000, false, true, true },
        { "Band, Type 2 limiter", 96000, false, true, true },
        { "Band, Type 2 limiter", 192000, false, true, true },
        { "Low Latency, full band", 48000, true, false, false },
        { "Low Latency, band, Type 2", 48000, true, true, true } };
    for (auto& k : cases)
    {
        Rig rig (k.sr, k.ll);
        auto& c = rig.params.ch[0];
        c.curve.thresholdDb = -20; c.curve.slope = 0.25;
        if (k.band) c.band.type = FilterType::bandPass;
        if (k.tp) { rig.params.limiterOn = true; rig.params.limiterType = LimiterType::type2; }
        rig.apply();
        const int n = (int) k.sr * 4;
        auto l = sig::pink (n, 0.9), r = sig::pink (n, 0.9, 7);
        const auto start = std::chrono::steady_clock::now();
        rig.run (l, r, 512);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        std::fprintf (f, "%s,%g,%d,%.2f,%.1f\n", k.name, k.sr, rig.engine.latencySamples(), rig.engine.latencySamples() * 1000.0 / k.sr, seconds / 4.0 * 100.0);
    }
    std::fclose (f);
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    staticCurves (dir);
    burstResponses (dir);
    crossover (dir);
    truePeak (dir);
    cpuAndLatency (dir);
    return 0;
}
