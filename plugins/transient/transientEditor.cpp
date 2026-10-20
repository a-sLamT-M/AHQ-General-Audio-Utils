#include "transientEditor.h"
#include <cmath>

TransientEditor::TransientEditor(TransientProcessor &owner)
    : AudioProcessorEditor(owner), ComponentMovementWatcher(this), processor(owner),
      display(owner.getAnalysis()) {
    addAndMakeVisible(presetBar);
    setSize(660, 676);
    const std::array<const char *, 5> names{"Attack", "Sustain", "Sensitivity", "Time",
                                            "Wet mix (0-1)"};
    constexpr std::array<const char *, 5> suffixes{" dB", " dB", " dBFS", " ms", ""};
    for (int index = 0; index < 5; ++index) {
        configureSlider(sliders[size_t(index)], index, suffixes[size_t(index)]);
        labels[size_t(index)].setText(names[size_t(index)], juce::dontSendNotification);
        addAndMakeVisible(labels[size_t(index)]);
    }
    sliders[2].setTooltip(
        "Envelope noise sensitivity, not source recognition or a compressor threshold.");
    addAndMakeVisible(status);
    addAndMakeVisible(presets);
    presets.setTextWhenNothingSelected("Factory presets");
    presets.setTooltip(
        "Instrument presets are starting points; no instrument recognition is used.");
    for (int index = 0; index < processor.getNumPrograms(); ++index)
        presets.addItem(processor.getProgramName(index), index + 1);
    presets.onChange = [this] {
        processor.loadFactoryPreset(presets.getSelectedId() - 1);
        presets.setSelectedId(0, juce::dontSendNotification);
    };
    for (auto *button : {&undoButton, &redoButton})
        addAndMakeVisible(button);
    undoButton.onClick = [this] {
        processor.undo();
        timerCallback();
    };
    redoButton.onClick = [this] {
        processor.redo();
        timerCallback();
    };
    timerCallback();
    updateSpectrumVisibility();
}
TransientEditor::~TransientEditor() {
    stopTimer();
    processor.setSpectrumEnabled(false);
}
void TransientEditor::visibilityChanged() { updateSpectrumVisibility(); }
void TransientEditor::parentHierarchyChanged() { updateSpectrumVisibility(); }
void TransientEditor::updateSpectrumVisibility() {
    const bool visible = isShowing();
    if (visible == spectrumVisible)
        return;
    spectrumVisible = visible;
    processor.setSpectrumEnabled(visible);
    if (visible)
        startTimerHz(30);
    else
        stopTimer();
}

void TransientEditor::configureSlider(ParameterSlider &slider, int index,
                                      const juce::String &suffix) {
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 26);
    slider.setTextValueSuffix(suffix);
    slider.setNumDecimalPlacesToDisplay(index == 1 ? 1 : 3);
    constexpr std::array<const char *, 5> ids{"attack", "sustain", "sensitivity", "time", "mix"};
    auto *parameter = processor.getParameters().getParameter(ids[size_t(index)]);
    slider.bindParameter(*parameter);
    const auto &range = parameter->getNormalisableRange();
    slider.setRange(range.start, range.end, range.interval);
    slider.onDragStart = [this, parameter] {
        dragBefore = processor.readValues();
        dragging = true;
        parameter->beginChangeGesture();
    };
    slider.onValueChange = [this, &slider, index, parameter] {
        if (dragging) {
            parameter->setValueNotifyingHost(parameter->convertTo0to1(float(slider.getValue())));
        } else {
            auto values = processor.readValues();
            values[size_t(index)] = float(slider.getValue());
            processor.applyUserValues(values, "Edit parameter");
        }
    };
    slider.onDragEnd = [this, parameter, index] {
        parameter->endChangeGesture();
        dragging = false;
        processor.recordUserEdit(dragBefore, index, "Edit parameter");
    };
}
void TransientEditor::timerCallback() {
    const auto values = processor.readValues();
    if (!dragging)
        for (size_t index = 0; index < 5; ++index)
            sliders[index].setValue(values[index], juce::dontSendNotification);
    undoButton.setEnabled(processor.canUndo());
    redoButton.setEnabled(processor.canRedo());
    display.refresh(spectrumVisible);
    repaint();
}

void TransientEditor::paint(juce::Graphics &graphics) {
    appearance.paintBackground(graphics, getWidth());
    graphics.setColour(juce::Colours::white);
    graphics.setFont(22.0F);
    graphics.drawText("AHQ Transient", 20, 12, 400, 32, juce::Justification::left);
    graphics.setFont(14.0F);
    display.paintMeters(graphics);
    display.paintSpectrum(graphics);
}
void TransientEditor::resized() {
    presetBar.setBounds(0, getHeight() - ahq::ui::PresetBar::height, getWidth(),
                        ahq::ui::PresetBar::height);
    undoButton.setBounds(440, 55, 90, 28);
    redoButton.setBounds(550, 55, 90, 28);
    for (int index = 0; index < 5; ++index) {
        labels[size_t(index)].setBounds(15 + index * 128, 96, 124, 24);
        sliders[size_t(index)].setBounds(15 + index * 128, 120, 124, 145);
    }
    presets.setBounds(20, 335, 250, 28);
}
