#include "cabinetProcessor.h"
#include "../common/parameterPanel.h"
#include <charconv>
#include <cmath>
#include <limits>
void CabinetProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &) {
    juce::ScopedNoDenormals noDenormals;
    if (!prepared || buffer.getNumSamples() == 0)
        return;
    const auto v = readValues();
    if (!state.validateValues(v))
        return;
    masterGain.setTargetValue(juce::Decibels::decibelsToGain(v[masterOutputIndex]));
    bool anyLoaded = false;
    for (int slot = 0; slot < 4; ++slot) {
        impulses.poll(slot);
        anyLoaded |= impulses.loaded(slot);
        const size_t base = size_t(slot * 5);
        const float volume = v[base] != 0 ? juce::Decibels::decibelsToGain(v[base + 1]) *
                                                (v[base + 3] == 0 ? 1.F : -1.F)
                                          : 0.F;
        gains[size_t(slot * 2)].setTargetValue(volume * (v[base + 2] > 0 ? 1 - v[base + 2] : 1));
        gains[size_t(slot * 2 + 1)].setTargetValue(volume *
                                                   (v[base + 2] < 0 ? 1 + v[base + 2] : 1));
        delays[size_t(slot)].setTargetValue(v[base + 4] * float(rate * .001));
    }
    if (!anyLoaded) {
        applyMasterVolume(buffer);
        return;
    }
    for (int offset = 0; offset < buffer.getNumSamples(); offset += chunkSize) {
        const int n = std::min(chunkSize, buffer.getNumSamples() - offset);
        for (int c = 0; c < channels; ++c) {
            tail.copyFrom(c, 0, buffer, c, offset, n);
            buffer.clear(c, offset, n);
        }
        for (int slot = 0; slot < 4; ++slot) {
            auto &leftGain = gains[size_t(slot * 2)], &rightGain = gains[size_t(slot * 2 + 1)];
            if (!impulses.loaded(slot))
                continue;
            if (!leftGain.isSmoothing() && !rightGain.isSmoothing() &&
                leftGain.getTargetValue() == 0 && rightGain.getTargetValue() == 0) {
                if (!suspended[size_t(slot)]) {
                    impulses.resetSlot(slot);
                    delayBuffers[size_t(slot)].clear();
                    delayPositions[size_t(slot)] = 0;
                    suspended[size_t(slot)] = true;
                }
                continue;
            }
            suspended[size_t(slot)] = false;
            for (int c = 0; c < channels; ++c)
                scratch.copyFrom(c, 0, tail, c, 0, n);
            auto block = juce::dsp::AudioBlock<float>(scratch)
                             .getSubsetChannelBlock(0, size_t(channels))
                             .getSubBlock(0, size_t(n));
            impulses.process(slot, block);
            auto &delay = delayBuffers[size_t(slot)];
            auto &position = delayPositions[size_t(slot)];
            for (int i = 0; i < n; ++i) {
                double read = position - delays[size_t(slot)].getNextValue();
                if (read < 0)
                    read += delay.getNumSamples();
                const int index = int(read), next = (index + 1) % delay.getNumSamples();
                const float fraction = float(read - index);
                const float gainL = leftGain.getNextValue(), gainR = rightGain.getNextValue();
                for (int c = 0; c < channels; ++c) {
                    delay.setSample(c, position, scratch.getSample(c, i));
                    const float sample =
                        delay.getSample(c, index) +
                        fraction * (delay.getSample(c, next) - delay.getSample(c, index));
                    buffer.addSample(c, offset + i,
                                     sample * (channels == 1 ? (gainL + gainR) * .5F
                                               : c == 0      ? gainL
                                                             : gainR));
                }
                if (++position == delay.getNumSamples())
                    position = 0;
            }
        }
    }
    applyMasterVolume(buffer);
}
void CabinetProcessor::applyMasterVolume(juce::AudioBuffer<float> &buffer) noexcept {
    if (!masterGain.isSmoothing()) {
        const float gain = masterGain.getCurrentValue();
        if (gain != 1.F)
            buffer.applyGain(gain);
    } else {
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const float gain = masterGain.getNextValue();
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.setSample(c, i, buffer.getSample(c, i) * gain);
        }
    }
    float peak = 0;
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        peak = std::max(peak, buffer.getMagnitude(c, 0, buffer.getNumSamples()));
    displayGainDb.store(juce::Decibels::gainToDecibels(peak, -96.F));
}
juce::AudioProcessorValueTreeState::ParameterLayout CabinetProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids[0], 1}, "IR 1 Enable", juce::StringArray{"Off", "On"}, 1));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[1], 1}, "IR 1 Volume (dB)",
        juce::NormalisableRange<float>{-60.0F, 12.0F}, 0.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[2], 1}, "IR 1 Pan", juce::NormalisableRange<float>{-1.0F, 1.0F},
        0.0F));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids[3], 1}, "IR 1 Polarity", juce::StringArray{"Normal", "Invert"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[4], 1}, "IR 1 Delay (ms)",
        juce::NormalisableRange<float>{0.0F, 20.0F}, 0.0F));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids[5], 1}, "IR 2 Enable", juce::StringArray{"Off", "On"}, 1));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[6], 1}, "IR 2 Volume (dB)",
        juce::NormalisableRange<float>{-60.0F, 12.0F}, 0.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[7], 1}, "IR 2 Pan", juce::NormalisableRange<float>{-1.0F, 1.0F},
        0.0F));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids[8], 1}, "IR 2 Polarity", juce::StringArray{"Normal", "Invert"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[9], 1}, "IR 2 Delay (ms)",
        juce::NormalisableRange<float>{0.0F, 20.0F}, 0.0F));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids[10], 1}, "IR 3 Enable", juce::StringArray{"Off", "On"}, 1));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[11], 1}, "IR 3 Volume (dB)",
        juce::NormalisableRange<float>{-60.0F, 12.0F}, 0.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[12], 1}, "IR 3 Pan", juce::NormalisableRange<float>{-1.0F, 1.0F},
        0.0F));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids[13], 1}, "IR 3 Polarity", juce::StringArray{"Normal", "Invert"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[14], 1}, "IR 3 Delay (ms)",
        juce::NormalisableRange<float>{0.0F, 20.0F}, 0.0F));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids[15], 1}, "IR 4 Enable", juce::StringArray{"Off", "On"}, 1));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[16], 1}, "IR 4 Volume (dB)",
        juce::NormalisableRange<float>{-60.0F, 12.0F}, 0.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[17], 1}, "IR 4 Pan", juce::NormalisableRange<float>{-1.0F, 1.0F},
        0.0F));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids[18], 1}, "IR 4 Polarity", juce::StringArray{"Normal", "Invert"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[19], 1}, "IR 4 Delay (ms)",
        juce::NormalisableRange<float>{0.0F, 20.0F}, 0.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[masterOutputIndex], 1}, "Master (dB)",
        juce::NormalisableRange<float>{-60.0F, 12.0F}, 0.0F));
    return layout;
}
CabinetProcessor::CabinetProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "CabinetParameters", createParameterLayout()),
      state(parameters, ids, "AHQCabinetState", program) {}
