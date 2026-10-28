# Changelog

## 1.1.0 — Updated look & feel

### UI

- Moved Input, Wet/Dry Mix and Output onto ultra-thin vertical glass faders in a
  full-height right-side Mixer Bay, with the stereo OUT METER directly below the
  mixers.
- Trimmed the footer to a slim signal-path strip and moved the MIX control out of
  the Guitar Shape panel to avoid duplicate controls.

## 1.0.0 — Guitar Limiter

Initial release.

### DSP

- Guitar-first feed-forward limiter with fixed 2 ms lookahead and
  latency-matched bypass.
- Thump-filtered detector (20–400 Hz) to prevent palm-mute pumping.
- Program-dependent AUTO release plus manual release mode.
- Absolute soft ceiling, validated against +18 dBFS input.
- BODY (180 Hz low shelf), AIR (3.2 kHz high shelf) and BITE soft saturation.
- Parallel MIX, ±24 dB input/output trim, mono and stereo support.
- Real-time safe implementation: no allocation, locks, or OS calls in
  `process()`.

### UI

- 1180 × 720 hardware-style editor: brushed gunmetal chassis, flush glass
  overlay, crimson LED edge-glow and soft-box reflections.
- Live crimson waveform visualiser, stereo output meter and gain-reduction meter.
- Knurled low-profile knobs, ultra-thin glass sliders and red-bordered tactile
  buttons.
- Preset pill with eight factory programmes and EDITED indicator.

### Tooling

- Offline Linux build against JUCE 8.0.8 (VST3 + Standalone).
- DSP test suite, VST3 host smoke test and 2× UI screenshot renderer.
- GitHub Actions build/test/release for Windows x64, Linux x64 and macOS
  Universal.
