#pragma once
#include "../common/analysisDisplay.h"
#include "trueLimiterProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

class TrueLimiterEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer,
                                private juce::ComponentMovementWatcher {
  public:
    explicit TrueLimiterEditor(TrueLimiterProcessor &);
    ~TrueLimiterEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

  private:
    void timerCallback() override;
    void chooseAudio();
    void componentMovedOrResized(bool, bool) override {}
    void componentPeerChanged() override { updateSpectrumVisibility(); }
    void componentVisibilityChanged() override { updateSpectrumVisibility(); }
    void updateSpectrumVisibility();
    TrueLimiterProcessor &processor;
    std::array<juce::Slider, 6> sliders;
    std::array<juce::Label, 6> labels;
    juce::ToggleButton live{"Live 3 s LUFS"};
    juce::ComboBox quality;
    std::unique_ptr<juce::ParameterAttachment> qualityAttachment;
    juce::TextButton analyze{"Analyze audio file"}, cancel{"Cancel"}, apply{"Apply match"};
    juce::TextButton undo{"Undo"}, redo{"Redo"};
    juce::Label meters, result, notice;
    juce::TooltipWindow tooltips{this, 700};
    std::unique_ptr<juce::FileChooser> chooser;
    TrueLimiterProcessor::Values dragBefore{};
    bool dragging = false;
    bool spectrumVisible = false;
    AnalysisDisplay display;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrueLimiterEditor)
};
