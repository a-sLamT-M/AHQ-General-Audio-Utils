#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <mutex>
#include <thread>
/// Four bounded IR slots. Decoding/generation/encoding occurs on one worker.
/// The audio thread moves prepared buffers into JUCE's wait-free convolution queue.
class ImpulseBank {
  public:
    explicit ImpulseBank(int slots, double maximumSeconds, bool longResponses);
    ~ImpulseBank();
    void requestFile(int slot, const juce::File &); // UI thread
    void requestEmbedded(int slot, juce::MemoryBlock, juce::String name);
    // Audio-safe: quantized length/cutoff/reverse packed into one atomic word; zero disables
    // generation.
    void setGenerated(int slot, int lengthMs, int cutoffHz, bool reverse) noexcept;
    bool prepare(double sampleRate, unsigned blockSize, unsigned channels);
    void process(int slot, juce::dsp::AudioBlock<float>) noexcept;
    void poll(int slot) noexcept { adopt(slot, true); }
    void reset() noexcept;
    void resetSlot(int slot) noexcept { slots[size_t(slot)].convolution->reset(); }
    bool loaded(int slot) const noexcept;
    juce::String status(int slot) const;
    juce::XmlElement
    save(); // non-audio thread; includes PCM so sessions do not depend on file paths
    bool restore(const juce::XmlElement &, bool validateAudio = false);

  private:
    struct Prepared {
        juce::AudioBuffer<float> audio;
        double rate = 44100;
        bool clear = false;
    };
    struct Slot {
        std::unique_ptr<juce::dsp::Convolution> convolution;
        std::atomic<Prepared *> pending{nullptr}, retired{nullptr};
        std::atomic<bool> active{false};
        std::atomic<uint64_t> generated{0};
        uint64_t lastGenerated = 0;
        juce::File requestedFile;
        juce::MemoryBlock requestedBytes, savedBytes;
        juce::String requestedName, name = "Empty", error;
        bool requested = false;
    };
    void run();
    void adopt(int slot, bool realtime) noexcept;
    void flush();
    std::unique_ptr<Prepared> decode(const juce::MemoryBlock &, juce::String &error);
    std::unique_ptr<Prepared> generate(uint64_t) const;
    juce::MemoryBlock encode(const Prepared &) const;
    int count;
    double maximumSeconds;
    juce::dsp::ConvolutionMessageQueue queue{64};
    std::array<Slot, 4> slots;
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false, working = false;
    std::thread worker;
};
