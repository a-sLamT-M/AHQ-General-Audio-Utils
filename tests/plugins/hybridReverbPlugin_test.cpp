#include "choiceEditor_test.h"
#include "hybridReverbProcessor.h"
#include "userPreset_test.h"
#include <cmath>
#include <iostream>
int main() {
    juce::ScopedJuceInitialiser_GUI init;
    if (!testUserPresetLibrary<HybridReverbProcessor>())
        return 1;
    bool ok = testChoiceEditor<HybridReverbProcessor>({});
    auto check = [&](bool b, const char *name) {
        if (!b)
            std::cerr << name << '\n';
        ok &= b;
    };
    HybridReverbProcessor p;
    auto v = p.readValues();
    v[4] = 0;
    v[6] = 1;
    check(p.applyUserValues(v, "Wet"), "parameters");
    p.prepareToPlay(48000, 256);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> b(2, 256);
    double energy = 0, later = 0;
    for (int block = 0; block < 188; ++block) {
        b.clear();
        if (block == 0) {
            b.setSample(0, 0, 1);
            b.setSample(1, 0, 1);
        }
        p.processBlock(b, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 256; ++i) {
                const float s = b.getSample(c, i);
                check(std::isfinite(s), "finite output");
                energy += s * s;
                if (block > 40)
                    later += s * s;
            }
    }
    check(energy > .01 && later > .00001, "early convolution and algorithmic tail");
    juce::MemoryBlock state;
    p.getStateInformation(state);
    HybridReverbProcessor restored;
    restored.setStateInformation(state.getData(), int(state.getSize()));
    check(restored.readValues() == p.readValues(), "state roundtrip");
    restored.prepareToPlay(48000, 1);
    check(restored.impulseBank().loaded(0), "embedded early IR restored");
    {
        const auto before = restored.createPresetXml();
        auto custom = juce::parseXML(p.createPresetXml());
        custom->setAttribute("late", -24.0);
        custom->getChildByName("ImpulseResponses")
            ->getChildElement(0)
            ->setAttribute("name", "Portable early reflections");
        check(restored.loadPresetXml(custom->toString()), "load complete reverb user preset");
        const auto after = restored.createPresetXml();
        check(after.contains("Portable early reflections") &&
                  std::abs(restored.readValues()[1] + 24.F) < .001F,
              "reverb preset restores IR and parameter data together");
        check(restored.undo() && restored.createPresetXml() == before && restored.redo() &&
                  restored.createPresetXml() == after,
              "complete reverb preset undo and redo");
        auto bad = juce::parseXML(after);
        auto *ir = bad->getChildByName("ImpulseResponses")->getChildElement(0);
        ir->deleteAllChildElements();
        ir->addTextElement(juce::MemoryBlock("not audio", 9).toBase64Encoding());
        check(!restored.loadPresetXml(bad->toString()) && restored.createPresetXml() == after,
              "corrupt IR preset preserves reverb parameters and embedded audio");
    }
    v[6] = 0;
    check(p.applyUserValues(v, "Dry"), "dry parameter");
    b.clear();
    p.processBlock(b, midi);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 256; ++i)
            b.setSample(c, i, .25F);
    p.processBlock(b, midi);
    check(b.getSample(0, 255) == .25F, "dry mix");
    return ok ? 0 : 1;
}
