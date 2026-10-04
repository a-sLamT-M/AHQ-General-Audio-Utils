#include "ahq/audio/trueLimiter.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace ahq::audio {
namespace {

double fromDb(double db) noexcept { return std::pow(10.0, db / 20.0); }

bool validParameters(const TrueLimiterParameters &p) noexcept {
    return std::isfinite(p.ceilingDb) && p.ceilingDb >= -24.0F && p.ceilingDb <= 0.0F &&
           std::isfinite(p.releaseMs) && p.releaseMs >= 10.0F && p.releaseMs <= 1000.0F &&
           std::isfinite(p.inputGainDb) && p.inputGainDb >= -24.0F && p.inputGainDb <= 24.0F &&
           std::isfinite(p.mix) && p.mix >= 0.0F && p.mix <= 1.0F && std::isfinite(p.targetLufs) &&
           p.targetLufs >= -36.0F && p.targetLufs <= -5.0F;
}

LoudnessReading measure(std::span<const float> left, std::span<const float> right, double rate) {
    LoudnessMeter meter;
    if (!meter.prepare(rate, right.empty() ? 1 : 2)) {
        return {};
    }
    IntegratedLoudness integrated;
    for (std::size_t i = 0; i < left.size(); ++i) {
        LoudnessBlock block;
        if (!meter.processSample(left[i], right.empty() ? 0.0F : right[i], block)) {
            return {};
        }
        if (block.ready) {
            (void)integrated.addBlock(block.energy);
        }
    }
    return integrated.reading();
}

} // namespace

bool TrueLimiter::prepare(double rate, unsigned channels, double lookaheadMs,
                          unsigned oversampling) noexcept {
    if (!std::isfinite(rate) || rate < 32000.0 || rate > 384000.0 ||
        (channels != 1 && channels != 2) || !std::isfinite(lookaheadMs) || lookaheadMs < 0.0 ||
        lookaheadMs > 20.0 || (oversampling != 4 && oversampling != 8 && oversampling != 16)) {
        return false;
    }
    const auto latency =
        static_cast<std::size_t>(std::floor(rate * lookaheadMs / 1000.0 + 0.5)) + 24;
    std::array<std::vector<double>, 2> delay;
    std::vector<double> peakDelay;
    LoudnessMeter meter;
    try {
        for (unsigned channel = 0; channel < channels; ++channel) {
            delay[channel].resize(latency, 0.0);
        }
        peakDelay.resize(latency - 24 + 1, 0.0);
    } catch (...) {
        return false;
    }
    if (!meter.prepare(rate, channels)) {
        return false;
    }
    for (unsigned phase = 0; phase < 16; ++phase) {
        double sum = 0.0;
        for (unsigned tap = 0; tap < 24; ++tap) {
            const double x = tap - 11.0 - static_cast<double>(phase) / 16;
            const double sinc = std::abs(x) < 1.0e-12
                                    ? 1.0
                                    : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
            const double window =
                std::abs(x) < 12.0 ? 0.5 + 0.5 * std::cos(std::numbers::pi * x / 12.0) : 0.0;
            coefficients_[phase][tap] = sinc * window;
            // Exactly zero integer sinc sidelobes avoid ill-conditioned reciprocals.
            if (std::abs(coefficients_[phase][tap]) < 1.0e-14) {
                coefficients_[phase][tap] = 0.0;
            }
            sum += coefficients_[phase][tap];
        }
        for (unsigned tap = 0; tap < 24; ++tap) {
            coefficients_[phase][tap] /= sum;
            reciprocal_[phase][tap] =
                coefficients_[phase][tap] == 0.0 ? 0.0 : 1.0 / coefficients_[phase][tap];
        }
    }
    delay_.swap(delay);
    peakDelay_.swap(peakDelay);
    loudness_ = std::move(meter);
    sampleRate_ = rate;
    channels_ = channels;
    oversampling_ = oversampling;
    maximumLookaheadMs_ = lookaheadMs;
    maximumLookaheadFrames_ = effectiveLookaheadFrames_ = latency - 24;
    smoothingFrames_ = static_cast<std::uint32_t>(std::ceil(rate * 0.005));
    liveCoefficient_ = std::exp(-1.0 / (rate * 0.25));
    reset();
    return true;
}

