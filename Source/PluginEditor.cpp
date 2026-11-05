#include "PluginEditor.h"

using namespace refract;
namespace colours = refract::ui::colours;

namespace
{
    void styleLabel (juce::Label& l, const juce::String& text, bool dim = true)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, dim ? colours::dimText : colours::text);
        l.setInterceptsMouseClicks (false, false);
    }

    void addChoices (juce::ComboBox& box, APVTS& state, const juce::String& paramId)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
            box.addItemList (p->choices, 1);   // items must exist before the attachment is made
    }
}

//==============================================================================
Knob::Knob (APVTS& state, const juce::String& paramId, const juce::String& text, bool bipolar)
    : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow),
      attachment (state, paramId, slider)
{
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setDoubleClickReturnValue (true, (double) state.getParameter (paramId)->convertFrom0to1 (
                                                state.getParameter (paramId)->getDefaultValue()));
    styleLabel (label, text);
    addAndMakeVisible (slider);
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    slider.setBounds (r);
}

Choice::Choice (APVTS& state, const juce::String& paramId, const juce::String& text)
{
    addChoices (box, state, paramId);
    attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, box);
    styleLabel (label, text);
    addAndMakeVisible (box);
    addAndMakeVisible (label);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    box.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 110), 24));
}

Toggle::Toggle (APVTS& state, const juce::String& paramId, const juce::String& text)
    : button (text), attachment (state, paramId, button)
{
    addAndMakeVisible (button);
}

//==============================================================================
void Section::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    g.setColour (colours::accent.withAlpha (0.85f));
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText (title, getLocalBounds().reduced (10, 6).removeFromTop (14), juce::Justification::left);
}

void Section::resized()
{
    if (items.empty())
        return;
    auto area = getContentArea();
    const int w = area.getWidth() / (int) items.size();
    for (auto& c : items)
        c->setBounds (area.removeFromLeft (w).reduced (2));
}

//==============================================================================
ModMatrixSection::ModMatrixSection (APVTS& state) : Section ("MOD MATRIX")
{
    for (int i = 0; i < numModSlots; ++i)
    {
        auto& row = rows[(size_t) i];
        addChoices (row.source, state, ids::mod (i, "src"));
        addChoices (row.dest,   state, ids::mod (i, "dst"));
        row.sourceAttachment = std::make_unique<APVTS::ComboBoxAttachment> (state, ids::mod (i, "src"), row.source);
        row.destAttachment   = std::make_unique<APVTS::ComboBoxAttachment> (state, ids::mod (i, "dst"), row.dest);
        row.amountAttachment = std::make_unique<APVTS::SliderAttachment>   (state, ids::mod (i, "amt"), row.amount);

        row.amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
        row.amount.setColour (juce::Slider::trackColourId, colours::accent2);
        row.amount.setDoubleClickReturnValue (true, 0.0);

        addAndMakeVisible (row.source);
        addAndMakeVisible (row.dest);
        addAndMakeVisible (row.amount);
    }
}

void ModMatrixSection::resized()
{
    auto area = getContentArea();
    const int rowH = area.getHeight() / numModSlots;
    for (auto& row : rows)
    {
        auto r = area.removeFromTop (rowH).reduced (0, 2);
        const int third = r.getWidth() / 3;
        row.source.setBounds (r.removeFromLeft (third).reduced (2, 0));
        row.dest.setBounds (r.removeFromLeft (third).reduced (2, 0));
        row.amount.setBounds (r.reduced (2, 0));
    }
}

