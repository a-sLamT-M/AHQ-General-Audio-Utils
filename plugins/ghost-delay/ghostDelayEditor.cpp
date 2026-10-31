#include "ghostDelayEditor.h"
#include "../common/ui/parameterGroups.h"
#include <cmath>
namespace {
constexpr ahq::ui::ParameterGroup uiGroups[]{
    {"Grains", 0, 5}, {"Feedback and random", 5, 3}, {"Mix", 8, 1}};
}

GhostDelayEditor::GhostDelayEditor(GhostDelayProcessor &owner)
    : AudioProcessorEditor(owner), ComponentMovementWatcher(this), processor(owner),
      display(owner.getAnalysis()) {
    addAndMakeVisible(presetBar);
    addAndMakeVisible(display.controls);
    display.controls.onChange = [this] {
        resized();
        repaint();
    };
    setResizable(true, true);
    setResizeLimits(1120, 900, 2000, 1500);
    setSize(1280, 1000);
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

void GhostDelayEditor::configureSlider(ParameterSlider &slider, int index,
                                       const juce::String &suffix) {
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 26);
    slider.setTextValueSuffix(suffix);
    slider.setNumDecimalPlacesToDisplay(index == 1 ? 1 : 3);
    constexpr std::array<const char *, 9> ids{
        "grain", "delay", "pitch", "scatter", "density", "feedback", "randomness", "seed", "mix"};
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
void GhostDelayEditor::timerCallback() {
    const auto values = processor.readValues();
    if (!dragging)
        for (size_t index = 0; index < 9; ++index)
            sliders[index].setValue(values[index], juce::dontSendNotification);
    undoButton.setEnabled(processor.canUndo());
    redoButton.setEnabled(processor.canRedo());
    display.refresh(spectrumVisible);
    repaint(display.getSpectrumBounds().withTrimmedBottom(-58).toNearestInt());
}

void GhostDelayEditor::paint(juce::Graphics &graphics) {
    appearance.paintBackground(graphics, getWidth());
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(23));
    graphics.drawText(processor.getName(), 20, 10, 370, 32, juce::Justification::left);
    appearance.paintFlow(graphics, {20, 48, 620, 24},
                         {"Input", "Grain processing + dry mix", "Output"});
    ahq::ui::paintGroups(graphics, appearance, labels, sliders, uiGroups);
    display.paintSpectrum(graphics);
    display.paintMeters(graphics);
}
void GhostDelayEditor::resized() {
    presetBar.setBounds(0, getHeight() - ahq::ui::PresetBar::height, getWidth(),
                        ahq::ui::PresetBar::height);
    undoButton.setBounds(getWidth() - 188, 12, 76, 28);
    redoButton.setBounds(getWidth() - 100, 12, 76, 28);
    presets.setBounds(getWidth() - 460, 12, 250, 28);
    display.controls.setBounds(20, 80, getWidth() - 40, 30);
    const int rightWidth = juce::jmax(
        320, int((getWidth() - 60) * (1 - display.controls.preferences.share.load() / 100.F)));
    const int rightX = getWidth() - rightWidth - 20;
    display.setSpectrumBounds({20, 122, float(rightX - 40), float(getHeight() - 122 - 154)});

    ahq::ui::layoutGroups(labels, sliders, uiGroups,
                          {rightX, 122, rightWidth, getHeight() - 122 - 104});
}
