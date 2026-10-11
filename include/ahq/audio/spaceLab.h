#pragma once

#include "ahq/audio/parameterRange.h"

#include "ahq/audio/linearSmoother.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace ahq::audio {

struct SpaceLabParameters {
    float width = 1.0F;
    float signedHaasMs = 0.0F;
    float leftTiltDb = 0.0F;
    float rightTiltDb = 0.0F;
    bool invertLeft = false;
    bool invertRight = false;
    float modRateHz = 0.0F;
    float modDepthMs = 0.0F;
    bool split = false;
    float mix = 1.0F;

    static constexpr ParameterRange widthRange{0, 3};
    static constexpr ParameterRange signedHaasMsRange{-30, 30};
    static constexpr ParameterRange leftTiltDbRange{-12, 12};
    static constexpr ParameterRange rightTiltDbRange{-12, 12};
    static constexpr ParameterRange modRateHzRange{0, 10};
    static constexpr ParameterRange modDepthMsRange{0, 20};
    static constexpr ParameterRange mixRange{0, 1};
    [[nodiscard]] bool isValid() const noexcept;
};

/// Stereo experiments: M/S width, Haas, independent tilt/polarity and right delay modulation.
/// No HRTF, feedback, phase-cancellation protection or fixed host-compensation latency.
/// All calls use one thread. Opposite-polarity input can cancel when reduced to mono M.
class SpaceLab {
  public:
    /// Finite sample rate [8000,384000] Hz. Preallocates three 70 ms float histories and resets.
    /// Invalid rate/allocation failure returns false and leaves all existing state unchanged.
    [[nodiscard]] bool prepare(double sampleRate) noexcept;
    /// Width [0,3]; signed Haas [-30,30] ms (positive delays right, negative delays left).
    /// Independent tilt [-12,12] dB: 1 kHz complementary one-pole low/high gains -tilt/2,+tilt/2
    /// dB. Right modulation rate [0,10] Hz, depth [0,20] ms; delay=depth*(1+sin(phase))/2. Phase
    /// starts zero; rate zero freezes phase (initially gives depth/2 delay). Mix [0,1]. All floats
    /// finite; requires prepare. All controls, including boolean routing/polarity, transition over
    /// 5 ms. Repeated targets retain transitions. Invalid calls preserve state. Normal wet path:
    /// width M/S, delays, independent tilt, polarity. Split wet path: left is current input
    /// M=(L+R)/2 completely dry; right is delayed/tilted/inverted M. In full split, width, negative
    /// Haas, left tilt and left polarity have no effect. Original stereo dry always remains
    /// available through Mix. Creative linear delay interpolation can alias.
    [[nodiscard]] bool setParameters(const SpaceLabParameters &parameters) noexcept;
    /// Clears histories/EQ/modulation phase and aligns all current controls with targets.
    void reset() noexcept;
    /// Two equal-length nonoverlapping stereo channels required; both empty succeeds without
    /// state advancement. A wrapper may duplicate mono input into both output channels.
    /// Default width1/all effects0/mix1 is sample-exact unity; width0 is M in both channels.
    /// Mix0 is exact original stereo. Finite input expected; no clipping, so extreme samples
    /// can overflow. Bad sizes/unprepared nonempty calls preserve buffers and complete state.
    /// No processing allocation, locking or I/O.
    [[nodiscard]] bool process(std::span<float> left, std::span<float> right) noexcept;

  private:
    double readDelay(std::size_t channel, double frames) const noexcept;
    double applyTilt(double input, std::size_t channel, double tiltDb) noexcept;

    SpaceLabParameters parameters_;
    double sampleRate_ = 0.0;
    double eqCoefficient_ = 0.0;
    std::uint32_t transitionFrames_ = 0;
    std::array<std::vector<float>, 3> history_;
    std::size_t position_ = 0;
    std::array<double, 2> eqLow_{};
    double modulationPhase_ = 0.0;
    LinearSmoother width_;
    LinearSmoother haas_;
    LinearSmoother leftTilt_;
    LinearSmoother rightTilt_;
    LinearSmoother leftPolarity_;
    LinearSmoother rightPolarity_;
    LinearSmoother modRate_;
    LinearSmoother modDepth_;
    LinearSmoother split_;
    LinearSmoother mix_;
};

} // namespace ahq::audio
