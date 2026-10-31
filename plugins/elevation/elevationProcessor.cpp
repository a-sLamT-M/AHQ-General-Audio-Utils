#include "elevationProcessor.h"
#include "elevationEditor.h"
#include <cmath>
juce::AudioProcessorValueTreeState::ParameterLayout ElevationProcessor::createParameterLayout() {
    using P = ahq::audio::ElevationParameters;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[0], 1}, "Elevation (degrees)", toJuceRange(P::elevationRange), 0.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[1], 1}, "Output (dB)", toJuceRange(P::outputRange), 0.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[2], 1}, "Mix",
                                                           toJuceRange(P::mixRange), 1.0F));
    return layout;
}
ElevationProcessor::ElevationProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "ElevationParameters", createParameterLayout()),
      state(parameters, ids, "AHQElevationState", program) {}
void ElevationProcessor::prepareToPlay(double rate, int) {
    prepared = effect.prepare(rate);
    setLatencySamples(int(effect.latencySamples()));
}
bool ElevationProcessor::isBusesLayoutSupported(const BusesLayout &b) const {
    const auto main = b.getMainOutputChannelSet();
    return main == juce::AudioChannelSet::stereo() &&
           (b.getMainInputChannelSet() == juce::AudioChannelSet::mono() ||
            b.getMainInputChannelSet() == juce::AudioChannelSet::stereo());
}
void ElevationProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &) {
    juce::ScopedNoDenormals noDenormals;
    if (!prepared || buffer.getNumSamples() == 0)
        return;
    const auto v = readValues();
    if (!state.validateValues(v))
        return;
    ahq::audio::ElevationParameters p;
    p.elevationDegrees = v[0];
    p.outputDb = v[1];
    p.mix = v[2];
    if (!effect.setParameters(p))
        return;

    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());
    const auto n = size_t(buffer.getNumSamples());
    (void)effect.process({buffer.getWritePointer(0), n},
                         buffer.getNumChannels() > 1
                             ? std::span<float>(buffer.getWritePointer(1), n)
                             : std::span<float>{});
    displayGainDb.store(v[1], std::memory_order_relaxed);
}
juce::AudioProcessorEditor *ElevationProcessor::createEditor() {
    return new ElevationEditor(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new ElevationProcessor; }
