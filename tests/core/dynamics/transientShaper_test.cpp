#include "ahq/audio/transientShaper.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

using ahq::audio::TransientShaper;
using ahq::audio::TransientShaperParameters;

namespace {

bool expect(bool condition, const char *caseName) {
    if (!condition) {
        std::cerr << "FAILED: " << caseName << '\n';
    }
    return condition;
}

std::vector<float> percussion() {
    std::vector<float> samples(12000);
    for (std::size_t frame = 0; frame < samples.size(); ++frame) {
        samples[frame] = static_cast<float>(0.5 * std::exp(-static_cast<double>(frame) / 2400.0));
    }
    return samples;
}

bool testAttackAndSustainDirections() {
    bool passed = true;
    const auto input = percussion();
    for (const float amount : {-12.0F, 12.0F}) {
        TransientShaper attack;
        TransientShaper sustain;
        TransientShaperParameters parameters;
        passed &= expect(attack.prepare(48000.0) && sustain.prepare(48000.0), "prepare direction");
        parameters.attackDb = amount;
        passed &= expect(attack.setParameters(parameters), "set attack direction");
        parameters.attackDb = 0.0F;
        parameters.sustainDb = amount;
        passed &= expect(sustain.setParameters(parameters), "set sustain direction");
        attack.reset();
        sustain.reset();
        auto attacked = input;
        auto sustained = input;
        passed &=
            expect(attack.process(attacked) && sustain.process(sustained), "process percussion");
        passed &=
            expect(amount > 0.0F ? attacked[0] > input[0] * 3.0F : attacked[0] < input[0] * 0.4F,
                   "attack changes onset in requested direction");
        passed &= expect(std::abs(attacked[6000] - input[6000]) < 0.00001F,
                         "attack leaves settled decay unchanged");
        passed &=
            expect(std::abs(sustained[0] - input[0]) < 0.005F, "sustain largely preserves onset");
        passed &= expect(amount > 0.0F ? sustained[6000] > input[6000] * 3.0F
                                       : sustained[6000] < input[6000] * 0.4F,
                         "sustain changes decay in requested direction");
    }
    return passed;
}

bool testUnitySilenceThresholdAndTime() {
    TransientShaper processor;
    bool passed = expect(processor.prepare(48000.0), "prepare unity");
    auto input = percussion();
    auto samples = input;
    passed &= expect(processor.process(samples) && samples == input, "zero parameters exact unity");
    TransientShaperParameters parameters;
    parameters.attackDb = 12.0F;
    parameters.sustainDb = -12.0F;
    parameters.mix = 0.0F;
    passed &= expect(processor.setParameters(parameters), "set dry");
    processor.reset();
    samples = input;
    passed &= expect(processor.process(samples) && samples == input, "dry exact identity");
    parameters.mix = 1.0F;
    parameters.sensitivityDb = -20.0F;
    passed &= expect(processor.setParameters(parameters), "set noise threshold");
    processor.reset();
    std::array<float, 1000> noise{};
    noise.fill(0.01F);
    passed &= expect(processor.process(noise), "process below threshold");
    for (const float sample : noise) {
        passed &= expect(sample == 0.01F, "below threshold exact unity");
    }
    std::array<float, 1000> silence{};
    passed &= expect(processor.process(silence), "process silence");
    for (const float sample : silence) {
        passed &= expect(sample == 0.0F, "silence remains zero");
    }

    TransientShaper shortTime;
    TransientShaper longTime;
    parameters = {};
    parameters.attackDb = 12.0F;
    parameters.timeMs = 5.0F;
    passed &= expect(shortTime.prepare(48000.0) && longTime.prepare(48000.0) &&
                         shortTime.setParameters(parameters),
                     "prepare time comparison");
    parameters.timeMs = 200.0F;
    passed &= expect(longTime.setParameters(parameters), "set long observation time");
    shortTime.reset();
    longTime.reset();
    std::array<float, 960> shortSamples{};
    shortSamples.fill(0.5F);
    auto longSamples = shortSamples;
    passed &= expect(shortTime.process(shortSamples) && longTime.process(longSamples),
                     "process time comparison");
    passed &= expect(shortSamples.back() < 0.52F && longSamples.back() > 1.5F,
                     "time controls actual onset observation duration");
    std::array<float, 48000> steady{};
    steady.fill(0.5F);
    passed &= expect(shortTime.process(steady) && std::abs(steady.back() - 0.5F) < 0.00001F,
                     "attack settles to unity for steady amplitude");
    return passed;
}

bool testStereoChunkingAndReset() {
    TransientShaper whole;
    TransientShaper split;
    TransientShaperParameters parameters;
    parameters.attackDb = 9.0F;
    parameters.sustainDb = -6.0F;
    bool passed = expect(whole.prepare(48000.0) && split.prepare(48000.0) &&
                             whole.setParameters(parameters) && split.setParameters(parameters),
                         "prepare stereo chunks");
    auto left = percussion();
    auto right = left;
    for (float &sample : right) {
        sample *= -2.0F;
    }
    auto splitLeft = left;
    auto splitRight = right;
    passed &= expect(whole.process(left, right), "whole stereo");
    auto leftSpan = std::span<float>(splitLeft);
    auto rightSpan = std::span<float>(splitRight);
    passed &= expect(split.process(leftSpan.first(73), rightSpan.first(73)) && split.process({}) &&
                         split.setParameters(parameters) &&
                         split.process(leftSpan.subspan(73), rightSpan.subspan(73)),
                     "split stereo and repeated targets");
    passed &= expect(left == splitLeft && right == splitRight, "exact chunking consistency");
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        passed &= expect(right[frame] == -2.0F * left[frame], "linked gain preserves stereo ratio");
    }
    passed &= expect(whole.prepare(44100.0), "change sample rate");
    TransientShaper fresh;
    passed &= expect(fresh.prepare(44100.0) && fresh.setParameters(parameters), "fresh new rate");
    fresh.reset();
    auto changed = percussion();
    auto expected = changed;
    passed &= expect(whole.process(changed) && fresh.process(expected) && changed == expected,
                     "prepare clears detector and parameter transitions");
    whole.reset();
    changed = percussion();
    passed &= expect(whole.process(changed) && changed == expected, "reset clears detector");
    return passed;
}

