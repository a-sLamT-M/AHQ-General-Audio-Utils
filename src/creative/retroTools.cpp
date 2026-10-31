#include "ahq/audio/retroTools.h"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace ahq::audio {
bool RetroToolsParameters::isValid() const noexcept {
    return ensembleRange.contains(ensemble) && rateRange.contains(rateHz) &&
           wowRange.contains(wow) && flutterRange.contains(flutter) && driveRange.contains(drive) &&
           toneRange.contains(toneHz) && bitsRange.contains(bits) && mixRange.contains(mix) &&
           faultRange.contains(fault);
}
bool RetroTools::prepare(double rate) noexcept {
    if (!std::isfinite(rate) || rate < 8000 || rate > 384000)
        return false;
    std::array<std::vector<float>, 2> history;
    try {
        for (auto &channel : history)
            channel.resize(size_t(std::ceil(rate * .024)) + 2);
    } catch (...) {
        return false;
    }
    history_ = std::move(history);
    rate_ = rate;
    latency_ = unsigned(std::ceil(rate * .012));
    rampFrames_ = unsigned(std::ceil(rate * .005));
    faultAttack_ = -std::expm1(-1 / (.002 * rate));
    faultRelease_ = -std::expm1(-1 / (.090 * rate));
    for (size_t i = 0; i < sine_.size(); ++i)
        sine_[i] = float(std::sin(2 * std::numbers::pi * double(i) / 1024));
    const auto parameters = parameters_;
    parameters_.toneHz = -1;
    (void)setParameters(parameters);
    reset();
    return true;
}
bool RetroTools::setParameters(const RetroToolsParameters &p) noexcept {
    if (rate_ == 0 || !p.isValid())
        return false;
    if (p == parameters_)
        return true;
    parameters_ = p;
    damping_ = -std::expm1(-2 * std::numbers::pi * std::min(p.toneHz, rate_ * .45) / rate_);
    const double values[]{p.ensemble, p.rateHz, p.wow,    p.flutter,
                          p.drive,    p.mix,    damping_, p.fault};
    for (size_t i = 0; i < smooth_.size(); ++i)
        smooth_[i].setTarget(values[i], rampFrames_);
    quantizer_ = p.bits < 24 ? std::exp2(p.bits - 1) : 0;
    return true;
}
void RetroTools::reset() noexcept {
    for (auto &c : history_)
        std::fill(c.begin(), c.end(), 0);
    position_ = 0;
    low_.fill(0);
    chorusPhase_ = wowPhase_ = flutterPhase_ = 0;
    randomState_ = 0x72657472U;
    faultFrames_ = 0;
    faultPhase_ = faultEnvelope_ = 0;
    faultDepth_.reset(0);
    faultSpeed_.reset(30);
    faultJitter_.reset(0);
    const auto &p = parameters_;
    const double values[]{p.ensemble, p.rateHz, p.wow,    p.flutter,
                          p.drive,    p.mix,    damping_, p.fault};
    for (size_t i = 0; i < smooth_.size(); ++i)
        smooth_[i].reset(values[i]);
}
double RetroTools::random() noexcept {
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return double(randomState_) / 4294967296.0;
}
double RetroTools::sine(double phase) const noexcept {
    phase -= std::floor(phase);
    const double index = phase * 1024;
    const size_t i = size_t(index);
    return sine_[i] + (sine_[i + 1] - sine_[i]) * (index - double(i));
}
float RetroTools::read(size_t channel, double delay) const noexcept {
    double index = double(position_) - delay;
    if (index < 0)
        index += double(history_[channel].size());
    const size_t i = size_t(index), next = (i + 1) % history_[channel].size();
    return float(history_[channel][i] +
                 (index - double(i)) * (history_[channel][next] - history_[channel][i]));
}
bool RetroTools::process(std::span<float> left, std::span<float> right) noexcept {
    if (!right.empty() && right.size() != left.size())
        return false;
    if (left.empty())
        return true;
    if (rate_ == 0)
        return false;
    for (size_t n = 0; n < left.size(); ++n) {
        history_[0][position_] = left[n];
        history_[1][position_] = right.empty() ? left[n] : right[n];
        const double ensemble = smooth_[0].next(), speed = smooth_[1].next(),
                     wow = smooth_[2].next(), flutter = smooth_[3].next(),
                     drive = smooth_[4].next(), mix = smooth_[5].next(),
                     damping = smooth_[6].next(), fault = smooth_[7].next();
        // Free-running, stereo-linked fault bursts; never reseed at host block boundaries.
        if (faultFrames_ == 0) {
            faultFrames_ = unsigned(rate_ * (.025 + .155 * random()));
            faultDepth_.setTarget(.15 + .85 * random(), rampFrames_);
            faultSpeed_.setTarget(18 + 47 * random(), rampFrames_);
            faultJitter_.setTarget(2 * random() - 1, faultFrames_);
        }
        --faultFrames_;
        const double depth = faultDepth_.next(), faultSpeed = faultSpeed_.next(),
                     jitter = faultJitter_.next();
        double level = std::abs(read(0, latency_));
        if (!right.empty())
            level = std::max(level, double(std::abs(read(1, latency_))));
        faultEnvelope_ +=
            (level > faultEnvelope_ ? faultAttack_ : faultRelease_) * (level - faultEnvelope_);
        const double stress = fault * faultEnvelope_ / (faultEnvelope_ + .025);
        const double flutterWave = sine(faultPhase_);
        const double collapse = stress * depth * (.5 + .5 * flutterWave);
        const double motion =
            rate_ * (.003 * wow * sine(wowPhase_) + .0003 * flutter * sine(flutterPhase_) +
                     .00008 * stress * depth * (flutterWave + .35 * jitter));
        for (size_t c = 0; c < (right.empty() ? 1U : 2U); ++c) {
            const double dry = read(c, latency_), center = read(c, double(latency_) + motion);
            double chorus = 0;
            for (int voice = 0; voice < 3; ++voice)
                chorus += read(c, double(latency_) + motion +
                                      rate_ * .003 *
                                          sine(chorusPhase_ + double(voice) / 3 + double(c) * .17));
            double wet = center * (1 - ensemble) + chorus * (ensemble / 3);
            if (drive > 0) {
                const double pushed = wet * (1 + drive * 8);
                const double shaped = pushed / (1 + std::abs(pushed));
                wet += drive * (shaped - wet);
            }
            if (stress > 0) {
                // Lower the clipping threshold instead of boosting the signal. Both shaping
                // and the irregular cone-like collapse only reduce the wet sample magnitude.
                const double threshold = std::max(.0001, faultEnvelope_ * (1 - .88 * collapse));
                const double broken = wet / (1 + std::abs(wet) / threshold);
                wet += stress * (broken - wet);
                wet *= 1 - .85 * collapse;
            }
            low_[c] += damping * (wet - low_[c]);
            wet = low_[c];
            if (quantizer_ > 0)
                wet = std::round(wet * quantizer_) / quantizer_;
            const float output = float(dry + mix * (wet - dry));
            if (c == 0)
                left[n] = output;
            else
                right[n] = output;
        }
        chorusPhase_ += speed / rate_;
        chorusPhase_ -= std::floor(chorusPhase_);
        wowPhase_ += .47 / rate_;
        wowPhase_ -= std::floor(wowPhase_);
        flutterPhase_ += 7.3 / rate_;
        flutterPhase_ -= std::floor(flutterPhase_);
        faultPhase_ += faultSpeed / rate_;
        faultPhase_ -= std::floor(faultPhase_);
        if (++position_ == history_[0].size())
            position_ = 0;
    }
    return true;
}
} // namespace ahq::audio
