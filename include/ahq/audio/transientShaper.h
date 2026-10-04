#pragma once

#include "ahq/audio/linearSmoother.h"

#include <cstdint>
#include <span>

namespace ahq::audio {

struct TransientShaperParameters {
    float attackDb = 0.0F;
    float sustainDb = 0.0F;
    float sensitivityDb = -60.0F;
    float timeMs = 50.0F;
    float mix = 1.0F;
};

/// Linked mono/stereo transient shaping. All calls to an instance use one thread.
/// An immediate-attack peak envelope releases over timeMs; a slow envelope follows that same
/// peak envelope over timeMs. Their positive difference selects attack; remaining activity selects
/// sustain. Comparing envelopes in the same amplitude domain avoids a peak/mean steady bias.
/// This is envelope shaping, not independent source separation or a compressor threshold.
class TransientShaper {
  public:
    /// Accepts finite sample rates in [1, 384000] Hz. Clears detection history and smoothing.
    /// Failure preserves the complete previous state.
    [[nodiscard]] bool prepare(double sampleRate) noexcept;

    /// Attack/sustain: [-12, 12] dB. Sensitivity: [-80, -20] dBFS envelope noise threshold.
    /// Time: [5, 200] ms. Mix: [0, 1]. Values must be finite; requires prepare().
    /// Parameters ramp for 5 ms; repeated targets preserve an existing transition.
    /// Signals whose fast envelope is at/below the sensitivity threshold receive unity gain.
    /// Above it, processing enters continuously. Invalid calls preserve all state.
    [[nodiscard]] bool setParameters(const TransientShaperParameters &parameters) noexcept;

    /// Clears detection history and immediately aligns current parameters to their targets.
    void reset() noexcept;

    /// Empty right selects mono; stereo spans must have equal lengths and not overlap.
    /// A common detector/gain preserves channel ratios. Both gain parameters at zero are
    /// strictly unity; mix zero preserves input exactly after its transition finishes.
    /// Empty blocks do not advance state. Missing prepare/invalid lengths return false without
    /// changing buffers/state. Input must be finite; large float samples may overflow output.
    /// No allocation, locking, clipping, or I/O is performed.
    [[nodiscard]] bool process(std::span<float> left, std::span<float> right = {}) noexcept;

  private:
    TransientShaperParameters parameters_;
    double sampleRate_ = 0.0;
    std::uint32_t transitionFrames_ = 0;
    LinearSmoother attack_;
    LinearSmoother sustain_;
    LinearSmoother sensitivity_;
    LinearSmoother time_;
    LinearSmoother mix_;
    double fastEnvelope_ = 0.0;
    double slowEnvelope_ = 0.0;
};

} // namespace ahq::audio
