#pragma once

#include <array>
#include <span>

namespace ahq::audio {

struct PeakReading {
    double samplePeak = 0.0;
    double truePeak = 0.0;
    [[nodiscard]] double samplePeakDb() const noexcept;
    [[nodiscard]] double truePeakDb() const noexcept;
};

/// Per-channel maximum sample peak and estimated intersample peak, using the
/// ITU-R BS.1770-5 Annex 2 four-phase, 12-tap-per-phase FIR. All four phases
/// are evaluated; the original sample peak is also a lower bound on truePeak.
/// The FIR is delayed 23.5 oversampled frames (5.875 input frames). This is a
/// finite 4x estimate, with filter ripple and under-reading near Nyquist, not
/// an exact bound on an arbitrary reconstructed waveform. Below 48 kHz, 4x
/// does not reach Annex 2's 192 kHz guidance. No official-set certification.
/// All calls to an instance must be on one thread.
class TruePeakMeter {
  public:
    /// Accepts finite [8000, 384000] Hz and 1 or 2 channels; successful prepare
    /// clears history and peaks. Failure preserves the complete previous state.
    [[nodiscard]] bool prepare(double sampleRate, unsigned channels) noexcept;
    /// Clears history, peaks and finished state, retaining preparation.
    void reset() noexcept;
    /// Clears only held maxima. Retained FIR history can contribute to future
    /// peaks, so reset must be used to start an independent measurement.
    void clearPeaks() noexcept;
    /// Read-only input. Mono requires empty right, stereo equal-sized right.
    /// Finite input is checked before any mutation; invalid shape, samples,
    /// preparation or a nonempty call after finish returns false unchanged.
    /// Empty valid-shaped buffers succeed without advancing state.
    /// No allocation, locking or I/O; work per frame is bounded by 2*4*12 taps.
    [[nodiscard]] bool process(std::span<const float> left,
                               std::span<const float> right = {}) noexcept;
    /// Ends a measurement by feeding exactly 11 zero frames to include all
    /// remaining FIR output. Zeros do not contribute to samplePeak. Repeated
    /// finish calls preserve readings. Nonempty process then requires reset or
    /// successful prepare. False before preparation, preserving all state.
    [[nodiscard]] bool finish() noexcept;
    /// Index 0 is left/mono, 1 is right; inactive/out-of-range indices return
    /// zero peaks. Db methods map zero to negative infinity, never NaN.
    [[nodiscard]] PeakReading reading(unsigned channel = 0) const noexcept;

  private:
    void processFrame(double left, double right, bool countSamplePeak) noexcept;
    std::array<std::array<double, 12>, 2> history_{};
    std::array<PeakReading, 2> readings_{};
    unsigned channels_ = 0;
    bool finished_ = false;
};

} // namespace ahq::audio
