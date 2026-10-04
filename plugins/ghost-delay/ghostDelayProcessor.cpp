#include "ghostDelayProcessor.h"
#include "ghostDelayEditor.h"
#include <cmath>
#include <limits>

namespace {
constexpr std::array<const char *, 9> parameterIds{
    "grain", "delay", "pitch", "scatter", "density", "feedback", "randomness", "seed", "mix"};
constexpr std::array<const char *, 4> presetNames{"Default ghost", "Low ghost echoes",
                                                  "Bright scattered grains", "Long sparse ghosts"};
constexpr std::array<GhostDelayProcessor::Values, 4> factoryValues{
    {{100, 250, 0, 0.25F, 20, 0.15F, 0.2F, 1, 0.5F},
     {180, 650, -12, 0.7F, 12, 0.35F, 0.5F, 42, 0.6F},
     {80, 350, 12, 0.8F, 30, 0.25F, 0.5F, 17, 0.45F},
     {300, 1200, -7, 0.5F, 5, 0.45F, 0.35F, 123, 0.65F}}};
ahq::audio::GrainDelayParameters coreParameters(const GhostDelayProcessor::Values &v) noexcept {
    return {v[0], v[1], v[2], v[3], v[4], v[5], v[6], std::uint32_t(v[7]), v[8]};
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout GhostDelayProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(
        std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"grain", 1}, "Grain length",
                                                    juce::NormalisableRange<float>(20, 500), 100));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"delay", 1}, "Delay", juce::NormalisableRange<float>(10, 2000), 250));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"pitch", 1}, "Pitch", juce::NormalisableRange<float>(-24, 24), 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"scatter", 1}, "Scatter", juce::NormalisableRange<float>(0, 1), 0.25F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"density", 1}, "Density", juce::NormalisableRange<float>(1, 80), 20));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"feedback", 1}, "Feedback", juce::NormalisableRange<float>(0, 0.95F),
        0.15F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"randomness", 1}, "Randomness", juce::NormalisableRange<float>(0, 1),
        0.2F));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"seed", 1}, "Seed", 0,
                                                         65535, 1));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mix", 1}, "Mix", juce::NormalisableRange<float>(0, 1), 0.5F));
    return layout;
}
GhostDelayProcessor::GhostDelayProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "GhostDelayParameters", createParameterLayout()),
      state(parameters, parameterIds, "AHQGhostDelayState", validateValues, currentProgram) {
    setLatencySamples(0);
}

const juce::String GhostDelayProcessor::getName() const { return "AHQ Ghost Delay"; }

void GhostDelayProcessor::prepareToPlay(double rate, int) {
    analysis.prepare(rate);
    prepared = filter.prepare(rate);
    if (prepared) {
        const auto v = readValues();
        prepared = validateValues(v) && filter.setParameters(coreParameters(v));
        filter.reset();
    }
}
void GhostDelayProcessor::releaseResources() { prepared = false; }
bool GhostDelayProcessor::isBusesLayoutSupported(const BusesLayout &layout) const {
    const auto output = layout.getMainOutputChannelSet();
    return output == layout.getMainInputChannelSet() &&
           (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}
void GhostDelayProcessor::processBlock(juce::AudioBuffer<float> &b, juce::MidiBuffer &) {
    processAudio(b, false);
}
void GhostDelayProcessor::processBlockBypassed(juce::AudioBuffer<float> &b, juce::MidiBuffer &) {
    processAudio(b, true);
}
void GhostDelayProcessor::processAudio(juce::AudioBuffer<float> &buffer, bool bypassed) {
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() < 1 || buffer.getNumChannels() > 2)
        return;
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
// Conservative decay estimate, not a measured or strictly finite tail: the core's
// attenuation-only pan and normalized overlap bound loop gain by feedback. Allow 4.5 seconds
// per history/grain generation plus one initial generation; estimate decay below -80 dB.
// The extra factor of two covers normal input plus the bounded feedback contribution.
double GhostDelayProcessor::getTailLengthSeconds() const {
    const auto v = readValues();
    if (!validateValues(v))
        return std::numeric_limits<double>::infinity();
    const double feedback = v[5];
    if (feedback <= 0)
        return 4.5;
    return 4.5 * (1 + std::ceil(std::log(0.0001 / 2.0) / std::log(feedback))) + 0.005;
}
bool GhostDelayProcessor::validateValues(std::span<const float> v) noexcept {
    if (v.size() != 9)
        return false;
    for (float value : v)
        if (!std::isfinite(value))
            return false;
    return v[0] >= 20 && v[0] <= 500 && v[1] >= 10 && v[1] <= 2000 && v[2] >= -24 && v[2] <= 24 &&
           v[3] >= 0 && v[3] <= 1 && v[4] >= 1 && v[4] <= 80 && v[5] >= 0 && v[5] <= 0.95F &&
           v[6] >= 0 && v[6] <= 1 && v[7] >= 0 && v[7] <= 65535 && v[7] == std::floor(v[7]) &&
           v[8] >= 0 && v[8] <= 1;
}
int GhostDelayProcessor::getNumPrograms() { return static_cast<int>(factoryValues.size()); }
const juce::String GhostDelayProcessor::getProgramName(int index) {
    return juce::isPositiveAndBelow(index, getNumPrograms()) ? presetNames[size_t(index)] : "";
}
void GhostDelayProcessor::setCurrentProgram(int index) {
    if (juce::isPositiveAndBelow(index, getNumPrograms())) {
        state.writeValues(factoryValues[size_t(index)], false);
        currentProgram.store(index);
        state.invalidateHistory();
    }
}
bool GhostDelayProcessor::loadFactoryPreset(int index) {
    if (!juce::isPositiveAndBelow(index, getNumPrograms()))
        return false;
    return applyUserValues(factoryValues[size_t(index)], "Factory preset");
}

juce::AudioProcessorEditor *GhostDelayProcessor::createEditor() {
    return new GhostDelayEditor(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new GhostDelayProcessor(); }

GhostDelayProcessor::Values GhostDelayProcessor::readValues() const noexcept {
    Values values{};
    state.fillValues(values);
    return values;
}
juce::String GhostDelayProcessor::createPresetXml() const { return state.createPresetXml(); }
bool GhostDelayProcessor::loadPresetXml(const juce::String &s, bool undoable) {
    return state.loadPresetXml(s, undoable);
}
bool GhostDelayProcessor::applyUserValues(const Values &v, const juce::String &n) {
    return state.applyUserValues(v, n);
}
void GhostDelayProcessor::recordUserEdit(const Values &v, int i, const juce::String &n) {
    state.recordUserEdit(v, i, n);
}
void GhostDelayProcessor::getStateInformation(juce::MemoryBlock &b) {
    state.getStateInformation(b);
}
void GhostDelayProcessor::setStateInformation(const void *p, int n) {
    state.setStateInformation(p, n);
}
bool GhostDelayProcessor::undo() { return state.undo(); }
bool GhostDelayProcessor::redo() { return state.redo(); }

std::array<float, 2> GhostDelayProcessor::readLevels() noexcept { return analysis.readLevels(); }
void GhostDelayProcessor::setSpectrumEnabled(bool v) noexcept { analysis.setSpectrumEnabled(v); }
bool GhostDelayProcessor::readSpectrum(std::span<float> l, std::span<float> r) noexcept {
    return analysis.readSpectrum(l, r);
}
