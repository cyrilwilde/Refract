#include "SpectralEditor.h"
#include "RefractLookAndFeel.h"

using namespace refract;
namespace colours = refract::ui::colours;

SpectralEditor::SpectralEditor (RefractAudioProcessor& p, int layerIndex)
    : processor (p), layer (layerIndex),
      overlay (juce::Image::ARGB, maskWidth, maskHeight, true, juce::SoftwareImageType())
{
    rebuildOverlay();
    startTimerHz (30);
}

SpectralEditor::~SpectralEditor()
{
    stopTimer();
}

void SpectralEditor::refresh (const RefractAudioProcessor::LayerDisplayInfo& info)
{
    const bool pictureChanged = ! (info.spectrogram == spectrogram);
    const int oldLength = playback != nullptr ? playback->audio.getNumSamples() : 0;

    spectrogram = info.spectrogram;
    playback = info.playback;
    peaks = info.peaks;
    hasSample = playback != nullptr && spectrogram.isValid();
    busy = info.busy;
    missing = info.missing;
    fileName = info.fileName;

    if (pictureChanged)
        rebuildOverlay();   // a new file means a fresh mask too
    if (playback != nullptr && playback->audio.getNumSamples() != oldLength)
        zoomToFit();        // different sample - show all of it
    repaint();
}

void SpectralEditor::rebuildOverlay()
{
    juce::Image::BitmapData pixels (overlay, juce::Image::BitmapData::writeOnly);
    const auto& mask = processor.getMask (layer);
    for (int y = 0; y < maskHeight; ++y)
        for (int x = 0; x < maskWidth; ++x)
        {
            const float removed = 1.0f - (float) mask[(size_t) (y * maskWidth + x)] / 255.0f;
            pixels.setPixelColour (x, maskHeight - 1 - y, juce::Colours::black.withAlpha (removed * 0.88f));
        }
    repaint();
}

void SpectralEditor::updateOverlayPixel (int x, int y)
{
    const auto m = processor.getMask (layer)[(size_t) (y * maskWidth + x)];
    const float removed = 1.0f - (float) m / 255.0f;
    // Mask row 0 is the lowest frequency, but image row 0 is the top - so flip.
    overlay.setPixelAt (x, maskHeight - 1 - y, juce::Colours::black.withAlpha (removed * 0.88f));
}

//==============================================================================
//  Coordinates and zoom
//==============================================================================
double SpectralEditor::xToNorm (float x) const
{
    const float w = (float) juce::jmax (1, getWidth());
    return viewStart + (double) (x / w) * (viewEnd - viewStart);
}

float SpectralEditor::normToX (double n) const
{
    return (float) ((n - viewStart) / (viewEnd - viewStart)) * (float) getWidth();
}

void SpectralEditor::zoomAround (double anchor, double factor)
{
    const double len = playback != nullptr ? (double) playback->audio.getNumSamples() : 1.0;
    const double minSpan = juce::jlimit (0.0001, 1.0, 64.0 / len);   // at most ~64 samples across the view
    const double span = juce::jlimit (minSpan, 1.0, (viewEnd - viewStart) * factor);

    // Keep the point under the mouse where it is
    const double t = (anchor - viewStart) / (viewEnd - viewStart);
    viewStart = juce::jlimit (0.0, 1.0 - span, anchor - t * span);
    viewEnd = viewStart + span;
    repaint();
}

void SpectralEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! hasSample)
        return;
    if (std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        // Horizontal scroll pans along the sample
        const double span = viewEnd - viewStart;
        viewStart = juce::jlimit (0.0, 1.0 - span, viewStart - (double) wheel.deltaX * span * 0.5);
        viewEnd = viewStart + span;
        repaint();
    }
    else if (wheel.deltaY != 0.0f)
    {
        zoomAround (xToNorm (e.position.x), std::exp (-(double) wheel.deltaY * 1.5));   // scroll up = zoom in
    }
}

void SpectralEditor::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (hasSample && scaleFactor > 0.0f)
        zoomAround (xToNorm (e.position.x), 1.0 / (double) scaleFactor);   // trackpad pinch
}

void SpectralEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.position.y < (float) rulerHeight)
        zoomToFit();   // double-click the ruler to see the whole sample
}

//==============================================================================
//  Markers
//==============================================================================
juce::RangedAudioParameter* SpectralEditor::param (const char* name) const
{
    return processor.apvts.getParameter (ids::layer (layer, name));
}

