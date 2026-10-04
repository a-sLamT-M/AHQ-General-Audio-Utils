#include "ahq/audio/quickFilter.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>

using ahq::audio::FilterMode;
using ahq::audio::FilterSlope;
using ahq::audio::QuickFilter;
using ahq::audio::QuickFilterParameters;

namespace {

bool expect(bool condition, const char *caseName) {
    if (!condition) {
        std::cerr << "FAILED: " << caseName << '\n';
    }
    return condition;
}

double measureGain(FilterMode mode, FilterSlope slope, double frequency) {
    QuickFilter processor;
    QuickFilterParameters parameters;
    parameters.mode = mode;
    parameters.slope = slope;
    if (!processor.prepare(48000.0) || !processor.setParameters(parameters)) {
        return -1.0;
    }
    processor.reset();
    std::array<float, 48000> samples;
    for (std::size_t frame = 0; frame < samples.size(); ++frame) {
        samples[frame] = static_cast<float>(
            std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(frame) / 48000.0));
    }
    if (!processor.process(samples)) {
        return -1.0;
    }
    double energy = 0.0;
    for (std::size_t frame = samples.size() / 2; frame < samples.size(); ++frame) {
        energy += static_cast<double>(samples[frame]) * samples[frame];
    }
    return std::sqrt(energy * 4.0 / static_cast<double>(samples.size()));
}

bool testFrequencyResponse() {
    bool passed = true;
    double previousLow = 1.0;
    double previousHigh = 1.0;
    for (int index = 0; index < 4; ++index) {
        const auto slope = static_cast<FilterSlope>(index);
        const double lowStop = measureGain(FilterMode::lowPass, slope, 8000.0);
        const double highStop = measureGain(FilterMode::highPass, slope, 125.0);
        passed &= expect(measureGain(FilterMode::lowPass, slope, 100.0) > 0.99,
                         "low pass passes low frequencies");
        passed &= expect(measureGain(FilterMode::highPass, slope, 8000.0) > 0.99,
                         "high pass passes high frequencies");
        passed &= expect(lowStop > 0.0 && lowStop < previousLow * 0.03,
                         "each low pass section adds stopband attenuation");
        passed &= expect(highStop > 0.0 && highStop < previousHigh * 0.02,
                         "each high pass section adds stopband attenuation");
        const double cutoffGain = measureGain(FilterMode::lowPass, slope, 1000.0);
        const double expected = std::pow(1.0 / std::numbers::sqrt2, index + 1);
        passed &= expect(std::abs(cutoffGain - expected) < 0.0001,
                         "cascade cutoff response matches section Q semantics");
        previousLow = lowStop;
        previousHigh = highStop;
    }
    const double lowOctave = measureGain(FilterMode::lowPass, FilterSlope::db12, 4000.0) /
                             measureGain(FilterMode::lowPass, FilterSlope::db12, 2000.0);
    const double highOctave = measureGain(FilterMode::highPass, FilterSlope::db12, 125.0) /
                              measureGain(FilterMode::highPass, FilterSlope::db12, 250.0);
    passed &= expect(lowOctave > 0.22 && lowOctave < 0.27 && highOctave > 0.24 && highOctave < 0.26,
                     "one section attenuates about 12 dB per stopband octave");
    return passed;
}

bool testMixStereoAndReset() {
    QuickFilter dry;
    QuickFilter wet;
    QuickFilter blend;
    bool passed = expect(dry.prepare(48000.0) && wet.prepare(48000.0) && blend.prepare(48000.0),
                         "prepare mix tests");
    QuickFilterParameters parameters;
    parameters.mix = 0.0F;
    passed &= expect(dry.setParameters(parameters), "set dry");
    parameters.mix = 0.25F;
    passed &= expect(blend.setParameters(parameters), "set partial mix");
    dry.reset();
    blend.reset();
    std::array<float, 512> drySamples{};
    drySamples[0] = 1.0F;
    auto wetSamples = drySamples;
    auto blendSamples = drySamples;
    auto right = std::array<float, 512>{};
    passed &= expect(dry.process(drySamples) && wet.process(wetSamples, right) &&
                         blend.process(blendSamples),
                     "process mix tests");
    for (std::size_t frame = 0; frame < drySamples.size(); ++frame) {
        passed &= expect(drySamples[frame] == (frame == 0 ? 1.0F : 0.0F), "exact dry endpoint");
        passed &= expect(std::abs(blendSamples[frame] - (0.75F * drySamples[frame] +
                                                         0.25F * wetSamples[frame])) < 0.000001F,
                         "linear dry wet blend");
        passed &= expect(right[frame] == 0.0F, "no stereo crosstalk");
    }
    wet.reset();
    auto repeat = std::array<float, 512>{};
    repeat[0] = 1.0F;
    passed &= expect(wet.process(repeat), "process reset impulse");
    passed &= expect(repeat == wetSamples, "reset clears history");
    passed &= expect(wet.prepare(44100.0), "change sample rate");
    QuickFilter fresh;
    passed &= expect(fresh.prepare(44100.0), "prepare comparison at new rate");
    auto changedRate = std::array<float, 512>{};
    changedRate[0] = 1.0F;
    auto freshRate = changedRate;
    passed &=
        expect(wet.process(changedRate) && fresh.process(freshRate) && changedRate == freshRate,
               "prepare clears history and recalculates coefficients");
    return passed;
}

