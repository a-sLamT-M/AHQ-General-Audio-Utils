#pragma once
#include "pluginAnalysis.h"
#include "ui/spectrumControls.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
/// Message-thread FFT, paths and approximate meters shared by the two editors.
class AnalysisDisplay {
  public:
    explicit AnalysisDisplay(PluginAnalysis &);
    ahq::ui::SpectrumControls controls;
    void setSpectrumBounds(juce::Rectangle<float> bounds) { spectrumBounds = bounds; }
    juce::Rectangle<float> getSpectrumBounds() const { return spectrumBounds; }
    void paintConfiguredSpectrum(juce::Graphics &, juce::Rectangle<float>, juce::Colour,
                                 bool drawLevelGrid = true);
    void refresh(bool spectrumVisible);
    void paintMeters(juce::Graphics &);
    void paintSpectrum(juce::Graphics &);
    // Display gain only: never changes the audio or published measurements.
    void paintSpectrum(juce::Graphics &, juce::Rectangle<float>, float gainDb, float rangeDb,
                       int style, juce::Colour colour, bool drawLevelGrid = true);

  private:
    void updateSpectrum(double nowMs);
    PluginAnalysis &analysis;
    std::array<float, 2> displayedLevels{};
    static constexpr int fftSize = 2048;
    static constexpr int hopSize = fftSize / 4;
    juce::dsp::FFT fft{11};
    juce::dsp::WindowingFunction<float> window{fftSize, juce::dsp::WindowingFunction<float>::hann,
                                               false};
    std::array<float, fftSize * 2> fftLeft{}, fftRight{};
    std::array<float, fftSize> windowLeft{}, windowRight{};
    std::array<float, fftSize / 2> targetSpectrumDb{};
    std::array<float, fftSize / 2> spectrumDb{}, smoothedSpectrumDb{};
    int windowPosition = 0, windowSamples = 0;
    juce::Rectangle<float> spectrumBounds;
    double lastRefreshMs = 0;
    double lastSpectrumDataMs = 0;
    float smoothingAlpha = 1;
    float windowSum = 1;
    double displayedSampleRate = 48000;
};
