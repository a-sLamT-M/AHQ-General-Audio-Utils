#pragma once
#include "../common/analysisDisplay.h"
#include "../common/parameterControl.h"
#include "../common/ui/presetBar.h"
#include "ghostDelayProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

/// Native controls and preset file operations, all owned by the message thread.
class GhostDelayEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer,
                               private juce::ComponentMovementWatcher {
  public:
    explicit GhostDelayEditor(GhostDelayProcessor &);
    ~GhostDelayEditor() override;
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
    GhostDelayProcessor &processor;
    ahq::ui::PluginTheme appearance{*this, processor.getName()};
    ahq::ui::PresetBar presetBar{processor, appearance};
    std::array<ParameterSlider, 9> sliders;
    juce::ComboBox presets;
    juce::TextButton undoButton{"Undo"}, redoButton{"Redo"};
    std::array<juce::Label, 9> labels;
    juce::Label status;
    juce::TooltipWindow tooltipWindow{this, 700};
    GhostDelayProcessor::Values dragBefore{};
    bool dragging = false;
    AnalysisDisplay display;
    bool spectrumVisible = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostDelayEditor)
};
