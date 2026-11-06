#pragma once
//
// SpectralProcessor.h
// Offline (non-real-time) spectral tools:
//   - renderSpectrogram(): turns a sample into a picture you can paint on
//   - applyMask():         rebuilds the sample with painted frequencies removed
//
// Both run on a background thread, never on the audio thread. That is the key
// design decision: the expensive FFT work happens once per edit, and playback
// is just reading a pre-rendered buffer - cheap, glitch-free and stable.
//
#include <juce_dsp/juce_dsp.h>
#include <juce_graphics/juce_graphics.h>
#include <functional>
#include <vector>
#include "Parameters.h"

namespace refract::spectral
{
    constexpr int fftOrder = 11;              // 2^11 = 2048-point FFT
    constexpr int fftSize  = 1 << fftOrder;
    constexpr int hopSize  = fftSize / 4;     // 75% overlap between frames

    using Mask = std::vector<uint8_t>;        // maskWidth * maskHeight: 0 = removed, maskUnity = untouched, 255 = +12 dB

    inline Mask makeEmptyMask() { return Mask ((size_t) (maskWidth * maskHeight), (uint8_t) maskUnity); }
    bool isMaskEmpty (const Mask& mask);      // true if nothing has been painted

    // Returns the edited audio, or an empty buffer if shouldAbort() returned true.
    juce::AudioBuffer<float> applyMask (const juce::AudioBuffer<float>& input, double sampleRate,
                                        const Mask& mask, const std::function<bool()>& shouldAbort);

    // A maskWidth x maskHeight colour image, low frequencies at the bottom.
    juce::Image renderSpectrogram (const juce::AudioBuffer<float>& input, double sampleRate);
}
