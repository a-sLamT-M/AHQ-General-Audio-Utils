#include "ahq/audio/truePeakMeter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

using ahq::audio::TruePeakMeter;

namespace {

bool expect(bool condition, const char *name) {
    if (!condition) {
        std::cerr << "FAILED: " << name << '\n';
    }
    return condition;
}

std::vector<float> sine(double rate, double fraction, double phase, double amplitude) {
    std::vector<float> samples(static_cast<std::size_t>(rate / 5));
    const auto fade = static_cast<std::size_t>(rate / 100); // EBU 10 ms taper.
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double envelope = std::min({1.0, static_cast<double>(i) / fade,
                                          static_cast<double>(samples.size() - 1 - i) / fade});
        samples[i] = static_cast<float>(amplitude * envelope *
                                        std::sin(2.0 * std::numbers::pi * fraction * i + phase));
    }
    return samples;
}

bool testEbuSinesAndRates() {
    bool passed = true;
    // Re-created EBU Tech 3341 Table 1 signals 15-19, not downloaded test assets.
    const std::array<double, 5> fractions{0.25, 0.25, 1.0 / 6, 0.125, 0.25};
    const std::array<double, 5> phases{0.0, std::numbers::pi / 4, std::numbers::pi / 3,
                                       3.0 * std::numbers::pi / 8, std::numbers::pi / 4};
    for (double rate : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0}) {
        for (std::size_t test = 0; test < fractions.size(); ++test) {
            TruePeakMeter meter;
            passed &= expect(meter.prepare(rate, 2), "prepare EBU sine");
            const double amplitude = test == 4 ? 1.41 : 0.5;
            const auto samples = sine(rate, fractions[test], phases[test], amplitude);
            passed &= expect(meter.process(samples, samples) && meter.finish(), "process EBU sine");
            const auto peak = meter.reading();
            const double expected = test == 4 ? 3.0 : -6.0;
            if (!(peak.truePeakDb() >= expected - 0.4 && peak.truePeakDb() <= expected + 0.2)) {
                std::cerr << "EBU sine " << test << " rate " << rate << " db " << peak.truePeakDb()
                          << '\n';
                passed = false;
            }
            passed &=
                expect(peak.truePeak == meter.reading(1).truePeak, "stereo independent peaks");
            if (test == 1) {
                passed &= expect(peak.truePeak > peak.samplePeak * 1.3,
                                 "quarter-rate pi/4 intersample peak exceeds samples");
            }
        }
    }
    // Independent known bandlimited-sine amplitude, including high-frequency
    // phase scans. These finite tapered tones are only tested within their
    // theoretical amplitude tolerance; no arbitrary near-Nyquist guarantee.
    for (double fraction : {0.05, 0.125, 0.25, 0.4, 0.45, 0.47, 0.49}) {
        for (double phase : {0.0, 0.17, std::numbers::pi / 4, std::numbers::pi / 2}) {
            TruePeakMeter meter;
            passed &= expect(meter.prepare(48000.0, 1), "prepare phase scan");
            const auto samples = sine(48000.0, fraction, phase, 0.5);
            passed &= expect(meter.process(samples) && meter.finish(), "process phase scan");
            const auto peak = meter.reading();
            const double error = peak.truePeakDb() - 20.0 * std::log10(0.5);
            if (!(error >= -0.7 && error <= 0.3 && peak.truePeak >= peak.samplePeak)) {
                std::cerr << "scan frequency " << fraction << " phase " << phase << " error "
                          << error << '\n';
                passed = false;
            }
        }
    }
    return passed;
}

