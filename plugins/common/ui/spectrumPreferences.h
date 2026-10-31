#pragma once
#include <atomic>
namespace ahq::ui {
// Display preferences belong to the processor instance, never to its audio parameters.
struct SpectrumPreferences {
    std::atomic<int> style{1}, frequencySmoothing{1};
    std::atomic<float> smoothingMs{140}, share{65}, gainDb{0};
};

} // namespace ahq::ui
