#pragma once
//
// PluginEditor.h
// The window you see in Ableton. It only *displays* and *edits* - all sound
// comes from the processor. Knobs are linked to parameters with
// "attachments", which keep the two in sync in both directions (so automation
// moves the knobs, and moving a knob records automation).
//
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SpectralEditor.h"
#include "RefractLookAndFeel.h"

using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
// Small reusable controls
//==============================================================================
class Knob : public juce::Component
{
public:
    Knob (APVTS& state, const juce::String& paramId, const juce::String& text, bool bipolar = false);
    void resized() override;

    juce::Slider slider;
    juce::Label label;
private:
    APVTS::SliderAttachment attachment;
};

class Choice : public juce::Component
{
public:
    Choice (APVTS& state, const juce::String& paramId, const juce::String& text);
    void resized() override;

    juce::ComboBox box;
    juce::Label label;
private:
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

class Toggle : public juce::Component
{
public:
    Toggle (APVTS& state, const juce::String& paramId, const juce::String& text);
    void resized() override { button.setBounds (getLocalBounds().withSizeKeepingCentre (getWidth(), 24)); }

    juce::ToggleButton button;
private:
    APVTS::ButtonAttachment attachment;
};

// A titled panel that lays its controls out in a row.
class Section : public juce::Component
{
public:
    explicit Section (const juce::String& sectionTitle) : title (sectionTitle) {}

    template <typename C, typename... Args>
    C& add (Args&&... args)
    {
        auto c = std::make_unique<C> (std::forward<Args> (args)...);
        auto& ref = *c;
        addAndMakeVisible (ref);
        items.push_back (std::move (c));
        return ref;
    }

    int getNumItems() const { return (int) items.size(); }
    void paint (juce::Graphics&) override;
    void resized() override;

protected:
    juce::Rectangle<int> getContentArea() const { return getLocalBounds().reduced (8).withTrimmedTop (18); }

private:
    juce::String title;
    std::vector<std::unique_ptr<juce::Component>> items;
};

// Four rows of  [source] -> [destination]  [amount]
class ModMatrixSection : public Section
{
public:
    explicit ModMatrixSection (APVTS& state);
    void resized() override;

private:
    struct Row
    {
        juce::ComboBox source, dest;
        juce::Slider amount { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<APVTS::ComboBoxAttachment> sourceAttachment, destAttachment;
        std::unique_ptr<APVTS::SliderAttachment> amountAttachment;
    };
    std::array<Row, refract::numModSlots> rows;
};

//==============================================================================
// Everything for one sample layer: the sample view, painting tools, and the
// layer's tuning / region / loop controls.
//==============================================================================
class LayerPanel : public juce::Component
{
public:
    LayerPanel (RefractAudioProcessor& processor, int layer);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void setView (SpectralEditor::View v);
    void detectPitch();
    void setParameter (const juce::String& id, float value);

    RefractAudioProcessor& processor;
    const int layer;

    SpectralEditor spectral;

    // Toolbar
    juce::TextButton loadButton { "Load" }, clearButton { "Clear" };
    juce::TextButton waveButton { "Wave" }, spectralButton { "Spectral" }, fitButton { "Fit" };
    juce::ToggleButton snapButton { "Snap" };
    juce::Label fileLabel;
    juce::String detectedText;

    // Painting tools (only shown in the spectral view)
    juce::TextButton resetMaskButton { "Reset Paint" }, invertMaskButton { "Invert" };
    juce::ComboBox toolBox, modeBox;
    juce::Slider sizeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider strengthSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label sizeLabel, strengthLabel;

    // Layer controls
    Toggle on;
    Knob level, pan;
    Knob root, transpose, fine;
    juce::TextButton detectButton { "Detect" };
    Knob start, end;
    Choice loopMode;
    Knob loopStart, loopEnd, crossfade;
    Toggle reverse;

    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
class RefractAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::ChangeListener
{
public:
    explicit RefractAudioProcessorEditor (RefractAudioProcessor&);
    ~RefractAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showLayer (int index);

    refract::ui::RefractLookAndFeel lookAndFeel;   // declared first, destroyed last
    RefractAudioProcessor& processor;

    std::array<juce::TextButton, refract::numLayers> tabs;
    std::array<std::unique_ptr<LayerPanel>, refract::numLayers> panels;

    Section sub { "SUB" }, ampEnv { "AMP ENVELOPE" }, modEnv { "MOD ENVELOPE" }, filter { "FILTER" };
    Section lfo1 { "LFO 1" }, lfo2 { "LFO 2" }, delay { "DELAY" }, reverb { "REVERB" }, output { "OUTPUT" };
    ModMatrixSection matrix;

    juce::TooltipWindow tooltips { this, 600 };   // shows the hover hints on buttons

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RefractAudioProcessorEditor)
};
