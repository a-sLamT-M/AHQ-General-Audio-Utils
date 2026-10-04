#include "ahq/audio/orbit.h"

#include "../../../src/spatial/data/kemarHorizontal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <vector>

using ahq::audio::Orbit;
using ahq::audio::OrbitDirection;
using ahq::audio::OrbitParameters;

namespace {

struct Stereo {
    std::vector<float> left;
    std::vector<float> right;
};

bool expect(bool condition, const char *caseName) {
    if (!condition) {
        std::cerr << "FAILED: " << caseName << '\n';
    }
    return condition;
}

Stereo impulse(Orbit &processor, float angle, float depth = 1.0F) {
    OrbitParameters parameters;
    parameters.rateHz = 0.0F;
    parameters.phaseDegrees = angle;
    parameters.depth = depth;
    if (!processor.setParameters(parameters)) {
        return {};
    }
    processor.reset();
    Stereo samples{std::vector<float>(2048), std::vector<float>(2048)};
    samples.left[0] = 1.0F;
    samples.right[0] = 1.0F;
    if (!processor.process(samples.left, samples.right)) {
        return {};
    }
    return samples;
}

double energy(const std::vector<float> &samples) {
    double sum = 0.0;
    for (const float sample : samples) {
        sum += static_cast<double>(sample) * sample;
    }
    return sum;
}

std::size_t peakIndex(const std::vector<float> &samples) {
    return static_cast<std::size_t>(
        std::max_element(samples.begin(), samples.end(),
                         [](float a, float b) { return std::abs(a) < std::abs(b); }) -
        samples.begin());
}

double magnitude(const std::vector<float> &samples, double rate, double frequency) {
    double real = 0.0;
    double imaginary = 0.0;
    for (std::size_t frame = 0; frame < samples.size(); ++frame) {
        const double phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(frame) / rate;
        real += samples[frame] * std::cos(phase);
        imaginary += samples[frame] * std::sin(phase);
    }
    return std::hypot(real, imaginary);
}

bool testDirectionalMeasuredImpulseResponses() {
    auto processor = std::make_unique<Orbit>();
    bool passed = expect(processor->prepare(44100.0), "prepare measured directions");
    const auto front = impulse(*processor, 0.0F);
    const auto right = impulse(*processor, 90.0F);
    const auto back = impulse(*processor, 180.0F);
    const auto left = impulse(*processor, 270.0F);
    passed &= expect(front.left.size() == 2048 && right.left.size() == 2048 &&
                         back.left.size() == 2048 && left.left.size() == 2048,
                     "render directions");
    if (!passed) {
        return false;
    }
    passed &=
        expect(front.left == front.right && back.left == back.right, "median plane symmetric ears");
    passed &= expect(right.left == left.right && right.right == left.left,
                     "left right hemisphere exactly mirrored");
    passed &= expect(front.left != back.left && front.left != right.left && back.left != right.left,
                     "front right back HRIRs actually differ");
    passed &= expect(energy(right.right) > 2.0 * energy(right.left),
                     "right direction preserves near ear energy advantage");
    passed &= expect(peakIndex(right.left) > peakIndex(right.right),
                     "right direction preserves far ear delay");
    const auto delay = processor->getLatencySamples();
    passed &= expect(delay == 16, "native rate common causal delay");
    for (std::size_t frame = 0; frame < 128; ++frame) {
        passed &= expect(std::abs(right.left[frame + delay] -
                                  ahq::audio::data::KEMAR_HORIZONTAL[18][2 * frame] / 32768.0F) <
                             0.000001F,
                         "native HRIR left PCM matches measured table");
        passed &= expect(
            std::abs(right.right[frame + delay] -
                     ahq::audio::data::KEMAR_HORIZONTAL[18][2 * frame + 1] / 32768.0F) < 0.000001F,
            "native HRIR right PCM matches measured table");
    }
    const auto frontDepth = impulse(*processor, 90.0F, 0.0F);
    const auto halfDepth = impulse(*processor, 90.0F, 0.5F);
    passed &= expect(frontDepth.left == front.left && frontDepth.right == front.right,
                     "depth zero is front HRIR");
    for (std::size_t frame = 0; frame < front.left.size(); ++frame) {
        passed &= expect(std::abs(halfDepth.left[frame] -
                                  0.5F * (front.left[frame] + right.left[frame])) < 0.000001F,
                         "depth interpolates actual directional HRIR");
    }
    const auto intermediate = impulse(*processor, 92.5F);
    const auto next = impulse(*processor, 95.0F);
    for (std::size_t frame = 0; frame < right.left.size(); ++frame) {
        passed &= expect(std::abs(intermediate.left[frame] -
                                  0.5F * (right.left[frame] + next.left[frame])) < 0.000001F,
                         "angular interpolation between measured HRIRs");
    }
    return passed;
}

bool testResamplingTimingAndGain() {
    auto processor = std::make_unique<Orbit>();
    bool passed = expect(processor->prepare(44100.0), "native timing reference");
    const auto reference = impulse(*processor, 90.0F);
    const double referenceItd = (static_cast<double>(peakIndex(reference.left)) -
                                 static_cast<double>(peakIndex(reference.right))) /
                                44100.0;
    std::cout << "44100 Hz reference HRIR peak ITD: " << referenceItd * 1000000.0 << " us\n";
    for (const double rate : {8000.0, 48000.0, 96000.0, 384000.0}) {
        passed &= expect(processor->prepare(rate), "prepare resampled HRIR");
        const auto samples = impulse(*processor, 90.0F);
        const auto mirrored = impulse(*processor, 270.0F);
        passed &= expect(samples.left == mirrored.right && samples.right == mirrored.left,
                         "resampling retains exact mirroring");
        const double itd = (static_cast<double>(peakIndex(samples.left)) -
                            static_cast<double>(peakIndex(samples.right))) /
                           rate;
        std::cout << rate << " Hz HRIR peak ITD: " << itd * 1000000.0 << " us\n";
        passed &= expect(std::abs(itd - referenceItd) < 2.0 / rate + 2.0 / 44100.0,
                         "resampling preserves interaural physical timing");
        passed &= expect(energy(samples.right) > energy(samples.left),
                         "resampling preserves right near ear");
        for (const double frequency : {0.0, 800.0, 2500.0, 5000.0, 12000.0}) {
            if (frequency >= rate * 0.4) {
                continue;
            }
            const double expected = magnitude(reference.right, 44100.0, frequency);
            const double actual = magnitude(samples.right, rate, frequency);
            passed &= expect(std::abs(actual - expected) < 0.03 * expected + 0.001,
                             "resampled HRIR magnitude and impulse gain preserved");
        }
    }
    return passed;
}

bool testDrySyncDirectionAndChunking() {
    auto whole = std::make_unique<Orbit>();
    auto split = std::make_unique<Orbit>();
    bool passed = expect(whole->prepare(48000.0) && split->prepare(48000.0), "prepare rotation");
    OrbitParameters parameters;
    parameters.rateHz = 0.5F;
    passed &= expect(whole->setParameters(parameters), "Hz rate");
    parameters.syncToBpm = true;
    parameters.bpm = 120.0F;
    parameters.beatsPerRevolution = 4.0F;
    passed &= expect(split->setParameters(parameters), "equivalent BPM rate");
    whole->reset();
    split->reset();
    std::vector<float> left(12000);
    std::vector<float> right(left.size());
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        left[frame] = static_cast<float>(0.2 * std::sin(static_cast<double>(frame) * 0.13));
        right[frame] = 0.5F * left[frame];
    }
    auto chunkLeft = left;
    auto chunkRight = right;
    passed &= expect(whole->process(left, right), "whole revolution signal");
    auto spanLeft = std::span<float>(chunkLeft);
    auto spanRight = std::span<float>(chunkRight);
    passed &= expect(split->process(spanLeft.first(137), spanRight.first(137)) &&
                         split->process({}, {}) && split->setParameters(parameters) &&
                         split->process(spanLeft.subspan(137), spanRight.subspan(137)),
                     "BPM chunks repeated target and empty block");
    passed &= expect(left == chunkLeft && right == chunkRight,
                     "BPM sync actual rate and exact chunk invariance");
    parameters.syncToBpm = false;
    parameters.rateHz = 1.0F;
    passed &= expect(whole->setParameters(parameters), "clockwise direction");
    parameters.direction = OrbitDirection::counterClockwise;
    passed &= expect(split->setParameters(parameters), "counterclockwise direction");
    whole->reset();
    split->reset();
    left.assign(12000, 0.25F);
    right = left;
    chunkLeft = left;
    chunkRight = right;
    passed &= expect(whole->process(left, right) && split->process(chunkLeft, chunkRight),
                     "opposite orbit directions");
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        passed &= expect(std::abs(left[frame] - chunkRight[frame]) < 0.00001F &&
                             std::abs(right[frame] - chunkLeft[frame]) < 0.00001F,
                         "direction reverses binaural hemisphere");
    }
    parameters.mix = 0.0F;
    passed &= expect(whole->setParameters(parameters), "dry mix");
    whole->reset();
    left = {0.5F, -0.25F, 0.75F};
    right = {-0.1F, 0.2F, -0.3F};
    left.resize(300);
    right.resize(300);
    chunkLeft = left;
    chunkRight = right;
    const auto delay = whole->getLatencySamples();
    passed &= expect(whole->process(left, right), "delayed stereo dry");
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        passed &= expect(left[frame] == (frame < delay ? 0.0F : chunkLeft[frame - delay]) &&
                             right[frame] == (frame < delay ? 0.0F : chunkRight[frame - delay]),
                         "dry exactly retains stereo with common delay");
    }
    parameters.mix = 1.0F;
    passed &= expect(whole->setParameters(parameters), "wet cancellation");
    whole->reset();
    left.assign(300, 0.25F);
    right.assign(300, -0.25F);
    passed &= expect(whole->process(left, right) && energy(left) == 0.0 && energy(right) == 0.0,
                     "documented opposite-polarity mono source cancellation");
    return passed;
}

