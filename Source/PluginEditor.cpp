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

    void drawPanel (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
    }
}

//==============================================================================
//  Knob
//==============================================================================
Knob::Knob (APVTS& state, const juce::String& paramId, const juce::String& text, bool bipolar)
    : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow),
      attachment (state, paramId, slider),
      parameter (state.getParameter (paramId))
{
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setDoubleClickReturnValue (true, (double) parameter->convertFrom0to1 (parameter->getDefaultValue()));
    slider.onRightClick = [this] { showModMenu(); };
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

void Knob::setModContext (RefractAudioProcessor* p)
{
    processor = p;
    modTarget = p != nullptr ? p->getParamTable().indexOf (parameter->getParameterID()) : -1;
    if (modTarget >= 0)
        slider.setTooltip ("Right-click to modulate");
}

void Knob::showModMenu()
{
    if (processor == nullptr || modTarget < 0)
        return;

    const auto names = modSourceNames();
    juce::PopupMenu sources;
    for (int s = 1; s < numModSources; ++s)
        sources.addItem (100 + s, names[s]);

    juce::PopupMenu menu;
    menu.addSectionHeader (parameter->getName (40));
    menu.addSubMenu ("Modulate with", sources);

    // Existing routes into this dial can be removed from here too
    bool any = false;
    for (int slot = 0; slot < numModSlots; ++slot)
        if (processor->getModTarget (slot) == modTarget && processor->getModSource (slot) != srcNone)
        {
            if (! any) menu.addSeparator();
            any = true;
            menu.addItem (200 + slot, "Remove " + names[processor->getModSource (slot)]
                                      + " (matrix row " + juce::String (slot + 1) + ")");
        }

    juce::Component::SafePointer<Knob> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider), [safe] (int result)
    {
        if (safe == nullptr || result == 0) return;
        auto* p = safe->processor;
        if (result >= 200)       p->clearModSlot (result - 200);
        else if (result >= 100)  p->addModulation (result - 100, safe->modTarget);
    });
}

//==============================================================================
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
    drawPanel (g, getLocalBounds().toFloat().reduced (1.0f));
    g.setColour (colours::dimText);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText (title, getLocalBounds().reduced (10, 6).removeFromTop (14), juce::Justification::left);
}

void Section::resized()
{
    if (items.empty())
        return;
    auto area = getContentArea();
    const int perRow = ((int) items.size() + numRows - 1) / numRows;
    const int rowH = area.getHeight() / numRows;
    const int w = area.getWidth() / perRow;
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const int row = i / perRow, col = i % perRow;
        items[(size_t) i]->setBounds (juce::Rectangle<int> (area.getX() + col * w, area.getY() + row * rowH, w, rowH).reduced (2));
    }
}

