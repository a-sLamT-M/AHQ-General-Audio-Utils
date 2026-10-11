#include "ahq/audio/loudnessMeter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <vector>

using ahq::audio::IntegratedLoudness;
using ahq::audio::LoudnessBlock;
using ahq::audio::LoudnessMeter;

namespace {

bool expect(bool condition, const char *name) {
    if (!condition) {
        std::cerr << "FAILED: " << name << '\n';
    }
    return condition;
}

bool near(double actual, double expected, double tolerance = 0.05) {
    if (std::abs(actual - expected) <= tolerance) {
        return true;
    }
    std::cerr << "actual=" << actual << " expected=" << expected << '\n';
    return false;
}

double energyAt(double lufs) { return std::pow(10.0, (lufs + 0.691) / 10.0); }

void tone(LoudnessMeter &meter, double rate, double frequency, double amplitude, unsigned seconds,
          IntegratedLoudness *integrated = nullptr) {
    LoudnessBlock block;
    const auto frames = static_cast<std::size_t>(rate * seconds);
    for (std::size_t i = 0; i < frames; ++i) {
        const auto sample =
            static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * frequency *
                                                    static_cast<double>(i) / rate));
        if (!meter.processSample(sample, sample, block)) {
            std::cerr << "tone rejected\n";
        }
        if (integrated != nullptr && block.ready) {
            (void)integrated->addBlock(block.energy);
        }
    }
}

// Independent steady-state transfer calculation from the published 48 kHz
// ITU tables. At other rates the same analogue frequency is obtained by the
// inverse bilinear frequency mapping, rather than applying a time-domain filter.
double mappedReferenceLoudness(double rate, double frequency) {
    const double omega =
        2.0 * std::atan(rate / 48000.0 * std::tan(std::numbers::pi * frequency / rate));
    const auto z = std::polar(1.0, -omega);
    const auto shelf = (1.53512485958697 - 2.69169618940638 * z + 1.19839281085285 * z * z) /
                       (1.0 - 1.69065929318241 * z + 0.73248077421585 * z * z);
    const auto highPass =
        (1.0 - 2.0 * z + z * z) / (1.0 - 1.99004745483398 * z + 0.99007225036621 * z * z);
    return -0.691 + 10.0 * std::log10(0.5 * std::norm(shelf * highPass));
}

// The actual standard response target at the SAME physical Hz: directly
// evaluate the published 48 kHz table, without remapping the test frequency.
double standardReferenceLoudness(double frequency) {
    const auto z = std::polar(1.0, -2.0 * std::numbers::pi * frequency / 48000.0);
    const auto shelf = (1.53512485958697 - 2.69169618940638 * z + 1.19839281085285 * z * z) /
                       (1.0 - 1.69065929318241 * z + 0.73248077421585 * z * z);
    const auto highPass =
        (1.0 - 2.0 * z + z * z) / (1.0 - 1.99004745483398 * z + 0.99007225036621 * z * z);
    return -0.691 + 10.0 * std::log10(0.5 * std::norm(shelf * highPass));
}

