#pragma once

#include "ahq/audio/parameterRange.h"

#include "ahq/audio/linearSmoother.h"

#include <array>
#include <cstdint>
#include <span>

namespace ahq::audio {

enum class SaturatorModel { softClip, hardClip, tube, tape, wavefolder, rectifier, bitcrusher };
enum class OversamplingFactor { x1, x2, x4 };

struct SaturatorParameters {
    SaturatorModel model = SaturatorModel::softClip;
    float driveDb = 0.0F;
    float bias = 0.0F;
    float tone = 0.0F;
    float mix = 1.0F;

    static constexpr ParameterRange modelRange{0, 6, 1};
    static constexpr ParameterRange driveDbRange{0, 36};
    static constexpr ParameterRange biasRange{-1, 1};
    static constexpr ParameterRange toneRange{-1, 1};
    static constexpr ParameterRange mixRange{0, 1};
    [[nodiscard]] bool isValid() const noexcept;
};

/// Digital nonlinear curves, not circuit simulations. All calls use one thread.
/// 33-tap Hamming-windowed sinc half-band FIRs implement actual interpolation/decimation.
/// Curves: tanh soft clipping; clamped hard clipping; asymmetric tanh tube style;
/// x/sqrt(1+x*x) tape style; triangular periodic folding; clamped full-wave rectification;
/// and rounding to 255 evenly spaced levels in [-1,1] for bit crushing.
/// Rectification/asymmetric shaping can intentionally generate signal-dependent DC.
/// Finite FIR length and 2x/4x rates reduce selected aliases, not all nonlinear aliasing.
class Saturator {
  public:
    /// Finite base sample rate [1, 384000] Hz; quality changes only here, clearing all history.
    /// Returns false without state changes for invalid rate/factor.
    [[nodiscard]] bool prepare(double sampleRate,
                               OversamplingFactor factor = OversamplingFactor::x2) noexcept;
    /// Drive [0,36] dB; bias/tone [-1,1]; mix [0,1]; all finite, valid model, prepared instance.
    /// Continuous parameters and model weights transition over 5 ms; repeated targets preserve
    /// transitions. Bias applies before shaping, subtracting curve(bias) to keep zero input zero.
    /// Tone: -1 is a 2 kHz one-pole low pass; +1 boosts its complementary high band by 2x.
    /// Tone zero bypasses the tone operation exactly. Cutoff is capped to 0.2 * base sample rate.
    /// Invalid calls preserve all state.
    [[nodiscard]] bool setParameters(const SaturatorParameters &parameters) noexcept;
    /// Clears histories and aligns current parameters to targets.
    void reset() noexcept;
    /// Reports base-rate latency: 0/16/24 samples for 1x/2x/4x; zero before prepare.
    [[nodiscard]] std::uint32_t getLatencySamples() const noexcept;
    /// Empty right means mono; stereo lengths must match and buffers must not overlap.
    /// Mix zero is exactly the original input delayed by getLatencySamples(). Nonzero tone can
    /// add frequency-dependent phase beyond the FIR latency. FIR ringing means wet peaks can
    /// exceed curve limits. Input must be finite; extreme dry samples may overflow mixed output.
    /// Empty blocks do not advance state. Missing prepare/invalid lengths leave buffers and
    /// state unchanged. No allocation, locks, I/O, or final clipping.
    [[nodiscard]] bool process(std::span<float> left, std::span<float> right = {}) noexcept;

  private:
    struct FirState {
        std::array<double, 33> history{};
        std::size_t position = 0;
        double process(double input) noexcept;
    };
    struct ChannelState {
        FirState upFirst;
        FirState downFirst;
        FirState upSecond;
        FirState downSecond;
        std::array<float, 25> dryHistory{};
        std::size_t dryPosition = 0;
        double toneLow = 0.0;
    };
    static double shape(double input, SaturatorModel model) noexcept;
    double shapeAndTone(double input, ChannelState &channel, double drive, double bias,
                        double tone) noexcept;
    double processWet(double input, ChannelState &channel, double drive, double bias,
                      double tone) noexcept;
    float delayDry(float input, ChannelState &channel) noexcept;

    SaturatorParameters parameters_;
    OversamplingFactor factor_ = OversamplingFactor::x2;
    std::uint32_t transitionFrames_ = 0;
    std::uint32_t latencySamples_ = 0;
    double toneCoefficient_ = 0.0;
    LinearSmoother drive_;
    LinearSmoother bias_;
    LinearSmoother tone_;
    LinearSmoother mix_;
    std::array<double, 7> weights_{};
    std::array<double, 7> weightSteps_{};
    std::uint32_t remainingModelFrames_ = 0;
    std::array<ChannelState, 2> channels_{};
};

} // namespace ahq::audio
