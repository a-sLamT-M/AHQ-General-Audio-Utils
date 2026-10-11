#include "orbitEditor.h"
#include <cmath>

OrbitEditor::OrbitEditor(OrbitProcessor &owner)
    : AudioProcessorEditor(owner), ComponentMovementWatcher(this), processor(owner),
      display(owner.getAnalysis()) {
    setSize(660, 610);
    constexpr std::array<const char *, 6> names{"Rate",  "Manual BPM", "Beats / turn",
                                                "Depth", "Phase",      "Wet mix"};
    constexpr std::array<const char *, 6> suffixes{" Hz", "", "", "", " deg", ""};
    constexpr std::array<int, 6> indices{0, 2, 3, 5, 6, 7};
    for (int index = 0; index < 6; ++index) {
        configureSlider(sliders[size_t(index)], indices[size_t(index)], suffixes[size_t(index)]);
        labels[size_t(index)].setText(names[size_t(index)], juce::dontSendNotification);
        addAndMakeVisible(labels[size_t(index)]);
    }
    speedMode.addItemList({"Hz speed", "BPM speed"}, 1);
    direction.addItemList({"Clockwise", "Counterclockwise"}, 1);
    for (auto *combo : {&speedMode, &direction, &presets})
        addAndMakeVisible(combo);
    speedMode.onChange = [this] {
        auto v = processor.readValues();
        v[1] = float(speedMode.getSelectedId() - 1);
        processor.applyUserValues(v, "Change speed mode");
    };
    direction.onChange = [this] {
        auto v = processor.readValues();
        v[4] = float(direction.getSelectedId() - 1);
        processor.applyUserValues(v, "Change direction");
    };
    sliders[3].setTooltip("Depth blends fixed-front and rotating responses; it is not distance.");
    sliders[1].setTooltip(
        "Used when valid host BPM is unavailable. BPM sets speed, not beat-grid locking.");
    addAndMakeVisible(status);
    addAndMakeVisible(tempoStatus);
    presets.setTextWhenNothingSelected("Factory presets");
    presets.setTooltip(
        "Headphone binaural presets use a fixed KEMAR response; perception varies by listener.");
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
    speedModeAttachment = std::make_unique<juce::ParameterAttachment>(
        *processor.getParameters().getParameter("sync"), [this](float value) {
            speedMode.setSelectedId(juce::roundToInt(value) + 1, juce::dontSendNotification);
        });
    speedModeAttachment->sendInitialUpdate();
    directionAttachment = std::make_unique<juce::ParameterAttachment>(
        *processor.getParameters().getParameter("direction"), [this](float value) {
            direction.setSelectedId(juce::roundToInt(value) + 1, juce::dontSendNotification);
        });
    directionAttachment->sendInitialUpdate();
    timerCallback();
    updateSpectrumVisibility();
}
OrbitEditor::~OrbitEditor() {
    stopTimer();
    processor.setSpectrumEnabled(false);
}
void OrbitEditor::visibilityChanged() { updateSpectrumVisibility(); }
void OrbitEditor::parentHierarchyChanged() { updateSpectrumVisibility(); }
void OrbitEditor::updateSpectrumVisibility() {
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

void OrbitEditor::configureSlider(juce::Slider &slider, int index, const juce::String &suffix) {
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 102, 26);
    slider.setTextValueSuffix(suffix);
    slider.setNumDecimalPlacesToDisplay(index == 1 ? 1 : 3);
    constexpr std::array<const char *, 8> ids{"rate",      "sync",  "bpm",   "beats",
                                              "direction", "depth", "phase", "mix"};
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
void OrbitEditor::timerCallback() {
    const auto values = processor.readValues();
    if (!dragging)
        for (size_t index = 0; index < 6; ++index) {
            constexpr std::array<int, 6> indices{0, 2, 3, 5, 6, 7};
            sliders[index].setValue(values[size_t(indices[index])], juce::dontSendNotification);
        }
    const auto tempo = processor.readTempo();
    juce::String tempoText;
    switch (tempo.source) {
    case OrbitProcessor::TempoSource::hz:
        tempoText = "Last audio: Hz speed";
        break;
    case OrbitProcessor::TempoSource::host:
        tempoText = "Last audio: Host BPM " + juce::String(tempo.bpm, 1);
        break;
    case OrbitProcessor::TempoSource::manual:
        tempoText = "Last audio: Manual BPM " + juce::String(tempo.bpm, 1);
        break;
    }
    tempoStatus.setText(tempoText, juce::dontSendNotification);
    undoButton.setEnabled(processor.canUndo());
    redoButton.setEnabled(processor.canRedo());
    display.refresh(spectrumVisible);
    repaint();
}

void OrbitEditor::paint(juce::Graphics &graphics) {
    graphics.fillAll(juce::Colour(0xff24282d));
    graphics.setColour(juce::Colours::white);
    graphics.setFont(22.0F);
    graphics.drawText("AHQ Orbit", 20, 12, 210, 32, juce::Justification::left);
    graphics.setFont(14.0F);
    graphics.setFont(12.0F);
    graphics.drawText("Headphones | Wet source: mono | Fixed KEMAR response", 20, 252, 620, 18,
                      juce::Justification::left);
    display.paintMeters(graphics);
    display.paintSpectrum(graphics);
}
void OrbitEditor::resized() {
    undoButton.setBounds(440, 55, 90, 28);
    redoButton.setBounds(550, 55, 90, 28);
    speedMode.setBounds(20, 55, 160, 28);
    direction.setBounds(195, 55, 225, 28);
    tempoStatus.setBounds(235, 12, 405, 32);
    for (int index = 0; index < 6; ++index) {
        labels[size_t(index)].setBounds(10 + index * 107, 96, 105, 24);
        sliders[size_t(index)].setBounds(10 + index * 107, 120, 105, 128);
    }
    presets.setBounds(20, 335, 250, 28);
    saveButton.setBounds(285, 335, 125, 28);
    loadButton.setBounds(425, 335, 125, 28);
    status.setBounds(20, 362, 620, 16);
}
void OrbitEditor::choosePresetFile(bool save) {
    if (fileChooser != nullptr)
        return;
    fileChooser = std::make_unique<juce::FileChooser>(
        save ? "Save orbit preset" : "Load orbit preset", juce::File{}, "*.ahqorbit");
    juce::Component::SafePointer<OrbitEditor> safeThis(this);
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
