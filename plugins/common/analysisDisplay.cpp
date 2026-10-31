#include "analysisDisplay.h"
#include <cmath>
AnalysisDisplay::AnalysisDisplay(PluginAnalysis &a) : controls(a.spectrumPreferences), analysis(a) {
    std::array<float, fftSize> unityWindow;
    unityWindow.fill(1);
    window.multiplyWithWindowingTable(unityWindow.data(), fftSize);
    windowSum = 0;
    for (float sample : unityWindow)
        windowSum += sample;
    spectrumDb.fill(-120);
    smoothedSpectrumDb.fill(-120);
}
void AnalysisDisplay::refresh(bool spectrumVisible) {
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto elapsed = lastRefreshMs > 0 ? std::clamp(now - lastRefreshMs, 1.0, 250.0) : 33.0;
    lastRefreshMs = now;
    const float ms = controls.preferences.smoothingMs.load();
    smoothingAlpha = ms > 0 ? float(-std::expm1(-elapsed / ms)) : 1.F;
    const auto levels = analysis.readLevels();
    for (size_t index = 0; index < levels.size(); ++index)
        displayedLevels[index] = std::max(levels[index], displayedLevels[index] * 0.88F);
    if (spectrumVisible)
        updateSpectrum();
}
void AnalysisDisplay::paintMeters(juce::Graphics &graphics) {
    if (!spectrumBounds.isEmpty()) {
        for (size_t i = 0; i < 2; ++i) {
            const float db = juce::Decibels::gainToDecibels(displayedLevels[i], -60.F);
            const float y = spectrumBounds.getBottom() + 10 + float(i) * 22;
            graphics.setFont(juce::FontOptions(12));
            graphics.setColour(juce::Colour(0xffc2ced5));
            graphics.drawText(i == 0 ? "Input" : "Output", 20, int(y), 54, 20,
                              juce::Justification::left);
            const auto track =
                juce::Rectangle<float>(80, y + 5, spectrumBounds.getWidth() - 150, 8);
            graphics.setColour(juce::Colour(0xff10171c));
            graphics.fillRoundedRectangle(track, 3);
            graphics.setColour(db >= 0 ? juce::Colours::orangered : juce::Colour(0xff28ced1));
            graphics.fillRoundedRectangle(
                track.withWidth(track.getWidth() * juce::jlimit(0.F, 1.F, (db + 60) / 60)), 3);
            graphics.setColour(juce::Colour(0xffc2ced5));
            graphics.drawText(juce::String(db, 1) + " dBFS", int(track.getRight()) + 8, int(y), 72,
                              20, juce::Justification::right);
        }
        return;
    }
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
        smoothedSpectrumDb.fill(-120);
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
            spectrumDb[size_t(bin)] += smoothingAlpha * (decibels - spectrumDb[size_t(bin)]);
        }
    } else {
        for (auto &value : spectrumDb)
            value += smoothingAlpha * (-120.0F - value);
    }
    const int mode = juce::jlimit(0, 3, controls.preferences.frequencySmoothing.load());
    if (mode == 0) {
        smoothedSpectrumDb = spectrumDb;
    } else {
        std::array<double, fftSize / 2 + 1> powerSum{};
        for (size_t i = 0; i < spectrumDb.size(); ++i)
            powerSum[i + 1] = powerSum[i] + std::pow(10.0, spectrumDb[i] / 10.0);
        const double octaves[]{0, 1.0 / 12, 1.0 / 6, 1.0 / 3};
        const double factor = std::pow(2.0, octaves[mode] * .5);
        for (int i = 1; i < fftSize / 2; ++i) {
            const int lo = juce::jmax(1, int(std::ceil(i / factor)));
            const int hi = juce::jmin(fftSize / 2 - 1, int(std::floor(i * factor)));
            const double power = (powerSum[size_t(hi + 1)] - powerSum[size_t(lo)]) / (hi - lo + 1);
            smoothedSpectrumDb[size_t(i)] = float(10 * std::log10(std::max(1e-12, power)));
        }
    }
}

void AnalysisDisplay::paintSpectrum(juce::Graphics &graphics) {
    if (!spectrumBounds.isEmpty()) {
        graphics.setColour(juce::Colour(0xff11191d));
        graphics.fillRoundedRectangle(spectrumBounds, 8.F);
        paintConfiguredSpectrum(graphics,
                                spectrumBounds.withTrimmedLeft(44)
                                    .withTrimmedRight(12)
                                    .withTrimmedTop(18)
                                    .withTrimmedBottom(30),
                                juce::Colour(0xff28ced1));
        return;
    }
    graphics.setColour(juce::Colours::white);
    graphics.setFont(12.0F);
    graphics.drawText("Output spectrum (approximate L/R power average)", 20, 378, 620, 22,
                      juce::Justification::left);
    paintSpectrum(graphics, {65, 405, 550, 150}, 0, 90, 0, juce::Colours::lightblue);
}

void AnalysisDisplay::paintConfiguredSpectrum(juce::Graphics &g, juce::Rectangle<float> plot,
                                              juce::Colour colour) {
    paintSpectrum(g, plot, controls.preferences.gainDb.load(), 90,
                  controls.preferences.style.load(), colour);
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
                graphics.fillRect(x, y, 4.F, plot.getBottom() - y);
        };
        graphics.setColour(colour);
        for (int bin = 1; bin < fftSize / 2; ++bin) {
            const double hz = bin * displayedSampleRate / fftSize;
            if (hz < 20 || hz > maximumHz)
                continue;
            const int pixel = int(xFor(hz) - plot.getX());
            const int column = style == 2 ? (pixel / 6) * 6 : pixel;
            const float y =
                plot.getBottom() -
                plot.getHeight() *
                    juce::jlimit(0.F, 1.F,
                                 (smoothedSpectrumDb[size_t(bin)] + gainDb + rangeDb) / rangeDb);
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
