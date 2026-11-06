#pragma once
//
// Wavetable.h
// A wavetable is a stack of single-cycle waveforms ("frames"). The Position
// knob sweeps through the stack, morphing from one frame to the next.
//
// To stop high notes sounding harsh (aliasing), every frame is stored at
// several "mip levels": copies with fewer and fewer harmonics. A high note
// reads a level with only the harmonics that fit below half the sample rate.
//
#include <juce_audio_basics/juce_audio_basics.h>
#include "SampleData.h"
#include <memory>
#include <vector>

namespace refract
{
    constexpr int wtSize   = 2048;   // samples per single cycle
    constexpr int wtFrames = 32;     // frames per table
    constexpr int wtLevels = 10;     // level 0 = all 1024 harmonics, level L = 1024 / 2^L harmonics

    constexpr int numBuiltInTables = 5;
    constexpr int numWavetables    = numBuiltInTables + numLayers;   // + one made from each layer's sample

    inline juce::StringArray wavetableNames()
    {
        return { "Basic Shapes", "PWM", "Harmonic Sweep", "Formant", "Digital",
                 "From Layer 1", "From Layer 2", "From Layer 3" };
    }

    struct Wavetable
    {
        // [frame][level][wtSize + 1]; the extra sample repeats the first so
        // interpolation never has to wrap around.
        std::vector<float> data;

        const float* get (int frame, int level) const
        {
            return data.data() + (size_t) (frame * wtLevels + level) * (size_t) (wtSize + 1);
        }

        // Which level to use for a given phase increment (cycles per sample)
        static int levelFor (double increment)
        {
            // Harmonics that fit below Nyquist: 0.5 / increment
            const double maxHarmonics = 0.5 / juce::jmax (1.0e-9, increment);
            int level = 0;
            while (level < wtLevels - 1 && (double) (1024 >> level) > maxHarmonics)
                ++level;
            return level;
        }
    };
    using WavetablePtr = std::shared_ptr<const Wavetable>;

    // Builds a table (with all mip levels) from wtFrames single cycles of wtSize samples
    WavetablePtr buildWavetable (const std::vector<std::vector<float>>& frames);

    WavetablePtr makeBuiltInTable (int index);

    // Slices a sample into wtFrames cycles. If the sample's pitch is known,
    // each frame is exactly one cycle of it (cleanest result).
    WavetablePtr makeTableFromSample (const SampleData& sample, double periodInSamples);

    // Same lock-free swap idea as PlaybackBank, for wavetables
    class WavetableBank
    {
    public:
        WavetablePtr get (int index) const   { return std::atomic_load (&slots[(size_t) index]); }
        WavetablePtr exchange (int index, WavetablePtr t) { return std::atomic_exchange (&slots[(size_t) index], std::move (t)); }
    private:
        std::array<WavetablePtr, numWavetables> slots;
    };
}
