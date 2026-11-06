#include "SpectralProcessor.h"

namespace refract::spectral
{
    namespace
    {
        // Periodic Hann window. Windowing each frame before the FFT avoids the
        // "edges" of the frame showing up as fake frequencies.
        std::vector<float> makeHann()
        {
            std::vector<float> w ((size_t) fftSize);
            for (int n = 0; n < fftSize; ++n)
                w[(size_t) n] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) fftSize));
            return w;
        }

        // Look up the mask between grid cells (bilinear interpolation), so edits
        // sound smooth instead of stepping from cell to cell.
        float sampleMask (const Mask& m, float x, float y)
        {
            x = juce::jlimit (0.0f, (float) (maskWidth - 1),  x);
            y = juce::jlimit (0.0f, (float) (maskHeight - 1), y);
            const int x0 = (int) x, y0 = (int) y;
            const int x1 = juce::jmin (x0 + 1, maskWidth - 1);
            const int y1 = juce::jmin (y0 + 1, maskHeight - 1);
            const float fx = x - (float) x0, fy = y - (float) y0;

            auto at = [&m] (int xx, int yy) { return (float) m[(size_t) (yy * maskWidth + xx)] * (1.0f / 255.0f); };
            const float bottom = at (x0, y0) + fx * (at (x1, y0) - at (x0, y0));
            const float top    = at (x0, y1) + fx * (at (x1, y1) - at (x0, y1));
            return bottom + fy * (top - bottom);
        }

        // JUCE's FFT backends may scale the inverse differently, so measure the
        // round-trip gain once instead of assuming it.
        float measureRoundTripScale (const juce::dsp::FFT& fft)
        {
            std::vector<float> d ((size_t) fftSize * 2, 0.0f);
            d[0] = 1.0f;
            fft.performRealOnlyForwardTransform (d.data());
            fft.performRealOnlyInverseTransform (d.data());
            return std::abs (d[0]) > 1.0e-9f ? 1.0f / d[0] : 1.0f;
        }

        // Black -> greys -> white (quiet frequencies dark, loud ones bright)
        juce::Colour heat (float v)
        {
            static const juce::Colour stops[] = { juce::Colour (0xff000000), juce::Colour (0xff141414),
                                                  juce::Colour (0xff3a3a3a), juce::Colour (0xff767676),
                                                  juce::Colour (0xffbdbdbd), juce::Colour (0xffffffff) };
            constexpr int numStops = (int) (sizeof (stops) / sizeof (stops[0]));
            v = juce::jlimit (0.0f, 1.0f, v) * (float) (numStops - 1);
            const int i = juce::jmin ((int) v, numStops - 2);
            return stops[i].interpolatedWith (stops[i + 1], v - (float) i);
        }
    }

    bool isMaskEmpty (const Mask& mask)
    {
        for (auto v : mask)
            if (v != 255)
                return false;
        return true;
    }

    juce::AudioBuffer<float> applyMask (const juce::AudioBuffer<float>& input, double sampleRate,
                                        const Mask& mask, const std::function<bool()>& shouldAbort)
    {
        const int numSamples  = input.getNumSamples();
        const int numChannels = input.getNumChannels();
        if (numSamples == 0 || numChannels == 0)
            return {};

        // Pad both ends by one frame so the very start and end are fully covered.
        const int pad       = fftSize;
        const int paddedLen = numSamples + 2 * pad;
        const int numFrames = (paddedLen - fftSize) / hopSize + 1;

        juce::dsp::FFT fft (fftOrder);
        const auto  window = makeHann();
        const float scale  = measureRoundTripScale (fft);

        // Which (fractional) mask row each FFT bin falls on.
        constexpr int numBins = fftSize / 2 + 1;
        std::vector<float> binRow ((size_t) numBins);
        for (int k = 0; k < numBins; ++k)
        {
            const float freq = (float) k * (float) sampleRate / (float) fftSize;
            binRow[(size_t) k] = freqToLogPos (freq) * (float) (maskHeight - 1);
        }

        // Overlap-add bookkeeping: sum of squared windows at each sample, used to
        // undo the amplitude change caused by overlapping windowed frames.
        std::vector<float> norm ((size_t) paddedLen, 0.0f);
        for (int f = 0; f < numFrames; ++f)
            for (int n = 0; n < fftSize; ++n)
                norm[(size_t) (f * hopSize + n)] += window[(size_t) n] * window[(size_t) n];

        std::vector<std::vector<float>> accum ((size_t) numChannels, std::vector<float> ((size_t) paddedLen, 0.0f));
        std::vector<float> fftData ((size_t) fftSize * 2);
        std::vector<float> gains ((size_t) numBins);

        for (int f = 0; f < numFrames; ++f)
        {
            if ((f & 31) == 0 && shouldAbort && shouldAbort())
                return {};   // a newer edit arrived - stop wasting time on this one

            const int frameStart = f * hopSize - pad;   // position in the original sample

            // 1. Work out this frame's gain per frequency bin from the mask column.
            const float centre = (float) (frameStart + fftSize / 2) / (float) numSamples;
            const float column = juce::jlimit (0.0f, 1.0f, centre) * (float) (maskWidth - 1);
            for (int k = 0; k < numBins; ++k)
                gains[(size_t) k] = sampleMask (mask, column, binRow[(size_t) k]);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* in = input.getReadPointer (ch);

                // 2. Copy a windowed slice of audio in and transform it to frequencies.
                for (int n = 0; n < fftSize; ++n)
                {
                    const int idx = frameStart + n;
                    const float s = (idx >= 0 && idx < numSamples) ? in[idx] : 0.0f;
                    fftData[(size_t) n] = s * window[(size_t) n];
                }
                std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
                fft.performRealOnlyForwardTransform (fftData.data());

                // 3. Scale each bin by the mask. The spectrum is stored as interleaved
                //    (real, imag) pairs; bins above Nyquist mirror those below, so we
                //    scale the mirror too to keep the result a real signal.
                for (int k = 0; k < numBins; ++k)
                {
                    const float g = gains[(size_t) k];
                    fftData[(size_t) (2 * k)]     *= g;
                    fftData[(size_t) (2 * k + 1)] *= g;
                    if (k > 0 && k < fftSize / 2)
                    {
                        const int mirror = fftSize - k;
                        fftData[(size_t) (2 * mirror)]     *= g;
                        fftData[(size_t) (2 * mirror + 1)] *= g;
                    }
                }

                // 4. Back to audio, window again, and overlap-add into the output.
                fft.performRealOnlyInverseTransform (fftData.data());
                auto& acc = accum[(size_t) ch];
                const int outStart = f * hopSize;
                for (int n = 0; n < fftSize; ++n)
                    acc[(size_t) (outStart + n)] += fftData[(size_t) n] * scale * window[(size_t) n];
            }
        }

        juce::AudioBuffer<float> out (numChannels, numSamples);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* dst = out.getWritePointer (ch);
            const auto& acc = accum[(size_t) ch];
            for (int i = 0; i < numSamples; ++i)
                dst[i] = acc[(size_t) (i + pad)] / juce::jmax (norm[(size_t) (i + pad)], 1.0e-6f);
        }
        return out;
    }

    juce::Image renderSpectrogram (const juce::AudioBuffer<float>& input, double sampleRate)
    {
        juce::Image img (juce::Image::RGB, maskWidth, maskHeight, true, juce::SoftwareImageType());
        const int numSamples  = input.getNumSamples();
        const int numChannels = input.getNumChannels();
        if (numSamples == 0 || numChannels == 0)
            return img;

        juce::dsp::FFT fft (fftOrder);
        const auto window = makeHann();
        std::vector<float> fftData ((size_t) fftSize * 2);

        // A full-scale sine through a Hann window peaks at about fftSize/4.
        const float normaliser = 4.0f / (float) fftSize;
        constexpr float floorDb = -90.0f;
        const float binsPerHz = (float) fftSize / (float) sampleRate;

        juce::Image::BitmapData pixels (img, juce::Image::BitmapData::writeOnly);

        for (int x = 0; x < maskWidth; ++x)
        {
            // One FFT per image column, centred on that column's moment in time.
            const int centre = (int) (((float) x + 0.5f) / (float) maskWidth * (float) numSamples);
            const int start  = centre - fftSize / 2;

            for (int n = 0; n < fftSize; ++n)
            {
                const int idx = start + n;
                float s = 0.0f;
                if (idx >= 0 && idx < numSamples)
                    for (int ch = 0; ch < numChannels; ++ch)
                        s += input.getSample (ch, idx);
                fftData[(size_t) n] = s / (float) numChannels * window[(size_t) n];
            }
            std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (fftData.data());   // magnitudes

            for (int y = 0; y < maskHeight; ++y)
            {
                const float freq = logPosToFreq ((float) y / (float) (maskHeight - 1));
                const float bin  = freq * binsPerHz;
                float mag = 0.0f;
                if (bin < (float) (fftSize / 2))
                {
                    const int b0 = (int) bin;
                    const float t = bin - (float) b0;
                    mag = fftData[(size_t) b0] + t * (fftData[(size_t) (b0 + 1)] - fftData[(size_t) b0]);
                }
                const float db = juce::Decibels::gainToDecibels (mag * normaliser, floorDb);
                const float v  = juce::jmap (db, floorDb, 0.0f, 0.0f, 1.0f);
                pixels.setPixelColour (x, maskHeight - 1 - y, heat (v));   // low freqs at the bottom
            }
        }
        return img;
    }
}
