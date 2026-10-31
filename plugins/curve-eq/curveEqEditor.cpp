#include "curveEqEditor.h"
#include <cmath>
CurveEqEditor::CurveEqEditor(CurveEqProcessor &p)
    : AudioProcessorEditor(p), ComponentMovementWatcher(this), processor(p),
      spectrum(p.getAnalysis()) {
    for (size_t i = 0; i < defaults.size(); ++i) {
        auto *parameter = p.getParameters().getParameter(p.parameterId(i));
        defaults[i] = parameter->convertFrom0to1(parameter->getDefaultValue());
    }
    addAndMakeVisible(presetBar);
    addAndMakeVisible(spectrum.controls);
    spectrum.controls.setAreaControlVisible(false);
    spectrum.controls.onChange = [this] {
        resized();
        repaint();
    };
    for (auto *c : std::array<juce::Component *, 10>{&band, &shape, &channels, &slope, &scale,
                                                     &enabled, &add, &remove, &undo, &redo})
        addAndMakeVisible(c);
    for (int i = 1; i <= 24; ++i)
        band.addItem("Band " + juce::String(i), i);
    shape.addItemList(
        {"Bell", "Low shelf", "High shelf", "Low cut", "High cut", "Notch", "Band pass"}, 1);
    channels.addItemList({"Stereo", "Left", "Right", "Mid", "Side"}, 1);
    slope.addItemList({"12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct"}, 1);
    scale.addItemList({"+/- 6 dB", "+/- 12 dB", "+/- 24 dB", "+/- 48 dB"}, 1);
    scale.setSelectedId(2, juce::dontSendNotification);
    scale.onChange = [this] {
        rangeDb = float(3 * (1 << scale.getSelectedId()));
        responseDirty = true;
        repaint();
    };
    band.onChange = [this] { selectBand(band.getSelectedId() - 1); };
    enabled.onClick = [this] {
        auto v = processor.readValues();
        v[size_t(selected) * CurveEqProcessor::fields] = enabled.getToggleState() ? 1.F : 0.F;
        processor.applyUserValues(v, "Enable band");
    };
    add.onClick = [this] { createBand({xFor(1000), yFor(0)}, false, false); };
    remove.onClick = [this] { deleteSelectedBands(); };
    undo.onClick = [this] { processor.undo(); };
    redo.onClick = [this] { processor.redo(); };
    const std::array<const char *, 9> names{"Frequency Hz", "Gain dB",      "Q",
                                            "Dynamic dB",   "Threshold dB", "Attack ms",
                                            "Release ms",   "Output dB",    "Mix"};
    for (size_t i = 0; i < sliders.size(); ++i) {
        auto &s = sliders[i];
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 94, 24);
        s.setNumDecimalPlacesToDisplay(i == 0 ? 1 : 2);
        labels[i].setText(names[i], juce::dontSendNotification);
        labels[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(s);
        addAndMakeVisible(labels[i]);
    }
    for (size_t i = 3; i < sliders.size(); ++i) {
        sliders[i].setSliderStyle(juce::Slider::LinearHorizontal);
        sliders[i].setTextBoxStyle(juce::Slider::TextBoxRight, false, 68, 24);
    }
    sliders[0].setName("Band frequency");
    sliders[1].setName("Band gain");
    sliders[2].setName("Band Q");
    sliders[0].valueFromTextFunction = [](const juce::String &text) {
        const auto value = text.trim().toLowerCase();
        return value.getDoubleValue() * (value.containsChar('k') ? 1000 : 1);
    };
    sliders[3].setTooltip("Negative: compress this band's gain above threshold. Positive: expand. "
                          "Zero skips the detector. Available for bells and shelves.");
    selectBand(0);
    if (p.readValues()[0] == 0)
        selectedBands.reset();
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(1120, 760, 2000, 1500);
    setSize(1600, 1000);
    timerCallback();
    visibilityChanged();
}
CurveEqEditor::~CurveEqEditor() {
    finishNodeDrag();
    stopTimer();
    processor.setDisplayEnabled(false);
}
void CurveEqEditor::visibilityChanged() {
    processor.setDisplayEnabled(isShowing());
    if (isShowing())
        startTimerHz(30);
    else
        stopTimer();
}
void CurveEqEditor::selectBand(int index, bool preserveSelection) {
    if (dragging || nodeDragging)
        return;
    selected = std::clamp(index, 0, 23);
    if (!preserveSelection)
        selectedBands.reset();
    selectedBands.set(size_t(selected));
    band.setSelectedId(selected + 1, juce::dontSendNotification);
    const size_t base = size_t(selected) * CurveEqProcessor::fields;
    bindParameterControl(enabled,
                         *processor.getParameters().getParameter(processor.parameterId(base)));
    const std::array<juce::ComboBox *, 3> boxes{&shape, &channels, &slope};
    const std::array<size_t, 3> fields{1, 2, 6};
    for (size_t i = 0; i < boxes.size(); ++i) {
        auto *box = boxes[i];
        const size_t parameterIndex = base + fields[i];
        auto *parameter =
            processor.getParameters().getParameter(processor.parameterId(parameterIndex));
        bindParameterControl(*box, *parameter);
        box->onChange = [this, box, parameterIndex, i] {
            auto v = processor.readValues();
            v[parameterIndex] = float(box->getSelectedId() - (i == 2 ? 0 : 1));
            processor.applyUserValues(v, "Band setting");
        };
        choices[i] = std::make_unique<juce::ParameterAttachment>(*parameter, [box, i](float value) {
            box->setSelectedId(int(value) + (i == 2 ? 0 : 1), juce::dontSendNotification);
        });
        choices[i]->sendInitialUpdate();
    }
    for (size_t i = 0; i < sliders.size(); ++i) {
        const size_t index =
            i < 7 ? base + size_t(sliderFields[i]) : CurveEqProcessor::count - 9 + i;
        auto *p = processor.getParameters().getParameter(processor.parameterId(index));
        auto &s = sliders[i];
        s.bindParameter(*p);
        const auto &range = p->getNormalisableRange();
        s.setNormalisableRange({double(range.start), double(range.end), double(range.interval),
                                double(range.skew), range.symmetricSkew});
        s.onDragStart = [this, p] {
            before = processor.readValues();
            dragging = true;
            p->beginChangeGesture();
        };
        s.onValueChange = [this, p, index, i] {
            if (dragging)
                p->setValueNotifyingHost(p->convertTo0to1(float(sliders[i].getValue())));
            else {
                auto v = processor.readValues();
                v[index] = float(sliders[i].getValue());
                processor.applyUserValues(v, "Band value");
            }
        };
        s.onDragEnd = [this, p, index] {
            p->endChangeGesture();
            dragging = false;
            const int f = int(index);
            processor.recordUserEdit(before, {&f, 1});
        };
    }
    responseDirty = true;
    timerCallback();
    layoutBandControls();
}
void CurveEqEditor::resized() {
    presetBar.setBounds(0, getHeight() - ahq::ui::PresetBar::height, getWidth(),
                        ahq::ui::PresetBar::height);
    add.setBounds(20, 50, 98, 28);
    undo.setBounds(126, 50, 70, 28);
    redo.setBounds(204, 50, 70, 28);
    for (size_t i = 7; i < 9; ++i) {
        const int x = 304 + int(i - 7) * 244;
        labels[i].setBounds(x, 50, 72, 28);
        sliders[i].setBounds(x + 76, 50, 160, 28);
    }
    scale.setBounds(getWidth() - 130, 50, 110, 28);
    spectrum.controls.setBounds(20, 86, getWidth() - 40, 30);
    graph = {62, 146, float(getWidth() - 124), float(getHeight() - 270)};
    layoutBandControls();
    responseDirty = true;
    rebuildResponse();
}
void CurveEqEditor::layoutBandControls() {
    const bool show = selectedBands.any() && !graph.isEmpty();
    for (auto *control :
         std::array<juce::Component *, 6>{&band, &shape, &channels, &slope, &enabled, &remove})
        control->setVisible(show);
    for (size_t i = 0; i < 7; ++i) {
        sliders[i].setVisible(show);
        labels[i].setVisible(show);
    }
    bandControls = {};
    if (!show)
        return;
    const auto v = processor.readValues();
    const auto base = size_t(selected) * CurveEqProcessor::fields;
    const auto node = juce::Point<float>(xFor(v[base + 3]), yFor(v[base + 4]));
    const float width = 760, height = 202;
    const float x =
        std::clamp(node.x - width * .5F, graph.getX() + 12, graph.getRight() - width - 12);
    const float y =
        std::clamp(node.y + 32 + height < graph.getBottom() ? node.y + 32 : node.y - height - 32,
                   graph.getY() + 12, graph.getBottom() - height - 12);
    bandControls = {x, y, width, height};
    int controlX = int(x) + 14;
    const std::array<juce::Component *, 6> controls{&band,  &shape,   &channels,
                                                    &slope, &enabled, &remove};
    const std::array<int, 6> widths{96, 122, 96, 106, 106, 76};
    for (size_t i = 0; i < controls.size(); ++i) {
        controls[i]->setBounds(controlX, int(y) + 10, widths[i], 26);
        controlX += widths[i] + 8;
    }
    for (size_t i = 0; i < 3; ++i) {
        const int left = int(x) + 12 + int(i) * 126;
        labels[i].setBounds(left, int(y) + 44, 120, 18);
        sliders[i].setBounds(left, int(y) + 64, 120, 128);
    }
    for (size_t i = 3; i < 7; ++i) {
        const int top = int(y) + 48 + int(i - 3) * 34;
        labels[i].setBounds(int(x) + 400, top, 108, 26);
        sliders[i].setBounds(int(x) + 512, top, 232, 26);
    }
}
double CurveEqEditor::frequencyFor(float x) const {
    const double top = std::min(20000., processor.displaySampleRate.load() * .5);
    return 20 *
           std::pow(top / 20, std::clamp(double((x - graph.getX()) / graph.getWidth()), 0., 1.));
}
float CurveEqEditor::xFor(double hz) const {
    const double top = std::min(20000., processor.displaySampleRate.load() * .5);
    return graph.getX() +
           graph.getWidth() * float(std::clamp(std::log(hz / 20) / std::log(top / 20), 0., 1.));
}
float CurveEqEditor::yFor(double db) const {
    return graph.getCentreY() - float(db) * graph.getHeight() / (2 * rangeDb);
}
void CurveEqEditor::rebuildResponse() {
    const auto rate = processor.displaySampleRate.load();
    if (!responseDirty && displayed == responseValues && rate == responseRate)
        return;
    responseDirty = false;
    responseValues = displayed;
    responseRate = rate;
    ahq::audio::ParametricEq response;
    if (!response.prepare(rate))
        return;
    for (size_t i = 0; i < 24; ++i)
        (void)response.setBand(i, CurveEqProcessor::bandParameters(displayed, i));
    midResponse.clear();
    sideResponse.clear();
    selectionResponse.clear();
    ahq::audio::ParametricEq selection;
    const bool showSelection = selectedBands.any() && selection.prepare(rate);
    if (showSelection)
        for (size_t i = 0; i < 24; ++i)
            if (selectedBands[i])
                (void)selection.setBand(i, CurveEqProcessor::bandParameters(displayed, i));
    for (int x = 0; x <= int(graph.getWidth()); x += 2) {
        const auto magnitudes = response.responseMagnitudes(frequencyFor(graph.getX() + x),
                                                            displayed[CurveEqProcessor::count - 1]);
        const auto point = [&](juce::Path &path, double magnitude) {
            const float y = yFor(20 * std::log10(std::max(magnitude, 1e-12)) +
                                 displayed[CurveEqProcessor::count - 2]);
            if (x == 0)
                path.startNewSubPath(graph.getX(), y);
            else
                path.lineTo(graph.getX() + x, y);
        };
        point(midResponse, magnitudes[0]);
        point(sideResponse, magnitudes[1]);
        if (showSelection) {
            const auto magnitude = selection.responseMagnitudes(frequencyFor(graph.getX() + x), 1);
            const auto y = yFor(20 * std::log10(std::max(magnitude[0], 1e-12)));
            if (x == 0)
                selectionResponse.startNewSubPath(graph.getX(), y);
            else
                selectionResponse.lineTo(graph.getX() + x, y);
        }
    }
}
void CurveEqEditor::timerCallback() {
    displayed = processor.readValues();
    visibleBands.reset();
    for (size_t i = 0; i < 24; ++i) {
        const auto base = i * CurveEqProcessor::fields;
        for (size_t f = 0; f < CurveEqProcessor::fields; ++f)
            if (displayed[base + f] != defaults[base + f]) {
                visibleBands.set(i);
                break;
            }
    }
    const size_t base = size_t(selected) * CurveEqProcessor::fields;
    enabled.setToggleState(displayed[base] != 0, juce::dontSendNotification);
    const int shapeId = int(displayed[base + 1]);
    slope.setEnabled(shapeId == 3 || shapeId == 4);
    sliders[1].setEnabled(shapeId <= 2);
    for (size_t i = 3; i < 7; ++i)
        sliders[i].setEnabled(shapeId <= 2);
    if (!dragging)
        for (size_t i = 0; i < sliders.size(); ++i) {
            const size_t index =
                i < 7 ? base + size_t(sliderFields[i]) : CurveEqProcessor::count - 9 + i;
            sliders[i].setValue(displayed[index], juce::dontSendNotification);
        }
    spectrum.refresh(isShowing());
    rebuildResponse();
    repaint();
}
void CurveEqEditor::paint(juce::Graphics &g) {
    appearance.paintBackground(g, getWidth());
    g.setColour(juce::Colours::white);
    g.setFont(24.F);
    g.drawText("AHQ Curve EQ", 20, 10, 400, 32, juce::Justification::left);
    g.setFont(13.F);
    g.setColour(juce::Colour(0xffb7c6d5));
    g.drawText("24 bands  /  zero latency", 430, 14, getWidth() - 450, 24,
               juce::Justification::right);
    g.setColour(juce::Colour(0xff11151b));
    g.fillRect(graph);
    spectrum.paintConfiguredSpectrum(g, graph, juce::Colour(0xff28ced1).withAlpha(.55F), false);
    const float step = rangeDb <= 6 ? 1 : rangeDb / 4;
    for (float db = -rangeDb; db <= rangeDb; db += step) {
        const auto y = yFor(db);
        g.setColour(juce::Colour(db == 0 ? 0xff718079 : 0xff313b40));
        g.drawHorizontalLine(juce::roundToInt(y), graph.getX(), graph.getRight());
        g.setColour(appearance.accent);
        g.drawText((db > 0 ? "+" : "") + juce::String(db, 0), int(graph.getRight()) + 6, int(y) - 8,
                   46, 16, juce::Justification::left);
    }
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(graph.expanded(6).toNearestInt());
        if (!selectionResponse.isEmpty()) {
            auto fill = selectionResponse;
            fill.lineTo(graph.getRight(), yFor(0));
            fill.lineTo(graph.getX(), yFor(0));
            fill.closeSubPath();
            g.setColour(appearance.accent.withAlpha(.13F));
            g.fillPath(fill);
        }
        g.setColour(juce::Colour(0xffd8aaff));
        g.strokePath(sideResponse, juce::PathStrokeType(1.2F));
        g.setColour(juce::Colour(0xff8acbff));
        g.strokePath(midResponse, juce::PathStrokeType(2.F));
        for (size_t i = 0; i < 24; ++i) {
            const auto base = i * CurveEqProcessor::fields;
            if (!visibleBands[i])
                continue;
            const float x = xFor(displayed[base + 3]), y = yFor(displayed[base + 4]);
            const float dynamic = processor.dynamicDb[i].load();
            g.setColour(juce::Colour(0xffffc273));
            if (std::abs(dynamic) > .05F)
                g.drawLine(x, y, x, yFor(displayed[base + 4] + dynamic), 3.F);
            if (selectedBands[i]) {
                g.setColour(appearance.accent.withAlpha(.3F));
                g.fillEllipse(x - 11, y - 11, 22, 22);
            }
            g.setColour(selectedBands[i] ? juce::Colour(0xffffc273) : juce::Colour(0xff8acbff));
            if (displayed[base] != 0)
                g.fillEllipse(x - 6, y - 6, 12, 12);
            else
                g.drawEllipse(x - 6, y - 6, 12, 12, 1.5F);
            if (selectedBands[i] && std::abs(displayed[base + 7]) > .05F &&
                displayed[base + 1] <= 2) {
                const auto rangeY = yFor(displayed[base + 4] + displayed[base + 7]);
                g.drawLine(x, y, x, rangeY, 1.2F);
                g.drawEllipse(x - 4, rangeY - 4, 8, 8, 1.2F);
            }
            g.setFont(12.F);
            g.drawText(juce::String(int(i + 1)), int(x) - 15, int(y) - 25, 30, 18,
                       juce::Justification::centred);
        }
        if (selecting && !selectionBox.isEmpty()) {
            g.setColour(appearance.accent.withAlpha(.1F));
            g.fillRect(selectionBox);
            g.setColour(appearance.accent.withAlpha(.7F));
            g.drawRect(selectionBox, 1);
        }
        if (hovering && !nodeDragging && !selecting && hitBand(hoverPosition) < 0 &&
            onResponse(hoverPosition)) {
            juce::Point<float> nearest;
            midResponse.getNearestPoint(hoverPosition, nearest);
            g.setColour(appearance.accent);
            g.drawEllipse(nearest.x - 5, nearest.y - 5, 10, 10, 1.5F);
        }
    }
    appearance.paintGroup(g, bandControls, {});
}
int CurveEqEditor::hitBand(juce::Point<float> position) const {
    const auto values = processor.readValues();
    int found = -1;
    float distance = 225;
    for (int i = 0; i < 24; ++i) {
        const auto base = size_t(i) * CurveEqProcessor::fields;
        if (!visibleBands[size_t(i)] && values[base] == 0)
            continue;
        const auto point = juce::Point<float>(xFor(values[base + 3]), yFor(values[base + 4]));
        const auto d = point.getDistanceSquaredFrom(position);
        if (d < distance) {
            found = i;
            distance = d;
        }
    }
    return found;
}
bool CurveEqEditor::onResponse(juce::Point<float> position) const {
    juce::Point<float> nearest;
    midResponse.getNearestPoint(position, nearest);
    return !midResponse.isEmpty() && nearest.getDistanceSquaredFrom(position) < 64;
}
bool CurveEqEditor::createBand(juce::Point<float> position, bool fromCurve, bool dynamic) {
    auto values = processor.readValues();
    for (size_t i = 0; i < 24; ++i) {
        const auto base = i * CurveEqProcessor::fields;
        if (values[base] != 0 || visibleBands[i])
            continue;
        for (size_t f = 0; f < CurveEqProcessor::fields; ++f)
            values[base + f] = defaults[base + f];
        const float relativeX = (position.x - graph.getX()) / graph.getWidth();
        int shapeId = 0;
        if (relativeX < .07F)
            shapeId = fromCurve ? 1 : 3;
        else if (relativeX > .93F)
            shapeId = fromCurve ? 2 : 4;
        else if (!fromCurve && position.y > graph.getBottom() - 28)
            shapeId = 5;
        values[base] = 1;
        values[base + 1] = float(shapeId);
        values[base + 3] = float(frequencyFor(position.x));
        values[base + 4] =
            shapeId <= 2
                ? std::clamp((graph.getCentreY() - position.y) * 2 * rangeDb / graph.getHeight(),
                             -30.F, 30.F)
                : 0;
        values[base + 7] = dynamic && shapeId <= 2 ? -3.F : 0;
        if (!processor.applyUserValues(values, "Add band"))
            return false;
        visibleBands.set(i);
        selectBand(int(i));
        return true;
    }
    return false;
}
void CurveEqEditor::beginNodeDrag(juce::Point<float> origin) {
    before = processor.readValues();
    dragOrigin = origin;
    dragAxis = 0;
    nodeDragging = true;
    gestureFields.clear();
    for (size_t i = 0; i < 24; ++i)
        if (selectedBands[i])
            for (size_t f : {size_t(3), size_t(4), size_t(5), size_t(7)}) {
                const auto index = i * CurveEqProcessor::fields + f;
                processor.getParameters()
                    .getParameter(processor.parameterId(index))
                    ->beginChangeGesture();
                gestureFields.push_back(int(index));
            }
}
void CurveEqEditor::finishNodeDrag() {
    if (!nodeDragging)
        return;
    for (int index : gestureFields)
        processor.getParameters()
            .getParameter(processor.parameterId(size_t(index)))
            ->endChangeGesture();
    processor.recordUserEdit(before, gestureFields);
    gestureFields.clear();
    nodeDragging = dynamicDragging = false;
    layoutBandControls();
}
void CurveEqEditor::mouseDown(const juce::MouseEvent &e) {
    if (!graph.expanded(10).contains(e.position) || bandControls.contains(e.position))
        return;
    if (isShowing())
        grabKeyboardFocus();
    const bool command = e.mods.isCommandDown();
    const int found = hitBand(e.position);
    clickAction = 0;
    if (found >= 0) {
        if (command && !e.mods.isAltDown() && selectedBands[size_t(found)])
            clickAction = 1;
        if (e.mods.isShiftDown() && !e.mods.isAltDown()) {
            for (int i = std::min(selected, found); i <= std::max(selected, found); ++i)
                if (visibleBands[size_t(i)])
                    selectedBands.set(size_t(i));
            selectBand(found, true);
        } else
            selectBand(found, command || selectedBands[size_t(found)]);
        if (e.mods.isPopupMenu()) {
            showBandMenu(found);
            return;
        }
        if (e.mods.isAltDown()) {
            clickAction = command ? 3 : e.mods.isShiftDown() ? 4 : 2;
        }
        beginNodeDrag(e.position);
        return;
    }
    // A separate range handle edits dynamics without moving frequency or static gain.
    const auto values = processor.readValues();
    for (size_t i = 0; i < 24; ++i) {
        const auto base = i * CurveEqProcessor::fields;
        if (selectedBands[i] && values[base + 1] <= 2 && std::abs(values[base + 7]) > .05F &&
            juce::Point<float>(xFor(values[base + 3]), yFor(values[base + 4] + values[base + 7]))
                    .getDistanceSquaredFrom(e.position) < 144) {
            beginNodeDrag(e.position);
            dynamicDragging = true;
            return;
        }
    }
    if (e.mods.isPopupMenu())
        return;
    if (command) {
        if (createBand(e.position, false, e.mods.isAltDown()))
            beginNodeDrag(e.position);
        return;
    }
    dragOrigin = e.position;
    pendingCurve = onResponse(e.position);
    selecting = !pendingCurve;
    selectionBox = {};
    if (!e.mods.isShiftDown())
        selectedBands.reset();
    responseDirty = true;
    layoutBandControls();
    repaint();
}
void CurveEqEditor::mouseDoubleClick(const juce::MouseEvent &e) {
    if (!graph.contains(e.position) || bandControls.contains(e.position) || e.mods.isPopupMenu())
        return;
    finishNodeDrag();
    clickAction = 0;
    selecting = pendingCurve = false;
    const int found = hitBand(e.position);
    if (found >= 0) {
        selectBand(found);
        sliders[0].showTextBox();
    } else
        createBand(e.position, false, e.mods.isAltDown());
}
void CurveEqEditor::mouseDrag(const juce::MouseEvent &e) {
    if (pendingCurve && e.position.getDistanceFrom(dragOrigin) >= 3) {
        pendingCurve = false;
        const auto origin = dragOrigin;
        if (createBand(origin, true, e.mods.isAltDown()))
            beginNodeDrag(origin);
    }
    if (selecting) {
        selectionBox = juce::Rectangle<float>(dragOrigin, e.position).getIntersection(graph);
        if (!e.mods.isShiftDown())
            selectedBands.reset();
        const auto values = processor.readValues();
        for (size_t i = 0; i < 24; ++i) {
            const auto base = i * CurveEqProcessor::fields;
            if (visibleBands[i] && selectionBox.contains(juce::Point<float>(
                                       xFor(values[base + 3]), yFor(values[base + 4]))))
                selectedBands.set(i);
        }
        responseDirty = true;
        rebuildResponse();
        repaint();
        return;
    }
    if (!nodeDragging)
        return;
    auto delta = e.position - dragOrigin;
    if (delta.getDistanceFromOrigin() < 3 && clickAction != 0)
        return;
    clickAction = 0;
    if (e.mods.isAltDown()) {
        if (dragAxis == 0)
            dragAxis = std::abs(delta.x) >= std::abs(delta.y) ? 1 : 2;
        if (dragAxis == 1)
            delta.y = 0;
        else
            delta.x = 0;
    }
    if (e.mods.isShiftDown())
        delta *= .1F;
    const auto primary = size_t(selected) * CurveEqProcessor::fields;
    const float gainDelta = -delta.y * 2 * rangeDb / graph.getHeight();
    const double ratio = std::pow(std::min(20000., processor.displaySampleRate.load() * .5) / 20,
                                  delta.x / graph.getWidth());
    const bool qDrag = e.mods.isCommandDown();
    for (size_t i = 0; i < 24; ++i) {
        if (!selectedBands[i])
            continue;
        const auto base = i * CurveEqProcessor::fields;
        const auto set = [&](size_t field, float value) {
            auto *p = processor.getParameters().getParameter(processor.parameterId(base + field));
            p->setValueNotifyingHost(p->convertTo0to1(value));
        };
        if (dynamicDragging) {
            if (before[base + 1] <= 2)
                set(7, std::clamp(before[base + 7] + gainDelta, -24.F, 24.F));
        } else if (qDrag) {
            set(5, std::clamp(before[base + 5] * std::exp(-delta.y * .015F), .1F, 30.F));
        } else {
            set(3, float(std::clamp(before[base + 3] * ratio, 10., 30000.)));
            if (before[base + 1] <= 2) {
                const float gain =
                    selectedBands.count() > 1 && std::abs(before[primary + 4]) > .01F
                        ? before[base + 4] * (before[primary + 4] + gainDelta) / before[primary + 4]
                        : before[base + 4] + gainDelta;
                set(4, std::clamp(gain, -30.F, 30.F));
            } else
                set(5, std::clamp(before[base + 5] * std::exp(-delta.y * .015F), .1F, 30.F));
        }
    }
    timerCallback();
}
void CurveEqEditor::mouseUp(const juce::MouseEvent &) {
    finishNodeDrag();
    if (clickAction == 1)
        selectedBands.reset(size_t(selected));
    else if (clickAction > 1) {
        auto values = processor.readValues();
        const auto base = size_t(selected) * CurveEqProcessor::fields;
        const auto field = base + (clickAction == 3 ? 1 : clickAction == 4 ? 6 : 0);
        values[field] = clickAction == 3   ? float((int(values[field]) + 1) % 7)
                        : clickAction == 4 ? float(1 + int(values[field]) % 4)
                                           : 1.F - values[field];
        processor.applyUserValues(values, "Band setting");
    }
    if (selecting || clickAction == 1)
        for (size_t i = 0; i < 24; ++i)
            if (selectedBands[i]) {
                selectBand(int(i), true);
                break;
            }
    clickAction = 0;
    selecting = pendingCurve = false;
    selectionBox = {};
    responseDirty = true;
    layoutBandControls();
    timerCallback();
}
void CurveEqEditor::mouseWheelMove(const juce::MouseEvent &e,
                                   const juce::MouseWheelDetails &wheel) {
    if (!graph.contains(e.position) || bandControls.contains(e.position) || nodeDragging ||
        dragging || selecting)
        return;
    const int found = hitBand(e.position);
    if (found < 0)
        return;
    if (!selectedBands[size_t(found)])
        selectBand(found);
    auto values = processor.readValues();
    const auto delta = wheel.deltaY * (e.mods.isShiftDown() ? .1F : 1.F);
    for (size_t bandIndex = 0; bandIndex < 24; ++bandIndex) {
        if (!selectedBands[bandIndex])
            continue;
        const auto base = bandIndex * CurveEqProcessor::fields;
        if (e.mods.isAltDown() && values[base + 1] <= 2)
            values[base + 7] = std::clamp(values[base + 7] + delta * 6, -24.F, 24.F);
        if (e.mods.isCommandDown() && values[base + 1] <= 2)
            values[base + 4] = std::clamp(
                values[base + 4] + delta * (e.mods.isAltDown() ? -6.F : 6.F), -30.F, 30.F);
        if (!e.mods.isAltDown() && !e.mods.isCommandDown())
            values[base + 5] = std::clamp(values[base + 5] * std::exp(delta), .1F, 30.F);
    }
    processor.applyUserValues(values, "Band adjustment");
    timerCallback();
}
void CurveEqEditor::mouseMove(const juce::MouseEvent &e) {
    hoverPosition = e.position;
    hovering = graph.contains(e.position) && !bandControls.contains(e.position);
    setMouseCursor(hovering && (hitBand(e.position) >= 0 || onResponse(e.position))
                       ? juce::MouseCursor::PointingHandCursor
                       : juce::MouseCursor::NormalCursor);
    repaint(graph.toNearestInt());
}
void CurveEqEditor::mouseExit(const juce::MouseEvent &) {
    hovering = false;
    repaint(graph.toNearestInt());
}
void CurveEqEditor::deleteSelectedBands() {
    finishNodeDrag();
    auto values = processor.readValues();
    for (size_t i = 0; i < 24; ++i)
        if (selectedBands[i]) {
            for (size_t f = 0; f < CurveEqProcessor::fields; ++f)
                values[i * CurveEqProcessor::fields + f] =
                    defaults[i * CurveEqProcessor::fields + f];
            visibleBands.reset(i);
        }
    processor.applyUserValues(values, "Delete bands");
    selectedBands.reset();
    layoutBandControls();
    responseDirty = true;
    timerCallback();
}
bool CurveEqEditor::keyPressed(const juce::KeyPress &key) {
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (!selectedBands.any())
            return false;
        deleteSelectedBands();
        return true;
    }
    if (key == juce::KeyPress::escapeKey) {
        finishNodeDrag();
        selectedBands.reset();
        layoutBandControls();
        responseDirty = true;
        timerCallback();
        return true;
    }
    return false;
}
void CurveEqEditor::showBandMenu(int index) {
    const auto values = processor.readValues();
    const auto base = size_t(index) * CurveEqProcessor::fields;
    juce::PopupMenu menu, shapes, channelModes, slopes;
    for (int i = 0; i < shape.getNumItems(); ++i)
        shapes.addItem(10 + i, shape.getItemText(i), true, int(values[base + 1]) == i);
    for (int i = 0; i < channels.getNumItems(); ++i)
        channelModes.addItem(30 + i, channels.getItemText(i), true, int(values[base + 2]) == i);
    for (int i = 0; i < slope.getNumItems(); ++i)
        slopes.addItem(40 + i, slope.getItemText(i), true, int(values[base + 6]) == i + 1);
    menu.addSubMenu("Shape", shapes);
    menu.addSubMenu("Channels", channelModes);
    menu.addSubMenu("Slope", slopes, values[base + 1] == 3 || values[base + 1] == 4);
    menu.addItem(1, "Band enabled", true, values[base] != 0);
    menu.addItem(2, "Dynamic", values[base + 1] <= 2, values[base + 7] != 0);
    menu.addItem(3, "Invert gain", values[base + 1] <= 2);
    menu.addSeparator();
    menu.addItem(4, "Delete selected bands");
    menu.addItem(5, "Host automation", getHostContext() != nullptr);
    const juce::Component::SafePointer<CurveEqEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                       [safe, base](int result) {
                           if (safe == nullptr || result == 0)
                               return;
                           if (result == 4) {
                               safe->deleteSelectedBands();
                               return;
                           }
                           if (result == 5) {
                               if (auto *host = safe->getHostContext())
                                   if (auto hostMenu = host->getContextMenuForParameter(
                                           safe->processor.getParameters().getParameter(
                                               safe->processor.parameterId(base + 4))))
                                       hostMenu->showNativeMenu(safe->getMouseXYRelative());
                               return;
                           }
                           auto v = safe->processor.readValues();
                           if (result == 1)
                               v[base] = 1.F - v[base];
                           else if (result == 2)
                               v[base + 7] = v[base + 7] == 0 ? -3.F : 0;
                           else if (result == 3)
                               v[base + 4] = -v[base + 4];
                           else if (result >= 10 && result < 17)
                               v[base + 1] = float(result - 10);
                           else if (result >= 30 && result < 35)
                               v[base + 2] = float(result - 30);
                           else if (result >= 40 && result < 44)
                               v[base + 6] = float(result - 39);
                           safe->processor.applyUserValues(v, "Band setting");
                           safe->timerCallback();
                       });
}
