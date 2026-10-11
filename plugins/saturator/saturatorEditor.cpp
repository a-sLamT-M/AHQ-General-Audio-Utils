#include "saturatorEditor.h"
#include <cmath>

SaturatorEditor::SaturatorEditor(SaturatorProcessor &owner)
    : AudioProcessorEditor(owner), ComponentMovementWatcher(this), processor(owner),
      display(owner.getAnalysis()) {
    setSize(660, 610);
    constexpr std::array<const char *, 4> names{"Drive", "Bias", "Tone", "Wet mix (0-1)"};
    constexpr std::array<const char *, 4> suffixes{" dB", "", "", ""};
    constexpr std::array<int, 4> indices{1, 2, 3, 5};
    for (int index = 0; index < 4; ++index) {
        configureSlider(sliders[size_t(index)], indices[size_t(index)], suffixes[size_t(index)]);
        labels[size_t(index)].setText(names[size_t(index)], juce::dontSendNotification);
        addAndMakeVisible(labels[size_t(index)]);
    }
    model.addItemList({"Soft clip", "Hard clip", "Tube style", "Tape style", "Wavefolder",
                       "Rectifier", "Bitcrusher"},
                      1);
    quality.addItemList({"1x", "2x", "4x"}, 1);
    model.setTooltip("Digital curves, not circuit simulations. Asymmetric curves may create DC.");
    quality.setTooltip(
        "Oversampling reduces some aliases; finite FIR filters cannot remove all aliasing.");
    for (auto *combo : {&model, &quality, &presets})
        addAndMakeVisible(combo);
    model.onChange = [this] {
        auto v = processor.readValues();
        v[0] = float(model.getSelectedId() - 1);
        processor.applyUserValues(v, "Change model");
    };
    quality.onChange = [this] {
        auto v = processor.readValues();
        v[4] = float(quality.getSelectedId() - 1);
        processor.applyUserValues(v, "Change quality");
    };
    addAndMakeVisible(status);
    presets.setTextWhenNothingSelected("Factory presets");
    presets.setTooltip(
        "Instrument presets are starting points; no instrument recognition is used.");
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
    // Parameter changes drive choices; timer polling can overwrite a pending menu selection.
    modelAttachment = std::make_unique<juce::ParameterAttachment>(
        *processor.getParameters().getParameter("model"), [this](float value) {
            model.setSelectedId(juce::roundToInt(value) + 1, juce::dontSendNotification);
        });
    modelAttachment->sendInitialUpdate();
    qualityAttachment = std::make_unique<juce::ParameterAttachment>(
        *processor.getParameters().getParameter("oversampling"), [this](float value) {
            quality.setSelectedId(juce::roundToInt(value) + 1, juce::dontSendNotification);
        });
    qualityAttachment->sendInitialUpdate();
    timerCallback();
    updateSpectrumVisibility();
}
SaturatorEditor::~SaturatorEditor() {
    stopTimer();
    processor.setSpectrumEnabled(false);
}
void SaturatorEditor::visibilityChanged() { updateSpectrumVisibility(); }
void SaturatorEditor::parentHierarchyChanged() { updateSpectrumVisibility(); }
void SaturatorEditor::updateSpectrumVisibility() {
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

void SaturatorEditor::configureSlider(juce::Slider &slider, int index, const juce::String &suffix) {
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 26);
    slider.setTextValueSuffix(suffix);
    slider.setNumDecimalPlacesToDisplay(index == 1 ? 1 : 3);
    constexpr std::array<const char *, 6> ids{"model", "drive",        "bias",
                                              "tone",  "oversampling", "mix"};
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
void SaturatorEditor::timerCallback() {
    const auto values = processor.readValues();
    if (!dragging)
        for (size_t index = 0; index < 4; ++index) {
            constexpr std::array<int, 4> indices{1, 2, 3, 5};
            sliders[index].setValue(values[size_t(indices[index])], juce::dontSendNotification);
        }
    undoButton.setEnabled(processor.canUndo());
    redoButton.setEnabled(processor.canRedo());
    display.refresh(spectrumVisible);
    repaint();
}

void SaturatorEditor::paint(juce::Graphics &graphics) {
    graphics.fillAll(juce::Colour(0xff24282d));
    graphics.setColour(juce::Colours::white);
    graphics.setFont(22.0F);
    graphics.drawText("AHQ Saturator", 20, 12, 400, 32, juce::Justification::left);
    graphics.setFont(14.0F);
    graphics.setFont(11.0F);
    graphics.drawText(
        "Digital curves; asymmetric shaping may add DC. Oversampling reduces some aliases.", 20,
        255, 620, 18, juce::Justification::left);
    display.paintMeters(graphics);
    display.paintSpectrum(graphics);
}
void SaturatorEditor::resized() {
    undoButton.setBounds(440, 55, 90, 28);
    redoButton.setBounds(550, 55, 90, 28);
    model.setBounds(20, 55, 245, 28);
    quality.setBounds(280, 55, 140, 28);
    for (int index = 0; index < 4; ++index) {
        labels[size_t(index)].setBounds(15 + index * 160, 96, 154, 24);
        sliders[size_t(index)].setBounds(15 + index * 160, 120, 154, 132);
    }
    presets.setBounds(20, 335, 250, 28);
    saveButton.setBounds(285, 335, 125, 28);
    loadButton.setBounds(425, 335, 125, 28);
    status.setBounds(20, 362, 620, 16);
}
void SaturatorEditor::choosePresetFile(bool save) {
    if (fileChooser != nullptr)
        return;
    fileChooser = std::make_unique<juce::FileChooser>(
        save ? "Save saturator preset" : "Load saturator preset", juce::File{}, "*.ahqsaturator");
    juce::Component::SafePointer<SaturatorEditor> safeThis(this);
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
