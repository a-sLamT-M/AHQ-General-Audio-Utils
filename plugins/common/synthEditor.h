#pragma once
#include "analysisDisplay.h"
#include "parameterControl.h"
#include "synthProcessor.h"
#include "ui/parameterGroups.h"
#include "ui/presetBar.h"
#include <juce_audio_utils/juce_audio_utils.h>
class SynthEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer,
                          private juce::ComponentMovementWatcher {
  public:
    explicit SynthEditor(SynthProcessor &);
    ~SynthEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;
    void visibilityChanged() override;
    int getControlParameterIndex(juce::Component &c) override { return controlParameterIndex(c); }
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &) override;

  private:
    struct Control {
        int parameter = 0;
        juce::Label label;
        std::unique_ptr<juce::Component> component;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        bool compact = false;
    };
    void rebuild();
    void addControl(int index, bool compact = false);
    void place(size_t control, juce::Rectangle<int>, bool showLabel = true);
    int keyAt(juce::Point<float>) const;
    void playKey(int);
    void timerCallback() override;
    void componentMovedOrResized(bool, bool) override {}
    void componentPeerChanged() override { visibilityChanged(); }
    void componentVisibilityChanged() override { visibilityChanged(); }
    SynthProcessor &processor;
    ahq::ui::PluginTheme appearance{*this, processor.getName()};
    ahq::ui::PresetBar presetBar{processor, appearance};
    int displayedProgram = -1;
    AnalysisDisplay display;
    juce::ComboBox presets, page, generator, combination, mode;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;
    juce::TextButton undo{"Undo"}, redo{"Redo"}, panic{"Panic"};
    std::vector<std::unique_ptr<Control>> controls;
    std::vector<ahq::ui::ParameterGroup> groups;
    SynthProcessor::Values before;
    bool dragging = false;
    int soundingKey = -1;
    juce::Rectangle<int> content, spectrum;
    juce::Rectangle<float> keyboard;
    std::array<juce::Rectangle<float>, 24> keyBounds;
    std::array<bool, 24> blackKeys{};
    juce::Label status;
};
