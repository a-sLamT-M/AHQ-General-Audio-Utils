#include "ahq/audio/loudnessRange.h"
#include <algorithm>
#include <cmath>
namespace ahq::audio {
void LoudnessRange::reset() noexcept {
    history.clear();
    distribution.clear();
    lastCount = 0;
    result = {};
}
void LoudnessRange::addShortTerm(double lufs) {
    if (!std::isfinite(lufs) || lufs <= -70)
        return;
    history.push_back(lufs);
    if (history.size() < lastCount + 10)
        return;
    double energy = 0;
    for (double value : history)
        energy += std::pow(10.0, value / 10);
    const double gate = 10 * std::log10(energy / double(history.size())) - 20;
    distribution.clear();
    for (double value : history)
        if (value > gate)
            distribution.push_back(value);
    std::sort(distribution.begin(), distribution.end());
    auto percentile = [&](double fraction) {
        const double at = fraction * double(distribution.size() - 1);
        const std::size_t lower = std::size_t(at),
                          upper = std::min(lower + 1, distribution.size() - 1);
        return distribution[lower] +
               (at - double(lower)) * (distribution[upper] - distribution[lower]);
    };
    result.valid = distribution.size() >= 10;
    if (result.valid)
        result.lu = percentile(.95) - percentile(.1);
    lastCount = history.size();
}
} // namespace ahq::audio
