#pragma once
#include "../common/impulseBank.h"
#include "../common/pluginState.h"
#include <ahq/audio/reverb.h>
class HybridReverbProcessor final : public juce::AudioProcessor {
  public:
    static constexpr std::array<const char *, 7> ids{"early",    "late",  "decay", "damping",
                                                     "predelay", "width", "mix"};
    using Values = std::array<float, ids.size()>;
    HybridReverbProcessor();
    const juce::String getName() const override { return "AHQ Hybrid Reverb"; }
    juce::String description() const {
        return "Convolution early reflections and an eight-line algorithmic tail. IR limit: 12 "
               "seconds.";
    }
    void prepareToPlay(double, int) override;
    void releaseResources() override { prepared = false; }
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 40; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor *createEditor() override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override;
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
    juce::String createPresetXml();
    bool loadPresetXml(const juce::String &);
    bool undo() { return state.undo(); }
    bool redo() { return state.redo(); }
    static constexpr int impulseSlots = 1;
    ImpulseBank &impulseBank() { return impulses; }
    static constexpr bool outputMeter = true;
    std::atomic<float> displayGainDb{0};

  private:
    bool restorePresetXml(const juce::String &, bool notifyHost);
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<int> program{0};
    PluginState state;
    ImpulseBank impulses{1, 12.0, true};
    juce::AudioBuffer<float> scratch{2, 2048}, tail{2, 2048};
    std::array<juce::SmoothedValue<float>, 3> gains;
    std::array<juce::SmoothedValue<float>, 1> delays;
    std::array<juce::AudioBuffer<float>, 1> delayBuffers;
    std::array<int, 1> delayPositions{};
    ahq::audio::Reverb reverb;
    double rate = 48000;
    int channels = 2, chunkSize = 256;
    bool prepared = false;
};
