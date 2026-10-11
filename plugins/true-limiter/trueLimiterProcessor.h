#pragma once
#include "../common/pluginAnalysis.h"
#include "../common/pluginState.h"
#include <ahq/audio/trueLimiter.h>
#include <ahq/audio/truePeakMeter.h>
#include <array>
#include <atomic>
#include <cmath>
#include <juce_audio_utils/juce_audio_utils.h>
#include <mutex>
#include <thread>

class TrueLimiterProcessor final : public juce::AudioProcessor {
  public:
    using Values =
        std::array<float, 8>; // ceiling, target, live, gain, lookahead, release, quality, mix
    struct FileAnalysis {
        bool running = false, valid = false, cancelled = false;
        bool targetReached = false, peakLimited = false;
        double inputLufs = -INFINITY, outputLufs = -INFINITY, gainDb = 0;
        double sampleRate = 0;
        unsigned channels = 0;
        juce::String fileName, error;
        Values settings{};
    };
    TrueLimiterProcessor();
    ~TrueLimiterProcessor() override;
    const juce::String getName() const override { return "AHQ True Limiter"; }
    void prepareToPlay(double, int) override;
    void releaseResources() override { prepared = false; }
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    void processBlockBypassed(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.021; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor *createEditor() override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &b) override { state.getStateInformation(b); }
    void setStateInformation(const void *p, int n) override { state.setStateInformation(p, n); }
    Values readValues() const noexcept;
    bool applyUserValues(const Values &v, const juce::String &n) {
        return state.applyUserValues(v, n);
    }
    void recordUserEdit(const Values &v, int i, const juce::String &n) {
        state.recordUserEdit(v, i, n);
    }
    bool undo() { return state.undo(); }
    bool redo() { return state.redo(); }
    bool canUndo() const { return state.canUndo(); }
    bool canRedo() const { return state.canRedo(); }
    juce::String createPresetXml() const { return state.createPresetXml(); }
    bool loadPresetXml(const juce::String &xml) { return state.loadPresetXml(xml, true); }
    juce::AudioProcessorValueTreeState &getParameters() noexcept { return parameters; }
    PluginAnalysis &getAnalysis() noexcept { return displayAnalysis; }
    double inputShortTermLufs() const noexcept { return shortTerm.load(); }
    double outputShortTermLufs() const noexcept { return outputShortTerm.load(); }
    double outputTruePeakDb() const noexcept { return outputPeak.load(); }
    double gainReductionDb() const noexcept { return reduction.load(); }
    /// Message thread only. File work is owned by the processor, not its editor.
    void startFileAnalysis(const juce::File &);
    void cancelFileAnalysis();
    FileAnalysis fileAnalysis() const;
    bool applyFileAnalysis();

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void processAudio(juce::AudioBuffer<float> &, bool bypassed);
    void analyzeFile(juce::File, Values);
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<int> program{0};
    PluginState state;
    PluginAnalysis displayAnalysis;
    ahq::audio::TrueLimiter limiter;
    ahq::audio::LoudnessMeter outputMeter;
    ahq::audio::TruePeakMeter peakMeter;
    std::array<std::vector<float>, 2> dryDelay;
    std::size_t dryPosition = 0;
    bool prepared = false;
    std::atomic<double> shortTerm{-INFINITY}, reduction{0};
    std::atomic<double> outputShortTerm{-INFINITY}, outputPeak{-INFINITY};
    std::atomic<bool> cancelAnalysis{false};
    mutable std::mutex resultMutex;
    FileAnalysis result;
    std::thread worker;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrueLimiterProcessor)
};
