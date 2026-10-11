#include "chaosProcessor.h"
#include "chaosEditor.h"
#include <cmath>
#include <limits>

namespace {
constexpr std::array<const char *, 10> parameterIds{
    "slice", "probability", "repeats",    "reverse", "pitchRange",
    "pan",   "drive",       "distortion", "seed",    "mix"};
constexpr std::array<const char *, 4> presetNames{"Dry decisions", "Repeating fragments",
                                                  "Reverse drift", "Distorted scatter"};
constexpr std::array<ChaosProcessor::Values, 4> factoryValues{
    {{100, 0, 1, 0, 0, 0, 0, 0, 1, 1},
     {100, 1, 4, 0, 0, 0, 0, 0, 42, 0.7F},
     {180, 0.8F, 2, 1, 12, 0.4F, 0, 0, 17, 0.7F},
     {40, 0.7F, 3, 0.5F, 24, 1, 18, 0.8F, 123, 0.6F}}};
ahq::audio::ChaosParameters coreParameters(const ChaosProcessor::Values &v) noexcept {
    return {v[0], v[1], std::uint32_t(v[2]), v[3], v[4], v[5],
            v[6], v[7], std::uint32_t(v[8]), v[9]};
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout ChaosProcessor::createParameterLayout() {
    using Parameters = ahq::audio::ChaosParameters;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    constexpr std::array<const char *, 10> names{"Slice length", "Slice probability",
                                                 "Repeats",      "Reverse probability",
                                                 "Pitch range",  "Pan amount",
                                                 "Drive",        "Distortion probability",
                                                 "Seed",         "Mix"};
    constexpr std::array<ahq::audio::ParameterRange, 10> ranges{
        {Parameters::sliceMsRange,
         Parameters::probabilityRange,
         Parameters::repeatsRange,
         Parameters::reverseProbabilityRange,
         Parameters::pitchRangeSemitonesRange,
         Parameters::panAmountRange,
         Parameters::driveDbRange,
         Parameters::distortionProbabilityRange,
         {0, 65535, 1},
         Parameters::mixRange}};
    for (size_t i = 0; i < parameterIds.size(); ++i) {
        if (i == 2 || i == 8)
            layout.add(std::make_unique<juce::AudioParameterInt>(
                juce::ParameterID{parameterIds[i], 1}, names[i], int(ranges[i].minimum),
                int(ranges[i].maximum), int(factoryValues[0][i])));
        else
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{parameterIds[i], 1}, names[i], toJuceRange(ranges[i]),
                factoryValues[0][i]));
    }
    return layout;
}
ChaosProcessor::ChaosProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "ChaosParameters", createParameterLayout()),
      state(parameters, parameterIds, "AHQChaosState", currentProgram) {
    setLatencySamples(0);
}

const juce::String ChaosProcessor::getName() const { return "AHQ Chaos"; }

void ChaosProcessor::prepareToPlay(double rate, int) {
    analysis.prepare(rate);
    prepared = filter.prepare(rate);
    if (prepared) {
        const auto v = readValues();
        prepared = state.validateValues(v) && filter.setParameters(coreParameters(v));
        filter.reset();
    }
}
void ChaosProcessor::releaseResources() { prepared = false; }
bool ChaosProcessor::isBusesLayoutSupported(const BusesLayout &layout) const {
    const auto output = layout.getMainOutputChannelSet();
    return output == layout.getMainInputChannelSet() &&
           (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}
void ChaosProcessor::processBlock(juce::AudioBuffer<float> &b, juce::MidiBuffer &) {
    processAudio(b, false);
}
void ChaosProcessor::processBlockBypassed(juce::AudioBuffer<float> &b, juce::MidiBuffer &) {
    processAudio(b, true);
}
void ChaosProcessor::processAudio(juce::AudioBuffer<float> &buffer, bool bypassed) {
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() < 1 || buffer.getNumChannels() > 2)
        return;
    const float inputPeak = PluginAnalysis::peak(buffer);
    const auto v = readValues();
    if (prepared && state.validateValues(v)) {
        (void)filter.setParameters(coreParameters(v));
        if (!bypassed) {
            const size_t count = size_t(buffer.getNumSamples());
            (void)filter.process({buffer.getWritePointer(0), count},
                                 buffer.getNumChannels() == 2
                                     ? std::span<float>(buffer.getWritePointer(1), count)
                                     : std::span<float>{});
        } else {
            for (int frame = 0; frame < buffer.getNumSamples(); ++frame) {
                std::array<float, 2> wet{
                    buffer.getReadPointer(0)[frame],
                    buffer.getNumChannels() == 2 ? buffer.getReadPointer(1)[frame] : 0};
                (void)filter.process({&wet[0], 1}, buffer.getNumChannels() == 2
                                                       ? std::span<float>(&wet[1], 1)
                                                       : std::span<float>{});
            }
        }
    }
    analysis.publish(buffer, inputPeak);
}
// Conservative silence tail: nine seconds of history plus the longest 8.25-second event.
// Zero input can still capture past audio until it leaves history; there is no feedback.
double ChaosProcessor::getTailLengthSeconds() const { return 17.25; }
int ChaosProcessor::getNumPrograms() { return static_cast<int>(factoryValues.size()); }
const juce::String ChaosProcessor::getProgramName(int index) {
    return juce::isPositiveAndBelow(index, getNumPrograms()) ? presetNames[size_t(index)] : "";
}
void ChaosProcessor::setCurrentProgram(int index) {
    if (juce::isPositiveAndBelow(index, getNumPrograms())) {
        state.writeValues(factoryValues[size_t(index)], false);
        currentProgram.store(index);
        state.invalidateHistory();
    }
}
bool ChaosProcessor::loadFactoryPreset(int index) {
    if (!juce::isPositiveAndBelow(index, getNumPrograms()))
        return false;
    return applyUserValues(factoryValues[size_t(index)], "Factory preset");
}

juce::AudioProcessorEditor *ChaosProcessor::createEditor() { return new ChaosEditor(*this); }
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new ChaosProcessor(); }

ChaosProcessor::Values ChaosProcessor::readValues() const noexcept {
    Values values{};
    state.fillValues(values);
    return values;
}
juce::String ChaosProcessor::createPresetXml() const { return state.createPresetXml(); }
bool ChaosProcessor::loadPresetXml(const juce::String &s, bool undoable) {
    return state.loadPresetXml(s, undoable);
}
bool ChaosProcessor::applyUserValues(const Values &v, const juce::String &n) {
    return state.applyUserValues(v, n);
}
void ChaosProcessor::recordUserEdit(const Values &v, int i, const juce::String &n) {
    state.recordUserEdit(v, i, n);
}
void ChaosProcessor::getStateInformation(juce::MemoryBlock &b) { state.getStateInformation(b); }
void ChaosProcessor::setStateInformation(const void *p, int n) { state.setStateInformation(p, n); }
bool ChaosProcessor::undo() { return state.undo(); }
bool ChaosProcessor::redo() { return state.redo(); }

std::array<float, 2> ChaosProcessor::readLevels() noexcept { return analysis.readLevels(); }
void ChaosProcessor::setSpectrumEnabled(bool v) noexcept { analysis.setSpectrumEnabled(v); }
bool ChaosProcessor::readSpectrum(std::span<float> l, std::span<float> r) noexcept {
    return analysis.readSpectrum(l, r);
}
