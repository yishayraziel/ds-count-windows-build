#include "PluginEditor.h"

using namespace dsq;
namespace c = dsq::theme::c;

// Layout of the 1620 x 968 design space, shared by both skins.
static const juce::Rectangle<float> kLoud { 8, 126, 420, 300 }, kLevels { 436, 126, 330, 300 };
static const juce::Rectangle<int> kGraphs { 774, 126, 838, 300 }, kPanel { 12, 494, 1596, 430 };
static const juce::Rectangle<float> kModule { 8, 434, 1604, 494 };

DSCountEditor::DSCountEditor (DSCountProcessor& p)
    : AudioProcessorEditor (p), proc (p), um (&p.undo),
      dynamics (p),
      prev ("Prev", um), presetName ("", um), next ("Next", um), save ("Save", um),
      slotA ("A", um), slotB ("B", um), copyAB ("Copy A>B", um), undoButton ("Undo", um), redoButton ("Redo", um),
      bypass ("Bypass", um), skin311 ("3.11", um), skinClassic ("Classic", um),
      lowLatency ("Low Latency", um), resetLoudness ("Reset", um), resetPeaks ("Reset", um),
      input ("Input", um), output ("Output", um),
      dither ({ "Off", "16", "20", "24" }, um, Segment::Style::popup), ditherType ({ "TPDF", "S1", "S2", "S3" }, um, Segment::Style::popup)
{
    setLookAndFeel (&look);
    tooltips.setLookAndFeel (&look);
    addAndMakeVisible (holder);
    holder.addAndMakeVisible (surface);
    addChildComponent (crt);
    surface.addAndMakeVisible (dynamics);
    surface.addAndMakeVisible (dynamics.lcd);

    lowLatency.setShowLamp (true);
    presetName.setPopupLook (true);
    for (auto* c : std::initializer_list<juce::Component*> { &prev, &presetName, &next, &save, &slotA, &slotB, &copyAB, &undoButton, &redoButton,
                                                            &bypass, &skin311, &skinClassic, &lowLatency, &resetLoudness, &resetPeaks,
                                                            &input, &output, &dither, &ditherType })
        surface.addAndMakeVisible (c);

    // 3.11 toolbar icons.
    auto arrow = [] (bool left) { return [left] (juce::Graphics& g, juce::Rectangle<float> r, bool)
    {
        juce::Path a;
        a.addArrow (left ? juce::Line<float> (r.getCentreX() + 9, r.getCentreY(), r.getCentreX() - 9, r.getCentreY())
                         : juce::Line<float> (r.getCentreX() - 9, r.getCentreY(), r.getCentreX() + 9, r.getCentreY()), 5.0f, 14.0f, 9.0f);
        g.setColour (c::navy);
        g.fillPath (a);
    }; };
    prev.icon = arrow (true);
    next.icon = arrow (false);
    save.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, bool)
    {
        auto d = r.withSizeKeepingCentre (18, 18);
        g.setColour (c::navy); g.fillRect (d);
        g.setColour (c::silver); g.fillRect (d.getX() + 3, d.getY(), 10.0f, 6.0f);
        g.setColour (c::white); g.fillRect (d.getX() + 3, d.getY() + 9, 12.0f, 9.0f);
        g.setColour (c::black); g.drawRect (d, 1.0f);
    };
    auto letter = [] (const char* s) { return [s] (juce::Graphics& g, juce::Rectangle<float> r, bool)
    { theme::text (g, s, r, juce::Justification::centred, true, false, c::black, 18.0f); }; };
    slotA.icon = letter ("A");
    slotB.icon = letter ("B");
    copyAB.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, bool)
    {
        auto a = juce::Rectangle<float> (r.getCentreX() - 13, r.getCentreY() - 6, 8, 12), b = a.translated (18, 0);
        g.setColour (c::white); g.fillRect (a); g.fillRect (b);
        g.setColour (c::black); g.drawRect (a, 1.0f); g.drawRect (b, 1.0f);
        g.drawArrow ({ a.getRight() + 1, a.getCentreY(), b.getX() - 1, b.getCentreY() }, 1.0f, 5.0f, 4.0f);
    };
    auto curve = [] (bool undoIcon) { return [undoIcon] (juce::Graphics& g, juce::Rectangle<float> r, bool disabled)
    {
        juce::Path p;
        const float s = undoIcon ? 1.0f : -1.0f, cx = r.getCentreX(), cy = r.getCentreY();
        p.startNewSubPath (cx + 6 * s, cy + 7);
        p.cubicTo (cx + 12 * s, cy + 7, cx + 12 * s, cy - 4, cx + 4 * s, cy - 4);
        p.lineTo (cx - 6 * s, cy - 4);
        g.setColour (disabled ? c::gray : c::black);
        g.strokePath (p, juce::PathStrokeType (2.5f));
        g.fillPath (juce::Path (), {});
        juce::Path h;
        h.addTriangle (cx - 10 * s, cy - 4, cx - 4 * s, cy - 9, cx - 4 * s, cy + 1);
        g.fillPath (h);
    }; };
    undoButton.icon = curve (true);
    redoButton.icon = curve (false);
    bypass.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, bool)
    {
        g.setColour (c::black);
        g.fillRect (r.getCentreX() - 11, r.getCentreY() - 1, 22.0f, 2.0f);
        auto box = r.withSizeKeepingCentre (9, 13);
        g.setColour (c::red); g.fillRect (box);
        g.setColour (c::black); g.drawRect (box, 1.0f);
    };
    auto screen = [] (bool classicIcon) { return [classicIcon] (juce::Graphics& g, juce::Rectangle<float> r, bool)
    {
        auto s = r.withSizeKeepingCentre (22, 16);
        g.setColour (c::white); g.fillRect (s);
        g.setColour (c::black); g.drawRect (s, 1.0f);
        if (classicIcon) { g.fillRect (s.getX() + 1, s.getY() + 3, 20.0f, 1.0f); g.fillRect (s.getX() + 1, s.getY() + 5, 20.0f, 1.0f); }
        else { g.setColour (c::navy); g.fillRect (s.getX() + 1, s.getY() + 1, 20.0f, 4.0f); }
    }; };
    skin311.icon = screen (false);
    skinClassic.icon = screen (true);

    bypass.bindTo (p.apvts.getParameter (ids::globalBypass));
    lowLatency.bindTo (p.apvts.getParameter (ids::lowLatency));
    input.bindTo (p.apvts.getParameter (ids::inGain));
    output.bindTo (p.apvts.getParameter (ids::outGain));
    dither.bindTo (p.apvts.getParameter (ids::dither));
    ditherType.bindTo (p.apvts.getParameter (ids::ditherType));
    for (auto* k : { &input, &output })
    {
        k->setLabelWidth (72.0f);
        k->setFieldWidth (80.0f);
    }

    prev.onClick = [this] { proc.presets.step (-1); };
    next.onClick = [this] { proc.presets.step (1); };
    presetName.onClick = [this] { showPresetMenu(); };
    save.onClick = [this]
    {
        if (proc.presets.currentIsUser())
            proc.presets.saveUser (proc.presets.currentName());
        else
            askName ("Save Preset", proc.presets.currentName(), [this] (const juce::String& n) { proc.presets.saveUser (n); });
    };
    slotA.onClick = [this] { proc.presets.selectSlot (0); };
    slotB.onClick = [this] { proc.presets.selectSlot (1); };
    copyAB.onClick = [this] { proc.presets.copyActiveToOther(); };
    undoButton.onClick = [this] { um->undo(); };
    redoButton.onClick = [this] { um->redo(); };
    resetLoudness.onClick = [this] { proc.chain.meters.loudness.requestIntegratedReset(); };
    resetPeaks.onClick = [this] { resetMeters(); };
    skin311.onClick = [this] { theme::setSkin (theme::Skin::win311); applySkin(); };
    skinClassic.onClick = [this] { theme::setSkin (theme::Skin::classic); applySkin(); };

    prev.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    presetName.setTooltip ("Presets: factory, user, save, rename, delete");
    slotA.setTooltip ("Compare: work on setting A");
    slotB.setTooltip ("Compare: work on setting B");
    copyAB.setTooltip ("Copies the current setting into the other slot");
    skin311.setTooltip ("16-colour skin");
    skinClassic.setTooltip ("Black and white skin");
    lowLatency.setTooltip ("Short FIR band filters and at most 1 ms lookahead. Changes the latency");
    bypass.setTooltip ("Crossfades to the input, keeping the latency. Automatable");
    dither.setTooltip ("Output word length for dither. Off for floating point delivery");
    ditherType.setTooltip ("Dither shape: TPDF or one of three noise-shaping curves");

    setResizable (true, true);
    setResizeLimits (1300, 777, 2560, 1530);
    if (auto* cst = getConstrainer())
        cst->setFixedAspectRatio ((double) kDesignW / kDesignH);
    const int w = juce::jlimit (1300, 2560, proc.uiWidth);
    setSize (w, juce::roundToInt (w * (double) kDesignH / kDesignW));
    applySkin();
}

