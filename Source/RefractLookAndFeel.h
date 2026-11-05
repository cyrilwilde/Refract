#pragma once
//
// RefractLookAndFeel.h
// All the colours and the custom knob drawing in one place. Change the
// palette here to re-skin the whole plugin.
//
#include <juce_gui_basics/juce_gui_basics.h>

namespace refract::ui
{
    namespace colours
    {
        const juce::Colour background { 0xff0e0f16 };
        const juce::Colour panel      { 0xff171925 };
        const juce::Colour panelEdge  { 0xff262a3b };
        const juce::Colour text       { 0xffd9dbe6 };
        const juce::Colour dimText    { 0xff8a8fa8 };
        const juce::Colour accent     { 0xff59d8e6 };   // cyan - values
        const juce::Colour accent2    { 0xffe0569b };   // magenta - modulation
        const juce::Colour track      { 0xff2b2f42 };
    }

    class RefractLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        RefractLookAndFeel()
        {
            using namespace colours;
            setColour (juce::ResizableWindow::backgroundColourId, background);
            setColour (juce::Label::textColourId, text);
            setColour (juce::Slider::textBoxTextColourId, text);
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::rotarySliderFillColourId, accent);
            setColour (juce::Slider::rotarySliderOutlineColourId, track);
            setColour (juce::Slider::trackColourId, accent);
            setColour (juce::Slider::backgroundColourId, track);
            setColour (juce::Slider::thumbColourId, text);
            setColour (juce::ComboBox::backgroundColourId, track);
            setColour (juce::ComboBox::outlineColourId, panelEdge);
            setColour (juce::ComboBox::textColourId, text);
            setColour (juce::ComboBox::arrowColourId, dimText);
            setColour (juce::PopupMenu::backgroundColourId, panel);
            setColour (juce::PopupMenu::textColourId, text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
            setColour (juce::TextButton::buttonColourId, track);
            setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.35f));
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextButton::textColourOnId, juce::Colours::white);
            setColour (juce::ToggleButton::textColourId, text);
            setColour (juce::ToggleButton::tickColourId, accent);
            setColour (juce::ToggleButton::tickDisabledColourId, dimText);
        }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                               float startAngle, float endAngle, juce::Slider& slider) override
        {
            const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
            const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto centre = bounds.getCentre();
            const float lineW = juce::jmax (2.5f, radius * 0.16f);
            const float arcR = radius - lineW * 0.5f;

            // Background track
            juce::Path track;
            track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
            g.setColour (colours::track);
            g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Value arc - bipolar knobs (pan, fine, mod amount) grow out from the centre
            const bool bipolar = slider.getProperties().getWithDefault ("bipolar", false);
            const float valueAngle = startAngle + pos * (endAngle - startAngle);
            const float fromAngle  = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                                 juce::jmin (fromAngle, valueAngle), juce::jmax (fromAngle, valueAngle), true);
            g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
            g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Pointer
            const float inner = radius * 0.55f;
            const juce::Point<float> tip (centre.x + inner * std::sin (valueAngle), centre.y - inner * std::cos (valueAngle));
            g.setColour (colours::text);
            g.drawLine ({ centre, tip }, lineW * 0.6f);
        }

        juce::Font getLabelFont (juce::Label&) override { return juce::Font (juce::FontOptions (12.0f)); }
    };
}
