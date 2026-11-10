#include "Theme.h"
#include "BinaryData.h"

namespace dsq::theme
{
// ---- Preferences ----------------------------------------------------------------------

static std::atomic<int> gSkin { -1 }, gCrt { -1 }, gGeneration { 0 };

static juce::File settingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile ("DS-COUNT").getChildFile ("Settings.xml");
}

static void load()
{
    if (gSkin.load() >= 0)
        return;
    int s = 0, c = 1;
    if (auto xml = juce::XmlDocument::parse (settingsFile()))
    {
        s = juce::jlimit (0, 1, xml->getIntAttribute ("skin", 0));
        c = juce::jlimit (0, 2, xml->getIntAttribute ("crt", 1));
    }
    gSkin = s;
    gCrt = c;
}

static void save()
{
    juce::XmlElement xml ("DSCOUNT_SETTINGS");
    xml.setAttribute ("skin", gSkin.load());
    xml.setAttribute ("crt", gCrt.load());
    auto f = settingsFile();
    f.getParentDirectory().createDirectory();
    xml.writeTo (f);
}

Skin skin() { load(); return (Skin) gSkin.load(); }
Crt crt() { load(); return (Crt) gCrt.load(); }
int preferenceGeneration() { return gGeneration.load(); }
void setSkin (Skin s) { load(); gSkin = (int) s; ++gGeneration; save(); }
void setCrt (Crt v) { load(); gCrt = (int) v; ++gGeneration; save(); }
void setSkinForTest (Skin s) { load(); gSkin = (int) s; ++gGeneration; }
void setCrtForTest (Crt v) { load(); gCrt = (int) v; ++gGeneration; }

// ---- Fonts ----------------------------------------------------------------------------

static juce::Typeface::Ptr face (const char* data, int size) { return juce::Typeface::createSystemTypefaceFor (data, (size_t) size); }
static juce::Typeface::Ptr systemBold()    { static auto t = face (BinaryData::DSCSystemBold_ttf, BinaryData::DSCSystemBold_ttfSize); return t; }
static juce::Typeface::Ptr systemRegular() { static auto t = face (BinaryData::DSCSystemRegular_ttf, BinaryData::DSCSystemRegular_ttfSize); return t; }
static juce::Typeface::Ptr chicago()       { static auto t = face (BinaryData::ChicagoFLF_ttf, BinaryData::ChicagoFLF_ttfSize); return t; }

juce::Font ui (float pt, bool bold)
{
    auto t = bold ? (classic() ? chicago() : systemBold()) : systemRegular();
    return juce::Font (juce::FontOptions (t).withPointHeight (pt));
}

juce::Font small (float pt) { return juce::Font (juce::FontOptions (systemRegular()).withPointHeight (pt)); }

static juce::Image dither (juce::Colour ink)
{
    juce::Image im (juce::Image::ARGB, 2, 2, true);
    im.setPixelAt (0, 0, ink);
    im.setPixelAt (1, 1, ink);
    return im;
}

void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Justification j, bool bold, bool disabled,
           juce::Colour col, float pt)
{
    g.setFont (ui (pt, bold));
    if (! disabled)
    {
        g.setColour (col);
        g.drawText (s, r, j, false);
        return;
    }
    if (classic())
    {
        g.saveState();
        g.setFillType (juce::FillType (dither (col), {}));
        g.drawText (s, r, j, false);
        g.restoreState();
    }
    else
    {
        g.setColour (c::white);
        g.drawText (s, r.translated (1.0f, 1.0f), j, false);
        g.setColour (c::gray);
        g.drawText (s, r, j, false);
    }
}

float textWidth (const juce::String& s, bool bold, float pt)
{
    return juce::GlyphArrangement::getStringWidth (ui (pt, bold), s);
}

// ---- Chrome ---------------------------------------------------------------------------

