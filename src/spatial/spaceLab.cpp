#include "ahq/audio/spaceLab.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <numbers>

namespace ahq::audio {

bool SpaceLabParameters::isValid() const noexcept {
    return widthRange.contains(width) && signedHaasMsRange.contains(signedHaasMs) &&
           leftTiltDbRange.contains(leftTiltDb) && rightTiltDbRange.contains(rightTiltDb) &&
           modRateHzRange.contains(modRateHz) && modDepthMsRange.contains(modDepthMs) &&
           mixRange.contains(mix);
}

bool SpaceLab::prepare(double sampleRate) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0) {
        return false;
    }
    std::array<std::vector<float>, 3> history;
    try {
        const auto frames = static_cast<std::size_t>(std::ceil(sampleRate * 0.07)) + 2;
        for (auto &channel : history) {
            channel.resize(frames);
        }
    } catch (const std::bad_alloc &) {
        return false;
    }
    history_ = std::move(history);
    sampleRate_ = sampleRate;
    transitionFrames_ = static_cast<std::uint32_t>(std::ceil(sampleRate * 0.005));
    eqCoefficient_ = -std::expm1(-2.0 * std::numbers::pi * 1000.0 / sampleRate);
    reset();
    return true;
}

bool SpaceLab::setParameters(const SpaceLabParameters &parameters) noexcept {
    if (transitionFrames_ == 0 || !parameters.isValid()) {
        return false;
    }
    parameters_ = parameters;
    width_.setTarget(parameters.width, transitionFrames_);
    haas_.setTarget(parameters.signedHaasMs, transitionFrames_);
    leftTilt_.setTarget(parameters.leftTiltDb, transitionFrames_);
    rightTilt_.setTarget(parameters.rightTiltDb, transitionFrames_);
    leftPolarity_.setTarget(parameters.invertLeft ? -1.0 : 1.0, transitionFrames_);
    rightPolarity_.setTarget(parameters.invertRight ? -1.0 : 1.0, transitionFrames_);
    modRate_.setTarget(parameters.modRateHz, transitionFrames_);
    modDepth_.setTarget(parameters.modDepthMs, transitionFrames_);
    split_.setTarget(parameters.split ? 1.0 : 0.0, transitionFrames_);
    mix_.setTarget(parameters.mix, transitionFrames_);
    return true;
}

void SpaceLab::reset() noexcept {
    for (auto &channel : history_) {
        std::fill(channel.begin(), channel.end(), 0.0F);
    }
    position_ = 0;
    eqLow_ = {};
    modulationPhase_ = 0.0;
    width_.reset(parameters_.width);
    haas_.reset(parameters_.signedHaasMs);
    leftTilt_.reset(parameters_.leftTiltDb);
    rightTilt_.reset(parameters_.rightTiltDb);
    leftPolarity_.reset(parameters_.invertLeft ? -1.0 : 1.0);
    rightPolarity_.reset(parameters_.invertRight ? -1.0 : 1.0);
    modRate_.reset(parameters_.modRateHz);
    modDepth_.reset(parameters_.modDepthMs);
    split_.reset(parameters_.split ? 1.0 : 0.0);
    mix_.reset(parameters_.mix);
}

double SpaceLab::readDelay(std::size_t channel, double frames) const noexcept {
    const auto firstDelay = static_cast<std::size_t>(frames);
    const double fraction = frames - static_cast<double>(firstDelay);
    const auto first =
        (position_ + history_[channel].size() - firstDelay) % history_[channel].size();
    const auto second = first == 0 ? history_[channel].size() - 1 : first - 1;
    return (1.0 - fraction) * history_[channel][first] + fraction * history_[channel][second];
}

double SpaceLab::applyTilt(double input, std::size_t channel, double tiltDb) noexcept {
    eqLow_[channel] += eqCoefficient_ * (input - eqLow_[channel]);
    if (tiltDb == 0.0) {
        return input;
    }
    const double highGain = std::pow(10.0, tiltDb / 40.0);
    const double lowGain = 1.0 / highGain;
    return lowGain * eqLow_[channel] + highGain * (input - eqLow_[channel]);
}

bool SpaceLab::process(std::span<float> left, std::span<float> right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    if (left.empty()) {
        return true;
    }
    if (transitionFrames_ == 0) {
        return false;
    }
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        const double width = width_.next();
        const double haasMs = haas_.next();
        const double leftTilt = leftTilt_.next();
        const double rightTilt = rightTilt_.next();
        const double leftPolarity = leftPolarity_.next();
        const double rightPolarity = rightPolarity_.next();
        const double modRate = modRate_.next();
        const double modDepth = modDepth_.next();
        const double split = split_.next();
        const double mix = mix_.next();
        const double dryLeft = left[frame];
        const double dryRight = right[frame];
        const double mid = 0.5 * (dryLeft + dryRight);
        const double side = 0.5 * (dryLeft - dryRight);
        history_[0][position_] =
            width == 1.0 ? left[frame] : static_cast<float>(mid + width * side);
        history_[1][position_] =
            width == 1.0 ? right[frame] : static_cast<float>(mid - width * side);
        history_[2][position_] = static_cast<float>(mid);
        const double rightDelayMs =
            std::max(0.0, haasMs) + modDepth * 0.5 * (1.0 + std::sin(modulationPhase_));
        const double leftDelayed = readDelay(0, std::max(0.0, -haasMs) * 0.001 * sampleRate_);
        const double rightNormal = readDelay(1, rightDelayMs * 0.001 * sampleRate_);
        const double rightMid = readDelay(2, rightDelayMs * 0.001 * sampleRate_);
        const double leftNormal = applyTilt(leftDelayed, 0, leftTilt) * leftPolarity;
        const double rightSource = (1.0 - split) * rightNormal + split * rightMid;
        const double wetRight = applyTilt(rightSource, 1, rightTilt) * rightPolarity;
        const double wetLeft = (1.0 - split) * leftNormal + split * mid;
        if (mix != 0.0) {
            left[frame] = static_cast<float>((1.0 - mix) * dryLeft + mix * wetLeft);
            right[frame] = static_cast<float>((1.0 - mix) * dryRight + mix * wetRight);
        }
        modulationPhase_ += 2.0 * std::numbers::pi * modRate / sampleRate_;
        if (modulationPhase_ >= 2.0 * std::numbers::pi) {
            modulationPhase_ -= 2.0 * std::numbers::pi;
        }
        position_ = (position_ + 1) % history_[0].size();
    }
    return true;
}

} // namespace ahq::audio
