#pragma once
#include "linearSmoother.h"
#include "parameterRange.h"
#include <array>
#include <span>
#include <vector>
namespace ahq::audio {
struct ReverbParameters {
    double decaySeconds = 2, dampingHz = 6000, preDelayMs = 20, width = 1, mix = .25;
    static constexpr ParameterRange decayRange{.1F, 20}, dampingRange{500, 18000},
        preDelayRange{0, 250}, widthRange{0, 1}, mixRange{0, 1};
    [[nodiscard]] bool isValid() const noexcept;
    bool operator==(const ReverbParameters &) const = default;
};
/// Eight-line orthogonal feedback delay network. Allocates only in prepare().
class Reverb {
  public:
    [[nodiscard]] bool prepare(double sampleRate) noexcept;
    [[nodiscard]] bool setParameters(const ReverbParameters &) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process(std::span<float>, std::span<float> = {}) noexcept;

  private:
    void update() noexcept;
    ReverbParameters parameters_;
    std::array<std::vector<float>, 8> delays_;
    std::array<size_t, 8> positions_{};
    std::array<double, 8> feedback_{}, low_{};
    std::vector<float> preDelay_;
    size_t prePosition_ = 0;
    double rate_ = 0, damping_ = 0;
    LinearSmoother mix_, width_, preFrames_;
    unsigned rampFrames_ = 1;
};
} // namespace ahq::audio
