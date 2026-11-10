// Plugin-level checks through the same interface a host uses, plus the UI snapshot tool.
#include "../../Source/Plugin/PluginProcessor.h"
#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
#endif
#include "../../Source/Plugin/PluginEditor.h"
#include "../../Source/Presets/FactoryPresets.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>

using namespace dsq;

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("[%s] %s\n", ok ? " ok " : "FAIL", what);
    if (! ok) ++failures;
}

static void setPlain (DSCountProcessor& p, const juce::String& id, float v)
{
    auto* q = p.apvts.getParameter (id);
    q->setValueNotifyingHost (q->convertTo0to1 (v));
}

static float getPlain (DSCountProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }

#if JUCE_LINUX || JUCE_WINDOWS
namespace juce::detail { bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages); }
#endif
/** Delivers pending messages (modal callbacks, async calls). */
static void runMessages()
{
#if JUCE_LINUX || JUCE_WINDOWS
    for (int i = 0; i < 50; ++i)
        juce::detail::dispatchNextMessageOnSystemQueue (true);
#elif JUCE_MAC
    // JUCE posts its messages to a source on the main run loop; run it for a moment.
    for (int i = 0; i < 20; ++i)
        CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false);
#endif
}

static void pump (DSCountProcessor& p, int blocks, int size = 512, double sr = 48000.0)
{
    juce::AudioBuffer<float> b (2, size);
    juce::MidiBuffer m;
    double phase = 0;
    juce::Random r (1);
    for (int i = 0; i < blocks; ++i)
    {
        for (int n = 0; n < size; ++n)
        {
            const float env = 0.2f + 0.5f * (float) (std::fmod (i * size + n, sr * 0.4) < sr * 0.05);
            const float s = env * (0.7f * (float) std::sin (phase) + 0.3f * (r.nextFloat() * 2 - 1));
            phase += 2 * juce::MathConstants<double>::pi * 180.0 / sr;
            b.setSample (0, n, s);
            b.setSample (1, n, s * 0.8f);
        }
        p.processBlock (b, m);
    }
}

static juce::String dy (int c, const char* s) { return cid (ids::dynamics, c, s); }

static void testStatePersistence()
{
    DSCountProcessor a;
    a.prepareToPlay (48000, 512);
    setPlain (a, dy (0, ids::threshold), -33.0f);
    setPlain (a, dy (1, ids::ratio), 6.66f);
    setPlain (a, dy (1, ids::filterType), 2.0f);
    setPlain (a, gid (ids::dynamics, ids::midSide), 1.0f);
    setPlain (a, dy (0, ids::frequency), 8123.0f);
    setPlain (a, gid (ids::dynamics, ids::limType), 2.0f);
    setPlain (a, ids::dither, 2.0f);
    a.uiWidth = 1700;
    a.presets.copyActiveToOther();
    setPlain (a, dy (0, ids::threshold), -44.0f);

    juce::MemoryBlock mb;
    a.getStateInformation (mb);

    DSCountProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    check (std::abs (getPlain (b, dy (0, ids::threshold)) + 44.0f) < 0.02f, "state: active slot threshold restored");
    check (std::abs (getPlain (b, dy (1, ids::ratio)) - 6.66f) < 0.02f, "state: ch2 ratio restored");
    check ((int) getPlain (b, dy (1, ids::filterType)) == 2, "state: ch2 band mode restored");
    check (getPlain (b, gid (ids::dynamics, ids::midSide)) > 0.5f, "state: mid/side restored");
    check ((int) getPlain (b, gid (ids::dynamics, ids::limType)) == 2, "state: limiter type restored");
    check (std::abs (getPlain (b, dy (0, ids::frequency)) - 8123.0f) < 1.0f, "state: band frequency restored");
    check ((int) getPlain (b, ids::dither) == 2, "state: dither restored");
    check (b.uiWidth == 1700, "state: interface size restored");
    check (b.presets.currentName() == a.presets.currentName(), "state: preset name restored");
    b.presets.selectSlot (1);
    check (std::abs (getPlain (b, dy (0, ids::threshold)) + 33.0f) < 0.02f, "state: other A/B slot restored");
}

