#pragma once
#include "../common/pluginState.h"
#include <ahq/audio/retroTools.h>
class RetroToolsProcessor final : public juce::AudioProcessor {
  public:
    static constexpr std::array<const char *, 9> ids{"ensemble", "rate", "wow", "flutter", "drive",
                                                     "tone",     "bits", "mix", "fault"};
    using Values = std::array<float, ids.size()>;
    RetroToolsProcessor();
    const juce::String getName() const override { return "AHQ Retro Tools"; }
    juce::String description() const {
        return "Ensemble and tape motion. Fault adds random speaker breakup without gain boost.";
    }
    void prepareToPlay(double, int) override;
    void releaseResources() override { prepared = false; }
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor *createEditor() override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &b) override { state.getStateInformation(b); }
    void setStateInformation(const void *, int) override;
    Values readValues() const noexcept {
        Values v{};
        state.fillValues(v);
        return v;
    }
    juce::AudioProcessorValueTreeState &getParameters() noexcept { return parameters; }
    bool applyUserValues(const Values &v, const juce::String &name) {
        return state.applyUserValues(v, name);
    }
    void recordUserEdit(const Values &v, int index) { state.recordUserEdit(v, index, "Value"); }
    juce::String createPresetXml() const { return state.createPresetXml(); }
    bool loadPresetXml(const juce::String &xml) { return restorePresetXml(xml, true); }
    bool undo() { return state.undo(); }
    bool redo() { return state.redo(); }
    static constexpr bool outputMeter = true;
    std::atomic<float> displayGainDb{0};

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    bool restorePresetXml(const juce::String &, bool undoable);
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<int> program{0};
    PluginState state;
    ahq::audio::RetroTools effect;
    bool prepared = false;
};
