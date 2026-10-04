#pragma once
#include "../common/analysisDisplay.h"
#include "ghostDelayProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

/// Native controls and preset file operations, all owned by the message thread.
class GhostDelayEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer,
                               private juce::ComponentMovementWatcher {
  public:
    explicit GhostDelayEditor(GhostDelayProcessor &);
    ~GhostDelayEditor() override;
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
    void configureSlider(juce::Slider &, int index, const juce::String &suffix);
    GhostDelayProcessor &processor;
    std::array<juce::Slider, 9> sliders;
    juce::ComboBox presets;
    juce::TextButton saveButton{"Save preset"}, loadButton{"Load preset"};
    juce::TextButton undoButton{"Undo"}, redoButton{"Redo"};
    std::array<juce::Label, 9> labels;
    juce::Label status;
    juce::TooltipWindow tooltipWindow{this, 700};
    std::unique_ptr<juce::FileChooser> fileChooser;
    GhostDelayProcessor::Values dragBefore{};
    bool dragging = false;
    AnalysisDisplay display;
    bool spectrumVisible = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostDelayEditor)
};