static void bevel (juce::Graphics& g, juce::Rectangle<float> r, bool raised, float w = 2.0f)
{
    const auto hi = raised ? c::white : c::gray, lo = raised ? c::gray : c::white;
    g.setColour (hi);
    g.fillRect (r.getX(), r.getY(), r.getWidth(), w);
    g.fillRect (r.getX(), r.getY(), w, r.getHeight());
    g.setColour (lo);
    g.fillRect (r.getX(), r.getBottom() - w, r.getWidth(), w);
    g.fillRect (r.getRight() - w, r.getY(), w, r.getHeight());
}

static void outline (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col = c::black, float w = 1.0f)
{
    g.setColour (col);
    g.drawRect (r, w);
}

static void triangle (juce::Graphics& g, juce::Rectangle<float> r, int dir, juce::Colour col, bool hollow = false)
{
    // dir: 0 up, 1 right, 2 down, 3 left
    juce::Path p;
    const auto cx = r.getCentreX(), cy = r.getCentreY(), w = r.getWidth() * 0.5f, h = r.getHeight() * 0.5f;
    if (dir == 0) p.addTriangle (cx - w, cy + h, cx + w, cy + h, cx, cy - h);
    if (dir == 2) p.addTriangle (cx - w, cy - h, cx + w, cy - h, cx, cy + h);
    if (dir == 1) p.addTriangle (cx - w, cy - h, cx - w, cy + h, cx + w, cy);
    if (dir == 3) p.addTriangle (cx + w, cy - h, cx + w, cy + h, cx - w, cy);
    g.setColour (col);
    if (hollow)
        g.strokePath (p, juce::PathStrokeType (1.0f));
    else
        g.fillPath (p);
}

/** The 3.11 scroll arrow: a filled arrow with a stem. */
static void stemArrow (juce::Graphics& g, juce::Point<float> c0, int dir, juce::Colour col)
{
    juce::Path p;
    p.startNewSubPath (-4.5f, 0); p.lineTo (-0.5f, -4); p.lineTo (-0.5f, -1.5f); p.lineTo (4.5f, -1.5f);
    p.lineTo (4.5f, 1.5f); p.lineTo (-0.5f, 1.5f); p.lineTo (-0.5f, 4); p.closeSubPath();
    const float angle = dir == 1 ? juce::MathConstants<float>::pi : dir == 0 ? juce::MathConstants<float>::halfPi
                      : dir == 2 ? -juce::MathConstants<float>::halfPi : 0.0f;
    g.setColour (col);
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c0));
}

void desktop (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (classic())
    {
        g.setColour (c::white);
        g.fillRect (r);
        g.saveState();
        g.setFillType (juce::FillType (dither (c::black), {}));
        g.fillRect (r);
        g.restoreState();
    }
    else
    {
        g.setColour (c::white);
        g.fillRect (r);
    }
}

