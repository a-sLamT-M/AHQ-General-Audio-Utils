#include "ghostDelayEditor.h"
#include <cmath>

GhostDelayEditor::GhostDelayEditor(GhostDelayProcessor &owner)
    : AudioProcessorEditor(owner), ComponentMovementWatcher(this), processor(owner),
      display(owner.getAnalysis()) {
    setSize(660, 750);
    constexpr std::array<const char *, 9> names{"Grain",      "Delay",          "Pitch",
                                                "Scatter",    "Density",        "Feedback",
                                                "Randomness", "Seed (integer)", "Wet mix"};
    constexpr std::array<const char *, 9> suffixes{" ms", " ms", " st", "", " Hz", "", "", "", ""};
    for (int index = 0; index < 9; ++index) {
        configureSlider(sliders[size_t(index)], index, suffixes[size_t(index)]);
        labels[size_t(index)].setText(names[size_t(index)], juce::dontSendNotification);
        addAndMakeVisible(labels[size_t(index)]);
    }
    sliders[7].setNumDecimalPlacesToDisplay(0);
    sliders[7].setTooltip("Integer seeds 0-65535 reproduce grain scatter, pitch variation and pan "
                          "events after reset.");
    sliders[2].setTooltip(
        "Creative interpolated pitch, not an antialiased high-fidelity pitch shifter.");
    sliders[1].setTooltip(
        "Delay may be raised internally to keep pitched grains behind the input writer.");
    addAndMakeVisible(status);
    addAndMakeVisible(presets);
    presets.setTextWhenNothingSelected("Factory presets");
    presets.setTooltip("Creative grain-delay starting points; feedback may produce a long decay.");
    for (int index = 0; index < processor.getNumPrograms(); ++index)
        presets.addItem(processor.getProgramName(index), index + 1);
    presets.onChange = [this] {
        processor.loadFactoryPreset(presets.getSelectedId() - 1);
        presets.setSelectedId(0, juce::dontSendNotification);
    };
    for (auto *button : {&saveButton, &loadButton, &undoButton, &redoButton})
        addAndMakeVisible(button);
    saveButton.onClick = [this] { choosePresetFile(true); };
    loadButton.onClick = [this] { choosePresetFile(false); };
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
GhostDelayEditor::~GhostDelayEditor() {
    stopTimer();
    processor.setSpectrumEnabled(false);
}
void GhostDelayEditor::visibilityChanged() { updateSpectrumVisibility(); }
void GhostDelayEditor::parentHierarchyChanged() { updateSpectrumVisibility(); }
void GhostDelayEditor::updateSpectrumVisibility() {
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

void GhostDelayEditor::configureSlider(juce::Slider &slider, int index,
                                       const juce::String &suffix) {
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 26);
    slider.setTextValueSuffix(suffix);
    slider.setNumDecimalPlacesToDisplay(index == 1 ? 1 : 3);
    constexpr std::array<const char *, 9> ids{
        "grain", "delay", "pitch", "scatter", "density", "feedback", "randomness", "seed", "mix"};
    auto *parameter = processor.getParameters().getParameter(ids[size_t(index)]);
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
void GhostDelayEditor::timerCallback() {
    const auto values = processor.readValues();
    if (!dragging)
        for (size_t index = 0; index < 9; ++index)
            sliders[index].setValue(values[index], juce::dontSendNotification);
    undoButton.setEnabled(processor.canUndo());
    redoButton.setEnabled(processor.canRedo());
    display.refresh(spectrumVisible);
    repaint();
}

void GhostDelayEditor::paint(juce::Graphics &graphics) {
    graphics.fillAll(juce::Colour(0xff24282d));
    graphics.setColour(juce::Colours::white);
    graphics.setFont(22.0F);
    graphics.drawText("AHQ Ghost Delay", 20, 12, 400, 32, juce::Justification::left);
    graphics.setFont(14.0F);
    graphics.setFont(12.0F);
    graphics.drawText("Seed 0-65535 | Estimated decay below -80 dB: " +
                          juce::String(processor.getTailLengthSeconds(), 1) + " s",
                      20, 390, 620, 20, juce::Justification::left);
    {
        juce::Graphics::ScopedSaveState save(graphics);
        graphics.addTransform(juce::AffineTransform::translation(0, 140));
        display.paintMeters(graphics);
        display.paintSpectrum(graphics);
    }
}
void GhostDelayEditor::resized() {
    undoButton.setBounds(440, 55, 90, 28);
    redoButton.setBounds(550, 55, 90, 28);
    for (int index = 0; index < 9; ++index) {
        const bool first = index < 5;
        const int x = 15 + (first ? index * 128 : (index - 5) * 160);
        const int width = first ? 124 : 154;
        const int y = first ? 96 : 242;
        labels[size_t(index)].setBounds(x, y, width, 24);
        sliders[size_t(index)].setBounds(x, y + 24, width, 113);
    }
    presets.setBounds(20, 475, 250, 28);
    saveButton.setBounds(285, 475, 125, 28);
    loadButton.setBounds(425, 475, 125, 28);
    status.setBounds(20, 502, 620, 16);
}
void GhostDelayEditor::choosePresetFile(bool save) {
    if (fileChooser != nullptr)
        return;
    fileChooser = std::make_unique<juce::FileChooser>(
        save ? "Save ghost preset" : "Load ghost preset", juce::File{}, "*.ahqghost");
    juce::Component::SafePointer<GhostDelayEditor> safeThis(this);
    const int flags = juce::FileBrowserComponent::canSelectFiles |
                      (save ? juce::FileBrowserComponent::saveMode |
                                  juce::FileBrowserComponent::warnAboutOverwriting
                            : juce::FileBrowserComponent::openMode);
    fileChooser->launchAsync(flags, [safeThis, save](const juce::FileChooser &chooser) {
        if (safeThis == nullptr)
            return;
        auto &editor = *safeThis.getComponent();
        auto file = chooser.getResult();
        if (file != juce::File{}) {
            bool success = false;
            if (save) {
                juce::TemporaryFile temporary(file);
                success = temporary.getFile().replaceWithText(editor.processor.createPresetXml()) &&
                          temporary.overwriteTargetFileWithTemporary();
            } else if (file.getSize() > 0 && file.getSize() <= 65536) {
                success = editor.processor.loadPresetXml(file.loadFileAsString());
            }
            editor.status.setText(success ? "Preset saved/loaded" : "Preset operation failed",
                                  juce::dontSendNotification);
        }
        // Keep the chooser alive until its own completion callback has returned.
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr)
                safeThis->fileChooser.reset();
        });
    });
}
