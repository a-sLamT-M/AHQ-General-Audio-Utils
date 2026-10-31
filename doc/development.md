# Development Guide

## Requirements and setup

Use a C++20 compiler, CMake 3.25+, and Ninja. Plugin builds also need a C compiler.
The DSP core builds on its own; plugin presets fetch JUCE 9.0.3 at commit
`be29c81492b6151c8ea8d14c840e1311963b3a83`, with an archive SHA-256 check.

**Windows:** Install the MSVC C++ tools and Windows SDK, then open an x64 Developer
PowerShell. With localized MSVC, run `chcp 65001` before both configuration and builds.
Keep MSVC plugin builds separate from MinGW build directories.

**Ubuntu/Debian:** Install the audio and GUI development packages below, along with
compiler and build tools that meet the versions above:

```sh
sudo apt install pkg-config libasound2-dev libfontconfig1-dev libfreetype6-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
    libxrandr-dev libxrender-dev libxi-dev
```

For a local JUCE checkout, configure with
`cmake --preset plugins-dev -DFETCHCONTENT_SOURCE_DIR_JUCE=/absolute/path/to/JUCE`.
Use the pinned revision above. The ignored `.tools/` directory holds local caches;
provide your own toolchain on a fresh checkout.

## Build and test

Run commands from the repository root. Each workflow configures, builds, and runs CTest.

| Command | Scope | Configuration |
| --- | --- | --- |
| `cmake --workflow --preset dev` | DSP core and core tests | Debug |
| `cmake --workflow --preset release` | DSP core and core tests | Release |
| `cmake --workflow --preset plugins-dev` | All plugins and tests | Debug |
| `cmake --workflow --preset plugins-release` | All plugins and tests | Release |

For an incremental build or a single test after configuration:

```sh
cmake --build --preset plugins-dev --target ahq_quick_filter_VST3 quickFilterPlugin_test
ctest --preset plugins-dev -R '^quickFilterPlugin_test$'
```

Plugin targets use `ahq_<name>`, replacing hyphens with underscores: for example,
`ahq_ghost_delay`. Format targets append `_VST3`, `_LV2`, or `_Standalone`.
Windows builds VST3 and Standalone; Linux also builds LV2.

Build directories are `build/<OS>/<preset>/`. Plugin bundles are under
`plugins/<plugin>/<target>_artefacts/<Debug|Release>/` within that directory.
To install on Windows, copy the complete `.vst3` folder to
`C:\Program Files\Common Files\VST3` after closing hosts that have loaded it.
On Linux, copy the bundle to your host's configured plugin search directory.
Windows binaries require the MSVC x64 runtime.

## Code layout and changes

| Location | Responsibility |
| --- | --- |
| `include/ahq/audio/` | Public DSP APIs, parameter types, and range declarations |
| `src/` | DSP implementations grouped by processing category |
| `plugins/<plugin>/` | JUCE processor, editor, and plugin build target |
| `plugins/common/` | Shared state, undo, metering transport, and display code |
| `plugins/common/ui/` | `ahq::plugin_ui`: shared native theme, central preset storage and browser |
| `tests/core/` | DSP tests grouped like `src/` |
| `tests/plugins/` | State, automation, rendering, and editor regression tests |

Link DSP consumers against `ahq::audio_core`. Keep DSP implementation independent of
JUCE; the plugin layer handles host lifecycle, automation, UI, and file access.
When adding a processor, register its source and test in the root `CMakeLists.txt`.
For a new plugin, add its directory to `plugins/CMakeLists.txt` and register its plugin test.

Use explicit C++ and the existing shared utilities. Prefer `if` or `switch` for
multi-branch logic. Format changed C++ files with the repository's `.clang-format`
(LLVM style, four-space indentation, 100-column limit; checked with clang-format 19).
Follow the applicable team AFFiNE/NuRules conventions alongside repository instructions.

## DSP and parameter contracts

