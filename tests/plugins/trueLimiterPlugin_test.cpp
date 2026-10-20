#include "choiceEditor_test.h"
#include "trueLimiterProcessor.h"
#include "userPreset_test.h"
#include <cmath>
#include <iostream>
#include <numbers>

namespace {
bool expect(bool condition, const char *name) {
    if (!condition)
        std::cerr << "FAILED: " << name << '\n';
    return condition;
}
bool same(const TrueLimiterProcessor::Values &a, const TrueLimiterProcessor::Values &b) {
    for (size_t i = 0; i < a.size(); ++i)
        if (std::abs(a[i] - b[i]) > 0.0001F)
            return false;
    return true;
}
bool testState() {
    TrueLimiterProcessor p;
    const auto initial = p.readValues();
    auto edited = initial;
    edited[0] = -3;
    edited[1] = -23;
    edited[4] = 17;
    edited[6] = 2;
    bool passed = expect(p.applyUserValues(edited, "Edit") && same(p.readValues(), edited),
                         "parameter transaction");
    passed &= expect(p.undo() && same(p.readValues(), initial) && p.redo() &&
                         same(p.readValues(), edited),
                     "undo redo");
    const auto xml = p.createPresetXml();
    passed &= expect(!p.loadPresetXml(xml.replace("AHQTrueLimiterState", "AHQGhostDelayState")) &&
                         same(p.readValues(), edited),
                     "invalid preset unchanged");
    auto invalid = edited;
    invalid[6] = 1.5F;
    passed &= expect(!p.applyUserValues(invalid, "Invalid"), "invalid quality rejected");
    juce::MemoryBlock saved;
    p.getStateInformation(saved);
    TrueLimiterProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    passed &= expect(same(restored.readValues(), edited), "host state roundtrip");
    return passed;
}
bool testAudioAndBypass() {
    bool passed = true;
    for (int channels : {1, 2}) {
        TrueLimiterProcessor p;
        auto layout = p.getBusesLayout();
        layout.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        layout.outputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        passed &= expect(p.setBusesLayout(layout), "mono stereo layouts");
        p.prepareToPlay(48000, 257);
        passed &= expect(p.getLatencySamples() == 984, "constant maximum latency");
        ahq::audio::TrueLimiter reference;
        auto values = p.readValues();
        passed &= expect(reference.prepare(48000, static_cast<unsigned>(channels), 20, 8),
                         "prepare reference");
        (void)reference.setLookaheadMs(5);
        reference.reset();
        std::array<std::vector<float>, 2> dryHistory;
        for (auto &v : dryHistory)
            v.assign(984, 0.0F);
        size_t position = 0;
        juce::MidiBuffer midi;
        for (int iteration = 0; iteration < 60; ++iteration) {
            const int count = iteration % 3 == 0 ? 31 : 257;
            juce::AudioBuffer<float> buffer(channels, count), expected(channels, count);
            std::array<std::vector<float>, 2> dry;
            for (auto &v : dry)
                v.resize(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                for (int c = 0; c < channels; ++c) {
                    const float sample = static_cast<float>(
                        1.4 * std::sin((iteration * 257 + i) * 0.37) * (c == 0 ? 1 : 0.5));
                    buffer.setSample(c, i, sample);
                    expected.setSample(c, i, sample);
                    dry[static_cast<size_t>(c)][static_cast<size_t>(i)] =
                        dryHistory[static_cast<size_t>(c)][position];
                    dryHistory[static_cast<size_t>(c)][position] = sample;
                }
                position = (position + 1) % 984;
            }
            if (iteration == 20) {
                values[4] = 0;
                values[6] = 0;
                passed &= expect(p.applyUserValues(values, "Change structure"),
                                 "real-time host controls");
                (void)reference.setLookaheadMs(0);
                (void)reference.setOversamplingFactor(4);
            }
            (void)reference.process({expected.getWritePointer(0), static_cast<size_t>(count)},
                                    channels == 2 ? std::span<float>(expected.getWritePointer(1),
                                                                     static_cast<size_t>(count))
                                                  : std::span<float>{});
            const bool bypassed = iteration % 4 == 1;
            if (bypassed)
                p.processBlockBypassed(buffer, midi);
            else
                p.processBlock(buffer, midi);
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < count; ++i)
                    passed &=
                        expect(buffer.getSample(c, i) ==
                                   (bypassed ? dry[static_cast<size_t>(c)][static_cast<size_t>(i)]
                                             : expected.getSample(c, i)),
                               "exact core output and delayed bypass with warm history");
            passed &= expect(p.getLatencySamples() == 984, "parameter changes preserve latency");
        }
    }
    return passed;
}
bool waitForAnalysis(TrueLimiterProcessor &p) {
    for (int i = 0; i < 1000; ++i) {
        if (!p.fileAnalysis().running)
            return true;
        juce::Thread::sleep(10);
    }
    p.cancelFileAnalysis();
    return false;
}
bool testFileAnalysis() {
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("ahq-limiter-" + juce::Uuid().toString() + ".wav");
    juce::AudioBuffer<float> audio(1, 160000);
    for (int i = 0; i < audio.getNumSamples(); ++i)
        audio.setSample(
            0, i, static_cast<float>(0.02 * std::sin(2.0 * std::numbers::pi * 1000 * i / 32000)));
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    auto writer = format.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions{}.withSampleRate(32000).withNumChannels(1).withBitsPerSample(
            24));
    if (!expect(writer != nullptr, "create actual file fixture"))
        return false;
    bool passed = expect(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()),
                         "write PCM fixture");
    writer.reset();
    TrueLimiterProcessor p;
    auto snapshot = p.readValues();
    snapshot[1] = -23;
    snapshot[4] = 11;
    snapshot[5] = 230;
    snapshot[6] = 2;
    passed &= expect(p.applyUserValues(snapshot, "Analysis settings"), "configure file analysis");
    p.startFileAnalysis(file);
    passed &= expect(waitForAnalysis(p), "file worker completes");
    const auto result = p.fileAnalysis();
    passed &= expect(result.valid && result.targetReached && !result.peakLimited &&
                         std::abs(result.outputLufs + 23) <= 0.1 && result.gainDb > 0,
                     "two-pass actual rendered integrated result");
    auto changed = snapshot;
    changed[0] = -20;
    changed[2] = 1;
    changed[4] = 0;
    changed[6] = 0;
    passed &= expect(p.applyUserValues(changed, "Edit after analysis") && p.applyFileAnalysis(),
                     "apply saved analyzed settings");
    auto applied = snapshot;
    applied[2] = 0;
    applied[3] = static_cast<float>(result.gainDb);
    applied[7] = 1;
    passed &=
        expect(same(p.readValues(), applied), "Apply restores all rendering choices and gain");
    passed &= expect(p.undo() && same(p.readValues(), changed) && p.redo() &&
                         same(p.readValues(), applied),
                     "Apply is one undo transaction");
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(0, juce::AudioChannelSet::mono());
    layout.outputBuses.set(0, juce::AudioChannelSet::mono());
    passed &= expect(p.setBusesLayout(layout), "matching analyzed channel layout");
    p.prepareToPlay(32000, 4096);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> readback(formats.createReaderFor(file));
    passed &= expect(readback != nullptr, "read actual analyzed PCM back");
    if (readback) {
        ahq::audio::LoudnessMeter actualMeter;
        ahq::audio::IntegratedLoudness actualIntegrated;
        ahq::audio::TruePeakMeter actualPeak;
        passed &= expect(actualMeter.prepare(32000, 1) && actualPeak.prepare(32000, 1),
                         "prepare independent plugin output measurements");
        const int latency = p.getLatencySamples();
        juce::MidiBuffer midi;
        for (int offset = 0; offset < audio.getNumSamples() + latency; offset += 4096) {
            const int count = std::min(4096, audio.getNumSamples() + latency - offset);
            juce::AudioBuffer<float> block(1, count);
            block.clear();
            const int sourceCount = std::max(0, std::min(count, audio.getNumSamples() - offset));
            if (sourceCount > 0)
                passed &= expect(readback->read(&block, 0, sourceCount, offset, true, false),
                                 "read waveform for actual plugin playback");
            p.processBlock(block, midi);
            (void)actualPeak.process({block.getReadPointer(0), static_cast<size_t>(count)});
            for (int i = 0; i < count; ++i) {
                const int aligned = offset + i - latency;
                if (aligned < 0 || aligned >= audio.getNumSamples())
                    continue;
                ahq::audio::LoudnessBlock completed;
                (void)actualMeter.processSample(block.getSample(0, i), 0, completed);
                if (completed.ready)
                    (void)actualIntegrated.addBlock(completed.energy);
            }
        }
        passed &= expect(actualIntegrated.reading().valid &&
                             std::abs(actualIntegrated.reading().lufs - result.outputLufs) < 0.01,
                         "applied actual processBlock output matches file-worker render");
        passed &=
            expect(std::isfinite(p.outputShortTermLufs()) &&
                       std::abs(p.outputTruePeakDb() - actualPeak.reading().truePeakDb()) < 0.0001,
                   "actual output S and since-prepare TP snapshots");
    }
    snapshot[0] = -20;
    snapshot[1] = -5;
    p.applyUserValues(snapshot, "Limited file target");
    p.startFileAnalysis(file);
    passed &= expect(waitForAnalysis(p), "limited worker completes");
    const auto limited = p.fileAnalysis();
    passed &= expect(limited.valid && !limited.targetReached && limited.peakLimited &&
                         limited.outputLufs < -5.1,
                     "file result reports real protection and unreached target");
    p.startFileAnalysis(file);
    p.cancelFileAnalysis();
    passed &=
        expect(p.fileAnalysis().cancelled && !p.fileAnalysis().running && !p.applyFileAnalysis(),
               "worker cancellation result");
    p.startFileAnalysis(file);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        editor.reset(); // Worker result cannot retain an editor pointer.
    }
    passed &= expect(waitForAnalysis(p), "analysis safely outlives editor");
    p.cancelFileAnalysis();
    readback.reset();
    file.deleteFile();
    return passed;
}
bool renderEditor(const juce::File &destination) {
    TrueLimiterProcessor p;
    p.prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditorAndMakeActive());
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    editor->setVisible(true);
    juce::Thread::sleep(40);
    juce::Timer::callPendingTimersSynchronously();
    juce::MidiBuffer midi;
    for (int blockIndex = 0; blockIndex < 350; ++blockIndex) {
        juce::AudioBuffer<float> audio(2, 512);
        for (int i = 0; i < 512; ++i) {
            const int frame = blockIndex * 512 + i;
            audio.setSample(
                0, i,
                static_cast<float>(0.7 * std::sin(2.0 * std::numbers::pi * 997 * frame / 48000)));
            audio.setSample(
                1, i,
                static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 1499 * frame / 48000)));
        }
        p.processBlock(audio, midi);
    }
    juce::Thread::sleep(40);
    juce::Timer::callPendingTimersSynchronously();
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
    editor->setVisible(false);
    editor->removeFromDesktop();
    p.editorBeingDeleted(editor.get());
    editor.reset();
    auto stream = destination.createOutputStream();
    if (!stream)
        return false;
    stream->setPosition(0);
    stream->truncate();
    juce::PNGImageFormat png;
    return png.writeImageToStream(image, *stream);
}
} // namespace
int main(int argc, char **argv) {
    juce::ScopedJuceInitialiser_GUI initialise;
    if (!testUserPresetLibrary<TrueLimiterProcessor>())
        return 1;
    const bool choicesPassed = testChoiceEditor<TrueLimiterProcessor>({{"4x", "quality"}});
    bool passed = testState();
    passed &= testAudioAndBypass();
    passed &= testFileAnalysis();
    if (argc == 2)
        passed &= renderEditor(juce::File(juce::String::fromUTF8(argv[1])));
    return passed && choicesPassed ? 0 : 1;
}