bool testEbuTransientOffsets() {
    // Re-create Tech 3341 signals 20-23 at 4*48 kHz: fs/6 background at 0.5,
    // one phase-continuous fs/4 cycle at 1.0, then independent anti-alias FIR
    // and four possible decimation offsets. The standard does not specify
    // the anti-alias filter coefficients; these are synthesized test signals.
    constexpr int SOURCE_FRAMES = 19200;
    constexpr int BURST_START = SOURCE_FRAMES / 2;
    constexpr int RADIUS = 128;
    std::vector<double> source(SOURCE_FRAMES);
    for (int i = 0; i < SOURCE_FRAMES; ++i) {
        double value;
        if (i < BURST_START) {
            value = 0.5 * std::sin(2.0 * std::numbers::pi * (i - BURST_START) / 24);
        } else if (i < BURST_START + 16) {
            value = std::sin(2.0 * std::numbers::pi * (i - BURST_START) / 16);
        } else {
            value = 0.5 * std::sin(2.0 * std::numbers::pi * (i - BURST_START - 16) / 24);
        }
        const double taper = std::min({1.0, static_cast<double>(i) / 1920,
                                       static_cast<double>(SOURCE_FRAMES - 1 - i) / 1920});
        source[i] = value * taper;
    }
    std::array<double, 2 * RADIUS + 1> filter{};
    double normalization = 0.0;
    for (int k = -RADIUS; k <= RADIUS; ++k) {
        const double ideal =
            k == 0 ? 0.25 : std::sin(std::numbers::pi * k / 4) / (std::numbers::pi * k);
        const double window = 0.42 + 0.5 * std::cos(std::numbers::pi * k / RADIUS) +
                              0.08 * std::cos(2.0 * std::numbers::pi * k / RADIUS);
        filter[k + RADIUS] = ideal * window;
        normalization += filter[k + RADIUS];
    }
    bool passed = true;
    for (int offset = 0; offset < 4; ++offset) {
        std::vector<float> samples;
        for (int i = offset; i < SOURCE_FRAMES; i += 4) {
            double value = 0.0;
            for (int k = -RADIUS; k <= RADIUS; ++k) {
                if (i - k >= 0 && i - k < SOURCE_FRAMES) {
                    value += source[i - k] * filter[k + RADIUS] / normalization;
                }
            }
            samples.push_back(static_cast<float>(value));
        }
        TruePeakMeter meter;
        passed &=
            expect(meter.prepare(48000.0, 2) && meter.process(samples, samples) && meter.finish(),
                   "EBU transient offset process");
        const double db = meter.reading().truePeakDb();
        if (!(db >= -0.4 && db <= 0.2)) {
            std::cerr << "EBU transient offset " << offset << " dB " << db << '\n';
            passed = false;
        }
    }
    return passed;
}

// Independent ideal bandlimited interpolation of a finite zero-padded sequence
// sampled at 64x, used for impulses and a pair of adjacent equal pulses.
double sincReference(std::span<const float> samples) {
    double peak = 0.0;
    for (int grid = -64 * 16; grid <= static_cast<int>((samples.size() + 16) * 64); ++grid) {
        const double time = static_cast<double>(grid) / 64;
        double value = 0.0;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const double x = std::numbers::pi * (time - i);
            value += samples[i] * (std::abs(x) < 1.0e-12 ? 1.0 : std::sin(x) / x);
        }
        peak = std::max(peak, std::abs(value));
    }
    return peak;
}

