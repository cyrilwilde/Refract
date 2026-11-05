#pragma once
//
// SpectralEditor.h
// The paintable spectrogram. Time runs left to right, frequency bottom to top.
// Paint to remove frequencies; switch to Restore (or drag with the right
// mouse button) to bring them back. Drop audio files onto it to load them.
//
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class SpectralEditor : public juce::Component,
                       public juce::FileDragAndDropTarget
{
public:
    enum class Tool { brush, rectangle };
    enum class Mode { remove, restore };

    SpectralEditor (RefractAudioProcessor& processor, int layer);

    void refresh (const RefractAudioProcessor::LayerDisplayInfo& info);   // new picture / mask from the processor
    void rebuildOverlay();                                                // after the whole mask changed

    void setTool (Tool t)           { tool = t; repaint(); }
    void setMode (Mode m)           { mode = m; }
    void setBrushSize (float px)    { brushRadius = px; repaint(); }
    void setStrength (float amount) { strength = amount; }

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true;  repaint(); }
    void fileDragExit (const juce::StringArray&) override            { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    juce::Point<float> toMask (juce::Point<float> p) const;   // component pixels -> mask cells
    Mode effectiveMode() const { return eraseInverted ? (mode == Mode::remove ? Mode::restore : Mode::remove) : mode; }
    void stamp (juce::Point<float> componentPos);
    void strokeTo (juce::Point<float> componentPos);
    void applyRectangle (juce::Rectangle<float> componentArea);
    void writeCell (int x, int y, float amount);                // amount 0..1 = how strongly to apply
    void updateOverlayPixel (int x, int y);

    RefractAudioProcessor& processor;
    const int layer;

    juce::Image spectrogram;
    juce::Image overlay;               // darkens the parts you've removed
    bool hasSample = false, busy = false, missing = false;
    juce::String fileName;

    Tool tool = Tool::brush;
    Mode mode = Mode::remove;
    float brushRadius = 14.0f;
    float strength = 1.0f;

    bool painting = false, eraseInverted = false, dragHover = false, mouseInside = false;
    juce::Point<float> lastPos, dragStart, mousePos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralEditor)
};
