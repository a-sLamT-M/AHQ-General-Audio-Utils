#include "chaosProcessor.h"
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
bool nearValues(const ChaosProcessor::Values &a, const ChaosProcessor::Values &b) {
    for (size_t index = 0; index < a.size(); ++index)
        if (std::abs(a[index] - b[index]) > 0.0001F)
            return false;
    return true;
}
bool testState() {
    ChaosProcessor p;
    const auto initial = p.readValues();
    const ChaosProcessor::Values edited{120, 0.8F, 4, 0.7F, 18, 0.8F, 24, 0.9F, 65535, 0.7F};
    bool passed = expect(p.applyUserValues(edited, "Edit"), "apply parameters");
    passed &= expect(nearValues(p.readValues(), edited), "parameters preserved");
    passed &= expect(p.undo() && nearValues(p.readValues(), initial), "single transaction undo");
    passed &= expect(p.redo() && nearValues(p.readValues(), edited), "redo");
    auto *slice = p.getParameters().getParameter("slice");
    auto *mix = p.getParameters().getParameter("mix");
    slice->setValueNotifyingHost(slice->convertTo0to1(80.0F));
    mix->setValueNotifyingHost(mix->convertTo0to1(0.25F));
    p.recordUserEdit(edited, 0, "Drag slice");
    auto expected = edited;
    expected[9] = 0.25F;
    ParameterEvents mixEvents;
    mix->addListener(&mixEvents);
    passed &= expect(p.undo() && nearValues(p.readValues(), expected),
                     "drag undo preserves other automation");
    passed &= expect(mixEvents.values == 0 && mixEvents.gestures == 0,
                     "slice undo sends no value or gesture events to unedited mix");
    passed &= expect(p.redo(), "redo slice drag");
    passed &= expect(mixEvents.values == 0 && mixEvents.gestures == 0,
                     "slice redo sends no value or gesture events to unedited mix");
    passed &= expect(p.undo(), "return slice to edited value");
    mix->removeListener(&mixEvents);
    const auto xml = p.createPresetXml();
    for (const auto &bad :
         {juce::String("<bad/>"), xml.replace("version=\"1\"", "version=\"9\""),
          xml.replace("slice=\"", "slice=\"nan"), xml.replace(" reverse=", " unknown="),
          xml.replace("AHQChaosState", "AHQQuickFilterState"),
          juce::String::repeatedString("x", 65537)}) {
        passed &= expect(!p.loadPresetXml(bad) && nearValues(p.readValues(), expected),
                         "invalid preset unchanged");
    }
    for (size_t index = 0; index < 10; ++index) {
        auto bad = expected;
        bad[index] = std::numeric_limits<float>::infinity();
        passed &= expect(!p.applyUserValues(bad, "Invalid"), "nonfinite parameter rejected");
    }
    for (size_t index : {size_t(2), size_t(8)}) {
        auto bad = expected;
        bad[index] += 0.5F;
        passed &= expect(!p.applyUserValues(bad, "Fractional integer") &&
                             nearValues(p.readValues(), expected),
                         "fractional repeats/seed rejected without changing state");
    }
    juce::MemoryBlock saved;
    p.getStateInformation(saved);
    ChaosProcessor restored;
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
ahq::audio::ChaosParameters coreParameters(const ChaosProcessor::Values &v) {
    return {v[0], v[1], std::uint32_t(v[2]), v[3], v[4], v[5],
            v[6], v[7], std::uint32_t(v[8]), v[9]};
}
bool testCoreLayoutsAndBypass() {
    ChaosProcessor p;
    constexpr std::array<const char *, 10> ids{"slice",      "probability", "repeats", "reverse",
                                               "pitchRange", "pan",         "drive",   "distortion",
                                               "seed",       "mix"};
    bool passed = expect(p.getLatencySamples() == 0 && p.getTailLengthSeconds() == 17.25,
                         "zero latency and conservative captured-audio tail");
    for (const char *id : ids)
        passed &= expect(p.getParameters().getParameter(id) != nullptr, "stable ID");
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(0, juce::AudioChannelSet::mono());
    passed &= expect(!p.isBusesLayoutSupported(layout), "reject mismatched buses");
    juce::MidiBuffer midi;
    for (int channels : {1, 2}) {
        layout.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        layout.outputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        passed &= expect(p.setBusesLayout(layout), "matched mono/stereo");
        ChaosProcessor::Values values{20, 1, 3, 0.7F, 18, 0.8F, 18, 0.7F, 65535, 0.8F};
        p.applyUserValues(values, "Core reference");
        p.prepareToPlay(48000, 257);
        ahq::audio::Chaos reference;
        passed &= expect(reference.prepare(48000) &&
                             reference.setParameters(coreParameters(p.readValues())),
                         "configure reference");
        reference.reset();
        int offset = 0;
        for (int iteration = 0; iteration < 80; ++iteration) {
            juce::AudioBuffer<float> block(channels, 257), expected(channels, 257);
            for (int c = 0; c < channels; ++c)
                for (int f = 0; f < 257; ++f) {
                    block.setSample(c, f, source(offset + f, c));
                    expected.setSample(c, f, source(offset + f, c));
                }
            passed &= expect(
                reference.process({expected.getWritePointer(0), 257},
                                  channels == 2 ? std::span<float>(expected.getWritePointer(1), 257)
                                                : std::span<float>{}),
                "reference process");
            bool bypass = iteration % 3 == 1;
            if (bypass)
                p.processBlockBypassed(block, midi);
            else
                p.processBlock(block, midi);
            for (int c = 0; c < channels; ++c)
                for (int f = 0; f < 257; ++f)
                    passed &= expect(block.getSample(c, f) == (bypass ? source(offset + f, c)
                                                                      : expected.getSample(c, f)),
                                     "core mapping and bypass random/playback continuity");
            offset += 257;
        }
        values[9] = 0;
        p.applyUserValues(values, "Dry");
        p.prepareToPlay(48000, 257);
        juce::AudioBuffer<float> dry(channels, 257);
        for (int c = 0; c < channels; ++c)
            for (int f = 0; f < 257; ++f)
                dry.setSample(c, f, source(f, c));
        p.processBlock(dry, midi);
        for (int c = 0; c < channels; ++c)
            for (int f = 0; f < 257; ++f)
                passed &= expect(dry.getSample(c, f) == source(f, c), "mix zero exact");
    }
    return passed;
}
bool testSeedChunking() {
    ChaosProcessor whole, chunks;
    const ChaosProcessor::Values values{20, 0.9F, 3, 0.5F, 24, 0.8F, 24, 0.8F, 65535, 1};
    whole.applyUserValues(values, "Seed");
    chunks.applyUserValues(values, "Seed");
    whole.prepareToPlay(48000, 24000);
    chunks.prepareToPlay(48000, 257);
    juce::AudioBuffer<float> expected(2, 24000);
    for (int c = 0; c < 2; ++c)
        for (int f = 0; f < 24000; ++f)
            expected.setSample(c, f, source(f, c));
    juce::MidiBuffer midi;
    whole.processBlock(expected, midi);
    bool passed = true;
    for (int offset = 0; offset < 24000;) {
        const int n = std::min(1 + (offset % 257), 24000 - offset);
        juce::AudioBuffer<float> block(2, n);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < n; ++f)
                block.setSample(c, f, source(offset + f, c));
        chunks.processBlock(block, midi);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < n; ++f)
                passed &= expect(block.getSample(c, f) == expected.getSample(c, offset + f),
                                 "same seed arbitrary host blocks exact");
        offset += n;
    }
    return passed;
}
bool renderEditor(const juce::File &destination) {
    ChaosProcessor p;
    p.loadFactoryPreset(2);
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
    bool passed = testState();
    passed &= testCoreLayoutsAndBypass();
    passed &= testSeedChunking();
    if (argc == 2)
        passed &= renderEditor(juce::File(juce::String::fromUTF8(argv[1])));
    return passed ? 0 : 1;
}
