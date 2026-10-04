#include "ahq/audio/loudnessMeter.h"
#include "ahq/audio/loudnessRange.h"
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <span>
bool check(std::span<const double> levels, double expected) {
    ahq::audio::LoudnessMeter meter;
    ahq::audio::LoudnessRange range;
    if (!meter.prepare(32000, 2))
        return false;
    for (double level : levels)
        for (int i = 0; i < 640000; ++i) {
            const float sample = float(std::pow(10., level / 20) *
                                       std::sin(2 * std::numbers::pi * 1000 * i / 32000));
            ahq::audio::LoudnessBlock block;
            if (!meter.processSample(sample, sample, block))
                return false;
            if (block.ready) {
                auto shortTerm = meter.shortTerm();
                if (shortTerm.valid)
                    range.addShortTerm(shortTerm.lufs);
            }
        }
    const auto reading = range.reading();
    if (!reading.valid || std::abs(reading.lu - expected) > 1) {
        std::cerr << "LRA " << reading.lu << " expected " << expected << '\n';
        return false;
    }
    range.reset();
    return !range.reading().valid;
}
int main() {
    bool passed = check(std::array{-20., -30.}, 10);
    passed &= check(std::array{-20., -15.}, 5);
    passed &= check(std::array{-40., -20.}, 20);
    passed &= check(std::array{-50., -35., -20., -35., -50.}, 15);
    return passed ? 0 : 1;
}
