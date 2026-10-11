#include "ahq/audio/stereoMeter.h"
#include <cmath>
#include <iostream>
#include <numbers>

bool expect(bool condition, const char *message) {
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}
int main() {
    ahq::audio::StereoMeter meter;
    bool passed = expect(meter.prepare(48000, 2), "prepare");
    for (int mode = 0; mode < 3; ++mode) {
        meter.reset();
        for (int frame = 0; frame < 19200; ++frame) {
            const float l = float(0.5 * std::sin(2 * std::numbers::pi * 1000 * frame / 48000));
            const float r =
                mode == 0   ? l
                : mode == 1 ? -l
                            : float(0.5 * std::cos(2 * std::numbers::pi * 1000 * frame / 48000));
            passed &= meter.processSample(l, r);
        }
        const auto result = meter.reading();
        passed &= expect(result.valid && std::abs(result.rms[0] - 0.5 / std::sqrt(2.0)) < 1e-7,
                         "absolute rms");
        passed &= expect(std::abs(result.crestDb[0] - 20 * std::log10(std::sqrt(2.0))) < 1e-6,
                         "window crest");
        passed &= expect(std::abs(result.correlation - (mode == 0   ? 1
                                                        : mode == 1 ? -1
                                                                    : 0)) < 1e-7,
                         "correlation");
        passed &= expect(mode == 0   ? result.sideEnergy < 1e-10
                         : mode == 1 ? result.midEnergy < 1e-10
                                     : std::abs(result.midEnergy - result.sideEnergy) < 1e-10,
                         "MS energy");
    }
    meter.reset();
    passed &= expect(!meter.reading().valid, "reset clears completed reading");
    passed &= expect(!meter.processSample(NAN, 0), "invalid frame rejected");
    return passed ? 0 : 1;
}
