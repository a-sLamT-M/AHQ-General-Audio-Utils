#pragma once
#include "../common/analysisDisplay.h"
#include "../common/parameterControl.h"
#include "../common/ui/presetBar.h"
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
    int getControlParameterIndex(juce::Component &c) override { return controlParameterIndex(c); }

  private:
    void timerCallback() override;
    void paintPhase(juce::Graphics &, juce::Rectangle<float>, juce::Colour);
    void paintWaveform(juce::Graphics &, juce::Rectangle<float>, juce::Colour);
    void paintHistory(juce::Graphics &, juce::Rectangle<float>, juce::Colour);
    void componentMovedOrResized(bool, bool) override {}
    void componentPeerChanged() override { visibilityChanged(); }
    void componentVisibilityChanged() override { visibilityChanged(); }
    MasterScopeProcessor &processor;
    ahq::ui::PluginTheme appearance{*this, processor.getName()};
    ahq::ui::PresetBar presetBar{processor, appearance};
    AnalysisDisplay display;
    juce::ToggleButton capture{"Capture"}, hold{"Peak hold"};
    juce::TextButton reset{"Reset"}, undo{"Undo"}, redo{"Redo"}, details{"Details"};
    juce::ComboBox coordinates;
    juce::ComboBox style, range, theme;
    juce::Slider spectrumGain, scopeGain;
    juce::Label spectrumGainLabel, scopeGainLabel;
    std::unique_ptr<juce::ParameterAttachment> coordinatesAttachment;
    juce::TooltipWindow tooltips{this, 700};
    MasterScopeProcessor::Metering metering;
    MasterScopeProcessor::IntegratedReading integrated;
    juce::Rectangle<float> spectrumBounds, phaseBounds, waveformBounds, historyBounds;
    size_t sampleCount = 0, historyPosition = 0, historyCount = 0;
    static constexpr size_t historyCapacity = 1800; // 60 seconds at 30 Hz, GUI only.
    std::array<float, historyCapacity> momentaryHistory{}, shortHistory{};
    std::array<double, historyCapacity> historyTimes{};
    double lastHistoryTime = 0;
    unsigned displayedGeneration = 0;
    unsigned displayRevision = 0;
    std::array<MasterScopeProcessor::PhaseSample, 4096> samples{};
};
