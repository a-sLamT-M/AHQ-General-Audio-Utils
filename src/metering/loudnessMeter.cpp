#include "ahq/audio/loudnessMeter.h"

#include <algorithm>
#include <cmath>

namespace ahq::audio {
namespace {

LoudnessReading fromEnergy(double energy, bool valid) noexcept {
    return {energy > 0.0 ? -0.691 + 10.0 * std::log10(energy)
                         : -std::numeric_limits<double>::infinity(),
            valid};
}

// Inverse bilinear transform of ITU-R BS.1770-5 Annex 1 Tables 1/2 at 48 kHz,
// followed by the bilinear transform at the requested rate. This preserves the
// same analogue weighting and reproduces the published 48 kHz coefficients.
std::array<double, 3> transform(const std::array<double, 3> &c, double ratio) noexcept {
    const double c0 = c[0] + c[1] + c[2];
    const double c1 = 2.0 * (c[0] - c[2]) * ratio;
    const double c2 = (c[0] - c[1] + c[2]) * ratio * ratio;
    return {c0 + c1 + c2, 2.0 * (c0 - c2), c0 - c1 + c2};
}

} // namespace

double LoudnessMeter::Biquad::process(double input) noexcept {
    const double output = b[0] * input + z1;
    z1 = b[1] * input - a[0] * output + z2;
    z2 = b[2] * input - a[1] * output;
    // Bound denormal tails well below any meaningful loudness measurement.
    if (std::abs(z1) < 1.0e-30) {
        z1 = 0.0;
    }
    if (std::abs(z2) < 1.0e-30) {
        z2 = 0.0;
    }
    return output;
}

bool LoudnessMeter::prepare(double sampleRate, unsigned channels) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 32000.0 || sampleRate > 384000.0 ||
        (channels != 1 && channels != 2)) {
        return false;
    }
    std::vector<double> ring;
    try {
        ring.resize(static_cast<std::size_t>(std::floor(3.0 * sampleRate + 0.5)), 0.0);
    } catch (...) {
        return false;
    }
    const double ratio = sampleRate / 48000.0;
    auto setCoefficients = [ratio](Biquad &filter, const std::array<double, 3> &b,
                                   const std::array<double, 3> &a) {
        const auto numerator = transform(b, ratio);
        const auto denominator = transform(a, ratio);
        for (std::size_t i = 0; i < 3; ++i) {
            filter.b[i] = numerator[i] / denominator[0];
        }
        filter.a = {denominator[1] / denominator[0], denominator[2] / denominator[0]};
    };
    setCoefficients(shelf_[0], {1.53512485958697, -2.69169618940638, 1.19839281085285},
                    {1.0, -1.69065929318241, 0.73248077421585});
    setCoefficients(highPass_[0], {1.0, -2.0, 1.0}, {1.0, -1.99004745483398, 0.99007225036621});
    shelf_[1] = shelf_[0];
    highPass_[1] = highPass_[0];
    energies_.swap(ring);
    momentaryFrames_ = static_cast<std::size_t>(std::floor(0.4 * sampleRate + 0.5));
    stepFrames_ = static_cast<std::size_t>(std::floor(0.1 * sampleRate + 0.5));
    channels_ = channels;
    reset();
    return true;
}

void LoudnessMeter::reset() noexcept {
    for (auto &filter : shelf_) {
        filter.z1 = filter.z2 = 0.0;
    }
    for (auto &filter : highPass_) {
        filter.z1 = filter.z2 = 0.0;
    }
    std::fill(energies_.begin(), energies_.end(), 0.0);
    position_ = 0;
    frames_ = 0;
    nextBlockEnd_ = momentaryFrames_;
    momentarySum_ = shortTermSum_ = 0.0;
    momentaryNonzero_ = shortTermNonzero_ = 0;
}

bool LoudnessMeter::processSample(float left, float right, LoudnessBlock &block) noexcept {
    if (channels_ == 0 || !std::isfinite(left) || (channels_ == 2 && !std::isfinite(right))) {
        return false;
    }
    double weighted = highPass_[0].process(shelf_[0].process(left));
    double energy = weighted * weighted;
    if (channels_ == 2) {
        weighted = highPass_[1].process(shelf_[1].process(right));
        energy += weighted * weighted;
    }
    const auto size = energies_.size();
    const auto momentaryOld = energies_[(position_ + size - momentaryFrames_) % size];
    const auto shortTermOld = energies_[position_];
    momentaryNonzero_ -= momentaryOld > 0.0 ? 1 : 0;
    shortTermNonzero_ -= shortTermOld > 0.0 ? 1 : 0;
    momentaryNonzero_ += energy > 0.0 ? 1 : 0;
    shortTermNonzero_ += energy > 0.0 ? 1 : 0;
    momentarySum_ += energy - momentaryOld;
    shortTermSum_ += energy - shortTermOld;
    if (momentaryNonzero_ == 0) {
        momentarySum_ = 0.0;
    }
    if (shortTermNonzero_ == 0) {
        shortTermSum_ = 0.0;
    }
    energies_[position_] = energy;
    position_ = (position_ + 1) % size;
    ++frames_;
    block = {};
    if (frames_ == nextBlockEnd_) {
        block = {std::max(0.0, momentarySum_) / static_cast<double>(momentaryFrames_), frames_,
                 true};
        nextBlockEnd_ += stepFrames_;
    }
    return true;
}

LoudnessReading LoudnessMeter::momentary() const noexcept {
    if (channels_ == 0 || frames_ < momentaryFrames_) {
        return {};
    }
    return fromEnergy(std::max(0.0, momentarySum_) / static_cast<double>(momentaryFrames_), true);
}

LoudnessReading LoudnessMeter::shortTerm() const noexcept {
    if (channels_ == 0 || frames_ < energies_.size()) {
        return {};
    }
    return fromEnergy(std::max(0.0, shortTermSum_) / static_cast<double>(energies_.size()), true);
}

bool IntegratedLoudness::addBlock(double energy) {
    if (!std::isfinite(energy) || energy < 0.0) {
        return false;
    }
    energies_.push_back(energy);
    return true;
}

void IntegratedLoudness::reset() noexcept { energies_.clear(); }

std::size_t IntegratedLoudness::blockCount() const noexcept { return energies_.size(); }

LoudnessReading IntegratedLoudness::reading() const noexcept {
    const double absoluteGate = std::pow(10.0, (-70.0 + 0.691) / 10.0);
    long double sum = 0.0;
    std::size_t count = 0;
    for (double energy : energies_) {
        if (energy > absoluteGate) {
            sum += energy;
            ++count;
        }
    }
    if (count == 0) {
        return {};
    }
    const double relativeGate = static_cast<double>(sum / count) * 0.1;
    sum = 0.0;
    count = 0;
    for (double energy : energies_) {
        if (energy > absoluteGate && energy > relativeGate) {
            sum += energy;
            ++count;
        }
    }
    return count == 0 ? LoudnessReading{} : fromEnergy(static_cast<double>(sum / count), true);
}

} // namespace ahq::audio
