#include "ahq/audio/spaceLab.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

using ahq::audio::SpaceLab;
using ahq::audio::SpaceLabParameters;

namespace {

bool expect(bool condition, const char *caseName) {
    if (!condition) {
        std::cerr << "FAILED: " << caseName << '\n';
    }
    return condition;
}

bool testWidthPolarityAndMono() {
    SpaceLab processor;
    bool passed = expect(processor.prepare(48000.0), "prepare stereo matrix");
    std::array<float, 4> left{1.0F, -0.5F, 1.0e20F, -0.0F};
    std::array<float, 4> right{0.0F, 0.25F, 1.0F, 0.0F};
    const auto originalLeft = left;
    const auto originalRight = right;
    passed &=
        expect(processor.process(left, right) && left == originalLeft && right == originalRight,
               "default exact unity including very unequal channels");
    SpaceLabParameters parameters;
    parameters.width = 0.0F;
    passed &= expect(processor.setParameters(parameters), "width mono");
    processor.reset();
    left = {1.0F, -0.5F, 0.25F, -1.0F};
    right = {0.0F, 0.25F, 0.25F, 1.0F};
    passed &= expect(processor.process(left, right) &&
                         left == std::array<float, 4>{0.5F, -0.125F, 0.25F, 0.0F} && left == right,
                     "width zero exactly mid including antiphase cancellation");
    parameters.width = 3.0F;
    passed &= expect(processor.setParameters(parameters), "extreme width");
    processor.reset();
    std::array<float, 2> wideLeft{1.0F, 0.0F};
    std::array<float, 2> wideRight{0.0F, 1.0F};
    passed &= expect(processor.process(wideLeft, wideRight) && wideLeft[0] == 2.0F &&
                         wideRight[0] == -1.0F && wideLeft[1] == -1.0F && wideRight[1] == 2.0F,
                     "extreme left right M/S output matches matrix");
    parameters = {};
    parameters.invertLeft = true;
    passed &= expect(processor.setParameters(parameters), "invert only left");
    processor.reset();
    wideLeft = {0.5F, -0.5F};
    wideRight = wideLeft;
    passed &= expect(processor.process(wideLeft, wideRight) && wideLeft[0] == -0.5F &&
                         wideRight[0] == 0.5F,
                     "independent polarity flip");
    parameters.invertRight = true;
    parameters.invertLeft = false;
    parameters.width = 0.0F;
    passed &= expect(processor.setParameters(parameters), "mono then right polarity");
    processor.reset();
    wideLeft = {0.25F, -0.25F};
    wideRight = wideLeft;
    passed &= expect(processor.process(wideLeft, wideRight) && wideRight[0] == -wideLeft[0],
                     "duplicated mono obeys stereo controls");
    return passed;
}

bool testHaasSplitAndDry() {
    SpaceLab processor;
    bool passed = expect(processor.prepare(48000.0), "prepare Haas");
    SpaceLabParameters parameters;
    for (const float haas : {-10.0F, 10.0F}) {
        parameters.signedHaasMs = haas;
        passed &= expect(processor.setParameters(parameters), "set signed Haas");
        processor.reset();
        std::vector<float> left(1000);
        auto right = left;
        left[0] = 1.0F;
        right[0] = 1.0F;
        passed &= expect(processor.process(left, right), "process Haas impulse");
        for (std::size_t frame = 0; frame < left.size(); ++frame) {
            passed &= expect(left[frame] == (frame == (haas < 0.0F ? 480U : 0U) ? 1.0F : 0.0F) &&
                                 right[frame] == (frame == (haas > 0.0F ? 480U : 0U) ? 1.0F : 0.0F),
                             "signed Haas delays only requested side by 10 ms");
        }
    }
    parameters = {};
    parameters.split = true;
    parameters.width = 3.0F;
    parameters.leftTiltDb = 12.0F;
    parameters.invertLeft = true;
    parameters.signedHaasMs = -30.0F;
    parameters.modDepthMs = 20.0F;
    passed &= expect(processor.setParameters(parameters), "split dry left wet right");
    processor.reset();
    std::vector<float> left(2000);
    auto right = left;
    left[0] = 1.0F;
    passed &= expect(processor.process(left, right), "split pulse");
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        passed &= expect(left[frame] == (frame == 0 ? 0.5F : 0.0F) &&
                             right[frame] == (frame == 480 ? 0.5F : 0.0F),
                         "split left is unmodified M despite left controls; right M delayed");
    }
    parameters.mix = 0.0F;
    passed &= expect(processor.setParameters(parameters), "set original dry stereo");
    processor.reset();
    std::array<float, 2> dryLeft{0.125F, -0.25F};
    std::array<float, 2> dryRight{-0.375F, 0.5F};
    const auto expectedLeft = dryLeft;
    const auto expectedRight = dryRight;
    passed &= expect(processor.process(dryLeft, dryRight) && dryLeft == expectedLeft &&
                         dryRight == expectedRight,
                     "dry exact original stereo with extreme effects");
    return passed;
}

