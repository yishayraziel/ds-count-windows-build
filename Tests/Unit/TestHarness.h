#pragma once
// Minimal test harness. No dependencies, so the DSP tests build anywhere in seconds.
#include "../../Source/DSP/Engine.h"
#include <cstdio>
#include <functional>
#include <map>
#include <random>
#include <string>

namespace test
{
struct Case { const char* name; void (*fn)(); };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
struct Reg { Reg (const char* n, void (*f)()) { cases().push_back ({ n, f }); } };

/** Values that end up in the measurement report. */
inline std::map<std::string, double>& results() { static std::map<std::string, double> r; return r; }
inline void record (const std::string& k, double v) { results()[k] = v; }

inline void check (bool ok, const char* expr, const char* file, int line, double a = 0, double b = 0)
{
    ++checks();
    if (! ok)
    {
        ++failures();
        std::printf ("  FAILED %s:%d  %s   (%.9g vs %.9g)\n", file, line, expr, a, b);
    }
}
} // namespace test

#define TEST(name) static void name(); static test::Reg reg_##name (#name, name); static void name()
#define CHECK(x) test::check ((x), #x, __FILE__, __LINE__)
#define NEAR(a, b, tol) test::check (std::abs ((double) (a) - (double) (b)) <= (double) (tol), #a " ~ " #b, __FILE__, __LINE__, (double) (a), (double) (b))

namespace sig
{
using namespace dsq;
inline std::vector<double> sine (int n, double f, double sr, double amp, double phase = 0.0)
{
    std::vector<double> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = amp * std::sin (2.0 * kPi * f * i / sr + phase);
    return v;
}
inline std::vector<double> noise (int n, double amp, unsigned seed = 1)
{
    std::mt19937 g (seed);
    std::uniform_real_distribution<double> u (-amp, amp);
    std::vector<double> v ((size_t) n);
    for (auto& s : v) s = u (g);
    return v;
}
/** Paul Kellet pink filter. */
inline std::vector<double> pink (int n, double amp, unsigned seed = 2)
{
    auto w = noise (n, 1.0, seed);
    double b0 = 0, b1 = 0, b2 = 0;
    std::vector<double> v ((size_t) n);
    double peak = 1e-9;
    for (int i = 0; i < n; ++i)
    {
        b0 = 0.99765 * b0 + w[(size_t) i] * 0.0990460;
        b1 = 0.96300 * b1 + w[(size_t) i] * 0.2965164;
        b2 = 0.57000 * b2 + w[(size_t) i] * 1.0526913;
        v[(size_t) i] = b0 + b1 + b2 + w[(size_t) i] * 0.1848;
        peak = std::max (peak, std::abs (v[(size_t) i]));
    }
    for (auto& s : v) s *= amp / peak;
    return v;
}
/** Synthetic percussive loop: decaying noise and tone hits on a grid. */
inline std::vector<double> percussive (int n, double sr, double amp)
{
    auto w = noise (n, 1.0, 3);
    std::vector<double> v ((size_t) n, 0.0);
    const int period = (int) (sr * 0.25);
    for (int i = 0; i < n; ++i)
    {
        const double t = (i % period) / sr;
        v[(size_t) i] = amp * std::exp (-t * 30.0) * (0.6 * std::sin (2.0 * kPi * 90.0 * t) + 0.4 * w[(size_t) i]);
    }
    return v;
}
inline double rms (const std::vector<double>& v, int from, int to)
{
    double s = 0;
    for (int i = from; i < to; ++i) s += v[(size_t) i] * v[(size_t) i];
    return std::sqrt (s / std::max (1, to - from));
}
inline double peak (const std::vector<double>& v, int from, int to)
{
    double p = 0;
    for (int i = from; i < to; ++i) p = std::max (p, std::abs (v[(size_t) i]));
    return p;
}
/** High quality reconstruction peak: 16x sinc interpolation with a long window. */
inline double truePeak16 (const std::vector<double>& v, int from, int to)
{
    double best = 0;
    constexpr int half = 32, os = 16;
    for (int i = std::max (from, half); i < std::min (to, (int) v.size() - half); ++i)
        for (int p = 0; p < os; ++p)
        {
            const double frac = (double) p / os;
            double s = 0;
            for (int k = -half + 1; k <= half; ++k)
            {
                const double t = k - frac;
                const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
                const double w = 0.5 + 0.5 * std::cos (kPi * t / (half + 1));
                s += v[(size_t) (i + k)] * sinc * w;
            }
            best = std::max (best, std::abs (s));
        }
    return best;
}
} // namespace sig

/** Runs a stereo signal through an Engine. Returns output with the host latency removed. */
struct Rig
{
    dsq::Engine engine;
    double sampleRate;
    dsq::EngineParams params;

    explicit Rig (double sr = 48000.0, bool lowLatency = false, int oversampling = 0) : sampleRate (sr) { engine.prepare (sr, lowLatency, oversampling); neutral(); }

    void neutral()
    {
        for (auto& c : params.ch)
        {
            c.curve.thresholdDb = 0.0;
            c.curve.slope = 1.0;
            c.curve.knee = 0.0;
            c.curve.makeupDb = 0.0;
            c.previewMs = 0.0;
        }
    }

    void apply() { engine.setParamsImmediately (params); }

    void run (std::vector<double>& l, std::vector<double>& r, int blockSize = 512, bool removeLatency = true,
              const std::vector<double>* scL = nullptr)
    {
        const int latency = removeLatency ? engine.latencySamples() : 0;
        const int total = (int) l.size();
        std::vector<double> inL (l), inR (r), outL ((size_t) total + (size_t) latency), outR ((size_t) total + (size_t) latency);
        inL.resize (outL.size(), 0.0);
        inR.resize (outR.size(), 0.0);
        std::vector<double> sc (outL.size(), 0.0);
        if (scL) std::copy (scL->begin(), scL->end(), sc.begin());
        for (int pos = 0; pos < (int) outL.size(); pos += blockSize)
        {
            const int n = std::min (blockSize, (int) outL.size() - pos);
            const double* ins[2] = { inL.data() + pos, inR.data() + pos };
            double* outs[2] = { outL.data() + pos, outR.data() + pos };
            const double* scs[2] = { sc.data() + pos, sc.data() + pos };
            engine.setParams (params);
            engine.process<double> (ins, outs, scL ? scs : nullptr, 2, scL ? 2 : 0, n);
        }
        l.assign (outL.begin() + latency, outL.end());
        r.assign (outR.begin() + latency, outR.end());
    }
};
