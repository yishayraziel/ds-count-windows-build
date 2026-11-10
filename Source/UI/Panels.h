#pragma once
#include "Graphs.h"
#include "../Plugin/PluginProcessor.h"

namespace dsq
{
/** The module's graph windows in the upper row. Paints its own window frames. */
struct LcdPage : public juce::Component
{
    std::function<void()> onResize;
    std::function<void (juce::Graphics&)> onPaint;
    void resized() override { if (onResize) onResize(); }
    void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
};

/** Lays out graph windows: returns each window's client rectangle (relative to the page) and remembers the frames. */
struct GraphWindows
{
    struct W { juce::Rectangle<float> frame; juce::String title; };
    std::vector<W> windows;
    /** Splits the page into windows of the given widths (0 = take the rest) and returns the plot areas. */
    std::vector<juce::Rectangle<int>> layout (juce::Rectangle<int> page, std::initializer_list<std::pair<juce::String, int>> spec);
    void paint (juce::Graphics&) const;
};

/** The dialog content inside the main window (1596 x 430 design units) plus its graph windows. */
class ModulePanel : public juce::Component
{
public:
    LcdPage lcd;
    ModulePanel (DSCountProcessor& p, juce::String title, juce::String subtitle);
    virtual void tick (const ChainParams& s) = 0;
    /** Deepest gain reduction, dB. */
    virtual float reduction() const = 0;
    /** Three short status texts for the window's information strip. */
    virtual juce::StringArray info() const = 0;
    const juce::String& getTitle() const { return title; }
    void paint (juce::Graphics&) override;
    void resized() override;

protected:
    juce::String latencyText() const;
    juce::RangedAudioParameter* param (const juce::String& id) const { return proc.apvts.getParameter (id); }

    DSCountProcessor& proc;
    juce::UndoManager* um;
    juce::String title, subtitle;
    GraphWindows windows;
};

class DynamicsPanel : public ModulePanel
{
public:
    explicit DynamicsPanel (DSCountProcessor&);
    void tick (const ChainParams& s) override;
    float reduction() const override { return std::min ({ gr[0], gr[1], limGr }); }
    juce::StringArray info() const override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void bindChannel (int c);
    void updateMakeupRule (int channel, float plain);
    void setupMakeup (Knob& k, int channel);

    const char* prefix;
    int bound = -1;
    EngineParams shown;

    TransferGraph transfer;
    HistoryGraph history { HistoryGraph::Mode::dynamics };
    BandGraph bandGraph;

    Segment channel, monitor, scSource, filterType, limType;
    Toggle ganged, link, midSide, scFilter, copy, autoMakeup, parallel, limiter;
    Knob threshold, ratio, knee, attack, lookahead, makeup;
    Knob relDelay, relFast, average, relSlow, rms;
    Knob frequency, bandwidth, preLimiter, ceiling;

    float gr[2] { 0, 0 }, limGr = 0, lampFast = 0, lampSlow = 0;
};

} // namespace dsq
