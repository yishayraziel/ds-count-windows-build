# DS-COUNT 2.0 Specification

DS-COUNT is a mastering dynamics processor by Yishay Raziel: one module, Precision Dynamics. DSP, interface and
artwork are original.

    Input gain -> Precision Dynamics -> Output gain -> Dither -> Global bypass

Precision Dynamics is a two-channel (L/R or M/S, ganged or independent) compressor / limiter / expander with
dual adaptive release, look-ahead up to 63 ms, peak or RMS detection, band-selective processing (Full, Low Pass,
Band Pass, High Pass), parallel mode, external sidechain and an output limiter (Safe, Type 1, Type 2 true peak).

De-essing is done inside this module: set the band to Band Pass or High Pass around the
sibilance (about 5 to 9 kHz), use a fast attack and a short release, and only that band is detected and turned
down; Monitor > Detector lets you hear the band. Four factory presets start from there.

Global: input and output gain (-24 to +24 dB), Bypass (crossfade to the latency-aligned input, automatable),
dither (off / 16 / 20 / 24 bit, TPDF or three noise shapes), Low Latency (shorter linear-phase band filters and
at most 1 ms look-ahead), peak meter type (sample or true peak), BS.1770-4 loudness (momentary, short-term,
gated integrated).

Formats: VST3, AU (macOS), Standalone. Mono and stereo. Optional stereo sidechain input ("External").
Sample rates 44.1 to 192 kHz. 32-bit and 64-bit processing; internally 64-bit.


## Session behaviour

* Presets: 37 factory presets, user presets (save,
  save as, rename, delete; stored in `~/Library/Audio/Presets/Yishay Raziel/DS-COUNT`, extension
  `.dscountpreset`), A/B workspaces with copy. Presets and A/B never change session settings (global bypass,
  Low Latency, peak meter type, edit channel).
* Undo/redo of every parameter change.
* DS-COUNT 1.x sessions and presets load with their settings.
* The interface size is stored with the session.

## Factory presets

Transparent Master, Gentle Glue, Loud Master, Streaming Ready (-1 dBTP), Mid/Side Mastering, Bass Control,
Parallel Drum Compression, Upward Dynamics Restoration, De-Ess Vocal (Band Pass 6.5 kHz), De-Ess Bright Vocal
(High Pass 7 kHz), De-Ess Low Sibilance (Band Pass 5 kHz), De-Ess Mix Bus (Mid only, 7.5 kHz),
Cymbal Tamer (High Pass 9 kHz).

More presets: Compressor Anti-Distortion Trumpet, Compressor
Gentle, De-Ess Vocal Hard M/S, De-Ess Vocal Soft M/S, Dual Compressor Vox, Dual Expander Vox, Neutral M/S
Unlinked, Neutral Stereo Linked Ganged, Parallel Comp Tonal Presence, Parallel Comp Tonal Warmth, Parallel Comp
Tonalisation, Parallel Comp Transparent, Upward Expansion Bass Drum, Upward Expansion High Pass, Upward Expansion
Warming, Upward Expansion Wide Band, Bass Boost, De-Ess Hi Hat Hard, De-Ess Hi Hat Soft, De-Ess Vocal Hard,
De-Ess Vocal Soft, Limiter Hard, Limiter Mid, Limiter Soft.

## Parameters as the host sees them

