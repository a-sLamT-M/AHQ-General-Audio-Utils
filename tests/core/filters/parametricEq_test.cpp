#include "ahq/audio/parametricEq.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <vector>
using namespace ahq::audio;
int main() {
    bool passed = true;
    auto check = [&](bool condition, const char *name) {
        if (!condition)
            std::cerr << "FAILED: " << name << '\n';
        passed &= condition;
    };
    ParametricEq eq;
    check(eq.prepare(48000), "prepare");
    std::vector<float> signal(48000);
    for (size_t i = 0; i < signal.size(); ++i)
        signal[i] = float(.25 * std::sin(2 * std::numbers::pi * 1000 * double(i) / 48000));
    auto left = signal, right = signal;
    check(eq.process(left, right) && left == signal && right == signal,
          "all disabled exact bypass");
    EqBandParameters p;
    p.enabled = true;
    p.frequency = 1000;
    p.gainDb = 6;
    p.q = 1;
    check(eq.setBand(0, p), "enable bell");
    check(std::abs(20 * std::log10(eq.magnitude(0, 1000)) - 6) < 1e-8, "bell centre gain");
    eq.reset();
    check(eq.process(left, right), "filter sine");
    double inputEnergy = 0, outputEnergy = 0;
    for (size_t i = 24000; i < signal.size(); ++i) {
        inputEnergy += signal[i] * signal[i];
        outputEnergy += left[i] * left[i];
    }
    check(std::abs(10 * std::log10(outputEnergy / inputEnergy) - 6) < .001, "measured bell gain");
    check(left == right, "stereo symmetry");
    ParametricEq split;
    check(split.prepare(48000) && split.setBand(0, p), "second filter");
    split.reset();
    auto chunks = signal;
    for (size_t i = 0; i < chunks.size();) {
        const auto n = std::min(size_t(31), chunks.size() - i);
        check(split.process(std::span(chunks).subspan(i, n)), "small block");
        i += n;
    }
    check(chunks == left, "block invariance");
    p.channels = EqChannels::side;
    check(eq.setBand(0, p), "side only");
    eq.reset();
    left = signal;
    right = signal;
    check(eq.process(left, right) && left == signal && right == signal,
          "side EQ leaves mono centre untouched");
    p.channels = EqChannels::mid;
    check(eq.setBand(0, p), "mid only");
    eq.reset();
    left = signal;
    right = signal;
    for (auto &s : right)
        s = -s;
    auto opposite = right;
    check(eq.process(left, right) && left == signal && right == opposite,
          "mid EQ leaves side untouched");
    p.channels = EqChannels::stereo;
    p.gainDb = 0;
    p.dynamicRangeDb = -12;
    p.thresholdDb = -50;
    check(eq.setBand(0, p), "dynamic band");
    eq.reset();
    left = signal;
    check(eq.process(left), "dynamic processing");
    check(eq.dynamicGain(0) < -11.9, "dynamic attenuation");
    check(std::abs(left[47004]) < std::abs(signal[47004]) * .3, "dynamic output reduced");
    p.dynamicRangeDb = 0;
    for (int shape = 0; shape <= 6; ++shape)
        for (double rate : {8000., 44100., 96000., 384000.}) {
            check(eq.prepare(rate), "sample rate");
            p.shape = EqShape(shape);
            p.q = 30;
            p.gainDb = 30;
            p.frequency = 30000;
            p.stages = 4;
            check(eq.setBand(0, p), "extreme band");
            left.assign(4096, 0);
            left[0] = .01F;
            eq.reset();
            check(eq.process(left), "impulse");
            check(std::all_of(left.begin(), left.end(), [](float x) { return std::isfinite(x); }),
                  "finite impulse");
        }
    p.frequency = std::numeric_limits<double>::quiet_NaN();
    check(!eq.setBand(0, p) && !eq.setBand(24, {}), "invalid edits rejected");
    check(!eq.process(left, std::span(left).first(1)), "layout validation");
    // Report CPU cost; no machine-dependent timing threshold in correctness tests.
    check(eq.prepare(48000), "benchmark prepare");
    p = {};
    p.enabled = true;
    p.gainDb = 1;
    for (size_t i = 0; i < ParametricEq::bandCount; ++i) {
        p.frequency = 40 * std::pow(1.28, double(i));
        check(eq.setBand(i, p), "benchmark band");
    }
    eq.reset();
    left.assign(256, .01F);
    right = left;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) {
        std::fill(left.begin(), left.end(), .01F);
        std::fill(right.begin(), right.end(), .01F);
        (void)eq.process(left, right);
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "24 static stereo bands / 48 kHz / 256 frames: " << seconds * 1000
              << " ms for 5.333 s audio (" << seconds / (256000. / 48000) * 100
              << "% of one core)\n";
    return passed ? 0 : 1;
}
