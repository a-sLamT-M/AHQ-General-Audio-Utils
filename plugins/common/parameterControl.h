#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// The VST3 wrapper asks the editor which parameter belongs to the component under
// the cursor. Walk parents because a slider's text box is a separate component.
inline int controlParameterIndex(juce::Component &component) {
    for (auto *c = &component; c != nullptr; c = c->getParentComponent())
        if (c->getProperties().contains("ahqParameterIndex"))
            return int(c->getProperties()["ahqParameterIndex"]);
    return -1;
}

inline void bindParameterControl(juce::Component &component,
                                 juce::AudioProcessorParameter &parameter) {
    component.getProperties().set("ahqParameterIndex", parameter.getParameterIndex());
}

// Shared by every rotary control. Right-click must never start a value gesture.
class ParameterSlider : public juce::Slider {
  public:
    void bindParameter(juce::RangedAudioParameter &p) {
        parameter = &p;
        bindParameterControl(*this, p);
    }
    void mouseDown(const juce::MouseEvent &event) override {
        contextClick = event.mods.isPopupMenu();
        if (!contextClick) {
            juce::Slider::mouseDown(event);
            return;
        }
        if (auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
            if (auto *context = editor->getHostContext())
                if (parameter != nullptr)
                    if (auto menu = context->getContextMenuForParameter(parameter))
                        menu->showNativeMenu(event.getEventRelativeTo(editor).getPosition());
    }
    void mouseDrag(const juce::MouseEvent &event) override {
        if (!contextClick)
            juce::Slider::mouseDrag(event);
    }
    void mouseUp(const juce::MouseEvent &event) override {
        if (!contextClick)
            juce::Slider::mouseUp(event);
        contextClick = false;
    }

  private:
    juce::RangedAudioParameter *parameter = nullptr;
    bool contextClick = false;
};
