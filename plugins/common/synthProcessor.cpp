#include "synthProcessor.h"
#include "synthEditor.h"
#include <cmath>
using ahq::audio::Synthesizer;
namespace {
constexpr const char *generatorFields[]{"source", "wave",  "sample", "voices", "tune",
                                        "fine",   "level", "pan",    "shape",  "fm"};
constexpr const char *envelopeFields[]{"attack", "decay", "sustain", "release"};
constexpr const char *filterFields[]{"cutoff", "resonance", "env", "keytrack", "mode"};
constexpr const char *lfoFields[]{"rate", "beats", "sync", "shape"};
constexpr const char *modFields[]{"source", "target", "amount"};
constexpr const char *effectFields[]{"drive", "chorus", "reverb", "decay", "output", "polyphony"};
constexpr const char *partFields[]{"program", "enabled", "channel", "low",
                                   "high",    "tune",    "level",   "pan"};
} // namespace
std::vector<std::string> SynthProcessor::makeIds(bool pcm) {
    std::vector<std::string> result;
    auto group = [&](const char *prefix, int count, auto &fields) {
        for (int i = 0; i < count; ++i)
            for (auto *field : fields)
                result.push_back(std::string(prefix) + std::to_string(i + 1) + "_" + field);
    };
    group("g", pcm ? 2 : 4, generatorFields);
    group("env", 2, envelopeFields);
    for (auto *field : filterFields)
        result.push_back(std::string("filter_") + field);
    group("lfo", 2, lfoFields);
    group("mod", pcm ? 4 : 8, modFields);
    for (auto *field : effectFields)
        result.push_back(std::string("fx_") + field);
    if (pcm) {
        result.push_back("mode");
        group("part", 8, partFields);
    }
    return result;
}
std::vector<const char *> SynthProcessor::pointers(const std::vector<std::string> &source) {
    std::vector<const char *> result;
    result.reserve(source.size());
    for (const auto &s : source)
        result.push_back(s.c_str());
    return result;
}
SynthProcessor::SynthProcessor(bool usePcm)
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      pcm(usePcm), generatorCount(pcm ? 2 : 4), routeCount(pcm ? 4 : 8),
      envelopeOffset(generatorCount * 10), filterOffset(envelopeOffset + 8),
      lfoOffset(filterOffset + 5), modulationOffset(lfoOffset + 8),
      effectsOffset(modulationOffset + routeCount * 3), modeIndex(effectsOffset + 6),
      partsOffset(modeIndex + 1), parameterCount(pcm ? partsOffset + 64 : modeIndex),
      idStorage(makeIds(pcm)), ids(pointers(idStorage)),
      parameters(*this, nullptr, pcm ? "PcmStationParameters" : "ModularParameters",
                 createParameterLayout()),
      state(parameters, ids, pcm ? "AHQPcmStationState" : "AHQModularState", program) {}
