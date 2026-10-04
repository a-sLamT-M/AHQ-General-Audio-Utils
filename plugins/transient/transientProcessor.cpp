#include "transientProcessor.h"
#include "transientEditor.h"
#include <cmath>

namespace {
constexpr std::array<const char *, 5> parameterIds{"attack", "sustain", "sensitivity", "time",
                                                   "mix"};
constexpr std::array<const char *, 4> presetNames{"Neutral", "Vocal start", "Drum start",
                                                  "Guitar start"};
constexpr std::array<TransientProcessor::Values, 4> factoryValues{
    {{0, 0, -60, 50, 1}, {-2, 2, -55, 80, 1}, {4, -3, -60, 35, 1}, {2, -1, -60, 65, 1}}};
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout TransientProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"attack", 1}, "Attack", juce::NormalisableRange<float>(-12, 12), 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"sustain", 1}, "Sustain", juce::NormalisableRange<float>(-12, 12), 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"sensitivity", 1}, "Sensitivity",
        juce::NormalisableRange<float>(-80, -20), -60));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"time", 1}, "Time", juce::NormalisableRange<float>(5, 200), 50));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mix", 1}, "Mix", juce::NormalisableRange<float>(0, 1), 1));
    return layout;
}
TransientProcessor::TransientProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "TransientParameters", createParameterLayout()),
      state(parameters, parameterIds, "AHQTransientState", validateValues, currentProgram) {
    setLatencySamples(0);
}

const juce::String TransientProcessor::getName() const { return "AHQ Transient"; }

void TransientProcessor::prepareToPlay(double sampleRate, int) {
    analysis.prepare(sampleRate);
    prepared = filter.prepare(sampleRate);
    if (prepared) {
        const auto values = readValues();
        prepared = filter.setParameters({values[0], values[1], values[2], values[3], values[4]});
        filter.reset();
    }
}
void TransientProcessor::releaseResources() { prepared = false; }
bool TransientProcessor::isBusesLayoutSupported(const BusesLayout &layout) const {
    const auto output = layout.getMainOutputChannelSet();
    return output == layout.getMainInputChannelSet() &&
           (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}
void TransientProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &) {
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumSamples() == 0)
        return;
    const auto inputPeak = PluginAnalysis::peak(buffer);
    const auto values = readValues();
    if (prepared && validateValues(values) && buffer.getNumChannels() >= 1 &&
        buffer.getNumChannels() <= 2) {
        const bool accepted =
            filter.setParameters({values[0], values[1], values[2], values[3], values[4]});
        if (accepted) {
            const auto count = static_cast<size_t>(buffer.getNumSamples());
            const std::span<float> left(buffer.getWritePointer(0), count);
            const auto right = buffer.getNumChannels() == 2
                                   ? std::span<float>(buffer.getWritePointer(1), count)
                                   : std::span<float>{};
            (void)filter.process(left, right);
        }
    }
    analysis.publish(buffer, inputPeak);
}

bool TransientProcessor::validateValues(std::span<const float> v) noexcept {
    if (v.size() != 5)
        return false;
    for (float value : v)
        if (!std::isfinite(value))
            return false;
    return v[0] >= -12 && v[0] <= 12 && v[1] >= -12 && v[1] <= 12 && v[2] >= -80 && v[2] <= -20 &&
           v[3] >= 5 && v[3] <= 200 && v[4] >= 0 && v[4] <= 1;
}

int TransientProcessor::getNumPrograms() { return static_cast<int>(factoryValues.size()); }
const juce::String TransientProcessor::getProgramName(int index) {
    return juce::isPositiveAndBelow(index, getNumPrograms()) ? presetNames[size_t(index)] : "";
}
void TransientProcessor::setCurrentProgram(int index) {
    if (juce::isPositiveAndBelow(index, getNumPrograms())) {
        state.writeValues(factoryValues[size_t(index)], false);
        currentProgram.store(index);
        state.invalidateHistory();
    }
}
bool TransientProcessor::loadFactoryPreset(int index) {
    if (!juce::isPositiveAndBelow(index, getNumPrograms()))
        return false;
    return applyUserValues(factoryValues[size_t(index)], "Factory preset");
}

juce::AudioProcessorEditor *TransientProcessor::createEditor() {
    return new TransientEditor(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new TransientProcessor(); }

TransientProcessor::Values TransientProcessor::readValues() const noexcept {
    Values values{};
    state.fillValues(values);
    return values;
}
juce::String TransientProcessor::createPresetXml() const { return state.createPresetXml(); }
bool TransientProcessor::loadPresetXml(const juce::String &s, bool undoable) {
    return state.loadPresetXml(s, undoable);
}
bool TransientProcessor::applyUserValues(const Values &v, const juce::String &n) {
    return state.applyUserValues(v, n);
}
void TransientProcessor::recordUserEdit(const Values &v, int i, const juce::String &n) {
    state.recordUserEdit(v, i, n);
}
void TransientProcessor::getStateInformation(juce::MemoryBlock &b) { state.getStateInformation(b); }
void TransientProcessor::setStateInformation(const void *p, int n) {
    state.setStateInformation(p, n);
}
bool TransientProcessor::undo() { return state.undo(); }
bool TransientProcessor::redo() { return state.redo(); }

std::array<float, 2> TransientProcessor::readLevels() noexcept { return analysis.readLevels(); }
void TransientProcessor::setSpectrumEnabled(bool v) noexcept { analysis.setSpectrumEnabled(v); }
bool TransientProcessor::readSpectrum(std::span<float> l, std::span<float> r) noexcept {
    return analysis.readSpectrum(l, r);
}
