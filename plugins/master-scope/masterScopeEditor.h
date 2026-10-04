#pragma once
#include "../common/analysisDisplay.h"
#include "masterScopeProcessor.h"
class MasterScopeEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer,
                                private juce::ComponentMovementWatcher {
  public:
    explicit MasterScopeEditor(MasterScopeProcessor &);
    ~MasterScopeEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;
    void visibilityChanged() override;

  private:
    void timerCallback() override;
    void componentMovedOrResized(bool, bool) override {}
    void componentPeerChanged() override { visibilityChanged(); }
    void componentVisibilityChanged() override { visibilityChanged(); }
    MasterScopeProcessor &processor;
    AnalysisDisplay display;
    juce::ToggleButton capture{"Capture"}, hold{"Peak hold"};
    juce::TextButton reset{"Reset"}, undo{"Undo"}, redo{"Redo"};
    juce::ComboBox coordinates;
    std::unique_ptr<juce::ParameterAttachment> coordinatesAttachment;
    juce::TooltipWindow tooltips{this, 700};
    MasterScopeProcessor::Metering metering;
    MasterScopeProcessor::IntegratedReading integrated;
    juce::Path phase;
    unsigned displayedGeneration = 0;
    std::array<MasterScopeProcessor::PhaseSample, 4096> samples{};
};
