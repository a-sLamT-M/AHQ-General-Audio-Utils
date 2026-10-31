#pragma once
#include "linearSmoother.h"
#include "parameterRange.h"
#include <array>
#include <span>
#include <vector>
namespace ahq::audio {
struct RetroToolsParameters {
    double ensemble = .5, rateHz = .4, wow = 0, flutter = 0, drive = 0, toneHz = 12000, mix = .5;
    int bits = 24;
    double fault = 0;
    static constexpr ParameterRange ensembleRange{0, 1}, rateRange{.05F, 4}, wowRange{0, 1},
        flutterRange{0, 1}, driveRange{0, 1}, toneRange{1000, 18000}, bitsRange{4, 24, 1},
        mixRange{0, 1}, faultRange{0, 1};
    [[nodiscard]] bool isValid() const noexcept;
    bool operator==(const RetroToolsParameters &) const = default;
};
/// Ensemble, tape motion and speaker-fault colour. Dry is latency aligned. Fault is a
/// gain-reducing behavioural effect, not a calibrated circuit model. Reset repeats its random
/// sequence; processing is independent of block size.
class RetroTools {
  public:
    [[nodiscard]] bool prepare(double sampleRate) noexcept;
    [[nodiscard]] bool setParameters(const RetroToolsParameters &) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process(std::span<float>, std::span<float> = {}) noexcept;
    [[nodiscard]] unsigned latencySamples() const noexcept { return latency_; }

  private:
    double sine(double phase) const noexcept;
    float read(size_t channel, double delay) const noexcept;
    double random() noexcept;
    RetroToolsParameters parameters_;
    std::array<std::vector<float>, 2> history_;
    std::array<float, 1025> sine_{};
    std::array<double, 2> low_{};
    std::array<LinearSmoother, 8> smooth_;
    LinearSmoother faultDepth_, faultSpeed_, faultJitter_;
    size_t position_ = 0;
    double rate_ = 0, chorusPhase_ = 0, wowPhase_ = 0, flutterPhase_ = 0, damping_ = 1,
           quantizer_ = 0;
    unsigned latency_ = 0, rampFrames_ = 1;
    std::uint32_t randomState_ = 0x72657472U;
    unsigned faultFrames_ = 0;
    double faultPhase_ = 0, faultEnvelope_ = 0, faultAttack_ = 1, faultRelease_ = 1;
};
} // namespace ahq::audio