//==============================================================================
//  LFO scope and panel
//==============================================================================
void LfoScope::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (colours::background);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (colours::panelEdge);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    auto& s = processor.apvts;
    const int shape    = (int) s.getRawParameterValue (ids::lfo (lfo, "shape"))->load();
    const float depth  = s.getRawParameterValue (ids::lfo (lfo, "depth"))->load();
    const float smooth = s.getRawParameterValue (ids::lfo (lfo, "smooth"))->load();
    const float phase0 = s.getRawParameterValue (ids::lfo (lfo, "phase"))->load();

    // Draw two cycles of the shape, with a preview of the smoothing
    static const float randomSteps[] = { 0.6f, -0.3f, 0.9f, -0.8f, 0.2f, -0.5f };
    const int points = juce::jmax (2, (int) r.getWidth());
    const float coef = smooth <= 0.0001f ? 1.0f : (float) (1.0 - std::exp (-1.0 / (smooth * smooth * 0.5 * points / 2.0)));
    juce::Path path;
    float y = 0.0f;
    for (int i = 0; i < points; ++i)
    {
        const float t = (float) i / (float) (points - 1) * 2.0f + phase0;
        const float p = t - std::floor (t);
        float raw;
        switch (shape)
        {
            case lfoSine:     raw = std::sin (juce::MathConstants<float>::twoPi * p); break;
            case lfoTriangle: raw = 1.0f - 4.0f * std::abs (p - 0.5f); break;
            case lfoSawUp:    raw = 2.0f * p - 1.0f; break;
            case lfoSawDown:  raw = 1.0f - 2.0f * p; break;
            case lfoSquare:   raw = p < 0.5f ? 1.0f : -1.0f; break;
            default:          raw = randomSteps[(int) std::floor (t * 2.0f) % 6]; break;   // a made-up random pattern
        }
        y = i == 0 ? raw : y + (raw - y) * coef;
        const float py = r.getCentreY() - y * depth * r.getHeight() * 0.42f;
        const float px = r.getX() + (float) i;
        if (i == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
    }
    g.setColour (colours::waveform);
    g.strokePath (path, juce::PathStrokeType (1.5f));

    // Where the LFO is right now (newest note, or the free-running LFO)
    const float live = processor.getLiveSource (srcLfo1 + lfo);
    const float ly = r.getCentreY() - live * r.getHeight() * 0.42f;
    g.setColour (colours::modulation);
    g.drawHorizontalLine ((int) ly, r.getX(), r.getRight());
    g.fillEllipse (r.getRight() - 9.0f, ly - 4.0f, 8.0f, 8.0f);
}

LfoPanel::LfoPanel (RefractAudioProcessor& p, int lfoIndex)
    : processor (p), lfo (lfoIndex),
      shape  (p.apvts, ids::lfo (lfoIndex, "shape"),  "Shape"),
      mode   (p.apvts, ids::lfo (lfoIndex, "mode"),   "Rate Mode"),
      rate   (p.apvts, ids::lfo (lfoIndex, "rate"),   "Rate"),
      sync   (p.apvts, ids::lfo (lfoIndex, "sync"),   "Sync"),
      ratio  (p.apvts, ids::lfo (lfoIndex, "ratio"),  "Ratio"),
      depth  (p.apvts, ids::lfo (lfoIndex, "depth"),  "Depth"),
      phase  (p.apvts, ids::lfo (lfoIndex, "phase"),  "Phase"),
      jitter (p.apvts, ids::lfo (lfoIndex, "jitter"), "Jitter"),
      smooth (p.apvts, ids::lfo (lfoIndex, "smooth"), "Smooth"),
      retrig (p.apvts, ids::lfo (lfoIndex, "retrig"), "Retrigger"),
      scope  (p, lfoIndex)
{
    for (auto* c : std::initializer_list<juce::Component*> { &shape, &mode, &rate, &sync, &ratio, &depth, &phase,
                                                              &jitter, &smooth, &retrig, &scope })
        addAndMakeVisible (c);
    retrig.button.setTooltip ("On: every note restarts the LFO at its Phase. Off: all notes share one free-running LFO.");
    jitter.slider.setTooltip ("Randomly stretches or squeezes each cycle (up to an octave)");
    smooth.slider.setTooltip ("Rounds off sharp edges - turns Random steps into slow drifting curves");
    timerCallback();
    startTimerHz (10);
}

void LfoPanel::timerCallback()
{
    const int m = (int) processor.apvts.getRawParameterValue (ids::lfo (lfo, "mode"))->load();
    if (m == shownMode) return;
    shownMode = m;
    rate.setVisible (m == rateHz);
    sync.setVisible (m == rateSync);
    ratio.setVisible (m == rateFreq);
}