double toneAmplitude(const std::vector<float> &samples) {
    double energy = 0.0;
    for (std::size_t frame = 24000; frame < samples.size(); ++frame) {
        energy += static_cast<double>(samples[frame]) * samples[frame];
    }
    return std::sqrt(2.0 * energy / static_cast<double>(samples.size() - 24000));
}

bool testIndependentEqAndActualModulation() {
    SpaceLab processor;
    SpaceLabParameters parameters;
    parameters.leftTiltDb = -12.0F;
    parameters.rightTiltDb = 12.0F;
    bool passed = expect(processor.prepare(48000.0) && processor.setParameters(parameters),
                         "prepare independent tilt");
    for (const double frequency : {50.0, 8000.0}) {
        processor.reset();
        std::vector<float> left(48000);
        for (std::size_t frame = 0; frame < left.size(); ++frame) {
            left[frame] = static_cast<float>(0.1 * std::sin(2.0 * std::numbers::pi * frequency *
                                                            static_cast<double>(frame) / 48000.0));
        }
        auto right = left;
        passed &= expect(processor.process(left, right), "process independent tilt tone");
        const double leftAmplitude = toneAmplitude(left);
        const double rightAmplitude = toneAmplitude(right);
        std::cout << frequency << " Hz tilt left/right: " << leftAmplitude << '/' << rightAmplitude
                  << '\n';
        passed &= expect(frequency < 1000.0 ? leftAmplitude > 3.5 * rightAmplitude
                                            : rightAmplitude > 2.0 * leftAmplitude,
                         "independent EQ produces opposing low high frequency response");
    }
    parameters = {};
    parameters.modDepthMs = 20.0F;
    parameters.modRateHz = 2.0F;
    passed &= expect(processor.setParameters(parameters), "set moving right delay");
    processor.reset();
    std::vector<float> left(12000);
    std::vector<float> right(left.size());
    for (std::size_t frame = 0; frame < right.size(); ++frame) {
        right[frame] = static_cast<float>(frame) / 12000.0F;
    }
    const auto source = right;
    passed &= expect(processor.process(left, right), "process right modulated ramp");
    for (std::size_t frame = 1000; frame < right.size(); ++frame) {
        const double phase = 2.0 * std::numbers::pi * 2.0 * static_cast<double>(frame) / 48000.0;
        const double read = static_cast<double>(frame) - 480.0 * (1.0 + std::sin(phase));
        const auto first = static_cast<std::size_t>(read);
        const double fraction = read - static_cast<double>(first);
        const double expected = (1.0 - fraction) * source[first] + fraction * source[first + 1];
        passed &= expect(std::abs(right[frame] - expected) < 0.000001,
                         "actual right reads follow sinusoidal time offset");
        passed &= expect(left[frame] == 0.0F, "right modulation does not cross into left");
    }
    passed &= expect(std::abs(right[6000] - source[5040]) < 0.000001F,
                     "quarter-cycle right delay reaches full 20 ms");
    return passed;
}

