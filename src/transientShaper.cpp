#include "ahq/audio/transientShaper.h"

#include <algorithm>
#include <cmath>

namespace ahq::audio {
namespace {

bool validParameters(const TransientShaperParameters &parameters) noexcept {
    return std::isfinite(parameters.attackDb) && parameters.attackDb >= -12.0F &&
           parameters.attackDb <= 12.0F && std::isfinite(parameters.sustainDb) &&
           parameters.sustainDb >= -12.0F && parameters.sustainDb <= 12.0F &&
           std::isfinite(parameters.sensitivityDb) && parameters.sensitivityDb >= -80.0F &&
           parameters.sensitivityDb <= -20.0F && std::isfinite(parameters.timeMs) &&
           parameters.timeMs >= 5.0F && parameters.timeMs <= 200.0F &&
           std::isfinite(parameters.mix) && parameters.mix >= 0.0F && parameters.mix <= 1.0F;
}

} // namespace

bool TransientShaper::prepare(double sampleRate) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 1.0 || sampleRate > 384000.0) {
        return false;
    }
    sampleRate_ = sampleRate;
    transitionFrames_ = static_cast<std::uint32_t>(std::ceil(sampleRate * 0.005));
    reset();
    return true;
}

bool TransientShaper::setParameters(const TransientShaperParameters &parameters) noexcept {
    if (transitionFrames_ == 0 || !validParameters(parameters)) {
        return false;
    }
    parameters_ = parameters;
    attack_.setTarget(parameters.attackDb, transitionFrames_);
    sustain_.setTarget(parameters.sustainDb, transitionFrames_);
    sensitivity_.setTarget(parameters.sensitivityDb, transitionFrames_);
    time_.setTarget(parameters.timeMs, transitionFrames_);
    mix_.setTarget(parameters.mix, transitionFrames_);
    return true;
}

void TransientShaper::reset() noexcept {
    fastEnvelope_ = 0.0;
    slowEnvelope_ = 0.0;
    attack_.reset(parameters_.attackDb);
    sustain_.reset(parameters_.sustainDb);
    sensitivity_.reset(parameters_.sensitivityDb);
    time_.reset(parameters_.timeMs);
    mix_.reset(parameters_.mix);
}

bool TransientShaper::process(std::span<float> left, std::span<float> right) noexcept {
    if (!right.empty() && right.size() != left.size()) {
        return false;
    }
    if (left.empty()) {
        return true;
    }
    if (transitionFrames_ == 0) {
        return false;
    }
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        const double attackDb = attack_.next();
        const double sustainDb = sustain_.next();
        const double threshold = std::pow(10.0, sensitivity_.next() / 20.0);
        const double timeFrames = time_.next() * 0.001 * sampleRate_;
        const double mix = mix_.next();
        double amplitude = std::abs(static_cast<double>(left[frame]));
        if (!right.empty()) {
            amplitude = std::max(amplitude, std::abs(static_cast<double>(right[frame])));
        }
        fastEnvelope_ = std::max(amplitude, fastEnvelope_ * std::exp(-1.0 / timeFrames));
        slowEnvelope_ += -std::expm1(-1.0 / timeFrames) * (fastEnvelope_ - slowEnvelope_);
        if (fastEnvelope_ <= threshold || mix == 0.0 || (attackDb == 0.0 && sustainDb == 0.0)) {
            continue;
        }
        const double activity = (fastEnvelope_ - threshold) / (fastEnvelope_ + threshold);
        const double onset =
            std::clamp((fastEnvelope_ - slowEnvelope_) / (fastEnvelope_ + threshold), 0.0, 1.0);
        const double gainDb = activity * (attackDb * onset + sustainDb * (1.0 - onset));
        const double gain = (1.0 - mix) + mix * std::pow(10.0, gainDb / 20.0);
        left[frame] = static_cast<float>(left[frame] * gain);
        if (!right.empty()) {
            right[frame] = static_cast<float>(right[frame] * gain);
        }
    }
    return true;
}

} // namespace ahq::audio
