#include "trueLimiterProcessor.h"
#include "trueLimiterEditor.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
constexpr std::array<const char *, 8> IDS{"ceiling",   "target",  "live",    "gain",
                                          "lookahead", "release", "quality", "mix"};
unsigned quality(float choice) {
    if (choice == 0)
        return 4;
    if (choice == 1)
        return 8;
    return 16;
}
ahq::audio::TrueLimiterParameters core(const TrueLimiterProcessor::Values &v) {
    return {v[0], v[5], v[3], v[7], v[2] != 0, v[1]};
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout TrueLimiterProcessor::createLayout() {
    using Parameters = ahq::audio::TrueLimiterParameters;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto add = [&](const char *id, const char *name, ahq::audio::ParameterRange range,
                   float value) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, 1}, name,
                                                               toJuceRange(range), value));
    };
    add("ceiling", "Peak ceiling", Parameters::ceilingDbRange, -1);
    add("target", "Target loudness", Parameters::targetLufsRange, -14);
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"live", 1},
                                                          "Live short-term LUFS", false));
    add("gain", "Input gain", Parameters::inputGainDbRange, 0);
    add("lookahead", "Lookahead", {0, 20}, 5);
    add("release", "Release", Parameters::releaseMsRange, 100);
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"quality", 1}, "Oversampling", juce::StringArray{"4x", "8x", "16x"}, 1));
    add("mix", "Mix before protection", Parameters::mixRange, 1);
    return layout;
}
TrueLimiterProcessor::TrueLimiterProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "TrueLimiterParameters", createLayout()),
      state(parameters, IDS, "AHQTrueLimiterState", program) {}
