#include "orbitProcessor.h"
#include "orbitEditor.h"
#include <bit>
#include <cmath>

namespace {
constexpr std::array<const char *, 8> parameterIds{"rate",      "sync",  "bpm",   "beats",
                                                   "direction", "depth", "phase", "mix"};
constexpr std::array<const char *, 4> presetNames{"Slow clockwise", "Slow counterclockwise",
                                                  "One revolution per four beats",
                                                  "Fixed front reference"};
constexpr std::array<OrbitProcessor::Values, 4> factoryValues{{{0.2F, 0, 120, 4, 0, 1, 0, 1},
                                                               {0.2F, 0, 120, 4, 1, 1, 0, 1},
                                                               {0.2F, 1, 120, 4, 0, 1, 0, 1},
                                                               {0, 0, 120, 4, 0, 0, 0, 1}}};
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout OrbitProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"rate", 1}, "Rate", juce::NormalisableRange<float>(0, 10), 0.2F));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"sync", 1}, "Speed mode", juce::StringArray{"Hz", "BPM"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"bpm", 1}, "Manual BPM", juce::NormalisableRange<float>(20, 300), 120));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"beats", 1}, "Beats per revolution",
        juce::NormalisableRange<float>(0.25F, 64), 4));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"direction", 1}, "Direction",
        juce::StringArray{"Clockwise", "Counterclockwise"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"depth", 1}, "Depth", juce::NormalisableRange<float>(0, 1), 1));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"phase", 1}, "Phase", juce::NormalisableRange<float>(0, 360), 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mix", 1}, "Mix", juce::NormalisableRange<float>(0, 1), 1));
    return layout;
}
OrbitProcessor::OrbitProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "OrbitParameters", createParameterLayout()),
      state(parameters, parameterIds, "AHQOrbitState", validateValues, currentProgram) {
    tempoSnapshot.store(std::bit_cast<std::uint32_t>(120.0F));
}

const juce::String OrbitProcessor::getName() const { return "AHQ Orbit"; }