bool TrueLimiter::setParameters(const TrueLimiterParameters &parameters) noexcept {
    if (channels_ == 0 || !validParameters(parameters)) {
        return false;
    }
    parameters_ = parameters;
    ceiling_ = fromDb(parameters.ceilingDb - (oversampling_ == 4 ? 1.75 : 1.6)) * (1.0 - 1.0e-6);
    releaseCoefficient_ = std::exp(-1.0 / (sampleRate_ * parameters.releaseMs / 1000.0));
    inputGain_.setTarget(fromDb(parameters.inputGainDb), smoothingFrames_);
    mix_.setTarget(parameters.mix, smoothingFrames_);
    if (!parameters.liveLoudness) {
        liveTarget_ = 1.0;
    }
    return true;
}

bool TrueLimiter::setLookaheadMs(double lookaheadMs) noexcept {
    if (channels_ == 0 || !std::isfinite(lookaheadMs) || lookaheadMs < 0.0 ||
        lookaheadMs > maximumLookaheadMs_) {
        return false;
    }
    effectiveLookaheadFrames_ =
        static_cast<std::size_t>(std::floor(sampleRate_ * lookaheadMs / 1000.0 + 0.5));
    return true;
}

bool TrueLimiter::setOversamplingFactor(unsigned factor) noexcept {
    if (channels_ == 0 || (factor != 4 && factor != 8 && factor != 16)) {
        return false;
    }
    oversampling_ = factor;
    ceiling_ = fromDb(parameters_.ceilingDb - (oversampling_ == 4 ? 1.75 : 1.6)) * (1.0 - 1.0e-6);
    return true;
}

void TrueLimiter::reset() noexcept {
    for (auto &buffer : delay_) {
        std::fill(buffer.begin(), buffer.end(), 0.0);
    }
    detectorHistory_ = {};
    std::fill(peakDelay_.begin(), peakDelay_.end(), 0.0);
    partials_ = {};
    delayPosition_ = 0;
    peakPosition_ = 0;
    holdFrames_ = 0;
    peakEnvelope_ = 0.0;
    liveGain_ = liveTarget_ = 1.0;
    lastReduction_ = 1.0;
    protectionEngaged_ = false;
    loudness_.reset();
    inputGain_.reset(fromDb(parameters_.inputGainDb));
    mix_.reset(parameters_.mix);
    ceiling_ = fromDb(parameters_.ceilingDb - (oversampling_ == 4 ? 1.75 : 1.6)) * (1.0 - 1.0e-6);
    if (sampleRate_ > 0.0) {
        releaseCoefficient_ = std::exp(-1.0 / (sampleRate_ * parameters_.releaseMs / 1000.0));
    }
}

double TrueLimiter::detectPeak(double left, double right) noexcept {
    const std::array<double, 2> samples{left, right};
    double peak = 0.0;
    for (unsigned channel = 0; channel < channels_; ++channel) {
        auto &history = detectorHistory_[channel];
        for (unsigned i = 23; i > 0; --i) {
            history[i] = history[i - 1];
        }
        history[0] = samples[channel];
        peak = std::max(peak, std::abs(samples[channel]));
        for (unsigned phase = 0; phase < 16; phase += 16 / oversampling_) {
            double value = 0.0;
            for (unsigned tap = 0; tap < 24; ++tap) {
                value += history[tap] * coefficients_[phase][tap];
            }
            peak = std::max(peak, std::abs(value));
        }
    }
    return peak;
}

