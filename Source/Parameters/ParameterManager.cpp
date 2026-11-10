#include "ParameterManager.h"

namespace dsq
{
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
using Attr = juce::AudioParameterFloatAttributes;
using Range = juce::NormalisableRange<float>;

// Compression half is uniform in slope, as the documented ratio table is. Position 0.2 is 1:1.
static constexpr double kExpansionShare = 0.2, kMaxExpansion = 5.0, kMinSlope = 0.001;

double ratioFromNormalised (double n) noexcept
{
    const double slope = n < kExpansionShare ? std::pow (kMaxExpansion, 1.0 - n / kExpansionShare)
                                             : 1.0 - (n - kExpansionShare) / (1.0 - kExpansionShare) * (1.0 - kMinSlope);
    return 1.0 / slope;
}

double normalisedFromRatio (double r) noexcept
{
    const double slope = 1.0 / r;
    if (slope > 1.0)
        return kExpansionShare * (1.0 - std::log (slope) / std::log (kMaxExpansion));
    return kExpansionShare + (1.0 - slope) / (1.0 - kMinSlope) * (1.0 - kExpansionShare);
}

juce::String ratioText (double r)
{
    if (r >= 999.0) return "1000 : 1";
    if (r >= 100.0) return juce::String (r, 0) + " : 1";
    if (r >= 1.0)   return juce::String (r, 2) + " : 1";
    return "1 : " + juce::String (1.0 / r, 2);
}

juce::String timeText (double ms)
{
    if (ms >= 1000.0) return juce::String (ms / 1000.0, 2) + " s";
    if (ms >= 100.0)  return juce::String (ms, 0) + " ms";
    if (ms >= 10.0)   return juce::String (ms, 1) + " ms";
    return juce::String (ms, 2) + " ms";
}

static Range logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float n) { return s * std::pow (e / s, n); },
             [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
             [] (float, float, float v) { return v; } };
}

static float timeFromText (const juce::String& t)
{
    const auto s = t.trim().toLowerCase();
    const float v = s.getFloatValue();
    return s.endsWith ("ms") ? v : s.endsWith ("s") ? v * 1000.0f : v;
}

static Attr timeAttr()
{
    return Attr().withLabel ("ms")
        .withStringFromValueFunction ([] (float v, int) { return timeText (v); })
        .withValueFromStringFunction ([] (const juce::String& t) { return timeFromText (t); });
}

static Attr dbAttr (int decimals = 1)
{
    return Attr().withLabel ("dB")
        .withStringFromValueFunction ([decimals] (float v, int)
        {
            // Never show "-0.0 dB".
            const auto text = juce::String (v, decimals);
            return (text.getDoubleValue() == 0.0 ? text.trimCharactersAtStart ("-") : text) + " dB";
        })
        .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
}

template <typename P, typename... A>
static void add (Layout& l, const juce::String& id, const juce::String& name, A&&... args)
{
    l.add (std::make_unique<P> (juce::ParameterID { id, 1 }, name, std::forward<A> (args)...));
}

static void addFloat (Layout& l, const juce::String& id, const juce::String& name, Range r, float def, Attr a)
{
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def, a));
}

static void addBool (Layout& l, const juce::String& id, const juce::String& name, bool def, bool automatable = true)
{
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def,
        juce::AudioParameterBoolAttributes().withAutomatable (automatable)));
}

static void addChoice (Layout& l, const juce::String& id, const juce::String& name, juce::StringArray items, int def, bool automatable = true)
{
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def,
        juce::AudioParameterChoiceAttributes().withAutomatable (automatable)));
}

static Range linearRange (float lo, float hi, float step = 0.01f) { return { lo, hi, step }; }

static Attr hzAttr()
{
    return Attr().withLabel ("Hz")
        .withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (v, 0) + " Hz"; })
        .withValueFromStringFunction ([] (const juce::String& t)
        {
            const auto s = t.trim().toLowerCase();
            return s.getFloatValue() * (s.endsWith ("k") || s.endsWith ("khz") ? 1000.0f : 1.0f);
        });
}

