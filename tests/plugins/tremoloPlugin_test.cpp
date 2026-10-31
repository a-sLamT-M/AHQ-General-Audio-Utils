#include "choiceEditor_test.h"
#include "tremoloProcessor.h"
#include "userPreset_test.h"
#include <cmath>
#include <iostream>
int main() {
    juce::ScopedJuceInitialiser_GUI init;
    if (!testUserPresetLibrary<TremoloProcessor>())
        return 1;
    bool ok = testChoiceEditor<TremoloProcessor>({{"Host BPM", "sync"}, {"Sine", "shape"}});
    auto check = [&](bool b, const char *name) {
        if (!b)
            std::cerr << name << '\n';
        ok &= b;
    };
    TremoloProcessor p;
    p.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> block(2, 4097);
    juce::MidiBuffer midi;
    for (int i = 0; i < block.getNumSamples(); ++i) {
        block.setSample(0, i, .3F);
        block.setSample(1, i, -.15F);
    }
    p.processBlock(block, midi);
    check(p.getLatencySamples() == 0, "zero latency");
    for (int i = 0; i < block.getNumSamples(); ++i)
        check(std::isfinite(block.getSample(0, i)) &&
                  block.getSample(1, i) == -.5F * block.getSample(0, i),
              "linked finite output");
    auto values = p.readValues();
    values[0] = values[0] == 0 ? 1.F : values[0] * .9F;
    check(p.applyUserValues(values, "Edit"), "parameter edit");
    juce::MemoryBlock state;
    p.getStateInformation(state);
    TremoloProcessor restored;
    restored.setStateInformation(state.getData(), int(state.getSize()));
    check(p.readValues() == restored.readValues(), "state roundtrip");
    values[0] = NAN;
    check(!p.applyUserValues(values, "Invalid"), "reject invalid state");
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(0, juce::AudioChannelSet::mono());
    layout.outputBuses.set(0, juce::AudioChannelSet::mono());
    check(p.setBusesLayout(layout), "mono supported");
    return ok ? 0 : 1;
}
