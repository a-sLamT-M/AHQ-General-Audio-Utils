#pragma once
#include "linearSmoother.h"
#include <array>
#include <span>
namespace ahq::audio {
enum class EqShape { bell, lowShelf, highShelf, lowCut, highCut, notch, bandPass };
enum class EqChannels { stereo, left, right, mid, side };
struct EqBandParameters {
    bool enabled = false;
    EqShape shape = EqShape::bell;
    EqChannels channels = EqChannels::stereo;
    double frequency = 1000, gainDb = 0, q = .707106781;
    int stages = 1; // 12 / 24 / 36 / 48 dB/oct cuts; other shapes use one stage.
    double dynamicRangeDb = 0, thresholdDb = -24, attackMs = 10, releaseMs = 150;
    [[nodiscard]] bool isValid() const noexcept;
    bool operator==(const EqBandParameters &) const = default;
};
// 24 independent biquads, with up to four sections for cuts. Disabled bands
// skip their sample loop once the enable ramp reaches zero. No FFT latency.
class ParametricEq {
  public:
    static constexpr size_t bandCount = 24;
    [[nodiscard]] bool prepare(double sampleRate) noexcept;
    [[nodiscard]] bool setBand(size_t, const EqBandParameters &) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process(std::span<float> left, std::span<float> right = {}) noexcept;
    [[nodiscard]] double magnitude(size_t band, double frequency) const noexcept;
    // RMS output magnitude for unit mid and unit side inputs, including mixed routes.
    [[nodiscard]] std::array<double, 2> responseMagnitudes(double frequency,
                                                           double mix = 1) const noexcept;
    [[nodiscard]] double dynamicGain(size_t band) const noexcept;

  private:
    struct Coefficients {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    };
    struct State {
        double z1 = 0, z2 = 0;
    };
    struct Band {
        EqBandParameters p;
        Coefficients current, target, delta, detector;
        std::array<std::array<State, 4>, 2> states{};
        std::array<State, 2> detectorStates{};
        EqBandParameters fadingParameters;
        Coefficients fadingCoefficients;
        std::array<std::array<State, 4>, 2> fadingStates{};
        double fadingWet = 0;
        unsigned fadingFrames = 0;
        LinearSmoother wet;
        double wetValue = 0, envelope = 0, attack = 0, release = 0, dynamicDb = 0;
        unsigned coefficientFrames = 0, controlClock = 0;
    };
    static Coefficients coefficients(const EqBandParameters &, double rate, double gainDb) noexcept;
    static double tick(double, State &, const Coefficients &) noexcept;
    void target(Band &, double gainDb, bool immediate) noexcept;
    std::array<Band, bandCount> bands_;
    double rate_ = 0;
    unsigned rampFrames_ = 1;
};
} // namespace ahq::audio
