#pragma once
#include "../common/pluginAnalysis.h"
#include "../common/pluginState.h"
#include <ahq/audio/loudnessMeter.h>
#include <ahq/audio/stereoMeter.h>
#include <ahq/audio/truePeakMeter.h>
#include <mutex>
#include <thread>

/// Read-only stereo measurement. Audio never waits for the integrated-loudness worker.
class MasterScopeProcessor final : public juce::AudioProcessor {
  public:
    using Values = std::array<float, 3>; // run, peak hold, scope coordinate mode
    struct Metering {
        ahq::audio::LoudnessReading momentary, shortTerm;
        ahq::audio::StereoReading stereo;
        std::array<ahq::audio::PeakReading, 2> peaks{};
        bool peaksValid = false, resetPending = false, loudnessSupported = false;
        unsigned generation = 0;
    };
    struct IntegratedReading {
        ahq::audio::LoudnessReading loudness;
        size_t blocks = 0;
        unsigned generation = 0;
        bool gap = false, allocationFailed = false;
        double loudnessRange = 0;
        bool rangeValid = false;
    };
    struct PhaseSample {
        float left = 0, right = 0;
        unsigned generation = 0;
    };
    /// Disabling the worker is only for deterministic queue-overflow tests.
    explicit MasterScopeProcessor(bool enableWorker = true);
    ~MasterScopeProcessor() override;
    const juce::String getName() const override { return "AHQ Master Scope"; }
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    void processBlockBypassed(juce::AudioBuffer<float> &b, juce::MidiBuffer &m) override {
        processBlock(b, m);
    }
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
    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;
    // UI-only settings. Atomics allow host state capture independently of painting.
    std::atomic<float> spectrumGainDb{0}, scopeGainDb{0}, spectrumSmoothingMs{140},
        spectrumShare{65};
    std::atomic<int> spectrumRange{90}, displayStyle{1}, displayTheme{0},
        spectrumFrequencySmoothing{1};
    std::atomic<unsigned> displayRevision{0};
    std::atomic<int> editorWidth{1180}, editorHeight{780};
    Values readValues() const noexcept;
    juce::AudioProcessorValueTreeState &getParameters() noexcept { return parameters; }
    bool applyUserValues(const Values &v, const juce::String &name) {
        return state.applyUserValues(v, name);
    }
    bool undo() { return state.undo(); }
    bool redo() { return state.redo(); }
    juce::String createPresetXml() const;
    bool loadPresetXml(const juce::String &, bool undoable = true);
    /// Request is coalesced and applied by audio at a block boundary. No UI mutation of DSP
    /// history.
    void requestReset() noexcept { requestedGeneration.fetch_add(1, std::memory_order_release); }
    /// Approximate display snapshots; fields may originate from adjacent audio publications.
    Metering readMetering() const noexcept;
    /// Off-audio only: locks the worker's published result. Gap means integrated is incomplete.
    IntegratedReading readIntegrated() const;
    void setDisplayEnabled(bool) noexcept;
    size_t readPhase(std::span<PhaseSample>) noexcept;
    PluginAnalysis &getAnalysis() noexcept { return analysis; }

  private:
    bool restorePresetXml(const juce::String &, bool notifyHost);
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void stopWorker();
    void workerLoop();
    bool applyReset() noexcept;
    void publishMetering() noexcept;
    struct Event {
        double energy = 0, shortLufs = 0;
        unsigned generation = 0;
        bool shortValid = false;
    };
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<int> program{0};
    PluginState state;
    PluginAnalysis analysis;
    std::array<ahq::audio::LoudnessMeter, 2> loudness;
    ahq::audio::TruePeakMeter peak;
    ahq::audio::StereoMeter stereo;
    std::array<std::atomic<bool>, 2> bankReady{};
    std::atomic<int> retiredBank{-1};
    int activeBank = 0;
    std::atomic<unsigned> requestedGeneration{1}, measurementGeneration{0}, gapGeneration{0};
    unsigned audioGeneration = 0, channels = 2, peakFrames = 0, peakInterval = 4800;
    bool prepared = false, supportsLoudness = false, previousRun = true, previousHold = true;
    std::atomic<bool> pending{false}, supported{false}, displayEnabled{false};
    static constexpr int eventCapacity = 256, phaseCapacity = 4096;
    juce::AbstractFifo eventFifo{eventCapacity}, phaseFifo{phaseCapacity};
    std::array<Event, eventCapacity> events{};
    std::array<PhaseSample, phaseCapacity> phaseSamples{};
    // Snapshot slots: M/S LUFS, L/R RMS, L/R crest, correlation, M/S energy, L/R SP, L/R TP.
    std::array<std::atomic<double>, 13> snapshot{};
    std::atomic<unsigned> flags{0};
    const bool workerEnabled;
    std::atomic<bool> stopping{false};
    std::thread worker;
    mutable std::mutex integratedMutex;
    IntegratedReading integrated;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterScopeProcessor)
};
