#pragma once
#include "ui/spectrumPreferences.h"
#include <array>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <span>
/// Fixed SPSC transport: audio producer, one message-thread consumer. No shared FIFO reset.
class PluginAnalysis {
  public:
    ahq::ui::SpectrumPreferences spectrumPreferences;
    static float peak(const juce::AudioBuffer<float> &) noexcept;
    void prepare(double) noexcept;
    void publish(const juce::AudioBuffer<float> &, float inputPeak) noexcept;
    std::array<float, 2> readLevels() noexcept;
    void setSpectrumEnabled(bool) noexcept;
    bool readSpectrum(std::span<float>, std::span<float>) noexcept;
    double getSpectrumSampleRate() const noexcept { return spectrumSampleRate.load(); }

  private:
    std::atomic<float> inputLevel{0}, outputLevel{0};
    static constexpr int spectrumCapacity = 8192;
    juce::AbstractFifo spectrumFifo{spectrumCapacity};
    std::array<float, spectrumCapacity> spectrumLeft{}, spectrumRight{};
    std::atomic<bool> spectrumEnabled{false};
    std::atomic<double> spectrumSampleRate{48000};
};
