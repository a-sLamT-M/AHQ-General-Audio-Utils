#include "choiceEditor_test.h"
#include "masterScopeProcessor.h"
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
    int values = 0, gestures = 0;
};
bool expect(bool condition, const char *description) {
    if (!condition)
        std::cerr << "FAILED: " << description << '\n';
    return condition;
}
float signal(int frame) {
    return float(.5 * std::sin(2 * std::numbers::pi * 1000 * frame / 48000));
}
bool waitFor(MasterScopeProcessor &p, size_t blocks) {
    for (int i = 0; i < 400; ++i) {
        if (p.readIntegrated().blocks == blocks)
            return true;
        juce::Thread::sleep(5);
    }
    return false;
}
bool testMetering() {
    MasterScopeProcessor p;
    p.prepareToPlay(48000, 257);
    p.setDisplayEnabled(true);
    ahq::audio::LoudnessMeter reference;
    ahq::audio::IntegratedLoudness integrated;
    bool passed = reference.prepare(48000, 2);
    ahq::audio::TruePeakMeter peakReference;
    passed &= peakReference.prepare(48000, 2);
    size_t blocks = 0;
    juce::MidiBuffer midi;
    for (int offset = 0; offset < 192000;) {
        const int count = std::min((offset % 3 == 0 ? 257 : 113), 192000 - offset);
        juce::AudioBuffer<float> buffer(2, count);
        for (int i = 0; i < count; ++i) {
            const float s = signal(offset + i);
            buffer.setSample(0, i, s);
            buffer.setSample(1, i, -s);
            ahq::audio::LoudnessBlock block;
            passed &= reference.processSample(s, -s, block);
            const float right = -s;
            passed &= peakReference.process(std::span<const float>(&s, 1),
                                            std::span<const float>(&right, 1));
            if (block.ready) {
                passed &= integrated.addBlock(block.energy);
                ++blocks;
            }
        }
        p.processBlock(buffer, midi);
        for (int i = 0; i < count; ++i)
            passed &= expect(buffer.getSample(0, i) == signal(offset + i) &&
                                 buffer.getSample(1, i) == -signal(offset + i),
                             "bit-exact stereo pass");
        offset += count;
    }
    passed &= expect(waitFor(p, blocks), "worker completes without an editor");
    const auto meters = p.readMetering();
    const auto total = p.readIntegrated();
    passed &= expect(meters.peaksValid &&
                         meters.peaks[0].samplePeak == peakReference.reading(0).samplePeak &&
                         meters.peaks[1].truePeak == peakReference.reading(1).truePeak,
                     "sample and 4x estimated true peaks match core");
    std::array<MasterScopeProcessor::PhaseSample, 64> phase{};
    const auto phaseCount = p.readPhase(phase);
    bool actualPhase = phaseCount > 0;
    for (size_t i = 0; i < phaseCount; ++i)
        actualPhase &= phase[i].left == -phase[i].right;
    passed &= expect(actualPhase, "vectorscope receives actual antiphase channels");
    p.setDisplayEnabled(false);
    passed &= expect(meters.momentary.valid &&
                         std::abs(meters.momentary.lufs - reference.momentary().lufs) < 1e-9,
                     "momentary core equality");
    passed &= expect(meters.shortTerm.valid &&
                         std::abs(meters.shortTerm.lufs - reference.shortTerm().lufs) < 1e-9,
                     "short-term core equality");
    passed &= expect(total.loudness.valid &&
                         std::abs(total.loudness.lufs - integrated.reading().lufs) < 1e-9,
                     "integrated core equality");
    passed &= expect(std::abs(meters.stereo.rms[0] - .5 / std::sqrt(2.)) < 1e-7, "absolute RMS");
    passed &= expect(std::abs(meters.stereo.correlation + 1) < 1e-7 &&
                         meters.stereo.midEnergy < 1e-12 && meters.stereo.sideEnergy > .12,
                     "antiphase correlation and M/S");
    passed &= expect(std::abs(meters.stereo.crestDb[0] - 20 * std::log10(std::sqrt(2.))) < 1e-6,
                     "same-window actual crest");
    p.requestReset();
    p.requestReset();
    juce::AudioBuffer<float> silence(2, 1);
    silence.clear();
    p.processBlock(silence, midi);
    passed &= expect(!p.readMetering().stereo.valid && !p.readIntegrated().loudness.valid,
                     "reset invalidates old windows immediately");
    for (int offset = 0; offset < 19200;) {
        const int n = std::min(257, 19200 - offset);
        juce::AudioBuffer<float> b(2, n);
        b.clear();
        p.processBlock(b, midi);
        offset += n;
    }
    passed &= expect(waitFor(p, 1), "new generation excludes previous integrated blocks");
    const auto silent = p.readIntegrated();
    passed &= expect(silent.blocks == 1 && !silent.loudness.valid &&
                         silent.loudness.lufs == -std::numeric_limits<double>::infinity(),
                     "new silent generation has no old energy");
    return passed;
}
bool testStateLayoutsAndOverflow() {
    MasterScopeProcessor p;
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(0, juce::AudioChannelSet::mono());
    layout.outputBuses.set(0, juce::AudioChannelSet::mono());
    bool passed = expect(p.setBusesLayout(layout), "mono matching supported");
    auto bad = layout;
    bad.outputBuses.set(0, juce::AudioChannelSet::stereo());
    passed &= expect(!p.isBusesLayoutSupported(bad), "mismatched buses rejected");
    p.prepareToPlay(48000, 1);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> b(1, 19);
    for (int i = 0; i < 19; ++i)
        b.setSample(0, i, signal(i));
    p.processBlockBypassed(b, midi);
    for (int i = 0; i < 19; ++i)
        passed &= expect(b.getSample(0, i) == signal(i), "mono bypass sample exact");
    passed &= expect(p.getLatencySamples() == 0, "zero latency");
    ParameterEvents events;
    auto *hold = p.getParameters().getParameter("hold");
    hold->addListener(&events);
    auto runOnly = p.readValues();
    runOnly[0] = 0;
    passed &= expect(p.applyUserValues(runOnly, "Capture") && p.undo(), "individual capture undo");
    passed &=
        expect(events.values == 0 && events.gestures == 0, "undo sends no unrelated hold events");
    hold->removeListener(&events);
    const auto initial = p.readValues();
    auto invalidControls = initial;
    invalidControls[0] = 0.5F;
    passed &=
        expect(!p.applyUserValues(invalidControls, "Invalid capture") && p.readValues() == initial,
               "boolean controls reject fractional values without changing state");
    passed &= expect(p.applyUserValues({0, 0, 1}, "Controls"), "edit controls");
    const auto xml = p.createPresetXml();
    juce::MemoryBlock saved;
    p.getStateInformation(saved);
    passed &= expect(p.undo() && p.readValues() == initial && p.redo(), "undo redo controls");
    MasterScopeProcessor restored;
    restored.setStateInformation(saved.getData(), int(saved.getSize()));
    passed &= expect(restored.readValues() == p.readValues(), "all state values roundtrip");
    passed &= expect(!restored.loadPresetXml(xml.replace("version=\"1\"", "version=\"999\"")) &&
                         restored.readValues() == p.readValues(),
                     "invalid version leaves controls unchanged");
    MasterScopeProcessor overflow(false);
    overflow.prepareToPlay(32000, 1024);
    juce::AudioBuffer<float> buffer(2, 1024);
    buffer.clear();
    for (int i = 0; i < 850; ++i)
        overflow.processBlock(buffer, midi);
    passed &= expect(overflow.readIntegrated().gap && !overflow.readIntegrated().loudness.valid,
                     "queue overflow explicitly invalidates integrated");
    overflow.requestReset();
    overflow.processBlock(buffer, midi);
    passed &= expect(!overflow.readIntegrated().gap && !overflow.readMetering().stereo.valid,
                     "reset removes previous generation gap");
    return passed;
}
bool renderEditor(const juce::File &destination) {
    MasterScopeProcessor p;
    p.prepareToPlay(48000, 2048);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditorAndMakeActive());
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    editor->setVisible(true);
    bool passed = expect(editor->isShowing(), "real visible window");
    juce::MidiBuffer midi;
    for (int iteration = 0; iteration < 100; ++iteration) {
        juce::AudioBuffer<float> b(2, 2048);
        for (int i = 0; i < 2048; ++i) {
            const float s = signal(iteration * 2048 + i);
            b.setSample(0, i, s);
            b.setSample(1, i, -s);
        }
        p.processBlock(b, midi);
        juce::Thread::sleep(35);
        juce::Timer::callPendingTimersSynchronously();
    }
    auto image = editor->createComponentSnapshot(editor->getLocalBounds());
    editor->setVisible(false);
    editor->removeFromDesktop();
    p.editorBeingDeleted(editor.get());
    editor.reset();
    auto out = destination.createOutputStream();
    if (!out)
        return false;
    out->setPosition(0);
    out->truncate();
    juce::PNGImageFormat png;
    return passed && png.writeImageToStream(image, *out);
}
} // namespace
int main(int argc, char **argv) {
    juce::ScopedJuceInitialiser_GUI init;
    bool passed = testMetering();
    passed &= testChoiceEditor<MasterScopeProcessor>({{"L/R phase", "scope"}});
    passed &= testStateLayoutsAndOverflow();
    if (argc == 2)
        passed &= renderEditor(juce::File(juce::String::fromUTF8(argv[1])));
    return passed ? 0 : 1;
}