double TrueLimiter::guardFrame(double left, double right, float &outLeft,
                               float &outRight) noexcept {
    const std::array<double, 2> samples{left, right};
    double gain = 1.0;
    for (unsigned channel = 0; channel < channels_; ++channel) {
        const double sample = samples[channel];
        if (sample == 0.0) {
            continue;
        }
        const double inverseSample = 1.0 / sample;
        for (unsigned phase = 0; phase < 16; phase += 16 / oversampling_) {
            const auto &partial = partials_[channel][phase];
            for (unsigned tap = 0; tap < 24; ++tap) {
                const double coefficient = coefficients_[phase][tap];
                if (coefficient == 0.0) {
                    continue;
                }
                const double prior = tap < 23 ? partial[tap + 1] : 0.0;
                // A lowered ceiling cannot undo old emitted samples. Retain
                // their contribution, but never strengthen an existing overrun.
                const double upper = std::max(ceiling_, prior);
                const double lower = std::min(-ceiling_, prior);
                const double bound = coefficient * sample > 0.0 ? upper - prior : lower - prior;
                gain = std::min(gain, bound * reciprocal_[phase][tap] * inverseSample);
            }
        }
    }
    gain = std::clamp(gain, 0.0, 1.0);
    outLeft = static_cast<float>(left * gain);
    outRight = channels_ == 2 ? static_cast<float>(right * gain) : 0.0F;
    const std::array<double, 2> output{outLeft, outRight};
    for (unsigned channel = 0; channel < channels_; ++channel) {
        for (unsigned phase = 0; phase < 16; ++phase) {
            auto &partial = partials_[channel][phase];
            for (unsigned tap = 0; tap < 24; ++tap) {
                partial[tap] = (tap < 23 ? partial[tap + 1] : 0.0) +
                               coefficients_[phase][tap] * output[channel];
            }
        }
    }
    return gain;
}

void TrueLimiter::updateLiveGain(const LoudnessBlock &block) noexcept {
    if (!block.ready || !parameters_.liveLoudness) {
        return;
    }
    const auto reading = loudness_.shortTerm();
    if (!reading.valid || !std::isfinite(reading.lufs) || reading.lufs < -60.0) {
        liveTarget_ = 1.0;
        return;
    }
    const double correctionDb = std::clamp(static_cast<double>(parameters_.targetLufs) -
                                               reading.lufs - parameters_.inputGainDb,
                                           -96.0, 24.0);
    liveTarget_ = fromDb(correctionDb);
}

bool TrueLimiter::process(std::span<float> left, std::span<float> right) noexcept {
    if (channels_ == 0 || (channels_ == 1 && !right.empty()) ||
        (channels_ == 2 && right.size() != left.size())) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (!std::isfinite(left[i]) || (channels_ == 2 && !std::isfinite(right[i]))) {
            return false;
        }
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        LoudnessBlock block;
        (void)loudness_.processSample(left[i], channels_ == 2 ? right[i] : 0.0F, block);
        updateLiveGain(block);
        liveGain_ = liveTarget_ + liveCoefficient_ * (liveGain_ - liveTarget_);
        const double gain = inputGain_.next() * liveGain_;
        const double mix = mix_.next();
        const double mixedGain = 1.0 + mix * (gain - 1.0);
        const double candidateLeft = left[i] * mixedGain;
        const double candidateRight = channels_ == 2 ? right[i] * mixedGain : 0.0;
        const double detected = detectPeak(candidateLeft, candidateRight);
        peakDelay_[peakPosition_] = detected;
        const auto detectorLag = maximumLookaheadFrames_ - effectiveLookaheadFrames_;
        const double peak =
            peakDelay_[(peakPosition_ + peakDelay_.size() - detectorLag) % peakDelay_.size()];
        peakPosition_ = (peakPosition_ + 1) % peakDelay_.size();
        if (peak >= peakEnvelope_) {
            peakEnvelope_ = peak;
            holdFrames_ = effectiveLookaheadFrames_ + 24;
        } else if (holdFrames_ != 0) {
            --holdFrames_;
        } else {
            peakEnvelope_ = peak + releaseCoefficient_ * (peakEnvelope_ - peak);
        }
        const double limitingGain = peakEnvelope_ > ceiling_ ? ceiling_ / peakEnvelope_ : 1.0;
        const double delayedLeft = delay_[0][delayPosition_];
        delay_[0][delayPosition_] = candidateLeft;
        double delayedRight = 0.0;
        if (channels_ == 2) {
            delayedRight = delay_[1][delayPosition_];
            delay_[1][delayPosition_] = candidateRight;
        }
        delayPosition_ = (delayPosition_ + 1) % delay_[0].size();
        float outLeft;
        float outRight;
        const double guardGain =
            guardFrame(delayedLeft * limitingGain, delayedRight * limitingGain, outLeft, outRight);
        left[i] = outLeft;
        if (channels_ == 2) {
            right[i] = outRight;
        }
        lastReduction_ = limitingGain * guardGain;
        if (lastReduction_ < 1.0 && (delayedLeft != 0.0 || delayedRight != 0.0)) {
            protectionEngaged_ = true;
        }
    }
    return true;
}

