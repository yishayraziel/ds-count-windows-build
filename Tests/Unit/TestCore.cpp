// Brief section 18, tests 1-12.
#include "TestHarness.h"
using namespace dsq;

TEST (t01_unity_passthrough)
{
    Rig rig;
    rig.apply();
    auto l = sig::pink (48000, 0.8), r = sig::noise (48000, 0.5);
    const auto dl = l, dr = r;
    rig.run (l, r);
    double maxErr = 0;
    for (size_t i = 0; i < dl.size(); ++i)
        maxErr = std::max ({ maxErr, std::abs (l[i] - dl[i]), std::abs (r[i] - dr[i]) });
    test::record ("passthrough_max_error", maxErr);
    CHECK (maxErr == 0.0);
}

TEST (t02_bypass_is_bit_exact_with_latency)
{
    Rig rig;
    rig.params.bypass = true;
    rig.params.outputDb = 12.0;
    rig.params.inputDb = -6.0;
    rig.params.ch[0].curve.thresholdDb = -40;
    rig.params.ch[0].curve.slope = 0.1;
    rig.params.ditherBits = 16;
    rig.apply();
    auto l = sig::pink (48000, 0.9), r = l;
    const auto d = l;
    rig.run (l, r);
    for (size_t i = 0; i < d.size(); ++i)
        if (l[i] != d[i]) { CHECK (false); break; }
    CHECK (true);
}

TEST (t03_input_output_gain_accuracy)
{
    for (double db : { -24.0, -6.0, 3.5, 24.0 })
    {
        Rig rig;
        rig.params.inputDb = db;
        rig.apply();
        auto l = sig::sine (48000, 997, 48000, 0.01), r = l;
        rig.run (l, r);
        NEAR (20 * std::log10 (sig::rms (l, 1000, 40000) / sig::rms (sig::sine (48000, 997, 48000, 0.01), 1000, 40000)), db, 1e-6);

        Rig rig2;
        rig2.params.outputDb = db;
        rig2.apply();
        auto a = sig::sine (48000, 997, 48000, 0.01), b = a;
        rig2.run (a, b);
        NEAR (20 * std::log10 (sig::rms (a, 1000, 40000) / sig::rms (sig::sine (48000, 997, 48000, 0.01), 1000, 40000)), db, 1e-6);
    }
}

// Long steady tone with slow release, so the envelope equals the peak level.
static double staticOutputDb (TransferParams c, double inDb)
{
    Rig rig;
    rig.params.ch[0].curve = c;
    rig.params.ch[0].times.attackMs = 1;
    rig.params.ch[0].times.releaseDelayMs = 200;
    rig.params.ch[0].times.releaseFastMs = 2000;
    rig.params.ch[0].times.releaseSlowMs = 2000;
    rig.apply();
    const double a = dbToGain (inDb);
    auto l = sig::sine (48000, 1000, 48000, a), r = l;
    rig.run (l, r);
    return 20 * std::log10 (sig::rms (l, 38000, 47000) * std::sqrt (2.0));
}

TEST (t04_threshold_behavior)
{
    TransferParams c;
    c.thresholdDb = -20; c.slope = 0.25; c.knee = 0;
    NEAR (staticOutputDb (c, -30), -30, 0.01);
    NEAR (staticOutputDb (c, -21), -21, 0.01);
    NEAR (staticOutputDb (c, -10), -17.5, 0.02);
}

TEST (t05_compression_ratios)
{
    for (double ratio : { 1.5, 2.0, 4.0, 10.0, 20.0, 1000.0 })
    {
        TransferParams c;
        c.thresholdDb = -24; c.slope = 1.0 / ratio; c.knee = 0;
        const double measured = (staticOutputDb (c, -4) - staticOutputDb (c, -14)) / 10.0;
        NEAR (measured, 1.0 / ratio, 0.003);
        test::record ("ratio_" + std::to_string ((int) ratio) + "_slope", measured);
    }
}

TEST (t06_expansion_ratios_and_max_makeup)
{
    for (double ratio : { 2.0, 5.0 })
    {
        TransferParams c;
        c.thresholdDb = -4; c.slope = ratio; c.knee = 0; c.autoMakeup = true;
        // Documented behaviour: a 0 dBFS input is unchanged, below threshold is a constant reduction.
        NEAR (staticOutputDb (c, 0), 0.0, 0.03);
        NEAR (staticOutputDb (c, -30), -30 - (ratio - 1) * 4, 0.03);
        NEAR (staticOutputDb (c, -2), -2 * ratio, 0.03);
    }
}

