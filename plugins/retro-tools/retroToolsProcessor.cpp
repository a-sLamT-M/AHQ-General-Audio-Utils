#include "retroToolsProcessor.h"
#include "../common/parameterPanel.h"
#include <cmath>
juce::AudioProcessorValueTreeState::ParameterLayout RetroToolsProcessor::createParameterLayout() {
    using P = ahq::audio::RetroToolsParameters;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[0], 1}, "Ensemble",
                                                           toJuceRange(P::ensembleRange), 0.5F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[1], 1}, "Rate (Hz)", toJuceRange(P::rateRange), 0.4F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[2], 1}, "Wow",
                                                           toJuceRange(P::wowRange), 0.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[3], 1}, "Flutter",
                                                           toJuceRange(P::flutterRange), 0.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[4], 1}, "Drive",
                                                           toJuceRange(P::driveRange), 0.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ids[5], 1}, "Tone (Hz)", toJuceRange(P::toneRange), 12000.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[6], 1}, "Bits",
                                                           toJuceRange(P::bitsRange), 24.0F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[7], 1}, "Mix",
                                                           toJuceRange(P::mixRange), 0.5F));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[8], 1}, "Fault",
                                                           toJuceRange(P::faultRange), 0.0F));
    return layout;
}
RetroToolsProcessor::RetroToolsProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "RetroToolsParameters", createParameterLayout()),
      state(parameters, ids, "AHQRetroToolsState", program) {}
bool RetroToolsProcessor::restorePresetXml(const juce::String &text, bool undoable) {
    if (text.getNumBytesAsUTF8() > 65536)
        return false;
    auto xml = juce::parseXML(text);
    if (!xml || !xml->hasTagName("AHQRetroToolsState") || xml->getStringAttribute("version") != "1")
        return false;
    // Original eight-parameter projects and presets retain their sound even if Fault was on.
    if (!xml->hasAttribute("fault") && xml->getNumAttributes() == 9)
        xml->setAttribute("fault", 0.0);
    return state.loadPresetXml(xml->toString(), undoable);
}
void RetroToolsProcessor::setStateInformation(const void *data, int size) {
    if (data == nullptr || size <= 0 || size > 65536)
        return;
    if (auto xml = getXmlFromBinary(data, size))
        (void)restorePresetXml(xml->toString(), false);
}
void RetroToolsProcessor::prepareToPlay(double rate, int) {
    prepared = effect.prepare(rate);
    setLatencySamples(int(effect.latencySamples()));
}
bool RetroToolsProcessor::isBusesLayoutSupported(const BusesLayout &b) const {
    const auto main = b.getMainOutputChannelSet();
    return main == b.getMainInputChannelSet() &&
           (main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo());
}
void RetroToolsProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &) {
    juce::ScopedNoDenormals noDenormals;
    if (!prepared || buffer.getNumSamples() == 0)
        return;
    const auto v = readValues();
    if (!state.validateValues(v))
        return;
    ahq::audio::RetroToolsParameters p;
    p.ensemble = v[0];
    p.rateHz = v[1];
    p.wow = v[2];
    p.flutter = v[3];
    p.drive = v[4];
    p.toneHz = v[5];
    p.bits = int(v[6]);
    p.mix = v[7];
    p.fault = v[8];
    if (!effect.setParameters(p))
        return;

    const auto n = size_t(buffer.getNumSamples());
    (void)effect.process({buffer.getWritePointer(0), n},
                         buffer.getNumChannels() > 1
                             ? std::span<float>(buffer.getWritePointer(1), n)
                             : std::span<float>{});
    displayGainDb.store(
        juce::Decibels::gainToDecibels(buffer.getMagnitude(0, buffer.getNumSamples()), -96.F),
        std::memory_order_relaxed);
}
juce::AudioProcessorEditor *RetroToolsProcessor::createEditor() {
    return new ParameterPanel<RetroToolsProcessor>(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new RetroToolsProcessor; }
