#include "ahq/audio/saturator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ahq::audio {
namespace {

// sinc(0.5 * (n-16)) * 0.5, Hamming window; odd taps normalized to sum 0.5,
// center fixed at 0.5, other even taps zero. Exact symmetry gives 16 high-rate samples delay.
constexpr std::array<double, 33> HALF_BAND{
    0.0, -0.001890547042313345, 0.0, 0.003867938184033391,  0.0, -0.008254117892324827,
    0.0, 0.015969656317533235,  0.0, -0.028717095530917582, 0.0, 0.050790255751729817,
    0.0, -0.098154459727262722, 0.0, 0.31638836993952207,   0.5, 0.31638836993952207,
    0.0, -0.098154459727262722, 0.0, 0.050790255751729817,  0.0, -0.028717095530917582,
    0.0, 0.015969656317533235,  0.0, -0.008254117892324827, 0.0, 0.003867938184033391,
    0.0, -0.001890547042313345, 0.0};

bool validParameters(const SaturatorParameters &parameters) noexcept {
    const int model = static_cast<int>(parameters.model);
    return model >= 0 && model < 7 && std::isfinite(parameters.driveDb) &&
           parameters.driveDb >= 0.0F && parameters.driveDb <= 36.0F &&
           std::isfinite(parameters.bias) && parameters.bias >= -1.0F && parameters.bias <= 1.0F &&
           std::isfinite(parameters.tone) && parameters.tone >= -1.0F && parameters.tone <= 1.0F &&
           std::isfinite(parameters.mix) && parameters.mix >= 0.0F && parameters.mix <= 1.0F;
}

} // namespace

double Saturator::FirState::process(double input) noexcept {
    history[position] = input;
    double output = 0.0;
    std::size_t index = position;
    for (std::size_t tap = 0; tap < HALF_BAND.size(); ++tap) {
        if (HALF_BAND[tap] != 0.0) {
            output += HALF_BAND[tap] * history[index];
        }
        index = index == 0 ? history.size() - 1 : index - 1;
    }
    position = (position + 1) % history.size();
    return output;
}

bool Saturator::prepare(double sampleRate, OversamplingFactor factor) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 1.0 || sampleRate > 384000.0 ||
        (factor != OversamplingFactor::x1 && factor != OversamplingFactor::x2 &&
         factor != OversamplingFactor::x4)) {
        return false;
    }
    factor_ = factor;
    latencySamples_ = factor == OversamplingFactor::x1   ? 0
                      : factor == OversamplingFactor::x2 ? 16
                                                         : 24;
    transitionFrames_ = static_cast<std::uint32_t>(std::ceil(sampleRate * 0.005));
    const double highSampleRate = sampleRate * (factor == OversamplingFactor::x1   ? 1.0
                                                : factor == OversamplingFactor::x2 ? 2.0
                                                                                   : 4.0);
    toneCoefficient_ =
        -std::expm1(-2.0 * std::numbers::pi * std::min(2000.0, sampleRate * 0.2) / highSampleRate);
    reset();
    return true;
}

bool Saturator::setParameters(const SaturatorParameters &parameters) noexcept {
    if (transitionFrames_ == 0 || !validParameters(parameters)) {
        return false;
    }
    const bool modelChanged = parameters.model != parameters_.model;
    parameters_ = parameters;
    drive_.setTarget(parameters.driveDb, transitionFrames_);
    bias_.setTarget(parameters.bias, transitionFrames_);
    tone_.setTarget(parameters.tone, transitionFrames_);
    mix_.setTarget(parameters.mix, transitionFrames_);
    if (modelChanged) {
        remainingModelFrames_ = transitionFrames_;
        const auto selected = static_cast<std::size_t>(parameters.model);
        for (std::size_t model = 0; model < weights_.size(); ++model) {
            weightSteps_[model] = ((model == selected ? 1.0 : 0.0) - weights_[model]) /
                                  static_cast<double>(transitionFrames_);
        }
    }
    return true;
}

void Saturator::reset() noexcept {
    channels_ = {};
    drive_.reset(parameters_.driveDb);
    bias_.reset(parameters_.bias);
    tone_.reset(parameters_.tone);
    mix_.reset(parameters_.mix);
    remainingModelFrames_ = 0;
    weightSteps_ = {};
    const auto selected = static_cast<std::size_t>(parameters_.model);
    for (std::size_t model = 0; model < weights_.size(); ++model) {
        weights_[model] = model == selected ? 1.0 : 0.0;
    }
}

std::uint32_t Saturator::getLatencySamples() const noexcept { return latencySamples_; }