TrueLimiterProcessor::~TrueLimiterProcessor() { cancelFileAnalysis(); }
TrueLimiterProcessor::Values TrueLimiterProcessor::readValues() const noexcept {
    Values values{};
    state.fillValues(values);
    return values;
}
void TrueLimiterProcessor::prepareToPlay(double rate, int) {
    displayAnalysis.prepare(rate);
    const auto values = readValues();
    prepared = limiter.prepare(rate, static_cast<unsigned>(getTotalNumInputChannels()), 20,
                               quality(values[6]));
    if (prepared) {
        prepared = outputMeter.prepare(rate, static_cast<unsigned>(getTotalNumInputChannels())) &&
                   peakMeter.prepare(rate, static_cast<unsigned>(getTotalNumInputChannels()));
    }
    if (prepared) {
        prepared = limiter.setParameters(core(values)) && limiter.setLookaheadMs(values[4]);
        limiter.reset();
        for (auto &buffer : dryDelay)
            buffer.assign(limiter.latencyFrames(), 0.0F);
        dryPosition = 0;
        setLatencySamples(static_cast<int>(limiter.latencyFrames()));
    } else {
        setLatencySamples(0);
    }
    shortTerm.store(-INFINITY);
    reduction.store(0);
    outputShortTerm.store(-INFINITY);
    outputPeak.store(-INFINITY);
}
bool TrueLimiterProcessor::isBusesLayoutSupported(const BusesLayout &layout) const {
    const auto output = layout.getMainOutputChannelSet();
    return output == layout.getMainInputChannelSet() &&
           (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}
void TrueLimiterProcessor::processBlock(juce::AudioBuffer<float> &b, juce::MidiBuffer &) {
    processAudio(b, false);
}
void TrueLimiterProcessor::processBlockBypassed(juce::AudioBuffer<float> &b, juce::MidiBuffer &) {
    processAudio(b, true);
}
void TrueLimiterProcessor::processAudio(juce::AudioBuffer<float> &buffer, bool bypassed) {
    juce::ScopedNoDenormals noDenormals;
    const int channels = buffer.getNumChannels();
    if (channels < 1 || channels > 2 || buffer.getNumSamples() == 0)
        return;
    const float inputPeak = PluginAnalysis::peak(buffer);
    if (prepared && channels == getTotalNumInputChannels()) {
        const auto values = readValues();
        (void)limiter.setParameters(core(values));
        (void)limiter.setLookaheadMs(values[4]);
        (void)limiter.setOversamplingFactor(quality(values[6]));
        // Always advance both the dry delay and DSP, including host bypass.
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            std::array<float, 2> wet{}, delayed{};
            for (int c = 0; c < channels; ++c) {
                wet[static_cast<size_t>(c)] = buffer.getSample(c, i);
                delayed[static_cast<size_t>(c)] = dryDelay[static_cast<size_t>(c)][dryPosition];
                dryDelay[static_cast<size_t>(c)][dryPosition] = wet[static_cast<size_t>(c)];
            }
            dryPosition = (dryPosition + 1) % dryDelay[0].size();
            (void)limiter.process({wet.data(), 1}, channels == 2
                                                       ? std::span<float>(wet.data() + 1, 1)
                                                       : std::span<float>{});
            for (int c = 0; c < channels; ++c)
                buffer.setSample(
                    c, i, bypassed ? delayed[static_cast<size_t>(c)] : wet[static_cast<size_t>(c)]);
            ahq::audio::LoudnessBlock outputBlock;
            (void)outputMeter.processSample(
                buffer.getSample(0, i), channels == 2 ? buffer.getSample(1, i) : 0.0F, outputBlock);
        }
        shortTerm.store(limiter.inputShortTerm().lufs);
        reduction.store(limiter.gainReductionDb());
        outputShortTerm.store(outputMeter.shortTerm().lufs);
        const auto count = static_cast<size_t>(buffer.getNumSamples());
        (void)peakMeter.process({buffer.getReadPointer(0), count},
                                channels == 2
                                    ? std::span<const float>(buffer.getReadPointer(1), count)
                                    : std::span<const float>{});
        outputPeak.store(
            std::max(peakMeter.reading(0).truePeakDb(), peakMeter.reading(1).truePeakDb()));
    }
    displayAnalysis.publish(buffer, inputPeak);
}
juce::AudioProcessorEditor *TrueLimiterProcessor::createEditor() {
    return new TrueLimiterEditor(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new TrueLimiterProcessor(); }

void TrueLimiterProcessor::cancelFileAnalysis() {
    cancelAnalysis.store(true);
    if (worker.joinable())
        worker.join();
}
void TrueLimiterProcessor::startFileAnalysis(const juce::File &file) {
    cancelFileAnalysis();
    cancelAnalysis.store(false);
    const auto values = readValues();
    {
        std::lock_guard lock(resultMutex);
        result = {};
        result.running = true;
        result.fileName = file.getFileName();
        result.settings = values;
    }
    worker = std::thread([this, file, values] { analyzeFile(file, values); });
}
TrueLimiterProcessor::FileAnalysis TrueLimiterProcessor::fileAnalysis() const {
    std::lock_guard lock(resultMutex);
    return result;
}
bool TrueLimiterProcessor::applyFileAnalysis() {
    const auto analysis = fileAnalysis();
    if (!analysis.valid || analysis.running)
        return false;
    auto values = analysis.settings;
    values[2] = 0;
    values[3] = static_cast<float>(analysis.gainDb);
    values[7] = 1;
    return applyUserValues(values, "Apply analyzed loudness match");
}
void TrueLimiterProcessor::analyzeFile(juce::File file, Values snapshot) {
    FileAnalysis finished;
    finished.fileName = file.getFileName();
    finished.settings = snapshot;
    try {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (!reader || reader->numChannels < 1 || reader->numChannels > 2 ||
            reader->lengthInSamples <= 0)
            finished.error = "Audio file must contain mono or stereo samples.";
        else {
            const auto initialSize = file.getSize();
            const auto initialTime = file.getLastModificationTime();
            const unsigned channels = reader->numChannels;
            finished.sampleRate = reader->sampleRate;
            finished.channels = channels;
            ahq::audio::LoudnessMeter meter;
            ahq::audio::TrueLimiter renderer;
            if (!meter.prepare(reader->sampleRate, channels) ||
                !renderer.prepare(reader->sampleRate, channels, 20, quality(snapshot[6]))) {
                finished.error = "LUFS processing supports 32-384 kHz files.";
            } else {
                juce::AudioBuffer<float> buffer(static_cast<int>(channels), 4096);
                ahq::audio::IntegratedLoudness integrated;
                for (juce::int64 offset = 0;
                     offset < reader->lengthInSamples && !cancelAnalysis.load(); offset += 4096) {
                    const int count = static_cast<int>(
                        std::min<juce::int64>(4096, reader->lengthInSamples - offset));
                    if (!reader->read(&buffer, 0, count, offset, true, channels == 2))
                        throw std::runtime_error("Cannot read the complete audio file.");
                    for (int i = 0; i < count; ++i) {
                        ahq::audio::LoudnessBlock block;
                        if (!meter.processSample(buffer.getSample(0, i),
                                                 channels == 2 ? buffer.getSample(1, i) : 0, block))
                            throw std::runtime_error("Audio contains non-finite samples.");
                        if (block.ready)
                            (void)integrated.addBlock(block.energy);
                    }
                }
                const auto input = integrated.reading();
                if (!cancelAnalysis.load() && input.valid && std::isfinite(input.lufs)) {
                    finished.inputLufs = input.lufs;
                    finished.gainDb =
                        std::clamp(static_cast<double>(snapshot[1]) - input.lufs, -24.0, 24.0);
                    auto renderingValues = snapshot;
                    renderingValues[2] = 0;
                    renderingValues[3] = static_cast<float>(finished.gainDb);
                    renderingValues[7] = 1;
                    finished.gainDb = renderingValues[3];
                    (void)renderer.setParameters(core(renderingValues));
                    (void)renderer.setLookaheadMs(snapshot[4]);
                    renderer.reset();
                    meter.reset();
                    integrated.reset();
                    const auto latency = static_cast<juce::int64>(renderer.latencyFrames());
                    for (juce::int64 offset = 0;
                         offset < reader->lengthInSamples + latency && !cancelAnalysis.load();
                         offset += 4096) {
                        const int count = static_cast<int>(std::min<juce::int64>(
                            4096, reader->lengthInSamples + latency - offset));
                        buffer.clear();
                        const int sourceCount = static_cast<int>(std::max<juce::int64>(
                            0, std::min<juce::int64>(count, reader->lengthInSamples - offset)));
                        if (sourceCount > 0 &&
                            !reader->read(&buffer, 0, sourceCount, offset, true, channels == 2))
                            throw std::runtime_error("Cannot read the second analysis pass.");
                        if (!renderer.process(
                                {buffer.getWritePointer(0), static_cast<size_t>(count)},
                                channels == 2 ? std::span<float>(buffer.getWritePointer(1),
                                                                 static_cast<size_t>(count))
                                              : std::span<float>{}))
                            throw std::runtime_error("Limiter render failed.");
                        for (int i = 0; i < count; ++i) {
                            const auto aligned = offset + i - latency;
                            if (aligned < 0 || aligned >= reader->lengthInSamples)
                                continue;
                            ahq::audio::LoudnessBlock block;
                            (void)meter.processSample(buffer.getSample(0, i),
                                                      channels == 2 ? buffer.getSample(1, i) : 0,
                                                      block);
                            if (block.ready)
                                (void)integrated.addBlock(block.energy);
                        }
                    }
                    const auto output = integrated.reading();
                    if (!cancelAnalysis.load() && output.valid) {
                        finished.outputLufs = output.lufs;
                        finished.targetReached = std::abs(output.lufs - snapshot[1]) <= 0.1;
                        finished.peakLimited = renderer.protectionEngaged();
                        finished.valid = true;
                    }
                } else if (!cancelAnalysis.load())
                    finished.error = "No complete audible gated loudness blocks.";
                if (file.getSize() != initialSize ||
                    file.getLastModificationTime() != initialTime) {
                    finished.valid = false;
                    finished.error = "File changed during analysis; analyze again.";
                }
            }
        }
    } catch (const std::exception &error) {
        finished.error = error.what();
        finished.valid = false;
    } catch (...) {
        finished.error = "File analysis failed.";
        finished.valid = false;
    }
    if (cancelAnalysis.load()) {
        finished.cancelled = true;
        finished.valid = false;
        finished.error = "Analysis cancelled.";
    }
    std::lock_guard lock(resultMutex);
    result = std::move(finished);
}