juce::Rectangle<float> window (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, bool active, bool clientGray, bool minMax)
{
    if (classic())
    {
        g.setColour (c::black);
        g.fillRect (r.translated (2.0f, 2.0f));
        g.setColour (c::white);
        g.fillRect (r);
        outline (g, r);
        auto bar = r.withHeight (26.0f);
        g.setColour (c::black);
        g.drawHorizontalLine ((int) bar.getBottom(), r.getX(), r.getRight());
        if (active)
            for (float y = bar.getY() + 6.0f; y < bar.getBottom() - 5.0f; y += 2.0f)
                g.fillRect (bar.getX() + 3.0f, y, bar.getWidth() - 6.0f, 1.0f);
        if (title.isNotEmpty())
        {
            const float w = textWidth (title) + 20.0f;
            auto t = bar.withSizeKeepingCentre (w, 24.0f);
            g.setColour (c::white);
            g.fillRect (t);
            text (g, title, t, juce::Justification::centred);
        }
        if (active)
        {
            auto box = juce::Rectangle<float> (bar.getX() + 12.0f, bar.getY() + 5.0f, 16.0f, 15.0f);
            g.setColour (c::white);
            g.fillRect (box.expanded (2.0f));
            g.setColour (c::white);
            g.fillRect (box);
            outline (g, box);
        }
        return r.withTrimmedTop (27.0f).reduced (1.0f, 0.0f).withTrimmedBottom (1.0f);
    }

    g.setColour (c::silver);
    g.fillRect (r);
    outline (g, r);
    auto in = r.reduced (4.0f);
    outline (g, in.expanded (1.0f));
    auto bar = in.removeFromTop (25.0f);
    g.setColour (active ? c::navy : c::white);
    g.fillRect (bar);
    g.setColour (c::black);
    g.drawHorizontalLine ((int) bar.getBottom(), bar.getX(), bar.getRight());
    in.removeFromTop (1.0f);
    // Control-menu box.
    auto cm = bar.removeFromLeft (25.0f);
    g.setColour (c::silver);
    g.fillRect (cm);
    g.setColour (c::black);
    g.drawVerticalLine ((int) cm.getRight(), cm.getY(), cm.getBottom());
    auto barIcon = cm.withSizeKeepingCentre (13.0f, 4.0f);
    g.setColour (c::gray);
    g.fillRect (barIcon.translated (1.0f, 1.0f));
    g.setColour (c::white);
    g.fillRect (barIcon);
    outline (g, barIcon);
    if (minMax)
        for (int i = 0; i < 2; ++i)
        {
            auto b = bar.removeFromRight (25.0f);
            g.setColour (c::silver);
            g.fillRect (b);
            bevel (g, b.withTrimmedLeft (1.0f), true, 1.0f);
            g.setColour (c::black);
            g.drawVerticalLine ((int) b.getX(), b.getY(), b.getBottom());
            triangle (g, b.withSizeKeepingCentre (9.0f, 5.0f), i == 0 ? 0 : 2, c::black);
        }
    text (g, title, bar, juce::Justification::centred, true, false, active ? c::white : c::black);
    g.setColour (clientGray ? c::silver : c::white);
    g.fillRect (in);
    return in;
}

void groupBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    outline (g, r);
    if (title.isEmpty())
        return;
    const float w = textWidth (title) + 10.0f;
    auto cap = juce::Rectangle<float> (r.getX() + 8.0f, r.getY() - 11.0f, w, 22.0f);
    g.setColour (classic() ? c::white : c::silver);
    g.fillRect (cap.withTrimmedTop (9.0f).withHeight (3.0f));
    text (g, title, cap, juce::Justification::centred);
}

void button (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, bool pressed, bool disabled, bool isDefault)
{
    if (classic())
    {
        const float rad = std::min (11.0f, r.getHeight() * 0.4f);
        if (isDefault)
        {
            g.setColour (c::black);
            g.drawRoundedRectangle (r.expanded (3.5f), rad + 3.5f, 3.0f);
        }
        g.setColour (pressed ? c::black : c::white);
        g.fillRoundedRectangle (r, rad);
        g.setColour (c::black);
        g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
        text (g, label, r, juce::Justification::centred, true, disabled, pressed ? c::white : c::black);
        return;
    }
    g.setColour (c::silver);
    g.fillRoundedRectangle (r, 3.0f);
    auto in = r.reduced (isDefault ? 2.0f : 1.0f);
    if (pressed)
    {
        g.setColour (c::gray);
        g.fillRect (in.getX(), in.getY(), in.getWidth(), 1.0f);
        g.fillRect (in.getX(), in.getY(), 1.0f, in.getHeight());
        g.setColour (juce::Colour (0xffd8d8d8));
        g.fillRect (in.reduced (1.0f).withTrimmedLeft (0.0f));
    }
    else
        bevel (g, in, true, 2.0f);
    g.setColour (c::black);
    g.drawRoundedRectangle (r.reduced (isDefault ? 1.0f : 0.5f), 3.0f, isDefault ? 2.0f : 1.0f);
    text (g, label, pressed ? r.translated (1.0f, 1.0f) : r, juce::Justification::centred, true, disabled);
}

void tool (juce::Graphics& g, juce::Rectangle<float> r, bool pressed)
{
    g.setColour (pressed ? juce::Colour (0xffdcdcdc) : c::silver);
    g.fillRoundedRectangle (r, 3.0f);
    if (pressed)
    {
        g.setColour (c::gray);
        g.fillRect (r.getX() + 1.0f, r.getY() + 1.0f, r.getWidth() - 2.0f, 2.0f);
        g.fillRect (r.getX() + 1.0f, r.getY() + 1.0f, 2.0f, r.getHeight() - 2.0f);
    }
    else
        bevel (g, r.reduced (1.0f), true, 2.0f);
    g.setColour (c::black);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
}