Call `prepare()` before configuring or processing nonempty audio, and check returned
status values. Use one thread per core processor instance. Pass finite samples in
valid, nonoverlapping buffers; stereo spans must have matching lengths. Public
[headers](../include/ahq/audio/) specify each processor's sample-rate range, reset
behavior, empty-block handling, and failure guarantees.

Keep audio callbacks bounded: allocate buffers during preparation, and perform file
I/O, locking, preset parsing, and growing-history analysis off the audio thread.
The JUCE processors use `juce::ScopedNoDenormals`; other core consumers should handle
denormals in their audio callback. Preserve parameter smoothing and latency-aligned
mixing when changing DSP.

Declare effect parameter bounds in its `*Parameters` type. `ParameterRange` checks
finite values, inclusive bounds, and discrete steps. JUCE layouts reuse these bounds;
sliders read their ranges from registered parameters. `PluginState` caches ranges at
construction and validates complete edits before applying them.

Keep parameter IDs, ordering, plugin IDs, and state schemas stable for existing DAW
projects and presets. Rejected preset loads preserve current values. Host state
restoration invalidates stale undo history; a single-parameter undo must leave other
parameters' automation intact.

Use `juce::ParameterAttachment` to update choice controls. A timer that rewrites a
ComboBox selection can overwrite a user's selection before its asynchronous callback.
The editor tests cover that ordering, undo/redo, and host updates. Spectrum transport
uses a fixed queue; FFT and drawing run on the message thread, with capture enabled
while the editor is visible.

### Shared UI and user presets

All 22 products use the header-only `ahq::plugin_ui` CMake target. `PluginTheme` owns
each editor's LookAndFeel lifetime and maps product names to explicit colours and
function labels. `PresetBar` owns the shared browser, name field, save/import actions
and folder access. DSP-specific editors and their existing native parameter controls
remain separate. The bar reserves 66 pixels below the existing content; Master Scope
continues storing content dimensions in its existing state fields.

`PresetLibrary` uses JUCE's system Documents location plus
`AHQ/Presets/<product name>/`. New files use `.ahqpreset`, atomic temporary-file
replacement, sanitized names and non-overwriting revision suffixes. A browser scans
only its product subdirectory when opened; there are no filesystem timers, services
or audio-callback changes. Import validates the processor's existing XML tag/schema
before copying into the library. Original AHQ XML presets remain readable.

Processors expose `createPresetXml()` and `loadPresetXml()` to the common bar.
Parameter-only presets use `PluginState` validation and undo. Cabinet and Hybrid
Reverb validate and embed IRs; Master Scope includes its display settings. Their
complete-snapshot undo uses the same bounded history as parameter edits. Existing
host state schemas and parameter ordering are retained. `userPreset_test.h` exercises
file round trips, Unicode names, separate plugin folders, preserved same-name files,
invalid/cross-plugin rejection, undo/redo and bar layout for every plugin.

## Processing behavior to preserve

Exact parameter ranges are documented in the linked headers.

