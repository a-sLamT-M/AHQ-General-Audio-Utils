#pragma once
#include <array>
#include <cstdint>

namespace ahq::audio {
struct StereoReading {
    std::array<double, 2> rms{};
    std::array<double, 2> crestDb{}; // Undefined on silence; zero placeholder when RMS is zero.
    double midEnergy = 0, sideEnergy = 0, correlation = 0;
    bool valid = false, stereo = false, correlationValid = false;
};
/// Nonoverlapping rectangular 400 ms measurements, published once per complete window.
/// Crest is 20*log10(max(abs(sample))/RMS) in that same window. M/S use (L+R)/2,(L-R)/2.
/// Mono has no correlation or M/S interpretation. All calls belong to one thread.
class StereoMeter {
  public:
    /// Finite [8000,384000] Hz, mono/stereo. No allocation. Success resets all readings.
    [[nodiscard]] bool prepare(double sampleRate, unsigned channels) noexcept;
    /// Clears scalar accumulators and published reading in constant time.
    void reset() noexcept;
    /// Read-only bounded work; invalid input/preparation leaves state unchanged.
    [[nodiscard]] bool processSample(float left, float right = 0) noexcept;
    /// The last completed 400 ms block, or invalid until the first complete block after reset.
    [[nodiscard]] StereoReading reading() const noexcept { return reading_; }

  private:
    StereoReading reading_;
    unsigned channels_ = 0;
    std::uint32_t windowFrames_ = 0, frames_ = 0;
    std::array<double, 2> sumSquares_{}, maximum_{};
    double cross_ = 0;
};
} // namespace ahq::audio
