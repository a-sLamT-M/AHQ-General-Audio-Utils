#include "ahq/audio/saturator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

using ahq::audio::OversamplingFactor;
using ahq::audio::Saturator;
using ahq::audio::SaturatorModel;
using ahq::audio::SaturatorParameters;

namespace {

bool expect(bool condition, const char *caseName) {
    if (!condition) {
        std::cerr << "FAILED: " << caseName << '\n';
    }
    return condition;
}

std::vector<float> tone(double frequency, std::size_t frames = 96000, double amplitude = 0.5) {
    std::vector<float> samples(frames);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        samples[frame] =
            static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * frequency *
                                                    static_cast<double>(frame) / 48000.0));
    }
    return samples;
}

double binAmplitude(const std::vector<float> &samples, double frequency) {
    double real = 0.0;
    double imaginary = 0.0;
    for (std::size_t frame = 48000; frame < samples.size(); ++frame) {
        const double phase =
            2.0 * std::numbers::pi * frequency * static_cast<double>(frame) / 48000.0;
        real += samples[frame] * std::cos(phase);
        imaginary += samples[frame] * std::sin(phase);
    }
    return 2.0 * std::hypot(real, imaginary) / static_cast<double>(samples.size() - 48000);
}

bool testCurvesAndParameters() {
    bool passed = true;
    constexpr std::array<double, 7> EXPECTED{0.9640275801, 1.0, 0.6633698358, 0.8944271910,
                                             0.0,          1.0, 1.0};
    std::array<float, 7> modelOutputs{};
    for (int model = 0; model < 7; ++model) {
        Saturator processor;
        SaturatorParameters parameters;
        parameters.model = static_cast<SaturatorModel>(model);
        passed &= expect(processor.prepare(48000.0, OversamplingFactor::x1) &&
                             processor.setParameters(parameters),
                         "prepare curve");
        processor.reset();
        std::array<float, 1> sample{2.0F};
        passed &= expect(processor.process(sample), "process known curve point");
        modelOutputs[model] = sample[0];
        passed &= expect(std::abs(sample[0] - EXPECTED[model]) < 0.000001,
                         "curve matches mathematical known point");
        parameters.bias = 0.75F;
        passed &= expect(processor.setParameters(parameters), "set curve bias");
        processor.reset();
        sample[0] = 0.0F;
        passed &= expect(processor.process(sample) && sample[0] == 0.0F,
                         "bias compensation keeps zero exactly zero");
    }
    passed &= expect(modelOutputs[0] != modelOutputs[2] && modelOutputs[0] != modelOutputs[3],
                     "soft tube tape differ");
    Saturator processor;
    SaturatorParameters parameters;
    parameters.model = SaturatorModel::tube;
    passed &= expect(processor.prepare(48000.0, OversamplingFactor::x1) &&
                         processor.setParameters(parameters),
                     "prepare asymmetric tube");
    processor.reset();
    std::array<float, 2> bipolar{1.0F, -1.0F};
    passed &= expect(processor.process(bipolar) && std::abs(bipolar[0] + bipolar[1]) > 0.2F,
                     "tube positive negative halves asymmetric");
    parameters.model = SaturatorModel::rectifier;
    passed &= expect(processor.setParameters(parameters), "set rectifier");
    processor.reset();
    bipolar = {0.25F, -0.25F};
    passed &= expect(processor.process(bipolar) && bipolar[0] == 0.25F && bipolar[1] == 0.25F,
                     "rectifier fullwave creates intentional DC");
    parameters.model = SaturatorModel::bitcrusher;
    passed &= expect(processor.setParameters(parameters), "set crusher");
    processor.reset();
    bipolar = {0.101F, 0.102F};
    passed &= expect(processor.process(bipolar) && bipolar[0] == bipolar[1] &&
                         std::abs(bipolar[0] - 13.0F / 127.0F) < 0.000001F,
                     "crusher rounds to discrete levels");
    parameters = {};
    passed &= expect(processor.setParameters(parameters), "set soft baseline");
    processor.reset();
    std::array<float, 1> baseline{0.25F};
    passed &= expect(processor.process(baseline), "soft baseline");
    parameters.driveDb = 12.0F;
    passed &= expect(processor.setParameters(parameters), "set drive");
    processor.reset();
    bipolar = {0.25F, 0.0F};
    passed &= expect(processor.process(bipolar) && bipolar[0] > 2.0F * baseline[0],
                     "drive increases saturation input");
    parameters.driveDb = 0.0F;
    parameters.bias = 0.8F;
    passed &= expect(processor.setParameters(parameters), "set bias");
    processor.reset();
    bipolar = {0.25F, -0.25F};
    passed &= expect(processor.process(bipolar) && std::abs(bipolar[0] + bipolar[1]) > 0.02F,
                     "bias changes asymmetry beyond static offset");
    std::array<double, 3> amplitude{};
    for (int index = 0; index < 3; ++index) {
        parameters = {};
        parameters.model = SaturatorModel::hardClip;
        parameters.tone = static_cast<float>(index - 1);
        passed &= expect(processor.setParameters(parameters), "set tone color");
        processor.reset();
        auto samples = tone(8000.0, 96000, 0.1);
        passed &= expect(processor.process(samples), "process tone color");
        amplitude[index] = binAmplitude(samples, 8000.0);
    }
    passed &= expect(amplitude[0] < 0.04 && std::abs(amplitude[1] - 0.1) < 0.000001 &&
                         amplitude[2] > 0.16,
                     "tone actual high frequency cut and boost");
    return passed;
}

