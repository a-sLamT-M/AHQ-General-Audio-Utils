#include "choiceEditor_test.h"
#include "elevationProcessor.h"
#include "userPreset_test.h"
#include <cmath>
#include <iostream>
int main(int argc, char **argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (!testUserPresetLibrary<ElevationProcessor>())
        return 1;
    bool ok = testChoiceEditor<ElevationProcessor>({});
    auto check = [&](bool b, const char *name) {
        if (!b)
            std::cerr << name << '\n';
        ok &= b;
    };
    auto p = std::make_unique<ElevationProcessor>();
    auto layout = p->getBusesLayout();
    layout.inputBuses.set(0, juce::AudioChannelSet::mono());
    check(p->setBusesLayout(layout), "mono to stereo");
    p->prepareToPlay(48000, 256);
    check(p->getLatencySamples() == 120, "reported delay");
    juce::AudioBuffer<float> b(2, 4096);
    b.clear();
    b.setSample(0, 0, 1);
    juce::MidiBuffer midi;
    p->processBlock(b, midi);
    check(b.getMagnitude(0, 4096) > .01F && b.getMagnitude(1, 0, 4096) > .01F,
          "mono spatialized to both ears");
    auto v = p->readValues();
    v[0] = -90;
    check(p->applyUserValues(v, "Below"), "edit elevation");
    juce::MemoryBlock state;
    p->getStateInformation(state);
    auto restored = std::make_unique<ElevationProcessor>();
    restored->setStateInformation(state.getData(), int(state.getSize()));
    check(restored->readValues() == p->readValues(), "state roundtrip");
    if (argc == 2) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditorAndMakeActive());
        editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        editor->setVisible(true);
        auto out = juce::File(juce::String::fromUTF8(argv[1])).createOutputStream();
        if (out) {
            out->setPosition(0);
            out->truncate();
            juce::PNGImageFormat png;
            check(png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),
                                         *out),
                  "snapshot");
        }
        editor->setVisible(false);
        editor->removeFromDesktop();
        p->editorBeingDeleted(editor.get());
    }
    return ok ? 0 : 1;
}
