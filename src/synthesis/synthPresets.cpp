#include "ahq/audio/synthesizer.h"
#include <algorithm>
namespace ahq::audio {
namespace {
constexpr const char *samples[]{"Sine",          "Triangle",       "Saw",
                                "Pulse",         "Original Piano", "Original Electric Piano",
                                "Drawbar Organ", "Metal Bell",     "Wood Mallet",
                                "String Cycle",  "Vowel Cycle",    "Brass Cycle",
                                "Round Bass",    "Harmonic Pluck", "Synth Kick",
                                "Synth Snare"};
constexpr const char *pcmNames[]{
    "Daylight Piano",  "Soft Felt",         "Bright Keys",      "Octave Piano",      "Quiet Room",
    "Layered Keys",    "Piano and Air",     "Night Piano",      "Electric Stage",    "Glass Tines",
    "Warm Suitcase",   "Dyno Keys",         "Electric Octaves", "Electric and Bell", "Dream Keys",
    "Soft Electric",   "Drawbar Eight",     "Soft Drawbars",    "Bright Organ",      "Octave Bars",
    "Cathedral Layer", "Rotary Memory",     "Organ and Choir",  "Low Drawbars",      "Metal Bell",
    "Soft Bell",       "Bright Chime",      "Bell Octaves",     "Temple Bell",       "Glass Pad",
    "Bell and Air",    "Low Chime",         "Wood Mallet",      "Soft Marimba",      "Hard Mallet",
    "Mallet Octaves",  "Music Box",         "Mallet and Bell",  "Wood and Strings",  "Low Marimba",
    "String Ensemble", "Muted Strings",     "Bright Strings",   "Octave Strings",    "Slow Strings",
    "String Machine",  "Strings and Choir", "Low Strings",      "Vowel Choir",       "Soft Voices",
    "Air Choir",       "Choir Octaves",     "Slow Vowels",      "Voices and Glass",  "Wide Voices",
    "Dark Choir",      "Round Bass",        "Soft Bass",        "Bright Bass",       "Octave Bass",
    "Deep Bass",       "Bass and Pluck",    "Analog Bass",      "Short Bass"};
constexpr const char *modularNames[]{
    "Analog Foundation", "Pulse Motion",  "Triangle Duo",    "Sine Stack",      "Warm Unison",
    "Wide Pulse",        "Octave Lead",   "Soft Fifth",      "Morphing Glass",  "Wavetable Sweep",
    "Hollow Motion",     "Drawbar Morph", "Phase Bell",      "Phase Bass",      "Metallic Lead",
    "Phase Pluck",       "Piano and Saw", "Electric Hybrid", "Choir and Pulse", "Mallet Dust",
    "Noise Tide",        "Filtered Air",  "Noise Pluck",     "Wind Layer",      "Slow Horizon",
    "Velvet Pad",        "Bright Canopy", "Dark Swell",      "Sync Pulse",      "Stepped Glass",
    "Wheel Vibrato",     "Pressure Bloom"};
constexpr const char *combiNames[]{
    "Piano and Strings", "Electric and Choir",  "Bell Atmosphere",   "Organ Ensemble",
    "Piano Bass Split",  "Electric Bass Split", "Mallet Bass Split", "Organ Bass Split",
    "Octave Piano",      "Glass Stack",         "Choir Orchestra",   "Warm Layers",
    "Eight-Part Studio", "Low Key Layers",      "High Key Layers",   "Full Spectrum"};
} // namespace
const char *Synthesizer::sampleName(size_t index) noexcept {
    return samples[std::min(index, size_t(15))];
}
const char *Synthesizer::pcmProgramName(size_t index) noexcept {
    return pcmNames[index % pcmProgramCount];
}
const char *Synthesizer::modularProgramName(size_t index) noexcept {
    return modularNames[index % modularProgramCount];
}
const char *Synthesizer::combiName(size_t index) noexcept { return combiNames[index % combiCount]; }
SynthProgram Synthesizer::pcmProgram(size_t index) {
    index %= pcmProgramCount;
    const size_t family = index / 8, variation = index % 8;
    constexpr int source[]{4, 5, 6, 7, 8, 9, 10, 12};
    SynthProgram p;
    auto &g = p.generators[0];
    g.source = 3;
    g.sample = source[family];
    g.level = .8;
    p.envelopes[0] = {family == 5 || family == 6 ? 120. : 3., 300, family == 7 ? .65 : .85, 350};
    p.envelopes[1] = {10, 400, 0, 300};
    p.cutoffHz = 11000;
    p.keyTrack = .35;
    if (family == 7) {
        p.cutoffHz = 3500;
        p.filterEnvelope = 1;
        p.envelopes[0].releaseMs = 120;
    }
    switch (variation) {
    case 1:
        p.cutoffHz = 3200;
        p.envelopes[0].attackMs *= 2;
        g.level = .7;
        break;
    case 2:
        p.cutoffHz = 18000;
        p.resonance = .2;
        p.filterEnvelope = .5;
        break;
    case 3:
        p.generators[1] = g;
        p.generators[1].transpose = 12;
        p.generators[1].level = .25;
        break;
    case 4:
        p.cutoffHz = 5000;
        p.envelopes[0].attackMs = family >= 5 ? 600 : 12;
        p.envelopes[0].releaseMs = 900;
        break;
    case 5:
        p.generators[1] = g;
        p.generators[1].sample = family < 5 ? 7 : 11;
        p.generators[1].level = .18;
        p.generators[0].pan = -.2;
        p.generators[1].pan = .2;
        break;
    case 6:
        p.generators[1] = g;
        p.generators[1].sample = family == 7 ? 2 : 10;
        p.generators[1].level = .2;
        p.generators[1].fineCents = 7;
        p.envelopes[0].releaseMs = 700;
        break;
    case 7:
        p.cutoffHz = 2500;
        p.envelopes[0].releaseMs = 120;
        g.transpose = family == 7 ? 0 : -12;
        break;
    default:
        break;
    }
    if (family == 5 || family == 6) {
        p.lfoRateHz[0] = 4.7;
        p.modulation[0] = {1, 0, .004};
    }
    return p;
}
SynthProgram Synthesizer::modularProgram(size_t index) {
    index %= modularProgramCount;
    const int variant = int(index % 4), family = int(index / 4);
    SynthProgram p;
    auto &a = p.generators[0];
    a.source = 1;
    a.waveform = variant;
    a.level = .65;
    p.cutoffHz = 7000;
    p.envelopes[1] = {10, 600, 0, 300};
    switch (family) {
    case 0:
        p.generators[1] = a;
        p.generators[1].fineCents = 5;
        p.generators[1].level = .25;
        p.generators[1].transpose = variant == 3 ? 12 : 0;
        break;
    case 1:
        a.unison = 3;
        a.level = .45;
        a.waveform = variant % 2 ? 3 : 2;
        p.generators[1] = a;
        p.generators[1].transpose = variant == 3 ? 7 : 12;
        p.generators[1].level = .2;
        break;
    case 2:
        a.source = 2;
        a.shape = .2 + variant * .2;
        p.modulation[0] = {1, 10, .2};
        p.lfoRateHz[0] = .15 + variant * .12;
        p.envelopes[0].attackMs = 60;
        break;
    case 3:
        a.waveform = 0;
        a.level = .05;
        a.transpose = 12;
        p.generators[1] = a;
        p.generators[1].level = .7;
        p.generators[1].transpose = 0;
        p.generators[1].phaseMod = .5 + variant * .7;
        p.modulation[0] = {4, 15, .3};
        p.envelopes[0] = {2, 500, variant == 2 ? .7 : 0., 350};
        p.cutoffHz = 14000;
        break;
    case 4:
        a.source = 3;
        a.sample = variant == 0 ? 4 : variant == 1 ? 5 : variant == 2 ? 10 : 8;
        p.generators[1].source = variant == 3 ? 4 : 1;
        p.generators[1].waveform = variant % 2 ? 3 : 2;
        p.generators[1].level = .2;
        p.generators[1].transpose = -12;
        p.cutoffHz = 5500;
        break;
    case 5:
        a.source = 4;
        a.level = .35;
        p.cutoffHz = 1200 + variant * 1100;
        p.resonance = .65;
        p.envelopes[0] = {variant == 2 ? 1. : 300., 700, variant == 2 ? 0. : .7, 800};
        p.modulation[0] = {1, 1, .3};
        break;
    case 6:
        a.source = 2;
        a.unison = 3;
        a.level = .4;
        a.shape = .3 + variant * .15;
        p.envelopes[0] = {500 + variant * 200., 800, .8, 1600};
        p.filterEnvelope = 1;
        p.cutoffHz = variant == 3 ? 1200 : 3500;
        p.modulation[0] = {1, 6, .3};
        p.modulation[1] = {2, 10, .2};
        break;
    case 7:
        a.source = variant == 1 ? 2 : 1;
        a.waveform = 3;
        a.shape = .35;
        p.lfoSync[0] = variant < 2;
        p.lfoBeats[0] = .5;
        p.lfoRateHz[0] = 5;
        p.lfoShape[0] = variant == 1 ? 3 : 0;
        if (variant < 2)
            p.modulation[0] = {1, 1, .3};
        else if (variant == 2) {
            p.modulation[0] = {1, 0, .003};
            p.modulation[1] = {7, 1, .5};
        } else
            p.modulation[0] = {8, 1, .8};
        break;
    }
    return p;
}
std::array<int, 8> Synthesizer::combiPrograms(size_t index) noexcept {
    constexpr std::array<std::array<int, 8>, 16> programs{{{0, 40, -1, -1, -1, -1, -1, -1},
                                                           {8, 48, -1, -1, -1, -1, -1, -1},
                                                           {24, 44, 48, -1, -1, -1, -1, -1},
                                                           {16, 41, 49, -1, -1, -1, -1, -1},
                                                           {56, 0, -1, -1, -1, -1, -1, -1},
                                                           {57, 8, -1, -1, -1, -1, -1, -1},
                                                           {56, 32, -1, -1, -1, -1, -1, -1},
                                                           {58, 16, -1, -1, -1, -1, -1, -1},
                                                           {0, 3, -1, -1, -1, -1, -1, -1},
                                                           {9, 24, 32, -1, -1, -1, -1, -1},
                                                           {40, 48, 51, -1, -1, -1, -1, -1},
                                                           {1, 9, 41, -1, -1, -1, -1, -1},
                                                           {0, 8, 16, 24, 32, 40, 48, 56},
                                                           {56, 47, 55, -1, -1, -1, -1, -1},
                                                           {26, 34, 50, -1, -1, -1, -1, -1},
                                                           {0, 8, 40, 48, -1, -1, -1, -1}}};
    return programs[index % combiCount];
}
SynthPart Synthesizer::combiPart(size_t index, size_t part) noexcept {
    index %= combiCount;
    part = std::min(part, size_t(7));
    SynthPart p;
    p.enabled = combiPrograms(index)[part] >= 0;
    p.level = part == 0 ? .8 : .35;
    p.pan = part == 0 ? 0 : part % 2 ? -.25 : .25;
    if (index >= 4 && index <= 7) {
        if (part == 0)
            p.highKey = 59;
        else
            p.lowKey = 60;
    }
    if (index == 8 && part == 1)
        p.transpose = 12;
    if (index == 12) {
        p.channel = int(part + 1);
        p.level = .8;
    }
    if (index == 13)
        p.highKey = 71;
    if (index == 14)
        p.lowKey = 48;
    return p;
}
} // namespace ahq::audio
