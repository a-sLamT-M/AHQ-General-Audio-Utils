#include "choiceEditor_test.h"
#include "orbitProcessor.h"
#include "userPreset_test.h"
#include <cmath>
#include <iostream>
#include <juce_audio_utils/juce_audio_utils.h>
#include <limits>

namespace {
bool expect(bool condition, const char *message) {
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
bool nearValues(const OrbitProcessor::Values &a, const OrbitProcessor::Values &b) {
    for (size_t index = 0; index < a.size(); ++index)
        if (std::abs(a[index] - b[index]) > 0.0001F)
            return false;
    return true;
}
struct ParameterEvents final : juce::AudioProcessorParameter::Listener {
    void parameterValueChanged(int, float) override { ++values; }
    void parameterGestureChanged(int, bool) override { ++gestures; }
    int values = 0, gestures = 0;
};
struct PlayHead final : juce::AudioPlayHead {
    juce::Optional<PositionInfo> getPosition() const override {
        ++calls;
        if (!hasPosition)
            return {};
        PositionInfo info;
        info.setBpm(bpm);
        return info;
    }
    bool hasPosition = true;
    juce::Optional<double> bpm{240.0};
    mutable int calls = 0;
};
float signal(int frame, int channel) {
    return float(std::sin(frame * 0.039) * (channel == 0 ? 0.3 : 0.15));
}
void fill(juce::AudioBuffer<float> &block, int offset, bool mono = false) {
    for (int f = 0; f < block.getNumSamples(); ++f) {
        block.setSample(0, f, signal(offset + f, 0));
        block.setSample(1, f, mono ? 0 : signal(offset + f, 1));
    }
}
bool testState() {
    auto owner = std::make_unique<OrbitProcessor>();
    auto &p = *owner;
    const auto initial = p.readValues();
    const OrbitProcessor::Values edited{0.75F, 1, 140, 8, 1, 0.7F, 120, 0.4F};
    bool passed = expect(p.applyUserValues(edited, "Edit") && nearValues(p.readValues(), edited),
                         "eight parameters apply");
    passed &= expect(p.undo() && nearValues(p.readValues(), initial) && p.redo() &&
                         nearValues(p.readValues(), edited),
                     "group undo redo");
    auto *rate = p.getParameters().getParameter("rate");
    auto *mix = p.getParameters().getParameter("mix");
    rate->setValueNotifyingHost(rate->convertTo0to1(2));
    mix->setValueNotifyingHost(mix->convertTo0to1(0.8F));
    p.recordUserEdit(edited, 0, "Rate");
    ParameterEvents events;
    mix->addListener(&events);
    auto expected = edited;
    expected[7] = 0.8F;
    passed &= expect(p.undo() && nearValues(p.readValues(), expected) && events.values == 0 &&
                         events.gestures == 0,
                     "single rate undo leaves mix values and events alone");
    mix->removeListener(&events);
    const auto xml = p.createPresetXml();
    for (const auto &bad : {juce::String("<bad/>"), xml.replace("version=\"1\"", "version=\"2\""),
                            xml.replace("rate=\"", "rate=\"nan"), xml.replace(" bpm=", " unknown="),
                            xml.replace("AHQOrbitState", "AHQSaturatorState"),
                            juce::String::repeatedString("x", 65537)})
        passed &= expect(!p.loadPresetXml(bad) && nearValues(p.readValues(), expected),
                         "bad preset preserves all parameters");
    for (size_t index = 0; index < 8; ++index) {
        auto invalid = expected;
        invalid[index] = std::numeric_limits<float>::quiet_NaN();
        passed &= expect(!p.applyUserValues(invalid, "Invalid"), "nonfinite state rejected");
    }
    juce::MemoryBlock saved;
    p.getStateInformation(saved);
    auto restored = std::make_unique<OrbitProcessor>();
    restored->loadFactoryPreset(2);
    restored->setStateInformation(saved.getData(), int(saved.getSize()));
    passed &= expect(nearValues(restored->readValues(), expected) && !restored->canUndo(),
                     "host state roundtrip invalidates old history");
    for (int index = 0; index < restored->getNumPrograms(); ++index) {
        restored->setCurrentProgram(index);
        passed &= expect(restored->getCurrentProgram() == index, "host program valid");
    }
    return passed;
}
bool testLayoutAndDry() {
    auto owner = std::make_unique<OrbitProcessor>();
    auto &p = *owner;
    bool passed = true;
    auto layout = p.getBusesLayout();
    layout.outputBuses.set(0, juce::AudioChannelSet::mono());
    passed &= expect(!p.isBusesLayoutSupported(layout), "reject mono binaural output");
    layout.outputBuses.set(0, juce::AudioChannelSet::stereo());
    juce::MidiBuffer midi;
    for (int channels : {1, 2}) {
        layout.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        passed &= expect(p.setBusesLayout(layout), "mono/stereo input to stereo output");
        for (double rate : {8000.0, 48000.0, 192000.0, 384000.0}) {
            p.applyUserValues({0.2F, 0, 120, 4, 0, 1, 0, 0}, "Dry");
            p.setRateAndBufferSizeDetails(rate, 97);
            p.prepareToPlay(rate, 97);
            const int latency = int(std::ceil(16 * std::max(1.0, rate / 44100)));
            passed &= expect(p.getLatencySamples() == latency,
                             "reported common delay changes with prepare sample rate");
            passed &= expect(p.getTailLengthSeconds() > 0 && p.getTailLengthSeconds() < 0.03,
                             "finite short FIR tail");
            int offset = 0;
            for (int size : {0, 1, 17, 257, 97}) {
                juce::AudioBuffer<float> block(2, size);
                fill(block, offset, channels == 1);
                p.processBlock(block, midi);
                for (int c = 0; c < 2; ++c)
                    for (int f = 0; f < size; ++f)
                        passed &= expect(block.getSample(c, f) ==
                                             (offset + f < latency ? 0
                                                                   : signal(offset + f - latency,
                                                                            channels == 1 ? 0 : c)),
                                         "exact delayed original channels with variable blocks");
                offset += size;
            }
        }
    }
    p.applyUserValues({0.2F, 0, 120, 4, 0, 1, 0, 1}, "Wet");
    p.prepareToPlay(48000, 31);
    auto continuous = std::make_unique<OrbitProcessor>();
    continuous->applyUserValues(p.readValues(), "Continuous wet reference");
    continuous->prepareToPlay(48000, 31);
    const int latency = p.getLatencySamples();
    int offset = 0;
    for (int iteration = 0; iteration < 60; ++iteration) {
        juce::AudioBuffer<float> block(2, 31);
        fill(block, offset);
        juce::AudioBuffer<float> reference(2, 31);
        fill(reference, offset);
        continuous->processBlock(reference, midi);
        if (iteration % 2 == 0)
            p.processBlockBypassed(block, midi);
        else
            p.processBlock(block, midi);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < 31; ++f)
                passed &= expect(
                    iteration % 2
                        ? std::abs(block.getSample(c, f) - reference.getSample(c, f)) < 0.000001F
                        : block.getSample(c, f) ==
                              (offset + f < latency ? 0 : signal(offset + f - latency, c)),
                    "host bypass aligns dry and keeps processing history");
        offset += 31;
    }
    return passed;
}
bool testTempo() {
    auto hz = std::make_unique<OrbitProcessor>(), bpm = std::make_unique<OrbitProcessor>();
    hz->applyUserValues({0.5F, 0, 240, 8, 0, 1, 0, 1}, "Hz");
    bpm->applyUserValues({0.5F, 1, 240, 8, 0, 1, 0, 1}, "BPM");
    hz->prepareToPlay(48000, 257);
    bpm->prepareToPlay(48000, 257);
    PlayHead ignored, host;
    hz->setPlayHead(&ignored);
    bpm->setPlayHead(&host);
    juce::MidiBuffer midi;
    bool passed = true;
    int offset = 0;
    auto compare = [&](int frames) {
        juce::AudioBuffer<float> a(2, frames), b(2, frames);
        fill(a, offset);
        fill(b, offset);
        hz->processBlock(a, midi);
        bpm->processBlock(b, midi);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < frames; ++f)
                passed &= expect(std::abs(a.getSample(c, f) - b.getSample(c, f)) < 0.000001F,
                                 "Hz and effective BPM produce matching rotation output");
        offset += frames;
    };
    for (int block = 0; block < 8; ++block)
        compare(257);
    passed &=
        expect(ignored.calls == 0 && bpm->readTempo().source == OrbitProcessor::TempoSource::host &&
                   bpm->readTempo().bpm == 240,
               "Hz skips playhead; valid host BPM reported");
    auto *manual = bpm->getParameters().getParameter("bpm");
    manual->setValueNotifyingHost(manual->convertTo0to1(120));
    auto *hzRate = hz->getParameters().getParameter("rate");
    hzRate->setValueNotifyingHost(hzRate->convertTo0to1(0.25F));
    for (double invalid : {0.0, 19.0, 301.0, std::numeric_limits<double>::quiet_NaN()}) {
        host.bpm = invalid;
        compare(257);
        const auto current = bpm->readTempo();
        passed &=
            expect(current.source == OrbitProcessor::TempoSource::manual && current.bpm == 120,
                   "invalid host BPM uses explicit manual fallback during rotation");
    }
    host.bpm = {};
    compare(17);
    passed &= expect(bpm->readTempo().source == OrbitProcessor::TempoSource::manual,
                     "missing BPM falls back");
    host.hasPosition = false;
    compare(17);
    passed &= expect(bpm->readTempo().source == OrbitProcessor::TempoSource::manual,
                     "missing host position falls back");
    bpm->setPlayHead(nullptr);
    compare(17);
    passed &= expect(bpm->readTempo().source == OrbitProcessor::TempoSource::manual,
                     "missing playhead falls back");
    host.hasPosition = true;
    host.bpm = 180.0;
    bpm->setPlayHead(&host);
    hzRate->setValueNotifyingHost(hzRate->convertTo0to1(0.375F));
    compare(513);
    passed &= expect(bpm->readTempo().source == OrbitProcessor::TempoSource::host &&
                         bpm->readTempo().bpm == 180,
                     "host tempo resumes with continuous speed transition");
    hz->setPlayHead(nullptr);
    bpm->setPlayHead(nullptr);
    return passed;
}
bool testFrontBackIr() {
    auto owner = std::make_unique<OrbitProcessor>();
    auto &p = *owner;
    juce::MidiBuffer midi;
    auto response = [&](float phase) {
        p.applyUserValues({0, 0, 120, 4, 0, 1, phase, 1}, "Direction");
        p.prepareToPlay(48000, 512);
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        buffer.setSample(0, 0, 1);
        buffer.setSample(1, 0, 1);
        p.processBlock(buffer, midi);
        return buffer;
    };
    const auto front = response(0), back = response(180);
    double difference = 0, frontEnergy = 0, backEnergy = 0;
    for (int c = 0; c < 2; ++c)
        for (int f = 0; f < 512; ++f) {
            const double a = front.getSample(c, f), b = back.getSample(c, f);
            difference += (a - b) * (a - b);
            frontEnergy += a * a;
            backEnergy += b * b;
        }
    return expect(frontEnergy > 0.01 && backEnergy > 0.01 && difference > 0.01,
                  "front and back actual impulse responses differ");
}
bool renderEditor(const juce::File &destination) {
    auto owner = std::make_unique<OrbitProcessor>();
    auto &p = *owner;
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
        fill(block, iteration * 2048);
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
    if (!testUserPresetLibrary<OrbitProcessor>())
        return 1;
    bool passed = testState();
    passed &= testChoiceEditor<OrbitProcessor>({{"Hz speed", "sync"}, {"Clockwise", "direction"}});
    passed &= testLayoutAndDry();
    passed &= testTempo();
    passed &= testFrontBackIr();
    if (argc == 2)
        passed &= renderEditor(juce::File(juce::String::fromUTF8(argv[1])));
    return passed ? 0 : 1;
}
