#pragma once

#include <cstdint>
#include <span>

namespace ahq::audio {

/// In-place mono or stereo gain with fixed 5 ms linear gain and mix transitions.
/// Call all functions from one thread; concurrent access to an instance is unsupported.
class GainProcessor {
  public:
    /// Accepts a finite sample rate in [1, 384000] Hz. A successful call
    /// aligns current gain and mix with their targets and clears both transitions.
    /// Failure leaves the complete previous state unchanged.
    [[nodiscard]] bool prepare(double sampleRate) noexcept;

    /// Accepts a finite linear gain in [0, 16]. A new target starts a full 5 ms
    /// transition from the current value. Repeating the target preserves an
    /// existing transition. Requires a successful prepare().
    [[nodiscard]] bool setGain(float linearGain) noexcept;

    /// Accepts a finite mix in [0, 1], where 0 is dry and 1 is fully gained.
    /// A changed target follows the same 5 ms transition rule as gain.
    /// Requires a successful prepare(). Failure preserves the previous state.
    [[nodiscard]] bool setMix(float mix) noexcept;

    /// Immediately sets current gain and mix to their targets.
    void reset() noexcept;

    /// Processes all frames in left; an empty right selects mono. For stereo,
    /// right must have the same size as left. Empty buffers succeed without
    /// advancing state. Nonempty buffers require a successful prepare().
    /// The spans must refer to valid, distinct, nonoverlapping buffers. Input
    /// samples must be finite. No clipping is applied, so large samples may
    /// produce nonfinite output. Returns false for invalid size or missing prepare.
    /// Processing performs no allocation, I/O, or locking.
    [[nodiscard]] bool process(std::span<float> left, std::span<float> right = {}) noexcept;

  private:
    float currentGain_ = 1.0F;
    float targetGain_ = 1.0F;
    float gainStep_ = 0.0F;
    float currentMix_ = 1.0F;
    float targetMix_ = 1.0F;
    float mixStep_ = 0.0F;
    std::uint32_t transitionFrames_ = 0;
    std::uint32_t remainingGainFrames_ = 0;
    std::uint32_t remainingMixFrames_ = 0;
};

} // namespace ahq::audio
