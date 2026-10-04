#pragma once

#include "ahq/audio/parameterRange.h"

#include "ahq/audio/linearSmoother.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace ahq::audio {

struct GrainDelayParameters {
    float grainMs = 100.0F;
    float delayMs = 250.0F;
    float pitchSemitones = 0.0F;
    float scatter = 0.0F;
    float densityHz = 20.0F;
    float feedback = 0.0F;
    float randomness = 0.0F;
    std::uint32_t seed = 1;
    float mix = 0.5F;

    static constexpr ParameterRange grainMsRange{20, 500};
    static constexpr ParameterRange delayMsRange{10, 2000};
    static constexpr ParameterRange pitchSemitonesRange{-24, 24};
    static constexpr ParameterRange scatterRange{0, 1};
    static constexpr ParameterRange densityHzRange{1, 80};
    static constexpr ParameterRange feedbackRange{0, 0.95F};
    static constexpr ParameterRange randomnessRange{0, 1};
    static constexpr ParameterRange mixRange{0, 1};
    [[nodiscard]] bool isValid() const noexcept;
};

/// Creative Hann-windowed granular delay with a fixed 64-grain limit and linked stereo events.
/// Calls use one thread. Pitch uses linear interpolation, not antialiased high-fidelity shifting.
class GrainDelay {
  public:
    /// Finite sample rates [8000,384000] Hz. Allocates four seconds of stereo float history here
    /// (about 1.5 MiB at 48 kHz / 11.7 MiB at 384 kHz), then resets. Allocation failure returns
    /// false; invalid rates/failure leave all previous state unchanged.
    [[nodiscard]] bool prepare(double sampleRate) noexcept;
    /// Grain [20,500] ms; delay [10,2000] ms; pitch [-24,24] semitones; density [1,80] Hz;
    /// scatter/randomness/mix [0,1]; feedback [0,0.95]. Finite values; prepared instance required.
    /// Scatter jitters start delay by +/- 0.5*delay*scatter. Randomness adds up to +/-12 semitones
    /// (final pitch clamped to +/-24) and random stereo pan. Pan only attenuates: center keeps
    /// both channels at unity; panning toward one side reduces only the opposite side.
    /// Seed makes both effects reproducible.
    /// New grains latch all controls except feedback/mix, which ramp for 5 ms. Density changes
    /// retain the spawn clock and apply at its next event. A changed seed restarts PRNG only;
    /// repeating parameters never resets history/events/PRNG. Invalid calls preserve all state.
    /// Effective delay is raised if required to prevent pitched read heads catching the writer.
    [[nodiscard]] bool setParameters(const GrainDelayParameters &parameters) noexcept;
    /// Clears history/events, resets deterministic PRNG and clock, aligns smoothing to targets.
    void reset() noexcept;
    [[nodiscard]] std::uint32_t getActiveGrainCount() const noexcept;
    /// Stereo buffers must be same-sized, nonoverlapping; empty right selects mono.
    /// Full grain slots deterministically skip the next spawn without interrupting active grains.
    /// Wet overlap is divided by max(1,sum(Hann windows)); each ear's pan gain is at most one.
    /// Feedback writes input+feedback*tanh(previous wet): saturation affects only feedback.
    /// With silent input the loop is contractive, with gain no greater than feedback <=0.95.
    /// Mix zero preserves original input exactly. This musical delay has no host latency to report.
    /// Empty blocks do not advance state. Bad lengths/missing prepare preserve buffers/state.
    /// Input must be finite; normal audio amplitude around [-1,1] is expected. No final clipping;
    /// extreme float input can overflow output. No processing allocation, locking or I/O.
    [[nodiscard]] bool process(std::span<float> left, std::span<float> right = {}) noexcept;

  private:
    struct Grain {
        double readPosition = 0.0;
        double speed = 1.0;
        double leftGain = 1.0;
        double rightGain = 1.0;
        std::uint32_t age = 0;
        std::uint32_t duration = 0;
    };
    double randomSigned() noexcept;
    void spawnGrain() noexcept;
    float readHistory(std::size_t channel, double position) const noexcept;

    GrainDelayParameters parameters_;
    double sampleRate_ = 0.0;
    std::uint32_t transitionFrames_ = 0;
    std::array<std::vector<float>, 2> history_;
    std::array<Grain, 64> grains_{};
    std::uint64_t frame_ = 0;
    double framesUntilSpawn_ = 0.0;
    std::uint32_t randomState_ = 1;
    LinearSmoother feedback_;
    LinearSmoother mix_;
    std::array<double, 2> previousWet_{};
};

} // namespace ahq::audio
