#include "masterScopeEditor.h"
#include <cmath>
#include <limits>
namespace {
constexpr auto background = 0xff181c22;
constexpr auto foreground = 0xffe7edf5;
constexpr auto muted = 0xffb7c6d5;
juce::String loudnessText(ahq::audio::LoudnessReading r) {
    return r.valid && std::isfinite(r.lufs) ? juce::String(r.lufs, 1) : "--";
}
juce::String db(double amplitude) {
    return amplitude > 0 ? juce::String(20 * std::log10(amplitude), 1) : "-inf";
}
juce::Rectangle<float> plotArea(juce::Graphics &g, juce::Rectangle<float> box,
                                const juce::String &title) {
    g.setColour(juce::Colour(0xff11151b));
    g.fillRect(box);
    g.setColour(juce::Colour(muted));
    g.setFont(13.F);
    g.drawText(title, box.toNearestInt().reduced(12, 6).withHeight(20), juce::Justification::left);
    return {box.getX() + 42, box.getY() + 34, box.getWidth() - 62, box.getHeight() - 62};
}
} // namespace
MasterScopeEditor::MasterScopeEditor(MasterScopeProcessor &p)
    : AudioProcessorEditor(p), ComponentMovementWatcher(this), processor(p),
      display(p.getAnalysis()) {
    addAndMakeVisible(presetBar);
    for (auto *c : std::array<juce::Component *, 14>{
             &capture, &hold, &reset, &undo, &redo, &coordinates, &style, &range, &theme,
             &spectrumGain, &scopeGain, &spectrumGainLabel, &scopeGainLabel, &details})
        addAndMakeVisible(c);
    coordinates.addItemList({"L/R phase", "M/S phase"}, 1);
    style.addItemList({"Lines", "Filled / dots", "Bars / dots"}, 1);
    range.addItemList({"60 dB range", "90 dB range", "120 dB range"}, 1);
    theme.addItemList({"Ice", "Amber", "Mint"}, 1);
    style.setSelectedId(p.displayStyle.load() + 1, juce::dontSendNotification);
    range.setSelectedId(p.spectrumRange.load() / 30 - 1, juce::dontSendNotification);
    theme.setSelectedId(p.displayTheme.load() + 1, juce::dontSendNotification);
    style.onChange = [this] {
        processor.displayStyle.store(style.getSelectedId() - 1);
        repaint();
    };
    range.onChange = [this] {
        processor.spectrumRange.store((range.getSelectedId() + 1) * 30);
        repaint();
    };
    theme.onChange = [this] {
        processor.displayTheme.store(theme.getSelectedId() - 1);
        repaint();
    };
    for (auto *slider : {&spectrumGain, &scopeGain}) {
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 68, 24);
        slider->setRange(-24, 36, .5);
        slider->setTextValueSuffix(" dB");
        slider->setDoubleClickReturnValue(true, 0);
        slider->setTooltip("Display gain only. Double-click to reset. Audio is unchanged.");
    }
    spectrumGain.setValue(p.spectrumGainDb.load(), juce::dontSendNotification);
    scopeGain.setValue(p.scopeGainDb.load(), juce::dontSendNotification);
    spectrumGain.onValueChange = [this] {
        processor.spectrumGainDb.store(float(spectrumGain.getValue()));
        repaint();
    };
    scopeGain.onValueChange = [this] {
        processor.scopeGainDb.store(float(scopeGain.getValue()));
        repaint();
    };
    spectrumGainLabel.setText("Spectrum gain", juce::dontSendNotification);
    scopeGainLabel.setText("Phase / wave gain", juce::dontSendNotification);
    capture.onClick = [this] {
        auto v = processor.readValues();
        v[0] = capture.getToggleState() ? 1.F : 0.F;
        processor.applyUserValues(v, "Capture");
    };
    hold.onClick = [this] {
        auto v = processor.readValues();
        v[1] = hold.getToggleState() ? 1.F : 0.F;
        processor.applyUserValues(v, "Peak hold");
    };
    coordinates.onChange = [this] {
        auto v = processor.readValues();
        v[2] = float(coordinates.getSelectedId() - 1);
        processor.applyUserValues(v, "Scope coordinates");
    };
    reset.onClick = [this] { processor.requestReset(); };
    undo.onClick = [this] { processor.undo(); };
    redo.onClick = [this] { processor.redo(); };
    details.onClick = [this] {
        juce::PopupMenu menu;
        menu.addSectionHeader("Measurement details");
        const auto &s = metering.stereo;
        menu.addItem(1,
                     "Sample peak L / R: " + db(metering.peaks[0].samplePeak) + " / " +
                         (s.stereo ? db(metering.peaks[1].samplePeak) : "--") + " dBFS",
                     false);
        menu.addItem(2,
                     "True peak L / R: " + db(metering.peaks[0].truePeak) + " / " +
                         (s.stereo ? db(metering.peaks[1].truePeak) : "--") + " dBTP (4x estimate)",
                     false);
        menu.addItem(
            3,
            "Crest L / R: " + (s.valid && s.rms[0] > 0 ? juce::String(s.crestDb[0], 2) : "--") +
                " / " +
                (s.valid && s.stereo && s.rms[1] > 0 ? juce::String(s.crestDb[1], 2) : "--") +
                " dB",
            false);
        const double total = s.midEnergy + s.sideEnergy;
        menu.addItem(4,
                     total > 0 && s.valid && s.stereo
                         ? "M/S energy: " + juce::String(100 * s.midEnergy / total, 1) + "% / " +
                               juce::String(100 * s.sideEnergy / total, 1) + "%"
                         : "M/S energy: --",
                     false);
        menu.addItem(5, "Integrated blocks: " + juce::String(juce::int64(integrated.blocks)),
                     false);
        menu.addItem(6, "RMS and crest use 400 ms windows.", false);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&details));
    };
    hold.setTooltip(
        "Off: peaks per 100 ms interval. On: maximum since reset. True peak is a 4x estimate.");
    bindParameterControl(capture, *p.getParameters().getParameter("run"));
    bindParameterControl(hold, *p.getParameters().getParameter("hold"));
    bindParameterControl(coordinates, *p.getParameters().getParameter("scope"));
    coordinatesAttachment = std::make_unique<juce::ParameterAttachment>(
        *p.getParameters().getParameter("scope"), [this](float value) {
            coordinates.setSelectedId(juce::roundToInt(value) + 1, juce::dontSendNotification);
        });
    coordinatesAttachment->sendInitialUpdate();
    const int initialWidth = p.editorWidth.load(), initialHeight = p.editorHeight.load();
    setResizable(true, true);
    setResizeLimits(980, 746, 2000, 1466);
    setSize(initialWidth, initialHeight + ahq::ui::PresetBar::height);
    timerCallback();
    visibilityChanged();
}
MasterScopeEditor::~MasterScopeEditor() {
    stopTimer();
    processor.setDisplayEnabled(false);
}
void MasterScopeEditor::visibilityChanged() {
    processor.setDisplayEnabled(isShowing());
    if (isShowing())
        startTimerHz(30);
    else
        stopTimer();
    lastHistoryTime = 0;
}
void MasterScopeEditor::resized() {
    presetBar.setBounds(0, getHeight() - ahq::ui::PresetBar::height, getWidth(),
                        ahq::ui::PresetBar::height);
    details.setBounds(getWidth() - 108, 12, 88, 28);
    processor.editorWidth.store(getWidth());
    processor.editorHeight.store((getHeight() - ahq::ui::PresetBar::height));
    auto row = juce::Rectangle<int>(20, 50, getWidth() - 40, 28);
    for (auto *c : std::array<juce::Component *, 5>{&capture, &hold, &reset, &undo, &redo})
        c->setBounds(row.removeFromLeft(90).reduced(0, 1).withTrimmedRight(8));
    coordinates.setBounds(row.removeFromLeft(140).withTrimmedRight(8));
    style.setBounds(row.removeFromLeft(145).withTrimmedRight(8));
    range.setBounds(row.removeFromLeft(125).withTrimmedRight(8));
    theme.setBounds(row.withWidth(80));
    const int half = (getWidth() - 52) / 2;
    spectrumGainLabel.setBounds(20, 88, 110, 26);
    spectrumGain.setBounds(130, 88, half - 110, 26);
    scopeGainLabel.setBounds(32 + half, 88, 130, 26);
    scopeGain.setBounds(162 + half, 88, half - 130, 26);
    auto graphs = juce::Rectangle<float>(20, 208, float(getWidth() - 40),
                                         float((getHeight() - ahq::ui::PresetBar::height) - 244));
    const float leftWidth = std::floor(graphs.getWidth() * .66F);
    const float upperHeight = std::floor(graphs.getHeight() * .60F);
    spectrumBounds = {graphs.getX(), graphs.getY(), leftWidth - 6, upperHeight - 6};
    phaseBounds = {graphs.getX() + leftWidth + 6, graphs.getY(), graphs.getWidth() - leftWidth - 6,
                   upperHeight - 6};
    waveformBounds = {graphs.getX(), graphs.getY() + upperHeight + 6, leftWidth - 6,
                      graphs.getHeight() - upperHeight - 6};
    historyBounds = {phaseBounds.getX(), waveformBounds.getY(), phaseBounds.getWidth(),
                     waveformBounds.getHeight()};
}
void MasterScopeEditor::timerCallback() {
    const auto revision = processor.displayRevision.load();
    if (revision != displayRevision) {
        displayRevision = revision;
        spectrumGain.setValue(processor.spectrumGainDb.load(), juce::dontSendNotification);
        scopeGain.setValue(processor.scopeGainDb.load(), juce::dontSendNotification);
        style.setSelectedId(processor.displayStyle.load() + 1, juce::dontSendNotification);
        range.setSelectedId(processor.spectrumRange.load() / 30 - 1, juce::dontSendNotification);
        theme.setSelectedId(processor.displayTheme.load() + 1, juce::dontSendNotification);
        setSize(processor.editorWidth.load(),
                processor.editorHeight.load() + ahq::ui::PresetBar::height);
    }
    const auto v = processor.readValues();
    capture.setToggleState(v[0] != 0, juce::dontSendNotification);
    hold.setToggleState(v[1] != 0, juce::dontSendNotification);
    metering = processor.readMetering();
    if (metering.generation != displayedGeneration || metering.resetPending) {
        sampleCount = historyCount = historyPosition = 0;
        displayedGeneration = metering.generation;
    }
    integrated = processor.readIntegrated();
    display.refresh(isShowing());
    const auto count = processor.readPhase(samples);
    if (count != 0)
        sampleCount = count;
    if (metering.resetPending)
        sampleCount = 0;
    // Timer jitter does not turn a delayed GUI callback into a burst of history work.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (v[0] != 0 && !metering.resetPending && now - lastHistoryTime >= 30) {
        const auto reading = [](auto r) {
            return r.valid && std::isfinite(r.lufs) ? float(r.lufs)
                                                    : std::numeric_limits<float>::quiet_NaN();
        };
        momentaryHistory[historyPosition] = reading(metering.momentary);
        shortHistory[historyPosition] = reading(metering.shortTerm);
        historyTimes[historyPosition] = now;
        historyPosition = (historyPosition + 1) % historyCapacity;
        historyCount = std::min(historyCount + 1, historyCapacity);
        lastHistoryTime = now;
    }
    repaint();
}
void MasterScopeEditor::paintPhase(juce::Graphics &g, juce::Rectangle<float> box,
                                   juce::Colour colour) {
    auto area =
        plotArea(g, box, coordinates.getSelectedId() == 2 ? "PHASE   S / M" : "PHASE   L / R");
    const auto size = std::min(area.getWidth(), area.getHeight());
    area = area.withSizeKeepingCentre(size, size);
    g.setColour(juce::Colour(0xff353e4a));
    g.drawRect(area);
    g.drawLine(area.getX(), area.getCentreY(), area.getRight(), area.getCentreY());
    g.drawLine(area.getCentreX(), area.getY(), area.getCentreX(), area.getBottom());
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(area.toNearestInt());
    const float gain = juce::Decibels::decibelsToGain(processor.scopeGainDb.load());
    const bool ms = processor.readValues()[2] != 0;
    if (ms && !metering.stereo.stereo)
        return;
    juce::Path path;
    path.preallocateSpace(int(sampleCount) * 3);
    bool first = true;
    g.setColour(colour.withAlpha(.75F));
    for (size_t i = 0; i < sampleCount; ++i) {
        const auto s = samples[i];
        if (s.generation != displayedGeneration)
            continue;
        const auto x = (ms ? (s.left - s.right) * .5F : s.left) * gain;
        const auto y = (ms ? (s.left + s.right) * .5F : s.right) * gain;
        const auto px = area.getCentreX() + x * size * .5F;
        const auto py = area.getCentreY() - y * size * .5F;
        if (processor.displayStyle.load() != 0)
            g.fillRect(px, py, 1.6F, 1.6F);
        else {
            if (first)
                path.startNewSubPath(px, py);
            else
                path.lineTo(px, py);
            first = false;
        }
    }
    if (processor.displayStyle.load() == 0)
        g.strokePath(path, juce::PathStrokeType(1.F));
}
void MasterScopeEditor::paintWaveform(juce::Graphics &g, juce::Rectangle<float> box,
                                      juce::Colour colour) {
    auto area = plotArea(g, box, "WAVEFORM   L / R   captured window");
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(area.toNearestInt());
    const float gain = juce::Decibels::decibelsToGain(processor.scopeGainDb.load());
    for (int channel = 0; channel < 2; ++channel) {
        const float centre = area.getY() + area.getHeight() * (channel == 0 ? .25F : .75F);
        g.setColour(juce::Colour(0xff353e4a));
        g.drawHorizontalLine(int(centre), area.getX(), area.getRight());
        juce::Path path;
        path.preallocateSpace(int(sampleCount) * 3);
        bool first = true;
        for (size_t i = 0; i < sampleCount; ++i) {
            if (samples[i].generation != displayedGeneration)
                continue;
            const float x = area.getX() + area.getWidth() * float(i) /
                                              float(std::max(size_t(1), sampleCount - 1));
            const float value = channel == 0 ? samples[i].left : samples[i].right;
            const float y = centre - std::clamp(value * gain, -1.F, 1.F) * area.getHeight() * .23F;
            if (first)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
            first = false;
        }
        g.setColour(channel == 0 ? colour : juce::Colour(0xffd8aaff));
        g.strokePath(path, juce::PathStrokeType(1.F));
    }
}
void MasterScopeEditor::paintHistory(juce::Graphics &g, juce::Rectangle<float> box,
                                     juce::Colour colour) {
    const auto area = plotArea(g, box, "LOUDNESS   M / S   last 60 s");
    for (int lu : {-48, -36, -24, -12, 0}) {
        const float y = area.getBottom() - (lu + 48) * area.getHeight() / 48;
        g.setColour(juce::Colour(0xff353e4a));
        g.drawHorizontalLine(int(y), area.getX(), area.getRight());
        g.setColour(juce::Colour(muted));
        g.drawText(juce::String(lu), int(area.getX()) - 34, int(y) - 8, 28, 16,
                   juce::Justification::right);
    }
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(area.toNearestInt());
    for (int curve = 0; curve < 2; ++curve) {
        juce::Path path;
        path.preallocateSpace(int(historyCount) * 3);
        bool first = true;
        for (size_t i = 0; i < historyCount; ++i) {
            const auto slot =
                (historyPosition + historyCapacity - historyCount + i) % historyCapacity;
            const float value = curve == 0 ? momentaryHistory[slot] : shortHistory[slot];
            const double endTime =
                historyTimes[(historyPosition + historyCapacity - 1) % historyCapacity];
            const double age = endTime - historyTimes[slot];
            if (!std::isfinite(value) || age > 60000) {
                first = true;
                continue;
            }
            if (i > 0 &&
                historyTimes[slot] - historyTimes[(slot + historyCapacity - 1) % historyCapacity] >
                    150)
                first = true;
            const float x = area.getRight() - area.getWidth() * float(age / 60000);
            const float y = area.getBottom() - (value + 48) * area.getHeight() / 48;
            if (first)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
            first = false;
        }
        g.setColour(curve == 0 ? colour : juce::Colour(0xffd8aaff));
        g.strokePath(path, juce::PathStrokeType(curve == 0 ? 1.F : 2.F));
    }
}
void MasterScopeEditor::paint(juce::Graphics &g) {
    appearance.paintBackground(g, getWidth());
    const std::array<juce::Colour, 3> colours{juce::Colour(0xff8acbff), juce::Colour(0xffffc273),
                                              juce::Colour(0xff64d7bc)};
    const auto colour = colours[size_t(processor.displayTheme.load())];
    g.setColour(juce::Colour(foreground));
    g.setFont(24.F);
    g.drawText("AHQ Master Scope", 20, 10, 420, 32, juce::Justification::left);
    g.setColour(juce::Colour(muted));
    g.setFont(13.F);
    g.drawText("Audio unchanged  /  display gain only", getWidth() - 450, 14, 320, 26,
               juce::Justification::right);
    juce::String integratedText = loudnessText(integrated.loudness);
    if (integrated.generation != metering.generation || metering.resetPending)
        integratedText = "--";
    if (integrated.gap || integrated.allocationFailed)
        integratedText = "Invalid";
    const double peak = std::max(metering.peaks[0].truePeak, metering.peaks[1].truePeak);
    const std::array<juce::String, 6> names{"M  LUFS", "S  LUFS",       "I  LUFS",
                                            "LRA  LU", "TP EST.  dBTP", "RMS L / R  dBFS"};
    const std::array<juce::String, 6> values{
        loudnessText(metering.momentary),
        loudnessText(metering.shortTerm),
        integratedText,
        integrated.rangeValid && !integrated.gap && !integrated.allocationFailed
            ? juce::String(integrated.loudnessRange, 1)
            : "--",
        metering.peaksValid ? db(peak) : "--",
        metering.stereo.valid ? db(metering.stereo.rms[0]) + " / " +
                                    (metering.stereo.stereo ? db(metering.stereo.rms[1]) : "--")
                              : "--"};
    const int cell = (getWidth() - 40) / 7;
    for (int i = 0; i < 6; ++i) {
        const int x = 20 + i * cell;
        const int w = i == 5 ? cell * 2 : cell;
        g.setColour(juce::Colour(muted));
        g.setFont(12.F);
        g.drawText(names[size_t(i)], x, 124, w, 18, juce::Justification::left);
        g.setColour(juce::Colour(foreground));
        g.setFont(25.F);
        g.drawText(values[size_t(i)], x, 145, w, 32, juce::Justification::left);
    }
    g.setFont(12.F);
    g.setColour(juce::Colour(muted));
    g.drawText("Correlation", 20, 181, 90, 18, juce::Justification::left);
    const auto bar = juce::Rectangle<float>(118, 186, float(getWidth() - 238), 7);
    g.setColour(juce::Colour(0xff353e4a));
    g.fillRect(bar);
    if (metering.stereo.correlationValid) {
        const float x = bar.getX() + bar.getWidth() * float(metering.stereo.correlation + 1) * .5F;
        g.setColour(metering.stereo.correlation < 0 ? juce::Colour(0xffffa188) : colour);
        g.fillRect(x - 2, bar.getY() - 3, 4.F, 13.F);
    }
    g.setColour(juce::Colour(muted));
    g.drawText(metering.stereo.correlationValid ? juce::String(metering.stereo.correlation, 2)
                                                : "--",
               getWidth() - 110, 181, 90, 18, juce::Justification::right);
    auto spectrumPlot = plotArea(g, spectrumBounds, "SPECTRUM   dBFS / Hz");
    display.paintSpectrum(g, spectrumPlot, processor.spectrumGainDb.load(),
                          float(processor.spectrumRange.load()), processor.displayStyle.load(),
                          colour);
    paintPhase(g, phaseBounds, colour);
    paintWaveform(g, waveformBounds, colour);
    paintHistory(g, historyBounds, colour);
    juce::String status =
        "Capture continues with editor closed. True peak is a 4x finite-filter estimate.";
    if (metering.resetPending)
        status = "Reset pending: waiting for measurement history.";
    else if (integrated.gap)
        status = "Capture gap: integrated loudness and LRA are invalid. Reset to measure again.";
    else if (integrated.allocationFailed)
        status = "Loudness history allocation failed. Reset to measure again.";
    else if (!metering.loudnessSupported)
        status = "LUFS requires a sample rate of at least 32 kHz.";
    else if (processor.readValues()[0] == 0)
        status = "Capture paused. Readings are held.";
    g.setColour(juce::Colour(muted));
    g.setFont(12.F);
    g.drawText(status, 20, (getHeight() - ahq::ui::PresetBar::height) - 28, getWidth() - 40, 20,
               juce::Justification::left);
}
