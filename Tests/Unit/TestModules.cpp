// Tests for the loudness meter and the signal chain, including band-selective de-essing.
#include "TestHarness.h"
#include "../../Source/DSP/Chain.h"
using namespace dsq;

namespace
{
std::vector<double> addTones (std::initializer_list<std::pair<double, double>> tones, int n, double sr)
{
    std::vector<double> v ((size_t) n, 0.0);
    for (auto [f, a] : tones)
        for (int i = 0; i < n; ++i)
            v[(size_t) i] += a * std::sin (2 * kPi * f * i / sr);
    return v;
}

double toneLevelDb (const std::vector<double>& v, int from, int to, double f, double sr)
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
} // namespace

// ---- Loudness ------------------------------------------------------------------------

TEST (m12_loudness_ebu_reference)
{
    // EBU Tech 3341 case: stereo 1 kHz sine at -23 dBFS reads -23.0 LUFS.
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        LoudnessMeter m;
        m.prepare (sr);
        const double a = dbToGain (-23.0);
        const int n = (int) (sr * 20);
        for (int i = 0; i < n; ++i)
        {
            const double s = a * std::sin (2 * kPi * 1000.0 * i / sr);
            m.process (s, s, 2);
        }
        NEAR (m.momentary.load(), -23.0, 0.1);
        NEAR (m.shortTerm.load(), -23.0, 0.1);
        NEAR (m.integrated(), -23.0, 0.1);
    }

    // Gating: 10 s at -20 then 10 s at -40 (dBFS) integrates close to the loud part only.
    LoudnessMeter m;
    m.prepare (48000);
    for (int part = 0; part < 2; ++part)
    {
        const double a = dbToGain (part == 0 ? -20.0 : -40.0);
        for (int i = 0; i < 480000; ++i)
        {
            const double s = a * std::sin (2 * kPi * 1000.0 * i / 48000);
            m.process (s, s, 2);
        }
    }
    test::record ("loudness_gated_dB", m.integrated());
    NEAR (m.integrated(), -20.0, 0.2);
}

// ---- Chain ---------------------------------------------------------------------------

namespace
{
struct ChainRig
{
    Chain chain;
    ChainParams p;
    explicit ChainRig (bool lowLatency = false)
    {
        for (auto& c : p.dynamics.ch)
        {
            c.curve.thresholdDb = 0; c.curve.slope = 1; c.curve.knee = 0; c.previewMs = 0;
        }
        chain.prepare (48000, lowLatency);
        chain.setParamsImmediately (p);
    }
    void run (std::vector<double>& l, std::vector<double>& r, int block = 512)
    {
        const int lat = chain.latencySamples();
        l.resize (l.size() + (size_t) lat, 0.0);
        r.resize (r.size() + (size_t) lat, 0.0);
        for (size_t pos = 0; pos < l.size(); pos += (size_t) block)
        {
            const int n = (int) std::min ((size_t) block, l.size() - pos);
            const double* in[2] = { l.data() + pos, r.data() + pos };
            double* out[2] = { l.data() + pos, r.data() + pos };
            chain.setParams (p);
            chain.process<double> (in, out, nullptr, 2, 0, n);
        }
        l.erase (l.begin(), l.begin() + lat);
        r.erase (r.begin(), r.begin() + lat);
    }
    /** De-ess settings: only the selected band is detected and turned down. */
    void deess (FilterType type, double hz, double widthOct = 1.2)
    {
        auto& c = p.dynamics.ch[0];
        c.band.type = type;
        c.band.frequency = hz;
        c.band.bandwidthOct = widthOct;
        c.curve.thresholdDb = -30; c.curve.slope = 1.0 / 6.0; c.curve.knee = 0.2;
        c.times.attackMs = 0.1; c.times.releaseDelayMs = 0; c.times.releaseFastMs = 40; c.times.averageMs = 60; c.times.releaseSlowMs = 80;
        c.previewMs = 1.0;
        chain.setParamsImmediately (p);
    }
};
} // namespace

TEST (m13_chain_latency_is_the_dynamics_latency)
{
    ChainRig rig;
    CHECK (rig.chain.latencySamples() == rig.chain.dynamics.latencySamples());
    CHECK (rig.chain.latencySamples() > 0);
    test::record ("chain_latency_ms", rig.chain.latencySamples() / 48.0);
    ChainRig fast (true);
    test::record ("chain_latency_low_latency_ms", fast.chain.latencySamples() / 48.0);
    CHECK (fast.chain.latencySamples() < rig.chain.latencySamples());
}

TEST (m14_chain_neutral_is_bit_exact_and_aligned)
{
    for (bool lowLatency : { false, true })
    {
        ChainRig rig (lowLatency);
        auto l = sig::pink (48000, 0.5), r = sig::noise (48000, 0.3);
        const auto dl = l, dr = r;
        rig.run (l, r);
        double err = 0;
        for (size_t i = 0; i < dl.size(); ++i) err = std::max ({ err, std::abs (l[i] - dl[i]), std::abs (r[i] - dr[i]) });
        CHECK (err == 0.0);
    }
}

