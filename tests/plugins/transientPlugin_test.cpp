#include "choiceEditor_test.h"
#include "transientProcessor.h"
#include "userPreset_test.h"
#include <cmath>
#include <iostream>
#include <juce_audio_utils/juce_audio_utils.h>
#include <limits>
#include <numbers>

namespace {
class ParameterEvents final : public juce::AudioProcessorParameter::Listener {
  public:
    void parameterValueChanged(int, float) override { ++values; }
    void parameterGestureChanged(int, bool) override { ++gestures; }
    int values = 0;
    int gestures = 0;
};
bool expect(bool condition, const char *message) {
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
bool nearValues(const TransientProcessor::Values &a, const TransientProcessor::Values &b) {
    for (size_t index = 0; index < a.size(); ++index)
        if (std::abs(a[index] - b[index]) > 0.0001F)
            return false;
    return true;
}
bool testState() {
    TransientProcessor p;
    const auto initial = p.readValues();
    const TransientProcessor::Values edited{4, -3, -55, 80, 0.6F};
    bool passed = expect(p.applyUserValues(edited, "Edit"), "apply parameters");
    passed &= expect(nearValues(p.readValues(), edited), "parameters preserved");
    passed &= expect(p.undo() && nearValues(p.readValues(), initial), "single transaction undo");
    passed &= expect(p.redo() && nearValues(p.readValues(), edited), "redo");
    auto *attack = p.getParameters().getParameter("attack");
    auto *mix = p.getParameters().getParameter("mix");
    attack->setValueNotifyingHost(attack->convertTo0to1(8));
    mix->setValueNotifyingHost(mix->convertTo0to1(0.25F));
    p.recordUserEdit(edited, 0, "Drag attack");
    auto expected = edited;
    expected[4] = 0.25F;
    ParameterEvents mixEvents;
    mix->addListener(&mixEvents);
    passed &= expect(p.undo() && nearValues(p.readValues(), expected),
                     "drag undo preserves other automation");
    passed &= expect(mixEvents.values == 0 && mixEvents.gestures == 0,
                     "attack undo sends no value or gesture events to unedited mix");
    passed &= expect(p.redo(), "redo attack drag");
    passed &= expect(mixEvents.values == 0 && mixEvents.gestures == 0,
                     "attack redo sends no value or gesture events to unedited mix");
    passed &= expect(p.undo(), "return attack to edited value");
    mix->removeListener(&mixEvents);
    const auto xml = p.createPresetXml();
    for (const auto &bad :
         {juce::String("<bad/>"), xml.replace("version=\"1\"", "version=\"9\""),
          xml.replace("attack=\"", "attack=\"nan"), xml.replace(" time=", " unknown="),
          xml.replace("AHQTransientState", "AHQQuickFilterState"),
          juce::String::repeatedString("x", 65537)}) {
        passed &= expect(!p.loadPresetXml(bad) && nearValues(p.readValues(), expected),
                         "invalid preset unchanged");
    }
    for (size_t index = 0; index < 5; ++index) {
        auto bad = expected;
        bad[index] = std::numeric_limits<float>::infinity();
        passed &= expect(!p.applyUserValues(bad, "Invalid"), "nonfinite parameter rejected");
    }
    juce::MemoryBlock saved;
    p.getStateInformation(saved);
    TransientProcessor restored;
    restored.loadFactoryPreset(2);
    restored.setStateInformation(saved.getData(), int(saved.getSize()));
    passed &= expect(nearValues(restored.readValues(), expected) && !restored.canUndo(),
                     "host restore invalidates history");
    restored.setStateInformation(nullptr, 64);
    passed &= expect(nearValues(restored.readValues(), expected), "bad host state unchanged");
    passed &= expect(restored.loadPresetXml(xml), "preset roundtrip");
    passed &= expect(restored.loadFactoryPreset(3) && restored.undo() &&
                         nearValues(restored.readValues(), expected),
                     "preset grouped undo");
    for (int index = 0; index < restored.getNumPrograms(); ++index) {
        restored.setCurrentProgram(index);
        passed &= expect(restored.getCurrentProgram() == index, "valid host program");
    }
    return passed;
}
bool testHost() {
    TransientProcessor p;
    juce::MidiBuffer midi;
    bool passed = expect(!p.acceptsMidi() && !p.producesMidi() && p.getLatencySamples() == 0,
                         "host MIDI and latency");
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(0, juce::AudioChannelSet::mono());
    passed &= expect(!p.isBusesLayoutSupported(layout), "reject asymmetric layout");
    p.applyUserValues({6, -6, -60, 50, 0}, "Dry");
    for (int channels : {1, 2}) {
        layout.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        layout.outputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        passed &= expect(p.setBusesLayout(layout), "mono/stereo layout");
        for (double rate : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0}) {
            p.setRateAndBufferSizeDetails(rate, 512);
            p.prepareToPlay(rate, 512);
            for (int frames : {0, 1, 17, 512, 1025}) {
                juce::AudioBuffer<float> block(channels, frames);
                for (int c = 0; c < channels; ++c)
                    for (int f = 0; f < frames; ++f)
                        block.setSample(c, f, c ? -0.25F : 0.125F);
                p.processBlock(block, midi);
                for (int c = 0; c < channels; ++c)
                    for (int f = 0; f < frames; ++f)
                        passed &= expect(block.getSample(c, f) == (c ? -0.25F : 0.125F),
                                         "dry sample exact variable block");
            }
            p.releaseResources();
        }
    }
    p.setSpectrumEnabled(true);
    p.prepareToPlay(48000, 2048);
    juce::AudioBuffer<float> block(2, 2048);
    for (int i = 0; i < 2048; ++i) {
        block.setSample(0, i, 0.25F);
        block.setSample(1, i, -0.25F);
    }
    p.processBlock(block, midi);
    std::array<float, 2048> l{}, r{};
    passed &= expect(p.readSpectrum(l, r) && l[0] == 0.25F && r[0] == -0.25F,
                     "spectrum preserves anti-phase channels");
    p.setSpectrumEnabled(false);
    p.processBlock(block, midi);
    passed &= expect(!p.readSpectrum(l, r), "disabled spectrum stops producer");
    return passed;
}
double renderEnergy(float attack, float sustain, bool steady) {
    TransientProcessor p;
    p.applyUserValues({attack, sustain, -60, 50, 1}, "Waveform");
    p.prepareToPlay(48000, 257);
    juce::MidiBuffer midi;
    double energy = 0;
    const int frames = steady ? 72000 : 2400;
    for (int offset = 0; offset < frames; offset += 257) {
        const int count = std::min(257, frames - offset);
        juce::AudioBuffer<float> block(2, count);
        for (int i = 0; i < count; ++i) {
            const double t = (offset + i) / 48000.0;
            const float sample = float(0.4 * std::sin(2 * std::numbers::pi * 170 * t) *
                                       (steady ? 1 : std::exp(-t / 0.025)));
            block.setSample(0, i, sample);
            block.setSample(1, i, -sample);
        }
        p.processBlock(block, midi);
        for (int i = 0; i < count; ++i)
            if (steady ? offset + i >= 48000 : offset + i < 480) {
                const double sample = block.getSample(0, i);
                energy += sample * sample;
            }
    }
    return energy;
}
bool testShaping() {
    const double neutralAttack = renderEnergy(0, 0, false),
                 neutralSustain = renderEnergy(0, 0, true);
    bool passed = expect(renderEnergy(6, 0, false) > neutralAttack * 1.2,
                         "attack boost increases onset energy");
    passed &= expect(renderEnergy(-6, 0, false) < neutralAttack * 0.8,
                     "attack cut decreases onset energy");
    passed &= expect(renderEnergy(0, 6, true) > neutralSustain * 1.2,
                     "sustain boost increases settled energy");
    passed &= expect(renderEnergy(0, -6, true) < neutralSustain * 0.8,
                     "sustain cut decreases settled energy");
    return passed;
}
bool renderEditor(const juce::File &destination) {
    TransientProcessor p;
    p.loadFactoryPreset(2);
    p.prepareToPlay(48000, 2048);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditorAndMakeActive());
    if (!expect(editor != nullptr, "native editor"))
        return false;
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    editor->setVisible(true);
    const bool visible = expect(editor->isShowing(), "visible native window");
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block(2, 2048);
    for (int iteration = 0; iteration < 12; ++iteration) {
        for (int i = 0; i < 2048; ++i) {
            const double t = ((iteration * 2048 + i) % 12000) / 48000.0;
            const float sample =
                float(0.6 * std::exp(-t / 0.035) * std::sin(2 * std::numbers::pi * 180 * t));
            block.setSample(0, i, sample);
            block.setSample(1, i, -sample);
        }
        p.processBlock(block, midi);
        juce::Thread::sleep(40);
        juce::Timer::callPendingTimersSynchronously();
    }
    const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
    editor->setVisible(false);
    editor->removeFromDesktop();
    p.editorBeingDeleted(editor.get());
    editor.reset();
    auto out = destination.createOutputStream();
    if (!expect(out != nullptr, "snapshot output"))
        return false;
    out->setPosition(0);
    out->truncate();
    juce::PNGImageFormat png;
    return visible && expect(png.writeImageToStream(snapshot, *out), "snapshot PNG");
}
} // namespace
int main(int argc, char **argv) {
    juce::ScopedJuceInitialiser_GUI initialise;
    if (!testUserPresetLibrary<TransientProcessor>())
        return 1;
    bool passed = testState();
    passed &= testHost();
    passed &= testShaping();
    passed &= testChoiceEditor<TransientProcessor>({});
    if (argc == 2)
        passed &= renderEditor(juce::File(juce::String::fromUTF8(argv[1])));
    return passed ? 0 : 1;
}