void LfoPanel::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (area.getHeight() / 2);
    auto bottom = area;

    // Top row: shape, mode, rate (one of three), depth, phase
    const int w = top.getWidth() / 5;
    shape.setBounds (top.removeFromLeft (w).reduced (2));
    mode.setBounds (top.removeFromLeft (w).reduced (2));
    const auto rateSlot = top.removeFromLeft (w).reduced (2);
    rate.setBounds (rateSlot); sync.setBounds (rateSlot); ratio.setBounds (rateSlot);
    depth.setBounds (top.removeFromLeft (w).reduced (2));
    phase.setBounds (top.reduced (2));

    // Bottom row: jitter, smooth, retrigger, scope
    jitter.setBounds (bottom.removeFromLeft (w).reduced (2));
    smooth.setBounds (bottom.removeFromLeft (w).reduced (2));
    retrig.setBounds (bottom.removeFromLeft (w).reduced (2));
    scope.setBounds (bottom.reduced (4, 6));
}

//==============================================================================
TabbedSection::TabbedSection (const juce::String& t, const juce::StringArray& tabNames, int radioGroup)
    : Section (t)
{
    for (const auto& name : tabNames)
    {
        auto* b = tabs.add (new juce::TextButton (name));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (radioGroup);
        const int i = tabs.size() - 1;
        b->onClick = [this, i] { show (i); };
        addAndMakeVisible (b);
    }
}

void TabbedSection::addPanel (std::unique_ptr<juce::Component> panel)
{
    addChildComponent (*panel);
    panels.push_back (std::move (panel));
    if (panels.size() == 1) show (0);
}

void TabbedSection::show (int index)
{
    for (int i = 0; i < tabs.size(); ++i)
        tabs[i]->setToggleState (i == index, juce::dontSendNotification);
    for (size_t i = 0; i < panels.size(); ++i)
        panels[i]->setVisible ((int) i == index);
}

void TabbedSection::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto tabRow = area.removeFromTop (20).withTrimmedLeft (juce::jmin (140, area.getWidth() / 3));
    const int w = juce::jmin (60, tabRow.getWidth() / juce::jmax (1, tabs.size()));
    for (auto* t : tabs)
        t->setBounds (tabRow.removeFromLeft (w).reduced (2, 0));
    area.removeFromTop (4);
    for (auto& p : panels)
        p->setBounds (area);
}

//==============================================================================
//  Mod matrix page
//==============================================================================
ModMatrixPage::ModMatrixPage (RefractAudioProcessor& p) : processor (p)
{
    const auto& table = p.getParamTable();
    const auto sourceNames = modSourceNames();

    for (int i = 0; i < numModSlots; ++i)
    {
        auto& row = rows[(size_t) i];
        styleLabel (row.number, juce::String (i + 1));

        // Sources: item ID = source number + 1
        for (int s = 0; s < numModSources; ++s)
            row.source.addItem (sourceNames[s], s + 1);

        // Destinations: grouped into sub-menus by section. Item ID = table index + 2 (1 = None)
        row.target.addItem ("None", 1);
        juce::StringArray groups;
        for (int t = 0; t < table.size(); ++t)
            groups.addIfNotAlreadyThere (table[t].group);
        for (const auto& group : groups)
        {
            juce::PopupMenu sub;
            for (int t = 0; t < table.size(); ++t)
                if (table[t].group == group)
                    sub.addItem (t + 2, table[t].name);
            row.target.getRootMenu()->addSubMenu (group, sub);
        }
        row.target.setTextWhenNothingSelected ("None");

        row.source.onChange = [this, i]
        {
            auto& r = rows[(size_t) i];
            processor.setModRouting (i, r.source.getSelectedId() - 1, processor.getModTarget (i));
        };
        row.target.onChange = [this, i]
        {
            auto& r = rows[(size_t) i];
            processor.setModRouting (i, processor.getModSource (i), r.target.getSelectedId() - 2);
        };

        row.amountAttachment = std::make_unique<APVTS::SliderAttachment> (p.apvts, ids::modAmount (i), row.amount);
        row.amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
        row.amount.setDoubleClickReturnValue (true, 0.0);
        row.amount.setTooltip ("Amount: +/-1 = the full travel of the destination knob. Shift-drag for fine control.");

        row.clear.setTooltip ("Clear this row");
        row.clear.onClick = [this, i] { processor.clearModSlot (i); };

        for (auto* c : std::initializer_list<juce::Component*> { &row.number, &row.source, &row.target, &row.amount, &row.clear })
            addAndMakeVisible (c);
    }
    refresh();
}

