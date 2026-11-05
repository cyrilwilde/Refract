#include "SpectralEditor.h"
#include "RefractLookAndFeel.h"

using namespace refract;

SpectralEditor::SpectralEditor (RefractAudioProcessor& p, int layerIndex)
    : processor (p), layer (layerIndex),
      overlay (juce::Image::ARGB, maskWidth, maskHeight, true, juce::SoftwareImageType())
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    rebuildOverlay();
}

void SpectralEditor::refresh (const RefractAudioProcessor::LayerDisplayInfo& info)
{
    const bool pictureChanged = ! (info.spectrogram == spectrogram);
    spectrogram = info.spectrogram;
    hasSample = spectrogram.isValid();
    busy = info.busy;
    missing = info.missing;
    fileName = info.fileName;
    if (pictureChanged)
        rebuildOverlay();   // a new file means a fresh mask too
    repaint();
}

void SpectralEditor::rebuildOverlay()
{
    for (int y = 0; y < maskHeight; ++y)
        for (int x = 0; x < maskWidth; ++x)
            updateOverlayPixel (x, y);
    repaint();
}

void SpectralEditor::updateOverlayPixel (int x, int y)
{
    const auto m = processor.getMask (layer)[(size_t) (y * maskWidth + x)];
    const float removed = 1.0f - (float) m / 255.0f;
    // Mask row 0 is the lowest frequency, but image row 0 is the top - so flip.
    overlay.setPixelAt (x, maskHeight - 1 - y, juce::Colour (0xff05050a).withAlpha (removed * 0.88f));
}

juce::Point<float> SpectralEditor::toMask (juce::Point<float> p) const
{
    const float w = (float) juce::jmax (1, getWidth()), h = (float) juce::jmax (1, getHeight());
    return { p.x / w * (float) maskWidth, (1.0f - p.y / h) * (float) maskHeight };
}

void SpectralEditor::writeCell (int x, int y, float amount)
{
    if (x < 0 || y < 0 || x >= maskWidth || y >= maskHeight || amount <= 0.0f)
        return;

    auto& cell = processor.getMask (layer)[(size_t) (y * maskWidth + x)];
    const float a = amount * strength;
    // Using min/max (rather than adding) means going over the same spot twice
    // doesn't keep digging deeper - the result is predictable.
    if (effectiveMode() == Mode::remove)
        cell = (uint8_t) juce::jmin ((int) cell, juce::roundToInt (255.0f * (1.0f - a)));
    else
        cell = (uint8_t) juce::jmax ((int) cell, juce::roundToInt (255.0f * a));
    updateOverlayPixel (x, y);
}

void SpectralEditor::stamp (juce::Point<float> pos)
{
    // The brush is round on screen, which is an ellipse in mask cells
    // because the cells aren't square.
    const auto c = toMask (pos);
    const float rx = brushRadius * (float) maskWidth  / (float) juce::jmax (1, getWidth());
    const float ry = brushRadius * (float) maskHeight / (float) juce::jmax (1, getHeight());

    for (int y = (int) std::floor (c.y - ry); y <= (int) std::ceil (c.y + ry); ++y)
        for (int x = (int) std::floor (c.x - rx); x <= (int) std::ceil (c.x + rx); ++x)
        {
            const float dx = ((float) x + 0.5f - c.x) / rx;
            const float dy = ((float) y + 0.5f - c.y) / ry;
            const float d = std::sqrt (dx * dx + dy * dy);
            if (d < 1.0f)   // soft edge: full strength in the middle, fading to 0 at the rim
                writeCell (x, y, 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * d)) * 1.15f);
        }
}

void SpectralEditor::strokeTo (juce::Point<float> pos)
{
    // Fill in the gap between mouse events so fast strokes stay continuous.
    const float dist = lastPos.getDistanceFrom (pos);
    const float step = juce::jmax (1.0f, brushRadius * 0.3f);
    const int steps = juce::jmax (1, (int) (dist / step));
    for (int i = 1; i <= steps; ++i)
        stamp (lastPos + (pos - lastPos) * ((float) i / (float) steps));
    lastPos = pos;
}

