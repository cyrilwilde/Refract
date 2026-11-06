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
    // Brushes paint as you drag; shapes are dragged out as a box and applied on release
    enum class Tool { softBrush, hardBrush, squareBrush, spray, smudge, harmonic, freqBand, timeSlice,
                      rectangle, ellipse, heart };
    enum class Mode { remove, restore, boost };
    static juce::StringArray toolNames()
    {
        return { "Soft Brush", "Hard Brush", "Square Brush", "Spray", "Smudge", "Harmonic",
                 "Freq Band", "Time Slice", "Rectangle", "Ellipse", "Heart" };
    }
    static juce::StringArray modeNames() { return { "Remove", "Restore", "Boost" }; }

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

    // Painting undo / redo (call pushUndo() before changing the mask from outside)
    void pushUndo();
    void undo();
    void redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }

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
    enum class Marker { none, start, end, loopStart, loopEnd, loopBar, grainPos };
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
    Mode effectiveMode() const { return eraseInverted ? (mode == Mode::restore ? Mode::remove : Mode::restore) : mode; }
    bool isShapeTool() const { return tool == Tool::rectangle || tool == Tool::ellipse || tool == Tool::heart; }
    bool isGranular() const;
    void stamp (juce::Point<float> componentPos);
    void stampCircle (juce::Point<float> maskCentre, float rx, float ry, bool soft);
    void smudgeAt (juce::Point<float> maskCentre, float rx, float ry);
    void applyShape (juce::Rectangle<float> componentArea);
    juce::Path shapeOutline (juce::Rectangle<float> area) const;
    void strokeTo (juce::Point<float> componentPos);
    void writeCell (int x, int y, float amount);                // amount 0..1 = how strongly to apply
    void updateOverlayPixel (int x, int y);

    // --- Drawing -------------------------------------------------------------------
    void paintWaveform (juce::Graphics&, juce::Rectangle<float> body);
    void paintSpectral (juce::Graphics&, juce::Rectangle<float> body);
    void paintRegionAndLoop (juce::Graphics&, juce::Rectangle<float> body);
    void paintRuler (juce::Graphics&);
    void paintPlayheads (juce::Graphics&, juce::Rectangle<float> body);
    void paintGrains (juce::Graphics&, juce::Rectangle<float> body);

    RefractAudioProcessor& processor;
    const int layer;

    juce::Image spectrogram;
    juce::Image overlay;               // darkens the parts you've removed
    refract::SampleDataPtr playback;
    refract::WaveformPeaksPtr peaks;
    bool hasSample = false, busy = false, missing = false;
    juce::String fileName;

    View view = View::waveform;
    Tool tool = Tool::softBrush;
    Mode mode = Mode::remove;
    float brushRadius = 14.0f;
    float strength = 1.0f;
    bool snapToZero = false;

    double viewStart = 0.0, viewEnd = 1.0;   // visible part of the sample, 0..1

    bool painting = false, eraseInverted = false, dragHover = false, mouseInside = false;
    juce::Point<float> lastPos, dragStart, mousePos;

    Marker dragMarker = Marker::none;
    std::vector<refract::spectral::Mask> undoStack, redoStack;
    juce::Random random;
    double loopBarGrabOffset = 0.0, loopBarLength = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralEditor)
};