TEST (t07_knee_continuity)
{
    for (double t : { -60.0, -30.0, -12.0, -1.0 })
        for (double k : { 0.0, 0.25, 0.5, 1.0 })
            for (double s : { 0.001, 0.25, 0.5, 2.0, 5.0 })
            {
                TransferParams c;
                c.thresholdDb = t; c.knee = k; c.slope = s;
                const double dx = 0.001;
                double prev = curveGainDb (-250, c), prevSlope = (prev - curveGainDb (-250 - dx, c)) / dx, worstStep = 0, worstBend = 0;
                for (double x = -250 + dx; x < 12; x += dx)
                {
                    const double y = curveGainDb (x, c), sl = (y - prev) / dx;
                    worstStep = std::max (worstStep, std::abs (y - prev));
                    if (k > 0 && t < -0.5) worstBend = std::max (worstBend, std::abs (sl - prevSlope));
                    prev = y; prevSlope = sl;
                }
                CHECK (worstStep < 0.0041);           // no jumps at the largest possible slope
                if (k > 0) CHECK (worstBend < 0.15);  // no corners: slope is continuous
            }

    // Design target: at knee 100 % and -20 dB the knee spans -46 to +6 dB.
    TransferParams c; c.thresholdDb = -20; c.slope = 0.25; c.knee = 1.0;
    NEAR (curveGainDb (-46.0, c), 0.0, 1e-12);
    CHECK (curveGainDb (-45.0, c) < 0.0);
    NEAR (curveGainDb (6.0, c), (0.25 - 1) * 26, 1e-9);
    // and the target gain at -36 dB (0.66 dB) and -21 dB (4.35 dB), within 0.1 dB.
    NEAR (curveGainDb (-36.0 - 0.4, c), -0.66, 0.1);
    NEAR (curveGainDb (-21.0 - 0.4, c), -4.35, 0.1);
}

TEST (t07b_auto_makeup_knob_rule)
{
    // Dragging past Max must give one clean change of state, never a flip on every mouse move.
    const double maxDb = 12.0;
    bool on = false;
    int changes = 0;
    for (double knob = 0.0; knob <= 30.0; knob += 0.1)
    {
        const bool next = nextAutoMakeup (on, knob, maxDb, true);
        changes += next != on;
        on = next;
    }
    CHECK (on);
    CHECK (changes == 1);

    for (double knob = 30.0; knob >= 0.0; knob -= 0.1)
    {
        const bool next = nextAutoMakeup (on, knob, maxDb, true);
        changes += next != on;
        on = next;
    }
    CHECK (! on);
    CHECK (changes == 2);

    // Turning up from the Max position changes nothing, turning down leaves at that point.
    CHECK (nextAutoMakeup (true, maxDb, maxDb, true));
    CHECK (nextAutoMakeup (true, maxDb + 1, maxDb, true));
    CHECK (! nextAutoMakeup (true, maxDb - 0.5, maxDb, true));

    // Band mode and Parallel never lock, and a Max set by hand still leaves on a turn down.
    CHECK (! nextAutoMakeup (false, 50.0, maxDb, false));
    CHECK (! nextAutoMakeup (true, maxDb - 0.5, maxDb, false));
}

TEST (t08_attack_time_accuracy)
{
    for (double attackMs : { 0.02, 0.1, 1.0, 10.0, 100.0 })
    {
        EnvelopeFollower e;
        EnvelopeTimes t; t.attackMs = attackMs;
        e.configure (t, 192000.0);
        const double tau = attackMs * 0.001 * 192000.0 * EnvelopeFollower::kTimeScale;
        const int n = (int) std::ceil (tau);
        double v = 0;
        for (int i = 0; i < n; ++i) v = e.process (1.0, 0.0);
        const double target = 1.0 - std::exp (-n / tau);
        NEAR (v, target, 1e-9);
    }
    // The same physical time is obtained at every sample rate.
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        EnvelopeFollower e;
        EnvelopeTimes t; t.attackMs = 5.0;
        e.configure (t, sr);
        double v = 0;
        const int n = (int) std::lround (0.005 * sr);
        for (int i = 0; i < n; ++i) v = e.process (1.0, 0.0);
        NEAR (v, 1.0 - std::exp (-n / (0.005 * sr * EnvelopeFollower::kTimeScale)), 1e-9);
    }
}

static void charge (EnvelopeFollower& e) { for (int i = 0; i < 2000; ++i) e.process (1.0, 0.0); }