//==============================================================================
LayerPanel::LayerPanel (RefractAudioProcessor& p, int layerIndex)
    : processor (p), layer (layerIndex), spectral (p, layerIndex),
      on     (p.apvts, ids::layer (layerIndex, "on"), "On"),
      level  (p.apvts, ids::layer (layerIndex, "level"), "Level"),
      coarse (p.apvts, ids::layer (layerIndex, "coarse"), "Coarse", true),
      fine   (p.apvts, ids::layer (layerIndex, "fine"), "Fine", true),
      pan    (p.apvts, ids::layer (layerIndex, "pan"), "Pan", true),
      start  (p.apvts, ids::layer (layerIndex, "start"), "Start"),
      loop   (p.apvts, ids::layer (layerIndex, "loop"), "Loop")
{
    addAndMakeVisible (spectral);

    for (auto* c : std::initializer_list<juce::Component*> { &loadButton, &clearButton, &resetMaskButton, &invertMaskButton,
                                                              &fileLabel, &toolBox, &modeBox, &sizeSlider, &strengthSlider,
                                                              &sizeLabel, &strengthLabel, &hintLabel,
                                                              &on, &level, &coarse, &fine, &pan, &start, &loop })
        addAndMakeVisible (c);

    fileLabel.setColour (juce::Label::textColourId, colours::text);
    fileLabel.setMinimumHorizontalScale (0.6f);

    // --- Loading --------------------------------------------------------------
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(),
                                                       processor.formatManager.getWildcardForAllFormats());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.existsAsFile())
                                      processor.loadSample (layer, file);
                              });
    };
    clearButton.onClick = [this] { processor.clearSample (layer); };

    // --- Painting tools ----------------------------------------------------
    toolBox.addItemList ({ "Brush", "Rectangle" }, 1);
    toolBox.setSelectedId (1, juce::dontSendNotification);
    toolBox.onChange = [this] { spectral.setTool (toolBox.getSelectedId() == 1 ? SpectralEditor::Tool::brush
                                                                               : SpectralEditor::Tool::rectangle); };

    modeBox.addItemList ({ "Remove", "Restore" }, 1);
    modeBox.setSelectedId (1, juce::dontSendNotification);
    modeBox.onChange = [this] { spectral.setMode (modeBox.getSelectedId() == 1 ? SpectralEditor::Mode::remove
                                                                               : SpectralEditor::Mode::restore); };

    sizeSlider.setRange (3.0, 60.0);
    sizeSlider.setValue (14.0, juce::dontSendNotification);
    sizeSlider.onValueChange = [this] { spectral.setBrushSize ((float) sizeSlider.getValue()); };

    strengthSlider.setRange (0.05, 1.0);
    strengthSlider.setValue (1.0, juce::dontSendNotification);
    strengthSlider.onValueChange = [this] { spectral.setStrength ((float) strengthSlider.getValue()); };

    styleLabel (sizeLabel, "Size");
    styleLabel (strengthLabel, "Strength");
    styleLabel (hintLabel, "Right-drag = opposite mode");
    hintLabel.setFont (juce::Font (juce::FontOptions (11.0f)));

    resetMaskButton.onClick = [this]
    {
        auto& mask = processor.getMask (layer);
        std::fill (mask.begin(), mask.end(), (uint8_t) 255);
        spectral.rebuildOverlay();
        processor.maskEdited (layer);
    };
    invertMaskButton.onClick = [this]
    {
        for (auto& v : processor.getMask (layer))
            v = (uint8_t) (255 - v);
        spectral.rebuildOverlay();
        processor.maskEdited (layer);
    };

    refresh();
}

void LayerPanel::refresh()
{
    const auto info = processor.getDisplayInfo (layer);
    juce::String text = info.fileName.isEmpty() ? juce::String ("No sample") : info.fileName;
    if (info.lengthSeconds > 0.0)
        text << "  (" << juce::String (info.lengthSeconds, 2) << " s)";
    if (info.missing)
        text << "  - missing!";
    fileLabel.setText (text, juce::dontSendNotification);
    spectral.refresh (info);
}

void LayerPanel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
}

void LayerPanel::resized()
{
    auto area = getLocalBounds().reduced (10);

    // Toolbar
    auto bar = area.removeFromTop (26);
    loadButton.setBounds (bar.removeFromLeft (60));
    bar.removeFromLeft (4);
    clearButton.setBounds (bar.removeFromLeft (56));
    bar.removeFromLeft (8);

    invertMaskButton.setBounds (bar.removeFromRight (60));
    bar.removeFromRight (4);
    resetMaskButton.setBounds (bar.removeFromRight (90));
    bar.removeFromRight (10);
    strengthSlider.setBounds (bar.removeFromRight (80));
    strengthLabel.setBounds (bar.removeFromRight (56));
    sizeSlider.setBounds (bar.removeFromRight (80));
    sizeLabel.setBounds (bar.removeFromRight (34));
    bar.removeFromRight (6);
    modeBox.setBounds (bar.removeFromRight (90));
    bar.removeFromRight (4);
    toolBox.setBounds (bar.removeFromRight (100));
    bar.removeFromRight (8);
    fileLabel.setBounds (bar);

    area.removeFromTop (6);

    // Layer knobs along the bottom
    auto controls = area.removeFromBottom (74);
    hintLabel.setBounds (controls.removeFromRight (170));
    juce::Component* row[] = { &on, &level, &coarse, &fine, &pan, &start, &loop };
    const int w = juce::jmin (90, controls.getWidth() / (int) std::size (row));
    for (auto* c : row)
        c->setBounds (controls.removeFromLeft (w).reduced (3, 0));

    area.removeFromBottom (6);
    spectral.setBounds (area);
}