void SpectralEditor::applyRectangle (juce::Rectangle<float> area)
{
    const auto a = toMask (area.getTopLeft());
    const auto b = toMask (area.getBottomRight());
    const int x0 = (int) std::floor (juce::jmin (a.x, b.x)), x1 = (int) std::ceil (juce::jmax (a.x, b.x));
    const int y0 = (int) std::floor (juce::jmin (a.y, b.y)), y1 = (int) std::ceil (juce::jmax (a.y, b.y));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            writeCell (x, y, 1.0f);
}

//==============================================================================
void SpectralEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! hasSample)
        return;
    painting = true;
    eraseInverted = e.mods.isPopupMenu();   // right-click (or ctrl-click) = opposite mode
    lastPos = dragStart = mousePos = e.position;
    if (tool == Tool::brush)
        stamp (e.position);
    repaint();
}

void SpectralEditor::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (! painting)
        return;
    if (tool == Tool::brush)
        strokeTo (e.position);
    repaint();
}

void SpectralEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! painting)
        return;
    if (tool == Tool::rectangle)
        applyRectangle (juce::Rectangle<float> (dragStart, e.position));
    painting = false;
    processor.maskEdited (layer);   // re-render the sample in the background
    repaint();
}

void SpectralEditor::mouseMove (const juce::MouseEvent& e) { mousePos = e.position; mouseInside = true; repaint(); }
void SpectralEditor::mouseExit (const juce::MouseEvent&)   { mouseInside = false; repaint(); }

bool SpectralEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (processor.formatManager.findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void SpectralEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (processor.formatManager.findFormatForFileExtension (file.getFileExtension()) != nullptr)
        {
            processor.loadSample (layer, file);
            break;
        }
    }
    repaint();
}

//==============================================================================
void SpectralEditor::paint (juce::Graphics& g)
{
    namespace c = refract::ui::colours;
    const auto area = getLocalBounds().toFloat();

    g.setColour (juce::Colour (0xff07080d));
    g.fillRoundedRectangle (area, 6.0f);

    if (hasSample)
    {
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImage (spectrogram, area);
        g.drawImage (overlay, area);
    }
    else
    {
        g.setColour (c::dimText);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        const auto msg = missing ? "File not found: " + fileName + "\nDrop the file here again, or click Load."
                                 : juce::String ("Drop an audio file here, or click Load");
        g.drawFittedText (msg, getLocalBounds().reduced (20), juce::Justification::centred, 3);
    }

    // Frequency grid
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float y = (1.0f - freqToLogPos (f)) * area.getHeight();
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) y, 0.0f, area.getWidth());
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String ((int) f),
                    4, (int) y - 12, 40, 12, juce::Justification::left);
    }

    // Tool preview
    if (hasSample && tool == Tool::brush && (mouseInside || painting))
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawEllipse (juce::Rectangle<float> (brushRadius * 2.0f, brushRadius * 2.0f).withCentre (mousePos), 1.0f);
    }
    if (hasSample && tool == Tool::rectangle && painting)
    {
        const juce::Rectangle<float> r (dragStart, mousePos);
        g.setColour ((effectiveMode() == Mode::remove ? c::accent2 : c::accent).withAlpha (0.2f));
        g.fillRect (r);
        g.setColour (effectiveMode() == Mode::remove ? c::accent2 : c::accent);
        g.drawRect (r, 1.0f);
    }

    if (busy)
    {
        g.setColour (c::accent);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("Rendering...", getLocalBounds().reduced (8, 6), juce::Justification::topRight);
    }

    if (dragHover)
    {
        g.setColour (c::accent);
        g.drawRoundedRectangle (area.reduced (1.5f), 6.0f, 3.0f);
    }
    else
    {
        g.setColour (c::panelEdge);
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);
    }
}
