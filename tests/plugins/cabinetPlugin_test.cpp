#include "cabinetProcessor.h"
#include "choiceEditor_test.h"
#include "userPreset_test.h"
#include <chrono>
#include <cmath>
#include <iostream>
juce::MemoryBlock impulse(float gain, int length = 32) {
    juce::MemoryBlock bytes;
    std::unique_ptr<juce::OutputStream> stream =
        std::make_unique<juce::MemoryOutputStream>(bytes, false);
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(
        stream, juce::AudioFormatWriterOptions{}
                    .withSampleRate(48000)
                    .withNumChannels(1)
                    .withBitsPerSample(32)
                    .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    juce::AudioBuffer<float> audio(1, length);
    audio.clear();
    audio.setSample(0, 0, gain);
    for (int i = 1; length > 32 && i < length; ++i)
        audio.setSample(0, i, gain * std::sin(float(i) * .17F) * std::exp(-float(i) / 800));
    writer->writeFromAudioSampleBuffer(audio, 0, length);
    writer.reset();
    return bytes;
}
int main(int argc, char **argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (!testUserPresetLibrary<CabinetProcessor>())
        return 1;
    if (argc == 2 && juce::String::fromUTF8(argv[1]) == "--benchmark") {
        juce::ScopedNoDenormals noDenormals;
        CabinetProcessor bench;
        for (int slot = 0; slot < 4; ++slot)
            bench.impulseBank().requestEmbedded(slot, impulse(.01F, 4096), "4096-tap IR");
        bench.prepareToPlay(48000, 256);
        juce::AudioBuffer<float> audio(2, 256);
        juce::MidiBuffer events;
        std::array<float, 256> input{};
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = .2F * std::sin(float(i) * .083F);
        for (int run = 0; run < 4; ++run) {
            const auto begin = std::chrono::steady_clock::now();
            for (int block = 0; block < 4000; ++block) {
                for (int channel = 0; channel < 2; ++channel)
                    audio.copyFrom(channel, 0, input.data(), 256);
                bench.processBlock(audio, events);
            }
            const double seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
            if (run != 0)
                std::cout << "Cabinet four 4096-tap mono IRs -> stereo, 48000/256: " << seconds
                          << " s / 21.3333 s audio = " << seconds / (4000. * 256 / 48000) * 100
                          << "% of one core, checksum " << audio.getMagnitude(0, 256) << '\n';
        }
        return 0;
    }
    bool ok =
        testChoiceEditor<CabinetProcessor>({{"Off", "ir1_enabled"}, {"Normal", "ir1_polarity"}});
    auto check = [&](bool b, const char *name) {
        if (!b)
            std::cerr << name << '\n';
        ok &= b;
    };
    CabinetProcessor p;
    for (int slot = 0; slot < 4; ++slot)
        p.impulseBank().requestEmbedded(slot, impulse(.1F * float(slot + 1)), "Test IR");
    p.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> b(2, 4097);
    juce::MidiBuffer midi;
    auto fill = [&] {
        for (int i = 0; i < b.getNumSamples(); ++i) {
            b.setSample(0, i, .5F);
            b.setSample(1, i, -.25F);
        }
    };
    fill();
    p.processBlock(b, midi);
    check(std::abs(b.getSample(0, 4000) - .5F) < 1e-5 &&
              std::abs(b.getSample(1, 4000) + .25F) < 1e-5,
          "four simultaneous IRs");
    auto v = p.readValues();
    v[8] = 1;
    check(p.applyUserValues(v, "Polarity"), "set polarity");
    fill();
    p.processBlock(b, midi);
    check(std::abs(b.getSample(0, 4000) - .3F) < 1e-5, "independent polarity");
    v[2] = 1;
    check(p.applyUserValues(v, "Pan"), "set pan");
    fill();
    p.processBlock(b, midi);
    check(std::abs(b.getSample(0, 4000) - .25F) < 1e-5 &&
              std::abs(b.getSample(1, 4000) + .15F) < 1e-5,
          "independent pan");
    v[11] = -6.0206F;
    check(p.applyUserValues(v, "Level"), "set volume");
    fill();
    p.processBlock(b, midi);
    check(std::abs(b.getSample(0, 4000) - .175F) < 1e-5, "independent volume");
    const auto slotValues = p.readValues();
    v = slotValues;
    v[CabinetProcessor::masterOutputIndex] = -6.0206F;
    check(p.applyUserValues(v, "Master"), "set master volume");
    v = p.readValues();
    fill();
    p.processBlock(b, midi);
    check(std::abs(b.getSample(0, 0) - .175F) < .002F &&
              std::abs(b.getSample(0, 4000) - .0875F) < 1e-5,
          "master volume follows the summed output with a smooth transition");
    check(std::equal(v.begin(), v.begin() + 20, slotValues.begin()),
          "master does not change individual slot parameters");
    juce::MemoryBlock state;
    p.getStateInformation(state);
    CabinetProcessor restored;
    restored.setStateInformation(state.getData(), int(state.getSize()));
    restored.prepareToPlay(48000, 17);
    auto reference = b;
    fill();
    restored.processBlock(b, midi);
    check(p.readValues() == restored.readValues() &&
              std::abs(b.getSample(0, 4000) - reference.getSample(0, 4000)) < 1e-6,
          "embedded IR state roundtrip");
    {
        auto legacyXml =
            juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
        legacyXml->removeAttribute("master_volume");
        juce::MemoryBlock legacyState;
        juce::AudioProcessor::copyXmlToBinary(*legacyXml, legacyState);
        CabinetProcessor legacy;
        legacy.setStateInformation(legacyState.getData(), int(legacyState.getSize()));
        legacy.prepareToPlay(48000, 256);
        fill();
        legacy.processBlock(b, midi);
        const auto legacyValues = legacy.readValues();
        check(legacyValues[20] == 0 &&
                  std::equal(legacyValues.begin(), legacyValues.begin() + 20, slotValues.begin()) &&
                  std::abs(b.getSample(0, 4000) - .175F) < 1e-5,
              "old 20-parameter projects restore IRs and default master to 0 dB");
    }
    {
        juce::TemporaryFile preset(".ahqcabinet"), invalid(".ahqcabinet");
        check(p.savePresetFile(preset.getFile()), "save portable preset");
        CabinetProcessor portable;
        check(portable.loadPresetFile(preset.getFile()), "load portable preset");
        portable.prepareToPlay(48000, 256);
        fill();
        portable.processBlock(b, midi);
        check(portable.readValues() == v && std::abs(b.getSample(0, 4000) - .0875F) < 1e-5,
              "preset includes all four IRs and master without source paths");
        check(portable.undo(), "undo complete preset load");
        portable.prepareToPlay(48000, 256);
        fill();
        portable.processBlock(b, midi);
        check(portable.readValues()[20] == 0 && !portable.impulseBank().loaded(0) &&
                  std::abs(b.getSample(0, 4000) - .5F) < 1e-5,
              "preset undo restores empty IRs and original parameters");
        check(portable.redo(), "redo complete preset load");
        portable.prepareToPlay(48000, 256);
        fill();
        portable.processBlock(b, midi);
        check(portable.readValues() == v && std::abs(b.getSample(0, 4000) - .0875F) < 1e-5,
              "preset redo restores IR audio");
        juce::MemoryBlock beforeBad, afterBad;
        portable.getStateInformation(beforeBad);
        auto bad = juce::parseXML(preset.getFile().loadFileAsString());
        auto *lastIr = bad->getChildByName("ImpulseResponses")->getChildElement(3);
        lastIr->deleteAllChildElements();
        const juce::MemoryBlock garbage("not audio", 9);
        lastIr->addTextElement(garbage.toBase64Encoding());
        check(invalid.getFile().replaceWithText(bad->toString()), "write corrupt IR fixture");
        check(!portable.loadPresetFile(invalid.getFile()), "reject corrupt fourth IR");
        portable.getStateInformation(afterBad);
        check(beforeBad == afterBad, "failed preset load preserves every IR and parameter");
        auto invalidValues = juce::parseXML(preset.getFile().loadFileAsString());
        invalidValues->setAttribute("master_volume", "nan");
        check(invalid.getFile().replaceWithText(invalidValues->toString()),
              "write invalid master fixture");
        check(!portable.loadPresetFile(invalid.getFile()) && portable.readValues() == v,
              "reject non-finite master without changes");
        portable.loadFactoryPreset(2);
        const auto dual = portable.readValues();
        check(dual[0] == 1 && dual[5] == 1 && dual[10] == 0 && dual[15] == 0 && dual[20] == v[20] &&
                  std::abs(dual[1] + 6.0206F) < .0001F && portable.impulseBank().loaded(3),
              "dual routing preset keeps embedded IRs and master");
        check(portable.undo() && portable.readValues() == v, "undo routing preset");
        auto emptyValues = CabinetProcessor::Values{};
        CabinetProcessor empty;
        emptyValues = empty.readValues();
        emptyValues[20] = -6.0206F;
        check(empty.applyUserValues(emptyValues, "Empty master"), "set empty bank master");
        empty.prepareToPlay(48000, 256);
        fill();
        empty.processBlock(b, midi);
        check(std::abs(b.getSample(0, 4000) - .25F) < 1e-5,
              "master also applies with no loaded IR");
    }
    for (int slot = 0; slot < 4; ++slot)
        v[size_t(slot * 5)] = 0;
    check(p.applyUserValues(v, "Mute"), "mute slots");
    fill();
    p.processBlock(b, midi);
    check(b.getSample(0, 4000) == 0 && b.getSample(1, 4000) == 0, "all slots disabled");
    b.clear();
    p.processBlock(b, midi);
    v[0] = 1;
    check(p.applyUserValues(v, "Enable"), "re-enable slot");
    p.processBlock(b, midi);
    check(b.getMagnitude(0, b.getNumSamples()) == 0, "re-enable has no stale history");
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(restored.createEditorAndMakeActive());
        editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        editor->setVisible(true);
        juce::Timer::callPendingTimersSynchronously();
        bool load = false, save = false, master = false;
        auto children = editor->getChildren();
        for (auto *child : editor->getChildren())
            if (dynamic_cast<ahq::ui::PresetBar *>(child))
                children.addArray(child->getChildren());
        for (auto *child : children) {
            if (auto *button = dynamic_cast<juce::TextButton *>(child)) {
                load |= button->getButtonText() == "Load preset" && button->isVisible();
                save |= button->getButtonText() == "Save preset" && button->isVisible();
            }
            if (auto *slider = dynamic_cast<ParameterSlider *>(child))
                master |= editor->getControlParameterIndex(*slider) == 20 && slider->isVisible() &&
                          !slider->getBounds().isEmpty();
        }
        check(load && save && master, "preset buttons and master control are visible");
        editor->setSize(1000, 896);
        for (auto *child : editor->getChildren())
            if (dynamic_cast<ParameterSlider *>(child))
                check(editor->getLocalBounds().contains(child->getBounds()),
                      "controls fit the minimum editor size");
        editor->setSize(1180, 936);
        auto out =
            argc == 2 ? juce::File(juce::String::fromUTF8(argv[1])).createOutputStream() : nullptr;
        if (out != nullptr) {
            out->setPosition(0);
            out->truncate();
            juce::PNGImageFormat png;
            check(png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),
                                         *out),
                  "snapshot");
        }
        editor->setVisible(false);
        editor->removeFromDesktop();
        restored.editorBeingDeleted(editor.get());
    }
    return ok ? 0 : 1;
}
