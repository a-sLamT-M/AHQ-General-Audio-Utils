#pragma once
#include "pluginAnalysis.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
/// Message-thread FFT, paths and approximate meters shared by the two editors.
class AnalysisDisplay {
  public:
    explicit AnalysisDisplay(PluginAnalysis &);
    void refresh(bool spectrumVisible);
    void paintMeters(juce::Graphics &);
    void paintSpectrum(juce::Graphics &);

  private:
    void updateSpectrum();
    PluginAnalysis &analysis;
    std::array<float, 2> displayedLevels{};
    static constexpr int fftSize = 2048;
    juce::dsp::FFT fft{11};
    juce::dsp::WindowingFunction<float> window{fftSize, juce::dsp::WindowingFunction<float>::hann,
                                               false};
    std::array<float, fftSize * 2> fftLeft{}, fftRight{};
    std::array<float, fftSize / 2> spectrumDb{};
    juce::Path spectrumPath;
    float windowSum = 1;
    double displayedSampleRate = 48000;
};
