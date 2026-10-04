#include "spaceLabProcessor.h"
#include "spaceLabEditor.h"
#include <cmath>
#include <limits>

namespace {
constexpr std::array<const char *, 10> parameterIds{
    "width",       "haas",    "leftTilt", "rightTilt", "invertLeft",
    "invertRight", "modRate", "modDepth", "split",     "mix"};
constexpr std::array<const char *, 4> presetNames{"Neutral stereo", "Wide Haas",
                                                  "Split moving mono", "Opposite colour"};
constexpr std::array<SpaceLabProcessor::Values, 4> factoryValues{
    {{1, 0, 0, 0, 0, 0, 0, 0, 0, 1},
     {2, 12, -3, 3, 0, 0, 0, 0, 0, 0.7F},
     {1, 0, 0, 0, 0, 0, 0.3F, 12, 1, 1},
     {1.5F, -8, -9, 9, 0, 1, 0.5F, 4, 0, 0.5F}}};
ahq::audio::SpaceLabParameters coreParameters(const SpaceLabProcessor::Values &v) noexcept {
    return {v[0], v[1], v[2], v[3], v[4] == 1, v[5] == 1, v[6], v[7], v[8] == 1, v[9]};
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout SpaceLabProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    constexpr std::array<const char *, 10> names{
        "Width",        "Signed Haas",     "Left tilt",        "Right tilt", "Invert left",
        "Invert right", "Modulation rate", "Modulation depth", "Split mono", "Mix"};
    constexpr std::array<float, 10> minimum{0, -30, -12, -12, 0, 0, 0, 0, 0, 0},
        maximum{3, 30, 12, 12, 1, 1, 10, 20, 1, 1};
    for (size_t i = 0; i < 10; ++i) {
        if (i == 4 || i == 5 || i == 8)
            layout.add(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID{parameterIds[i], 1}, names[i], false));
        else
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{parameterIds[i], 1}, names[i],
                juce::NormalisableRange<float>(minimum[i], maximum[i]), factoryValues[0][i]));
    }
    return layout;
}
SpaceLabProcessor::SpaceLabProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "SpaceLabParameters", createParameterLayout()),
      state(parameters, parameterIds, "AHQSpaceLabState", validateValues, currentProgram) {
    setLatencySamples(0);
}

const juce::String SpaceLabProcessor::getName() const { return "AHQ Space Lab"; }

void SpaceLabProcessor::prepareToPlay(double rate, int) {
    analysis.prepare(rate);
    prepared = filter.prepare(rate);
    if (prepared) {
        const auto v = readValues();
        prepared = validateValues(v) && filter.setParameters(coreParameters(v));
        filter.reset();
    }
}
void SpaceLabProcessor::releaseResources() { prepared = false; }
bool SpaceLabProcessor::isBusesLayoutSupported(const BusesLayout &layout) const {
    const auto input = layout.getMainInputChannelSet();
    return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() &&
           (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}
void SpaceLabProcessor::processBlock(juce::AudioBuffer<float> &b, juce::MidiBuffer &) {
    processAudio(b, false);
}
void SpaceLabProcessor::processBlockBypassed(juce::AudioBuffer<float> &b, juce::MidiBuffer &) {
    processAudio(b, true);
}
void SpaceLabProcessor::processAudio(juce::AudioBuffer<float> &buffer, bool bypassed) {
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() != 2)
        return;
    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());
    const float inputPeak = PluginAnalysis::peak(buffer);
    const auto v = readValues();
    if (prepared && validateValues(v)) {
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
// Conservative -80 dB estimate for 1 kHz one-pole EQ plus maximum creative delay and smoothing.
// IIR decay is asymptotic; 0.1 seconds is an estimate, not a strict finite impulse duration.
double SpaceLabProcessor::getTailLengthSeconds() const { return 0.1; }
bool SpaceLabProcessor::validateValues(std::span<const float> v) noexcept {
    if (v.size() != 10)
        return false;
    for (float value : v)
        if (!std::isfinite(value))
            return false;
    return v[0] >= 0 && v[0] <= 3 && v[1] >= -30 && v[1] <= 30 && v[2] >= -12 && v[2] <= 12 &&
           v[3] >= -12 && v[3] <= 12 && (v[4] == 0 || v[4] == 1) && (v[5] == 0 || v[5] == 1) &&
           v[6] >= 0 && v[6] <= 10 && v[7] >= 0 && v[7] <= 20 && (v[8] == 0 || v[8] == 1) &&
           v[9] >= 0 && v[9] <= 1;
}
int SpaceLabProcessor::getNumPrograms() { return static_cast<int>(factoryValues.size()); }
const juce::String SpaceLabProcessor::getProgramName(int index) {
    return juce::isPositiveAndBelow(index, getNumPrograms()) ? presetNames[size_t(index)] : "";
}
void SpaceLabProcessor::setCurrentProgram(int index) {
    if (juce::isPositiveAndBelow(index, getNumPrograms())) {
        state.writeValues(factoryValues[size_t(index)], false);
        currentProgram.store(index);
        state.invalidateHistory();
    }
}
bool SpaceLabProcessor::loadFactoryPreset(int index) {
    if (!juce::isPositiveAndBelow(index, getNumPrograms()))
        return false;
    return applyUserValues(factoryValues[size_t(index)], "Factory preset");
}

juce::AudioProcessorEditor *SpaceLabProcessor::createEditor() { return new SpaceLabEditor(*this); }
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new SpaceLabProcessor(); }

SpaceLabProcessor::Values SpaceLabProcessor::readValues() const noexcept {
    Values values{};
    state.fillValues(values);
    return values;
}
juce::String SpaceLabProcessor::createPresetXml() const { return state.createPresetXml(); }
bool SpaceLabProcessor::loadPresetXml(const juce::String &s, bool undoable) {
    return state.loadPresetXml(s, undoable);
}
bool SpaceLabProcessor::applyUserValues(const Values &v, const juce::String &n) {
    return state.applyUserValues(v, n);
}
void SpaceLabProcessor::recordUserEdit(const Values &v, int i, const juce::String &n) {
    state.recordUserEdit(v, i, n);
}
void SpaceLabProcessor::getStateInformation(juce::MemoryBlock &b) { state.getStateInformation(b); }
void SpaceLabProcessor::setStateInformation(const void *p, int n) {
    state.setStateInformation(p, n);
}
bool SpaceLabProcessor::undo() { return state.undo(); }
bool SpaceLabProcessor::redo() { return state.redo(); }

std::array<float, 2> SpaceLabProcessor::readLevels() noexcept { return analysis.readLevels(); }
void SpaceLabProcessor::setSpectrumEnabled(bool v) noexcept { analysis.setSpectrumEnabled(v); }
bool SpaceLabProcessor::readSpectrum(std::span<float> l, std::span<float> r) noexcept {
    return analysis.readSpectrum(l, r);
}
