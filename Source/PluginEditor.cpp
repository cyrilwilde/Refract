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

    g.setColour (colours::dimText);
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
      on        (p.apvts, ids::layer (layerIndex, "on"),        "On"),
      level     (p.apvts, ids::layer (layerIndex, "level"),     "Level"),
      pan       (p.apvts, ids::layer (layerIndex, "pan"),       "Pan", true),
      root      (p.apvts, ids::layer (layerIndex, "root"),      "Root"),
      transpose (p.apvts, ids::layer (layerIndex, "coarse"),    "Transpose", true),
      fine      (p.apvts, ids::layer (layerIndex, "fine"),      "Fine", true),
      start     (p.apvts, ids::layer (layerIndex, "start"),     "Start"),
      end       (p.apvts, ids::layer (layerIndex, "end"),       "End"),
      loopMode  (p.apvts, ids::layer (layerIndex, "loopmode"),  "Loop"),
      loopStart (p.apvts, ids::layer (layerIndex, "loopstart"), "Loop Start"),
      loopEnd   (p.apvts, ids::layer (layerIndex, "loopend"),   "Loop End"),
      crossfade (p.apvts, ids::layer (layerIndex, "xfade"),     "X-Fade"),
      reverse   (p.apvts, ids::layer (layerIndex, "reverse"),   "Reverse")
{
    addAndMakeVisible (spectral);

    for (auto* c : std::initializer_list<juce::Component*> {
             &loadButton, &clearButton, &waveButton, &spectralButton, &fitButton, &snapButton, &fileLabel,
             &resetMaskButton, &invertMaskButton, &toolBox, &modeBox, &sizeSlider, &strengthSlider,
             &sizeLabel, &strengthLabel,
             &on, &level, &pan, &root, &transpose, &fine, &detectButton, &start, &end,
             &loopMode, &loopStart, &loopEnd, &crossfade, &reverse })
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

    // --- View ----------------------------------------------------------------
    for (auto* b : { &waveButton, &spectralButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (2000 + layer);
    }
    waveButton.onClick     = [this] { setView (SpectralEditor::View::waveform); };
    spectralButton.onClick = [this] { setView (SpectralEditor::View::spectral); };
    fitButton.onClick      = [this] { spectral.zoomToFit(); };
    fitButton.setTooltip ("Show the whole sample (or double-click the ruler). Scroll or pinch to zoom.");
    snapButton.onClick     = [this] { spectral.setSnapToZero (snapButton.getToggleState()); };
    snapButton.setTooltip ("Snap markers to zero crossings, so cuts and loops don't click");

    // --- Painting tools ----------------------------------------------------
    toolBox.addItemList ({ "Brush", "Rectangle" }, 1);
    toolBox.setSelectedId (1, juce::dontSendNotification);
    toolBox.onChange = [this] { spectral.setTool (toolBox.getSelectedId() == 1 ? SpectralEditor::Tool::brush
                                                                               : SpectralEditor::Tool::rectangle); };

    modeBox.addItemList ({ "Remove", "Restore" }, 1);
    modeBox.setSelectedId (1, juce::dontSendNotification);
    modeBox.onChange = [this] { spectral.setMode (modeBox.getSelectedId() == 1 ? SpectralEditor::Mode::remove
                                                                               : SpectralEditor::Mode::restore); };
    modeBox.setTooltip ("Right-drag always does the opposite");

    sizeSlider.setRange (3.0, 60.0);
    sizeSlider.setValue (14.0, juce::dontSendNotification);
    sizeSlider.onValueChange = [this] { spectral.setBrushSize ((float) sizeSlider.getValue()); };

    strengthSlider.setRange (0.05, 1.0);
    strengthSlider.setValue (1.0, juce::dontSendNotification);
    strengthSlider.onValueChange = [this] { spectral.setStrength ((float) strengthSlider.getValue()); };

    styleLabel (sizeLabel, "Size");
    styleLabel (strengthLabel, "Strength");

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

    // --- Tuning ------------------------------------------------------------------
    detectButton.setTooltip ("Find the sample's pitch and set Root + Fine so it plays in tune");
    detectButton.onClick = [this] { detectPitch(); };

    setView (SpectralEditor::View::waveform);
    refresh();
}

void LayerPanel::setView (SpectralEditor::View v)
{
    spectral.setView (v);
    waveButton.setToggleState (v == SpectralEditor::View::waveform, juce::dontSendNotification);
    spectralButton.setToggleState (v == SpectralEditor::View::spectral, juce::dontSendNotification);

    const bool paintTools = v == SpectralEditor::View::spectral;
    for (auto* c : std::initializer_list<juce::Component*> { &resetMaskButton, &invertMaskButton, &toolBox, &modeBox,
                                                              &sizeSlider, &strengthSlider, &sizeLabel, &strengthLabel })
        c->setVisible (paintTools);
}

void LayerPanel::setParameter (const juce::String& id, float value)
{
    if (auto* p = processor.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

void LayerPanel::detectPitch()
{
    const float midi = processor.detectPitch (layer);
    if (midi < 0.0f)
    {
        detectedText = "no clear pitch found";
    }
    else
    {
        // Root = nearest note; Fine cancels out the leftover so that pressing the
        // root key plays exactly that note.
        const int note = juce::jlimit (0, 127, juce::roundToInt (midi));
        const float cents = ((float) note - midi) * 100.0f;
        setParameter (ids::layer (layer, "root"), (float) note);
        setParameter (ids::layer (layer, "fine"), cents);
        detectedText = "detected " + juce::MidiMessage::getMidiNoteName (note, true, true, 3)
                     + (std::abs (cents) >= 0.5f ? " (" + juce::String (-cents, 0) + " ct)" : juce::String());
    }
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
    if (detectedText.isNotEmpty())
        text << "  - " << detectedText;
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

    // Toolbar: [Load][Clear] file name ... [Wave][Spectral][Fit][Snap] | paint tools
    auto bar = area.removeFromTop (26);
    loadButton.setBounds (bar.removeFromLeft (56));
    bar.removeFromLeft (4);
    clearButton.setBounds (bar.removeFromLeft (52));
    bar.removeFromLeft (10);
    waveButton.setBounds (bar.removeFromLeft (52));
    spectralButton.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (4);
    fitButton.setBounds (bar.removeFromLeft (40));
    bar.removeFromLeft (6);
    snapButton.setBounds (bar.removeFromLeft (62));
    bar.removeFromLeft (8);

    invertMaskButton.setBounds (bar.removeFromRight (56));
    bar.removeFromRight (4);
    resetMaskButton.setBounds (bar.removeFromRight (84));
    bar.removeFromRight (8);
    strengthSlider.setBounds (bar.removeFromRight (70));
    strengthLabel.setBounds (bar.removeFromRight (54));
    sizeSlider.setBounds (bar.removeFromRight (70));
    sizeLabel.setBounds (bar.removeFromRight (32));
    bar.removeFromRight (4);
    modeBox.setBounds (bar.removeFromRight (86));
    bar.removeFromRight (4);
    toolBox.setBounds (bar.removeFromRight (96));
    bar.removeFromRight (8);
    fileLabel.setBounds (bar);

    area.removeFromTop (6);

    // Layer controls along the bottom, in groups
    auto controls = area.removeFromBottom (74);
    const int w = controls.getWidth() / 15;
    auto place = [&] (juce::Component& c) { c.setBounds (controls.removeFromLeft (w).reduced (2, 0)); };
    auto gap = [&] { controls.removeFromLeft (w / 4); };

    place (on); place (level); place (pan); gap();
    place (root); place (transpose); place (fine);
    detectButton.setBounds (controls.removeFromLeft (w).withSizeKeepingCentre (w - 6, 24)); gap();
    place (start); place (end); gap();
    place (loopMode); place (loopStart); place (loopEnd); place (crossfade);
    place (reverse);

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

    lfo1.add<Knob>   (s, "lfo1_rate", "Rate");
    lfo1.add<Choice> (s, "lfo1_shape", "Shape");
    lfo2.add<Knob>   (s, "lfo2_rate", "Rate");
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
    setSize (1140, 830);
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

    auto layerArea = area.removeFromTop (390);
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
