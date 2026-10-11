#pragma once

#include "ahq/audio/parameterRange.h"

#include "ahq/audio/linearSmoother.h"

#include <array>
#include <cstdint>
#include <span>

namespace ahq::audio {

enum class FilterMode { lowPass, highPass };
enum class FilterSlope { db12, db24, db36, db48 };

struct QuickFilterParameters {
    FilterMode mode = FilterMode::lowPass;
    float frequencyHz = 1000.0F;
    float q = 0.70710678F;
    FilterSlope slope = FilterSlope::db12;
    float mix = 1.0F;

    static constexpr ParameterRange modeRange{0, 1, 1};
    static constexpr ParameterRange frequencyHzRange{20, 20000};
    static constexpr ParameterRange qRange{0.1F, 10};
    static constexpr ParameterRange slopeRange{0, 3, 1};
    static constexpr ParameterRange mixRange{0, 1};
    [[nodiscard]] bool isValid() const noexcept;
};

/// Mono/stereo TPT state-variable filter with 5 ms parameter and topology transitions.
/// Q controls the first second-order section; later sections use Q = 1/sqrt(2).
/// Slopes describe asymptotic attenuation, not a Butterworth alignment.
/// All calls to an instance must occur on one thread.
class QuickFilter {
  public:
    /// Finite sample rates in [1, 384000] Hz. Clears history and aligns to target parameters.
    /// The effective cutoff is min(frequencyHz, 0.45 * sampleRate).
    /// Failure preserves the complete previous state.
    [[nodiscard]] bool prepare(double sampleRate) noexcept;

    /// Requires prepare. Frequency: [20, 20000] Hz; Q: [0.1, 10]; mix: [0, 1].
    /// Values must be finite and enums valid. Invalid calls preserve all state.
    /// Changed targets ramp for 5 ms from current values; repeated targets do not restart ramps.
    [[nodiscard]] bool setParameters(const QuickFilterParameters &parameters) noexcept;

    /// Clears both channels' history and immediately aligns all parameters with their targets.
    void reset() noexcept;

    /// Empty right means mono. Stereo spans must have equal lengths and must not overlap.
    /// Empty blocks succeed without advancing state. Invalid lengths/missing prepare return false
    /// without changing buffers or state. Input must be finite; extreme samples can overflow float
    /// output. No clipping, allocation, locking, or I/O is performed.
    [[nodiscard]] bool process(std::span<float> left, std::span<float> right = {}) noexcept;

  private:
    struct SectionState {
        double band = 0.0;
        double low = 0.0;
    };

    struct Coefficients {
        double first;
        double second;
        double third;
        double damping;
    };

    static Coefficients calculateCoefficients(double tangent, double q) noexcept;
    double processChannel(double input, std::size_t channel,
                          const std::array<Coefficients, 2> &coefficients,
                          const std::array<double, 8> &weights) noexcept;

    QuickFilterParameters parameters_;
    double sampleRate_ = 0.0;
    std::uint32_t transitionFrames_ = 0;
    LinearSmoother frequency_;
    LinearSmoother q_;
    LinearSmoother mix_;
    std::array<double, 8> weights_{};
    std::array<double, 8> weightSteps_{};
    std::uint32_t remainingTopologyFrames_ = 0;
    std::array<std::array<std::array<SectionState, 4>, 2>, 2> states_{};
};

} // namespace ahq::audio
