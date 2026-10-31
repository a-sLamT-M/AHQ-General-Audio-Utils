#pragma once
#include "../common/pluginState.h"
#include <ahq/audio/volumeEnvelope.h>

class VolumeEnvelopeProcessor final : public juce::AudioProcessor {
  public:
    static constexpr std::array<const char *, 18> ids{
        "mode",  "beats",  "threshold", "mix",    "time1",  "time2",  "time3",  "time4",  "time5",
        "time6", "level0", "level1",    "level2", "level3", "level4", "level5", "level6", "level7"};
    using Values = std::array<float, ids.size()>;
    VolumeEnvelopeProcessor();
    const juce::String getName() const override { return "AHQ Volume Envelope"; }
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
    int getNumPrograms() override { return 4; }
    int getCurrentProgram() override { return std::max(0, program.load()); }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &b) override { state.getStateInformation(b); }
    void setStateInformation(const void *p, int n) override { state.setStateInformation(p, n); }
    Values readValues() const noexcept {
        Values v{};
        state.fillValues(v);
        return v;
    }
    juce::AudioProcessorValueTreeState &getParameters() noexcept { return parameters; }
    bool applyUserValues(const Values &v, const juce::String &name) {
        return state.applyUserValues(v, name);
    }
    void recordUserEdit(const Values &v, std::span<const int> indices) {
        state.recordUserEdit(v, indices, "Envelope edit");
    }
    juce::String createPresetXml() const { return state.createPresetXml(); }
    bool loadPresetXml(const juce::String &xml) { return state.loadPresetXml(xml, true); }
    bool undo() { return state.undo(); }
    bool redo() { return state.redo(); }
    std::atomic<float> displayedPhase{0}, displayedGain{1}, displayedBpm{120};

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<int> program{0};
    PluginState state;
    ahq::audio::VolumeEnvelope envelope;
    bool prepared = false;
};
