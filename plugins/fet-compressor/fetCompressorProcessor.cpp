#include "fetCompressorProcessor.h"
#include "../common/parameterPanel.h"
#include <cmath>
juce::AudioProcessorValueTreeState::ParameterLayout
FetCompressorProcessor::createParameterLayout() {
    using P = ahq::audio::ClassicCompressorParameters;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[0], 1}, "Input (dB)", toJuceRange(P::inputRange), 0.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[1], 1}, "Output (dB)", toJuceRange(P::outputRange), 0.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[2], 1}, "Attack (ms)", toJuceRange(P::attackRange), 0.4F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[3], 1}, "Release (ms)", toJuceRange(P::releaseRange), 200.0F));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids[4], 1}, "Ratio",
        juce::StringArray{"4:1", "8:1", "12:1", "20:1", "All"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[5], 1}, "Mix",
                                                           toJuceRange(P::mixRange), 1.0F));
    return layout;
}
FetCompressorProcessor::FetCompressorProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "FetCompressorParameters", createParameterLayout()),
      state(parameters, ids, "AHQFetCompressorState", program) {}
void FetCompressorProcessor::prepareToPlay(double rate, int) {
    prepared = effect.prepare(rate);
    setLatencySamples(0);
}
bool FetCompressorProcessor::isBusesLayoutSupported(const BusesLayout &b) const {
    const auto main = b.getMainOutputChannelSet();
    return main == b.getMainInputChannelSet() &&
           (main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo());
}
void FetCompressorProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &) {
    juce::ScopedNoDenormals noDenormals;
    if (!prepared || buffer.getNumSamples() == 0)
        return;
    const auto v = readValues();
    if (!state.validateValues(v))
        return;
    ahq::audio::ClassicCompressorParameters p;
    p.inputDb = v[0];
    p.outputDb = v[1];
    p.attackMs = v[2];
    p.releaseMs = v[3];
    p.ratio = int(v[4]);
    p.mix = v[5];
    if (!effect.setParameters(p))
        return;

    const auto n = size_t(buffer.getNumSamples());
    (void)effect.process({buffer.getWritePointer(0), n},
                         buffer.getNumChannels() > 1
                             ? std::span<float>(buffer.getWritePointer(1), n)
                             : std::span<float>{});
    displayGainDb.store(float(-effect.reductionDb()), std::memory_order_relaxed);
}
juce::AudioProcessorEditor *FetCompressorProcessor::createEditor() {
    return new ParameterPanel<FetCompressorProcessor>(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new FetCompressorProcessor; }
