#include "noiseGateProcessor.h"
#include "../common/parameterPanel.h"
#include <cmath>
juce::AudioProcessorValueTreeState::ParameterLayout NoiseGateProcessor::createParameterLayout() {
    using P = ahq::audio::NoiseGateParameters;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[0], 1}, "Threshold (dB)", toJuceRange(P::thresholdRange), -48.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[1], 1}, "Hysteresis (dB)", toJuceRange(P::hysteresisRange), 6.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[2], 1}, "Attack (ms)", toJuceRange(P::attackRange), 0.5F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[3], 1}, "Hold (ms)", toJuceRange(P::holdRange), 40.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[4], 1}, "Release (ms)", toJuceRange(P::releaseRange), 100.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[5], 1}, "Floor (dB)", toJuceRange(P::rangeRange), -96.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[6], 1}, "Mix",
                                                           toJuceRange(P::mixRange), 1.0F));
    return layout;
}
NoiseGateProcessor::NoiseGateProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "NoiseGateParameters", createParameterLayout()),
      state(parameters, ids, "AHQNoiseGateState", program) {}
void NoiseGateProcessor::prepareToPlay(double rate, int) {
    prepared = effect.prepare(rate);
    setLatencySamples(0);
}
bool NoiseGateProcessor::isBusesLayoutSupported(const BusesLayout &b) const {
    const auto main = b.getMainOutputChannelSet();
    return main == b.getMainInputChannelSet() &&
           (main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo());
}
void NoiseGateProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &) {
    juce::ScopedNoDenormals noDenormals;
    if (!prepared || buffer.getNumSamples() == 0)
        return;
    const auto v = readValues();
    if (!state.validateValues(v))
        return;
    ahq::audio::NoiseGateParameters p;
    p.thresholdDb = v[0];
    p.hysteresisDb = v[1];
    p.attackMs = v[2];
    p.holdMs = v[3];
    p.releaseMs = v[4];
    p.rangeDb = v[5];
    p.mix = v[6];
    if (!effect.setParameters(p))
        return;

    const auto n = size_t(buffer.getNumSamples());
    (void)effect.process({buffer.getWritePointer(0), n},
                         buffer.getNumChannels() > 1
                             ? std::span<float>(buffer.getWritePointer(1), n)
                             : std::span<float>{});
    displayGainDb.store(float(20 * std::log10(std::max(1e-6, effect.gain()))),
                        std::memory_order_relaxed);
}
juce::AudioProcessorEditor *NoiseGateProcessor::createEditor() {
    return new ParameterPanel<NoiseGateProcessor>(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new NoiseGateProcessor; }
