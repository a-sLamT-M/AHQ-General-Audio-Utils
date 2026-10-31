#include "volumeEnvelopeProcessor.h"
#include "volumeEnvelopeEditor.h"
#include <cmath>
juce::AudioProcessorValueTreeState::ParameterLayout
VolumeEnvelopeProcessor::createParameterLayout() {
    using P = ahq::audio::VolumeEnvelopeParameters;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids[0], 1}, "Trigger", juce::StringArray{"Host BPM", "Audio sidechain"},
        0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[1], 1}, "Length in beats", toJuceRange(P::beatsRange), 1.F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[2], 1}, "Trigger threshold", toJuceRange(P::thresholdRange), -24.F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[3], 1}, "Mix",
                                                           toJuceRange(P::mixRange), 1.F));
    const ahq::audio::VolumeEnvelopeParameters defaults;
    for (size_t i = 1; i < 7; ++i)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ids[i + 3], 1}, "Node " + juce::String(int(i)) + " time",
            toJuceRange(P::innerTimeRange), float(defaults.times[i])));
    for (size_t i = 0; i < 8; ++i)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ids[i + 10], 1}, "Node " + juce::String(int(i)) + " level",
            toJuceRange(P::levelRange), float(defaults.levels[i])));
    return layout;
}
VolumeEnvelopeProcessor::VolumeEnvelopeProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withInput("Trigger", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "VolumeEnvelopeParameters", createParameterLayout()),
      state(parameters, ids, "AHQVolumeEnvelopeState", program) {}
void VolumeEnvelopeProcessor::prepareToPlay(double rate, int) {
    prepared = envelope.prepare(rate);
    setLatencySamples(0);
}
bool VolumeEnvelopeProcessor::isBusesLayoutSupported(const BusesLayout &layout) const {
    const auto main = layout.getMainOutputChannelSet();
    const auto trigger = layout.getChannelSet(true, 1);
    return main == layout.getMainInputChannelSet() &&
           (main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo()) &&
           (trigger.isDisabled() || trigger == juce::AudioChannelSet::mono() ||
            trigger == juce::AudioChannelSet::stereo());
}
void VolumeEnvelopeProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &) {
    juce::ScopedNoDenormals noDenormals;
    if (!prepared || buffer.getNumSamples() == 0)
        return;
    const auto values = readValues();
    ahq::audio::VolumeEnvelopeParameters p;
    p.audioTrigger = values[0] != 0;
    p.beats = values[1];
    p.thresholdDb = values[2];
    p.mix = values[3];
    for (size_t i = 1; i < 7; ++i)
        p.times[i] =
            std::clamp(double(values[i + 3]), p.times[i - 1] + .001, 1 - double(7 - i) * .001);
    for (size_t i = 0; i < 8; ++i)
        p.levels[i] = values[i + 10];
    if (!envelope.setParameters(p))
        return;
    double bpm = 120, ppq = 0;
    bool positioned = false, playing = true;
    if (auto *playhead = getPlayHead())
        if (auto position = playhead->getPosition()) {
            if (auto tempo = position->getBpm())
                if (std::isfinite(*tempo) && *tempo >= 1 && *tempo <= 1000)
                    bpm = *tempo;
            if (auto beats = position->getPpqPosition())
                if (std::isfinite(*beats)) {
                    ppq = *beats;
                    positioned = true;
                }
            playing = position->getIsPlaying();
        }
    auto main = getBusBuffer(buffer, false, 0);
    auto trigger = getBusBuffer(buffer, true, 1);
    const auto n = size_t(buffer.getNumSamples());
    (void)envelope.process(
        {main.getWritePointer(0), n},
        main.getNumChannels() > 1 ? std::span<float>(main.getWritePointer(1), n)
                                  : std::span<float>{},
        trigger.getNumChannels() ? std::span<const float>(trigger.getReadPointer(0), n)
                                 : std::span<const float>{},
        trigger.getNumChannels() > 1 ? std::span<const float>(trigger.getReadPointer(1), n)
                                     : std::span<const float>{},
        bpm, ppq, positioned, playing);
    displayedPhase.store(float(envelope.phase()), std::memory_order_relaxed);
    displayedGain.store(float(envelope.gain()), std::memory_order_relaxed);
    displayedBpm.store(float(bpm), std::memory_order_relaxed);
}
const juce::String VolumeEnvelopeProcessor::getProgramName(int i) {
    const std::array<const char *, 4> names{"Pump", "Short kick", "Long kick", "Swell"};
    return names[size_t(std::clamp(i, 0, 3))];
}
void VolumeEnvelopeProcessor::setCurrentProgram(int i) {
    if (i < 0 || i >= 4)
        return;
    auto v = readValues();
    const ahq::audio::VolumeEnvelopeParameters defaults;
    for (size_t j = 1; j < 7; ++j)
        v[j + 3] = float(defaults.times[j]);
    const std::array<std::array<float, 8>, 4> levels{{{0, 0, .08F, .3F, .6F, .82F, .96F, 1},
                                                      {0, .1F, .75F, 1, 1, 1, 1, 1},
                                                      {0, 0, 0, .03F, .3F, .6F, .85F, 1},
                                                      {0, .08F, .2F, .36F, .52F, .7F, .86F, 1}}};
    for (size_t j = 0; j < 8; ++j)
        v[j + 10] = levels[size_t(i)][j];
    state.invalidateHistory();
    state.writeValues(v, false);
    program.store(i);
}
juce::AudioProcessorEditor *VolumeEnvelopeProcessor::createEditor() {
    return new VolumeEnvelopeEditor(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new VolumeEnvelopeProcessor; }
