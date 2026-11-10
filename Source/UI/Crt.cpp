#include "Crt.h"

namespace dsq
{
CrtScreen::CrtScreen (juce::Component& s) : source (s)
{
    setInterceptsMouseClicks (false, false);
    setOpaque (true);
}

void CrtScreen::render (juce::Rectangle<int> area, float scale)
{
    juce::Graphics ig (base);
    ig.reduceClipRegion (area);
    ig.addTransform (juce::AffineTransform::scale (scale));
    source.paintEntireComponent (ig, true);
}

void CrtScreen::paint (juce::Graphics& g)
{
    const double start = juce::Time::getMillisecondCounterHiRes();
    const auto mode = theme::crt();
    // Render at the display resolution, capped so a large window on a dense screen stays affordable.
    float s = juce::jlimit (0.5f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const double pixels = (double) getWidth() * getHeight() * s * s;
    if (pixels > 1.8e6)
        s *= (float) std::sqrt (1.8e6 / pixels);
    const int W = juce::roundToInt (getWidth() * s), H = juce::roundToInt (getHeight() * s);
    if (W <= 0 || H <= 0)
        return;

    bool full = false;
    if (! base.isValid() || base.getWidth() != W || base.getHeight() != H || baseScale != s || baseGeneration != theme::preferenceGeneration())
    {
        base = juce::Image (juce::Image::RGB, W, H, true, juce::SoftwareImageType());
        baseScale = s;
        baseGeneration = theme::preferenceGeneration();
        full = true;
    }
    const auto clip = full ? getLocalBounds() : g.getClipBounds().getIntersection (getLocalBounds());
    const auto bounds = juce::Rectangle<int> (W, H);
    const auto area = (clip.toFloat() * s).getSmallestIntegerContainer().expanded (1).getIntersection (bounds);
    if (area.isEmpty())
        return;
    render (area, s);
    const double rendered = juce::Time::getMillisecondCounterHiRes();

    // The glow reaches a little outside the repainted region.
    const int reach = juce::roundToInt (14.0f * s);
    const auto region = area.expanded (reach).getIntersection (bounds);
    filter (region, s, mode == theme::Crt::strong, ! theme::classic());

    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.drawImageTransformed (out, juce::AffineTransform::translation ((float) region.getX(), (float) region.getY()).scaled (1.0f / s));
    lastPaintMs = juce::Time::getMillisecondCounterHiRes() - start;
    lastRenderMs = rendered - start;
}

void CrtScreen::filter (juce::Rectangle<int> r, float s, bool strong, bool colour)
{
    const int w = r.getWidth(), h = r.getHeight();
    if (! out.isValid() || out.getWidth() != w || out.getHeight() != h)
        out = juce::Image (juce::Image::RGB, w, h, false, juce::SoftwareImageType());

    const int dx = colour ? juce::roundToInt ((strong ? 1.1f : 0.6f) * s) : 0;
    const float glowK = colour ? (strong ? 0.45f : 0.28f) : (strong ? 0.35f : 0.2f);
    const float scan = strong ? (colour ? 0.24f : 0.22f) : (colour ? 0.13f : 0.12f);
    const float mask = colour ? (strong ? 0.10f : 0.05f) : 0.0f;
    const float vig = strong ? 0.42f : 0.26f;

    const juce::Image::BitmapData src (base, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData dst (out, juce::Image::BitmapData::writeOnly);
    const int W = base.getWidth(), H = base.getHeight(), ps = src.pixelStride;
    constexpr int R = juce::PixelRGB::indexR, G = juce::PixelRGB::indexG, B = juce::PixelRGB::indexB;
    constexpr float inv = 1.0f / 255.0f;

    // Glow: a box-blurred quarter-resolution copy, screened on top.
    const int f = 4, sw = (w + f - 1) / f + 1, sh = (h + f - 1) / f + 1;
    for (auto& v : small)
        v.assign ((size_t) (sw * sh), 0.0f);
    for (int y = 0; y < sh; ++y)
        for (int j = 0; j < f; ++j)
        {
            const int yy = juce::jlimit (0, H - 1, r.getY() + y * f + j);
            const auto* line = src.getLinePointer (yy);
            for (int x = 0; x < sw; ++x)
            {
                float a0 = 0, a1 = 0, a2 = 0;
                const int xs = r.getX() + x * f;
                for (int i = 0; i < f; ++i)
                {
                    const auto* p = line + juce::jlimit (0, W - 1, xs + i) * ps;
                    a0 += p[R]; a1 += p[G]; a2 += p[B];
                }
                const size_t k = (size_t) (y * sw + x);
                small[0][k] += a0; small[1][k] += a1; small[2][k] += a2;
            }
        }
    const float norm = inv / (float) (f * f);
    tmp.resize ((size_t) (sw * sh));
    for (auto& v : small)
    {
        for (auto& e : v)
            e *= norm;
        // Two passes of a radius-2 box blur, horizontal then vertical.
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int y = 0; y < sh; ++y)
                for (int x = 0; x < sw; ++x)
                {
                    float a = 0; int n = 0;
                    for (int k = std::max (0, x - 2); k <= std::min (sw - 1, x + 2); ++k) { a += v[(size_t) (y * sw + k)]; ++n; }
                    tmp[(size_t) (y * sw + x)] = a / (float) n;
                }
            for (int y = 0; y < sh; ++y)
                for (int x = 0; x < sw; ++x)
                {
                    float a = 0; int n = 0;
                    for (int k = std::max (0, y - 2); k <= std::min (sh - 1, y + 2); ++k) { a += tmp[(size_t) (k * sw + x)]; ++n; }
                    v[(size_t) (y * sw + x)] = a / (float) n;
                }
        }
    }

    // Per-column tables.
    std::vector<int> ix0 ((size_t) w), xl ((size_t) w), xr ((size_t) w), phase ((size_t) w);
    std::vector<float> fx ((size_t) w), ex2 ((size_t) w);
    const float cx = W * 0.5f, cy = H * 0.5f, rx = W * 0.75f, ry = H * 0.70f;
    for (int x = 0; x < w; ++x)
    {
        const float gx = (float) x / f;
        ix0[(size_t) x] = juce::jlimit (0, sw - 2, (int) gx);
        fx[(size_t) x] = juce::jlimit (0.0f, 1.0f, gx - (float) ix0[(size_t) x]);
        const int ax = r.getX() + x;
        xl[(size_t) x] = juce::jlimit (0, W - 1, ax - dx) * ps;
        xr[(size_t) x] = juce::jlimit (0, W - 1, ax + dx) * ps;
        const float e = (ax - cx) / rx;
        ex2[(size_t) x] = e * e;
        phase[(size_t) x] = ((int) ((float) ax / s)) % 3;
    }
    std::vector<float> rowGlow[3];
    for (auto& v : rowGlow)
        v.resize ((size_t) sw);
    const float tint[3] = { colour ? 1.0f : 0.96f, colour ? 1.0f : 0.98f, 1.0f }, lift[3] = { colour ? 0.0f : 0.01f, colour ? 0.0f : 0.01f, colour ? 0.0f : 0.02f };

    for (int y = 0; y < h; ++y)
    {
        const int ay = r.getY() + y;
        const float rowK = std::fmod ((float) ay / s, 3.0f) >= 2.0f ? 1.0f - scan : 1.0f;
        const float ey = (ay - cy) / ry, ey2 = ey * ey;
        const float gy = (float) y / f;
        const int y0 = juce::jlimit (0, sh - 2, (int) gy);
        const float fy = juce::jlimit (0.0f, 1.0f, gy - (float) y0);
        for (int c = 0; c < 3; ++c)
        {
            const float* a = small[c].data() + y0 * sw;
            const float* b = a + sw;
            for (int x = 0; x < sw; ++x)
                rowGlow[c][(size_t) x] = (a[x] + (b[x] - a[x]) * fy) * glowK;
        }
        const auto* line = src.getLinePointer (ay);
        auto* o = dst.getLinePointer (y);
        for (int x = 0; x < w; ++x)
        {
            const size_t xi = (size_t) x;
            const int ax = (r.getX() + x) * ps;
            const float in[3] = { line[xl[xi] + R] * inv, line[ax + G] * inv, line[xr[xi] + B] * inv };
            const float v = rowK * (1.0f - vig * juce::jlimit (0.0f, 1.0f, (ex2[xi] + ey2 - 0.384f) * 1.6234f));
            const int g0 = ix0[xi];
            const float t = fx[xi];
            auto* q = o + x * dst.pixelStride;
            const int idx[3] = { R, G, B };
            for (int c = 0; c < 3; ++c)
            {
                const float gl = rowGlow[c][(size_t) g0] + (rowGlow[c][(size_t) g0 + 1] - rowGlow[c][(size_t) g0]) * t;
                float val = (in[c] + gl * (1.0f - in[c])) * v;
                if (mask > 0.0f && c != phase[xi])
                    val *= 1.0f - mask;
                val = val * tint[c] + lift[c];
                q[idx[c]] = (juce::uint8) (val >= 1.0f ? 255 : val <= 0.0f ? 0 : (int) (val * 255.0f + 0.5f));
            }
        }
    }
}
} // namespace dsq
