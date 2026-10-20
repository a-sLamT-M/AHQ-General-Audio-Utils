#include "analysisDisplay.h"
#include <cmath>
AnalysisDisplay::AnalysisDisplay(PluginAnalysis &a) : analysis(a) {
    std::array<float, fftSize> unityWindow;
    unityWindow.fill(1);
    window.multiplyWithWindowingTable(unityWindow.data(), fftSize);
    windowSum = 0;
    for (float sample : unityWindow)
        windowSum += sample;
    spectrumDb.fill(-120);
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
        spectrumDb.fill(-120);
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
            const float decibels = 10 * std::log10(std::max(power, 1.0e-12F));
            spectrumDb[size_t(bin)] = std::max(decibels, spectrumDb[size_t(bin)] - 2);
        }
    } else {
        for (auto &value : spectrumDb)
            value = std::max(-120.0F, value - 2);
    }
}

void AnalysisDisplay::paintSpectrum(juce::Graphics &graphics) {
    graphics.setColour(juce::Colours::white);
    graphics.setFont(12.0F);
    graphics.drawText("Output spectrum (approximate L/R power average)", 20, 378, 620, 22,
                      juce::Justification::left);
    paintSpectrum(graphics, {65, 405, 550, 150}, 0, 90, 0, juce::Colours::lightblue);
}

void AnalysisDisplay::paintSpectrum(juce::Graphics &graphics, juce::Rectangle<float> plot,
                                    float gainDb, float rangeDb, int style, juce::Colour colour) {
    graphics.setFont(12.0F);
    rangeDb = juce::jlimit(30.F, 120.F, rangeDb);
    for (int step = 0; step <= 4; ++step) {
        const float y = plot.getY() + plot.getHeight() * step / 4;
        graphics.setColour(juce::Colour(0xff353e4a));
        graphics.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
        graphics.setColour(juce::Colour(0xffb7c6d5));
        graphics.drawText(juce::String(-rangeDb * step / 4, 0), int(plot.getX()) - 38, int(y) - 8,
                          32, 16, juce::Justification::right);
    }
    const double maximumHz = std::min(20000.0, analysis.getSpectrumSampleRate() * 0.5);
    if (maximumHz > 20) {
        const auto xFor = [&](double hz) {
            return plot.getX() +
                   plot.getWidth() * float(std::log(hz / 20) / std::log(maximumHz / 20));
        };
        for (int hz : {20, 100, 1000, 10000, 20000}) {
            if (hz > maximumHz)
                continue;
            const int x = int(xFor(hz));
            graphics.setColour(juce::Colour(0xff353e4a));
            graphics.drawVerticalLine(x, plot.getY(), plot.getBottom());
            graphics.setColour(juce::Colour(0xffb7c6d5));
            graphics.drawText(hz >= 1000 ? juce::String(hz / 1000) + "k" : juce::String(hz), x - 24,
                              int(plot.getBottom()) + 4, 48, 18, juce::Justification::centred);
        }
        juce::Graphics::ScopedSaveState save(graphics);
        graphics.reduceClipRegion(plot.toNearestInt());
        juce::Path line;
        line.preallocateSpace(fftSize * 3);
        bool first = true;
        float firstX = plot.getX(), lastX = firstX;
        // Aggregate by pixel column so a wide FFT does not draw thousands of
        // overlapping bars in the high-frequency part of a logarithmic display.
        int previousColumn = -1;
        float columnTop = plot.getBottom();
        auto point = [&](int column, float y) {
            const float x = plot.getX() + column;
            if (first) {
                line.startNewSubPath(x, y);
                firstX = x;
                first = false;
            } else
                line.lineTo(x, y);
            lastX = x;
            if (style == 2)
                graphics.fillRect(x, y, 1.F, plot.getBottom() - y);
        };
        graphics.setColour(colour);
        for (int bin = 1; bin < fftSize / 2; ++bin) {
            const double hz = bin * displayedSampleRate / fftSize;
            if (hz < 20 || hz > maximumHz)
                continue;
            const int column = int(xFor(hz) - plot.getX());
            const float y =
                plot.getBottom() -
                plot.getHeight() *
                    juce::jlimit(0.F, 1.F, (spectrumDb[size_t(bin)] + gainDb + rangeDb) / rangeDb);
            if (column != previousColumn) {
                if (previousColumn >= 0)
                    point(previousColumn, columnTop);
                previousColumn = column;
                columnTop = y;
            } else
                columnTop = std::min(columnTop, y);
        }
        if (previousColumn >= 0)
            point(previousColumn, columnTop);
        if (style == 1 && !first) {
            auto fill = line;
            fill.lineTo(lastX, plot.getBottom());
            fill.lineTo(firstX, plot.getBottom());
            fill.closeSubPath();
            graphics.setColour(colour.withAlpha(.22F));
            graphics.fillPath(fill);
        }
        graphics.setColour(colour);
        if (style != 2)
            graphics.strokePath(line, juce::PathStrokeType(1.5F));
    }
}