| Component | Important behavior |
| --- | --- |
| [Gain](../include/ahq/audio/gainProcessor.h) | Gain and Mix use independent 5 ms ramps. Repeating a target preserves the current ramp; reset moves directly to the target. |
| [Quick Filter](../include/ahq/audio/quickFilter.h) | Q controls the first section; later sections use Q = 1/sqrt(2). Slopes are asymptotic. Cutoff is capped at 0.45 times the sample rate. Topology changes crossfade over 5 ms. |
| [Transient](../include/ahq/audio/transientShaper.h) | A shared stereo detector applies linked gain. Sensitivity sets the activity threshold; Time controls envelope timing. Zero Attack and Sustain preserve the input. |
| [Saturator](../include/ahq/audio/saturator.h) | Seven digital waveshaping models. Core latency is 0/16/24 samples for 1x/2x/4x; the plugin reports a fixed 24 samples and crossfades quality changes. FIR ringing can exceed the curve's peak. |
| [Orbit](../include/ahq/audio/orbit.h) | Horizontal KEMAR binaural rotation of M = (L+R)/2. Depth blends front and rotating responses. BPM sets speed; phase is independent of host beat position. Dry audio is delay-aligned (18 samples at 48 kHz). |
| [Grain Delay](../include/ahq/audio/grainDelay.h) | 64 grain slots and four seconds of history. Pitch-up can increase effective delay to keep reads behind the writer. Seeded events are repeatable across buffer sizes. |
| [Space Lab](../include/ahq/audio/spaceLab.h) | Positive Haas delays right; negative delays left. Split routes dry M left and processed M right, using right-side EQ/polarity and delay. Width, left EQ/polarity, and negative Haas are inactive in Split. Check mono cancellation. |
| [Chaos](../include/ahq/audio/chaos.h) | Events read captured input from nine seconds of history and latch their parameters. Seeds reproduce the same input and parameter timeline. Grain Delay and Chaos use linear-interpolation pitch shifting, which can alias. |
| [Metering](../include/ahq/audio/loudnessMeter.h) | Loudness supports 32–384 kHz: 400 ms momentary, 3 s short-term, and gated integrated readings. Master Scope passes audio unchanged; integrated loudness and LRA run in a background thread even with the editor closed. Queue gaps invalidate whole-program statistics. True Peak readings are finite-filter estimates. |

### True Limiter

The [limiter](../include/ahq/audio/trueLimiter.h) supports 32–384 kHz and linked mono/stereo
processing. The plugin reserves 20 ms of lookahead plus 24 samples (984 samples at
48 kHz). Changing Lookahead or 4x/8x/16x quality keeps this reported latency fixed.

Mix comes **before final peak protection**. Mix=0 retains protection and delay; host
bypass returns delay-aligned original audio while advancing DSP history. The current
algorithm reserves 1.75 dB at 4x and 1.6 dB at 8x/16x, so output can fall below Ceiling.
After lowering Ceiling or enabling additional interpolation phases, prior output
remains in the filter history for up to 24 samples.

Live loudness control uses the received audio's 3 s short-term loudness, updating every
100 ms with 250 ms gain smoothing. Whole-file matching runs two streaming passes on a
worker thread and measures the rendered result. Peak limiting can prevent reaching
the requested LUFS target. Check the result's target-reached and peak-limited fields.
Apply Match restores the analyzed parameter snapshot, sets Mix=1 and Live=Off, and
forms one undo operation. The prediction applies to the analyzed signal, layout, and
sample rate. The core `analyzeMatch()` helper retains full PCM and belongs in offline code.

## Verification

Use core tests for DSP behavior and plugin tests for state, automation, editor events,
channel layouts, sample-rate changes, latency, and offline rendering. GUI tests need a
display server on Linux (a desktop session, WSLg, or configured Xvfb).
For shared plugin changes, also run pluginval on the affected VST3 bundles:

```sh
pluginval --strictness-level 5 --random-seed 42 \
    --sample-rates 44100,48000,96000 --block-sizes 1,17,256,1024 \
    --validate "/absolute/path/to/AHQ Quick Filter.vst3"
```

Recorded baseline, **2026-10-04 / `b4beda8`**: Windows MSVC and Linux GCC Release each
passed 23 tests; all nine Windows VST3 bundles passed pluginval level 5. Linux pluginval
and LV2 load/process checks were recorded before that refactor. DAW playback and
listening checks remain separate acceptance steps.

