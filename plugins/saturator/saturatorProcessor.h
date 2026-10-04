#pragma once

#include "../common/pluginAnalysis.h"
#include "../common/pluginState.h"
#include <ahq/audio/saturator.h>
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

/// Latency-aligned mono/stereo digital saturation. DSP belongs exclusively to the host audio
/// thread.
/// Always reports 24 samples. A quality change prepares one inactive fixed engine and replays
/// at most 512 input frames once, then fades for 5 ms. During a fade, requests are deferred until
/// the next block after completion; stable processing runs one engine. Both wet paths are padded
/// to 24 samples. Mix and host bypass share one continuously advancing 24-sample dry history.
class SaturatorProcessor final : public juce::AudioProcessor {
  public:
    using Values = std::array<float, 6>; // model, drive dB, bias, tone, quality, mix
    SaturatorProcessor();
    const juce::String getName() const override;
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void processBlockBypassed(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    /// Diagnostics for bounded quality switching; increments on engine prepare, never each frame.
    unsigned getQualityPreparationCount() const noexcept { return qualityPreparations.load(); }
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor *createEditor() override;
    int getNumPrograms() override;
    int getCurrentProgram() override { return std::max(0, currentProgram.load()); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;

    /// Lock-free parameter and meter snapshots; valid from the message thread.
    Values readValues() const noexcept;
    /// Consumes approximate block peaks for display, not an exact measurement.
    /// Concurrent audio writes may carry a peak to the next frame or miss a transient.
    std::array<float, 2> readLevels() noexcept;
    /// Message-thread spectrum consumer. Sampling is bounded and may drop on overflow.
    void setSpectrumEnabled(bool enabled) noexcept;
    bool readSpectrum(std::span<float> left, std::span<float> right) noexcept;
    double getSpectrumSampleRate() const noexcept { return analysis.getSpectrumSampleRate(); }
    PluginAnalysis &getAnalysis() noexcept { return analysis; }
    /// Validated preset XML. Invalid input never changes parameters. Call off audio thread.
    juce::String createPresetXml() const;
    bool loadPresetXml(const juce::String &, bool undoable = true);
    /// User operations require the message thread. Each call is one undo transaction.
    bool applyUserValues(const Values &, const juce::String &transaction);
    void recordUserEdit(const Values &before, int parameterIndex, const juce::String &transaction);
    bool loadFactoryPreset(int index);
    bool undo();
    bool redo();
    bool canUndo() const { return state.canUndo(); }
    bool canRedo() const { return state.canRedo(); }
    juce::AudioProcessorValueTreeState &getParameters() noexcept { return parameters; }

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static bool validateValues(std::span<const float>) noexcept;
    juce::AudioProcessorValueTreeState parameters;
    struct Engine {
        ahq::audio::Saturator core;
        std::array<std::array<float, 24>, 2> padding{};
        size_t position = 0;
        int quality = 1;
    };
    void processAudio(juce::AudioBuffer<float> &, bool bypassed);
    bool initialiseEngine(int index, int quality, const Values &values) noexcept;
    void startQualityChange(int quality, const Values &values) noexcept;
    std::array<float, 2> processEngine(int index, std::array<float, 2> input) noexcept;
    std::array<Engine, 2> engines;
    std::array<std::array<float, 512>, 2> inputHistory{};
    std::array<std::array<float, 24>, 2> dryHistory{};
    size_t inputPosition = 0, inputCount = 0, dryPosition = 0;
    int activeEngine = 0;
    std::uint32_t fadePosition = 0, fadeFrames = 1;
    bool fading = false;
    double currentSampleRate = 48000;
    ahq::audio::LinearSmoother mixSmoother;
    Values lastValues{0, 0, 0, 0, 1, 1};
    std::atomic<unsigned> qualityPreparations{0};
    bool prepared = false;
    std::atomic<int> currentProgram{0};
    PluginState state;
    PluginAnalysis analysis;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SaturatorProcessor)
};