bool testTransitionsChunksResetAndBoundaries() {
    SpaceLab whole;
    SpaceLab chunks;
    bool passed = expect(whole.prepare(48000.0) && chunks.prepare(48000.0), "prepare chunks");
    SpaceLabParameters parameters;
    parameters.width = 3.0F;
    parameters.signedHaasMs = 30.0F;
    parameters.leftTiltDb = -12.0F;
    parameters.rightTiltDb = 12.0F;
    parameters.invertLeft = true;
    parameters.invertRight = true;
    parameters.modRateHz = 10.0F;
    parameters.modDepthMs = 20.0F;
    parameters.split = true;
    parameters.mix = 0.4F;
    passed &= expect(whole.setParameters(parameters) && chunks.setParameters(parameters),
                     "set all controls transitioning");
    std::vector<float> left(4000, 0.5F);
    std::vector<float> right(4000, -0.25F);
    auto splitLeft = left;
    auto splitRight = right;
    passed &= expect(whole.process(left, right), "whole transitioning controls");
    auto leftSpan = std::span<float>(splitLeft);
    auto rightSpan = std::span<float>(splitRight);
    passed &= expect(chunks.process(leftSpan.first(73), rightSpan.first(73)) &&
                         chunks.process({}, {}) && chunks.setParameters(parameters) &&
                         chunks.process(leftSpan.subspan(73), rightSpan.subspan(73)),
                     "chunked controls repeated targets");
    passed &= expect(left == splitLeft && right == splitRight, "bit exact chunk consistency");
    SpaceLab polarity;
    passed &= expect(polarity.prepare(48000.0), "prepare boolean fade");
    parameters = {};
    parameters.invertLeft = true;
    passed &= expect(polarity.setParameters(parameters), "start polarity fade");
    std::array<float, 240> rampLeft{};
    rampLeft.fill(1.0F);
    auto rampRight = rampLeft;
    passed &= expect(polarity.process(rampLeft, rampRight), "polarity transition");
    passed &= expect(std::abs(rampLeft[0] - (1.0F - 2.0F / 240.0F)) < 0.000001F &&
                         std::abs(rampLeft[119]) < 0.000001F && rampLeft[239] == -1.0F &&
                         rampRight[119] == 1.0F,
                     "boolean polarity crossfade lasts exactly 5 ms");
    for (const double rate : {8000.0, 44100.0, 384000.0}) {
        passed &= expect(whole.prepare(rate), "prepare boundary rates");
        parameters.signedHaasMs = -30.0F;
        parameters.modDepthMs = 20.0F;
        parameters.modRateHz = 10.0F;
        parameters.width = 3.0F;
        passed &= expect(whole.setParameters(parameters), "extreme controls at boundary");
        for (int block = 0; block < 100; ++block) {
            parameters.split = block % 2 == 0;
            parameters.invertRight = block % 3 == 0;
            parameters.signedHaasMs = block % 2 == 0 ? -30.0F : 30.0F;
            passed &= expect(whole.setParameters(parameters), "rapid boolean delay changes");
            std::array<float, 17> blockLeft{};
            blockLeft.fill(0.5F);
            auto blockRight = blockLeft;
            passed &= expect(whole.process(blockLeft, blockRight), "rapid boundary processing");
            for (std::size_t frame = 0; frame < blockLeft.size(); ++frame) {
                passed &= expect(
                    std::isfinite(blockLeft[frame]) && std::abs(blockLeft[frame]) < 5.0F &&
                        std::isfinite(blockRight[frame]) && std::abs(blockRight[frame]) < 5.0F,
                    "all controls output finite within bound");
            }
        }
    }
    passed &= expect(whole.prepare(44100.0) && chunks.prepare(44100.0) &&
                         chunks.setParameters(parameters),
                     "new rate clears all histories");
    chunks.reset();
    left.assign(4000, 0.5F);
    right = left;
    splitLeft = left;
    splitRight = right;
    passed &= expect(whole.process(left, right) && chunks.process(splitLeft, splitRight) &&
                         left == splitLeft && right == splitRight,
                     "prepare aligns effects to targets");
    whole.reset();
    splitLeft.assign(4000, 0.5F);
    splitRight = splitLeft;
    passed &=
        expect(whole.process(splitLeft, splitRight) && splitLeft == left && splitRight == right,
               "reset clears delay EQ and modulation phase");
    return passed;
}