static void testV1Migration()
{
    // A DS-COUNT 1.2 session, written in the 1.x format.
    juce::ValueTree root ("DSCOUNT"), params ("PARAMS");
    root.setProperty ("version", "1.2.0", nullptr);
    auto add = [&] (const char* id, double v) { juce::ValueTree t ("PARAM"); t.setProperty ("id", id, nullptr); t.setProperty ("value", v, nullptr); params.appendChild (t, nullptr); };
    add ("ch1_threshold", -27.5);
    add ("ch2_ratio", 3.0);
    add ("midside", 1.0);
    add ("input", 4.0);
    add ("limiter", 1.0);
    add ("meterrange", 1.0);
    root.appendChild (params, nullptr);
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary (*root.createXml(), mb);

    DSCountProcessor p;
    p.setStateInformation (mb.getData(), (int) mb.getSize());
    check (std::abs (getPlain (p, dy (0, ids::threshold)) + 27.5f) < 0.02f, "1.x session: threshold mapped to Dynamics");
    check (std::abs (getPlain (p, dy (1, ids::ratio)) - 3.0f) < 0.02f, "1.x session: ch2 ratio mapped");
    check (getPlain (p, gid (ids::dynamics, ids::midSide)) > 0.5f && getPlain (p, gid (ids::dynamics, ids::limiter)) > 0.5f, "1.x session: module switches mapped");
    check (std::abs (getPlain (p, ids::inGain) - 4.0f) < 0.02f, "1.x session: input gain mapped");
    check (std::abs (getPlain (p, ids::outGain)) < 0.01f && (int) getPlain (p, dy (0, ids::filterType)) == 0, "1.x session: unlisted values at defaults");

    // A 1.x user preset file.
    auto folder = p.presets.userFolder();
    folder.createDirectory();
    auto file = folder.getChildFile ("IntegrationV1.dscountpreset");
    juce::XmlElement x ("DSCOUNTPRESET");
    x.setAttribute ("version", 1);
    auto* e = x.createNewChildElement ("P");
    e->setAttribute ("id", "ch1_attack");
    e->setAttribute ("value", 7.5);
    x.writeTo (file);
    check (p.presets.loadUser ("IntegrationV1") && std::abs (getPlain (p, dy (0, ids::attack)) - 7.5f) < 0.05f, "1.x preset file loads into Dynamics");
    file.deleteFile();
}

static void testLatencyAndRates()
{
    DSCountProcessor p;
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        p.prepareToPlay (sr, 256);
        check (p.getLatencySamples() == p.chain.latencySamples() && p.getLatencySamples() > 0,
               ("latency reported at " + juce::String (sr)).toRawUTF8());
    }
    p.prepareToPlay (48000, 256);
    const int all = p.getLatencySamples();
    check (all == p.chain.dynamics.latencySamples(), "reported latency is the dynamics latency");

    setPlain (p, ids::lowLatency, 1.0f);
    pump (p, 2, 256);
    p.applyStructureNow();
    check (p.getLatencySamples() < all / 4, "low latency mode reports a lower latency");

    setPlain (p, ids::globalBypass, 1.0f);
    pump (p, 2, 256);
    check (p.getLatencySamples() == p.chain.latencySamples(), "bypass keeps the reported latency");
}

static void testDoubleAndFloatMatch()
{
    DSCountProcessor f, d;
    for (auto* p : { &f, &d })
    {
        setPlain (*p, dy (0, ids::threshold), -30.0f);
        setPlain (*p, dy (0, ids::ratio), 4.0f);
    }
    d.setProcessingPrecision (juce::AudioProcessor::doublePrecision);
    f.prepareToPlay (48000, 512);
    d.prepareToPlay (48000, 512);
    juce::AudioBuffer<float> bf (2, 512);
    juce::AudioBuffer<double> bd (2, 512);
    juce::MidiBuffer m;
    double err = 0;
    for (int k = 0; k < 400; ++k)
    {
        for (int n = 0; n < 512; ++n)
        {
            const double s = 0.5 * std::sin (0.03 * (k * 512 + n));
            for (int c = 0; c < 2; ++c) { bf.setSample (c, n, (float) s); bd.setSample (c, n, s); }
        }
        f.processBlock (bf, m);
        d.processBlock (bd, m);
        if (k > 200)
            for (int n = 0; n < 512; ++n)
                err = std::max (err, std::abs ((double) bf.getSample (0, n) - bd.getSample (0, n)));
    }
    check (err < 1.0e-4, "32-bit and 64-bit processing agree");
}

