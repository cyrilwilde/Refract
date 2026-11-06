#include "SpectralEditor.h"
#include "RefractLookAndFeel.h"

using namespace refract;
namespace colours = refract::ui::colours;

namespace
{
    // Removed areas are darkened, boosted areas lightened
    juce::Colour overlayColour (uint8_t m)
    {
        if (m <= maskUnity)
            return juce::Colours::black.withAlpha (0.88f * (1.0f - (float) m / (float) maskUnity));
        return juce::Colours::white.withAlpha (0.45f * (float) (m - maskUnity) / (float) (255 - maskUnity));
    }
}

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
    {
        rebuildOverlay();   // a new file means a fresh mask too
        undoStack.clear();
        redoStack.clear();
    }
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
            pixels.setPixelColour (x, maskHeight - 1 - y, overlayColour (mask[(size_t) (y * maskWidth + x)]));
    repaint();
}

void SpectralEditor::updateOverlayPixel (int x, int y)
{
    const auto m = processor.getMask (layer)[(size_t) (y * maskWidth + x)];
    // Mask row 0 is the lowest frequency, but image row 0 is the top - so flip.
    overlay.setPixelAt (x, maskHeight - 1 - y, overlayColour (m));
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

bool SpectralEditor::isGranular() const
{
    return (int) value ("mode") == layerGranular;
}

SpectralEditor::Marker SpectralEditor::hitTestMarker (juce::Point<float> p) const
{
    if (! hasSample)
        return Marker::none;

    // Granular: the bottom half of the ruler holds the grain Position marker (G)
    if (isGranular() && p.y >= (float) rulerHeight * 0.5f && p.y < (float) rulerHeight)
        return Marker::grainPos;

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
        case Marker::grainPos:
            begin ("gpos");
            dragMarkerTo (p);   // jump straight to where you clicked
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
        case Marker::grainPos:
        {
            // Position is relative to the Start..End region
            const float s = value ("start"), e = value ("end");
            setParam ("gpos", juce::jlimit (0.0f, 1.0f, (pos - s) / juce::jmax (0.0001f, e - s)));
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
        case Marker::grainPos:  end ("gpos");      break;
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
    // Remove and Boost use min/max (rather than adding), so going over the same
    // spot twice doesn't keep digging deeper - the result is predictable.
    switch (effectiveMode())
    {
        case Mode::remove:  cell = (uint8_t) juce::jmin ((int) cell, juce::roundToInt ((float) maskUnity * (1.0f - a))); break;
        case Mode::boost:   cell = (uint8_t) juce::jmax ((int) cell, juce::roundToInt ((float) maskUnity + a * (float) (255 - maskUnity))); break;
        case Mode::restore: cell = (uint8_t) juce::roundToInt ((float) cell + ((float) maskUnity - (float) cell) * a); break;
    }
    updateOverlayPixel (x, y);
}

void SpectralEditor::stampCircle (juce::Point<float> c, float rx, float ry, bool soft)
{
    for (int y = (int) std::floor (c.y - ry); y <= (int) std::ceil (c.y + ry); ++y)
        for (int x = (int) std::floor (c.x - rx); x <= (int) std::ceil (c.x + rx); ++x)
        {
            const float dx = ((float) x + 0.5f - c.x) / rx;
            const float dy = ((float) y + 0.5f - c.y) / ry;
            const float d = std::sqrt (dx * dx + dy * dy);
            if (d < 1.0f)   // soft edge: full strength in the middle, fading to 0 at the rim
                writeCell (x, y, soft ? 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * d)) * 1.15f : 1.0f);
        }
}

// Smudge: blends each cell towards the average of its neighbours - softens edges of earlier edits
void SpectralEditor::smudgeAt (juce::Point<float> c, float rx, float ry)
{
    auto& mask = processor.getMask (layer);
    const int x0 = juce::jmax (0, (int) std::floor (c.x - rx)), x1 = juce::jmin (maskWidth - 1, (int) std::ceil (c.x + rx));
    const int y0 = juce::jmax (0, (int) std::floor (c.y - ry)), y1 = juce::jmin (maskHeight - 1, (int) std::ceil (c.y + ry));
    if (x1 < x0 || y1 < y0) return;

    const auto copy = mask;   // read from a snapshot so the blur doesn't smear in one direction
    auto at = [&copy] (int x, int y) { return (float) copy[(size_t) (juce::jlimit (0, maskHeight - 1, y) * maskWidth + juce::jlimit (0, maskWidth - 1, x))]; };
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
        {
            const float dx = ((float) x + 0.5f - c.x) / rx, dy = ((float) y + 0.5f - c.y) / ry;
            const float d = std::sqrt (dx * dx + dy * dy);
            if (d >= 1.0f) continue;
            float sum = 0.0f;
            for (int j = -2; j <= 2; ++j)
                for (int i = -2; i <= 2; ++i)
                    sum += at (x + i, y + j);
            const float avg = sum / 25.0f;
            const float k = (1.0f - d) * strength;
            mask[(size_t) (y * maskWidth + x)] = (uint8_t) juce::roundToInt (at (x, y) + (avg - at (x, y)) * k);
            updateOverlayPixel (x, y);
        }
}