std::size_t TrueLimiter::latencyFrames() const noexcept { return delay_[0].size(); }

double TrueLimiter::gainReductionDb() const noexcept {
    return lastReduction_ > 0.0 ? -20.0 * std::log10(lastReduction_)
                                : std::numeric_limits<double>::infinity();
}

LoudnessReading TrueLimiter::inputShortTerm() const noexcept { return loudness_.shortTerm(); }

bool TrueLimiter::protectionEngaged() const noexcept { return protectionEngaged_; }

unsigned TrueLimiter::oversampling() const noexcept { return oversampling_; }

LoudnessMatchResult TrueLimiter::analyzeMatch(std::span<const float> left,
                                              std::span<const float> right, double rate,
                                              double targetLufs,
                                              const TrueLimiterParameters &parameters,
                                              double lookaheadMs, unsigned oversampling) noexcept {
    LoudnessMatchResult result;
    if (!std::isfinite(targetLufs) || targetLufs < -36.0 || targetLufs > -5.0 ||
        (!right.empty() && right.size() != left.size()) || !validParameters(parameters)) {
        return result;
    }
    try {
        const auto input = measure(left, right, rate);
        if (!input.valid || !std::isfinite(input.lufs)) {
            return result;
        }
        TrueLimiter limiter;
        if (!limiter.prepare(rate, right.empty() ? 1 : 2, lookaheadMs, oversampling)) {
            return result;
        }
        auto settings = parameters;
        const double requestedDb = targetLufs - input.lufs;
        const double appliedDb = std::clamp(requestedDb, -24.0, 24.0);
        settings.inputGainDb = static_cast<float>(appliedDb);
        settings.liveLoudness = false;
        settings.mix = 1.0F;
        if (!limiter.setParameters(settings)) {
            return result;
        }
        limiter.reset();
        std::vector<float> outputLeft(left.begin(), left.end());
        std::vector<float> outputRight(right.begin(), right.end());
        const auto latency = limiter.latencyFrames();
        outputLeft.resize(outputLeft.size() + latency, 0.0F);
        if (!right.empty()) {
            outputRight.resize(outputRight.size() + latency, 0.0F);
        }
        if (!limiter.process(outputLeft, outputRight)) {
            return result;
        }
        // Remove leading delay to retain the same block alignment and exclude
        // artificial tail zeros from the program loudness measurement.
        const auto alignedLeft = std::span<const float>(outputLeft).subspan(latency, left.size());
        const auto alignedRight =
            right.empty() ? std::span<const float>{}
                          : std::span<const float>(outputRight).subspan(latency, right.size());
        const auto output = measure(alignedLeft, alignedRight, rate);
        if (!output.valid) {
            return result;
        }
        result.valid = true;
        result.inputLufs = input.lufs;
        result.outputLufs = output.lufs;
        result.gainDb = settings.inputGainDb;
        result.targetReachable = std::abs(output.lufs - targetLufs) <= 0.1;
        result.peakLimited = limiter.protectionEngaged_;
    } catch (...) {
        return {};
    }
    return result;
}

} // namespace ahq::audio
