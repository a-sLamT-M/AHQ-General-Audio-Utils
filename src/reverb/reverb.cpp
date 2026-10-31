#include "ahq/audio/reverb.h"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace ahq::audio {
bool ReverbParameters::isValid() const noexcept {
    return decayRange.contains(decaySeconds) && dampingRange.contains(dampingHz) &&
           preDelayRange.contains(preDelayMs) && widthRange.contains(width) &&
           mixRange.contains(mix);
}
bool Reverb::prepare(double rate) noexcept {
    if (!std::isfinite(rate) || rate < 8000 || rate > 384000)
        return false;
    constexpr std::array<double, 8> seconds{.0297, .0371, .0411, .0437, .0531, .0613, .0719, .0793};
    std::array<std::vector<float>, 8> delays;
    std::vector<float> pre;
    try {
        for (size_t i = 0; i < 8; ++i)
            delays[i].resize(size_t(std::ceil(seconds[i] * rate)) | 1U);
        pre.resize(size_t(std::ceil(rate * .25)) + 2);
    } catch (...) {
        return false;
    }
    delays_ = std::move(delays);
    preDelay_ = std::move(pre);
    rate_ = rate;
    rampFrames_ = unsigned(std::ceil(rate * .005));
    update();
    reset();
    return true;
}
bool Reverb::setParameters(const ReverbParameters &p) noexcept {
    if (rate_ == 0 || !p.isValid())
        return false;
    if (parameters_ != p) {
        parameters_ = p;
        update();
    }
    return true;
}
void Reverb::update() noexcept {
    for (size_t i = 0; i < 8; ++i)
        feedback_[i] = std::exp(-std::log(1000.) * double(delays_[i].size()) /
                                (rate_ * parameters_.decaySeconds));
    damping_ =
        -std::expm1(-2 * std::numbers::pi * std::min(parameters_.dampingHz, rate_ * .45) / rate_);
    mix_.setTarget(parameters_.mix, rampFrames_);
    width_.setTarget(parameters_.width, rampFrames_);
    preFrames_.setTarget(parameters_.preDelayMs * .001 * rate_, rampFrames_);
}
void Reverb::reset() noexcept {
    for (auto &line : delays_)
        std::fill(line.begin(), line.end(), 0);
    std::fill(preDelay_.begin(), preDelay_.end(), 0);
    positions_.fill(0);
    low_.fill(0);
    prePosition_ = 0;
    mix_.reset(parameters_.mix);
    width_.reset(parameters_.width);
    preFrames_.reset(parameters_.preDelayMs * .001 * rate_);
}
bool Reverb::process(std::span<float> left, std::span<float> right) noexcept {
    if (!right.empty() && right.size() != left.size())
        return false;
    if (left.empty())
        return true;
    if (rate_ == 0)
        return false;
    for (size_t n = 0; n < left.size(); ++n) {
        const double dryL = left[n], dryR = right.empty() ? dryL : right[n];
        preDelay_[prePosition_] = float((dryL + dryR) * .5);
        double read = double(prePosition_) - preFrames_.next();
        if (read < 0)
            read += double(preDelay_.size());
        const auto index = size_t(read);
        const double input =
            preDelay_[index] +
            (preDelay_[(index + 1) % preDelay_.size()] - preDelay_[index]) * (read - double(index));
        if (++prePosition_ == preDelay_.size())
            prePosition_ = 0;
        std::array<double, 8> values{};
        double wetL = 0, wetR = 0;
        for (size_t i = 0; i < 8; ++i) {
            const double value = delays_[i][positions_[i]];
            low_[i] += damping_ * (value - low_[i]);
            values[i] = low_[i] * feedback_[i];
            wetL += value * ((i & 1) ? -.25 : .25);
            wetR += value * ((i & 2) ? -.25 : .25);
        }
        for (size_t stride = 1; stride < 8; stride *= 2)
            for (size_t i = 0; i < 8; i += stride * 2)
                for (size_t j = 0; j < stride; ++j) {
                    const double a = values[i + j], b = values[i + j + stride];
                    values[i + j] = a + b;
                    values[i + j + stride] = a - b;
                }
        for (size_t i = 0; i < 8; ++i) {
            delays_[i][positions_[i]] =
                float(values[i] * .3535533905932738 + input * ((i & 4) ? -.25 : .25));
            if (++positions_[i] == delays_[i].size())
                positions_[i] = 0;
        }
        const double mid = (wetL + wetR) * .5, side = (wetL - wetR) * .5 * width_.next();
        const double mix = mix_.next();
        left[n] = float(dryL * (1 - mix) + (mid + side) * mix);
        if (!right.empty())
            right[n] = float(dryR * (1 - mix) + (mid - side) * mix);
    }
    return true;
}
} // namespace ahq::audio
