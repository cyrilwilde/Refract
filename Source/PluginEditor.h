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

// A Slider that hands right-clicks to its owner instead of dragging
class ModSlider : public juce::Slider
{
public:
    using juce::Slider::Slider;
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick) { onRightClick(); return; }
        juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override   { if (! e.mods.isPopupMenu()) juce::Slider::mouseUp (e); }
};

// A labelled dial. Right-click it to add or remove modulation.
class Knob : public juce::Component
{
public:
    Knob (APVTS& state, const juce::String& paramId, const juce::String& text, bool bipolar = false);
    void resized() override;

    // Called by the editor once everything exists, so the dial can show its
    // modulation ring and offer the right-click menu.
    void setModContext (RefractAudioProcessor* processor);
    int  getModTarget() const { return modTarget; }
    juce::RangedAudioParameter* getParameter() const { return parameter; }

    ModSlider slider;
    juce::Label label;

private:
    void showModMenu();

    APVTS::SliderAttachment attachment;
    juce::RangedAudioParameter* parameter = nullptr;
    RefractAudioProcessor* processor = nullptr;
    int modTarget = -1;
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

// A titled panel that lays its controls out in a row (or a grid).
class Section : public juce::Component
{
public:
    explicit Section (const juce::String& sectionTitle, int rows = 1) : title (sectionTitle), numRows (rows) {}

    template <typename C, typename... Args>
    C& add (Args&&... args)
    {
        auto c = std::make_unique<C> (std::forward<Args> (args)...);
        auto& ref = *c;
        addAndMakeVisible (ref);
        items.push_back (std::move (c));
        return ref;
    }

    void paint (juce::Graphics&) override;
    void resized() override;

protected:
    juce::Rectangle<int> getContentArea() const { return getLocalBounds().reduced (8).withTrimmedTop (18); }
    juce::String title;

private:
    int numRows;
    std::vector<std::unique_ptr<juce::Component>> items;
};

//==============================================================================
// Modulation pages
//==============================================================================

// Draws an LFO's shape, with a dot showing where it is right now
class LfoScope : public juce::Component, private juce::Timer
{
public:
    LfoScope (RefractAudioProcessor& p, int lfoIndex) : processor (p), lfo (lfoIndex) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    RefractAudioProcessor& processor;
    const int lfo;
};

class LfoPanel : public juce::Component, private juce::Timer
{
public:
    LfoPanel (RefractAudioProcessor& p, int lfoIndex);
    void resized() override;
private:
    void timerCallback() override;   // shows Rate, Sync or Ratio depending on the mode
    RefractAudioProcessor& processor;
    const int lfo;
    Choice shape, mode;
    Knob rate;  Choice sync;  Knob ratio;
    Knob depth, phase, jitter, smooth;
    Toggle retrig;
    LfoScope scope;
    int shownMode = -1;
};

// A section with numbered tabs (LFO 1-4, Env 2-4) that shows one panel at a time
class TabbedSection : public Section
{
public:
    TabbedSection (const juce::String& title, const juce::StringArray& tabNames, int radioGroup);
    void addPanel (std::unique_ptr<juce::Component> panel);
    void show (int index);
    void resized() override;
private:
    juce::OwnedArray<juce::TextButton> tabs;
    std::vector<std::unique_ptr<juce::Component>> panels;
};

// The 16-row mod matrix
class ModMatrixPage : public juce::Component
{
public:
    explicit ModMatrixPage (RefractAudioProcessor& p);
    void refresh();   // re-read the routing (after loading, or a right-click change)
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    struct Row
    {
        juce::Label number;
        juce::ComboBox source, target;
        juce::Slider amount { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::TextButton clear { "x" };
        std::unique_ptr<APVTS::SliderAttachment> amountAttachment;
    };
    RefractAudioProcessor& processor;
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
                                    private juce::ChangeListener,
                                    private juce::Timer
{
public:
    explicit RefractAudioProcessorEditor (RefractAudioProcessor&);
    ~RefractAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;   // updates the modulation rings
    void showLayer (int index);
    void showPage (int index);
    void collectKnobs (juce::Component& parent);

    refract::ui::RefractLookAndFeel lookAndFeel;   // declared first, destroyed last
    RefractAudioProcessor& processor;

    std::array<juce::TextButton, refract::numLayers> tabs;
    std::array<std::unique_ptr<LayerPanel>, refract::numLayers> panels;

    // Bottom pages
    std::array<juce::TextButton, 3> pageTabs;
    juce::Component mainPage, modPage;
    ModMatrixPage matrixPage;

    // Main page
    Section sub { "SUB" }, ampEnv { "AMP ENVELOPE" }, filter { "FILTER" }, voice { "VOICE" };
    Section delay { "DELAY" }, reverb { "REVERB" }, macros { "MACROS" }, output { "OUTPUT" };

    // Modulation page
    TabbedSection lfoSection, envSection;

    std::vector<Knob*> knobs;   // every dial, for the modulation rings

    juce::TooltipWindow tooltips { this, 600 };   // shows the hover hints on buttons

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RefractAudioProcessorEditor)
};
