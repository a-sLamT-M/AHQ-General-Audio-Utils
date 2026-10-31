#include "choiceEditor_test.h"
#include "retroToolsProcessor.h"
#include "userPreset_test.h"
#include <cmath>
#include <iostream>
int main() {
    juce::ScopedJuceInitialiser_GUI init;
    if (!testUserPresetLibrary<RetroToolsProcessor>())
        return 1;
    bool ok = testChoiceEditor<RetroToolsProcessor>({});
    auto check = [&](bool b, const char *name) {
        if (!b)
            std::cerr << name << '\n';
        ok &= b;
    };
    RetroToolsProcessor p;
    p.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> block(2, 4097);
    juce::MidiBuffer midi;
    for (int i = 0; i < block.getNumSamples(); ++i) {
        block.setSample(0, i, .3F);
        block.setSample(1, i, -.15F);
    }
    p.processBlock(block, midi);
    check(p.getLatencySamples() == 576, "reported latency");
    for (int i = 0; i < block.getNumSamples(); ++i)
        check(std::isfinite(block.getSample(0, i)) && std::isfinite(block.getSample(1, i)),
              "finite stereo output");
    auto values = p.readValues();
    values[0] = values[0] == 0 ? 1.F : values[0] * .9F;
    values[8] = .8F;
    check(p.applyUserValues(values, "Edit"), "parameter edit");
    juce::MemoryBlock state;
    p.getStateInformation(state);
    RetroToolsProcessor restored;
    restored.setStateInformation(state.getData(), int(state.getSize()));
    check(p.readValues() == restored.readValues(), "state roundtrip");
    check(restored.loadPresetXml(p.createPresetXml()) && restored.readValues() == values,
          "fault preset roundtrip");
    auto *fault = p.getParameters().getParameter("fault");
    check(fault != nullptr && fault->getParameterIndex() == 8, "fault appended after old IDs");
    if (fault) {
        fault->setValueNotifyingHost(.6F);
        check(std::abs(p.readValues()[8] - .6F) < 1e-6F, "fault host automation");
    }
    const auto beforeLegacy = restored.readValues();
    auto legacy = juce::parseXML(p.createPresetXml());
    legacy->removeAttribute("fault");
    auto legacyValues = p.readValues();
    legacyValues[8] = 0;
    check(restored.loadPresetXml(legacy->toString()) && restored.readValues() == legacyValues,
          "old preset restores original values with fault off");
    check(restored.undo() && restored.readValues() == beforeLegacy, "old preset undo");
    check(restored.redo() && restored.readValues() == legacyValues, "old preset redo");
    check(restored.applyUserValues(beforeLegacy, "Fault on"), "enable before old host state");
    juce::MemoryBlock legacyState;
    juce::AudioProcessor::copyXmlToBinary(*legacy, legacyState);
    restored.setStateInformation(legacyState.getData(), int(legacyState.getSize()));
    check(restored.readValues() == legacyValues && !restored.undo(),
          "old host state turns fault off and clears stale undo");
    legacy->setAttribute("fault", "nan");
    check(!restored.loadPresetXml(legacy->toString()) && restored.readValues() == legacyValues,
          "invalid fault preset leaves state intact");
    legacy->removeAttribute("fault");
    legacy->removeAttribute("flutter");
    check(!restored.loadPresetXml(legacy->toString()), "incomplete old preset rejected");

    RetroToolsProcessor clean, damaged;
    auto settings = clean.readValues();
    settings[0] = 0;
    settings[7] = 1;
    check(clean.applyUserValues(settings, "Clean"), "clean render settings");
    settings[8] = 1;
    check(damaged.applyUserValues(settings, "Fault"), "fault render settings");
    clean.prepareToPlay(48000, 256);
    damaged.prepareToPlay(48000, 256);
    double cleanEnergy = 0, damagedEnergy = 0;
    for (int n = 0; n < 200; ++n) {
        juce::AudioBuffer<float> dry(2, 256), wet(2, 256);
        for (int i = 0; i < 256; ++i)
            for (int c = 0; c < 2; ++c) {
                const float sample = float(.3 * std::sin(.04 * (n * 256 + i)));
                dry.setSample(c, i, sample);
                wet.setSample(c, i, sample);
            }
        clean.processBlock(dry, midi);
        damaged.processBlock(wet, midi);
        for (int i = 0; i < 256; ++i) {
            cleanEnergy += double(dry.getSample(0, i)) * dry.getSample(0, i);
            damagedEnergy += double(wet.getSample(0, i)) * wet.getSample(0, i);
        }
    }
    check(damagedEnergy > 0 && damagedEnergy < cleanEnergy * .8,
          "fault parameter reaches DSP without gain boost");
    values[0] = NAN;
    check(!p.applyUserValues(values, "Invalid"), "reject invalid state");
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(0, juce::AudioChannelSet::mono());
    layout.outputBuses.set(0, juce::AudioChannelSet::mono());
    check(p.setBusesLayout(layout), "mono supported");
    return ok ? 0 : 1;
}