static Attr octAttr()
{
    return Attr().withLabel ("oct")
        .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " oct"; })
        .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
}

static Range ratioRange()
{
    return { 0.2f, 1000.0f,
        [] (float, float, float n) { return (float) ratioFromNormalised (n); },
        [] (float, float, float v) { return (float) normalisedFromRatio (v); },
        [] (float, float, float v) { return v; } };
}

static Attr ratioAttr()
{
    return Attr()
        .withStringFromValueFunction ([] (float v, int) { return ratioText (v); })
        .withValueFromStringFunction ([] (const juce::String& t)
        {
            const auto left = t.upToFirstOccurrenceOf (":", false, false).getFloatValue();
            const auto right = t.fromFirstOccurrenceOf (":", false, false).getFloatValue();
            return right > 0.0f ? left / right : left;
        });
}

static Range skewedTime (float hi, float power)
{
    return { 0.0f, hi,
             [power] (float, float e, float n) { return e * std::pow (n, power); },
             [power] (float, float e, float v) { return std::pow (v / e, 1.0f / power); },
             [] (float, float, float v) { return v; } };
}

static void addDynamicsChannel (Layout& l, const char* m, const juce::String& moduleName, int c)
{
    const juce::String p = moduleName + (c == 0 ? " Ch1 " : " Ch2 ");
    addFloat (l, cid (m, c, ids::threshold), p + "Threshold", { -60.0f, 0.0f, 0.01f }, -12.0f, dbAttr());
    addFloat (l, cid (m, c, ids::ratio), p + "Ratio", ratioRange(), 2.0f, ratioAttr());
    addFloat (l, cid (m, c, ids::knee), p + "Soft Knee", { 0.0f, 1.0f, 0.001f }, 0.5f, Attr().withLabel ("%")
        .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
        .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue() / 100.0f; }));
    addFloat (l, cid (m, c, ids::makeup), p + "Gain Makeup", { -60.0f, 60.0f, 0.01f }, 0.0f, dbAttr());
    addBool (l, cid (m, c, ids::autoMakeup), p + "Auto Make-up", false);
    addFloat (l, cid (m, c, ids::attack), p + "Attack", logRange (0.02f, 800.0f), 0.1f, timeAttr());
    addFloat (l, cid (m, c, ids::relDelay), p + "Release Delay", skewedTime (8000.0f, 4.0f), 0.0f, timeAttr());
    addFloat (l, cid (m, c, ids::relFast), p + "Release Fast", logRange (0.02f, 8000.0f), 10.0f, timeAttr());
    addFloat (l, cid (m, c, ids::average), p + "Average", logRange (0.02f, 8000.0f), 250.0f, timeAttr());
    addFloat (l, cid (m, c, ids::relSlow), p + "Release Slow", logRange (0.02f, 8000.0f), 250.0f, timeAttr());

    Range rmsRange { 0.0f, 8000.0f,
        [] (float, float, float n) { return n <= 0.0f ? 0.0f : 0.08f * std::pow (100000.0f, n); },
        [] (float, float, float v) { return v <= 0.0f ? 0.0f : std::log (std::max (v, 0.08f) / 0.08f) / std::log (100000.0f); },
        [] (float, float, float v) { return v; } };
    addFloat (l, cid (m, c, ids::rmsDetect), p + "RMS Detect", rmsRange, 0.0f, Attr().withLabel ("ms")
        .withStringFromValueFunction ([] (float v, int) { return v <= 0.0f ? juce::String ("Peak") : timeText (v); })
        .withValueFromStringFunction ([] (const juce::String& t) { return t.trim().equalsIgnoreCase ("peak") ? 0.0f : timeFromText (t); }));
    addFloat (l, cid (m, c, ids::preview), p + "Lookahead", skewedTime (63.0f, 3.0f), 0.3f, timeAttr());
    addChoice (l, cid (m, c, ids::filterType), p + "Band Mode", { "Full Band", "Low Pass", "Band Pass", "High Pass" }, 0);
    addFloat (l, cid (m, c, ids::frequency), p + "Frequency", logRange (20.0f, 20000.0f), 3200.0f, hzAttr());
    addFloat (l, cid (m, c, ids::bandwidth), p + "Bandwidth", logRange (0.1f, 8.0f), 1.0f, octAttr());

    addBool (l, cid (m, c, ids::parallel), p + "Parallel", false);
}

