#include "spaceLabEditor.h"
#include <cmath>

SpaceLabEditor::SpaceLabEditor(SpaceLabProcessor &owner)
    : AudioProcessorEditor(owner), ComponentMovementWatcher(this), processor(owner),
      display(owner.getAnalysis()) {
    setSize(660, 750);
    constexpr std::array<const char *, 10> names{
        "Width",        "Signed Haas", "Left tilt", "Right tilt", "Invert left",
        "Invert right", "Mod rate",    "Mod depth", "Split mono", "Wet mix"};
    constexpr std::array<const char *, 10> ids{"width",      "haas",        "leftTilt", "rightTilt",
                                               "invertLeft", "invertRight", "modRate",  "modDepth",
                                               "split",      "mix"};
    constexpr std::array<double, 10> minimum{0, -30, -12, -12, 0, 0, 0, 0, 0, 0},
        maximum{3, 30, 12, 12, 1, 1, 10, 20, 1, 1};
    for (int i = 0; i < 10; ++i) {
        labels[size_t(i)].setText(names[size_t(i)], juce::dontSendNotification);
        addAndMakeVisible(labels[size_t(i)]);
        if (i == 4 || i == 5 || i == 8) {
            const size_t toggleIndex = i == 4 ? 0 : i == 5 ? 1 : 2;
            auto &toggle = toggles[toggleIndex];
            toggle.setButtonText("On");
            toggle.setComponentID(ids[size_t(i)]);
            addAndMakeVisible(toggle);
            toggle.onClick = [this, i, toggleIndex] {
                auto values = processor.readValues();
                values[size_t(i)] = toggles[toggleIndex].getToggleState() ? 1.0F : 0.0F;
                processor.applyUserValues(values, "Switch parameter");
                timerCallback();
            };
        } else {
            configureSlider(sliders[size_t(i)], i, minimum[size_t(i)], maximum[size_t(i)],
                            i == 1 || i == 7   ? " ms"
                            : i == 2 || i == 3 ? " dB"
                            : i == 6           ? " Hz"
                                               : "");
            sliders[size_t(i)].setComponentID(ids[size_t(i)]);
        }
    }
    sliders[1].setTooltip("Positive Haas delays right; negative delays left. Negative Haas has no "
                          "effect in full split mode.");
    sliders[7].setTooltip(
        "Right delay moves from zero to depth; rate zero freezes it initially at depth/2.");
    toggles[2].setTooltip("Left is input M without left effects; right is delayed/EQ/polarity M. "
                          "Opposite-polarity stereo may cancel.");
    addAndMakeVisible(status);
    addAndMakeVisible(presets);
    presets.setTextWhenNothingSelected("Factory presets");
    presets.setTooltip(
        "Stereo width and delay starting points. Extreme width/polarity may cancel in mono.");
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
SpaceLabEditor::~SpaceLabEditor() {
    stopTimer();
    processor.setSpectrumEnabled(false);
}
void SpaceLabEditor::visibilityChanged() { updateSpectrumVisibility(); }
void SpaceLabEditor::parentHierarchyChanged() { updateSpectrumVisibility(); }
void SpaceLabEditor::updateSpectrumVisibility() {
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

void SpaceLabEditor::configureSlider(juce::Slider &slider, int index, double minimum,
                                     double maximum, const juce::String &suffix) {
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 26);
    slider.setRange(minimum, maximum);
    slider.setTextValueSuffix(suffix);
    slider.setNumDecimalPlacesToDisplay(index == 1 ? 1 : 3);
    constexpr std::array<const char *, 10> ids{"width",      "haas",        "leftTilt", "rightTilt",
                                               "invertLeft", "invertRight", "modRate",  "modDepth",
                                               "split",      "mix"};
    auto *parameter = processor.getParameters().getParameter(ids[size_t(index)]);
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
void SpaceLabEditor::timerCallback() {
    const auto values = processor.readValues();
    if (!dragging)
        for (size_t index = 0; index < 10; ++index)
            sliders[index].setValue(values[index], juce::dontSendNotification);
    toggles[0].setToggleState(values[4] == 1, juce::dontSendNotification);
    toggles[1].setToggleState(values[5] == 1, juce::dontSendNotification);
    toggles[2].setToggleState(values[8] == 1, juce::dontSendNotification);
    const bool splitEnabled = values[8] == 1;
    sliders[0].setEnabled(!splitEnabled);
    sliders[2].setEnabled(!splitEnabled);
    toggles[0].setEnabled(!splitEnabled);
    undoButton.setEnabled(processor.canUndo());
    redoButton.setEnabled(processor.canRedo());
    display.refresh(spectrumVisible);
    repaint();
}

void SpaceLabEditor::paint(juce::Graphics &graphics) {
    graphics.fillAll(juce::Colour(0xff24282d));
    graphics.setColour(juce::Colours::white);
    graphics.setFont(22.0F);
    graphics.drawText("AHQ Space Lab", 20, 12, 400, 32, juce::Justification::left);
    graphics.setFont(14.0F);
    graphics.setFont(12.0F);
    graphics.drawText(
        processor.readValues()[8] == 1
            ? "Split: left is dry M; right is processed M. Negative Haas has no effect."
            : "Width / Haas / independent colour / right delay modulation",
        20, 390, 620, 20, juce::Justification::left);
    {
        juce::Graphics::ScopedSaveState save(graphics);
        graphics.addTransform(juce::AffineTransform::translation(0, 140));
        display.paintMeters(graphics);
        display.paintSpectrum(graphics);
    }
}
void SpaceLabEditor::resized() {
    undoButton.setBounds(440, 55, 90, 28);
    redoButton.setBounds(550, 55, 90, 28);
    for (int index = 0; index < 10; ++index) {
        const bool first = index < 5;
        const int x = 15 + (first ? index : index - 5) * 128;
        const int width = 124;
        const int y = first ? 96 : 242;
        labels[size_t(index)].setBounds(x, y, width, 24);
        sliders[size_t(index)].setBounds(x, y + 24, width, 113);
        if (index == 4 || index == 5 || index == 8)
            toggles[size_t(index == 4   ? 0
                           : index == 5 ? 1
                                        : 2)]
                .setBounds(x + 20, y + 50, width - 30, 35);
    }
    presets.setBounds(20, 475, 250, 28);
    saveButton.setBounds(285, 475, 125, 28);
    loadButton.setBounds(425, 475, 125, 28);
    status.setBounds(20, 502, 620, 16);
}
void SpaceLabEditor::choosePresetFile(bool save) {
    if (fileChooser != nullptr)
        return;
    fileChooser = std::make_unique<juce::FileChooser>(
        save ? "Save Space Lab preset" : "Load Space Lab preset", juce::File{}, "*.ahqspacelab");
    juce::Component::SafePointer<SpaceLabEditor> safeThis(this);
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
