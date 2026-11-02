#include "choiceEditor_test.h"
#include "curveEqEditor.h"
#include "curveEqProcessor.h"
#include "userPreset_test.h"
#include <cmath>
#include <numbers>
namespace {
bool testBandMenuPosition() {
    CurveEqProcessor processor;
    auto values = processor.readValues();
    values[0] = 1;
    processor.applyUserValues(values, "Menu position test");
    std::unique_ptr<CurveEqEditor> editor(
        static_cast<CurveEqEditor *>(processor.createEditorAndMakeActive()));
    editor->setSize(1120, 760);
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    editor->setVisible(true);
    bool passed = true;
    for (float scale : {1.F, 1.25F}) {
        editor->setScaleFactor(scale);
        editor->setTopLeftPosition(70, 50);
        const juce::Point<float> position{62 + float(editor->getWidth() - 124) *
                                                   float(std::log(50.) / std::log(1000.)),
                                          146 + float(editor->getHeight() - 270) * .5F};
        const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), position,
                                     juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 1,
                                     0, 0, 0, 0, editor.get(), editor.get(),
                                     juce::Time::getCurrentTime(), position,
                                     juce::Time::getCurrentTime(), 1, false);
        editor->mouseDown(event);
        bool nearClick = false;
        auto &desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i) {
            auto *component = desktop.getComponent(i);
            if (component->getName() == "menu" && component->isShowing()) {
                const auto bounds = component->getScreenBounds();
                std::cout << "Band menu at scale " << scale << ": click "
                          << event.getScreenPosition().toString() << ", bounds "
                          << bounds.toString() << '\n';
                nearClick |= bounds.expanded(16).contains(event.getScreenPosition());
            }
        }
        if (!nearClick)
            std::cerr << "FAILED: band menu opens beside the right-click position\n";
        passed &= nearClick && processor.readValues() == values;
        juce::PopupMenu::dismissAllActiveMenus();
    }
    editor->setVisible(false);
    editor->removeFromDesktop();
    processor.editorBeingDeleted(editor.get());
    return passed;
}
bool testGraphEditing() {
    CurveEqProcessor processor;
    processor.prepareToPlay(48000, 256);
    const auto flat = processor.createPresetXml();
    std::unique_ptr<CurveEqEditor> editor(
        static_cast<CurveEqEditor *>(processor.createEditorAndMakeActive()));
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition)
            std::cerr << "FAILED: " << message << '\n';
        passed &= condition;
    };
    check(editor->getWidth() == 1600, "new EQ editor defaults to 1600 pixels wide");
    const juce::Rectangle<float> graph{62, 146, float(editor->getWidth() - 124),
                                       float(editor->getHeight() - 270)};
    const auto point = [&](double hz, double gain) {
        return juce::Point<float>(graph.getX() +
                                      graph.getWidth() * float(std::log(hz / 20) / std::log(1000.)),
                                  graph.getCentreY() - float(gain) * graph.getHeight() / 24);
    };
    const auto event = [&](juce::Point<float> position, int modifiers = 0) {
        return juce::MouseEvent(
            juce::Desktop::getInstance().getMainMouseSource(), position,
            juce::ModifierKeys(modifiers | juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            editor.get(), editor.get(), juce::Time::getCurrentTime(), position,
            juce::Time::getCurrentTime(), 1, false);
    };
    const auto click = [&](juce::Point<float> position, int modifiers = 0) {
        editor->mouseDown(event(position, modifiers));
        editor->mouseUp(event(position, modifiers));
    };
    const auto drag = [&](juce::Point<float> start, juce::Point<float> end, int modifiers = 0) {
        editor->mouseDown(event(start, modifiers));
        editor->mouseDrag(event(end, modifiers));
        editor->mouseUp(event(end, modifiers));
    };
    const auto reset = [&] {
        check(processor.loadPresetXml(flat), "restore flat EQ for the next gesture");
        editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    };
    struct Gestures final : juce::AudioProcessorParameter::Listener {
        int depth = 0, starts = 0;
        bool balanced = true;
        void parameterValueChanged(int, float) override {}
        void parameterGestureChanged(int, bool starting) override {
            depth += starting ? 1 : -1;
            starts += starting ? 1 : 0;
            balanced &= depth >= 0;
        }
    } gestures;
    for (auto *parameter : static_cast<juce::AudioProcessor &>(processor).getParameters())
        parameter->addListener(&gestures);

    drag(point(500, 0), point(750, 4));
    auto values = processor.readValues();
    check(values[0] == 1 && std::abs(values[3] - 750) < .01F && std::abs(values[4] - 4) < .01F,
          "dragging the flat response creates and positions a bell");
    check(processor.undo() && processor.readValues()[4] == 0 && processor.redo() &&
              processor.readValues() == values,
          "node drag is undoable and redoable");
    reset();
    editor->mouseDoubleClick(event(point(1000, 6)));
    values = processor.readValues();
    check(values[0] == 1 && std::abs(values[3] - 1000) < .01F && std::abs(values[4] - 6) < .01F,
          "double-click creates a bell at the clicked frequency and gain");
    editor->mouseDoubleClick(event(point(100, 3)));
    click(point(1000, 6), juce::ModifierKeys::commandModifier);
    const auto beforeWheel = processor.readValues();
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = .25F;
    editor->mouseWheelMove(event(point(1000, 6)), wheel);
    values = processor.readValues();
    check(values[5] > beforeWheel[5] && values[16] > beforeWheel[16],
          "Ctrl selection and wheel adjust Q for both selected bands");
    const auto beforeDrag = values;
    drag(point(1000, 6), point(1500, 8));
    values = processor.readValues();
    check(std::abs(values[3] / beforeDrag[3] - values[14] / beforeDrag[14]) < .0001F &&
              std::abs(values[4] / values[15] - 2) < .001F,
          "group drag preserves frequency spacing and relative gains");
    check(processor.undo() && processor.readValues() == beforeDrag && processor.redo() &&
              processor.readValues() == values,
          "group edits undo as one operation");
    check(editor->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)),
          "Delete handles selection");
    check(processor.readValues()[0] == 0 && processor.readValues()[11] == 0,
          "Delete removes all selected bands");
    check(processor.undo() && processor.readValues() == values,
          "deleted selection can be restored");
    editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    drag(point(70, 10), point(4000, -3));
    editor->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
    check(processor.readValues()[0] == 0 && processor.readValues()[11] == 0,
          "background rectangle selects adjacent bands");
    reset();
    editor->mouseDoubleClick(event(point(24, 0)));
    check(processor.readValues()[1] == 3, "left edge creates a low cut");
    editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    editor->mouseDoubleClick(event(point(18000, 0)));
    check(processor.readValues()[12] == 4, "right edge creates a high cut");
    editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    editor->mouseDoubleClick(event(point(1000, 0), juce::ModifierKeys::altModifier));
    check(processor.readValues()[29] == -3, "Alt double-click creates a dynamic bell");
    const auto beforeDynamic = processor.readValues();
    editor->mouseWheelMove(event(point(1000, 0), juce::ModifierKeys::altModifier), wheel);
    check(processor.readValues()[29] > beforeDynamic[29] &&
              processor.readValues()[26] == beforeDynamic[26],
          "Alt wheel edits dynamic range only");
    const auto beforeEmptyWheel = processor.readValues();
    editor->mouseWheelMove(event(point(300, 10)), wheel);
    check(processor.readValues() == beforeEmptyWheel, "wheel over empty space preserves bands");
    reset();
    editor->mouseDoubleClick(event(point(1000, 0)));
    drag(point(1000, 0), point(1000, 4), juce::ModifierKeys::shiftModifier);
    values = processor.readValues();
    check(std::abs(values[4] - .4F) < .01F, "Shift drag provides fine gain adjustment");
    const auto beforeAxis = values;
    drag(point(values[3], values[4]), point(values[3], values[4]) + juce::Point<float>(100, -20),
         juce::ModifierKeys::altModifier);
    values = processor.readValues();
    check(values[0] == 1 && values[3] > beforeAxis[3] && values[4] == beforeAxis[4],
          "Alt drag locks the frequency axis without bypassing");
    const auto beforeQ = values;
    drag(point(values[3], values[4]), point(values[3], values[4]) + juce::Point<float>(0, -60),
         juce::ModifierKeys::commandModifier);
    values = processor.readValues();
    check(values[5] > beforeQ[5] && values[3] == beforeQ[3] && values[4] == beforeQ[4],
          "Ctrl vertical drag edits Q without moving the band");
    check(gestures.starts > 0 && gestures.depth == 0 && gestures.balanced,
          "all graph edits balance host automation gestures");
    editor->setSize(1120, 760);
    const auto resizedImage = editor->createComponentSnapshot(editor->getLocalBounds());
    bool curveAtNewPosition = false;
    const int expectedY = 146 + (editor->getHeight() - 270) / 2;
    for (int y = expectedY - 4; y <= expectedY + 4; ++y) {
        const auto pixel = resizedImage.getPixelAt(editor->getWidth() - 160, y);
        curveAtNewPosition |=
            pixel.getBlue() > 220 && pixel.getGreen() > 140 && pixel.getRed() < 180;
    }
    check(curveAtNewPosition, "resize immediately redraws the response at the new graph position");
    for (auto *parameter : static_cast<juce::AudioProcessor &>(processor).getParameters())
        parameter->removeListener(&gestures);
    processor.editorBeingDeleted(editor.get());
    return passed;
}
} // namespace
int main(int argc, char **argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (!testUserPresetLibrary<CurveEqProcessor>())
        return 1;
    bool passed = testGraphEditing();
    passed &= testBandMenuPosition();
    auto check = [&](bool condition, const char *name) {
        if (!condition)
            std::cerr << "FAILED: " << name << '\n';
        passed &= condition;
    };
    passed &= testChoiceEditor<CurveEqProcessor>({{"Bell", "b1_shape"}, {"Stereo", "b1_channels"}});
    CurveEqProcessor p;
    p.prepareToPlay(48000, 256);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(2, 4097);
    auto fill = [&] {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample(ch, i,
                                 float(.2 * std::sin(2 * std::numbers::pi * 1000 * i / 48000)));
    };
    fill();
    const juce::AudioBuffer<float> original(buffer);
    p.processBlock(buffer, midi);
    check(buffer.getSample(0, 1000) == original.getSample(0, 1000), "flat bypass");
    auto values = p.readValues();
    values[0] = 1;
    values[4] = 6;
    check(p.applyUserValues(values, "Bell"), "edit band");
    fill();
    p.processBlock(buffer, midi);
    double energyIn = 0, energyOut = 0;
    for (int i = 2048; i < 4097; ++i) {
        energyIn += std::pow(original.getSample(0, i), 2);
        energyOut += std::pow(buffer.getSample(0, i), 2);
    }
    check(std::abs(10 * std::log10(energyOut / energyIn) - 6) < .02, "oversized block bell gain");
    check(p.getLatencySamples() == 0, "zero host latency");
    juce::MemoryBlock state;
    p.getStateInformation(state);
    CurveEqProcessor restored;
    restored.setStateInformation(state.getData(), int(state.getSize()));
    check(restored.readValues() == p.readValues(), "all 266 parameters saved");
    const auto valid = p.readValues();
    values = valid;
    values[1] = 99;
    check(!p.applyUserValues(values, "Invalid") && p.readValues() == valid, "reject invalid shape");
    values = valid;
    values[CurveEqProcessor::count - 1] = 0;
    check(p.applyUserValues(values, "Dry"), "dry mix");
    fill();
    p.processBlock(buffer, midi);
    check(buffer.getSample(0, 4096) == original.getSample(0, 4096), "settled dry mix exact");
    if (argc == 2) {
        values = valid;
        values[4] = 5;
        values[3] = 160;
        values[11] = 1;
        values[14] = 1800;
        values[15] = -6;
        values[16] = 2;
        values[18] = -4;
        values[22] = 1;
        values[23] = 2;
        values[25] = 8500;
        values[26] = 3;
        check(p.applyUserValues(values, "Display setup"), "display bands");
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditorAndMakeActive());
        editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        editor->setVisible(true);
        for (int i = 0; i < 8; ++i) {
            fill();
            p.processBlock(buffer, midi);
            juce::Thread::sleep(35);
            juce::Timer::callPendingTimersSynchronously();
        }
        auto image = editor->createComponentSnapshot(editor->getLocalBounds());
        auto out = juce::File(juce::String::fromUTF8(argv[1])).createOutputStream();
        if (out) {
            out->setPosition(0);
            out->truncate();
            juce::PNGImageFormat png;
            check(png.writeImageToStream(image, *out), "snapshot");
        } else
            check(false, "snapshot output");
        editor->setVisible(false);
        editor->setSize(1120, 760);
        const auto minimum = editor->createComponentSnapshot(editor->getLocalBounds());
        auto minimumOut = juce::File(juce::String::fromUTF8(argv[1]))
                              .getSiblingFile("curveEq-minimum.png")
                              .createOutputStream();
        if (minimumOut) {
            minimumOut->setPosition(0);
            minimumOut->truncate();
            check(juce::PNGImageFormat().writeImageToStream(minimum, *minimumOut),
                  "minimum-size snapshot");
        } else
            check(false, "minimum-size snapshot output");
        editor->removeFromDesktop();
        p.editorBeingDeleted(editor.get());
    }
    return passed ? 0 : 1;
}
