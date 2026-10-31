#include "ahq/audio/elevation.h"
#include "data/sadieElevation.h"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace ahq::audio {
bool ElevationParameters::isValid() const noexcept {
    return elevationRange.contains(elevationDegrees) && outputRange.contains(outputDb) &&
           mixRange.contains(mix);
}
bool Elevation::prepare(double rate) noexcept {
    if (!std::isfinite(rate) || rate < 8000 || rate > 384000)
        return false;
    rate_ = rate;
    rampFrames_ = unsigned(std::ceil(rate * .005));
    const double ratio = rate / 48000, cutoff = std::min(1., ratio);
    const unsigned padding = unsigned(std::ceil(16 * std::max(1., ratio)));
    taps_ = size_t(std::ceil(256 * ratio)) + 2 * padding + 1;
    latency_ = unsigned(std::ceil(104 * ratio)) + padding;
    for (size_t direction = 0; direction < filters_.size(); ++direction)
        for (size_t ear = 0; ear < 2; ++ear) {
            auto &filter = filters_[direction][ear];
            filter.fill(0);
            for (size_t tap = 0; tap < taps_; ++tap) {
                const double center = (double(tap) - padding) / ratio;
                const int first = std::max(0, int(std::ceil(center - 16 / cutoff)));
                const int last = std::min(255, int(std::floor(center + 16 / cutoff)));
                double value = 0;
                for (int n = first; n <= last; ++n) {
                    const double x = cutoff * (center - n);
                    const double sinc = std::abs(x) < 1e-12 ? 1
                                                            : std::sin(std::numbers::pi * x) /
                                                                  (std::numbers::pi * x);
                    const double window = .5 + .5 * std::cos(std::numbers::pi * x / 16);
                    value +=
                        data::SADIE_ELEVATION[direction][ear][n] * sinc * window * cutoff / ratio;
                }
                filter[tap] = float(value);
            }
        }
    reset();
    return true;
}
bool Elevation::setParameters(const ElevationParameters &p) noexcept {
    if (rate_ == 0 || !p.isValid())
        return false;
    parameters_ = p;
    elevation_.setTarget(p.elevationDegrees, rampFrames_);
    gain_.setTarget(std::pow(10., p.outputDb / 20), rampFrames_);
    mix_.setTarget(p.mix, rampFrames_);
    return true;
}
void Elevation::blend(double elevation) noexcept {
    if (elevation == lastElevation_)
        return;
    lastElevation_ = elevation;
    size_t first = 0;
    while (first + 1 < data::SADIE_ELEVATIONS.size() &&
           data::SADIE_ELEVATIONS[first + 1] <= elevation)
        ++first;
    const size_t second = std::min(first + 1, data::SADIE_ELEVATIONS.size() - 1);
    const float fraction =
        first == second ? 0
                        : float((elevation - data::SADIE_ELEVATIONS[first]) /
                                (data::SADIE_ELEVATIONS[second] - data::SADIE_ELEVATIONS[first]));
    for (size_t ear = 0; ear < 2; ++ear)
        for (size_t tap = 0; tap < taps_; ++tap)
            targets_[ear][tap] =
                filters_[first][ear][tap] +
                fraction * (filters_[second][ear][tap] - filters_[first][ear][tap]);
    coefficientsMoving_ = true;
}
void Elevation::reset() noexcept {
    history_.fill(0);
    for (auto &c : dry_)
        c.fill(0);
    position_ = dryPosition_ = controlFrame_ = 0;
    elevation_.reset(parameters_.elevationDegrees);
    gain_.reset(std::pow(10., parameters_.outputDb / 20));
    mix_.reset(parameters_.mix);
    lastElevation_ = 1000;
    blend(parameters_.elevationDegrees);
    coefficients_ = targets_;
    coefficientsMoving_ = false;
}
bool Elevation::process(std::span<float> left, std::span<float> right) noexcept {
    if (left.size() != right.size())
        return false;
    if (left.empty())
        return true;
    if (rate_ == 0)
        return false;
    for (size_t n = 0; n < left.size(); ++n) {
        const double elevation = elevation_.next();
        if (controlFrame_ == 0) {
            if (coefficientsMoving_)
                coefficients_ = targets_;
            coefficientsMoving_ = false;
            blend(elevation);
        }
        history_[position_] = (left[n] + right[n]) * .5F;
        dry_[0][dryPosition_] = left[n];
        dry_[1][dryPosition_] = right[n];
        const size_t dryRead = (dryPosition_ + maxDry - latency_) % maxDry;
        std::array<double, 2> wet{};
        size_t index = position_;
        // Coefficients are static for settled positions; ramps only run during an elevation move.
        for (size_t tap = 0; tap < taps_; ++tap) {
            for (size_t ear = 0; ear < 2; ++ear) {
                const float coefficient =
                    coefficientsMoving_ ? coefficients_[ear][tap] +
                                              float(controlFrame_ + 1) / 32 *
                                                  (targets_[ear][tap] - coefficients_[ear][tap])
                                        : coefficients_[ear][tap];
                wet[ear] += coefficient * history_[index];
            }
            index = index == 0 ? maxTaps - 1 : index - 1;
        }
        const double mix = mix_.next(), gain = gain_.next();
        left[n] = float((dry_[0][dryRead] * (1 - mix) + wet[0] * mix) * gain);
        right[n] = float((dry_[1][dryRead] * (1 - mix) + wet[1] * mix) * gain);
        position_ = (position_ + 1) % maxTaps;
        dryPosition_ = (dryPosition_ + 1) % maxDry;
        controlFrame_ = (controlFrame_ + 1) & 31U;
    }
    return true;
}
} // namespace ahq::audio