For meter changes, compare against an independent implementation. Previous FFmpeg
comparisons found up to 0.0244 LU difference in integrated loudness and a 0.869 dB
under-read in the 4x true-peak estimate for one wideband noise signal. Keep the estimate
label and test limiter output separately. Algorithm references:
[BS.1770-5](https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-C.pdf),
[EBU Tech 3341](https://tech.ebu.ch/docs/tech/tech3341.pdf), and
[EBU Tech 3342](https://tech.ebu.ch/docs/tech/tech3342.pdf).

## Troubleshooting and attribution

If localized MSVC builds print raw include traces and `ninja -t deps` shows zero header
dependencies, switch to code page 65001, run
`cmake --fresh --preset plugins-release`, then
`cmake --build --preset plugins-release --clean-first`. This clears stale objects
created with incorrectly parsed header dependencies.

JUCE carries AGPLv3/commercial licensing terms. Embedded MIT KEMAR data has separate
[attribution and usage terms](../src/spatial/data/kemarAttribution.md); its provenance
and generation details are recorded with the data and in
[`buildKemarTable.ps1`](../scripts/buildKemarTable.ps1).

## 2026-10 expansion

The suite now has 22 plugins. Existing plugin/parameter IDs and state schemas remain
unchanged. Host-facing controls map child components back to their parameters and
request the VST3 host's native context menu on right-click. This requires host support;
the automated editor tests use a supplied host context and verify the requested parameter,
menu invocation and absence of unintended value changes.

| Component | Implementation and limits |
| --- | --- |
| Curve EQ | 24 bands; bell, shelves, cuts, notch and band-pass; stereo/L/R/M/S routes. Bell and shelf bands support level-dependent gain. Cuts cascade one to four sections. Minimum-phase IIR processing; no linear-phase mode, spectral matching or commercial preset compatibility. Dragging frequency and gain is one undo operation that preserves unrelated automation. |
| Master Scope | Default 1180×780, resizable; spectrum, phase, waveform and 60-second loudness history. Display gain, 60/90/120 dB ranges, lines/fill/bars and colour choices are saved with the window size in an optional `Display` state element. Restoring state updates an open editor. Display controls never change audio. Detailed metrics remain in the Details menu. |
| Volume Envelope | Eight editable points, default host beat/BPM playback; optional threshold-triggered mono/stereo sidechain input. Crossing the threshold starts the fixed envelope; detector level does not continuously determine gain. Hysteresis prevents repeated triggering near the threshold. Tempo mode passes unity while the host is stopped. |
| Noise Gate / Tremolo | Gate: linked stereo detector, hysteresis, attack/hold/release and range. Tremolo: sine/triangle/square/ramp, host BPM or free Hz. Both use smoothed gain and zero algorithmic latency. |
| Opto Leveler / FET Compressor | Behavioural dynamics, not calibrated LA-2A/1176 circuits. Opto has a fast release component and a history-dependent slow component. FET provides fast attack and ratio selection; All changes threshold/knee/ratio rather than reproducing a hardware circuit's distortion. |
| Cabinet | Four parallel IRs, each with enable, level, balance pan, polarity and 0–20 ms delay. Accepts mono/stereo audio, 8–384 kHz, up to one second per IR. No normalization or trimming. With no loaded IR, input passes through the master gain; loaded but disabled slots are silent. Muted slots clear their history before processing is suspended. |
| Hybrid Reverb | Loaded/generated early IR plus an eight-line Hadamard feedback delay network; separate early/late level, decay, damping, width, predelay and mix. External IRs may be up to 12 seconds. |
| Gated Reverb | Original finite 50–1500 ms nonlinear or reverse IR, brightness, predelay and mix. Inspired by finite nonlinear reverberation workflows; no original machine ROM. IR changes are generated in the background, so length/brightness/shape automation is asynchronous, not sample-accurate. |
| Elevation | Fourteen front-median-plane SADIE II D1 responses from −90° to +90°, interpolated and sample-rate converted. Input becomes M=(L+R)/2 before spatialization. General dummy-head data cannot guarantee elevation perception for a particular listener. The source's −90° response is itself interpolated from −81°. |
| Retro Tools | Three-voice ensemble, slow wow, fast flutter, random speaker-fault breakup, rational waveshaping, low-pass tone and 4–24-bit quantization. Mix uses a fixed 12 ms aligned dry path. |
| Modular | Four analog/wavetable/PCM/noise generators, up to three unison oscillators per generator, preceding-generator phase modulation, two envelopes, two LFOs, filter, eight modulation routes and drive/chorus/reverb. 32 original programs. |
| PCM Station | Two generators, filter, two envelopes/LFOs, four modulation routes, 64 original programs and 16 combinations. Combination mode has eight factory-program parts with enable, MIDI channel, key range, transpose, level and pan. Program edits are saved in plugin state; combination parts reference factory programs, not a user program bank. |

Retro Tools appends `fault` (0–1, default 0) after its original eight parameters. Fault
combines irregular 18–65 Hz flutter, 25–180 ms changes in fault depth, signal-dependent
soft clipping and amplitude collapse. It suggests an overloaded or failing speaker;
it is a behavioural effect, not a calibrated circuit simulation. Fault lowers the
clipping threshold and attenuates the wet signal without makeup gain. Existing Drive
and Bits retain their separate behaviour; Fault is not an output limiter. Try Ensemble=0,
Drive=0, Bits=24, Mix=1 and Fault=0.5–0.8 to isolate it, then blend with Mix.
Fault modulation is stereo-linked, its amount and random targets are smoothed, and its
seeded sequence repeats on reset independently of host buffer size. The original
Flutter control remains the periodic tape effect. Old projects and presets missing
`fault` restore with Fault=0, including when it was enabled before loading.

The instruments use fixed storage for up to 64 active voices, defaulting to 32. Both
consume sample-positioned MIDI, sustain, pitch bend, mod wheel and channel pressure.
Stealing a voice crossfades its previous output. Panic clears voices and effect tails.
The on-screen keyboard uses a bounded event queue. MIDI Program Change messages are
not interpreted; the host's program selector and plugin program selector load programs.
Loading a program keeps the currently selected global effects.

The PCM bank comprises sixteen original, code-generated source sounds with anti-alias
mip levels. Wavetables are also generated from original code. These instruments do not
contain Korg samples or Kilohearts presets and cannot load their proprietary formats.
They implement bounded versions of the same synthesis families, rather than identical
feature sets or sound reproduction.

IR file decoding, generation and encoding happen on a worker thread; JUCE prepares
convolution engines through its message queue. Loaded IR audio is embedded in the
saved state so projects do not depend on the source file paths. The decode limit is
64 MiB per encoded IR. Processing uses the host block-size hint, bounded to 2048 frames,
and divides oversized callbacks without allocation. Nonuniform convolution keeps
early processing short and handles the longer tail separately.

New plugins report zero algorithmic latency except Retro Tools (12 ms; 576 samples at
48 kHz) and Elevation (120 samples at 48 kHz). Their dry paths are aligned. Cabinet slot
delays and reverb predelay are creative timing controls, not added compensation latency.
Graphics and FFT analysis stop when editors are hidden; Master Scope's independent
integrated loudness worker continues as before.

Local performance measurements on **2026-10-20**, AMD Ryzen 9 9950X3D, MSVC Release,
48 kHz stereo, 256-frame blocks: one warm-up plus three timed runs, 1,024,000 frames
(21.333 seconds of audio) per run. The table gives the median processing time divided
by audio duration, as a percentage of one core's time. It is not the DAW CPU meter,
system-wide CPU usage or a real-time deadline guarantee.

| Processing case | Time / audio duration |
| --- | ---: |
| Opto Leveler core, default | 0.108% |
| FET Compressor core, default | 0.096% |
| Curve EQ core, 24 static bells | 0.467% |
| Curve EQ core, 24 dynamic bells | 0.752% |
| Elevation core, settled +90° | 0.789% |
| Feedback reverb core, default | 0.094% |
| Retro Tools core, default | 0.312% |
| PCM core, 32 sustained organ voices | 1.75% |
| Modular core, 32 sustained default-program voices | 3.56% |
| Modular core, 32 voices × 4 wavetable generators × 3 unison | 15.1% |
| Modular core, 64 voices × 4 wavetable generators × 3 unison | 29.1% |
| Cabinet plugin processor, four 4096-tap mono IRs, stereo audio | 4.26% |

Instrument measurements disable reverb; other global effects remain at zero. The core
measurements exclude host parameter handling and GUI cost. The Cabinet measurement
includes its processor wrapper, parameter reading, four convolution engines, per-slot
mixing and copying the input. Changing its convolution configuration from fixed
2048-frame uniform processing to host-sized nonuniform processing reduced the median
from 18.3% to 4.26% in that case, with matching output checksums.
The optional Cabinet timing run is `cabinetPlugin_test --benchmark`.

Cabinet's 2026-10-20 preset/output update appends `master_volume` after the original
20 parameters, with a −60 to +12 dB range, default 0 dB and a 5 ms ramp after summing
the four slots. Old state without this attribute restores at 0 dB. The toolbar exposes
Load/Save `.ahqcabinet` presets containing all four IRs and every parameter; loading
validates all embedded audio before changing the current state. Undo/redo includes
the IR audio as well as parameters, with a 32 MiB history budget retaining at least
one complete transaction. Routing presets (default, single, dual blend, stereo pair,
four-way blend) retain the user's loaded IRs and master volume; they are not bundled
cabinet recordings. Eight affected plugin regression tests and Cabinet pluginval
level 5 passed after this update.

Final Windows verification on 2026-10-20: all **45 CTest tests passed**; all **22 VST3
bundles passed pluginval level 5**, seed 42, sample rates 44.1/48/96 kHz and block sizes
1/17/256/1024. This includes rendering all 96 instrument programs and all 16 combinations,
MIDI sample offsets, sustain/voice stealing, embedded-IR state restoration, open-editor
display restoration and native parameter-menu mapping. Native UI snapshots were
inspected for the main new editors. These checks do not establish subjective sound
quality or Studio Pro compatibility: actual right-click automation creation, playback,
project save, close and reopen in Studio Pro remain unverified. No plugin was installed
into the system VST3 directory during this work. The new code was not built on Linux
in this run.

SADIE data is distributed under Apache-2.0. Its original copyright, license,
source hashes, extraction script and the −90° limitation are recorded in
[sadieAttribution.md](../src/spatial/data/sadieAttribution.md).

## Shared plugin UI

The native `ahq::plugin_ui` theme draws the rotary controls and faders. Related
parameters use rounded groups; flow diagrams describe the processor path rather
than the order of controls on screen. Cabinet displays four parallel IR branches
feeding its master fader. Output/master parameters keep their existing parameter
IDs and host gestures when presented as faders.

Editors with an existing spectrum use a shared display toolbar: line, filled or
bar rendering; 0–600 ms time smoothing; raw, 1/12-, 1/6- or 1/3-octave frequency
smoothing; display gain; and a requested 50–75% spectrum width. Minimum parameter
panel widths take priority in small windows. Time smoothing uses elapsed UI time;
frequency smoothing averages power over neighbouring FFT bins. These operations
run on the message thread and do not change audio, latency or automation values.
Non-spectral editors retain their relevant gain-history, envelope or position
visualisations instead of adding an FFT to the audio path.

Display preferences survive editor recreation within the same processor instance.
Master Scope also saves its display settings with presets and host state; old
states without the new optional attributes use their defaults. Other processors'
new spectrum preferences are instance-local, not saved in project/preset XML.

The existing plugin tests exercise minimum window bounds and both spectrum-area
limits without changing audio parameters. Set `AHQ_UI_SNAPSHOT_DIR` to an existing
folder when running them to export default, minimum-size and area-limit images.
