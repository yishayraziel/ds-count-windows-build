# DSP Architecture (DS-COUNT 2.0)

All DSP is in `Source/DSP`, free of JUCE, 64-bit internally, and tested by `Tests/Unit` without a host.
All algorithms are original designs.

## Signal flow (`Chain.cpp`)

    in gain -> [Precision Dynamics] -> out gain -> dither -> global bypass

* Processing runs in fixed 256-sample sub-blocks, so the output is identical at any host block size.
* Bypass crossfades (10 ms) to a copy of the input delayed by the plug-in latency, so it can be automated without
  timing jumps.
* Changing Low Latency is a structural change: the processor applies it on the message thread with processing
  suspended and reports the new latency to the host.
* Latency at 48 kHz: 5662 samples (118 ms), 302 samples (6.3 ms) in Low Latency. Most of it is the 63 ms
  look-ahead range and the linear-phase band filter; it does not change with the settings.

## Dynamics engine (`Engine.cpp`)

Per channel (L/R or M/S, ganged or independent):

1. Band selection (`LinearPhaseCrossover`): Full, Low Pass, Band Pass or High Pass, linear-phase FIR. The audio
   path is `dry + band x (g - 1)`, so band modes affect only the band and neutral settings are bit-exact.
2. Oversampled detector (4x at 44.1/48 kHz, 2x at 88.2/96 kHz).
3. Level: peak, or RMS with a power time constant of 0.4 x RMS setting (true RMS calibration).
4. Look-ahead: running maximum of the level over the Preview window (0 to 63 ms). Gain reduction starts ahead of
   a transient and does not release before it has passed.
5. Envelope (`Envelope.h`): attack and both releases move the level by two nepers (17.4 dB) per time setting
   (time constant = setting / 2), so release is a straight line in dB. Release Delay holds before releasing.
6. Dual release: Release Fast acts while the envelope is more than 2.5 dB above an average reference; below
   that Release Slow acts (crossfaded over +-0.5 dB). The reference charges with a power time constant of
   2 x Average and discharges with 0.35 x Average. A short burst never charges it (Fast only); after a long burst
   the release runs on Slow until the reference has fallen away and then follows it. Verified with bursts of 50 ms,
   300 ms and 3 s.
7. Transfer curve (`TransferCurve.h`), shared by the audio path and the graph: threshold, ratio from 1:5
   (upward expansion) to 1000:1, quadratic soft knee of half-width 1.3 x knee x |threshold|.
8. Make-up or Max (auto) make-up: the gain that returns 0 dBFS to 0 dBFS.
9. Parallel: dry + processed.
10. Output limiter: Safe, Type 1 or Type 2 (true peak), with Limiter Gain and Ceiling.

## De-essing with the band

There is no separate de-esser: the band selection of step 1 does it. With Band Pass or High
Pass set around the sibilance, the detector sees only that band and the gain change is applied only to it
(`dry + band x (g - 1)`), so the rest of the spectrum passes untouched. Fast attack (0.05 to 0.2 ms), 1 ms
look-ahead and a short release (30 to 50 ms) catch single "s" sounds. Monitor > Detector solos the band.
Measured in `Tests/Unit/TestModules.cpp` (m17 to m19), with the De-Ess Vocal settings at 48 kHz: a 6.5 kHz tone at
-12 dBFS is turned down by 15.0 dB while a 300 Hz tone in the same signal changes by less than 0.001 dB; with
High Pass at 7 kHz a 10 kHz tone drops 13.3 dB and 1 kHz is unchanged.

## Metering

Input and output peak (sample or true peak) and RMS, true-peak maximum, BS.1770-4 loudness, gain reduction per
channel and limiter, detector levels, release activity, level and gain history. Everything shown is measured in the audio thread or computed by the same function as the audio.

## Crossover

Type I symmetric Kaiser-windowed FIR, so phase is exactly linear.

| | Taps at 48 kHz | Block | Kaiser β | Note |
|---|---|---|---|---|
| Normal | 4095 | 256 | 9 | about 70 Hz transition, stopband beyond −100 dB |
| Low Latency | 255 | 32 | 5 | about 1 kHz transition, stopband about −60 dB |

Taps double with each sample-rate octave, to keep the same frequency resolution.

The complement is computed as delayed input minus band. Reconstruction is therefore exact
for any kernel, including during a kernel change. Kernels are built a slice per internal block
on the audio thread, with no allocation, and crossfaded over 6 blocks. All work is on a fixed
internal block grid, so the output is bit-identical at any host block size.

In the audio path `dry + band × (g − 1)` is used, which makes neutral settings bit-exact.

## Limiter

Required gain → sliding minimum over W+1 samples → release → W-sample box average, with the
audio delayed to match. The gain has fully arrived when a peak reaches the output.

| Type | Detection | Window | Release |
|---|---|---|---|
| Safe | Sample peak | 1 ms | 80 ms |
| Type 1 | Sample peak | 3 ms | 40 ms below average gain, 600 ms above |
| Type 2 | Reconstruction peak, 8× at 44.1/48 kHz, 4× at 88.2/96, 2× at 176.4/192 | 5 ms | As Type 1 |

These are original algorithms.

## Oversampling

The detector, envelope and gain run at the oversampled rate, so time constants and peak
detection are finer, and the products of fast gain modulation do not fold back into the audio
band. Only the wet term passes the down-sampler, so a gain of 1 gives exactly zero and neutral
settings remain bit-exact. Up plus down delay is 64 host samples, 16 in Low Latency mode, and
the dry path is delayed by the same amount.

Measured: a 19 kHz tone at 44.1 kHz with 20 µs attack, 1 ms release and 10:1 gave an alias at
12.9 kHz of −30 dBc without oversampling and −119 dBc with it, on the target Mac.

## Dither shapes

These are independent filters, not POW-R, which is a licensed and unpublished algorithm.
Each target curve is converted to a minimum-phase noise transfer function by the real-cepstrum
method for the running sample rate.

| Shape | Order | 2-5 kHz against TPDF, 44.1 kHz | 20-22 kHz against TPDF |
|---|---|---|---|
| Shape 1, flat in the audible band | 16 | −1.5 dB | +12 dB |
| Shape 2, low-order psychoacoustic | 5 | −13 dB | +14 dB |
| Shape 3, high-order psychoacoustic | 9 | −21 dB | +22 dB |

## Real-time rules

No allocation, locks or I/O on the audio thread; a unit test replaces global `operator new` and fails on any
allocation while the full chain processes, including kernel rebuilds and fades. Band filter kernels are rebuilt in
slices on the audio thread (the Linkwitz-Riley bands with one inverse FFT of 16384 points at 48 kHz) and
crossfaded. The Low Latency change is applied with processing suspended.
