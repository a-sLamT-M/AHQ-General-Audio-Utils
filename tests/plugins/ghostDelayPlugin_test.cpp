#include "choiceEditor_test.h"
#include "ghostDelayProcessor.h"
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
bool nearValues(const GhostDelayProcessor::Values &a, const GhostDelayProcessor::Values &b) {
    for (size_t index = 0; index < a.size(); ++index)
        if (std::abs(a[index] - b[index]) > 0.0001F)
            return false;
    return true;
}
bool testState() {
    GhostDelayProcessor p;
    const auto initial = p.readValues();
    const GhostDelayProcessor::Values edited{150, 400, -7, 0.7F, 30, 0.4F, 0.8F, 65535, 0.6F};
    bool passed = expect(p.applyUserValues(edited, "Edit"), "apply parameters");
    passed &= expect(nearValues(p.readValues(), edited), "parameters preserved");
    passed &= expect(p.undo() && nearValues(p.readValues(), initial), "single transaction undo");
    passed &= expect(p.redo() && nearValues(p.readValues(), edited), "redo");
    auto *grain = p.getParameters().getParameter("grain");
    auto *mix = p.getParameters().getParameter("mix");
    grain->setValueNotifyingHost(grain->convertTo0to1(80));
    mix->setValueNotifyingHost(mix->convertTo0to1(0.25F));
    p.recordUserEdit(edited, 0, "Drag grain");
    auto expected = edited;
    expected[8] = 0.25F;
    ParameterEvents mixEvents;
    mix->addListener(&mixEvents);
    passed &= expect(p.undo() && nearValues(p.readValues(), expected),
                     "drag undo preserves other automation");
    passed &= expect(mixEvents.values == 0 && mixEvents.gestures == 0,
                     "grain undo sends no value or gesture events to unedited mix");
    passed &= expect(p.redo(), "redo grain drag");
    passed &= expect(mixEvents.values == 0 && mixEvents.gestures == 0,
                     "grain redo sends no value or gesture events to unedited mix");
    passed &= expect(p.undo(), "return grain to edited value");
    mix->removeListener(&mixEvents);
    const auto xml = p.createPresetXml();
    for (const auto &bad :
         {juce::String("<bad/>"), xml.replace("version=\"1\"", "version=\"9\""),
          xml.replace("grain=\"", "grain=\"nan"), xml.replace(" seed=", " unknown="),
          xml.replace("AHQGhostDelayState", "AHQQuickFilterState"),
          juce::String::repeatedString("x", 65537)}) {
        passed &= expect(!p.loadPresetXml(bad) && nearValues(p.readValues(), expected),
                         "invalid preset unchanged");
    }
    for (size_t index = 0; index < 9; ++index) {
        auto bad = expected;
        bad[index] = std::numeric_limits<float>::infinity();
        passed &= expect(!p.applyUserValues(bad, "Invalid"), "nonfinite parameter rejected");
    }
    juce::MemoryBlock saved;
    p.getStateInformation(saved);
    GhostDelayProcessor restored;
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
float source(int frame, int channel) {
    return float(std::sin(frame * 0.081) * (channel == 0 ? 0.3 : -0.2));
}
ahq::audio::GrainDelayParameters coreParameters(const GhostDelayProcessor::Values &v) {
    return {v[0], v[1], v[2], v[3], v[4], v[5], v[6], std::uint32_t(v[7]), v[8]};
}
bool testDryLayoutsAndTail() {
    GhostDelayProcessor p;
    bool passed = expect(p.getLatencySamples() == 0 && !p.acceptsMidi() && !p.producesMidi(),
                         "intentional delay reports zero host latency");
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(0, juce::AudioChannelSet::mono());
    passed &= expect(!p.isBusesLayoutSupported(layout), "reject mismatched mono/stereo buses");
    for (int channels : {1, 2}) {
        layout.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        layout.outputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        passed &= expect(p.setBusesLayout(layout), "matched mono/stereo layouts");
        auto values = p.readValues();
        values[8] = 0;
        p.applyUserValues(values, "Dry");
        for (double rate : {8000.0, 48000.0, 192000.0}) {
            p.prepareToPlay(rate, 257);
            juce::MidiBuffer midi;
            int offset = 0;
            for (int frames : {0, 1, 31, 257, 1025}) {
                juce::AudioBuffer<float> block(channels, frames);
                for (int c = 0; c < channels; ++c)
                    for (int f = 0; f < frames; ++f)
                        block.setSample(c, f, source(offset + f, c));
                p.processBlock(block, midi);
                for (int c = 0; c < channels; ++c)
                    for (int f = 0; f < frames; ++f)
                        passed &= expect(block.getSample(c, f) == source(offset + f, c),
                                         "dry bit exact without alignment delay");
                offset += frames;
            }
        }
    }
    auto values = p.readValues();
    values[5] = 0;
    p.applyUserValues(values, "No feedback");
    passed &=
        expect(p.getTailLengthSeconds() >= 4.5, "no feedback conservative history/grain support");
    values[5] = 0.95F;
    p.applyUserValues(values, "High feedback");
    passed &= expect(p.getTailLengthSeconds() > 800 && std::isfinite(p.getTailLengthSeconds()),
                     "high feedback reports long conservative decay estimate");
    auto bad = values;
    bad[7] = 1.5F;
    passed &= expect(!p.applyUserValues(bad, "Fractional seed"), "integer seed required");
    bad[7] = 65536;
    passed &= expect(!p.applyUserValues(bad, "Large seed"), "UI seed range explicit and exact");
    return passed;
}
bool testCoreAndBypass() {
    GhostDelayProcessor p;
    const GhostDelayProcessor::Values settings{80, 120, -3, 0.5F, 30, 0.2F, 0.8F, 42, 0.7F};
    p.applyUserValues(settings, "Core comparison");
    p.prepareToPlay(48000, 257);
    ahq::audio::GrainDelay reference;
    bool passed =
        expect(reference.prepare(48000) && reference.setParameters(coreParameters(p.readValues())),
               "independent core configured");
    reference.reset();
    juce::MidiBuffer midi;
    int offset = 0;
    double wetDifference = 0;
    for (int iteration = 0; iteration < 180; ++iteration) {
        const int frames = iteration % 3 == 0 ? 31 : 257;
        juce::AudioBuffer<float> block(2, frames), expected(2, frames);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < frames; ++f) {
                block.setSample(c, f, source(offset + f, c));
                expected.setSample(c, f, source(offset + f, c));
            }
        passed &= expect(reference.process({expected.getWritePointer(0), size_t(frames)},
                                           {expected.getWritePointer(1), size_t(frames)}),
                         "reference processes waveform");
        const bool bypassed = iteration % 4 == 1;
        if (bypassed)
            p.processBlockBypassed(block, midi);
        else
            p.processBlock(block, midi);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < frames; ++f) {
                const float original = source(offset + f, c);
                passed &= expect(block.getSample(c, f) ==
                                     (bypassed ? original : expected.getSample(c, f)),
                                 "core exact normal output and bypass restoration history");
                wetDifference += std::abs(expected.getSample(c, f) - original);
            }
        offset += frames;
    }
    passed &= expect(wetDifference > 1, "actual ghost delay changes audio");
    return passed;
}
std::vector<float> renderSeed(int seed, const std::array<int, 4> &blocks) {
    GhostDelayProcessor p;
    p.applyUserValues({80, 120, -3, 0.5F, 30, 0.1F, 0.8F, float(seed), 1}, "Seed render");
    p.prepareToPlay(48000, 1024);
    std::vector<float> output;
    output.reserve(48000 * 2);
    juce::MidiBuffer midi;
    int offset = 0, iteration = 0;
    while (offset < 48000) {
        const int frames = std::min(blocks[size_t(iteration++ % 4)], 48000 - offset);
        juce::AudioBuffer<float> buffer(2, frames);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < frames; ++f)
                buffer.setSample(c, f, source(offset + f, c));
        p.processBlock(buffer, midi);
        for (int f = 0; f < frames; ++f) {
            output.push_back(buffer.getSample(0, f));
            output.push_back(buffer.getSample(1, f));
        }
        offset += frames;
    }
    return output;
}
bool testSeedReproducibility() {
    const auto first = renderSeed(42, {257, 31, 1024, 1}), same = renderSeed(42, {113, 512, 7, 97}),
               different = renderSeed(43, {257, 31, 1024, 1});
    bool passed =
        expect(first == same, "same integer seed is sample exact across block partitions");
    passed &= expect(first != different, "different seed changes actual grain audio");
    return passed;
}
bool renderEditor(const juce::File &destination) {
    GhostDelayProcessor p;
    p.loadFactoryPreset(1);
    p.prepareToPlay(48000, 2048);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditorAndMakeActive());
    if (!expect(editor != nullptr, "native editor"))
        return false;
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    editor->setVisible(true);
    bool passed = expect(editor->isShowing(), "visible native window");
    juce::MidiBuffer midi;
    for (int iteration = 0; iteration < 30; ++iteration) {
        juce::AudioBuffer<float> buffer(2, 2048);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < 2048; ++f)
                buffer.setSample(c, f, source(iteration * 2048 + f, c));
        p.processBlock(buffer, midi);
        juce::Thread::sleep(35);
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
    passed &= expect(png.writeImageToStream(snapshot, *out), "snapshot PNG");
    return passed;
}
} // namespace
int main(int argc, char **argv) {
    juce::ScopedJuceInitialiser_GUI initialise;
    if (!testUserPresetLibrary<GhostDelayProcessor>())
        return 1;
    bool passed = testState();
    passed &= testDryLayoutsAndTail();
    passed &= testCoreAndBypass();
    passed &= testSeedReproducibility();
    passed &= testChoiceEditor<GhostDelayProcessor>({});
    if (argc == 2)
        passed &= renderEditor(juce::File(juce::String::fromUTF8(argv[1])));
    return passed ? 0 : 1;
}
