#include "ahq/audio/noiseGate.h"
#include <algorithm>
#include <cmath>
namespace ahq::audio {
bool NoiseGateParameters::isValid() const noexcept {
    return thresholdRange.contains(thresholdDb) && hysteresisRange.contains(hysteresisDb) &&
           attackRange.contains(attackMs) && holdRange.contains(holdMs) &&
           releaseRange.contains(releaseMs) && rangeRange.contains(rangeDb) &&
           mixRange.contains(mix);
}
void NoiseGate::update() noexcept {
    opening_ = std::pow(10., parameters_.thresholdDb / 20);
    closing_ = opening_ * std::pow(10., -parameters_.hysteresisDb / 20);
    attack_ = -std::expm1(-1 / (parameters_.attackMs * .001 * rate_));
    release_ = -std::expm1(-1 / (parameters_.releaseMs * .001 * rate_));
    detectorRelease_ = std::exp(-1 / (.01 * rate_));
    holdFrames_ = unsigned(std::ceil(parameters_.holdMs * .001 * rate_));
    floor_ = parameters_.rangeDb <= -96 ? 0 : std::pow(10., parameters_.rangeDb / 20);
}
bool NoiseGate::prepare(double rate) noexcept {
    if (!std::isfinite(rate) || rate < 8000 || rate > 384000)
        return false;
    rate_ = rate;
    update();
    reset();
    return true;
}
bool NoiseGate::setParameters(const NoiseGateParameters &p) noexcept {
    if (rate_ == 0 || !p.isValid())
        return false;
    if (p != parameters_) {
        parameters_ = p;
        update();
    }
    return true;
}
void NoiseGate::reset() noexcept {
    detector_ = 0;
    gain_ = floor_;
    open_ = false;
    holdRemaining_ = 0;
}
bool NoiseGate::process(std::span<float> left, std::span<float> right) noexcept {
    if (!right.empty() && right.size() != left.size())
        return false;
    if (left.empty())
        return true;
    if (rate_ == 0)
        return false;
    for (size_t i = 0; i < left.size(); ++i) {
        const double level =
            std::max(std::abs(double(left[i])), right.empty() ? 0 : std::abs(double(right[i])));
        detector_ = std::max(level, detector_ * detectorRelease_);
        if (detector_ >= opening_) {
            open_ = true;
            holdRemaining_ = holdFrames_;
        } else if (open_) {
            if (detector_ >= closing_)
                holdRemaining_ = holdFrames_;
            else if (holdRemaining_ > 0)
                --holdRemaining_;
            else
                open_ = false;
        }
        const double target = open_ ? 1 : floor_;
        gain_ += (target > gain_ ? attack_ : release_) * (target - gain_);
        const float gain = float(1 + parameters_.mix * (gain_ - 1));
        left[i] *= gain;
        if (!right.empty())
            right[i] *= gain;
    }
    return true;
}
} // namespace ahq::audio
