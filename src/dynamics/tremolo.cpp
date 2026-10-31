#include "ahq/audio/tremolo.h"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace ahq::audio {
bool TremoloParameters::isValid() const noexcept {
    return shapeRange.contains(shape) && rateRange.contains(rateHz) && beatsRange.contains(beats) &&
           depthRange.contains(depth) && phaseRange.contains(phaseDegrees);
}
bool Tremolo::prepare(double rate) noexcept {
    if (!std::isfinite(rate) || rate < 8000 || rate > 384000)
        return false;
    rate_ = rate;
    transitionFrames_ = unsigned(std::ceil(rate * .005));
    smoothing_ = -std::expm1(-1 / (.0005 * rate));
    for (size_t i = 0; i < sine_.size(); ++i)
        sine_[i] = float(.5 + .5 * std::cos(2 * std::numbers::pi * double(i) / 1024));
    reset();
    return true;
}
bool Tremolo::setParameters(const TremoloParameters &p) noexcept {
    if (rate_ == 0 || !p.isValid())
        return false;
    parameters_ = p;
    depth_.setTarget(p.depth, transitionFrames_);
    return true;
}
void Tremolo::reset() noexcept {
    phase_ = 0;
    gain_ = 1;
    depth_.reset(parameters_.depth);
}
bool Tremolo::process(std::span<float> left, std::span<float> right, double bpm, double ppq,
                      bool positioned, bool playing) noexcept {
    if ((!right.empty() && right.size() != left.size()) || !std::isfinite(bpm) || bpm < 1 ||
        bpm > 1000 || !std::isfinite(ppq))
        return false;
    if (left.empty())
        return true;
    if (rate_ == 0)
        return false;
    const double step =
        (parameters_.sync ? bpm / (60 * parameters_.beats) : parameters_.rateHz) / rate_;
    if (parameters_.sync && positioned && playing) {
        const double cycle = ppq / parameters_.beats;
        phase_ = cycle - std::floor(cycle);
    }
    for (size_t i = 0; i < left.size(); ++i) {
        double phase = phase_ + parameters_.phaseDegrees / 360;
        phase -= std::floor(phase);
        double wave = 0;
        switch (parameters_.shape) {
        case 0: {
            const double position = phase * 1024;
            const auto j = size_t(position);
            wave = sine_[j] + (sine_[j + 1] - sine_[j]) * (position - double(j));
            break;
        }
        case 1:
            wave = std::abs(2 * phase - 1);
            break;
        case 2:
            wave = phase < .5 ? 1 : 0;
            break;
        case 3:
            wave = phase;
            break;
        }
        const double depth = depth_.next();
        const double target =
            parameters_.sync && positioned && !playing ? 1 : 1 - depth * (1 - wave);
        gain_ += smoothing_ * (target - gain_);
        left[i] *= float(gain_);
        if (!right.empty())
            right[i] *= float(gain_);
        if (!parameters_.sync || !positioned || playing) {
            phase_ += step;
            if (phase_ >= 1)
                phase_ -= std::floor(phase_);
        }
    }
    return true;
}
} // namespace ahq::audio
