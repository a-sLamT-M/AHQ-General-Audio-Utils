#include "ahq/audio/orbit.h"

#include "data/kemarHorizontal.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ahq::audio {

bool OrbitParameters::isValid() const noexcept {
    return rateHzRange.contains(rateHz) && bpmRange.contains(bpm) &&
           beatsPerRevolutionRange.contains(beatsPerRevolution) &&
           directionRange.contains(static_cast<int>(direction)) && depthRange.contains(depth) &&
           phaseDegreesRange.contains(phaseDegrees) && mixRange.contains(mix);
}

namespace {

double sinc(double value) noexcept {
    return std::abs(value) < 1.0e-12
               ? 1.0
               : std::sin(std::numbers::pi * value) / (std::numbers::pi * value);
}

} // namespace

double Orbit::signedRate(const OrbitParameters &parameters) noexcept {
    const double rate = parameters.syncToBpm
                            ? parameters.bpm / (60.0 * parameters.beatsPerRevolution)
                            : parameters.rateHz;
    return parameters.direction == OrbitDirection::clockwise ? rate : -rate;
}

double Orbit::wrapDegrees(double angle) noexcept {
    angle = std::fmod(angle, 360.0);
    return angle < 0.0 ? angle + 360.0 : angle;
}

bool Orbit::prepare(double sampleRate) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0) {
        return false;
    }
    sampleRate_ = sampleRate;
    transitionFrames_ = static_cast<std::uint32_t>(std::ceil(sampleRate * 0.005));
    const double ratio = sampleRate / 44100.0;
    const double cutoff = std::min(1.0, ratio);
    latencySamples_ = static_cast<std::uint32_t>(std::ceil(16.0 * std::max(1.0, ratio)));
    taps_ = static_cast<std::size_t>(std::ceil(128.0 * ratio)) + 2 * latencySamples_ + 1;
    for (std::size_t direction = 0; direction < filters_.size(); ++direction) {
        for (std::size_t ear = 0; ear < 2; ++ear) {
            auto &filter = filters_[direction][ear];
            filter.fill(0.0F);
            for (std::size_t tap = 0; tap < taps_; ++tap) {
                const double sourcePosition = (static_cast<double>(tap) - latencySamples_) / ratio;
                const double radius = 16.0 / cutoff;
                const int begin = std::max(0, static_cast<int>(std::ceil(sourcePosition - radius)));
                const int end =
                    std::min(127, static_cast<int>(std::floor(sourcePosition + radius)));
                double coefficient = 0.0;
                for (int source = begin; source <= end; ++source) {
                    const double distance = cutoff * (sourcePosition - source);
                    const double window = 0.5 + 0.5 * std::cos(std::numbers::pi * distance / 16.0);
                    const double sample =
                        data::KEMAR_HORIZONTAL[direction][2 * source + ear] / 32768.0;
                    coefficient += sample * cutoff / ratio * sinc(distance) * window;
                }
                filter[tap] = static_cast<float>(coefficient);
            }
        }
    }
    reset();
    return true;
}

bool Orbit::setParameters(const OrbitParameters &parameters) noexcept {
    if (transitionFrames_ == 0 || !parameters.isValid()) {
        return false;
    }
    if (wrapDegrees(parameters.phaseDegrees) != wrapDegrees(parameters_.phaseDegrees)) {
        phaseTarget_ =
            phaseDegrees_ + std::remainder(parameters.phaseDegrees - phaseDegrees_, 360.0);
        remainingPhaseFrames_ = transitionFrames_;
        phaseStep_ = (phaseTarget_ - phaseDegrees_) / static_cast<double>(transitionFrames_);
    }
    parameters_ = parameters;
    rate_.setTarget(signedRate(parameters), transitionFrames_);
    depth_.setTarget(parameters.depth, transitionFrames_);
    mix_.setTarget(parameters.mix, transitionFrames_);
    return true;
}

void Orbit::reset() noexcept {
    history_ = {};
    historyPosition_ = 0;
    dryHistory_ = {};
    dryPosition_ = 0;
    orbitDegrees_ = 0.0;
    phaseDegrees_ = wrapDegrees(parameters_.phaseDegrees);
    phaseTarget_ = phaseDegrees_;
    phaseStep_ = 0.0;
    remainingPhaseFrames_ = 0;
    rate_.reset(signedRate(parameters_));
    depth_.reset(parameters_.depth);
    mix_.reset(parameters_.mix);
}

std::uint32_t Orbit::getLatencySamples() const noexcept { return latencySamples_; }

std::array<double, 2> Orbit::convolve(double angle, double depth) noexcept {
    const bool mirrored = angle > 180.0;
    const double position = (mirrored ? 360.0 - angle : angle) / 5.0;
    const auto first = static_cast<std::size_t>(position);
    const std::size_t second = std::min(first + 1, filters_.size() - 1);
    const double fraction = position - static_cast<double>(first);
    std::array<double, 2> output{};
    std::size_t historyIndex = historyPosition_;
    for (std::size_t tap = 0; tap < taps_; ++tap) {
        for (std::size_t ear = 0; ear < 2; ++ear) {
            const auto sourceEar = mirrored ? 1 - ear : ear;
            const double rotating = (1.0 - fraction) * filters_[first][sourceEar][tap] +
                                    fraction * filters_[second][sourceEar][tap];
            const double coefficient = (1.0 - depth) * filters_[0][ear][tap] + depth * rotating;
            output[ear] += coefficient * history_[historyIndex];
        }
        historyIndex = historyIndex == 0 ? MAX_TAPS - 1 : historyIndex - 1;
    }
    return output;
}

bool Orbit::process(std::span<float> left, std::span<float> right) noexcept {
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
        const double rate = rate_.next();
        const double depth = depth_.next();
        const double mix = mix_.next();
        if (remainingPhaseFrames_ != 0) {
            phaseDegrees_ += phaseStep_;
            --remainingPhaseFrames_;
            if (remainingPhaseFrames_ == 0) {
                phaseDegrees_ = wrapDegrees(phaseTarget_);
            }
        }
        history_[historyPosition_] = (static_cast<double>(left[frame]) + right[frame]) * 0.5;
        const auto wet = convolve(wrapDegrees(orbitDegrees_ + phaseDegrees_), depth);
        dryHistory_[0][dryPosition_] = left[frame];
        dryHistory_[1][dryPosition_] = right[frame];
        const std::size_t dryRead = (dryPosition_ + MAX_DELAY - latencySamples_) % MAX_DELAY;
        left[frame] =
            mix == 0.0 ? dryHistory_[0][dryRead]
                       : static_cast<float>((1.0 - mix) * dryHistory_[0][dryRead] + mix * wet[0]);
        right[frame] =
            mix == 0.0 ? dryHistory_[1][dryRead]
                       : static_cast<float>((1.0 - mix) * dryHistory_[1][dryRead] + mix * wet[1]);
        historyPosition_ = (historyPosition_ + 1) % MAX_TAPS;
        dryPosition_ = (dryPosition_ + 1) % MAX_DELAY;
        orbitDegrees_ = wrapDegrees(orbitDegrees_ + rate * 360.0 / sampleRate_);
    }
    return true;
}

} // namespace ahq::audio
