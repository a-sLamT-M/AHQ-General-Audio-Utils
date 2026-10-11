#include "trueLimiterEditor.h"
#include <cmath>

namespace {
constexpr std::array<int, 6> INDEX{0, 1, 3, 4, 5, 7};
constexpr std::array<const char *, 6> IDS{"ceiling",   "target",  "gain",
                                          "lookahead", "release", "mix"};
juce::String lufs(double value) { return std::isfinite(value) ? juce::String(value, 2) : "--"; }
} // namespace
TrueLimiterEditor::TrueLimiterEditor(TrueLimiterProcessor &owner)
    : AudioProcessorEditor(owner), ComponentMovementWatcher(this), processor(owner),
      display(owner.getAnalysis()) {
    setSize(720, 980);
    constexpr std::array<const char *, 6> names{"Peak ceiling", "Target LUFS",
                                                "Input gain",   "Lookahead",
                                                "Release",      "Mix before protection"};
    constexpr std::array<const char *, 6> suffixes{" dB", " LUFS", " dB", " ms", " ms", ""};
    for (size_t i = 0; i < sliders.size(); ++i) {
        auto &slider = sliders[i];
        addAndMakeVisible(slider);
        addAndMakeVisible(labels[i]);
        labels[i].setText(names[i], juce::dontSendNotification);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 130, 25);
        slider.setNumDecimalPlacesToDisplay(2);
        slider.setTextValueSuffix(suffixes[i]);
        auto *parameter = processor.getParameters().getParameter(IDS[i]);
        const auto &range = parameter->getNormalisableRange();
        slider.setRange(range.start, range.end, range.interval);
        slider.onDragStart = [this, parameter] {
            dragBefore = processor.readValues();
            dragging = true;
            parameter->beginChangeGesture();
        };
        slider.onValueChange = [this, i, parameter] {
            if (dragging)
                parameter->setValueNotifyingHost(
                    parameter->convertTo0to1(static_cast<float>(sliders[i].getValue())));
            else {
                auto values = processor.readValues();
                values[static_cast<size_t>(INDEX[i])] = static_cast<float>(sliders[i].getValue());
                processor.applyUserValues(values, "Edit limiter parameter");
            }
        };
        slider.onDragEnd = [this, i, parameter] {
            parameter->endChangeGesture();
            dragging = false;
            processor.recordUserEdit(dragBefore, INDEX[i], "Edit limiter parameter");
        };
    }
    for (auto *component : std::array<juce::Component *, 9>{&live, &quality, &analyze, &cancel,
                                                            &apply, &undo, &redo, &meters, &result})
        addAndMakeVisible(component);
    addAndMakeVisible(notice);
    quality.addItem("4x", 1);
    quality.addItem("8x", 2);
    quality.addItem("16x", 3);
    quality.onChange = [this] {
        auto v = processor.readValues();
        v[6] = static_cast<float>(quality.getSelectedId() - 1);
        processor.applyUserValues(v, "Change peak detection quality");
    };
    live.onClick = [this] {
        auto v = processor.readValues();
        v[2] = live.getToggleState() ? 1.0F : 0.0F;
        processor.applyUserValues(v, "Change live loudness mode");
    };
    analyze.onClick = [this] { chooseAudio(); };
    cancel.onClick = [this] {
        processor.cancelFileAnalysis();
        timerCallback();
    };
    apply.onClick = [this] {
        processor.applyFileAnalysis();
        timerCallback();
    };
    undo.onClick = [this] {
        processor.undo();
        timerCallback();
    };
    redo.onClick = [this] {
        processor.redo();
        timerCallback();
    };
    notice.setFont(juce::FontOptions(12.0F));
    result.setText("Select a mono/stereo 32-384 kHz file for a two-pass integrated match.\n"
                   "Apply restores the analyzed settings, gain, mix 1 and live mode off.",
                   juce::dontSendNotification);
    result.setFont(juce::FontOptions(13.0F));
    sliders[3].setTooltip(
        "Effective advance detection; host latency stays fixed at 20 ms + 24 frames.");
    live.setTooltip(
        "Corrects raw 3 s input LUFS every 100 ms; compensates manual gain, smooths for 250 ms.");
    // Parameter changes drive choices; timer polling can overwrite a pending menu selection.
    qualityAttachment = std::make_unique<juce::ParameterAttachment>(
        *processor.getParameters().getParameter("quality"), [this](float value) {
            quality.setSelectedId(juce::roundToInt(value) + 1, juce::dontSendNotification);
        });
    qualityAttachment->sendInitialUpdate();
    timerCallback();
    updateSpectrumVisibility();
}
TrueLimiterEditor::~TrueLimiterEditor() {
    stopTimer();
    processor.getAnalysis().setSpectrumEnabled(false);
}
void TrueLimiterEditor::visibilityChanged() { updateSpectrumVisibility(); }
void TrueLimiterEditor::parentHierarchyChanged() { updateSpectrumVisibility(); }
void TrueLimiterEditor::updateSpectrumVisibility() {
    const bool showing = isShowing();
    if (showing == spectrumVisible)
        return;
    spectrumVisible = showing;
    processor.getAnalysis().setSpectrumEnabled(showing);
    if (showing)
        startTimerHz(30);
    else
        stopTimer();
}
void TrueLimiterEditor::timerCallback() {
    const auto values = processor.readValues();
    const auto sampleRate = processor.getAnalysis().getSpectrumSampleRate();
    notice.setText(
        sampleRate < 32000.0 || sampleRate > 384000.0
            ? "Processing bypassed: requires 32-384 kHz."
            : "Mix is before final peak protection; even mix 0 is protected.\n"
              "Finite FIR estimate: 4x keeps 1.75 dB headroom; 8x/16x keep 1.6 dB.\n"
              "Ceiling is an upper limit; live LUFS cannot predict full-program loudness.",
        juce::dontSendNotification);
    if (!dragging)
        for (size_t i = 0; i < sliders.size(); ++i)
            sliders[i].setValue(values[static_cast<size_t>(INDEX[i])], juce::dontSendNotification);
    live.setToggleState(values[2] != 0, juce::dontSendNotification);
    undo.setEnabled(processor.canUndo());
    redo.setEnabled(processor.canRedo());
    meters.setText(
        "Input S: " + lufs(processor.inputShortTermLufs()) +
            " LUFS   Output S: " + lufs(processor.outputShortTermLufs()) +
            " LUFS   Reduction: " + lufs(processor.gainReductionDb()) + " dB\n" +
            "Output 4x TP estimate (since prepare): " + lufs(processor.outputTruePeakDb()) +
            " dBTP   Latency: " + juce::String(processor.getLatencySamples()) + " frames",
        juce::dontSendNotification);
    const auto analysis = processor.fileAnalysis();
    cancel.setEnabled(analysis.running);
    apply.setEnabled(analysis.valid && !analysis.running);
    if (analysis.running)
        result.setText("Analyzing " + analysis.fileName + " in two streaming passes...",
                       juce::dontSendNotification);
    else if (analysis.valid)
        result.setText(analysis.fileName + " | " + juce::String(analysis.sampleRate, 0) + " Hz | " +
                           juce::String(analysis.channels) + " channels\nInput I " +
                           lufs(analysis.inputLufs) + " LUFS   Rendered output I " +
                           lufs(analysis.outputLufs) + " LUFS   Gain " + lufs(analysis.gainDb) +
                           " dB\n" +
                           (analysis.targetReached ? "Target reached (+/-0.1 LU). "
                                                   : "Target not reached in this render. ") +
                           (analysis.peakLimited ? "Peak protection engaged."
                                                 : "Peak protection not engaged.") +
                           "\nDAW playback needs the same source, channel layout and sample rate.",
                       juce::dontSendNotification);
    else if (analysis.error.isNotEmpty())
        result.setText(analysis.error, juce::dontSendNotification);
    display.refresh(spectrumVisible);
    repaint();
}
void TrueLimiterEditor::paint(juce::Graphics &g) {
    g.fillAll(juce::Colour(0xff24282d));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(23.0F));
    g.drawText("AHQ True Limiter", 20, 12, 450, 32, juce::Justification::left);
    {
        juce::Graphics::ScopedSaveState saved(g);
        g.addTransform(juce::AffineTransform::translation(20, 402));
        display.paintMeters(g);
    }
    {
        juce::Graphics::ScopedSaveState saved(g);
        g.addTransform(juce::AffineTransform::translation(20, 372));
        display.paintSpectrum(g);
    }
}
void TrueLimiterEditor::resized() {
    undo.setBounds(510, 16, 85, 28);
    redo.setBounds(610, 16, 85, 28);
    live.setBounds(20, 55, 220, 28);
    quality.setBounds(290, 55, 140, 28);
    for (size_t i = 0; i < sliders.size(); ++i) {
        const int x = 20 + static_cast<int>(i % 3) * 232, y = 100 + static_cast<int>(i / 3) * 150;
        labels[i].setBounds(x, y, 220, 22);
        sliders[i].setBounds(x, y + 22, 220, 120);
    }
    meters.setBounds(20, 398, 680, 50);
    notice.setBounds(20, 451, 680, 65);
    analyze.setBounds(20, 530, 220, 28);
    cancel.setBounds(260, 530, 100, 28);
    apply.setBounds(380, 530, 170, 28);
    result.setBounds(20, 565, 680, 95);
}
void TrueLimiterEditor::chooseAudio() {
    if (chooser)
        return;
    chooser = std::make_unique<juce::FileChooser>("Analyze full audio file", juce::File{},
                                                  "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    juce::Component::SafePointer<TrueLimiterEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectFiles,
                         [safe](const juce::FileChooser &choice) {
                             if (!safe)
                                 return;
                             const auto file = choice.getResult();
                             if (file != juce::File{})
                                 safe->processor.startFileAnalysis(file);
                             juce::MessageManager::callAsync([safe] {
                                 if (safe)
                                     safe->chooser.reset();
                             });
                         });
}
