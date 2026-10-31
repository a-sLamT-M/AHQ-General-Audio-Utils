#include "gatedReverbProcessor.h"
#include "../common/parameterPanel.h"
#include <cmath>
void GatedReverbProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &) {
    juce::ScopedNoDenormals noDenormals;
    if (!prepared || buffer.getNumSamples() == 0)
        return;
    const auto v = readValues();
    if (!state.validateValues(v))
        return;
    impulses.setGenerated(0, int(v[0]), int(v[1]), v[2] != 0);
    gains[0].setTargetValue(v[4]);
    delays[0].setTargetValue(v[3] * float(rate * .001));
    float peak = 0;
    auto &delay = delayBuffers[0];
    auto &position = delayPositions[0];
    for (int offset = 0; offset < buffer.getNumSamples(); offset += chunkSize) {
        const int n = std::min(chunkSize, buffer.getNumSamples() - offset);
        for (int i = 0; i < n; ++i) {
            double read = position - delays[0].getNextValue();
            if (read < 0)
                read += delay.getNumSamples();
            const int index = int(read), next = (index + 1) % delay.getNumSamples();
            const float fraction = float(read - index);
            for (int c = 0; c < channels; ++c) {
                delay.setSample(c, position, buffer.getSample(c, offset + i));
                scratch.setSample(c, i,
                                  delay.getSample(c, index) +
                                      fraction *
                                          (delay.getSample(c, next) - delay.getSample(c, index)));
            }
            if (++position == delay.getNumSamples())
                position = 0;
        }
        impulses.process(0, juce::dsp::AudioBlock<float>(scratch)
                                .getSubsetChannelBlock(0, size_t(channels))
                                .getSubBlock(0, size_t(n)));
        for (int i = 0; i < n; ++i) {
            const float mix = gains[0].getNextValue();
            for (int c = 0; c < channels; ++c) {
                const float sample =
                    buffer.getSample(c, offset + i) * (1 - mix) + scratch.getSample(c, i) * mix;
                buffer.setSample(c, offset + i, sample);
                peak = std::max(peak, std::abs(sample));
            }
        }
    }
    displayGainDb.store(juce::Decibels::gainToDecibels(peak, -96.F));
}
juce::AudioProcessorValueTreeState::ParameterLayout GatedReverbProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[0], 1}, "Length (ms)", juce::NormalisableRange<float>{50.0F, 1500.0F},
        400.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[1], 1}, "Brightness (Hz)",
        juce::NormalisableRange<float>{500.0F, 18000.0F}, 7000.0F));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids[2], 1}, "Tail", juce::StringArray{"Non-linear", "Reverse"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[3], 1}, "Pre-delay (ms)",
        juce::NormalisableRange<float>{0.0F, 250.0F}, 20.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[4], 1}, "Mix", juce::NormalisableRange<float>{0.0F, 1.0F}, 0.3F));
    return layout;
}
GatedReverbProcessor::GatedReverbProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "GatedReverbParameters", createParameterLayout()),
      state(parameters, ids, "AHQGatedReverbState", program) {}
bool GatedReverbProcessor::isBusesLayoutSupported(const BusesLayout &b) const {
    auto main = b.getMainOutputChannelSet();
    return main == b.getMainInputChannelSet() &&
           (main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo());
}
void GatedReverbProcessor::prepareToPlay(double sr, int blockSize) {
    rate = sr;
    channels = getTotalNumOutputChannels();
    const auto v = readValues();
    impulses.setGenerated(0, int(v[0]), int(v[1]), v[2] != 0);
    chunkSize = std::clamp(blockSize, 1, 2048);
    prepared = impulses.prepare(sr, unsigned(chunkSize), unsigned(channels));

    if (!prepared)
        return;
    for (auto &g : gains) {
        g.reset(sr, .005);
        g.setCurrentAndTargetValue(0);
    }
    for (auto &t : delays) {
        t.reset(sr, .02);
        t.setCurrentAndTargetValue(0);
    }
    for (auto &d : delayBuffers) {
        d.setSize(2, int(std::ceil(sr * .251)) + 2);
        d.clear();
    }
    gains[0].setCurrentAndTargetValue(v[4]);
    delays[0].setCurrentAndTargetValue(v[3] * float(sr * .001));
    delayPositions.fill(0);
    setLatencySamples(0);
}
juce::AudioProcessorEditor *GatedReverbProcessor::createEditor() {
    return new ParameterPanel<GatedReverbProcessor>(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new GatedReverbProcessor; }
