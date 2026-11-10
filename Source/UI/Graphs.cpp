#include "Graphs.h"
#include "../DSP/LinearPhaseCrossover.h"

namespace dsq
{
juce::String dbText (float dB, int decimals)
{
    if (dB <= -119.0f)
        return "-inf";
    auto s = juce::String (dB, decimals);
    if (s.getDoubleValue() == 0.0)
        s = s.trimCharactersAtStart ("-");
    return s;
}

// ---- TransferGraph ---------------------------------------------------------------------

void TransferGraph::setData (const TransferParams& c, float e, float g)
{
    curve = c;
    envDb = e;
    gr = g;
    repaint();
}

// ---- HistoryGraph ----------------------------------------------------------------------

HistoryGraph::HistoryGraph (Mode m) : mode (m) { history.resize (1200); }

void HistoryGraph::clear()
{
    std::fill (history.begin(), history.end(), WaveColumn {});
    repaint();
}

void HistoryGraph::push (const WaveColumn* c, int n)
{
    for (int i = 0; i < n; ++i)
    {
        history[(size_t) pos] = c[i];
        pos = (pos + 1) % (int) history.size();
    }
    if (n > 0)
        repaint();
}

void HistoryGraph::update (const WaveColumn* c, int n)
{
    if (n > 0)
    {
        idleTicks = 0;
        push (c, n);
        return;
    }
    // About 90 columns a second at a 30 Hz display rate.
    if (++idleTicks > 6)
    {
        const WaveColumn silent[3] {};
        push (silent, 3);
    }
}

// ---- BandGraph -------------------------------------------------------------------------

void BandGraph::setSpec (const BandSpec& spec, double sampleRate, bool lowLatency)
{
    if (valid && spec == shown && sampleRate == sr && lowLatency == lowLatencyShown)
        return;
    shown = spec;
    sr = sampleRate;
    lowLatencyShown = lowLatency;
    valid = true;
    curve.assign (160, 0.0f);
    if (spec.type != FilterType::fullBand)
    {
        const auto config = LinearPhaseCrossover::configFor (sr, lowLatency);
        std::vector<double> h;
        LinearPhaseCrossover::designForDisplay (h, spec, sr, config);
        constexpr int n = 1 << 16;
        FFT f;
        f.prepare (n);
        std::vector<FFT::Complex> d ((size_t) n);
        for (size_t i = 0; i < h.size() && i < (size_t) n; ++i)
            d[i] = h[i];
        f.forward (d.data());
        for (int i = 0; i < 160; ++i)
        {
            const double hz = 20.0 * std::pow (1000.0, i / 159.0);
            const int bin = juce::jlimit (0, n / 2, (int) (hz / sr * n + 0.5));
            curve[(size_t) i] = (float) gainToDb (std::abs (d[(size_t) bin]));
        }
    }
    repaint();
}

// ---- Painting (both skins) -------------------------------------------------------------

static void scaleText (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Justification j)
{
    g.setColour (theme::ink().text);
    g.setFont (theme::small (12.0f));
    g.drawText (s, r, j, false);
}

/** Thick accent line: yellow in 3.11, black with a white core in Classic. */
static void accentStroke (juce::Graphics& g, const juce::Path& p, float width)
{
    if (theme::classic())
    {
        g.setColour (theme::c::black);
        g.strokePath (p, juce::PathStrokeType (width + 3.0f));
        g.setColour (theme::c::white);
        g.strokePath (p, juce::PathStrokeType (1.5f));
        return;
    }
    g.setColour (theme::c::yellow);
    g.strokePath (p, juce::PathStrokeType (width));
}

static void dashed (juce::Graphics& g, const juce::Path& p, float width, juce::Colour col, float on = 5.0f, float off = 4.0f)
{
    juce::Path d;
    const float dashes[] = { on, off };
    juce::PathStrokeType (width).createDashedStroke (d, p, dashes, 2);
    g.setColour (col);
    g.fillPath (d);
}

void TransferGraph::paint (juce::Graphics& g)
{
    const auto k = theme::ink();
    auto area = theme::plot (g, getLocalBounds().toFloat());
    auto r = area.withTrimmedLeft (34.0f).withTrimmedBottom (20.0f).withTrimmedRight (10.0f).withTrimmedTop (10.0f);
    r = r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight()), std::min (r.getWidth(), r.getHeight()));
    const float lo = -60.0f, hi = 0.0f;
    auto px = [&] (float dB) { return r.getX() + (dB - lo) / (hi - lo) * r.getWidth(); };
    auto py = [&] (float dB) { return r.getBottom() - (juce::jlimit (lo - 6.0f, hi + 6.0f, dB) - lo) / (hi - lo) * r.getHeight(); };

    for (int d = -60; d <= 0; d += 15)
    {
        theme::gridLine (g, { px ((float) d), r.getY() }, { px ((float) d), r.getBottom() });
        theme::gridLine (g, { r.getX(), py ((float) d) }, { r.getRight(), py ((float) d) });
        if (d % 30 == 0 || ! theme::classic())
        {
            scaleText (g, juce::String (d), { r.getX() - 34.0f, py ((float) d) - 7.0f, 30.0f, 14.0f }, juce::Justification::centredRight);
            scaleText (g, juce::String (d), { px ((float) d) - 16.0f, r.getBottom() + 4.0f, 32.0f, 14.0f }, juce::Justification::centred);
        }
    }
    juce::Path diag;
    diag.startNewSubPath (px (lo), py (lo));
    diag.lineTo (px (hi), py (hi));
    dashed (g, diag, 1.0f, k.dim, 3.0f, 3.0f);

    g.saveState();
    g.reduceClipRegion (r.expanded (2.0f).toNearestInt());
    if (curve.thresholdDb > lo)
    {
        juce::Path t;
        t.startNewSubPath (px ((float) curve.thresholdDb), r.getY());
        t.lineTo (px ((float) curve.thresholdDb), r.getBottom());
        dashed (g, t, 1.0f, theme::classic() ? theme::c::black : theme::c::cyan, 4.0f, 3.0f);
    }
    juce::Path p;
    for (int i = 0; i <= 240; ++i)
    {
        const float x = lo + (hi - lo) * (float) i / 240.0f;
        const float y = (float) transferOutputDb (x, curve);
        if (i == 0) p.startNewSubPath (px (x), py (y));
        else        p.lineTo (px (x), py (y));
    }
    g.setColour (k.line);
    g.strokePath (p, juce::PathStrokeType (theme::classic() ? 3.0f : 2.5f));
    if (envDb > lo)
    {
        const float yv = (float) transferOutputDb (envDb, curve);
        auto dot = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ px (envDb), py (yv) });
        if (theme::classic())
        {
            g.setColour (theme::c::white);
            g.fillRect (dot);
            g.setColour (theme::c::black);
            g.drawRect (dot, 2.0f);
        }
        else
        {
            g.setColour (theme::c::yellow);
            g.fillRect (dot);
        }
    }
    g.restoreState();

    const auto tag = "GR " + dbText (gr) + " dB";
    auto tr = juce::Rectangle<float> (r.getX() + 6.0f, r.getY() + 6.0f, theme::textWidth (tag) + 14.0f, 24.0f);
    if (theme::classic())
    {
        g.setColour (theme::c::black);
        g.fillRect (tr);
        theme::text (g, tag, tr, juce::Justification::centred, true, false, theme::c::white);
    }
    else
        theme::text (g, tag, tr, juce::Justification::centredLeft, true, false, theme::c::yellow);
}

