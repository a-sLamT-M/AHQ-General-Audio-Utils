# AHQ General Audio

English | [中文](README.zh-CN.md)

**Note: This project is currently a vibe coding experiment.**

Twenty-two audio plugins built with C++20 and JUCE. Windows supports VST3 and standalone applications; Linux also supports LV2.

| Plugin | Purpose |
| --- | --- |
| Quick Filter | High-pass and low-pass filtering with adjustable frequency, Q, and 12/24/36/48 dB/oct slopes |
| Transient | Shape attack and sustain to add punch or soften transients |
| Saturator | Seven saturation and distortion models with Drive, Bias, Tone, and 1x/2x/4x oversampling |
| Orbit | Binaural rotation for headphones with adjustable speed, direction, depth, and BPM sync |
| Ghost Delay | Granular delay, pitch shifting, random scatter, and feedback |
| Space Lab | Stereo width, Haas delay, independent left/right EQ, polarity inversion, and spatial modulation |
| Chaos | Random slicing, repeats, reverse playback, pitch shifting, panning, and distortion |
| Master Scope | Resizable spectrum, phase, waveform and loudness-history plots; display gain, range, styles and colours; LUFS, LRA, RMS and true-peak estimates |
| True Limiter | Peak limiting, real-time short-term loudness control, and whole-file loudness analysis and matching |
| Curve EQ | 24 draggable bands, stereo/L/R/M/S routing, dynamic bell/shelf bands and an output spectrum |
| Volume Envelope | Editable eight-point volume curve; host BPM/beat sync by default, or threshold-triggered sidechain playback |
| Noise Gate | Stereo-linked gate with hysteresis, attack, hold, release and adjustable attenuation |
| Tremolo | BPM-synced or free-running volume modulation with four shapes |
| Opto Leveler | Opto-style gain reduction with illumination-dependent dual-stage release |
| FET Compressor | Fast FET-style dynamics, 4:1/8:1/12:1/20:1 ratios and an approximate All mode |
| Cabinet | Four parallel mono/stereo IRs with individual level, pan, polarity and delay, master output volume, routing presets and portable preset files containing all IRs |
| Hybrid Reverb | Convolution early reflections plus an eight-line feedback-network tail |
| Gated Reverb | Finite nonlinear/reverse reverb, adjustable length, brightness and predelay |
| Elevation | Headphone positioning from below to above using licensed SADIE II responses |
| Retro Tools | Ensemble, wow/flutter, drive, tone and bit-depth reduction |
| Modular | Four analog/wavetable/PCM/noise generators, two envelopes, two LFOs, eight modulation routes and 32 original programs |
| PCM Station | Two-oscillator PCM programs and eight-part layers/splits; 64 original programs and 16 combinations |

Plugins support host automation, state saving and undo/redo. Parameter controls expose
the host's native context menu when the host provides one. Most effects have dry/wet
controls; Cabinet mixes its four IR outputs directly, and Master Scope passes audio unchanged.

Every plugin has a shared user-preset bar at the bottom. Enter a name and press
`Save preset` to create an `.ahqpreset` file in the system Documents folder under
`AHQ/Presets/<plugin name>/`. Same-name saves keep the previous file as a separate
revision. `User presets` shows only this plugin's library and refreshes when opened.
`Load preset` imports compatible legacy AHQ files into that library; `Folder` opens it.
Preset loads support undo/redo. Cabinet and Hybrid Reverb include their IR audio;
Master Scope includes display gains and settings. Shared native UI components provide
fixed product colours and function labels while preserving existing parameter IDs and sessions.

The new instruments share synthesis methods and editing concepts with modular and PCM
workstations; they do not reproduce proprietary ROM, factory sounds or preset formats.
Their sounds are generated from original code. Opto/FET processing is a behavioural
model, not a calibrated circuit emulation. Gated Reverb uses newly generated impulses.
Elevation uses general dummy-head responses, so perceived height varies by listener.

Performance work keeps allocation, file decoding and IR construction outside audio
processing, bounds synth polyphony to 64 voices, and stops graphical analysis when
editors are hidden. See the [implementation and validation notes](doc/development.md#2026-10-expansion)
for limits, latency and measured processing costs.

## Build

Install a C++20 compiler, CMake 3.25+, and Ninja. On Windows, use MSVC Developer PowerShell. See the [development guide](doc/development.md#requirements-and-setup) for Linux dependencies.

```sh
cmake --workflow --preset plugins-release
```

This command builds and tests all plugins. Output is in `build/<OS>/plugins-release/plugins/<plugin>/<target>_artefacts/Release/`.
For DSP core development, use `cmake --workflow --preset dev`.

## Install

Windows: Copy each complete `.vst3` folder to `C:\Program Files\Common Files\VST3`, then rescan plugins in your host.
Linux: Copy the plugin bundles to your host's configured VST3 or LV2 search directory. Standalone applications can be launched directly.

See the [development guide](doc/development.md) for parameter descriptions, implementation details, and test results.
