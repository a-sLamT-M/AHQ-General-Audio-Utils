#pragma once
#include "../common/analysisDisplay.h"
#include "../common/parameterControl.h"
#include "../common/ui/presetBar.h"
#include "curveEqProcessor.h"
#include <bitset>
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
    void mouseMove(const juce::MouseEvent &) override;
    void mouseExit(const juce::MouseEvent &) override;
    void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &) override;
    bool keyPressed(const juce::KeyPress &) override;
    int getControlParameterIndex(juce::Component &c) override { return controlParameterIndex(c); }

  private:
    void timerCallback() override;
    void componentMovedOrResized(bool, bool) override {}
    void componentPeerChanged() override { visibilityChanged(); }
    void componentVisibilityChanged() override { visibilityChanged(); }
    void selectBand(int, bool preserveSelection = false);
    int hitBand(juce::Point<float>) const;
    bool onResponse(juce::Point<float>) const;
    bool createBand(juce::Point<float>, bool fromCurve, bool dynamic);
    void layoutBandControls();
    void beginNodeDrag(juce::Point<float>);
    void finishNodeDrag();
    void deleteSelectedBands();
    void showBandMenu(int);
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
    juce::Rectangle<float> graph, bandControls, selectionBox;
    juce::Path midResponse, sideResponse, selectionResponse;
    CurveEqProcessor::Values before{}, displayed{}, responseValues{}, defaults{};
    double responseRate = 0;
    int selected = 0;
    std::bitset<24> selectedBands, visibleBands;
    std::vector<int> gestureFields;
    juce::Point<float> dragOrigin, hoverPosition;
    float rangeDb = 12;
    int dragAxis = 0;
    int clickAction = 0;
    bool selecting = false, pendingCurve = false, dynamicDragging = false, hovering = false;
    bool dragging = false, nodeDragging = false, responseDirty = true;
};