bool testChunkingAndTransitions() {
    QuickFilter whole;
    QuickFilter chunks;
    bool passed = expect(whole.prepare(48000.0) && chunks.prepare(48000.0), "prepare chunks");
    std::array<float, 500> warm{};
    warm.fill(0.25F);
    auto warmCopy = warm;
    passed &= expect(whole.process(warm) && chunks.process(warmCopy), "warm candidates");
    QuickFilterParameters parameters;
    parameters.mode = FilterMode::highPass;
    parameters.slope = FilterSlope::db48;
    parameters.frequencyHz = 16000.0F;
    parameters.q = 10.0F;
    parameters.mix = 0.3F;
    passed &= expect(whole.setParameters(parameters) && chunks.setParameters(parameters),
                     "start all transitions");
    std::array<float, 600> samples{};
    samples.fill(0.25F);
    auto split = samples;
    passed &= expect(whole.process(samples), "whole transitioning block");
    auto splitSpan = std::span<float>(split);
    passed &=
        expect(chunks.process(splitSpan.first(37)) && chunks.process({}) &&
                   chunks.setParameters(parameters) && chunks.process(splitSpan.subspan(37, 91)) &&
                   chunks.process(splitSpan.subspan(128)),
               "chunks and repeated targets preserve transitions");
    passed &= expect(samples == split, "sample exact chunking invariance");

    QuickFilter reference;
    QuickFilter switched;
    passed &= expect(reference.prepare(48000.0) && switched.prepare(48000.0), "prepare switch");
    warm.fill(0.25F);
    warmCopy = warm;
    passed &= expect(reference.process(warm) && switched.process(warmCopy), "warm switch");
    parameters = {};
    parameters.mode = FilterMode::highPass;
    passed &= expect(switched.setParameters(parameters), "switch mode");
    std::array<float, 1> unchanged{0.25F};
    auto transitioning = unchanged;
    passed &= expect(reference.process(unchanged) && switched.process(transitioning),
                     "first switch sample");
    passed &= expect(std::abs(unchanged[0] - transitioning[0]) < 0.002F,
                     "mode switch begins with small weighted step");
    return passed;
}

bool testRapidChangesAndBoundaries() {
    bool passed = true;
    for (const double sampleRate : {1.0, 8000.0, 44100.0, 48000.0, 384000.0}) {
        QuickFilter processor;
        passed &= expect(processor.prepare(sampleRate), "prepare dynamic sample rate");
        QuickFilterParameters parameters;
        std::array<float, 17> samples{};
        for (int block = 0; block < 2000; ++block) {
            parameters.mode = block % 2 == 0 ? FilterMode::lowPass : FilterMode::highPass;
            parameters.slope = static_cast<FilterSlope>(block % 4);
            parameters.frequencyHz = block % 2 == 0 ? 20.0F : 20000.0F;
            parameters.q = block % 3 == 0 ? 0.1F : 10.0F;
            parameters.mix = block % 5 == 0 ? 0.0F : 1.0F;
            passed &= expect(processor.setParameters(parameters), "rapid parameter retarget");
            for (std::size_t frame = 0; frame < samples.size(); ++frame) {
                samples[frame] = static_cast<float>(
                    std::sin(0.3 * (static_cast<double>(block * 17) + static_cast<double>(frame))));
            }
            passed &= expect(processor.process(samples), "process rapid retarget");
            for (const float sample : samples) {
                passed &= expect(std::isfinite(sample) && std::abs(sample) < 100.0F,
                                 "rapid extreme parameters remain finite and bounded");
            }
        }
        parameters.frequencyHz = 20000.0F;
        parameters.q = 10.0F;
        passed &= expect(processor.setParameters(parameters), "set sustained extreme Q");
        processor.reset();
        for (int block = 0; block < 1000; ++block) {
            samples.fill(block == 0 ? 1.0F : 0.0F);
            passed &= expect(processor.process(samples), "process extreme Q tail");
            for (const float sample : samples) {
                passed &= expect(std::isfinite(sample) && std::abs(sample) < 100.0F,
                                 "near Nyquist extreme Q tail stays bounded");
            }
        }
    }
    return passed;
}

