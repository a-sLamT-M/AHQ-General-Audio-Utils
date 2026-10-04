#pragma once

#include "ahq/audio/parameterRange.h"

#include "ahq/audio/linearSmoother.h"
#include "ahq/audio/loudnessMeter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace ahq::audio {

struct TrueLimiterParameters {
    float ceilingDb = -1.0F;
    float releaseMs = 100.0F;
    float inputGainDb = 0.0F;
    float mix = 1.0F;
    bool liveLoudness = false;
    float targetLufs = -14.0F;

    static constexpr ParameterRange ceilingDbRange{-24, 0};
    static constexpr ParameterRange releaseMsRange{10, 1000};
    static constexpr ParameterRange inputGainDbRange{-24, 24};
    static constexpr ParameterRange mixRange{0, 1};
    static constexpr ParameterRange targetLufsRange{-36, -5};
    [[nodiscard]] bool isValid() const noexcept;
};

struct LoudnessMatchResult {
    bool valid = false;
    bool targetReachable = false;
    bool peakLimited = false;
    double inputLufs = -std::numeric_limits<double>::infinity();
    double outputLufs = -std::numeric_limits<double>::infinity();
    double gainDb = 0.0;
};

/// Linked mono/stereo lookahead limiter, with a finite FIR output peak guard.
/// Dry/wet is BEFORE final protection: mix zero disables input/LUFS gain, but
/// remains delayed and peak protected. All calls to an instance use one thread.
/// This is a conservative gain processor, not a transparent mastering algorithm.
class TrueLimiter {
  public:
    /// Accepts [32000,384000] Hz, channels 1/2, lookahead [0,20] ms, and
    /// oversampling 4/8/16 (default 8). Lookahead is the prepared maximum;
    /// effective lookahead initially equals it. Allocates all phase history.
    /// Allocates a fixed delay; success resets all measurements and transitions.
    /// Failure preserves state. Latency is round(rate*lookaheadMs/1000)+24 frames.
    /// The 24-frame base delay covers the peak detector's finite FIR support.
    [[nodiscard]] bool prepare(double sampleRate, unsigned channels, double lookaheadMs = 5.0,
                               unsigned oversampling = 8) noexcept;

    /// Ceiling [-24,0] dB, release [10,1000] ms, gain [-24,24] dB, mix [0,1],
    /// target [-36,-5] LUFS; all finite. Gain/mix ramp for 5 ms, live correction
    /// smooths for 250 ms. Ceiling/release apply to subsequent emitted samples.
    /// Live correction reads ungated 3 s INPUT LUFS every 100 ms, is bounded
    /// to [-96,+24] dB, and returns toward unity below -60 LUFS or while unfilled.
    /// Live correction compensates manual inputGainDb to retain the target at
    /// steady state; mix zero disables both. Live mode cannot know the future
    /// or guarantee integrated LUFS. The [-96,+24] dB correction range still applies.
    /// A lowered ceiling retains old FIR contributions, allowing them only to
    /// decrease toward the new bound. New sampled ceiling applies immediately,
    /// but interpolated new-ceiling assurance begins after 24
    /// emitted frames, once old higher-level output leaves the FIR support.
    [[nodiscard]] bool setParameters(const TrueLimiterParameters &parameters) noexcept;
    /// Real-time safe; effective [0, prepared maximum] ms. Delays the detector
    /// instead of moving the audio delay read head, so host latency is constant.
    /// This changes advance detection and its peak hold, without resetting LUFS.
    [[nodiscard]] bool setLookaheadMs(double lookaheadMs) noexcept;
    /// Real-time safe; factor 4/8/16. All 16 phase partial histories remain live.
    /// Newly enabled phases use retained old contributions; their new grid
    /// assurance applies after 24 output frames. Host latency remains constant.
    [[nodiscard]] bool setOversamplingFactor(unsigned factor) noexcept;
    void reset() noexcept;

    /// In-place mono requires empty right, stereo equal-sized right. Buffers
    /// must not overlap; finite input is checked before any mutation. Invalid
    /// calls preserve state/buffers. Empty valid buffers do not advance state.
    /// No allocation, locking or I/O; selected phase count bounds per-frame work.
    /// To drain the delayed tail, process latencyFrames() zero input frames.
    [[nodiscard]] bool process(std::span<float> left, std::span<float> right = {}) noexcept;
    [[nodiscard]] std::size_t latencyFrames() const noexcept;
    [[nodiscard]] double gainReductionDb() const noexcept;
    [[nodiscard]] LoudnessReading inputShortTerm() const noexcept;
    /// Reports whether actual nonzero output has been peak attenuated since reset.
    [[nodiscard]] bool protectionEngaged() const noexcept;

    /// Final guard assurance is for a DC-normalized, 24-tap Hann-windowed sinc
    /// FIR evaluated at the selected 4/8/16 equally-spaced phases, with 1.75 dB
    /// extra headroom at 4x and 1.6 dB at 8x/16x (longer-reference validation). It constrains every
    /// future FIR partial sum; output gain changes are included. It is NOT a mathematical guarantee
    /// for arbitrary continuous-time reconstruction or other filters.
    [[nodiscard]] unsigned oversampling() const noexcept;

    /// OFFLINE only: may allocate and repeatedly scan complete block histories.
    /// Measures full integrated input, estimates matching constant gain, then
    /// renders this limiter with that gain and measures actual integrated output
    /// including its delayed tail. Returns valid=false for invalid/all-gated
    /// input or allocation failure. targetReachable means output within 0.1 LU;
    /// peakLimited reports protection-induced loss. Live mode is disabled for
    /// this analysis. Apply gainDb with the same parameters/structural settings.
    [[nodiscard]] static LoudnessMatchResult
    analyzeMatch(std::span<const float> left, std::span<const float> right, double sampleRate,
                 double targetLufs, const TrueLimiterParameters &parameters = {},
                 double lookaheadMs = 5.0, unsigned oversampling = 8) noexcept;

  private:
    double guardFrame(double left, double right, float &outLeft, float &outRight) noexcept;
    double detectPeak(double left, double right) noexcept;
    void updateLiveGain(const LoudnessBlock &block) noexcept;

    TrueLimiterParameters parameters_;
    LoudnessMeter loudness_;
    LinearSmoother inputGain_;
    LinearSmoother mix_;
    std::array<std::array<double, 24>, 16> coefficients_{};
    std::array<std::array<double, 24>, 16> reciprocal_{};
    std::array<std::array<std::array<double, 24>, 16>, 2> partials_{};
    std::array<std::array<double, 24>, 2> detectorHistory_{};
    std::array<std::vector<double>, 2> delay_;
    std::vector<double> peakDelay_;
    std::size_t delayPosition_ = 0;
    std::size_t peakPosition_ = 0;
    std::size_t maximumLookaheadFrames_ = 0;
    std::size_t effectiveLookaheadFrames_ = 0;
    std::uint32_t smoothingFrames_ = 0;
    std::size_t holdFrames_ = 0;
    double sampleRate_ = 0.0;
    double maximumLookaheadMs_ = 0.0;
    double ceiling_ = 0.0;
    double releaseCoefficient_ = 0.0;
    double liveCoefficient_ = 0.0;
    double liveGain_ = 1.0;
    double liveTarget_ = 1.0;
    double peakEnvelope_ = 0.0;
    double lastReduction_ = 1.0;
    unsigned channels_ = 0;
    unsigned oversampling_ = 8;
    bool protectionEngaged_ = false;
};

} // namespace ahq::audio
