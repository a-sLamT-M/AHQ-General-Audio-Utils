#include "quickFilterProcessor.h"
#include "quickFilterEditor.h"
#include <cmath>

namespace {
constexpr std::array<const char *, 5> parameterIds{"mode", "frequency", "q", "slope", "mix"};
constexpr std::array<const char *, 4> presetNames{"Default low pass", "Low cut", "Warm low pass",
                                                  "Telephone high pass"};
constexpr std::array<QuickFilterProcessor::Values, 4> factoryValues{{{0, 1000, 0.7071F, 0, 1},
                                                                     {1, 80, 0.7071F, 1, 1},
                                                                     {0, 3500, 0.7071F, 1, 1},
                                                                     {1, 600, 0.9F, 2, 1}}};

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout QuickFilterProcessor::createParameterLayout() {
    using Parameters = ahq::audio::QuickFilterParameters;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"mode", 1}, "Mode", juce::StringArray{"Low pass", "High pass"}, 0));
    auto frequencyRange = toJuceRange(Parameters::frequencyHzRange);
    frequencyRange.setSkewForCentre(1000);
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"frequency", 1},
                                                           "Frequency", frequencyRange, 1000));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"q", 1}, "Q", toJuceRange(Parameters::qRange), 0.7071F));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"slope", 1}, "Slope",
        juce::StringArray{"12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"mix", 1}, "Mix",
                                                           toJuceRange(Parameters::mixRange), 1));
    return layout;
}

QuickFilterProcessor::QuickFilterProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "QuickFilterParameters", createParameterLayout()),
      state(parameters, parameterIds, "AHQQuickFilterState", currentProgram) {
    setLatencySamples(0);
}

const juce::String QuickFilterProcessor::getName() const { return "AHQ Quick Filter"; }

void QuickFilterProcessor::prepareToPlay(double sampleRate, int) {
    analysis.prepare(sampleRate);
    prepared = filter.prepare(sampleRate);
    if (prepared) {
        const auto values = readValues();
        prepared = filter.setParameters(
            {static_cast<ahq::audio::FilterMode>(int(values[0])), values[1], values[2],
             static_cast<ahq::audio::FilterSlope>(int(values[3])), values[4]});
        filter.reset();
    }
}
void QuickFilterProcessor::releaseResources() { prepared = false; }
bool QuickFilterProcessor::isBusesLayoutSupported(const BusesLayout &layout) const {
    const auto output = layout.getMainOutputChannelSet();
    return output == layout.getMainInputChannelSet() &&
           (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}
void QuickFilterProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &) {
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumSamples() == 0)
        return;
    const auto inputPeak = PluginAnalysis::peak(buffer);
    const auto values = readValues();
    if (prepared && state.validateValues(values) && buffer.getNumChannels() >= 1 &&
        buffer.getNumChannels() <= 2) {
        const bool accepted = filter.setParameters(
            {static_cast<ahq::audio::FilterMode>(int(values[0])), values[1], values[2],
             static_cast<ahq::audio::FilterSlope>(int(values[3])), values[4]});
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

int QuickFilterProcessor::getNumPrograms() { return static_cast<int>(factoryValues.size()); }
const juce::String QuickFilterProcessor::getProgramName(int index) {
    return juce::isPositiveAndBelow(index, getNumPrograms()) ? presetNames[size_t(index)] : "";
}
void QuickFilterProcessor::setCurrentProgram(int index) {
    if (juce::isPositiveAndBelow(index, getNumPrograms())) {
        state.writeValues(factoryValues[size_t(index)], false);
        currentProgram.store(index);
        state.invalidateHistory();
    }
}
bool QuickFilterProcessor::loadFactoryPreset(int index) {
    if (!juce::isPositiveAndBelow(index, getNumPrograms()))
        return false;
    return applyUserValues(factoryValues[size_t(index)], "Factory preset");
}

juce::AudioProcessorEditor *QuickFilterProcessor::createEditor() {
    return new QuickFilterEditor(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new QuickFilterProcessor(); }

QuickFilterProcessor::Values QuickFilterProcessor::readValues() const noexcept {
    Values values{};
    state.fillValues(values);
    return values;
}
juce::String QuickFilterProcessor::createPresetXml() const { return state.createPresetXml(); }
bool QuickFilterProcessor::loadPresetXml(const juce::String &s, bool undoable) {
    return state.loadPresetXml(s, undoable);
}
bool QuickFilterProcessor::applyUserValues(const Values &v, const juce::String &n) {
    return state.applyUserValues(v, n);
}
void QuickFilterProcessor::recordUserEdit(const Values &v, int i, const juce::String &n) {
    state.recordUserEdit(v, i, n);
}
void QuickFilterProcessor::getStateInformation(juce::MemoryBlock &b) {
    state.getStateInformation(b);
}
void QuickFilterProcessor::setStateInformation(const void *p, int n) {
    state.setStateInformation(p, n);
}
bool QuickFilterProcessor::undo() { return state.undo(); }
bool QuickFilterProcessor::redo() { return state.redo(); }

std::array<float, 2> QuickFilterProcessor::readLevels() noexcept { return analysis.readLevels(); }
void QuickFilterProcessor::setSpectrumEnabled(bool v) noexcept { analysis.setSpectrumEnabled(v); }
bool QuickFilterProcessor::readSpectrum(std::span<float> l, std::span<float> r) noexcept {
    return analysis.readSpectrum(l, r);
}
