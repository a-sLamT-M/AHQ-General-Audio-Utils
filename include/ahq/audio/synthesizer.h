#pragma once
#include "parameterRange.h"
#include "reverb.h"
#include <array>
#include <memory>
#include <span>
#include <vector>
namespace ahq::audio {
struct SynthEnvelope {
    double attackMs = 5, decayMs = 200, sustain = .7, releaseMs = 400;
    static constexpr ParameterRange timeRange{.1F, 10000}, sustainRange{0, 1};
    [[nodiscard]] bool isValid() const noexcept;
    bool operator==(const SynthEnvelope &) const = default;
};
struct SynthGenerator {
    int source = 0; // Off, analog, wavetable, PCM, noise.
    int waveform = 2, sample = 0, unison = 1;
    double transpose = 0, fineCents = 0, level = .5, pan = 0, shape = .5, phaseMod = 0;
    static constexpr ParameterRange sourceRange{0, 4, 1}, waveformRange{0, 3, 1},
        sampleRange{0, 15, 1}, unisonRange{1, 3, 1}, transposeRange{-48, 48}, fineRange{-100, 100},
        unitRange{0, 1}, panRange{-1, 1}, phaseModRange{0, 4};
    [[nodiscard]] bool isValid() const noexcept;
    bool operator==(const SynthGenerator &) const = default;
};
struct SynthModulation {
    int source =
        0; // Off, LFO 1, LFO 2, amp envelope, mod envelope, velocity, key, wheel, pressure.
    int target = 0; // Pitch, cutoff, level 1-4, pan 1-4, shape 1-4, phase modulation 1-4.
    double amount = 0;
    static constexpr ParameterRange sourceRange{0, 8, 1}, targetRange{0, 17, 1}, amountRange{-1, 1};
    [[nodiscard]] bool isValid() const noexcept;
    bool operator==(const SynthModulation &) const = default;
};
struct SynthProgram {
    std::array<SynthGenerator, 4> generators{};
    std::array<SynthEnvelope, 2> envelopes{};
    std::array<SynthModulation, 8> modulation{};
    double cutoffHz = 10000, resonance = .1, filterEnvelope = 0, keyTrack = .5;
    int filterMode = 0; // Low-pass, high-pass, band-pass, bypass.
    std::array<double, 2> lfoRateHz{.5, .13}, lfoBeats{4, 8};
    std::array<bool, 2> lfoSync{false, false};
    std::array<int, 2> lfoShape{0, 0};
    static constexpr ParameterRange cutoffRange{20, 20000}, resonanceRange{0, .98F},
        filterEnvelopeRange{-4, 4}, keyTrackRange{0, 1}, filterModeRange{0, 3, 1},
        lfoRateRange{.01F, 30}, lfoBeatsRange{.0625F, 64}, lfoShapeRange{0, 3, 1};
    [[nodiscard]] bool isValid() const noexcept;
    bool operator==(const SynthProgram &) const = default;
};
struct SynthPart {
    bool enabled = false;
    int channel = 0, lowKey = 0, highKey = 127, transpose = 0;
    double level = 1, pan = 0;
    [[nodiscard]] bool isValid() const noexcept;
    bool operator==(const SynthPart &) const = default;
};
struct SynthEffects {
    double drive = 0, chorus = 0, reverb = .15, decaySeconds = 2, outputDb = -12;
    int polyphony = 32;
    [[nodiscard]] bool isValid() const noexcept;
    bool operator==(const SynthEffects &) const = default;
};
/// Fixed 64-voice synthesis with four generators per voice, two envelopes, two part LFOs,
/// eight modulation routes and eight layer/split parts. Original PCM is synthesized once
/// outside audio processing, then played by interpolation. No proprietary ROM or presets.
class Synthesizer {
  public:
    Synthesizer();
    ~Synthesizer();
    [[nodiscard]] bool prepare(double sampleRate) noexcept;
    [[nodiscard]] bool setPart(size_t, const SynthPart &, const SynthProgram &) noexcept;
    [[nodiscard]] bool setEffects(const SynthEffects &) noexcept;
    void setTempo(double bpm) noexcept;
    void noteOn(int channel, int note, float velocity) noexcept;
    void noteOff(int channel, int note) noexcept;
    void controller(int channel, int number, float value) noexcept;
    void pitchBend(int channel, float bipolar) noexcept;
    void pressure(int channel, float value) noexcept;
    void allNotesOff(bool immediate = false) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process(std::span<float> left, std::span<float> right) noexcept;
    [[nodiscard]] unsigned activeVoices() const noexcept { return activeCount_; }
    static constexpr size_t pcmProgramCount = 64, modularProgramCount = 32, combiCount = 16;
    static SynthProgram pcmProgram(size_t);
    static SynthProgram modularProgram(size_t);
    static const char *pcmProgramName(size_t) noexcept;
    static const char *modularProgramName(size_t) noexcept;
    static const char *sampleName(size_t) noexcept;
    static const char *combiName(size_t) noexcept;
    static std::array<int, 8> combiPrograms(size_t) noexcept;
    static SynthPart combiPart(size_t combi, size_t part) noexcept;

  private:
    struct Bank;
    struct Envelope {
        double value = 0, step = 0, target = 0;
        unsigned remaining = 0;
        int stage = 0;
        void start(const SynthEnvelope &, double rate) noexcept;
        void release(const SynthEnvelope &, double rate) noexcept;
        double next(const SynthEnvelope &, double rate) noexcept;
    };
    struct Voice {
        bool active = false, released = false, heldByPedal = false;
        int channel = 1, note = 60;
        size_t part = 0;
        uint64_t age = 0;
        double velocity = 0;
        std::array<Envelope, 2> envelopes;
        std::array<std::array<double, 3>, 4> position{};
        std::array<double, 4> increment{}, pitchKey{}, panKey{}, panL{}, panR{}, gain{}, shape{},
            fm{};
        std::array<double, 2> ic1{}, ic2{}, lastOutput{}, stealTail{};
        double a1 = 1, a2 = 0, a3 = 0, k = 1, cutoffKey = -1, resonanceKey = -1;
        unsigned controlFrame = 0, stealFrames = 0;
        uint32_t random = 1;
    };
    void updateVoice(Voice &, double ampEnvelope, double modEnvelope) noexcept;
    double oscillator(Voice &, size_t generator, int unison, double phaseMod) noexcept;
    double lfo(size_t part, size_t index) const noexcept;
    std::shared_ptr<const Bank> bank_;
    std::array<Voice, 64> voices_{};
    std::array<size_t, 64> active_{};
    unsigned activeCount_ = 0;
    uint64_t age_ = 0;
    std::array<SynthPart, 8> parts_{};
    std::array<SynthProgram, 8> programs_{};
    std::array<std::array<double, 2>, 8> lfoPhase_{};
    std::array<float, 16> bends_{}, wheels_{}, pressures_{};
    std::array<bool, 16> sustain_{};
    SynthEffects effects_;
    Reverb reverb_;
    std::array<std::vector<float>, 2> chorusBuffer_;
    size_t chorusPosition_ = 0;
    double chorusPhase_ = 0, rate_ = 0, bpm_ = 120;
    LinearSmoother output_, drive_, chorus_;
};
} // namespace ahq::audio