void SpectralEditor::stamp (juce::Point<float> pos)
{
    // The brush is round on screen, which is an ellipse in mask cells because
    // the cells aren't square (and zooming stretches them further).
    const auto body = bodyArea();
    const auto c = toMask (pos);
    const float rx = juce::jmax (0.5f, brushRadius * (float) (viewEnd - viewStart) * (float) maskWidth / juce::jmax (1.0f, body.getWidth()));
    const float ry = juce::jmax (0.5f, brushRadius * (float) maskHeight / juce::jmax (1.0f, body.getHeight()));

    switch (tool)
    {
        case Tool::softBrush: stampCircle (c, rx, ry, true);  break;
        case Tool::hardBrush: stampCircle (c, rx, ry, false); break;

        case Tool::squareBrush:
            for (int y = (int) std::floor (c.y - ry); y <= (int) std::ceil (c.y + ry); ++y)
                for (int x = (int) std::floor (c.x - rx); x <= (int) std::ceil (c.x + rx); ++x)
                    writeCell (x, y, 1.0f);
            break;

        case Tool::spray:   // random speckles inside the circle - texture rather than solid cuts
            for (int y = (int) std::floor (c.y - ry); y <= (int) std::ceil (c.y + ry); ++y)
                for (int x = (int) std::floor (c.x - rx); x <= (int) std::ceil (c.x + rx); ++x)
                {
                    const float dx = ((float) x + 0.5f - c.x) / rx, dy = ((float) y + 0.5f - c.y) / ry;
                    if (dx * dx + dy * dy < 1.0f && random.nextFloat() < 0.18f)
                        writeCell (x, y, random.nextFloat());
                }
            break;

        case Tool::smudge: smudgeAt (c, rx, ry); break;

        case Tool::harmonic:
        {
            // Paint the note under the cursor *and* its overtones (2x, 3x, 4x... its
            // frequency) - removes or isolates a pitched sound in one stroke.
            const float f0 = logPosToFreq (juce::jlimit (0.0f, 1.0f, c.y / (float) (maskHeight - 1)));
            const float rowR = juce::jmax (0.6f, ry * 0.35f);
            for (int h = 1; h <= 40; ++h)
            {
                const float fh = f0 * (float) h;
                if (fh > maxMaskFreq) break;
                const float row = freqToLogPos (fh) * (float) (maskHeight - 1);
                stampCircle ({ c.x, row }, rx, rowR, true);
            }
            break;
        }

        case Tool::freqBand:   // a horizontal band across the whole sample
            for (int y = (int) std::floor (c.y - ry); y <= (int) std::ceil (c.y + ry); ++y)
            {
                const float d = std::abs (((float) y + 0.5f - c.y) / ry);
                if (d >= 1.0f) continue;
                const float f = 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * d)) * 1.15f;
                for (int x = 0; x < maskWidth; ++x)
                    writeCell (x, y, f);
            }
            break;

        case Tool::timeSlice:  // a vertical slice through every frequency
            for (int x = (int) std::floor (c.x - rx); x <= (int) std::ceil (c.x + rx); ++x)
            {
                const float d = std::abs (((float) x + 0.5f - c.x) / rx);
                if (d >= 1.0f) continue;
                const float f = 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * d)) * 1.15f;
                for (int y = 0; y < maskHeight; ++y)
                    writeCell (x, y, f);
            }
            break;

        default: break;   // shapes are applied on mouse-up
    }
}

