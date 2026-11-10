#pragma once
#include "Theme.h"

namespace dsq
{
/** Draws the editor through an old-screen filter: colour fringe, phosphor glow, scan lines, aperture mask
    (3.11 only) and a soft vignette. No bezel and no curvature.

    It sits on top of the component it shows, is opaque and lets every mouse event through, so the real
    controls underneath keep working. Only the region JUCE asks to repaint is rendered again; the filter
    runs on that region plus the glow radius. */
class CrtScreen : public juce::Component
{
public:
    explicit CrtScreen (juce::Component& source);
    void paint (juce::Graphics&) override;
    void resized() override { base = {}; }

    /** Milliseconds spent in the last paint, for the validation report. */
    double lastPaintMs = 0.0, lastRenderMs = 0.0;

private:
    void render (juce::Rectangle<int> area, float scale);
    void filter (juce::Rectangle<int> area, float scale, bool strong, bool colour);

    juce::Component& source;
    juce::Image base, out;
    float baseScale = 0.0f;
    int baseGeneration = -1;
    std::vector<float> small[3], tmp;
};
} // namespace dsq
