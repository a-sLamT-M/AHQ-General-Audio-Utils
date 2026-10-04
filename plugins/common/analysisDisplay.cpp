#include "analysisDisplay.h"
#include <cmath>
AnalysisDisplay::AnalysisDisplay(PluginAnalysis &a) : analysis(a) {
    std::array<float, fftSize> unityWindow;
    unityWindow.fill(1);
    window.multiplyWithWindowingTable(unityWindow.data(), fftSize);
    windowSum = 0;
    for (float sample : unityWindow)
        windowSum += sample;
    spectrumDb.fill(-90);
}
void AnalysisDisplay::refresh(bool spectrumVisible) {
    const auto levels = analysis.readLevels();
    for (size_t index = 0; index < levels.size(); ++index)
        displayedLevels[index] = std::max(levels[index], displayedLevels[index] * 0.88F);
    if (spectrumVisible)
        updateSpectrum();
}
void AnalysisDisplay::paintMeters(juce::Graphics &graphics) {
    graphics.setFont(14.0F);
    for (int index = 0; index < 2; ++index) {
        const float decibels =
            juce::Decibels::gainToDecibels(displayedLevels[size_t(index)], -60.0F);
        const int y = 278 + index * 26;
        graphics.setColour(juce::Colours::white);
        graphics.drawText(index == 0 ? "Input" : "Output", 20, y, 70, 20,
                          juce::Justification::left);
        graphics.setColour(juce::Colour(0xff41474e));
        graphics.fillRect(92, y + 3, 420, 14);
        graphics.setColour(decibels >= 0 ? juce::Colours::orangered : juce::Colours::lightgreen);
        graphics.fillRect(92, y + 3, int(420 * juce::jlimit(0.0F, 1.0F, (decibels + 60) / 60)), 14);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(juce::String(decibels, 1) + " dBFS", 525, y, 115, 20,
                          juce::Justification::right);
    }
}
void AnalysisDisplay::updateSpectrum() {
    const double sampleRate = analysis.getSpectrumSampleRate();
    if (sampleRate != displayedSampleRate) {
        displayedSampleRate = sampleRate;
        analysis.setSpectrumEnabled(true);
        spectrumDb.fill(-90);
        spectrumPath.clear();
        return;
    }
    bool received = false;
    // Drain only a bounded number of windows so a busy producer cannot monopolize the GUI.
    for (int count = 0; count < 4; ++count) {
        if (!analysis.readSpectrum(std::span<float>(fftLeft.data(), fftSize),
                                   std::span<float>(fftRight.data(), fftSize)))
            break;
        received = true;
    }
    if (received) {
        window.multiplyWithWindowingTable(fftLeft.data(), fftSize);
        window.multiplyWithWindowingTable(fftRight.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform(fftLeft.data());
        fft.performFrequencyOnlyForwardTransform(fftRight.data());
        for (int bin = 1; bin < fftSize / 2; ++bin) {
            const float left = fftLeft[size_t(bin)] * 2 / windowSum;
            const float right = fftRight[size_t(bin)] * 2 / windowSum;
            const float power = (left * left + right * right) * 0.5F;
            const float decibels = 10 * std::log10(std::max(power, 1.0e-9F));
            spectrumDb[size_t(bin)] = std::max(decibels, spectrumDb[size_t(bin)] - 2);
        }
    } else {
        for (auto &value : spectrumDb)
            value = std::max(-90.0F, value - 2);
    }
    spectrumPath.clear();
    const double maximumHz = std::min(20000.0, sampleRate * 0.5);
    if (maximumHz <= 20)
        return;
    bool first = true;
    for (int bin = 1; bin < fftSize / 2; ++bin) {
        const double frequencyHz = bin * sampleRate / fftSize;
        if (frequencyHz < 20 || frequencyHz > maximumHz)
            continue;
        const float x = 65 + 550 * float(std::log(frequencyHz / 20) / std::log(maximumHz / 20));
        const float y = 555 - 150 * juce::jlimit(0.0F, 1.0F, (spectrumDb[size_t(bin)] + 90) / 90);
        if (first) {
            spectrumPath.startNewSubPath(x, y);
            first = false;
        } else
            spectrumPath.lineTo(x, y);
    }
}

void AnalysisDisplay::paintSpectrum(juce::Graphics &graphics) {
    graphics.setFont(12.0F);
    graphics.setColour(juce::Colours::white);
    graphics.drawText("Output spectrum (approximate L/R power average)", 20, 378, 620, 22,
                      juce::Justification::left);
    for (int decibels : {0, -30, -60, -90}) {
        const int y = 405 - decibels * 150 / 90;
        graphics.setColour(juce::Colour(0xff41474e));
        graphics.drawHorizontalLine(y, 65, 615);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(juce::String(decibels), 20, y - 8, 38, 16, juce::Justification::right);
    }
    graphics.drawText("dBFS", 20, 564, 38, 18, juce::Justification::right);
    const double maximumHz = std::min(20000.0, analysis.getSpectrumSampleRate() * 0.5);
    if (maximumHz > 20) {
        for (int hz : {20, 100, 1000, 10000, 20000}) {
            if (hz > maximumHz)
                continue;
            const int x = 65 + int(550 * std::log(hz / 20.0) / std::log(maximumHz / 20));
            graphics.setColour(juce::Colour(0xff41474e));
            graphics.drawVerticalLine(x, 405, 555);
            graphics.setColour(juce::Colours::white);
            graphics.drawText(juce::String(hz), x - 24, 560, 48, 18, juce::Justification::centred);
        }
    }
    graphics.drawText("Hz", 610, 580, 30, 18, juce::Justification::right);
    graphics.setColour(juce::Colours::lightblue);
    graphics.strokePath(spectrumPath, juce::PathStrokeType(1.5F));
}