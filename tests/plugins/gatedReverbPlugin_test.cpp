#include "choiceEditor_test.h"
#include "gatedReverbProcessor.h"
#include "userPreset_test.h"
#include <cmath>
#include <iostream>
int main() {
    juce::ScopedJuceInitialiser_GUI init;
    if (!testUserPresetLibrary<GatedReverbProcessor>())
        return 1;
    bool ok = testChoiceEditor<GatedReverbProcessor>({{"Non-linear", "shape"}});
    auto check = [&](bool b, const char *name) {
        if (!b)
            std::cerr << name << '\n';
        ok &= b;
    };
    GatedReverbProcessor p;
    auto v = p.readValues();
    v[0] = 200;
    v[3] = 0;
    v[4] = 1;
    check(p.applyUserValues(v, "Wet tail"), "parameters");
    p.prepareToPlay(48000, 256);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> b(2, 256);
    double early = 0, middle = 0, after = 0;
    for (int block = 0; block < 100; ++block) {
        b.clear();
        if (block == 0) {
            b.setSample(0, 0, 1);
            b.setSample(1, 0, 1);
        }
        p.processBlock(b, midi);
        for (int i = 0; i < 256; ++i) {
            const float s = b.getSample(0, i);
            check(std::isfinite(s), "finite output");
            const int frame = block * 256 + i;
            if (frame >= 1200 && frame < 3600)
                early += s * s;
            if (frame >= 6000 && frame < 8400)
                middle += s * s;
            if (frame > 10000)
                after += s * s;
        }
    }
    check(early > .01 && middle / early > .6 && middle / early < 1.6,
          "roughly constant-energy non-linear tail");
    check(after < 1e-10, "finite tail cuts off after selected duration");
    juce::MemoryBlock state;
    p.getStateInformation(state);
    GatedReverbProcessor restored;
    restored.setStateInformation(state.getData(), int(state.getSize()));
    check(p.readValues() == restored.readValues(), "state roundtrip");
    return ok ? 0 : 1;
}
