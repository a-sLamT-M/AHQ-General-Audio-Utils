#include "ahq/audio/truePeakMeter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ahq::audio {
namespace {

// ITU-R BS.1770-5 Annex 2: 48 kHz, four-phase FIR interpolation table.
// Coefficients are used directly with floating point (no integer headroom shift).
constexpr std::array<std::array<double, 12>, 4> COEFFICIENTS{
    {{0.0017089843750, 0.0109863281250, -0.0196533203125, 0.0332031250000, -0.0594482421875,
      0.1373291015625, 0.9721679687500, -0.1022949218750, 0.0476074218750, -0.0266113281250,
      0.0148925781250, -0.0083007812500},
     {-0.0291748046875, 0.0292968750000, -0.0517578125000, 0.0891113281250, -0.1665039062500,
      0.4650878906250, 0.7797851562500, -0.2003173828125, 0.1015625000000, -0.0582275390625,
      0.0330810546875, -0.0189208984375},
     {-0.0189208984375, 0.0330810546875, -0.0582275390625, 0.1015625000000, -0.2003173828125,
      0.7797851562500, 0.4650878906250, -0.1665039062500, 0.0891113281250, -0.0517578125000,
      0.0292968750000, -0.0291748046875},
     {-0.0083007812500, 0.0148925781250, -0.0266113281250, 0.0476074218750, -0.1022949218750,
      0.9721679687500, 0.1373291015625, -0.0594482421875, 0.0332031250000, -0.0196533203125,
      0.0109863281250, 0.0017089843750}}};

double toDb(double peak) noexcept {
    return peak > 0.0 ? 20.0 * std::log10(peak) : -std::numeric_limits<double>::infinity();
}

} // namespace

double PeakReading::samplePeakDb() const noexcept { return toDb(samplePeak); }
double PeakReading::truePeakDb() const noexcept { return toDb(truePeak); }

bool TruePeakMeter::prepare(double sampleRate, unsigned channels) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0 ||
        (channels != 1 && channels != 2)) {
        return false;
    }
    channels_ = channels;
    reset();
    return true;
}

void TruePeakMeter::reset() noexcept {
    history_ = {};
    readings_ = {};
    finished_ = false;
}

void TruePeakMeter::clearPeaks() noexcept { readings_ = {}; }

void TruePeakMeter::processFrame(double left, double right, bool countSamplePeak) noexcept {
    const std::array<double, 2> samples{left, right};
    for (unsigned channel = 0; channel < channels_; ++channel) {
        auto &history = history_[channel];
        for (std::size_t i = history.size() - 1; i > 0; --i) {
            history[i] = history[i - 1];
        }
        history[0] = samples[channel];
        auto &peak = readings_[channel];
        if (countSamplePeak) {
            peak.samplePeak = std::max(peak.samplePeak, std::abs(samples[channel]));
            peak.truePeak = std::max(peak.truePeak, peak.samplePeak);
        }
        for (const auto &phase : COEFFICIENTS) {
            double interpolated = 0.0;
            for (std::size_t i = 0; i < history.size(); ++i) {
                interpolated += phase[i] * history[i];
            }
            peak.truePeak = std::max(peak.truePeak, std::abs(interpolated));
        }
    }
}

bool TruePeakMeter::process(std::span<const float> left, std::span<const float> right) noexcept {
    if (channels_ == 0 || (channels_ == 1 && !right.empty()) ||
        (channels_ == 2 && right.size() != left.size()) || (finished_ && !left.empty())) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (!std::isfinite(left[i]) || (channels_ == 2 && !std::isfinite(right[i]))) {
            return false;
        }
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        processFrame(left[i], channels_ == 2 ? right[i] : 0.0, true);
    }
    return true;
}

bool TruePeakMeter::finish() noexcept {
    if (channels_ == 0) {
        return false;
    }
    if (!finished_) {
        for (unsigned i = 0; i < 11; ++i) {
            processFrame(0.0, 0.0, false);
        }
        finished_ = true;
    }
    return true;
}

PeakReading TruePeakMeter::reading(unsigned channel) const noexcept {
    return channel < channels_ ? readings_[channel] : PeakReading{};
}

} // namespace ahq::audio