double Saturator::shape(double input, SaturatorModel model) noexcept {
    switch (model) {
    case SaturatorModel::softClip:
        return std::tanh(input);
    case SaturatorModel::hardClip:
        return std::clamp(input, -1.0, 1.0);
    case SaturatorModel::tube:
        return input >= 0.0 ? std::tanh(1.5 * input) / 1.5 : std::tanh(0.75 * input) / 0.75;
    case SaturatorModel::tape:
        return input / std::sqrt(1.0 + input * input);
    case SaturatorModel::wavefolder: {
        double phase = std::fmod(input + 1.0, 4.0);
        if (phase < 0.0) {
            phase += 4.0;
        }
        return phase <= 2.0 ? phase - 1.0 : 3.0 - phase;
    }
    case SaturatorModel::rectifier:
        return std::min(std::abs(input), 1.0);
    case SaturatorModel::bitcrusher:
        return std::round(std::clamp(input, -1.0, 1.0) * 127.0) / 127.0;
    }
    return 0.0;
}

double Saturator::shapeAndTone(double input, ChannelState &channel, double drive, double bias,
                               double tone) noexcept {
    double shaped = 0.0;
    for (std::size_t model = 0; model < weights_.size(); ++model) {
        if (weights_[model] != 0.0) {
            const auto selected = static_cast<SaturatorModel>(model);
            shaped +=
                weights_[model] * (shape(input * drive + bias, selected) - shape(bias, selected));
        }
    }
    channel.toneLow += toneCoefficient_ * (shaped - channel.toneLow);
    return tone < 0.0 ? shaped + (-tone) * (channel.toneLow - shaped)
                      : shaped + tone * (shaped - channel.toneLow);
}

double Saturator::processWet(double input, ChannelState &channel, double drive, double bias,
                             double tone) noexcept {
    if (factor_ == OversamplingFactor::x1) {
        return shapeAndTone(input, channel, drive, bias, tone);
    }
    double output = 0.0;
    for (std::size_t firstPhase = 0; firstPhase < 2; ++firstPhase) {
        const double upFirst = 2.0 * channel.upFirst.process(firstPhase == 0 ? input : 0.0);
        double intermediate;
        if (factor_ == OversamplingFactor::x4) {
            intermediate = 0.0;
            for (std::size_t secondPhase = 0; secondPhase < 2; ++secondPhase) {
                const double upSecond =
                    2.0 * channel.upSecond.process(secondPhase == 0 ? upFirst : 0.0);
                const double shaped = shapeAndTone(upSecond, channel, drive, bias, tone);
                const double downSecond = channel.downSecond.process(shaped);
                if (secondPhase == 0) {
                    intermediate = downSecond;
                }
            }
        } else {
            intermediate = shapeAndTone(upFirst, channel, drive, bias, tone);
        }
        const double downFirst = channel.downFirst.process(intermediate);
        if (firstPhase == 0) {
            output = downFirst;
        }
    }
    return output;
}

float Saturator::delayDry(float input, ChannelState &channel) noexcept {
    channel.dryHistory[channel.dryPosition] = input;
    const auto read = (channel.dryPosition + channel.dryHistory.size() - latencySamples_) %
                      channel.dryHistory.size();
    const float delayed = channel.dryHistory[read];
    channel.dryPosition = (channel.dryPosition + 1) % channel.dryHistory.size();
    return delayed;
}

bool Saturator::process(std::span<float> left, std::span<float> right) noexcept {
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
        const double drive = std::pow(10.0, drive_.next() / 20.0);
        const double bias = bias_.next();
        const double tone = tone_.next();
        const double mix = mix_.next();
        if (remainingModelFrames_ != 0) {
            --remainingModelFrames_;
            const auto selected = static_cast<std::size_t>(parameters_.model);
            for (std::size_t model = 0; model < weights_.size(); ++model) {
                weights_[model] = remainingModelFrames_ == 0
                                      ? (model == selected ? 1.0 : 0.0)
                                      : weights_[model] + weightSteps_[model];
            }
        }
        const double wetLeft = processWet(left[frame], channels_[0], drive, bias, tone);
        const float dryLeft = delayDry(left[frame], channels_[0]);
        left[frame] =
            mix == 0.0 ? dryLeft : static_cast<float>((1.0 - mix) * dryLeft + mix * wetLeft);
        if (!right.empty()) {
            const double wetRight = processWet(right[frame], channels_[1], drive, bias, tone);
            const float dryRight = delayDry(right[frame], channels_[1]);
            right[frame] =
                mix == 0.0 ? dryRight : static_cast<float>((1.0 - mix) * dryRight + mix * wetRight);
        }
    }
    return true;
}

} // namespace ahq::audio
