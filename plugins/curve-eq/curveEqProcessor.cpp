#include "curveEqProcessor.h"
#include "curveEqEditor.h"
#include <cmath>
std::array<std::string, CurveEqProcessor::count> CurveEqProcessor::makeIds() {
    std::array<std::string, count> result;
    const std::array<const char *, fields> names{"enabled",   "shape",  "channels", "frequency",
                                                 "gain",      "q",      "stages",   "dynamic",
                                                 "threshold", "attack", "release"};
    for (size_t band = 0; band < 24; ++band)
        for (size_t f = 0; f < fields; ++f)
            result[band * fields + f] = "b" + std::to_string(band + 1) + "_" + names[f];
    result[count - 2] = "output";
    result[count - 1] = "mix";
    return result;
}
std::array<const char *, CurveEqProcessor::count>
CurveEqProcessor::makePointers(const std::array<std::string, count> &storage) {
    std::array<const char *, count> result{};
    for (size_t i = 0; i < count; ++i)
        result[i] = storage[i].c_str();
    return result;
}
juce::AudioProcessorValueTreeState::ParameterLayout
CurveEqProcessor::createParameterLayout(const std::array<const char *, count> &ids) {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto continuous = [&](size_t index, const juce::String &name, float lo, float hi, float initial,
                          float midpoint = 0) {
        juce::NormalisableRange<float> range(lo, hi);
        if (midpoint > lo && midpoint < hi)
            range.setSkewForCentre(midpoint);
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[index], 1},
                                                               name, range, initial));
    };
    for (size_t band = 0; band < 24; ++band) {
        const size_t i = band * fields;
        const auto prefix = "Band " + juce::String(int(band + 1)) + " ";
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids[i], 1},
                                                              prefix + "enabled", false));
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{ids[i + 1], 1}, prefix + "shape",
            juce::StringArray{"Bell", "Low shelf", "High shelf", "Low cut", "High cut", "Notch",
                              "Band pass"},
            0));
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{ids[i + 2], 1}, prefix + "channels",
            juce::StringArray{"Stereo", "Left", "Right", "Mid", "Side"}, 0));
        continuous(i + 3, prefix + "frequency", 10, 30000, 1000, 1000);
        continuous(i + 4, prefix + "gain", -30, 30, 0);
        continuous(i + 5, prefix + "Q", .1F, 30, .7071068F, 1);
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{ids[i + 6], 1},
                                                             prefix + "cut stages", 1, 4, 1));
        continuous(i + 7, prefix + "dynamic range", -24, 24, 0);
        continuous(i + 8, prefix + "threshold", -80, 0, -24);
        continuous(i + 9, prefix + "attack", .1F, 200, 10, 10);
        continuous(i + 10, prefix + "release", 10, 2000, 150, 150);
    }
    continuous(count - 2, "Output gain", -24, 24, 0);
    continuous(count - 1, "Mix", 0, 1, 1);
    return layout;
}
CurveEqProcessor::CurveEqProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      idStorage(makeIds()), ids(makePointers(idStorage)),
      parameters(*this, nullptr, "CurveEqParameters", createParameterLayout(ids)),
      state(parameters, ids, "AHQCurveEqState", program) {}
ahq::audio::EqBandParameters CurveEqProcessor::bandParameters(const Values &v, size_t band) {
    const auto i = band * fields;
    return {v[i] != 0,
            ahq::audio::EqShape(int(v[i + 1])),
            ahq::audio::EqChannels(int(v[i + 2])),
            v[i + 3],
            v[i + 4],
            v[i + 5],
            int(v[i + 6]),
            v[i + 7],
            v[i + 8],
            v[i + 9],
            v[i + 10]};
}
void CurveEqProcessor::prepareToPlay(double rate, int) {
    prepared = eq.prepare(rate);
    analysis.prepare(rate);
    displaySampleRate.store(rate);
    if (prepared) {
        const auto v = readValues();
        for (size_t i = 0; i < 24; ++i)
            (void)eq.setBand(i, bandParameters(v, i));
        eq.reset();
        outputGain.reset(std::pow(10., v[count - 2] / 20.));
        wetMix.reset(v[count - 1]);
        smoothingFrames = unsigned(std::ceil(rate * .005));
    }
    setLatencySamples(0);
}
bool CurveEqProcessor::isBusesLayoutSupported(const BusesLayout &layout) const {
    const auto c = layout.getMainInputChannelSet();
    return c == layout.getMainOutputChannelSet() &&
           (c == juce::AudioChannelSet::mono() || c == juce::AudioChannelSet::stereo());
}
void CurveEqProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &) {
    juce::ScopedNoDenormals noDenormals;
    if (!prepared || buffer.getNumSamples() == 0 || buffer.getNumChannels() < 1)
        return;
    const float inputPeak = PluginAnalysis::peak(buffer);
    const auto v = readValues();
    if (!state.validateValues(v))
        return;
    for (size_t i = 0; i < 24; ++i)
        (void)eq.setBand(i, bandParameters(v, i));
    outputGain.setTarget(std::pow(10., v[count - 2] / 20.), smoothingFrames);
    wetMix.setTarget(v[count - 1], smoothingFrames);
    const bool stereo = buffer.getNumChannels() > 1;
    for (int offset = 0; offset < buffer.getNumSamples();) {
        const auto n = size_t(std::min(2048, buffer.getNumSamples() - offset));
        auto left = std::span<float>(buffer.getWritePointer(0) + offset, n);
        auto right =
            stereo ? std::span<float>(buffer.getWritePointer(1) + offset, n) : std::span<float>{};
        std::copy(left.begin(), left.end(), dryLeft.begin());
        if (stereo)
            std::copy(right.begin(), right.end(), dryRight.begin());
        (void)eq.process(left, right);
        for (size_t i = 0; i < n; ++i) {
            const float gain = float(outputGain.next()), mix = float(wetMix.next());
            left[i] = (dryLeft[i] + mix * (left[i] - dryLeft[i])) * gain;
            if (stereo)
                right[i] = (dryRight[i] + mix * (right[i] - dryRight[i])) * gain;
        }
        offset += int(n);
    }
    for (size_t i = 0; i < 24; ++i)
        dynamicDb[i].store(float(eq.dynamicGain(i)), std::memory_order_relaxed);
    analysis.publish(buffer, inputPeak);
}
juce::AudioProcessorEditor *CurveEqProcessor::createEditor() { return new CurveEqEditor(*this); }
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new CurveEqProcessor; }
