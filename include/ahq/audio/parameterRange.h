#pragma once

#include <cmath>

namespace ahq::audio {

/// Inclusive finite bounds. An interval of zero accepts continuous values.
/// Integer and choice parameters use interval one. Validation never clamps input.
struct ParameterRange {
    float minimum;
    float maximum;
    float interval = 0.0F;

    [[nodiscard]] bool contains(double value) const noexcept {
        if (!std::isfinite(value) || value < minimum || value > maximum) {
            return false;
        }
        if (interval == 0.0F) {
            return true;
        }
        const double steps = (value - minimum) / interval;
        return steps == std::floor(steps);
    }
};

} // namespace ahq::audio