float SpectralEditor::value (const char* name) const
{
    return processor.apvts.getRawParameterValue (ids::layer (layer, name))->load();
}

void SpectralEditor::setParam (const char* name, float v)
{
    if (auto* p = param (name))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

SpectralEditor::Marker SpectralEditor::hitTestMarker (juce::Point<float> p) const
{
    if (! hasSample)
        return Marker::none;

    const float xs = normToX (value ("start")),     xe = normToX (value ("end"));
    const float xa = normToX (value ("loopstart")), xb = normToX (value ("loopend"));
    constexpr float grab = 6.0f;

    if (p.y < (float) rulerHeight)
    {
        if (p.y < (float) rulerHeight * 0.5f)   // top half: Start / End flags
        {
            const float ds = std::abs (p.x - xs), de = std::abs (p.x - xe);
            if (ds <= grab || de <= grab) return ds <= de ? Marker::start : Marker::end;
            return Marker::none;
        }
        // bottom half: the loop bar
        const float da = std::abs (p.x - xa), db = std::abs (p.x - xb);
        if (da <= grab || db <= grab) return da <= db ? Marker::loopStart : Marker::loopEnd;
        if (p.x > juce::jmin (xa, xb) && p.x < juce::jmax (xa, xb)) return Marker::loopBar;
        return Marker::none;
    }

    // In the waveform view the marker lines themselves can be grabbed too
    if (view == View::waveform)
    {
        const std::pair<Marker, float> lines[] = { { Marker::start, xs }, { Marker::end, xe },
                                                   { Marker::loopStart, xa }, { Marker::loopEnd, xb } };
        Marker best = Marker::none;
        float bestDist = grab;
        for (const auto& [m, x] : lines)
            if (std::abs (p.x - x) <= bestDist) { best = m; bestDist = std::abs (p.x - x); }
        return best;
    }
    return Marker::none;
}

double SpectralEditor::snap (double norm) const
{
    if (! snapToZero || playback == nullptr)
        return norm;

    // Look for the nearest place the waveform crosses zero - cuts and loop
    // points there don't click.
    const auto& audio = playback->audio;
    const int len = audio.getNumSamples();
    const int centre = juce::jlimit (0, len - 1, (int) (norm * len));
    const int radius = juce::jmax (256, (int) ((viewEnd - viewStart) * len * 0.01));
    const float* d = audio.getReadPointer (0);
    for (int off = 0; off < radius; ++off)
        for (int i : { centre - off, centre + off })
            if (i > 0 && i < len && ((d[i - 1] <= 0.0f && d[i] > 0.0f) || (d[i - 1] >= 0.0f && d[i] < 0.0f)))
                return (double) i / (double) len;
    return norm;
}

void SpectralEditor::beginMarkerDrag (Marker m, juce::Point<float> p)
{
    dragMarker = m;
    auto begin = [this] (const char* n) { if (auto* pr = param (n)) pr->beginChangeGesture(); };
    switch (m)
    {
        case Marker::start:     begin ("start");     break;
        case Marker::end:       begin ("end");       break;
        case Marker::loopStart: begin ("loopstart"); break;
        case Marker::loopEnd:   begin ("loopend");   break;
        case Marker::loopBar:
            begin ("loopstart"); begin ("loopend");
            loopBarGrabOffset = xToNorm (p.x) - value ("loopstart");
            loopBarLength = value ("loopend") - value ("loopstart");
            break;
        case Marker::none: break;
    }
}

void SpectralEditor::dragMarkerTo (juce::Point<float> p)
{
    const double len = playback != nullptr ? (double) playback->audio.getNumSamples() : 1.0;
    const float minGap = (float) juce::jmax (0.0005, 64.0 / len);   // markers can't cross each other
    const float pos = (float) juce::jlimit (0.0, 1.0, snap (xToNorm (p.x)));

    switch (dragMarker)
    {
        case Marker::start:     setParam ("start",     juce::jmin (pos, value ("end") - minGap)); break;
        case Marker::end:       setParam ("end",       juce::jmax (pos, value ("start") + minGap)); break;
        case Marker::loopStart: setParam ("loopstart", juce::jmin (pos, value ("loopend") - minGap)); break;
        case Marker::loopEnd:   setParam ("loopend",   juce::jmax (pos, value ("loopstart") + minGap)); break;
        case Marker::loopBar:
        {
            const double newStart = juce::jlimit (0.0, 1.0 - loopBarLength, snap (xToNorm (p.x) - loopBarGrabOffset));
            setParam ("loopstart", (float) newStart);
            setParam ("loopend",   (float) (newStart + loopBarLength));
            break;
        }
        case Marker::none: break;
    }
    repaint();
}

void SpectralEditor::endMarkerDrag()
{
    auto end = [this] (const char* n) { if (auto* pr = param (n)) pr->endChangeGesture(); };
    switch (dragMarker)
    {
        case Marker::start:     end ("start");     break;
        case Marker::end:       end ("end");       break;
        case Marker::loopStart: end ("loopstart"); break;
        case Marker::loopEnd:   end ("loopend");   break;
        case Marker::loopBar:   end ("loopstart"); end ("loopend"); break;
        case Marker::none: break;
    }
    dragMarker = Marker::none;
}

//==============================================================================
//  Painting the spectral mask
//==============================================================================
juce::Point<float> SpectralEditor::toMask (juce::Point<float> p) const
{
    const auto body = bodyArea();
    const float yNorm = 1.0f - (p.y - body.getY()) / juce::jmax (1.0f, body.getHeight());
    return { (float) xToNorm (p.x) * (float) maskWidth, yNorm * (float) maskHeight };
}

void SpectralEditor::writeCell (int x, int y, float amount)
{
    if (x < 0 || y < 0 || x >= maskWidth || y >= maskHeight || amount <= 0.0f)
        return;

    auto& cell = processor.getMask (layer)[(size_t) (y * maskWidth + x)];
    const float a = juce::jmin (1.0f, amount * strength);
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
    // The brush is round on screen, which is an ellipse in mask cells because
    // the cells aren't square (and zooming stretches them further).
    const auto body = bodyArea();
    const auto c = toMask (pos);
    const float rx = juce::jmax (0.5f, brushRadius * (float) (viewEnd - viewStart) * (float) maskWidth / juce::jmax (1.0f, body.getWidth()));
    const float ry = juce::jmax (0.5f, brushRadius * (float) maskHeight / juce::jmax (1.0f, body.getHeight()));

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
//  Mouse
//==============================================================================
void SpectralEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! hasSample)
        return;
    mousePos = e.position;

    const auto marker = hitTestMarker (e.position);
    if (marker != Marker::none)
    {
        beginMarkerDrag (marker, e.position);
        return;
    }

    if (view == View::spectral && e.position.y >= (float) rulerHeight)
    {
        painting = true;
        eraseInverted = e.mods.isPopupMenu();   // right-click (or ctrl-click) = opposite mode
        lastPos = dragStart = e.position;
        if (tool == Tool::brush)
            stamp (e.position);
        repaint();
    }
}

