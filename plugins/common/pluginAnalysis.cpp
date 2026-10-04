#include "pluginAnalysis.h"
#include <cmath>
float PluginAnalysis::peak(const juce::AudioBuffer<float> &buffer) noexcept {
    float maximum = 0;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
            maximum = std::max(maximum, std::abs(buffer.getReadPointer(channel)[frame]));
    return maximum;
}
void PluginAnalysis::prepare(double sampleRate) noexcept {
    if (std::isfinite(sampleRate) && sampleRate > 0)
        spectrumSampleRate.store(sampleRate, std::memory_order_relaxed);
    inputLevel.store(0, std::memory_order_relaxed);
    outputLevel.store(0, std::memory_order_relaxed);
}
void PluginAnalysis::publish(const juce::AudioBuffer<float> &buffer, float inputPeak) noexcept {
    // Bounded load/store keeps the audio path free of compare/exchange retry loops.
    // A simultaneous editor exchange may carry a peak into the next display frame.
    inputLevel.store(std::max(inputPeak, inputLevel.load(std::memory_order_relaxed)),
                     std::memory_order_relaxed);
    outputLevel.store(std::max(peak(buffer), outputLevel.load(std::memory_order_relaxed)),
                      std::memory_order_relaxed);
    if (spectrumEnabled.load(std::memory_order_relaxed) && buffer.getNumChannels() > 0) {
        int firstStart = 0, firstCount = 0, secondStart = 0, secondCount = 0;
        spectrumFifo.prepareToWrite(buffer.getNumSamples(), firstStart, firstCount, secondStart,
                                    secondCount);
        const auto *left = buffer.getReadPointer(0);
        const auto *right = buffer.getReadPointer(buffer.getNumChannels() > 1 ? 1 : 0);
        for (int index = 0; index < firstCount; ++index) {
            spectrumLeft[size_t(firstStart + index)] = left[index];
            spectrumRight[size_t(firstStart + index)] = right[index];
        }
        for (int index = 0; index < secondCount; ++index) {
            spectrumLeft[size_t(secondStart + index)] = left[firstCount + index];
            spectrumRight[size_t(secondStart + index)] = right[firstCount + index];
        }
        spectrumFifo.finishedWrite(firstCount + secondCount);
    }
}
void PluginAnalysis::setSpectrumEnabled(bool enabled) noexcept {
    spectrumEnabled.store(false, std::memory_order_relaxed);
    // Only the message-thread consumer advances reads; prepare never resets the shared FIFO.
    int firstStart = 0, firstCount = 0, secondStart = 0, secondCount = 0;
    spectrumFifo.prepareToRead(spectrumFifo.getNumReady(), firstStart, firstCount, secondStart,
                               secondCount);
    spectrumFifo.finishedRead(firstCount + secondCount);
    spectrumEnabled.store(enabled, std::memory_order_relaxed);
}

bool PluginAnalysis::readSpectrum(std::span<float> left, std::span<float> right) noexcept {
    if (left.empty() || left.size() != right.size() || left.size() >= spectrumCapacity ||
        spectrumFifo.getNumReady() < int(left.size()))
        return false;
    int firstStart = 0, firstCount = 0, secondStart = 0, secondCount = 0;
    spectrumFifo.prepareToRead(int(left.size()), firstStart, firstCount, secondStart, secondCount);
    for (int index = 0; index < firstCount; ++index) {
        left[size_t(index)] = spectrumLeft[size_t(firstStart + index)];
        right[size_t(index)] = spectrumRight[size_t(firstStart + index)];
    }
    for (int index = 0; index < secondCount; ++index) {
        left[size_t(firstCount + index)] = spectrumLeft[size_t(secondStart + index)];
        right[size_t(firstCount + index)] = spectrumRight[size_t(secondStart + index)];
    }
    spectrumFifo.finishedRead(firstCount + secondCount);
    return true;
}

std::array<float, 2> PluginAnalysis::readLevels() noexcept {
    return {inputLevel.exchange(0, std::memory_order_relaxed),
            outputLevel.exchange(0, std::memory_order_relaxed)};
}