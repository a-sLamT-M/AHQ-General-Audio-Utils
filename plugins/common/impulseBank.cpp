#include "impulseBank.h"
#include <algorithm>
#include <chrono>
#include <cmath>
ImpulseBank::ImpulseBank(int n, double limit, bool longResponses)
    : count(std::clamp(n, 1, 4)), maximumSeconds(limit) {
    for (int i = 0; i < count; ++i) {
        auto &slot = slots[size_t(i)];
        if (longResponses)
            slot.convolution = std::make_unique<juce::dsp::Convolution>(
                juce::dsp::Convolution::NonUniform{256}, queue);
        else
            slot.convolution = std::make_unique<juce::dsp::Convolution>(queue);
    }
    worker = std::thread([this] { run(); });
}
ImpulseBank::~ImpulseBank() {
    {
        std::lock_guard lock(mutex);
        stopping = true;
    }
    wake.notify_all();
    worker.join();
    for (auto &slot : slots) {
        delete slot.pending.exchange(nullptr);
        delete slot.retired.exchange(nullptr);
    }
}
void ImpulseBank::requestFile(int i, const juce::File &file) {
    if (i < 0 || i >= count)
        return;
    auto &slot = slots[size_t(i)];
    {
        std::lock_guard lock(mutex);
        slot.requestedFile = file;
        slot.requestedBytes.reset();
        slot.requestedName = file.getFileName();
        slot.requested = true;
        slot.error.clear();
        slot.generated.store(0);
    }
    wake.notify_one();
}
void ImpulseBank::requestEmbedded(int i, juce::MemoryBlock data, juce::String name) {
    if (i < 0 || i >= count)
        return;
    auto &slot = slots[size_t(i)];
    {
        std::lock_guard lock(mutex);
        slot.requestedFile = {};
        slot.requestedBytes = std::move(data);
        slot.requestedName = std::move(name);
        slot.requested = true;
        slot.error.clear();
        slot.generated.store(0);
    }
    wake.notify_one();
}
void ImpulseBank::setGenerated(int i, int ms, int cutoff, bool reverse) noexcept {
    if (i < 0 || i >= count)
        return;
    const uint64_t value = uint64_t(std::clamp(ms, 50, 1500)) |
                           (uint64_t(std::clamp(cutoff, 500, 18000)) << 16) |
                           (uint64_t(reverse) << 32);
    slots[size_t(i)].generated.store(value, std::memory_order_release);
}
std::unique_ptr<ImpulseBank::Prepared> ImpulseBank::decode(const juce::MemoryBlock &bytes,
                                                           juce::String &error) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(
        formats.createReaderFor(std::make_unique<juce::MemoryInputStream>(bytes, false)));
    if (!reader || !std::isfinite(reader->sampleRate) || reader->sampleRate < 8000 ||
        reader->sampleRate > 384000 || reader->numChannels < 1 || reader->numChannels > 2 ||
        reader->lengthInSamples < 1 ||
        reader->lengthInSamples > reader->sampleRate * maximumSeconds) {
        error = "Use mono/stereo audio, 8-384 kHz, at most " + juce::String(maximumSeconds, 1) +
                " seconds.";
        return {};
    }
    auto result = std::make_unique<Prepared>();
    result->rate = reader->sampleRate;
    result->audio.setSize(int(reader->numChannels), int(reader->lengthInSamples));
    if (!reader->read(&result->audio, 0, result->audio.getNumSamples(), 0, true, true)) {
        error = "Could not decode audio.";
        return {};
    }
    for (int c = 0; c < result->audio.getNumChannels(); ++c)
        for (int n = 0; n < result->audio.getNumSamples(); ++n)
            if (!std::isfinite(result->audio.getSample(c, n))) {
                error = "IR contains non-finite samples.";
                return {};
            }
    return result;
}
std::unique_ptr<ImpulseBank::Prepared> ImpulseBank::generate(uint64_t key) const {
    const int frames = int(double(key & 65535U) * 44.1);
    const double cutoff = double((key >> 16) & 65535U);
    const bool reverse = (key >> 32) != 0;
    auto result = std::make_unique<Prepared>();
    result->audio.setSize(2, frames);
    const double damping = -std::expm1(-2 * juce::MathConstants<double>::pi * cutoff / 44100);
    for (int c = 0; c < 2; ++c) {
        uint32_t random = 0x9368421U + uint32_t(c) * 953U;
        double low = 0, energy = 0;
        for (int n = 0; n < frames; ++n) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            const double noise = double(random) / 2147483648. - 1;
            low += damping * (noise - low);
            // Level non-linear tail with a 5 ms onset and a 10 ms closing ramp.
            double envelope =
                std::min(1., double(n) / 220) * std::min(1., double(frames - 1 - n) / 441);
            if (reverse)
                envelope *= double(n) / double(frames);
            const float sample = float(low * envelope);
            result->audio.setSample(c, n, sample);
            energy += sample * sample;
        }
        result->audio.applyGain(c, 0, frames, float(.7 / std::sqrt(std::max(energy, 1e-12))));
    }
    return result;
}
juce::MemoryBlock ImpulseBank::encode(const Prepared &data) const {
    juce::MemoryBlock bytes;
    std::unique_ptr<juce::OutputStream> stream =
        std::make_unique<juce::MemoryOutputStream>(bytes, false);
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(
        stream, juce::AudioFormatWriterOptions{}
                    .withSampleRate(data.rate)
                    .withNumChannels(data.audio.getNumChannels())
                    .withBitsPerSample(32)
                    .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer)
        writer->writeFromAudioSampleBuffer(data.audio, 0, data.audio.getNumSamples());
    writer.reset();
    return bytes;
}
void ImpulseBank::run() {
    for (;;) {
        bool didWork = false;
        for (int i = 0; i < count; ++i) {
            auto &slot = slots[size_t(i)];
            delete slot.retired.exchange(nullptr, std::memory_order_acquire);
            juce::File file;
            juce::MemoryBlock bytes;
            juce::String name;
            uint64_t key = 0;
            {
                std::lock_guard lock(mutex);
                if (stopping)
                    return;
                key = slot.generated.load(std::memory_order_acquire);
                if (!slot.requested && (key == 0 || key == slot.lastGenerated))
                    continue;
                if (slot.requested) {
                    file = slot.requestedFile;
                    bytes = std::move(slot.requestedBytes);
                    name = slot.requestedName;
                    slot.requested = false;
                    key = 0;
                } else
                    name =
                        ((key >> 32) != 0) ? "Original reverse tail" : "Original non-linear tail";
                working = true;
            }
            didWork = true;
            juce::String error;
            std::unique_ptr<Prepared> prepared;
            juce::MemoryBlock saved;
            try {
                if (key != 0)
                    prepared = generate(key);
                else {
                    if (file != juce::File{}) {
                        if (file.getSize() > 64 * 1024 * 1024 || !file.loadFileAsData(bytes))
                            error = "Cannot read IR (maximum file size 64 MiB).";
                    }
                    if (error.isEmpty()) {
                        if (bytes.getSize() == 0 && file == juce::File{}) {
                            prepared = std::make_unique<Prepared>();
                            prepared->audio.setSize(1, 1);
                            prepared->audio.clear();
                            prepared->clear = true;
                        } else
                            prepared = decode(bytes, error);
                    }
                }
                if (prepared && !prepared->clear)
                    saved = encode(*prepared);
            } catch (const std::exception &) {
                prepared.reset();
                error = "Could not allocate or decode the impulse response.";
            }
            {
                std::lock_guard lock(mutex);
                if (prepared) {
                    slot.savedBytes = std::move(saved);
                    slot.name = name;
                    delete slot.pending.exchange(prepared.release(), std::memory_order_acq_rel);
                }
                slot.error = error;
                slot.lastGenerated = key;
                working = false;
            }
            wake.notify_all();
        }
        std::unique_lock lock(mutex);
        if (stopping)
            return;
        if (!didWork)
            wake.wait_for(lock, std::chrono::milliseconds(10));
    }
}
void ImpulseBank::flush() {
    std::unique_lock lock(mutex);
    wake.notify_all();
    wake.wait(lock, [this] {
        if (working)
            return false;
        for (int i = 0; i < count; ++i) {
            const auto &s = slots[size_t(i)];
            const auto key = s.generated.load(std::memory_order_acquire);
            if (s.requested || (key != 0 && key != s.lastGenerated))
                return false;
        }
        return true;
    });
}
void ImpulseBank::adopt(int i, bool realtime) noexcept {
    auto &slot = slots[size_t(i)];
    if (slot.retired.load(std::memory_order_acquire) != nullptr)
        return;
    if (auto *data = slot.pending.exchange(nullptr, std::memory_order_acq_rel)) {
        slot.convolution->loadImpulseResponse(
            std::move(data->audio), data->rate, juce::dsp::Convolution::Stereo::yes,
            juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        slot.active.store(!data->clear, std::memory_order_release);
        if (realtime)
            slot.retired.store(data, std::memory_order_release);
        else
            delete data;
    }
}
bool ImpulseBank::prepare(double rate, unsigned blockSize, unsigned channels) {
    if (!std::isfinite(rate) || rate < 8000 || rate > 384000 || channels < 1 || channels > 2)
        return false;
    flush();
    for (int i = 0; i < count; ++i) {
        delete slots[size_t(i)].retired.exchange(nullptr);
        adopt(i, false);
        slots[size_t(i)].convolution->prepare({rate, std::max(1U, blockSize), channels});
    }
    return true;
}
void ImpulseBank::process(int i, juce::dsp::AudioBlock<float> block) noexcept {
    adopt(i, true);
    if (loaded(i))
        slots[size_t(i)].convolution->process(juce::dsp::ProcessContextReplacing<float>(block));
    else
        block.clear();
}
void ImpulseBank::reset() noexcept {
    for (int i = 0; i < count; ++i)
        slots[size_t(i)].convolution->reset();
}
bool ImpulseBank::loaded(int i) const noexcept {
    return slots[size_t(i)].active.load(std::memory_order_acquire);
}
juce::String ImpulseBank::status(int i) const {
    std::lock_guard lock(mutex);
    const auto &s = slots[size_t(i)];
    if (!s.error.isEmpty())
        return s.error;
    if (s.requested || working)
        return "Loading...";
    return s.name;
}
juce::XmlElement ImpulseBank::save() {
    flush();
    juce::XmlElement xml("ImpulseResponses");
    std::lock_guard lock(mutex);
    for (int i = 0; i < count; ++i) {
        auto *child = xml.createNewChildElement("IR");
        child->setAttribute("slot", i);
        child->setAttribute("name", slots[size_t(i)].name);
        child->addTextElement(slots[size_t(i)].savedBytes.toBase64Encoding());
    }
    return xml;
}
bool ImpulseBank::restore(const juce::XmlElement &xml, bool validateAudio) {
    if (!xml.hasTagName("ImpulseResponses") || xml.getNumChildElements() != count)
        return false;
    std::array<juce::MemoryBlock, 4> bytes;
    std::array<juce::String, 4> names;
    std::array<bool, 4> present{};
    for (auto *child : xml.getChildIterator()) {
        const int i = child->getIntAttribute("slot", -1);
        if (!child->hasTagName("IR") || i < 0 || i >= count || present[size_t(i)])
            return false;
        present[size_t(i)] = true;
        if (!bytes[size_t(i)].fromBase64Encoding(child->getAllSubText()) ||
            bytes[size_t(i)].getSize() > 64 * 1024 * 1024)
            return false;
        names[size_t(i)] = child->getStringAttribute("name");
    }
    // Portable user presets are checked before scheduling any slot replacement.
    // This optional decode runs on the caller's non-audio thread.
    if (validateAudio) {
        try {
            for (int i = 0; i < count; ++i) {
                juce::String error;
                if (bytes[size_t(i)].getSize() != 0 && !decode(bytes[size_t(i)], error))
                    return false;
            }
        } catch (const std::exception &) {
            return false;
        }
    }
    for (int i = 0; i < count; ++i)
        requestEmbedded(i, std::move(bytes[size_t(i)]), names[size_t(i)]);
    return true;
}
