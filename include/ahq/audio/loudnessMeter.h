#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace ahq::audio {

struct LoudnessReading {
    double lufs = -std::numeric_limits<double>::infinity();
    bool valid = false;
};

struct LoudnessBlock {
    double energy = 0.0;
    std::uint64_t endFrame = 0;
    bool ready = false;
};

/// BS.1770 K-weighted mono/stereo energy and EBU rectangular M/S windows.
/// All calls to an instance must be on one thread.
class LoudnessMeter {
  public:
    /// Accepts finite [32000, 384000] Hz and 1 or 2 channels. Allocates the 3 s
    /// energy ring; failure (including allocation failure) preserves all state.
    /// The supported range keeps the weighting response within 0.025 dB of the
    /// published 48 kHz response at the same physical frequency, 50 Hz to
    /// min(20 kHz, 0.45 * sampleRate). This is a design accuracy bound, not
    /// certification against the official audio test set.
    /// Success clears filters, windows and block timing. Durations 0.4 s, 3 s
    /// and 0.1 s are individually rounded to the nearest frame (half rounds up).
    [[nodiscard]] bool prepare(double sampleRate, unsigned channels) noexcept;

    /// Clears measurements without allocating; retains preparation and channels.
    /// Not an audio callback operation: clearing the ring takes up to 3 s frames.
    void reset() noexcept;

    /// Advances one frame and replaces block. Mono ignores right, stereo sums
    /// both channels with weight 1, with no channel-count division or dual-mono
    /// compensation. Inputs used must be finite floats; buffers are never modified.
    /// Requires prepare; false preserves state and block. Complete 400 ms blocks
    /// start at frame 0 and every rounded 100 ms; incomplete final blocks are
    /// discarded. block.endFrame is the exclusive position since prepare/reset.
    /// This function performs bounded work, no allocation, locking or I/O.
    [[nodiscard]] bool processSample(float left, float right, LoudnessBlock &block) noexcept;
    [[nodiscard]] LoudnessReading momentary() const noexcept;
    [[nodiscard]] LoudnessReading shortTerm() const noexcept;

  private:
    struct Biquad {
        std::array<double, 3> b{};
        std::array<double, 2> a{};
        double z1 = 0.0;
        double z2 = 0.0;
        double process(double input) noexcept;
    };
    std::array<Biquad, 2> shelf_{};
    std::array<Biquad, 2> highPass_{};
    std::vector<double> energies_;
    std::size_t position_ = 0;
    std::size_t momentaryFrames_ = 0;
    std::size_t stepFrames_ = 0;
    std::size_t momentaryNonzero_ = 0;
    std::size_t shortTermNonzero_ = 0;
    std::uint64_t frames_ = 0;
    std::uint64_t nextBlockEnd_ = 0;
    double momentarySum_ = 0.0;
    double shortTermSum_ = 0.0;
    unsigned channels_ = 0;
};

/// Offline/UI-thread collection only: addBlock may allocate and reading scans
/// the entire history twice. Never call these functions on the audio thread.
/// Feed every ready block from one uninterrupted LoudnessMeter measurement.
class IntegratedLoudness {
  public:
    /// Accepts finite nonnegative energy; invalid energy preserves history.
    /// Allocation failure is propagated; the previous history is retained.
    [[nodiscard]] bool addBlock(double energy);
    void reset() noexcept;
    [[nodiscard]] LoudnessReading reading() const noexcept;
    [[nodiscard]] std::size_t blockCount() const noexcept;

  private:
    std::vector<double> energies_;
};

} // namespace ahq::audio
