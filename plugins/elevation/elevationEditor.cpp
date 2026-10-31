#include "elevationEditor.h"
#include "../common/ui/parameterGroups.h"
#include <cmath>
ElevationEditor::ElevationEditor(ElevationProcessor &p) : AudioProcessorEditor(p), processor(p) {
    addAndMakeVisible(presetBar);
    for (size_t i = 0; i < sliders.size(); ++i) {
        auto *parameter = p.getParameters().getParameter(ElevationProcessor::ids[i]);
        auto &slider = sliders[i];
        slider.bindParameter(*parameter);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 24);
        const auto &range = parameter->getNormalisableRange();
        slider.setRange(range.start, range.end, range.interval);
        slider.setNumDecimalPlacesToDisplay(2);
        slider.setDoubleClickReturnValue(true,
                                         parameter->convertFrom0to1(parameter->getDefaultValue()));
        slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff80cbdc));
        slider.onDragStart = [this, parameter] {
            before = processor.readValues();
            dragging = true;
            parameter->beginChangeGesture();
        };
        slider.onValueChange = [this, i, parameter] {
            if (dragging)
                parameter->setValueNotifyingHost(
                    parameter->convertTo0to1(float(sliders[i].getValue())));
            else {
                auto v = processor.readValues();
                v[i] = float(sliders[i].getValue());
                processor.applyUserValues(v, "Value");
            }
        };
        slider.onDragEnd = [this, i, parameter] {
            parameter->endChangeGesture();
            dragging = false;
            processor.recordUserEdit(before, int(i));
        };
        attachments[i] =
            std::make_unique<juce::ParameterAttachment>(*parameter, [this, i](float value) {
                sliders[i].setValue(value, juce::dontSendNotification);
                if (i == 0) {
                    elevation = value;
                    repaint();
                }
            });
        attachments[i]->sendInitialUpdate();
        labels[i].setText(parameter->getName(32), juce::dontSendNotification);
        labels[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(slider);
        addAndMakeVisible(labels[i]);
    }
    sliders[1].setSliderStyle(juce::Slider::LinearVertical);
    addAndMakeVisible(undo);
    addAndMakeVisible(redo);
    undo.onClick = [this] { processor.undo(); };
    redo.onClick = [this] { processor.redo(); };
    setResizable(true, true);
    setResizeLimits(760, 686, 1600, 1266);
    setSize(940, 766);
}
void ElevationEditor::resized() {
    presetBar.setBounds(0, getHeight() - ahq::ui::PresetBar::height, getWidth(),
                        ahq::ui::PresetBar::height);
    undo.setBounds(getWidth() - 190, 24, 72, 28);
    redo.setBounds(getWidth() - 108, 24, 72, 28);
    graph = {28, 120, float(getWidth() - 56),
             float((getHeight() - ahq::ui::PresetBar::height) - 328)};
    for (int i = 0; i < 3; ++i) {
        const int x = 28 + i * (getWidth() - 56) / 3;
        labels[size_t(i)].setBounds(x, (getHeight() - ahq::ui::PresetBar::height) - 170,
                                    (getWidth() - 56) / 3, 26);
        sliders[size_t(i)].setBounds(x, (getHeight() - ahq::ui::PresetBar::height) - 145,
                                     (getWidth() - 56) / 3, 110);
    }
}
void ElevationEditor::paint(juce::Graphics &g) {
    appearance.paintBackground(g, getWidth());
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(26));
    g.drawText("AHQ Elevation", 28, 22, 400, 35, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffb4c1cd));
    g.setFont(juce::FontOptions(14));
    g.drawText("Drag the source above or below the listener. Use headphones.", 28, 60,
               getWidth() - 56, 24, juce::Justification::centredLeft);
    const ahq::ui::ParameterGroup groups[]{{"Position", 0, 1}, {"Output", 1, 2}};
    ahq::ui::paintGroups(g, appearance, labels, sliders, groups);
    appearance.paintFlow(g, {28, 86, float(getWidth() - 56), 22},
                         {"Input", "Elevation / mix / output", "Output"});
    g.setColour(juce::Colour(0xff1b2833));
    g.fillRoundedRectangle(graph, 10);
    const juce::Point<float> center{graph.getX() + graph.getWidth() * .42F, graph.getCentreY()};
    const float radius = graph.getHeight() * .37F;
    g.setColour(juce::Colour(0xff455a6c));
    juce::Path arc;
    arc.addCentredArc(center.x, center.y, radius, radius, 0, 0, juce::MathConstants<float>::pi,
                      true);
    g.strokePath(arc, juce::PathStrokeType(1.5F));
    g.drawHorizontalLine(int(center.y), center.x + 35, center.x + radius + 20);
    g.setColour(juce::Colour(0xffc7d2db));
    g.drawEllipse(center.x - 24, center.y - 32, 48, 60, 2);
    g.drawLine(center.x - 12, center.y + 25, center.x - 12, center.y + 45, 2);
    g.drawLine(center.x + 12, center.y + 25, center.x + 12, center.y + 45, 2);
    g.drawLine(center.x - 45, center.y + 58, center.x - 12, center.y + 45, 2);
    g.drawLine(center.x + 45, center.y + 58, center.x + 12, center.y + 45, 2);
    g.drawEllipse(center.x + 11, center.y - 3, 7, 12, 1.5F);
    const float radians = juce::degreesToRadians(elevation);
    const auto point =
        center + juce::Point<float>(std::cos(radians) * radius, -std::sin(radians) * radius);
    g.setColour(juce::Colour(0xff80cbdc));
    g.drawLine({center, point}, 1);
    g.fillEllipse(point.x - 10, point.y - 10, 20, 20);
    g.setFont(juce::FontOptions(25));
    g.drawText(juce::String(elevation, 1) + juce::String::charToString(0x00b0),
               graph.toNearestInt().reduced(22).withHeight(38), juce::Justification::topLeft);
    g.setFont(juce::FontOptions(14));
    g.setColour(juce::Colour(0xffbac9d5));
    g.drawText("Above +90", int(center.x + 20), int(center.y - radius - 20), 130, 22,
               juce::Justification::left);
    g.drawText("Below -90", int(center.x + 20), int(center.y + radius - 2), 130, 22,
               juce::Justification::left);
    g.drawText("Front 0", int(center.x + radius + 20), int(center.y - 12), 85, 24,
               juce::Justification::left);
    g.setFont(juce::FontOptions(12));
    g.drawText("SADIE II D1 / general HRTF / -90 response interpolated / stereo is one mid source",
               28, (getHeight() - ahq::ui::PresetBar::height) - 25, getWidth() - 56, 18,
               juce::Justification::centredLeft);
}
void ElevationEditor::dragPosition(juce::Point<float> position) {
    const juce::Point<float> center{graph.getX() + graph.getWidth() * .42F, graph.getCentreY()};
    const float angle =
        std::clamp(juce::radiansToDegrees(
                       std::atan2(center.y - position.y, std::max(0.F, position.x - center.x))),
                   -90.F, 90.F);
    auto *parameter = processor.getParameters().getParameter("elevation");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(angle));
}
void ElevationEditor::mouseDown(const juce::MouseEvent &event) {
    if (!graph.contains(event.position))
        return;
    auto *parameter = processor.getParameters().getParameter("elevation");
    if (event.mods.isPopupMenu()) {
        if (auto *host = getHostContext())
            if (auto menu = host->getContextMenuForParameter(parameter))
                menu->showNativeMenu(event.getPosition());
        return;
    }
    before = processor.readValues();
    positionDrag = true;
    parameter->beginChangeGesture();
    dragPosition(event.position);
}
void ElevationEditor::mouseDrag(const juce::MouseEvent &event) {
    if (positionDrag)
        dragPosition(event.position);
}
void ElevationEditor::mouseUp(const juce::MouseEvent &) {
    if (!positionDrag)
        return;
    positionDrag = false;
    processor.getParameters().getParameter("elevation")->endChangeGesture();
    processor.recordUserEdit(before, 0);
}
