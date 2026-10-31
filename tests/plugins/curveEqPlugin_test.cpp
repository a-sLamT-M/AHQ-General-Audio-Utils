#include "choiceEditor_test.h"
#include "curveEqProcessor.h"
#include "userPreset_test.h"
#include <cmath>
#include <numbers>
int main(int argc, char **argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (!testUserPresetLibrary<CurveEqProcessor>())
        return 1;
    bool passed = true;
    auto check = [&](bool condition, const char *name) {
        if (!condition)
            std::cerr << "FAILED: " << name << '\n';
        passed &= condition;
    };
    passed &= testChoiceEditor<CurveEqProcessor>({{"Bell", "b1_shape"}, {"Stereo", "b1_channels"}});
    CurveEqProcessor p;
    p.prepareToPlay(48000, 256);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(2, 4097);
    auto fill = [&] {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample(ch, i,
                                 float(.2 * std::sin(2 * std::numbers::pi * 1000 * i / 48000)));
    };
    fill();
    const juce::AudioBuffer<float> original(buffer);
    p.processBlock(buffer, midi);
    check(buffer.getSample(0, 1000) == original.getSample(0, 1000), "flat bypass");
    auto values = p.readValues();
    values[0] = 1;
    values[4] = 6;
    check(p.applyUserValues(values, "Bell"), "edit band");
    fill();
    p.processBlock(buffer, midi);
    double energyIn = 0, energyOut = 0;
    for (int i = 2048; i < 4097; ++i) {
        energyIn += std::pow(original.getSample(0, i), 2);
        energyOut += std::pow(buffer.getSample(0, i), 2);
    }
    check(std::abs(10 * std::log10(energyOut / energyIn) - 6) < .02, "oversized block bell gain");
    check(p.getLatencySamples() == 0, "zero host latency");
    juce::MemoryBlock state;
    p.getStateInformation(state);
    CurveEqProcessor restored;
    restored.setStateInformation(state.getData(), int(state.getSize()));
    check(restored.readValues() == p.readValues(), "all 266 parameters saved");
    const auto valid = p.readValues();
    values = valid;
    values[1] = 99;
    check(!p.applyUserValues(values, "Invalid") && p.readValues() == valid, "reject invalid shape");
    values = valid;
    values[CurveEqProcessor::count - 1] = 0;
    check(p.applyUserValues(values, "Dry"), "dry mix");
    fill();
    p.processBlock(buffer, midi);
    check(buffer.getSample(0, 4096) == original.getSample(0, 4096), "settled dry mix exact");
    if (argc == 2) {
        values = valid;
        values[4] = 5;
        values[3] = 160;
        values[11] = 1;
        values[14] = 1800;
        values[15] = -6;
        values[16] = 2;
        values[18] = -4;
        values[22] = 1;
        values[23] = 2;
        values[25] = 8500;
        values[26] = 3;
        check(p.applyUserValues(values, "Display setup"), "display bands");
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditorAndMakeActive());
        editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        editor->setVisible(true);
        for (int i = 0; i < 8; ++i) {
            fill();
            p.processBlock(buffer, midi);
            juce::Thread::sleep(35);
            juce::Timer::callPendingTimersSynchronously();
        }
        auto image = editor->createComponentSnapshot(editor->getLocalBounds());
        auto out = juce::File(juce::String::fromUTF8(argv[1])).createOutputStream();
        if (out) {
            out->setPosition(0);
            out->truncate();
            juce::PNGImageFormat png;
            check(png.writeImageToStream(image, *out), "snapshot");
        } else
            check(false, "snapshot output");
        editor->setVisible(false);
        editor->removeFromDesktop();
        p.editorBeingDeleted(editor.get());
    }
    return passed ? 0 : 1;
}
