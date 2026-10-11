#include "ahq/audio/chaos.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace ahq::audio {

bool ChaosParameters::isValid() const noexcept {
    return sliceMsRange.contains(sliceMs) && probabilityRange.contains(probability) &&
           repeatsRange.contains(repeats) && reverseProbabilityRange.contains(reverseProbability) &&
           pitchRangeSemitonesRange.contains(pitchRangeSemitones) &&
           panAmountRange.contains(panAmount) && driveDbRange.contains(driveDb) &&
           distortionProbabilityRange.contains(distortionProbability) && mixRange.contains(mix);
}

bool Chaos::prepare(double sampleRate) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0) {
        return false;
    }
    std::array<std::vector<float>, 2> history;
    try {
        const auto frames = static_cast<std::size_t>(std::ceil(sampleRate * 9.0)) + 8;
        history[0].resize(frames);
        history[1].resize(frames);
    } catch (const std::bad_alloc &) {
        return false;
    }
    history_ = std::move(history);
    sampleRate_ = sampleRate;
    transitionFrames_ = static_cast<std::uint32_t>(std::ceil(sampleRate * 0.005));
    reset();
    return true;
}

bool Chaos::setParameters(const ChaosParameters &parameters) noexcept {
    if (transitionFrames_ == 0 || !parameters.isValid()) {
        return false;
    }
    if (parameters.seed != parameters_.seed) {
        randomState_ = parameters.seed;
    }
    parameters_ = parameters;
    mix_.setTarget(parameters.mix, transitionFrames_);
    return true;
}

void Chaos::reset() noexcept {
    for (auto &channel : history_) {
        std::fill(channel.begin(), channel.end(), 0.0F);
    }
    frame_ = 0;
    randomState_ = parameters_.seed;
    mix_.reset(parameters_.mix);
    eventActive_ = false;
    eventReverse_ = false;
    eventDistortion_ = false;
    sourceStart_ = 0;
    sourceFrames_ = 0;
    cycleFrames_ = 0;
    fadeFrames_ = 0;
    remainingFrames_ = 0;
    eventAge_ = 0;
    eventSpeed_ = 1.0;
    eventDrive_ = 1.0;
    eventPan_ = {1.0, 1.0};
}

double Chaos::randomUnit() noexcept {
    randomState_ = randomState_ * 1664525U + 1013904223U;
    return static_cast<double>(randomState_) / 4294967296.0;
}

void Chaos::decideEvent() noexcept {
    sourceFrames_ =
        static_cast<std::uint32_t>(std::ceil(parameters_.sliceMs * 0.001 * sampleRate_));
    eventActive_ = randomUnit() < parameters_.probability;
    eventAge_ = 0;
    if (!eventActive_) {
        remainingFrames_ = sourceFrames_;
        return;
    }
    sourceStart_ = static_cast<std::int64_t>(frame_) - sourceFrames_;
    eventReverse_ = randomUnit() < parameters_.reverseProbability;
    const double pitch = (2.0 * randomUnit() - 1.0) * parameters_.pitchRangeSemitones;
    eventSpeed_ = std::exp2(pitch / 12.0);
    const double pan = (2.0 * randomUnit() - 1.0) * parameters_.panAmount;
    eventPan_ = {std::min(1.0, std::sqrt(1.0 - pan)), std::min(1.0, std::sqrt(1.0 + pan))};
    eventDistortion_ = randomUnit() < parameters_.distortionProbability;
    eventDrive_ = std::pow(10.0, parameters_.driveDb / 20.0);
    cycleFrames_ = static_cast<std::uint32_t>(std::ceil(sourceFrames_ / eventSpeed_));
    remainingFrames_ = cycleFrames_ * parameters_.repeats;
    fadeFrames_ =
        std::min(static_cast<std::uint32_t>(std::ceil(sampleRate_ * 0.003)), cycleFrames_ / 2);
}

double Chaos::readHistory(std::size_t channel, double offset) const noexcept {
    const double sourcePosition = static_cast<double>(sourceStart_) + offset;
    const auto first = static_cast<std::int64_t>(std::floor(sourcePosition));
    const double fraction = sourcePosition - static_cast<double>(first);
    const auto finalSource = sourceStart_ + sourceFrames_ - 1;
    const auto sample = [&](std::int64_t index) {
        index = std::min(index, finalSource);
        if (index < 0 || index < sourceStart_ || index >= static_cast<std::int64_t>(frame_) ||
            static_cast<double>(frame_) - static_cast<double>(index) >= history_[channel].size()) {
            return 0.0;
        }
        const auto position =
            static_cast<std::size_t>(static_cast<std::uint64_t>(index) % history_[channel].size());
        return static_cast<double>(history_[channel][position]);
    };
    return (1.0 - fraction) * sample(first) + fraction * sample(first + 1);
}

bool Chaos::process(std::span<float> left, std::span<float> right) noexcept {
    if (!right.empty() && left.size() != right.size()) {
        return false;
    }
    if (left.empty()) {
        return true;
    }
    if (transitionFrames_ == 0) {
        return false;
    }
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        const double mix = mix_.next();
        if (remainingFrames_ == 0) {
            decideEvent();
        }
        if (eventActive_) {
            const auto cycleAge = eventAge_ % cycleFrames_;
            const double offset =
                std::min(static_cast<double>(sourceFrames_ - 1), cycleAge * eventSpeed_);
            const double readOffset = eventReverse_ ? sourceFrames_ - 1.0 - offset : offset;
            const double fade =
                std::min({1.0, static_cast<double>(cycleAge) / fadeFrames_,
                          static_cast<double>(cycleFrames_ - 1 - cycleAge) / fadeFrames_});
            double wetLeft = readHistory(0, readOffset);
            double wetRight = right.empty() ? 0.0 : readHistory(1, readOffset);
            if (eventDistortion_) {
                wetLeft = std::tanh(wetLeft * eventDrive_);
                wetRight = std::tanh(wetRight * eventDrive_);
            }
            wetLeft *= eventPan_[0];
            wetRight *= eventPan_[1];
            const double weight = mix * fade;
            // The ring always stores original input, never the edited output.
            const auto write = static_cast<std::size_t>(frame_ % history_[0].size());
            history_[0][write] = left[frame];
            history_[1][write] = right.empty() ? 0.0F : right[frame];
            if (weight != 0.0) {
                left[frame] = static_cast<float>((1.0 - weight) * left[frame] + weight * wetLeft);
                if (!right.empty()) {
                    right[frame] =
                        static_cast<float>((1.0 - weight) * right[frame] + weight * wetRight);
                }
            }
        } else {
            const auto write = static_cast<std::size_t>(frame_ % history_[0].size());
            history_[0][write] = left[frame];
            history_[1][write] = right.empty() ? 0.0F : right[frame];
        }
        ++eventAge_;
        --remainingFrames_;
        ++frame_;
    }
    return true;
}

} // namespace ahq::audio
