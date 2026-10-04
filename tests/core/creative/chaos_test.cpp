#include "ahq/audio/chaos.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

using ahq::audio::Chaos;
using ahq::audio::ChaosParameters;

namespace {

bool expect(bool condition, const char *caseName) {
    if (!condition) {
        std::cerr << "FAILED: " << caseName << '\n';
    }
    return condition;
}

std::vector<float> tone(std::size_t frames, double frequency = 1000.0) {
    std::vector<float> samples(frames);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        samples[frame] = static_cast<float>(0.25 * std::sin(2.0 * std::numbers::pi * frequency *
                                                            static_cast<double>(frame) / 48000.0));
    }
    return samples;
}

bool testProbabilityDryReverseAndRepeats() {
    Chaos processor;
    ChaosParameters parameters;
    parameters.sliceMs = 10.0F;
    bool passed = expect(processor.prepare(8000.0) && processor.setParameters(parameters),
                         "prepare probability zero");
    processor.reset();
    std::vector<float> warm(80);
    for (std::size_t frame = 0; frame < warm.size(); ++frame) {
        warm[frame] = static_cast<float>(frame) / 80.0F;
    }
    const auto source = warm;
    passed &= expect(processor.process(warm) && warm == source, "probability zero exact dry");
    parameters.probability = 1.0F;
    parameters.reverseProbability = 1.0F;
    parameters.repeats = 3;
    passed &= expect(processor.setParameters(parameters), "always reverse repeat");
    std::vector<float> output(320, 0.0F);
    passed &= expect(processor.process(output), "reverse known ramp three times");
    for (std::size_t repetition = 0; repetition < 3; ++repetition) {
        for (std::size_t age = 24; age < 56; ++age) {
            passed &= expect(output[repetition * 80 + age] == source[79 - age],
                             "reverse reads exact captured ramp in all repeats");
        }
        passed &= expect(output[repetition * 80] == 0.0F && output[repetition * 80 + 79] == 0.0F,
                         "each repetition fades seams to current dry");
    }
    passed &=
        expect(output[280] == 0.0F, "new capture after exactly three repeats contains new dry");
    parameters.mix = 0.0F;
    passed &= expect(processor.setParameters(parameters), "mix zero target");
    processor.reset();
    auto dry = source;
    passed &=
        expect(processor.process(dry) && dry == source, "mix zero exact original despite edits");
    parameters.mix = 1.0F;
    parameters.probability = 0.0F;
    parameters.panAmount = 1.0F;
    parameters.distortionProbability = 1.0F;
    parameters.driveDb = 36.0F;
    parameters.pitchRangeSemitones = 24.0F;
    passed &= expect(processor.setParameters(parameters), "probability zero all other effects");
    processor.reset();
    dry = source;
    passed &=
        expect(processor.process(dry) && dry == source, "probability zero never imposes windows");
    return passed;
}

double amplitude(const std::vector<float> &samples, double frequency) {
    double real = 0.0;
    double imaginary = 0.0;
    for (std::size_t frame = 0; frame < samples.size(); ++frame) {
        const double phase =
            2.0 * std::numbers::pi * frequency * static_cast<double>(frame) / 48000.0;
        real += samples[frame] * std::cos(phase);
        imaginary += samples[frame] * std::sin(phase);
    }
    return 2.0 * std::hypot(real, imaginary) / static_cast<double>(samples.size());
}

