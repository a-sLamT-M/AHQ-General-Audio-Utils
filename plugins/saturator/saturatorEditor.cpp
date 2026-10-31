#include "saturatorEditor.h"
#include "../common/ui/parameterGroups.h"
#include <cmath>
namespace {
constexpr ahq::ui::ParameterGroup uiGroups[]{{"Drive and bias", 0, 2}, {"Tone and mix", 2, 2}};
}

SaturatorEditor::SaturatorEditor(SaturatorProcessor &owner)
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

void SaturatorEditor::configureSlider(ParameterSlider &slider, int index,
                                      const juce::String &suffix) {
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 26);
    slider.setTextValueSuffix(suffix);
    slider.setNumDecimalPlacesToDisplay(index == 1 ? 1 : 3);
    constexpr std::array<const char *, 6> ids{"model", "drive",        "bias",
                                              "tone",  "oversampling", "mix"};
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
    repaint(display.getSpectrumBounds().withTrimmedBottom(-58).toNearestInt());
}

void SaturatorEditor::paint(juce::Graphics &graphics) {
    appearance.paintBackground(graphics, getWidth());
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(23));
    graphics.drawText(processor.getName(), 20, 10, 370, 32, juce::Justification::left);
    appearance.paintFlow(graphics, {20, 48, 620, 24},
                         {"Input", "Saturator processing + mix", "Output"});
    ahq::ui::paintGroups(graphics, appearance, labels, sliders, uiGroups);
    display.paintSpectrum(graphics);
    display.paintMeters(graphics);
}
void SaturatorEditor::resized() {
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
    model.setBounds(rightX, 120, rightWidth * 3 / 5 - 6, 28);
    quality.setBounds(rightX + rightWidth * 3 / 5, 120, rightWidth * 2 / 5, 28);
    ahq::ui::layoutGroups(labels, sliders, uiGroups,
                          {rightX, 160, rightWidth, getHeight() - 160 - 104});
}