static void addDynamicsModule (Layout& l, const char* m, const juce::String& name)
{
    addDynamicsChannel (l, m, name, 0);
    addDynamicsChannel (l, m, name, 1);
    addBool (l, gid (m, ids::monitor), name + " Monitor", false);
    addBool (l, gid (m, ids::link), name + " Sidechain Link", true);
    addBool (l, gid (m, ids::midSide), name + " Mid/Side", false);
    addBool (l, gid (m, ids::ganged), name + " Ganged", true);
    addChoice (l, gid (m, ids::selChannel), name + " Edit Channel", { "1", "2" }, 0, false);
    addBool (l, gid (m, ids::limiter), name + " Limiter", true);
    addChoice (l, gid (m, ids::limType), name + " Limiter Type", { "Safe", "Type 1", "Type 2 (TP)" }, 1);
    addFloat (l, gid (m, ids::ceiling), name + " Ceiling", { -12.0f, 0.0f, 0.01f }, -0.1f, dbAttr (2));
    addFloat (l, gid (m, ids::preLimiter), name + " Limiter Gain", { -24.0f, 30.0f, 0.01f }, 0.0f, dbAttr());
    addChoice (l, gid (m, ids::scSource), name + " Sidechain", { "Internal", "External" }, 0);
    addBool (l, gid (m, ids::scFilter), name + " Sidechain Filter", false);
}

Layout ParameterManager::createLayout()
{
    Layout l;
    addDynamicsModule (l, ids::dynamics, "Dynamics");

    addFloat (l, ids::inGain, "Input", { -24.0f, 24.0f, 0.01f }, 0.0f, dbAttr());
    addFloat (l, ids::outGain, "Output", { -24.0f, 24.0f, 0.01f }, 0.0f, dbAttr());
    addBool (l, ids::globalBypass, "Bypass", false);
    addChoice (l, ids::dither, "Dither", { "Off", "16 bit", "20 bit", "24 bit" }, 0);
    addChoice (l, ids::ditherType, "Dither Type", { "TPDF", "Shape 1", "Shape 2", "Shape 3" }, 0);
    addBool (l, ids::lowLatency, "Low Latency", false, false);
    addChoice (l, ids::peakMeter, "Peak Meter", { "Sample Peak", "True Peak" }, 1, false);
    return l;
}

bool ParameterManager::isSessionParameter (const juce::String& id)
{
    return id == ids::globalBypass || id == ids::lowLatency || id == ids::peakMeter || id.endsWith ("_selchannel");
}

juce::String ParameterManager::migrateV1Id (const juce::String& id)
{
    if (id.startsWith ("ch1_") || id.startsWith ("ch2_"))
        return juce::String (ids::dynamics) + "_" + id;
    if (id == "input")
        return ids::inGain;
    if (id == "output")
        return ids::outGain;
    if (id == ids::globalBypass || id == ids::dither || id == ids::ditherType || id == ids::lowLatency || id == ids::peakMeter)
        return id;
    static const char* const moduleWide[] = { ids::monitor, ids::link, ids::midSide, ids::ganged, ids::selChannel, ids::limiter,
                                              ids::limType, ids::ceiling, ids::preLimiter, ids::scSource, ids::scFilter };
    for (auto* m : moduleWide)
        if (id == m)
            return gid (ids::dynamics, m);
    return {}; // 1.x display options (meter range, overshoot lamp...) are UI settings now
}