void SpectralEditor::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (dragMarker != Marker::none)
    {
        dragMarkerTo (e.position);
        return;
    }
    if (painting && tool == Tool::brush)
        strokeTo (e.position);
    repaint();
}

void SpectralEditor::mouseUp (const juce::MouseEvent& e)
{
    if (dragMarker != Marker::none)
    {
        endMarkerDrag();
        return;
    }
    if (! painting)
        return;
    if (tool == Tool::rectangle)
        applyRectangle (juce::Rectangle<float> (dragStart, e.position));
    painting = false;
    processor.maskEdited (layer);   // re-render the sample in the background
    repaint();
}

void SpectralEditor::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    mouseInside = true;

    const auto m = hitTestMarker (e.position);
    if (m == Marker::loopBar)
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else if (m != Marker::none)
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (view == View::spectral && hasSample && e.position.y >= (float) rulerHeight)
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void SpectralEditor::mouseExit (const juce::MouseEvent&) { mouseInside = false; }

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
//  Drawing
//==============================================================================
void SpectralEditor::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto body = bodyArea();

    g.setColour (colours::background);
    g.fillRect (area);

    if (! hasSample)
    {
        g.setColour (colours::dimText);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        const auto msg = busy    ? juce::String ("Loading...")
                       : missing ? "File not found: " + fileName + "\nDrop the file here again, or click Load."
                                 : juce::String ("Drop an audio file here, or click Load");
        g.drawFittedText (msg, getLocalBounds().reduced (20), juce::Justification::centred, 3);
    }
    else
    {
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (body.toNearestInt());
            if (view == View::waveform) paintWaveform (g, body);
            else                        paintSpectral (g, body);
            paintRegionAndLoop (g, body);
            paintPlayheads (g, body);
        }
        paintRuler (g);
    }

    if (busy && hasSample)
    {
        g.setColour (colours::text);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("Rendering...", getLocalBounds().reduced (8).withTrimmedTop (rulerHeight), juce::Justification::topRight);
    }

    g.setColour (dragHover ? colours::text : colours::panelEdge);
    g.drawRect (area, dragHover ? 2.0f : 1.0f);
}

