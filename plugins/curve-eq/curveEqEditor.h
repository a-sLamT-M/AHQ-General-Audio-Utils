#pragma once
#include "../common/analysisDisplay.h"
#include "../common/parameterControl.h"
#include "../common/ui/presetBar.h"
#include "curveEqProcessor.h"
class CurveEqEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer,
                            private juce::ComponentMovementWatcher {
  public:
    explicit CurveEqEditor(CurveEqProcessor &);
    ~CurveEqEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDoubleClick(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &) override;
    void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &) override;
    int getControlParameterIndex(juce::Component &c) override { return controlParameterIndex(c); }

  private:
    void timerCallback() override;
    void componentMovedOrResized(bool, bool) override {}
    void componentPeerChanged() override { visibilityChanged(); }
    void componentVisibilityChanged() override { visibilityChanged(); }
    void selectBand(int);
    void rebuildResponse();
    float xFor(double) const;
    double frequencyFor(float) const;
    float yFor(double) const;
    CurveEqProcessor &processor;
    ahq::ui::PluginTheme appearance{*this, processor.getName()};
    ahq::ui::PresetBar presetBar{processor, appearance};
    AnalysisDisplay spectrum;
    juce::ComboBox band, shape, channels, slope, scale;
    juce::ToggleButton enabled{"Band enabled"};
    juce::TextButton add{"Add band"}, remove{"Delete"}, undo{"Undo"}, redo{"Redo"};
    static constexpr std::array<int, 7> sliderFields{3, 4, 5, 7, 8, 9, 10};
    std::array<ParameterSlider, 9> sliders;
    std::array<juce::Label, 9> labels;
    std::array<std::unique_ptr<juce::ParameterAttachment>, 3> choices;
    juce::TooltipWindow tooltips{this, 600};
    juce::Rectangle<float> graph;
    juce::Path midResponse, sideResponse;
    CurveEqProcessor::Values before{}, displayed{}, responseValues{};
    double responseRate = 0;
    int selected = 0;
    float rangeDb = 24;
    bool dragging = false, nodeDragging = false, responseDirty = true;
};
