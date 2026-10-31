#pragma once
#include "../common/pluginAnalysis.h"
#include "../common/pluginState.h"
#include <ahq/audio/parametricEq.h>

class CurveEqProcessor final : public juce::AudioProcessor {
  public:
    static constexpr size_t fields = 11, count = 24 * fields + 2;
    using Values = std::array<float, count>;
    CurveEqProcessor();
    const juce::String getName() const override { return "AHQ Curve EQ"; }
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
    const juce::String getProgramName(int) override { return "Flat"; }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &b) override { state.getStateInformation(b); }
    void setStateInformation(const void *p, int n) override { state.setStateInformation(p, n); }
    Values readValues() const noexcept {
        Values v{};
        state.fillValues(v);
        return v;
    }
    juce::AudioProcessorValueTreeState &getParameters() noexcept { return parameters; }
    const char *parameterId(size_t i) const noexcept { return ids[i]; }
    static ahq::audio::EqBandParameters bandParameters(const Values &, size_t);
    bool applyUserValues(const Values &v, const juce::String &name) {
        return state.applyUserValues(v, name);
    }
    void recordUserEdit(const Values &v, std::span<const int> indices) {
        state.recordUserEdit(v, indices, "EQ edit");
    }
    juce::String createPresetXml() const { return state.createPresetXml(); }
    bool loadPresetXml(const juce::String &xml) { return state.loadPresetXml(xml, true); }
    bool undo() { return state.undo(); }
    bool redo() { return state.redo(); }
    PluginAnalysis &getAnalysis() noexcept { return analysis; }
    void setDisplayEnabled(bool enabled) { analysis.setSpectrumEnabled(enabled); }
    std::array<std::atomic<float>, 24> dynamicDb{};
    std::atomic<double> displaySampleRate{48000};

  private:
    static std::array<std::string, count> makeIds();
    static std::array<const char *, count> makePointers(const std::array<std::string, count> &);
    static juce::AudioProcessorValueTreeState::ParameterLayout
    createParameterLayout(const std::array<const char *, count> &);
    std::array<std::string, count> idStorage;
    std::array<const char *, count> ids;
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<int> program{0};
    PluginState state;
    PluginAnalysis analysis;
    ahq::audio::ParametricEq eq;
    ahq::audio::LinearSmoother outputGain, wetMix;
    // Fixed scratch size; oversized host buffers are processed in bounded chunks.
    std::array<float, 2048> dryLeft{}, dryRight{};
    unsigned smoothingFrames = 1;
    bool prepared = false;
};