ParameterManager::ParameterManager (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    auto get = [this] (const juce::String& id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    static const char* const channelSuffixes[] = {
        ids::threshold, ids::ratio, ids::knee, ids::makeup, ids::autoMakeup, ids::attack, ids::relDelay, ids::relFast,
        ids::average, ids::relSlow, ids::rmsDetect, ids::preview, ids::filterType, ids::frequency, ids::bandwidth,
        ids::parallel };

    auto setupEngine = [&] (EnginePtrs& e, const char* m)
    {
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kChannelParams; ++k)
                e.ch[c].p[k] = apvts.getRawParameterValue (cid (m, c, channelSuffixes[k]));   // missing ones stay null
        e.monitor = get (gid (m, ids::monitor)); e.link = get (gid (m, ids::link)); e.midSide = get (gid (m, ids::midSide));
        e.ganged = get (gid (m, ids::ganged)); e.selChannel = get (gid (m, ids::selChannel)); e.limiter = get (gid (m, ids::limiter));
        e.limType = get (gid (m, ids::limType)); e.ceiling = get (gid (m, ids::ceiling)); e.preLimiter = get (gid (m, ids::preLimiter));
        e.scSource = get (gid (m, ids::scSource)); e.scFilter = get (gid (m, ids::scFilter));
    };
    setupEngine (dyn, ids::dynamics);

    inGain = get (ids::inGain); outGain = get (ids::outGain); globalBypass = get (ids::globalBypass);
    dither = get (ids::dither); ditherType = get (ids::ditherType); peakMeter = get (ids::peakMeter);
}

void ParameterManager::readEngine (const EnginePtrs& e, EngineParams& out) const noexcept
{
    enum { kThr, kRatio, kKnee, kMakeup, kAuto, kAttack, kDelay, kFast, kAvg, kSlow, kRms, kPreview, kFilter, kFreq, kBw, kParallel };

    for (int c = 0; c < 2; ++c)
    {
        auto v = [&] (int k, float def = 0.0f) { auto* p = e.ch[c].p[k]; return p != nullptr ? p->load() : def; };
        auto& s = out.ch[c];
        s.curve.thresholdDb = v (kThr);
        s.curve.slope = 1.0 / std::max (v (kRatio, 1.0f), 0.2f);
        s.curve.knee = v (kKnee);
        s.curve.makeupDb = v (kMakeup);
        s.curve.autoMakeup = v (kAuto) > 0.5f;
        s.times.attackMs = v (kAttack, 1.0f);
        s.times.releaseDelayMs = v (kDelay);
        s.times.releaseFastMs = v (kFast, 10.0f);
        s.times.averageMs = v (kAvg, 250.0f);
        s.times.releaseSlowMs = v (kSlow, 250.0f);
        s.times.rmsDetectMs = v (kRms);
        s.previewMs = v (kPreview);
        s.band.type = (FilterType) juce::jlimit (0, 3, (int) v (kFilter));
        s.band.frequency = v (kFreq, 1000.0f);
        s.band.bandwidthOct = v (kBw, 1.0f);
        s.parallel = v (kParallel) > 0.5f;
        s.mix = 1.0;
    }

    out.monitor = e.monitor->load() > 0.5f;
    out.link = e.link->load() > 0.5f;
    out.midSide = e.midSide->load() > 0.5f;
    out.ganged = e.ganged->load() > 0.5f;
    out.selectedChannel = juce::jlimit (0, 1, (int) e.selChannel->load());
    out.limiterOn = e.limiter->load() > 0.5f;
    out.limiterType = (LimiterType) juce::jlimit (0, 2, (int) e.limType->load());
    out.ceilingDb = e.ceiling->load();
    out.preLimiterDb = e.preLimiter->load();
    out.externalSidechain = (int) e.scSource->load() == 1;
    out.sidechainFilter = e.scFilter->load() > 0.5f;
}

ChainParams ParameterManager::read() const noexcept
{
    ChainParams p;
    readEngine (dyn, p.dynamics);

    static constexpr int ditherBits[] = { 0, 16, 20, 24 };
    p.inputDb = inGain->load();
    p.outputDb = outGain->load();
    p.bypass = globalBypass->load() > 0.5f;
    p.ditherBits = ditherBits[juce::jlimit (0, 3, (int) dither->load())];
    p.ditherShape = juce::jlimit (0, 3, (int) ditherType->load());
    p.truePeakMeters = (int) peakMeter->load() == 1;
    return p;
}
} // namespace dsq
