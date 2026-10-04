#include "ahq/audio/linearSmoother.h"

namespace ahq::audio {

void LinearSmoother::reset(double value) noexcept {
    current_ = value;
    target_ = value;
    step_ = 0.0;
    remainingFrames_ = 0;
}

void LinearSmoother::setTarget(double value, std::uint32_t frames) noexcept {
    if (value == target_) {
        return;
    }
    if (frames == 0) {
        reset(value);
        return;
    }
    target_ = value;
    remainingFrames_ = frames;
    step_ = (target_ - current_) / static_cast<double>(frames);
}

double LinearSmoother::next() noexcept {
    if (remainingFrames_ != 0) {
        current_ += step_;
        --remainingFrames_;
        if (remainingFrames_ == 0) {
            current_ = target_;
        }
    }
    return current_;
}

} // namespace ahq::audio