static void testPresets()
{
    DSCountProcessor p;
    p.prepareToPlay (48000, 512);
    const auto names = p.presets.factoryNames();
    check (names.size() == (int) factoryPresets().size() && names.size() >= 12, "factory presets listed");
    bool allStable = true, idsValid = true;
    for (auto& fp : factoryPresets())
        for (auto& v : fp.values)
            idsValid = idsValid && p.apvts.getParameter (v.id) != nullptr;
    check (idsValid, "every factory preset value names a real parameter");
    for (int i = 0; i < names.size(); ++i)
    {
        p.presets.loadFactory (i);
        pump (p, 20);
        p.applyStructureNow();
        pump (p, 100);
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer m;
        b.clear();
        p.processBlock (b, m);
        allStable = allStable && std::isfinite (b.getSample (0, 100)) && p.getLatencySamples() == p.chain.latencySamples();
    }
    check (allStable, "every factory preset processes audio stably and reports its latency");

    auto name = juce::String ("IntegrationTestPreset");
    p.presets.loadFactory (5);   // Bass Control
    check (p.presets.saveUser (name), "user preset: save");
    p.presets.loadFactory (0);
    check (p.presets.loadUser (name), "user preset: load");
    check ((int) getPlain (p, dy (0, ids::filterType)) == 1, "user preset: values come back");
    check (p.presets.renameUser (name, name + "2"), "user preset: rename");
    check (p.presets.userNames().contains (name + "2"), "user preset: listed");
    check (p.presets.deleteUser (name + "2"), "user preset: delete");

    p.presets.loadFactory (4);   // Mid/Side Mastering
    check (getPlain (p, gid (ids::dynamics, ids::midSide)) > 0.5f && getPlain (p, gid (ids::dynamics, ids::ganged)) < 0.5f, "M/S preset sets channel mode");
    int deess = 0;
    for (int i = 0; i < names.size(); ++i)
        if (names[i].startsWith ("De-Ess"))
        {
            p.presets.loadFactory (i);
            const int band = (int) getPlain (p, dy (0, ids::filterType));
            deess += (band == 2 || band == 3) && getPlain (p, dy (0, ids::frequency)) >= 4000.0f && getPlain (p, dy (0, ids::attack)) <= 5.0f;
        }
    check (deess >= 10, "de-ess presets use a band pass or high pass band above 4 kHz with a fast attack");
    p.presets.copyChannel (ids::dynamics, 0, 1);
    check (getPlain (p, dy (1, ids::threshold)) == getPlain (p, dy (0, ids::threshold)), "channel copy");

    setPlain (p, ids::lowLatency, 1.0f);
    p.presets.loadFactory (0);
    check (getPlain (p, ids::lowLatency) > 0.5f, "presets leave session settings alone");
    setPlain (p, ids::lowLatency, 0.0f);

    p.undo.clearUndoHistory();

}

static void testAutomation()
{
    // Every automatable parameter is exposed to the host; structural ones are not.
    DSCountProcessor p;
    int automatable = 0;
    bool structuralHidden = true;
    for (auto* q : p.getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (q);
        const auto id = r->getParameterID();
        if (q->isAutomatable())
            ++automatable;
        if (id == ids::lowLatency || id == ids::peakMeter)
            structuralHidden = structuralHidden && ! q->isAutomatable();
    }
    check (automatable > 40, "parameters are automatable");
    check (structuralHidden, "low latency and meter mode are not automatable");
    bool noModules = true;
    for (auto* q : p.getParameters())
    {
        const auto id = dynamic_cast<juce::RangedAudioParameter*> (q)->getParameterID();
        noModules = noModules && ! id.startsWith ("de_") && ! id.startsWith ("cl_") && ! id.startsWith ("mx_") && ! id.endsWith ("_power");
    }
    check (noModules, "only the dynamics module exists (no de-esser, comp or maximizer parameters)");
}