void OrbitProcessor::prepareToPlay(double sampleRate, int) {
    analysis.prepare(sampleRate);
    prepared = filter.prepare(sampleRate);
    if (prepared) {
        const auto v = readValues();
        prepared = filter.setParameters({v[0], v[1] == 1, v[2], v[3],
                                         static_cast<ahq::audio::OrbitDirection>(int(v[4])), v[5],
                                         v[6], v[7]});
        filter.reset();
        delaySamples = filter.getLatencySamples();
        dryHistory = {};
        dryPosition = 0;
        setLatencySamples(int(delaySamples));
        const double taps = std::ceil(128.0 * sampleRate / 44100.0) + 2 * delaySamples + 1;
        tailSeconds.store((taps - 1) / sampleRate);
    }
}
void OrbitProcessor::releaseResources() { prepared = false; }
bool OrbitProcessor::isBusesLayoutSupported(const BusesLayout &layout) const {
    const auto input = layout.getMainInputChannelSet();
    return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() &&
           (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}
OrbitProcessor::TempoInfo OrbitProcessor::readTempo() const noexcept {
    const auto packed = tempoSnapshot.load(std::memory_order_relaxed);
    return {static_cast<TempoSource>(packed >> 32), std::bit_cast<float>(std::uint32_t(packed))};
}
ahq::audio::OrbitParameters OrbitProcessor::effectiveParameters(const Values &v) noexcept {
    float bpm = v[2];
    TempoSource source = v[1] == 1 ? TempoSource::manual : TempoSource::hz;
    if (v[1] == 1) {
        if (auto *playhead = getPlayHead()) {
            if (const auto position = playhead->getPosition()) {
                if (const auto hostBpm = position->getBpm()) {
                    if (std::isfinite(*hostBpm) && *hostBpm >= 20 && *hostBpm <= 300) {
                        bpm = float(*hostBpm);
                        source = TempoSource::host;
                    }
                }
            }
        }
    }
    tempoSnapshot.store(std::bit_cast<std::uint32_t>(bpm) | (std::uint64_t(source) << 32),
                        std::memory_order_relaxed);
    return {v[0], v[1] == 1, bpm, v[3], static_cast<ahq::audio::OrbitDirection>(int(v[4])),
            v[5], v[6],      v[7]};
}
void OrbitProcessor::processBlock(juce::AudioBuffer<float> &b, juce::MidiBuffer &) {
    processAudio(b, false);
}
void OrbitProcessor::processBlockBypassed(juce::AudioBuffer<float> &b, juce::MidiBuffer &) {
    processAudio(b, true);
}
void OrbitProcessor::processAudio(juce::AudioBuffer<float> &buffer, bool bypassed) {
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() != 2)
        return;
    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());
    const auto inputPeak = PluginAnalysis::peak(buffer);
    const auto values = readValues();
    if (prepared && validateValues(values))
        (void)filter.setParameters(effectiveParameters(values));
    auto *left = buffer.getWritePointer(0);
    auto *right = buffer.getWritePointer(1);
    for (int frame = 0; frame < buffer.getNumSamples(); ++frame) {
        std::array<float, 2> wet{left[frame], right[frame]}, dry = wet;
        if (delaySamples > 0) {
            for (size_t channel = 0; channel < 2; ++channel) {
                dry[channel] = dryHistory[channel][dryPosition];
                dryHistory[channel][dryPosition] = wet[channel];
            }
            dryPosition = (dryPosition + 1) % delaySamples;
        }
        if (prepared)
            (void)filter.process({&wet[0], 1}, {&wet[1], 1});
        left[frame] = bypassed || !prepared ? dry[0] : wet[0];
        right[frame] = bypassed || !prepared ? dry[1] : wet[1];
    }
    analysis.publish(buffer, inputPeak);
}
bool OrbitProcessor::validateValues(std::span<const float> v) noexcept {
    if (v.size() != 8)
        return false;
    for (float value : v)
        if (!std::isfinite(value))
            return false;
    return v[0] >= 0 && v[0] <= 10 && (v[1] == 0 || v[1] == 1) && v[2] >= 20 && v[2] <= 300 &&
           v[3] >= 0.25F && v[3] <= 64 && (v[4] == 0 || v[4] == 1) && v[5] >= 0 && v[5] <= 1 &&
           v[6] >= 0 && v[6] <= 360 && v[7] >= 0 && v[7] <= 1;
}
int OrbitProcessor::getNumPrograms() { return static_cast<int>(factoryValues.size()); }
const juce::String OrbitProcessor::getProgramName(int index) {
    return juce::isPositiveAndBelow(index, getNumPrograms()) ? presetNames[size_t(index)] : "";
}
void OrbitProcessor::setCurrentProgram(int index) {
    if (juce::isPositiveAndBelow(index, getNumPrograms())) {
        state.writeValues(factoryValues[size_t(index)], false);
        currentProgram.store(index);
        state.invalidateHistory();
    }
}
bool OrbitProcessor::loadFactoryPreset(int index) {
    if (!juce::isPositiveAndBelow(index, getNumPrograms()))
        return false;
    return applyUserValues(factoryValues[size_t(index)], "Factory preset");
}

juce::AudioProcessorEditor *OrbitProcessor::createEditor() { return new OrbitEditor(*this); }
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new OrbitProcessor(); }

OrbitProcessor::Values OrbitProcessor::readValues() const noexcept {
    Values values{};
    state.fillValues(values);
    return values;
}
juce::String OrbitProcessor::createPresetXml() const { return state.createPresetXml(); }
bool OrbitProcessor::loadPresetXml(const juce::String &s, bool undoable) {
    return state.loadPresetXml(s, undoable);
}
bool OrbitProcessor::applyUserValues(const Values &v, const juce::String &n) {
    return state.applyUserValues(v, n);
}
void OrbitProcessor::recordUserEdit(const Values &v, int i, const juce::String &n) {
    state.recordUserEdit(v, i, n);
}
void OrbitProcessor::getStateInformation(juce::MemoryBlock &b) { state.getStateInformation(b); }
void OrbitProcessor::setStateInformation(const void *p, int n) { state.setStateInformation(p, n); }
bool OrbitProcessor::undo() { return state.undo(); }
bool OrbitProcessor::redo() { return state.redo(); }

std::array<float, 2> OrbitProcessor::readLevels() noexcept { return analysis.readLevels(); }
void OrbitProcessor::setSpectrumEnabled(bool v) noexcept { analysis.setSpectrumEnabled(v); }
bool OrbitProcessor::readSpectrum(std::span<float> l, std::span<float> r) noexcept {
    return analysis.readSpectrum(l, r);
}