void SpectralEditor::strokeTo (juce::Point<float> pos)
{
    // Fill in the gap between mouse events so fast strokes stay continuous.
    const float dist = lastPos.getDistanceFrom (pos);
    const float step = juce::jmax (1.0f, brushRadius * (tool == Tool::smudge ? 0.5f : 0.3f));
    const int steps = juce::jmax (1, (int) (dist / step));
    for (int i = 1; i <= steps; ++i)
        stamp (lastPos + (pos - lastPos) * ((float) i / (float) steps));
    lastPos = pos;
}

// Rectangle, ellipse and heart: fill the dragged-out box
void SpectralEditor::applyShape (juce::Rectangle<float> area)
{
    const auto a = toMask (area.getTopLeft());
    const auto b = toMask (area.getBottomRight());
    const float left = juce::jmin (a.x, b.x), right = juce::jmax (a.x, b.x);
    const float bottom = juce::jmin (a.y, b.y), top = juce::jmax (a.y, b.y);
    const float w = juce::jmax (0.5f, right - left), h = juce::jmax (0.5f, top - bottom);

    for (int y = (int) std::floor (bottom); y < (int) std::ceil (top); ++y)
        for (int x = (int) std::floor (left); x < (int) std::ceil (right); ++x)
        {
            const float u = (((float) x + 0.5f - left) / w) * 2.0f - 1.0f;    // -1..1 across
            const float t = ((float) y + 0.5f - bottom) / h;                   //  0..1 bottom to top
            bool inside = true;
            if (tool == Tool::ellipse)
            {
                const float vv = t * 2.0f - 1.0f;
                inside = u * u + vv * vv <= 1.0f;
            }
            else if (tool == Tool::heart)
            {
                // The classic heart curve: (x^2 + y^2 - 1)^3 - x^2 y^3 <= 0
                const float hx = u * 1.2f, hy = -1.05f + t * 2.35f;
                const float q = hx * hx + hy * hy - 1.0f;
                inside = q * q * q - hx * hx * hy * hy * hy <= 0.0f;
            }
            if (inside)
                writeCell (x, y, 1.0f);
        }
}

