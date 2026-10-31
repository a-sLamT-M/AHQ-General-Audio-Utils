#pragma once
#include "parameterRange.h"
#include <array>
#include <span>

namespace ahq::audio {
struct VolumeEnvelopeParameters {
    static constexpr ParameterRange beatsRange{.0625F, 16.F};
    static constexpr ParameterRange thresholdRange{-80.F, 0.F};
    static constexpr ParameterRange mixRange{0.F, 1.F};
    static constexpr ParameterRange innerTimeRange{.001F, .999F};
    static constexpr ParameterRange levelRange{0.F, 1.F};
    bool audioTrigger = false;
    double beats = 1, thresholdDb = -24, mix = 1;
    std::array<double, 8> times{0, .08, .2, .36, .52, .7, .86, 1};
    std::array<double, 8> levels{0, 0, .08, .3, .6, .82, .96, 1};
    [[nodiscard]] bool isValid() const noexcept;
};

// Fixed-gain envelope, not a level-dependent compressor. All storage is fixed.
class VolumeEnvelope {
  public:
    [[nodiscard]] bool prepare(double sampleRate) noexcept;
    [[nodiscard]] bool setParameters(const VolumeEnvelopeParameters &) noexcept;
    void reset() noexcept;
    // ppq is the position of the first sample. When unavailable, phase free-runs.
    // A stopped, positioned transport passes audio unchanged in BPM mode.
    [[nodiscard]] bool process(std::span<float> left, std::span<float> right,
                               std::span<const float> triggerLeft,
                               std::span<const float> triggerRight, double bpm = 120,
                               double ppq = 0, bool positioned = false,
                               bool playing = true) noexcept;
    [[nodiscard]] double phase() const noexcept { return phase_; }
    [[nodiscard]] double gain() const noexcept { return gain_; }
    [[nodiscard]] double valueAt(double phase) const noexcept;

  private:
    VolumeEnvelopeParameters parameters_;
    double rate_ = 0, phase_ = 1, gain_ = 1, detector_ = 0;
    double threshold_ = 0.063095734, detectorDecay_ = 0, smoothing_ = 0;
    bool active_ = false, armed_ = true;
};
} // namespace ahq::audio
