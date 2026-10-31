#pragma once
#include "pluginAnalysis.h"
#include "pluginState.h"
#include <ahq/audio/synthesizer.h>
/// Shared MIDI/state adapter for the modular and PCM instruments.
class SynthProcessor : public juce::AudioProcessor {
  public:
    using Values = std::vector<float>;
    explicit SynthProcessor(bool pcm);
    const bool pcm;
    const int generatorCount, routeCount, envelopeOffset, filterOffset, lfoOffset, modulationOffset,
        effectsOffset, modeIndex, partsOffset, parameterCount;
    const juce::String getName() const override { return pcm ? "AHQ PCM Station" : "AHQ Modular"; }
    void prepareToPlay(double, int) override;
    void releaseResources() override { prepared = false; }
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 40; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor *createEditor() override;
    int getNumPrograms() override { return pcm ? 64 : 32; }
    int getCurrentProgram() override { return std::max(0, program.load()); }
    int currentFactoryProgram() const noexcept { return program.load(); }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &b) override { state.getStateInformation(b); }
    void setStateInformation(const void *p, int n) override { state.setStateInformation(p, n); }
    juce::AudioProcessorValueTreeState &getParameters() noexcept { return parameters; }
    juce::RangedAudioParameter *parameter(int index) {
        return parameters.getParameter(ids[size_t(index)]);
    }
    const char *parameterId(int index) const noexcept { return ids[size_t(index)]; }
    Values readValues() const {
        Values values(size_t(parameterCount), 0.F);
        state.fillValues(values);
        return values;
    }
    bool applyUserValues(const Values &v, const juce::String &name) {
        return state.applyUserValues(v, name);
    }
    void recordUserEdit(const Values &v, int i) { state.recordUserEdit(v, i, "Value"); }
    juce::String createPresetXml() const { return state.createPresetXml(); }
    bool loadPresetXml(const juce::String &xml) { return state.loadPresetXml(xml, true); }
    bool undo() { return state.undo(); }
    bool redo() { return state.redo(); }
    void loadFactory(int index, bool undoable);
    void loadCombination(int index);
    void audition(int note, bool on) noexcept;
    void panic() noexcept { panicRequested.store(true); }
    void setDisplayEnabled(bool enabled) noexcept { analysis.setSpectrumEnabled(enabled); }
    PluginAnalysis &getAnalysis() noexcept { return analysis; }
    std::atomic<int> displayedVoices{0};

  private:
    static std::vector<std::string> makeIds(bool);
    static std::vector<const char *> pointers(const std::vector<std::string> &);
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void writeProgram(Values &, const ahq::audio::SynthProgram &) const;
    ahq::audio::SynthProgram readProgram(std::span<const float>) const noexcept;
    bool configure(std::span<const float>) noexcept;
    void midi(const juce::uint8 *, int size) noexcept;
    std::vector<std::string> idStorage;
    std::vector<const char *> ids;
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<int> program{0};
    PluginState state;
    ahq::audio::Synthesizer synth;
    PluginAnalysis analysis;
    std::array<float, 128> previous{};
    bool prepared = false, configured = false;
    struct Note {
        int note = 60;
        bool on = false;
    };
    std::array<Note, 256> guiNotes{};
    juce::AbstractFifo guiFifo{256};
    std::atomic<bool> panicRequested{false};
};