static int timeToFall (EnvelopeFollower& e, double from, double to, double avg, int limit = 10000000)
{
    (void) from;
    int n = 0;
    while (e.value() > to && n < limit) { e.process (0.0, avg); ++n; }
    return n;
}

TEST (t09_fast_release)
{
    for (double ms : { 5.0, 50.0, 500.0 })
    {
        EnvelopeFollower e;
        EnvelopeTimes t; t.attackMs = 0.02; t.releaseFastMs = ms; t.releaseSlowMs = 8000;
        e.configure (t, 48000);
        charge (e);
        const int n = timeToFall (e, 1, 0.1, 0.0);   // 2 nepers (17.37 dB) per Release time
        NEAR (n / 48.0, ms * 20.0 / 17.3718, ms * 0.02 + 0.1);
    }
}

TEST (t10_slow_release)
{
    for (double ms : { 100.0, 1000.0 })
    {
        EnvelopeFollower e;
        EnvelopeTimes t; t.attackMs = 0.02; t.releaseFastMs = 1; t.releaseSlowMs = ms;
        e.configure (t, 48000);
        charge (e);
        // Reference held far above the envelope: pure Release Slow.
        const int n = timeToFall (e, 1, 0.1, 100.0);
        NEAR (n / 48.0, ms * 20.0 / 17.3718, ms * 0.02);
    }

    // The two constants are selected by the peak/average relationship.
    EnvelopeTimes t;
    CHECK (ReleaseController::fastWeight (2.0, 0.5) == 1.0);
    CHECK (ReleaseController::fastWeight (0.5, 2.0) == 0.0);
    NEAR (ReleaseController::fastWeight (dbToGain (ReleaseController::kOffsetDb), 1.0), 0.5, 1e-9);
    double prev = 1.0;
    for (double r = 4.0; r > 0.5; r *= 0.9995)
    {
        const double w = ReleaseController::fastWeight (r, 1.0);
        CHECK (w <= prev + 1e-12);
        CHECK (prev - w < 0.01);
        prev = w;
    }
}

TEST (t10b_burst_length_selects_release_portion)
{
    // Documented: short burst -> Fast only; longer burst -> a Slow portion that grows with length.
    auto measure = [] (double burstMs, double averageMs)
    {
        Rig rig;
        auto& ch = rig.params.ch[0];
        ch.curve.thresholdDb = -30; ch.curve.slope = 0.25; ch.curve.knee = 0;
        ch.times.attackMs = 0.1; ch.times.releaseFastMs = 10; ch.times.releaseSlowMs = 600; ch.times.averageMs = averageMs;
        rig.apply();
        const int sr = 48000, pre = sr, burst = (int) (burstMs * 48), post = 6 * sr;
        auto l = sig::sine (pre + burst + post, 1000, sr, dbToGain (-40));
        for (int i = pre; i < pre + burst; ++i) l[(size_t) i] *= 100.0;
        auto r = l;
        rig.run (l, r);
        // Time for the output level to come back within 1 dB of the pre-burst level.
        int back = post - 1;
        const double ref = sig::rms (l, pre - 4800, pre);
        const int win = 240;
        for (int i = pre + burst; i < pre + burst + post - win; i += 24)
            if (sig::rms (l, i, i + win) > ref * 0.891) { back = i - (pre + burst); break; }
        return back / 48.0;
    };

    const double shortBurst = measure (2, 300), mid = measure (100, 300), longBurst = measure (500, 300), shortAvg = measure (500, 10);
    test::record ("burst_2ms_recovery_ms", shortBurst);
    test::record ("burst_100ms_recovery_ms", mid);
    test::record ("burst_500ms_recovery_ms", longBurst);
    test::record ("burst_500ms_shortavg_recovery_ms", shortAvg);
    CHECK (shortBurst < mid);
    CHECK (mid < longBurst);
    CHECK (shortAvg < longBurst);
}

