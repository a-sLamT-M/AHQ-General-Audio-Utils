#pragma once
#include "../common/parameterControl.h"
#include "../common/ui/presetBar.h"
#include "elevationProcessor.h"
class ElevationEditor final : public juce::AudioProcessorEditor {
  public:
    explicit ElevationEditor(ElevationProcessor &);
    void paint(juce::Graphics &) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &) override;
    int getControlParameterIndex(juce::Component &c) override { return controlParameterIndex(c); }

  private:
    void dragPosition(juce::Point<float>);
    ElevationProcessor &processor;
    ahq::ui::PluginTheme appearance{*this, processor.getName()};
    ahq::ui::PresetBar presetBar{processor, appearance};
    std::array<ParameterSlider, 3> sliders;
    std::array<juce::Label, 3> labels;
    std::array<std::unique_ptr<juce::ParameterAttachment>, 3> attachments;
    juce::TextButton undo{"Undo"}, redo{"Redo"};
    ElevationProcessor::Values before{};
    juce::Rectangle<float> graph;
    bool dragging = false, positionDrag = false;
    float elevation = 0;
};