bool testActualPitchPanAndDistortion() {
    Chaos processor;
    ChaosParameters parameters;
    parameters.sliceMs = 250.0F;
    bool passed = expect(processor.prepare(48000.0) && processor.setParameters(parameters),
                         "prepare pitch capture");
    processor.reset();
    auto warm = tone(12000);
    passed &= expect(processor.process(warm), "capture quarter second tone");
    parameters.probability = 1.0F;
    parameters.pitchRangeSemitones = 24.0F;
    parameters.repeats = 8;
    // The LCG's third draw with this seed is exactly 0.75: random signed pitch=+12 semitones.
    parameters.seed = 795723259U;
    passed &= expect(processor.setParameters(parameters), "deterministic octave event");
    auto output = tone(48000);
    passed &= expect(processor.process(output), "replay quarter second tone at double speed");
    const double shifted = amplitude(output, 2000.0);
    const double original = amplitude(output, 1000.0);
    std::cout << "Chaos +12 semitones: 2kHz " << shifted << ", 1kHz " << original << '\n';
    passed &= expect(shifted > 0.23 && original < 0.01, "actual pitched replay frequency doubled");

    parameters = {};
    parameters.sliceMs = 10.0F;
    passed &= expect(processor.prepare(8000.0) && processor.setParameters(parameters),
                     "prepare pan distortion capture");
    processor.reset();
    warm.assign(80, 0.25F);
    auto warmRight = warm;
    passed &= expect(processor.process(warm, warmRight), "capture constant stereo");
    parameters.probability = 1.0F;
    parameters.repeats = 8;
    parameters.panAmount = 1.0F;
    parameters.seed = 795723259U;
    passed &= expect(processor.setParameters(parameters), "random attenuating pan event");
    std::vector<float> left(80, 0.25F);
    auto right = left;
    passed &= expect(processor.process(left, right) && left[40] < 0.1F && right[40] == 0.25F,
                     "random pan actually changes stereo and only attenuates");
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        passed &= expect(left[frame] >= 0.0F && left[frame] <= 0.25F && right[frame] >= 0.0F &&
                             right[frame] <= 0.25F,
                         "pan output gain bounded by one");
    }
    parameters.probability = 0.0F;
    parameters.panAmount = 0.0F;
    passed &= expect(processor.setParameters(parameters), "warm distortion reset");
    processor.reset();
    warm.assign(80, 0.25F);
    passed &= expect(processor.process(warm), "capture distortion source");
    parameters.probability = 1.0F;
    parameters.distortionProbability = 1.0F;
    parameters.driveDb = 12.0F;
    passed &= expect(processor.setParameters(parameters), "always distort event");
    left.assign(80, 0.0F);
    passed &=
        expect(processor.process(left) &&
                   std::abs(left[40] - std::tanh(0.25 * std::pow(10.0, 12.0 / 20.0))) < 0.000001,
               "distortion probability invokes actual driven tanh curve");
    return passed;
}

bool testSeedChunksResetAndSourceSafety() {
    Chaos whole;
    Chaos split;
    ChaosParameters parameters;
    parameters.probability = 0.8F;
    parameters.reverseProbability = 0.5F;
    parameters.pitchRangeSemitones = 24.0F;
    parameters.panAmount = 1.0F;
    parameters.distortionProbability = 0.5F;
    parameters.driveDb = 12.0F;
    parameters.repeats = 3;
    parameters.seed = 12345;
    bool passed = expect(whole.prepare(48000.0) && split.prepare(48000.0) &&
                             whole.setParameters(parameters) && split.setParameters(parameters),
                         "prepare random reproducibility");
    whole.reset();
    split.reset();
    const auto source = tone(96000, 777.0);
    auto left = source;
    auto splitLeft = left;
    std::vector<float> right(source.size());
    auto splitRight = right;
    passed &= expect(whole.process(left, right), "whole random edits");
    auto spanLeft = std::span<float>(splitLeft);
    auto spanRight = std::span<float>(splitRight);
    std::size_t offset = 0;
    while (offset < source.size()) {
        const auto size = std::min<std::size_t>(1 + offset % 997, source.size() - offset);
        passed &=
            expect(split.process(spanLeft.subspan(offset, size), spanRight.subspan(offset, size)) &&
                       split.setParameters(parameters) && split.process({}),
                   "irregular chunks repeated seed preserve decisions");
        offset += size;
    }
    passed &=
        expect(left == splitLeft && right == splitRight, "seed block exact events and output");
    whole.reset();
    auto repeated = source;
    passed &=
        expect(whole.process(repeated) && repeated == left, "mono matches stereo left and reset");
    for (const float sample : right) {
        passed &= expect(sample == 0.0F, "random edits do not cross left audio to right");
    }
    parameters.seed = 6789;
    passed &= expect(whole.setParameters(parameters), "change deterministic seed");
    whole.reset();
    repeated = source;
    passed &=
        expect(whole.process(repeated) && repeated != left, "different seed changes actual audio");

    parameters = {};
    parameters.sliceMs = 250.0F;
    passed &=
        expect(whole.prepare(8000.0) && whole.setParameters(parameters), "prepare longest capture");
    whole.reset();
    std::vector<float> warm(80000, 0.25F);
    passed &= expect(whole.process(warm), "warm ten seconds across nine second ring wrap");
    parameters.probability = 1.0F;
    parameters.pitchRangeSemitones = 24.0F;
    parameters.repeats = 8;
    // Third draw zero gives -24 semitones, fourfold slow play and exactly eight seconds total.
    parameters.seed = 1869465083U;
    passed &= expect(whole.setParameters(parameters), "longest slow repeated event");
    repeated.assign(64000, 0.0F);
    passed &=
        expect(whole.process(repeated), "play fixed source for eight seconds while writer moves");
    for (std::size_t repetition = 0; repetition < 8; ++repetition) {
        for (std::size_t age = 24; age < 7976; ++age) {
            passed &= expect(repeated[repetition * 8000 + age] == 0.25F,
                             "oldest slow source remains captured data not overwritten zeros");
        }
    }
    for (const double rate : {8000.0, 384000.0}) {
        passed &= expect(whole.prepare(rate), "prepare sample rate endpoints");
        repeated.assign(2000, 0.0F);
        passed &= expect(whole.process(repeated), "initial zero past history");
        for (const float sample : repeated) {
            passed &= expect(sample == 0.0F, "zero unwritten initial history finite");
        }
    }
    return passed;
}