void ModMatrixPage::refresh()
{
    for (int i = 0; i < numModSlots; ++i)
    {
        auto& row = rows[(size_t) i];
        row.source.setSelectedId (processor.getModSource (i) + 1, juce::dontSendNotification);
        const int t = processor.getModTarget (i);
        row.target.setSelectedId (t >= 0 ? t + 2 : 1, juce::dontSendNotification);
    }
}

void ModMatrixPage::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat().reduced (1.0f));
    g.setColour (colours::dimText);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    auto header = getLocalBounds().reduced (10, 6).removeFromTop (14);
    g.drawText ("MOD MATRIX     source  >  destination  x  amount        (or right-click any dial)", header, juce::Justification::left);
}

void ModMatrixPage::resized()
{
    auto area = getLocalBounds().reduced (10).withTrimmedTop (20);
    const int half = numModSlots / 2;
    auto columns = std::array<juce::Rectangle<int>, 2> { area.removeFromLeft (area.getWidth() / 2).withTrimmedRight (8), area.withTrimmedLeft (8) };
    for (int i = 0; i < numModSlots; ++i)
    {
        auto& col = columns[(size_t) (i / half)];
        const int rowH = juce::jmin (36, (area.getHeight()) / half);
        auto r = juce::Rectangle<int> (col.getX(), col.getY() + (i % half) * rowH, col.getWidth(), rowH).reduced (0, 3);
        auto& row = rows[(size_t) i];
        row.number.setBounds (r.removeFromLeft (22));
        row.clear.setBounds (r.removeFromRight (24));
        r.removeFromRight (4);
        const int w = r.getWidth();
        row.source.setBounds (r.removeFromLeft (w * 28 / 100).reduced (2, 0));
        row.target.setBounds (r.removeFromLeft (w * 36 / 100).reduced (2, 0));
        row.amount.setBounds (r.reduced (2, 0));
    }
}

