#include "ahq/audio/grainDelay.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

using ahq::audio::GrainDelay;
using ahq::audio::GrainDelayParameters;

namespace {

bool expect(bool condition, const char *caseName) {
    if (!condition) {
        std::cerr << "FAILED: " << caseName << '\n';
    }
    return condition;
}

std::vector<float> tone(std::size_t frames, double frequency = 1000.0) {
    std::vector<float> samples(frames);
    for (std::size_t frame = 0; frame < samples.size(); ++frame) {
        samples[frame] = static_cast<float>(0.25 * std::sin(2.0 * std::numbers::pi * frequency *
                                                            static_cast<double>(frame) / 48000.0));
    }
    return samples;
}

double amplitude(const std::vector<float> &samples, double frequency) {
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

bool testKnownDelayWindowAndDry() {
    GrainDelay processor;
    GrainDelayParameters parameters;
    parameters.grainMs = 100.0F;
    parameters.delayMs = 50.0F;
    parameters.densityHz = 40.0F;
    parameters.mix = 1.0F;
    bool passed = expect(processor.prepare(48000.0) && processor.setParameters(parameters),
                         "prepare fixed delay");
    processor.reset();
    const auto original = tone(24000);
    auto samples = original;
    passed &= expect(processor.process(samples), "fixed delay tone");
    for (std::size_t frame = 4800; frame < samples.size(); ++frame) {
        passed &= expect(std::abs(samples[frame] - original[frame - 2400]) < 0.000001F,
                         "normalized identical read heads give exact 50 ms delay");
    }
    parameters.densityHz = 1.0F;
    parameters.grainMs = 100.0F;
    parameters.delayMs = 10.0F;
    passed &= expect(processor.setParameters(parameters), "set isolated Hann grain");
    processor.reset();
    samples.assign(10000, 0.25F);
    passed &= expect(processor.process(samples), "isolated grain constant source");
    const std::size_t frame = 2400;
    const double window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * frame / 4799.0);
    passed &= expect(std::abs(samples[frame] - 0.25 * window) < 0.000001 && samples[0] == 0.0F &&
                         samples[4799] == 0.0F && samples[6000] == 0.0F,
                     "actual isolated Hann window and finite grain life");
    parameters.mix = 0.0F;
    passed &= expect(processor.setParameters(parameters), "set dry");
    processor.reset();
    samples = original;
    passed &= expect(processor.process(samples) && samples == original, "dry exact original");
    return passed;
}

bool testPitchFrequencyAndSafetyDelay() {
    GrainDelay processor;
    GrainDelayParameters parameters;
    parameters.grainMs = 80.0F;
    parameters.delayMs = 150.0F;
    parameters.densityHz = 50.0F;
    parameters.mix = 1.0F;
    bool passed = expect(processor.prepare(48000.0), "prepare pitch");
    for (const float semitones : {-12.0F, 0.0F, 12.0F}) {
        parameters.pitchSemitones = semitones;
        passed &= expect(processor.setParameters(parameters), "set pitch interval");
        processor.reset();
        auto samples = tone(96000);
        passed &= expect(processor.process(samples), "pitch tone");
        const double expectedFrequency = 1000.0 * std::exp2(semitones / 12.0);
        const double desired = amplitude(samples, expectedFrequency);
        const double wrong = amplitude(samples, expectedFrequency == 1000.0 ? 2000.0 : 1000.0);
        std::cout << "Pitch " << semitones << ": " << expectedFrequency << " Hz amplitude "
                  << desired << ", original/wrong " << wrong << '\n';
        passed &= expect(desired > 0.2 && wrong < 0.005,
                         "actual pitch frequency changes by octave ratio");
    }
    parameters.grainMs = 500.0F;
    parameters.delayMs = 10.0F;
    parameters.densityHz = 1.0F;
    parameters.pitchSemitones = 24.0F;
    passed &= expect(processor.prepare(8000.0) && processor.setParameters(parameters),
                     "prepare maximum speed minimum requested delay");
    processor.reset();
    std::vector<float> samples(5000, 0.25F);
    passed &= expect(processor.process(samples), "render causal fast grain");
    // duration 4000, speed 4: start delay is raised to (3999*3)+2=11999.
    // Earliest nonzero read is at age 3000, rather than reading future history at age 80.
    for (std::size_t frame = 0; frame < 3000; ++frame) {
        passed &= expect(samples[frame] == 0.0F, "fast read head starts sufficiently far in past");
    }
    passed &= expect(samples[3000] > 0.0F && samples[3999] == 0.0F,
                     "fast grain reaches written input and ends on zero window");
    return passed;
}

bool testRandomnessScatterStereoAndChunks() {
    GrainDelay whole;
    GrainDelay chunks;
    GrainDelayParameters parameters;
    parameters.scatter = 0.8F;
    parameters.randomness = 0.9F;
    parameters.feedback = 0.7F;
    parameters.mix = 1.0F;
    parameters.seed = 12345;
    bool passed = expect(whole.prepare(48000.0) && chunks.prepare(48000.0) &&
                             whole.setParameters(parameters) && chunks.setParameters(parameters),
                         "prepare random stereo grains");
    whole.reset();
    chunks.reset();
    const auto original = tone(48000, 777.0);
    auto left = original;
    auto splitLeft = left;
    std::vector<float> right(left.size());
    auto splitRight = right;
    passed &= expect(whole.process(left, right), "whole random stereo");
    auto leftSpan = std::span<float>(splitLeft);
    auto rightSpan = std::span<float>(splitRight);
    std::size_t offset = 0;
    while (offset < left.size()) {
        const auto size = std::min<std::size_t>(1 + offset % 257, left.size() - offset);
        passed &=
            expect(chunks.process(leftSpan.subspan(offset, size), rightSpan.subspan(offset, size)),
                   "irregular chunks");
        passed &= expect(chunks.setParameters(parameters) && chunks.process({}),
                         "repeat target and empty block preserve random clock");
        offset += size;
    }
    passed &= expect(left == splitLeft && right == splitRight,
                     "same seed arbitrary chunks bit identical");
    for (const float sample : right) {
        passed &= expect(sample == 0.0F, "right input silence has no left crosstalk");
    }
    whole.reset();
    auto repeated = original;
    passed &=
        expect(whole.process(repeated) && repeated == left, "reset restores same seed events");
    parameters.seed = 6789;
    passed &= expect(whole.setParameters(parameters), "change seed");
    whole.reset();
    repeated = original;
    passed &= expect(whole.process(repeated) && repeated != left, "different seed changes output");
    parameters.seed = 12345;
    parameters.randomness = 0.0F;
    passed &= expect(whole.setParameters(parameters), "remove random pitch pan");
    whole.reset();
    auto noRandom = original;
    passed &=
        expect(whole.process(noRandom) && noRandom != left, "randomness has actual audio effect");
    parameters.scatter = 0.0F;
    passed &= expect(whole.setParameters(parameters), "remove scatter");
    whole.reset();
    repeated = original;
    passed &= expect(whole.process(repeated) && repeated != noRandom,
                     "scatter changes read start times independently");
    whole.reset();
    repeated = original;
    auto duplicate = original;
    passed &= expect(whole.process(repeated, duplicate) && repeated == duplicate,
                     "no random pan duplicated stereo matches mono");
    return passed;
}

bool testFeedbackBoundsAndRates() {
    GrainDelay processor;
    GrainDelayParameters parameters;
    parameters.feedback = 0.95F;
    parameters.randomness = 1.0F;
    parameters.scatter = 1.0F;
    parameters.grainMs = 500.0F;
    parameters.delayMs = 2000.0F;
    parameters.densityHz = 80.0F;
    parameters.mix = 1.0F;
    bool passed = true;
    for (const double rate : {8000.0, 48000.0, 384000.0}) {
        passed &= expect(processor.prepare(rate) && processor.setParameters(parameters),
                         "prepare extreme delay rate");
        processor.reset();
        std::array<float, 256> left{};
        std::array<float, 256> right{};
        const int blocks = rate == 384000.0 ? 16 : static_cast<int>(rate * 12.0 / left.size());
        for (int block = 0; block < blocks; ++block) {
            left.fill(block < 10 ? 0.5F : 0.0F);
            right.fill(block < 10 ? -0.25F : 0.0F);
            passed &=
                expect(processor.process(left, right) && processor.getActiveGrainCount() <= 64,
                       "finite active grain upper bound");
            for (std::size_t frame = 0; frame < left.size(); ++frame) {
                passed &= expect(std::isfinite(left[frame]) && std::abs(left[frame]) < 3.0F &&
                                     std::isfinite(right[frame]) && std::abs(right[frame]) < 3.0F,
                                 "saturated feedback does not grow without bound");
            }
        }
        parameters.pitchSemitones = -24.0F;
        parameters.delayMs = 10.0F;
        passed &= expect(processor.setParameters(parameters), "extreme slow read");
        left.fill(0.25F);
        passed &= expect(processor.process(left), "slow read finite at rate boundary");
        parameters.pitchSemitones = 24.0F;
        parameters.delayMs = 2000.0F;
    }
    // Cross the complete ring length with the oldest possible slow grains. Every read must
    // still contain 0.25, not a guarded zero from already overwritten or unwritten history.
    parameters.feedback = 0.0F;
    parameters.randomness = 0.0F;
    parameters.pitchSemitones = -24.0F;
    passed &= expect(processor.prepare(8000.0) && processor.setParameters(parameters),
                     "prepare maximum historical read age");
    processor.reset();
    std::vector<float> constant(48000, 0.25F);
    passed &= expect(processor.process(constant), "render through full history wrap");
    for (std::size_t frame = 32000; frame < constant.size(); ++frame) {
        passed &= expect(std::abs(constant[frame] - 0.25F) < 0.000001F,
                         "extreme slow scattered reads retain real wrapped history");
    }
    passed &= expect(processor.getActiveGrainCount() >= 39 && processor.getActiveGrainCount() <= 40,
                     "maximum density duration has actual bounded overlap");
    return passed;
}

bool testInvalidStateAndSampleRateReset() {
    GrainDelay processor;
    std::array<float, 1> sample{0.5F};
    bool passed = expect(processor.process({}) && !processor.process(sample) && sample[0] == 0.5F &&
                             !processor.setParameters({}),
                         "unprepared behavior");
    passed &= expect(processor.prepare(48000.0), "prepare invalid tests");
    GrainDelayParameters parameters;
    parameters.feedback = 0.5F;
    parameters.randomness = 1.0F;
    parameters.mix = 1.0F;
    passed &= expect(processor.setParameters(parameters) && processor.process(sample),
                     "start valid state");
    GrainDelay reference = processor;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float value : {nan, inf, 0.0F, 501.0F}) {
        parameters = {};
        parameters.grainMs = value;
        passed &= expect(!processor.setParameters(parameters), "invalid grain time");
    }
    for (const float value : {nan, inf, 0.0F, 2001.0F}) {
        parameters = {};
        parameters.delayMs = value;
        passed &= expect(!processor.setParameters(parameters), "invalid delay time");
    }
    for (const float value : {nan, inf, -25.0F, 25.0F}) {
        parameters = {};
        parameters.pitchSemitones = value;
        passed &= expect(!processor.setParameters(parameters), "invalid pitch");
    }
    for (const float value : {nan, inf, 0.0F, 81.0F}) {
        parameters = {};
        parameters.densityHz = value;
        passed &= expect(!processor.setParameters(parameters), "invalid density");
    }
    for (const float value : {nan, inf, -0.1F, 1.1F}) {
        parameters = {};
        parameters.scatter = value;
        passed &= expect(!processor.setParameters(parameters), "invalid scatter");
        parameters = {};
        parameters.randomness = value;
        passed &= expect(!processor.setParameters(parameters), "invalid randomness");
        parameters = {};
        parameters.mix = value;
        passed &= expect(!processor.setParameters(parameters), "invalid mix");
    }
    for (const float value : {nan, inf, -0.1F, 0.96F}) {
        parameters = {};
        parameters.feedback = value;
        passed &= expect(!processor.setParameters(parameters), "invalid feedback");
    }
    passed &= expect(!processor.prepare(7999.0) && !processor.prepare(384001.0) &&
                         !processor.prepare(nan) && !processor.prepare(inf),
                     "invalid sample rate");
    std::array<float, 2> left{0.25F, -0.25F};
    std::array<float, 1> right{0.1F};
    passed &= expect(!processor.process(left, right) && !processor.process({}, right) &&
                         left[0] == 0.25F && right[0] == 0.1F,
                     "invalid lengths preserve buffers");
    auto input = tone(48000, 777.0);
    auto expected = input;
    passed &= expect(processor.process(input) && reference.process(expected) && input == expected,
                     "invalid setters prepare and lengths preserve all grain PRNG history state");
    parameters = {};
    parameters.mix = 1.0F;
    passed &= expect(processor.setParameters(parameters) && processor.prepare(44100.0) &&
                         reference.prepare(44100.0) && reference.setParameters(parameters),
                     "sample rate reset");
    reference.reset();
    input = tone(48000, 777.0);
    expected = input;
    passed &= expect(processor.process(input) && reference.process(expected) && input == expected,
                     "sample rate prepare resets history clock and transitions");
    return passed;
}