bool testInvalidCallsAndRateReset() {
    Chaos processor;
    std::array<float, 1> sample{0.25F};
    bool passed = expect(processor.process({}) && !processor.process(sample) &&
                             sample[0] == 0.25F && !processor.setParameters({}),
                         "unprepared behavior");
    passed &= expect(processor.prepare(48000.0), "prepare errors");
    ChaosParameters parameters;
    parameters.probability = 1.0F;
    parameters.mix = 0.5F;
    passed &=
        expect(processor.setParameters(parameters) && processor.process(sample), "begin event");
    Chaos reference = processor;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float value : {nan, inf, 0.0F, 251.0F}) {
        parameters = {};
        parameters.sliceMs = value;
        passed &= expect(!processor.setParameters(parameters), "invalid slice");
    }
    for (const float value : {nan, inf, -0.1F, 1.1F}) {
        parameters = {};
        parameters.probability = value;
        passed &= expect(!processor.setParameters(parameters), "invalid event probability");
        parameters = {};
        parameters.reverseProbability = value;
        passed &= expect(!processor.setParameters(parameters), "invalid reverse probability");
        parameters = {};
        parameters.panAmount = value;
        passed &= expect(!processor.setParameters(parameters), "invalid pan");
        parameters = {};
        parameters.distortionProbability = value;
        passed &= expect(!processor.setParameters(parameters), "invalid distortion probability");
        parameters = {};
        parameters.mix = value;
        passed &= expect(!processor.setParameters(parameters), "invalid mix");
    }
    for (const float value : {nan, inf, -1.0F, 25.0F}) {
        parameters = {};
        parameters.pitchRangeSemitones = value;
        passed &= expect(!processor.setParameters(parameters), "invalid random pitch range");
    }
    for (const float value : {nan, inf, -1.0F, 37.0F}) {
        parameters = {};
        parameters.driveDb = value;
        passed &= expect(!processor.setParameters(parameters), "invalid drive");
    }
    parameters = {};
    parameters.repeats = 0;
    passed &= expect(!processor.setParameters(parameters), "invalid zero repeats");
    parameters.repeats = 9;
    passed &= expect(!processor.setParameters(parameters) && !processor.prepare(7999.0) &&
                         !processor.prepare(384001.0) && !processor.prepare(nan) &&
                         !processor.prepare(inf),
                     "invalid repeat upper bound and sample rate");
    std::array<float, 2> left{0.25F, -0.25F};
    std::array<float, 1> right{0.125F};
    passed &= expect(!processor.process(left, right) && !processor.process({}, right) &&
                         left[0] == 0.25F && right[0] == 0.125F,
                     "invalid sizes preserve buffers");
    auto actual = tone(12000);
    auto expected = actual;
    passed &= expect(processor.process(actual) && reference.process(expected) && actual == expected,
                     "invalid calls preserve history event clock PRNG and mix");
    parameters = {};
    parameters.probability = 1.0F;
    parameters.reverseProbability = 1.0F;
    passed &= expect(processor.setParameters(parameters) && processor.prepare(44100.0) &&
                         reference.prepare(44100.0) && reference.setParameters(parameters),
                     "new sample rate reset");
    reference.reset();
    actual = tone(24000);
    expected = actual;
    passed &= expect(processor.process(actual) && reference.process(expected) && actual == expected,
                     "prepare clears captured source and aligns controls");
    return passed;
}

} // namespace

int main() {
    bool passed = true;
    passed &= testProbabilityDryReverseAndRepeats();
    passed &= testActualPitchPanAndDistortion();
    passed &= testSeedChunksResetAndSourceSafety();
    passed &= testInvalidCallsAndRateReset();
    return passed ? 0 : 1;
}