bool CabinetProcessor::isBusesLayoutSupported(const BusesLayout &b) const {
    auto main = b.getMainOutputChannelSet();
    return main == b.getMainInputChannelSet() &&
           (main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo());
}
void CabinetProcessor::prepareToPlay(double sr, int blockSize) {
    rate = sr;
    channels = getTotalNumOutputChannels();
    const auto v = readValues();
    masterGain.reset(sr, .005);
    masterGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(v[masterOutputIndex]));

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
        d.setSize(2, int(std::ceil(sr * .021)) + 2);
        d.clear();
    }
    for (int slot = 0; slot < 4; ++slot) {
        const size_t b = size_t(slot * 5);
        const float level =
            v[b] != 0 ? juce::Decibels::decibelsToGain(v[b + 1]) * (v[b + 3] == 0 ? 1.F : -1.F)
                      : 0.F;
        gains[size_t(slot * 2)].setCurrentAndTargetValue(level * (v[b + 2] > 0 ? 1 - v[b + 2] : 1));
        gains[size_t(slot * 2 + 1)].setCurrentAndTargetValue(level *
                                                             (v[b + 2] < 0 ? 1 + v[b + 2] : 1));
        delays[size_t(slot)].setCurrentAndTargetValue(v[b + 4] * float(sr * .001));
    }
    delayPositions.fill(0);
    setLatencySamples(0);
}
juce::String CabinetProcessor::createPresetXml() {
    auto xml = juce::parseXML(state.createPresetXml());
    xml->addChildElement(new juce::XmlElement(impulses.save()));
    return xml->toString();
}
bool CabinetProcessor::restorePresetXml(const juce::String &text, bool notifyHost) {
    if (text.getNumBytesAsUTF8() > 128 * 1024 * 1024)
        return false;
    auto xml = juce::parseXML(text);
    if (!xml || !xml->hasTagName("AHQCabinetState") || xml->getStringAttribute("version") != "1" ||
        xml->getNumChildElements() != 1)
        return false;
    auto *ir = xml->getChildByName("ImpulseResponses");
    if (!ir)
        return false;
    // Original 20-parameter sessions predate the appended master control.
    if (!xml->hasAttribute("master_volume") && xml->getNumAttributes() == 21)
        xml->setAttribute("master_volume", 0.0);
    if (xml->getNumAttributes() != int(ids.size() + 1))
        return false;
    Values values{};
    for (size_t i = 0; i < ids.size(); ++i) {
        const auto value = xml->getStringAttribute(ids[i]).toStdString();
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), values[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
            return false;
    }
    if (!state.validateValues(values) || !impulses.restore(*ir, true))
        return false;
    state.writeValues(values, notifyHost);
    return true;
}
void CabinetProcessor::getStateInformation(juce::MemoryBlock &out) {
    if (auto xml = juce::parseXML(createPresetXml()))
        copyXmlToBinary(*xml, out);
}
void CabinetProcessor::setStateInformation(const void *data, int size) {
    if (!data || size <= 0 || size > 128 * 1024 * 1024)
        return;
    if (auto xml = getXmlFromBinary(data, size); xml && restorePresetXml(xml->toString(), false))
        state.invalidateHistory();
}
bool CabinetProcessor::loadPresetXml(const juce::String &xml) {
    return state.applyUserPreset(createPresetXml(), xml, [this](const juce::String &text) {
        return restorePresetXml(text, true);
    });
}
bool CabinetProcessor::savePresetFile(const juce::File &file) {
    juce::TemporaryFile temporary(file);
    return temporary.getFile().replaceWithText(createPresetXml()) &&
           temporary.overwriteTargetFileWithTemporary();
}
bool CabinetProcessor::loadPresetFile(const juce::File &file) {
    if (file.getSize() <= 0 || file.getSize() > 128 * 1024 * 1024)
        return false;
    return loadPresetXml(file.loadFileAsString());
}
const juce::String CabinetProcessor::getProgramName(int index) {
    constexpr const char *names[]{"Default: four parallel", "Single IR 1", "Dual blend 1 + 2",
                                  "Stereo pair 1 / 2", "Four-way blend"};
    return juce::isPositiveAndBelow(index, int(std::size(names))) ? names[index] : "";
}
CabinetProcessor::Values CabinetProcessor::routingValues(int index) const noexcept {
    auto values = readValues();
    for (size_t slot = 0; slot < 4; ++slot) {
        const size_t b = slot * 5;
        values[b] = index == 1                 ? (slot == 0 ? 1.F : 0.F)
                    : index == 2 || index == 3 ? (slot < 2 ? 1.F : 0.F)
                                               : 1.F;
        values[b + 1] = index == 2 ? -6.0206F : index == 4 ? -12.0412F : 0.F;
        values[b + 2] = index == 3 && slot < 2 ? (slot == 0 ? -1.F : 1.F) : 0.F;
        values[b + 3] = values[b + 4] = 0;
    }
    return values;
}
void CabinetProcessor::setCurrentProgram(int index) {
    if (!juce::isPositiveAndBelow(index, getNumPrograms()))
        return;
    state.writeValues(routingValues(index), false);
    state.invalidateHistory();
    program.store(index);
}
void CabinetProcessor::loadFactoryPreset(int index) {
    if (!juce::isPositiveAndBelow(index, getNumPrograms()))
        return;
    if (state.applyUserValues(routingValues(index), "Routing preset"))
        program.store(index);
}
juce::AudioProcessorEditor *CabinetProcessor::createEditor() {
    return new ParameterPanel<CabinetProcessor>(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new CabinetProcessor; }
