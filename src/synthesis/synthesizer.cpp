#include "ahq/audio/synthesizer.h"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace ahq::audio {
namespace {
constexpr double pi = std::numbers::pi;
double wrap(double phase) noexcept { return phase - std::floor(phase); }
double polyBlep(double t, double dt) noexcept {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1;
    }
    if (t > 1 - dt) {
        t = (t - 1) / dt;
        return t * t + t + t + 1;
    }
    return 0;
}
float randomSample(uint32_t &state) noexcept {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return float(double(state) / 2147483648. - 1);
}
} // namespace
struct Synthesizer::Bank {
    struct Sample {
        std::array<std::vector<float>, 6> mip;
        bool loop = false;
    };
    std::array<Sample, 16> samples;
    std::array<float, 2049> sine{};
    std::array<std::array<std::array<float, 2049>, 4>, 5> waves{};
    Bank() {
        for (size_t i = 0; i < sine.size(); ++i)
            sine[i] = float(std::sin(2 * pi * double(i) / 2048));
        for (size_t mip = 0; mip < waves.size(); ++mip)
            for (size_t wave = 0; wave < 4; ++wave) {
                const int harmonics = std::max(1, 16 >> int(mip));
                double peak = 0;
                for (size_t i = 0; i < 2048; ++i) {
                    const double phase = 2 * pi * double(i) / 2048;
                    double value = 0;
                    for (int h = 1; h <= harmonics; ++h) {
                        double amplitude = 0;
                        switch (wave) {
                        case 0:
                            amplitude = h == 1 ? 1 : 0;
                            break;
                        case 1:
                            amplitude = 1. / h;
                            break;
                        case 2:
                            amplitude = h % 2 == 1 ? 1. / h : 0;
                            break;
                        case 3:
                            amplitude =
                                h == 1 || h == 2 || h == 4 || h == 8 ? 1. / std::sqrt(h) : 0;
                            break;
                        }
                        value += amplitude * std::sin(phase * h);
                    }
                    waves[mip][wave][i] = float(value);
                    peak = std::max(peak, std::abs(value));
                }
                for (auto &value : waves[mip][wave])
                    value /= float(std::max(peak, .001));
                waves[mip][wave][2048] = waves[mip][wave][0];
            }
        std::array<double, 31> halfBand{};
        double sum = 0;
        for (int i = 0; i < 31; ++i) {
            const int x = i - 15;
            halfBand[size_t(i)] = (x == 0 ? .45 : std::sin(pi * .45 * x) / (pi * x)) *
                                  (.5 + .5 * std::cos(pi * x / 16));
            sum += halfBand[size_t(i)];
        }
        for (auto &c : halfBand)
            c /= sum;
        for (size_t instrument = 0; instrument < samples.size(); ++instrument) {
            auto &sample = samples[instrument];
            sample.loop =
                instrument < 4 || instrument == 6 || (instrument >= 9 && instrument <= 12);
            const size_t length = sample.loop ? 3360 : 132300;
            sample.mip[0].resize(length);
            uint32_t random = 12347 + uint32_t(instrument) * 971;
            double peak = 0;
            for (size_t i = 0; i < length; ++i) {
                const double t = double(i) / 44100, phase = 2 * pi * double(i) / 168;
                double value = 0;
                for (int h = 1; h <= 24; ++h) {
                    double amplitude = 0, decay = 0, stretch = 1;
                    switch (instrument) {
                    case 0:
                        amplitude = h == 1 ? 1 : 0;
                        break;
                    case 1:
                        amplitude = h % 2 ? 1. / (h * h) : 0;
                        break;
                    case 2:
                        amplitude = 1. / h;
                        break;
                    case 3:
                        amplitude = h % 2 ? 1. / h : 0;
                        break;
                    case 4:
                        amplitude = 1. / std::pow(h, 1.3);
                        decay = 1.2 + h * .4;
                        stretch = std::sqrt(1 + .00012 * h * h);
                        break;
                    case 5:
                        amplitude = h == 1 ? 1 : h == 2 ? .5 : h == 3 ? .25 : h == 7 ? .12 : 0;
                        decay = .9 + h * .55;
                        break;
                    case 6:
                        amplitude = h == 1   ? .8
                                    : h == 2 ? .7
                                    : h == 3 ? .25
                                    : h == 4 ? .5
                                    : h == 8 ? .2
                                             : 0;
                        break;
                    case 7:
                        amplitude = h < 8 ? 1. / h : 0;
                        stretch = 1 + .027 * h;
                        decay = .8 + h * .32;
                        break;
                    case 8:
                        amplitude = h == 1 ? 1 : h == 3 ? .3 : h == 6 ? .12 : 0;
                        decay = 3 + h;
                        break;
                    case 9:
                        amplitude = 1. / std::pow(h, 1.2);
                        break;
                    case 10:
                        amplitude = (.15 + std::exp(-std::pow((h * 262.5 - 800) / 300, 2)) +
                                     .7 * std::exp(-std::pow((h * 262.5 - 2200) / 500, 2))) /
                                    h;
                        break;
                    case 11:
                        amplitude = 1. / std::pow(h, .8);
                        break;
                    case 12:
                        amplitude = h == 1 ? 1 : h == 2 ? .35 : h == 3 ? .15 : 0;
                        break;
                    case 13:
                        amplitude = 1. / h;
                        decay = 4 + h * .8;
                        break;
                    default:
                        break;
                    }
                    if (amplitude != 0)
                        value += amplitude * std::sin(phase * h * stretch) *
                                 (decay == 0 ? 1 : std::exp(-t * decay));
                }
                if (instrument == 14)
                    value = std::sin(2 * pi * (50 * t + 5 * (1 - std::exp(-t * 30)))) *
                            std::exp(-t * 8);
                if (instrument == 15)
                    value = (randomSample(random) * .8 + std::sin(2 * pi * 180 * t) * .2) *
                            std::exp(-t * 14);
                if (!sample.loop)
                    value *=
                        std::min(1., double(i) / 32) * std::min(1., double(length - 1 - i) / 512);
                sample.mip[0][i] = float(value);
                peak = std::max(peak, std::abs(value));
            }
            for (auto &value : sample.mip[0])
                value *= float(.8 / std::max(peak, .001));
            for (size_t mip = 1; mip < sample.mip.size(); ++mip) {
                const auto &input = sample.mip[mip - 1];
                auto &output = sample.mip[mip];
                output.resize(input.size() / 2);
                for (size_t i = 0; i < output.size(); ++i) {
                    double value = 0;
                    for (int tap = 0; tap < 31; ++tap) {
                        int index = int(i * 2) + tap - 15;
                        if (sample.loop)
                            index = (index + int(input.size())) % int(input.size());
                        if (index >= 0 && index < int(input.size()))
                            value += input[size_t(index)] * halfBand[size_t(tap)];
                    }
                    output[i] = float(value);
                }
            }
        }
    }
    double sin(double phase) const noexcept {
        const double x = wrap(phase) * 2048;
        const auto i = size_t(x);
        return sine[i] + (x - double(i)) * (sine[i + 1] - sine[i]);
    }
};
bool SynthEnvelope::isValid() const noexcept {
    return timeRange.contains(attackMs) && timeRange.contains(decayMs) &&
           sustainRange.contains(sustain) && timeRange.contains(releaseMs);
}
bool SynthGenerator::isValid() const noexcept {
    return sourceRange.contains(source) && waveformRange.contains(waveform) &&
           sampleRange.contains(sample) && unisonRange.contains(unison) &&
           transposeRange.contains(transpose) && fineRange.contains(fineCents) &&
           unitRange.contains(level) && panRange.contains(pan) && unitRange.contains(shape) &&
           phaseModRange.contains(phaseMod);
}
bool SynthModulation::isValid() const noexcept {
    return sourceRange.contains(source) && targetRange.contains(target) &&
           amountRange.contains(amount);
}
bool SynthProgram::isValid() const noexcept {
    for (const auto &g : generators)
        if (!g.isValid())
            return false;
    for (const auto &e : envelopes)
        if (!e.isValid())
            return false;
    for (const auto &m : modulation)
        if (!m.isValid())
            return false;
    for (size_t i = 0; i < 2; ++i)
        if (!lfoRateRange.contains(lfoRateHz[i]) || !lfoBeatsRange.contains(lfoBeats[i]) ||
            !lfoShapeRange.contains(lfoShape[i]))
            return false;
    return cutoffRange.contains(cutoffHz) && resonanceRange.contains(resonance) &&
           filterEnvelopeRange.contains(filterEnvelope) && keyTrackRange.contains(keyTrack) &&
           filterModeRange.contains(filterMode);
}
bool SynthPart::isValid() const noexcept {
    return channel >= 0 && channel <= 16 && lowKey >= 0 && highKey <= 127 && lowKey <= highKey &&
           transpose >= -24 && transpose <= 24 && std::isfinite(level) && level >= 0 &&
           level <= 2 && std::isfinite(pan) && pan >= -1 && pan <= 1;
}
bool SynthEffects::isValid() const noexcept {
    return SynthGenerator::unitRange.contains(drive) &&
           SynthGenerator::unitRange.contains(chorus) &&
           SynthGenerator::unitRange.contains(reverb) &&
           ReverbParameters::decayRange.contains(decaySeconds) && std::isfinite(outputDb) &&
           outputDb >= -48 && outputDb <= 12 && polyphony >= 1 && polyphony <= 64;
}
void Synthesizer::Envelope::start(const SynthEnvelope &p, double rate) noexcept {
    value = 0;
    stage = 1;
    remaining = std::max(1U, unsigned(p.attackMs * .001 * rate));
    target = 1;
    step = 1. / remaining;
}
void Synthesizer::Envelope::release(const SynthEnvelope &p, double rate) noexcept {
    if (stage == 0 || stage == 4)
        return;
    stage = 4;
    remaining = std::max(1U, unsigned(p.releaseMs * .001 * rate));
    target = 0;
    step = -value / remaining;
}
double Synthesizer::Envelope::next(const SynthEnvelope &p, double rate) noexcept {
    if (stage == 0)
        return 0;
    if (stage == 3) {
        value += .01 * (p.sustain - value);
        return value;
    }
    value += step;
    if (--remaining == 0) {
        value = target;
        if (stage == 1) {
            stage = 2;
            target = p.sustain;
            remaining = std::max(1U, unsigned(p.decayMs * .001 * rate));
            step = (target - value) / remaining;
        } else if (stage == 2)
            stage = 3;
        else {
            stage = 0;
            value = 0;
        }
    }
    return value;
}
Synthesizer::Synthesizer() {
    static const auto shared = std::make_shared<const Bank>();
    bank_ = shared;
    parts_[0].enabled = true;
    programs_[0] = modularProgram(0);
}
Synthesizer::~Synthesizer() = default;
bool Synthesizer::prepare(double rate) noexcept {
    if (!std::isfinite(rate) || rate < 8000 || rate > 384000)
        return false;
    try {
        for (auto &c : chorusBuffer_)
            c.resize(size_t(std::ceil(rate * .03)) + 2);
    } catch (...) {
        return false;
    }
    if (!reverb_.prepare(rate))
        return false;
    rate_ = rate;
    (void)setEffects(effects_);
    reset();
    return true;
}
bool Synthesizer::setPart(size_t index, const SynthPart &part,
                          const SynthProgram &program) noexcept {
    if (index >= parts_.size() || !part.isValid() || !program.isValid())
        return false;
    parts_[index] = part;
    programs_[index] = program;
    return true;
}
bool Synthesizer::setEffects(const SynthEffects &p) noexcept {
    if (rate_ == 0 || !p.isValid())
        return false;
    effects_ = p;
    while (activeCount_ > unsigned(p.polyphony)) {
        unsigned oldest = 0;
        for (unsigned i = 1; i < activeCount_; ++i)
            if (voices_[active_[i]].age < voices_[active_[oldest]].age)
                oldest = i;
        voices_[active_[oldest]].active = false;
        active_[oldest] = active_[--activeCount_];
    }
    const auto frames = unsigned(std::ceil(rate_ * .005));
    output_.setTarget(std::pow(10., p.outputDb / 20), frames);
    drive_.setTarget(p.drive, frames);
    chorus_.setTarget(p.chorus, frames);
    ReverbParameters room;
    room.mix = p.reverb;
    room.decaySeconds = p.decaySeconds;
    room.preDelayMs = 15;
    return reverb_.setParameters(room);
}
void Synthesizer::setTempo(double bpm) noexcept {
    if (std::isfinite(bpm) && bpm >= 1 && bpm <= 1000)
        bpm_ = bpm;
}
void Synthesizer::reset() noexcept {
    activeCount_ = 0;
    age_ = 0;
    voices_ = {};
    lfoPhase_ = {};
    bends_.fill(0);
    wheels_.fill(0);
    pressures_.fill(0);
    sustain_.fill(false);
    for (auto &c : chorusBuffer_)
        std::fill(c.begin(), c.end(), 0.F);
    chorusPosition_ = 0;
    chorusPhase_ = 0;
    output_.reset(std::pow(10., effects_.outputDb / 20));
    drive_.reset(effects_.drive);
    chorus_.reset(effects_.chorus);
    reverb_.reset();
}
void Synthesizer::noteOn(int channel, int note, float velocity) noexcept {
    if (rate_ == 0 || channel < 1 || channel > 16 || note < 0 || note > 127 ||
        !std::isfinite(velocity))
        return;
    if (velocity <= 0) {
        noteOff(channel, note);
        return;
    }
    for (size_t part = 0; part < parts_.size(); ++part) {
        const auto &p = parts_[part];
        if (!p.enabled || note < p.lowKey || note > p.highKey ||
            (p.channel != 0 && p.channel != channel))
            continue;
        size_t selected = voices_.size();
        if (activeCount_ < unsigned(effects_.polyphony))
            for (size_t i = 0; i < voices_.size(); ++i)
                if (!voices_[i].active) {
                    selected = i;
                    break;
                }
        bool stealing = selected == voices_.size();
        if (stealing) {
            selected = active_[0];
            for (unsigned i = 1; i < activeCount_; ++i)
                if (voices_[active_[i]].age < voices_[selected].age)
                    selected = active_[i];
        }
        auto &v = voices_[selected];
        const auto previous = v.lastOutput;
        v = {};
        v.active = true;
        v.channel = channel;
        v.note = note;
        v.velocity = std::clamp(double(velocity), 0., 1.);
        v.part = part;
        v.age = ++age_;
        v.random = 1231U + uint32_t(age_) * 971U;
        v.pitchKey.fill(-10000);
        v.panKey.fill(-10000);
        for (size_t e = 0; e < 2; ++e)
            v.envelopes[e].start(programs_[part].envelopes[e], rate_);
        if (stealing) {
            v.stealTail = previous;
            v.stealFrames = 64;
        } else
            active_[activeCount_++] = selected;
    }
}
void Synthesizer::noteOff(int channel, int note) noexcept {
    if (channel < 1 || channel > 16)
        return;
    for (unsigned i = 0; i < activeCount_; ++i) {
        auto &v = voices_[active_[i]];
        if (v.channel != channel || v.note != note || v.released)
            continue;
        v.released = true;
        v.heldByPedal = sustain_[size_t(channel - 1)];
        if (!v.heldByPedal)
            for (size_t e = 0; e < 2; ++e)
                v.envelopes[e].release(programs_[v.part].envelopes[e], rate_);
    }
}
void Synthesizer::controller(int channel, int number, float value) noexcept {
    if (channel < 1 || channel > 16 || !std::isfinite(value))
        return;
    const auto c = size_t(channel - 1);
    value = std::clamp(value, 0.F, 1.F);
    if (number == 1)
        wheels_[c] = value;
    else if (number == 64) {
        sustain_[c] = value >= .5F;
        if (!sustain_[c])
            for (unsigned i = 0; i < activeCount_; ++i) {
                auto &v = voices_[active_[i]];
                if (v.channel == channel && v.heldByPedal) {
                    v.heldByPedal = false;
                    for (size_t e = 0; e < 2; ++e)
                        v.envelopes[e].release(programs_[v.part].envelopes[e], rate_);
                }
            }
    } else if (number == 120 || number == 123) {
        for (unsigned i = 0; i < activeCount_; ++i) {
            auto &v = voices_[active_[i]];
            if (v.channel == channel) {
                v.released = true;
                v.heldByPedal = false;
                for (size_t e = 0; e < 2; ++e)
                    v.envelopes[e].release(programs_[v.part].envelopes[e], rate_);
                if (number == 120)
                    v.envelopes[0].stage = 0;
            }
        }
    }
}
void Synthesizer::pitchBend(int channel, float value) noexcept {
    if (channel >= 1 && channel <= 16 && std::isfinite(value))
        bends_[size_t(channel - 1)] = std::clamp(value, -1.F, 1.F);
}
void Synthesizer::pressure(int channel, float value) noexcept {
    if (channel >= 1 && channel <= 16 && std::isfinite(value))
        pressures_[size_t(channel - 1)] = std::clamp(value, 0.F, 1.F);
}
void Synthesizer::allNotesOff(bool immediate) noexcept {
    if (immediate) {
        activeCount_ = 0;
        for (auto &v : voices_)
            v.active = false;
        return;
    }
    for (int channel = 1; channel <= 16; ++channel)
        controller(channel, 123, 0);
}
double Synthesizer::lfo(size_t part, size_t index) const noexcept {
    const double phase = lfoPhase_[part][index];
    switch (programs_[part].lfoShape[index]) {
    case 0:
        return bank_->sin(phase);
    case 1:
        return 1 - 4 * std::abs(phase - .5);
    case 2:
        return 2 * phase - 1;
    default:
        return phase < .5 ? 1 : -1;
    }
}
void Synthesizer::updateVoice(Voice &v, double amp, double env) noexcept {
    const auto &p = programs_[v.part];
    const auto &part = parts_[v.part];
    const double sources[]{0,
                           lfo(v.part, 0),
                           lfo(v.part, 1),
                           amp,
                           env,
                           v.velocity,
                           (v.note - 60) / 60.,
                           wheels_[size_t(v.channel - 1)],
                           pressures_[size_t(v.channel - 1)]};
    std::array<double, 18> modulation{};
    for (const auto &route : p.modulation)
        if (route.source != 0 && route.amount != 0)
            modulation[size_t(route.target)] += sources[route.source] * route.amount;
    for (size_t i = 0; i < 4; ++i) {
        const auto &g = p.generators[i];
        if (g.source == 0) {
            v.gain[i] = 0;
            continue;
        }
        const double pitch = v.note + part.transpose + g.transpose + g.fineCents * .01 +
                             bends_[size_t(v.channel - 1)] * 2 + modulation[0] * 24;
        if (pitch != v.pitchKey[i]) {
            v.pitchKey[i] = pitch;
            const double frequency =
                std::clamp(440 * std::exp2((pitch - 69) / 12), 1., rate_ * .45);
            v.increment[i] = frequency / rate_;
        }
        const double pan = std::clamp(g.pan + part.pan + modulation[6 + i], -1., 1.);
        if (pan != v.panKey[i]) {
            v.panKey[i] = pan;
            v.panL[i] = std::cos((pan + 1) * pi * .25);
            v.panR[i] = std::sin((pan + 1) * pi * .25);
        }
        v.gain[i] =
            std::clamp(g.level + modulation[2 + i], 0., 1.) * part.level / std::sqrt(g.unison);
        v.shape[i] = std::clamp(g.shape + modulation[10 + i], 0., 1.);
        v.fm[i] = std::clamp(g.phaseMod + modulation[14 + i] * 4, 0., 4.);
    }
    const double cutoff =
        std::clamp(p.cutoffHz * std::exp2(p.filterEnvelope * env + p.keyTrack * (v.note - 60) / 12 +
                                          modulation[1] * 4),
                   20., rate_ * .45);
    if (cutoff != v.cutoffKey || p.resonance != v.resonanceKey) {
        v.cutoffKey = cutoff;
        v.resonanceKey = p.resonance;
        const double g = std::tan(pi * cutoff / rate_);
        v.k = 2 - 1.95 * p.resonance;
        v.a1 = 1 / (1 + g * (g + v.k));
        v.a2 = g * v.a1;
        v.a3 = g * v.a2;
    }
}
double Synthesizer::oscillator(Voice &v, size_t index, int unison, double phaseMod) noexcept {
    const auto &g = programs_[v.part].generators[index];
    auto &position = v.position[index][size_t(unison)];
    const double increment = v.increment[index] * (unison == 0 ? 1 : unison == 1 ? .996 : 1.004);
    if (g.source == 3) {
        const double sampleIncrement = increment * 44100 / 262.5;
        const auto &sample = bank_->samples[size_t(g.sample)];
        if (position >= double(sample.mip[0].size())) {
            if (sample.loop)
                position = std::fmod(position, double(sample.mip[0].size()));
            else
                return 0;
        }
        int mip = 0;
        double stride = sampleIncrement;
        while (stride >= 2 && mip < 5) {
            stride *= .5;
            ++mip;
        }
        const auto &data = sample.mip[size_t(mip)];
        const double at = position / double(1U << mip);
        const auto i = std::min(size_t(at), data.size() - 1);
        const auto next = i + 1 < data.size() ? i + 1 : sample.loop ? 0 : i;
        const double output = data[i] + (at - double(i)) * (data[next] - data[i]);
        position += sampleIncrement;
        return output;
    }
    if (g.source == 4)
        return randomSample(v.random);
    const double phase = wrap(position + phaseMod * v.fm[index] * .1);
    position = wrap(position + increment);
    if (g.source == 2) {
        int mip = 0;
        double highest = increment * 16;
        while (highest > .45 && mip < 4) {
            highest *= .5;
            ++mip;
        }
        const double shape = v.shape[index] * 3;
        const int first = int(shape), second = std::min(3, first + 1);
        const double at = phase * 2048;
        const auto i = size_t(at);
        const double fraction = at - double(i);
        const auto &a = bank_->waves[size_t(mip)][size_t(first)],
                   &b = bank_->waves[size_t(mip)][size_t(second)];
        const double va = a[i] + fraction * (a[i + 1] - a[i]),
                     vb = b[i] + fraction * (b[i + 1] - b[i]);
        return va + (shape - first) * (vb - va);
    }
    switch (g.waveform) {
    case 0:
        return bank_->sin(phase);
    case 1:
        return 1 - 4 * std::abs(phase - .5);
    case 2:
        return 2 * phase - 1 - polyBlep(phase, increment);
    default: {
        const double width = .05 + .9 * v.shape[index];
        return (phase < width ? 1 : -1) + polyBlep(phase, increment) -
               polyBlep(wrap(phase - width), increment);
    }
    }
}
bool Synthesizer::process(std::span<float> left, std::span<float> right) noexcept {
    if (left.size() != right.size())
        return false;
    if (left.empty())
        return true;
    if (rate_ == 0)
        return false;
    std::fill(left.begin(), left.end(), 0.F);
    std::fill(right.begin(), right.end(), 0.F);
    for (size_t n = 0; n < left.size(); ++n) {
        double l = 0, r = 0;
        for (unsigned voice = 0; voice < activeCount_;) {
            auto &v = voices_[active_[voice]];
            const auto &program = programs_[v.part];
            const double amp = v.envelopes[0].next(program.envelopes[0], rate_);
            const double env = v.envelopes[1].next(program.envelopes[1], rate_);
            if (v.envelopes[0].stage == 0 || !parts_[v.part].enabled) {
                v.active = false;
                active_[voice] = active_[--activeCount_];
                continue;
            }
            if (v.controlFrame == 0)
                updateVoice(v, amp, env);
            v.controlFrame = (v.controlFrame + 1) & 31U;
            std::array<double, 2> sum{};
            double previous = 0;
            for (size_t generator = 0; generator < 4; ++generator) {
                const auto &g = program.generators[generator];
                if (g.source == 0) {
                    previous = 0;
                    continue;
                }
                double sample = 0;
                for (int u = 0; u < g.unison; ++u)
                    sample += oscillator(v, generator, u, previous);
                previous = sample / g.unison;
                sample *= v.gain[generator];
                sum[0] += sample * v.panL[generator];
                sum[1] += sample * v.panR[generator];
            }
            for (size_t c = 0; c < 2; ++c) {
                const double x = sum[c], v1 = v.a1 * v.ic1[c] + v.a2 * (x - v.ic2[c]),
                             v2 = v.ic2[c] + v.a2 * v.ic1[c] + v.a3 * (x - v.ic2[c]);
                v.ic1[c] = 2 * v1 - v.ic1[c];
                v.ic2[c] = 2 * v2 - v.ic2[c];
                double out = program.filterMode == 0   ? v2
                             : program.filterMode == 1 ? x - v.k * v1 - v2
                             : program.filterMode == 2 ? v1
                                                       : x;
                out *= amp * v.velocity;
                if (v.stealFrames > 0) {
                    const double fade = double(v.stealFrames) / 64;
                    out = out * (1 - fade) + v.stealTail[c] * fade;
                }
                v.lastOutput[c] = out;
                if (c == 0)
                    l += out;
                else
                    r += out;
            }
            if (v.stealFrames > 0)
                --v.stealFrames;
            ++voice;
        }
        for (size_t part = 0; part < 8; ++part)
            if (parts_[part].enabled)
                for (size_t i = 0; i < 2; ++i) {
                    const auto &p = programs_[part];
                    const double speed =
                        p.lfoSync[i] ? bpm_ / (60 * p.lfoBeats[i]) : p.lfoRateHz[i];
                    lfoPhase_[part][i] = wrap(lfoPhase_[part][i] + speed / rate_);
                }
        const double drive = drive_.next(), chorus = chorus_.next(), gain = output_.next();
        for (size_t c = 0; c < 2; ++c) {
            double sample = c == 0 ? l : r;
            if (drive > 0) {
                const double pushed = sample * (1 + 8 * drive);
                sample += drive * (pushed / (1 + std::abs(pushed)) - sample);
            }
            chorusBuffer_[c][chorusPosition_] = float(sample);
            if (chorus > 0) {
                double read = double(chorusPosition_) -
                              rate_ * (.012 + .003 * bank_->sin(chorusPhase_ + c * .25));
                if (read < 0)
                    read += double(chorusBuffer_[c].size());
                const auto i = size_t(read), next = (i + 1) % chorusBuffer_[c].size();
                const double delayed =
                    chorusBuffer_[c][i] +
                    (read - double(i)) * (chorusBuffer_[c][next] - chorusBuffer_[c][i]);
                sample = sample * (1 - chorus * .5) + delayed * chorus * .5;
            }
            if (c == 0)
                left[n] = float(sample * gain);
            else
                right[n] = float(sample * gain);
        }
        chorusPosition_ = (chorusPosition_ + 1) % chorusBuffer_[0].size();
        chorusPhase_ = wrap(chorusPhase_ + .37 / rate_);
    }
    return reverb_.process(left, right);
}
} // namespace ahq::audio
