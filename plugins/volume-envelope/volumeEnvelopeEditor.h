#pragma once
#include "../common/parameterControl.h"
#include "../common/ui/presetBar.h"
#include "volumeEnvelopeProcessor.h"

class VolumeEnvelopeEditor final : public juce::AudioProcessorEditor,
                                   private juce::Timer,
                                   private juce::ComponentMovementWatcher {
  public:
    explicit VolumeEnvelopeEditor(VolumeEnvelopeProcessor &);
    void paint(juce::Graphics &) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &) override;
    int getControlParameterIndex(juce::Component &c) override { return controlParameterIndex(c); }

  private:
    void timerCallback() override;
    void componentMovedOrResized(bool, bool) override {}
    void componentPeerChanged() override { visibilityChanged(); }
    void componentVisibilityChanged() override { visibilityChanged(); }
    VolumeEnvelopeProcessor &processor;
    ahq::ui::PluginTheme appearance{*this, processor.getName()};
    ahq::ui::PresetBar presetBar{processor, appearance};
    juce::ComboBox mode, presets;
    juce::TextButton undo{"Undo"}, redo{"Redo"};
    std::array<ParameterSlider, 3> sliders;
    std::array<juce::Label, 3> labels;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;
    juce::TooltipWindow tooltips{this, 600};
    juce::Rectangle<float> graph;
    VolumeEnvelopeProcessor::Values before{};
    int draggedNode = -1;
    bool sliderDragging = false;
};
