#pragma once
#include "linearSmoother.h"
#include "parameterRange.h"
#include <span>
namespace ahq::audio {
/// Behavioural opto/FET models. These are not circuit emulations of particular units.
struct ClassicCompressorParameters {
    double inputDb = 0, outputDb = 0, peakReduction = 40, attackMs = .4, releaseMs = 200, mix = 1;
    int ratio = 0;
    bool limit = false;
    static constexpr ParameterRange inputRange{-24, 36}, outputRange{-36, 24},
        reductionRange{0, 100}, attackRange{.02F, .8F}, releaseRange{50, 1100}, mixRange{0, 1},
        ratioRange{0, 4, 1};
    [[nodiscard]] bool isValid() const noexcept;
    bool operator==(const ClassicCompressorParameters &) const = default;
};
class ClassicCompressor {
  public:
    enum class Model { Opto, Fet };
    explicit ClassicCompressor(Model model) : model_(model) {}
    [[nodiscard]] bool prepare(double sampleRate) noexcept;
    [[nodiscard]] bool setParameters(const ClassicCompressorParameters &) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process(std::span<float>, std::span<float> = {}) noexcept;
    [[nodiscard]] double reductionDb() const noexcept { return reduction_; }

  private:
    void update() noexcept;
    Model model_;
    ClassicCompressorParameters parameters_;
    LinearSmoother input_, output_, mix_;
    double rate_ = 0, attack_ = 0, release_ = 0, slowRelease_ = 0, memoryAttack_ = 0,
           memoryRelease_ = 0, threshold_ = -18, ratio_ = 4, fast_ = 0, slow_ = 0, memory_ = 0,
           reduction_ = 0, knee_ = 6;
    unsigned smoothingFrames_ = 1, controlFrame_ = 0;
};
} // namespace ahq::audio