bool testTransitionsResetAndInvalidCalls() {
    auto processor = std::make_unique<Orbit>();
    std::array<float, 1> left{0.5F};
    std::array<float, 1> right{0.25F};
    bool passed = expect(processor->process({}, {}) && !processor->process(left, right) &&
                             !processor->setParameters({}) && left[0] == 0.5F,
                         "unprepared contract");
    passed &= expect(processor->prepare(48000.0), "prepare transition boundary");
    OrbitParameters parameters;
    parameters.rateHz = 0.0F;
    parameters.phaseDegrees = 359.0F;
    passed &= expect(processor->setParameters(parameters), "phase before wrap");
    processor->reset();
    std::array<float, 300> warmLeft{};
    warmLeft.fill(0.25F);
    auto warmRight = warmLeft;
    passed &= expect(processor->process(warmLeft, warmRight), "warm wrap state");
    parameters.phaseDegrees = 1.0F;
    passed &= expect(processor->setParameters(parameters), "short arc phase target");
    std::array<float, 120> halfLeft{};
    halfLeft.fill(0.25F);
    auto halfRight = halfLeft;
    passed &= expect(processor->process(halfLeft, halfRight), "halfway shortest phase arc");
    passed &= expect(std::abs(halfLeft.back() - halfRight.back()) < 0.000001F,
                     "phase halfway is zero degrees through wrap");
    auto reference = std::make_unique<Orbit>(*processor);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const float value : {nan, infinity, -1.0F, 11.0F}) {
        parameters = {};
        parameters.rateHz = value;
        passed &= expect(!processor->setParameters(parameters), "invalid rate");
    }
    for (const float value : {nan, infinity, 0.0F, 301.0F}) {
        parameters = {};
        parameters.syncToBpm = true;
        parameters.bpm = value;
        passed &= expect(!processor->setParameters(parameters), "invalid missing BPM");
    }
    for (const float value : {nan, infinity, 0.0F, 65.0F}) {
        parameters = {};
        parameters.beatsPerRevolution = value;
        passed &= expect(!processor->setParameters(parameters), "invalid beat count");
    }
    for (const float value : {nan, infinity, -0.1F, 1.1F}) {
        parameters = {};
        parameters.depth = value;
        passed &= expect(!processor->setParameters(parameters), "invalid depth");
        parameters = {};
        parameters.mix = value;
        passed &= expect(!processor->setParameters(parameters), "invalid mix");
    }
    for (const float value : {nan, infinity, -1.0F, 361.0F}) {
        parameters = {};
        parameters.phaseDegrees = value;
        passed &= expect(!processor->setParameters(parameters), "invalid phase");
    }
    parameters = {};
    parameters.direction = static_cast<OrbitDirection>(99);
    passed &= expect(!processor->setParameters(parameters) && !processor->prepare(7999.0) &&
                         !processor->prepare(384001.0) && !processor->prepare(nan) &&
                         !processor->prepare(infinity),
                     "invalid direction and sample rates");
    passed &= expect(!processor->process(left, {}) && !processor->process({}, right) &&
                         left[0] == 0.5F && right[0] == 0.25F,
                     "invalid lengths preserve buffers");
    auto expectedLeft = left;
    auto expectedRight = right;
    passed &=
        expect(processor->process(left, right) && reference->process(expectedLeft, expectedRight) &&
                   left == expectedLeft && right == expectedRight,
               "invalid calls preserve history phase and smoothing");
    for (int block = 0; block < 100; ++block) {
        parameters = {};
        parameters.rateHz = 10.0F;
        parameters.direction =
            block % 2 == 0 ? OrbitDirection::clockwise : OrbitDirection::counterClockwise;
        parameters.phaseDegrees = block % 2 == 0 ? 359.0F : 1.0F;
        parameters.depth = block % 2 == 0 ? 0.0F : 1.0F;
        parameters.mix = parameters.depth;
        passed &= expect(processor->setParameters(parameters), "rapid orbit parameters");
        left[0] = 0.5F;
        right[0] = 0.25F;
        passed &= expect(processor->process(left, right) && std::isfinite(left[0]) &&
                             std::isfinite(right[0]) && std::abs(left[0]) < 4.0F &&
                             std::abs(right[0]) < 4.0F,
                         "finite rapid orbit output");
    }
    const auto first = impulse(*processor, 90.0F);
    const auto reset = impulse(*processor, 90.0F);
    passed &= expect(first.left == reset.left && first.right == reset.right,
                     "reset clears orbit and convolution history");
    passed &= expect(processor->prepare(96000.0), "sample rate change history clear");
    passed &= expect(reference->prepare(96000.0), "fresh comparison rate");
    const auto changed = impulse(*processor, 90.0F);
    const auto fresh = impulse(*reference, 90.0F);
    passed &= expect(changed.left == fresh.left && changed.right == fresh.right,
                     "prepare aligns reset resampled output");
    return passed;
}

} // namespace

int main() {
    bool passed = true;
    passed &= testDirectionalMeasuredImpulseResponses();
    passed &= testResamplingTimingAndGain();
    passed &= testDrySyncDirectionAndChunking();
    passed &= testTransitionsResetAndInvalidCalls();
    return passed ? 0 : 1;
}