void mark (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col)
{
    // 13 x 11 pixel stepped waveform.
    static const char* rows[] = { "....#........", "....#........", "....#..#.....", "....#..#.....",
                                  ".#..#..#..#..", ".#..#..#..#..", ".#..#..#..#..", ".#..#..#..#.#",
                                  "#############", "#############", "#############" };
    const float px = std::min (r.getWidth() / 13.0f, r.getHeight() / 11.0f);
    const auto o = r.getCentre() - juce::Point<float> (6.5f * px, 5.5f * px);
    g.setColour (col);
    for (int y = 0; y < 11; ++y)
        for (int x = 0; x < 13; ++x)
            if (rows[y][x] == '#')
                g.fillRect (o.x + x * px, o.y + y * px, px, px);
}

void checkbox (juce::Graphics& g, juce::Rectangle<float> b, bool on, bool disabled)
{
    g.setColour (c::white);
    g.fillRect (b);
    outline (g, b, disabled && ! classic() ? c::gray : c::black);
    if (on)
    {
        g.setColour (disabled ? c::gray : c::black);
        auto x = b.reduced (2.5f);
        g.drawLine ({ x.getTopLeft(), x.getBottomRight() }, 1.6f);
        g.drawLine ({ x.getTopRight(), x.getBottomLeft() }, 1.6f);
    }
}

void radio (juce::Graphics& g, juce::Rectangle<float> b, bool on, bool disabled)
{
    g.setColour (c::white);
    g.fillEllipse (b);
    g.setColour (disabled && ! classic() ? c::gray : c::black);
    g.drawEllipse (b.reduced (0.5f), 1.0f);
    if (on)
        g.fillEllipse (b.withSizeKeepingCentre (b.getWidth() * 0.45f, b.getHeight() * 0.45f));
}

void scrollbar (juce::Graphics& g, juce::Rectangle<float> r, float pos, bool disabled)
{
    const float a = r.getHeight();
    auto left = r.withWidth (a), right = r.withX (r.getRight() - a).withWidth (a);
    auto track = r.reduced (a, 0.0f);
    if (classic())
    {
        g.setColour (c::white);
        g.fillRect (r);
        if (! disabled)
        {
            g.saveState();
            g.setFillType (juce::FillType (dither (c::black), {}));
            g.fillRect (track);
            g.restoreState();
        }
        outline (g, r);
        g.setColour (c::black);
        g.drawVerticalLine ((int) left.getRight(), r.getY(), r.getBottom());
        g.drawVerticalLine ((int) right.getX(), r.getY(), r.getBottom());
        if (! disabled)
        {
            triangle (g, left.withSizeKeepingCentre (7.0f, 8.0f), 3, c::black, true);
            triangle (g, right.withSizeKeepingCentre (7.0f, 8.0f), 1, c::black, true);
        }
        if (pos >= 0.0f && ! disabled)
        {
            auto th = juce::Rectangle<float> (track.getX() + pos * (track.getWidth() - a), r.getY(), a, r.getHeight());
            g.setColour (c::white);
            g.fillRect (th);
            outline (g, th);
        }
        return;
    }
    g.setColour (c::silver);
    g.fillRect (r);
    for (auto b : { left, right })
    {
        bevel (g, b.reduced (1.0f), true, 1.0f);
    }
    outline (g, r);
    g.setColour (c::black);
    g.drawVerticalLine ((int) left.getRight(), r.getY(), r.getBottom());
    g.drawVerticalLine ((int) right.getX(), r.getY(), r.getBottom());
    if (! disabled)
    {
        stemArrow (g, left.getCentre(), 3, c::black);
        stemArrow (g, right.getCentre(), 1, c::black);
        if (pos >= 0.0f)
        {
            auto th = juce::Rectangle<float> (track.getX() + pos * (track.getWidth() - a), r.getY(), a, r.getHeight());
            g.setColour (c::silver);
            g.fillRect (th);
            bevel (g, th.reduced (1.0f, 1.0f), true, 1.0f);
            g.setColour (c::black);
            g.drawVerticalLine ((int) th.getX(), th.getY(), th.getBottom());
            g.drawVerticalLine ((int) th.getRight() - 1, th.getY(), th.getBottom());
        }
    }
}