bool testInvalidCallsPreserveState() {
    SpaceLab processor;
    std::array<float, 1> left{0.25F};
    std::array<float, 1> right{0.5F};
    bool passed = expect(processor.process({}, {}) && !processor.process(left, right) &&
                             left[0] == 0.25F && !processor.setParameters({}),
                         "unprepared behavior");
    passed &= expect(processor.prepare(48000.0), "prepare invalid cases");
    SpaceLabParameters parameters;
    parameters.modDepthMs = 10.0F;
    parameters.split = true;
    passed &= expect(processor.setParameters(parameters) && processor.process(left, right),
                     "start state before invalid calls");
    SpaceLab reference = processor;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float value : {nan, inf, -1.0F, 4.0F}) {
        parameters = {};
        parameters.width = value;
        passed &= expect(!processor.setParameters(parameters), "invalid width");
    }
    for (const float value : {nan, inf, -31.0F, 31.0F}) {
        parameters = {};
        parameters.signedHaasMs = value;
        passed &= expect(!processor.setParameters(parameters), "invalid Haas");
    }
    for (const float value : {nan, inf, -13.0F, 13.0F}) {
        parameters = {};
        parameters.leftTiltDb = value;
        passed &= expect(!processor.setParameters(parameters), "invalid left tilt");
        parameters = {};
        parameters.rightTiltDb = value;
        passed &= expect(!processor.setParameters(parameters), "invalid right tilt");
    }
    for (const float value : {nan, inf, -1.0F, 11.0F}) {
        parameters = {};
        parameters.modRateHz = value;
        passed &= expect(!processor.setParameters(parameters), "invalid modulation rate");
    }
    for (const float value : {nan, inf, -1.0F, 21.0F}) {
        parameters = {};
        parameters.modDepthMs = value;
        passed &= expect(!processor.setParameters(parameters), "invalid modulation depth");
    }
    for (const float value : {nan, inf, -0.1F, 1.1F}) {
        parameters = {};
        parameters.mix = value;
        passed &= expect(!processor.setParameters(parameters), "invalid mix");
    }
    passed &= expect(!processor.prepare(7999.0) && !processor.prepare(384001.0) &&
                         !processor.prepare(nan) && !processor.prepare(inf),
                     "invalid sample rates");
    passed &= expect(!processor.process(left, {}) && !processor.process({}, right),
                     "reject missing stereo channel");
    std::array<float, 2> wrong{0.1F, 0.2F};
    passed &= expect(!processor.process(wrong, right) && wrong[0] == 0.1F,
                     "mismatched lengths preserve buffers");
    auto expectedLeft = left;
    auto expectedRight = right;
    passed &=
        expect(processor.process(left, right) && reference.process(expectedLeft, expectedRight) &&
                   left == expectedLeft && right == expectedRight,
               "invalid setters prepare lengths preserve complete state");
    return passed;
}

} // namespace

int main() {
    bool passed = true;
    passed &= testWidthPolarityAndMono();
    passed &= testHaasSplitAndDry();
    passed &= testIndependentEqAndActualModulation();
    passed &= testTransitionsChunksResetAndBoundaries();
    passed &= testInvalidCallsPreserveState();
    return passed ? 0 : 1;
}
