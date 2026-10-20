#pragma once
#include "../common/analysisDisplay.h"
#include "../common/parameterControl.h"
#include "../common/ui/presetBar.h"
#include "orbitProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

/// Native controls and preset file operations, all owned by the message thread.
class OrbitEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer,
                          private juce::ComponentMovementWatcher {
  public:
    explicit OrbitEditor(OrbitProcessor &);
    ~OrbitEditor() override;
    int getControlParameterIndex(juce::Component &c) override { return controlParameterIndex(c); }
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
    void configureSlider(ParameterSlider &, int index, const juce::String &suffix);
    OrbitProcessor &processor;
    ahq::ui::PluginTheme appearance{*this, processor.getName()};
    ahq::ui::PresetBar presetBar{processor, appearance};
    std::array<ParameterSlider, 6> sliders;
    juce::ComboBox speedMode, direction, presets;
    std::unique_ptr<juce::ParameterAttachment> speedModeAttachment, directionAttachment;
    juce::TextButton undoButton{"Undo"}, redoButton{"Redo"};
    std::array<juce::Label, 6> labels;
    juce::Label status, tempoStatus;
    juce::TooltipWindow tooltipWindow{this, 700};
    OrbitProcessor::Values dragBefore{};
    bool dragging = false;
    AnalysisDisplay display;
    bool spectrumVisible = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OrbitEditor)
};
