#pragma once
#include <cstddef>
#include <vector>
namespace ahq::audio {
struct LoudnessRangeReading {
    double lu = 0;
    bool valid = false;
};
/// Off-audio EBU Tech 3342 distribution of 3 s short-term LUFS observations at 10 Hz.
/// Absolute gate >-70 LUFS, relative gate >mean energy -20 LU, 95th minus 10th percentile.
/// Short recordings and boundary silence can increase the reported range.
class LoudnessRange {
  public:
    /// May allocate. Invalid/no-signal observations are ignored. Update every ten accepted values.
    void addShortTerm(double lufs);
    void reset() noexcept;
    LoudnessRangeReading reading() const noexcept { return result; }

  private:
    std::vector<double> history, distribution;
    std::size_t lastCount = 0;
    LoudnessRangeReading result;
};
} // namespace ahq::audio