| # | Parameter | Range | Default | Automatable |
|---|---|---|---|---|
| 0 | Dynamics Ch1 Threshold | -60.0 dB to 0.0 dB | -12.0 dB | yes |
| 1 | Dynamics Ch1 Ratio | 1 : 5.00 to 1000 : 1 | 2.00 : 1 | yes |
| 2 | Dynamics Ch1 Soft Knee | 0 % to 100 % | 50 % | yes |
| 3 | Dynamics Ch1 Gain Makeup | -60.0 dB to 60.0 dB | 0.0 dB | yes |
| 4 | Dynamics Ch1 Auto Make-up | Off / On | Off | yes |
| 5 | Dynamics Ch1 Attack | 0.02 ms to 800 ms | 0.10 ms | yes |
| 6 | Dynamics Ch1 Release Delay | 0.00 ms to 8.00 s | 0.00 ms | yes |
| 7 | Dynamics Ch1 Release Fast | 0.02 ms to 8.00 s | 10.0 ms | yes |
| 8 | Dynamics Ch1 Average | 0.02 ms to 8.00 s | 250 ms | yes |
| 9 | Dynamics Ch1 Release Slow | 0.02 ms to 8.00 s | 250 ms | yes |
| 10 | Dynamics Ch1 RMS Detect | Peak to 8.00 s | Peak | yes |
| 11 | Dynamics Ch1 Lookahead | 0.00 ms to 63.0 ms | 0.30 ms | yes |
| 12 | Dynamics Ch1 Band Mode | Full Band / Low Pass / Band Pass / High Pass | Full Band | yes |
| 13 | Dynamics Ch1 Frequency | 20 Hz to 20.00 kHz | 3.20 kHz | yes |
| 14 | Dynamics Ch1 Bandwidth | 0.10 oct to 8.00 oct | 1.00 oct | yes |
| 15 | Dynamics Ch1 Parallel | Off / On | Off | yes |
| 16 | Dynamics Ch2 Threshold | -60.0 dB to 0.0 dB | -12.0 dB | yes |
| 17 | Dynamics Ch2 Ratio | 1 : 5.00 to 1000 : 1 | 2.00 : 1 | yes |
| 18 | Dynamics Ch2 Soft Knee | 0 % to 100 % | 50 % | yes |
| 19 | Dynamics Ch2 Gain Makeup | -60.0 dB to 60.0 dB | 0.0 dB | yes |
| 20 | Dynamics Ch2 Auto Make-up | Off / On | Off | yes |
| 21 | Dynamics Ch2 Attack | 0.02 ms to 800 ms | 0.10 ms | yes |
| 22 | Dynamics Ch2 Release Delay | 0.00 ms to 8.00 s | 0.00 ms | yes |
| 23 | Dynamics Ch2 Release Fast | 0.02 ms to 8.00 s | 10.0 ms | yes |
| 24 | Dynamics Ch2 Average | 0.02 ms to 8.00 s | 250 ms | yes |
| 25 | Dynamics Ch2 Release Slow | 0.02 ms to 8.00 s | 250 ms | yes |
| 26 | Dynamics Ch2 RMS Detect | Peak to 8.00 s | Peak | yes |
| 27 | Dynamics Ch2 Lookahead | 0.00 ms to 63.0 ms | 0.30 ms | yes |
| 28 | Dynamics Ch2 Band Mode | Full Band / Low Pass / Band Pass / High Pass | Full Band | yes |
| 29 | Dynamics Ch2 Frequency | 20 Hz to 20.00 kHz | 3.20 kHz | yes |
| 30 | Dynamics Ch2 Bandwidth | 0.10 oct to 8.00 oct | 1.00 oct | yes |
| 31 | Dynamics Ch2 Parallel | Off / On | Off | yes |
| 32 | Dynamics Monitor | Off / On | Off | yes |
| 33 | Dynamics Sidechain Link | Off / On | On | yes |
| 34 | Dynamics Mid/Side | Off / On | Off | yes |
| 35 | Dynamics Ganged | Off / On | On | yes |
| 36 | Dynamics Edit Channel | 1 / 2 | 1 | no (changes latency or is a view setting) |
| 37 | Dynamics Limiter | Off / On | On | yes |
| 38 | Dynamics Limiter Type | Safe / Type 1 / Type 2 (TP) | Type 1 | yes |
| 39 | Dynamics Ceiling | -12.00 dB to 0.00 dB | -0.10 dB | yes |
| 40 | Dynamics Limiter Gain | -24.0 dB to 30.0 dB | 0.0 dB | yes |
| 41 | Dynamics Sidechain | Internal / External | Internal | yes |
| 42 | Dynamics Sidechain Filter | Off / On | Off | yes |
| 43 | Input | -24.0 dB to 24.0 dB | 0.0 dB | yes |
| 44 | Output | -24.0 dB to 24.0 dB | 0.0 dB | yes |
| 45 | Bypass | Off / On | Off | yes |
| 46 | Dither | Off / 16 bit / 20 bit / 24 bit | Off | yes |
| 47 | Dither Type | TPDF / Shape 1 / Shape 2 / Shape 3 | TPDF | yes |
| 48 | Low Latency | Off / On | Off | no (changes latency or is a view setting) |
| 49 | Peak Meter | Sample Peak / True Peak | True Peak | no (changes latency or is a view setting) |