DSCountEditor::~DSCountEditor()
{
    stopTimer();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void DSCountEditor::applySkin()
{
    generation = theme::preferenceGeneration();
    look.refresh();
    const bool cl = theme::classic();
    bypass.setShowLamp (cl);
    skin311.setLit (! cl);
    skinClassic.setLit (cl);
    slotA.setText (cl ? "A" : "Setting");
    slotB.setText (cl ? "B" : "Setting");
    stopTimer();
    startTimerHz (theme::crt() != theme::Crt::off ? 20 : 30);
    prev.setText (cl ? "<" : "Prev");
    next.setText (cl ? ">" : "Next");
    save.setText (cl ? "Save..." : "Save");
    crt.setVisible (theme::crt() != theme::Crt::off);
    resized();
    dynamics.resized();
    surface.repaint();
    crt.repaint();
}

void DSCountEditor::paint (juce::Graphics& g) { g.fillAll (c::white); }

juce::Rectangle<float> DSCountEditor::frame (juce::Rectangle<float> r) const
{
    return theme::classic() ? r.withTrimmedRight (2.0f).withTrimmedBottom (2.0f) : r;
}

void DSCountEditor::resized()
{
    const float scale = (float) getWidth() / (float) kDesignW;
    holder.setBounds (getLocalBounds());
    crt.setBounds (getLocalBounds());
    surface.setBounds (0, 0, kDesignW, kDesignH);
    surface.setTransform (juce::AffineTransform::scale (scale));
    proc.uiWidth = getWidth();

    dynamics.setBounds (kPanel);
    dynamics.lcd.setBounds (kGraphs);
    layoutMenus();

    const bool cl = theme::classic();
    auto put = [] (juce::Component& comp, float x, float y, float w, float h) { comp.setBounds (juce::Rectangle<float> (x, y, w, h).toNearestInt()); };
    if (cl)
    {
        const float y = 76.0f;
        put (prev, 28, y, 44, 32);
        put (presetName, 84, y + 3, 300, 26);
        put (next, 394, y, 44, 32);
        put (save, 452, y, 100, 32);
        put (slotA, 576, y, 48, 32);
        put (slotB, 632, y, 48, 32);
        put (copyAB, 690, y, 140, 32);
        put (undoButton, 852, y, 86, 32);
        put (redoButton, 946, y, 86, 32);
        put (bypass, 1056, y + 1, 150, 30);
        put (skin311, 1420, 4, 70, 24);
        put (skinClassic, 1494, 4, 104, 24);
        copyAB.setText ("Copy A > B");
    }
    else
    {
        const float y = 60.0f, h = 50.0f;
        put (prev, 8, y, 84, h);
        put (presetName, 100, y + 12, 280, 26);
        put (next, 388, y, 84, h);
        put (save, 478, y, 84, h);
        put (slotA, 578, y, 84, h);
        put (slotB, 668, y, 84, h);
        put (copyAB, 758, y, 100, h);
        put (undoButton, 874, y, 84, h);
        put (redoButton, 964, y, 84, h);
        put (bypass, 1064, y, 104, h);
        put (skin311, 1440, y, 74, h);
        put (skinClassic, 1520, y, 92, h);
        copyAB.setText ("Copy A>B");
    }

    // Loudness window client.
    auto lc = theme::classic() ? frame (kLoud).withTrimmedTop (27.0f) : kLoud.reduced (4.0f).withTrimmedTop (26.0f);
    const float lx = lc.getX() + 12.0f, ly = lc.getY() + 8.0f;
    put (resetPeaks, lc.getRight() - 92, ly - 1, 80, 30);
    put (resetLoudness, lc.getRight() - 92, ly + 33, 80, 30);
    put (dither, lx + 128, ly + 156, 72, 26);
    put (ditherType, lx + 208, ly + 156, 100, 26);
    put (lowLatency, lx, ly + 192, 180, 30);

    // Levels window client.
    auto vc = theme::classic() ? frame (kLevels).withTrimmedTop (27.0f) : kLevels.reduced (4.0f).withTrimmedTop (26.0f);
    put (input, vc.getX() + 10, vc.getBottom() - 70, vc.getWidth() - 20, 30);
    put (output, vc.getX() + 10, vc.getBottom() - 36, vc.getWidth() - 20, 30);
    metersArea = { vc.getX(), vc.getY(), vc.getWidth(), vc.getHeight() - 76 };
}

void DSCountEditor::layoutMenus()
{
    const bool cl = theme::classic();
    const juce::StringArray names = cl ? juce::StringArray { "File", "Edit", "Preset", "Compare", "Options" }
                                       : juce::StringArray { "File", "Edit", "Preset", "Compare", "Options", "Help" };
    menus.clear();
    float x = cl ? 66.0f : 10.0f;
    const float y = cl ? 0.0f : 31.0f, h = cl ? 32.0f : 25.0f;
    for (auto& n : names)
    {
        const float w = theme::textWidth (n) + 20.0f;
        menus.push_back ({ n, { x, y, w, h } });
        x += w + (cl ? 8.0f : 4.0f);
    }
}

void DSCountEditor::surfaceClick (juce::Point<float> pt)
{
    for (int i = 0; i < (int) menus.size(); ++i)
        if (menus[(size_t) i].area.contains (pt))
            return showMenu (i);
    if (theme::classic() && juce::Rectangle<float> (14, 0, 46, 32).contains (pt))
        return showMenu (5);   // the mark holds Help
    if (metersArea.contains (pt))
        return resetMeters();
    if (peakModePane.contains (pt))
        if (auto* pm = proc.apvts.getParameter (ids::peakMeter))
            pm->setValueNotifyingHost (pm->getValue() > 0.5f ? 0.0f : 1.0f);
}

void DSCountEditor::resetMeters()
{
    for (auto* b : { &in[0], &in[1], &out[0], &out[1] })
        *b = {};
    proc.chain.meters.truePeakMax.store (0.0f);
    tpMax = -120.0f;
}

void DSCountEditor::timerCallback()
{
    if (generation != theme::preferenceGeneration())
        applySkin();
    shown = proc.params.read();
    dynamics.tick (shown);

    auto& m = proc.chain.meters;
    const float dt = getTimerInterval() > 0 ? (float) getTimerInterval() / 1000.0f : 1.0f / 30.0f;
    auto update = [dt] (Bar& b, float peakLin, float rmsLin)
    {
        const float pk = (float) gainToDb (peakLin), rm = (float) gainToDb (rmsLin);
        b.peak = pk > b.peak ? pk : std::max (pk, b.peak - 30.0f * dt);
        b.rms = rm;
        if (pk >= b.held) { b.held = pk; b.holdTime = 0; }
        else if ((b.holdTime += dt) > 2.0f) b.held = std::max (pk, b.held - 20.0f * dt);
    };
    for (int ch = 0; ch < 2; ++ch)
    {
        update (in[ch], m.inPeak[ch].exchange (0.0f), m.inRms[ch].load());
        update (out[ch], m.outPeak[ch].exchange (0.0f), m.outRms[ch].load());
    }
    tpMax = (float) gainToDb (m.truePeakMax.load());
    momentary = m.loudness.momentary.load();
    shortTerm = m.loudness.shortTerm.load();
    integrated = (float) m.loudness.integrated();

    auto& pm = proc.presets;
    presetName.setText (pm.currentName());
    slotA.setLit (pm.activeSlot() == 0);
    slotB.setLit (pm.activeSlot() == 1);
    undoButton.setDimmed (! um->canUndo());
    redoButton.setDimmed (! um->canRedo());
    ditherType.setDimmed (shown.ditherBits == 0);
    surface.repaint (0, 0, kDesignW, (int) kModule.getY() + 60);
    surface.repaint (0, (int) kModule.getBottom(), kDesignW, kDesignH - (int) kModule.getBottom());
}

// ---- Painting -------------------------------------------------------------------------

void DSCountEditor::paintSurface (juce::Graphics& g)
{
    const auto all = juce::Rectangle<float> (0, 0, (float) kDesignW, (float) kDesignH);
    const bool cl = theme::classic();
    if (cl)
        theme::desktop (g, all);
    else
    {
        g.setColour (c::silver);
        g.fillRect (all);
    }
    paintChrome (g);
    if (! cl)
        theme::desktop (g, { 1, 115, (float) kDesignW - 2, 817 });

    paintLoudness (g, theme::window (g, frame (kLoud), cl ? "Loudness" : "Loudness and Output", false, true, false));
    paintLevels (g, theme::window (g, frame (kLevels), cl ? "Levels" : "Level Meters", false, true, false));
    paintModuleWindow (g);
}

void DSCountEditor::paintChrome (juce::Graphics& g)
{
    const bool cl = theme::classic();
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const auto srText = juce::String (sr / 1000.0, 1) + " kHz";
    const auto latText = juce::String (proc.chain.latencySamples() * 1000.0 / sr, 1) + " ms";
    if (cl)
    {
        // Menu bar.
        g.setColour (c::white);
        g.fillRect (0.0f, 0.0f, (float) kDesignW, 32.0f);
        g.setColour (c::black);
        g.drawHorizontalLine (32, 0.0f, (float) kDesignW);
        theme::mark (g, { 20, 5, 26, 22 }, c::black);
        for (auto& m : menus)
            theme::text (g, m.name, m.area, juce::Justification::centred);
        theme::text (g, "Skin:", { 1340, 0, 76, 32 }, juce::Justification::centredRight, false);
        // Presets window.
        auto pw = juce::Rectangle<float> (14, 42, 1590, 74);
        theme::window (g, pw, "DS-COUNT " + juce::String (JucePlugin_VersionString) + " by Yishay Raziel", false, false, false);
        theme::text (g, srText + "  ·  latency " + latText, { 1230, 74, 360, 34 }, juce::Justification::centredRight, false);
        return;
    }
    // Title bar.
    auto title = juce::Rectangle<float> (0, 0, (float) kDesignW, 30);
    g.setColour (c::navy);
    g.fillRect (title);
    auto cm = title.withWidth (30.0f);
    g.setColour (c::silver);
    g.fillRect (cm);
    auto bar = cm.withSizeKeepingCentre (16, 4);
    g.setColour (c::gray); g.fillRect (bar.translated (1, 1));
    g.setColour (c::white); g.fillRect (bar);
    g.setColour (c::black); g.drawRect (bar, 1.0f);
    g.drawVerticalLine (30, 0.0f, 30.0f);
    theme::text (g, "DS-COUNT " + juce::String (JucePlugin_VersionString) + " - " + proc.presets.currentName(), title, juce::Justification::centred, true, false, c::white);
    g.setColour (c::black);
    g.drawHorizontalLine (30, 0.0f, (float) kDesignW);
    // Menu bar.
    g.setColour (c::white);
    g.fillRect (0.0f, 31.0f, (float) kDesignW, 25.0f);
    g.setColour (c::black);
    g.drawHorizontalLine (56, 0.0f, (float) kDesignW);
    for (auto& m : menus)
    {
        theme::text (g, m.name, m.area, juce::Justification::centred);
        const float w = theme::textWidth (m.name.substring (0, 1));
        const float x0 = m.area.getCentreX() - theme::textWidth (m.name) * 0.5f;
        g.fillRect (x0, m.area.getBottom() - 5.0f, w, 1.5f);
    }
    // Toolbar.
    g.setColour (c::black);
    g.drawHorizontalLine (114, 0.0f, (float) kDesignW);
    for (float x : { 570.0f, 866.0f, 1056.0f })
        theme::vline (g, x, 64.0f, 106.0f);
    theme::text (g, "Skin:", { 1370, 60, 64, 50 }, juce::Justification::centredRight, false);
    // Status bar.
    auto status = juce::Rectangle<float> (0, 932, (float) kDesignW, 36);
    g.setColour (c::silver);
    g.fillRect (status);
    g.setColour (c::black);
    g.drawHorizontalLine (932, 0.0f, (float) kDesignW);
    auto pane = [&g] (juce::Rectangle<float> r, const juce::String& s)
    {
        g.setColour (c::gray);
        g.drawHorizontalLine ((int) r.getY(), r.getX(), r.getRight());
        g.drawVerticalLine ((int) r.getX(), r.getY(), r.getBottom());
        g.setColour (c::white);
        g.drawHorizontalLine ((int) r.getBottom(), r.getX(), r.getRight());
        g.drawVerticalLine ((int) r.getRight(), r.getY(), r.getBottom());
        theme::text (g, s, r.reduced (8, 0), juce::Justification::centredLeft, false);
    };
    const auto info = dynamics.info();
    pane ({ 6, 938, 990, 24 }, info[0] + (shown.bypass ? "  (bypassed)" : ""));
    pane ({ 1004, 938, 130, 24 }, srText);
    pane ({ 1142, 938, 250, 24 }, "Latency " + latText);
    peakModePane = { 1400, 938, 212, 24 };
    pane (peakModePane, juce::String ("Meters: ") + (shown.truePeakMeters ? "True Peak" : "Sample Peak"));
}

void DSCountEditor::paintLoudness (juce::Graphics& g, juce::Rectangle<float> lc)
{
    const float lx = lc.getX() + 12.0f, ly = lc.getY() + 8.0f;
    auto num = [] (float v, float floor) { return v <= floor ? juce::String ("--") : juce::String (v, 1); };
    theme::text (g, "True Peak", { lx, ly, 124, 28 }, juce::Justification::centredLeft);
    theme::readout (g, { lx + 128, ly + 1, 86, 26 }, tpMax <= -119.0f ? juce::String ("--") : dbText (tpMax, 1));
    theme::text (g, "dBTP", { lx + 222, ly, 60, 28 }, juce::Justification::centredLeft, false);
    theme::text (g, "Integrated", { lx, ly + 34, 124, 28 }, juce::Justification::centredLeft);
    theme::readout (g, { lx + 128, ly + 35, 86, 26 }, num (integrated, -70.0f));
    theme::text (g, "LUFS", { lx + 222, ly + 34, 60, 28 }, juce::Justification::centredLeft, false);
    theme::text (g, "Short-term", { lx, ly + 68, 124, 28 }, juce::Justification::centredLeft);
    theme::readout (g, { lx + 128, ly + 69, 86, 26 }, num (shortTerm, -70.0f));
    theme::text (g, "LUFS", { lx + 222, ly + 68, 60, 28 }, juce::Justification::centredLeft, false);
    theme::text (g, "Momentary", { lx, ly + 102, 124, 28 }, juce::Justification::centredLeft);
    theme::readout (g, { lx + 128, ly + 103, 86, 26 }, num (momentary, -70.0f));
    theme::text (g, "LUFS", { lx + 222, ly + 102, 60, 28 }, juce::Justification::centredLeft, false);
    // Separator: etched in 3.11, a plain rule in Classic.
    g.setColour (theme::classic() ? c::black : c::gray);
    g.fillRect (lc.getX() + 8, ly + 142, lc.getWidth() - 16, 1.0f);
    if (! theme::classic())
    {
        g.setColour (c::white);
        g.fillRect (lc.getX() + 8, ly + 143, lc.getWidth() - 16, 1.0f);
    }
    theme::text (g, "Dither", { lx, ly + 154, 120, 30 }, juce::Justification::centredLeft);
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    theme::text (g, "Latency " + juce::String (proc.chain.latencySamples() * 1000.0 / sr, 1) + " ms (" + juce::String (proc.chain.latencySamples()) + " samples)",
                 { lx, ly + 230, lc.getWidth() - 24, 28 }, juce::Justification::centredLeft, false);
}

void DSCountEditor::paintLevels (juce::Graphics& g, juce::Rectangle<float> vc)
{
    auto pos = [] (float dB) { return juce::jlimit (0.0f, 1.0f, (dB + 60.0f) / 60.0f); };
    const float top = vc.getY() + 30.0f, h = vc.getHeight() - 76.0f - 58.0f;
    const float cx = vc.getCentreX();
    const float xs[4] = { cx - 136, cx - 82, cx + 54, cx + 108 };
    const Bar* bars[4] = { &in[0], &in[1], &out[0], &out[1] };
    const char* names[4] = { "In L", "In R", "Out L", "Out R" };
    for (int i = 0; i < 4; ++i)
    {
        auto r = juce::Rectangle<float> (xs[i], top, 28, h);
        theme::vmeter (g, r, pos (bars[i]->peak), pos (bars[i]->held));
        // RMS: a thin line across the bar.
        g.setColour (theme::classic() ? c::white : c::white.withAlpha (0.85f));
        g.fillRect (r.getX() + 4, r.getBottom() - 3 - pos (bars[i]->rms) * (r.getHeight() - 6) - 1, r.getWidth() - 8, 2.0f);
        theme::text (g, names[i], { r.getCentreX() - 30, vc.getY() + 4, 60, 24 }, juce::Justification::centred, false);
        theme::text (g, dbText (bars[i]->held), { r.getCentreX() - 30, r.getBottom() + 2, 60, 22 }, juce::Justification::centred, false);
    }
    for (int d = 0; d >= -60; d -= 12)
        theme::text (g, juce::String (d), { cx - 30, top + 3 + (float) -d / 60.0f * (h - 6) - 7, 60, 14 }, juce::Justification::centred,
                     false, false, c::black, 12.0f);
}

void DSCountEditor::paintModuleWindow (juce::Graphics& g)
{
    auto* p = &dynamics;
    auto client = theme::window (g, frame (kModule), "Precision Dynamics", true, true, ! theme::classic());
    auto strip = client.withHeight (29.0f);
    const auto info = p->info();
    if (theme::classic())
    {
        g.setColour (c::white);
        g.fillRect (strip);
        g.setColour (c::black);
        g.drawHorizontalLine ((int) strip.getBottom() - 3, strip.getX(), strip.getRight());
        g.drawHorizontalLine ((int) strip.getBottom() - 1, strip.getX(), strip.getRight());
    }
    else
    {
        g.setColour (c::gray);
        g.drawHorizontalLine ((int) strip.getBottom() - 2, strip.getX() + 8, strip.getRight() - 8);
        g.setColour (c::white);
        g.drawHorizontalLine ((int) strip.getBottom() - 1, strip.getX() + 8, strip.getRight() - 8);
    }
    auto t = strip.reduced (14, 0).withTrimmedBottom (3);
    theme::text (g, info[0], t, juce::Justification::centredLeft, false);
    theme::text (g, info[1], t, juce::Justification::centred, false);
    theme::text (g, info[2], t, juce::Justification::centredRight, false);
}

// ---- Menus ----------------------------------------------------------------------------

void DSCountEditor::showMenu (int index)
{
    auto& pm = proc.presets;
    juce::PopupMenu m;
    juce::Component::SafePointer<DSCountEditor> safe (this);
    auto setParam = [this] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) { um->beginNewTransaction(); p->setValueNotifyingHost (v); } };
    switch (index)
    {
        case 0:
            m.addItem ("Save", pm.currentIsUser(), false, [safe] { if (safe) safe->proc.presets.saveUser (safe->proc.presets.currentName()); });
            m.addItem ("Save As...", [safe] { if (safe) safe->askName ("Save Preset", safe->proc.presets.currentName(), [safe] (const juce::String& n) { if (safe) safe->proc.presets.saveUser (n); }); });
            m.addItem ("Rename...", pm.currentIsUser(), false, [safe]
            {
                if (! safe) return;
                const auto old = safe->proc.presets.currentName();
                safe->askName ("Rename Preset", old, [safe, old] (const juce::String& n) { if (safe) safe->proc.presets.renameUser (old, n); });
            });
            m.addItem ("Delete", pm.currentIsUser(), false, [safe] { if (safe) safe->proc.presets.deleteUser (safe->proc.presets.currentName()); });
            m.addSeparator();
            m.addItem ("Show Preset Folder", [safe] { if (safe) { safe->proc.presets.userFolder().createDirectory(); safe->proc.presets.userFolder().revealToUser(); } });
            break;
        case 1:
            m.addItem ("Undo", um->canUndo(), false, [this] { um->undo(); });
            m.addItem ("Redo", um->canRedo(), false, [this] { um->redo(); });
            m.addSeparator();
            m.addItem (pm.activeSlot() == 0 ? "Copy A > B" : "Copy B > A", [safe] { if (safe) safe->proc.presets.copyActiveToOther(); });
            break;
        case 2:
            return showPresetMenu();
        case 3:
            m.addItem ("Setting A", true, pm.activeSlot() == 0, [safe] { if (safe) safe->proc.presets.selectSlot (0); });
            m.addItem ("Setting B", true, pm.activeSlot() == 1, [safe] { if (safe) safe->proc.presets.selectSlot (1); });
            m.addSeparator();
            m.addItem (pm.activeSlot() == 0 ? "Copy A > B" : "Copy B > A", [safe] { if (safe) safe->proc.presets.copyActiveToOther(); });
            break;
        case 4:
        {
            juce::PopupMenu skinMenu, crtMenu, meterMenu;
            skinMenu.addItem ("3.11", true, ! theme::classic(), [safe] { theme::setSkin (theme::Skin::win311); if (safe) safe->applySkin(); });
            skinMenu.addItem ("Classic", true, theme::classic(), [safe] { theme::setSkin (theme::Skin::classic); if (safe) safe->applySkin(); });
            const char* crtNames[] = { "Off", "Soft", "Strong" };
            for (int i = 0; i < 3; ++i)
                crtMenu.addItem (crtNames[i], true, (int) theme::crt() == i, [safe, i] { theme::setCrt ((theme::Crt) i); if (safe) safe->applySkin(); });
            meterMenu.addItem ("Sample Peak", true, ! shown.truePeakMeters, [setParam] { setParam (ids::peakMeter, 0.0f); });
            meterMenu.addItem ("True Peak", true, shown.truePeakMeters, [setParam] { setParam (ids::peakMeter, 1.0f); });
            m.addSubMenu ("Skin", skinMenu);
            m.addSubMenu ("Screen Effect", crtMenu);
            m.addSubMenu ("Peak Meters", meterMenu);
            m.addSeparator();
            const bool ll = proc.chain.isLowLatency();
            m.addItem ("Low Latency", true, ll, [setParam, ll] { setParam (ids::lowLatency, ll ? 0.0f : 1.0f); });
            m.addItem ("Bypass", true, bypass.isLit(), [this, setParam] { setParam (ids::globalBypass, bypass.isLit() ? 0.0f : 1.0f); });
            break;
        }
        default:
            m.addItem ("DS-COUNT " + juce::String (JucePlugin_VersionString) + " by Yishay Raziel", false, false, nullptr);
            m.addSeparator();
            m.addItem ("Double-click a value to type it", false, false, nullptr);
            m.addItem ("Alt-click a control for its default", false, false, nullptr);
            m.addItem ("Shift-drag for fine steps", false, false, nullptr);
            break;
    }
    juce::Rectangle<float> at = index < (int) menus.size() ? menus[(size_t) index].area : juce::Rectangle<float> (14, 0, 46, 32);
    const auto screen = surface.localAreaToGlobal (at.toNearestInt());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screen).withMinimumWidth (180));
}

