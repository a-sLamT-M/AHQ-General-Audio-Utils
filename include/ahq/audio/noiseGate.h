#pragma once
#include "parameterRange.h"
#include <span>
namespace ahq::audio {
struct NoiseGateParameters {
    double thresholdDb = -48, hysteresisDb = 6, attackMs = .5, holdMs = 40, releaseMs = 100,
           rangeDb = -96, mix = 1;
    static constexpr ParameterRange thresholdRange{-90, 0}, hysteresisRange{0, 24},
        attackRange{.01F, 200}, holdRange{0, 1000}, releaseRange{1, 3000}, rangeRange{-96, 0},
        mixRange{0, 1};
    [[nodiscard]] bool isValid() const noexcept;
    bool operator==(const NoiseGateParameters &) const = default;
};
class NoiseGate {
  public:
    [[nodiscard]] bool prepare(double sampleRate) noexcept;
    [[nodiscard]] bool setParameters(const NoiseGateParameters &) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process(std::span<float> left, std::span<float> right = {}) noexcept;
    [[nodiscard]] double gain() const noexcept { return gain_; }
    [[nodiscard]] bool isOpen() const noexcept { return open_; }

  private:
    void update() noexcept;
    NoiseGateParameters parameters_;
    double rate_ = 0, opening_ = 0, closing_ = 0, attack_ = 0, release_ = 0, detectorRelease_ = 0,
           floor_ = 0, detector_ = 0, gain_ = 0;
    unsigned holdFrames_ = 0, holdRemaining_ = 0;
    bool open_ = false;
};
} // namespace ahq::audio
