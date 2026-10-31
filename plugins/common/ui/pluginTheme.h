#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

namespace ahq::ui {
/// Shared native styling; each product has an explicit identity, not a random colour.
class PluginTheme final : public juce::LookAndFeel_V4 {
  public:
    PluginTheme(juce::Component &component, const juce::String &product) : owner(component) {
        struct Identity {
            const char *name, *label;
            juce::uint32 colour;
        };
        static constexpr Identity identities[]{{"Quick Filter", "FILTER", 0xff86cf93},
                                               {"Transient", "TRANSIENT", 0xffe6bd70},
                                               {"Saturator", "SATURATION", 0xffe59b73},
                                               {"Orbit", "ORBIT / PAN", 0xff81cbb8},
                                               {"Ghost Delay", "GRAIN DELAY", 0xffb5afe4},
                                               {"Space Lab", "STEREO", 0xff8abde5},
                                               {"Master Scope", "METERING", 0xffa9cc7b},
                                               {"Chaos", "BUFFER FX", 0xffe58f99},
                                               {"True Limiter", "LIMITER", 0xffe0c685},
                                               {"Volume Envelope", "DUCKING", 0xff9fb5ec},
                                               {"Curve EQ", "EQUALIZER", 0xff81d4c0},
                                               {"Noise Gate", "GATE", 0xffb8c886},
                                               {"Tremolo", "VOLUME MOTION", 0xffc6a5e0},
                                               {"Opto Leveler", "OPTO", 0xffdfc392},
                                               {"FET Compressor", "FET", 0xffd6a288},
                                               {"Cabinet", "CABINET / IR", 0xffdfae75},
                                               {"Hybrid Reverb", "HYBRID REVERB", 0xff82bbd1},
                                               {"Gated Reverb", "GATED REVERB", 0xffd999ba},
                                               {"Retro Tools", "80s EFFECTS", 0xffc5a1df},
                                               {"Elevation", "ELEVATION", 0xff93c6cb},
                                               {"Modular", "MODULAR SYNTH", 0xffe6ad75},
                                               {"PCM Station", "PCM WORKSTATION", 0xff91c4a9}};
        for (const auto &identity : identities)
            if (product == juce::String("AHQ ") + identity.name) {
                accent = juce::Colour(identity.colour);
                category = identity.label;
                break;
            }
        background = juce::Colour(0xff16191d).interpolatedWith(accent, .055F);
        setColour(juce::Slider::rotarySliderFillColourId, accent);
        setColour(juce::Slider::trackColourId, accent);
        setColour(juce::Slider::thumbColourId, accent);
        setColour(juce::Slider::textBoxBackgroundColourId, background.brighter(.08F));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff59616a));
        setColour(juce::ComboBox::backgroundColourId, background.brighter(.08F));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff59616a));
        setColour(juce::TextButton::buttonColourId, background.brighter(.14F));
        setColour(juce::TextButton::buttonOnColourId, accent.darker(.5F));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accent.darker(.55F));
        setColour(juce::ToggleButton::tickColourId, accent);
        setColour(juce::Label::textColourId, juce::Colour(0xffe6e9ec));
        owner.setLookAndFeel(this);
    }
    void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height, float position,
                          float start, float end, juce::Slider &slider) override {
        auto area =
            juce::Rectangle<float>(float(x), float(y), float(width), float(height)).reduced(2);
        const float radius = juce::jmax(
            2.F, juce::jmin(130.F, juce::jmin(area.getWidth(), area.getHeight())) * .5F - 3);
        const auto centre = area.getCentre();
        const float angle = start + position * (end - start);
        const auto ink = slider.isEnabled() ? accent : accent.withMultipliedAlpha(.3F);
        juce::Path track, active;
        track.addCentredArc(centre.x, centre.y, radius, radius, 0, start, end, true);
        active.addCentredArc(centre.x, centre.y, radius, radius, 0, start, angle, true);
        g.setColour(juce::Colour(0xff0d1317));
        g.strokePath(track, juce::PathStrokeType(4));
        g.setColour(ink);
        g.strokePath(active, juce::PathStrokeType(3));
        for (int i = 0; i <= 24; ++i) {
            const float a = start + float(i) / 24 * (end - start);
            auto outer = centre.getPointOnCircumference(radius + 5, a);
            auto inner = centre.getPointOnCircumference(radius + 2, a);
            g.setColour(juce::Colour(0xff647079).withAlpha(i / 24.F <= position ? .8F : .35F));
            g.drawLine({inner, outer}, 1);
        }
        const auto knob = juce::Rectangle<float>(radius * 1.65F, radius * 1.65F).withCentre(centre);
        g.setColour(juce::Colours::black.withAlpha(.3F));
        g.fillEllipse(knob.translated(0, 3));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a555c), knob.getTopLeft(),
                                               juce::Colour(0xff252e34), knob.getBottomRight(),
                                               false));
        g.fillEllipse(knob);
        g.setColour(juce::Colour(0xff10171b));
        g.drawEllipse(knob, 1.5F);
        g.setColour(slider.isEnabled() ? juce::Colour(0xffe2eaed) : juce::Colour(0xff687279));
        g.drawLine({centre.getPointOnCircumference(radius * .47F, angle),
                    centre.getPointOnCircumference(radius * .70F, angle)},
                   2.5F);
    }
    void drawLinearSlider(juce::Graphics &g, int x, int y, int width, int height, float position,
                          float minimum, float maximum, juce::Slider::SliderStyle style,
                          juce::Slider &slider) override {
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical) {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, position, minimum,
                                                   maximum, style, slider);
            return;
        }
        const bool vertical = style == juce::Slider::LinearVertical;
        const float cx = float(x) + width * .5F, cy = float(y) + height * .5F;
        auto track = vertical ? juce::Rectangle<float>(cx - 3, float(y), 6, float(height))
                              : juce::Rectangle<float>(float(x), cy - 3, float(width), 6);
        g.setColour(juce::Colour(0xff0d1317));
        g.fillRoundedRectangle(track, 3);
        g.setColour(slider.isEnabled() ? accent : accent.withAlpha(.25F));
        auto fill = vertical ? track.withY(position).withBottom(track.getBottom())
                             : track.withWidth(juce::jmax(0.F, position - x));
        g.fillRoundedRectangle(fill.reduced(2, 0), 2);
        auto cap = vertical ? juce::Rectangle<float>(cx - 12, position - 8, 24, 16)
                            : juce::Rectangle<float>(position - 7, cy - 9, 14, 18);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff65717a), cap.getTopLeft(),
                                               juce::Colour(0xff303b43), cap.getBottomLeft(),
                                               false));
        g.fillRoundedRectangle(cap, 3);
        g.setColour(juce::Colour(0xff11181d));
        g.drawRoundedRectangle(cap, 3, 1);
        g.setColour(juce::Colour(0xffcbd7dd));
        if (vertical)
            g.drawHorizontalLine(int(position), cap.getX() + 4, cap.getRight() - 4);
        else
            g.drawVerticalLine(int(position), cap.getY() + 4, cap.getBottom() - 4);
    }
    void paintGroup(juce::Graphics &g, juce::Rectangle<float> box,
                    const juce::String &title) const {
        if (box.isEmpty())
            return;
        g.setColour(background.brighter(.055F));
        g.fillRoundedRectangle(box, 9);
        g.setColour(juce::Colour(0xff47535b));
        g.drawRoundedRectangle(box.reduced(.5F), 9, 1);
        if (title.isNotEmpty()) {
            g.setFont(juce::FontOptions(12).withStyle("Bold"));
            g.setColour(juce::Colour(0xffbdcbd1));
            g.drawText(title, box.toNearestInt().reduced(12, 4).withHeight(20),
                       juce::Justification::centredLeft);
        }
    }
    void paintFlow(juce::Graphics &g, juce::Rectangle<float> box,
                   const juce::StringArray &stages) const {
        if (stages.isEmpty())
            return;
        const float cell = box.getWidth() / float(stages.size());
        g.setFont(juce::FontOptions(12));
        for (int i = 0; i < stages.size(); ++i) {
            auto label = box.withX(box.getX() + i * cell).withWidth(cell - 24);
            g.setColour(juce::Colour(0xffbdcbd1));
            g.drawText(stages[i], label, juce::Justification::centred);
            if (i + 1 < stages.size()) {
                g.setColour(accent.withAlpha(.7F));
                const float y = box.getCentreY(), x = label.getRight();
                g.drawArrow({x, y, x + 20, y}, 1.2F, 5, 5);
            }
        }
    }
    ~PluginTheme() override { owner.setLookAndFeel(nullptr); }
    void paintBackground(juce::Graphics &g, int width) const {
        g.fillAll(background);
        g.setColour(accent);
        g.fillRect(0, 0, width, 3);
    }
    juce::Colour accent{0xffa9c4cd}, background;
    juce::String category{"AUDIO"};

  private:
    juce::Component &owner;
};
} // namespace ahq::ui
