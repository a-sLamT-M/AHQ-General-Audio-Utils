#include "hybridReverbProcessor.h"
#include "../common/parameterPanel.h"
#include <cmath>
void HybridReverbProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &) {
    juce::ScopedNoDenormals noDenormals;
    if (!prepared || buffer.getNumSamples() == 0)
        return;
    const auto v = readValues();
    if (!state.validateValues(v))
        return;
    ahq::audio::ReverbParameters p;
    p.decaySeconds = v[2];
    p.dampingHz = v[3];
    p.width = v[5];
    p.preDelayMs = 0;
    p.mix = 1;
    if (!reverb.setParameters(p))
        return;
    gains[0].setTargetValue(juce::Decibels::decibelsToGain(v[0]));
    gains[1].setTargetValue(juce::Decibels::decibelsToGain(v[1]));
    gains[2].setTargetValue(v[6]);
    delays[0].setTargetValue(v[4] * float(rate * .001));
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
                const float sample =
                    delay.getSample(c, index) +
                    fraction * (delay.getSample(c, next) - delay.getSample(c, index));
                scratch.setSample(c, i, sample);
                tail.setSample(c, i, sample);
            }
            if (++position == delay.getNumSamples())
                position = 0;
        }
        impulses.process(0, juce::dsp::AudioBlock<float>(scratch)
                                .getSubsetChannelBlock(0, size_t(channels))
                                .getSubBlock(0, size_t(n)));
        (void)reverb.process({tail.getWritePointer(0), size_t(n)},
                             channels > 1 ? std::span<float>(tail.getWritePointer(1), size_t(n))
                                          : std::span<float>{});
        for (int i = 0; i < n; ++i) {
            const float early = gains[0].getNextValue(), late = gains[1].getNextValue(),
                        mix = gains[2].getNextValue();
            for (int c = 0; c < channels; ++c) {
                const float sample =
                    buffer.getSample(c, offset + i) * (1 - mix) +
                    mix * (scratch.getSample(c, i) * early + tail.getSample(c, i) * late);
                buffer.setSample(c, offset + i, sample);
                peak = std::max(peak, std::abs(sample));
            }
        }
    }
    displayGainDb.store(juce::Decibels::gainToDecibels(peak, -96.F));
}
juce::AudioProcessorValueTreeState::ParameterLayout HybridReverbProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[0], 1}, "Early (dB)", juce::NormalisableRange<float>{-60.0F, 6.0F},
        -6.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[1], 1}, "Late (dB)", juce::NormalisableRange<float>{-60.0F, 6.0F},
        -3.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[2], 1}, "Decay (s)", juce::NormalisableRange<float>{0.1F, 20.0F},
        2.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[3], 1}, "Damping (Hz)",
        juce::NormalisableRange<float>{500.0F, 18000.0F}, 6000.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[4], 1}, "Pre-delay (ms)",
        juce::NormalisableRange<float>{0.0F, 250.0F}, 20.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[5], 1}, "Width", juce::NormalisableRange<float>{0.0F, 1.0F}, 1.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[6], 1}, "Mix", juce::NormalisableRange<float>{0.0F, 1.0F}, 0.25F));
    return layout;
}
HybridReverbProcessor::HybridReverbProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "HybridReverbParameters", createParameterLayout()),
      state(parameters, ids, "AHQHybridReverbState", program) {
    impulses.setGenerated(0, 120, 9000, false);
}
bool HybridReverbProcessor::isBusesLayoutSupported(const BusesLayout &b) const {
    auto main = b.getMainOutputChannelSet();
    return main == b.getMainInputChannelSet() &&
           (main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo());
}
void HybridReverbProcessor::prepareToPlay(double sr, int blockSize) {
    rate = sr;
    channels = getTotalNumOutputChannels();
    const auto v = readValues();

    chunkSize = std::clamp(blockSize, 1, 2048);
    prepared = impulses.prepare(sr, unsigned(chunkSize), unsigned(channels));
    prepared = reverb.prepare(sr) && prepared;
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
    gains[0].setCurrentAndTargetValue(juce::Decibels::decibelsToGain(v[0]));
    gains[1].setCurrentAndTargetValue(juce::Decibels::decibelsToGain(v[1]));
    gains[2].setCurrentAndTargetValue(v[6]);
    delays[0].setCurrentAndTargetValue(v[4] * float(sr * .001));
    ahq::audio::ReverbParameters p;
    p.decaySeconds = v[2];
    p.dampingHz = v[3];
    p.width = v[5];
    p.mix = 1;
    p.preDelayMs = 0;
    prepared = reverb.setParameters(p) && prepared;
    reverb.reset();
    delayPositions.fill(0);
    setLatencySamples(0);
}
juce::String HybridReverbProcessor::createPresetXml() {
    auto xml = juce::parseXML(state.createPresetXml());
    xml->addChildElement(new juce::XmlElement(impulses.save()));
    return xml->toString();
}
bool HybridReverbProcessor::restorePresetXml(const juce::String &text, bool notifyHost) {
    if (text.getNumBytesAsUTF8() > 128 * 1024 * 1024)
        return false;
    auto xml = juce::parseXML(text);
    if (!xml || xml->getNumChildElements() != 1)
        return false;
    auto *ir = xml->getChildByName("ImpulseResponses");
    if (!ir)
        return false;
    auto irCopy = std::make_unique<juce::XmlElement>(*ir);
    xml->removeChildElement(ir, true);
    PluginState::Values values;
    if (!state.decodePresetXml(xml->toString(), values) || !impulses.restore(*irCopy, true))
        return false;
    state.writeValues(values, notifyHost);
    return true;
}
bool HybridReverbProcessor::loadPresetXml(const juce::String &xml) {
    return state.applyUserPreset(createPresetXml(), xml, [this](const juce::String &text) {
        return restorePresetXml(text, true);
    });
}
void HybridReverbProcessor::getStateInformation(juce::MemoryBlock &out) {
    if (auto xml = juce::parseXML(createPresetXml()))
        copyXmlToBinary(*xml, out);
}
void HybridReverbProcessor::setStateInformation(const void *data, int size) {
    if (!data || size <= 0 || size > 128 * 1024 * 1024)
        return;
    if (auto xml = getXmlFromBinary(data, size); xml && restorePresetXml(xml->toString(), false))
        state.invalidateHistory();
}
juce::AudioProcessorEditor *HybridReverbProcessor::createEditor() {
    return new ParameterPanel<HybridReverbProcessor>(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new HybridReverbProcessor; }
