#include "saturatorProcessor.h"
#include "saturatorEditor.h"
#include <cmath>

namespace {
constexpr std::array<const char *, 6> parameterIds{"model", "drive",        "bias",
                                                   "tone",  "oversampling", "mix"};
constexpr std::array<const char *, 4> presetNames{"Default soft clip", "Vocal start", "Drum start",
                                                  "Electric guitar start"};
constexpr std::array<SaturatorProcessor::Values, 4> factoryValues{{{0, 0, 0, 0, 1, 1},
                                                                   {2, 5, 0.08F, -0.15F, 1, 0.5F},
                                                                   {1, 8, 0, 0.1F, 2, 0.65F},
                                                                   {3, 14, 0.1F, -0.2F, 2, 0.8F}}};
} // namespace
namespace {
ahq::audio::SaturatorParameters coreParameters(const SaturatorProcessor::Values &v) noexcept {
    return {static_cast<ahq::audio::SaturatorModel>(int(v[0])), v[1], v[2], v[3], 1};
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout SaturatorProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"model", 1}, "Model",
        juce::StringArray{"Soft clip", "Hard clip", "Tube style", "Tape style", "Wavefolder",
                          "Rectifier", "Bitcrusher"},
        0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drive", 1}, "Drive", juce::NormalisableRange<float>(0, 36), 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"bias", 1}, "Bias", juce::NormalisableRange<float>(-1, 1), 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"tone", 1}, "Tone", juce::NormalisableRange<float>(-1, 1), 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"oversampling", 1}, "Quality", juce::StringArray{"1x", "2x", "4x"}, 1));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mix", 1}, "Mix", juce::NormalisableRange<float>(0, 1), 1));
    return layout;
}
SaturatorProcessor::SaturatorProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "SaturatorParameters", createParameterLayout()),
      state(parameters, parameterIds, "AHQSaturatorState", validateValues, currentProgram) {
    setLatencySamples(24);
}

const juce::String SaturatorProcessor::getName() const { return "AHQ Saturator"; }