//==============================================================================
//  Oscillator page
//==============================================================================
void OscScope::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (colours::background);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (colours::panelEdge);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    auto& s = processor.apvts;
    auto get = [&s, this] (const char* name) { return s.getRawParameterValue (ids::osc (osc, name))->load(); };
    const bool on = get ("on") > 0.5f;
    const int mode = (int) get ("mode");

    // Work out one cycle of the waveform at 'points' positions
    const int points = juce::jmax (2, (int) r.getWidth());
    std::vector<float> values ((size_t) points, 0.0f);

    if (mode == oscAnalog)
    {
        const float wave = get ("wave"), pw = get ("pw");
        auto shape = [pw] (int k, float p)
        {
            switch (k)
            {
                case 0:  return std::sin (juce::MathConstants<float>::twoPi * p);
                case 1:  return 1.0f - 4.0f * std::abs (p - 0.5f);
                case 2:  return 2.0f * p - 1.0f;
                default: return p < pw ? 1.0f : -1.0f;
            }
        };
        const int a = juce::jmin ((int) wave, 2);
        const float mix = wave - (float) a;
        for (int i = 0; i < points; ++i)
        {
            const float p = (float) i / (float) (points - 1);
            values[(size_t) i] = shape (a, p) + (shape (a + 1, p) - shape (a, p)) * mix;
        }
    }
    else if (auto table = processor.getWavetable ((int) get ("table")))
    {
        const float pos = juce::jlimit (0.0f, 1.0f, get ("pos")) * (float) (wtFrames - 1);
        const int f0 = juce::jmin ((int) pos, wtFrames - 1), f1 = juce::jmin (f0 + 1, wtFrames - 1);
        const float mix = pos - (float) f0;
        const float* fa = table->get (f0, 0);
        const float* fb = table->get (f1, 0);
        for (int i = 0; i < points; ++i)
        {
            const int idx = juce::jmin (wtSize - 1, i * wtSize / points);
            values[(size_t) i] = fa[idx] + (fb[idx] - fa[idx]) * mix;
        }
    }
    else
    {
        g.setColour (colours::dimText);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ((int) get ("table") >= numBuiltInTables ? "Load a sample into that layer" : "Building...",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    juce::Path path;
    for (int i = 0; i < points; ++i)
    {
        const float x = r.getX() + (float) i;
        const float y = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, values[(size_t) i]) * r.getHeight() * 0.42f;
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    g.setColour (on ? colours::waveform : colours::track);
    g.strokePath (path, juce::PathStrokeType (1.5f));
}

OscPanel::OscPanel (RefractAudioProcessor& p, int oscIndex)
    : Section ("OSC " + juce::String (oscIndex + 1)), processor (p), osc (oscIndex),
      on          (p.apvts, ids::osc (oscIndex, "on"),     "On"),
      mode        (p.apvts, ids::osc (oscIndex, "mode"),   "Mode"),
      wave        (p.apvts, ids::osc (oscIndex, "wave"),   "Wave"),
      table       (p.apvts, ids::osc (oscIndex, "table"),  "Table"),
      pulseWidth  (p.apvts, ids::osc (oscIndex, "pw"),     "Pulse W"),
      position    (p.apvts, ids::osc (oscIndex, "pos"),    "Position"),
      level       (p.apvts, ids::osc (oscIndex, "level"),  "Level"),
      pan         (p.apvts, ids::osc (oscIndex, "pan"),    "Pan", true),
      octave      (p.apvts, ids::osc (oscIndex, "oct"),    "Octave", true),
      semi        (p.apvts, ids::osc (oscIndex, "semi"),   "Semi", true),
      fine        (p.apvts, ids::osc (oscIndex, "fine"),   "Fine", true),
      unison      (p.apvts, ids::osc (oscIndex, "unison"), "Unison"),
      detune      (p.apvts, ids::osc (oscIndex, "detune"), "Detune"),
      spread      (p.apvts, ids::osc (oscIndex, "spread"), "Spread"),
      randomPhase (p.apvts, ids::osc (oscIndex, "rphase"), "Rnd Phase"),
      scope (p, oscIndex)
{
    if (oscIndex == 1)
    {
        sync = std::make_unique<Toggle> (p.apvts, "osc2_sync", "Sync");
        fm = std::make_unique<Knob> (p.apvts, "osc2_fm", "FM");
        sync->button.setTooltip ("Hard sync: Osc 2 restarts every time Osc 1 starts a cycle. Sweep Osc 2's pitch for classic sync sounds.");
        fm->slider.setTooltip ("Osc 1 modulates Osc 2's phase - metallic, bell and growl tones");
        addAndMakeVisible (*sync);
        addAndMakeVisible (*fm);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &on, &mode, &wave, &table, &pulseWidth, &position, &level, &pan,
                                                              &octave, &semi, &fine, &unison, &detune, &spread, &randomPhase, &scope })
        addAndMakeVisible (c);
    wave.slider.setTooltip ("Morphs sine > triangle > saw > square");
    randomPhase.slider.setTooltip ("How randomly each note (and each unison voice) starts - makes unison wide instead of phasey");
    timerCallback();
    startTimerHz (10);
}

void OscPanel::timerCallback()
{
    const int m = (int) processor.apvts.getRawParameterValue (ids::osc (osc, "mode"))->load();
    if (m == shownMode) return;
    shownMode = m;
    wave.setVisible (m == oscAnalog);
    pulseWidth.setVisible (m == oscAnalog);
    table.setVisible (m == oscWavetable);
    position.setVisible (m == oscWavetable);
}

