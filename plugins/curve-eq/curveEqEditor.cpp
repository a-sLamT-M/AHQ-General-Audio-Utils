#include "curveEqEditor.h"
#include "../common/ui/parameterGroups.h"
#include <cmath>
namespace {
constexpr ahq::ui::ParameterGroup uiGroups[]{{"Band", 0, 3}, {"Dynamics", 3, 4}, {"Output", 7, 2}};
}
CurveEqEditor::CurveEqEditor(CurveEqProcessor &p)
    : AudioProcessorEditor(p), ComponentMovementWatcher(this), processor(p),
      spectrum(p.getAnalysis()) {
    addAndMakeVisible(presetBar);
    addAndMakeVisible(spectrum.controls);
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
    scale.setSelectedId(3, juce::dontSendNotification);
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
    add.onClick = [this] {
        auto v = processor.readValues();
        for (int i = 0; i < 24; ++i)
            if (v[size_t(i) * CurveEqProcessor::fields] == 0) {
                v[size_t(i) * CurveEqProcessor::fields] = 1;
                processor.applyUserValues(v, "Add band");
                selectBand(i);
                break;
            }
    };
    remove.onClick = [this] {
        auto v = processor.readValues();
        v[size_t(selected) * CurveEqProcessor::fields] = 0;
        processor.applyUserValues(v, "Delete band");
    };
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
    sliders[7].setSliderStyle(juce::Slider::LinearVertical);
    sliders[3].setTooltip("Negative: compress this band's gain above threshold. Positive: expand. "
                          "Zero skips the detector. Available for bells and shelves.");
    selectBand(0);
    setResizable(true, true);
    setResizeLimits(1120, 900, 2000, 1500);
    setSize(1280, 1000);
    timerCallback();
    visibilityChanged();
}
CurveEqEditor::~CurveEqEditor() {
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
void CurveEqEditor::selectBand(int index) {
    if (dragging || nodeDragging)
        return;
    selected = std::clamp(index, 0, 23);
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
    timerCallback();
}
void CurveEqEditor::resized() {
    presetBar.setBounds(0, getHeight() - ahq::ui::PresetBar::height, getWidth(),
                        ahq::ui::PresetBar::height);
    add.setBounds(20, 50, 98, 28);
    remove.setBounds(126, 50, 76, 28);
    undo.setBounds(210, 50, 70, 28);
    redo.setBounds(288, 50, 70, 28);
    band.setBounds(370, 50, 108, 28);
    enabled.setBounds(486, 50, 118, 28);
    shape.setBounds(612, 50, 130, 28);
    channels.setBounds(750, 50, 104, 28);
    slope.setBounds(862, 50, 116, 28);
    scale.setBounds(getWidth() - 130, 50, 110, 28);
    spectrum.controls.setBounds(20, 86, getWidth() - 40, 30);
    const int rightWidth = juce::jmax(
        320, int((getWidth() - 60) * (1 - spectrum.controls.preferences.share.load() / 100.F)));
    const int rightX = getWidth() - 20 - rightWidth;
    graph = {62, 160, float(rightX - 124), float(getHeight() - 286)};
    ahq::ui::layoutGroups(labels, sliders, uiGroups, {rightX, 150, rightWidth, getHeight() - 250});
    responseDirty = true;
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
    }
}
void CurveEqEditor::timerCallback() {
    displayed = processor.readValues();
    const size_t base = size_t(selected) * CurveEqProcessor::fields;
    enabled.setToggleState(displayed[base] != 0, juce::dontSendNotification);
    const int shapeId = int(displayed[base + 1]);
    slope.setEnabled(shapeId == 3 || shapeId == 4);
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
    g.drawText("24 bands  /  zero latency  /  double-click to add  /  wheel adjusts Q", 430, 14,
               getWidth() - 450, 24, juce::Justification::right);
    g.drawText(
        "Output spectrum: left dBFS   |   EQ response: right dB   |   Mid (blue) / Side (violet)",
        20, 122, getWidth() - 40, 24, juce::Justification::left);
    g.setColour(juce::Colour(0xff11151b));
    g.fillRect(graph);
    spectrum.paintConfiguredSpectrum(g, graph, juce::Colour(0xff28ced1).withAlpha(.65F));
    ahq::ui::paintGroups(g, appearance, labels, sliders, uiGroups);
    appearance.paintFlow(g, {20, float(getHeight() - 104), float(getWidth() - 40), 20},
                         {"Input", "EQ bands", "Dry / wet mix", "Master", "Output"});
    for (int i = -2; i <= 2; ++i) {
        const auto y = yFor(rangeDb * i / 2);
        g.setColour(juce::Colour(0xffb7c6d5));
        g.drawText(juce::String(rangeDb * i / 2, 0), int(graph.getRight()) + 6, int(y) - 8, 46, 16,
                   juce::Justification::left);
    }
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(graph.expanded(6).toNearestInt());
        g.setColour(juce::Colour(0xffd8aaff));
        g.strokePath(sideResponse, juce::PathStrokeType(1.2F));
        g.setColour(juce::Colour(0xff8acbff));
        g.strokePath(midResponse, juce::PathStrokeType(2.F));
        for (size_t i = 0; i < 24; ++i) {
            const auto base = i * CurveEqProcessor::fields;
            if (displayed[base] == 0)
                continue;
            const float x = xFor(displayed[base + 3]), y = yFor(displayed[base + 4]);
            const float dynamic = processor.dynamicDb[i].load();
            g.setColour(juce::Colour(0xffffc273));
            if (std::abs(dynamic) > .05F)
                g.drawLine(x, y, x, yFor(displayed[base + 4] + dynamic), 3.F);
            g.setColour(int(i) == selected ? juce::Colour(0xffffc273) : juce::Colour(0xff8acbff));
            g.fillEllipse(x - 6, y - 6, 12, 12);
            g.setFont(12.F);
            g.drawText(juce::String(int(i + 1)), int(x) - 15, int(y) - 25, 30, 18,
                       juce::Justification::centred);
        }
    }
    g.setColour(juce::Colour(0xffb7c6d5));
    g.setFont(12.F);
    const auto amount = processor.dynamicDb[size_t(selected)].load();
    g.drawText("Selected band dynamic gain: " + juce::String(amount, 2) +
                   " dB.  Dynamic controls apply to bells and shelves.",
               20, (getHeight() - ahq::ui::PresetBar::height) - 16, getWidth() - 40, 14,
               juce::Justification::left);
}
void CurveEqEditor::mouseDown(const juce::MouseEvent &e) {
    if (!graph.expanded(10).contains(e.position))
        return;
    auto values = processor.readValues();
    int found = -1;
    float distance = 225;
    for (int i = 0; i < 24; ++i) {
        const auto base = size_t(i) * CurveEqProcessor::fields;
        if (values[base] == 0)
            continue;
        const auto point = juce::Point<float>(xFor(values[base + 3]), yFor(values[base + 4]));
        const auto d = point.getDistanceSquaredFrom(e.position);
        if (d < distance) {
            found = i;
            distance = d;
        }
    }
    if (found < 0)
        return;
    selectBand(found);
    const auto base = size_t(selected) * CurveEqProcessor::fields;
    if (e.mods.isPopupMenu()) {
        if (auto *host = getHostContext())
            if (auto menu = host->getContextMenuForParameter(
                    processor.getParameters().getParameter(processor.parameterId(base + 4))))
                menu->showNativeMenu(e.getPosition());
        return;
    }
    before = values;
    nodeDragging = true;
    for (size_t f : {size_t(3), size_t(4)})
        processor.getParameters()
            .getParameter(processor.parameterId(base + f))
            ->beginChangeGesture();
}
void CurveEqEditor::mouseDoubleClick(const juce::MouseEvent &e) {
    if (!graph.contains(e.position) || nodeDragging || e.mods.isPopupMenu())
        return;
    auto values = processor.readValues();
    for (size_t i = 0; i < 24; ++i)
        if (values[i * CurveEqProcessor::fields] == 0) {
            const auto base = i * CurveEqProcessor::fields;
            values[base] = 1;
            values[base + 3] = float(frequencyFor(e.position.x));
            values[base + 4] = std::clamp(
                (graph.getCentreY() - e.position.y) * 2 * rangeDb / graph.getHeight(), -30.F, 30.F);
            processor.applyUserValues(values, "Add band");
            selectBand(int(i));
            break;
        }
}
void CurveEqEditor::mouseDrag(const juce::MouseEvent &e) {
    if (!nodeDragging)
        return;
    const auto base = size_t(selected) * CurveEqProcessor::fields;
    const std::array<float, 2> values{
        float(frequencyFor(e.position.x)),
        std::clamp((graph.getCentreY() - e.position.y) * 2 * rangeDb / graph.getHeight(), -30.F,
                   30.F)};
    for (size_t i = 0; i < 2; ++i) {
        auto *p = processor.getParameters().getParameter(processor.parameterId(base + 3 + i));
        p->setValueNotifyingHost(p->convertTo0to1(values[i]));
    }
    timerCallback();
}
void CurveEqEditor::mouseUp(const juce::MouseEvent &) {
    if (!nodeDragging)
        return;
    const int base = selected * int(CurveEqProcessor::fields);
    const std::array<int, 2> fields{base + 3, base + 4};
    for (int i : fields)
        processor.getParameters()
            .getParameter(processor.parameterId(size_t(i)))
            ->endChangeGesture();
    processor.recordUserEdit(before, fields);
    nodeDragging = false;
}
void CurveEqEditor::mouseWheelMove(const juce::MouseEvent &e,
                                   const juce::MouseWheelDetails &wheel) {
    if (!graph.contains(e.position))
        return;
    auto values = processor.readValues();
    const auto i = size_t(selected) * CurveEqProcessor::fields + 5;
    values[i] = std::clamp(values[i] * std::exp(wheel.deltaY), .1F, 30.F);
    processor.applyUserValues(values, "Band Q");
}