static void testEditor()
{
    DSCountProcessor p;
    p.prepareToPlay (48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    check (e != nullptr, "editor opens");
    if (auto* ed = dynamic_cast<DSCountEditor*> (e.get()))
    {
        for (auto skin : { dsq::theme::Skin::win311, dsq::theme::Skin::classic })
            for (auto crt : { dsq::theme::Crt::off, dsq::theme::Crt::strong })
            {
                dsq::theme::setSkinForTest (skin);
                dsq::theme::setCrtForTest (crt);
                ed->applySkin();
                for (int preset : { 0, 8 })
                {
                    p.presets.loadFactory (preset);
                    pump (p, 50);
                    ed->refreshForTest();
                    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                    check (img.isValid(), "editor paints in every skin");
                }
            }
        // The Save dialog closes on OK and on Cancel, and OK saves. Needs a display.
        const bool haveDisplay = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr;
        if (haveDisplay)
            ed->addToDesktop (0);
        for (bool ok : { false, true })
        {
            if (! haveDisplay)
                break;
            auto* dlg = ed->openSaveDialogForTest();
            check (dlg != nullptr && dlg->isVisible(), "save dialog opens");
            if (dlg == nullptr)
                break;
            dlg->getTextEditor ("name")->setText ("Dialog Test");
            for (auto* child : dlg->getChildren())
                if (auto* b = dynamic_cast<juce::Button*> (child))
                    if (b->getButtonText() == (ok ? "OK" : "Cancel"))
                        b->triggerClick();
            runMessages();
            check (! dlg->isShowing(), ok ? "save dialog closes on OK" : "save dialog closes on Cancel");
        }
#if JUCE_LINUX || JUCE_MAC || JUCE_WINDOWS
        if (haveDisplay)
            check (p.presets.userNames().contains ("Dialog Test"), "OK in the save dialog saves the preset");
#endif
        p.presets.deleteUser ("Dialog Test");
        for (int w : { 1300, 2560 })
        {
            ed->setSize (w, w * 968 / 1620);
            check (ed->getWidth() == w, "editor resizes");
        }
    }
    p.editorBeingDeleted (e.get());
    e.reset();
}

static int snapshot (const juce::String& dir)
{
    DSCountProcessor p;
    p.presets.loadFactory (0);   // Transparent Master
    p.prepareToPlay (48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ed = dynamic_cast<DSCountEditor*> (e.get());
    ed->setSize (1620, 968);

    auto shot = [&] (const juce::String& name, float scale)
    {
        for (int i = 0; i < 60; ++i) { pump (p, 3); ed->refreshForTest(); }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
        juce::File f = juce::File (dir).getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream s (f);
        juce::PNGImageFormat().writeImageToStream (img, s);
    };

    for (auto skin : { dsq::theme::Skin::win311, dsq::theme::Skin::classic })
    {
        const juce::String sk = skin == dsq::theme::Skin::classic ? "classic" : "win311";
        dsq::theme::setSkinForTest (skin);
        dsq::theme::setCrtForTest (dsq::theme::Crt::off);
        ed->applySkin();
        p.presets.loadFactory (0);
        shot ("ui_" + sk + "_dynamics.png", 1.0f);
        p.presets.loadFactory (8);   // De-Ess Vocal
        shot ("ui_" + sk + "_deess.png", 1.0f);
        p.presets.loadFactory (0);
        dsq::theme::setCrtForTest (dsq::theme::Crt::soft);
        ed->applySkin();
        shot ("ui_" + sk + "_dynamics_crt.png", 2.0f);
        for (float sc : { 1.0f, 2.0f }) { ed->createComponentSnapshot (ed->getLocalBounds(), true, sc); std::printf ("CRT paint %s x%.0f: %.1f ms (render %.1f)\n", sk.toRawUTF8(), sc, ed->lastCrtPaintMs(), ed->lastCrtRenderMs()); }
    }
    dsq::theme::setSkinForTest (dsq::theme::Skin::win311);
    dsq::theme::setCrtForTest (dsq::theme::Crt::off);
    ed->applySkin();
    ed->setSize (1300, 777);
    shot ("ui_small.png", 1.0f);

    p.editorBeingDeleted (e.get());
    e.reset();
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
        return snapshot (argv[2]);

    testStatePersistence();
    testV1Migration();
    testLatencyAndRates();
    testDoubleAndFloatMatch();
    testPresets();
    testAutomation();
    testEditor();
    std::printf ("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
