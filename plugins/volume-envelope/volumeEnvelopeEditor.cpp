#include "volumeEnvelopeEditor.h"
#include "../common/ui/parameterGroups.h"
#include <cmath>
VolumeEnvelopeEditor::VolumeEnvelopeEditor(VolumeEnvelopeProcessor &p)
    : AudioProcessorEditor(p), ComponentMovementWatcher(this), processor(p) {
    addAndMakeVisible(presetBar);
    for (auto *c : std::array<juce::Component *, 4>{&mode, &presets, &undo, &redo})
        addAndMakeVisible(c);
    mode.addItemList({"Host BPM", "Audio sidechain"}, 1);
    for (int i = 0; i < p.getNumPrograms(); ++i)
        presets.addItem(p.getProgramName(i), i + 1);
    presets.setTextWhenNothingSelected("Envelope presets");
    presets.onChange = [this] {
        processor.setCurrentProgram(presets.getSelectedId() - 1);
        presets.setSelectedId(0, juce::dontSendNotification);
        repaint();
    };
    mode.onChange = [this] {
        auto v = processor.readValues();
        v[0] = float(mode.getSelectedId() - 1);
        processor.applyUserValues(v, "Trigger mode");
    };
    modeAttachment = std::make_unique<juce::ParameterAttachment>(
        *p.getParameters().getParameter("mode"), [this](float value) {
            mode.setSelectedId(int(value) + 1, juce::dontSendNotification);
            sliders[1].setEnabled(value != 0);
        });
    modeAttachment->sendInitialUpdate();
    bindParameterControl(mode, *p.getParameters().getParameter("mode"));
    const std::array<const char *, 3> names{"Length (beats)", "Threshold (dBFS)", "Mix"};
    for (size_t i = 0; i < sliders.size(); ++i) {
        auto &slider = sliders[i];
        auto *parameter = p.getParameters().getParameter(VolumeEnvelopeProcessor::ids[i + 1]);
        slider.bindParameter(*parameter);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 24);
        const auto &r = parameter->getNormalisableRange();
        slider.setRange(r.start, r.end, r.interval);
        slider.setNumDecimalPlacesToDisplay(i == 0 ? 4 : 2);
        slider.onDragStart = [this, parameter] {
            before = processor.readValues();
            sliderDragging = true;
            parameter->beginChangeGesture();
        };
        slider.onValueChange = [this, parameter, i] {
            if (sliderDragging)
                parameter->setValueNotifyingHost(
                    parameter->convertTo0to1(float(sliders[i].getValue())));
            else {
                auto v = processor.readValues();
                v[i + 1] = float(sliders[i].getValue());
                processor.applyUserValues(v, "Envelope parameter");
            }
        };
        slider.onDragEnd = [this, parameter, i] {
            parameter->endChangeGesture();
            sliderDragging = false;
            const int index = int(i + 1);
            processor.recordUserEdit(before, {&index, 1});
        };
        labels[i].setText(names[i], juce::dontSendNotification);
        labels[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(slider);
        addAndMakeVisible(labels[i]);
    }
    sliders[0].setSkewFactorFromMidPoint(1);
    sliders[1].setTooltip("External Trigger bus only. Crossing the threshold starts the fixed "
                          "envelope; louder triggers do not increase ducking.");
    undo.onClick = [this] {
        processor.undo();
        repaint();
    };
    redo.onClick = [this] {
        processor.redo();
        repaint();
    };
    setResizable(true, true);
    setResizeLimits(760, 606, 1500, 1066);
    setSize(940, 686);
    timerCallback();
    visibilityChanged();
}
void VolumeEnvelopeEditor::visibilityChanged() {
    if (isShowing())
        startTimerHz(30);
    else
        stopTimer();
}
void VolumeEnvelopeEditor::timerCallback() {
    if (!sliderDragging) {
        const auto v = processor.readValues();
        for (size_t i = 0; i < sliders.size(); ++i)
            sliders[i].setValue(v[i + 1], juce::dontSendNotification);
    }
    repaint();
}
void VolumeEnvelopeEditor::resized() {
    presetBar.setBounds(0, getHeight() - ahq::ui::PresetBar::height, getWidth(),
                        ahq::ui::PresetBar::height);
    mode.setBounds(20, 52, 200, 30);
    presets.setBounds(232, 52, 210, 30);
    undo.setBounds(getWidth() - 198, 52, 82, 30);
    redo.setBounds(getWidth() - 104, 52, 82, 30);
    const int x = getWidth() - 310;
    const ahq::ui::ParameterGroup groups[]{{"Clock and trigger", 0, 2}, {"Mix", 2, 1}};
    ahq::ui::layoutGroups(labels, sliders, groups, {x, 154, 290, getHeight() - 266});
    graph = {52, 166, float(x - 80), float(getHeight() - 304)};
}
void VolumeEnvelopeEditor::paint(juce::Graphics &g) {
    appearance.paintBackground(g, getWidth());
    g.setColour(juce::Colours::white);
    g.setFont(24.F);
    g.drawText("AHQ Volume Envelope", 20, 10, 500, 32, juce::Justification::left);
    g.setFont(13.F);
    g.setColour(juce::Colour(0xffb7c6d5));
    g.drawText("Drag nodes to shape gain. Default: one beat per cycle.", 20, 126, getWidth() - 200,
               24, juce::Justification::left);
    g.drawText(juce::String(processor.displayedBpm.load(), 1) + " BPM", getWidth() - 170, 126, 144,
               24, juce::Justification::right);
    const ahq::ui::ParameterGroup groups[]{{"Clock and trigger", 0, 2}, {"Mix", 2, 1}};
    ahq::ui::paintGroups(g, appearance, labels, sliders, groups);
    appearance.paintFlow(g, {20, 90, float(getWidth() - 40), 24},
                         {"Input", "Envelope gain + mix", "Output"});
    g.setColour(juce::Colour(0xff11151b));
    g.fillRect(graph);
    for (int i = 0; i <= 4; ++i) {
        const float x = graph.getX() + graph.getWidth() * i / 4,
                    y = graph.getY() + graph.getHeight() * i / 4;
        g.setColour(juce::Colour(0xff353e4a));
        g.drawVerticalLine(int(x), graph.getY(), graph.getBottom());
        g.drawHorizontalLine(int(y), graph.getX(), graph.getRight());
        g.setColour(juce::Colour(0xffb7c6d5));
        g.drawText(juce::String(100 - i * 25), 10, int(y) - 8, 34, 16, juce::Justification::right);
    }
    const auto v = processor.readValues();
    ahq::audio::VolumeEnvelopeParameters p;
    p.times.front() = 0;
    p.times.back() = 1;
    for (int i = 1; i < 7; ++i)
        p.times[size_t(i)] =
            std::clamp(double(v[size_t(i + 3)]), p.times[size_t(i - 1)] + .001, 1 - (7 - i) * .001);
    for (int i = 0; i < 8; ++i)
        p.levels[size_t(i)] = v[size_t(i + 10)];
    ahq::audio::VolumeEnvelope curve;
    (void)curve.prepare(48000);
    (void)curve.setParameters(p);
    juce::Path path;
    path.preallocateSpace(int(graph.getWidth()) * 3 + 8);
    for (int x = 0; x <= int(graph.getWidth()); ++x) {
        const float y = graph.getBottom() -
                        float(curve.valueAt(x / double(graph.getWidth()))) * graph.getHeight();
        if (x == 0)
            path.startNewSubPath(graph.getX(), y);
        else
            path.lineTo(graph.getX() + x, y);
    }
    auto fill = path;
    fill.lineTo(graph.getRight(), graph.getBottom());
    fill.lineTo(graph.getX(), graph.getBottom());
    fill.closeSubPath();
    g.setColour(juce::Colour(0xff8acbff).withAlpha(.14F));
    g.fillPath(fill);
    g.setColour(juce::Colour(0xff8acbff));
    g.strokePath(path, juce::PathStrokeType(2.F));
    for (int i = 0; i < 8; ++i) {
        const auto x = graph.getX() + float(p.times[size_t(i)]) * graph.getWidth();
        const auto y = graph.getBottom() - v[size_t(i + 10)] * graph.getHeight();
        g.fillEllipse(x - 5, y - 5, 10, 10);
    }
    const float x = graph.getX() + processor.displayedPhase.load() * graph.getWidth();
    g.setColour(juce::Colours::white.withAlpha(.6F));
    g.drawVerticalLine(int(x), graph.getY(), graph.getBottom());
    g.setColour(juce::Colour(0xffb7c6d5));
    g.setFont(13.F);
    g.drawText(v[0] == 0
                   ? "Host BPM / PPQ sync. One beat = one quarter note."
                   : "Route the kick into the Trigger sidechain bus. No trigger input: unity gain.",
               20, (getHeight() - ahq::ui::PresetBar::height) - 32, getWidth() - 40, 24,
               juce::Justification::left);
}
void VolumeEnvelopeEditor::mouseDown(const juce::MouseEvent &e) {
    if (!graph.expanded(10).contains(e.position))
        return;
    before = processor.readValues();
    float distance = 400;
    for (int i = 0; i < 8; ++i) {
        const float t = i == 0 ? 0 : i == 7 ? 1 : before[size_t(i + 3)];
        const auto point =
            juce::Point<float>(graph.getX() + t * graph.getWidth(),
                               graph.getBottom() - before[size_t(i + 10)] * graph.getHeight());
        const float d = point.getDistanceSquaredFrom(e.position);
        if (d < distance) {
            draggedNode = i;
            distance = d;
        }
    }
    if (draggedNode < 0)
        return;
    auto *level = processor.getParameters().getParameter(
        VolumeEnvelopeProcessor::ids[size_t(draggedNode + 10)]);
    if (e.mods.isPopupMenu()) {
        if (auto *host = getHostContext())
            if (auto menu = host->getContextMenuForParameter(level))
                menu->showNativeMenu(e.getPosition());
        draggedNode = -1;
        return;
    }
    level->beginChangeGesture();
    if (draggedNode > 0 && draggedNode < 7)
        processor.getParameters()
            .getParameter(VolumeEnvelopeProcessor::ids[size_t(draggedNode + 3)])
            ->beginChangeGesture();
}
void VolumeEnvelopeEditor::mouseDrag(const juce::MouseEvent &e) {
    if (draggedNode < 0)
        return;
    const auto v = processor.readValues();
    auto set = [&](int i, float value) {
        auto *p = processor.getParameters().getParameter(VolumeEnvelopeProcessor::ids[size_t(i)]);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    set(draggedNode + 10,
        std::clamp((graph.getBottom() - e.position.y) / graph.getHeight(), 0.F, 1.F));
    if (draggedNode > 0 && draggedNode < 7) {
        const float low = draggedNode == 1 ? 0 : v[size_t(draggedNode + 2)];
        const float high = draggedNode == 6 ? 1 : v[size_t(draggedNode + 4)];
        if (high - low > .002F)
            set(draggedNode + 3, std::clamp((e.position.x - graph.getX()) / graph.getWidth(),
                                            low + .001F, high - .001F));
    }
    repaint();
}
void VolumeEnvelopeEditor::mouseUp(const juce::MouseEvent &) {
    if (draggedNode < 0)
        return;
    std::array<int, 2> indices{draggedNode + 10, draggedNode + 3};
    const auto edited = std::span(indices).first(draggedNode == 0 || draggedNode == 7 ? 1 : 2);
    for (int i : edited)
        processor.getParameters()
            .getParameter(VolumeEnvelopeProcessor::ids[size_t(i)])
            ->endChangeGesture();
    processor.recordUserEdit(before, edited);
    draggedNode = -1;
}
