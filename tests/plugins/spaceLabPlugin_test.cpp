#include "spaceLabProcessor.h"
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
bool nearValues(const SpaceLabProcessor::Values &a, const SpaceLabProcessor::Values &b) {
    for (size_t index = 0; index < a.size(); ++index)
        if (std::abs(a[index] - b[index]) > 0.0001F)
            return false;
    return true;
}
bool testState() {
    SpaceLabProcessor p;
    const auto initial = p.readValues();
    const SpaceLabProcessor::Values edited{2, 15, -8, 7, 1, 0, 2, 18, 1, 0.7F};
    bool passed = expect(p.applyUserValues(edited, "Edit"), "apply parameters");
    passed &= expect(nearValues(p.readValues(), edited), "parameters preserved");
    passed &= expect(p.undo() && nearValues(p.readValues(), initial), "single transaction undo");
    passed &= expect(p.redo() && nearValues(p.readValues(), edited), "redo");
    auto *width = p.getParameters().getParameter("width");
    auto *mix = p.getParameters().getParameter("mix");
    width->setValueNotifyingHost(width->convertTo0to1(1.5F));
    mix->setValueNotifyingHost(mix->convertTo0to1(0.25F));
    p.recordUserEdit(edited, 0, "Drag width");
    auto expected = edited;
    expected[9] = 0.25F;
    ParameterEvents mixEvents;
    mix->addListener(&mixEvents);
    passed &= expect(p.undo() && nearValues(p.readValues(), expected),
                     "drag undo preserves other automation");
    passed &= expect(mixEvents.values == 0 && mixEvents.gestures == 0,
                     "width undo sends no value or gesture events to unedited mix");
    passed &= expect(p.redo(), "redo width drag");
    passed &= expect(mixEvents.values == 0 && mixEvents.gestures == 0,
                     "width redo sends no value or gesture events to unedited mix");
    passed &= expect(p.undo(), "return width to edited value");
    mix->removeListener(&mixEvents);
    const auto xml = p.createPresetXml();
    for (const auto &bad :
         {juce::String("<bad/>"), xml.replace("version=\"1\"", "version=\"9\""),
          xml.replace("width=\"", "width=\"nan"), xml.replace(" split=", " unknown="),
          xml.replace("AHQSpaceLabState", "AHQQuickFilterState"),
          juce::String::repeatedString("x", 65537)}) {
        passed &= expect(!p.loadPresetXml(bad) && nearValues(p.readValues(), expected),
                         "invalid preset unchanged");
    }
    for (size_t index = 0; index < 10; ++index) {
        auto bad = expected;
        bad[index] = std::numeric_limits<float>::infinity();
        passed &= expect(!p.applyUserValues(bad, "Invalid"), "nonfinite parameter rejected");
    }
    juce::MemoryBlock saved;
    p.getStateInformation(saved);
    SpaceLabProcessor restored;
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
ahq::audio::SpaceLabParameters coreParameters(const SpaceLabProcessor::Values &v) {
    return {v[0], v[1], v[2], v[3], v[4] == 1, v[5] == 1, v[6], v[7], v[8] == 1, v[9]};
}
bool testCoreLayoutsAndBypass() {
    SpaceLabProcessor p;
    constexpr std::array<const char *, 10> ids{"width",      "haas",        "leftTilt", "rightTilt",
                                               "invertLeft", "invertRight", "modRate",  "modDepth",
                                               "split",      "mix"};
    bool passed = expect(p.getLatencySamples() == 0 && p.getTailLengthSeconds() > 0 &&
                             p.getTailLengthSeconds() < 1,
                         "zero latency and nonzero short estimated IIR tail");
    for (const auto *id : ids)
        passed &= expect(p.getParameters().getParameter(id) != nullptr, "stable parameter ID");
    auto layout = p.getBusesLayout();
    layout.outputBuses.set(0, juce::AudioChannelSet::mono());
    passed &= expect(!p.isBusesLayoutSupported(layout), "reject mono output");
    layout.outputBuses.set(0, juce::AudioChannelSet::stereo());
    juce::MidiBuffer midi;
    for (int inputs : {1, 2}) {
        layout.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(inputs));
        passed &= expect(p.setBusesLayout(layout), "mono or stereo input to stereo output");
        SpaceLabProcessor::Values values{2, 12, -5, 8, 1, 0, 2, 12, 1, 0.7F};
        passed &= expect(p.applyUserValues(values, "Core reference"), "apply actual controls");
        p.prepareToPlay(48000, 257);
        ahq::audio::SpaceLab reference;
        passed &= expect(reference.prepare(48000) &&
                             reference.setParameters(coreParameters(p.readValues())),
                         "configure core reference");
        reference.reset();
        int offset = 0;
        for (int iteration = 0; iteration < 25; ++iteration) {
            juce::AudioBuffer<float> block(2, 257), expected(2, 257);
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < 257; ++f) {
                    const float original = source(offset + f, inputs == 1 ? 0 : c);
                    expected.setSample(c, f, original);
                    block.setSample(c, f, inputs == 1 && c == 1 ? 999.0F : original);
                }
            passed &= expect(reference.process({expected.getWritePointer(0), 257},
                                               {expected.getWritePointer(1), 257}),
                             "core reference process");
            const bool bypass = iteration % 3 == 1;
            if (bypass)
                p.processBlockBypassed(block, midi);
            else
                p.processBlock(block, midi);
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < 257; ++f)
                    passed &= expect(block.getSample(c, f) ==
                                         (bypass ? source(offset + f, inputs == 1 ? 0 : c)
                                                 : expected.getSample(c, f)),
                                     "routing core audio and bypass state continuity");
            offset += 257;
        }
        values[9] = 0;
        p.applyUserValues(values, "Dry");
        p.prepareToPlay(48000, 257);
        juce::AudioBuffer<float> dry(2, 257);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < 257; ++f)
                dry.setSample(c, f, source(f, inputs == 1 ? 0 : c));
        p.processBlock(dry, midi);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < 257; ++f)
                passed &= expect(dry.getSample(c, f) == source(f, inputs == 1 ? 0 : c),
                                 "dry exact routed input");
    }
    return passed;
}
bool testSplitControls() {
    SpaceLabProcessor p;
    p.loadFactoryPreset(2);
    p.prepareToPlay(48000, 2048);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditorAndMakeActive());
    if (!expect(editor != nullptr, "native editor"))
        return false;
    const auto stored = p.readValues();
    for (const auto *id : {"width", "leftTilt", "invertLeft"}) {
        auto *control = editor->findChildWithID(id);
        if (!expect(control != nullptr && !control->isEnabled(),
                    "split disables ineffective control"))
            return false;
    }
    if (!expect(p.readValues() == stored, "split GUI does not overwrite preserved controls"))
        return false;
    p.editorBeingDeleted(editor.get());
    return true;
}
bool renderEditor(const juce::File &destination) {
    SpaceLabProcessor p;
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
    passed &= testSplitControls();
    if (argc == 2)
        passed &= renderEditor(juce::File(juce::String::fromUTF8(argv[1])));
    return passed ? 0 : 1;
}