void OscPanel::resized()
{
    auto area = getContentArea();
    auto top = area.removeFromTop (area.getHeight() / 2);
    auto bottom = area;
    const int w = top.getWidth() / 9;
    auto place = [w] (juce::Rectangle<int>& row, juce::Component& c) { c.setBounds (row.removeFromLeft (w).reduced (2)); };

    place (top, on); place (top, mode);
    const auto slotA = top.removeFromLeft (w).reduced (2);
    wave.setBounds (slotA); table.setBounds (slotA);
    const auto slotB = top.removeFromLeft (w).reduced (2);
    pulseWidth.setBounds (slotB); position.setBounds (slotB);
    place (top, level); place (top, pan);
    scope.setBounds (top.reduced (4, 8));

    place (bottom, octave); place (bottom, semi); place (bottom, fine);
    place (bottom, unison); place (bottom, detune); place (bottom, spread); place (bottom, randomPhase);
    if (sync != nullptr) place (bottom, *sync);
    if (fm != nullptr)   place (bottom, *fm);
}

//==============================================================================
//  Layer panel
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
      reverse   (p.apvts, ids::layer (layerIndex, "reverse"),   "Reverse"),
      unison    (p.apvts, ids::layer (layerIndex, "unison"),    "Unison"),
      detune    (p.apvts, ids::layer (layerIndex, "detune"),    "Detune"),
      spread    (p.apvts, ids::layer (layerIndex, "spread"),    "Spread")
{
    addAndMakeVisible (spectral);

    for (auto* c : std::initializer_list<juce::Component*> {
             &loadButton, &clearButton, &waveButton, &spectralButton, &fitButton, &snapButton, &fileLabel,
             &resetMaskButton, &invertMaskButton, &toolBox, &modeBox, &sizeSlider, &strengthSlider,
             &sizeLabel, &strengthLabel,
             &on, &level, &pan, &root, &transpose, &fine, &detectButton, &start, &end,
             &loopMode, &loopStart, &loopEnd, &crossfade, &reverse, &unison, &detune, &spread })
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
    const int w = controls.getWidth() / 18;
    auto place = [&] (juce::Component& c) { c.setBounds (controls.removeFromLeft (w).reduced (2, 0)); };
    auto gap = [&] { controls.removeFromLeft (w / 4); };

    place (on); place (level); place (pan); gap();
    place (root); place (transpose); place (fine);
    detectButton.setBounds (controls.removeFromLeft (w).withSizeKeepingCentre (w - 6, 24)); gap();
    place (start); place (end); gap();
    place (loopMode); place (loopStart); place (loopEnd); place (crossfade);
    place (reverse); gap();
    place (unison); place (detune); place (spread);

    area.removeFromBottom (6);
    spectral.setBounds (area);
}