void HistoryGraph::paint (juce::Graphics& g)
{
    const auto k = theme::ink();
    auto area = theme::plot (g, getLocalBounds().toFloat());
    auto r = area.withTrimmedLeft (34.0f).withTrimmedBottom (20.0f).withTrimmedRight (8.0f).withTrimmedTop (10.0f);
    const int columns = std::min ((int) history.size(), (int) r.getWidth());
    auto level = [] (float lin) { return juce::jlimit (0.0f, 1.0f, ((float) gainToDb (lin) + 60.0f) / 60.0f); };

    for (int i = 0; i <= 4; ++i)
    {
        const float y = r.getY() + (float) i / 4.0f * r.getHeight();
        theme::gridLine (g, { r.getX(), y }, { r.getRight(), y });
        const int d = juce::roundToInt (grRange * (float) i / 4.0f);
        scaleText (g, d == 0 ? juce::String ("0") : "-" + juce::String (d), { r.getX() - 34.0f, y - 7.0f, 30.0f, 14.0f }, juce::Justification::centredRight);
    }
    const float seconds = (float) columns / 90.0f;
    scaleText (g, juce::String (seconds, 1) + " s", { r.getX(), r.getBottom() + 4.0f, 60.0f, 14.0f }, juce::Justification::centredLeft);
    scaleText (g, "now", { r.getRight() - 40.0f, r.getBottom() + 4.0f, 40.0f, 14.0f }, juce::Justification::centredRight);

    auto at = [&] (int i) -> const WaveColumn& { return history[(size_t) ((pos - columns + i + (int) history.size() * 2) % (int) history.size())]; };
    const float x0 = r.getRight() - (float) columns;
    // Level areas: peak (or input) behind, RMS (or output) in front.
    juce::Path back, front;
    back.startNewSubPath (x0, r.getBottom());
    front.startNewSubPath (x0, r.getBottom());
    for (int i = 0; i < columns; ++i)
    {
        const auto& c = at (i);
        back.lineTo (x0 + (float) i, r.getBottom() - level (c.peak) * r.getHeight());
        front.lineTo (x0 + (float) i, r.getBottom() - level (c.rms) * r.getHeight());
    }
    back.lineTo (r.getRight(), r.getBottom());
    front.lineTo (r.getRight(), r.getBottom());
    back.closeSubPath();
    front.closeSubPath();
    if (theme::classic())
    {
        theme::plotFill (g, back, mode == Mode::maximizer);
        if (mode == Mode::maximizer)
            theme::plotFill (g, front, false);
    }
    else
    {
        g.setColour (mode == Mode::maximizer ? juce::Colour (0xff005000) : theme::c::green);
        g.fillPath (back);
        g.setColour (mode == Mode::maximizer ? juce::Colour (0xff00a000) : juce::Colour (0xff00c000));
        g.fillPath (front);
    }
    juce::Path p;
    for (int i = 0; i < columns; ++i)
    {
        const float y = r.getY() + juce::jlimit (0.0f, 1.0f, -at (i).grDb / grRange) * r.getHeight();
        if (i == 0) p.startNewSubPath (x0, y);
        else        p.lineTo (x0 + (float) i, y);
    }
    accentStroke (g, p, 2.2f);
    juce::ignoreUnused (k);
}

