#include "ahq/audio/stereoMeter.h"
#include <algorithm>
#include <cmath>

namespace ahq::audio {
bool StereoMeter::prepare(double rate, unsigned channels) noexcept {
    if (!std::isfinite(rate) || rate < 8000 || rate > 384000 || (channels != 1 && channels != 2))
        return false;
    channels_ = channels;
    windowFrames_ = std::uint32_t(std::floor(rate * 0.4 + 0.5));
    reset();
    return true;
}
void StereoMeter::reset() noexcept {
    reading_ = {};
    frames_ = 0;
    sumSquares_ = {};
    maximum_ = {};
    cross_ = 0;
}
bool StereoMeter::processSample(float left, float right) noexcept {
    if (channels_ == 0 || !std::isfinite(left) || (channels_ == 2 && !std::isfinite(right)))
        return false;
    const std::array<double, 2> samples{left, channels_ == 2 ? right : 0};
    for (unsigned channel = 0; channel < channels_; ++channel) {
        sumSquares_[channel] += samples[channel] * samples[channel];
        maximum_[channel] = std::max(maximum_[channel], std::abs(samples[channel]));
    }
    if (channels_ == 2)
        cross_ += samples[0] * samples[1];
    if (++frames_ == windowFrames_) {
        reading_ = {};
        reading_.valid = true;
        reading_.stereo = channels_ == 2;
        for (unsigned channel = 0; channel < channels_; ++channel) {
            reading_.rms[channel] = std::sqrt(sumSquares_[channel] / windowFrames_);
            reading_.crestDb[channel] =
                reading_.rms[channel] > 0
                    ? 20 * std::log10(maximum_[channel] / reading_.rms[channel])
                    : 0;
        }
        if (channels_ == 2) {
            reading_.midEnergy =
                std::max(0.0, (sumSquares_[0] + sumSquares_[1] + 2 * cross_) / (4 * windowFrames_));
            reading_.sideEnergy =
                std::max(0.0, (sumSquares_[0] + sumSquares_[1] - 2 * cross_) / (4 * windowFrames_));
            const double divisor = std::sqrt(sumSquares_[0] * sumSquares_[1]);
            reading_.correlationValid = divisor > 0;
            if (divisor > 0)
                reading_.correlation = std::clamp(cross_ / divisor, -1.0, 1.0);
        }
        frames_ = 0;
        sumSquares_ = {};
        maximum_ = {};
        cross_ = 0;
    }
    return true;
}
} // namespace ahq::audio
