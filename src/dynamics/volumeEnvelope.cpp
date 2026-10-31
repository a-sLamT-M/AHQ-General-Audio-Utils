#include "ahq/audio/volumeEnvelope.h"
#include <algorithm>
#include <cmath>
namespace ahq::audio {
bool VolumeEnvelopeParameters::isValid() const noexcept {
    if (!std::isfinite(beats) || beats < .0625 || beats > 16 || !std::isfinite(thresholdDb) ||
        thresholdDb < -80 || thresholdDb > 0 || !std::isfinite(mix) || mix < 0 || mix > 1 ||
        times.front() != 0 || times.back() != 1)
        return false;
    for (size_t i = 0; i < times.size(); ++i)
        if (!std::isfinite(times[i]) || !std::isfinite(levels[i]) || levels[i] < 0 ||
            levels[i] > 1 || (i && times[i] <= times[i - 1]))
            return false;
    return true;
}
bool VolumeEnvelope::prepare(double rate) noexcept {
    if (!std::isfinite(rate) || rate < 8000 || rate > 384000)
        return false;
    rate_ = rate;
    detectorDecay_ = std::exp(-1 / (.01 * rate));
    smoothing_ = -std::expm1(-1 / (.0003 * rate));
    reset();
    return true;
}
bool VolumeEnvelope::setParameters(const VolumeEnvelopeParameters &p) noexcept {
    if (rate_ == 0 || !p.isValid())
        return false;
    if (p.thresholdDb != parameters_.thresholdDb)
        threshold_ = std::pow(10.0, p.thresholdDb / 20);
    const bool modeChanged = p.audioTrigger != parameters_.audioTrigger;
    parameters_ = p;
    if (modeChanged)
        reset();
    return true;
}
void VolumeEnvelope::reset() noexcept {
    phase_ = parameters_.audioTrigger ? 1 : 0;
    gain_ = 1;
    detector_ = 0;
    active_ = false;
    armed_ = true;
}
double VolumeEnvelope::valueAt(double phase) const noexcept {
    phase = std::clamp(phase, 0.0, 1.0);
    for (size_t i = 1; i < parameters_.times.size(); ++i)
        if (phase <= parameters_.times[i]) {
            const double t = (phase - parameters_.times[i - 1]) /
                             (parameters_.times[i] - parameters_.times[i - 1]);
            // Cubic smoothstep keeps each editable node continuous.
            const double curved = t * t * (3 - 2 * t);
            return parameters_.levels[i - 1] +
                   curved * (parameters_.levels[i] - parameters_.levels[i - 1]);
        }
    return parameters_.levels.back();
}
bool VolumeEnvelope::process(std::span<float> left, std::span<float> right,
                             std::span<const float> triggerLeft,
                             std::span<const float> triggerRight, double bpm, double ppq,
                             bool positioned, bool playing) noexcept {
    if ((!right.empty() && right.size() != left.size()) ||
        (!triggerLeft.empty() && triggerLeft.size() != left.size()) ||
        (!triggerRight.empty() && triggerRight.size() != left.size()) || !std::isfinite(bpm) ||
        bpm < 1 || bpm > 1000 || !std::isfinite(ppq))
        return false;
    if (left.empty())
        return true;
    if (rate_ == 0)
        return false;
    const double step = bpm / (60 * rate_ * parameters_.beats);
    if (!parameters_.audioTrigger && positioned && playing) {
        const double cycles = ppq / parameters_.beats;
        phase_ = cycles - std::floor(cycles);
    }
    for (size_t i = 0; i < left.size(); ++i) {
        if (parameters_.audioTrigger) {
            const double l = triggerLeft.empty() ? 0 : std::abs(double(triggerLeft[i]));
            const double r = triggerRight.empty() ? 0 : std::abs(double(triggerRight[i]));
            detector_ = std::max(std::max(l, r), detector_ * detectorDecay_);
            if (detector_ < threshold_ * .5)
                armed_ = true;
            if (armed_ && detector_ >= threshold_) {
                active_ = true;
                armed_ = false;
                phase_ = 0;
            }
        } else
            active_ = !positioned || playing;
        const double envelope = active_ ? valueAt(phase_) : 1;
        const double target = 1 + parameters_.mix * (envelope - 1);
        gain_ += smoothing_ * (target - gain_);
        left[i] *= float(gain_);
        if (!right.empty())
            right[i] *= float(gain_);
        if (active_) {
            phase_ += step;
            if (phase_ >= 1) {
                if (parameters_.audioTrigger) {
                    phase_ = 1;
                    active_ = false;
                } else
                    phase_ -= std::floor(phase_);
            }
        }
    }
    return true;
}
} // namespace ahq::audio
