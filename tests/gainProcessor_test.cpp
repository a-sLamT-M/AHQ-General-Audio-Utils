#include "ahq/audio/gainProcessor.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <span>

using ahq::audio::GainProcessor;

namespace {

bool expect(bool condition, const char *caseName) {
    if (!condition) {
        std::cerr << "FAILED: " << caseName << '\n';
    }
    return condition;
}

bool near(float actual, float expected) { return std::abs(actual - expected) <= 0.0001F; }

bool testDefaultAndEndpoints() {
    GainProcessor processor;
    bool passed = expect(processor.prepare(48000.0), "prepare default");
    std::array<float, 2> mono{0.25F, -0.5F};
    passed &= expect(processor.process(mono), "process default mono");
    passed &= expect(mono[0] == 0.25F && mono[1] == -0.5F, "default transparent");

    passed &= expect(processor.setGain(0.0F) && processor.setMix(0.0F), "set dry zero gain");
    processor.reset();
    mono = {0.25F, -0.5F};
    passed &= expect(processor.process(mono), "process dry");
    passed &= expect(mono[0] == 0.25F && mono[1] == -0.5F, "dry endpoint");

    passed &= expect(processor.setMix(1.0F), "set wet mix");
    processor.reset();
    passed &= expect(processor.process(mono), "process wet");
    passed &= expect(mono[0] == 0.0F && mono[1] == 0.0F, "wet zero gain");

    passed &= expect(processor.setGain(2.0F) && processor.setMix(0.25F), "set stereo midpoint");
    processor.reset();
    std::array<float, 1> left{0.25F};
    std::array<float, 1> right{-0.5F};
    passed &= expect(processor.process(left, right), "process stereo midpoint");
    passed &= expect(near(left[0], 0.3125F) && near(right[0], -0.625F), "stereo gain and mix");
    return passed;
}

bool testGainAndMixTransitions() {
    GainProcessor gain;
    bool passed = expect(gain.prepare(48000.0), "prepare gain transition");
    passed &= expect(gain.setGain(0.0F), "set gain target");
    passed &= expect(gain.process({}), "empty block during gain transition");
    std::array<float, 240> left;
    std::array<float, 240> right;
    left.fill(1.0F);
    right.fill(2.0F);
    passed &= expect(gain.process(left, right), "process gain transition");
    passed &= expect(near(left[0], 239.0F / 240.0F), "gain first step");
    passed &= expect(near(left[119], 0.5F), "gain halfway");
    passed &= expect(left[239] == 0.0F, "gain reaches target");
    passed &= expect(near(right[0], 2.0F * left[0]) && near(right[119], 2.0F * left[119]),
                     "stereo uses one gain per frame");

    GainProcessor mix;
    passed &= expect(mix.prepare(48000.0), "prepare mix transition");
    passed &= expect(mix.setGain(0.0F), "set gain for mix transition");
    mix.reset();
    passed &= expect(mix.setMix(0.0F), "set mix target");
    std::array<float, 240> samples;
    samples.fill(1.0F);
    passed &= expect(mix.process(samples), "process mix transition");
    passed &= expect(near(samples[0], 1.0F / 240.0F), "mix first step");
    passed &= expect(near(samples[119], 0.5F), "mix halfway");
    passed &= expect(samples[239] == 1.0F, "mix reaches dry target");
    return passed;
}

bool testChunkingAndRetarget() {
    GainProcessor whole;
    GainProcessor chunks;
    bool passed = expect(whole.prepare(48000.0) && chunks.prepare(48000.0), "prepare chunking");
    passed &= expect(whole.setGain(0.0F) && chunks.setGain(0.0F) && whole.setMix(0.0F) &&
                         chunks.setMix(0.0F),
                     "set chunked targets");
    std::array<float, 240> wholeSamples;
    std::array<float, 240> chunkSamples;
    wholeSamples.fill(0.75F);
    chunkSamples.fill(0.75F);
    passed &= expect(whole.process(wholeSamples), "whole block");
    auto chunkSpan = std::span<float>(chunkSamples);
    passed &= expect(chunks.process(chunkSpan.first(120)), "first chunk");
    passed &= expect(chunks.setGain(0.0F) && chunks.setMix(0.0F), "repeat targets mid-transition");
    passed &= expect(chunks.process(chunkSpan.last(120)), "second chunk");
    for (std::size_t frame = 0; frame < wholeSamples.size(); ++frame) {
        passed &= expect(wholeSamples[frame] == chunkSamples[frame], "chunking frame values");
    }

    GainProcessor retarget;
    passed &= expect(retarget.prepare(48000.0) && retarget.setGain(0.0F), "prepare retarget");
    std::array<float, 120> firstHalf;
    firstHalf.fill(1.0F);
    passed &= expect(retarget.process(firstHalf), "first half gain");
    passed &= expect(retarget.setGain(1.0F), "retarget gain");
    std::array<float, 240> secondRamp;
    secondRamp.fill(1.0F);
    passed &= expect(retarget.process(secondRamp), "process retarget");
    passed &= expect(near(secondRamp[0], 0.5F + 0.5F / 240.0F), "retarget starts at current gain");
    passed &= expect(secondRamp[239] == 1.0F, "retarget lasts full 240 frames");
    return passed;
}

bool testMixRetargetAndStateReset() {
    GainProcessor processor;
    bool passed =
        expect(processor.prepare(48000.0) && processor.setGain(0.0F), "prepare mix retarget");
    processor.reset();
    passed &= expect(processor.setMix(0.0F), "first mix target");
    std::array<float, 120> firstHalf;
    firstHalf.fill(1.0F);
    passed &= expect(processor.process(firstHalf), "first half mix");
    passed &= expect(processor.setMix(1.0F), "retarget mix");
    std::array<float, 240> secondRamp;
    secondRamp.fill(1.0F);
    passed &= expect(processor.process(secondRamp), "process mix retarget");
    passed &=
        expect(near(secondRamp[0], 0.5F - 0.5F / 240.0F), "mix retarget starts at current value");
    passed &= expect(secondRamp[239] == 0.0F, "mix retarget lasts full 240 frames");

    passed &= expect(processor.setGain(1.0F), "start gain transition before prepare");
    std::array<float, 1> sample{0.5F};
    passed &= expect(processor.process(sample), "advance gain before prepare");
    passed &= expect(processor.prepare(48000.0), "prepare clears gain transition");
    sample[0] = 0.5F;
    passed &=
        expect(processor.process(sample) && sample[0] == 0.5F, "prepare aligns gain with target");

    passed &= expect(processor.setGain(0.0F), "set zero gain before mix prepare");
    processor.reset();
    passed &= expect(processor.setMix(0.0F), "start mix transition before prepare");
    sample[0] = 0.5F;
    passed &= expect(processor.process(sample), "advance mix before prepare");
    passed &= expect(processor.prepare(48000.0), "prepare clears mix transition");
    sample[0] = 0.5F;
    passed &=
        expect(processor.process(sample) && sample[0] == 0.5F, "prepare aligns mix with target");
    return passed;
}

bool testInvalidAndBoundaryInputs() {
    GainProcessor processor;
    std::array<float, 1> sample{0.8F};
    bool passed = expect(processor.process({}), "empty before prepare");
    passed &=
        expect(!processor.process(sample) && sample[0] == 0.8F, "nonempty before prepare rejected");
    passed &= expect(!processor.setGain(0.5F) && !processor.setMix(0.5F),
                     "setters before prepare rejected");
    passed &= expect(!processor.prepare(0.5) && !processor.prepare(0.0) &&
                         !processor.prepare(std::numeric_limits<double>::infinity()),
                     "invalid sample rates rejected");
    passed &= expect(processor.prepare(1.0) && processor.setGain(0.0F), "minimum sample rate");
    passed &= expect(processor.process(sample) && sample[0] == 0.0F,
                     "minimum sample rate takes one frame");

    passed &=
        expect(processor.prepare(48000.0) && processor.setGain(0.5F) && processor.setMix(0.25F),
               "set valid state");
    processor.reset();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    passed &=
        expect(!processor.setGain(-1.0F) && !processor.setGain(17.0F) && !processor.setGain(nan),
               "invalid gains rejected");
    passed &= expect(!processor.setMix(-0.1F) && !processor.setMix(1.1F) && !processor.setMix(nan),
                     "invalid mixes rejected");
    passed &= expect(!processor.prepare(384001.0) &&
                         !processor.prepare(std::numeric_limits<double>::quiet_NaN()),
                     "invalid prepare rejected");
    std::array<float, 2> left{0.8F, 0.8F};
    std::array<float, 1> right{0.3F};
    passed &= expect(!processor.process(left, right), "mismatched stereo rejected");
    passed &= expect(left[0] == 0.8F && right[0] == 0.3F, "mismatched stereo preserves buffers");
    passed &= expect(processor.process(left), "process after invalid calls");
    passed &= expect(near(left[0], 0.7F), "invalid calls preserve gain and mix");
    passed &= expect(processor.prepare(384000.0), "maximum sample rate");

    GainProcessor rate44100;
    passed &= expect(rate44100.prepare(44100.0) && rate44100.setGain(0.0F), "prepare 44100");
    std::array<float, 221> samples;
    samples.fill(1.0F);
    passed &= expect(rate44100.process(samples), "44100 transition");
    passed &= expect(samples[219] > 0.0F && samples[220] == 0.0F, "44100 rounds up to 221 frames");
    return passed;
}

} // namespace

int main() {
    bool passed = true;
    passed &= testDefaultAndEndpoints();
    passed &= testGainAndMixTransitions();
    passed &= testChunkingAndRetarget();
    passed &= testMixRetargetAndStateReset();
    passed &= testInvalidAndBoundaryInputs();
    return passed ? 0 : 1;
}
