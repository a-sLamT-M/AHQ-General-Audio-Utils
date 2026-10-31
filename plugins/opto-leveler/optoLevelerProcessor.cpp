#include "optoLevelerProcessor.h"
#include "../common/parameterPanel.h"
#include <cmath>
juce::AudioProcessorValueTreeState::ParameterLayout OptoLevelerProcessor::createParameterLayout() {
    using P = ahq::audio::ClassicCompressorParameters;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[0], 1}, "Peak reduction", toJuceRange(P::reductionRange), 40.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[1], 1}, "Output (dB)", toJuceRange(P::outputRange), 0.0F));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids[2], 1}, "Mode", juce::StringArray{"Compress", "Limit"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[3], 1}, "Input (dB)", toJuceRange(P::inputRange), 0.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[4], 1}, "Mix",
                                                           toJuceRange(P::mixRange), 1.0F));
    return layout;
}
OptoLevelerProcessor::OptoLevelerProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "OptoLevelerParameters", createParameterLayout()),
      state(parameters, ids, "AHQOptoLevelerState", program) {}
void OptoLevelerProcessor::prepareToPlay(double rate, int) {
    prepared = effect.prepare(rate);
    setLatencySamples(0);
}
bool OptoLevelerProcessor::isBusesLayoutSupported(const BusesLayout &b) const {
    const auto main = b.getMainOutputChannelSet();
    return main == b.getMainInputChannelSet() &&
           (main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo());
}
void OptoLevelerProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &) {
    juce::ScopedNoDenormals noDenormals;
    if (!prepared || buffer.getNumSamples() == 0)
        return;
    const auto v = readValues();
    if (!state.validateValues(v))
        return;
    ahq::audio::ClassicCompressorParameters p;
    p.peakReduction = v[0];
    p.outputDb = v[1];
    p.limit = v[2] != 0;
    p.inputDb = v[3];
    p.mix = v[4];
    if (!effect.setParameters(p))
        return;

    const auto n = size_t(buffer.getNumSamples());
    (void)effect.process({buffer.getWritePointer(0), n},
                         buffer.getNumChannels() > 1
                             ? std::span<float>(buffer.getWritePointer(1), n)
                             : std::span<float>{});
    displayGainDb.store(float(-effect.reductionDb()), std::memory_order_relaxed);
}
juce::AudioProcessorEditor *OptoLevelerProcessor::createEditor() {
    return new ParameterPanel<OptoLevelerProcessor>(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new OptoLevelerProcessor; }