void SpectralEditor::paintWaveform (juce::Graphics& g, juce::Rectangle<float> body)
{
    const auto& audio = playback->audio;
    const int len = audio.getNumSamples();
    const int numCh = audio.getNumChannels();
    const float mid = body.getCentreY(), halfH = body.getHeight() * 0.48f;

    g.setColour (colours::panelEdge);
    g.drawHorizontalLine ((int) mid, body.getX(), body.getRight());

    g.setColour (colours::waveform);
    const int w = getWidth();
    for (int x = 0; x < w; ++x)
    {
        const double n0 = xToNorm ((float) x) * len, n1 = xToNorm ((float) (x + 1)) * len;
        float lo = 0.0f, hi = 0.0f;

        if (peaks != nullptr && n1 - n0 >= (double) WaveformPeaks::bucketSize)
        {
            // Zoomed out: use the pre-computed min/max blocks
            const int b0 = juce::jlimit (0, (int) peaks->lo.size() - 1, (int) (n0 / WaveformPeaks::bucketSize));
            const int b1 = juce::jlimit (b0 + 1, (int) peaks->lo.size(), (int) (n1 / WaveformPeaks::bucketSize));
            for (int b = b0; b < b1; ++b) { lo = juce::jmin (lo, peaks->lo[(size_t) b]); hi = juce::jmax (hi, peaks->hi[(size_t) b]); }
        }
        else
        {
            // Zoomed in: read the actual samples
            const int i0 = juce::jlimit (0, len - 1, (int) n0);
            const int i1 = juce::jlimit (i0 + 1, len, (int) std::ceil (n1) + 1);
            lo = hi = audio.getSample (0, i0);
            for (int ch = 0; ch < numCh; ++ch)
                for (int i = i0; i < i1; ++i)
                {
                    const float v = audio.getSample (ch, i);
                    lo = juce::jmin (lo, v); hi = juce::jmax (hi, v);
                }
        }

        const float top = mid - juce::jlimit (-1.0f, 1.0f, hi) * halfH;
        const float bottom = mid - juce::jlimit (-1.0f, 1.0f, lo) * halfH;
        g.drawVerticalLine (x, top, juce::jmax (bottom, top + 1.0f));
    }
}

void SpectralEditor::paintSpectral (juce::Graphics& g, juce::Rectangle<float> body)
{
    // Stretch the full-length spectrogram so the visible part fills the view
    const float imgW = (float) spectrogram.getWidth(), imgH = (float) spectrogram.getHeight();
    const float sx = body.getWidth() / ((float) (viewEnd - viewStart) * imgW);
    const float sy = body.getHeight() / imgH;
    const auto t = juce::AffineTransform::scale (sx, sy)
                       .translated (body.getX() - (float) viewStart * imgW * sx, body.getY());

    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.drawImageTransformed (spectrogram, t);
    g.drawImageTransformed (overlay, t);

    // Frequency grid
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float y = body.getY() + (1.0f - freqToLogPos (f)) * body.getHeight();
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) y, body.getX(), body.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String ((int) f),
                    4, (int) y - 12, 40, 12, juce::Justification::left);
    }

    // Tool preview
    if (tool == Tool::brush && (mouseInside || painting) && mousePos.y >= body.getY() && dragMarker == Marker::none)
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawEllipse (juce::Rectangle<float> (brushRadius * 2.0f, brushRadius * 2.0f).withCentre (mousePos), 1.0f);
    }
    if (tool == Tool::rectangle && painting)
    {
        const juce::Rectangle<float> r (dragStart, mousePos);
        g.setColour (juce::Colours::white.withAlpha (effectiveMode() == Mode::remove ? 0.12f : 0.25f));
        g.fillRect (r);
        g.setColour (juce::Colours::white);
        g.drawRect (r, 1.0f);
    }
}

