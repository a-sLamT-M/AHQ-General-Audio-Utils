#pragma once
#include "parameterControl.h"
#include "ui/parameterGroups.h"
#include "ui/presetBar.h"
#include <array>
#include <cmath>
#include <juce_audio_utils/juce_audio_utils.h>
/// Shared editor for the small dynamics effects. Construction and attachments stay on the UI
/// thread.
template <class Processor>
class ParameterPanel final : public juce::AudioProcessorEditor,
                             private juce::Timer,
                             private juce::ComponentMovementWatcher {
  public:
    explicit ParameterPanel(Processor &p)
        : AudioProcessorEditor(p), ComponentMovementWatcher(this), processor(p) {
        addAndMakeVisible(presetBar);
        history.fill(0);
        size_t index = 0;
        for (auto *parameter : static_cast<juce::AudioProcessor &>(p).getParameters()) {
            auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(parameter);
            if (!ranged)
                continue;
            auto label = std::make_unique<juce::Label>();
            label->setText(parameter->getName(40), juce::dontSendNotification);
            label->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(*label);
            labels.push_back(std::move(label));
            const auto parameterIndex = index++;
            if (auto *choice = dynamic_cast<juce::AudioParameterChoice *>(parameter)) {
                auto box = std::make_unique<juce::ComboBox>();
                box->addItemList(choice->choices, 1);
                bindParameterControl(*box, *ranged);
                auto *control = box.get();
                control->onChange = [this, parameterIndex, control] {
                    auto v = processor.readValues();
                    v[parameterIndex] = float(control->getSelectedId() - 1);
                    processor.applyUserValues(v, "Setting");
                };
                auto attachment =
                    std::make_unique<juce::ParameterAttachment>(*ranged, [control](float v) {
                        control->setSelectedId(int(v) + 1, juce::dontSendNotification);
                    });
                attachment->sendInitialUpdate();
                addAndMakeVisible(*control);
                controls.push_back(std::move(box));
                attachments.push_back(std::move(attachment));
            } else {
                auto slider = std::make_unique<ParameterSlider>();
                slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 24);
                slider->bindParameter(*ranged);
                const auto name = parameter->getName(40);
                if (name.startsWithIgnoreCase("Output") || name.startsWithIgnoreCase("Master") ||
                    (p.getName() == "AHQ Opto Leveler" && parameterIndex == 1))
                    slider->setSliderStyle(juce::Slider::LinearVertical);
                const auto &range = ranged->getNormalisableRange();
                slider->setNormalisableRange({double(range.start), double(range.end),
                                              double(range.interval), double(range.skew),
                                              range.symmetricSkew});
                slider->setNumDecimalPlacesToDisplay(range.interval >= 1 ? 0 : 2);
                slider->setDoubleClickReturnValue(
                    true, ranged->convertFrom0to1(ranged->getDefaultValue()));
                slider->setColour(juce::Slider::rotarySliderFillColourId, appearance.accent);
                auto *control = slider.get();
                control->onDragStart = [this, ranged] {
                    before = processor.readValues();
                    dragging = true;
                    ranged->beginChangeGesture();
                };
                control->onValueChange = [this, control, ranged, parameterIndex] {
                    if (dragging)
                        ranged->setValueNotifyingHost(
                            ranged->convertTo0to1(float(control->getValue())));
                    else {
                        auto v = processor.readValues();
                        v[parameterIndex] = float(control->getValue());
                        processor.applyUserValues(v, "Value");
                    }
                };
                control->onDragEnd = [this, ranged, parameterIndex] {
                    ranged->endChangeGesture();
                    dragging = false;
                    processor.recordUserEdit(before, int(parameterIndex));
                };
                auto attachment =
                    std::make_unique<juce::ParameterAttachment>(*ranged, [control](float v) {
                        control->setValue(v, juce::dontSendNotification);
                    });
                attachment->sendInitialUpdate();
                addAndMakeVisible(*control);
                controls.push_back(std::move(slider));
                attachments.push_back(std::move(attachment));
            }
        }
        for (auto *button : {&undoButton, &redoButton})
            addAndMakeVisible(*button);
        undoButton.onClick = [this] { processor.undo(); };
        redoButton.onClick = [this] { processor.redo(); };
        if constexpr (requires { Processor::masterOutputIndex; }) {
            auto *master =
                dynamic_cast<ParameterSlider *>(controls[Processor::masterOutputIndex].get());
            master->setSliderStyle(juce::Slider::LinearVertical);
            master->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 26);
            master->setTextValueSuffix(" dB");
            for (auto *component : std::array<juce::Component *, 1>{&presets})
                addAndMakeVisible(component);
            presets.setTextWhenNothingSelected("Routing presets");
            presets.setTooltip(
                "Changes slot routing and levels; keeps your IRs and master volume.");
            for (int i = 0; i < p.getNumPrograms(); ++i)
                presets.addItem(p.getProgramName(i), i + 1);
            presets.onChange = [this] {
                if (presets.getSelectedId() > 0) {
                    processor.loadFactoryPreset(presets.getSelectedId() - 1);
                    presets.setSelectedId(0, juce::dontSendNotification);
                }
            };
        }
        if constexpr (requires { p.impulseBank(); }) {
            for (int i = 0; i < Processor::impulseSlots; ++i) {
                auto button = std::make_unique<juce::TextButton>("Load IR " + juce::String(i + 1));
                button->onClick = [this, i] {
                    choosers[size_t(i)] = std::make_unique<juce::FileChooser>(
                        "Load impulse response", juce::File{}, "*.wav;*.aif;*.aiff;*.flac");
                    const juce::Component::SafePointer<ParameterPanel> safe(this);
                    choosers[size_t(i)]->launchAsync(
                        juce::FileBrowserComponent::openMode |
                            juce::FileBrowserComponent::canSelectFiles,
                        [safe, i](const juce::FileChooser &chooser) {
                            if (safe != nullptr && chooser.getResult() != juce::File{})
                                safe->processor.impulseBank().requestFile(i, chooser.getResult());
                        });
                };
                addAndMakeVisible(*button);
                fileButtons.push_back(std::move(button));
                auto label = std::make_unique<juce::Label>();
                label->setJustificationType(juce::Justification::centredLeft);
                label->setText(p.impulseBank().status(i), juce::dontSendNotification);
                addAndMakeVisible(*label);
                fileLabels.push_back(std::move(label));
            }
        }
        std::vector<int> order;
        const auto product = p.getName();
        if (product == "AHQ FET Compressor") {
            order = {0, 2, 3, 4, 1, 5};
            groups = {{"Input", 0, 1}, {"Compression", 1, 3}, {"Output", 4, 2}};
        } else if (product == "AHQ Opto Leveler") {
            order = {3, 0, 2, 1, 4};
            groups = {{"Input", 0, 1}, {"Compression", 1, 2}, {"Output", 3, 2}};
        } else if (product == "AHQ Noise Gate") {
            groups = {{"Detector", 0, 2}, {"Envelope", 2, 3}, {"Floor and mix", 5, 2}};
        } else if (product == "AHQ Tremolo") {
            groups = {{"Timing and shape", 0, 4}, {"Depth and mix", 4, 2}};
        } else if (product == "AHQ Hybrid Reverb") {
            groups = {{"Early / late balance", 0, 2}, {"Tail", 2, 2}, {"Space and mix", 4, 3}};
        } else if (product == "AHQ Gated Reverb") {
            groups = {{"Reverb shape", 0, 3}, {"Pre-delay and mix", 3, 2}};
        } else if (product == "AHQ Retro Tools") {
            groups = {{"Ensemble", 0, 2},
                      {"Tape motion", 2, 2},
                      {"Colour", 4, 3},
                      {"Mix and fault", 7, 2}};
        }
        if (order.empty())
            for (size_t i = 0; i < controls.size(); ++i)
                order.push_back(int(i));
        for (int i : order) {
            groupedLabels.push_back(labels[size_t(i)].get());
            groupedControls.push_back(controls[size_t(i)].get());
        }
        if (groups.empty())
            groups = {{"Controls", 0, int(controls.size())}};
        setResizable(true, true);
        setResizeLimits(1000, 850, 2000, 1500);
        setSize(1200, 960);
        visibilityChanged();
    }
    int getControlParameterIndex(juce::Component &c) override { return controlParameterIndex(c); }
    void paint(juce::Graphics &g) override {
        appearance.paintBackground(g, getWidth());
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(25));
        g.drawText(processor.getName(), 28, 18, getWidth() - 220, 34,
                   juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffa8b7c3));
        g.setFont(juce::FontOptions(14));
        g.drawText(processor.description(), 28, 55, getWidth() - 56, 24,
                   juce::Justification::centredLeft);
        if (fileButtons.size() > 1) {
            const int usable = getWidth() - 190;
            const int width = usable / 4;
            for (int i = 0; i < 4; ++i) {
                appearance.paintGroup(
                    g, {float(28 + i * width), 142, float(width - 12), float(getHeight() - 250)},
                    "IR " + juce::String(i + 1));
            }
            appearance.paintGroup(g, {float(getWidth() - 146), 142, 120, float(getHeight() - 250)},
                                  "Master");
            appearance.paintFlow(g, {28, 92, float(getWidth() - 56), 28},
                                 {"Input", "4 parallel IR branches", "Sum", "Master", "Output"});
            // Each IR joins a common summing bus; these are parallel, never a serial chain.
            const float busY = float(getHeight() - 92), sumX = float(getWidth() - 86);
            g.setColour(appearance.accent.withAlpha(.6F));
            for (int i = 0; i < 4; ++i) {
                const float x = float(28 + i * width + (width - 12) / 2);
                g.drawLine(x, 134, x, 142, 1);
                g.drawLine(x, float(getHeight() - 108), x, busY, 1);
                g.drawLine(x, busY, sumX, busY, 1);
            }
            g.drawArrow({sumX, busY, sumX, float(getHeight() - 108)}, 1, 5, 5);
            return;
        }
        appearance.paintFlow(g, {28, 84, float(getWidth() - 56), 24},
                             {"Input", "Processing + dry / wet", "Output"});
        ahq::ui::paintGroups(g, appearance, groupedLabels, groupedControls, groups);
        g.setColour(juce::Colour(0xff1a252e));
        g.fillRoundedRectangle(graph, 8);
        auto plot = graph.reduced(54, 35);
        g.setFont(juce::FontOptions(12));
        for (int db : {0, -12, -24, -36, -48}) {
            const float y = plot.getY() - float(db) / 48 * plot.getHeight();
            g.setColour(juce::Colour(0xff34434f));
            g.drawHorizontalLine(int(y), plot.getX(), plot.getRight());
            g.setColour(juce::Colour(0xff9cabb6));
            g.drawText(juce::String(db), int(graph.getX()) + 10, int(y) - 8, 35, 16,
                       juce::Justification::centredRight);
        }
        juce::Path path;
        for (size_t i = 0; i < history.size(); ++i) {
            const float x = plot.getX() + float(i) / float(history.size() - 1) * plot.getWidth();
            const float y =
                plot.getY() - std::clamp(history[(cursor + i) % history.size()], -48.F, 0.F) / 48 *
                                  plot.getHeight();
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        g.setColour(appearance.accent);
        g.strokePath(path, juce::PathStrokeType(2));
        g.setFont(juce::FontOptions(14));
        juce::String meterName = "GAIN HISTORY";
        if constexpr (requires { Processor::outputMeter; })
            meterName = "OUTPUT PEAK HISTORY";
        g.drawText(meterName + "  /  " + juce::String(displayDb, 1) + " dB",
                   graph.withHeight(28).reduced(16, 0), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff9cabb6));
        g.setFont(juce::FontOptions(12));
        g.drawText("Double-click: default     Right-click: host automation", 28,
                   (getHeight() - ahq::ui::PresetBar::height) - 28, getWidth() - 56, 18,
                   juce::Justification::centredLeft);
    }
    void resized() override {
        presetBar.setBounds(0, getHeight() - ahq::ui::PresetBar::height, getWidth(),
                            ahq::ui::PresetBar::height);
        undoButton.setBounds(getWidth() - 190, 22, 72, 28);
        redoButton.setBounds(getWidth() - 110, 22, 72, 28);
        if constexpr (requires { Processor::masterOutputIndex; }) {
            presets.setBounds(280, 18, 240, 30);
            labels[Processor::masterOutputIndex]->setBounds(getWidth() - 140, 184, 108, 24);
            controls[Processor::masterOutputIndex]->setBounds(getWidth() - 130, 222, 88,
                                                              getHeight() - 360);
        }
        if (fileButtons.size() > 1) {
            graph = {};
            const int width = (getWidth() - 190) / 4;
            for (int row = 0; row < 4; ++row) {
                const int x = 28 + row * width;
                fileButtons[size_t(row)]->setBounds(x + 10, 174, width - 32, 28);
                fileLabels[size_t(row)]->setBounds(x + 10, 210, width - 32, 44);
                const int h = (getHeight() - 374) / 5;
                for (int field = 0; field < 5; ++field) {
                    const size_t i = size_t(row * 5 + field);
                    labels[i]->setBounds(x + 10, 260 + field * h, width - 32, 20);
                    controls[i]->setBounds(
                        x + 18, 282 + field * h, width - 48,
                        dynamic_cast<juce::ComboBox *>(controls[i].get()) ? 28 : h - 26);
                    if (field == 1)
                        if (auto *slider = dynamic_cast<juce::Slider *>(controls[i].get()))
                            slider->setSliderStyle(juce::Slider::LinearHorizontal);
                }
            }
            return;
        }
        const int rightWidth = juce::jmax(370, getWidth() * 38 / 100);
        const int rightX = getWidth() - rightWidth - 28;
        graph = {28, 126, float(rightX - 48), float(getHeight() - 250)};
        if (fileButtons.size() == 1) {
            fileButtons[0]->setBounds(28, 122, 140, 28);
            fileLabels[0]->setBounds(180, 122, rightX - 200, 28);
            graph = graph.withTrimmedTop(42);
        }
        ahq::ui::layoutGroups(groupedLabels, groupedControls, groups,
                              {rightX, 122, rightWidth, getHeight() - 226});
        for (auto *control : groupedControls)
            if (dynamic_cast<juce::ComboBox *>(control))
                control->setSize(control->getWidth(), 28);
    }

    void visibilityChanged() override {
        if (isShowing())
            startTimerHz(30);
        else
            stopTimer();
    }

  private:
    static constexpr int presetToolbarHeight = requires { Processor::masterOutputIndex; } ? 70 : 0;
    void componentMovedOrResized(bool, bool) override {}
    void componentPeerChanged() override { visibilityChanged(); }
    void componentVisibilityChanged() override { visibilityChanged(); }
    void timerCallback() override {
        if constexpr (requires { processor.impulseBank(); }) {
            for (size_t i = 0; i < fileLabels.size(); ++i) {
                const auto status = processor.impulseBank().status(int(i));
                fileLabels[i]->setText(status, juce::dontSendNotification);
                fileLabels[i]->setTooltip(status);
            }
        }
        displayDb = processor.displayGainDb.load(std::memory_order_relaxed);
        history[cursor] = displayDb;
        cursor = (cursor + 1) % history.size();
        repaint(graph.toNearestInt());
    }
    Processor &processor;
    ahq::ui::PluginTheme appearance{*this, processor.getName()};
    ahq::ui::PresetBar presetBar{processor, appearance};
    std::vector<ahq::ui::ParameterGroup> groups;
    std::vector<juce::Label *> groupedLabels;
    std::vector<juce::Component *> groupedControls;
    std::vector<std::unique_ptr<juce::Component>> controls;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> attachments;
    std::vector<std::unique_ptr<juce::TextButton>> fileButtons;
    std::vector<std::unique_ptr<juce::Label>> fileLabels;
    std::array<std::unique_ptr<juce::FileChooser>, 4> choosers;
    typename Processor::Values before{};
    std::array<float, 180> history{};
    size_t cursor = 0;
    float displayDb = 0;
    bool dragging = false;
    juce::Rectangle<float> graph;
    juce::TextButton undoButton{"Undo"}, redoButton{"Redo"};
    juce::ComboBox presets;
};
