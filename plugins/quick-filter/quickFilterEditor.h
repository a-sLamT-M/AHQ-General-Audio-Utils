#pragma once
#include "../common/analysisDisplay.h"
#include "../common/parameterControl.h"
#include "../common/ui/presetBar.h"
#include "quickFilterProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

/// Native controls and preset file operations, all owned by the message thread.
class QuickFilterEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer,
                                private juce::ComponentMovementWatcher {
  public:
    explicit QuickFilterEditor(QuickFilterProcessor &);
    ~QuickFilterEditor() override;
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
    QuickFilterProcessor &processor;
    ahq::ui::PluginTheme appearance{*this, processor.getName()};
    ahq::ui::PresetBar presetBar{processor, appearance};
    ParameterSlider frequency, q, mix;
    juce::ComboBox mode, slope, presets;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment, slopeAttachment;
    juce::TextButton undoButton{"Undo"}, redoButton{"Redo"};
    juce::Label frequencyLabel, qLabel, mixLabel;
    juce::TooltipWindow tooltipWindow{this, 700};
    QuickFilterProcessor::Values dragBefore{};
    bool dragging = false;
    AnalysisDisplay display;
    bool spectrumVisible = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuickFilterEditor)
};
