#include "choiceEditor_test.h"
#include "quickFilterProcessor.h"

#include <cmath>
#include <iostream>
#include <juce_audio_utils/juce_audio_utils.h>
#include <limits>
#include <numbers>

namespace {
bool expect(bool condition, const char *message) {
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool nearValues(const QuickFilterProcessor::Values &left,
                const QuickFilterProcessor::Values &right) {
    for (size_t index = 0; index < left.size(); ++index)
        if (std::abs(left[index] - right[index]) > 0.00001F * std::max(1.0F, right[index]))
            return false;
    return true;
}

struct ParameterEvents final : juce::AudioProcessorParameter::Listener {
    void parameterValueChanged(int, float) override { ++values; }
    void parameterGestureChanged(int, bool) override { ++gestures; }
    int values = 0;
    int gestures = 0;
};

bool testStateAndUndo() {
    QuickFilterProcessor processor;
    const auto initial = processor.readValues();
    const QuickFilterProcessor::Values edited{1, 2400, 2.5F, 3, 0.4F};
    bool passed = expect(processor.applyUserValues(edited, "Test edit"), "apply parameters");
    const auto stored = processor.readValues();
    passed &= expect(nearValues(stored, edited), "parameters reach requested values");
    passed &= expect(processor.undo() && nearValues(processor.readValues(), initial),
                     "grouped preset undo restores all parameters");
    passed &= expect(processor.redo() && nearValues(processor.readValues(), stored),
                     "grouped preset redo restores all parameters");
    auto beforeDrag = processor.readValues();
    auto &parameters = processor.getParameters();
    auto *frequency = parameters.getParameter("frequency");
    auto *mix = parameters.getParameter("mix");
    frequency->setValueNotifyingHost(frequency->convertTo0to1(6000));
    mix->setValueNotifyingHost(mix->convertTo0to1(0.8F)); // Concurrent host automation.
    processor.recordUserEdit(beforeDrag, 1, "Frequency drag");
    ParameterEvents mixEvents;
    mix->addListener(&mixEvents);
    passed &= expect(processor.undo(), "undo explicit edited parameter");
    auto expected = stored;
    expected[4] = 0.8F;
    passed &= expect(nearValues(processor.readValues(), expected),
                     "undo leaves another parameter's automation untouched");
    passed &= expect(processor.redo(), "redo explicit edited parameter");
    passed &= expect(processor.readValues()[4] == expected[4],
                     "redo preserves another parameter's automation");
    passed &= expect(mixEvents.values == 0 && mixEvents.gestures == 0,
                     "undo and redo send no host edit events for an untouched parameter");
    mix->removeListener(&mixEvents);
    passed &= expect(processor.undo(), "restore frequency before preset checks");

    const auto preset = processor.createPresetXml();
    for (const auto &invalid :
         {juce::String("<bad/>"), preset.replace("version=\"1\"", "version=\"2\""),
          preset.replace("frequency=\"", "frequency=\"nan"), preset.replace(" q=", " unknown="),
          juce::String::repeatedString("x", 65537)}) {
        passed &= expect(!processor.loadPresetXml(invalid), "reject malformed preset");
        passed &= expect(nearValues(processor.readValues(), expected),
                         "invalid preset preserves parameters");
    }
    auto invalidValues = expected;
    invalidValues[2] = std::numeric_limits<float>::quiet_NaN();
    passed &= expect(!processor.applyUserValues(invalidValues, "Invalid"),
                     "reject nonfinite user values");
    for (const float mode : {-1.0F, 0.5F, 2.0F}) {
        invalidValues = expected;
        invalidValues[0] = mode;
        passed &= expect(!processor.applyUserValues(invalidValues, "Invalid mode") &&
                             nearValues(processor.readValues(), expected),
                         "choice validation rejects fractional and out-of-range modes atomically");
    }
    auto invalidPreset = juce::parseXML(preset);
    invalidPreset->setAttribute("slope", 1.5);
    passed &= expect(!processor.loadPresetXml(invalidPreset->toString()) &&
                         nearValues(processor.readValues(), expected),
                     "preset validation rejects fractional choices without changing state");

    juce::MemoryBlock state;
    processor.getStateInformation(state);
    QuickFilterProcessor restored;
    passed &= expect(restored.loadFactoryPreset(2) && restored.canUndo(), "make old history");
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    passed &= expect(nearValues(restored.readValues(), expected) && !restored.canUndo(),
                     "host state restores values and invalidates stale undo");
    const char malformed[] = "not a JUCE state";
    restored.setStateInformation(malformed, sizeof(malformed));
    restored.setStateInformation(nullptr, 10);
    passed &= expect(nearValues(restored.readValues(), expected), "bad host state is ignored");
    passed &= expect(restored.loadPresetXml(preset), "preset XML round trip");
    for (int program = 0; program < restored.getNumPrograms(); ++program) {
        restored.setCurrentProgram(program);
        passed &= expect(restored.getCurrentProgram() == program, "valid host program index");
    }
    const auto beforeInvalidProgram = restored.readValues();
    restored.setCurrentProgram(-1);
    restored.setCurrentProgram(restored.getNumPrograms());
    passed &= expect(nearValues(restored.readValues(), beforeInvalidProgram),
                     "out of range host programs are ignored");
    return passed;
}

bool testHostLifecycle() {
    QuickFilterProcessor processor;
    bool passed = true;
    juce::MidiBuffer midi;
    auto layout = processor.getBusesLayout();
    layout.inputBuses.set(0, juce::AudioChannelSet::mono());
    passed &= expect(!processor.isBusesLayoutSupported(layout), "reject asymmetric buses");
    for (int channels : {1, 2}) {
        layout.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        layout.outputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        passed &= expect(processor.setBusesLayout(layout), "accept mono and stereo layouts");
        passed &= expect(processor.applyUserValues({0, 1000, 0.7071F, 0, 0}, "Dry"), "set dry");
        for (double sampleRate : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0}) {
            processor.setRateAndBufferSizeDetails(sampleRate, 512);
            processor.prepareToPlay(sampleRate, 512);
            for (int size : {0, 1, 17, 512, 1025}) {
                juce::AudioBuffer<float> block(channels, size);
                for (int channel = 0; channel < channels; ++channel)
                    for (int frame = 0; frame < size; ++frame)
                        block.setSample(channel, frame, channel == 0 ? 0.125F : -0.25F);
                processor.processBlock(block, midi);
                for (int channel = 0; channel < channels; ++channel)
                    for (int frame = 0; frame < size; ++frame)
                        passed &= expect(block.getSample(channel, frame) ==
                                             (channel == 0 ? 0.125F : -0.25F),
                                         "dry output remains sample exact across host blocks");
            }
            processor.releaseResources();
        }
    }
    passed &= expect(processor.getLatencySamples() == 0, "filter reports zero latency");
    return passed;
}

bool testSpectrumTransfer() {
    QuickFilterProcessor processor;
    bool passed =
        expect(processor.applyUserValues({0, 1000, 0.7071F, 0, 0}, "Dry"), "spectrum dry");
    processor.prepareToPlay(48000, 2048);
    juce::AudioBuffer<float> block(2, 2048);
    juce::MidiBuffer midi;
    for (int frame = 0; frame < block.getNumSamples(); ++frame) {
        block.setSample(0, frame, 0.25F);
        block.setSample(1, frame, -0.25F);
    }
    std::array<float, 2048> left{}, right{};
    processor.processBlock(block, midi);
    passed &= expect(!processor.readSpectrum(left, right), "closed editor does not capture audio");
    processor.setSpectrumEnabled(true);
    processor.processBlock(block, midi);
    passed &= expect(!processor.readSpectrum(left, std::span<float>(right).first(10)),
                     "bad consumer sizes leave queued data intact");
    passed &= expect(processor.readSpectrum(left, right), "read captured stereo window");
    for (size_t frame = 0; frame < left.size(); ++frame)
        passed &= expect(left[frame] == 0.25F && right[frame] == -0.25F,
                         "antiphase channels remain separate in spectrum transfer");
    for (int repetition = 0; repetition < 10; ++repetition)
        processor.processBlock(block, midi); // Overflow drops data without blocking the producer.
    processor.setSpectrumEnabled(false);
    passed &= expect(!processor.readSpectrum(left, right), "closing editor discards queued audio");
    processor.prepareToPlay(96000, 2048);
    passed &=
        expect(processor.getSpectrumSampleRate() == 96000, "spectrum follows host sample rate");
    return passed;
}

bool testOfflineRender() {
    QuickFilterProcessor processor;
    bool passed =
        expect(processor.applyUserValues({0, 1000, 0.7071F, 1, 1}, "Render"), "set offline filter");
    processor.setNonRealtime(true);
    processor.setRateAndBufferSizeDetails(48000, 127);
    processor.prepareToPlay(48000, 127);
    juce::AudioBuffer<float> render(2, 48000);
    juce::MidiBuffer midi;
    for (int frame = 0; frame < render.getNumSamples(); ++frame) {
        const double time = frame / 48000.0;
        render.setSample(0, frame, float(0.5 * std::sin(2 * std::numbers::pi * 100 * time)));
        render.setSample(1, frame, float(0.5 * std::sin(2 * std::numbers::pi * 8000 * time)));
    }
    for (int offset = 0; offset < render.getNumSamples(); offset += 127) {
        const int count = std::min(127, render.getNumSamples() - offset);
        juce::AudioBuffer<float> block(render.getArrayOfWritePointers(), 2, offset, count);
        processor.processBlock(block, midi);
    }
    const auto lowRms = render.getRMSLevel(0, 24000, 24000);
    const auto highRms = render.getRMSLevel(1, 24000, 24000);
    passed &= expect(std::abs(lowRms - 0.353553F) < 0.001F && highRms < 0.0001F,
                     "offline render passes 100 Hz and rejects 8 kHz at 24 dB/oct");

    juce::TemporaryFile temporary(".wav");
    std::unique_ptr<juce::OutputStream> stream = temporary.getFile().createOutputStream();
    juce::WavAudioFormat format;
    const auto options =
        juce::AudioFormatWriterOptions()
            .withSampleRate(48000)
            .withNumChannels(2)
            .withBitsPerSample(32)
            .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format.createWriterFor(stream, options);
    if (!expect(writer != nullptr, "create offline WAV writer"))
        return false;
    passed &= expect(writer->writeFromAudioSampleBuffer(render, 0, render.getNumSamples()),
                     "write rendered audio");
    writer.reset();
    std::unique_ptr<juce::AudioFormatReader> reader(
        format.createReaderFor(temporary.getFile().createInputStream().release(), true));
    if (!expect(reader != nullptr, "read rendered WAV"))
        return false;
    passed &= expect(reader->numChannels == 2 && reader->sampleRate == 48000 &&
                         reader->lengthInSamples == render.getNumSamples(),
                     "rendered WAV retains layout, sample rate and duration");
    juce::AudioBuffer<float> readback(2, render.getNumSamples());
    passed &= expect(reader->read(&readback, 0, readback.getNumSamples(), 0, true, true),
                     "decode rendered WAV");
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < render.getNumSamples(); ++frame)
            passed &= expect(readback.getSample(channel, frame) == render.getSample(channel, frame),
                             "float WAV readback is sample exact");
    return passed;
}