bool testLatencyMixAndReset() {
    bool passed = true;
    for (const auto factor :
         {OversamplingFactor::x1, OversamplingFactor::x2, OversamplingFactor::x4}) {
        Saturator processor;
        SaturatorParameters parameters;
        parameters.model = SaturatorModel::hardClip;
        passed &= expect(processor.prepare(48000.0, factor) && processor.setParameters(parameters),
                         "prepare wet impulse");
        processor.reset();
        const auto latency = processor.getLatencySamples();
        const auto expectedLatency = factor == OversamplingFactor::x1   ? 0U
                                     : factor == OversamplingFactor::x2 ? 16U
                                                                        : 24U;
        passed &= expect(latency == expectedLatency, "quality latency contract");
        std::array<float, 100> wet{};
        wet[0] = 0.1F;
        passed &= expect(processor.process(wet), "process linear impulse through FIR");
        const auto peak = std::max_element(wet.begin(), wet.end());
        passed &= expect(static_cast<std::size_t>(peak - wet.begin()) == latency,
                         "wet impulse peak validates reported latency");
        for (std::size_t frame = 0; frame <= latency; ++frame) {
            passed &= expect(std::abs(wet[frame] - wet[2 * latency - frame]) < 0.000001F,
                             "linear wet impulse symmetric around latency");
        }
        double impulseSum = 0.0;
        for (const float sample : wet) {
            impulseSum += sample;
        }
        passed &= expect(std::abs(impulseSum - 0.1) < 0.000001,
                         "interpolation gain compensates inserted zero samples");
        parameters.mix = 0.0F;
        passed &= expect(processor.setParameters(parameters), "set delayed dry");
        processor.reset();
        auto original = tone(777.0, 100);
        auto dry = original;
        passed &= expect(processor.process(dry), "process delayed dry");
        for (std::size_t frame = 0; frame < dry.size(); ++frame) {
            passed &= expect(dry[frame] == (frame < latency ? 0.0F : original[frame - latency]),
                             "dry exactly delayed original");
        }
        parameters.mix = 0.25F;
        passed &= expect(processor.setParameters(parameters), "set partial mix");
        processor.reset();
        std::array<float, 100> blend{};
        blend[0] = 0.1F;
        passed &= expect(processor.process(blend), "process partial mix impulse");
        for (std::size_t frame = 0; frame < blend.size(); ++frame) {
            const float expected = 0.25F * wet[frame] + (frame == latency ? 0.075F : 0.0F);
            passed &= expect(std::abs(blend[frame] - expected) < 0.000001F,
                             "mix dry and wet impulses aligned");
        }
        processor.reset();
        auto repeated = std::array<float, 100>{};
        repeated[0] = 0.1F;
        passed &= expect(processor.process(repeated) && repeated == blend, "reset clears FIR");
        passed &= expect(processor.prepare(44100.0, factor), "change sample rate");
        Saturator fresh;
        passed &= expect(fresh.prepare(44100.0, factor) && fresh.setParameters(parameters),
                         "prepare fresh changed rate");
        fresh.reset();
        auto first = tone(777.0, 100);
        auto second = first;
        passed &= expect(processor.process(first) && fresh.process(second) && first == second,
                         "prepare clears history and smoothing");
    }
    return passed;
}