bool testCalibrationAndWeighting() {
    bool passed = true;
    LoudnessMeter mono;
    LoudnessMeter stereo;
    passed &= expect(mono.prepare(48000.0, 1) && stereo.prepare(48000.0, 2), "prepare tones");
    IntegratedLoudness integrated;
    // ITU Annex 1: full-scale 997 Hz mono is approximately -3.01 LKFS.
    tone(mono, 48000.0, 997.0, 1.0, 4, &integrated);
    tone(stereo, 48000.0, 997.0, 1.0, 4);
    passed &= expect(near(mono.shortTerm().lufs, -3.01), "ITU 997 Hz mono calibration");
    passed &= expect(near(integrated.reading().lufs, -3.01), "integrated ITU calibration");
    passed &= expect(
        near(stereo.shortTerm().lufs - mono.shortTerm().lufs, 10.0 * std::log10(2.0), 1.0e-8),
        "stereo energy sums");
    stereo.reset();
    tone(stereo, 48000.0, 1000.0, std::pow(10.0, -23.0 / 20.0), 4);
    // EBU Tech 3341 Table 1 test 1: stereo 1 kHz at -23 dBFS peak.
    passed &= expect(near(stereo.shortTerm().lufs, -23.0, 0.1), "EBU stereo calibration");
    for (double rate : {32000.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0}) {
        passed &= expect(stereo.prepare(rate, 2), "prepare EBU calibration across rates");
        tone(stereo, rate, 1000.0, std::pow(10.0, -23.0 / 20.0), 4);
        passed &= expect(near(stereo.shortTerm().lufs, -23.0, 0.1),
                         "EBU stereo calibration across supported rates");
        for (double frequency : {50.0, 1000.0, 3000.0}) {
            passed &= expect(mono.prepare(rate, 1), "prepare frequency response");
            tone(mono, rate, frequency, 1.0, 4);
            passed &=
                expect(near(mono.shortTerm().lufs, mappedReferenceLoudness(rate, frequency), 0.001),
                       "bilinear mapping implementation across rates");
        }
        for (double frequency : {50.0, 1000.0, 2000.0, 3000.0, 8000.0, 14000.0}) {
            passed &= expect(mono.prepare(rate, 1), "prepare physical response test");
            tone(mono, rate, frequency, 1.0, 4);
            passed &=
                expect(near(mono.shortTerm().lufs, standardReferenceLoudness(frequency), 0.025),
                       "same-Hz official response accuracy");
        }
        const double upperFrequency = std::min(20000.0, 0.45 * rate);
        for (int i = 0; i <= 20000; ++i) {
            const double frequency = 50.0 + (upperFrequency - 50.0) * i / 20000.0;
            passed &= expect(near(mappedReferenceLoudness(rate, frequency),
                                  standardReferenceLoudness(frequency), 0.025),
                             "dense same-Hz response sweep");
        }
    }
    return passed;
}

bool testWindowsAndState() {
    LoudnessMeter meter;
    LoudnessBlock block{123.0, 7, true};
    bool passed = expect(!meter.processSample(0.0F, 0.0F, block) && block.energy == 123.0,
                         "unprepared preserves output");
    passed &=
        expect(!meter.momentary().valid && !meter.shortTerm().valid, "unprepared readings invalid");
    passed &= expect(meter.prepare(32000.0, 1), "prepare windows");
    for (int i = 1; i <= 96000; ++i) {
        passed &= expect(meter.processSample(0.0F, std::numeric_limits<float>::quiet_NaN(), block),
                         "mono ignores right");
        if (i == 12799) {
            passed &= expect(!meter.momentary().valid && !block.ready, "400 ms not yet complete");
        }
        if (i == 12800) {
            passed &= expect(meter.momentary().valid && block.ready && block.endFrame == 12800,
                             "400 ms complete");
        }
        if (i == 95999) {
            passed &= expect(!meter.shortTerm().valid, "3 s not yet complete");
        }
    }
    passed &= expect(meter.shortTerm().valid && std::isinf(meter.shortTerm().lufs) &&
                         meter.shortTerm().lufs < 0.0,
                     "silent window is negative infinity");
    tone(meter, 32000.0, 1000.0, 1.0, 4);
    for (int i = 0; i < 320000; ++i) {
        (void)meter.processSample(0.0F, 0.0F, block);
    }
    passed &= expect(std::isinf(meter.momentary().lufs) && std::isinf(meter.shortTerm().lufs),
                     "silence clears both energy windows and filter tails");
    meter.reset();
    passed &=
        expect(!meter.momentary().valid && !meter.shortTerm().valid, "reset invalidates windows");
    (void)meter.processSample(0.0F, 0.0F, block);
    passed &= expect(!meter.prepare(31999.0, 1) && !meter.prepare(384001.0, 2) &&
                         !meter.prepare(48000.0, 3) &&
                         !meter.prepare(std::numeric_limits<double>::quiet_NaN(), 1),
                     "invalid configuration");
    block = {9.0, 17, true};
    passed &= expect(!meter.processSample(std::numeric_limits<float>::infinity(), 0.0F, block) &&
                         block.energy == 9.0 && block.endFrame == 17 && block.ready,
                     "invalid sample preserves output and state");
    for (int i = 1; i < 12800; ++i) {
        (void)meter.processSample(0.0F, 0.0F, block);
    }
    passed &= expect(block.ready && block.endFrame == 12800, "invalid calls preserve frame timing");
    passed &= expect(meter.prepare(44101.0, 2), "rate and channel change");
    passed &= expect(!meter.momentary().valid, "prepare clears readings");
    for (int i = 0; i < 17640; ++i) {
        (void)meter.processSample(0.0F, 0.0F, block);
    }
    passed &= expect(block.ready && block.endFrame == 17640, "fractional durations round");
    for (int i = 0; i < 4410; ++i) {
        (void)meter.processSample(0.0F, 0.0F, block);
    }
    passed &= expect(block.ready && block.endFrame == 22050, "step rounds independently");
    return passed;
}