void field (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, bool disabled)
{
    g.setColour (c::white);
    g.fillRect (r);
    outline (g, r, disabled && ! classic() ? c::gray : c::black);
    text (g, s, r.reduced (6.0f, 0.0f), juce::Justification::centredRight, ! classic(), disabled);
}

void spin (juce::Graphics& g, juce::Rectangle<float> r, bool disabled)
{
    auto up = r.withHeight (r.getHeight() * 0.5f), dn = up.translated (0.0f, up.getHeight());
    for (auto b : { up, dn })
    {
        g.setColour (c::silver);
        g.fillRect (b);
        bevel (g, b.reduced (1.0f), true, 1.0f);
    }
    outline (g, r, disabled ? c::gray : c::black);
    g.setColour (disabled ? c::gray : c::black);
    g.drawHorizontalLine ((int) up.getBottom(), r.getX(), r.getRight());
    if (! disabled)
    {
        triangle (g, up.withSizeKeepingCentre (7.0f, 4.0f), 0, c::black);
        triangle (g, dn.withSizeKeepingCentre (7.0f, 4.0f), 2, c::black);
    }
}

void popup (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, bool disabled)
{
    if (classic())
    {
        g.setColour (c::black);
        g.fillRect (r.translated (1.0f, 1.0f));
        g.setColour (c::white);
        g.fillRect (r);
        outline (g, r);
        text (g, s, r.reduced (8.0f, 0.0f).withTrimmedRight (16.0f), juce::Justification::centredLeft, true, disabled);
        triangle (g, juce::Rectangle<float> (r.getRight() - 20.0f, r.getCentreY() - 3.5f, 12.0f, 7.0f), 2, c::black);
        return;
    }
    auto btn = r.removeFromRight (20.0f);
    r.removeFromRight (2.0f);
    g.setColour (c::white);
    g.fillRect (r);
    outline (g, r, disabled ? c::gray : c::black);
    text (g, s, r.reduced (6.0f, 0.0f), juce::Justification::centredLeft, true, disabled);
    g.setColour (c::silver);
    g.fillRect (btn);
    bevel (g, btn.reduced (1.0f), true, 1.0f);
    outline (g, btn, disabled ? c::gray : c::black);
    const auto col = disabled ? c::gray : c::black;
    stemArrow (g, btn.getCentre().translated (0.0f, -1.5f), 2, col);
    g.setColour (col);
    g.fillRect (btn.getCentreX() - 4.5f, btn.getBottom() - 6.0f, 9.0f, 1.5f);
}

void readout (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, bool dim)
{
    g.setColour (c::black);
    g.fillRect (r);
    if (! classic())
    {
        g.setColour (c::gray);
        g.drawHorizontalLine ((int) r.getY(), r.getX(), r.getRight());
        g.drawVerticalLine ((int) r.getX(), r.getY(), r.getBottom());
        g.setColour (c::white);
        g.drawHorizontalLine ((int) r.getBottom() - 1, r.getX(), r.getRight());
        g.drawVerticalLine ((int) r.getRight() - 1, r.getY(), r.getBottom());
    }
    text (g, s, r.reduced (7.0f, 0.0f), juce::Justification::centredRight, true, false,
          classic() ? c::white : (dim ? c::maroon : c::red));
}

static void sunken (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (c::gray);
    g.drawHorizontalLine ((int) r.getY(), r.getX(), r.getRight());
    g.drawVerticalLine ((int) r.getX(), r.getY(), r.getBottom());
    g.setColour (c::white);
    g.drawHorizontalLine ((int) r.getBottom() - 1, r.getX(), r.getRight());
    g.drawVerticalLine ((int) r.getRight() - 1, r.getY(), r.getBottom());
}