bool testStereoChunkingAndModulation() {
    bool passed = true;
    for (const auto factor :
         {OversamplingFactor::x1, OversamplingFactor::x2, OversamplingFactor::x4}) {
        Saturator whole;
        Saturator split;
        Saturator mono;
        SaturatorParameters parameters;
        parameters.driveDb = 36.0F;
        parameters.bias = 1.0F;
        parameters.tone = -1.0F;
        parameters.model = SaturatorModel::wavefolder;
        passed &= expect(whole.prepare(48000.0, factor) && split.prepare(48000.0, factor) &&
                             mono.prepare(48000.0, factor) && whole.setParameters(parameters) &&
                             split.setParameters(parameters) && mono.setParameters(parameters),
                         "prepare stereo transitions");
        auto left = tone(777.0, 1000);
        auto chunked = left;
        auto monoSamples = left;
        std::vector<float> right(left.size());
        auto chunkedRight = right;
        passed &= expect(whole.process(left, right) && mono.process(monoSamples),
                         "whole stereo and mono");
        auto leftSpan = std::span<float>(chunked);
        auto rightSpan = std::span<float>(chunkedRight);
        passed &= expect(split.process(leftSpan.first(37), rightSpan.first(37)) &&
                             split.process({}) && split.setParameters(parameters) &&
                             split.process(leftSpan.subspan(37), rightSpan.subspan(37)),
                         "chunked repeated targets");
        passed &= expect(left == chunked && right == chunkedRight && monoSamples == left,
                         "exact chunking and mono stereo equivalence");
        passed &= expect(std::all_of(right.begin(), right.end(), [](float x) { return x == 0.0F; }),
                         "no stereo crosstalk");
        for (int block = 0; block < 500; ++block) {
            parameters.model = static_cast<SaturatorModel>(block % 7);
            parameters.driveDb = block % 2 == 0 ? 0.0F : 36.0F;
            parameters.bias = block % 2 == 0 ? -1.0F : 1.0F;
            parameters.tone = -parameters.bias;
            parameters.mix = block % 3 == 0 ? 0.0F : 1.0F;
            passed &= expect(whole.setParameters(parameters), "rapid model and controls");
            std::array<float, 17> samples{};
            samples.fill(0.9F);
            passed &= expect(whole.process(samples), "process rapid models");
            for (const float sample : samples) {
                passed &= expect(std::isfinite(sample) && std::abs(sample) < 12.0F,
                                 "all models transitions finite bounded");
            }
        }
    }
    return passed;
}

