#pragma once

#include "ahq/audio/linearSmoother.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace ahq::audio {

struct ChaosParameters {
    float sliceMs = 100.0F;
    float probability = 0.0F;
    std::uint32_t repeats = 1;
    float reverseProbability = 0.0F;
    float pitchRangeSemitones = 0.0F;
    float panAmount = 0.0F;
    float driveDb = 0.0F;
    float distortionProbability = 0.0F;
    std::uint32_t seed = 1;
    float mix = 1.0F;
};

/// Reproducible past-slice repetition/reversal/pitch/pan/distortion. Calls use one thread.
/// Creative linear pitch interpolation can alias; no feedback or fixed host latency.
class Chaos {
  public:
    /// Finite sample rate [8000,384000] Hz; preallocates nine seconds of stereo float history
    /// (about 3.3 MiB at 48 kHz / 26.4 MiB at 384 kHz). Invalid rate/allocation failure leaves
    /// all previous state unchanged. Successful prepare resets playback/history/PRNG.
    [[nodiscard]] bool prepare(double sampleRate) noexcept;
    /// Slice [10,250] ms; repeats integer [1,8]; probability/reverseProbability/panAmount/
    /// distortionProbability/mix [0,1]; random pitch range [0,24] semitones; drive [0,36] dB.
    /// All floats finite; prepare required. Mix ramps for 5 ms, other settings latch at the next
    /// decision. Changed seed resets only PRNG; repeating a seed never resets playback/history.
    /// Successful decisions capture the preceding slice, replay it repeats times at random
    /// +/-pitchRange speed, optionally reverse it, apply attenuating stereo pan and tanh drive.
    /// Each repetition lasts ceil(sliceFrames/speed); completed events make the next decision.
    /// Failed decisions pass original dry for one slice duration, without amplitude windows.
    /// Each successful repetition fades to/from current dry over 3 ms (short cycles: half length).
    /// Probability0 is exact dry once any already-latched event finishes; probability1 always
    /// selects a slice, with dry edge fades. Initial capture before stream start is zero history.
    /// Invalid settings preserve complete state; no live source-segment copying occurs.
    [[nodiscard]] bool setParameters(const ChaosParameters &parameters) noexcept;
    /// Clears history/playback and resets PRNG, clock and mix to targets.
    void reset() noexcept;
    /// Empty right means mono; stereo must have equal lengths and nonoverlapping spans.
    /// Empty blocks do not advance state. Mix0 is exact original input; no final clipping.
    /// Finite normal audio inputs expected; extreme samples can overflow intermediate/output.
    /// Invalid sizes/unprepared nonempty calls preserve buffers/state. No allocation/locks/I/O.
    [[nodiscard]] bool process(std::span<float> left, std::span<float> right = {}) noexcept;

  private:
    double randomUnit() noexcept;
    void decideEvent() noexcept;
    double readHistory(std::size_t channel, double offset) const noexcept;

    ChaosParameters parameters_;
    double sampleRate_ = 0.0;
    std::uint32_t transitionFrames_ = 0;
    std::array<std::vector<float>, 2> history_;
    std::uint64_t frame_ = 0;
    std::uint32_t randomState_ = 1;
    LinearSmoother mix_;
    bool eventActive_ = false;
    bool eventReverse_ = false;
    bool eventDistortion_ = false;
    std::int64_t sourceStart_ = 0;
    std::uint32_t sourceFrames_ = 0;
    std::uint32_t cycleFrames_ = 0;
    std::uint32_t fadeFrames_ = 0;
    std::uint32_t remainingFrames_ = 0;
    std::uint32_t eventAge_ = 0;
    double eventSpeed_ = 1.0;
    double eventDrive_ = 1.0;
    std::array<double, 2> eventPan_{1.0, 1.0};
};

} // namespace ahq::audio
