#include "quickFilterEditor.h"
#include "../common/ui/parameterGroups.h"
#include <cmath>

QuickFilterEditor::QuickFilterEditor(QuickFilterProcessor &owner)
    : AudioProcessorEditor(owner), ComponentMovementWatcher(this), processor(owner),
      display(owner.getAnalysis()) {
    addAndMakeVisible(presetBar);
    addAndMakeVisible(display.controls);
    display.controls.onChange = [this] {
        resized();
        repaint();
    };
    setResizable(true, true);
    setResizeLimits(1120, 850, 2000, 1500);
    setSize(1280, 940);
    configureSlider(frequency, 1, " Hz");
    frequency.setSkewFactorFromMidPoint(1000);
    configureSlider(q, 2, "");
    q.setTooltip("Q controls the first filter section; later sections use Q = 0.707.");
    configureSlider(mix, 4, "");
    frequencyLabel.setText("Frequency", juce::dontSendNotification);
    qLabel.setText("Q", juce::dontSendNotification);
    mixLabel.setText("Wet mix (0-1)", juce::dontSendNotification);
    for (auto *label : {&frequencyLabel, &qLabel, &mixLabel})
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

void QuickFilterEditor::configureSlider(ParameterSlider &slider, int index,
                                        const juce::String &suffix) {
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 26);
    slider.setTextValueSuffix(suffix);
    slider.setNumDecimalPlacesToDisplay(index == 1 ? 1 : 3);
    constexpr std::array<const char *, 5> ids{"mode", "frequency", "q", "slope", "mix"};
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
    repaint(display.getSpectrumBounds().withTrimmedBottom(-58).toNearestInt());
}

void QuickFilterEditor::paint(juce::Graphics &graphics) {
    appearance.paintBackground(graphics, getWidth());
    graphics.setColour(juce::Colours::white);
    graphics.setFont(22.0F);
    graphics.drawText("AHQ Quick Filter", 20, 12, 400, 32, juce::Justification::left);
    graphics.setFont(14.0F);
    appearance.paintFlow(graphics, {20, 48, 620, 24}, {"Input", "Filter + dry / wet", "Output"});
    std::array<juce::Label *, 3> names{&frequencyLabel, &qLabel, &mixLabel};
    std::array<juce::Slider *, 3> controls{&frequency, &q, &mix};
    const ahq::ui::ParameterGroup groups[]{{"Filter", 0, 2}, {"Mix", 2, 1}};
    ahq::ui::paintGroups(graphics, appearance, names, controls, groups);
    display.paintMeters(graphics);
    display.paintSpectrum(graphics);
}
void QuickFilterEditor::resized() {
    presetBar.setBounds(0, getHeight() - ahq::ui::PresetBar::height, getWidth(),
                        ahq::ui::PresetBar::height);
    undoButton.setBounds(getWidth() - 188, 12, 76, 28);
    redoButton.setBounds(getWidth() - 100, 12, 76, 28);
    presets.setBounds(getWidth() - 460, 12, 250, 28);
    display.controls.setBounds(20, 80, getWidth() - 40, 30);
    const int width = juce::jmax(
        320, int((getWidth() - 60) * (1 - display.controls.preferences.share.load() / 100.F)));
    const int x = getWidth() - width - 20;
    display.setSpectrumBounds({20, 122, float(x - 40), float(getHeight() - 276)});
    mode.setBounds(x, 122, width / 2 - 4, 28);
    slope.setBounds(x + width / 2, 122, width / 2, 28);
    std::array<juce::Label *, 3> names{&frequencyLabel, &qLabel, &mixLabel};
    std::array<juce::Slider *, 3> controls{&frequency, &q, &mix};
    const ahq::ui::ParameterGroup groups[]{{"Filter", 0, 2}, {"Mix", 2, 1}};
    ahq::ui::layoutGroups(names, controls, groups, {x, 164, width, getHeight() - 270});
}
