#include "chaosEditor.h"
#include "../common/ui/parameterGroups.h"
#include <cmath>
namespace {
constexpr ahq::ui::ParameterGroup uiGroups[]{
    {"Slices", 0, 3}, {"Pitch and position", 3, 3}, {"Distortion", 6, 2}, {"Seed and mix", 8, 2}};
}

ChaosEditor::ChaosEditor(ChaosProcessor &owner)
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
    setSize(1600, 1000);
    constexpr std::array<const char *, 10> names{
        "Slice",      "Probability", "Repeats",        "Reverse chance", "Pitch range",
        "Pan amount", "Drive",       "Distort chance", "Seed (integer)", "Wet mix"};
    constexpr std::array<const char *, 10> suffixes{" ms", "",    "", "", " st",
                                                    "",    " dB", "", "", ""};
    for (int index = 0; index < 10; ++index) {
        configureSlider(sliders[size_t(index)], index, suffixes[size_t(index)]);
        labels[size_t(index)].setText(names[size_t(index)], juce::dontSendNotification);
        addAndMakeVisible(labels[size_t(index)]);
    }
    for (size_t index : {size_t(2), size_t(8)}) {
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

void ChaosEditor::configureSlider(ParameterSlider &slider, int index, const juce::String &suffix) {
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 26);
    slider.setTextValueSuffix(suffix);
    slider.setNumDecimalPlacesToDisplay(index == 1 ? 1 : 3);
    constexpr std::array<const char *, 10> ids{"slice",      "probability", "repeats", "reverse",
                                               "pitchRange", "pan",         "drive",   "distortion",
                                               "seed",       "mix"};
    slider.setComponentID(ids[size_t(index)]);
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
void ChaosEditor::timerCallback() {
    const auto values = processor.readValues();
    if (!dragging)
        for (size_t index = 0; index < 10; ++index)
            sliders[index].setValue(values[index], juce::dontSendNotification);
    undoButton.setEnabled(processor.canUndo());
    redoButton.setEnabled(processor.canRedo());
    display.refresh(spectrumVisible);
    repaint(display.getSpectrumBounds().withTrimmedBottom(-58).toNearestInt());
}

void ChaosEditor::paint(juce::Graphics &graphics) {
    appearance.paintBackground(graphics, getWidth());
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(23));
    graphics.drawText(processor.getName(), 20, 10, 370, 32, juce::Justification::left);
    appearance.paintFlow(graphics, {20, 48, 620, 24},
                         {"Input", "Buffer processing + dry mix", "Output"});
    ahq::ui::paintGroups(graphics, appearance, labels, sliders, uiGroups);
    display.paintSpectrum(graphics);
    display.paintMeters(graphics);
}
void ChaosEditor::resized() {
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
