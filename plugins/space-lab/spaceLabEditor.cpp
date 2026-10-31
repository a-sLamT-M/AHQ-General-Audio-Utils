#include "spaceLabEditor.h"
#include "../common/ui/parameterGroups.h"
#include <cmath>
namespace {
constexpr ahq::ui::ParameterGroup uiGroups[]{{"Width and delay", 0, 2},
                                             {"Colour", 2, 2},
                                             {"Polarity", 4, 2},
                                             {"Modulation", 6, 2},
                                             {"Routing and mix", 8, 2}};
}

namespace {
int toggleIndexForParameter(int index) {
    switch (index) {
    case 4:
        return 0;
    case 5:
        return 1;
    case 8:
        return 2;
    default:
        return -1;
    }
}
} // namespace

SpaceLabEditor::SpaceLabEditor(SpaceLabProcessor &owner)
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
        "Width",        "Signed Haas", "Left tilt", "Right tilt", "Invert left",
        "Invert right", "Mod rate",    "Mod depth", "Split mono", "Wet mix"};
    constexpr std::array<const char *, 10> ids{"width",      "haas",        "leftTilt", "rightTilt",
                                               "invertLeft", "invertRight", "modRate",  "modDepth",
                                               "split",      "mix"};
    constexpr std::array<const char *, 10> suffixes{"", " ms", " dB", " dB", "",
                                                    "", " Hz", " ms", "",    ""};
    for (int i = 0; i < 10; ++i) {
        labels[size_t(i)].setText(names[size_t(i)], juce::dontSendNotification);
        addAndMakeVisible(labels[size_t(i)]);
        if (i == 4 || i == 5 || i == 8) {
            const size_t toggleIndex = size_t(toggleIndexForParameter(i));
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
            configureSlider(sliders[size_t(i)], i, suffixes[size_t(i)]);
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

void SpaceLabEditor::configureSlider(ParameterSlider &slider, int index,
                                     const juce::String &suffix) {
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 26);
    slider.setTextValueSuffix(suffix);
    slider.setNumDecimalPlacesToDisplay(index == 1 ? 1 : 3);
    constexpr std::array<const char *, 10> ids{"width",      "haas",        "leftTilt", "rightTilt",
                                               "invertLeft", "invertRight", "modRate",  "modDepth",
                                               "split",      "mix"};
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
    repaint(display.getSpectrumBounds().withTrimmedBottom(-58).toNearestInt());
}

void SpaceLabEditor::paint(juce::Graphics &graphics) {
    appearance.paintBackground(graphics, getWidth());
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(23));
    graphics.drawText(processor.getName(), 20, 10, 370, 32, juce::Justification::left);
    appearance.paintFlow(graphics, {20, 48, 620, 24},
                         {"Input", "Stereo routing + processing", "Output"});
    ahq::ui::paintGroups(graphics, appearance, labels, sliders, uiGroups);
    display.paintSpectrum(graphics);
    display.paintMeters(graphics);
    graphics.setFont(juce::FontOptions(12));
    graphics.drawText(processor.readValues()[8] == 1 ? "Split: left = dry M; right = processed M"
                                                     : "Stereo processing",
                      20, getHeight() - 94, getWidth() - 40, 20, juce::Justification::left);
}
void SpaceLabEditor::resized() {
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
    for (int i : {4, 5, 8}) {
        auto &toggle = toggles[size_t(toggleIndexForParameter(i))];
        toggle.setBounds(sliders[size_t(i)].getBounds().withHeight(30));
    }
}