juce::AudioProcessorValueTreeState::ParameterLayout SynthProcessor::createParameterLayout() {
    using G = ahq::audio::SynthGenerator;
    using E = ahq::audio::SynthEnvelope;
    using P = ahq::audio::SynthProgram;
    using M = ahq::audio::SynthModulation;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    Values defaults(size_t(parameterCount), 0.F);
    writeProgram(defaults, pcm ? Synthesizer::pcmProgram(0) : Synthesizer::modularProgram(0));
    auto number = [&](int i, const juce::String &name, ahq::audio::ParameterRange range,
                      float value, double center = 0) {
        auto r = toJuceRange(range);
        if (center > range.minimum && center < range.maximum)
            r.setSkewForCentre(float(center));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[size_t(i)], 1},
                                                               name, r, value));
    };
    auto choice = [&](int i, const juce::String &name, const juce::StringArray &items,
                      int selected) {
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{ids[size_t(i)], 1}, name, items, selected));
    };
    juce::StringArray sampleNames;
    for (size_t i = 0; i < 16; ++i)
        sampleNames.add(Synthesizer::sampleName(i));
    for (int g = 0; g < generatorCount; ++g) {
        const int b = g * 10;
        const auto prefix = "Generator " + juce::String(g + 1) + " ";
        choice(b, prefix + "Source", {"Off", "Analog", "Wavetable", "PCM", "Noise"},
               int(defaults[size_t(b)]));
        choice(b + 1, prefix + "Wave", {"Sine", "Triangle", "Saw", "Pulse"},
               int(defaults[size_t(b + 1)]));
        choice(b + 2, prefix + "Sample", sampleNames, int(defaults[size_t(b + 2)]));
        number(b + 3, prefix + "Unison", G::unisonRange, defaults[size_t(b + 3)]);
        number(b + 4, prefix + "Tune", G::transposeRange, defaults[size_t(b + 4)]);
        number(b + 5, prefix + "Fine (cents)", G::fineRange, defaults[size_t(b + 5)]);
        number(b + 6, prefix + "Level", G::unitRange, defaults[size_t(b + 6)]);
        number(b + 7, prefix + "Pan", G::panRange, defaults[size_t(b + 7)]);
        number(b + 8, prefix + "Shape", G::unitRange, defaults[size_t(b + 8)]);
        number(b + 9, prefix + "Phase modulation", G::phaseModRange, defaults[size_t(b + 9)]);
    }
    for (int e = 0; e < 2; ++e)
        for (int field = 0; field < 4; ++field) {
            const int i = envelopeOffset + e * 4 + field;
            const char *names[]{"Attack (ms)", "Decay (ms)", "Sustain", "Release (ms)"};
            number(i, juce::String(e == 0 ? "Amp " : "Mod ") + names[field],
                   field == 2 ? E::sustainRange : E::timeRange, defaults[size_t(i)],
                   field == 2 ? 0 : 300);
        }
    number(filterOffset, "Cutoff (Hz)", P::cutoffRange, defaults[size_t(filterOffset)], 2000);
    number(filterOffset + 1, "Resonance", P::resonanceRange, defaults[size_t(filterOffset + 1)]);
    number(filterOffset + 2, "Filter envelope (oct)", P::filterEnvelopeRange,
           defaults[size_t(filterOffset + 2)]);
    number(filterOffset + 3, "Key tracking", P::keyTrackRange, defaults[size_t(filterOffset + 3)]);
    choice(filterOffset + 4, "Filter mode", {"Low-pass", "High-pass", "Band-pass", "Bypass"},
           int(defaults[size_t(filterOffset + 4)]));
    for (int l = 0; l < 2; ++l) {
        const int b = lfoOffset + l * 4;
        const auto prefix = "LFO " + juce::String(l + 1) + " ";
        number(b, prefix + "Rate (Hz)", P::lfoRateRange, defaults[size_t(b)], 2);
        number(b + 1, prefix + "Beats", P::lfoBeatsRange, defaults[size_t(b + 1)], 4);
        choice(b + 2, prefix + "Clock", {"Free Hz", "Host BPM"}, int(defaults[size_t(b + 2)]));
        choice(b + 3, prefix + "Shape", {"Sine", "Triangle", "Saw", "Square"},
               int(defaults[size_t(b + 3)]));
    }
    juce::StringArray targets{"Pitch", "Cutoff"};
    for (auto *name : {"Level", "Pan", "Shape", "Phase mod"})
        for (int i = 0; i < generatorCount; ++i)
            targets.add(juce::String(name) + " " + juce::String(i + 1));
    for (int m = 0; m < routeCount; ++m) {
        const int b = modulationOffset + m * 3;
        const auto prefix = "Mod " + juce::String(m + 1) + " ";
        choice(b, prefix + "Source",
               {"Off", "LFO 1", "LFO 2", "Amp envelope", "Mod envelope", "Velocity", "Key",
                "Mod wheel", "Pressure"},
               int(defaults[size_t(b)]));
        choice(b + 1, prefix + "Target", targets, int(defaults[size_t(b + 1)]));
        number(b + 2, prefix + "Amount", M::amountRange, defaults[size_t(b + 2)]);
    }
    number(effectsOffset, "Drive", G::unitRange, 0);
    number(effectsOffset + 1, "Chorus", G::unitRange, 0);
    number(effectsOffset + 2, "Reverb", G::unitRange, .15F);
    number(effectsOffset + 3, "Reverb decay (s)", ahq::audio::ReverbParameters::decayRange, 2);
    number(effectsOffset + 4, "Output (dB)", {-48, 12}, -12);
    number(effectsOffset + 5, "Polyphony", {1, 64, 1}, 32);
    if (pcm) {
        choice(modeIndex, "Mode", {"Program", "Combination"}, 0);
        juce::StringArray programs;
        for (size_t i = 0; i < 64; ++i)
            programs.add(Synthesizer::pcmProgramName(i));
        juce::StringArray channels{"Omni"};
        for (int i = 1; i <= 16; ++i)
            channels.add(juce::String(i));
        for (int part = 0; part < 8; ++part) {
            const int b = partsOffset + part * 8;
            const auto prefix = "Part " + juce::String(part + 1) + " ";
            choice(b, prefix + "Program", programs, part * 8);
            choice(b + 1, prefix + "Enable", {"Off", "On"}, part == 0 ? 1 : 0);
            choice(b + 2, prefix + "Channel", channels, 0);
            number(b + 3, prefix + "Low key", {0, 127, 1}, 0);
            number(b + 4, prefix + "High key", {0, 127, 1}, 127);
            number(b + 5, prefix + "Tune", {-24, 24, 1}, 0);
            number(b + 6, prefix + "Level", {0, 2}, .8F);
            number(b + 7, prefix + "Pan", {-1, 1}, 0);
        }
    }
    return layout;
}
void SynthProcessor::writeProgram(Values &v, const ahq::audio::SynthProgram &p) const {
    for (int i = 0; i < generatorCount; ++i) {
        const auto &g = p.generators[size_t(i)];
        const std::array<float, 10> fields{float(g.source),  float(g.waveform),  float(g.sample),
                                           float(g.unison),  float(g.transpose), float(g.fineCents),
                                           float(g.level),   float(g.pan),       float(g.shape),
                                           float(g.phaseMod)};
        std::copy(fields.begin(), fields.end(), v.begin() + i * 10);
    }
    for (int i = 0; i < 2; ++i) {
        const auto &e = p.envelopes[size_t(i)];
        const std::array<float, 4> fields{float(e.attackMs), float(e.decayMs), float(e.sustain),
                                          float(e.releaseMs)};
        std::copy(fields.begin(), fields.end(), v.begin() + envelopeOffset + i * 4);
    }
    const std::array<float, 5> filter{float(p.cutoffHz), float(p.resonance),
                                      float(p.filterEnvelope), float(p.keyTrack),
                                      float(p.filterMode)};
    std::copy(filter.begin(), filter.end(), v.begin() + filterOffset);
    for (int i = 0; i < 2; ++i) {
        const std::array<float, 4> lfo{float(p.lfoRateHz[size_t(i)]), float(p.lfoBeats[size_t(i)]),
                                       p.lfoSync[size_t(i)] ? 1.F : 0.F,
                                       float(p.lfoShape[size_t(i)])};
        std::copy(lfo.begin(), lfo.end(), v.begin() + lfoOffset + i * 4);
    }
    for (int i = 0; i < routeCount; ++i) {
        const auto &m = p.modulation[size_t(i)];
        v[size_t(modulationOffset + i * 3)] = float(m.source);
        const int target =
            pcm && m.target >= 2 ? 2 + ((m.target - 2) / 4) * 2 + (m.target - 2) % 4 : m.target;
        v[size_t(modulationOffset + i * 3 + 1)] = float(target);
        v[size_t(modulationOffset + i * 3 + 2)] = float(m.amount);
    }
}
ahq::audio::SynthProgram SynthProcessor::readProgram(std::span<const float> v) const noexcept {
    ahq::audio::SynthProgram p;
    for (int i = 0; i < generatorCount; ++i) {
        const size_t b = size_t(i * 10);
        auto &g = p.generators[size_t(i)];
        g.source = int(v[b]);
        g.waveform = int(v[b + 1]);
        g.sample = int(v[b + 2]);
        g.unison = int(v[b + 3]);
        g.transpose = v[b + 4];
        g.fineCents = v[b + 5];
        g.level = v[b + 6];
        g.pan = v[b + 7];
        g.shape = v[b + 8];
        g.phaseMod = v[b + 9];
    }
    for (int i = 0; i < 2; ++i) {
        const size_t b = size_t(envelopeOffset + i * 4);
        p.envelopes[size_t(i)] = {v[b], v[b + 1], v[b + 2], v[b + 3]};
    }
    p.cutoffHz = v[size_t(filterOffset)];
    p.resonance = v[size_t(filterOffset + 1)];
    p.filterEnvelope = v[size_t(filterOffset + 2)];
    p.keyTrack = v[size_t(filterOffset + 3)];
    p.filterMode = int(v[size_t(filterOffset + 4)]);
    for (int i = 0; i < 2; ++i) {
        const size_t b = size_t(lfoOffset + i * 4);
        p.lfoRateHz[size_t(i)] = v[b];
        p.lfoBeats[size_t(i)] = v[b + 1];
        p.lfoSync[size_t(i)] = v[b + 2] != 0;
        p.lfoShape[size_t(i)] = int(v[b + 3]);
    }
    for (int i = 0; i < routeCount; ++i) {
        const size_t b = size_t(modulationOffset + i * 3);
        const int target = int(v[b + 1]);
        const int coreTarget =
            pcm && target >= 2 ? 2 + ((target - 2) / 2) * 4 + (target - 2) % 2 : target;
        p.modulation[size_t(i)] = {int(v[b]), coreTarget, v[b + 2]};
    }
    return p;
}
bool SynthProcessor::configure(std::span<const float> v) noexcept {
    if (!state.validateValues(v))
        return false;
    if (configured && std::equal(v.begin(), v.end(), previous.begin()))
        return true;
    if (pcm && v[size_t(modeIndex)] != 0) {
        for (size_t i = 0; i < 8; ++i) {
            const size_t b = size_t(partsOffset) + i * 8;
            ahq::audio::SynthPart part;
            part.enabled = v[b + 1] != 0;
            part.channel = int(v[b + 2]);
            part.highKey = int(v[b + 4]);
            part.lowKey = std::min(int(v[b + 3]), part.highKey);
            part.transpose = int(v[b + 5]);
            part.level = v[b + 6];
            part.pan = v[b + 7];
            if (!synth.setPart(i, part, Synthesizer::pcmProgram(size_t(v[b]))))
                return false;
        }
    } else {
        const auto patch = readProgram(v);
        ahq::audio::SynthPart part;
        part.enabled = true;
        if (!synth.setPart(0, part, patch))
            return false;
        part.enabled = false;
        for (size_t i = 1; i < 8; ++i)
            if (!synth.setPart(i, part, patch))
                return false;
    }
    const size_t b = size_t(effectsOffset);
    if (!synth.setEffects({v[b], v[b + 1], v[b + 2], v[b + 3], v[b + 4], int(v[b + 5])}))
        return false;
    std::copy(v.begin(), v.end(), previous.begin());
    configured = true;
    return true;
}
void SynthProcessor::prepareToPlay(double rate, int) {
    prepared = synth.prepare(rate);
    configured = false;
    setLatencySamples(0);
    analysis.prepare(rate);
    if (prepared) {
        std::array<float, 128> values{};
        auto v = std::span(values).first(size_t(parameterCount));
        state.fillValues(v);
        prepared = configure(v);
        synth.reset();
    }
}
bool SynthProcessor::isBusesLayoutSupported(const BusesLayout &b) const {
    return b.getMainInputChannelSet().isDisabled() &&
           b.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
void SynthProcessor::midi(const juce::uint8 *data, int size) noexcept {
    if (size < 2)
        return;
    const int type = data[0] & 0xf0, channel = (data[0] & 15) + 1;
    if (type == 0xd0) {
        synth.pressure(channel, float(data[1]) / 127);
        return;
    }
    if (size < 3)
        return;
    switch (type) {
    case 0x90:
        synth.noteOn(channel, data[1], float(data[2]) / 127);
        break;
    case 0x80:
        synth.noteOff(channel, data[1]);
        break;
    case 0xb0:
        synth.controller(channel, data[1], float(data[2]) / 127);
        break;
    case 0xe0:
        synth.pitchBend(channel, float((int(data[2]) << 7) + int(data[1]) - 8192) / 8192);
        break;
    default:
        break;
    }
}
void SynthProcessor::audition(int note, bool on) noexcept {
    if (note < 0 || note > 127)
        return;
    const auto write = guiFifo.write(1);
    if (write.blockSize1)
        guiNotes[size_t(write.startIndex1)] = {note, on};
    else
        panicRequested.store(true);
}
void SynthProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &messages) {
    juce::ScopedNoDenormals noDenormals;
    if (!prepared) {
        buffer.clear();
        messages.clear();
        return;
    }
    std::array<float, 128> values{};
    auto v = std::span(values).first(size_t(parameterCount));
    state.fillValues(v);
    if (!configure(v)) {
        buffer.clear();
        messages.clear();
        return;
    }
    if (auto *head = getPlayHead())
        if (auto position = head->getPosition())
            if (auto bpm = position->getBpm())
                synth.setTempo(*bpm);
    if (panicRequested.exchange(false))
        synth.reset();
    const auto notes = guiFifo.read(guiFifo.getNumReady());
    auto consume = [&](int first, int count) {
        for (int i = 0; i < count; ++i) {
            const auto &n = guiNotes[size_t(first + i)];
            if (n.on)
                synth.noteOn(1, n.note, .8F);
            else
                synth.noteOff(1, n.note);
        }
    };
    consume(notes.startIndex1, notes.blockSize1);
    consume(notes.startIndex2, notes.blockSize2);
    int rendered = 0;
    auto render = [&](int end) {
        if (end <= rendered)
            return;
        const size_t n = size_t(end - rendered);
        (void)synth.process({buffer.getWritePointer(0, rendered), n},
                            {buffer.getWritePointer(1, rendered), n});
        rendered = end;
    };
    for (const auto event : messages) {
        render(std::clamp(event.samplePosition, rendered, buffer.getNumSamples()));
        midi(event.data, event.numBytes);
    }
    render(buffer.getNumSamples());
    messages.clear();
    displayedVoices.store(int(synth.activeVoices()));
    analysis.publish(buffer, 0);
}
const juce::String SynthProcessor::getProgramName(int index) {
    return pcm ? Synthesizer::pcmProgramName(size_t(std::max(0, index)))
               : Synthesizer::modularProgramName(size_t(std::max(0, index)));
}
void SynthProcessor::setCurrentProgram(int index) { loadFactory(index, false); }
void SynthProcessor::loadFactory(int index, bool undoable) {
    if (index < 0 || index >= getNumPrograms())
        return;
    auto values = readValues();
    writeProgram(values, pcm ? Synthesizer::pcmProgram(size_t(index))
                             : Synthesizer::modularProgram(size_t(index)));
    if (pcm)
        values[size_t(modeIndex)] = 0;
    if (undoable)
        state.applyUserValues(values, "Factory program");
    else {
        state.invalidateHistory();
        state.writeValues(values, false);
    }
    program.store(index);
}
void SynthProcessor::loadCombination(int index) {
    if (!pcm || index < 0 || index >= int(Synthesizer::combiCount))
        return;
    auto values = readValues();
    values[size_t(modeIndex)] = 1;
    const auto programs = Synthesizer::combiPrograms(size_t(index));
    for (size_t i = 0; i < 8; ++i) {
        const auto part = Synthesizer::combiPart(size_t(index), i);
        const size_t b = size_t(partsOffset) + i * 8;
        values[b] = float(std::max(0, programs[i]));
        values[b + 1] = part.enabled ? 1.F : 0.F;
        values[b + 2] = float(part.channel);
        values[b + 3] = float(part.lowKey);
        values[b + 4] = float(part.highKey);
        values[b + 5] = float(part.transpose);
        values[b + 6] = float(part.level);
        values[b + 7] = float(part.pan);
    }
    state.applyUserValues(values, "Combination");
}
juce::AudioProcessorEditor *SynthProcessor::createEditor() { return new SynthEditor(*this); }
