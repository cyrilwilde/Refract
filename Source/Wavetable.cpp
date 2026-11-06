#include "Wavetable.h"
#include <juce_dsp/juce_dsp.h>

namespace refract
{
    WavetablePtr buildWavetable (const std::vector<std::vector<float>>& frames)
    {
        constexpr int order = 11;   // 2^11 = 2048 = wtSize
        static_assert ((1 << order) == wtSize, "FFT size must match the table size");

        auto table = std::make_shared<Wavetable>();
        table->data.assign ((size_t) (wtFrames * wtLevels * (wtSize + 1)), 0.0f);

        juce::dsp::FFT fft (order);
        std::vector<float> spectrum ((size_t) wtSize * 2), work ((size_t) wtSize * 2);

        // Measure how this FFT scales a forward+inverse round trip
        std::fill (work.begin(), work.end(), 0.0f);
        work[0] = 1.0f;
        fft.performRealOnlyForwardTransform (work.data());
        fft.performRealOnlyInverseTransform (work.data());
        const float scale = std::abs (work[0]) > 1.0e-9f ? 1.0f / work[0] : 1.0f;

        for (int f = 0; f < wtFrames; ++f)
        {
            const auto& src = frames[(size_t) juce::jmin (f, (int) frames.size() - 1)];
            std::fill (spectrum.begin(), spectrum.end(), 0.0f);
            for (int i = 0; i < wtSize; ++i)
                spectrum[(size_t) i] = i < (int) src.size() ? src[(size_t) i] : 0.0f;
            fft.performRealOnlyForwardTransform (spectrum.data());
            spectrum[0] = spectrum[1] = 0.0f;   // remove any DC offset

            float peak = 0.0f;
            for (int level = 0; level < wtLevels; ++level)
            {
                // Keep harmonics 1..maxHarmonic, silence the rest (and their mirror images)
                const int maxHarmonic = 1024 >> level;
                work = spectrum;
                for (int k = maxHarmonic + 1; k <= wtSize / 2; ++k)
                {
                    work[(size_t) (2 * k)] = work[(size_t) (2 * k + 1)] = 0.0f;
                    if (k < wtSize / 2)
                    {
                        const int m = wtSize - k;
                        work[(size_t) (2 * m)] = work[(size_t) (2 * m + 1)] = 0.0f;
                    }
                }
                fft.performRealOnlyInverseTransform (work.data());

                float* dst = table->data.data() + (size_t) (f * wtLevels + level) * (size_t) (wtSize + 1);
                for (int i = 0; i < wtSize; ++i)
                    dst[i] = work[(size_t) i] * scale;
                dst[wtSize] = dst[0];

                if (level == 0)
                    for (int i = 0; i < wtSize; ++i)
                        peak = juce::jmax (peak, std::abs (dst[i]));
            }

            // Normalise every frame to the same peak so morphing doesn't jump in volume
            if (peak > 1.0e-6f)
                for (int level = 0; level < wtLevels; ++level)
                {
                    float* dst = table->data.data() + (size_t) (f * wtLevels + level) * (size_t) (wtSize + 1);
                    for (int i = 0; i <= wtSize; ++i)
                        dst[i] /= peak;
                }
        }
        return table;
    }

    WavetablePtr makeBuiltInTable (int index)
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        std::vector<std::vector<float>> frames ((size_t) wtFrames, std::vector<float> ((size_t) wtSize));

        for (int f = 0; f < wtFrames; ++f)
        {
            const float m = (float) f / (float) (wtFrames - 1);   // 0..1 across the table
            auto& out = frames[(size_t) f];

            for (int i = 0; i < wtSize; ++i)
            {
                const float p = (float) i / (float) wtSize;   // 0..1 through one cycle
                float v = 0.0f;
                switch (index)
                {
                    case 0:   // Basic Shapes: sine -> triangle -> saw -> square
                    {
                        const float sine = std::sin (twoPi * p);
                        const float tri  = 1.0f - 4.0f * std::abs (p - 0.5f);
                        const float saw  = 2.0f * p - 1.0f;
                        const float sq   = p < 0.5f ? 1.0f : -1.0f;
                        const float w = m * 3.0f;
                        if (w < 1.0f)      v = sine + (tri - sine) * w;
                        else if (w < 2.0f) v = tri + (saw - tri) * (w - 1.0f);
                        else               v = saw + (sq - saw) * (w - 2.0f);
                        break;
                    }
                    case 1:   // PWM: pulse width narrowing from 50% to 2%
                    {
                        const float width = 0.5f - m * 0.48f;
                        v = p < width ? 1.0f : -1.0f;
                        break;
                    }
                    case 2:   // Harmonic Sweep: more and more harmonics added in
                    {
                        const int count = 1 + (int) (m * 60.0f);
                        for (int h = 1; h <= count; ++h)
                            v += std::sin (twoPi * p * (float) h) / (float) h;
                        break;
                    }
                    case 3:   // Formant: a vowel-like peak sliding up the harmonics
                    {
                        const float centre = 2.0f + m * 24.0f;
                        for (int h = 1; h <= 64; ++h)
                        {
                            const float d = ((float) h - centre) / 3.0f;
                            v += (std::exp (-d * d) + 0.15f / (float) h) * std::sin (twoPi * p * (float) h);
                        }
                        break;
                    }
                    default:  // Digital: a sine crushed into fewer and fewer steps
                    {
                        const float steps = std::pow (2.0f, 6.0f - m * 5.0f);   // 64 down to 2
                        const float s = std::sin (twoPi * p);
                        v = std::round (s * steps) / steps;
                        break;
                    }
                }
                out[(size_t) i] = v;
            }
        }
        return buildWavetable (frames);
    }

    WavetablePtr makeTableFromSample (const SampleData& sample, double period)
    {
        const auto& audio = sample.audio;
        const int len = audio.getNumSamples();
        if (len < 64)
            return nullptr;
        if (period < 16.0 || period > (double) len / 2.0)
            period = juce::jmin ((double) wtSize, (double) len / 2.0);   // unknown pitch: fixed-size slices

        std::vector<std::vector<float>> frames ((size_t) wtFrames, std::vector<float> ((size_t) wtSize));
        const int channels = audio.getNumChannels();

        for (int f = 0; f < wtFrames; ++f)
        {
            // Frames are spread evenly through the sample
            const double start = (double) f / (double) (wtFrames - 1) * ((double) len - period - 2.0);
            for (int i = 0; i < wtSize; ++i)
            {
                const double pos = start + (double) i / (double) wtSize * period;
                const int i0 = juce::jlimit (0, len - 2, (int) pos);
                const float t = (float) (pos - (double) i0);
                float v = 0.0f;
                for (int ch = 0; ch < channels; ++ch)
                    v += audio.getSample (ch, i0) + t * (audio.getSample (ch, i0 + 1) - audio.getSample (ch, i0));
                frames[(size_t) f][(size_t) i] = v / (float) channels;
            }
        }
        return buildWavetable (frames);
    }
}
