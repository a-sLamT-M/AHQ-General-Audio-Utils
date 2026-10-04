#include "ahq/audio/grainDelay.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <numbers>

namespace ahq::audio {
namespace {

bool validParameters(const GrainDelayParameters &parameters) noexcept {
    return std::isfinite(parameters.grainMs) && parameters.grainMs >= 20.0F &&
           parameters.grainMs <= 500.0F && std::isfinite(parameters.delayMs) &&
           parameters.delayMs >= 10.0F && parameters.delayMs <= 2000.0F &&
           std::isfinite(parameters.pitchSemitones) && parameters.pitchSemitones >= -24.0F &&
           parameters.pitchSemitones <= 24.0F && std::isfinite(parameters.scatter) &&
           parameters.scatter >= 0.0F && parameters.scatter <= 1.0F &&
           std::isfinite(parameters.densityHz) && parameters.densityHz >= 1.0F &&
           parameters.densityHz <= 80.0F && std::isfinite(parameters.feedback) &&
           parameters.feedback >= 0.0F && parameters.feedback <= 0.95F &&
           std::isfinite(parameters.randomness) && parameters.randomness >= 0.0F &&
           parameters.randomness <= 1.0F && std::isfinite(parameters.mix) &&
           parameters.mix >= 0.0F && parameters.mix <= 1.0F;
}

} // namespace

bool GrainDelay::prepare(double sampleRate) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0) {
        return false;
    }
    std::array<std::vector<float>, 2> history;
    try {
        const auto frames = static_cast<std::size_t>(std::ceil(sampleRate * 4.0)) + 8;
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

bool GrainDelay::setParameters(const GrainDelayParameters &parameters) noexcept {
    if (transitionFrames_ == 0 || !validParameters(parameters)) {
        return false;
    }
    if (parameters.seed != parameters_.seed) {
        randomState_ = parameters.seed;
    }
    parameters_ = parameters;
    feedback_.setTarget(parameters.feedback, transitionFrames_);
    mix_.setTarget(parameters.mix, transitionFrames_);
    return true;
}

void GrainDelay::reset() noexcept {
    for (auto &channel : history_) {
        std::fill(channel.begin(), channel.end(), 0.0F);
    }
    grains_ = {};
    frame_ = 0;
    framesUntilSpawn_ = 0.0;
    randomState_ = parameters_.seed;
    previousWet_ = {};
    feedback_.reset(parameters_.feedback);
    mix_.reset(parameters_.mix);
}

std::uint32_t GrainDelay::getActiveGrainCount() const noexcept {
    std::uint32_t count = 0;
    for (const auto &grain : grains_) {
        if (grain.duration != 0) {
            ++count;
        }
    }
    return count;
}

double GrainDelay::randomSigned() noexcept {
    // Defined unsigned overflow; seed zero is valid. Exactly one reproducible draw per call.
    randomState_ = randomState_ * 1664525U + 1013904223U;
    return static_cast<double>(randomState_) / 2147483648.0 - 1.0;
}

void GrainDelay::spawnGrain() noexcept {
    for (auto &grain : grains_) {
        if (grain.duration != 0) {
            continue;
        }
        const double scatter = randomSigned() * parameters_.scatter;
        const double pitch =
            std::clamp(parameters_.pitchSemitones + randomSigned() * 12.0 * parameters_.randomness,
                       -24.0, 24.0);
        const double pan = randomSigned() * parameters_.randomness;
        grain.duration =
            static_cast<std::uint32_t>(std::ceil(parameters_.grainMs * 0.001 * sampleRate_));
        grain.age = 0;
        grain.speed = std::exp2(pitch / 12.0);
        grain.leftGain = std::min(1.0, std::sqrt(1.0 - pan));
        grain.rightGain = std::min(1.0, std::sqrt(1.0 + pan));
        const double requestedDelay =
            parameters_.delayMs * 0.001 * sampleRate_ * (1.0 + 0.5 * scatter);
        const double minimumDelay = (grain.duration - 1.0) * std::max(grain.speed - 1.0, 0.0) + 2.0;
        const double effectiveDelay = std::max(requestedDelay, minimumDelay);
        grain.readPosition = static_cast<double>(frame_) - effectiveDelay;
        return;
    }
}

float GrainDelay::readHistory(std::size_t channel, double position) const noexcept {
    const double first = std::floor(position);
    const double fraction = position - first;
    // Initial negative time is silence. Defensive limits also reject future/overwritten history.
    const double oldest =
        static_cast<double>(frame_) - static_cast<double>(history_[channel].size()) + 1.0;
    const auto readSample = [&](double index) {
        if (index < 0.0 || index < oldest || index > static_cast<double>(frame_)) {
            return 0.0;
        }
        const auto wrapped =
            static_cast<std::size_t>(static_cast<std::uint64_t>(index) % history_[channel].size());
        return static_cast<double>(history_[channel][wrapped]);
    };
    return static_cast<float>((1.0 - fraction) * readSample(first) +
                              fraction * readSample(first + 1.0));
}

bool GrainDelay::process(std::span<float> left, std::span<float> right) noexcept {
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
        const double feedback = feedback_.next();
        const double mix = mix_.next();
        const auto write = static_cast<std::size_t>(frame_ % history_[0].size());
        history_[0][write] =
            static_cast<float>(left[frame] + feedback * std::tanh(previousWet_[0]));
        history_[1][write] = static_cast<float>((right.empty() ? 0.0 : right[frame]) +
                                                feedback * std::tanh(previousWet_[1]));
        if (framesUntilSpawn_ <= 0.0) {
            spawnGrain();
            framesUntilSpawn_ += sampleRate_ / parameters_.densityHz;
        }
        framesUntilSpawn_ -= 1.0;
        std::array<double, 2> wet{};
        double windowSum = 0.0;
        for (auto &grain : grains_) {
            if (grain.duration == 0) {
                continue;
            }
            const double window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * grain.age /
                                                       static_cast<double>(grain.duration - 1));
            wet[0] += window * grain.leftGain * readHistory(0, grain.readPosition);
            wet[1] += window * grain.rightGain * readHistory(1, grain.readPosition);
            windowSum += window;
            grain.readPosition += grain.speed;
            ++grain.age;
            if (grain.age == grain.duration) {
                grain.duration = 0;
            }
        }
        const double divisor = std::max(1.0, windowSum);
        previousWet_[0] = wet[0] / divisor;
        previousWet_[1] = wet[1] / divisor;
        if (mix != 0.0) {
            left[frame] = static_cast<float>((1.0 - mix) * left[frame] + mix * previousWet_[0]);
            if (!right.empty()) {
                right[frame] =
                    static_cast<float>((1.0 - mix) * right[frame] + mix * previousWet_[1]);
            }
        }
        ++frame_;
    }
    return true;
}

} // namespace ahq::audio
