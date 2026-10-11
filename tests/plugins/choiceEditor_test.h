#pragma once
#include <cmath>
#include <iostream>
#include <juce_audio_utils/juce_audio_utils.h>

struct ChoiceParameter {
    const char *firstItem;
    const char *parameterId;
};

// Exercise the ordering that occurs when a display timer runs before ComboBox's
// queued onChange callback. No mouse timing or private editor access is required.
template <typename Processor>
bool testChoiceEditor(std::initializer_list<ChoiceParameter> choices) {
    auto processor = std::make_unique<Processor>();
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorAndMakeActive());
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    editor->setVisible(true);
    bool passed = true;
    auto check = [&](bool condition, const char *message) {
        if (!condition)
            std::cerr << "FAILED: " << message << '\n';
        passed &= condition;
    };
    for (const auto &choice : choices) {
        juce::ComboBox *combo = nullptr;
        for (auto *child : editor->getChildren())
            if (auto *candidate = dynamic_cast<juce::ComboBox *>(child))
                if (candidate->getItemText(0) == choice.firstItem)
                    combo = candidate;
        auto *parameter = processor->getParameters().getParameter(choice.parameterId);
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
