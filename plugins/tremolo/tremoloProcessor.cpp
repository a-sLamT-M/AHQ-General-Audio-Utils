#include "tremoloProcessor.h"
#include "../common/parameterPanel.h"
#include <cmath>
juce::AudioProcessorValueTreeState::ParameterLayout TremoloProcessor::createParameterLayout() {
    using P = ahq::audio::TremoloParameters;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids[0], 1}, "Clock", juce::StringArray{"Host BPM", "Free Hz"}, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids[1], 1}, "Shape",
        juce::StringArray{"Sine", "Triangle", "Square", "Rise"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[2], 1}, "Rate (Hz)", toJuceRange(P::rateRange), 0.5F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[3], 1}, "Length (beats)", toJuceRange(P::beatsRange), 4.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[4], 1}, "Depth",
                                                           toJuceRange(P::depthRange), 1.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[5], 1}, "Phase (degrees)", toJuceRange(P::phaseRange), 0.0F));
    return layout;
}
TremoloProcessor::TremoloProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "TremoloParameters", createParameterLayout()),
      state(parameters, ids, "AHQTremoloState", program) {}
void TremoloProcessor::prepareToPlay(double rate, int) {
    prepared = effect.prepare(rate);
    setLatencySamples(0);
}
bool TremoloProcessor::isBusesLayoutSupported(const BusesLayout &b) const {
    const auto main = b.getMainOutputChannelSet();
    return main == b.getMainInputChannelSet() &&
           (main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo());
}
void TremoloProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &) {
    juce::ScopedNoDenormals noDenormals;
    if (!prepared || buffer.getNumSamples() == 0)
        return;
    const auto v = readValues();
    if (!state.validateValues(v))
        return;
    ahq::audio::TremoloParameters p;
    p.sync = v[0] == 0;
    p.shape = int(v[1]);
    p.rateHz = v[2];
    p.beats = v[3];
    p.depth = v[4];
    p.phaseDegrees = v[5];
    if (!effect.setParameters(p))
        return;
    double bpm = 120, ppq = 0;
    bool positioned = false, playing = true;
    if (auto *head = getPlayHead())
        if (auto pos = head->getPosition()) {
            if (auto b = pos->getBpm())
                if (std::isfinite(*b) && *b >= 1 && *b <= 1000)
                    bpm = *b;
            if (auto t = pos->getPpqPosition())
                if (std::isfinite(*t)) {
                    ppq = *t;
                    positioned = true;
                }
            playing = pos->getIsPlaying();
        }
    const auto n = size_t(buffer.getNumSamples());
    (void)effect.process({buffer.getWritePointer(0), n},
                         buffer.getNumChannels() > 1
                             ? std::span<float>(buffer.getWritePointer(1), n)
                             : std::span<float>{},
                         bpm, ppq, positioned, playing);
    displayGainDb.store(float(20 * std::log10(std::max(1e-6, effect.gain()))),
                        std::memory_order_relaxed);
}
juce::AudioProcessorEditor *TremoloProcessor::createEditor() {
    return new ParameterPanel<TremoloProcessor>(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new TremoloProcessor; }
