#pragma once
//
// RefractLookAndFeel.h
// All the colours and the custom knob drawing in one place. Change the
// palette here to re-skin the whole plugin.
//
// Palette: black and greys, with one red for "where the value is" (#880808)
// and a pale red for "where modulation is taking it" (#CF9F9F).
//
#include <juce_gui_basics/juce_gui_basics.h>

namespace refract::ui
{
    namespace colours
    {
        const juce::Colour background { 0xff000000 };
        const juce::Colour panel      { 0xff111111 };
        const juce::Colour panelEdge  { 0xff2a2a2a };
        const juce::Colour text       { 0xffe6e6e6 };
        const juce::Colour dimText    { 0xff8a8a8a };
        const juce::Colour track      { 0xff2c2c2c };   // unfilled part of a dial
        const juce::Colour control    { 0xff1e1e1e };   // buttons, menus
        const juce::Colour controlOn  { 0xff5a5a5a };   // a button that is switched on
        const juce::Colour value      { 0xff880808 };   // dial value arc
        const juce::Colour modulation { 0xffcf9f9f };   // modulation arc
        const juce::Colour waveform   { 0xffbdbdbd };
        const juce::Colour playhead   { 0xffffffff };
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
            setColour (juce::Slider::rotarySliderFillColourId, value);
            setColour (juce::Slider::rotarySliderOutlineColourId, track);
            setColour (juce::Slider::trackColourId, value);
            setColour (juce::Slider::backgroundColourId, track);
            setColour (juce::Slider::thumbColourId, text);
            setColour (juce::ComboBox::backgroundColourId, control);
            setColour (juce::ComboBox::outlineColourId, panelEdge);
            setColour (juce::ComboBox::textColourId, text);
            setColour (juce::ComboBox::arrowColourId, dimText);
            setColour (juce::PopupMenu::backgroundColourId, panel);
            setColour (juce::PopupMenu::textColourId, text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, controlOn);
            setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
            setColour (juce::TextButton::buttonColourId, control);
            setColour (juce::TextButton::buttonOnColourId, controlOn);
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextButton::textColourOnId, juce::Colours::white);
            setColour (juce::ToggleButton::textColourId, text);
            setColour (juce::ToggleButton::tickColourId, text);
            setColour (juce::ToggleButton::tickDisabledColourId, dimText);
            setColour (juce::TextEditor::backgroundColourId, control);
            setColour (juce::TextEditor::textColourId, text);
            setColour (juce::TextEditor::highlightColourId, controlOn);
            setColour (juce::CaretComponent::caretColourId, text);
        }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                               float startAngle, float endAngle, juce::Slider& slider) override
        {
            const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
            const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto centre = bounds.getCentre();
            const float lineW = juce::jmax (2.5f, radius * 0.16f);
            const float arcR = radius - lineW * 0.5f;
            const auto stroke = juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

            auto arc = [&] (float from, float to, float r)
            {
                juce::Path p;
                p.addCentredArc (centre.x, centre.y, r, r, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
                return p;
            };
            auto angleAt = [&] (float proportion) { return startAngle + juce::jlimit (0.0f, 1.0f, proportion) * (endAngle - startAngle); };

            // Background track
            g.setColour (colours::track);
            g.strokePath (arc (startAngle, endAngle, arcR), stroke);

            // Value arc - bipolar knobs (pan, fine, mod amount) grow out from the centre
            const bool bipolar = slider.getProperties().getWithDefault ("bipolar", false);
            const float valueAngle = angleAt (pos);
            g.setColour (colours::value);
            g.strokePath (arc (bipolar ? angleAt (0.5f) : startAngle, valueAngle, arcR), stroke);

            // Modulation arc (used from Stage 2): a thinner ring just inside, showing
            // the range modulation is currently moving this parameter through.
            const auto& props = slider.getProperties();
            if (props.contains ("modFrom") && props.contains ("modTo"))
            {
                const float from = props["modFrom"], to = props["modTo"];
                if (std::abs (to - from) > 0.001f)
                {
                    const float modR = arcR - lineW * 1.1f;
                    g.setColour (colours::modulation);
                    g.strokePath (arc (angleAt (from), angleAt (to), modR),
                                  juce::PathStrokeType (lineW * 0.55f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }

            // Pointer
            const float inner = radius * 0.5f;
            const juce::Point<float> tip (centre.x + inner * std::sin (valueAngle), centre.y - inner * std::cos (valueAngle));
            g.setColour (colours::text);
            g.drawLine ({ centre, tip }, lineW * 0.6f);
        }

        juce::Font getLabelFont (juce::Label&) override { return juce::Font (juce::FontOptions (12.0f)); }
    };
}