void hbar (juce::Graphics& g, juce::Rectangle<float> r, float fraction, float marker, bool disabled, bool level)
{
    const bool cl = classic();
    g.setColour (cl ? c::white : c::black);
    g.fillRect (r);
    if (cl) outline (g, r); else sunken (g, r);
    auto in = r.reduced (3.0f);
    const int n = std::max (8, (int) (in.getWidth() / 12.0f));
    const float step = in.getWidth() / (float) n;
    const auto on = level ? c::lime : c::yellow;
    const auto off = level ? juce::Colour (0xff003000) : juce::Colour (0xff404000);
    for (int i = 0; i < n; ++i)
    {
        const bool lit = ! disabled && (i + 0.5f) / (float) n <= fraction;
        auto seg = juce::Rectangle<float> (in.getX() + i * step, in.getY(), step - 2.0f, in.getHeight());
        if (cl) { if (lit) { g.setColour (c::black); g.fillRect (seg); } }
        else { g.setColour (lit ? on : (disabled ? juce::Colour (0xff202020) : off)); g.fillRect (seg); }
    }
    if (marker >= 0.0f && ! disabled)
    {
        const float x = in.getX() + juce::jlimit (0.0f, 1.0f, marker) * in.getWidth();
        auto m = juce::Rectangle<float> (x - 1.5f, r.getY() - 4.0f, 3.0f, r.getHeight() + 8.0f);
        g.setColour (cl ? c::white : c::black);
        g.fillRect (m.expanded (1.0f, 0.0f));
        g.setColour (cl ? c::black : c::red);
        g.fillRect (m);
    }
}

void vmeter (juce::Graphics& g, juce::Rectangle<float> r, float level, float hold)
{
    const bool cl = classic();
    g.setColour (cl ? c::white : c::black);
    g.fillRect (r);
    if (cl) outline (g, r); else sunken (g, r);
    auto in = r.reduced (3.0f);
    const int n = 25;
    const float step = in.getHeight() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const float p = (i + 0.5f) / (float) n;
        const bool lit = p <= level;
        auto seg = juce::Rectangle<float> (in.getX(), in.getBottom() - (i + 1) * step + 1.0f, in.getWidth(), step - 2.0f);
        if (cl)
        {
            if (! lit)
                continue;
            if (p > 0.92f)
            {
                g.setColour (c::black);
                for (float x = seg.getX(); x < seg.getRight(); x += 3.0f)
                    g.fillRect (x, seg.getY(), 1.5f, seg.getHeight());
            }
            else
            {
                g.setColour (c::black);
                g.fillRect (seg);
            }
        }
        else
        {
            const auto litC = p > 0.92f ? c::red : p > 0.75f ? c::yellow : c::lime;
            const auto offC = p > 0.92f ? juce::Colour (0xff400000) : p > 0.75f ? juce::Colour (0xff404000) : juce::Colour (0xff003000);
            g.setColour (lit ? litC : offC);
            g.fillRect (seg);
        }
    }
    if (hold > 0.0f)
    {
        g.setColour (cl ? c::black : c::white);
        g.fillRect (in.getX(), in.getBottom() - juce::jlimit (0.0f, 1.0f, hold) * in.getHeight() - 1.0f, in.getWidth(), 2.0f);
    }
}

void lamp (juce::Graphics& g, juce::Rectangle<float> r, bool on)
{
    g.setColour (classic() ? (on ? c::black : c::white) : (on ? c::lime : c::green));
    g.fillRect (r);
    outline (g, r);
}

void vline (juce::Graphics& g, float x, float y0, float y1)
{
    if (classic())
    {
        g.setColour (c::black);
        g.drawVerticalLine ((int) x, y0, y1);
        return;
    }
    g.setColour (c::gray);
    g.drawVerticalLine ((int) x, y0, y1);
    g.setColour (c::white);
    g.drawVerticalLine ((int) x + 1, y0, y1);
}

