#include "ahq/audio/trueLimiter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <span>
#include <vector>

using ahq::audio::TrueLimiter;
using ahq::audio::TrueLimiterParameters;

namespace {

bool expect(bool condition, const char *name) {
    if (!condition) {
        std::cerr << "FAILED: " << name << '\n';
    }
    return condition;
}

double fromDb(double db) { return std::pow(10.0, db / 20.0); }

// Offline time-domain reconstruction, independent of the runtime future-sum
// algorithm. The 128-frame-radius/64x reference is deliberately longer than
// the runtime FIR; a 12-frame radius checks the exact advertised finite grid.
double reconstructedPeak(std::span<const float> samples, unsigned phases, unsigned radius,
                         std::size_t start = 0) {
    double peak = 0.0;
    std::vector<std::vector<double>> coefficients(phases, std::vector<double>(2 * radius + 1));
    for (unsigned phase = 0; phase < phases; ++phase) {
        double normalization = 0.0;
        for (int tap = -static_cast<int>(radius); tap <= static_cast<int>(radius); ++tap) {
            const double distance = static_cast<double>(phase) / phases - tap;
            if (std::abs(distance) >= radius) {
                continue;
            }
            const double sinc =
                std::abs(distance) < 1.0e-12
                    ? 1.0
                    : std::sin(std::numbers::pi * distance) / (std::numbers::pi * distance);
            coefficients[phase][tap + radius] =
                sinc * (0.5 + 0.5 * std::cos(std::numbers::pi * distance / radius));
            normalization += coefficients[phase][tap + radius];
        }
        for (double &coefficient : coefficients[phase]) {
            coefficient /= normalization;
        }
    }
    for (int frame = static_cast<int>(start); frame < static_cast<int>(samples.size() + radius);
         ++frame) {
        for (unsigned phase = 0; phase < phases; ++phase) {
            double value = 0.0;
            for (int tap = -static_cast<int>(radius); tap <= static_cast<int>(radius); ++tap) {
                const int index = frame + tap;
                if (index >= 0 && index < static_cast<int>(samples.size())) {
                    value += samples[index] * coefficients[phase][tap + radius];
                }
            }
            peak = std::max(peak, std::abs(value));
        }
    }
    return peak;
}

std::vector<float> makeTone(std::size_t frames, double fraction, double amplitude,
                            double phase = 0.0) {
    std::vector<float> samples(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const double envelope =
            std::min({1.0, static_cast<double>(i) / 32, static_cast<double>(frames - 1 - i) / 32});
        samples[i] = static_cast<float>(amplitude * envelope *
                                        std::sin(2.0 * std::numbers::pi * fraction * i + phase));
    }
    return samples;
}

std::vector<float> render(std::span<const float> input, unsigned phases = 8,
                          float ceilingDb = -1.0F, std::size_t chunkSize = 137) {
    TrueLimiter limiter;
    if (!limiter.prepare(48000.0, 1, 5.0, phases)) {
        return {};
    }
    TrueLimiterParameters parameters;
    parameters.ceilingDb = ceilingDb;
    (void)limiter.setParameters(parameters);
    limiter.reset();
    std::vector<float> output(input.begin(), input.end());
    output.resize(output.size() + limiter.latencyFrames(), 0.0F);
    for (std::size_t start = 0; start < output.size(); start += chunkSize) {
        (void)limiter.process(
            std::span(output).subspan(start, std::min(chunkSize, output.size() - start)));
    }
    return output;
}

bool testCeilingReferences() {
    bool passed = true;
    std::vector<std::vector<float>> signals;
    for (double frequency : {0.02, 0.125, 0.25, 0.4, 0.45, 0.49}) {
        for (double phase : {0.0, std::numbers::pi / 4, 0.31}) {
            signals.push_back(makeTone(768, frequency, 2.0, phase));
        }
    }
    signals.push_back({2.0F});
    signals.push_back({2.0F, 2.0F});
    std::vector<float> square(768);
    std::vector<float> multitone(768);
    std::vector<float> noise(768);
    std::mt19937 random(42);
    std::uniform_real_distribution<float> distribution(-1.0F, 1.0F);
    for (std::size_t i = 0; i < 768; ++i) {
        square[i] = i % 7 < 3 ? 2.0F : -2.0F;
        multitone[i] = static_cast<float>(1.5 * std::sin(2.0 * std::numbers::pi * 0.07 * i) +
                                          1.1 * std::sin(2.0 * std::numbers::pi * 0.29 * i) +
                                          0.9 * std::sin(2.0 * std::numbers::pi * 0.43 * i));
        noise[i] = distribution(random) * (i % 113 == 0 ? 20.0F : 0.5F);
    }
    signals.push_back(square);
    signals.push_back(multitone);
    signals.push_back(noise);
    std::vector<float> whiteNoise(48000);
    for (float &sample : whiteNoise) {
        sample = distribution(random) * 2.0F;
    }
    signals.push_back(whiteNoise);
    for (unsigned phases : {4U, 8U, 16U}) {
        double maxReferenceDb = -1000.0;
        for (const auto &signal : signals) {
            const auto output = render(signal, phases);
            const double finitePeak = reconstructedPeak(output, phases, 12);
            passed &= expect(finitePeak <= fromDb(-1.0) * 1.00001,
                             "advertised finite output grid ceiling");
            const double referencePeak = reconstructedPeak(output, 64, 128);
            const double referenceDb = 20.0 * std::log10(referencePeak);
            maxReferenceDb = std::max(maxReferenceDb, referenceDb);
            if (referenceDb > -1.0 + 0.05) {
                std::cerr << "64x longer sinc overshoot phase count " << phases
                          << " peakDb=" << referenceDb << '\n';
                passed = false;
            }
        }
        std::cout << "64x reference maximum, quality " << phases << ": " << maxReferenceDb
                  << " dB\n";
    }
    return passed;
}

bool testDelayChunkingAndDry() {
    TrueLimiter limiter;
    bool passed = expect(limiter.prepare(48000.0, 1, 5.0, 8), "prepare delay");
    passed &= expect(limiter.latencyFrames() == 264, "lookahead host latency");
    std::vector<float> impulse(limiter.latencyFrames() + 2, 0.0F);
    impulse[0] = 0.1F;
    passed &= expect(limiter.process(impulse), "process delayed pulse");
    for (std::size_t i = 0; i < impulse.size(); ++i) {
        passed &= expect(impulse[i] == (i == 264 ? 0.1F : 0.0F), "exact low-level delayed samples");
    }
    passed &= expect(limiter.prepare(48000.0, 1, 20.0, 16) && limiter.latencyFrames() == 984 &&
                         limiter.oversampling() == 16,
                     "structural lookahead and quality");
    const auto input = makeTone(1024, 0.25, 3.0, std::numbers::pi / 4);
    const auto whole = render(input, 8, -1.0F, 100000);
    const auto chunks = render(input, 8, -1.0F, 7);
    passed &= expect(whole == chunks, "sample-identical chunking");
    passed &= expect(limiter.prepare(48000.0, 2, 0.0, 8), "prepare dry stereo");
    TrueLimiterParameters settings;
    settings.inputGainDb = 24.0F;
    settings.mix = 0.0F;
    passed &= expect(limiter.setParameters(settings), "set dry before protection");
    limiter.reset();
    std::vector<float> left(input.begin(), input.end());
    std::vector<float> right(input.begin(), input.end());
    for (float &value : right) {
        value *= 0.5F;
    }
    left.resize(left.size() + limiter.latencyFrames());
    right.resize(right.size() + limiter.latencyFrames());
    passed &= expect(limiter.process(left, right), "process dry stereo");
    passed &= expect(reconstructedPeak(left, 64, 128) <= fromDb(-1.0) * 1.01,
                     "dry still ceiling protected");
    for (std::size_t i = 0; i < left.size(); ++i) {
        passed &= expect(right[i] == left[i] * 0.5F, "linked gain preserves stereo ratio");
    }
    return passed;
}

bool testParameterChangesAndInvalid() {
    TrueLimiter limiter;
    TrueLimiter control;
    bool passed = expect(limiter.prepare(48000.0, 1, 0.0, 8) && control.prepare(48000.0, 1, 0.0, 8),
                         "prepare transitions");
    auto prefix = makeTone(256, 0.25, 2.0, std::numbers::pi / 4);
    auto prefixControl = prefix;
    passed &=
        expect(limiter.process(prefix) && control.process(prefixControl), "transition prefix");
    const std::array<float, 2> invalid{1.0F, std::numeric_limits<float>::quiet_NaN()};
    auto invalidCopy = invalid;
    passed &= expect(!limiter.process(invalidCopy) && invalidCopy[0] == 1.0F,
                     "finite validation is atomic");
    auto settings = TrueLimiterParameters{};
    settings.mix = -1.0F;
    passed &= expect(!limiter.setParameters(settings) && !limiter.prepare(31999.0, 1) &&
                         !limiter.prepare(48000.0, 3) && !limiter.prepare(48000.0, 1, 30.0) &&
                         !limiter.prepare(48000.0, 1, 5.0, 64),
                     "invalid parameters preserved");
    auto suffix = makeTone(256, 0.4, 2.0);
    auto suffixControl = suffix;
    passed &=
        expect(limiter.process(suffix) && control.process(suffixControl) && suffix == suffixControl,
               "invalid calls preserve full state");
    limiter.reset();
    settings = {};
    settings.ceilingDb = 0.0F;
    passed &= expect(limiter.setParameters(settings), "old higher ceiling");
    limiter.reset();
    std::vector<float> output;
    for (unsigned run = 0; run < 5; ++run) {
        auto part = makeTone(192, 0.25 + run * 0.037, 5.0, 0.31);
        settings.ceilingDb = -static_cast<float>(run * 4);
        settings.inputGainDb = run % 2 == 0 ? 12.0F : -12.0F;
        settings.mix = run % 2 == 0 ? 1.0F : 0.0F;
        settings.releaseMs = run % 2 == 0 ? 10.0F : 1000.0F;
        passed &=
            expect(limiter.setParameters(settings) && limiter.process(part), "rapid controls");
        output.insert(output.end(), part.begin(), part.end());
        const double peak =
            reconstructedPeak(output, 8, 12, static_cast<std::size_t>(run) * 192 + 24);
        passed &= expect(peak <= fromDb(settings.ceilingDb) * 1.00001,
                         "lower ceiling after support interval");
        const double longPeak =
            reconstructedPeak(output, 64, 128, static_cast<std::size_t>(run) * 192 + 128);
        passed &= expect(longPeak <= fromDb(settings.ceilingDb + 0.1),
                         "rapid controls longer independent reference after old support");
    }
    passed &= expect(reconstructedPeak(output, 8, 12) <= 1.00001,
                     "ceiling reduction never strengthens old overshoot");
    return passed;
}

bool testRealtimeLookaheadAndQuality() {
    TrueLimiter early;
    TrueLimiter late;
    bool passed = expect(early.prepare(48000.0, 1, 20.0, 8) && late.prepare(48000.0, 1, 20.0, 8),
                         "prepare fixed maximum latency");
    passed &= expect(early.setLookaheadMs(20.0) && late.setLookaheadMs(0.0) &&
                         early.latencyFrames() == 984 && late.latencyFrames() == 984,
                     "effective lookahead preserves host latency");
    std::vector<float> earlyOutput(2400, 0.0F);
    std::fill_n(earlyOutput.begin(), 1400, 0.2F);
    earlyOutput[512] = 5.0F;
    auto lateOutput = earlyOutput;
    passed &= expect(early.process(earlyOutput) && late.process(lateOutput),
                     "render changed detector advance");
    passed &= expect(earlyOutput[1100] < lateOutput[1100] * 0.5F,
                     "lookahead meaningfully changes earlier gain reduction");
    passed &= expect(reconstructedPeak(earlyOutput, 64, 128) <= fromDb(-1.0) * 1.01 &&
                         reconstructedPeak(lateOutput, 64, 128) <= fromDb(-1.0) * 1.01,
                     "both lookahead extremes longer-reference ceiling");
    TrueLimiter quality;
    passed &= expect(quality.prepare(32000.0, 1, 20.0, 4), "prepare live quality history");
    auto prefix = makeTone(104000, 0.03125, 0.1);
    passed &= expect(quality.process(prefix) && quality.inputShortTerm().valid,
                     "fill input loudness before quality switch");
    std::vector<float> output;
    for (unsigned factor : {16U, 4U, 8U, 16U}) {
        passed &=
            expect(quality.setOversamplingFactor(factor) && quality.oversampling() == factor &&
                       quality.latencyFrames() == 664 && quality.inputShortTerm().valid,
                   "quality switch retains latency and loudness history");
        auto part = makeTone(1024, 0.49, 3.0, 0.31);
        passed &= expect(quality.process(part), "render real-time quality switch");
        output.insert(output.end(), part.begin(), part.end());
        passed &= expect(reconstructedPeak(output, factor, 12, output.size() - 1024 + 24) <=
                             fromDb(-1.0) * 1.00001,
                         "new grid after phase transition interval");
    }
    passed &= expect(!quality.setLookaheadMs(20.001) &&
                         !quality.setLookaheadMs(std::numeric_limits<double>::quiet_NaN()) &&
                         !quality.setOversamplingFactor(64) && quality.oversampling() == 16 &&
                         quality.latencyFrames() == 664 && quality.inputShortTerm().valid,
                     "invalid realtime setters preserve all state");
    return passed;
}

bool testLoudnessModes() {
    constexpr double RATE = 32000.0;
    std::vector<float> tone(static_cast<std::size_t>(RATE * 5));
    for (std::size_t i = 0; i < tone.size(); ++i) {
        tone[i] = static_cast<float>(0.02 * std::sin(2.0 * std::numbers::pi * 1000 * i / RATE));
    }
    TrueLimiterParameters settings;
    settings.ceilingDb = 0.0F;
    const auto match = TrueLimiter::analyzeMatch(tone, {}, RATE, -23.0, settings);
    bool passed = expect(match.valid && match.targetReachable && !match.peakLimited,
                         "offline full-program match reaches nonlimited target");
    passed &= expect(std::abs(match.outputLufs + 23.0) <= 0.1 && match.gainDb > 0.0,
                     "offline match returns actual output and gain");
    settings.ceilingDb = -20.0F;
    const auto limited = TrueLimiter::analyzeMatch(tone, {}, RATE, -5.0, settings);
    passed &= expect(limited.valid && !limited.targetReachable && limited.peakLimited &&
                         limited.outputLufs < -5.1,
                     "offline reports ceiling limitation");
    std::vector<float> silence(tone.size());
    passed &= expect(!TrueLimiter::analyzeMatch(silence, {}, RATE, -23.0).valid,
                     "silent integrated match invalid");
    std::vector<float> gated(static_cast<std::size_t>(RATE * 8));
    for (std::size_t i = 0; i < gated.size(); ++i) {
        const double amplitude = fromDb(i < RATE * 4 ? -65.0 : -69.0);
        gated[i] =
            static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * 1000 * i / RATE));
    }
    const auto changedGate = TrueLimiter::analyzeMatch(gated, {}, RATE, -36.0);
    passed &= expect(changedGate.valid && !changedGate.peakLimited &&
                         changedGate.outputLufs < changedGate.inputLufs + changedGate.gainDb - 0.1,
                     "changed absolute gate is not falsely reported as peak limiting");
    TrueLimiter live;
    passed &= expect(live.prepare(RATE, 1, 0.0, 8), "prepare live mode");
    settings = {};
    settings.liveLoudness = true;
    settings.targetLufs = -23.0F;
    passed &= expect(live.setParameters(settings), "enable live short-term correction");
    live.reset();
    auto output = tone;
    passed &= expect(live.process(output), "render live correction");
    double early = 0.0;
    double late = 0.0;
    for (std::size_t i = 32000; i < 64000; ++i) {
        early += output[i] * output[i];
        late += output[i + 96000] * output[i + 96000];
    }
    passed &= expect(live.inputShortTerm().valid && late > early * 3.0,
                     "live correction waits for 3 s and increases level");
    live.reset();
    passed &= expect(live.process(silence) && std::all_of(silence.begin(), silence.end(),
                                                          [](float value) { return value == 0; }),
                     "live silence gate produces no noise or infinite boost");
    const std::array<std::array<double, 3>, 4> liveCases{
        {{-6.0, -23.0, 0.02}, {6.0, -23.0, 0.02}, {0.0, -36.0, 1.0}, {24.0, -36.0, 1.0}}};
    for (const auto &liveCase : liveCases) {
        const float manualGain = static_cast<float>(liveCase[0]);
        const float targetLufs = static_cast<float>(liveCase[1]);
        passed &= expect(live.prepare(RATE, 1, 0.0, 8), "prepare manual gain compensation");
        settings.inputGainDb = manualGain;
        settings.targetLufs = targetLufs;
        passed &= expect(live.setParameters(settings), "set live manual gain");
        live.reset();
        std::vector<float> longTone(static_cast<std::size_t>(RATE * 9));
        for (std::size_t i = 0; i < longTone.size(); ++i) {
            longTone[i] = static_cast<float>(liveCase[2] *
                                             std::sin(2.0 * std::numbers::pi * 1000 * i / RATE));
        }
        passed &= expect(live.process(longTone), "render compensated live gain");
        ahq::audio::LoudnessMeter outputMeter;
        passed &= expect(outputMeter.prepare(RATE, 1), "prepare actual output loudness");
        for (float sample : longTone) {
            ahq::audio::LoudnessBlock block;
            (void)outputMeter.processSample(sample, 0.0F, block);
        }
        const double measured = outputMeter.shortTerm().lufs;
        std::cout << "live manual gain " << manualGain << " target " << targetLufs
                  << " actual output S " << measured << '\n';
        if (std::abs(measured - targetLufs) > 0.1) {
            std::cerr << "live manual gain " << manualGain << " actual output S " << measured
                      << '\n';
            passed = false;
        }
    }
    return passed;
}

} // namespace

int main() {
    bool passed = testCeilingReferences();
    passed &= testDelayChunkingAndDry();
    passed &= testParameterChangesAndInvalid();
    passed &= testRealtimeLookaheadAndQuality();
    passed &= testLoudnessModes();
    return passed ? 0 : 1;
}