bool testAttenuatingPanAndFeedbackDecay() {
    GrainDelay processor;
    GrainDelayParameters parameters;
    parameters.grainMs = 500.0F;
    parameters.delayMs = 2000.0F;
    parameters.scatter = 1.0F;
    parameters.densityHz = 80.0F;
    parameters.randomness = 1.0F;
    parameters.mix = 1.0F;
    parameters.seed = 12345;
    bool passed = expect(processor.prepare(8000.0) && processor.setParameters(parameters),
                         "prepare attenuating pan gain bound");
    processor.reset();
    std::vector<float> left(48000, 0.25F);
    std::vector<float> right(48000, -0.5F);
    passed &= expect(processor.process(left, right), "random pan constant stereo");
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        passed &= expect(left[frame] >= 0.0F && left[frame] <= 0.25F && right[frame] <= 0.0F &&
                             right[frame] >= -0.5F,
                         "random pan and normalized overlap never amplify either channel");
    }
    parameters.feedback = 0.95F;
    passed &= expect(processor.setParameters(parameters), "set maximum feedback random pan");
    processor.reset();
    left.assign(240000, 0.0F);
    right.assign(240000, 0.0F);
    left[0] = 1.0F;
    right[0] = -0.5F;
    passed &= expect(processor.process(left, right), "single impulse then 30 seconds silence");
    double earlyPeak = 0.0;
    double latePeak = 0.0;
    for (std::size_t frame = 0; frame < 80000; ++frame) {
        earlyPeak = std::max(earlyPeak, std::abs(static_cast<double>(left[frame])));
    }
    for (std::size_t frame = 160000; frame < left.size(); ++frame) {
        latePeak = std::max(latePeak, std::abs(static_cast<double>(left[frame])));
    }
    std::cout << "Random maximum feedback impulse: early peak " << earlyPeak
              << ", final 10-second peak " << latePeak << '\n';
    passed &= expect(earlyPeak > 0.000001 && latePeak < earlyPeak * 0.5,
                     "maximum feedback and randomness tail actually decays after impulse");
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        passed &= expect(std::isfinite(left[frame]) && std::abs(left[frame]) <= 1.0F &&
                             std::isfinite(right[frame]) && std::abs(right[frame]) <= 0.5F,
                         "silent feedback output never exceeds input impulse bound");
    }
    return passed;
}

} // namespace

int main() {
    bool passed = true;
    passed &= testKnownDelayWindowAndDry();
    passed &= testPitchFrequencyAndSafetyDelay();
    passed &= testRandomnessScatterStereoAndChunks();
    passed &= testFeedbackBoundsAndRates();
    passed &= testInvalidStateAndSampleRateReset();
    passed &= testAttenuatingPanAndFeedbackDecay();
    return passed ? 0 : 1;
}
