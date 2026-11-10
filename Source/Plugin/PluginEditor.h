#pragma once
#include "PluginProcessor.h"
#include "../UI/Panels.h"
#include "../UI/Crt.h"

class DSCountEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kDesignW = 1620, kDesignH = 968;
    /** 60 % to 158 % of the design size; the default is 80 %. */
    static constexpr int kMinW = 972, kMaxW = 2560, kDefaultW = 1296;
    static int heightFor (int w) { return (int) std::lround (w * (double) kDesignH / kDesignW); }

    /** Sets the editor width (the height follows the fixed aspect ratio), within the limits. */
    void setWidth (int w);
    /** The largest width that fits the screen the editor is on. */
    int fitWidth() const;
    /** The maximise box: largest size that fits, and back. */
    void toggleZoom();

    explicit DSCountEditor (DSCountProcessor&);
    ~DSCountEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Runs one display update, for tests and snapshots. */
    void refreshForTest() { timerCallback(); }
    /** Re-reads the skin and the screen effect preference. */
    void applySkin();
    /** For tests: opens the Save As dialog and returns it. */
    juce::AlertWindow* openSaveDialogForTest() { save.onClick(); return alert.get(); }
    double lastCrtPaintMs() const { return crt.lastPaintMs; }
    double lastCrtRenderMs() const { return crt.lastRenderMs; }

private:
    /** Everything lives in a fixed 1620 x 968 design space scaled with a transform, so it stays vector-sharp at any size. */
    class Surface : public juce::Component
    {
    public:
        explicit Surface (DSCountEditor& e) : owner (e) {}
        void paint (juce::Graphics& g) override { owner.paintSurface (g); }
        void mouseUp (const juce::MouseEvent& e) override { owner.surfaceClick (e.position); }
    private:
        DSCountEditor& owner;
    };

    struct Bar { float peak = -120, rms = -120, held = -120, holdTime = 0; };
    struct MenuItem { juce::String name; juce::Rectangle<float> area; };

    void timerCallback() override;
    void paintSurface (juce::Graphics&);
    void paintChrome (juce::Graphics&);
    void paintLoudness (juce::Graphics&, juce::Rectangle<float> client);
    void paintLevels (juce::Graphics&, juce::Rectangle<float> client);
    void paintModuleWindow (juce::Graphics&);
    void surfaceClick (juce::Point<float>);
    void showMenu (int index);
    void showPresetMenu();
    void askName (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)>);
    void resetMeters();
    void layoutMenus();
    juce::Rectangle<float> frame (juce::Rectangle<float> r) const;

    DSCountProcessor& proc;
    dsq::theme::LookAndFeel look;
    juce::UndoManager* um;
    juce::Component holder;
    Surface surface { *this };
    dsq::CrtScreen crt { holder };

    dsq::DynamicsPanel dynamics;

    dsq::Toggle prev, presetName, next, save, slotA, slotB, copyAB, undoButton, redoButton, bypass, skin311, skinClassic;
    dsq::Toggle lowLatency, resetLoudness, resetPeaks;
    dsq::Knob input, output;
    dsq::Segment dither, ditherType;
    juce::TooltipWindow tooltips { this, 700 };
    std::unique_ptr<juce::AlertWindow> alert;

    std::vector<MenuItem> menus;
    juce::Rectangle<float> metersArea, peakModePane;
    int generation = -1, unzoomedWidth = 0;
    dsq::ChainParams shown;
    Bar in[2], out[2];
    float tpMax = -120.0f, momentary = -200, shortTerm = -200, integrated = -200;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DSCountEditor)
};
