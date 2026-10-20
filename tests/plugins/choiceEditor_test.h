#pragma once
#include "../../plugins/common/parameterControl.h"
#include <cmath>
#include <iostream>
#include <juce_audio_utils/juce_audio_utils.h>

struct ChoiceParameter {
    const char *firstItem;
    const char *parameterId;
};

inline bool testParameterMenus(juce::AudioProcessorEditor &editor) {
    struct Context final : juce::AudioProcessorEditorHostContext {
        mutable const juce::AudioProcessorParameter *requested = nullptr;
        mutable int shown = 0;
        struct Menu final : juce::HostProvidedContextMenu {
            explicit Menu(int &n) : count(n) {}
            juce::PopupMenu getEquivalentPopupMenu() const override { return {}; }
            void showNativeMenu(juce::Point<int>) const override { ++count; }
            int &count;
        };
        std::unique_ptr<juce::HostProvidedContextMenu>
        getContextMenuForParameter(const juce::AudioProcessorParameter *p) const override {
            requested = p;
            return std::make_unique<Menu>(shown);
        }
    } context;
    editor.setHostContext(&context);
    bool passed = true;
    for (auto *child : editor.getChildren()) {
        auto *slider = dynamic_cast<ParameterSlider *>(child);
        if (slider == nullptr)
            continue;
        const int index = editor.getControlParameterIndex(*slider);
        const double before = slider->getValue();
        const auto position = juce::Point<float>(10, 10);
        juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), position,
                               juce::ModifierKeys::rightButtonModifier, 1, 0, 0, 0, 0, slider,
                               slider, juce::Time::getCurrentTime(), position,
                               juce::Time::getCurrentTime(), 1, false);
        const int previous = context.shown;
        slider->mouseDown(event);
        slider->mouseDrag(event);
        slider->mouseUp(event);
        passed &= index >= 0 && context.shown == previous + 1 && context.requested != nullptr &&
                  context.requested->getParameterIndex() == index && slider->getValue() == before;
        for (auto *nested : slider->getChildren())
            passed &= editor.getControlParameterIndex(*nested) == index;
        editor.setHostContext(nullptr);
        slider->mouseDown(event);
        slider->mouseUp(event);
        passed &= slider->getValue() == before;
        editor.setHostContext(&context);
    }
    editor.setHostContext(nullptr);
    if (!passed)
        std::cerr << "FAILED: host parameter menu mapping or right-click changed a value\n";
    return passed;
}

// Exercise the ordering that occurs when a display timer runs before ComboBox's
// queued onChange callback. No mouse timing or private editor access is required.
template <typename Processor>
bool testChoiceEditor(std::initializer_list<ChoiceParameter> choices) {
    auto processor = std::make_unique<Processor>();
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorAndMakeActive());
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    editor->setVisible(true);
    bool passed = testParameterMenus(*editor);
    auto check = [&](bool condition, const char *message) {
        if (!condition)
            std::cerr << "FAILED: " << message << '\n';
        passed &= condition;
    };
    for (const auto &choice : choices) {
        juce::ComboBox *combo = nullptr;
        auto *parameter = processor->getParameters().getParameter(choice.parameterId);
        for (auto *child : editor->getChildren())
            if (auto *candidate = dynamic_cast<juce::ComboBox *>(child))
                if (candidate->getItemText(0) == choice.firstItem && parameter != nullptr &&
                    (editor->getControlParameterIndex(*candidate) < 0 ||
                     editor->getControlParameterIndex(*candidate) ==
                         parameter->getParameterIndex()))
                    combo = candidate;
        check(combo != nullptr && parameter != nullptr, "choice control and parameter exist");
        if (combo == nullptr || parameter == nullptr)
            continue;
        for (int index = 0; index < combo->getNumItems(); ++index) {
            const int before = int(parameter->convertFrom0to1(parameter->getValue()));
            const int selected = (before + 1) % combo->getNumItems();
            juce::Thread::sleep(45);
            combo->setSelectedId(selected + 1, juce::sendNotificationAsync);
            juce::Timer::callPendingTimersSynchronously();
            check(combo->getSelectedId() == selected + 1,
                  "display refresh preserves pending user selection");
            combo->handleAsyncUpdate();
            check(std::abs(parameter->convertFrom0to1(parameter->getValue()) - selected) < 0.001F,
                  "deferred choice callback commits selected parameter");
            check(processor->undo(), "choice edit creates undo entry");
            juce::Thread::sleep(40);
            juce::Timer::callPendingTimersSynchronously();
            check(combo->getSelectedId() == before + 1, "undo updates choice display");
            check(processor->redo(), "choice edit can be redone");
            juce::Thread::sleep(40);
            juce::Timer::callPendingTimersSynchronously();
            check(combo->getSelectedId() == selected + 1, "redo updates choice display");
        }
        parameter->setValueNotifyingHost(parameter->convertTo0to1(0));
        juce::Thread::sleep(40);
        juce::Timer::callPendingTimersSynchronously();
        check(combo->getSelectedId() == 1, "host parameter change updates choice display");
    }
    editor->setVisible(false);
    editor->removeFromDesktop();
    processor->editorBeingDeleted(editor.get());
    editor.reset();
    return passed;
}