bool testFlushAndHistory() {
    bool passed = true;
    for (const auto &samples : {std::vector<float>{1.0F}, std::vector<float>{1.0F, 1.0F},
                                std::vector<float>{0.0F, 1.0F, 1.0F}}) {
        TruePeakMeter meter;
        TruePeakMeter padded;
        passed &= expect(meter.prepare(48000.0, 1) && padded.prepare(48000.0, 1), "prepare tails");
        passed &= expect(meter.process(samples) && padded.process(samples), "process short tails");
        const double before = meter.reading().truePeak;
        passed &= expect(meter.finish(), "flush tail");
        std::array<float, 24> zeros{};
        passed &= expect(padded.process(zeros) && padded.finish(), "explicit zero padding");
        const auto after = meter.reading();
        passed &=
            expect(after.truePeak == padded.reading().truePeak, "flush matches explicit padding");
        passed &= expect(std::abs(20.0 * std::log10(after.truePeak / sincReference(samples))) < 0.4,
                         "finite-pulse independent 64x sinc reference");
        if (samples.size() > 1) {
            passed &= expect(after.truePeak > before, "tail catches late intersample peak");
        }
        passed &= expect(meter.finish() && meter.reading().truePeak == after.truePeak,
                         "finish idempotent");
        passed &= expect(!meter.process(samples) && meter.reading().truePeak == after.truePeak,
                         "finished rejects new stream unchanged");
        meter.reset();
        passed &=
            expect(meter.reading().truePeak == 0.0 && meter.process(samples), "reset new stream");
    }
    TruePeakMeter retained;
    passed &= expect(retained.prepare(48000.0, 1), "prepare clear");
    const std::array<float, 1> pulse{1.0F};
    passed &= expect(retained.process(pulse), "input clear pulse");
    retained.clearPeaks();
    passed &= expect(retained.reading().truePeak == 0.0 && retained.finish() &&
                         retained.reading().truePeak > 0.9 && retained.reading().samplePeak == 0.0,
                     "clear retains interpolation history");
    retained.reset();
    passed &= expect(retained.finish() && retained.reading().truePeak == 0.0,
                     "reset clears interpolation history");
    return passed;
}

bool testChunkingAndInvalid() {
    const auto left = sine(48000.0, 0.25, std::numbers::pi / 4, 0.5);
    auto right = left;
    for (float &sample : right) {
        sample *= 0.2F;
    }
    TruePeakMeter whole;
    TruePeakMeter chunks;
    bool passed = expect(!whole.finish() && !whole.process(left), "unprepared rejected");
    passed &= expect(whole.prepare(48000.0, 2) && chunks.prepare(48000.0, 2), "prepare chunks");
    passed &= expect(whole.process(left, right) && whole.finish(), "whole stream");
    for (std::size_t offset = 0; offset < left.size(); offset += 137) {
        const auto count = std::min<std::size_t>(137, left.size() - offset);
        passed &= expect(chunks.process(std::span(left).subspan(offset, count),
                                        std::span(right).subspan(offset, count)),
                         "chunk stream");
    }
    passed &= expect(chunks.finish(), "finish chunks");
    for (unsigned channel = 0; channel < 2; ++channel) {
        passed &=
            expect(whole.reading(channel).truePeak == chunks.reading(channel).truePeak &&
                       whole.reading(channel).samplePeak == chunks.reading(channel).samplePeak,
                   "chunk-independent peaks");
    }
    TruePeakMeter meter;
    TruePeakMeter control;
    passed &= expect(meter.prepare(48000.0, 1) && control.prepare(48000.0, 1), "prepare invalid");
    const std::array<float, 2> pair{0.2F, 0.5F};
    passed &= expect(meter.process(pair) && control.process(pair), "valid prefix");
    const std::array<float, 2> invalid{1.0F, std::numeric_limits<float>::quiet_NaN()};
    passed &=
        expect(!meter.process(invalid) && !meter.process(pair, pair) && !meter.prepare(7999.0, 1) &&
                   !meter.prepare(384001.0, 2) && !meter.prepare(48000.0, 3) &&
                   !meter.prepare(std::numeric_limits<double>::infinity(), 1),
               "invalid inputs rejected atomically");
    passed &= expect(meter.finish() && control.finish() &&
                         meter.reading().truePeak == control.reading().truePeak,
                     "invalid calls preserve complete interpolation history");
    passed &= expect(
        meter.reading(1).truePeak == 0.0 && std::isinf(meter.reading(1).truePeakDb()) &&
            meter.reading(1).truePeakDb() < 0.0 && !std::isnan(meter.reading(99).samplePeakDb()),
        "inactive and zero db");
    return passed;
}

} // namespace

int main() {
    bool passed = testEbuSinesAndRates();
    passed &= testEbuTransientOffsets();
    passed &= testFlushAndHistory();
    passed &= testChunkingAndInvalid();
    return passed ? 0 : 1;
}
