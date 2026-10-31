#include "choiceEditor_test.h"
#include "userPreset_test.h"
#include "volumeEnvelopeProcessor.h"
#include <chrono>
#include <iostream>
namespace {
struct Playhead final : juce::AudioPlayHead {
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        p.setBpm(120);
        p.setPpqPosition(ppq);
        p.setIsPlaying(playing);
        return p;
    }
    double ppq = 0;
    bool playing = true;
};
} // namespace
int main(int argc, char **argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (!testUserPresetLibrary<VolumeEnvelopeProcessor>())
        return 1;
    bool passed = true;
    auto check = [&](bool condition, const char *name) {
        if (!condition)
            std::cerr << "FAILED: " << name << '\n';
        passed &= condition;
    };
    passed &= testChoiceEditor<VolumeEnvelopeProcessor>({{"Host BPM", "mode"}});
    VolumeEnvelopeProcessor p;
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(1, juce::AudioChannelSet::mono());
    check(p.setBusesLayout(layout), "mono sidechain layout");
    Playhead playhead;
    p.setPlayHead(&playhead);
    p.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> block(3, 256);
    juce::MidiBuffer midi;
    auto fill = [&](float trigger) {
        for (int i = 0; i < block.getNumSamples(); ++i) {
            block.setSample(0, i, .5F);
            block.setSample(1, i, -.25F);
            block.setSample(2, i, trigger);
        }
    };
    fill(0);
    p.processBlock(block, midi);
    check(block.getSample(0, 255) < .01F, "default BPM ducking");
    check(block.getSample(1, 255) == block.getSample(0, 255) * -.5F, "linked channels");
    playhead.ppq = .98;
    fill(0);
    p.processBlock(block, midi);
    check(block.getSample(0, 255) > .49F, "PPQ jump follows host");
    auto values = p.readValues();
    values[0] = 1;
    check(p.applyUserValues(values, "Audio mode"), "switch mode");
    fill(0);
    p.processBlock(block, midi);
    check(block.getSample(0, 255) > .49F, "no sidechain signal preserves audio");
    fill(.5F);
    p.processBlock(block, midi);
    check(block.getSample(0, 255) < .01F, "external sidechain triggers envelope");
    juce::MemoryBlock state;
    p.getStateInformation(state);
    VolumeEnvelopeProcessor restored;
    restored.setStateInformation(state.getData(), int(state.getSize()));
    check(restored.readValues() == p.readValues(), "state roundtrip");
    // A two-axis edit must undo only its own parameters, preserving independent automation.
    auto before = p.readValues();
    values = before;
    values[4] = .06F;
    values[11] = .1F;
    p.getParameters().getParameter("time1")->setValueNotifyingHost(.06F);
    p.getParameters().getParameter("level1")->setValueNotifyingHost(.1F);
    const std::array<int, 2> edited{4, 11};
    p.recordUserEdit(before, edited);
    p.getParameters().getParameter("threshold")->setValueNotifyingHost(.5F);
    check(p.undo() && p.readValues()[4] == before[4] && p.readValues()[11] == before[11] &&
              p.readValues()[2] == -40.F,
          "two-axis undo preserves other automation");
    if (argc == 2) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditorAndMakeActive());
        editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        editor->setVisible(true);
        juce::Timer::callPendingTimersSynchronously();
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
    p.releaseResources();
    return passed ? 0 : 1;
}