bool SaturatorProcessor::initialiseEngine(int index, int quality, const Values &values) noexcept {
    auto &engine = engines[size_t(index)];
    if (!engine.core.prepare(currentSampleRate,
                             static_cast<ahq::audio::OversamplingFactor>(quality)) ||
        !engine.core.setParameters(coreParameters(values)))
        return false;
    engine.core.reset();
    engine.padding = {};
    engine.position = 0;
    engine.quality = quality;
    qualityPreparations.fetch_add(1, std::memory_order_relaxed);
    return true;
}
void SaturatorProcessor::prepareToPlay(double rate, int) {
    analysis.prepare(rate);
    currentSampleRate = rate;
    lastValues = readValues();
    inputHistory = {};
    dryHistory = {};
    inputPosition = 0;
    inputCount = 0;
    dryPosition = 0;
    activeEngine = 0;
    fading = false;
    fadePosition = 0;
    prepared = validateValues(lastValues) && initialiseEngine(0, int(lastValues[4]), lastValues);
    fadeFrames = prepared ? std::max(1u, std::uint32_t(std::ceil(rate * 0.005))) : 1;
    mixSmoother.reset(lastValues[5]);
}
void SaturatorProcessor::releaseResources() { prepared = false; }
bool SaturatorProcessor::isBusesLayoutSupported(const BusesLayout &layout) const {
    const auto output = layout.getMainOutputChannelSet();
    return output == layout.getMainInputChannelSet() &&
           (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}
std::array<float, 2> SaturatorProcessor::processEngine(int index,
                                                       std::array<float, 2> input) noexcept {
    auto &engine = engines[size_t(index)];
    (void)engine.core.process(std::span<float>(&input[0], 1), std::span<float>(&input[1], 1));
    const size_t delay = 24 - engine.core.getLatencySamples();
    if (delay > 0) {
        for (size_t channel = 0; channel < 2; ++channel) {
            const float delayed = engine.padding[channel][engine.position];
            engine.padding[channel][engine.position] = input[channel];
            input[channel] = delayed;
        }
        engine.position = (engine.position + 1) % delay;
    }
    return input;
}
void SaturatorProcessor::startQualityChange(int quality, const Values &values) noexcept {
    const int next = 1 - activeEngine;
    if (!initialiseEngine(next, quality, values))
        return;
    // Replay a bounded 512-frame input history, including both the FIR and padding paths.
    // Tone is a finite-history approximation; crossfade covers its remaining small discrepancy.
    const size_t start = (inputPosition + 512 - inputCount) % 512;
    for (size_t frame = 0; frame < inputCount; ++frame) {
        const size_t position = (start + frame) % 512;
        (void)processEngine(next, {inputHistory[0][position], inputHistory[1][position]});
    }
    fadePosition = 0;
    fading = true;
}
void SaturatorProcessor::processBlock(juce::AudioBuffer<float> &b, juce::MidiBuffer &) {
    processAudio(b, false);
}
void SaturatorProcessor::processBlockBypassed(juce::AudioBuffer<float> &b, juce::MidiBuffer &) {
    processAudio(b, true);
}
void SaturatorProcessor::processAudio(juce::AudioBuffer<float> &buffer, bool bypassed) {
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() < 1 || buffer.getNumChannels() > 2)
        return;
    const float inputPeak = PluginAnalysis::peak(buffer);
    const auto requested = readValues();
    if (validateValues(requested))
        lastValues = requested;
    if (prepared) {
        // Requests during a fade are deferred; the latest block snapshot is accepted only after it
        // finishes.
        if (!fading && int(lastValues[4]) != engines[size_t(activeEngine)].quality)
            startQualityChange(int(lastValues[4]), lastValues);
        (void)engines[size_t(activeEngine)].core.setParameters(coreParameters(lastValues));
        if (fading)
            (void)engines[size_t(1 - activeEngine)].core.setParameters(coreParameters(lastValues));
        mixSmoother.setTarget(lastValues[5], fadeFrames);
    }
    auto *left = buffer.getWritePointer(0);
    auto *right = buffer.getNumChannels() == 2 ? buffer.getWritePointer(1) : nullptr;
    for (int frame = 0; frame < buffer.getNumSamples(); ++frame) {
        const std::array<float, 2> input{left[frame], right ? right[frame] : 0};
        std::array<float, 2> dry{};
        for (size_t channel = 0; channel < 2; ++channel) {
            dry[channel] = dryHistory[channel][dryPosition];
            dryHistory[channel][dryPosition] = input[channel];
            inputHistory[channel][inputPosition] = input[channel];
        }
        dryPosition = (dryPosition + 1) % 24;
        inputPosition = (inputPosition + 1) % 512;
        inputCount = std::min(size_t(512), inputCount + 1);
        auto wet = dry;
        if (prepared) {
            wet = processEngine(activeEngine, input);
            if (fading) {
                const auto next = processEngine(1 - activeEngine, input);
                const double weight = double(++fadePosition) / fadeFrames;
                for (size_t channel = 0; channel < 2; ++channel)
                    wet[channel] = float(wet[channel] + (next[channel] - wet[channel]) * weight);
                if (fadePosition >= fadeFrames) {
                    activeEngine = 1 - activeEngine;
                    fading = false;
                }
            }
        }
        const double mix = prepared ? mixSmoother.next() : 0;
        for (size_t channel = 0; channel < size_t(buffer.getNumChannels()); ++channel) {
            const float output = bypassed || mix == 0 ? dry[channel]
                                 : mix == 1
                                     ? wet[channel]
                                     : float(dry[channel] + (wet[channel] - dry[channel]) * mix);
            if (channel == 0)
                left[frame] = output;
            else
                right[frame] = output;
        }
    }
    analysis.publish(buffer, inputPeak);
}
bool SaturatorProcessor::validateValues(std::span<const float> v) noexcept {
    if (v.size() != 6)
        return false;
    for (float value : v)
        if (!std::isfinite(value))
            return false;
    return v[0] >= 0 && v[0] <= 6 && v[0] == std::floor(v[0]) && v[1] >= 0 && v[1] <= 36 &&
           v[2] >= -1 && v[2] <= 1 && v[3] >= -1 && v[3] <= 1 && v[4] >= 0 && v[4] <= 2 &&
           v[4] == std::floor(v[4]) && v[5] >= 0 && v[5] <= 1;
}
int SaturatorProcessor::getNumPrograms() { return static_cast<int>(factoryValues.size()); }
const juce::String SaturatorProcessor::getProgramName(int index) {
    return juce::isPositiveAndBelow(index, getNumPrograms()) ? presetNames[size_t(index)] : "";
}
void SaturatorProcessor::setCurrentProgram(int index) {
    if (juce::isPositiveAndBelow(index, getNumPrograms())) {
        state.writeValues(factoryValues[size_t(index)], false);
        currentProgram.store(index);
        state.invalidateHistory();
    }
}
bool SaturatorProcessor::loadFactoryPreset(int index) {
    if (!juce::isPositiveAndBelow(index, getNumPrograms()))
        return false;
    return applyUserValues(factoryValues[size_t(index)], "Factory preset");
}

juce::AudioProcessorEditor *SaturatorProcessor::createEditor() {
    return new SaturatorEditor(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new SaturatorProcessor(); }

SaturatorProcessor::Values SaturatorProcessor::readValues() const noexcept {
    Values values{};
    state.fillValues(values);
    return values;
}
juce::String SaturatorProcessor::createPresetXml() const { return state.createPresetXml(); }
bool SaturatorProcessor::loadPresetXml(const juce::String &s, bool undoable) {
    return state.loadPresetXml(s, undoable);
}
bool SaturatorProcessor::applyUserValues(const Values &v, const juce::String &n) {
    return state.applyUserValues(v, n);
}
void SaturatorProcessor::recordUserEdit(const Values &v, int i, const juce::String &n) {
    state.recordUserEdit(v, i, n);
}
void SaturatorProcessor::getStateInformation(juce::MemoryBlock &b) { state.getStateInformation(b); }
void SaturatorProcessor::setStateInformation(const void *p, int n) {
    state.setStateInformation(p, n);
}
bool SaturatorProcessor::undo() { return state.undo(); }
bool SaturatorProcessor::redo() { return state.redo(); }

std::array<float, 2> SaturatorProcessor::readLevels() noexcept { return analysis.readLevels(); }
void SaturatorProcessor::setSpectrumEnabled(bool v) noexcept { analysis.setSpectrumEnabled(v); }
bool SaturatorProcessor::readSpectrum(std::span<float> l, std::span<float> r) noexcept {
    return analysis.readSpectrum(l, r);
}
