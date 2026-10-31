#pragma once
#include "../common/impulseBank.h"
#include "../common/pluginState.h"
#include <ahq/audio/reverb.h>
class CabinetProcessor final : public juce::AudioProcessor {
  public:
    static constexpr std::array<const char *, 21> ids{
        "ir1_enabled",  "ir1_volume", "ir1_pan", "ir1_polarity", "ir1_delay",
        "ir2_enabled",  "ir2_volume", "ir2_pan", "ir2_polarity", "ir2_delay",
        "ir3_enabled",  "ir3_volume", "ir3_pan", "ir3_polarity", "ir3_delay",
        "ir4_enabled",  "ir4_volume", "ir4_pan", "ir4_polarity", "ir4_delay",
        "master_volume"};
    static constexpr size_t masterOutputIndex = 20;
    using Values = std::array<float, ids.size()>;
    CabinetProcessor();
    const juce::String getName() const override { return "AHQ Cabinet"; }
    juce::String description() const {
        return "Four parallel IRs. Mono/stereo files up to 1 second; embedded in project state.";
    }
    void prepareToPlay(double, int) override;
    void releaseResources() override { prepared = false; }
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor *createEditor() override;
    int getNumPrograms() override { return 5; }
    int getCurrentProgram() override { return std::max(0, program.load()); }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;
    void loadFactoryPreset(int);
    juce::String createPresetXml();
    bool loadPresetXml(const juce::String &);
    bool savePresetFile(const juce::File &);
    bool loadPresetFile(const juce::File &);
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
    bool undo() { return state.undo(); }
    bool redo() { return state.redo(); }
    static constexpr int impulseSlots = 4;
    ImpulseBank &impulseBank() { return impulses; }
    static constexpr bool outputMeter = true;
    std::atomic<float> displayGainDb{0};

  private:
    bool restorePresetXml(const juce::String &, bool notifyHost);
    Values routingValues(int) const noexcept;
    void applyMasterVolume(juce::AudioBuffer<float> &) noexcept;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<int> program{0};
    PluginState state;
    ImpulseBank impulses{4, 1.0, true};
    juce::AudioBuffer<float> scratch{2, 2048}, tail{2, 2048};
    std::array<juce::SmoothedValue<float>, 8> gains;
    juce::SmoothedValue<float> masterGain;
    std::array<juce::SmoothedValue<float>, 4> delays;
    std::array<juce::AudioBuffer<float>, 4> delayBuffers;
    std::array<int, 4> delayPositions{};
    std::array<bool, 4> suspended{};
    double rate = 48000;
    int channels = 2, chunkSize = 256;
    bool prepared = false;
};