bool testInvalidAndRapidChanges() {
    TransientShaper processor;
    std::array<float, 1> sample{0.5F};
    bool passed = expect(processor.process({}) && !processor.process(sample) && sample[0] == 0.5F &&
                             !processor.setParameters({}),
                         "unprepared contract");
    passed &= expect(processor.prepare(48000.0), "prepare invalid tests");
    TransientShaperParameters parameters;
    parameters.attackDb = 6.0F;
    passed &= expect(processor.setParameters(parameters) && processor.process(sample),
                     "start transition before invalid calls");
    TransientShaper reference = processor;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const float value : {nan, infinity, -13.0F, 13.0F}) {
        parameters = {};
        parameters.attackDb = value;
        passed &= expect(!processor.setParameters(parameters), "reject attack");
        parameters = {};
        parameters.sustainDb = value;
        passed &= expect(!processor.setParameters(parameters), "reject sustain");
    }
    for (const float value : {nan, infinity, -81.0F, -19.0F}) {
        parameters = {};
        parameters.sensitivityDb = value;
        passed &= expect(!processor.setParameters(parameters), "reject sensitivity");
    }
    for (const float value : {nan, infinity, 4.0F, 201.0F}) {
        parameters = {};
        parameters.timeMs = value;
        passed &= expect(!processor.setParameters(parameters), "reject time");
    }
    for (const float value : {nan, infinity, -0.1F, 1.1F}) {
        parameters = {};
        parameters.mix = value;
        passed &= expect(!processor.setParameters(parameters), "reject mix");
    }
    passed &= expect(!processor.prepare(0.0) && !processor.prepare(384001.0) &&
                         !processor.prepare(std::numeric_limits<double>::quiet_NaN()) &&
                         !processor.prepare(std::numeric_limits<double>::infinity()),
                     "reject sample rates");
    std::array<float, 2> left{0.5F, -0.25F};
    std::array<float, 1> right{0.2F};
    passed &= expect(!processor.process(left, right) && !processor.process({}, right) &&
                         left[0] == 0.5F && right[0] == 0.2F,
                     "reject lengths without buffer changes");
    auto expected = left;
    passed &= expect(processor.process(left) && reference.process(expected) && left == expected,
                     "invalid calls preserve detection and transitions");
    for (const double rate : {1.0, 8000.0, 44100.0, 48000.0, 384000.0}) {
        passed &= expect(processor.prepare(rate), "prepare dynamic rate");
        std::array<float, 17> block{};
        for (int iteration = 0; iteration < 1000; ++iteration) {
            parameters.attackDb = iteration % 2 == 0 ? -12.0F : 12.0F;
            parameters.sustainDb = -parameters.attackDb;
            parameters.sensitivityDb = iteration % 3 == 0 ? -80.0F : -20.0F;
            parameters.timeMs = iteration % 2 == 0 ? 5.0F : 200.0F;
            parameters.mix = iteration % 3 == 0 ? 0.0F : 1.0F;
            passed &= expect(processor.setParameters(parameters), "rapid target change");
            block.fill(0.5F);
            passed &= expect(processor.process(block), "dynamic processing");
            for (const float output : block) {
                passed &= expect(std::isfinite(output) && output >= 0.125F && output <= 2.0F,
                                 "dynamic output finite and bounded by gain range");
            }
        }
    }
    return passed;
}

