#pragma once

#include <cstdint>

namespace ahq::audio {

/// A fixed-length linear transition; callers supply finite values and a positive frame count.
class LinearSmoother {
  public:
    void reset(double value) noexcept;
    void setTarget(double value, std::uint32_t frames) noexcept;
    [[nodiscard]] double next() noexcept;

  private:
    double current_ = 0.0;
    double target_ = 0.0;
    double step_ = 0.0;
    std::uint32_t remainingFrames_ = 0;
};

} // namespace ahq::audio