bool testInvalidCallsPreserveState() {
    QuickFilter processor;
    std::array<float, 1> sample{0.5F};
    bool passed = expect(processor.process({}) && !processor.process(sample) && sample[0] == 0.5F,
                         "unprepared and empty behavior");
    passed &= expect(!processor.setParameters({}), "reject unprepared setter");
    passed &= expect(processor.prepare(48000.0), "prepare invalid call test");
    QuickFilterParameters parameters;
    parameters.frequencyHz = 5000.0F;
    passed &= expect(processor.setParameters(parameters) && processor.process(sample),
                     "begin transition before failures");
    QuickFilter reference = processor;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const float frequency : {nan, infinity, 19.0F, 20001.0F}) {
        parameters.frequencyHz = frequency;
        passed &= expect(!processor.setParameters(parameters), "reject invalid frequency");
    }
    parameters = {};
    for (const float q : {nan, infinity, 0.09F, 10.1F}) {
        parameters.q = q;
        passed &= expect(!processor.setParameters(parameters), "reject invalid Q");
    }
    parameters = {};
    for (const float mix : {nan, infinity, -0.1F, 1.1F}) {
        parameters.mix = mix;
        passed &= expect(!processor.setParameters(parameters), "reject invalid mix");
    }
    parameters = {};
    parameters.mode = static_cast<FilterMode>(99);
    passed &= expect(!processor.setParameters(parameters), "reject invalid mode");
    parameters = {};
    parameters.slope = static_cast<FilterSlope>(99);
    passed &= expect(!processor.setParameters(parameters), "reject invalid slope");
    passed &= expect(!processor.prepare(0.0) && !processor.prepare(384001.0) &&
                         !processor.prepare(std::numeric_limits<double>::quiet_NaN()) &&
                         !processor.prepare(std::numeric_limits<double>::infinity()),
                     "reject invalid sample rates");
    std::array<float, 2> left{0.1F, 0.2F};
    std::array<float, 1> right{0.3F};
    passed &= expect(!processor.process(left, right) && left[0] == 0.1F && right[0] == 0.3F,
                     "invalid lengths preserve buffers");
    passed &= expect(!processor.process({}, right), "reject empty left nonempty right");
    auto expected = left;
    passed &= expect(processor.process(left) && reference.process(expected) && left == expected,
                     "all invalid calls preserve filter and smoother state");
    return passed;
}

bool testClampStereoAndFadeEndpoints() {
    QuickFilter clamped;
    QuickFilter explicitCutoff;
    QuickFilterParameters parameters;
    bool passed = expect(clamped.prepare(8000.0) && explicitCutoff.prepare(8000.0),
                         "prepare cutoff clamp comparison");
    parameters.frequencyHz = 20000.0F;
    passed &= expect(clamped.setParameters(parameters), "request above Nyquist");
    parameters.frequencyHz = 3600.0F;
    passed &= expect(explicitCutoff.setParameters(parameters), "request explicit safe cutoff");
    clamped.reset();
    explicitCutoff.reset();
    std::array<float, 512> impulse{};
    impulse[0] = 1.0F;
    auto safeImpulse = impulse;
    auto right = impulse;
    passed &= expect(clamped.process(impulse, right) && explicitCutoff.process(safeImpulse),
                     "process clamp comparison");
    passed &= expect(impulse == safeImpulse && impulse == right,
                     "cutoff clamps to 0.45 rate and stereo parameters match");

    QuickFilter switched;
    QuickFilter highPass;
    passed &= expect(switched.prepare(48000.0) && highPass.prepare(48000.0),
                     "prepare fade endpoint test");
    parameters = {};
    parameters.mode = FilterMode::highPass;
    parameters.slope = FilterSlope::db48;
    passed &= expect(highPass.setParameters(parameters), "prepare destination filter");
    highPass.reset();
    std::array<float, 300> warm{};
    warm.fill(0.25F);
    auto destinationWarm = warm;
    passed &= expect(switched.process(warm) && highPass.process(destinationWarm) &&
                         switched.setParameters(parameters),
                     "warm destination before fade");
    std::array<float, 240> transition{};
    transition.fill(0.25F);
    auto destinationTransition = transition;
    passed &= expect(switched.process(transition) && highPass.process(destinationTransition),
                     "process full topology fade");
    passed &= expect(transition.back() == destinationTransition.back(),
                     "topology fade reaches destination exactly at 5 ms");
    std::array<float, 64> tail{};
    tail.fill(0.25F);
    auto destinationTail = tail;
    passed &= expect(switched.process(tail) && highPass.process(destinationTail) &&
                         tail == destinationTail,
                     "topology destination stays exact after fade");

    ahq::audio::LinearSmoother smoother;
    smoother.reset(1.0);
    smoother.setTarget(0.0, 0);
    passed &= expect(smoother.next() == 0.0, "zero frame transition aligns immediately");
    smoother.setTarget(1.0, 1);
    passed &= expect(smoother.next() == 1.0, "one frame transition reaches target");
    smoother.setTarget(0.0, 4);
    passed &= expect(smoother.next() == 0.75, "linear smoother first step");
    smoother.setTarget(0.0, 4);
    passed &= expect(smoother.next() == 0.5, "repeating smoother target preserves duration");
    smoother.setTarget(1.0, 2);
    passed &= expect(smoother.next() == 0.75 && smoother.next() == 1.0,
                     "smoother retarget starts from current value");
    return passed;
}

} // namespace

int main() {
    bool passed = true;
    passed &= testFrequencyResponse();
    passed &= testMixStereoAndReset();
    passed &= testChunkingAndTransitions();
    passed &= testRapidChangesAndBoundaries();
    passed &= testInvalidCallsPreserveState();
    passed &= testClampStereoAndFadeEndpoints();
    return passed ? 0 : 1;
}
