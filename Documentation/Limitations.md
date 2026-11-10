# Known limitations (DS-COUNT 2.0)

## Design
* There is no over counter and the meter display options are fixed.
* There is no separate de-esser module; de-essing uses the band.
* Latency is 118 ms at 48 kHz, 6.3 ms in Low Latency.

## Platform and build
* Built and tested on macOS (Apple Silicon) and Linux. The Windows VST3 is not built or tested in this
  delivery; a GitHub Actions workflow is included.
* Bundles are ad-hoc signed for the building Mac. Distribution needs Developer ID signing and notarization.
* AAX is not built (it needs the Avid SDK and signing).

## DSP
* Look-ahead up to 63 ms is part of the latency even when set short, so that changing
  it never changes the reported latency. Low Latency mode limits it to 1 ms.
* Content within about 2 kHz of Nyquist at 44.1/48 kHz gets slightly less gain change
  (only the oversampled wet term is band-limited).
* The Safe and Type 1 limiters control sample peaks only: inter-sample peaks can pass.
* In Low Latency mode the band filters are short (about 1 kHz transition at 48 kHz), so a de-ess band reaches
  further down than its frequency setting suggests.

## Interface
* The interface keeps a fixed 1620:968 aspect ratio and scales from 1300 to 2560 pixels wide.
* The skin and the screen effect are global preferences (saved in `~/Library/Application Support/DS-COUNT/Settings.xml`
  on a Mac), not part of a session: all open instances follow the last change.
* The screen effect renders the whole editor off-screen and filters it on the message thread. On the Linux build
  machine a full frame took about 25 to 36 ms at 1620 x 968; the editor refreshes at 20 Hz while it is on
  (30 Hz when off). Turn it off on slow machines or with many editors open. Audio processing is not affected.
* With the screen effect on, the editor is drawn at no more than about 1.8 million pixels and scaled to the
  window, so on large Retina windows it is slightly softer than with the effect off (by design, it is a CRT).
* The 3.11 and Classic menu bars, title bars and window buttons are part of the drawing: the minimise and
  maximise boxes do nothing; the menus work.
* There is no MIDI learn or external control mapping beyond host automation.