bool renderEditor(const juce::File &destination) {
    QuickFilterProcessor processor;
    processor.prepareToPlay(48000, 2048);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorAndMakeActive());
    if (!expect(editor != nullptr, "create native editor"))
        return false;
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    editor->setVisible(true);
    const bool visible = expect(editor->isShowing(), "attach editor to native window");
    juce::AudioBuffer<float> block(2, 2048);
    juce::MidiBuffer midi;
    for (int iteration = 0; iteration < 8; ++iteration) {
        for (int frame = 0; frame < block.getNumSamples(); ++frame) {
            const double time = (iteration * 2048 + frame) / 48000.0;
            const float sample = float(0.25 * std::sin(2 * std::numbers::pi * 220 * time) +
                                       0.125 * std::sin(2 * std::numbers::pi * 1760 * time));
            block.setSample(0, frame, sample);
            block.setSample(1, frame, -sample);
        }
        processor.processBlock(block, midi);
        juce::Thread::sleep(40);
        juce::Timer::callPendingTimersSynchronously();
    }
    const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
    editor->setVisible(false);
    editor->removeFromDesktop();
    processor.editorBeingDeleted(editor.get());
    editor.reset();
    auto output = destination.createOutputStream();
    if (!expect(output != nullptr, "open editor snapshot"))
        return false;
    output->setPosition(0);
    output->truncate();
    juce::PNGImageFormat png;
    return expect(png.writeImageToStream(snapshot, *output), "write native editor snapshot") &&
           visible;
}
} // namespace

int main(int argc, char **argv) {
    juce::ScopedJuceInitialiser_GUI initialise;
    bool passed = testStateAndUndo();
    passed &=
        testChoiceEditor<QuickFilterProcessor>({{"Low pass", "mode"}, {"12 dB/oct", "slope"}});
    passed &= testHostLifecycle();
    passed &= testSpectrumTransfer();
    passed &= testOfflineRender();
    if (argc == 2)
        passed &= renderEditor(juce::File(juce::String::fromUTF8(argv[1])));
    return passed ? 0 : 1;
}