juce::Rectangle<float> plot (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (classic())
    {
        g.setColour (c::white);
        g.fillRect (r);
        outline (g, r);
    }
    else
    {
        g.setColour (c::black);
        g.fillRect (r);
        sunken (g, r);
    }
    return r.reduced (1.0f);
}

PlotInk ink()
{
    if (classic())
        return { c::white, c::black, c::black, c::black, c::black, c::black, c::black, c::black, true };
    return { c::black, juce::Colour (0xff006000), c::lime, c::lime, c::green, juce::Colour (0xff00c000), c::yellow, c::gray, false };
}

void plotFill (juce::Graphics& g, const juce::Path& p, bool second)
{
    if (classic())
    {
        g.saveState();
        if (second)
        {
            juce::Image im (juce::Image::ARGB, 4, 4, true);
            im.setPixelAt (0, 0, c::black);
            im.setPixelAt (2, 2, c::black);
            g.setFillType (juce::FillType (im, {}));
        }
        else
            g.setFillType (juce::FillType (dither (c::black), {}));
        g.fillPath (p);
        g.restoreState();
        return;
    }
    g.setColour (second ? juce::Colour (0xff00c000) : c::green);
    g.fillPath (p);
}

void gridLine (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b)
{
    const auto k = ink();
    g.setColour (k.grid);
    if (! k.dottedGrid)
    {
        g.drawLine ({ a, b }, 1.0f);
        return;
    }
    const float len = a.getDistanceFrom (b);
    const auto d = (b - a) / std::max (1.0f, len);
    for (float t = 0; t < len; t += 4.0f)
        g.fillRect (juce::Rectangle<float> (1.0f, 1.0f).withCentre (a + d * t));
}

// ---- LookAndFeel ----------------------------------------------------------------------

LookAndFeel::LookAndFeel() { refresh(); }

void LookAndFeel::refresh()
{
    const bool cl = classic();
    setColour (juce::PopupMenu::backgroundColourId, c::white);
    setColour (juce::PopupMenu::textColourId, c::black);
    setColour (juce::PopupMenu::headerTextColourId, c::black);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, cl ? c::black : c::navy);
    setColour (juce::PopupMenu::highlightedTextColourId, c::white);
    setColour (juce::AlertWindow::backgroundColourId, cl ? c::white : c::silver);
    setColour (juce::AlertWindow::textColourId, c::black);
    setColour (juce::AlertWindow::outlineColourId, c::black);
    setColour (juce::TextButton::buttonColourId, cl ? c::white : c::silver);
    setColour (juce::TextButton::textColourOffId, c::black);
    setColour (juce::TextEditor::backgroundColourId, c::white);
    setColour (juce::TextEditor::textColourId, c::black);
    setColour (juce::TextEditor::outlineColourId, c::black);
    setColour (juce::TextEditor::focusedOutlineColourId, c::black);
    setColour (juce::TextEditor::highlightColourId, cl ? c::black : c::navy);
    setColour (juce::TextEditor::highlightedTextColourId, c::white);
    setColour (juce::CaretComponent::caretColourId, c::black);
    setColour (juce::TooltipWindow::backgroundColourId, c::white);
    setColour (juce::TooltipWindow::textColourId, c::black);
    setColour (juce::TooltipWindow::outlineColourId, c::black);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (c::white);
    g.setColour (c::black);
    g.drawRect (0, 0, w, h, 1);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (4.0f);
    button (g, r, {}, down, false, false);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& s, int w, int h)
{
    g.fillAll (c::white);
    g.setColour (c::black);
    g.drawRect (0, 0, w, h, 1);
    g.setFont (ui (18.0f, false));
    g.drawFittedText (s, juce::Rectangle<int> (w, h).reduced (8, 4), juce::Justification::centredLeft, 6);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& s, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    juce::AttributedString a;
    a.append (s, ui (18.0f, false));
    juce::TextLayout tl;
    tl.createLayout (a, 420.0f);
    const int w = (int) tl.getWidth() + 18, h = (int) tl.getHeight() + 10;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h).constrainedWithin (parent);
}
} // namespace dsq::theme
