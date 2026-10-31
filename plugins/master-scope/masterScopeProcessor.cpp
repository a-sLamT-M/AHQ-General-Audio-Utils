#include "masterScopeProcessor.h"
#include "masterScopeEditor.h"
#include <ahq/audio/loudnessRange.h>
#include <chrono>
#include <cmath>
#include <limits>

namespace {
constexpr std::array<const char *, 3> ids{"run", "hold", "scope"};
}
juce::AudioProcessorValueTreeState::ParameterLayout MasterScopeProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"run", 1}, "Capture", true));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"hold", 1}, "Peak hold",
                                                     true));
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"scope", 1}, "Scope coordinates", juce::StringArray{"L/R", "M/S"}, 0));
    return l;
}
MasterScopeProcessor::MasterScopeProcessor(bool enableWorker)
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "MasterScopeParameters", createParameterLayout()),
      state(parameters, ids, "AHQMasterScopeState", program), workerEnabled(enableWorker) {}
MasterScopeProcessor::~MasterScopeProcessor() { stopWorker(); }
juce::String MasterScopeProcessor::createPresetXml() const {
    auto xml = juce::parseXML(state.createPresetXml());
    auto *view = xml->createNewChildElement("Display");
    view->setAttribute("spectrumGain", double(spectrumGainDb.load()));
    view->setAttribute("scopeGain", double(scopeGainDb.load()));
    view->setAttribute("frequencySmoothing", spectrumFrequencySmoothing.load());
    view->setAttribute("smoothingMs", double(spectrumSmoothingMs.load()));
    view->setAttribute("spectrumShare", double(spectrumShare.load()));
    view->setAttribute("range", spectrumRange.load());
    view->setAttribute("style", displayStyle.load());
    view->setAttribute("theme", displayTheme.load());
    view->setAttribute("width", editorWidth.load());
    view->setAttribute("height", editorHeight.load());
    return xml->toString();
}
void MasterScopeProcessor::getStateInformation(juce::MemoryBlock &block) {
    if (auto xml = juce::parseXML(createPresetXml()))
        copyXmlToBinary(*xml, block);
}
bool MasterScopeProcessor::loadPresetXml(const juce::String &xml, bool undoable) {
    if (undoable)
        return state.applyUserPreset(createPresetXml(), xml, [this](const juce::String &text) {
            return restorePresetXml(text, true);
        });
    if (!restorePresetXml(xml, false))
        return false;
    state.invalidateHistory();
    return true;
}
void MasterScopeProcessor::setStateInformation(const void *data, int size) {
    if (data == nullptr || size <= 0 || size > 65536)
        return;
    if (auto xml = getXmlFromBinary(data, size))
        loadPresetXml(xml->toString(), false);
}
bool MasterScopeProcessor::restorePresetXml(const juce::String &text, bool notifyHost) {
    if (text.getNumBytesAsUTF8() > 65536)
        return false;
    auto xml = juce::parseXML(text);
    if (!xml || xml->getNumChildElements() > 1)
        return false;
    std::unique_ptr<juce::XmlElement> view;
    if (auto *child = xml->getChildByName("Display")) {
        view = std::make_unique<juce::XmlElement>(*child);
        xml->removeChildElement(child, true);
    }
    PluginState::Values values;
    if (!state.decodePresetXml(xml->toString(), values))
        return false;
    state.writeValues(values, notifyHost);
    auto readGain = [&](const char *name) {
        const auto value = view->getDoubleAttribute(name, 0);
        return std::isfinite(value) ? float(std::clamp(value, -24.0, 36.0)) : 0.F;
    };
    if (view) {
        spectrumGainDb.store(readGain("spectrumGain"));
        scopeGainDb.store(readGain("scopeGain"));
        const double smoothing = view->getDoubleAttribute("smoothingMs", 140);
        const double share = view->getDoubleAttribute("spectrumShare", 65);
        spectrumFrequencySmoothing.store(
            std::clamp(view->getIntAttribute("frequencySmoothing", 1), 0, 3));
        spectrumSmoothingMs.store(
            std::isfinite(smoothing) ? float(std::clamp(smoothing, 0.0, 600.0)) : 140.F);
        spectrumShare.store(std::isfinite(share) ? float(std::clamp(share, 50.0, 75.0)) : 65.F);
        const int range = view->getIntAttribute("range", 90);
        spectrumRange.store(range == 60 || range == 120 ? range : 90);
        displayStyle.store(std::clamp(view->getIntAttribute("style", 1), 0, 2));
        displayTheme.store(std::clamp(view->getIntAttribute("theme", 0), 0, 2));
        editorWidth.store(std::clamp(view->getIntAttribute("width", 1600), 980, 2000));
        editorHeight.store(std::clamp(view->getIntAttribute("height", 780), 680, 1400));
        displayRevision.fetch_add(1);
    }
    return true;
}
void MasterScopeProcessor::stopWorker() {
    stopping.store(true, std::memory_order_release);
    if (worker.joinable())
        worker.join();
}
void MasterScopeProcessor::prepareToPlay(double rate, int) {
    stopWorker();
    channels = unsigned(getTotalNumInputChannels());
    supportsLoudness = loudness[0].prepare(rate, channels);
    supportsLoudness = loudness[1].prepare(rate, channels) && supportsLoudness;
    prepared = peak.prepare(rate, channels) && stereo.prepare(rate, channels);
    supported.store(supportsLoudness);
    activeBank = 0;
    bankReady[0].store(false, std::memory_order_release);
    bankReady[1].store(true, std::memory_order_release);
    retiredBank.store(-1);
    eventFifo.reset(); // Worker stopped; host preparation is outside audio processing.
    audioGeneration = requestedGeneration.fetch_add(1) + 1;
    measurementGeneration.store(audioGeneration, std::memory_order_release);
    gapGeneration.store(0);
    flags.store(0);
    pending.store(false);
    peakFrames = 0;
    peakInterval = prepared ? unsigned(std::max(1.0, std::round(rate * .1))) : 1U;
    previousRun = readValues()[0] != 0;
    previousHold = readValues()[1] != 0;
    analysis.prepare(rate);
    setLatencySamples(0);
    {
        std::lock_guard lock(integratedMutex);
        integrated = {};
        integrated.generation = audioGeneration;
    }
    stopping.store(false, std::memory_order_release);
    if (workerEnabled)
        worker = std::thread([this] { workerLoop(); });
}
void MasterScopeProcessor::releaseResources() {
    prepared = false;
    stopWorker();
}
bool MasterScopeProcessor::isBusesLayoutSupported(const BusesLayout &l) const {
    auto c = l.getMainInputChannelSet();
    return c == l.getMainOutputChannelSet() &&
           (c == juce::AudioChannelSet::mono() || c == juce::AudioChannelSet::stereo());
}
MasterScopeProcessor::Values MasterScopeProcessor::readValues() const noexcept {
    Values v{};
    state.fillValues(v);
    return v;
}
bool MasterScopeProcessor::applyReset() noexcept {
    const unsigned target = requestedGeneration.load(std::memory_order_acquire);
    if (target == audioGeneration)
        return true;
    const int next = 1 - activeBank;
    if (!bankReady[size_t(next)].exchange(false, std::memory_order_acquire)) {
        pending.store(true);
        flags.store(0);
        return false;
    }
    const int old = activeBank;
    activeBank = next;
    audioGeneration = target;
    peak.reset();
    stereo.reset();
    peakFrames = 0;
    flags.store(0);
    measurementGeneration.store(target, std::memory_order_release);
    retiredBank.store(old, std::memory_order_release);
    pending.store(false);
    return true;
}
void MasterScopeProcessor::processBlock(juce::AudioBuffer<float> &b, juce::MidiBuffer &) {
    if (!prepared || b.getNumSamples() == 0 || b.getNumChannels() < int(channels))
        return;
    const auto v = readValues();
    const bool run = v[0] != 0, hold = v[1] != 0;
    if (run && !previousRun)
        requestReset();
    previousRun = run;
    if (!applyReset() || !run)
        return;
    if (hold != previousHold) {
        peak.clearPeaks();
        peakFrames = 0;
    }
    previousHold = hold;
    const float *left = b.getReadPointer(0), *right = channels == 2 ? b.getReadPointer(1) : nullptr;
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const float l = left[i], r = right ? right[i] : 0;
        ahq::audio::LoudnessBlock block;
        if (supportsLoudness && !loudness[size_t(activeBank)].processSample(l, r, block))
            gapGeneration.store(audioGeneration, std::memory_order_release);
        if (block.ready) {
            int a, n, c, m;
            eventFifo.prepareToWrite(1, a, n, c, m);
            if (n + m) {
                const auto shortReading = loudness[size_t(activeBank)].shortTerm();
                events[size_t(n ? a : c)] = {block.energy, shortReading.lufs, audioGeneration,
                                             shortReading.valid};
                eventFifo.finishedWrite(1);
            } else
                gapGeneration.store(audioGeneration, std::memory_order_release);
        }
        if (!stereo.processSample(l, r) ||
            !peak.process(std::span<const float>(&left[i], 1),
                          right ? std::span<const float>(&right[i], 1) : std::span<const float>{}))
            gapGeneration.store(audioGeneration, std::memory_order_release);
        if (++peakFrames >= peakInterval) {
            for (unsigned ch = 0; ch < channels; ++ch) {
                auto p = peak.reading(ch);
                snapshot[9 + ch].store(p.samplePeak);
                snapshot[11 + ch].store(p.truePeak);
            }
            flags.fetch_or(32);
            peakFrames = 0;
            if (!hold)
                peak.clearPeaks(); // FIR history deliberately survives interval changes.
        }
    }
    if (displayEnabled.load(std::memory_order_relaxed)) {
        int first, firstCount, second, secondCount;
        phaseFifo.prepareToWrite(b.getNumSamples(), first, firstCount, second, secondCount);
        for (int i = 0; i < firstCount; ++i)
            phaseSamples[size_t(first + i)] = {left[i], right ? right[i] : left[i],
                                               audioGeneration};
        for (int i = 0; i < secondCount; ++i) {
            const int frame = firstCount + i;
            phaseSamples[size_t(second + i)] = {left[frame], right ? right[frame] : left[frame],
                                                audioGeneration};
        }
        phaseFifo.finishedWrite(firstCount + secondCount);
    }
    publishMetering();
    analysis.publish(b, PluginAnalysis::peak(b));
}
void MasterScopeProcessor::publishMetering() noexcept {
    auto s = stereo.reading();
    const auto m =
        supportsLoudness ? loudness[size_t(activeBank)].momentary() : ahq::audio::LoudnessReading{};
    const auto t =
        supportsLoudness ? loudness[size_t(activeBank)].shortTerm() : ahq::audio::LoudnessReading{};
    snapshot[0].store(m.lufs);
    snapshot[1].store(t.lufs);
    for (size_t i = 0; i < 2; ++i) {
        snapshot[2 + i].store(s.rms[i]);
        snapshot[4 + i].store(s.crestDb[i]);
    }
    snapshot[6].store(s.correlation);
    snapshot[7].store(s.midEnergy);
    snapshot[8].store(s.sideEnergy);
    flags.store((flags.load() & 32U) | (m.valid ? 1U : 0U) | (t.valid ? 2U : 0U) |
                (s.valid ? 4U : 0U) | (s.stereo ? 8U : 0U) | (s.correlationValid ? 16U : 0U));
}
MasterScopeProcessor::Metering MasterScopeProcessor::readMetering() const noexcept {
    Metering m;
    const unsigned f = flags.load();
    m.momentary = {snapshot[0].load(), bool(f & 1)};
    m.shortTerm = {snapshot[1].load(), bool(f & 2)};
    for (size_t i = 0; i < 2; ++i) {
        m.stereo.rms[i] = snapshot[2 + i].load();
        m.stereo.crestDb[i] = snapshot[4 + i].load();
        m.peaks[i] = {snapshot[9 + i].load(), snapshot[11 + i].load()};
    }
    m.stereo.correlation = snapshot[6].load();
    m.stereo.midEnergy = snapshot[7].load();
    m.stereo.sideEnergy = snapshot[8].load();
    m.stereo.valid = f & 4;
    m.stereo.stereo = f & 8;
    m.stereo.correlationValid = f & 16;
    m.peaksValid = f & 32;
    m.resetPending = pending.load();
    m.loudnessSupported = supported.load();
    m.generation = measurementGeneration.load(std::memory_order_acquire);
    return m;
}
void MasterScopeProcessor::workerLoop() {
    ahq::audio::IntegratedLoudness accumulator;
    unsigned generation = 0;
    bool failed = false;
    ahq::audio::LoudnessRange rangeMeter;

    while (!stopping.load(std::memory_order_acquire)) {
        const int retired = retiredBank.exchange(-1, std::memory_order_acquire);
        if (retired >= 0) {
            loudness[size_t(retired)].reset();
            bankReady[size_t(retired)].store(true, std::memory_order_release);
        }
        const unsigned current = measurementGeneration.load(std::memory_order_acquire);
        bool changed = current != generation;
        if (current != generation) {
            accumulator.reset();
            generation = current;
            failed = false;
            rangeMeter.reset();
        }
        int a, n, c, m;
        eventFifo.prepareToRead(eventCapacity, a, n, c, m);
        auto consume = [&](int start, int count) {
            for (int i = 0; i < count; ++i) {
                const auto &e = events[size_t(start + i)];
                const unsigned now = measurementGeneration.load(std::memory_order_acquire);
                if (now != generation) {
                    changed = true;
                    accumulator.reset();
                    generation = now;
                    failed = false;
                    rangeMeter.reset();
                }
                if (e.generation != generation || failed)
                    continue;
                try {
                    if (!accumulator.addBlock(e.energy))
                        failed = true;
                    if (e.shortValid)
                        rangeMeter.addShortTerm(e.shortLufs);
                } catch (const std::bad_alloc &) {
                    failed = true;
                }
            }
        };
        consume(a, n);
        consume(c, m);
        eventFifo.finishedRead(n + m);
        if (!changed && n + m == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        IntegratedReading result;
        result.generation = generation;
        result.blocks = accumulator.blockCount();
        result.allocationFailed = failed;
        result.loudnessRange = rangeMeter.reading().lu;
        result.rangeValid = rangeMeter.reading().valid;
        result.loudness = accumulator.reading();
        {
            std::lock_guard lock(integratedMutex);
            integrated = result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
MasterScopeProcessor::IntegratedReading MasterScopeProcessor::readIntegrated() const {
    std::lock_guard lock(integratedMutex);
    auto r = integrated;
    const unsigned g = measurementGeneration.load(std::memory_order_acquire);
    r.gap = g != 0 && gapGeneration.load(std::memory_order_acquire) == g;
    if (r.generation != g || r.gap || r.allocationFailed || pending.load()) {
        r.loudness.valid = false;
        r.rangeValid = false;
    }
    return r;
}
void MasterScopeProcessor::setDisplayEnabled(bool enabled) noexcept {
    displayEnabled.store(enabled);
    analysis.setSpectrumEnabled(enabled);
}
size_t MasterScopeProcessor::readPhase(std::span<PhaseSample> output) noexcept {
    int a, n, c, m;
    phaseFifo.prepareToRead(int(output.size()), a, n, c, m);
    size_t written = 0;
    const auto generation = measurementGeneration.load(std::memory_order_acquire);
    auto copy = [&](int start, int count) {
        for (int i = 0; i < count; ++i)
            if (phaseSamples[size_t(start + i)].generation == generation)
                output[written++] = phaseSamples[size_t(start + i)];
    };
    copy(a, n);
    copy(c, m);
    phaseFifo.finishedRead(n + m);
    return written;
}
juce::AudioProcessorEditor *MasterScopeProcessor::createEditor() {
    return new MasterScopeEditor(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new MasterScopeProcessor(); }
