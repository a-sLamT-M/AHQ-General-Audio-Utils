#include "choiceEditor_test.h"
#include "pcmStationProcessor.h"
#include "userPreset_test.h"
#include <iostream>
int main(int argc, char **argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (!testUserPresetLibrary<PcmStationProcessor>())
        return 1;
    bool ok = testChoiceEditor<PcmStationProcessor>(
        {{"Program", "mode"}, {"Off", "g1_source"}, {"Sine", "g1_wave"}});
    auto check = [&](bool b, const char *name) {
        if (!b)
            std::cerr << name << '\n';
        ok &= b;
    };
    PcmStationProcessor p;
    auto *targets =
        dynamic_cast<juce::AudioParameterChoice *>(p.getParameters().getParameter("mod1_target"));
    check(targets != nullptr && targets->choices.size() == 10 &&
              targets->choices[9] == "Phase mod 2",
          "PCM modulation targets refer only to its two oscillators");
    p.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, .8F), 64);
    p.processBlock(buffer, midi);
    check(buffer.getMagnitude(0, 64) == 0 && buffer.getMagnitude(64, 192) > .00001F,
          "sample accurate note onset");
    check(midi.isEmpty() && p.getLatencySamples() == 0, "MIDI consumed and zero latency");
    for (int i = 0; i < p.getNumPrograms(); ++i) {
        p.setCurrentProgram(i);
        check(p.getCurrentProgram() == i, "factory preset selection");
    }
    auto values = p.readValues();
    values[4] = 7;
    check(p.applyUserValues(values, "Tune"), "custom edit");
    juce::MemoryBlock state;
    p.getStateInformation(state);
    PcmStationProcessor restored;
    restored.setStateInformation(state.getData(), int(state.getSize()));
    check(p.readValues() == restored.readValues(), "all synth parameters roundtrip");
    p.panic();
    p.processBlock(buffer, midi);
    check(p.displayedVoices.load() == 0 && buffer.getMagnitude(0, 256) == 0,
          "panic clears voices and tails");
    p.audition(60, true);
    p.processBlock(buffer, midi);
    check(p.displayedVoices.load() > 0, "UI keyboard reaches audio");
    p.audition(60, false);
    p.loadCombination(4);
    p.panic();
    midi.addEvent(juce::MidiMessage::noteOn(1, 48, .8F), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, .8F), 0);
    p.processBlock(buffer, midi);
    check(p.displayedVoices.load() == 2, "combination key split");
    for (int i = 0; i < int(ahq::audio::Synthesizer::combiCount); ++i) {
        p.loadCombination(i);
        p.panic();
        for (int channel = 1; channel <= 8; ++channel)
            midi.addEvent(juce::MidiMessage::noteOn(channel, 60, .7F), 0);
        float peak = 0;
        for (int block = 0; block < 40; ++block) {
            p.processBlock(buffer, midi);
            peak = std::max(peak, buffer.getMagnitude(0, 256));
        }
        check(std::isfinite(peak) && peak > .00001F, "audible finite combination");
    }
    if (argc >= 2) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditorAndMakeActive());
        editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        editor->setVisible(true);
        if (argc >= 3)
            for (auto *child : editor->getChildren())
                if (auto *box = dynamic_cast<juce::ComboBox *>(child);
                    box != nullptr && box->getItemText(0) == "Generators")
                    box->setSelectedId(juce::String::fromUTF8(argv[2]).getIntValue(),
                                       juce::sendNotificationSync);
        juce::Thread::sleep(40);
        juce::Timer::callPendingTimersSynchronously();
        auto out = juce::File(juce::String::fromUTF8(argv[1])).createOutputStream();
        if (out) {
            out->setPosition(0);
            out->truncate();
            juce::PNGImageFormat png;
            check(png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),
                                         *out),
                  "snapshot");
        }
        editor->setVisible(false);
        editor->removeFromDesktop();
        p.editorBeingDeleted(editor.get());
    }
    return ok ? 0 : 1;
}