TEST (t10c_dual_release_targets)
{
    // Design targets: 1 kHz burst from -40 to
    // -6 dBFS, threshold -20, 4:1, attack 0.1 ms, Fast 40 ms, Average 630 ms, Slow 1250 ms.
    // Times for the gain reduction to recover 63 % and 90 %:
    //   50 ms burst  20 / 28 ms     300 ms burst  271 / 420 ms     3 s burst  423 / 574 ms
    auto recovery = [] (double onMs, double& t63, double& t90)
    {
        Rig rig;
        auto& c = rig.params.ch[0];
        c.curve.thresholdDb = -20; c.curve.slope = 0.25; c.curve.knee = 0;
        c.times.attackMs = 0.1; c.times.releaseFastMs = 40; c.times.averageMs = 630; c.times.releaseSlowMs = 1250;
        c.previewMs = 0.02;
        rig.apply();
        const int sr = 48000, pre = sr, on = (int) (onMs * 48), post = 4 * sr;
        auto l = sig::sine (pre + on + post, 1000, sr, dbToGain (-40));
        for (int i = pre; i < pre + on; ++i) l[(size_t) i] *= dbToGain (34.0);
        const auto in = l;
        auto r = l;
        rig.run (l, r);
        auto grDb = [&] (int i) { return 20 * std::log10 (sig::rms (l, i, i + 48) / sig::rms (in, i, i + 48)); };
        const double g0 = grDb (pre + on - 96);
        t63 = t90 = -1;
        for (int i = pre + on; i < pre + on + post - 48; i += 48)
        {
            const double g = grDb (i);
            if (t63 < 0 && g >= g0 * 0.37) t63 = (i - pre - on) / 48.0;
            if (g >= g0 * 0.1) { t90 = (i - pre - on) / 48.0; break; }
        }
    };
    double a63, a90, b63, b90, c63, c90;
    recovery (50, a63, a90);
    recovery (300, b63, b90);
    recovery (3000, c63, c90);
    test::record ("dual_50ms_t63", a63);
    test::record ("dual_50ms_t90", a90);
    test::record ("dual_300ms_t63", b63);
    test::record ("dual_300ms_t90", b90);
    test::record ("dual_3s_t63", c63);
    test::record ("dual_3s_t90", c90);
    NEAR (a63, 20, 8);  NEAR (a90, 28, 15);
    NEAR (b63, 271, 90); NEAR (b90, 420, 90);
    NEAR (c63, 423, 60); NEAR (c90, 574, 60);
}

TEST (t11_release_delay_holds_then_releases)
{
    for (double hold : { 5.0, 50.0, 200.0 })
    {
        EnvelopeFollower e;
        EnvelopeTimes t; t.attackMs = 0.02; t.releaseDelayMs = hold; t.releaseFastMs = 10; t.releaseSlowMs = 10;
        e.configure (t, 48000);
        charge (e);
        int held = 0;
        const double top = e.value();
        while (e.value() >= top && held < 1000000) { e.process (0.0, 0.0); ++held; }
        NEAR (held / 48.0, hold, 0.05);
    }
}

TEST (t12_lookahead_catches_transient_and_latency_is_constant)
{
    auto peakAfterStep = [] (double previewMs, bool lowLatency)
    {
        Rig rig (48000, lowLatency);
        auto& ch = rig.params.ch[0];
        ch.curve.thresholdDb = -20; ch.curve.slope = 0.001; ch.curve.knee = 0;
        ch.times.attackMs = 0.1; ch.times.releaseFastMs = 100; ch.times.releaseSlowMs = 100;
        ch.previewMs = previewMs;
        rig.apply();
        std::vector<double> l (9600, 0.0);
        for (int i = 4800; i < 9600; ++i) l[(size_t) i] = 1.0;
        auto r = l;
        rig.run (l, r);
        // Impulse position must not move with the preview: that is the host latency contract.
        return sig::peak (l, 4800, 9000);
    };
    const double none = peakAfterStep (0.0, false), previewed = peakAfterStep (0.4, false);
    test::record ("step_peak_no_preview_dB", 20 * std::log10 (none));
    test::record ("step_peak_preview_0p4ms_dB", 20 * std::log10 (previewed));
    CHECK (previewed < none * 0.5);
    NEAR (20 * std::log10 (peakAfterStep (0.8, false)), -20, 1.0);

    // The dry sample reaches the output exactly latencySamples() later for every preview.
    for (double p : { 0.0, 1.0, 10.0 })
    {
        Rig rig;
        rig.params.ch[0].previewMs = p;
        rig.apply();
        std::vector<double> l (200000, 0.0);
        l[100] = 1.0;
        auto r = l;
        rig.run (l, r, 512, false);
        int at = -1;
        for (size_t i = 0; i < l.size(); ++i) if (l[i] != 0.0) { at = (int) i; break; }
        CHECK (at == 100 + rig.engine.latencySamples());
    }

    Rig fast (48000, true);
    test::record ("latency_normal_48k_ms", Rig (48000, false).engine.latencySamples() / 48.0);
    test::record ("latency_low_48k_ms", fast.engine.latencySamples() / 48.0);
    CHECK (fast.engine.latencySamples() / 48.0 < 6.5);
}