juce::Path SpectralEditor::shapeOutline (juce::Rectangle<float> r) const
{
    juce::Path p;
    if (tool == Tool::ellipse)
        p.addEllipse (r);
    else if (tool == Tool::heart)
    {
        // Same heart, drawn with its parametric form
        for (int i = 0; i <= 64; ++i)
        {
            const float t = juce::MathConstants<float>::twoPi * (float) i / 64.0f;
            const float hx = 16.0f * std::pow (std::sin (t), 3.0f);
            const float hy = 13.0f * std::cos (t) - 5.0f * std::cos (2.0f * t) - 2.0f * std::cos (3.0f * t) - std::cos (4.0f * t);
            const juce::Point<float> pt (r.getCentreX() + hx / 17.0f * r.getWidth() * 0.5f,
                                         r.getY() + (1.0f - (hy + 17.0f) / 30.0f) * r.getHeight());
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        p.closeSubPath();
    }
    else
        p.addRectangle (r);
    return p;
}

void SpectralEditor::pushUndo()
{
    undoStack.push_back (processor.getMask (layer));
    if (undoStack.size() > 30)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void SpectralEditor::undo()
{
    if (undoStack.empty()) return;
    auto& mask = processor.getMask (layer);
    redoStack.push_back (mask);
    mask = undoStack.back();
    undoStack.pop_back();
    rebuildOverlay();
    processor.maskEdited (layer);
}

void SpectralEditor::redo()
{
    if (redoStack.empty()) return;
    auto& mask = processor.getMask (layer);
    undoStack.push_back (mask);
    mask = redoStack.back();
    redoStack.pop_back();
    rebuildOverlay();
    processor.maskEdited (layer);
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
        pushUndo();
        painting = true;
        eraseInverted = e.mods.isPopupMenu();   // right-click (or ctrl-click) = opposite mode
        lastPos = dragStart = e.position;
        if (! isShapeTool())
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
    if (painting && ! isShapeTool())
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
    if (isShapeTool())
        applyShape (juce::Rectangle<float> (dragStart, e.position));
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
            if (isGranular()) paintGrains (g, body);
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
    if (! isShapeTool() && (mouseInside || painting) && mousePos.y >= body.getY() && dragMarker == Marker::none)
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        const auto circle = juce::Rectangle<float> (brushRadius * 2.0f, brushRadius * 2.0f).withCentre (mousePos);
        if (tool == Tool::freqBand)       g.drawRect (juce::Rectangle<float> (body.getX(), circle.getY(), body.getWidth(), circle.getHeight()), 1.0f);
        else if (tool == Tool::timeSlice) g.drawRect (juce::Rectangle<float> (circle.getX(), body.getY(), circle.getWidth(), body.getHeight()), 1.0f);
        else if (tool == Tool::squareBrush) g.drawRect (circle, 1.0f);
        else                              g.drawEllipse (circle, 1.0f);

        if (tool == Tool::harmonic)   // show where the overtones will land
        {
            const float yNorm = 1.0f - (mousePos.y - body.getY()) / juce::jmax (1.0f, body.getHeight());
            const float f0 = logPosToFreq (juce::jlimit (0.0f, 1.0f, yNorm));
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            for (int h = 2; h <= 40 && f0 * (float) h <= maxMaskFreq; ++h)
            {
                const float y = body.getY() + (1.0f - freqToLogPos (f0 * (float) h)) * body.getHeight();
                g.drawHorizontalLine ((int) y, mousePos.x - brushRadius, mousePos.x + brushRadius);
            }
        }
    }
    if (isShapeTool() && painting)
    {
        const auto outline = shapeOutline (juce::Rectangle<float> (dragStart, mousePos));
        g.setColour (juce::Colours::white.withAlpha (effectiveMode() == Mode::remove ? 0.12f : 0.25f));
        g.fillPath (outline);
        g.setColour (juce::Colours::white);
        g.strokePath (outline, juce::PathStrokeType (1.0f));
    }
}

void SpectralEditor::paintRegionAndLoop (juce::Graphics& g, juce::Rectangle<float> body)
{
    const float xs = normToX (value ("start")),     xe = normToX (value ("end"));
    const float xa = normToX (value ("loopstart")), xb = normToX (value ("loopend"));
    const bool looping = (int) value ("loopmode") != loopOff && ! isGranular();

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

    if (isGranular())
    {
        // Grain Position marker (bottom half): a "G" flag you can drag
        const float s = value ("start"), e = value ("end");
        const float x = normToX (s + value ("gpos") * (e - s));
        const auto flagArea = juce::Rectangle<float> (x - 7.0f, ruler.getY() + half + 1.0f, 14.0f, half - 2.0f);
        g.setColour (colours::text);
        g.fillRoundedRectangle (flagArea, 2.0f);
        g.setColour (colours::background);
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.drawText ("G", flagArea, juce::Justification::centred);
    }

    // Loop bar (bottom half)
    const float xa = normToX (value ("loopstart")), xb = normToX (value ("loopend"));
    const bool looping = (int) value ("loopmode") != loopOff;
    if (! isGranular())
    {
        const auto bar = juce::Rectangle<float>::leftTopRightBottom (xa, ruler.getY() + half + 2.0f, xb, ruler.getBottom() - 2.0f);
        g.setColour (looping ? colours::controlOn : colours::track);
        g.fillRect (bar);
        g.setColour (looping ? colours::text : colours::dimText);
        g.fillRect (juce::Rectangle<float> (xa - 1.0f, bar.getY(), 3.0f, bar.getHeight()));
        g.fillRect (juce::Rectangle<float> (xb - 2.0f, bar.getY(), 3.0f, bar.getHeight()));
    }

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

void SpectralEditor::paintGrains (juce::Graphics& g, juce::Rectangle<float> body)
{
    const float s = value ("start"), e = value ("end");
    const float centre = s + value ("gpos") * (e - s);
    const float spray = value ("gspray") * (e - s);

    // Where grains can come from: Position +/- half the Spray
    const float x0 = normToX (centre - spray * 0.5f), x1 = normToX (centre + spray * 0.5f);
    g.setColour (colours::modulation.withAlpha (0.10f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (x0, x1) - 1.0f, body.getY(), juce::jmax (x0, x1) + 1.0f, body.getBottom()));
    g.setColour (colours::modulation.withAlpha (0.7f));
    g.drawVerticalLine ((int) normToX (centre), body.getY(), body.getBottom());

    // Recent grains as short ticks (only while notes play)
    if (! processor.isNoteActive())
        return;
    g.setColour (colours::playhead.withAlpha (0.45f));
    const float h = body.getHeight() * 0.35f;
    for (int i = 0; i < numGrainMarks; ++i)
    {
        const float pos = processor.getGrainMark (layer, i);
        if (pos >= 0.0f)
            g.fillRect (juce::Rectangle<float> (normToX (pos), body.getCentreY() - h * 0.5f, 1.0f, h));
    }
}
