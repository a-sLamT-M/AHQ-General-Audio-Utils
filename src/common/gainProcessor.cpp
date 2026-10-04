#include "ahq/audio/gainProcessor.h"

#include <cmath>

namespace ahq::audio {

bool GainProcessor::prepare(double sampleRate) noexcept {
    constexpr double MAX_SAMPLE_RATE = 384000.0;
    constexpr double TRANSITION_SECONDS = 0.005;
    if (!std::isfinite(sampleRate) || sampleRate < 1.0 || sampleRate > MAX_SAMPLE_RATE) {
        return false;
    }

    transitionFrames_ = static_cast<std::uint32_t>(std::ceil(sampleRate * TRANSITION_SECONDS));
    currentGain_ = targetGain_;
    gainStep_ = 0.0F;
    remainingGainFrames_ = 0;
    currentMix_ = targetMix_;
    mixStep_ = 0.0F;
    remainingMixFrames_ = 0;
    return true;
}

bool GainProcessor::setGain(float linearGain) noexcept {
    constexpr float MAX_GAIN = 16.0F;
    if (transitionFrames_ == 0 || !std::isfinite(linearGain) || linearGain < 0.0F ||
        linearGain > MAX_GAIN) {
        return false;
    }
    if (linearGain == targetGain_) {
        return true;
    }

    targetGain_ = linearGain;
    remainingGainFrames_ = transitionFrames_;
    gainStep_ = (targetGain_ - currentGain_) / static_cast<float>(remainingGainFrames_);
    return true;
}

bool GainProcessor::setMix(float mix) noexcept {
    if (transitionFrames_ == 0 || !std::isfinite(mix) || mix < 0.0F || mix > 1.0F) {
        return false;
    }
    if (mix == targetMix_) {
        return true;
    }

    targetMix_ = mix;
    remainingMixFrames_ = transitionFrames_;
    mixStep_ = (targetMix_ - currentMix_) / static_cast<float>(remainingMixFrames_);
    return true;
}

void GainProcessor::reset() noexcept {
    currentGain_ = targetGain_;
    gainStep_ = 0.0F;
    remainingGainFrames_ = 0;
    currentMix_ = targetMix_;
    mixStep_ = 0.0F;
    remainingMixFrames_ = 0;
}

bool GainProcessor::process(std::span<float> left, std::span<float> right) noexcept {
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
        if (remainingGainFrames_ != 0) {
            currentGain_ += gainStep_;
            --remainingGainFrames_;
            if (remainingGainFrames_ == 0) {
                currentGain_ = targetGain_;
            }
        }
        if (remainingMixFrames_ != 0) {
            currentMix_ += mixStep_;
            --remainingMixFrames_;
            if (remainingMixFrames_ == 0) {
                currentMix_ = targetMix_;
            }
        }

        const float gain = (1.0F - currentMix_) + currentMix_ * currentGain_;
        left[frame] *= gain;
        if (!right.empty()) {
            right[frame] *= gain;
        }
    }
    return true;
}

} // namespace ahq::audio