bool testInvalidInputs() {
    Saturator processor;
    std::array<float, 1> sample{0.5F};
    bool passed = expect(processor.process({}) && !processor.process(sample) && sample[0] == 0.5F &&
                             !processor.setParameters({}) && processor.getLatencySamples() == 0,
                         "unprepared behavior");
    passed &= expect(processor.prepare(48000.0), "prepare errors");
    SaturatorParameters parameters;
    parameters.driveDb = 12.0F;
    passed &= expect(processor.setParameters(parameters) && processor.process(sample),
                     "begin transition");
    Saturator reference = processor;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float value : {nan, inf, -1.0F, 37.0F}) {
        parameters = {};
        parameters.driveDb = value;
        passed &= expect(!processor.setParameters(parameters), "invalid drive");
    }
    for (const float value : {nan, inf, -1.1F, 1.1F}) {
        parameters = {};
        parameters.bias = value;
        passed &= expect(!processor.setParameters(parameters), "invalid bias");
        parameters = {};
        parameters.tone = value;
        passed &= expect(!processor.setParameters(parameters), "invalid tone");
    }
    for (const float value : {nan, inf, -0.1F, 1.1F}) {
        parameters = {};
        parameters.mix = value;
        passed &= expect(!processor.setParameters(parameters), "invalid mix");
    }
    parameters = {};
    parameters.model = static_cast<SaturatorModel>(99);
    passed &= expect(!processor.setParameters(parameters) &&
                         !processor.prepare(48000.0, static_cast<OversamplingFactor>(99)) &&
                         !processor.prepare(0.0) && !processor.prepare(384001.0) &&
                         !processor.prepare(std::numeric_limits<double>::quiet_NaN()) &&
                         !processor.prepare(std::numeric_limits<double>::infinity()),
                     "invalid enum and rates");
    std::array<float, 2> left{0.1F, 0.2F};
    std::array<float, 1> right{0.3F};
    passed &= expect(!processor.process(left, right) && !processor.process({}, right) &&
                         left[0] == 0.1F && right[0] == 0.3F,
                     "invalid lengths preserve buffers");
    auto expected = left;
    passed &= expect(processor.process(left) && reference.process(expected) && left == expected,
                     "invalid calls preserve all DSP state");
    for (const double rate : {1.0, 8000.0, 384000.0}) {
        passed &= expect(processor.prepare(rate, OversamplingFactor::x4), "sample rate boundaries");
        std::array<float, 64> zero{};
        passed &= expect(processor.process(zero) && std::all_of(zero.begin(), zero.end(),
                                                                [](float x) { return x == 0.0F; }),
                         "silence finite at boundary rate");
    }
    return passed;
}

bool testObservableAliasing() {
    bool passed = true;
    std::array<double, 3> foldedThird{};
    std::array<double, 3> foldedNinth{};
    int index = 0;
    for (const auto factor :
         {OversamplingFactor::x1, OversamplingFactor::x2, OversamplingFactor::x4}) {
        Saturator processor;
        SaturatorParameters parameters;
        parameters.model = SaturatorModel::hardClip;
        parameters.driveDb = 24.0F;
        passed &= expect(processor.prepare(48000.0, factor) && processor.setParameters(parameters),
                         "prepare spectral aliasing case");
        processor.reset();
        auto samples = tone(10000.0);
        passed &= expect(processor.process(samples), "process spectral aliasing case");
        // At 48 kHz, 30 kHz (third harmonic) folds to 18 kHz; at 96 kHz,
        // 90 kHz (ninth harmonic) folds to 6 kHz. Neither bin is a legal base-rate harmonic.
        foldedThird[index] = binAmplitude(samples, 18000.0);
        foldedNinth[index] = binAmplitude(samples, 6000.0);
        std::cout << (index == 0   ? "1x"
                      : index == 1 ? "2x"
                                   : "4x")
                  << " alias 18 kHz: " << foldedThird[index]
                  << "; alias 6 kHz: " << foldedNinth[index] << '\n';
        ++index;
    }
    passed &= expect(foldedThird[0] > 0.1 && foldedThird[1] < foldedThird[0] * 0.2 &&
                         foldedThird[2] < foldedThird[0] * 0.2,
                     "2x and 4x measurably suppress folded third harmonic");
    passed &= expect(foldedNinth[2] < foldedNinth[1] * 0.7,
                     "4x further suppresses high order alias remaining at 2x");
    return passed;
}

} // namespace

int main() {
    bool passed = true;
    passed &= testCurvesAndParameters();
    passed &= testLatencyMixAndReset();
    passed &= testStereoChunkingAndModulation();
    passed &= testInvalidInputs();
    passed &= testObservableAliasing();
    return passed ? 0 : 1;
}
