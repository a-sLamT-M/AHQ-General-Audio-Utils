#include "masterScopeEditor.h"
#include <cmath>
namespace {
juce::String loudnessText(ahq::audio::LoudnessReading r) {
    if (!r.valid)
        return "Window not filled";
    return std::isfinite(r.lufs) ? juce::String(r.lufs, 1) + " LUFS" : "No signal";
}
juce::String db(double amplitude) {
    return amplitude > 0 ? juce::String(20 * std::log10(amplitude), 1) : "-inf";
}
} // namespace
MasterScopeEditor::MasterScopeEditor(MasterScopeProcessor &p)
    : AudioProcessorEditor(p), ComponentMovementWatcher(this), processor(p),
      display(p.getAnalysis()) {
    for (auto *c :
         std::array<juce::Component *, 6>{&capture, &hold, &reset, &undo, &redo, &coordinates})
        addAndMakeVisible(c);
    coordinates.addItem("L/R phase", 1);
    coordinates.addItem("M/S phase", 2);
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
    hold.setTooltip(
        "Off: peaks for each 100 ms interval. On: maximum since reset. 4x true-peak estimate.");
    setSize(1000, 800);
    // Parameter changes drive choices; timer polling can overwrite a pending menu selection.
    coordinatesAttachment = std::make_unique<juce::ParameterAttachment>(
        *processor.getParameters().getParameter("scope"), [this](float value) {
            coordinates.setSelectedId(juce::roundToInt(value) + 1, juce::dontSendNotification);
        });
    coordinatesAttachment->sendInitialUpdate();
    timerCallback();
    visibilityChanged();
}
MasterScopeEditor::~MasterScopeEditor() {
    stopTimer();
    processor.setDisplayEnabled(false);
}
void MasterScopeEditor::visibilityChanged() {
    const bool visible = isShowing();
    processor.setDisplayEnabled(visible);
    if (visible)
        startTimerHz(30);
    else
        stopTimer();
}
void MasterScopeEditor::resized() {
    capture.setBounds(20, 55, 110, 30);
    hold.setBounds(140, 55, 110, 30);
    reset.setBounds(270, 55, 80, 30);
    undo.setBounds(360, 55, 80, 30);
    redo.setBounds(450, 55, 80, 30);
    coordinates.setBounds(710, 55, 250, 30);
}
void MasterScopeEditor::timerCallback() {
    const auto v = processor.readValues();
    capture.setToggleState(v[0] != 0, juce::dontSendNotification);
    hold.setToggleState(v[1] != 0, juce::dontSendNotification);
    metering = processor.readMetering();
    if (metering.generation != displayedGeneration) {
        phase.clear();
        displayedGeneration = metering.generation;
    }
    integrated = processor.readIntegrated();
    display.refresh(isShowing());
    auto count = processor.readPhase(samples);
    if (count) {
        phase.clear();
        const bool ms = v[2] != 0;
        for (size_t i = 0; i < count; ++i) {
            auto s = samples[i];
            const float x = ms ? (s.left - s.right) * .5F : s.left;
            const float y = ms ? (s.left + s.right) * .5F : s.right;
            const float px = 825 + std::clamp(x, -1.F, 1.F) * 130,
                        py = 460 - std::clamp(y, -1.F, 1.F) * 130;
            if (i == 0)
                phase.startNewSubPath(px, py);
            else
                phase.lineTo(px, py);
        }
    }
    if (metering.resetPending)
        phase.clear();
    repaint();
}
void MasterScopeEditor::paint(juce::Graphics &g) {
    g.fillAll(juce::Colour(0xff181c22));
    g.setColour(juce::Colours::white);
    g.setFont(24.F);
    g.drawText("AHQ Master Scope", 20, 12, 500, 35, juce::Justification::left);
    g.setFont(14.F);
    g.drawText("Audio passes unchanged", 710, 15, 260, 30, juce::Justification::right);
    auto text = [&](const juce::String &s, int x, int y, int w = 450) {
        g.drawText(s, x, y, w, 25, juce::Justification::left);
    };
    text("Momentary (400 ms): " + loudnessText(metering.momentary), 20, 105);
    text("Short-term (3 s): " + loudnessText(metering.shortTerm), 20, 137);
    juce::String integratedText = loudnessText(integrated.loudness);
    juce::String rangeText = "waiting for short-term windows";
    if (integrated.gap) {
        integratedText = "INVALID - capture gap";
        rangeText = integratedText;
    } else if (integrated.allocationFailed) {
        integratedText = "INVALID - memory exhausted";
        rangeText = integratedText;
    } else {
        if (integrated.blocks > 0 && integrated.generation == metering.generation &&
            !metering.resetPending && !integrated.loudness.valid)
            integratedText = "No signal / below gate";
        if (integrated.rangeValid)
            rangeText = juce::String(integrated.loudnessRange, 1) + " LU";
        else if (metering.shortTerm.valid && !std::isfinite(metering.shortTerm.lufs))
            rangeText = "no signal";
    }
    text("Integrated gated: " + integratedText, 20, 169);
    text("Integrated blocks: " + juce::String(juce::int64(integrated.blocks)), 20, 201);
    text("Loudness range: " + rangeText, 20, 297);
    const auto &s = metering.stereo;
    text("400 ms nonoverlapping windows", 520, 105);
    if (s.valid) {
        const auto rightRms = s.stereo ? db(s.rms[1]) : juce::String("n/a");
        const auto leftCrest = s.rms[0] > 0 ? juce::String(s.crestDb[0], 2) : juce::String("n/a");
        const auto rightCrest =
            s.stereo && s.rms[1] > 0 ? juce::String(s.crestDb[1], 2) : juce::String("n/a");
        text("RMS L / R: " + db(s.rms[0]) + " / " + rightRms + " dBFS", 520, 137);
        text("Crest L / R: " + leftCrest + " / " + rightCrest + " dB", 520, 169);
    } else {
        text("RMS: Window not filled", 520, 137);
        text("Crest: Window not filled", 520, 169);
    }
    text("Crest = 20 log10(window peak / window RMS)", 520, 201);
    text(s.correlationValid ? "Correlation: " + juce::String(s.correlation, 3)
                            : "Correlation: n/a / no signal",
         520, 233);
    const double total = s.midEnergy + s.sideEnergy;
    text(s.valid && s.stereo && total > 0
             ? "M/S energy share: " + juce::String(100 * s.midEnergy / total, 1) + "% / " +
                   juce::String(100 * s.sideEnergy / total, 1) + "%"
             : "M/S energy share: n/a / no signal",
         520, 265);
    if (metering.peaksValid) {
        const auto rightPeak = s.stereo ? db(metering.peaks[1].samplePeak) : juce::String("n/a");
        const auto rightTruePeak = s.stereo ? db(metering.peaks[1].truePeak) : juce::String("n/a");
        text("Sample peak L / R: " + db(metering.peaks[0].samplePeak) + " / " + rightPeak + " dBFS",
             20, 233);
        text("4x TP estimate L / R: " + db(metering.peaks[0].truePeak) + " / " + rightTruePeak +
                 " dBTP",
             20, 265);
    } else {
        text("Sample peak: Window not filled", 20, 233);
        text("4x TP estimate: Window not filled", 20, 265);
    }
    {
        juce::Graphics::ScopedSaveState save(g);
        g.addTransform(juce::AffineTransform::translation(0, -45));
        display.paintSpectrum(g);
    }
    g.setColour(juce::Colour(0xff353e4a));
    g.drawRect(695, 330, 260, 260);
    g.drawLine(695, 460, 955, 460);
    g.drawLine(825, 330, 825, 590);
    const bool ms = processor.readValues()[2] != 0;
    g.setColour(juce::Colours::white);
    text(ms ? "M/S phase (horizontal S, vertical M)" : "L/R phase (horizontal L, vertical R)", 675,
         305, 320);
    if (ms && !s.stereo)
        text("M/S unavailable for mono", 710, 440, 240);
    else {
        g.setColour(juce::Colour(0xff64d7bc));
        g.strokePath(phase, juce::PathStrokeType(1.F));
    }
    g.setColour(juce::Colours::white);
    text("Scope scale: +/-1 sample amplitude", 695, 600, 280);
    text("Spectrum is a windowed display; TP is a finite-filter estimate.", 20, 690, 950);
    if (!metering.loudnessSupported)
        text("LUFS requires sample rate >= 32 kHz", 20, 655, 950);
    text("Capture continues with the editor closed. Reset starts a new measurement.", 20, 725, 950);
    if (metering.resetPending)
        text("Reset pending: waiting for cleared measurement history", 20, 755, 950);
    else if (processor.readValues()[0] == 0)
        text("Capture paused - readings held", 20, 755, 950);
}
