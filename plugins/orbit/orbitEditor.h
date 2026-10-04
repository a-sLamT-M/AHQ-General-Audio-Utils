#pragma once
#include "../common/analysisDisplay.h"
#include "orbitProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

/// Native controls and preset file operations, all owned by the message thread.
class OrbitEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer,
                          private juce::ComponentMovementWatcher {
  public:
    explicit OrbitEditor(OrbitProcessor &);
    ~OrbitEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

  private:
    void timerCallback() override;
    void componentMovedOrResized(bool, bool) override {}
    void componentPeerChanged() override { updateSpectrumVisibility(); }
    void componentVisibilityChanged() override { updateSpectrumVisibility(); }
    void updateSpectrumVisibility();
    void choosePresetFile(bool save);
    void configureSlider(juce::Slider &, int index, double minimum, double maximum,
                         const juce::String &suffix);
    OrbitProcessor &processor;
    std::array<juce::Slider, 6> sliders;
    juce::ComboBox speedMode, direction, presets;
    std::unique_ptr<juce::ParameterAttachment> speedModeAttachment, directionAttachment;
    juce::TextButton saveButton{"Save preset"}, loadButton{"Load preset"};
    juce::TextButton undoButton{"Undo"}, redoButton{"Redo"};
    std::array<juce::Label, 6> labels;
    juce::Label status, tempoStatus;
    juce::TooltipWindow tooltipWindow{this, 700};
    std::unique_ptr<juce::FileChooser> fileChooser;
    OrbitProcessor::Values dragBefore{};
    bool dragging = false;
    AnalysisDisplay display;
    bool spectrumVisible = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OrbitEditor)
};
