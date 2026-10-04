#include "ahq/audio/quickFilter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ahq::audio {
namespace {

bool validParameters(const QuickFilterParameters &parameters) noexcept {
    return (parameters.mode == FilterMode::lowPass || parameters.mode == FilterMode::highPass) &&
           (parameters.slope == FilterSlope::db12 || parameters.slope == FilterSlope::db24 ||
            parameters.slope == FilterSlope::db36 || parameters.slope == FilterSlope::db48) &&
           std::isfinite(parameters.frequencyHz) && parameters.frequencyHz >= 20.0F &&
           parameters.frequencyHz <= 20000.0F && std::isfinite(parameters.q) &&
           parameters.q >= 0.1F && parameters.q <= 10.0F && std::isfinite(parameters.mix) &&
           parameters.mix >= 0.0F && parameters.mix <= 1.0F;
}

std::size_t selectedOutput(const QuickFilterParameters &parameters) noexcept {
    return (parameters.mode == FilterMode::highPass ? 4U : 0U) +
           static_cast<std::size_t>(parameters.slope);
}

} // namespace

bool QuickFilter::prepare(double sampleRate) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 1.0 || sampleRate > 384000.0) {
        return false;
    }
    sampleRate_ = sampleRate;
    transitionFrames_ = static_cast<std::uint32_t>(std::ceil(sampleRate * 0.005));
    reset();
    return true;
}

bool QuickFilter::setParameters(const QuickFilterParameters &parameters) noexcept {
    if (transitionFrames_ == 0 || !validParameters(parameters)) {
        return false;
    }
    const bool topologyChanged =
        parameters.mode != parameters_.mode || parameters.slope != parameters_.slope;
    parameters_ = parameters;
    frequency_.setTarget(std::min<double>(parameters.frequencyHz, sampleRate_ * 0.45),
                         transitionFrames_);
    q_.setTarget(parameters.q, transitionFrames_);
    mix_.setTarget(parameters.mix, transitionFrames_);
    if (topologyChanged) {
        const std::size_t selected = selectedOutput(parameters);
        remainingTopologyFrames_ = transitionFrames_;
        for (std::size_t output = 0; output < weights_.size(); ++output) {
            const double targetWeight = output == selected ? 1.0 : 0.0;
            weightSteps_[output] =
                (targetWeight - weights_[output]) / static_cast<double>(transitionFrames_);
        }
    }
    return true;
}

void QuickFilter::reset() noexcept {
    states_ = {};
    frequency_.reset(std::min<double>(parameters_.frequencyHz, sampleRate_ * 0.45));
    q_.reset(parameters_.q);
    mix_.reset(parameters_.mix);
    const std::size_t selected = selectedOutput(parameters_);
    for (std::size_t output = 0; output < weights_.size(); ++output) {
        weights_[output] = output == selected ? 1.0 : 0.0;
    }
    weightSteps_ = {};
    remainingTopologyFrames_ = 0;
}

QuickFilter::Coefficients QuickFilter::calculateCoefficients(double tangent, double q) noexcept {
    const double damping = 1.0 / q;
    const double first = 1.0 / (1.0 + tangent * (tangent + damping));
    return {first, tangent * first, tangent * tangent * first, damping};
}

double QuickFilter::processChannel(double input, std::size_t channel,
                                   const std::array<Coefficients, 2> &coefficients,
                                   const std::array<double, 8> &weights) noexcept {
    double wet = 0.0;
    // Both cascades stay warm. Each stage supplies one slope candidate; selecting a new
    // candidate changes only continuous output weights, including mid-transition retargets.
    for (std::size_t mode = 0; mode < 2; ++mode) {
        double sectionInput = input;
        for (std::size_t section = 0; section < 4; ++section) {
            auto &state = states_[channel][mode][section];
            const auto &coefficient = coefficients[section == 0 ? 0 : 1];
            const double difference = sectionInput - state.low;
            const double band = coefficient.first * state.band + coefficient.second * difference;
            const double low =
                state.low + coefficient.second * state.band + coefficient.third * difference;
            state.band = 2.0 * band - state.band;
            state.low = 2.0 * low - state.low;
            sectionInput = mode == 0 ? low : sectionInput - coefficient.damping * band - low;
            wet += weights[mode * 4 + section] * sectionInput;
        }
    }
    return wet;
}

bool QuickFilter::process(std::span<float> left, std::span<float> right) noexcept {
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
        const double tangent = std::tan(std::numbers::pi * frequency_.next() / sampleRate_);
        const std::array<Coefficients, 2> coefficients{
            calculateCoefficients(tangent, q_.next()),
            calculateCoefficients(tangent, std::numbers::sqrt2 / 2.0)};
        if (remainingTopologyFrames_ != 0) {
            --remainingTopologyFrames_;
            const std::size_t selected = selectedOutput(parameters_);
            for (std::size_t output = 0; output < weights_.size(); ++output) {
                weights_[output] = remainingTopologyFrames_ == 0
                                       ? (output == selected ? 1.0 : 0.0)
                                       : weights_[output] + weightSteps_[output];
            }
        }
        const double mix = mix_.next();
        const double dryLeft = left[frame];
        const double wetLeft = processChannel(dryLeft, 0, coefficients, weights_);
        left[frame] = static_cast<float>((1.0 - mix) * dryLeft + mix * wetLeft);
        if (!right.empty()) {
            const double dryRight = right[frame];
            const double wetRight = processChannel(dryRight, 1, coefficients, weights_);
            right[frame] = static_cast<float>((1.0 - mix) * dryRight + mix * wetRight);
        }
    }
    return true;
}

} // namespace ahq::audio