void SpectralEditor::paintRegionAndLoop (juce::Graphics& g, juce::Rectangle<float> body)
{
    const float xs = normToX (value ("start")),     xe = normToX (value ("end"));
    const float xa = normToX (value ("loopstart")), xb = normToX (value ("loopend"));
    const bool looping = (int) value ("loopmode") != loopOff;

    // Dim everything outside Start..End - it won't be played
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (body.getX(), body.getY(), juce::jmax (body.getX(), xs), body.getBottom()));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (body.getRight(), xe), body.getY(), body.getRight(), body.getBottom()));

    if (looping)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xa, body.getY(), xb, body.getBottom()));

        // Crossfade ramp: it fades in the audio from just before the loop start,
        // approaching the loop end (or the mirror of that when reversed).
        const float xf = value ("xfade");
        if (xf > 0.0f)
        {
            const bool rev = value ("reverse") > 0.5f;
            const double loopLen = value ("loopend") - value ("loopstart");
            const double avail = rev ? 1.0 - value ("loopend") : (double) value ("loopstart");
            const double fadeLen = juce::jmin ((double) xf * loopLen, avail);
            const float x0 = rev ? xa : normToX (value ("loopend") - fadeLen);
            const float x1 = rev ? normToX (value ("loopstart") + fadeLen) : xb;
            juce::Path ramp;
            if (rev) { ramp.startNewSubPath (x0, body.getY() + 2.0f); ramp.lineTo (x1, body.getBottom() - 2.0f); }
            else     { ramp.startNewSubPath (x0, body.getBottom() - 2.0f); ramp.lineTo (x1, body.getY() + 2.0f); }
            g.setColour (colours::text.withAlpha (0.6f));
            g.strokePath (ramp, juce::PathStrokeType (1.0f));
        }

        g.setColour (colours::dimText);
        g.drawVerticalLine ((int) xa, body.getY(), body.getBottom());
        g.drawVerticalLine ((int) xb, body.getY(), body.getBottom());
    }

    g.setColour (colours::text);
    g.drawVerticalLine ((int) xs, body.getY(), body.getBottom());
    g.drawVerticalLine ((int) xe, body.getY(), body.getBottom());
}

void SpectralEditor::paintRuler (juce::Graphics& g)
{
    const auto ruler = getLocalBounds().toFloat().removeFromTop ((float) rulerHeight);
    const float half = ruler.getHeight() * 0.5f;
    g.setColour (colours::panel);
    g.fillRect (ruler);

    // Loop bar (bottom half)
    const float xa = normToX (value ("loopstart")), xb = normToX (value ("loopend"));
    const bool looping = (int) value ("loopmode") != loopOff;
    const auto bar = juce::Rectangle<float>::leftTopRightBottom (xa, ruler.getY() + half + 2.0f, xb, ruler.getBottom() - 2.0f);
    g.setColour (looping ? colours::controlOn : colours::track);
    g.fillRect (bar);
    g.setColour (looping ? colours::text : colours::dimText);
    g.fillRect (juce::Rectangle<float> (xa - 1.0f, bar.getY(), 3.0f, bar.getHeight()));
    g.fillRect (juce::Rectangle<float> (xb - 2.0f, bar.getY(), 3.0f, bar.getHeight()));

    // Start / End flags (top half)
    g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
    auto flag = [&] (float x, const juce::String& label, bool pointsRight)
    {
        juce::Path tri;
        const float y0 = ruler.getY() + 1.0f, y1 = ruler.getY() + half;
        if (pointsRight) tri.addTriangle (x, y0, x + 10.0f, y0 + (y1 - y0) * 0.5f, x, y1);
        else             tri.addTriangle (x, y0, x - 10.0f, y0 + (y1 - y0) * 0.5f, x, y1);
        g.setColour (colours::text);
        g.fillPath (tri);
        g.setColour (colours::background);
        g.drawText (label, juce::Rectangle<float> (pointsRight ? x : x - 9.0f, y0, 9.0f, y1 - y0), juce::Justification::centred);
    };
    flag (normToX (value ("start")), "S", true);
    flag (normToX (value ("end")),   "E", false);

    g.setColour (colours::panelEdge);
    g.drawHorizontalLine ((int) ruler.getBottom() - 1, ruler.getX(), ruler.getRight());
}

void SpectralEditor::paintPlayheads (juce::Graphics& g, juce::Rectangle<float> body)
{
    g.setColour (colours::playhead.withAlpha (0.85f));
    for (int v = 0; v < numVoices; ++v)
    {
        const float pos = processor.getPlayhead (layer, v);
        if (pos >= 0.0f)
            g.fillRect (juce::Rectangle<float> (normToX (pos) - 0.5f, body.getY(), 1.5f, body.getHeight()));
    }
}
