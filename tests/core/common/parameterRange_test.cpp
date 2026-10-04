#include "ahq/audio/parameterRange.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
bool expect(bool condition, const char *message) {
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
} // namespace

int main() {
    using ahq::audio::ParameterRange;
    const ParameterRange continuous{0.1F, 10};
    bool passed =
        expect(continuous.contains(0.1F) && continuous.contains(10) && continuous.contains(1.25),
               "inclusive continuous range");
    passed &= expect(!continuous.contains(std::nextafter(0.1F, 0.0F)) &&
                         !continuous.contains(std::nextafter(10.0F, 11.0F)),
                     "reject values immediately outside bounds");
    passed &= expect(!continuous.contains(std::numeric_limits<double>::quiet_NaN()) &&
                         !continuous.contains(std::numeric_limits<double>::infinity()) &&
                         !continuous.contains(-std::numeric_limits<double>::infinity()),
                     "reject all nonfinite values");
    const ParameterRange choices{0, 3, 1};
    passed &= expect(choices.contains(0) && choices.contains(3) && !choices.contains(0.5) &&
                         !choices.contains(-1) && !choices.contains(4),
                     "choice indices must be integral and in range");
    const ParameterRange seed{0, 65535, 1};
    passed &= expect(seed.contains(65535) && !seed.contains(65536) && !seed.contains(1.5),
                     "integer seed boundaries");
    const ParameterRange feedback{0, 0.95F};
    passed &= expect(feedback.contains(0.95F) && !feedback.contains(0.96F),
                     "float parameter endpoints remain valid after promotion to double");
    return passed ? 0 : 1;
}