//==============================================================================
//  The editor window
//==============================================================================
RefractAudioProcessorEditor::RefractAudioProcessorEditor (RefractAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), matrixPage (p),
      osc1 (p, 0), osc2 (p, 1),
      lfoSection ("LFO", { "1", "2", "3", "4" }, 3001),
      envSection ("ENVELOPE", { "2", "3", "4" }, 3002)
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

    // Page tabs for the bottom half
    const char* pageNames[] = { "MAIN", "OSC", "MODULATION", "MATRIX" };
    for (int i = 0; i < 4; ++i)
    {
        auto& tab = pageTabs[(size_t) i];
        tab.setButtonText (pageNames[i]);
        tab.setClickingTogglesState (true);
        tab.setRadioGroupId (1002);
        tab.onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tab);
    }

    // --- Main page ---
    ampEnv.add<Knob> (s, ids::env (0, "a"), "Attack");
    ampEnv.add<Knob> (s, ids::env (0, "d"), "Decay");
    ampEnv.add<Knob> (s, ids::env (0, "s"), "Sustain");
    ampEnv.add<Knob> (s, ids::env (0, "r"), "Release");

    filter.add<Choice> (s, "flt_type", "Type");
    filter.add<Knob>   (s, "flt_cutoff", "Cutoff");
    filter.add<Knob>   (s, "flt_res", "Resonance");

    voice.add<Knob> (s, "voice_pitch", "Pitch", true);
    voice.add<Knob> (s, "voice_pan", "Pan", true);
    voice.add<Knob> (s, "voice_level", "Level");

    delay.add<Knob> (s, "dly_time", "Time");
    delay.add<Knob> (s, "dly_fb", "Feedback");
    delay.add<Knob> (s, "dly_mix", "Mix");

    reverb.add<Knob> (s, "rev_size", "Size");
    reverb.add<Knob> (s, "rev_damp", "Damping");
    reverb.add<Knob> (s, "rev_mix", "Mix");

    for (int m = 0; m < numMacros; ++m)
        macros.add<Knob> (s, ids::macro (m), "Macro " + juce::String (m + 1));

    output.add<Knob> (s, "master", "Master");

    for (auto* c : std::initializer_list<juce::Component*> { &ampEnv, &filter, &voice, &delay, &reverb, &macros, &output })
        mainPage.addAndMakeVisible (c);

    // --- Oscillator page ---
    subNoise.add<Knob>   (s, "sub_level", "Sub");
    subNoise.add<Choice> (s, "sub_octave", "Octave");
    subNoise.add<Choice> (s, "sub_wave", "Wave");
    subNoise.add<Knob>   (s, "noise_level", "Noise");
    subNoise.add<Knob>   (s, "noise_colour", "Colour", true);
    subNoise.add<juce::Component>();   // keeps the grid even

    glide.add<Choice> (s, "voice_mode", "Voice");
    glide.add<Choice> (s, "glide_mode", "Glide");
    glide.add<Knob>   (s, "glide_time", "Time");

    for (auto* c : std::initializer_list<juce::Component*> { &osc1, &osc2, &subNoise, &glide })
        oscPage.addAndMakeVisible (c);

    // --- Modulation page ---
    for (int k = 0; k < numLfos; ++k)
        lfoSection.addPanel (std::make_unique<LfoPanel> (p, k));

    for (int e = 1; e < numEnvs; ++e)
    {
        auto env = std::make_unique<Section> ("", 1);
        env->add<Knob> (s, ids::env (e, "a"), "Attack");
        env->add<Knob> (s, ids::env (e, "d"), "Decay");
        env->add<Knob> (s, ids::env (e, "s"), "Sustain");
        env->add<Knob> (s, ids::env (e, "r"), "Release");
        envSection.addPanel (std::move (env));
    }
    modPage.addAndMakeVisible (lfoSection);
    modPage.addAndMakeVisible (envSection);

    addChildComponent (mainPage);
    addChildComponent (oscPage);
    addChildComponent (modPage);
    addChildComponent (matrixPage);

    // Every dial gets its modulation ring and right-click menu
    collectKnobs (*this);
    for (auto* k : knobs)
        k->setModContext (&processor);

    processor.addChangeListener (this);
    showLayer (0);
    showPage (0);
    setSize (1140, 830);
    startTimerHz (30);
}

RefractAudioProcessorEditor::~RefractAudioProcessorEditor()
{
    stopTimer();
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void RefractAudioProcessorEditor::collectKnobs (juce::Component& parent)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* k = dynamic_cast<Knob*> (child))
            knobs.push_back (k);
        else
            collectKnobs (*child);
    }
}

void RefractAudioProcessorEditor::showLayer (int index)
{
    for (int i = 0; i < numLayers; ++i)
    {
        tabs[(size_t) i].setToggleState (i == index, juce::dontSendNotification);
        panels[(size_t) i]->setVisible (i == index);
    }
}

void RefractAudioProcessorEditor::showPage (int index)
{
    for (int i = 0; i < 4; ++i)
        pageTabs[(size_t) i].setToggleState (i == index, juce::dontSendNotification);
    mainPage.setVisible (index == 0);
    oscPage.setVisible (index == 1);
    modPage.setVisible (index == 2);
    matrixPage.setVisible (index == 3);
}

void RefractAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The processor finished loading/rendering, or the mod routing changed - redraw.
    for (auto& panel : panels)
        panel->refresh();
    matrixPage.refresh();
}

void RefractAudioProcessorEditor::timerCallback()
{
    // Modulation rings. For each dial work out:
    //  - the range the routed modulation *can* move it through (pale ring), and
    //  - where the newest note's modulation has it right now (bright arc).
    const auto& table = processor.getParamTable();
    const bool noteActive = processor.isNoteActive();

    for (auto* k : knobs)
    {
        const int t = k->getModTarget();
        if (t < 0 || ! k->isShowing())
            continue;

        float lo = 0.0f, hi = 0.0f;
        for (int slot = 0; slot < numModSlots; ++slot)
        {
            const int src = processor.getModSource (slot);
            if (processor.getModTarget (slot) != t || src == srcNone)
                continue;
            const float a = processor.apvts.getRawParameterValue (ids::modAmount (slot))->load();
            if (isBipolarSource (src)) { lo -= std::abs (a); hi += std::abs (a); }
            else if (a > 0.0f)         hi += a;
            else                       lo += a;
        }

        auto& props = k->slider.getProperties();
        const bool hadMod = props.contains ("modFrom");
        if (lo == 0.0f && hi == 0.0f)
        {
            if (hadMod)
            {
                props.remove ("modFrom"); props.remove ("modTo"); props.remove ("modLive");
                k->slider.repaint();
            }
            continue;
        }

        const float base = k->getParameter()->getValue();
        const float from = juce::jlimit (0.0f, 1.0f, base + lo);
        const float to   = juce::jlimit (0.0f, 1.0f, base + hi);
        const bool live  = noteActive || table[t].global;
        const float now  = live ? juce::jlimit (0.0f, 1.0f, base + processor.getLiveOffset (t)) : base;

        const bool changed = ! hadMod || (float) props["modFrom"] != from || (float) props["modTo"] != to
                          || (float) props["modLive"] != now;
        if (changed)
        {
            props.set ("modFrom", from);
            props.set ("modTo", to);
            props.set ("modLive", now);
            k->slider.repaint();
        }
    }
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

    area.removeFromTop (6);
    auto pageRow = area.removeFromTop (24);
    for (auto& tab : pageTabs)
        tab.setBounds (pageRow.removeFromLeft (110).reduced (2, 0));
    area.removeFromTop (4);
    area.removeFromBottom (10);

    mainPage.setBounds (area);
    oscPage.setBounds (area);
    modPage.setBounds (area);
    matrixPage.setBounds (area);

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
            s.first->setBounds (r.reduced (3));
        }
    };

    // Main page: two rows
    auto page = mainPage.getLocalBounds();
    const int rowH = page.getHeight() / 2;
    layoutRow (page.removeFromTop (rowH), { { &ampEnv, 4 }, { &filter, 3 }, { &voice, 3 } });

    // Oscillator page: the two oscillators side by side, sub/noise and glide on the right
    auto oscArea = oscPage.getLocalBounds();
    auto right = oscArea.removeFromRight (oscArea.getWidth() * 22 / 100);
    subNoise.setBounds (right.removeFromTop (right.getHeight() * 55 / 100).reduced (3));
    glide.setBounds (right.reduced (3));
    osc1.setBounds (oscArea.removeFromLeft (oscArea.getWidth() / 2).reduced (3));
    osc2.setBounds (oscArea.reduced (3));
    layoutRow (page, { { &delay, 3 }, { &reverb, 3 }, { &macros, 4 }, { &output, 1 } });

    // Modulation page: LFOs on the left, envelopes on the right
    auto mod = modPage.getLocalBounds();
    lfoSection.setBounds (mod.removeFromLeft (mod.getWidth() * 62 / 100).reduced (3));
    envSection.setBounds (mod.removeFromTop (mod.getHeight() * 55 / 100).reduced (3));
}
