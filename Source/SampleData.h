#pragma once
//
// SampleData.h
// A loaded (and possibly spectrally edited) sample, its waveform overview,
// and the "bank" through which the audio thread picks up new versions
// without ever waiting on a lock.
//
#include <juce_audio_basics/juce_audio_basics.h>
#include "Parameters.h"
#include <memory>

namespace refract
{
    struct SampleData
    {
        juce::AudioBuffer<float> audio;      // 1 or 2 channels
        juce::AudioBuffer<float> reversed;   // the same audio backwards, for Reverse mode
        double sampleRate = 44100.0;         // rate the file was recorded at
    };

    // shared_ptr to *const* data: once published, a sample is never modified,
    // only replaced. That is what makes sharing it between threads safe.
    using SampleDataPtr = std::shared_ptr<const SampleData>;

    // Builds a SampleData, including its reversed copy. Background threads only.
    inline std::shared_ptr<SampleData> makeSampleData (juce::AudioBuffer<float>&& audio, double sampleRate)
    {
        auto d = std::make_shared<SampleData>();
        d->audio = std::move (audio);
        d->sampleRate = sampleRate;
        d->reversed.makeCopyOf (d->audio);
        d->reversed.reverse (0, d->reversed.getNumSamples());
        return d;
    }

    // Min/max of every block of 64 samples - lets the UI draw a zoomed-out
    // waveform without reading millions of samples every frame.
    struct WaveformPeaks
    {
        static constexpr int bucketSize = 64;
        std::vector<float> lo, hi;
        int numSamples = 0;
    };
    using WaveformPeaksPtr = std::shared_ptr<const WaveformPeaks>;

    inline WaveformPeaksPtr makePeaks (const juce::AudioBuffer<float>& audio)
    {
        auto p = std::make_shared<WaveformPeaks>();
        p->numSamples = audio.getNumSamples();
        const int buckets = (p->numSamples + WaveformPeaks::bucketSize - 1) / WaveformPeaks::bucketSize;
        p->lo.assign ((size_t) buckets, 0.0f);
        p->hi.assign ((size_t) buckets, 0.0f);
        for (int b = 0; b < buckets; ++b)
        {
            const int s0 = b * WaveformPeaks::bucketSize;
            const int s1 = juce::jmin (p->numSamples, s0 + WaveformPeaks::bucketSize);
            float lo = 0.0f, hi = 0.0f;
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            {
                const float* d = audio.getReadPointer (ch);
                for (int s = s0; s < s1; ++s) { lo = juce::jmin (lo, d[s]); hi = juce::jmax (hi, d[s]); }
            }
            p->lo[(size_t) b] = lo;
            p->hi[(size_t) b] = hi;
        }
        return p;
    }

    // Holds the sample each layer currently plays.
    // get() is called by voices on the audio thread when a note starts;
    // exchange() is called by the background renderer when an edit is ready.
    class PlaybackBank
    {
    public:
        SampleDataPtr get (int layer) const
        {
            return std::atomic_load (&slots[(size_t) layer]);
        }

        SampleDataPtr exchange (int layer, SampleDataPtr newData)
        {
            return std::atomic_exchange (&slots[(size_t) layer], std::move (newData));
        }

    private:
        std::array<SampleDataPtr, numLayers> slots;
    };

    // Where each voice is in each layer's sample, as 0..1 of the sample length
    // (-1 = not playing). Written by the audio thread, read by the UI to draw
    // the moving playheads.
    using PlayheadArray = std::array<std::array<std::atomic<float>, numVoices>, numLayers>;
}