void DSCountEditor::showPresetMenu()
{
    auto& pm = proc.presets;
    juce::PopupMenu m, user;
    const auto fNames = pm.factoryNames();
    const auto uNames = pm.userNames();
    juce::Component::SafePointer<DSCountEditor> safe (this);
    m.addSectionHeader ("Factory");
    for (int i = 0; i < fNames.size(); ++i)
        m.addItem (fNames[i], true, ! pm.currentIsUser() && fNames[i] == pm.currentName(), [safe, i] { if (safe) safe->proc.presets.loadFactory (i); });
    for (int i = 0; i < uNames.size(); ++i)
        user.addItem (uNames[i], true, pm.currentIsUser() && uNames[i] == pm.currentName(), [safe, n = uNames[i]] { if (safe) safe->proc.presets.loadUser (n); });
    m.addSeparator();
    m.addSubMenu ("User Presets", user, uNames.size() > 0);
    m.addItem ("Init", [safe] { if (safe) safe->proc.presets.loadInit(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetName));
}

void DSCountEditor::askName (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> done)
{
    alert = std::make_unique<juce::AlertWindow> (title, "Preset name", juce::MessageBoxIconType::NoIcon, this);
    alert->setLookAndFeel (&look);
    alert->addTextEditor ("name", initial);
    alert->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<DSCountEditor> safe (this);
    alert->enterModalState (true, juce::ModalCallbackFunction::create ([safe, done] (int r)
    {
        if (safe == nullptr || safe->alert == nullptr)
            return;
        // Leaving the modal state does not hide the window: close it here, then delete it after this callback.
        const auto name = safe->alert->getTextEditorContents ("name").trim();
        safe->alert->setVisible (false);
        if (r == 1 && name.isNotEmpty())
            done (name);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->alert.reset(); });
    }));
}