//==============================================================================
RefractAudioProcessorEditor::RefractAudioProcessorEditor (RefractAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), matrix (p.apvts)
{
    setLookAndFeel (&lookAndFeel);
    auto& s = p.apvts;

    // Layer tabs: only one layer's panel is visible at a time.
    for (int i = 0; i < numLayers; ++i)
    {
        auto& tab = tabs[(size_t) i];
        tab.setButtonText ("Layer " + juce::String (i + 1));
        tab.setClickingTogglesState (true);
        tab.setRadioGroupId (1001);
        tab.onClick = [this, i] { showLayer (i); };
        addAndMakeVisible (tab);

        panels[(size_t) i] = std::make_unique<LayerPanel> (p, i);
        addChildComponent (*panels[(size_t) i]);
    }

    sub.add<Knob>   (s, "sub_level", "Level");
    sub.add<Choice> (s, "sub_octave", "Octave");
    sub.add<Choice> (s, "sub_wave", "Wave");

    using EnvSection = std::pair<Section*, const char*>;
    for (auto [section, prefix] : { EnvSection { &ampEnv, "amp" }, EnvSection { &modEnv, "env2" } })
    {
        const juce::String pre (prefix);
        section->add<Knob> (s, pre + "_a", "Attack");
        section->add<Knob> (s, pre + "_d", "Decay");
        section->add<Knob> (s, pre + "_s", "Sustain");
        section->add<Knob> (s, pre + "_r", "Release");
    }

    filter.add<Choice> (s, "flt_type", "Type");
    filter.add<Knob>   (s, "flt_cutoff", "Cutoff");
    filter.add<Knob>   (s, "flt_res", "Resonance");

    lfo1.add<Knob>   (s, "lfo1_rate", "Rate").slider.setColour (juce::Slider::rotarySliderFillColourId, colours::accent2);
    lfo1.add<Choice> (s, "lfo1_shape", "Shape");
    lfo2.add<Knob>   (s, "lfo2_rate", "Rate").slider.setColour (juce::Slider::rotarySliderFillColourId, colours::accent2);
    lfo2.add<Choice> (s, "lfo2_shape", "Shape");

    delay.add<Knob> (s, "dly_time", "Time");
    delay.add<Knob> (s, "dly_fb", "Feedback");
    delay.add<Knob> (s, "dly_mix", "Mix");

    reverb.add<Knob> (s, "rev_size", "Size");
    reverb.add<Knob> (s, "rev_damp", "Damping");
    reverb.add<Knob> (s, "rev_mix", "Mix");

    output.add<Knob> (s, "master", "Master");

    for (auto* c : std::initializer_list<juce::Component*> { &sub, &ampEnv, &modEnv, &filter, &lfo1, &lfo2,
                                                              &matrix, &delay, &reverb, &output })
        addAndMakeVisible (c);

    processor.addChangeListener (this);
    showLayer (0);
    setSize (1140, 790);
}

RefractAudioProcessorEditor::~RefractAudioProcessorEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void RefractAudioProcessorEditor::showLayer (int index)
{
    for (int i = 0; i < numLayers; ++i)
    {
        tabs[(size_t) i].setToggleState (i == index, juce::dontSendNotification);
        panels[(size_t) i]->setVisible (i == index);
    }
}

void RefractAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The processor finished loading or rendering something - redraw.
    for (auto& panel : panels)
        panel->refresh();
}

void RefractAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    auto header = getLocalBounds().removeFromTop (48).reduced (16, 0);
    g.setColour (colours::text);
    g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    g.drawText ("REFRACT", header, juce::Justification::centredLeft);
    g.setColour (colours::dimText);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("spectral sample synth", header.withTrimmedLeft (130), juce::Justification::centredLeft);
}

void RefractAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (10, 0);

    auto header = area.removeFromTop (48);
    auto tabArea = header.removeFromRight (360).reduced (0, 10);
    const int tabW = tabArea.getWidth() / numLayers;
    for (auto& tab : tabs)
        tab.setBounds (tabArea.removeFromLeft (tabW).reduced (3, 0));

    auto layerArea = area.removeFromTop (350);
    for (auto& panel : panels)
        panel->setBounds (layerArea);

    area.removeFromTop (8);
    area.removeFromBottom (10);

    // Lay out a row of sections, giving each width in proportion to its weight.
    auto layoutRow = [] (juce::Rectangle<int> row, std::initializer_list<std::pair<juce::Component*, int>> sections)
    {
        int total = 0;
        for (auto& s : sections) total += s.second;
        const int unit = row.getWidth() / juce::jmax (1, total);
        int remaining = (int) sections.size();
        for (auto& s : sections)
        {
            auto r = --remaining == 0 ? row : row.removeFromLeft (unit * s.second);
            s.first->setBounds (r.reduced (4));
        }
    };

    const int rowH = (area.getHeight() - 4) / 2;
    layoutRow (area.removeFromTop (rowH), { { &sub, 3 }, { &ampEnv, 4 }, { &modEnv, 4 }, { &filter, 3 } });
    area.removeFromTop (4);
    layoutRow (area, { { &lfo1, 2 }, { &lfo2, 2 }, { &matrix, 5 }, { &delay, 3 }, { &reverb, 3 }, { &output, 1 } });
}