bool testIntegratedGates() {
    IntegratedLoudness meter;
    bool passed = expect(!meter.reading().valid, "empty integrated invalid");
    passed &= expect(meter.addBlock(0.0) && meter.addBlock(energyAt(-80.0)) &&
                         meter.addBlock(energyAt(-70.0)),
                     "absolute gate inputs");
    passed &= expect(!meter.reading().valid, "absolute threshold is strict");
    passed &= expect(meter.addBlock(energyAt(-23.0)), "add loud block");
    passed &= expect(near(meter.reading().lufs, -23.0, 1.0e-10), "absolute gate removes quiet");
    passed &= expect(meter.addBlock(energyAt(-43.0)), "add relatively quiet block");
    passed &= expect(near(meter.reading().lufs, -23.0, 1.0e-10), "relative gate removes quiet");
    passed &= expect(meter.addBlock(energyAt(-26.0)), "add included quieter block");
    const double expected = -0.691 + 10.0 * std::log10((energyAt(-23.0) + energyAt(-26.0)) / 2);
    passed &= expect(near(meter.reading().lufs, expected, 1.0e-10), "average energy not LUFS");
    const auto count = meter.blockCount();
    passed &=
        expect(!meter.addBlock(-1.0) && !meter.addBlock(std::numeric_limits<double>::quiet_NaN()) &&
                   meter.blockCount() == count,
               "invalid energy preserves history");
    meter.reset();
    passed &= expect(meter.blockCount() == 0 && !meter.reading().valid, "reset integrated");
    return passed;
}

bool testChunking() {
    constexpr std::size_t FRAMES = 48000 * 4 + 123;
    std::vector<float> samples(FRAMES);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<float>(0.2 * std::sin(2.0 * std::numbers::pi * 997.0 * i / 48000));
    }
    std::array<std::vector<LoudnessBlock>, 2> blocks;
    bool passed = true;
    for (std::size_t run = 0; run < 2; ++run) {
        LoudnessMeter meter;
        passed &= expect(meter.prepare(48000.0, 1), "prepare chunk test");
        const std::size_t chunk = run == 0 ? FRAMES : 7777;
        for (std::size_t start = 0; start < FRAMES; start += chunk) {
            for (std::size_t i = start; i < std::min(start + chunk, FRAMES); ++i) {
                LoudnessBlock block;
                (void)meter.processSample(samples[i], 0.0F, block);
                if (block.ready) {
                    blocks[run].push_back(block);
                }
            }
        }
    }
    passed &= expect(blocks[0].size() == 37 && blocks[1].size() == 37,
                     "all overlapping blocks emitted and partial tail discarded");
    for (std::size_t i = 0; i < blocks[0].size(); ++i) {
        passed &= expect(blocks[0][i].energy == blocks[1][i].energy &&
                             blocks[0][i].endFrame == 19200 + i * 4800 &&
                             blocks[0][i].endFrame == blocks[1][i].endFrame,
                         "chunk-independent energy and frame positions");
    }
    return passed;
}

} // namespace

int main() {
    bool passed = testCalibrationAndWeighting();
    passed &= testWindowsAndState();
    passed &= testIntegratedGates();
    passed &= testChunking();
    return passed ? 0 : 1;
}
