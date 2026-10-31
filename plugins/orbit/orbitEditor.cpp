#include "orbitEditor.h"
#include "../common/ui/parameterGroups.h"
#include <cmath>
namespace {
constexpr ahq::ui::ParameterGroup uiGroups[]{{"Timing", 0, 3}, {"Motion", 3, 2}, {"Mix", 5, 1}};
}

OrbitEditor::OrbitEditor(OrbitProcessor &owner)
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

void OrbitEditor::configureSlider(ParameterSlider &slider, int index, const juce::String &suffix) {
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 102, 26);
    slider.setTextValueSuffix(suffix);
    slider.setNumDecimalPlacesToDisplay(index == 1 ? 1 : 3);
    constexpr std::array<const char *, 8> ids{"rate",      "sync",  "bpm",   "beats",
                                              "direction", "depth", "phase", "mix"};
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
    repaint(display.getSpectrumBounds().withTrimmedBottom(-58).toNearestInt());
}

void OrbitEditor::paint(juce::Graphics &graphics) {
    appearance.paintBackground(graphics, getWidth());
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(23));
    graphics.drawText(processor.getName(), 20, 10, 370, 32, juce::Justification::left);
    appearance.paintFlow(graphics, {20, 48, 620, 24},
                         {"Input", "Orbit processing + mix", "Output"});
    ahq::ui::paintGroups(graphics, appearance, labels, sliders, uiGroups);
    display.paintSpectrum(graphics);
    display.paintMeters(graphics);
}
void OrbitEditor::resized() {
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
    speedMode.setBounds(rightX, 120, rightWidth / 2 - 6, 28);
    direction.setBounds(rightX + rightWidth / 2, 120, rightWidth / 2, 28);
    tempoStatus.setBounds(660, 48, getWidth() - 680, 24);
    ahq::ui::layoutGroups(labels, sliders, uiGroups,
                          {rightX, 160, rightWidth, getHeight() - 160 - 104});
}
