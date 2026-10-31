#include "ahq/audio/classicCompressor.h"
#include <algorithm>
#include <cmath>
namespace ahq::audio {
bool ClassicCompressorParameters::isValid() const noexcept {
    return inputRange.contains(inputDb) && outputRange.contains(outputDb) &&
           reductionRange.contains(peakReduction) && attackRange.contains(attackMs) &&
           releaseRange.contains(releaseMs) && mixRange.contains(mix) && ratioRange.contains(ratio);
}
bool ClassicCompressor::prepare(double rate) noexcept {
    if (!std::isfinite(rate) || rate < 8000 || rate > 384000)
        return false;
    rate_ = rate;
    smoothingFrames_ = unsigned(std::ceil(rate * .005));
    memoryAttack_ = -std::expm1(-1 / (.5 * rate));
    memoryRelease_ = -std::expm1(-1 / (15 * rate));
    update();
    reset();
    return true;
}
bool ClassicCompressor::setParameters(const ClassicCompressorParameters &p) noexcept {
    if (rate_ == 0 || !p.isValid())
        return false;
    if (p == parameters_)
        return true;
    parameters_ = p;
    update();
    return true;
}
void ClassicCompressor::update() noexcept {
    input_.setTarget(std::pow(10., parameters_.inputDb / 20), smoothingFrames_);
    output_.setTarget(std::pow(10., parameters_.outputDb / 20), smoothingFrames_);
    mix_.setTarget(parameters_.mix, smoothingFrames_);
    if (model_ == Model::Opto) {
        threshold_ = -parameters_.peakReduction * .6;
        ratio_ = parameters_.limit ? 20 : 3;
        knee_ = 12;
        attack_ = -std::expm1(-1 / (.010 * rate_));
        // Half the release is fast; the remaining component retains illumination history.
        release_ = -std::expm1(-std::log(2.) / (.060 * rate_));
    } else {
        constexpr double ratios[]{4, 8, 12, 20, 16};
        ratio_ = ratios[parameters_.ratio];
        threshold_ = parameters_.ratio == 4 ? -22 : -18;
        knee_ = parameters_.ratio == 4 ? 12 : 6;
        attack_ = -std::expm1(-1 / (parameters_.attackMs * .001 * rate_));
        release_ = -std::expm1(-1 / (parameters_.releaseMs * .001 * rate_));
    }
}
void ClassicCompressor::reset() noexcept {
    input_.reset(std::pow(10., parameters_.inputDb / 20));
    output_.reset(std::pow(10., parameters_.outputDb / 20));
    mix_.reset(parameters_.mix);
    fast_ = slow_ = memory_ = reduction_ = 0;
    controlFrame_ = 0;
    slowRelease_ = rate_ > 0 ? -std::expm1(-1 / rate_) : 0;
}
bool ClassicCompressor::process(std::span<float> left, std::span<float> right) noexcept {
    if (!right.empty() && right.size() != left.size())
        return false;
    if (left.empty())
        return true;
    if (rate_ == 0)
        return false;
    for (size_t i = 0; i < left.size(); ++i) {
        const double input = input_.next(), output = output_.next(), mix = mix_.next();
        const double l = left[i] * input, r = right.empty() ? l : right[i] * input;
        const double level = std::max(std::abs(l), std::abs(r));
        const double over = 20 * std::log10(std::max(1e-12, level)) - threshold_;
        const double slope = 1 - 1 / ratio_;
        const double target = over <= -knee_ * .5 ? 0
                              : over >= knee_ * .5
                                  ? over * slope
                                  : slope * (over + knee_ * .5) * (over + knee_ * .5) / (2 * knee_);
        fast_ += (target > fast_ ? attack_ : release_) * (target - fast_);
        if (model_ == Model::Opto) {
            memory_ += (target > memory_ ? memoryAttack_ : memoryRelease_) * (target - memory_);
            if ((controlFrame_++ & 31U) == 0)
                slowRelease_ =
                    -std::expm1(-1 / ((1 + 14 * std::clamp(memory_ / 24, 0., 1.)) * rate_));
            slow_ += (target > slow_ ? attack_ : slowRelease_) * (target - slow_);
            reduction_ = .5 * (fast_ + slow_);
        } else
            reduction_ = fast_;
        const double gain = std::pow(10., -reduction_ / 20) * output;
        left[i] = float(left[i] * (1 - mix) + l * gain * mix);
        if (!right.empty())
            right[i] = float(right[i] * (1 - mix) + r * gain * mix);
    }
    return true;
}
} // namespace ahq::audio
