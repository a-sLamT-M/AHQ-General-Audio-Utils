#pragma once
#include "spectrumPreferences.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ahq::ui {
class SpectrumControls final : public juce::Component {
  public:
    explicit SpectrumControls(SpectrumPreferences &p) : preferences(p) {
        setName("Spectrum display controls");
        style.addItemList({"Line", "Filled", "Bars"}, 1);
        style.setSelectedId(p.style.load() + 1, juce::dontSendNotification);
        style.setTooltip("Spectrum drawing style. Does not change audio.");
        style.onChange = [this] {
            preferences.style.store(style.getSelectedId() - 1);
            changed();
        };
        addAndMakeVisible(style);
        frequency.addItemList({"Freq: raw", "Freq: 1/12 oct", "Freq: 1/6 oct", "Freq: 1/3 oct"}, 1);
        frequency.setSelectedId(p.frequencySmoothing.load() + 1, juce::dontSendNotification);
        frequency.setTooltip("Average power across neighbouring frequency bins. Display only.");
        frequency.onChange = [this] {
            preferences.frequencySmoothing.store(frequency.getSelectedId() - 1);
            changed();
        };
        addAndMakeVisible(frequency);
        configure(smoothing, 0, 600, 10, p.smoothingMs.load(), " ms", "Time smoothing");
        configure(share, 50, 75, 1, p.share.load(), " %", "Spectrum area");
        configure(gain, -24, 36, .5, p.gainDb.load(), " dB", "Display gain");
        share.setTooltip("Requested spectrum width. Minimum parameter-panel width takes priority "
                         "in smaller windows.");
        smoothing.onValueChange = [this] {
            preferences.smoothingMs.store(float(smoothing.getValue()));
            changed();
        };
        share.onValueChange = [this] {
            preferences.share.store(float(share.getValue()));
            changed();
        };
        gain.onValueChange = [this] {
            preferences.gainDb.store(float(gain.getValue()));
            changed();
        };
        for (auto *label : {&smoothLabel, &shareLabel, &gainLabel})
            addAndMakeVisible(label);
        smoothLabel.setText("Smooth", juce::dontSendNotification);
        shareLabel.setText("Area", juce::dontSendNotification);
        gainLabel.setText("View gain", juce::dontSendNotification);
    }
    void resized() override {
        auto row = getLocalBounds();
        style.setBounds(row.removeFromLeft(90).reduced(0, 3));
        frequency.setBounds(row.removeFromLeft(132).reduced(4, 3));
        const int width = row.getWidth() / (showArea ? 3 : 2);
        for (auto pair :
             {std::pair{&smoothLabel, &smoothing}, {&shareLabel, &share}, {&gainLabel, &gain}}) {
            if (!showArea && pair.second == &share)
                continue;
            auto cell = row.removeFromLeft(width).reduced(3, 0);
            pair.first->setBounds(cell.removeFromLeft(60));
            pair.second->setBounds(cell);
        }
    }
    void setAreaControlVisible(bool visible) {
        showArea = visible;
        share.setVisible(visible);
        shareLabel.setVisible(visible);
        resized();
    }
    void synchronise() {
        style.setSelectedId(preferences.style.load() + 1, juce::dontSendNotification);
        frequency.setSelectedId(preferences.frequencySmoothing.load() + 1,
                                juce::dontSendNotification);
        smoothing.setValue(preferences.smoothingMs.load(), juce::dontSendNotification);
        share.setValue(preferences.share.load(), juce::dontSendNotification);
        gain.setValue(preferences.gainDb.load(), juce::dontSendNotification);
    }
    std::function<void()> onChange;
    SpectrumPreferences &preferences;

  private:
    bool showArea = true;
    void changed() {
        if (onChange)
            onChange();
    }
    void configure(juce::Slider &s, double lo, double hi, double step, double value,
                   const juce::String &unit, const juce::String &name) {
        s.setName(name);
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 24);
        s.setRange(lo, hi, step);
        s.setValue(value, juce::dontSendNotification);
        s.setTextValueSuffix(unit);
        s.setTooltip(name + ". Visual only; audio is unchanged.");
        addAndMakeVisible(s);
    }
    juce::ComboBox style, frequency;
    juce::Slider smoothing, share, gain;
    juce::Label smoothLabel, shareLabel, gainLabel;
};
} // namespace ahq::ui
