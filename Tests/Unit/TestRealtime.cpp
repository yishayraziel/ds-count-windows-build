// Proof of real-time safety: the audio path must not touch the heap.
#include "TestHarness.h"
#include "../../Source/DSP/Chain.h"
#include <atomic>
#include <cstdlib>
#include <new>
using namespace dsq;

static std::atomic<long> allocations { 0 };

void* operator new (std::size_t n) { ++allocations; if (void* p = std::malloc (n)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

TEST (t29_no_heap_allocation_in_audio_path)
{
    Rig rig;
    auto& c = rig.params.ch[0];
    c.band.type = FilterType::bandPass;
    c.curve.thresholdDb = -30;
    rig.params.limiterOn = true;
    rig.params.limiterType = LimiterType::type2;
    rig.params.ditherBits = 16;
    rig.params.ditherShape = 3;
    rig.apply();
    auto in = sig::pink (96000, 0.9);
    std::vector<double> o1 (in.size()), o2 (in.size());

    const long before = allocations.load();
    for (int pos = 0; pos + 256 <= (int) in.size(); pos += 256)
    {
        c.band.frequency = 500.0 + pos * 0.05;        // forces kernel rebuilds
        c.curve.thresholdDb = -30.0 + (pos % 4096) * 0.001;
        rig.params.midSide = (pos / 25600) % 2 == 1;  // forces mode fades
        rig.engine.setParams (rig.params);
        const double* ins[2] = { in.data() + pos, in.data() + pos };
        double* outs[2] = { o1.data() + pos, o2.data() + pos };
        rig.engine.process<double> (ins, outs, nullptr, 2, 0, 256);
    }
    CHECK (allocations.load() == before);
}

TEST (t30_mono_path)
{
    Rig rig;
    rig.params.ch[0].band.type = FilterType::highPass;
    rig.params.limiterOn = true;
    rig.apply();
    const int L = rig.engine.latencySamples();
    auto in = sig::pink (48000 + L, 0.7);
    std::vector<double> out (in.size());
    const double* ins[1] = { in.data() };
    double* outs[1] = { out.data() };
    rig.engine.process<double> (ins, outs, nullptr, 1, 0, (int) in.size());
    double err = 0;
    for (size_t i = (size_t) L; i < in.size(); ++i) err = std::max (err, std::abs (out[i] - in[i - (size_t) L]));
    CHECK (err == 0.0);
}

TEST (t31_no_heap_allocation_in_the_full_chain)
{
    static Chain chain;
    ChainParams p;
    p.dynamics.ch[0].curve.thresholdDb = -30;
    p.dynamics.limiterOn = true;
    p.ditherBits = 16;
    p.ditherShape = 2;
    chain.prepare (48000, false);
    chain.setParamsImmediately (p);
    auto in = sig::pink (48000, 0.8);
    std::vector<double> o1 (in.size()), o2 (in.size());
    const long before = allocations.load();
    for (int pos = 0; pos + 256 <= (int) in.size(); pos += 256)
    {
        p.dynamics.ch[0].band.type = (FilterType) ((pos / 8192) % 4);
        p.dynamics.ch[0].band.frequency = 5000.0 + pos * 0.05;
        p.bypass = (pos / 12000) % 2 == 1;
        chain.setParams (p);
        const double* ins[2] = { in.data() + pos, in.data() + pos };
        double* outs[2] = { o1.data() + pos, o2.data() + pos };
        chain.process<double> (ins, outs, nullptr, 2, 0, 256);
    }
    CHECK (allocations.load() == before);
}
