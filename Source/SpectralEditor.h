#pragma once
//
// SpectralEditor.h
// The big sample display, in the spirit of Ableton's Sampler:
//   - Waveform view: see the sample, drag the Start / End / Loop markers.
//   - Spectral view: the same, plus paint on the spectrogram to remove or
//     restore frequencies.
// Both views show a moving playhead for every note that's playing, and can be
// zoomed (scroll / pinch) and scrolled (horizontal scroll). The strip along the
// top (the "ruler") always holds the marker handles.
//
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class SpectralEditor : public juce::Component,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer
{
public:
    enum class View { waveform, spectral };
    enum class Tool { brush, rectangle };
    enum class Mode { remove, restore };

    SpectralEditor (RefractAudioProcessor& processor, int layer);
    ~SpectralEditor() override;

    void refresh (const RefractAudioProcessor::LayerDisplayInfo& info);   // new picture / audio from the processor
    void rebuildOverlay();                                                // after the whole mask changed

    void setView (View v)           { view = v; repaint(); }
    View getView() const            { return view; }
    void setTool (Tool t)           { tool = t; repaint(); }
    void setMode (Mode m)           { mode = m; }
    void setBrushSize (float px)    { brushRadius = px; repaint(); }
    void setStrength (float amount) { strength = amount; }
    void setSnapToZero (bool s)     { snapToZero = s; }
    void zoomToFit()                { viewStart = 0.0; viewEnd = 1.0; repaint(); }

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true;  repaint(); }
    void fileDragExit (const juce::StringArray&) override            { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    enum class Marker { none, start, end, loopStart, loopEnd, loopBar };
    static constexpr int rulerHeight = 22;

    void timerCallback() override { if (isShowing()) repaint(); }   // keeps playheads moving

    // --- Coordinates -----------------------------------------------------------
    juce::Rectangle<float> bodyArea() const { return getLocalBounds().toFloat().withTrimmedTop ((float) rulerHeight); }
    double xToNorm (float x) const;      // pixel -> 0..1 position in the sample
    float  normToX (double n) const;     // 0..1 position -> pixel
    void   zoomAround (double anchorNorm, double factor);

    // --- Markers (Start / End / Loop Start / Loop End are parameters) ----------
    juce::RangedAudioParameter* param (const char* name) const;
    float value (const char* name) const;
    Marker hitTestMarker (juce::Point<float> p) const;
    void   setParam (const char* name, float v);
    double snap (double norm) const;     // nearest zero crossing, if Snap is on
    void   beginMarkerDrag (Marker m, juce::Point<float> p);
    void   dragMarkerTo (juce::Point<float> p);
    void   endMarkerDrag();

    // --- Painting ----------------------------------------------------------------
    juce::Point<float> toMask (juce::Point<float> p) const;   // component pixels -> mask cells
    Mode effectiveMode() const { return eraseInverted ? (mode == Mode::remove ? Mode::restore : Mode::remove) : mode; }
    void stamp (juce::Point<float> componentPos);
    void strokeTo (juce::Point<float> componentPos);
    void applyRectangle (juce::Rectangle<float> componentArea);
    void writeCell (int x, int y, float amount);                // amount 0..1 = how strongly to apply
    void updateOverlayPixel (int x, int y);

    // --- Drawing -------------------------------------------------------------------
    void paintWaveform (juce::Graphics&, juce::Rectangle<float> body);
    void paintSpectral (juce::Graphics&, juce::Rectangle<float> body);
    void paintRegionAndLoop (juce::Graphics&, juce::Rectangle<float> body);
    void paintRuler (juce::Graphics&);
    void paintPlayheads (juce::Graphics&, juce::Rectangle<float> body);

    RefractAudioProcessor& processor;
    const int layer;

    juce::Image spectrogram;
    juce::Image overlay;               // darkens the parts you've removed
    refract::SampleDataPtr playback;
    refract::WaveformPeaksPtr peaks;
    bool hasSample = false, busy = false, missing = false;
    juce::String fileName;

    View view = View::waveform;
    Tool tool = Tool::brush;
    Mode mode = Mode::remove;
    float brushRadius = 14.0f;
    float strength = 1.0f;
    bool snapToZero = false;

    double viewStart = 0.0, viewEnd = 1.0;   // visible part of the sample, 0..1

    bool painting = false, eraseInverted = false, dragHover = false, mouseInside = false;
    juce::Point<float> lastPos, dragStart, mousePos;

    Marker dragMarker = Marker::none;
    double loopBarGrabOffset = 0.0, loopBarLength = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralEditor)
};
