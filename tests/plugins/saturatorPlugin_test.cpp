#include "choiceEditor_test.h"
#include "saturatorProcessor.h"
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
bool nearValues(const SaturatorProcessor::Values &a, const SaturatorProcessor::Values &b) {
    for (size_t index = 0; index < a.size(); ++index)
        if (std::abs(a[index] - b[index]) > 0.0001F)
            return false;
    return true;
}
bool testState() {
    SaturatorProcessor p;
    const auto initial = p.readValues();
    const SaturatorProcessor::Values edited{2, 12, 0.25F, -0.3F, 2, 0.6F};
    bool passed = expect(p.applyUserValues(edited, "Edit"), "apply parameters");
    passed &= expect(nearValues(p.readValues(), edited), "parameters preserved");
    passed &= expect(p.undo() && nearValues(p.readValues(), initial), "single transaction undo");
    passed &= expect(p.redo() && nearValues(p.readValues(), edited), "redo");
    auto *drive = p.getParameters().getParameter("drive");
    auto *mix = p.getParameters().getParameter("mix");
    drive->setValueNotifyingHost(drive->convertTo0to1(8));
    mix->setValueNotifyingHost(mix->convertTo0to1(0.25F));
    p.recordUserEdit(edited, 1, "Drag drive");
    auto expected = edited;
    expected[5] = 0.25F;
    ParameterEvents mixEvents;
    mix->addListener(&mixEvents);
    passed &= expect(p.undo() && nearValues(p.readValues(), expected),
                     "drag undo preserves other automation");
    passed &= expect(mixEvents.values == 0 && mixEvents.gestures == 0,
                     "drive undo sends no value or gesture events to unedited mix");
    passed &= expect(p.redo(), "redo drive drag");
    passed &= expect(mixEvents.values == 0 && mixEvents.gestures == 0,
                     "drive redo sends no value or gesture events to unedited mix");
    passed &= expect(p.undo(), "return drive to edited value");
    mix->removeListener(&mixEvents);
    const auto xml = p.createPresetXml();
    for (const auto &bad :
         {juce::String("<bad/>"), xml.replace("version=\"1\"", "version=\"9\""),
          xml.replace("drive=\"", "drive=\"nan"), xml.replace(" oversampling=", " unknown="),
          xml.replace("AHQSaturatorState", "AHQQuickFilterState"),
          juce::String::repeatedString("x", 65537)}) {
        passed &= expect(!p.loadPresetXml(bad) && nearValues(p.readValues(), expected),
                         "invalid preset unchanged");
    }
    for (size_t index = 0; index < 6; ++index) {
        auto bad = expected;
        bad[index] = std::numeric_limits<float>::infinity();
        passed &= expect(!p.applyUserValues(bad, "Invalid"), "nonfinite parameter rejected");
    }
    juce::MemoryBlock saved;
    p.getStateInformation(saved);
    SaturatorProcessor restored;
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
    return float(std::sin(frame * 0.037) * (channel == 0 ? 0.35 : -0.2));
}
bool testDryAndSwitching() {
    bool passed = true;
    for (int quality = 0; quality < 3; ++quality) {
        SaturatorProcessor p;
        p.applyUserValues({4, 24, 0.5F, -0.5F, float(quality), 0}, "Dry");
        p.prepareToPlay(48000, 17);
        passed &= expect(p.getLatencySamples() == 24, "all quality modes report 24 samples");
        juce::MidiBuffer midi;
        int offset = 0;
        for (int blockIndex = 0; blockIndex < 150; ++blockIndex) {
            const int frames = blockIndex % 7 == 0 ? 0 : 1 + blockIndex % 29;
            auto *parameter = p.getParameters().getParameter("oversampling");
            parameter->setValueNotifyingHost(parameter->convertTo0to1(float(blockIndex % 3)));
            juce::AudioBuffer<float> block(2, frames);
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < frames; ++f)
                    block.setSample(c, f, source(offset + f, c));
            p.processBlock(block, midi);
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < frames; ++f)
                    passed &= expect(block.getSample(c, f) ==
                                         (offset + f < 24 ? 0 : source(offset + f - 24, c)),
                                     "dry stream exact across blocks and quality requests");
            passed &= expect(p.getLatencySamples() == 24,
                             "quality automation never changes host latency");
            offset += frames;
        }
    }
    SaturatorProcessor tiny;
    tiny.applyUserValues({0, 12, 0, 0, 0, 0}, "Tiny dry");
    tiny.prepareToPlay(48000, 1);
    juce::MidiBuffer midi;
    for (int frame = 0; frame < 1000; ++frame) {
        auto *quality = tiny.getParameters().getParameter("oversampling");
        quality->setValueNotifyingHost(quality->convertTo0to1(float((frame + 1) % 3)));
        juce::AudioBuffer<float> block(1, 1);
        block.setSample(0, 0, source(frame, 0));
        tiny.processBlock(block, midi);
        passed &= expect(block.getSample(0, 0) == (frame < 24 ? 0 : source(frame - 24, 0)),
                         "one-frame quality requests retain exact delayed dry");
    }
    passed &= expect(tiny.getQualityPreparationCount() <= 6,
                     "one-frame repeated requests prepare at most once per completed 5ms fade");
    return passed;
}
bool testBypass() {
    SaturatorProcessor p;
    p.applyUserValues({5, 24, 0.3F, 0.5F, 2, 1}, "Wet");
    p.prepareToPlay(48000, 31);
    juce::MidiBuffer midi;
    bool passed = true;
    for (int blockIndex = 0; blockIndex < 100; ++blockIndex) {
        juce::AudioBuffer<float> block(2, 31);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < 31; ++f)
                block.setSample(c, f, source(blockIndex * 31 + f, c));
        if (blockIndex % 2 == 0)
            p.processBlockBypassed(block, midi);
        else
            p.processBlock(block, midi);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < 31; ++f) {
                const int frame = blockIndex * 31 + f;
                if (blockIndex % 2 == 0)
                    passed &=
                        expect(block.getSample(c, f) == (frame < 24 ? 0 : source(frame - 24, c)),
                               "host bypass uses continuously advanced 24sample dry history");
                else
                    passed &= expect(std::isfinite(block.getSample(c, f)),
                                     "resume wet has valid histories");
            }
    }
    return passed;
}
double curve(double input, int model) {
    switch (model) {
    case 0:
        return std::tanh(input);
    case 1:
        return std::clamp(input, -1.0, 1.0);
    case 2:
        return input >= 0 ? std::tanh(1.5 * input) / 1.5 : std::tanh(0.75 * input) / 0.75;
    case 3:
        return input / std::sqrt(1 + input * input);
    case 4: {
        double phase = std::fmod(input + 1, 4);
        if (phase < 0)
            phase += 4;
        return phase <= 2 ? phase - 1 : 3 - phase;
    }
    case 5:
        return std::min(std::abs(input), 1.0);
    case 6:
        return std::round(std::clamp(input, -1.0, 1.0) * 127) / 127;
    }
    return 0;
}
bool testCurvesAndAlignment() {
    bool passed = true;
    for (int model = 0; model < 7; ++model) {
        SaturatorProcessor p;
        p.applyUserValues({float(model), 12, 0, 0, 0, 1}, "Curve");
        p.prepareToPlay(48000, 97);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> block(2, 97);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < 97; ++f)
                block.setSample(c, f, source(f, c));
        p.processBlock(block, midi);
        const double gain = std::pow(10.0, p.readValues()[1] / 20.0);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < 97; ++f)
                passed &= expect(std::abs(block.getSample(c, f) -
                                          (f < 24 ? 0 : curve(source(f - 24, c) * gain, model))) <
                                     0.00001,
                                 "actual 1x known curve output with 24sample padding");
    }
    for (int quality = 0; quality < 3; ++quality) {
        SaturatorProcessor p;
        p.applyUserValues({3, 9, 0.1F, 0, float(quality), 1}, "Alignment");
        p.prepareToPlay(48000, 1024);
        juce::MidiBuffer midi;
        ahq::audio::Saturator reference;
        passed &=
            expect(reference.prepare(48000, static_cast<ahq::audio::OversamplingFactor>(quality)),
                   "reference prepared");
        const auto values = p.readValues();
        passed &= expect(
            reference.setParameters({ahq::audio::SaturatorModel::tape, values[1], values[2], 0, 1}),
            "reference configured");
        reference.reset();
        juce::AudioBuffer<float> block(2, 1024), expected(2, 1024);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < 1024; ++f) {
                block.setSample(c, f, source(f, c));
                expected.setSample(c, f, source(f, c));
            }
        p.processBlock(block, midi);
        passed &= expect(reference.process({expected.getWritePointer(0), 1024},
                                           {expected.getWritePointer(1), 1024}),
                         "reference waveform processed");
        const int padding = 24 - int(reference.getLatencySamples());
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < 1024; ++f)
                passed &= expect(std::abs(block.getSample(c, f) -
                                          (f < padding ? 0 : expected.getSample(c, f - padding))) <
                                     0.000001F,
                                 "each quality aligns wet core output to constant latency");
    }
    return passed;
}
bool testRangesAndHost() {
    SaturatorProcessor p;
    bool passed = true;
    const auto initial = p.readValues();
    for (const auto &invalid : {SaturatorProcessor::Values{-1, 0, 0, 0, 1, 1},
                                {7, 0, 0, 0, 1, 1},
                                {0, 37, 0, 0, 1, 1},
                                {0, -1, 0, 0, 1, 1},
                                {0, 0, 1.1F, 0, 1, 1},
                                {0, 0, 0, -1.1F, 1, 1},
                                {0, 0, 0, 0, 3, 1},
                                {0, 0, 0, 0, 0.5F, 1},
                                {0, 0, 0, 0, 1, 1.1F}})
        passed &=
            expect(!p.applyUserValues(invalid, "Invalid") && nearValues(p.readValues(), initial),
                   "range/model invalid unchanged");
    passed &= expect(!p.acceptsMidi() && !p.producesMidi() && p.getLatencySamples() == 24,
                     "host MIDI and fixed latency");
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(0, juce::AudioChannelSet::mono());
    passed &= expect(!p.isBusesLayoutSupported(layout), "reject asymmetric buses");
    for (int channels : {1, 2}) {
        layout.outputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        layout.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        passed &= expect(p.setBusesLayout(layout), "support mono/stereo");
        for (double rate : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0}) {
            p.prepareToPlay(rate, 32);
            juce::AudioBuffer<float> empty(channels, 0);
            juce::MidiBuffer midi;
            p.processBlock(empty, midi);
            p.releaseResources();
        }
    }
    return passed;
}
bool testWetQualityTransition() {
    constexpr int totalFrames = 4096, switchFrame = 1536, fadeFrames = 240;
    juce::AudioBuffer<float> oldReference(2, totalFrames), newReference(2, totalFrames);
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < totalFrames; ++frame) {
            const float sample = source(frame, channel);
            oldReference.setSample(channel, frame, sample);
            newReference.setSample(channel, frame, sample);
        }
    ahq::audio::Saturator oldCore, newCore;
    bool passed = expect(oldCore.prepare(48000, ahq::audio::OversamplingFactor::x1) &&
                             newCore.prepare(48000, ahq::audio::OversamplingFactor::x4),
                         "warm quality references prepared");
    const ahq::audio::SaturatorParameters parameters{ahq::audio::SaturatorModel::hardClip, 24, 0, 0,
                                                     1};
    passed &= expect(oldCore.setParameters(parameters) && newCore.setParameters(parameters),
                     "warm quality references configured");
    oldCore.reset();
    newCore.reset();
    passed &= expect(oldCore.process({oldReference.getWritePointer(0), totalFrames},
                                     {oldReference.getWritePointer(1), totalFrames}) &&
                         newCore.process({newReference.getWritePointer(0), totalFrames},
                                         {newReference.getWritePointer(1), totalFrames}),
                     "independent references process the entire input history");
    // Both partitions place the automation at exactly switchFrame. They cross the fade boundary
    // differently, checking history replay order, latency alignment, and cross-block fade position.
    for (const auto &partition :
         {std::array<int, 4>{1, 17, 63, 97}, std::array<int, 4>{511, 3, 29, 257}}) {
        SaturatorProcessor processor;
        processor.applyUserValues({1, 24, 0, 0, 0, 1}, "Warm 1x");
        processor.prepareToPlay(48000, 511);
        juce::MidiBuffer midi;
        int offset = 0, blockIndex = 0;
        while (offset < totalFrames) {
            int frames = std::min(partition[size_t(blockIndex++ % 4)], totalFrames - offset);
            if (offset < switchFrame)
                frames = std::min(frames, switchFrame - offset);
            if (offset == switchFrame) {
                auto *quality = processor.getParameters().getParameter("oversampling");
                quality->setValueNotifyingHost(quality->convertTo0to1(2));
            }
            juce::AudioBuffer<float> block(2, frames);
            for (int channel = 0; channel < 2; ++channel)
                for (int frame = 0; frame < frames; ++frame)
                    block.setSample(channel, frame, source(offset + frame, channel));
            processor.processBlock(block, midi);
            for (int channel = 0; channel < 2; ++channel)
                for (int frame = 0; frame < frames; ++frame) {
                    const int absolute = offset + frame;
                    const float oldWet =
                        absolute < 24 ? 0 : oldReference.getSample(channel, absolute - 24);
                    const float newWet = newReference.getSample(channel, absolute);
                    const double alpha =
                        absolute < switchFrame
                            ? 0
                            : std::min(1.0, double(absolute - switchFrame + 1) / fadeFrames);
                    const float expected = float(oldWet + (newWet - oldWet) * alpha);
                    passed &=
                        expect(std::abs(block.getSample(channel, frame) - expected) < 0.000001F,
                               "wet quality fade equals aligned continuously warm references");
                }
            offset += frames;
        }
        passed &= expect(processor.getQualityPreparationCount() == 2,
                         "one wet quality change prepares exactly one inactive engine");
    }
    return passed;
}
bool renderEditor(const juce::File &destination) {
    SaturatorProcessor p;
    p.loadFactoryPreset(3);
    p.prepareToPlay(48000, 2048);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditorAndMakeActive());
    if (!expect(editor != nullptr, "native editor"))
        return false;
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    editor->setVisible(true);
    bool passed = expect(editor->isShowing(), "visible native window");
    juce::MidiBuffer midi;
    for (int iteration = 0; iteration < 10; ++iteration) {
        juce::AudioBuffer<float> block(2, 2048);
        for (int f = 0; f < 2048; ++f) {
            const float sample = source(iteration * 2048 + f, 0);
            block.setSample(0, f, sample);
            block.setSample(1, f, -sample);
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
    passed &= expect(png.writeImageToStream(snapshot, *out), "snapshot PNG");
    return passed;
}
} // namespace
int main(int argc, char **argv) {
    juce::ScopedJuceInitialiser_GUI initialise;
    if (!testUserPresetLibrary<SaturatorProcessor>())
        return 1;
    bool passed = testState();
    passed &=
        testChoiceEditor<SaturatorProcessor>({{"Soft clip", "model"}, {"1x", "oversampling"}});
    passed &= testRangesAndHost();
    passed &= testDryAndSwitching();
    passed &= testBypass();
    passed &= testCurvesAndAlignment();
    passed &= testWetQualityTransition();
    if (argc == 2)
        passed &= renderEditor(juce::File(juce::String::fromUTF8(argv[1])));
    return passed ? 0 : 1;
}
