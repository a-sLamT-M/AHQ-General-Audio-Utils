#include "synthEditor.h"
#include <cmath>
SynthEditor::SynthEditor(SynthProcessor &p)
    : AudioProcessorEditor(p), ComponentMovementWatcher(this), processor(p),
      display(p.getAnalysis()) {
    addAndMakeVisible(presetBar);
    addAndMakeVisible(display.controls);
    display.controls.onChange = [this] {
        resized();
        repaint();
    };
    for (auto *c : std::array<juce::Component *, 9>{&presets, &page, &generator, &combination,
                                                    &mode, &undo, &redo, &panic, &status})
        addAndMakeVisible(c);
    for (int i = 0; i < p.getNumPrograms(); ++i)
        presets.addItem(juce::String(i + 1).paddedLeft('0', 2) + "  " + p.getProgramName(i), i + 1);
    displayedProgram = p.currentFactoryProgram();
    presets.setTextWhenNothingSelected("Edited program");
    presets.setSelectedId(displayedProgram + 1, juce::dontSendNotification);
    presets.onChange = [this] {
        if (presets.getSelectedId() > 0)
            processor.loadFactory(presets.getSelectedId() - 1, true);
    };
    page.addItemList({"Generators", "Envelopes + filter", "LFO + modulation", "Effects"}, 1);
    if (p.pcm)
        page.addItem("Combination", 5);
    page.setSelectedId(1, juce::dontSendNotification);
    page.onChange = [this] { rebuild(); };
    for (int i = 0; i < p.generatorCount; ++i)
        generator.addItem((p.pcm ? "Oscillator " : "Generator ") + juce::String(i + 1), i + 1);
    generator.setSelectedId(1, juce::dontSendNotification);
    generator.onChange = [this] { rebuild(); };
    if (p.pcm) {
        for (size_t i = 0; i < ahq::audio::Synthesizer::combiCount; ++i)
            combination.addItem(ahq::audio::Synthesizer::combiName(i), int(i + 1));
        combination.setTextWhenNothingSelected("Load a combination");
        combination.onChange = [this] {
            processor.loadCombination(combination.getSelectedId() - 1);
            page.setSelectedId(5);
        };
        mode.addItemList({"Program", "Combination"}, 1);
        bindParameterControl(mode, *p.parameter(p.modeIndex));
        mode.onChange = [this] {
            auto v = processor.readValues();
            v[size_t(processor.modeIndex)] = float(mode.getSelectedId() - 1);
            processor.applyUserValues(v, "Mode");
        };
        modeAttachment = std::make_unique<juce::ParameterAttachment>(
            *p.parameter(p.modeIndex), [this](float value) {
                mode.setSelectedId(int(value) + 1, juce::dontSendNotification);
            });
        modeAttachment->sendInitialUpdate();
    } else {
        mode.setVisible(false);
        combination.setVisible(false);
    }
    undo.onClick = [this] { processor.undo(); };
    redo.onClick = [this] { processor.redo(); };
    panic.onClick = [this] {
        processor.panic();
        soundingKey = -1;
        repaint();
    };
    status.setJustificationType(juce::Justification::centredRight);
    setResizable(true, true);
    setResizeLimits(1200, 1000, 2000, 1600);
    setSize(1600, 1100);
    rebuild();
    visibilityChanged();
}
SynthEditor::~SynthEditor() {
    stopTimer();
    if (soundingKey >= 0)
        processor.audition(soundingKey, false);
    processor.setDisplayEnabled(false);
}
void SynthEditor::visibilityChanged() {
    processor.setDisplayEnabled(isShowing() && page.getSelectedId() != 5);
    if (isShowing())
        startTimerHz(30);
    else
        stopTimer();
}
void SynthEditor::addControl(int index, bool compact) {
    auto item = std::make_unique<Control>();
    item->parameter = index;
    item->compact = compact;
    auto *parameter = processor.parameter(index);
    item->label.setText(parameter->getName(64), juce::dontSendNotification);
    item->label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(item->label);
    if (auto *choice = dynamic_cast<juce::AudioParameterChoice *>(parameter)) {
        auto control = std::make_unique<juce::ComboBox>();
        control->addItemList(choice->choices, 1);
        bindParameterControl(*control, *parameter);
        auto *box = control.get();
        box->onChange = [this, index, box] {
            auto v = processor.readValues();
            v[size_t(index)] = float(box->getSelectedId() - 1);
            processor.applyUserValues(v, "Choice");
        };
        item->attachment =
            std::make_unique<juce::ParameterAttachment>(*parameter, [box](float value) {
                box->setSelectedId(int(value) + 1, juce::dontSendNotification);
            });
        item->component = std::move(control);
    } else {
        auto control = std::make_unique<ParameterSlider>();
        auto *slider = control.get();
        slider->bindParameter(*parameter);
        const auto &range = parameter->getNormalisableRange();
        slider->setNormalisableRange({double(range.start), double(range.end),
                                      double(range.interval), double(range.skew),
                                      range.symmetricSkew});
        slider->setSliderStyle(compact ? juce::Slider::LinearHorizontal
                                       : juce::Slider::RotaryHorizontalVerticalDrag);
        if (index == processor.effectsOffset + 4 ||
            (index < processor.envelopeOffset && index % 10 == 6))
            slider->setSliderStyle(juce::Slider::LinearVertical);
        slider->setTextBoxStyle(compact ? juce::Slider::TextBoxLeft : juce::Slider::TextBoxBelow,
                                false, compact ? 46 : 100, 24);
        slider->setNumDecimalPlacesToDisplay(range.interval >= 1 ? 0 : 2);
        slider->setDoubleClickReturnValue(true,
                                          parameter->convertFrom0to1(parameter->getDefaultValue()));
        slider->setColour(juce::Slider::rotarySliderFillColourId, appearance.accent);
        slider->onDragStart = [this, parameter] {
            before = processor.readValues();
            dragging = true;
            parameter->beginChangeGesture();
        };
        slider->onValueChange = [this, index, slider, parameter] {
            if (dragging)
                parameter->setValueNotifyingHost(
                    parameter->convertTo0to1(float(slider->getValue())));
            else {
                auto v = processor.readValues();
                v[size_t(index)] = float(slider->getValue());
                processor.applyUserValues(v, "Value");
            }
        };
        slider->onDragEnd = [this, index, parameter] {
            parameter->endChangeGesture();
            dragging = false;
            processor.recordUserEdit(before, index);
        };
        item->attachment =
            std::make_unique<juce::ParameterAttachment>(*parameter, [slider](float value) {
                slider->setValue(value, juce::dontSendNotification);
            });
        item->component = std::move(control);
    }
    addAndMakeVisible(*item->component);
    item->attachment->sendInitialUpdate();
    controls.push_back(std::move(item));
}
void SynthEditor::rebuild() {
    controls.clear();
    groups.clear();
    const int selected = page.getSelectedId();
    generator.setVisible(selected == 1);
    switch (selected) {
    case 1:
        groups = {{"Source", 0, 3},
                  {"Voices and tuning", 3, 3},
                  {"Level and pan", 6, 2},
                  {"Shape and modulation", 8, 2}};
        for (int i = 0; i < 10; ++i)
            addControl((generator.getSelectedId() - 1) * 10 + i);
        break;
    case 2:
        groups = {{"Amplitude envelope", 0, 4}, {"Modulation envelope", 4, 4}, {"Filter", 8, 5}};
        for (int i = 0; i < 13; ++i)
            addControl(processor.envelopeOffset + i);
        break;
    case 3:
        groups = {{"LFO 1", 0, 4}, {"LFO 2", 4, 4}};
        for (int i = 0; i < 8; ++i)
            addControl(processor.lfoOffset + i);
        for (int i = 0; i < processor.routeCount * 3; ++i)
            addControl(processor.modulationOffset + i, true);
        break;
    case 4:
        groups = {{"Drive and chorus", 0, 2}, {"Reverb", 2, 2}, {"Master and voices", 4, 2}};
        for (int i = 0; i < 6; ++i)
            addControl(processor.effectsOffset + i);
        break;
    case 5:
        for (int i = 0; i < 64; ++i)
            addControl(processor.partsOffset + i, true);
        break;
    default:
        break;
    }
    resized();
    visibilityChanged();
    repaint();
}
void SynthEditor::place(size_t index, juce::Rectangle<int> bounds, bool showLabel) {
    if (index >= controls.size())
        return;
    auto &item = *controls[index];
    item.label.setVisible(showLabel);
    if (showLabel)
        item.label.setBounds(bounds.removeFromTop(28));
    bounds.reduce(5, 4);
    if (dynamic_cast<juce::ComboBox *>(item.component.get()))
        item.component->setBounds(bounds.withHeight(30));
    else
        item.component->setBounds(bounds);
}
void SynthEditor::resized() {
    presetBar.setBounds(0, getHeight() - ahq::ui::PresetBar::height, getWidth(),
                        ahq::ui::PresetBar::height);
    presets.setBounds(245, 24, 290, 30);
    undo.setBounds(548, 24, 64, 30);
    redo.setBounds(622, 24, 64, 30);
    panic.setBounds(696, 24, 72, 30);
    status.setBounds(getWidth() - 200, 24, 172, 30);
    page.setBounds(28, 78, 220, 30);
    generator.setBounds(265, 78, 200, 30);
    mode.setBounds(getWidth() - 425, 78, 150, 30);
    combination.setBounds(getWidth() - 260, 78, 232, 30);
    content = {28, 130, getWidth() - 56, (getHeight() - ahq::ui::PresetBar::height) - 270};
    spectrum = {};
    const int selected = page.getSelectedId();
    display.controls.setVisible(selected == 1 || selected == 2 || selected == 4);
    display.controls.setBounds(28, 116, getWidth() - 56, 30);
    if (selected == 1 || selected == 2 || selected == 4) {
        const int rightWidth = juce::jmax(
            390, int(content.getWidth() * (1 - display.controls.preferences.share.load() / 100.F)));
        const int x = content.getRight() - rightWidth;
        std::vector<juce::Label *> names;
        std::vector<juce::Component *> widgets;
        for (auto &item : controls) {
            names.push_back(&item->label);
            widgets.push_back(item->component.get());
        }
        ahq::ui::layoutGroups(names, widgets, groups,
                              {x, 166, rightWidth, content.getBottom() - 166});
        for (auto *widget : widgets)
            if (dynamic_cast<juce::ComboBox *>(widget))
                widget->setSize(widget->getWidth(), 28);
        spectrum = {content.getX() + 38, 182, x - content.getX() - 72, content.getBottom() - 220};
    } else if (selected == 3) {
        const int lw = content.getWidth() / 8;
        for (size_t i = 0; i < 8; ++i)
            place(i, {content.getX() + int(i) * lw, content.getY() + 24, lw, 112});
        const int rows = processor.routeCount,
                  rowHeight = std::min(46, (content.getHeight() - 165) / rows);
        const int rowY = content.getY() + 160, width = content.getWidth() / 3;
        for (int row = 0; row < rows; ++row)
            for (int col = 0; col < 3; ++col)
                place(size_t(8 + row * 3 + col),
                      {content.getX() + col * width, rowY + row * rowHeight, width, rowHeight},
                      false);
    } else if (selected == 5) {
        const int available = content.getWidth(), wide = std::max(195, available / 5);
        const int narrow = (available - wide) / 7;
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col) {
                const int x =
                    col == 0 ? content.getX() : content.getX() + wide + (col - 1) * narrow;
                place(size_t(row * 8 + col),
                      {x, content.getY() + 35 + row * 56, col == 0 ? wide : narrow, 50}, false);
            }
    }
    keyboard = {28, float((getHeight() - ahq::ui::PresetBar::height) - 112), float(getWidth() - 56),
                84};
    const float whiteWidth = keyboard.getWidth() / 14;
    int white = 0;
    for (size_t i = 0; i < 24; ++i) {
        const int pitch = int(i % 12);
        blackKeys[i] = pitch == 1 || pitch == 3 || pitch == 6 || pitch == 8 || pitch == 10;
        if (blackKeys[i])
            keyBounds[i] = {keyboard.getX() + float(white) * whiteWidth - whiteWidth * .3F,
                            keyboard.getY(), whiteWidth * .6F, keyboard.getHeight() * .62F};
        else {
            keyBounds[i] = {keyboard.getX() + float(white) * whiteWidth, keyboard.getY(),
                            whiteWidth, keyboard.getHeight()};
            ++white;
        }
    }
}
void SynthEditor::paint(juce::Graphics &g) {
    appearance.paintBackground(g, getWidth());
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(24));
    g.drawText(processor.getName(), 28, 22, 210, 34, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff1b2630));
    g.fillRoundedRectangle(content.toFloat(), 8);
    if (!spectrum.isEmpty() && spectrum.getHeight() > 30)
        display.paintConfiguredSpectrum(g, spectrum.toFloat(), appearance.accent);
    for (auto group : groups) {
        juce::Rectangle<int> box;
        for (int j = 0; j < group.count; ++j) {
            const auto &item = *controls[size_t(group.first + j)];
            box = box.getUnion(item.label.getBounds()).getUnion(item.component->getBounds());
        }
        appearance.paintGroup(g, box.expanded(5, 4).withTrimmedTop(-24).toFloat(), group.title);
    }
    appearance.paintFlow(g, {28, 56, float(getWidth() - 56), 20},
                         {"Generators", "Voice filter / amp", "Effects", "Master", "Output"});
    g.setColour(juce::Colour(0xffb6c6d2));
    g.setFont(juce::FontOptions(12));
    if (page.getSelectedId() == 1) {
        g.drawText("Phase modulation uses the preceding generator. Shape morphs the wavetable or "
                   "changes pulse width.",
                   content.getX() + 14, int(keyboard.getY()) - 24, content.getWidth() - 28, 20,
                   juce::Justification::left);
    } else if (page.getSelectedId() == 3) {
        g.drawText("Source", content.getX() + 12, content.getY() + 140, 200, 20,
                   juce::Justification::left);
        g.drawText("Destination", content.getX() + content.getWidth() / 3 + 12,
                   content.getY() + 140, 200, 20, juce::Justification::left);
        g.drawText("Amount", content.getX() + content.getWidth() * 2 / 3 + 12, content.getY() + 140,
                   200, 20, juce::Justification::left);
    } else if (page.getSelectedId() == 5) {
        const char *headers[]{"Program",  "Enable", "MIDI",  "Low key",
                              "High key", "Tune",   "Level", "Pan"};
        const int wide = std::max(195, content.getWidth() / 5),
                  narrow = (content.getWidth() - wide) / 7;
        for (int i = 0; i < 8; ++i)
            g.drawText(
                headers[i], i == 0 ? content.getX() : content.getX() + wide + (i - 1) * narrow,
                content.getY() + 4, i == 0 ? wide : narrow, 26, juce::Justification::centred);
        g.drawText(
            "Each row is one part. Combination mode uses the factory program selected in that row.",
            content.getX() + 12, content.getBottom() - 24, content.getWidth() - 24, 20,
            juce::Justification::left);
    }
    for (int black = 0; black < 2; ++black)
        for (size_t i = 0; i < keyBounds.size(); ++i)
            if (blackKeys[i] == bool(black)) {
                g.setColour(soundingKey == int(i) + 48 ? appearance.accent
                            : black                    ? juce::Colour(0xff263543)
                                                       : juce::Colour(0xffd4dde4));
                g.fillRect(keyBounds[i].reduced(1, 0));
                if (i % 12 == 0) {
                    g.setColour(juce::Colour(0xff263543));
                    g.drawText(i == 0 ? "C3" : "C4", keyBounds[i].toNearestInt().withTrimmedTop(60),
                               juce::Justification::centred);
                }
            }
    g.setColour(juce::Colour(0xff9cabb6));
    g.drawText("Original sounds  /  right-click a control for host automation", 28,
               (getHeight() - ahq::ui::PresetBar::height) - 24, getWidth() - 56, 18,
               juce::Justification::left);
}
int SynthEditor::keyAt(juce::Point<float> p) const {
    for (size_t i = 0; i < 24; ++i)
        if (blackKeys[i] && keyBounds[i].contains(p))
            return int(i) + 48;
    for (size_t i = 0; i < 24; ++i)
        if (!blackKeys[i] && keyBounds[i].contains(p))
            return int(i) + 48;
    return -1;
}
void SynthEditor::playKey(int note) {
    if (note == soundingKey)
        return;
    if (soundingKey >= 0)
        processor.audition(soundingKey, false);
    soundingKey = note;
    if (note >= 0)
        processor.audition(note, true);
    repaint(keyboard.toNearestInt());
}
void SynthEditor::mouseDown(const juce::MouseEvent &e) {
    if (!e.mods.isPopupMenu())
        playKey(keyAt(e.position));
}
void SynthEditor::mouseDrag(const juce::MouseEvent &e) {
    if (soundingKey >= 0)
        playKey(keyAt(e.position));
}
void SynthEditor::mouseUp(const juce::MouseEvent &) { playKey(-1); }
void SynthEditor::timerCallback() {
    const int factory = processor.currentFactoryProgram();
    if (factory != displayedProgram) {
        displayedProgram = factory;
        presets.setSelectedId(factory + 1, juce::dontSendNotification);
    }
    display.refresh(!spectrum.isEmpty());
    status.setText(juce::String(processor.displayedVoices.load()) + " voices",
                   juce::dontSendNotification);
    const auto values = processor.readValues();
    const bool combinationActive = processor.pcm && values[size_t(processor.modeIndex)] != 0;
    const bool editable =
        !combinationActive || page.getSelectedId() == 4 || page.getSelectedId() == 5;
    for (auto &control : controls)
        control->component->setEnabled(editable);
    if (!spectrum.isEmpty())
        repaint(spectrum);
}
