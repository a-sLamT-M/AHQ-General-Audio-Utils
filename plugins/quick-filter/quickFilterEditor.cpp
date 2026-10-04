#include "quickFilterEditor.h"
#include <cmath>

QuickFilterEditor::QuickFilterEditor(QuickFilterProcessor &owner)
    : AudioProcessorEditor(owner), ComponentMovementWatcher(this), processor(owner),
      display(owner.getAnalysis()) {
    setSize(660, 610);
    configureSlider(frequency, 1, " Hz");
    frequency.setSkewFactorFromMidPoint(1000);
    configureSlider(q, 2, "");
    q.setTooltip("Q controls the first filter section; later sections use Q = 0.707.");
    configureSlider(mix, 4, "");
    frequencyLabel.setText("Frequency", juce::dontSendNotification);
    qLabel.setText("Q", juce::dontSendNotification);
    mixLabel.setText("Wet mix (0-1)", juce::dontSendNotification);
    for (auto *label : {&frequencyLabel, &qLabel, &mixLabel, &status})
        addAndMakeVisible(label);
    mode.addItemList({"Low pass", "High pass"}, 1);
    slope.addItemList({"12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct"}, 1);
    for (auto *combo : {&mode, &slope, &presets})
        addAndMakeVisible(combo);
    presets.setTextWhenNothingSelected("Factory presets");
    for (int index = 0; index < processor.getNumPrograms(); ++index)
        presets.addItem(processor.getProgramName(index), index + 1);
    mode.onChange = [this] {
        auto values = processor.readValues();
        values[0] = float(mode.getSelectedId() - 1);
        processor.applyUserValues(values, "Change mode");
    };
    slope.onChange = [this] {
        auto values = processor.readValues();
        values[3] = float(slope.getSelectedId() - 1);
        processor.applyUserValues(values, "Change slope");
    };
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
    modeAttachment = std::make_unique<juce::ParameterAttachment>(
        *processor.getParameters().getParameter("mode"), [this](float value) {
            mode.setSelectedId(juce::roundToInt(value) + 1, juce::dontSendNotification);
        });
    modeAttachment->sendInitialUpdate();
    slopeAttachment = std::make_unique<juce::ParameterAttachment>(
        *processor.getParameters().getParameter("slope"), [this](float value) {
            slope.setSelectedId(juce::roundToInt(value) + 1, juce::dontSendNotification);
        });
    slopeAttachment->sendInitialUpdate();
    timerCallback();
    updateSpectrumVisibility();
}
QuickFilterEditor::~QuickFilterEditor() {
    stopTimer();
    processor.setSpectrumEnabled(false);
}
void QuickFilterEditor::visibilityChanged() { updateSpectrumVisibility(); }
void QuickFilterEditor::parentHierarchyChanged() { updateSpectrumVisibility(); }
void QuickFilterEditor::updateSpectrumVisibility() {
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

void QuickFilterEditor::configureSlider(juce::Slider &slider, int index,
                                        const juce::String &suffix) {
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 26);
    slider.setTextValueSuffix(suffix);
    slider.setNumDecimalPlacesToDisplay(index == 1 ? 1 : 3);
    constexpr std::array<const char *, 5> ids{"mode", "frequency", "q", "slope", "mix"};
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
void QuickFilterEditor::timerCallback() {
    const auto values = processor.readValues();
    if (!dragging) {
        frequency.setValue(values[1], juce::dontSendNotification);
        q.setValue(values[2], juce::dontSendNotification);
        mix.setValue(values[4], juce::dontSendNotification);
    }
    undoButton.setEnabled(processor.canUndo());
    redoButton.setEnabled(processor.canRedo());
    display.refresh(spectrumVisible);
    repaint();
}

void QuickFilterEditor::paint(juce::Graphics &graphics) {
    graphics.fillAll(juce::Colour(0xff24282d));
    graphics.setColour(juce::Colours::white);
    graphics.setFont(22.0F);
    graphics.drawText("AHQ Quick Filter", 20, 12, 400, 32, juce::Justification::left);
    graphics.setFont(14.0F);
    display.paintMeters(graphics);
    display.paintSpectrum(graphics);
}
void QuickFilterEditor::resized() {
    mode.setBounds(20, 55, 190, 28);
    slope.setBounds(230, 55, 190, 28);
    undoButton.setBounds(440, 55, 90, 28);
    redoButton.setBounds(550, 55, 90, 28);
    frequencyLabel.setBounds(20, 96, 190, 24);
    qLabel.setBounds(230, 96, 190, 24);
    mixLabel.setBounds(440, 96, 200, 24);
    frequency.setBounds(20, 120, 190, 145);
    q.setBounds(230, 120, 190, 145);
    mix.setBounds(440, 120, 200, 145);
    presets.setBounds(20, 335, 250, 28);
    saveButton.setBounds(285, 335, 125, 28);
    loadButton.setBounds(425, 335, 125, 28);
    status.setBounds(20, 362, 620, 16);
}
void QuickFilterEditor::choosePresetFile(bool save) {
    if (fileChooser != nullptr)
        return;
    fileChooser = std::make_unique<juce::FileChooser>(
        save ? "Save filter preset" : "Load filter preset", juce::File{}, "*.ahqfilter");
    juce::Component::SafePointer<QuickFilterEditor> safeThis(this);
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