double energyRatioDb(const std::vector<float> &output, const std::vector<float> &input,
                     std::size_t begin, std::size_t end) {
    double outputEnergy = 0.0;
    double inputEnergy = 0.0;
    for (std::size_t frame = begin; frame < end; ++frame) {
        outputEnergy += static_cast<double>(output[frame]) * output[frame];
        inputEnergy += static_cast<double>(input[frame]) * input[frame];
    }
    return 10.0 * std::log10(outputEnergy / inputEnergy);
}

bool testBipolarSteadyTonesAndDrums() {
    bool passed = true;
    for (const double frequency : {80.0, 440.0, 1000.0}) {
        std::vector<float> input(96000);
        for (std::size_t frame = 0; frame < input.size(); ++frame) {
            input[frame] =
                static_cast<float>(0.25 * std::sin(2.0 * std::numbers::pi * frequency *
                                                   static_cast<double>(frame) / 48000.0));
        }
        for (const float attackDb : {-12.0F, 12.0F}) {
            TransientShaper shaper;
            TransientShaperParameters parameters;
            parameters.attackDb = attackDb;
            passed &= expect(shaper.prepare(48000.0) && shaper.setParameters(parameters),
                             "prepare bipolar steady tone");
            shaper.reset();
            auto output = input;
            passed &= expect(shaper.process(output), "process bipolar steady tone");
            const double steadyDb = energyRatioDb(output, input, 48000, 96000);
            std::cout << frequency << " Hz, attack " << attackDb << " dB, steady gain: " << steadyDb
                      << " dB\n";
            passed &= expect(std::abs(steadyDb) < 0.5,
                             "bipolar steady tone attack settles within 0.5 dB");
        }
    }

    std::vector<float> drum(12000);
    for (std::size_t frame = 0; frame < drum.size(); ++frame) {
        const double phase = 2.0 * std::numbers::pi * 80.0 * static_cast<double>(frame) / 48000.0;
        drum[frame] = static_cast<float>(0.5 * std::exp(-static_cast<double>(frame) / 2400.0) *
                                         std::cos(phase));
    }
    for (const float amount : {-12.0F, 12.0F}) {
        TransientShaper attack;
        TransientShaper sustain;
        TransientShaperParameters parameters;
        parameters.attackDb = amount;
        passed &= expect(attack.prepare(48000.0) && sustain.prepare(48000.0) &&
                             attack.setParameters(parameters),
                         "prepare bipolar decaying drum");
        parameters.attackDb = 0.0F;
        parameters.sustainDb = amount;
        passed &= expect(sustain.setParameters(parameters), "set bipolar drum sustain");
        attack.reset();
        sustain.reset();
        auto attacked = drum;
        auto sustained = drum;
        passed &=
            expect(attack.process(attacked) && sustain.process(sustained), "process bipolar drum");
        const double onsetDb = energyRatioDb(attacked, drum, 0, 240);
        const double decayAttackDb = energyRatioDb(attacked, drum, 6000, 9000);
        const double decaySustainDb = energyRatioDb(sustained, drum, 6000, 9000);
        passed &= expect(amount > 0.0F ? onsetDb > 8.0 : onsetDb < -8.0,
                         "bipolar drum retains substantial attack shaping");
        passed &= expect(std::abs(decayAttackDb) < 0.5,
                         "bipolar drum settled decay unaffected by attack");
        passed &= expect(amount > 0.0F ? decaySustainDb > 8.0 : decaySustainDb < -8.0,
                         "bipolar drum sustain shapes decay in both directions");
    }
    return passed;
}

} // namespace

int main() {
    bool passed = true;
    passed &= testAttackAndSustainDirections();
    passed &= testUnitySilenceThresholdAndTime();
    passed &= testStereoChunkingAndReset();
    passed &= testInvalidAndRapidChanges();
    passed &= testBipolarSteadyTonesAndDrums();
    return passed ? 0 : 1;
}
