#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Widgets.h"
#include "../DSP/LinearPhaseCrossover.h"
#include "../DSP/Metering.h"
#include "../DSP/TransferCurve.h"

namespace dsq
{
/** Static input/output curve, computed by the same function as the audio path, with the live detector level. */
class TransferGraph : public juce::Component
{
public:
    void setData (const TransferParams& c, float envelopeDb, float grDb);
    void paint (juce::Graphics&) override;

private:
    TransferParams curve;
    float envDb = -120.0f, gr = 0.0f;
};

/** Scrolling level history with the gain reduction drawn from the top. */
class HistoryGraph : public juce::Component
{
public:
    enum class Mode { dynamics, maximizer };
    explicit HistoryGraph (Mode m);
    void push (const WaveColumn* columns, int n);
    /** Call once per display tick with the columns that arrived. When nothing arrives for a while
        (module off, transport stopped, plug-in suspended by the host during a preset change) the
        graph keeps moving with silence instead of freezing on the last picture. */
    void update (const WaveColumn* columns, int n);
    void clear();
    void setGrRange (float dB) { grRange = dB; }
    void paint (juce::Graphics&) override;

private:
    Mode mode;
    std::vector<WaveColumn> history;
    int pos = 0, idleTicks = 0;
    float grRange = 24.0f;
};

/** Magnitude of a detector band filter, from its real FIR kernel. */
class BandGraph : public juce::Component
{
public:
    void setSpec (const BandSpec& spec, double sampleRate, bool lowLatency);
    void paint (juce::Graphics&) override;

private:
    BandSpec shown;
    double sr = 0.0;
    bool lowLatencyShown = false, valid = false;
    std::vector<float> curve;
};

juce::String dbText (float dB, int decimals = 1);
} // namespace dsq
