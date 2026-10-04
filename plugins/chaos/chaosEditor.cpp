#include "chaosEditor.h"
#include <cmath>

ChaosEditor::ChaosEditor(ChaosProcessor &owner)
    : AudioProcessorEditor(owner), ComponentMovementWatcher(this), processor(owner),
      display(owner.getAnalysis()) {
    setSize(660, 750);
    constexpr std::array<const char *, 10> names{
        "Slice",      "Probability", "Repeats",        "Reverse chance", "Pitch range",
        "Pan amount", "Drive",       "Distort chance", "Seed (integer)", "Wet mix"};
    constexpr std::array<double, 10> minimum{10, 0, 1, 0, 0, 0, 0, 0, 0, 0},
        maximum{250, 1, 8, 1, 24, 1, 36, 1, 65535, 1};
    for (int index = 0; index < 10; ++index) {
        configureSlider(sliders[size_t(index)], index, minimum[size_t(index)],
                        maximum[size_t(index)],
                        index == 0   ? " ms"
                        : index == 4 ? " st"
                        : index == 6 ? " dB"
                                     : "");
        labels[size_t(index)].setText(names[size_t(index)], juce::dontSendNotification);
        addAndMakeVisible(labels[size_t(index)]);
    }
    for (size_t index : {size_t(2), size_t(8)}) {
        sliders[index].setRange(minimum[index], maximum[index], 1);
        sliders[index].setNumDecimalPlacesToDisplay(0);
    }
    sliders[8].setTooltip(
        "Integer seed 0-65535 reproduces decisions after reset. Reapplying it preserves playback.");
    sliders[1].setTooltip("Probability of selecting a past slice at the next decision. Zero passes "
                          "dry after the current event.");
    sliders[4].setTooltip(
        "Random pitch within +/- range; creative linear interpolation can alias.");
    sliders[3].setTooltip("Probability of reading each captured slice backwards.");
    addAndMakeVisible(status);
    addAndMakeVisible(presets);
    presets.setTextWhenNothingSelected("Factory presets");
    presets.setTooltip(
        "Past slices repeat with short edge fades; controls latch at the next decision.");
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
ChaosEditor::~ChaosEditor() {
    stopTimer();
    processor.setSpectrumEnabled(false);
}
void ChaosEditor::visibilityChanged() { updateSpectrumVisibility(); }
void ChaosEditor::parentHierarchyChanged() { updateSpectrumVisibility(); }
void ChaosEditor::updateSpectrumVisibility() {
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

void ChaosEditor::configureSlider(juce::Slider &slider, int index, double minimum, double maximum,
                                  const juce::String &suffix) {
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 26);
    slider.setRange(minimum, maximum);
    slider.setTextValueSuffix(suffix);
    slider.setNumDecimalPlacesToDisplay(index == 1 ? 1 : 3);
    constexpr std::array<const char *, 10> ids{"slice",      "probability", "repeats", "reverse",
                                               "pitchRange", "pan",         "drive",   "distortion",
                                               "seed",       "mix"};
    slider.setComponentID(ids[size_t(index)]);
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
void ChaosEditor::timerCallback() {
    const auto values = processor.readValues();
    if (!dragging)
        for (size_t index = 0; index < 10; ++index)
            sliders[index].setValue(values[index], juce::dontSendNotification);
    undoButton.setEnabled(processor.canUndo());
    redoButton.setEnabled(processor.canRedo());
    display.refresh(spectrumVisible);
    repaint();
}

void ChaosEditor::paint(juce::Graphics &graphics) {
    graphics.fillAll(juce::Colour(0xff24282d));
    graphics.setColour(juce::Colours::white);
    graphics.setFont(22.0F);
    graphics.drawText("AHQ Chaos", 20, 12, 400, 32, juce::Justification::left);
    graphics.setFont(14.0F);
    graphics.setFont(12.0F);
    graphics.drawText("Past slices | Repeats and reverse | Creative pitch | No feedback", 20, 390,
                      620, 20, juce::Justification::left);
    {
        juce::Graphics::ScopedSaveState save(graphics);
        graphics.addTransform(juce::AffineTransform::translation(0, 140));
        display.paintMeters(graphics);
        display.paintSpectrum(graphics);
    }
}
void ChaosEditor::resized() {
    undoButton.setBounds(440, 55, 90, 28);
    redoButton.setBounds(550, 55, 90, 28);
    for (int index = 0; index < 10; ++index) {
        const bool first = index < 5;
        const int x = 15 + (first ? index * 128 : (index - 5) * 128);
        const int width = 124;
        const int y = first ? 96 : 242;
        labels[size_t(index)].setBounds(x, y, width, 24);
        sliders[size_t(index)].setBounds(x, y + 24, width, 113);
    }
    presets.setBounds(20, 475, 250, 28);
    saveButton.setBounds(285, 475, 125, 28);
    loadButton.setBounds(425, 475, 125, 28);
    status.setBounds(20, 502, 620, 16);
}
void ChaosEditor::choosePresetFile(bool save) {
    if (fileChooser != nullptr)
        return;
    fileChooser = std::make_unique<juce::FileChooser>(
        save ? "Save Chaos preset" : "Load Chaos preset", juce::File{}, "*.ahqchaos");
    juce::Component::SafePointer<ChaosEditor> safeThis(this);
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