void BandGraph::paint (juce::Graphics& g)
{
    const auto k = theme::ink();
    auto area = theme::plot (g, getLocalBounds().toFloat());
    auto r = area.withTrimmedLeft (34.0f).withTrimmedBottom (20.0f).withTrimmedRight (8.0f).withTrimmedTop (10.0f);
    for (int d = 0; d <= 48; d += 12)
    {
        const float y = r.getY() + d / 48.0f * r.getHeight();
        theme::gridLine (g, { r.getX(), y }, { r.getRight(), y });
        if (d % 24 == 0 || ! theme::classic())
            scaleText (g, d == 0 ? juce::String ("0") : "-" + juce::String (d), { r.getX() - 34.0f, y - 7.0f, 30.0f, 14.0f }, juce::Justification::centredRight);
    }
    for (double hz : { 100.0, 1000.0, 10000.0 })
    {
        const float x = r.getX() + (float) (std::log10 (hz / 20.0) / 3.0) * r.getWidth();
        theme::gridLine (g, { x, r.getY() }, { x, r.getBottom() });
        scaleText (g, hz >= 1000 ? juce::String ((int) hz / 1000) + "k" : juce::String ((int) hz), { x - 15.0f, r.getBottom() + 4.0f, 30.0f, 14.0f }, juce::Justification::centred);
    }
    juce::Path p;
    for (int i = 0; i < (int) curve.size(); ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (float) (curve.size() - 1);
        const float y = r.getY() + juce::jlimit (0.0f, 1.0f, -curve[(size_t) i] / 48.0f) * r.getHeight();
        if (i == 0) p.startNewSubPath (x, y);
        else        p.lineTo (x, y);
    }
    g.setColour (k.line);
    g.strokePath (p, juce::PathStrokeType (theme::classic() ? 3.0f : 2.5f));
    if (shown.type == FilterType::fullBand)
    {
        auto t = r.withSizeKeepingCentre (110.0f, 26.0f);
        if (theme::classic())
        {
            g.setColour (theme::c::white);
            g.fillRect (t);
        }
        theme::text (g, "Full band", t, juce::Justification::centred, true, false, k.text);
    }
}
} // namespace dsq
