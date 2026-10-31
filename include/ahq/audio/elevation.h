#pragma once
#include "linearSmoother.h"
#include "parameterRange.h"
#include <array>
#include <span>
namespace ahq::audio {
struct ElevationParameters {
    double elevationDegrees = 0, outputDb = 0, mix = 1;
    static constexpr ParameterRange elevationRange{-90, 90}, outputRange{-24, 12}, mixRange{0, 1};
    [[nodiscard]] bool isValid() const noexcept;
};
/// Median-plane headphone spatializer using SADIE II D1 responses, Apache-2.0.
/// Input becomes one source M=(L+R)/2. -90 degrees is interpolated in the source dataset.
/// A 104-sample/48 kHz reference arrival plus the resampling delay is reported and dry aligned.
class Elevation {
  public:
    /// Finite 8-384 kHz. Fixed storage; windowed-sinc resampling occurs here, never in process().
    [[nodiscard]] bool prepare(double sampleRate) noexcept;
    [[nodiscard]] bool setParameters(const ElevationParameters &) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process(std::span<float> left, std::span<float> right) noexcept;
    [[nodiscard]] unsigned latencySamples() const noexcept { return latency_; }

  private:
    static constexpr size_t maxTaps = 2560, maxDry = 1025;
    void blend(double elevation) noexcept;
    ElevationParameters parameters_;
    std::array<std::array<std::array<float, maxTaps>, 2>, 14> filters_{};
    std::array<std::array<float, maxTaps>, 2> coefficients_{}, targets_{};
    std::array<float, maxTaps> history_{};
    std::array<std::array<float, maxDry>, 2> dry_{};
    size_t position_ = 0, dryPosition_ = 0, taps_ = 0;
    double rate_ = 0, lastElevation_ = 1000;
    LinearSmoother elevation_, gain_, mix_;
    unsigned latency_ = 0, rampFrames_ = 1, controlFrame_ = 0;
    bool coefficientsMoving_ = false;
};
} // namespace ahq::audio
