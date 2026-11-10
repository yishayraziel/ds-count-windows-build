#include "Panels.h"

namespace dsq
{
static void place (juce::Component& c, float x, float y, float w, float h) { c.setBounds (juce::Rectangle<float> (x, y, w, h).toNearestInt()); }
static constexpr float kRow = 33.0f;   // dialog row pitch

// ---- Graph windows ---------------------------------------------------------------------

static juce::Rectangle<float> clientOf (juce::Rectangle<float> frame)
{
    if (theme::classic())
        return frame.withTrimmedTop (27.0f).reduced (1.0f, 0.0f).withTrimmedBottom (1.0f);
    return frame.reduced (4.0f).withTrimmedTop (26.0f);
}

std::vector<juce::Rectangle<int>> GraphWindows::layout (juce::Rectangle<int> page, std::initializer_list<std::pair<juce::String, int>> spec)
{
    windows.clear();
    const float gap = 8.0f, shadow = theme::classic() ? 2.0f : 0.0f;
    float fixed = 0.0f;
    int flexible = 0;
    for (auto& s : spec)
        if (s.second > 0) fixed += (float) s.second; else ++flexible;
    const float rest = (float) page.getWidth() - fixed - gap * (float) (spec.size() - 1);
    float x = (float) page.getX();
    std::vector<juce::Rectangle<int>> plots;
    for (auto& s : spec)
    {
        const float w = s.second > 0 ? (float) s.second : rest / (float) std::max (1, flexible);
        auto frame = juce::Rectangle<float> (x, (float) page.getY(), w - shadow, (float) page.getHeight() - shadow);
        windows.push_back ({ frame, s.first });
        plots.push_back (clientOf (frame).reduced (6.0f).toNearestInt());
        x += w + gap;
    }
    return plots;
}

void GraphWindows::paint (juce::Graphics& g) const
{
    for (auto& w : windows)
        theme::window (g, w.frame, w.title, theme::classic(), true, w.frame.getWidth() > 400.0f);
}

// ---- ModulePanel -----------------------------------------------------------------------

ModulePanel::ModulePanel (DSCountProcessor& p, juce::String t, juce::String s)
    : proc (p), um (&p.undo), title (std::move (t)), subtitle (std::move (s))
{
    lcd.onPaint = [this] (juce::Graphics& g) { windows.paint (g); };
}

void ModulePanel::resized() {}

juce::String ModulePanel::latencyText() const
{
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    return juce::String (proc.chain.latencySamples() * 1000.0 / sr, 1) + " ms";
}

void ModulePanel::paint (juce::Graphics&) {}

// ---- DynamicsPanel ---------------------------------------------------------------------

DynamicsPanel::DynamicsPanel (DSCountProcessor& p)
    : ModulePanel (p, "Dynamics",
                   "Two-channel dynamics with dual release, look-ahead, band-selective processing and parallel mode"),
      prefix (ids::dynamics),
      channel (juce::StringArray { "Ch 1 (Left)", "Ch 2 (Right)" }, um),
      monitor ({ "Off", "Detector" }, um, Segment::Style::popup),
      scSource ({ "Internal", "External" }, um, Segment::Style::radio),
      filterType ({ "Full", "Low Pass", "Band Pass", "High Pass" }, um, Segment::Style::popup),
      limType ({ "Safe", "Type 1", "Type 2 TP" }, um, Segment::Style::popup),
      ganged ("Ganged", um), link ("Link", um), midSide ("Mid/Side", um), scFilter ("SC Filter", um),
      copy ("Copy 1 > 2", um), autoMakeup ("Max make-up", um),
      parallel ("Parallel", um), limiter ("On", um),
      threshold ("Threshold", um), ratio ("Ratio", um), knee ("Knee", um), attack ("Attack", um),
      lookahead ("Lookahead", um), makeup ("Make-up", um),
      relDelay ("Rel Delay", um), relFast ("Rel Fast", um), average ("Average", um),
      relSlow ("Rel Slow", um), rms ("RMS", um),
      frequency ("Frequency", um), bandwidth ("Width", um), preLimiter ("Gain", um), ceiling ("Ceiling", um)
{
    for (auto* c : std::initializer_list<juce::Component*> { &transfer, &history, &bandGraph })
        lcd.addAndMakeVisible (c);
    lcd.onResize = [this]
    {
        auto plots = windows.layout (lcd.getLocalBounds(), { { "Transfer Curve", 250 }, { "Level / Gain Reduction", 0 }, { "Band Filter", 230 } });
        transfer.setBounds (plots[0]);
        history.setBounds (plots[1]);
        bandGraph.setBounds (plots[2]);
    };

    for (auto* c : std::initializer_list<juce::Component*> { &channel, &monitor, &scSource, &filterType, &limType,
                                                            &ganged, &link, &midSide, &scFilter, &copy, &autoMakeup, &limiter,
                                                            &threshold, &ratio, &knee, &attack, &lookahead, &makeup,
                                                            &relDelay, &relFast, &average, &relSlow, &rms,
                                                            &frequency, &bandwidth, &preLimiter, &ceiling })
        addAndMakeVisible (c);
    addAndMakeVisible (parallel);

    for (auto* t : { &ganged, &link, &midSide, &scFilter, &autoMakeup, &parallel, &limiter })
        t->setShowLamp (true);
    for (auto* k : { &frequency, &bandwidth })
    {
        k->setMode (Knob::Mode::field);
        k->setFieldWidth (92.0f);
    }
    for (auto* k : { &preLimiter, &ceiling })
        k->setLabelWidth (96.0f);

    channel.bindTo (param (gid (prefix, ids::selChannel)));
    monitor.bindTo (param (gid (prefix, ids::monitor)));
    scSource.bindTo (param (gid (prefix, ids::scSource)));
    limType.bindTo (param (gid (prefix, ids::limType)));
    ganged.bindTo (param (gid (prefix, ids::ganged)));
    link.bindTo (param (gid (prefix, ids::link)));
    midSide.bindTo (param (gid (prefix, ids::midSide)));
    scFilter.bindTo (param (gid (prefix, ids::scFilter)));
    limiter.bindTo (param (gid (prefix, ids::limiter)));
    preLimiter.bindTo (param (gid (prefix, ids::preLimiter)));
    ceiling.bindTo (param (gid (prefix, ids::ceiling)));

    ganged.onClick = [this] { if (auto* s = param (gid (prefix, ids::selChannel))) s->setValueNotifyingHost (0.0f); };
    copy.onClick = [this]
    {
        const int from = std::max (0, bound), to = 1 - from;
        um->beginNewTransaction();
        proc.presets.copyChannel (prefix, from, to);
    };
    monitor.setTooltip ("Detector: listen to the band being processed (the sibilance when de-essing)");
    filterType.setTooltip ("Full band, or only the selected band is detected and turned down (Band Pass / High Pass for de-essing)");
    scFilter.setTooltip ("High-pass filters the sidechain so that low frequencies drive the detector less");
    autoMakeup.setTooltip ("Max make-up: the gain that brings a 0 dBFS input back to 0 dBFS");
    setupMakeup (makeup, -1);
    bindChannel (0);
}

void DynamicsPanel::setupMakeup (Knob& k, int fixedChannel)
{
    // fixedChannel < 0: the knob follows the edited channel.
    auto ch = [this, fixedChannel] { return fixedChannel >= 0 ? fixedChannel : std::max (0, bound); };
    k.onUserChange = [this, ch] (float v) { updateMakeupRule (ch(), v); };
    k.valueOverride = [this, ch]
    {
        const auto& c = shown.ch[ch()].curve;
        return c.autoMakeup ? "Max " + dbText ((float) maxMakeupDb (c)) : juce::String();
    };
    k.positionOverride = [this, ch]
    {
        const auto& c = shown.ch[ch()].curve;
        return c.autoMakeup ? (float) maxMakeupDb (c) : std::numeric_limits<float>::quiet_NaN();
    };
}

void DynamicsPanel::bindChannel (int c)
{
    if (c == bound)
        return;
    bound = c;
    auto id = [this] (int ch, const char* s) { return param (cid (prefix, ch, s)); };
    const int a = c;
    threshold.bindTo (id (a, ids::threshold));
    ratio.bindTo (id (a, ids::ratio));
    knee.bindTo (id (a, ids::knee));
    attack.bindTo (id (a, ids::attack));
    lookahead.bindTo (id (a, ids::preview));
    makeup.bindTo (id (a, ids::makeup));
    autoMakeup.bindTo (id (a, ids::autoMakeup));
    parallel.bindTo (id (c, ids::parallel));
    relDelay.bindTo (id (c, ids::relDelay));
    relFast.bindTo (id (c, ids::relFast));
    average.bindTo (id (c, ids::average));
    relSlow.bindTo (id (c, ids::relSlow));
    rms.bindTo (id (c, ids::rmsDetect));
    filterType.bindTo (id (c, ids::filterType));
    frequency.bindTo (id (c, ids::frequency));
    bandwidth.bindTo (id (c, ids::bandwidth));
    copy.setText (c == 0 ? "Copy 1 > 2" : "Copy 2 > 1");
}

void DynamicsPanel::updateMakeupRule (int c, float plain)
{
    // Max leaves when make-up is turned below the max value and engages when the knob reaches it.
    const auto s = proc.params.read().dynamics;
    const auto& ch = s.ch[c];
    auto* p = param (cid (prefix, c, ids::autoMakeup));
    const bool on = p->getValue() > 0.5f;
    const bool canLock = ch.band.type == FilterType::fullBand && ! ch.parallel && ch.curve.slope < 1.0;
    const bool next = nextAutoMakeup (on, plain, maxMakeupDb (ch.curve), canLock);
    if (next != on)
        p->setValueNotifyingHost (next ? 1.0f : 0.0f);
}

void DynamicsPanel::resized()
{
    ModulePanel::resized();
    lcd.resized();
    place (copy, 1450, 14, 134, 34);
    const float cy = 24.0f;   // channel row
    place (channel, 28, cy, 330, 30);
    place (ganged, 372, cy, 116, 30);
    place (link, 498, cy, 76, 30);
    place (midSide, 584, cy, 120, 30);
    place (monitor, 816, cy + 2, 140, 26);

    Knob* a[] = { &threshold, &ratio, &knee, &attack, &lookahead, &makeup };
    for (int i = 0; i < 6; ++i)
        place (*a[i], 26, 96.0f + i * kRow, 448, 30);
    place (autoMakeup, 300, 96.0f + 6 * kRow, 174, 30);

    Knob* r[] = { &relDelay, &relFast, &average, &relSlow, &rms };
    for (int i = 0; i < 5; ++i)
        place (*r[i], 513, 96.0f + i * kRow, 452, 30);
    place (parallel, 513, 96.0f + 5 * kRow, 130, 30);

    place (filterType, 640, 337, 150, 26);
    frequency.setLabelWidth (118.0f);
    frequency.setFieldWidth (theme::classic() ? 92.0f : 116.0f);
    place (frequency, 513, 372, 250, 30);
    bandwidth.setLabelWidth (60.0f);
    bandwidth.setFieldWidth (theme::classic() ? 90.0f : 110.0f);
    place (bandwidth, 775, 372, 190, 30);

    place (scSource, 1008, 172, 260, 30);
    place (scFilter, 1290, 172, 130, 30);
    place (limiter, 1008, 252, 70, 30);
    place (limType, 1150, 254, 150, 26);
    place (preLimiter, 1008, 296, 412, 30);
    place (ceiling, 1008, 336, 412, 30);
}

void DynamicsPanel::tick (const ChainParams& all)
{
    const auto& s = all.dynamics;
    shown = s;
    const int sel = s.ganged ? 0 : s.selectedChannel;
    bindChannel (sel);
    channel.setDimmed (s.ganged);
    copy.setDimmed (s.ganged);
    channel.setLabels (s.midSide ? juce::StringArray { "Ch 1 (Mid)", "Ch 2 (Side)" } : juce::StringArray { "Ch 1 (Left)", "Ch 2 (Right)" });

    auto& m = proc.chain.dynamics.meters;
    for (int c = 0; c < 2; ++c)
    {
        const float g = m.channels[(size_t) c].compGrDb.exchange (0.0f);
        gr[c] = g < gr[c] ? g : gr[c] + (g - gr[c]) * 0.25f;
    }
    const float lg = m.limGrDb.exchange (0.0f);
    limGr = lg < limGr ? lg : limGr + (lg - limGr) * 0.25f;
    auto lampF = [] (float& v, float in) { v = in > v ? in : v * 0.8f; };
    lampF (lampFast, juce::jlimit (0.0f, 1.0f, m.channels[(size_t) sel].releaseFast.load() * 2.0f));
    lampF (lampSlow, juce::jlimit (0.0f, 1.0f, m.channels[(size_t) sel].releaseSlow.load() * 2.0f));

    WaveColumn cols[128];
    const int n = m.popColumns (cols, 128);
    history.update (cols, n);

    const auto& ch = s.ch[sel];
    transfer.setData (ch.curve, m.channels[(size_t) sel].envelopeDb.load(), gr[sel]);
    bandGraph.setSpec (ch.band, proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0, proc.chain.isLowLatency());

    frequency.setDimmed (ch.band.type == FilterType::fullBand);
    bandwidth.setDimmed (ch.band.type != FilterType::bandPass);
    preLimiter.setDimmed (! s.limiterOn);
    ceiling.setDimmed (! s.limiterOn);
    limType.setDimmed (! s.limiterOn);
    repaint();
}

juce::StringArray DynamicsPanel::info() const
{
    const juce::String gainText = "Gain reduction " + dbText (std::min (gr[0], gr[1])) + " dB";
    const auto& b = shown.ch[std::max (0, bound)].band;
    static const char* const bands[] = { "Full band", "Low band", "Band pass", "High band" };
    juce::String mid = shown.ganged ? "Ch 1 and Ch 2 ganged" : "Editing Ch " + juce::String (std::max (0, bound) + 1);
    if (b.type != FilterType::fullBand)
        mid << ", " << bands[(int) b.type] << " " << juce::String (b.frequency >= 1000.0 ? b.frequency / 1000.0 : b.frequency, b.frequency >= 1000.0 ? 1 : 0)
            << (b.frequency >= 1000.0 ? " kHz" : " Hz");
    return { gainText, mid, "Latency " + latencyText() };
}

void DynamicsPanel::paint (juce::Graphics& g)
{
    auto label = [&g] (const juce::String& s, float x, float y, float w, bool bold = true, bool dim = false)
    { theme::text (g, s, { x, y, w, 30.0f }, juce::Justification::centredLeft, bold, dim); };
    auto grRow = [&g] (const juce::String& name, float x, float y, float w, float nameW, float dB, float range)
    {
        theme::text (g, name, { x, y, nameW, 26.0f }, juce::Justification::centredLeft);
        theme::hbar (g, { x + nameW, y + 1.0f, w - nameW - 88.0f, 24.0f }, -dB / range);
        theme::readout (g, { x + w - 80.0f, y, 80.0f, 26.0f }, dbText (dB));
    };
    const char* n1 = shown.midSide ? "Ch 1 (Mid)" : "Ch 1 (Left)";
    const char* n2 = shown.midSide ? "Ch 2 (Side)" : "Ch 2 (Right)";

    theme::groupBox (g, { 12, 14, 966, 50 }, "Channels");
    label ("Monitor", 728, 24, 86);
    theme::groupBox (g, { 12, 80, 475, 340 }, "Gain Computer");
    theme::groupBox (g, { 499, 80, 479, 236 }, "Release");
    label ("Activity:", 664, 96.0f + 5 * kRow, 100, false);
    theme::lamp (g, { 760, 96.0f + 5 * kRow + 9, 13, 13 }, lampFast > 0.3f);
    label ("Fast", 780, 96.0f + 5 * kRow, 50, false);
    theme::lamp (g, { 846, 96.0f + 5 * kRow + 9, 13, 13 }, lampSlow > 0.3f);
    label ("Slow", 866, 96.0f + 5 * kRow, 50, false);
    theme::groupBox (g, { 499, 328, 479, 92 }, "Band");
    label ("Filter", 513, 335, 120);

    theme::groupBox (g, { 994, 14, 440, 128 }, "Gain Reduction");
    grRow (n1, 1008, 28, 412, 126, gr[0], 24.0f);
    grRow (n2, 1008, 66, 412, 126, gr[1], 24.0f);
    grRow ("Limiter", 1008, 104, 412, 126, limGr, 12.0f);
    theme::groupBox (g, { 994, 158, 440, 58 }, "Sidechain");
    theme::groupBox (g, { 994, 232, 440, 188 }, "Output Limiter");
    label ("Type", 1096, 252, 50, true, ! shown.limiterOn);
}
} // namespace dsq
