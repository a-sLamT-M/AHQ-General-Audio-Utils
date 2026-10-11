#pragma once

#include "ahq/audio/parameterRange.h"

#include "ahq/audio/linearSmoother.h"

#include <array>
#include <cstdint>
#include <span>

namespace ahq::audio {

enum class OrbitDirection { clockwise, counterClockwise };

struct OrbitParameters {
    float rateHz = 0.2F;
    bool syncToBpm = false;
    float bpm = 120.0F;
    float beatsPerRevolution = 4.0F;
    OrbitDirection direction = OrbitDirection::clockwise;
    float depth = 1.0F;
    float phaseDegrees = 0.0F;
    float mix = 1.0F;

    static constexpr ParameterRange rateHzRange{0, 10};
    static constexpr ParameterRange bpmRange{20, 300};
    static constexpr ParameterRange beatsPerRevolutionRange{0.25F, 64};
    static constexpr ParameterRange directionRange{0, 1, 1};
    static constexpr ParameterRange depthRange{0, 1};
    static constexpr ParameterRange phaseDegreesRange{0, 360};
    static constexpr ParameterRange mixRange{0, 1};
    [[nodiscard]] bool isValid() const noexcept;
};

/// Headphone binaural rotation using measured horizontal-plane diffuse-field KEMAR HRIRs.
/// Bill Gardner and Keith Martin, Copyright 1994 MIT Media Laboratory; see src/spatial/data
/// attribution. 0/90/180/270 degrees mean front/right/back/left; clockwise progresses toward the
/// right. Wet input is one source M=(L+R)/2, so opposite-polarity stereo can cancel in the wet
/// path. Depth crossfades front HRIR with rotating HRIR (0 fixed front, 1 full rotation), not
/// distance. All calls use one thread. An instance holds about 460 KiB of fixed FIR storage.
class Orbit {
  public:
    /// Accepts finite sample rates [8000,384000] Hz, resamples HRIRs offline and resets history.
    /// Uses a 16-lobe windowed-sinc low-pass kernel with common pre/post support and gain scaling.
    /// Added common causal delay is ceil(16*max(1,sampleRate/44100)) samples; dry is aligned to it.
    /// Native acoustic HRIR delays remain direction dependent. Invalid prepare preserves state.
    [[nodiscard]] bool prepare(double sampleRate) noexcept;
    /// Rate [0,10] Hz; BPM [20,300]; beats/revolution [0.25,64]; depth/mix [0,1]; phase [0,360].
    /// All floats finite and direction valid. Sync rate is BPM/(60*beats/revolution); no host read.
    /// BPM is always required valid; missing/zero BPM rejects the call, retaining previous state.
    /// Effective signed speed, depth, phase and mix transition over 5 ms. Phase takes shortest arc.
    /// Adjacent 5-degree HRIRs interpolate each sample; invalid calls preserve complete state.
    [[nodiscard]] bool setParameters(const OrbitParameters &parameters) noexcept;
    /// Clears all history, resets rotation to zero and aligns parameters to targets.
    void reset() noexcept;
    [[nodiscard]] std::uint32_t getLatencySamples() const noexcept;
    /// Requires two equal-length nonoverlapping channels. Both empty succeeds without advancing.
    /// Caller may duplicate mono input into both output channels before calling this function.
    /// Mix zero is exactly original stereo delayed by reported common causal delay.
    /// Input must be finite; no clipping (large samples can overflow), allocation, locking or I/O.
    /// Invalid lengths/unprepared nonempty calls preserve buffers and state.
    [[nodiscard]] bool process(std::span<float> left, std::span<float> right) noexcept;

  private:
    static constexpr std::size_t MAX_TAPS = 1536;
    static constexpr std::size_t MAX_DELAY = 141;
    static double signedRate(const OrbitParameters &parameters) noexcept;
    static double wrapDegrees(double angle) noexcept;
    std::array<double, 2> convolve(double angle, double depth) noexcept;

    OrbitParameters parameters_;
    double sampleRate_ = 0.0;
    std::uint32_t transitionFrames_ = 0;
    std::uint32_t latencySamples_ = 0;
    std::size_t taps_ = 0;
    std::array<std::array<std::array<float, MAX_TAPS>, 2>, 37> filters_{};
    std::array<double, MAX_TAPS> history_{};
    std::size_t historyPosition_ = 0;
    std::array<std::array<float, MAX_DELAY>, 2> dryHistory_{};
    std::size_t dryPosition_ = 0;
    LinearSmoother rate_;
    LinearSmoother depth_;
    LinearSmoother mix_;
    double orbitDegrees_ = 0.0;
    double phaseDegrees_ = 0.0;
    double phaseTarget_ = 0.0;
    double phaseStep_ = 0.0;
    std::uint32_t remainingPhaseFrames_ = 0;
};

} // namespace ahq::audio
