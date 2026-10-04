#pragma once

#include "../common/pluginAnalysis.h"
#include "../common/pluginState.h"
#include <ahq/audio/spaceLab.h>
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

/// Stereo spatial effects with mono/stereo input. Core is owned exclusively by the audio thread.
class SpaceLabProcessor final : public juce::AudioProcessor {
  public:
    using Values = std::array<float, 10>; // width, haas, leftTilt, rightTilt, invertLeft,
                                          // invertRight, modRate, modDepth, split, mix
    SpaceLabProcessor();
    const juce::String getName() const override;
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void processBlockBypassed(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
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
    void processAudio(juce::AudioBuffer<float> &, bool bypassed);
    ahq::audio::SpaceLab filter;
    bool prepared = false;
    std::atomic<int> currentProgram{0};
    PluginState state;
    PluginAnalysis analysis;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpaceLabProcessor)
};
