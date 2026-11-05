#pragma once
//
// SampleData.h
// A loaded (and possibly spectrally edited) sample, and the "bank" through
// which the audio thread picks up new versions without ever waiting on a lock.
//
#include <juce_audio_basics/juce_audio_basics.h>
#include "Parameters.h"
#include <memory>

namespace refract
{
    struct SampleData
    {
        juce::AudioBuffer<float> audio;   // 1 or 2 channels
        double sampleRate = 44100.0;      // rate the file was recorded at
    };

    // shared_ptr to *const* data: once published, a sample is never modified,
    // only replaced. That is what makes sharing it between threads safe.
    using SampleDataPtr = std::shared_ptr<const SampleData>;

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
}
