#pragma once
#include "linearSmoother.h"
#include "parameterRange.h"
#include <array>
#include <span>
namespace ahq::audio {
struct TremoloParameters {
    bool sync = true;
    int shape = 0; // Sine, triangle, square, rising saw.
    double rateHz = .5, beats = 4, depth = 1, phaseDegrees = 0;
    static constexpr ParameterRange shapeRange{0, 3, 1}, rateRange{.01F, 20},
        beatsRange{.0625F, 64}, depthRange{0, 1}, phaseRange{0, 360};
    [[nodiscard]] bool isValid() const noexcept;
};
class Tremolo {
  public:
    [[nodiscard]] bool prepare(double sampleRate) noexcept;
    [[nodiscard]] bool setParameters(const TremoloParameters &) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process(std::span<float>, std::span<float> = {}, double bpm = 120,
                               double ppq = 0, bool positioned = false,
                               bool playing = true) noexcept;
    [[nodiscard]] double phase() const noexcept { return phase_; }
    [[nodiscard]] double gain() const noexcept { return gain_; }

  private:
    TremoloParameters parameters_;
    std::array<float, 1025> sine_{};
    LinearSmoother depth_;
    double rate_ = 0, phase_ = 0, gain_ = 1, smoothing_ = 0;
    unsigned transitionFrames_ = 1;
};
} // namespace ahq::audio