TEST (m15_chain_global_bypass_is_bit_exact_and_keeps_latency)
{
    ChainRig g;
    g.p.dynamics.ch[0].curve.thresholdDb = -40;
    g.p.dynamics.ch[0].curve.slope = 0.1;
    g.p.bypass = true;
    g.p.inputDb = 12;
    g.p.ditherBits = 16;
    g.chain.setParamsImmediately (g.p);
    auto a = sig::pink (24000, 0.5), b = a;
    const auto da = a;
    g.run (a, b);
    double err = 0;
    for (size_t i = 0; i < da.size(); ++i) err = std::max (err, std::abs (a[i] - da[i]));
    CHECK (err == 0.0);
}

TEST (m16_chain_external_sidechain_is_aligned)
{
    ChainRig rig;
    rig.p.dynamics.externalSidechain = true;
    rig.p.dynamics.ch[0].curve.thresholdDb = -30;
    rig.p.dynamics.ch[0].curve.slope = 0.1;
    rig.chain.setParamsImmediately (rig.p);
    const int n = 48000, lat = rig.chain.latencySamples();
    std::vector<double> prog = sig::sine (n + lat, 400, 48000, 0.1), key ((size_t) (n + lat), 0.0), out[2];
    for (int i = 12000; i < 13000; ++i) key[(size_t) i] = 0.9;
    out[0] = prog; out[1] = prog;
    for (int pos = 0; pos < n + lat; pos += 256)
    {
        const int k = std::min (256, n + lat - pos);
        const double* in[2] = { prog.data() + pos, prog.data() + pos };
        double* o[2] = { out[0].data() + pos, out[1].data() + pos };
        const double* sc[2] = { key.data() + pos, key.data() + pos };
        rig.chain.process<double> (in, o, sc, 2, 2, k);
    }
    int onset = -1;
    for (int i = lat; i < n + lat && onset < 0; ++i)
        if (std::abs (out[0][(size_t) i]) < 0.5 * std::abs (prog[(size_t) (i - lat)]) && std::abs (prog[(size_t) (i - lat)]) > 0.05)
            onset = i - lat;
    test::record ("sidechain_onset_sample", onset);
    // The key starts at 12000; lookahead is 0, so the gain may not drop before it.
    CHECK (onset >= 12000 && onset < 12100);
}

TEST (m17_deess_band_pass_turns_down_only_the_band)
{
    ChainRig rig;
    rig.deess (FilterType::bandPass, 6500.0);
    auto l = addTones ({ { 300.0, dbToGain (-12.0) }, { 6500.0, dbToGain (-12.0) } }, 96000, 48000), r = l;
    const auto d = l;
    rig.run (l, r);
    const double lowDelta = toneLevelDb (l, 48000, 96000, 300.0, 48000) - toneLevelDb (d, 48000, 96000, 300.0, 48000);
    const double sDelta = toneLevelDb (l, 48000, 96000, 6500.0, 48000) - toneLevelDb (d, 48000, 96000, 6500.0, 48000);
    test::record ("deess_bp_300Hz_change_dB", lowDelta);
    test::record ("deess_bp_6k5_change_dB", sDelta);
    NEAR (lowDelta, 0.0, 0.3);
    CHECK (sDelta < -6.0);
}

TEST (m18_deess_high_pass_spares_the_mids)
{
    ChainRig rig;
    rig.deess (FilterType::highPass, 7000.0);
    auto l = addTones ({ { 1000.0, dbToGain (-10.0) }, { 10000.0, dbToGain (-14.0) } }, 96000, 48000), r = l;
    const auto d = l;
    rig.run (l, r);
    const double midDelta = toneLevelDb (l, 48000, 96000, 1000.0, 48000) - toneLevelDb (d, 48000, 96000, 1000.0, 48000);
    const double hiDelta = toneLevelDb (l, 48000, 96000, 10000.0, 48000) - toneLevelDb (d, 48000, 96000, 10000.0, 48000);
    test::record ("deess_hp_1k_change_dB", midDelta);
    test::record ("deess_hp_10k_change_dB", hiDelta);
    NEAR (midDelta, 0.0, 0.5);
    CHECK (hiDelta < -4.0);
}

TEST (m19_deess_quiet_sibilance_is_untouched)
{
    // Below threshold the band path is bit-exact apart from nothing: no reduction at all.
    ChainRig rig;
    rig.deess (FilterType::bandPass, 6500.0);
    auto l = addTones ({ { 300.0, dbToGain (-20.0) }, { 6500.0, dbToGain (-50.0) } }, 48000, 48000), r = l;
    const auto d = l;
    rig.run (l, r);
    NEAR (toneLevelDb (l, 24000, 48000, 6500.0, 48000) - toneLevelDb (d, 24000, 48000, 6500.0, 48000), 0.0, 0.2);
}
