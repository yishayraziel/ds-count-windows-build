# Validation report (DS-COUNT 2.0)

## Release candidate 2.0.0, Mac, 10 November 2026

MacBook Pro, Apple Silicon, macOS 27.0.1, Xcode 27.1, JUCE 9.0.5, universal binary (arm64 + x86_64).

| Check | Result |
|---|---|
| DSP test suite (`Tests/Unit`, 46 tests, 2,504,702 checks) | 0 failures |
| Plug-in integration tests (state, A/B, 1.x session and preset migration, latency at 44.1 to 192 kHz, low-latency change, bypass latency, 32/64-bit agreement, all 37 presets, de-ess presets, user presets, automation flags, no removed-module parameters, editor in both skins with and without the screen effect, sizes, save dialog) | 0 failures |
| `auval -v aufx Dsct Yraz` | AU VALIDATION SUCCEEDED |
| pluginval, strictness 5, VST3 | SUCCESS |
| pluginval, strictness 5, AU | SUCCESS |

`Build_DS-COUNT_on_Mac.command` result: `DSCOUNT_BUILD_FINISHED status=0`. (The first run that day failed only
the two save-dialog checks, because the test did not run the Mac message loop; the test was corrected.)

The same suites pass on Linux (x86_64, VST3 and Standalone), including the save-dialog checks.

## Release 2.0.0, Mac, 10 November 2026

`Release_DS-COUNT_on_Mac.command`, result `DSCOUNT_RELEASE_FINISHED status=0`:

| Check | Result |
|---|---|
| VST3, AU, Standalone signed with Developer ID Application (hardened runtime, timestamp) | valid, notarized (Accepted), stapled |
| `DS-COUNT-2.0.0-macOS.pkg` signed with Developer ID Installer | notarized (Accepted), stapled; Gatekeeper: accepted, Notarized Developer ID |
| `DS-COUNT-2.0.0-macOS.dmg` | notarized (Accepted), stapled; Gatekeeper: accepted, Notarized Developer ID |
| Installer packages | not relocatable; VST3 to /Library/Audio/Plug-Ins/VST3, AU to /Library/Audio/Plug-Ins/Components, app to /Applications |

SHA-256: pkg `0ceda42a2a83258698463cd5a8062d3624e5b2780511ca2ad8d2b6c5deee7c8f`,
dmg `073c14aed314b1fba18772950d12b441f5ca67bd00b36ac5e6066a498bcaee93`.

## What the DSP tests cover

* Transfer curve: continuity, ratio law, knee law, expansion, make-up rule.
* Envelope: attack, release and release-delay timing laws; dual release with 50 ms, 300 ms and 3 s bursts; look-ahead as a running maximum.
* Crossover: exact reconstruction, linear phase, kernel changes without discontinuities; Linkwitz-Riley band shapes.
* Limiters: ceilings for sample and true peak at all rates, no overs on worst-case signals.
* De-essing with the band: Band Pass turns down only the sibilance band (300 Hz unchanged within 0.3 dB,
  6.5 kHz down more than 6 dB), High Pass spares the mids, quiet sibilance is untouched.
* Chain: latency equals the dynamics latency, neutral settings bit-exact at normal and low latency, global
  bypass bit-exact with latency kept, external sidechain alignment, loudness meter (-23 LUFS reference, gating).
* Real time: no heap allocation anywhere in the full chain while processing.

## CPU and latency (Dynamics module, measured on the Mac, one core)

| Configuration | Rate | Latency | CPU |
|---|---|---|---|
| Full band, no limiter | 48 kHz | 5662 samples (118 ms) | 6.1 % |
| Band, Type 2 limiter | 48 kHz | 5662 (118 ms) | 9.6 % |
| Band, Type 2 limiter | 96 kHz | 11230 (117 ms) | 10.4 % |
| Band, Type 2 limiter | 192 kHz | 22302 (116 ms) | 7.3 % |
| Low Latency, full band | 48 kHz | 302 (6.3 ms) | 1.3 % |
| Low Latency, band, Type 2 | 48 kHz | 302 (6.3 ms) | 4.6 % |

## Not verified

* Windows build (workflow provided, not run).
* Long listening sessions and host-specific automation behaviour beyond pluginval's checks.
